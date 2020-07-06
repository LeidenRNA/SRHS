#if JS_JOBSCHED_TYPE!=JS_NONE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
#else
	#include <netdb.h>
#endif
#include "c_jobsched_client.h"
#include "c_jobsched_server.h"
#include "binn.h"

/*
 * static, inline replacemens for memset/memcpy - silences google sanitizers
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

/*
 * NOTE: the following functions are not thread-safe
 */

#define JS_CLIENT_SOCKET_SLEEP_MS   1

#ifdef _WIN32
// documented bug in MINGW - need to manually add forward declarations
void WSAAPI freeaddrinfo (struct addrinfo *);
int WSAAPI getaddrinfo (const char *, const char *, const struct addrinfo *,
                        struct addrinfo **);
// documented bug in MINGW - need to manually add forward declarations - ends
static SOCKET js_win_client_socket;
#else
static int js_unix_client_socket = 0;
#endif

/*
 * js_prepare_response:
 *          formats a scheduling command-specific response obtained
 *          from the scheduler into a response data string
 *
 * args:    scheduler cmd string,
 *          the server's response,
 *          response data buffer
 *
 * returns: success status flag
 */
static inline bool js_prepare_response (char *cmd,
                                        uchar server_response[JS_MSG_SIZE], void **response_data) {
	*response_data = NULL;
	binn *server_response_obj = binn_open (server_response);
	
	if (!server_response_obj) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "invalid response object received from server");
		return false;
	}
	
	else {
		if (!strcmp (JS_CMD_SUBMIT_JOB, cmd)) {
			char *response_data_str = NULL;
			
			if (binn_object_get_str (server_response_obj, JS_DATA_FULL_JOB_ID,
			                         &response_data_str) && response_data_str != NULL) {
				size_t response_data_str_len = strlen (response_data_str);
				char *r = malloc (response_data_str_len + 1);
				
				if (!r) {
					DEBUG_NOW (REPORT_ERRORS, SCHED, "could not allocate response data buffer");
					binn_free (server_response_obj);
					return false;
				}
				
				g_memcpy (r, response_data_str, response_data_str_len);
				r[response_data_str_len] = '\0';

				*response_data = r;
			}
			
			else {
				DEBUG_NOW (REPORT_ERRORS, SCHED, "no job id returned from server");
				binn_free (server_response_obj);
				return false;
			}
		}
		
		else
			if (!strcmp (JS_CMD_GET_JOB_STATUS, cmd)) {
				JS_JOB_STATUS server_job_status = JS_JOB_STATUS_UNKNOWN;
				
				if (binn_object_get_uint8 (server_response_obj, JS_DATA_JOB_STATUS,
				                           (uchar *)&server_job_status)) {
					JS_JOB_STATUS *job_status = malloc (sizeof (JS_JOB_STATUS));
					
					if (job_status == NULL) {
						DEBUG_NOW (REPORT_ERRORS, SCHED, "could not allocate job status buffer");
						binn_free (server_response_obj);
						return false;
					}

					*job_status = server_job_status;
					*response_data = job_status;
				}
				
				else {
					DEBUG_NOW (REPORT_ERRORS, SCHED, "no job status received from server");
					binn_free (server_response_obj);
					return false;
				}
			}
			
			else
				if (!strcmp (JS_CMD_DEL_JOB, cmd)) {
					int32_t deljob_ret_val = PBSE_NONE;
					
					if (binn_object_get_int32 (server_response_obj, JS_DATA_RETURN_VALUE,
					                           &deljob_ret_val)) {
						int *ret_val = malloc (sizeof (int));
						
						if (ret_val == NULL) {
							DEBUG_NOW (REPORT_ERRORS, SCHED, "could not allocate delete job buffer");
							binn_free (server_response_obj);
							return false;
						}
					
						*ret_val = deljob_ret_val;
						*response_data = ret_val;
					}
					
					else {
						DEBUG_NOW (REPORT_ERRORS, SCHED,
						           "no delete job return value received from server");
						binn_free (server_response_obj);
						return false;
					}
				}
				
				else
					if (!strcmp (JS_CMD_GET_NODE_INFO, cmd)) {
						int16_t num_up_nodes = -1, num_up_procs = -1, num_free_nodes = -1,
						        num_free_procs = -1;
						        
						if (binn_object_get_int16 (server_response_obj, JS_DATA_NUM_UP_NODES,
						                           &num_up_nodes) && num_up_nodes >= 0 &&
						    binn_object_get_int16 (server_response_obj, JS_DATA_NUM_UP_PROCS,
						                           &num_up_procs) && num_up_procs >= 0 &&
						    binn_object_get_int16 (server_response_obj, JS_DATA_NUM_FREE_NODES,
						                           &num_free_nodes) && num_free_nodes >= 0 &&
						    binn_object_get_int16 (server_response_obj, JS_DATA_NUM_FREE_PROCS,
						                           &num_free_procs) && num_free_procs >= 0) {
							jsp_node_info nip = malloc (sizeof (js_node_info));
							
							if (nip == NULL) {
								DEBUG_NOW (REPORT_ERRORS, SCHED, "could not allocate node info structure");
								binn_free (server_response_obj);
								return false;
							}
						
							nip->num_up_nodes = num_up_nodes;
							nip->num_up_procs = num_up_procs;
							nip->num_free_nodes = num_free_nodes;
							nip->num_free_procs = num_free_procs;
							*response_data = nip;
						}
						
						else {
							DEBUG_NOW (REPORT_ERRORS, SCHED,
							           "node status could not be retreived from server");
							binn_free (server_response_obj);
							return false;
						}
					}
					
					else {
						DEBUG_NOW (REPORT_ERRORS, SCHED,
						           "unknown command found in server response object");
						binn_free (server_response_obj);
						return false;
					}
	}

	binn_free (server_response_obj);
	return true;
}

/*
 * js_execute:
 *          prepare a server command for submission, send it
 *          over the network to the scheduler interface, and
 *          wait for a response from the server
 *
 * args:    scheduler cmd string,
 *          command data and size,
 *          worker job executable filename (optional),
 *          response data buffer
 *
 * returns: success status flag
 */
bool js_execute (char *cmd, void *cmd_data, size_t cmd_data_sz,
                 const char *job_bin_fn, void **response_data) {
	char client_request[JS_MSG_SIZE];
	g_memset (client_request, 0, JS_MSG_SIZE);
	*response_data = NULL;
	#ifndef NO_FULL_CHECKS
	
	if (!is_js_server_cmd (cmd)) {
		DEBUG_NOW (REPORT_ERRORS, SCHED, "unknown command supplied");
		return false;
	}
	
	#endif
	binn *js_obj = binn_object();
	
	if (js_obj == NULL) {
		DEBUG_NOW (REPORT_ERRORS, SCHED, "could not allocate job server object");
		return false;
	}
	
	binn_object_set_str (js_obj, JS_CMD_STRN, cmd);
	
	if (cmd_data) {
		binn_object_set_blob (js_obj, JS_CMD_DATA, cmd_data, cmd_data_sz);
	}
	
	if (job_bin_fn) {
		binn_object_set_str (js_obj, JS_CMD_BIN_EXE_FPATH, (char *) job_bin_fn);
	}
	
	int jo_sz = binn_size (js_obj);
	
	if (JS_MSG_SIZE < jo_sz) {
		DEBUG_NOW2 (REPORT_WARNINGS, SCHED,
		            "JS_MSG_SIZE (%d) < js_obj size (%d). truncating response to JS_MSG_SIZE",
		            JS_MSG_SIZE, jo_sz);
		g_memcpy (client_request, binn_ptr (js_obj), JS_MSG_SIZE);
	}
	
	else {
		if (JS_MSG_SIZE == jo_sz) {
			g_memcpy (client_request, binn_ptr (js_obj), JS_MSG_SIZE);
		}
		
		else {
			g_memcpy (client_request, binn_ptr (js_obj), (size_t) jo_sz);
			g_memset (client_request + jo_sz, 0,
			          (size_t) JS_MSG_SIZE - jo_sz);                 // initialize/pad remaining bytes
		}
	}

	/*
	 * send job
	 */
	#ifdef _WIN32
	send (js_win_client_socket, client_request, JS_MSG_SIZE, 0);
	#else
	send (js_unix_client_socket, client_request, JS_MSG_SIZE, 0);
	#endif
	binn_free (js_obj);
	/*
	 * process server response message
	 */
	static int num_bytes_read;
	static uchar js_client_socket_recv_buf[JS_MSG_SIZE],
	       js_client_socket_pending_buf[JS_MSG_SIZE];
	REGISTER bool have_part_message = false;
	REGISTER int previous_size = 0, j;
	
	while (1) {
		REGISTER bool nonblocking_retry;
		#ifdef _WIN32
		
		if ((num_bytes_read = recv (js_win_client_socket,
		                            (char *) js_client_socket_recv_buf, JS_MSG_SIZE, 0)) <= 0) {
			if (WSAGetLastError() != WSAEWOULDBLOCK) {
				shutdown (js_win_client_socket, SD_BOTH);
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
		static fd_set select_read_fds;
		static struct timeval select_tv;
		select_tv.tv_sec = 0;
		// select should return immediately. we use sleep_ms (JS_SERVER_SOCKET_SLEEP_MS) in while loop below
		select_tv.tv_usec = 0;
		int select_retval;
		FD_ZERO (&select_read_fds);
		FD_SET (js_unix_client_socket, &select_read_fds);
		select_retval = select (js_unix_client_socket + 1, &select_read_fds, NULL, NULL,
		                        &select_tv);
		
		if (select_retval < 0) {
			DEBUG_NOW (REPORT_ERRORS, SCHED, "error with select() in socket communication");
			shutdown (js_unix_client_socket, SHUT_RDWR);
			return false;
		}
		
		else
			if (select_retval) {
				if ((num_bytes_read = read (js_unix_client_socket, js_client_socket_recv_buf,
				                            JS_MSG_SIZE)) <= 0) {
					shutdown (js_unix_client_socket, SHUT_RDWR);
					break;  // client disconnected - restart
				}
		
				nonblocking_retry = false;
			}
		
			else {
				nonblocking_retry = true;   // select() returned 0 fds -> retry
			}
		
		#endif
		
		if (nonblocking_retry) {
			sleep_ms (JS_CLIENT_SOCKET_SLEEP_MS);
		}
		
		else {
			if (have_part_message) {
				if (previous_size + num_bytes_read <= JS_MSG_SIZE) {
					for (j = previous_size; j < previous_size + num_bytes_read; j++) {
						js_client_socket_pending_buf[j] = js_client_socket_recv_buf[j - previous_size];
					}
					
					if (previous_size + num_bytes_read == JS_MSG_SIZE) {
						return js_prepare_response (cmd, js_client_socket_pending_buf, response_data);
					}
					
					else {
						previous_size += num_bytes_read;
					}
				}
				
				else {
					for (j = previous_size; j < JS_MSG_SIZE; j++) {
						js_client_socket_pending_buf[j] = js_client_socket_recv_buf[j - previous_size];
					}
					
					return js_prepare_response (cmd, js_client_socket_pending_buf, response_data);
				}
				
				continue;
			}
			
			if (num_bytes_read == JS_MSG_SIZE) {
				return js_prepare_response (cmd, js_client_socket_recv_buf, response_data);
			}
			
			else {
				for (j = 0; j < num_bytes_read; j++) {
					js_client_socket_pending_buf[j] = js_client_socket_recv_buf[j];
				}
				
				previous_size = num_bytes_read;
				have_part_message = true;
			}
		}
	}
	
	return false;
}

/*
 * set up socket-based client communication with scheduling server
 */
static bool initialize_js_client_socket (const char *server,
                                        const ushort port) {
	#ifdef _WIN32
	// credit: https://docs.microsoft.com/en-us/windows/desktop/winsock/complete-client-code
	WSADATA wsaData;
	int iResult = WSAStartup (MAKEWORD (2, 2), &wsaData);
	
	if (iResult != 0) {
		DEBUG_NOW1 (REPORT_ERRORS, SCHED, "WSAStartup failed (error code %d)",
		            iResult);
		return false;
	}
	
	struct addrinfo hints, *addr_result;
	
	g_memset (&hints, 0, sizeof (hints));
	
	hints.ai_family = AF_INET;
	
	hints.ai_socktype = SOCK_STREAM;
	
	hints.ai_protocol = IPPROTO_TCP;
	
	hints.ai_flags = AI_PASSIVE;
	
	char port_strn[10];
	
	sprintf (port_strn, "%d", port);
	
	if (getaddrinfo (server, port_strn, &hints, &addr_result) != 0) {
		DEBUG_NOW (REPORT_ERRORS, SCHED, "could not resolve server address");
		WSACleanup();
		return false;
	}
	
	js_win_client_socket = socket (hints.ai_family, hints.ai_socktype,
	                               hints.ai_protocol);
	                               
	if (js_win_client_socket == INVALID_SOCKET ||
	    connect (js_win_client_socket, addr_result->ai_addr,
	             (int) addr_result->ai_addrlen) == SOCKET_ERROR) {
		DEBUG_NOW1 (REPORT_ERRORS, SCHED,
		            "could not set up socket connection (error code %d)",
		            WSAGetLastError());
		            
		if (js_win_client_socket != INVALID_SOCKET) {
			closesocket (js_win_client_socket);
		}
		
		freeaddrinfo (addr_result);
		WSACleanup();
		return false;
	}
	
	freeaddrinfo (addr_result);
	#else
	struct hostent *server_ent = gethostbyname (server);
	
	if (server_ent == NULL) {
		DEBUG_NOW (REPORT_ERRORS, SCHED, "could not resolve address");
		return false;
	}
	
	struct sockaddr_in serv_addr;
	
	g_memset (&serv_addr, 0, sizeof (serv_addr));
	
	serv_addr.sin_family = AF_INET;
	
	g_memcpy ((char *)&serv_addr.sin_addr.s_addr, (char *)server_ent->h_addr,
	          server_ent->h_length);
	
	serv_addr.sin_port = htons (port);
	
	if (((js_unix_client_socket = socket (AF_INET, SOCK_STREAM, 0)) < 0) ||
	    (connect (js_unix_client_socket, (struct sockaddr *)&serv_addr,
	              sizeof (serv_addr)) < 0)) {
		DEBUG_NOW (REPORT_ERRORS, SCHED, "failed to set up socket connection");
	
		if (js_unix_client_socket >= 0) {
			close (js_unix_client_socket);
		}
	
		return false;
	}
	
	#endif
	return true;
}

static void finalize_js_client_socket() {
	#ifdef _WIN32
	closesocket (js_win_client_socket);
	WSACleanup();
	#else
	close (js_unix_client_socket);
	#endif
}

bool initialize_jobsched_client (char *si_server, ushort si_port) {
	if (!initialize_js_client_socket (si_server, si_port)) {
		DEBUG_NOW (REPORT_ERRORS, SCHED, "failed to initialize job scheduling client");
		return false;
	}
	
	return true;
}

void finalize_jobsched_client() {
	finalize_js_client_socket();
}
#endif
