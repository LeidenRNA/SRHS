#ifndef RNA_DISTRIBUTE_H
#define RNA_DISTRIBUTE_H

#include <stdbool.h>
#include "util.h"
#include "datastore.h"

typedef struct {
	nt_rt_bytes job_id;
	nt_abs_seq_posn start_posn, end_posn;
} c_job, *cp_job;

#define DISPATCH_NULL_JOB_POSN	INT32_MIN

typedef char *rp_hit;

#define BACKEND_MIN_PORT        1024        // the lower limit is based on standard (RFC793) ranges for registered/user ports, but the upper limit is derived from
#define BACKEND_MAX_PORT        61000       // "/proc/sys/net/ipv4/ip_local_port_range" on "Linux node-003 3.2.0-5-amd64 #1 SMP Debian 3.2.96-3 x86_64 GNU/Linux")

#define MAX_Q_SIZE              100000000
#define Q_DEFAULT_PORT          8080
#define D_Q_NUM_SOCKET_THREADS  10
#ifndef _WIN32
	#define Q_DISABLE_NAGLE         false
#endif

#define DISPATCH_MSG_SZ             2                   // size of (control) messages passed between dispatch and running worker jobs; data msgs vary in length
#define DISPATCH_MSG_RUN            0                   // run job with the given sequence positions in data fields 1,2 (0-indexed); length of payload in field 3
#define DISPATCH_MSG_PING 	    1			// test comms between dispatch and worker node
#define DISPATCH_MSG_SHUTDOWN       2                   // shutdown job
#define DISPATCH_MSG_MPI_TYPE       MPI_UNSIGNED_SHORT  // unit data type used for DISPATCH (control) messaging
#define DISPATCH_MSG_PAYLOAD_TYPE   MPI_INT   		// unit data type used for DISPATCH (payload) transfer
#define WORKER_MSG_SZ               2                   // size of (control) messages passed between running worker jobs and dispatch
#define WORKER_MSG_MPI_TYPE         MPI_UNSIGNED_CHAR   // unit data type used for WORKER messaging
#define WORKER_MSG_PAYLOAD_TYPE     MPI_UNSIGNED_CHAR   // unit data type used for WORKER (payload) transfer

void dis_lock();					// distribute-allocate locking
void dis_unlock();

// launch dispatch service on 1 worker node core
#if JS_JOBSCHED_TYPE!=JS_NONE
bool distribute (const char *exe_name, const char *dispatch_arg,
                 const char *backend_port_arg, ushort port,
                 const char *ds_server_arg, char *ds_server, const char *ds_port_arg,
                 ushort ds_port,
                 const char *si_server_arg, char *si_server, const char *si_port_arg,
                 ushort si_port,
                 const char *scan_bin_fn_arg, char *scan_bin_fn);
#else
bool distribute (const char *exe_name, const char *dispatch_arg,
                 const char *backend_port_arg, ushort port,
                 const char *ds_server_arg, char *ds_server, const char *ds_port_arg,
                 ushort ds_port,
                 const char *scan_bin_fn_arg, char *scan_bin_fn);
#endif
// setup and start dispatch on local machine
#if JS_JOBSCHED_TYPE!=JS_NONE
	bool dispatch (ushort port, char *ds_server, ushort ds_port, char *si_server,
	ushort si_port, char *scan_bin_fn);
#else
	bool dispatch (ushort port, char *ds_server, ushort ds_port, char *scan_bin_fn);
#endif

bool enq_r (rp_hit r);

#endif //RNA_DISTRIBUTE_H
