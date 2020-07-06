#if JS_JOBSCHED_TYPE!=JS_NONE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdatomic.h>
#ifdef _WIN32
	#include <windows.h>
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <shellapi.h>
#else
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <string.h>
	#include <fcntl.h>
	#include <signal.h>
#endif
#include <mongoc/mongoc.h>
#include "c_jobsched_server.h"
#include "binn.h"
#include "rna.h"

atomic_bool scheduler_server_shutting_down = false;
static char *js_cmd_list[JS_NUM_CMDS] = {JS_CMD_GET_NODE_INFO, JS_CMD_SUBMIT_JOB,
                                         JS_CMD_GET_JOB_STATUS, JS_CMD_DEL_JOB};
static char *scan_job_args = NULL;

#define C_ARG_MAX                           1000       // ARG_MAX in limits.h is typically too large
#define JS_SERVER_SOCKET_SLEEP_MS           1
#define JS_SIGINT_GRACE_PERIOD_S            3          // grace period by SIGNINT handler, for finalization routines

#if JS_JOBSCHED_TYPE==JS_TORQUE                        // TORQUE-based environment settings; should be near-to-identical for PBSPro
	static struct attrl
	// attrl specifier used with pbs_statnode
	statjob_attrl;
	// current (initialized) TORQUE/PBS headnode server
	static char c_headnode_server[HOST_NAME_MAX + 1];
	
	static int pbs_connection_id = -1;
	#define TORQUE_CMD_Q_INFO               "qstat"
	#define TORQUE_JOB_FIELD_SEP            ','        // char that separates individual jobs in 'pbsnodes' ATTR_NODE_jobs attribute value's field
#elif JS_JOBSCHED_TYPE==JS_SLURM                       // TORQUE-based environment settings
	#define SLURM_SSH_CMD_PATH              ""
	#define SLURM_SSH_CMD                   "ssh"
	#define SLURM_CMD_REDIRECT              ">/dev/null 2>&1"
	#define SLURM_CMD_Q_INFO                "sinfo"
	#define SLURM_CMD_N_INFO                "scontrol show nodes"
	#define SLURM_CMD_MAX_OUT_LINE_LENGTH   1000
	#ifdef _WIN32
		#define SLURM_SSH_USER              ""
	#else
		#define SLURM_SSH_USER              ""
	#endif
#endif

#ifdef _WIN32
	static SOCKET js_win_listen_socket = 0;
#else
	static int js_unix_listen_socket_fd = 0;
	static struct sockaddr_in js_unix_address;
	static socklen_t js_unix_address_len = 0;
#endif

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

bool is_js_server_cmd (const char *cmd) {
	for (ushort i = 0; i < JS_NUM_CMDS; i++) {
		if (!strcmp (js_cmd_list[i], cmd)) {
			return true;
		}
	}
	
	return false;
}

bool get_node_info (int16_t *num_up_nodes, int16_t *num_up_procs,
                    int16_t *num_free_nodes, int16_t *num_free_procs);

static inline bool validate_server_connection() {
	int16_t num_up_nodes, num_up_procs, num_free_nodes, num_free_procs;
	DEBUG_NOW (REPORT_INFO, SCHED,
	           "validating interface to scheduler using current node info");
	           
	if (get_node_info (&num_up_nodes, &num_up_procs, &num_free_nodes,
	                   &num_free_procs)) {
		DEBUG_NOW4 (REPORT_INFO, SCHED,
		            "up (%d nodes, %d procs) free (%d nodes, %d procs)",
		            num_up_nodes, num_up_procs, num_free_nodes, num_free_procs);
		return true;
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "failed to get node info");
		return false;
	}
}

static bool initialize_js_server_socket (long port) {
	DEBUG_NOW (REPORT_INFO, SCHED,
	           "initializing socket connection for scheduling server");
	#ifdef _WIN32
	// credit: https://docs.microsoft.com/en-us/windows/desktop/winsock/complete-server-code
	WSADATA wsaData;
	
	if (WSAStartup (MAKEWORD (2, 2), &wsaData)) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "could not startup WSA");
		return false;
	}
	
	struct addrinfo hints;
	
	ZeroMemory (&hints, sizeof (hints));
	
	hints.ai_family = AF_INET;
	
	hints.ai_socktype = SOCK_STREAM;
	
	hints.ai_protocol = IPPROTO_TCP;
	
	hints.ai_flags = AI_PASSIVE;
	
	js_win_listen_socket = socket (AF_INET, SOCK_STREAM, 0);
	
	if (js_win_listen_socket == INVALID_SOCKET) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "could not initialize socket");
		WSACleanup();
		return false;
	}
	
	ulong mode = 1; // non-blocking
	
	if (ioctlsocket (js_win_listen_socket, FIONBIO, &mode) != NO_ERROR) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "could not control I/O mode for socket");
		closesocket (js_win_listen_socket);
		WSACleanup();
		return false;
	}
	
	struct sockaddr_in address;
	
	address.sin_family = AF_INET;
	
	address.sin_addr.s_addr = INADDR_ANY;
	
	address.sin_port = htons (port);
	
	if (bind (js_win_listen_socket, (struct sockaddr *)&address,
	          sizeof (address)) == SOCKET_ERROR) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "could not bind address to socket");
		closesocket (js_win_listen_socket);
		WSACleanup();
		return false;
	}
	
	if (listen (js_win_listen_socket, SOMAXCONN) == SOCKET_ERROR) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "could not listen for connections ");
		closesocket (js_win_listen_socket);
		WSACleanup();
		return false;
	}
	
	return true;
	#else
	
	// credit: https://www.geeksforgeeks.org/socket-programming-cc/
	if ((js_unix_listen_socket_fd = socket (AF_INET, SOCK_STREAM, 0)) == 0) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "could not initialize socket");
		return false;
	}
	
	int flags = fcntl (js_unix_listen_socket_fd, F_GETFL);
	
	if (flags < 0) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "could not get socket flags");
		close (js_unix_listen_socket_fd);
		return false;
	}
	
	if (fcntl (js_unix_listen_socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "could not set socket flags");
		close (js_unix_listen_socket_fd);
		return false;
	}
	
	int opt = 1;
	
	if (setsockopt (js_unix_listen_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
	                sizeof (opt))) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "could not set socket flags");
		close (js_unix_listen_socket_fd);
		return false;
	}
	
	js_unix_address.sin_family = AF_INET;
	js_unix_address.sin_addr.s_addr = htonl (INADDR_ANY);
	js_unix_address.sin_port = htons (port);
	js_unix_address_len = sizeof (js_unix_address);
	
	if (bind (js_unix_listen_socket_fd, (struct sockaddr *)&js_unix_address,
	          js_unix_address_len) < 0) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "could not bind address to socket");
		close (js_unix_listen_socket_fd);
		return false;
	}
	
	// note that backlog limit is set to 0: only 1 client connection expected
	if (listen (js_unix_listen_socket_fd, 0) < 0) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "could not listen for connections");
		close (js_unix_listen_socket_fd);
		return false;
	}
	
	return true;
	#endif
}

static void finalize_js_server_socket() {
	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "finalizing socket connections");
	#ifdef _WIN32
	closesocket (js_win_listen_socket);
	WSACleanup();
	#else
	close (js_unix_listen_socket_fd);
	#endif
}

#if JS_JOBSCHED_TYPE==JS_TORQUE
void reconnect_torque() {
	pbs_connection_id = pbs_connect (c_headnode_server);
	
	if (pbs_connection_id < 0) {
		DEBUG_NOW1 (REPORT_ERRORS, SCHED,
		            "failed to reconnect to headnode '%s'", c_headnode_server);
	}
}

bool initialize_jobsched_server (char *headnode_server) {
	DEBUG_NOW (REPORT_INFO, SCHED,
	           "initializing TORQUE/PBS job scheduling interface");
	/*
	 * scan_job_args to include:
	 * %s=%s, %s=--%s, %s=--%s --%s="%s", where string format specifiers are for
	 * JS_JOBSCHED_JOB_SUBMIT_BEP_ENV, job_bin_fn (MAX_FILENAME_LENGTH), JS_JOBSCHED_JOB_ID_ENV, SCHED_JOB_ID_ARG_LONG,
	 * JS_JOBSCHED_JOB_SUBMIT_ARGS_ENV, SCAN_MODE_ARG_LONG, MPI_PORT_NAME_ARG_LONG, job->mpi_port_name (MPI_MAX_PORT_NAME)
	 */
	scan_job_args = malloc (strlen (JS_JOBSCHED_JOB_SUBMIT_BEP_ENV) + 1 +
	                        MAX_FILENAME_LENGTH + 1 + strlen (JS_JOBSCHED_JOB_ID_ENV) + 1 +
	                        strlen (SCHED_JOB_ID_ARG_LONG) + 1 + strlen (JS_JOBSCHED_JOB_SUBMIT_ARGS_ENV) +
	                        1 + strlen (SCAN_MODE_ARG_LONG) + 1 +
	                        MPI_MAX_PORT_NAME + 1 + 17);
	                        
	if (!scan_job_args) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "failed to allocate memory for scan job arguments");
		return false;
	}
	
	pbs_connection_id = pbs_connect (headnode_server);
	
	if (pbs_connection_id >= 0) {
		DEBUG_NOW1 (REPORT_INFO, SCHED,
		            "successfully connected to headnode '%s'", headnode_server);
		strcpy (c_headnode_server, headnode_server);
		DEBUG_NOW (REPORT_INFO, SCHED,
		           "validating server connection");
		// attrl specifier used with pbs_statjob - only need ATTR_state
		statjob_attrl.name = ATTR_state;
		statjob_attrl.resource = NULL;
		statjob_attrl.value = NULL;
		statjob_attrl.next = NULL;
		return validate_server_connection();
	}
	
	else {
		DEBUG_NOW1 (REPORT_ERRORS, SCHED,
		            "failed to connect to headnode '%s'", headnode_server);
		return false;
	}
}
#elif JS_JOBSCHED_TYPE==JS_SLURM
bool initialize_jobsched_server() {
	DEBUG_NOW (REPORT_INFO, SCHED,
	           "initializing SLURM job scheduler interface");
	/*
	 * scan_job_args to include:
	 * %s=%s, %s=--%s, %s=--%s --%s="%s", where string format specifiers are for
	 * JS_JOBSCHED_JOB_SUBMIT_BEP_ENV, job_bin_fn (MAX_FILENAME_LENGTH), JS_JOBSCHED_JOB_ID_ENV, SCHED_JOB_ID_ARG_LONG,
	 * JS_JOBSCHED_JOB_SUBMIT_ARGS_ENV, SCAN_MODE_ARG_LONG, MPI_PORT_NAME_ARG_LONG, job->mpi_port_name (MPI_MAX_PORT_NAME)
	 */
	scan_job_args = malloc (strlen (JS_JOBSCHED_JOB_SUBMIT_BEP_ENV) + 1 +
	                        MAX_FILENAME_LENGTH + 1 + strlen (JS_JOBSCHED_JOB_ID_ENV) + 1 +
	                        strlen (SCHED_JOB_ID_ARG_LONG) + 1 + strlen (JS_JOBSCHED_JOB_SUBMIT_ARGS_ENV) +
	                        1 + strlen (SCAN_MODE_ARG_LONG) + 1 +
	                        MPI_MAX_PORT_NAME + 1 + 17);
	                        
	if (!scan_job_args) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "failed to allocate memory for scan job arguments");
		return false;
	}
	
	char cmd[C_ARG_MAX + 1];
	// cmd <- path|ssh user@headnode_server 'queue info command' redirection
	sprintf (cmd, "%s%s %s@%s \"%s %s\"", SLURM_SSH_CMD_PATH, SLURM_SSH_CMD,
	         SLURM_SSH_USER, headnode_server, SLURM_CMD_Q_INFO, SLURM_CMD_REDIRECT);
	DEBUG_NOW2 (REPORT_INFO, SCHED,
	            "validating connection to headnode server '%s' using command '%s'",
	            headnode_server, SLURM_CMD_Q_INFO);
	            
	if (!system (cmd)) {
		DEBUG_NOW1 (REPORT_INFO, SCHED,
		            "successfully connected to headnode '%s'", headnode_server);
		strcpy (c_headnode_server, headnode_server);
		return true;
	}
	
	else {
		DEBUG_NOW1 (REPORT_ERRORS, SCHED,
		            "failed to connect to headnode '%s'", headnode_server);
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, SCHED,
	           "validating server connection");
	return validate_server_connection();
}
#else
bool initialize_jobsched_server() {
	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "unsupported job scheduler specified");
	return false;
}
#endif

void finalize_jobsched_server() {
	#if JS_JOBSCHED_TYPE==JS_TORQUE

	if (pbs_connection_id < 0) {
		DEBUG_NOW (REPORT_WARNINGS, SCHED,
		           "TORQUE/PBS job scheduler interface not initialized");
		return;
	}
	
	DEBUG_NOW (REPORT_INFO, SCHED,
	           "finalizing job scheduling interface");
	pbs_disconnect (pbs_connection_id);
	#elif JS_JOBSCHED_TYPE==JS_SLURM
	DEBUG_NOW (REPORT_INFO, SCHED,
	           "finalizing job scheduling interface");
	free (scan_job_args);
	#else
	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "unsupported job scheduler specified");
	#endif
	free (scan_job_args);
}

bool get_node_info (int16_t *num_up_nodes, int16_t *num_up_procs,
                    int16_t *num_free_nodes, int16_t *num_free_procs) {
	*num_up_nodes = 0;
	*num_up_procs = 0;
	*num_free_nodes = 0;
	*num_free_procs = 0;
	#if JS_JOBSCHED_TYPE==JS_TORQUE
	struct batch_status *bs = pbs_statnode (pbs_connection_id, NULL, NULL, NULL);
	
	if (bs == NULL) {
		reconnect_torque();     // try to reconnect, in case of timeout (~360s on LIACS production headnode)
		bs = pbs_statnode (pbs_connection_id, NULL, NULL, NULL);
	}
	
	if (bs != NULL) {
		struct batch_status *next_bs = bs;
		
		do {
			if (next_bs->attribs != NULL) {
				ushort this_jobs = 0;
				int this_np = 0;
				bool this_up = false, this_free = false, is_cluster_node = false;
				struct attrl *next_attrib = next_bs->attribs;
				
				do {
					if (!strcmp (next_attrib->name, ATTR_NODE_jobs)) {
						size_t j_strn_len = strlen (next_attrib->value);
						
						if (j_strn_len > 0) {
							this_jobs++;    // attribute value field not-empty -> have at least one job running
							
							for (ushort i = 0; i < j_strn_len; i++) {
								if (next_attrib->value[i] == TORQUE_JOB_FIELD_SEP) {
									this_jobs++;
								}
							}
						}
					}
					
					else
						if (!strcmp (next_attrib->name, ATTR_NODE_state)) {
							// from pbs_ifl.h:
							// active = job-exclusive, job-sharing, or busy
							// up = job-execlusive, job-sharing, reserve, free, busy and time-shared
							// NOTE: here we only consider "cluster" node types as "up" (not "time-shared")
							this_free = ! (!strcmp (next_attrib->value, ND_job_exclusive) ||
							               !strcmp (next_attrib->value, ND_job_sharing) ||
							               !strcmp (next_attrib->value, ND_busy));
							this_up = !this_free ||
							          !strcmp (next_attrib->value, ND_free) ||
							          !strcmp (next_attrib->value, ND_reserve) ||
							          !strcmp (next_attrib->value, ND_timeshared);
							this_free &=
							                    this_up;                                      // is only really free if node is up
						}
						
						else
							if (!strcmp (next_attrib->name, ATTR_NODE_np)) {
								size_t np_strn_len = strlen (next_attrib->value);
								
								if (np_strn_len > 0) {
									this_np = atoi (next_attrib->value);
								}
							}
							
							else
								if (!strcmp (next_attrib->name, ATTR_NODE_ntype) &&
								    !strcmp (next_attrib->value, ND_cluster)) {
									is_cluster_node = true;
								}
								
					next_attrib = next_attrib->next;
				}
				while (next_attrib != NULL);
				
				if (is_cluster_node && this_up) {
					(*num_up_nodes)++;
					(*num_up_procs) += this_np;
					
					if (this_free) {
						(*num_free_nodes)++;
						(*num_free_procs) += this_np - this_jobs;
					}
				}
			}
			
			next_bs = next_bs->next;
		}
		while (next_bs != NULL);
		
		pbs_statfree (bs);
		return true;
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "failed to get node info");
		*num_up_nodes = -1;
		*num_up_procs = -1;
		*num_free_nodes = -1;
		*num_free_procs = -1;
		return false;
	}
	
	#elif JS_JOBSCHED_TYPE==JS_SLURM
	FILE *cmd_fp;
	char cmd[C_ARG_MAX + 1], cmd_out[SLURM_CMD_MAX_OUT_LINE_LENGTH + 1];
	// cmd <- path|ssh user@headnode_server 'queue info command' redirection
	sprintf (cmd, "%s%s %s@%s \"%s\"", SLURM_SSH_CMD_PATH, SLURM_SSH_CMD,
	         SLURM_SSH_USER, c_headnode_server, SLURM_CMD_N_INFO);
	cmd_fp = popen (cmd, "r");
	
	if (cmd_fp != NULL) {
		while (fgets (cmd_out, SLURM_CMD_MAX_OUT_LINE_LENGTH, cmd_fp) != NULL) {
			DEBUG_NOW (REPORT_INFO, SCHED, cmd_out);
		}
	
		pclose (cmd_fp);
		return true;
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "failed to get node info");
		return false;
	}
	
	node_info_msg_t *node_buffer_ptr = NULL;
	
	if (!slurm_load_node ((time_t) NULL, &node_buffer_ptr, SHOW_ALL)) {
		for (int i = 0; i < node_buffer_ptr->record_count; i++) {
			node_info_t *node_ptr;
			node_ptr = & (node_buffer_ptr->node_array[i]);
			// assumes run-of-the-mill cluster setup (not one of CLUSTER_FLAG_BG, CLUSTER_FLAG_CRAY, etc.)
			bool this_up = true;
			uint16_t my_state = node_ptr->node_state;
			uint16_t err_cpus = 0, alloc_cpus = 0;
			ushort avail_cpus = node_ptr->cpus, total_cpus = avail_cpus;
	
			// check all conditions for which this node might not be 'up', and clear non-BASE flags if required (see slurm.h)
			if (my_state & NODE_STATE_DRAIN ||
			    my_state & NODE_STATE_NO_RESPOND ||
			    my_state & NODE_STATE_POWER_SAVE ||
			    my_state & NODE_STATE_FAIL ||
			    my_state & NODE_STATE_MAINT) {
				my_state &= NODE_STATE_BASE;
				this_up = false;
			}
	
			if (my_state == NODE_STATE_UNKNOWN || my_state == NODE_STATE_DOWN ||
			    my_state == NODE_STATE_ERROR || my_state == NODE_STATE_FUTURE) {
				this_up = false;
			}
	
			slurm_get_select_nodeinfo (node_ptr->select_nodeinfo, SELECT_NODEDATA_SUBCNT,
			                           NODE_STATE_ALLOCATED, &alloc_cpus);
			avail_cpus -= alloc_cpus;
			slurm_get_select_nodeinfo (node_ptr->select_nodeinfo, SELECT_NODEDATA_SUBCNT,
			                           NODE_STATE_ERROR, &err_cpus);
			avail_cpus -= err_cpus;
	
			if ((alloc_cpus && err_cpus) ||
			    (avail_cpus && (avail_cpus != total_cpus))) {
				my_state &= NODE_STATE_FLAGS;
				my_state |= NODE_STATE_MIXED;
			}
	
			if (this_up) {
				(*num_up_nodes)++;
				(*num_up_procs) += node_ptr->cpus;
	
				if (avail_cpus) {
					(*num_free_nodes)++;
					(*num_free_procs) += avail_cpus;
				}
			}
		}
	
		slurm_free_node_info_msg (node_buffer_ptr);
		return true;
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "failed to get node info");
		*num_up_nodes = -1;
		*num_up_procs = -1;
		*num_free_nodes = -1;
		*num_free_procs = -1;
		return false;
	}
	
	#else
	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "unsupported job scheduling interface");
	return false;
	#endif
}

bool get_job_status (char *full_job_id, JS_JOB_STATUS *job_status) {
	*job_status = JS_JOB_STATUS_UNKNOWN;
	#ifndef NO_FULL_CHECKS
	
	if (!full_job_id) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "job id not supplied");
		return false;
	}
	
	#endif
	#if JS_JOBSCHED_TYPE==JS_TORQUE
	struct batch_status *bs = pbs_statjob (pbs_connection_id, full_job_id,
	                                       &statjob_attrl, NULL);
	                                       
	if (bs == NULL) {
		DEBUG_NOW1 (REPORT_WARNINGS, SCHED,
		            "reconnecting to PBS server in get_job_status [pbs_error=%d]",
		            pbs_errno);
		reconnect_torque();     // try to reconnect, in case of timeout (~360s on LIACS production headnode)
		bs = pbs_statjob (pbs_connection_id, full_job_id, &statjob_attrl, NULL);
	}
	
	if (bs != NULL) {
		struct batch_status *next_bs = bs;

		do {
			if (next_bs->name && next_bs->attribs != NULL &&
			    !strcmp (next_bs->name, full_job_id)) {
				struct attrl *next_attrib = next_bs->attribs;
				
				do {
					if (!strcmp (next_attrib->name, ATTR_state)) {
						if (!strcmp ("R", next_attrib->value)) {
							*job_status = JS_JOBS_STATUS_RUNNING;
							pbs_statfree (bs);
							return true;
						}
						
						else
							if (!strcmp ("C", next_attrib->value) ||
							    !strcmp ("E", next_attrib->value)) {
								*job_status = JS_JOBS_STATUS_NOT_RUNNING;
								pbs_statfree (bs);
								return true;
							}
							
							else
								if (!strcmp ("Q", next_attrib->value) ||
								    !strcmp ("H", next_attrib->value) ||
								    !strcmp ("T", next_attrib->value) ||
								    !strcmp ("W", next_attrib->value) ||
								    !strcmp ("S", next_attrib->value)) {
									*job_status = JS_JOBS_STATUS_WAITING;
									pbs_statfree (bs);
									return true;
								}
	
						break;
					}
					
					else {
						DEBUG_NOW2 (REPORT_WARNINGS, SCHED,
						            "unknown attrib name/value in get_job_status [%s/%s]",
						            next_attrib->name, next_attrib->value);
					}
					
					next_attrib = next_attrib->next;
				}
				while (next_attrib != NULL);
			}
			
			next_bs = next_bs->next;
		}
		while (next_bs != NULL);
		
		pbs_statfree (bs);
		return true;
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, SCHED, "failed to get job status");
		return false;
	}
	
	#elif JS_JOBSCHED_TYPE==JS_SLURM
	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "SLURM job status interface not yet implemented");
	return false;
	#else
	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "unsupported job scheduler interface");
	return false;
	#endif
}

bool del_job (char *full_job_id, int32_t *ret_val) {
	*ret_val = PBSE_JOBNOTFOUND;        // set to initial, arbitrary error value
	#ifndef NO_FULL_CHECKS
	
	if (!full_job_id) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "full job id not supplied");
		return false;
	}
	
	#endif
	#if JS_JOBSCHED_TYPE==JS_TORQUE
	*ret_val = pbs_deljob (pbs_connection_id, full_job_id, NULL);
	
	if (*ret_val != PBSE_NONE) {
		DEBUG_NOW1 (REPORT_WARNINGS, SCHED,
		            "reconnecting to PBS server in del_job [pbs_error=%d]", pbs_errno);
		reconnect_torque();     // try to reconnect, in case of timeout (~360s on LIACS production headnode)
		*ret_val = pbs_deljob (pbs_connection_id, full_job_id, NULL);
		
		if (*ret_val != PBSE_NONE) {
			DEBUG_NOW (REPORT_ERRORS, SCHED,
			           "failed to delete job");
			return false;
		}
	}
	
	return true;
	#elif JS_JOBSCHED_TYPE==JS_SLURM
	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "SLURM scheduler interface not yet implemented");
	return false;
	#else
	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "unsupported job scheduler");
	return false;
	#endif
}

bool submit_job (wnp_job job, const char *job_bin_fn,
                 char wn_job_id[JS_JOBSCHED_MAX_FULL_JOB_ID_LEN + 1]) {
	wn_job_id[0] = '\0';
	#if JS_JOBSCHED_TYPE==JS_TORQUE
	struct attropl a[3];
	a[0].name = ATTR_N;
	a[0].resource = NULL;
	a[0].value = JS_JOBSCHED_JOB_NAME;
	a[0].next = & (a[1]);
	a[1].name = ATTR_v;
	a[1].resource = NULL;
	// note: scheduler job ID not provided in scan_job_args, and needs to be supplied with submission script (via $PBS_JOBID, $SLURM_JOBID)
	sprintf (scan_job_args, "%s=%s, %s=--%s, %s=--%s --%s=\"%s\"",
	         JS_JOBSCHED_JOB_SUBMIT_BEP_ENV,
	         job_bin_fn,
	         JS_JOBSCHED_JOB_ID_ENV,
	         SCHED_JOB_ID_ARG_LONG,
	         JS_JOBSCHED_JOB_SUBMIT_ARGS_ENV,
	         SCAN_MODE_ARG_LONG,
	         MPI_PORT_NAME_ARG_LONG, job->mpi_port_name);
	a[1].value = scan_job_args;
	a[1].next = & (a[2]);
	a[2].name = ATTR_l;
	a[2].resource = JS_JOBSCHED_PBS_JOB_RESOURCES_TYPE;
	a[2].value = JS_JOBSCHED_PBS_JOB_RESOURCES_VALUE;
	a[2].next = NULL;

	char *pbs_submit_ret = pbs_submit (pbs_connection_id, & (a[0]),
	                                   JS_JOBSCHED_PBS_SCRIPT_FPATH, NULL, NULL);
	                                   
	if (pbs_errno != 0) {
		DEBUG_NOW2 (REPORT_WARNINGS, SCHED,
		            "submit job first attempt failed: %d (%s)", pbs_errno,
		            pbse_to_txt (pbs_errno));
		            
		if (pbs_submit_ret) {
			free (pbs_submit_ret);
		}
		
		reconnect_torque();     // try to reconnect in case of timeout (~360s on LIACS production headnode)
		pbs_submit_ret = pbs_submit (pbs_connection_id, & (a[0]),
		                             JS_JOBSCHED_PBS_SCRIPT_FPATH, NULL, NULL);
		                             
		if (pbs_errno != 0) {
			DEBUG_NOW2 (REPORT_WARNINGS, SCHED,
			            "submit job second attempt failed: %d (%s)", pbs_errno,
			            pbse_to_txt (pbs_errno));
		}
	}
	
	if (!pbs_errno && pbs_submit_ret) {
		DEBUG_NOW1 (REPORT_INFO, SCHED,
		            "submitted cluster job '%s'", pbs_submit_ret);
		char *sep_substring = strchr (pbs_submit_ret, JS_JOBSCHED_PBS_JOB_ID_SEPARATOR);
		
		if (sep_substring && sep_substring != pbs_submit_ret) {
			if (JS_JOBSCHED_MAX_JOB_ID_LEN < sep_substring - pbs_submit_ret) {
				DEBUG_NOW2 (REPORT_WARNINGS, SCHED,
				            "retrieved server job id length (%lu) exceeds JS_JOBSCHED_MAX_JOB_ID_LEN (%d) in submit_job",
				            sep_substring - pbs_submit_ret, JS_JOBSCHED_MAX_JOB_ID_LEN);
			}
			
			else {
				// here we only check for presence of separator but still return the full string,
				// which is required for job status testing
				size_t ret_len = strlen (pbs_submit_ret);
				g_memcpy (wn_job_id, pbs_submit_ret, ret_len);
				wn_job_id[ret_len] = '\0';
				free (pbs_submit_ret);
				return true;
			}
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, SCHED,
			           "failed to retrieve job id");
		}
		
		free (pbs_submit_ret);
		return false;
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "failed to submit job");
		           
		if (pbs_submit_ret) {
			free (pbs_submit_ret);
		}
		
		return false;
	}
	
	#elif JS_JOBSCHED_TYPE==JS_SLURM
	FILE *cmd_fp;
	char cmd[C_ARG_MAX + 1], cmd_out[SLURM_CMD_MAX_OUT_LINE_LENGTH + 1];
	// cmd <- path|ssh user@headnode_server 'queue info command' redirection
	sprintf (cmd, "%s%s %s@%s \"%s\"", SLURM_SSH_CMD_PATH, SLURM_SSH_CMD,
	         SLURM_SSH_USER, c_headnode_server, SLURM_CMD_N_INFO);
	cmd_fp = popen (cmd, "r");
	
	if (cmd_fp != NULL) {
		while (fgets (cmd_out, SLURM_CMD_MAX_OUT_LINE_LENGTH, cmd_fp) != NULL) {
			DEBUG_NOW (REPORT_INFO, SCHED, cmd_out);
		}
	
		pclose (cmd_fp);
		return true;
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "failed to get node status");
		return false;
	}
	
	return true;
	node_info_msg_t *node_buffer_ptr = NULL;
	
	if (!slurm_load_node ((time_t) NULL, &node_buffer_ptr, SHOW_ALL)) {
		for (int i = 0; i < node_buffer_ptr->record_count; i++) {
			node_info_t *node_ptr;
			node_ptr = & (node_buffer_ptr->node_array[i]);
			// assumes run-of-the-mill cluster setup (not one of CLUSTER_FLAG_BG, CLUSTER_FLAG_CRAY, etc.)
			bool this_up = true;
			uint16_t my_state = node_ptr->node_state;
			uint16_t err_cpus = 0, alloc_cpus = 0;
			ushort avail_cpus = node_ptr->cpus, total_cpus = avail_cpus;
	
			// check all conditions for which this node might not be 'up', and clear non-BASE flags if required (see slurm.h)
			if (my_state & NODE_STATE_DRAIN ||
			    my_state & NODE_STATE_NO_RESPOND ||
			    my_state & NODE_STATE_POWER_SAVE ||
			    my_state & NODE_STATE_FAIL ||
			    my_state & NODE_STATE_MAINT) {
				my_state &= NODE_STATE_BASE;
				this_up = false;
			}
	
			if (my_state == NODE_STATE_UNKNOWN || my_state == NODE_STATE_DOWN ||
			    my_state == NODE_STATE_ERROR || my_state == NODE_STATE_FUTURE) {
				this_up = false;
			}
	
			slurm_get_select_nodeinfo (node_ptr->select_nodeinfo, SELECT_NODEDATA_SUBCNT,
			                           NODE_STATE_ALLOCATED, &alloc_cpus);
			avail_cpus -= alloc_cpus;
			slurm_get_select_nodeinfo (node_ptr->select_nodeinfo, SELECT_NODEDATA_SUBCNT,
			                           NODE_STATE_ERROR, &err_cpus);
			avail_cpus -= err_cpus;
	
			if ((alloc_cpus && err_cpus) ||
			    (avail_cpus && (avail_cpus != total_cpus))) {
				my_state &= NODE_STATE_FLAGS;
				my_state |= NODE_STATE_MIXED;
			}
	
			if (this_up) {
				(*num_up_nodes)++;
				(*num_up_procs) += node_ptr->cpus;
	
				if (avail_cpus) {
					(*num_free_nodes)++;
					(*num_free_procs) += avail_cpus;
				}
			}
		}
	
		slurm_free_node_info_msg (node_buffer_ptr);
		return true;
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "failed to get node status");
		return false;
	}
	
	#else
	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "unsupported job scheduler interface");
	free_dataset (cssd_dataset);
	free_dataset (sequence_dataset);
	free_dataset (job_dataset);
	free (cs_strn);
	free (pos_var_strn);
	return false;
	#endif
}

#ifdef _WIN32
BOOL WINAPI scheduler_server_sig_handler (DWORD dwType) {
	switch (dwType) {
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		default:
			DEBUG_NOW (REPORT_INFO, SCHED,
			           "control signal event received. scheduling server shutting down...");
			scheduler_server_shutting_down = true;
			Sleep (JS_SIGINT_GRACE_PERIOD_S *
			       1000);  // grace period for finalization, before closing shop
			break;
	}
	
	return TRUE;
}
#else
void scheduler_server_sig_handler (int signum, siginfo_t *info, void *ptr) {
	DEBUG_NOW (REPORT_INFO, SCHED,
	           "control signal event received. scheduling server shutting down...");
	scheduler_server_shutting_down = true;
	sleep (JS_SIGINT_GRACE_PERIOD_S);                     // grace period for finalization, before closing shop
}
#endif

static inline bool setup_SIGINT_handler() {
	DEBUG_NOW (REPORT_INFO, SCHED,
	           "registering signal handler");
	#ifdef _WIN32
	           
	if (!SetConsoleCtrlHandler ((PHANDLER_ROUTINE)scheduler_server_sig_handler,
	                            TRUE)) {
	#else
	static struct sigaction _sigact;
	g_memset (&_sigact, 0, sizeof (_sigact));
	_sigact.sa_sigaction = scheduler_server_sig_handler;
	_sigact.sa_flags = SA_SIGINFO;
	                            
	if (sigaction (SIGINT, &_sigact, NULL) != 0 ||
	    sigaction (SIGTERM, &_sigact, NULL) != 0 ||
	    sigaction (SIGQUIT, &_sigact, NULL) != 0 ||
	    sigaction (SIGHUP, &_sigact, NULL) != 0) {
	#endif
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "failed to register signal handler");
		return false;
	}
	
	return true;
}

#ifdef _WIN32
void process_client_request (uchar *client_request,
                             SOCKET js_server_client_socket)
#else
void process_client_request (uchar *client_request, int js_server_client_socket)
#endif
{
	static char server_response[JS_MSG_SIZE];
	static char server_job_ret[JS_JOBSCHED_MAX_FULL_JOB_ID_LEN + 1];
	binn *client_request_obj = binn_open (client_request);
	
	if (!client_request_obj) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "invalid client request object received from client");
		return;
	}
	
	char *decoded_client_request = binn_object_str (client_request_obj,
	                                        JS_CMD_STRN);
	#ifndef NO_FULL_CHECKS
	                                        
	if (!decoded_client_request) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "failed to decode client request object");
		binn_free (client_request_obj);
		return;
	}
	
	#endif
	binn *server_response_obj = binn_object();
	
	if (server_response_obj == NULL) {
		DEBUG_NOW (REPORT_ERRORS, SCHED,
		           "cannot allocate server response object");
		binn_free (client_request_obj);
		return;
	}
	
	// from this point onwards, send an 'empty' reply on error...
	int sro_sz;
	
	if (!strcmp (decoded_client_request, JS_CMD_SUBMIT_JOB)) {
		wnp_job job = binn_object_blob (client_request_obj, JS_CMD_DATA, NULL);
		#ifndef NO_FULL_CHECKS
		
		if (!job) {
			DEBUG_NOW (REPORT_ERRORS, SCHED,
			           "failed to decode job from client request object");
			goto send_server_reply;
		}
		
		#endif
		const char *job_bin_fn = binn_object_str (client_request_obj,
		                                        JS_CMD_BIN_EXE_FPATH);
		#ifndef NO_FULL_CHECKS
		                                        
		if (!job_bin_fn) {
			DEBUG_NOW (REPORT_ERRORS, SCHED,
			           "failed to decode job binary executable filename from client request object");
			goto send_server_reply;
		}
		
		#endif
		
		if (!submit_job (job, job_bin_fn, server_job_ret)) {
			DEBUG_NOW (REPORT_ERRORS, SCHED,
			           "failed to submit job");
			goto send_server_reply;
		}
		
		binn_object_set_str (server_response_obj, JS_DATA_FULL_JOB_ID, server_job_ret);
	}
	
	else
		if (!strcmp (decoded_client_request, JS_CMD_GET_JOB_STATUS)) {
			char *full_job_id = (char *)binn_object_blob (client_request_obj, JS_CMD_DATA,
			                                        NULL);
			#ifndef NO_FULL_CHECKS
			                                        
			if (!full_job_id) {
				DEBUG_NOW (REPORT_ERRORS, SCHED,
				           "could not decode job id from client request object");
				goto send_server_reply;
			}
			
			#endif
			JS_JOB_STATUS job_status;
			
			if (!get_job_status (full_job_id, &job_status)) {
				DEBUG_NOW (REPORT_ERRORS, SCHED,
				           "failed to get job status from client request object");
				goto send_server_reply;
			}
			
			binn_object_set_uint8 (server_response_obj, JS_DATA_JOB_STATUS, job_status);
		}
		
		else
			if (!strcmp (decoded_client_request, JS_CMD_DEL_JOB)) {
				char *full_job_id = (char *)binn_object_blob (client_request_obj, JS_CMD_DATA,
				                                        NULL);
				#ifndef NO_FULL_CHECKS
				                                        
				if (!full_job_id) {
					DEBUG_NOW (REPORT_ERRORS, SCHED,
					           "could not decode job id from client request object");
					goto send_server_reply;
				}
				
				#endif
				int32_t deljob_ret_val;
				
				if (!del_job (full_job_id, &deljob_ret_val) || deljob_ret_val != PBSE_NONE) {
					DEBUG_NOW (REPORT_ERRORS, SCHED, "failed to delete job");
					// for del_job, need to send back deljob_ret_val even on failure
				}
				
				binn_object_set_int32 (server_response_obj, JS_DATA_RETURN_VALUE,
				                       deljob_ret_val);
			}
			
			else
				if (!strcmp (decoded_client_request, JS_CMD_GET_NODE_INFO)) {
					int16_t num_up_nodes, num_up_procs, num_free_nodes, num_free_procs;
					
					if (!get_node_info (&num_up_nodes, &num_up_procs, &num_free_nodes,
					                    &num_free_procs) ||
					    num_up_nodes < 0) {
						DEBUG_NOW (REPORT_ERRORS, SCHED, "could not get node info");
						goto send_server_reply;
					}
					
					binn_object_set_int16 (server_response_obj, JS_DATA_NUM_UP_NODES, num_up_nodes);
					binn_object_set_int16 (server_response_obj, JS_DATA_NUM_UP_PROCS, num_up_procs);
					binn_object_set_int16 (server_response_obj, JS_DATA_NUM_FREE_NODES,
					                       num_free_nodes);
					binn_object_set_int16 (server_response_obj, JS_DATA_NUM_FREE_PROCS,
					                       num_free_procs);
				}
				
send_server_reply:
	sro_sz = binn_size (server_response_obj);
	
	if (JS_MSG_SIZE < sro_sz) {
		DEBUG_NOW2 (REPORT_WARNINGS, SCHED,
		            "JS_MSG_SIZE (%d) < server_response_obj size (%d). truncating response to JS_MSG_SIZE",
		            JS_MSG_SIZE, sro_sz);
		g_memcpy (server_response, binn_ptr (server_response_obj), JS_MSG_SIZE);
	}
	
	else {
		if (JS_MSG_SIZE == sro_sz) {
			g_memcpy (server_response, binn_ptr (server_response_obj), JS_MSG_SIZE);
		}
		
		else {
			g_memcpy (server_response, binn_ptr (server_response_obj), (size_t) sro_sz);
			g_memset (server_response + sro_sz, 0,
			          (size_t) JS_MSG_SIZE -
			          sro_sz);                 // initialize/pad remaining bytes
		}
	}
	
	send (js_server_client_socket, server_response, JS_MSG_SIZE, 0);
	binn_free (client_request_obj);
	binn_free (server_response_obj);
}

void process_server_connection() {
	static uchar js_server_socket_recv_buf[JS_MSG_SIZE],
	       js_server_socket_pending_buf[JS_MSG_SIZE];
	static int num_bytes_read;
	REGISTER bool received_shutdown_signal;
	#ifdef _WIN32
	REGISTER SOCKET js_server_win_client_socket;
	#else
	REGISTER int js_server_unix_client_socket;
	static fd_set select_read_fds;
	static struct timeval select_tv;
	select_tv.tv_sec = 0;
	select_tv.tv_usec =
	                    0;              // select should return immediately. we use sleep_ms (JS_SERVER_SOCKET_SLEEP_MS) in while loop below
	int select_retval;
	#endif
	
	while (1) {
		/*
		 * wait for incoming connection
		 */
		while (1) {
			#ifdef _WIN32
		
			if ((js_server_win_client_socket = accept (js_win_listen_socket, NULL,
			                                        NULL)) == INVALID_SOCKET) {
				if (WSAGetLastError() != WSAEWOULDBLOCK) {
					DEBUG_NOW (REPORT_ERRORS, SCHED,
					           "error when accepting connection");
					return;
				}
			}
			
			else {
				break;  // connected
			}
			
			#else
			
			if ((js_server_unix_client_socket = accept (js_unix_listen_socket_fd, NULL,
			                                        NULL)) < 0) {
				if (errno != EWOULDBLOCK) {
					DEBUG_NOW (REPORT_ERRORS, SCHED,
					           "error when accepting connection");
					return;
				}
			}
			
			else {
				break;  // connected
			}
			
			#endif
			received_shutdown_signal = scheduler_server_shutting_down;
			
			if (received_shutdown_signal) {
				#ifdef _WIN32
				shutdown (js_server_win_client_socket, SD_BOTH);
				#else
				shutdown (js_server_unix_client_socket, SHUT_RDWR);
				#endif
				return;
			}
			
			sleep_ms (JS_SERVER_SOCKET_SLEEP_MS);
		}
		
		/*
		 * process incoming connection
		 */
		REGISTER bool have_part_message = false;
		REGISTER int previous_size = 0, j;
		
		while (1) {
			REGISTER bool nonblocking_retry;
			#ifdef _WIN32
			
			if ((num_bytes_read = recv (js_server_win_client_socket,
			                            (char *) js_server_socket_recv_buf, JS_MSG_SIZE, 0)) <= 0) {
				if (WSAGetLastError() != WSAEWOULDBLOCK) {
					shutdown (js_server_win_client_socket, SD_BOTH);
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
			FD_SET (js_server_unix_client_socket, &select_read_fds);
			select_retval = select (js_server_unix_client_socket + 1, &select_read_fds,
			                        NULL, NULL, &select_tv);
			
			if (select_retval < 0) {
				DEBUG_NOW (REPORT_ERRORS, SCHED,
				           "error with select() from socket");
				shutdown (js_server_unix_client_socket, SHUT_RDWR);
				return;
			}
			
			else
				if (select_retval) {
					if ((num_bytes_read = read (js_server_unix_client_socket,
					                            js_server_socket_recv_buf, JS_MSG_SIZE)) <= 0) {
						shutdown (js_server_unix_client_socket, SHUT_RDWR);
						break;  // client disconnected - restart
					}
			
					nonblocking_retry = false;
				}
			
				else {
					nonblocking_retry = true;   // select() returned 0 fds -> retry
				}
			
			#endif
			
			if (nonblocking_retry) {
				received_shutdown_signal = scheduler_server_shutting_down;
				
				if (received_shutdown_signal) {
					#ifdef _WIN32
					shutdown (js_server_win_client_socket, SD_BOTH);
					#else
					shutdown (js_server_unix_client_socket, SHUT_RDWR);
					#endif
					return;
				}
				
				sleep_ms (JS_SERVER_SOCKET_SLEEP_MS);
			}
			
			else {
				if (have_part_message) {
					if (previous_size + num_bytes_read <= JS_MSG_SIZE) {
						for (j = previous_size; j < previous_size + num_bytes_read; j++) {
							js_server_socket_pending_buf[j] = js_server_socket_recv_buf[j - previous_size];
						}
						
						if (previous_size + num_bytes_read == JS_MSG_SIZE) {
							#ifdef _WIN32
							process_client_request (js_server_socket_pending_buf,
							                        js_server_win_client_socket);
							#else
							process_client_request (js_server_socket_pending_buf,
							                        js_server_unix_client_socket);
							#endif
							have_part_message = false;
						}
						
						else {
							previous_size += num_bytes_read;
						}
					}
					
					else {
						for (j = previous_size; j < JS_MSG_SIZE; j++) {
							js_server_socket_pending_buf[j] = js_server_socket_recv_buf[j - previous_size];
						}
						
						#ifdef _WIN32
						process_client_request (js_server_socket_pending_buf,
						                        js_server_win_client_socket);
						#else
						process_client_request (js_server_socket_pending_buf,
						                        js_server_unix_client_socket);
						#endif
						                        
						for (j = JS_MSG_SIZE - previous_size; j < num_bytes_read; j++) {
							js_server_socket_pending_buf[j - (JS_MSG_SIZE - previous_size)] =
							                    js_server_socket_recv_buf[j];
						}
						
						previous_size = num_bytes_read - (JS_MSG_SIZE - previous_size);
					}
					
					continue;
				}
				
				if (num_bytes_read == JS_MSG_SIZE) {
					#ifdef _WIN32
					process_client_request (js_server_socket_recv_buf, js_server_win_client_socket);
					#else
					process_client_request (js_server_socket_recv_buf,
					                        js_server_unix_client_socket);
					#endif
				}
				
				else {
					for (j = 0; j < num_bytes_read; j++) {
						js_server_socket_pending_buf[j] = js_server_socket_recv_buf[j];
					}
					
					previous_size = num_bytes_read;
					have_part_message = true;
				}
			}
		}
	}
}

#if JS_JOBSCHED_TYPE==JS_TORQUE
bool scheduler_interface (long port, char *headnode_server) {
	bool success = false;

        if (!initialize_utils ()) {
                printf ("cannot initialize utils for scheduling interface\n"); fflush (stdout);
		return false;
        }
	
	if (setup_SIGINT_handler()) {
		DEBUG_NOW (REPORT_INFO, SCHED,
		           "initializing job scheduling interface");
		           
		if (initialize_jobsched_server (headnode_server)) {
			DEBUG_NOW (REPORT_INFO, SCHED,
			           "initializing job scheduling server socket");
			           
			if (initialize_js_server_socket (port)) {
				process_server_connection();
				finalize_js_server_socket();
				success = true;
			}
			
			else {
				DEBUG_NOW (REPORT_ERRORS, SCHED,
				           "failed to initialize job scheduling server socket");
			}
			
			finalize_jobsched_server();
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, SCHED,
			           "failed to initialize job scheduling interface");
		}
	}

	finalize_utils ();

	return success;
}
#elif JS_JOBSCHED_TYPE==JS_SLURM
bool scheduler_interface (long port) {
	bool success = false;
	
        if (!initialize_utils ()) {
                printf ("cannot initialize utils for scheduling interface\n"); fflush (stdout);
		return false;
        }

	if (setup_SIGINT_handler()) {
		DEBUG_NOW (REPORT_INFO, SCHED,
		           "initializing job scheduling interface");
		           
		if (initialize_jobsched_server()) {
			DEBUG_NOW (REPORT_INFO, SCHED,
			           "initializing job scheduling server socket");
			           
			if (initialize_js_server_socket (port)) {
				process_server_connection();
				finalize_js_server_socket();
				success = true;
			}
			
			finalize_jobsched_server();
		}
	}

	finalize_utils ();

	return success;
}
#else
bool scheduler_interface() {
        if (!initialize_utils ()) {
                printf ("cannot initialize utils for scheduling interface\n"); fflush (stdout);
		return false;
        }

	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "unsupported job scheduler interface");

	finalize_utils ();

	return false;
}
#endif

#endif  // JS_JOBSCHED_TYPE
