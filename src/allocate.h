#if JS_JOBSCHED_TYPE!=JS_NONE
#ifndef RNA_ALLOCATE_H
#define RNA_ALLOCATE_H
#include "distribute.h"
#include "util.h"

#define WORKER_STATUS_UNDEFINED		       -1	    // indicate invalid worker status (default status when waiting for ping reply)
#define WORKER_STATUS_AVAILABLE                 0           // this worker is available, running on some worker node, but not active
#define WORKER_STATUS_ACTIVE                    1           // this worker is available but currently already active
#define WORKER_STATUS_HAS_RESULT                2           // this worker is running on some node and is inactive, having a pending result

// maximum number of seconds a worker job is allowed to live, after allocation;
// pending tasks are allowed to complete, so actual time lived might exceed this value;
// current value (23hrs) assumes server TTL of 24hrs (allowing for a grace period of 1hr)
#define WORKER_JOB_TTL_S                        82813
// number of seconds to wait on available workers before sending/receiving ping request
// value > TTL disables PING...
// if PING not received from available OR active worker, then worker is killed
#define WORKER_JOB_PING_WAIT_S			3607	    
// ms to wait before PING MPI tests
#define WORKER_JOB_PING_SLEEP_MS		100
// max attempts for PING MPI tests
#define WORKER_JOB_PING_MAX_ATTEMPTS		50

#define MPI_TEST_WORK_SLEEP_MS 			1	    // sleep duration in ms, between subsequent MPI_Test calls after MPI_Isend or MPI_Irecv

bool initialize_allocate (char *si_server, unsigned short si_port,
                          const char *scan_bin_fn);
bool allocate_scan_job (cp_job job, char *ss_strn, char *pos_var_strn,
                        char *seq_strn, ds_int32_field ref_id);
void finalize_allocate();

#endif //RNA_ALLOCATE_H
#endif
