#if JS_JOBSCHED_TYPE!=JS_NONE
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <mpi.h>
#include <time.h>
#include "c_jobsched_client.h"
#include "allocate.h"
#include "interface.h"
#include "frontend.h"

// signal for allocator shutting down state
static bool allocate_shutting_down = false;
// synchronize allocation activities
static pthread_spinlock_t allocate_spinlock;
// threads for worker allocation and worker status update, respectively
static pthread_t allocate_thread, update_thread;
// indicators for cluster resource availability
static int32_t num_up_nodes = 0, num_up_procs = 0, num_free_nodes = 0,
               num_free_procs = 0;

#define ALLOCATE_JOB_STATUS_RETRY_MS            200         // sleep duration between consecutive job status query retries
#define ALLOCATE_JOB_STATUS_MAX_ATTEMPTS        2      	    // maximum number of job status check retries (works w/ALLOCATE_JOB_STATUS_RETRY_MS); limited by uchar
#define ALLOCATE_THREAD_SLEEP_S                 30 // 1     // secs in between allocate_thread checks/allocation iterations
#define ALLOCATE_THREAD_NODE_INFO_UPDATE_STEP   1  // 6     // number of iterations (based on ALLOCATE_THREAD_SLEEP_S) between get_node_info updates
#define ALLOCATE_WORKER_SURPLUS                 8           // maintain ALLOCATE_WORKER_SURPLUS == num_allocated_workers-num_active_workers
#define ALLOCATE_WORKER_MAX_ATTEMPTS            2
#define ALLOCATE_WORKER_ATTEMPT_RETRY_MS        100 // 10
#define ALLOCATE_MAX_WORKERS                    8           // limited by uchar
#define UPDATE_THREAD_SLEEP_MS                  10          // number of ms for comms update between update_thread and worker node jobs

#define WORKER_STATUS_NOT_AVAILABLE            -1           // this worker is not available (not currently running on any worker node)

#define ALLOCATE_LOCK_S    if (pthread_spin_lock (&allocate_spinlock)) { DEBUG_NOW (REPORT_ERRORS, ALLOCATE, "could not acquire allocate spin lock"); } else {
#define ALLOCATE_LOCK_E    if (pthread_spin_unlock (&allocate_spinlock)) { DEBUG_NOW (REPORT_ERRORS, ALLOCATE, "could not release allocate spin lock"); } }

// number of scan jobs submitted to MPI and in running status (WORKER_STATUS_AVAILABLE)
static uchar  num_available_workers = 0,
              // number of jobs in running status and actively performing a scan job (WORKER_STATUS_ACTIVE)
              num_active_workers = 0;

/*
 * track intercommunicators for all available workers;
 * each worker communicates exclusively with dispatch
 * via a unique intercommunicator
 */
static MPI_Comm
workers[ALLOCATE_MAX_WORKERS];
// track status (WORKER_STATUS_NOT_AVAILABLE/WORKER_STATUS_AVAILABLE/WORKER_STATUS_ACTIVE)
static int
worker_status[ALLOCATE_MAX_WORKERS];
// track job ids per worker
static
ds_object_id_field worker_job_id[ALLOCATE_MAX_WORKERS];
// track MPI requests per worker
static MPI_Request worker_mpi_request[ALLOCATE_MAX_WORKERS];
// MPI receive buffers per worker
static uchar worker_mpi_recv_buffer[ALLOCATE_MAX_WORKERS][WORKER_MSG_SZ];
// job name per worker
static char
worker_mpi_job_name[ALLOCATE_MAX_WORKERS][JS_JOBSCHED_MAX_FULL_JOB_ID_LEN + 1];
// last known ping time
static time_t worker_mpi_job_ping_time[ALLOCATE_MAX_WORKERS];
// job allocation time
static time_t worker_mpi_job_alloc_time[ALLOCATE_MAX_WORKERS];

static int last_allocated_worker = -1;

/*
 * by default scan workers launch the same binary executable
 * as the one run from the command line; optionally, an
 * alterantive version can be specified for debugging
 */
static char worker_scan_bin_fn[MAX_FILENAME_LENGTH + 1];

/*
 * static, inline replacement for memcpy - silences google sanitizers
 */
static inline void g_memcpy (void *p, const void *r, const int len) {
	REGISTER
	char *pc = (char *)p, *rc = (char *)r;
	
	for (REGISTER int i = 0; i < len; i++) {
		pc[i] = rc[i];
	}
}

static inline void update_node_info() {
	void *server_response = NULL;
	
	if (!js_execute (JS_CMD_GET_NODE_INFO, NULL, 0, NULL, &server_response) ||
	    (server_response == NULL)) {
		DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
		           "js_execute call for JS_CMD_GET_NODE_INFO failed");
	}
	
	else {
		num_up_nodes = ((jsp_node_info)server_response)->num_up_nodes;
		num_up_procs = ((jsp_node_info)server_response)->num_up_procs;
		num_free_nodes = ((jsp_node_info)server_response)->num_free_nodes;
		num_free_procs = ((jsp_node_info)server_response)->num_free_procs;
	}
	
	if (server_response) {
		free (server_response);
	}
}

static inline bool launch_worker (MPI_Comm *new_worker_comm,
                                  char **new_worker_mpi_name) {
	void *server_response = NULL;
	*new_worker_mpi_name = NULL;
	char port_name[MPI_MAX_PORT_NAME];

	if (MPI_SUCCESS != MPI_Open_port (MPI_INFO_NULL, port_name)) {
		DEBUG_NOW (REPORT_ERRORS, ALLOCATE, "failed to open MPI port");
		return false;
	}

	// launch job based on port name assigned by ompi_server
	wn_job mpi_wn_job;
	size_t port_name_len = strlen (port_name);
	g_memcpy (mpi_wn_job.mpi_port_name, port_name, port_name_len);
	mpi_wn_job.mpi_port_name[port_name_len] = '\0';
	
	if (!js_execute (JS_CMD_SUBMIT_JOB, &mpi_wn_job, sizeof (mpi_wn_job),
	                 worker_scan_bin_fn, &server_response) || (server_response == NULL)) {
		DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
		           "js_execute call for JS_CMD_SUBMIT_JOB failed");
		MPI_Close_port (port_name);
		
		if (server_response) {
			free (server_response);
		}
		
		return false;
	}
	
	void *job_status_server_response = NULL;
	bool check_again;
	uchar num_retries = ALLOCATE_JOB_STATUS_MAX_ATTEMPTS;

	do {
		// always allow for some time before next job status check
		sleep_ms (ALLOCATE_JOB_STATUS_RETRY_MS);

		if (!js_execute (JS_CMD_GET_JOB_STATUS, server_response,
		                 strlen (server_response), NULL, &job_status_server_response) ||
		    (job_status_server_response == NULL)) {
			DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
			           "js_execute call for JS_CMD_GET_JOB_STATUS failed");
			MPI_Close_port (port_name);
			
			if (job_status_server_response) {
				free (job_status_server_response);
			}
			
			free (server_response);
			return false;
		}

		check_again = * (JS_JOB_STATUS *)job_status_server_response !=
		              JS_JOBS_STATUS_RUNNING;
		free (job_status_server_response);
		
		if (!check_again) {
			break;
		}
	}
	while (--num_retries > 0);

	if (num_retries < 0 ||
	    (MPI_SUCCESS != MPI_Comm_accept (port_name, MPI_INFO_NULL, 0, MPI_COMM_SELF,
	                                     new_worker_comm))) {
		// failed to get JS_JOBS_STATUS_RUNNING status in ALLOCATE_JOB_STATUS_RETRY_MS*ALLOCATE_JOB_STATUS_MAX_ATTEMPTS,
		// OR could not establish connection over port_name -- try to kill job, and fail...
		DEBUG_NOW (REPORT_ERRORS, ALLOCATE, "failed to connect to worker node");
		DEBUG_NOW (REPORT_INFO, ALLOCATE, "deleting job");
		void *deljob_ret_val = NULL;
		
		if (!js_execute (JS_CMD_DEL_JOB, server_response, strlen (server_response),
		                 NULL, &deljob_ret_val) ||
		    (deljob_ret_val == NULL) ||
	    #if JS_JOBSCHED_TYPE==JS_TORQUE
		    (PBSE_NONE != * (int *)deljob_ret_val)
	    #elif JS_JOBSCHED_TYPE==JS_SLURM
		    (SLURM_SUCCESS != * (int *)deljob_ret_val)
	    #endif
		   ) {
			DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
			           "js_execute call for JS_CMD_DEL_JOB failed");
			MPI_Close_port (port_name);
			
			if (deljob_ret_val) {
				free (deljob_ret_val);
			}
			
			free (server_response);
			return false;
		}
		
		if (deljob_ret_val) {
			free (deljob_ret_val);
		}
		
		free (server_response);
		MPI_Close_port (port_name);
		return false;
	}

	*new_worker_mpi_name = server_response;
	MPI_Close_port (port_name);
	return true;
}

/*
 * allocate_thread_start:
 *          resource allocator thread that monitors work resources available
 *          and takes action based on ALLOCATE_WORKER_SURPLUS, ALLOCATE_MAX_WORKERS
 *          configuration settings and the current values of num_available_workers,
 *          num_active_workers
 */
static void *allocate_thread_start (void *arg) {
	REGISTER bool received_shutdown_signal = false, should_allocate = false,
	              success;
	REGISTER uchar niu_step, target_worker_idx;
	MPI_Comm new_worker_comm = (MPI_Comm) NULL;
	char *new_worker_mpi_name = NULL;
	time_t this_time;
	
	do {
		niu_step = ALLOCATE_THREAD_NODE_INFO_UPDATE_STEP;
		// get an update on cluster resources
		update_node_info();
		
		do {
			success = false;
			ALLOCATE_LOCK_S
			received_shutdown_signal = allocate_shutting_down;
			// allocate only when resources are available and surplus needed
			should_allocate = !received_shutdown_signal &&
			                  (ALLOCATE_WORKER_SURPLUS > num_available_workers - num_active_workers) &&
			                  num_free_procs && (ALLOCATE_MAX_WORKERS > num_available_workers);
			                  
			if (should_allocate) {
				DEBUG_NOW4 (REPORT_INFO, ALLOCATE,
				            "workers active %d, available %d, max %d, surplus %d",
				            num_active_workers, num_available_workers, ALLOCATE_MAX_WORKERS,
				            ALLOCATE_WORKER_SURPLUS);
				DEBUG_NOW1 (REPORT_INFO, ALLOCATE, "launching worker #%d",
				            num_available_workers + 1);
				target_worker_idx = ALLOCATE_MAX_WORKERS;
				
				for (REGISTER uchar i = 0; i < ALLOCATE_MAX_WORKERS; i++) {
					if ((MPI_Comm) NULL == workers[i]) {      // MPI_COMM_NULL
						target_worker_idx = i;
						break;
					}
				}
				
				if (ALLOCATE_MAX_WORKERS == target_worker_idx) {
					DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
					           "inconsistent state in allocation thread. shutting down...");
					allocate_shutting_down = true;
					received_shutdown_signal = true;
				}
				
				else {
					this_time = time (NULL);
					
					if (this_time) {
						success = launch_worker (&new_worker_comm, &new_worker_mpi_name) &&
						          (MPI_Comm)NULL != new_worker_comm && NULL != new_worker_mpi_name;
					}
				}
				
				if (success) {
					workers[target_worker_idx] = new_worker_comm;
					worker_status[target_worker_idx] = WORKER_STATUS_AVAILABLE;
					worker_job_id[target_worker_idx][0] = 0;
					size_t mn_len = strlen (new_worker_mpi_name);
					g_memcpy (worker_mpi_job_name[target_worker_idx], new_worker_mpi_name, mn_len);
					worker_mpi_job_name[target_worker_idx][mn_len] = '\0';
					worker_mpi_job_alloc_time[target_worker_idx] = this_time;
					// start with last known ping time == alloc time
					worker_mpi_job_ping_time[target_worker_idx] = this_time;
					free (new_worker_mpi_name);
					num_available_workers++;
				}
				
				else {
					DEBUG_NOW (REPORT_ERRORS, ALLOCATE, "failed to launch worker");
				}
			}
			
			ALLOCATE_LOCK_E
			
			if (!received_shutdown_signal) {
				sleep (ALLOCATE_THREAD_SLEEP_S);
				
				if (success) {
					update_node_info();
				}
			}
		}
		while (--niu_step && !received_shutdown_signal);
	}
	while (!received_shutdown_signal);
	
	DEBUG_NOW (REPORT_INFO, ALLOCATE,
	           "shutdown signal received in allocation thread. exiting...");
	ALLOCATE_LOCK_S
	void *deljob_ret_val = NULL;
	
	for (uchar i = 0; i < ALLOCATE_MAX_WORKERS; i++) {
		if ((MPI_Comm) NULL != workers[i] &&
		    WORKER_STATUS_NOT_AVAILABLE != worker_status[i]) {
			if (WORKER_STATUS_AVAILABLE == worker_status[i]) {
				// if worker status is available, send shutdown signal; otherwise have to kill worker
				unsigned short d_msg[DISPATCH_MSG_SZ];            // todo: fix ushort type issue
				d_msg[0] = DISPATCH_MSG_SHUTDOWN;
				d_msg[1] = 0;
				DEBUG_NOW1 (REPORT_INFO, ALLOCATE, "sending shutdown message to worker %d...",
				            i);
				            
				if (MPI_SUCCESS != MPI_Send (d_msg, DISPATCH_MSG_SZ, DISPATCH_MSG_MPI_TYPE, 0,
				                             0, workers[i]) ||
				    MPI_SUCCESS != MPI_Comm_disconnect (&workers[i])) {
					DEBUG_NOW1 (REPORT_ERRORS, ALLOCATE,
					            "could not send shutdown message to worker %d. killing job...",
					            i);
				}
				
				else {
					continue;
				}
			}
			
			DEBUG_NOW2 (REPORT_INFO, ALLOCATE,
			            "deleting job \"%s\" for worker idx %d",
			            worker_mpi_job_name[i], i);
			            
			if (!js_execute (JS_CMD_DEL_JOB, worker_mpi_job_name[i],
			                 strlen (worker_mpi_job_name[i]),
			                 NULL, &deljob_ret_val) || (deljob_ret_val == NULL) ||
		    #if JS_JOBSCHED_TYPE==JS_TORQUE
			    (PBSE_NONE != * (int *)deljob_ret_val)
		    #elif JS_JOBSCHED_TYPE==JS_SLURM
			    (SLURM_SUCCESS != * (int *)deljob_ret_val)
		    #endif
			   ) {
				#if JS_JOBSCHED_TYPE==JS_TORQUE
				DEBUG_NOW2 (REPORT_ERRORS, ALLOCATE,
				            "js_execute failed (PBS ERROR CODE %d) for JS_CMD_DEL_JOB and worker idx %d",
				            * (int *)deljob_ret_val, i);
				#elif JS_JOBSCHED_TYPE==JS_SLURM
				DEBUG_NOW2 (REPORT_ERRORS, ALLOCATE,
				            "js_execute failed (SLURM ERROR CODE %d) for JS_CMD_DEL_JOB and worker idx %d",
				            * (int *)deljob_ret_val, i);
				#endif
			}
			
			if (deljob_ret_val) {
				free (deljob_ret_val);
			}
		}
	}
	
	ALLOCATE_LOCK_E
	return NULL;
}

/*
 * update_thread_start:
 *          resource updater thread that monitors the status of individual
 *          workers, retreiving results as they become available and
 *          updating worker status accordingly; any results are enqueued
 *          to the dispatcher
 */
static void *update_thread_start (void *arg) {
	REGISTER bool received_shutdown_signal = false, should_update = false, cont;
	REGISTER uchar active_workers = 0;
	int mpi_test_flag;
	
	do {
		ALLOCATE_LOCK_S
		received_shutdown_signal = allocate_shutting_down;
		active_workers = num_active_workers;
		// only update when worker activity ongoing
		should_update = !received_shutdown_signal && active_workers > 0;
		/*
		 * first priority is to check status of any active workers;
		 * following that, check TTL and status of inactive workers
		 */
		cont = true;
		
		if (should_update) {
			// always start from the oldest worker, unless wrapping around to 0
			uchar curr_worker = ALLOCATE_MAX_WORKERS - 1 <= last_allocated_worker ?
			                    0 : (uchar) (last_allocated_worker + 1);
			uchar num_workers_checked = 0;
			
			while (cont && num_workers_checked < active_workers) {
				if (WORKER_STATUS_ACTIVE == worker_status[curr_worker]) {
					if ((MPI_Comm) NULL == workers[curr_worker]) {
						DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
						           "inconsistent worker state in update thread. sending shutdown signal...");
						allocate_shutting_down = true;
						received_shutdown_signal = allocate_shutting_down;
						break;
					}
				
					if (WORKER_JOB_PING_WAIT_S < time (NULL) - worker_mpi_job_ping_time[curr_worker]) {
                                        	DEBUG_NOW2 (REPORT_INFO, ALLOCATE,
                                                    "ping time exceeded for active worker idx %d (%s). trying to kill job...",
                                                    curr_worker, worker_mpi_job_name[curr_worker]);

						void *deljob_ret_val = NULL;
						
						if (!js_execute (JS_CMD_DEL_JOB, worker_mpi_job_name[curr_worker],
						                 strlen (worker_mpi_job_name[curr_worker]), NULL, &deljob_ret_val) ||
						    (deljob_ret_val == NULL) ||
					    #if JS_JOBSCHED_TYPE==JS_TORQUE
						    (PBSE_NONE != * (int *)deljob_ret_val)
					    #elif JS_JOBSCHED_TYPE==JS_SLURM
						    (SLURM_SUCCESS != * (int *)deljob_ret_val)
					    #endif
						   ) {
							DEBUG_NOW1 (REPORT_WARNINGS, ALLOCATE,
							            "js_execute call failed (%d) for JS_CMD_DEL_JOB",
							            * (int *)deljob_ret_val);
							            
							if (deljob_ret_val) {
								free (deljob_ret_val);
							}
						}
					
						worker_status[curr_worker] = WORKER_STATUS_NOT_AVAILABLE;
						workers[curr_worker] = (MPI_Comm) NULL;
						worker_mpi_request[curr_worker] = (MPI_Request)NULL;
						worker_mpi_job_alloc_time[curr_worker] = 0;
						worker_mpi_job_ping_time[curr_worker] = 0;
						num_available_workers--;
						num_active_workers--;
						active_workers=num_active_workers;

						int job_current_num_windows=0, job_current_num_windows_success=0, job_current_num_windows_failure=0;
						ds_int32_field ref_id;

						static dsp_dataset job_dataset = NULL;
						dis_lock();  // avoid conflicts with allocate (update_thread_start)

						if (read_job (&worker_job_id[curr_worker], &job_dataset) &&
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

							job_current_num_windows = atoi (job_dataset->data[DS_COL_JOB_NUM_WINDOWS_IDX]);
							job_current_num_windows_success = atoi (job_dataset->data[DS_COL_JOB_NUM_WINDOWS_SUCCESS_IDX]);
							job_current_num_windows_failure = atoi (job_dataset->data[DS_COL_JOB_NUM_WINDOWS_FAIL_IDX]);
							ref_id = atoi (job_dataset->data[DS_COL_JOB_REF_ID_IDX]);

							if (update_job_error (&worker_job_id[curr_worker], ref_id, DS_JOB_ERROR_FAIL)) {
								if (update_job_num_windows_fail (&worker_job_id[curr_worker], ref_id,
                                                                    ++job_current_num_windows_failure)) {
			                                        	if (job_current_num_windows == (job_current_num_windows_success+job_current_num_windows_failure)) {
										if (!update_job_status (&worker_job_id[curr_worker], ref_id, DS_JOB_STATUS_DONE)) {
											DEBUG_NOW1 (REPORT_ERRORS, DISPATCH,
												    "failed to update job status for job '%s'",
												    worker_job_id[curr_worker]);
										}
									}
								} else {
									DEBUG_NOW1 (REPORT_ERRORS, ALLOCATE,
                                                                    	"could not update number of failed windows for job '%s'",
                                                                    	worker_job_id[curr_worker]);
								}
							}
							else {
								DEBUG_NOW1 (REPORT_ERRORS, ALLOCATE,
								"could not update error field for job '%s'",
								worker_job_id[curr_worker]);
							}
						}
						else {
							DEBUG_NOW1 (REPORT_ERRORS, ALLOCATE,
								    "failed to read dataset for job '%s'",
								    worker_job_id[curr_worker]);
						}

						dis_unlock ();

						if (job_dataset) {
							free_dataset (job_dataset);
						}

						worker_job_id[curr_worker][0] = 0;
						continue;
					}
	
					// process available workers
					if ((MPI_Request)NULL != worker_mpi_request[curr_worker]) {
						// status update from worker is currently pending: do test again
						mpi_test_flag = 0;
						
						if (MPI_SUCCESS != MPI_Test (&worker_mpi_request[curr_worker], &mpi_test_flag,
						                             MPI_STATUS_IGNORE)) {
							DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
							           "MPI_Test failed in update thread. sending shutdown signal...");
							allocate_shutting_down = true;
							received_shutdown_signal = allocate_shutting_down;
							cont = false;
						}
						
						else {
							if (mpi_test_flag) {
								// have updated status from this worker
								if (WORKER_STATUS_HAS_RESULT == worker_mpi_recv_buffer[curr_worker][0]) {
									REGISTER
									int next_hit_len = worker_mpi_recv_buffer[curr_worker][1];
									// allocate max possible hit message length
									uchar w_msg[DS_JOB_RESULT_HIT_FIELD_LENGTH];
									REGISTER
									bool isFirstHit = true;
									/*
									 * retrieve ref_id and job_id for this (first) hit; this will be used
									 * to signal that the originating window has been processed successfully
									 * right after all hits are iterated in the loop below
									 */
									REGISTER
									ds_int32_field ref_id = INVALID_REF_ID;
									ds_object_id_field hit_job_id;
									// 19 bytes for representing ref_id as string
									char ref_id_tmp[20];
									
									while (next_hit_len) {
										// iterate over next hit
										MPI_Request request;
										int flag;
										
										if (MPI_SUCCESS != MPI_Irecv (w_msg, next_hit_len,
										                              WORKER_MSG_PAYLOAD_TYPE, MPI_ANY_SOURCE, MPI_ANY_TAG,
										                              workers[curr_worker], &request)) {
											DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
											           "MPI receive failed in update thread. sending shutdown signal...");
											allocate_shutting_down = true;
											received_shutdown_signal = allocate_shutting_down;
											cont = false;
										}
										
										else {
											flag = 0;
											
											while (!flag) {
												MPI_Test (&request, &flag, MPI_STATUS_IGNORE);
												sleep_ms (MPI_TEST_WORK_SLEEP_MS);
											}
											
											if (isFirstHit) {
												isFirstHit = false;
												g_memcpy (ref_id_tmp, w_msg, 19);
												ref_id_tmp[19] = '\0';
												ref_id = atoi (ref_id_tmp);
												g_memcpy (hit_job_id, 1 + 19 + w_msg, NUM_RT_BYTES);
											}
											
											if (S_HIT_DATA_LENGTH < next_hit_len) {
												// only enque and write/notify result if actual (non-empty) result
												// don't need (next hit's length in) last char
												char *r_hit = malloc ((size_t)
												                      next_hit_len);
												                      
												if (r_hit) {
													g_memcpy (r_hit, w_msg, (size_t)next_hit_len - 1);
													r_hit[next_hit_len - 1] = '\0';
													
													if (enq_r (r_hit)) {
														next_hit_len = w_msg[next_hit_len - 1];
														continue;
													}
													
													else {
														DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
														           "failed to enq result hit. sending shutdown signal...");
														free (r_hit);
													}
												}
												
												else {
													DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
													           "failed to allocate result hit. sending shutdown signal...");
												}
												
												allocate_shutting_down = true;
												received_shutdown_signal = allocate_shutting_down;
												cont = false;
											}
										}
										
										break;
									}
									
									ds_int32_field num_windows = 0, num_windows_success = 0, num_windows_fail = 0;
									dis_lock(); 	// prevent dispatch-allocation conflicts
									
									if (!read_job_windows (&hit_job_id, &num_windows, &num_windows_success,
									                       &num_windows_fail) ||
									    !update_job_num_windows_success (&hit_job_id, ref_id, ++num_windows_success)) {
										DEBUG_NOW (REPORT_ERRORS, ALLOCATE, "failed to update job window fields");
									}
									
									else
										if (num_windows == (num_windows_success + num_windows_fail)) {
											/*
											 * NOTE: only update job status to DONE when the number of successful and failed processed windows
											 *       are together equal to the number of windows submitted by filter (thread(s)) AND
											 *       ALL windows have been submitted by any filter_threads (which is when status is SUBMITTED)
											 */
											ds_int32_field current_status;
											
											if (!read_job_status (&hit_job_id, ref_id, &current_status)) {
												DEBUG_NOW (REPORT_ERRORS, ALLOCATE, "failed to read job status");
											}
											
											else
												if (DS_JOB_STATUS_SUBMITTED == current_status) {
													if (!update_job_status (&hit_job_id, ref_id, DS_JOB_STATUS_DONE)) {
														DEBUG_NOW (REPORT_ERRORS, ALLOCATE, "failed to update job status");
													}
												}
										}
										
									dis_unlock();
									num_active_workers--;
									
									/*
									 * now that curr_worker has just completed its latest task,
									 * check its TTL before making it available again
									 */
									if (WORKER_JOB_TTL_S < time (NULL) - worker_mpi_job_alloc_time[curr_worker]) {
										DEBUG_NOW2 (REPORT_INFO, ALLOCATE,
										            "TTL for worker idx %d (%s) exceeded. shutting down job...",
										            curr_worker, worker_mpi_job_name[curr_worker]);
										unsigned short d_msg[DISPATCH_MSG_SZ];
										d_msg[0] = DISPATCH_MSG_SHUTDOWN;
										d_msg[1] = 0;
										
										// should be safe to call (blocking) MPI_Comm_disconnect here, having successfully sent DISPATCH_MSG_SHUTDOWN
										if (MPI_SUCCESS != MPI_Send (d_msg, DISPATCH_MSG_SZ, DISPATCH_MSG_MPI_TYPE, 0,
										                             0, workers[curr_worker]) ||
										    MPI_SUCCESS != MPI_Comm_disconnect (&workers[curr_worker])) {
											DEBUG_NOW2 (REPORT_ERRORS, ALLOCATE,
											            "could not send shutdown message to worker idx %d (%s) in update thread. killing job...",
											            curr_worker, worker_mpi_job_name[curr_worker]);
											void *deljob_ret_val = NULL;
											
											if (!js_execute (JS_CMD_DEL_JOB, worker_mpi_job_name[curr_worker],
											                 strlen (worker_mpi_job_name[curr_worker]), NULL, &deljob_ret_val) ||
											    (deljob_ret_val == NULL) ||
										    #if JS_JOBSCHED_TYPE==JS_TORQUE
											    (PBSE_NONE != * (int *) deljob_ret_val)
										    #elif JS_JOBSCHED_TYPE==JS_SLURM
											    (SLURM_SUCCESS != * (int *)deljob_ret_val)
										    #endif
											   ) {
												DEBUG_NOW1 (REPORT_ERRORS, ALLOCATE,
												            "js_execute failed (%d) for JS_CMD_DEL_JOB",
												            * (int *) deljob_ret_val);
												            
												if (deljob_ret_val) {
													free (deljob_ret_val);
												}
											}
										}
										
										worker_status[curr_worker] = WORKER_STATUS_NOT_AVAILABLE;
										worker_job_id[curr_worker][0] = 0;
										workers[curr_worker] = (MPI_Comm) NULL;
										worker_mpi_request[curr_worker] = (MPI_Request) NULL;
										worker_mpi_job_alloc_time[curr_worker] = 0;
										worker_mpi_job_ping_time[curr_worker] = 0;
										num_available_workers--;
									}
									
									else {
										// free worker
										worker_status[curr_worker] = WORKER_STATUS_AVAILABLE;
                                        					worker_mpi_job_ping_time[curr_worker] = time (NULL);
										worker_job_id[curr_worker][0] = 0;
									}
								}
								
								else {
									DEBUG_NOW1 (REPORT_ERRORS, ALLOCATE,
									            "unknown recv buffer state (%d)", worker_mpi_recv_buffer[curr_worker][0]);
								}
								
								worker_mpi_request[curr_worker] = (MPI_Request)NULL; //MPI_REQUEST_NULL;
							}
						}
					}
					
					else {
						// worker is available, but still need to initiate (status) receive request from it
						if (MPI_SUCCESS != MPI_Irecv (((uchar *)worker_mpi_recv_buffer[curr_worker]),
						                              WORKER_MSG_SZ, WORKER_MSG_MPI_TYPE,
						                              MPI_ANY_SOURCE, MPI_ANY_TAG, workers[curr_worker],
						                              &worker_mpi_request[curr_worker])) {
							DEBUG_NOW1 (REPORT_ERRORS, ALLOCATE,
							            "MPI receive failed. sending shutdown signal...",
							            worker_mpi_recv_buffer[curr_worker][0]);
							allocate_shutting_down = true;
							received_shutdown_signal = allocate_shutting_down;
							cont = false;
						}
					}
					
					last_allocated_worker = curr_worker;
					num_workers_checked++;
				}
				
				else {
					if ((MPI_Comm) NULL != workers[curr_worker] &&
					    WORKER_STATUS_NOT_AVAILABLE == worker_status[curr_worker]) {
						DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
						           "inconsistent worker state. sending shutdown signal...");
						allocate_shutting_down = true;
						received_shutdown_signal = allocate_shutting_down;
						break;
					}
				}
				
				if (ALLOCATE_MAX_WORKERS > curr_worker + 1) {
					curr_worker++;
				}
				
				else {
					// wrap around array until we process all active_workers
					curr_worker = 0;
				}
			}
		}
		
		for (uchar worker_idx = 0; worker_idx < ALLOCATE_MAX_WORKERS; worker_idx++) {
			/*
			 * check TTL and consistency of all AVAILABLE (but not ACTIVE) workers
			 * note: to promote graceful degradation of worker service, we break
			 *       the loop after shutting down/killing a job. this is so that
			 *       terminations are aligned with allocation/update iterations
			 */
			if (WORKER_STATUS_AVAILABLE == worker_status[worker_idx]) {
				if ((MPI_Comm) NULL == workers[worker_idx] ||
				    (MPI_Request) NULL != worker_mpi_request[worker_idx]) {
					DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
					           "inconsistent worker state. sending shutdown signal...");
					allocate_shutting_down = true;
					received_shutdown_signal = allocate_shutting_down;
					break;
				}

				// first check when we last pinged this job; ping again if necessary
				if (WORKER_JOB_PING_WAIT_S < time (NULL) - worker_mpi_job_ping_time[worker_idx]) {
					DEBUG_NOW2 (REPORT_INFO, ALLOCATE,
					            "sending ping to worker idx %d (%s)",
					            worker_idx, worker_mpi_job_name[worker_idx]);
					unsigned short d_msg[DISPATCH_MSG_SZ];
					d_msg[0] = DISPATCH_MSG_PING;
					d_msg[1] = 0;

					MPI_Request request;
					REGISTER
					bool success=true;

					if (MPI_SUCCESS != MPI_Isend (d_msg, DISPATCH_MSG_SZ, DISPATCH_MSG_MPI_TYPE, 0, 0, workers[worker_idx], &request)) {
						success=false;
					}
					else {
						int mpi_test_flag = 0;

						ushort num_attempts=WORKER_JOB_PING_MAX_ATTEMPTS;
						while (0<num_attempts) {
							if (MPI_SUCCESS != MPI_Test (&request, &mpi_test_flag, MPI_STATUS_IGNORE)) {
								success=false;
								break;
							}

							if (mpi_test_flag) {
								DEBUG_NOW2 (REPORT_INFO, ALLOCATE,
									    "ping sent to worker idx %d (%s). now waiting for reply",
									    worker_idx, worker_mpi_job_name[worker_idx]);

								uchar ping_recv_buffer[WORKER_MSG_SZ];
								ping_recv_buffer[0]=WORKER_STATUS_UNDEFINED;

								if (MPI_SUCCESS != MPI_Irecv (((uchar *)ping_recv_buffer),
                                                                              WORKER_MSG_SZ, WORKER_MSG_MPI_TYPE,
                                                                              MPI_ANY_SOURCE, MPI_ANY_TAG, workers[worker_idx],
                                                                              &request)) {
									success=false;
									break;
								}
								else {
									num_attempts=WORKER_JOB_PING_MAX_ATTEMPTS;
									while (0<num_attempts) {
										if (MPI_SUCCESS != MPI_Test (&request, &mpi_test_flag, MPI_STATUS_IGNORE)) {
											success=false;
											break;
										}

										if (mpi_test_flag) {
											success=WORKER_STATUS_AVAILABLE == ping_recv_buffer[0];
											break;
										}
										else {
											if (1<num_attempts) {
												sleep_ms (WORKER_JOB_PING_SLEEP_MS);
												num_attempts--;
											}
											else {
												success=false;
												break;
											}
										}
									}
									break;
								}
							}
							else {
								if (1<num_attempts) {
									sleep_ms (WORKER_JOB_PING_SLEEP_MS);
									num_attempts--;
								}
								else {
									success=false;
									break;
								}
							}
						}
					}

					if (success) {
						DEBUG_NOW2 (REPORT_INFO, ALLOCATE,
							    "received ping back from worker idx %d (%s)",
							    worker_idx, worker_mpi_job_name[worker_idx]);

						// reset last known ping time
						worker_mpi_job_ping_time[worker_idx] = time (NULL);
					}
					else {
						DEBUG_NOW2 (REPORT_ERRORS, ALLOCATE,
						            "could not recieve ping message from worker idx %d (%s). trying to kill job...",
						            worker_idx, worker_mpi_job_name[worker_idx]);
						void *deljob_ret_val = NULL;
						
						if (!js_execute (JS_CMD_DEL_JOB, worker_mpi_job_name[worker_idx],
						                 strlen (worker_mpi_job_name[worker_idx]), NULL, &deljob_ret_val) ||
						    (deljob_ret_val == NULL) ||
					    #if JS_JOBSCHED_TYPE==JS_TORQUE
						    (PBSE_NONE != * (int *)deljob_ret_val)
					    #elif JS_JOBSCHED_TYPE==JS_SLURM
						    (SLURM_SUCCESS != * (int *)deljob_ret_val)
					    #endif
						   ) {
							DEBUG_NOW1 (REPORT_WARNINGS, ALLOCATE,
							            "js_execute call failed (%d) for JS_CMD_DEL_JOB",
							            * (int *)deljob_ret_val);
							            
							if (deljob_ret_val) {
								free (deljob_ret_val);
							}
						}
					
						worker_status[worker_idx] = WORKER_STATUS_NOT_AVAILABLE;
						worker_job_id[worker_idx][0] = 0;
						workers[worker_idx] = (MPI_Comm) NULL;
						worker_mpi_request[worker_idx] = (MPI_Request)NULL;
						worker_mpi_job_alloc_time[worker_idx] = 0;
						worker_mpi_job_ping_time[worker_idx] = 0;
						num_available_workers--;
						break;  
					}
				}

				// then check if TTL has expired; kill job if necessary (and will be re-spawned be allocated thread)
				if (WORKER_JOB_TTL_S < time (NULL) - worker_mpi_job_alloc_time[worker_idx]) {
					DEBUG_NOW2 (REPORT_INFO, ALLOCATE,
					            "TTL for worker idx %d (%s) exceeded. shutting down job...",
					            worker_idx, worker_mpi_job_name[worker_idx]);
					unsigned short d_msg[DISPATCH_MSG_SZ];
					d_msg[0] = DISPATCH_MSG_SHUTDOWN;
					d_msg[1] = 0;
					
					// should be safe to call (blocking) MPI_Comm_disconnect here, having successfully sent DISPATCH_MSG_SHUTDOWN
					if (MPI_SUCCESS != MPI_Send (d_msg, DISPATCH_MSG_SZ, DISPATCH_MSG_MPI_TYPE, 0,
					                             0, workers[worker_idx]) ||
					    MPI_SUCCESS != MPI_Comm_disconnect (&workers[worker_idx])) {
						DEBUG_NOW2 (REPORT_ERRORS, ALLOCATE,
						            "could not send shutdown message to worker idx %d (%s). killing job...",
						            worker_idx, worker_mpi_job_name[worker_idx]);
						void *deljob_ret_val = NULL;
						
						if (!js_execute (JS_CMD_DEL_JOB, worker_mpi_job_name[worker_idx],
						                 strlen (worker_mpi_job_name[worker_idx]), NULL, &deljob_ret_val) ||
						    (deljob_ret_val == NULL) ||
					    #if JS_JOBSCHED_TYPE==JS_TORQUE
						    (PBSE_NONE != * (int *)deljob_ret_val)
					    #elif JS_JOBSCHED_TYPE==JS_SLURM
						    (SLURM_SUCCESS != * (int *)deljob_ret_val)
					    #endif
						   ) {
							DEBUG_NOW1 (REPORT_ERRORS, ALLOCATE,
							            "js_execute call failed (%d) for JS_CMD_DEL_JOB",
							            * (int *)deljob_ret_val);
							            
							if (deljob_ret_val) {
								free (deljob_ret_val);
							}
						}
					}
					
					worker_status[worker_idx] = WORKER_STATUS_NOT_AVAILABLE;
					worker_job_id[worker_idx][0] = 0;
					workers[worker_idx] = (MPI_Comm) NULL;
					worker_mpi_request[worker_idx] = (MPI_Request)NULL;
					worker_mpi_job_alloc_time[worker_idx] = 0;
					worker_mpi_job_ping_time[worker_idx] = 0;
					num_available_workers--;
					break;  // see note above. avoid degradation of worker service availability by shutting down/terminating one job per iteration
				}
			}
		}
		
		ALLOCATE_LOCK_E
		
		if (!received_shutdown_signal) {
			sleep_ms (UPDATE_THREAD_SLEEP_MS);
		}
	}
	while (!received_shutdown_signal);
	
	DEBUG_NOW (REPORT_INFO, ALLOCATE,
	           "shutdown signal received in update thread. exiting...");
	return NULL;
}

/*
 * allocate_scan_job:
 *          given a inbound job, secondary structure, positional variables,
 *          nucleotide sequence string, and a user reference id; allocate
 *          the job to the next available worker
 *
 * args:
 *          cp_job, sequence, secondary structure, and positioanl variable
 *          string, and ref_id
 *
 * returns: boolean success flag
 */
bool allocate_scan_job (cp_job job, char *ss_strn, char *pos_var_strn,
                        char *seq_strn, ds_int32_field ref_id) {
	int allocate_attempts = ALLOCATE_WORKER_MAX_ATTEMPTS;
	uchar target_worker_idx;
	#ifndef NO_FULL_CHECKS
	
	if (!job || !ss_strn || !strlen (ss_strn) || !pos_var_strn ||
	    !strlen (pos_var_strn) || !seq_strn || !strlen (seq_strn)) {
		DEBUG_NOW4 (REPORT_ERRORS, ALLOCATE,
		            "invalid arguments supplied to allocate_scan_job (%p, \"%s\", \"%s\", \"%s\")",
		            job, ss_strn, pos_var_strn, seq_strn);
		return false;
	}
	
	#endif
	
	while (0 < allocate_attempts--) {
		target_worker_idx = ALLOCATE_MAX_WORKERS;
		bool can_allocate = false;
		ALLOCATE_LOCK_S
		// can allocate only when we have excess resources
		can_allocate = !allocate_shutting_down &&
		               (num_available_workers > num_active_workers);
		               
		if (can_allocate) {
			for (REGISTER uchar i = 0; i < ALLOCATE_MAX_WORKERS; i++) {
				if (WORKER_STATUS_AVAILABLE == worker_status[i] &&
				    (MPI_Comm)NULL != workers[i]) {
					target_worker_idx = i;
					break;
				}
			}
		}
		
		ALLOCATE_LOCK_E
		
		if (can_allocate) {
			if (ALLOCATE_MAX_WORKERS == target_worker_idx) {
				DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
				           "inconsistent state when allocating scan job");
				return false;
			}
			
			else {
				unsigned short d_msg[DISPATCH_MSG_SZ];
				d_msg[0] = DISPATCH_MSG_RUN;
				const uchar  ss_strn_len = (uchar) strlen (ss_strn),
				             pos_var_strn_len = (uchar) strlen (pos_var_strn);
				const unsigned short seq_strn_len = (unsigned short) (job->end_posn -
				                                        job->start_posn + 1);
				/*
				 * assumes the model's and therefore the ROI sequence strings' max length
				 * (MAX_MODEL_STRING_LEN) is such that d_msg[1] (ushort) can accommodate
				 * the total length
				 */
				d_msg[1] = (unsigned short) (4 +  // int32 for user ref_id
				                             NUM_RT_BYTES +
				                             2 + ss_strn_len
				                             +
				                             // remaining payload components are preceded by the respective length (2 x uchar)
				                             2 + pos_var_strn_len +
				                             2 + seq_strn_len +
				                             // start posn of this segment wrt original sequence
				                             4);
				// MPI test flag/request - avoid blocking Send here
				MPI_Request request;
				int flag;
				ALLOCATE_LOCK_S
				can_allocate    = (MPI_SUCCESS == MPI_Isend (d_msg, DISPATCH_MSG_SZ,
				                                        DISPATCH_MSG_MPI_TYPE, 0, 0, workers[target_worker_idx], &request)) &&
				                  ((MPI_Comm)NULL != workers[target_worker_idx]);
				                  
				if (can_allocate) {
					flag = 0;
					
					while (!flag) {
						MPI_Test (&request, &flag, MPI_STATUS_IGNORE);
						sleep_ms (MPI_TEST_WORK_SLEEP_MS);
					}
					
					int dp_msg[d_msg[1]];
					REGISTER unsigned short dp_idx = 4 + 2 + NUM_RT_BYTES, i;
					
					// send ref_id
					for (i = 0; i < 4; i++) {
						dp_msg[i] = (int) ((ref_id >> ((3 - i) * 8)) & 0xff);
					}
					
					// send job_id
					for (i = 0; i < NUM_RT_BYTES; i++) {
						dp_msg[4 + i] = (int) job->job_id[i];
					}
					
					dp_msg[4 + NUM_RT_BYTES  ] = (int) (ss_strn_len >> 8 & 0xff);
					dp_msg[4 + NUM_RT_BYTES + 1] = (int) (ss_strn_len    & 0xff);
					
					// send ss_strn
					for (i = 0; i < ss_strn_len; i++) {
						dp_msg[dp_idx++] = (int) ss_strn[i];
					}
					
					dp_msg[dp_idx++] = (int) (pos_var_strn_len >> 8 & 0xff);
					dp_msg[dp_idx++] = (int) (pos_var_strn_len    & 0xff);
					
					// send pos_var_strn
					for (i = 0; i < pos_var_strn_len; i++) {
						dp_msg[dp_idx++] = (int) pos_var_strn[i];
					}
					
					dp_msg[dp_idx++] = (int) (seq_strn_len >> 8 & 0xff);
					dp_msg[dp_idx++] = (int) (seq_strn_len    & 0xff);
					
					// send seq strn
					for (nt_abs_seq_posn s = job->start_posn - 1; s < job->end_posn; s++) {
						dp_msg[dp_idx++] = (int) seq_strn[s];
					}
					
					// send start posn (1-indexed)
					dp_msg[dp_idx++] = (int) (job->start_posn >> 24 & 0xff);
					dp_msg[dp_idx++] = (int) (job->start_posn >> 16 & 0xff);
					dp_msg[dp_idx++] = (int) (job->start_posn >> 8  & 0xff);
					dp_msg[dp_idx  ] = (int) (job->start_posn     & 0xff);
					can_allocate = (MPI_SUCCESS == MPI_Isend (dp_msg, d_msg[1],
					                                        DISPATCH_MSG_PAYLOAD_TYPE, 0, 0, workers[target_worker_idx], &request));
					                                        
					if (can_allocate) {
						flag = 0;
						
						while (!flag) {
							MPI_Test (&request, &flag, MPI_STATUS_IGNORE);
							sleep_ms (MPI_TEST_WORK_SLEEP_MS);
						}
						
						worker_status[target_worker_idx] = WORKER_STATUS_ACTIVE;
                                        	worker_mpi_job_ping_time[target_worker_idx] = time (NULL);

						for (i = 0; i < NUM_RT_BYTES; i++) {
							worker_job_id[target_worker_idx][i] = (int) job->job_id[i];
						}

						num_active_workers++;
					}
					
					else {
						allocate_attempts = 0; // if inner send fails -> cannot retry
					}
				}
				
				ALLOCATE_LOCK_E
				
				if (can_allocate) {
					break;
				}
				
				else {
					DEBUG_NOW2 (REPORT_ERRORS, ALLOCATE,
					            "could not send payload to target worker #%d after attempt #%d",
					            target_worker_idx, ALLOCATE_WORKER_MAX_ATTEMPTS - allocate_attempts);
				}
			}
		}
		
		// cannot allocate; retry
		if (allocate_attempts) {
			sleep_ms (ALLOCATE_WORKER_ATTEMPT_RETRY_MS);
		}
	}
	
	return allocate_attempts >= 0;
}

bool initialize_allocate (char *si_server, unsigned short si_port,
                          const char *scan_bin_fn) {
	if (!scan_bin_fn) {
		DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
		           "NULL scan binary filename supplied");
		return false;
	}
	
	size_t sbf_len = strlen (scan_bin_fn);
	
	if (sbf_len > MAX_FILENAME_LENGTH) {
		DEBUG_NOW1 (REPORT_ERRORS, ALLOCATE,
		            "scan binary filename exceeds MAX_FILENAME_LENGTH (%d)",
		            MAX_FILENAME_LENGTH);
		return false;
	}
	
	else
		if (!sbf_len) {
			DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
			           "scan binary filename is NULL");
			return false;
		}
		
	g_memcpy (worker_scan_bin_fn, scan_bin_fn, sbf_len);
	worker_scan_bin_fn[sbf_len] = '\0';
	num_available_workers = 0;
	num_active_workers = 0;
	last_allocated_worker = -1;
	
	for (uchar i = 0; i < ALLOCATE_MAX_WORKERS; i++) {
		worker_status[i] = WORKER_STATUS_NOT_AVAILABLE;
		worker_job_id[i][0] = 0;
		workers[i] = (MPI_Comm) NULL;
		worker_mpi_request[i] = (MPI_Request)NULL;
		worker_mpi_job_alloc_time[i] = 0;
		worker_mpi_job_ping_time[i] = 0;
	}
	
	DEBUG_NOW (REPORT_INFO, ALLOCATE,
	           "initializing MPI execution environment");
	           
	if (MPI_SUCCESS != MPI_Init (NULL, NULL)) {
		DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
		           "failed to initialize MPI execution environment");
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, ALLOCATE,
	           "initializing job scheduler client");
	           
	if (!initialize_jobsched_client (si_server, si_port)) {
		DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
		           "could not initialize job scheduler client");
		DEBUG_NOW (REPORT_INFO, ALLOCATE,
		           "finalizing MPI execution environment");
		MPI_Finalize();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, ALLOCATE,
	           "initializing allocate spinlock");
	           
	if (pthread_spin_init (&allocate_spinlock, PTHREAD_PROCESS_PRIVATE)) {
		DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
		           "could not initialize allocate spinlock");
		DEBUG_NOW (REPORT_INFO, ALLOCATE,
		           "finalizing job scheculer client");
		finalize_jobsched_client();
		DEBUG_NOW (REPORT_INFO, ALLOCATE,
		           "finalizing MPI execution environment");
		MPI_Finalize();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, ALLOCATE,
	           "launching allocation thread");
	           
	if (pthread_create (&allocate_thread, NULL, allocate_thread_start, NULL)) {
		DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
		           "could not launch allocation thread");
		DEBUG_NOW (REPORT_INFO, ALLOCATE,
		           "finalizing allocation spinlock");
		pthread_spin_destroy (&allocate_spinlock);
		DEBUG_NOW (REPORT_INFO, ALLOCATE,
		           "finalizing job scheculer client");
		finalize_jobsched_client();
		DEBUG_NOW (REPORT_INFO, ALLOCATE,
		           "finalizing MPI execution environment");
		MPI_Finalize();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, ALLOCATE,
	           "launching updater thread");
	           
	if (pthread_create (&update_thread, NULL, update_thread_start, NULL)) {
		DEBUG_NOW (REPORT_ERRORS, ALLOCATE,
		           "could not launch updater thread");
		DEBUG_NOW (REPORT_INFO, ALLOCATE,
		           "shutting down allocation thread");
		ALLOCATE_LOCK_S
		allocate_shutting_down = true;
		ALLOCATE_LOCK_E
		pthread_join (allocate_thread, NULL);
		DEBUG_NOW (REPORT_INFO, ALLOCATE,
		           "finalizing allocation spinlock");
		pthread_spin_destroy (&allocate_spinlock);
		DEBUG_NOW (REPORT_INFO, ALLOCATE,
		           "finalizing job scheduler client");
		finalize_jobsched_client();
		DEBUG_NOW (REPORT_INFO, ALLOCATE,
		           "finalizing MPI execution environment");
		MPI_Finalize();
		return false;
	}
	
	return true;
}

void finalize_allocate() {
	DEBUG_NOW (REPORT_INFO, ALLOCATE,
	           "shutting down allocation and updater threads");
	ALLOCATE_LOCK_S
	allocate_shutting_down = true;
	ALLOCATE_LOCK_E
	pthread_join (update_thread, NULL);
	pthread_join (allocate_thread, NULL);
	DEBUG_NOW (REPORT_INFO, ALLOCATE,
	           "finalizing allocation spinlock");
	pthread_spin_destroy (&allocate_spinlock);
	DEBUG_NOW (REPORT_INFO, ALLOCATE,
	           "finalizing job scheduler client");
	finalize_jobsched_client();
	DEBUG_NOW (REPORT_INFO, ALLOCATE,
	           "finalizing MPI execution environment");
	MPI_Finalize();
}
#endif
