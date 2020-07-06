#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#ifdef _WIN32
	#include <windows.h>
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <shellapi.h>
#else
	#include <sys/socket.h>
	#include <stdlib.h>
	#include <netinet/in.h>
	#include <string.h>
	#include <stdio.h>
	#include <fcntl.h>
#endif
#include "filter.h"
#include "datastore.h"
#include "allocate.h"
#include "interface.h"
#include "c_jobsched_server.h"
#include "distribute.h"

#define D_Q_DEQUEUE_SLEEP_S             1
#define D_Q_DEQUEUE_MAX_ATTEMPT_RETRIES 3
#define D_Q_HEARTBEAT                   NULL
#define D_Q_HEARTBEAT_MS                1000
#define D_Q_HEARTBEAT_MAX_MISSES        2
#define D_Q_SOCKET_BACKLOG_LIMIT        5
#define D_Q_CLIENT_SOCKET_SLEEP_MS      10

#define R_Q_SLEEP_MS                    10
#define R_Q_RESULT_ATTEMPT_RETRY_MS     1
#define R_Q_RESULT_MAX_ATTEMPT_RETRIES  3
#define R_Q_NULL                        NULL

#define Q_SIGINT_GRACE_PERIOD_SECONDS   10
#define Q_NOT_ACTIVE                    -1
#define Q_ACTIVE                        0

// sleep duration between consecutive job dispatch retries
#define DISPATCH_RETRY_MS            	20
// total dispatch retries for one user job (window) is 30mins
#define DISPATCH_MAX_ATTEMPTS           (ushort)((1000/DISPATCH_RETRY_MS)*60*30)

#ifndef _WIN32
	// qsub command line template
	// resource usage:      1 worker node's core
	// error/output files:  redirect to /dev/null
	#define DISTRIBUTE_SERVER_QSUB_COMMAND "qsub -l nodes=1:ppn=1 -e /dev/null -o /dev/null"   
	
	// exit code for successful invocation of qsub
	#define QSUB_SUCCESS 0
#endif

// queue for job distribution
static cp_job d_q[MAX_Q_SIZE];
static nt_q_size d_q_num_items = -1;
static nt_q_size d_q_head_posn = 0, d_q_tail_posn = 0;
static bool d_q_forward = true, d_q_shutting_down = false;
static pthread_spinlock_t d_q_spinlock;

// queue for result (hit) retrieval
static rp_hit r_q[MAX_Q_SIZE];
static nt_q_size r_q_num_items = -1;
static nt_q_size r_q_head_posn = 0, r_q_tail_posn = 0;
static bool r_q_forward = true, r_q_shutting_down = false;
static pthread_spinlock_t r_q_spinlock;

// spinlock for distribution/allocation synchronization (across dispatch_job in distribute/update_thread_start in allocate)
static pthread_spinlock_t dis_spinlock;

// threads for distribution queue
static pthread_t d_q_socket_threads[D_Q_NUM_SOCKET_THREADS];

#ifdef _WIN32
	static SOCKET win_listen_socket = 0;
#else
	static int unix_listen_socket_fd = 0;
	static struct sockaddr_in unix_address;
	static socklen_t unix_address_len = 0;
#endif

#define MAX_Q_SIZE_LESS_ONE MAX_Q_SIZE-1

_Static_assert (MAX_Q_SIZE > 1, "MAX_Q_SIZE must be greater than 1");

#define D_Q_LOCK_S    if (pthread_spin_lock (&d_q_spinlock)) { DEBUG_NOW (REPORT_ERRORS, DISPATCH, "could not acquire dispatch queue spinlock"); } else {
#define D_Q_LOCK_E    if (pthread_spin_unlock (&d_q_spinlock)) { DEBUG_NOW (REPORT_ERRORS, DISPATCH, "could not release dispatch queue spinlock"); pthread_exit (NULL); } }
#define R_Q_LOCK_S    if (pthread_spin_lock (&r_q_spinlock)) { DEBUG_NOW (REPORT_ERRORS, DISPATCH, "could not acquire result queue spinlock"); } else {
#define R_Q_LOCK_E    if (pthread_spin_unlock (&r_q_spinlock)) { DEBUG_NOW (REPORT_ERRORS, DISPATCH, "could not release dispatch queue spinlock"); pthread_exit (NULL); } }
void dis_lock() {
	pthread_spin_lock (&dis_spinlock);
}
void dis_unlock() {
	pthread_spin_unlock (&dis_spinlock);
}
/*
 * static, inline replacements for memset/memcpy - silences google sanitizers
 */
static inline void g_memset (void *p, const char v, const int len) {
	REGISTER
	char *pc = (char *)p;
	
	for (REGISTER int i = 0; i < len; i++) {
		pc[i] = v;
	}
}
static inline void g_memcpy (void *p, const void *r, const int len) {
	REGISTER
	char *pc = (char *)p, *rc = (char *)r;
	
	for (REGISTER int i = 0; i < len; i++) {
		pc[i] = rc[i];
	}
}
static bool initialize_sockets (ushort port) {
	#ifdef _WIN32
	// credit: https://docs.microsoft.com/en-us/windows/desktop/winsock/complete-server-code
	WSADATA wsaData;
	
	if (WSAStartup (MAKEWORD (2, 2), &wsaData)) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "could not startup WSA");
		return false;
	}
	
	struct addrinfo hints;
	
	ZeroMemory (&hints, sizeof (hints));
	
	hints.ai_family = AF_INET;
	
	hints.ai_socktype = SOCK_STREAM;
	
	hints.ai_protocol = IPPROTO_TCP;
	
	hints.ai_flags = AI_PASSIVE;
	
	win_listen_socket = socket (AF_INET, SOCK_STREAM, 0);
	
	if (win_listen_socket == INVALID_SOCKET) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "could not initialize sockets");
		WSACleanup();
		return false;
	}
	
	ulong mode = 1; // non-blocking
	
	if (ioctlsocket (win_listen_socket, FIONBIO, &mode) != NO_ERROR) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH,
		           "could not control I/O mode when initializing sockets");
		closesocket (win_listen_socket);
		WSACleanup();
		return false;
	}
	
	struct sockaddr_in address;
	
	address.sin_family = AF_INET;
	
	address.sin_addr.s_addr = INADDR_ANY;
	
	address.sin_port = htons (port);
	
	if (bind (win_listen_socket, (struct sockaddr *)&address,
	          sizeof (address)) == SOCKET_ERROR) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "could not bind address to socket");
		closesocket (win_listen_socket);
		WSACleanup();
		return false;
	}
	
	if (listen (win_listen_socket, SOMAXCONN) == SOCKET_ERROR) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "unable to listen for connections");
		closesocket (win_listen_socket);
		WSACleanup();
		return false;
	}
	
	return true;
	#else
	// credit: https://www.geeksforgeeks.org/socket-programming-cc/
	
	if ((unix_listen_socket_fd = socket (AF_INET, SOCK_STREAM, 0)) == 0) {
		return false;
	}
	
	#if Q_DISABLE_NAGLE
	int flag = 1;
	
	if (setsockopt (sock, IPPROTO_TCP, TCP_NODELAY, (char *)&flag,
	                sizeof (int)) < 0) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH,
		           "could not disable Nagle's algorithm when intializing sockets");
		close (unix_listen_socket_fd);
		return false;
	}
	
	#endif
	int flags = fcntl (unix_listen_socket_fd, F_GETFL);
	
	if (flags < 0) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH,
		           "could not get flags when intializing sockets");
		close (unix_listen_socket_fd);
		return false;
	}
	
	if (fcntl (unix_listen_socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH,
		           "could not set flags when intializing sockets");
		close (unix_listen_socket_fd);
		return false;
	}
	
	int opt = 1;
	
	if (setsockopt (unix_listen_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
	                sizeof (opt))) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH,
		           "could not set flags when intializing sockets");
		close (unix_listen_socket_fd);
		return false;
	}
	
	unix_address.sin_family = AF_INET;
	unix_address.sin_addr.s_addr = htonl (INADDR_ANY);
	unix_address.sin_port = htons (port);
	unix_address_len = sizeof (unix_address);
	
	if (bind (unix_listen_socket_fd, (struct sockaddr *)&unix_address,
	          unix_address_len) < 0) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH,
		           "could not bind address to socket when intializing sockets");
		close (unix_listen_socket_fd);
		return false;
	}
	
	if (listen (unix_listen_socket_fd, D_Q_SOCKET_BACKLOG_LIMIT) < 0) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "unable to listen to connections");
		close (unix_listen_socket_fd);
		return false;
	}
	
	return true;
	#endif
}
static void finalize_sockets() {
	#ifdef _WIN32
	closesocket (win_listen_socket);
	WSACleanup();
	#else
	close (unix_listen_socket_fd);
	#endif
}
static bool initialize_qs() {
	if (d_q_num_items >= Q_ACTIVE || r_q_num_items >= Q_ACTIVE) {
		return false;
	}
	
	else {
		d_q_num_items = Q_ACTIVE;
		r_q_num_items = Q_ACTIVE;
		return true;
	}
}
static bool enq_d (cp_job j) {
	if (d_q_num_items >= Q_ACTIVE && d_q_num_items < MAX_Q_SIZE) {
		if (d_q_forward) {
			if (d_q_tail_posn < MAX_Q_SIZE) {
				d_q[d_q_tail_posn++] = j;
			}
			
			else {
				d_q_tail_posn = 1;
				d_q[0] = j;
				d_q_forward = false;
			}
		}
		
		else {
			d_q[d_q_tail_posn++] = j;
		}
		
		d_q_num_items++;
		return true;
	}
	
	else {
		return false;
	}
}
bool enq_r (rp_hit r) {
	static bool ret_val;
	R_Q_LOCK_S
	
	if (r_q_num_items >= Q_ACTIVE && r_q_num_items < MAX_Q_SIZE) {
		if (r_q_forward) {
			if (r_q_tail_posn < MAX_Q_SIZE) {
				r_q[r_q_tail_posn++] = r;
			}
			
			else {
				r_q_tail_posn = 1;
				r_q[0] = r;
				r_q_forward = false;
			}
		}
		
		else {
			r_q[r_q_tail_posn++] = r;
		}
		
		r_q_num_items++;
		ret_val = true;
	}
	
	else {
		ret_val = false;
	}
	
	R_Q_LOCK_E
	return ret_val;
}
static cp_job deq_d() {
	if (d_q_num_items > Q_ACTIVE) {
		d_q_num_items--;
		
		if (d_q_forward || d_q_head_posn < MAX_Q_SIZE_LESS_ONE) {
			return d_q[d_q_head_posn++];
		}
		
		else {
			d_q_head_posn = 0;
			d_q_forward = true;
			return d_q[MAX_Q_SIZE_LESS_ONE];
		}
	}
	
	else {
		return NULL;
	}
}
static rp_hit deq_r() {
	static rp_hit ret_val;
	R_Q_LOCK_S
	
	if (r_q_num_items > Q_ACTIVE) {
		r_q_num_items--;
		
		if (r_q_forward || r_q_head_posn < MAX_Q_SIZE_LESS_ONE) {
			ret_val = r_q[r_q_head_posn++];
		}
		
		else {
			r_q_head_posn = 0;
			r_q_forward = true;
			ret_val = r_q[MAX_Q_SIZE_LESS_ONE];
		}
	}
	
	else {
		ret_val = NULL;
	}
	
	R_Q_LOCK_E
	return ret_val;
}
static nt_q_size count_d_q() {
	return d_q_num_items;
}
static nt_q_size count_r_q() {
	static nt_q_size ret_val;
	R_Q_LOCK_S
	ret_val = r_q_num_items;
	R_Q_LOCK_E
	return ret_val;
}
static void finalize_qs() {
	d_q_num_items = Q_NOT_ACTIVE;
	d_q_forward = true;
	d_q_head_posn = 0;
	d_q_tail_posn = 0;
	r_q_num_items = Q_NOT_ACTIVE;
	r_q_forward = true;
	r_q_head_posn = 0;
	r_q_tail_posn = 0;
}
static inline void d_q_process_msg (const uchar *q_socket_recv_buf,
                                    const ushort thread_id) {
	cp_job new_job = malloc (sizeof (c_job));
	
	if (new_job == NULL) {
		DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
		            "could not allocate job to enqueue in thread #%d", thread_id);
		return;
	}
	
	/*
	 * unpacking data order from (32) bytes : [NUM_RT_BYTES x job_id][4 x start_posn][4 x end_posn]
	 */
	new_job->start_posn = 0;
	new_job->end_posn = 0;
	
	for (uchar j = 0; j < 4; j++) {
		new_job->start_posn += (q_socket_recv_buf[NUM_RT_BYTES + 0 + j] << ((
		                                        3 - j) * 8));
		new_job->end_posn  += (q_socket_recv_buf[NUM_RT_BYTES + 4 + j] << ((
		                                        3 - j) * 8));
	}
	
	for (uchar j = 0; j < NUM_RT_BYTES; j++) {
		new_job->job_id[j] = q_socket_recv_buf[j];
	}
	
	D_Q_LOCK_S
	
	if (!enq_d (new_job)) {
		DEBUG_NOW1 (REPORT_ERRORS, DISPATCH, "could not enqueue job in thread #%d",
		            thread_id);
		free (new_job);
	}
	
	D_Q_LOCK_E
}
static void *socket_client_thread_start (void *arg) {
	ushort this_thread_num = * (ushort *)arg;
	uchar d_q_socket_recv_buf[FILTER_MSG_SIZE],
	      q_socket_pending_buf[FILTER_MSG_SIZE];
	int num_bytes_read;
	REGISTER bool received_shutdown_signal = false;
	#ifdef _WIN32
	REGISTER SOCKET win_client_socket;
	#else
	REGISTER int unix_client_socket;
	fd_set select_read_fds;
	struct timeval select_tv;
	select_tv.tv_sec = 0;
	select_tv.tv_usec =
	                    0;              // select should return immediately. we use sleep_ms (D_Q_CLIENT_SOCKET_SLEEP_MS) in while loop below
	int select_retval;
	#endif
	
	while (1) {
		/*
		 * wait for incoming connection
		 */
		while (1) {
			#ifdef _WIN32
		
			if ((win_client_socket = accept (win_listen_socket, NULL,
			                                 NULL)) == INVALID_SOCKET) {
				if (WSAGetLastError() != WSAEWOULDBLOCK) {
					DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
					            "socket error when accepting connection in thread #%u", this_thread_num);
					pthread_exit (NULL);
				}
			}
			
			else {
				break;  // connected
			}
			
			#else
			
			if ((unix_client_socket = accept (unix_listen_socket_fd, NULL, NULL)) < 0) {
				if (errno != EWOULDBLOCK) {
					DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
					            "socket error when accepting connection in thread #%u", this_thread_num);
					pthread_exit (NULL);
				}
			}
			
			else {
				break;  // connected
			}
			
			#endif
			D_Q_LOCK_S
			received_shutdown_signal = d_q_shutting_down;
			D_Q_LOCK_E
			
			if (received_shutdown_signal) {
				#ifdef _WIN32
				shutdown (win_client_socket, SD_BOTH);
				#else
				shutdown (unix_client_socket, SHUT_RDWR);
				#endif
				pthread_exit (NULL);
			}
			
			sleep_ms (D_Q_CLIENT_SOCKET_SLEEP_MS);
		}
		
		/*
		 * process incoming connection
		 */
		REGISTER bool have_part_message = false;
		REGISTER int previous_size = 0, j;
		
		while (1) {
			REGISTER bool nonblocking_retry;
			#ifdef _WIN32
			
			if ((num_bytes_read = recv (win_client_socket, d_q_socket_recv_buf,
			                            FILTER_MSG_SIZE, 0)) <= 0) {
				if (WSAGetLastError() != WSAEWOULDBLOCK) {
					shutdown (win_client_socket, SD_BOTH);
					break;  // client disconnected - restart
				}
				
				else {
					nonblocking_retry = true;
				}
			}
			
			else {
				nonblocking_retry = false;
			}
			
			#else
			FD_ZERO (&select_read_fds);
			FD_SET (unix_client_socket, &select_read_fds);
			select_retval = select (unix_client_socket + 1, &select_read_fds, NULL, NULL,
			                        &select_tv);
			
			if (select_retval < 0) {
				DEBUG_NOW1 (REPORT_ERRORS, DISPATCH, "error with select() in thread #%u",
				            this_thread_num);
				shutdown (unix_client_socket, SHUT_RDWR);
				pthread_exit (NULL);
				return NULL;
			}
			
			else
				if (select_retval) {
					if ((num_bytes_read = read (unix_client_socket, d_q_socket_recv_buf,
					                            FILTER_MSG_SIZE)) <= 0) {
						shutdown (unix_client_socket, SHUT_RDWR);
						break;  // client disconnected - restart
					}
			
					nonblocking_retry = false;
				}
			
				else {
					nonblocking_retry = true;   // select() returned 0 fds -> retry
				}
			
			#endif
			
			if (nonblocking_retry) {
				D_Q_LOCK_S
				received_shutdown_signal = d_q_shutting_down;
				D_Q_LOCK_E
				
				if (received_shutdown_signal) {
					#ifdef _WIN32
					shutdown (win_client_socket, SD_BOTH);
					#else
					shutdown (unix_client_socket, SHUT_RDWR);
					#endif
					pthread_exit (NULL);
					return NULL;
				}
				
				sleep_ms (D_Q_CLIENT_SOCKET_SLEEP_MS);
			}
			
			else {
				if (have_part_message) {
					if (previous_size + num_bytes_read <= FILTER_MSG_SIZE) {
						for (j = previous_size; j < previous_size + num_bytes_read; j++) {
							q_socket_pending_buf[j] = d_q_socket_recv_buf[j - previous_size];
						}
						
						if (previous_size + num_bytes_read == FILTER_MSG_SIZE) {
							d_q_process_msg (q_socket_pending_buf, this_thread_num);
							have_part_message = false;
						}
						
						else {
							previous_size += num_bytes_read;
						}
					}
					
					else {
						for (j = previous_size; j < FILTER_MSG_SIZE; j++) {
							q_socket_pending_buf[j] = d_q_socket_recv_buf[j - previous_size];
						}
						
						d_q_process_msg (q_socket_pending_buf, this_thread_num);
						
						for (j = FILTER_MSG_SIZE - previous_size; j < num_bytes_read; j++) {
							q_socket_pending_buf[j - (FILTER_MSG_SIZE - previous_size)] =
							                    d_q_socket_recv_buf[j];
						}
						
						previous_size = num_bytes_read - (FILTER_MSG_SIZE - previous_size);
					}
					
					continue;
				}
				
				if (num_bytes_read == FILTER_MSG_SIZE) {
					d_q_process_msg (d_q_socket_recv_buf, this_thread_num);
				}
				
				else {
					for (j = 0; j < num_bytes_read; j++) {
						q_socket_pending_buf[j] = d_q_socket_recv_buf[j];
					}
					
					previous_size = num_bytes_read;
					have_part_message = true;
				}
			}
		}
	}
}
static void *d_enq_thread_start (void *arg) {
	REGISTER
	bool check = false, received_shutdown_signal = false;
	
	while (1) {
		sleep_ms (D_Q_HEARTBEAT_MS);
		D_Q_LOCK_S
		check = enq_d (D_Q_HEARTBEAT);
		received_shutdown_signal = d_q_shutting_down;
		D_Q_LOCK_E
		
		if (received_shutdown_signal) {
			break;
		}
		
		if (!check) {
			DEBUG_NOW (REPORT_ERRORS, DISPATCH,
			           "could not enqueue into distribution queue");
		}
	}
	
	// clean exit - wait for all queue items to have been dequeued by deq_thread
	//            - up until D_Q_DEQUEUE_MAX_ATTEMPT_RETRIES attempts
	REGISTER
	ushort q_dequeue_attempt_retries = 0;
	
	do {
		D_Q_LOCK_S
		check = !count_d_q();
		D_Q_LOCK_E
		
		if (check) {
			break;
		}
		
		else {
			q_dequeue_attempt_retries++;
			
			if (q_dequeue_attempt_retries < D_Q_DEQUEUE_MAX_ATTEMPT_RETRIES) {
				sleep (D_Q_DEQUEUE_SLEEP_S);
			}
			
			else {
				break;
			}
		}
	}
	while (1);
	
	if (check) {
		DEBUG_NOW (REPORT_INFO, DISPATCH,
		           "distribution queue is empty. enqueue thread exiting...");
		sleep_ms (D_Q_HEARTBEAT_MS * (D_Q_HEARTBEAT_MAX_MISSES +
		                              1)); // starve deq_thread from heartbeat
	}
	
	else {
		DEBUG_NOW (REPORT_INFO, DISPATCH,
		           "distribution queue timed out while still holding items. enqueue thread exiting...");
	}
	
	return NULL;
}
#if JS_JOBSCHED_TYPE!=JS_NONE
static inline bool dispatch_job (cp_job job) {
	/*
	 * retrieve job details from datastore
	 */
	int job_current_status = DS_JOB_STATUS_UNDEFINED,
	    job_current_error = DS_JOB_ERROR_UNDEFINED,
	    job_current_num_windows = 0, job_current_num_windows_success = 0,
	    job_current_num_windows_fail = 0;
	static dsp_dataset job_dataset = NULL;
	dis_lock();  // avoid conflicts with allocate (update_thread_start)
	
	if (read_job (&job->job_id, &job_dataset) &&
	    job_dataset && job_dataset->num_records &&
	    job_dataset->num_fields_per_record == DS_COLLECTION_JOBS_NFIELDS &&
	    job_dataset->data[DS_COL_JOB_SEQUENCE_ID_IDX] &&
	    strlen (job_dataset->data[DS_COL_JOB_SEQUENCE_ID_IDX]) &&
	    job_dataset->data[DS_COL_JOB_CSSD_ID_IDX] &&
	    strlen (job_dataset->data[DS_COL_JOB_CSSD_ID_IDX]) &&
	    job_dataset->data[DS_COL_JOB_REF_ID_IDX] &&
	    strlen (job_dataset->data[DS_COL_JOB_REF_ID_IDX]) &&
	    job_dataset->data[DS_COL_JOB_NUM_WINDOWS_IDX] &&
	    strlen (job_dataset->data[DS_COL_JOB_NUM_WINDOWS_IDX]) &&
	    job_dataset->data[DS_COL_JOB_NUM_WINDOWS_SUCCESS_IDX] &&
	    strlen (job_dataset->data[DS_COL_JOB_NUM_WINDOWS_SUCCESS_IDX]) &&
	    job_dataset->data[DS_COL_JOB_NUM_WINDOWS_FAIL_IDX] &&
	    strlen (job_dataset->data[DS_COL_JOB_NUM_WINDOWS_FAIL_IDX])) {
		job_current_status = atoi (((char **)
		                            job_dataset->data)[DS_COL_JOB_STATUS_IDX]);
		job_current_error = atoi (((char **) job_dataset->data)[DS_COL_JOB_ERROR_IDX]);
		
		switch (job_current_status) {
			case DS_JOB_STATUS_INIT     :
			case DS_JOB_STATUS_PENDING  :
			case DS_JOB_STATUS_SUBMITTED:
				break;          // job is in an expected status; note SUBMITTED does not imply dispatched
				
			case DS_JOB_STATUS_DONE     :
				dis_unlock();
				DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
				            "job '%s' has already started or is complete. not re-submitting",
				            job->job_id);
				free_dataset (job_dataset);
				return false;
				
			default                     :
				dis_unlock();
				DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
				            "job '%s' is in an unkown state. not re-submitting",
				            job->job_id);
				free_dataset (job_dataset);
				return false;
		}
		
		job_current_num_windows = atoi (((char **)
		                                 job_dataset->data)[DS_COL_JOB_NUM_WINDOWS_IDX]);
		job_current_num_windows_success = atoi (((char **)
		                                        job_dataset->data)[DS_COL_JOB_NUM_WINDOWS_SUCCESS_IDX]);
		job_current_num_windows_fail = atoi (((char **)
		                                      job_dataset->data)[DS_COL_JOB_NUM_WINDOWS_FAIL_IDX]);
	}
	
	else {
		dis_unlock();
		DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
		            "document for job '%s' not found or is invalid. job submission failed",
		            job->job_id);
		            
		if (job_dataset) {
			free_dataset (job_dataset);
		}
		
		return false;
	}
	
	if (!job->start_posn && !job->end_posn) {
		/*
		 * 'null' or empty jobs are ones which are known to yield no results (as determined by filter_seq;
		 * no change in status or other updates are required; when a 'null' job is sent by filter process
		 * the job status will be handled accordingly: that is, from INIT to DONE in case this null job
		 * was the only job submitted
		 */
		dis_unlock();
		free_dataset (job_dataset);
		return true;
	}
	
	else
		if (DISPATCH_NULL_JOB_POSN == job->start_posn &&
		    DISPATCH_NULL_JOB_POSN == job->end_posn) {
			// filtering is complete and all filter_threads have submitted ROI for search;
			// update job status to DONE
			ds_int32_field ref_id = atoi (job_dataset->data[DS_COL_JOB_REF_ID_IDX]);
			free_dataset (job_dataset);
			
			if (DS_JOB_STATUS_INIT == job_current_status) {
				if (update_job_status (&job->job_id, ref_id, DS_JOB_STATUS_DONE)) {
					dis_unlock();
					return true;
				}
				
				else {
					dis_unlock();
					return false;
				}
			}
			
			else
				if (DS_JOB_STATUS_PENDING == job_current_status) {
					if (job_current_num_windows == (job_current_num_windows_success +
					                                job_current_num_windows_fail)) {
						if (update_job_status (&job->job_id, ref_id, DS_JOB_STATUS_DONE)) {
							dis_unlock();
							return true;
						}
						
						else {
							dis_unlock();
							return false;
						}
					}
					
					else {
						if (update_job_status (&job->job_id, ref_id, DS_JOB_STATUS_SUBMITTED)) {
							dis_unlock();
							return true;
						}
						
						else {
							dis_unlock();
							return false;
						}
					}
				}
				
				else
					if (DS_JOB_STATUS_SUBMITTED == job_current_status) {
						dis_unlock();
						return true;
					}
					
			dis_unlock();
			return false;
		}
		
		else {
			dis_unlock();  // unlock while we get sequence/CSSD info, which should be (typically) consistent
			dsp_dataset sequence_dataset = NULL;
			char *seq_oid_strn = ((char **) job_dataset->data)[DS_COL_JOB_SEQUENCE_ID_IDX];
			
			if (!read_sequence_by_id (((ds_object_id_field *)seq_oid_strn),
			                          &sequence_dataset) ||
			    !sequence_dataset || !sequence_dataset->num_records ||
			    sequence_dataset->num_fields_per_record != DS_COLLECTION_SEQUENCES_NFIELDS - 1
			    ||
			    !sequence_dataset->data[DS_COL_SEQUENCE_3P_UTR_IDX - 1] ||
			    !strlen (sequence_dataset->data[DS_COL_SEQUENCE_3P_UTR_IDX - 1])) {
				DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
				            "sequence document for job '%s' not found or is invalid. job submission failed",
				            job->job_id);
				free_dataset (job_dataset);
				
				if (sequence_dataset) {
					free_dataset (sequence_dataset);
				}
				
				return false;
			}
			
			dsp_dataset cssd_dataset = NULL;
			char *cssd_strn = ((char **) job_dataset->data)[DS_COL_JOB_CSSD_ID_IDX];
			
			if (!read_cssd_by_id (((ds_object_id_field *)cssd_strn), &cssd_dataset) ||
			    !cssd_dataset || !cssd_dataset->num_records ||
			    cssd_dataset->num_fields_per_record != DS_COLLECTION_CSSD_NFIELDS - 1 ||
			    !cssd_dataset->data[DS_COL_CSSD_STRING_IDX - 1] ||
			    !strlen (cssd_dataset->data[DS_COL_CSSD_STRING_IDX - 1])) {
				DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
				            "cssd document for job '%s' not found or is invalid. job submission failed",
				            job->job_id);
				free_dataset (sequence_dataset);
				free_dataset (job_dataset);
				
				if (cssd_dataset) {
					free_dataset (cssd_dataset);
				}
				
				return false;
			}
			
			size_t cssd_strn_len = strlen (((char **)
			                                cssd_dataset->data)[DS_COL_CSSD_STRING_IDX - 1]);
			char *cs_strn = malloc (cssd_strn_len + 1);
			char *pos_var_strn = malloc (cssd_strn_len + 1);
			
			if (!cs_strn || !pos_var_strn) {
				DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
				            "failed to allocate memory for cs and pos_var strings for job '%s'",
				            job->job_id);
				free_dataset (sequence_dataset);
				free_dataset (job_dataset);
				free_dataset (cssd_dataset);
				return false;
			}
			
			split_cssd (((char **) cssd_dataset->data)[0], &cs_strn, &pos_var_strn);
			/*
			 * allocate this job
			 */
			REGISTER
			bool job_submitted = false;
			ds_int32_field ref_id = atoi (job_dataset->data[DS_COL_JOB_REF_ID_IDX]);
			dis_lock();  // lock again as we update job window data
			
			if (!read_job_windows (&job->job_id, &job_current_num_windows,
			                       &job_current_num_windows_success, &job_current_num_windows_fail)) {
				dis_unlock();
				DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
				            "failed to read window status for job '%s'",
				            job->job_id);
				free_dataset (sequence_dataset);
				free_dataset (job_dataset);
				free_dataset (cssd_dataset);
				return false;
			}
			
			if (!update_job_num_windows (&job->job_id, ref_id, ++job_current_num_windows)) {
				dis_unlock();
				DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
				            "failed to update window status for job '%s'",
				            job->job_id);
				free_dataset (sequence_dataset);
				free_dataset (job_dataset);
				free_dataset (cssd_dataset);
				return false;
			}
			
			dis_unlock();   // only use lock once we successfully allocate scan job
			REGISTER
			ushort attempts = DISPATCH_MAX_ATTEMPTS;
			
			do {
				if (allocate_scan_job (job, cs_strn, pos_var_strn,
				                       sequence_dataset->data[DS_COL_SEQUENCE_3P_UTR_IDX - 1], ref_id)) {
					job_submitted = true;
					dis_lock();
					
					if (!read_job_status (&job->job_id, ref_id, &job_current_status)) {
						dis_unlock();
						DEBUG_NOW1 (REPORT_ERRORS, DISPATCH, "failed to read status for job '%s'",
						            job->job_id);
						free_dataset (sequence_dataset);
						free_dataset (job_dataset);
						free_dataset (cssd_dataset);
						return false;
					}
					
					if (DS_JOB_STATUS_INIT == job_current_status) {
						if (!update_job_status (&job->job_id, ref_id, DS_JOB_STATUS_PENDING)) {
							DEBUG_NOW1 (REPORT_ERRORS, DISPATCH, "failed to update status for job '%s'",
							            job->job_id);
							job_submitted = false;
						}
					}
					
					break;
				}
				
				if (--attempts) {
					sleep_ms (DISPATCH_RETRY_MS);
				}
				
				else {
					break;
				}
			}
			while (1);
			
			if (attempts && !job_submitted) {
				DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
				            "failed to update status fields for job '%s'",
				            job->job_id);
			}
			
			else
				if (!attempts) {
					DEBUG_NOW1 (REPORT_ERRORS, DISPATCH, "failed to dispatch job '%s'",
					            job->job_id);
					dis_lock();
					
					if (!read_job_windows (&job->job_id, &job_current_num_windows,
					                       &job_current_num_windows_success, &job_current_num_windows_fail)) {
						dis_unlock();
						DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
						            "failed to read window status for job '%s'",
						            job->job_id);
						free_dataset (sequence_dataset);
						free_dataset (job_dataset);
						free_dataset (cssd_dataset);
						return false;
					}
					
					if (DS_JOB_ERROR_FAIL != job_current_error &&
					    !update_job_error (&job->job_id, ref_id, DS_JOB_ERROR_FAIL)) {
						dis_unlock();
						DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
						            "could not update error field for job '%s'",
						            job->job_id);
						free_dataset (sequence_dataset);
						free_dataset (job_dataset);
						free_dataset (cssd_dataset);
						return false;
					}
					
					if (!update_job_num_windows_fail (&job->job_id, ref_id,
					                                  ++job_current_num_windows_fail)) {
						dis_unlock();
						DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
						            "failed to update status fields for job '%s'",
						            job->job_id);
						free_dataset (sequence_dataset);
						free_dataset (job_dataset);
						free_dataset (cssd_dataset);
						return false;
					}

					if (job_current_num_windows == (job_current_num_windows_success+job_current_num_windows_fail))
					{
						if (!update_job_status (&job->job_id, ref_id, DS_JOB_STATUS_DONE)) {
							dis_unlock();
							DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
								    "failed to update job status for job '%s'",
								    job->job_id);
							free_dataset (sequence_dataset);
							free_dataset (job_dataset);
							free_dataset (cssd_dataset);
							return false;
						}
					}
				}
				
			dis_unlock();
			free_dataset (cssd_dataset);
			free_dataset (sequence_dataset);
			free_dataset (job_dataset);
			free (cs_strn);
			free (pos_var_strn);
			return job_submitted;
		}
}
#endif
static void *d_deq_thread_start (void *arg) {
	REGISTER ushort d_q_dequeue_attempt_retries = 0;
	REGISTER
	nt_q_size q_size;
	REGISTER bool cont;
	cp_job this_job;
	#ifndef NO_FULL_CHECKS
	
	if (!scan_bin_fn || (!strlen (scan_bin_fn))) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH,
		           "NULL or empty scan binary filename supplied in dequeue thread. exiting...");
		pthread_exit (NULL);
	}
	
	lalala
	#endif
	
	while (1) {
		cont = false;
		this_job = NULL;
		q_size = 0;
		D_Q_LOCK_S
		q_size = count_d_q();
		
		if (q_size) {
			this_job = deq_d();
		}
		
		D_Q_LOCK_E
		
		if (q_size) {
			if (this_job != D_Q_HEARTBEAT) {
				#if JS_JOBSCHED_TYPE!=JS_NONE
			
				if (!dispatch_job (this_job)) {
					DEBUG_NOW (REPORT_ERRORS, DISPATCH, "failed to dispatch job in dequeue thread");
				}
				
				#else
				DEBUG_NOW3 (REPORT_ERRORS, DISPATCH,
				            "no scheduler interface specified. dequeuing job '%s' with start posn %llu and end posn %llu",
				            this_job->job_id, this_job->start_posn, this_job->end_posn);
				#endif
				free (this_job);
			}
			
			d_q_dequeue_attempt_retries = 0;
			cont = true;
		}
		
		if (!cont) {
			d_q_dequeue_attempt_retries++;
			
			if (d_q_dequeue_attempt_retries > D_Q_HEARTBEAT_MAX_MISSES) {
				DEBUG_NOW (REPORT_INFO, DISPATCH,
				           "no more heartbeats received. distribution dequeue thread exiting...");
				break;
			}
			
			sleep_ms (D_Q_HEARTBEAT_MS);
		}
	}
	
	pthread_exit (NULL);
}
static void *r_deq_thread_start (void *arg) {
	REGISTER bool cont = true;
	rp_hit this_hit;
	
	while (cont) {
		while (count_r_q()) {
			this_hit = deq_r();
			
			if (this_hit != R_Q_NULL) {
				char new_job_ojb_id[NUM_RT_BYTES + 1];
				// necessary to silence valgrind
				g_memset (new_job_ojb_id, 0, NUM_RT_BYTES + 1);
				// split hit into data and hit fields
				// 19 bytes for representing ref_id as string
				char ref_id_tmp[20];
				ds_int32_field ref_id;
				g_memcpy (ref_id_tmp, this_hit, 19);
				ref_id_tmp[19] = '\0';
				// TODO: validate transformation
				ref_id = atoi (ref_id_tmp);
				ds_object_id_field hit_job_id;
				// also skip S_HIT_SEPARATORs
				g_memcpy (hit_job_id, 1 + 19 + this_hit, NUM_RT_BYTES);
				ds_result_hit_field hit_result_data;
				g_memcpy (hit_result_data, 1 + 19 + 1 + NUM_RT_BYTES + this_hit,
				          strlen (this_hit) - NUM_RT_BYTES - 1 - 19 - 1);
				hit_result_data[strlen (this_hit) - NUM_RT_BYTES - 1 - 19 - 1] = '\0';
				free (this_hit);
				ushort attempts = R_Q_RESULT_MAX_ATTEMPT_RETRIES;
				bool success;
				
				do {
					success = create_result (&hit_job_id, &hit_result_data, ref_id,
					                         &new_job_ojb_id);
					                         
					if (!success) {
						if (1 < attempts--) {
							sleep_ms (R_Q_RESULT_ATTEMPT_RETRY_MS);
						}
						
						else {
							break;
						}
					}
					
					else {
						break;
					}
				}
				while (1);
			}
		}
		
		// persist pending results before checking for shutdown
		R_Q_LOCK_S
		cont = !r_q_shutting_down;
		R_Q_LOCK_E
		
		if (cont) {
			sleep_ms (R_Q_SLEEP_MS);
		}
	}
	
	pthread_exit (NULL);
}
#ifdef _WIN32
BOOL WINAPI dispatch_sig_handler (DWORD dwType) {
	switch (dwType) {
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		default:
			DEBUG_NOW (REPORT_INFO, DISPATCH,
			           "control signal received. dispatch shutting down...");
			D_Q_LOCK_S
			d_q_shutting_down = true;
			D_Q_LOCK_E
			R_Q_LOCK_S
			r_q_shutting_down = true;
			R_Q_LOCK_E
			// give enq/deq threads some time, before closing shop
			Sleep (Q_SIGINT_GRACE_PERIOD_SECONDS * 1000);
			break;
	}
	
	return TRUE;
}
#else
void dispatch_sig_handler (int signum, siginfo_t *info, void *ptr) {
	DEBUG_NOW (REPORT_INFO, DISPATCH,
	           "SIGUSR1 signal received. dispatch shutting down...");
	D_Q_LOCK_S
	d_q_shutting_down = true;
	D_Q_LOCK_E
	R_Q_LOCK_S
	r_q_shutting_down = true;
	R_Q_LOCK_E
	sleep (Q_SIGINT_GRACE_PERIOD_SECONDS);      // give enq/deq threads some time, before closing shop
}
#endif
#if JS_JOBSCHED_TYPE!=JS_NONE
bool distribute (const char *exe_name, const char *dispatch_arg,
                 const char *backend_port_arg, ushort port,
                 const char *ds_server_arg, char *ds_server, const char *ds_port_arg,
                 ushort ds_port,
                 const char *si_server_arg, char *si_server, const char *si_port_arg,
                 ushort si_port,
                 const char *scan_bin_fn_arg, char *scan_bin_fn)
#else
bool distribute (const char *exe_name, const char *dispatch_arg,
                 const char *backend_port_arg, ushort port,
                 const char *ds_server_arg, char *ds_server, const char *ds_port_arg,
                 ushort ds_port,
                 const char *scan_bin_fn_arg, char *scan_bin_fn)
#endif
{
	if (!initialize_utils ()) {
		printf ("cannot initialize utils for distribute server\n"); fflush (stdout);
	}

	#ifndef NO_FULL_CHECKS

	if (port && (port < MIN_PORT || port > MAX_PORT)) {
		DEBUG_NOW1 (REPORT_ERRORS, DISPATCH, "PORT provided (%d) is out of range",
		            port);

		finalize_utils ();
		return false;
	}
	
	#endif
	
	if (!port) {
		port = Q_DEFAULT_PORT;                      // port 0 -> assign default port
	}
	
	char cmd_line[MAX_FILENAME_LENGTH + 1];
	#if JS_JOBSCHED_TYPE!=JS_NONE
	sprintf (cmd_line, "%s --%s --%s=%d --%s=%s --%s=%d --%s=%s --%s=%d --%s=%s",
	         exe_name, dispatch_arg, backend_port_arg, port,
	         ds_server_arg, ds_server, ds_port_arg, ds_port,
	         si_server_arg, si_server, si_port_arg, si_port,
	         scan_bin_fn_arg, scan_bin_fn);
	#else
	sprintf (cmd_line, "%s --%s --%s=%d --%s=%s --%s=%d --%s=%s",
	         exe_name, dispatch_arg, backend_port_arg, port,
	         ds_server_arg, ds_server, ds_port_arg, ds_port,
	         scan_bin_fn_arg, scan_bin_fn);
	#endif
	#ifdef _WIN32
	/*
	 * currently, no direct support for PBSPro or similar products, so simply launch dispatch
	 */
	int ret_val = (int)WinExec (cmd_line, SW_SHOW);
	
	if (ret_val > 31) {
		DEBUG_NOW (REPORT_INFO, DISPATCH, "distribute server launched successfully");
	}
	
	else {
		DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
		            "failed to launch distribute server (error code %d)", ret_val);
	}

	finalize_utils ();

	// ret_val>31 => success, as documented in
	// https://docs.microsoft.com/en-us/windows/desktop/api/winbase/nf-winbase-winexec
	return ret_val > 31;
	#else
	/*
	 * TODO: replace with invocation of appropriate c_jobsched_client functionality
	 */
	char qsub_cmd_line[MAX_FILENAME_LENGTH * 2 + 1];
	// pipe cmd_line to qsub => launches dispatch service on a single worker node core
	sprintf (qsub_cmd_line, "echo \"%s\" | %s", cmd_line,
	         DISTRIBUTE_SERVER_QSUB_COMMAND);
	// system returns the return value of the command invoked
	int ret_val = system (qsub_cmd_line); 
	
	if (ret_val == QSUB_SUCCESS) {
		DEBUG_NOW (REPORT_INFO, DISPATCH, "distribute server launched successfully");
	}
	
	else {
		DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
		            "failed to launch distribute server (error code %d)", ret_val);
	}

	finalize_utils ();

	return ret_val == QSUB_SUCCESS;
	#endif
}
#if JS_JOBSCHED_TYPE!=JS_NONE
bool dispatch (unsigned short port, char *ds_server, unsigned short ds_port,
               char *si_server, unsigned short si_port, char *scan_bin_fn)
#else
bool dispatch (ushort port, char *ds_server, ushort ds_port, char *scan_bin_fn)
#endif
{
	if (!initialize_utils ()) {
		printf ("cannot initialize utils for dispatch server\n"); fflush (stdout);
	}

	#ifndef NO_FULL_CHECKS

	if (port && (port < MIN_PORT || port > MAX_PORT)) {
		DEBUG_NOW1 (REPORT_ERRORS, DISPATCH, "PORT provided (%d) is out of range",
		            port);
		finalize_utils ();
		return false;
	}
	
	if (!strlen (ds_server)) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "DS_SERVER argument is empty");
		finalize_utils ();
		return false;
	}
	
	if (!ds_port) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "DS_PORT argument is zero");
		finalize_utils ();
		return false;
	}
	
	if (!strlen (scan_bin_fn)) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "SCAN_BIN_FN argument is empty");
		finalize_utils ();
		return false;
	}
	
	#endif
	
	if (!port) {
		port = Q_DEFAULT_PORT;                      // port 0 -> assign default port
	}
	
	DEBUG_NOW (REPORT_INFO, DISPATCH, "registering signal handler");
	#ifdef _WIN32
	
	if (!SetConsoleCtrlHandler ((PHANDLER_ROUTINE) dispatch_sig_handler, TRUE)) {
	#else
	DEBUG_NOW (REPORT_INFO, DISPATCH, "registering SIGUSR1 signal handler");
	static struct sigaction _sigact;
	g_memset (&_sigact, 0, sizeof (_sigact));
	_sigact.sa_sigaction = dispatch_sig_handler;
	_sigact.sa_flags = SA_SIGINFO;
	
	// when running dispatch under mpirun/mpiexec/orterun, SIGINT/SIGTERM/SIGQUIT/SIGHUP signals are likely
	// to be captured and enforced onto child processes by the mpi execution environment -- so use SIGUSR1
	// to be safe (and which is known to be simply propagated to the child processes)
	if (sigaction (SIGUSR1, &_sigact, NULL) != 0) {
	#endif
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "failed to register SIGUSR1 handler");
		finalize_utils ();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, DISPATCH, "initializing datastore");
	
	if (!initialize_datastore (ds_server, ds_port, false)) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "failed to initialize datastore");
		finalize_utils ();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, DISPATCH, "initializing queue");
	
	if (!initialize_qs()) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "failed to initialize queue");
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing datastore");
		finalize_datastore();
		finalize_utils ();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, DISPATCH, "initializing dispatch queue spinlock");
	
	if (pthread_spin_init (&d_q_spinlock, PTHREAD_PROCESS_PRIVATE)) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH,
		           "failed to initialize dispatch queue spinlock");
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing datastore");
		finalize_datastore();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing queue");
		finalize_qs();
		finalize_utils();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, DISPATCH, "initializing result queue spinlock");
	
	if (pthread_spin_init (&r_q_spinlock, PTHREAD_PROCESS_PRIVATE)) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH,
		           "failed to initialize result queue spinlock");
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch queue spinlock");
		pthread_spin_destroy (&d_q_spinlock);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing datastore");
		finalize_datastore();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing queue");
		finalize_qs();
		finalize_utils();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, DISPATCH, "initializing dispatch allocation spinlock");
	
	if (pthread_spin_init (&dis_spinlock, PTHREAD_PROCESS_PRIVATE)) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH,
		           "failed to initialize dispatch allocation spinlock");
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing datastore");
		finalize_datastore();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing queue");
		finalize_qs();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch queue spinlock");
		pthread_spin_destroy (&d_q_spinlock);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing result queue spinlock");
		pthread_spin_destroy (&r_q_spinlock);
		finalize_utils();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, DISPATCH, "initializing sockets for queue");
	
	if (!initialize_sockets (port)) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "failed to initialize sockets for queue");
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing datastore");
		finalize_datastore();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing queue");
		finalize_qs();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch queue spinlock");
		pthread_spin_destroy (&d_q_spinlock);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing result queue spinlock");
		pthread_spin_destroy (&r_q_spinlock);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch allocation spinlock");
		pthread_spin_destroy (&dis_spinlock);
		finalize_utils();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, DISPATCH, "initializing allocator");
	
	if (!initialize_allocate (si_server, si_port, scan_bin_fn)) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "failed to initialize allocator");
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing sockets for queue");
		finalize_sockets();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing datastore");
		finalize_datastore();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing queue");
		finalize_qs();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch queue spinlock");
		pthread_spin_destroy (&d_q_spinlock);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing result queue spinlock");
		pthread_spin_destroy (&r_q_spinlock);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch allocation spinlock");
		pthread_spin_destroy (&dis_spinlock);
		finalize_utils();
		return false;
	}
	
	static pthread_t d_enq_thread, d_deq_thread, r_deq_thread;
	DEBUG_NOW (REPORT_INFO, DISPATCH, "launching dispatch enqueue thread");
	
	if (pthread_create (&d_enq_thread, NULL, d_enq_thread_start, NULL)) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "failed to launch dispatch enqueue thread");
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing allocator");
		finalize_allocate();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing sockets for queue");
		finalize_sockets();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing datastore");
		finalize_datastore();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing queue");
		finalize_qs();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch queue spinlock");
		pthread_spin_destroy (&d_q_spinlock);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing result queue spinlock");
		pthread_spin_destroy (&r_q_spinlock);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch allocation spinlock");
		pthread_spin_destroy (&dis_spinlock);
		finalize_utils();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, DISPATCH, "launching dispatch dequeue thread");
	
	if (pthread_create (&d_deq_thread, NULL, d_deq_thread_start, NULL)) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "failed to launch dispatch dequeue thread");
		DEBUG_NOW (REPORT_INFO, DISPATCH, "cancelling dispatch enqueue thread");
		pthread_cancel (d_enq_thread);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing allocator");
		finalize_allocate();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing sockets for queue");
		finalize_sockets();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing datastore");
		finalize_datastore();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing queue");
		finalize_qs();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch queue spinlock");
		pthread_spin_destroy (&d_q_spinlock);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing result queue spinlock");
		pthread_spin_destroy (&r_q_spinlock);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch allocation spinlock");
		pthread_spin_destroy (&dis_spinlock);
		finalize_utils();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, DISPATCH, "launching results dequeue thread");
	
	if (pthread_create (&r_deq_thread, NULL, r_deq_thread_start, NULL)) {
		DEBUG_NOW (REPORT_ERRORS, DISPATCH, "failed to launch results dequeue thread");
		DEBUG_NOW (REPORT_INFO, DISPATCH, "cancelling dispatch dequeue thread");
		pthread_cancel (d_deq_thread);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "cancelling dispatch enqueue thread");
		pthread_cancel (d_enq_thread);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing allocator");
		finalize_allocate();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing sockets for queue");
		finalize_sockets();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing datastore");
		finalize_datastore();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing queue");
		finalize_qs();
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch queue spinlock");
		pthread_spin_destroy (&d_q_spinlock);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing result queue spinlock");
		pthread_spin_destroy (&r_q_spinlock);
		DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch allocation spinlock");
		pthread_spin_destroy (&dis_spinlock);
		finalize_utils();
		return false;
	}
	
	DEBUG_NOW2 (REPORT_INFO, DISPATCH, "launching %d client distribution thread%s",
	            D_Q_NUM_SOCKET_THREADS, (D_Q_NUM_SOCKET_THREADS == 1 ? "" : "s"));
	ushort num_threads = 0;
	static ushort thread_ids[D_Q_NUM_SOCKET_THREADS];
	
	while (num_threads < D_Q_NUM_SOCKET_THREADS) {
		thread_ids[num_threads] = num_threads;
		
		if (pthread_create (&d_q_socket_threads[num_threads], NULL,
		                    socket_client_thread_start, &thread_ids[num_threads])) {
			D_Q_LOCK_S
			d_q_shutting_down = true;
			D_Q_LOCK_E
			break;
		}
		
		num_threads++;
	}
	
	if (num_threads == D_Q_NUM_SOCKET_THREADS) {
		DEBUG_NOW (REPORT_INFO, DISPATCH, "dispatch service is ready");
	}
	
	else {
		DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
		            "failed to launch client distribution thread%s. shutting down...",
		            (D_Q_NUM_SOCKET_THREADS == 1 ? "" : "s"));
	}
	
	/*
	 * wait for all threads to exit, after receiving SIGINT
	 */
	int ret_val = 0;
	
	for (ushort d = 0; d < num_threads; d++) {
		ret_val |= pthread_join (d_q_socket_threads[d], NULL);
	}
	
	DEBUG_NOW2 (REPORT_INFO, DISPATCH,
	            "%d client distribution thread%s successfully joined",
	            D_Q_NUM_SOCKET_THREADS - ret_val,
	            ((D_Q_NUM_SOCKET_THREADS - ret_val) == 1 ? "" : "s"));
	DEBUG_NOW (REPORT_INFO, DISPATCH, "now joining dispatch and result threads");
	ret_val |= pthread_join (d_enq_thread, NULL) | pthread_join (d_deq_thread,
	                                        NULL) | pthread_join (r_deq_thread, NULL);
	/*
	 * cleanup
	 */
	DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing allocator");
	finalize_allocate();
	DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing sockets for queue");
	finalize_sockets();
	DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing datastore");
	finalize_datastore();
	DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing queue");
	finalize_qs();
	DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch queue spinlock");
	pthread_spin_destroy (&d_q_spinlock);
	DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing result queue spinlock");
	pthread_spin_destroy (&r_q_spinlock);
	DEBUG_NOW (REPORT_INFO, DISPATCH, "finalizing dispatch allocation spinlock");
	pthread_spin_destroy (&dis_spinlock);
	DEBUG_NOW1 (REPORT_INFO, DISPATCH, "exiting dispatch with status '%s'",
	            (ret_val == 0) ? "ok" : "fail");
	finalize_utils();
	return ret_val == 0;
}
