#if JS_JOBSCHED_TYPE!=JS_NONE
#ifndef RNA_C_JOBSCHED_SERVER_H
#define RNA_C_JOBSCHED_SERVER_H
#include <stdbool.h>
#include <mpi.h>
#include "util.h"
#if JS_JOBSCHED_TYPE==JS_TORQUE
	#include <pbs_error.h>
	#include <pbs_ifl.h>
#elif JS_JOBSCHED_TYPE==JS_SLURM
	#include <slurm/slurm.h>
	#include <slurm/slurm_errno.h>
#endif

typedef struct {
	char mpi_port_name[MPI_MAX_PORT_NAME];
} wn_job, *wnp_job;

// PBS does not seem to have constants/macros/enums for job status, so need common enum for PBS/SLURM
typedef enum {
	JS_JOBS_STATUS_WAITING = 0,             // job is held, waiting, queued, suspended, moving to a new location, or any other status which *might*
	// eventually change to running status
	JS_JOBS_STATUS_RUNNING = 1,             // job is currently running
	JS_JOBS_STATUS_NOT_RUNNING = 2,         // job is completed, exiting, or any other status known to not lead to running status
	JS_JOB_STATUS_UNKNOWN = 3               // untracked job status
} JS_JOB_STATUS;

#define SI_MIN_PORT                         1024   // the lower limit is based on standard (RFC793) ranges for registered/user ports, but the upper limit is derived from
#define SI_MAX_PORT                         61000  // "/proc/sys/net/ipv4/ip_local_port_range" on "Linux node-003 3.2.0-5-amd64 #1 SMP Debian 3.2.96-3 x86_64 GNU/Linux")

#define JS_CMD_STRN                         "JSCMD"
#define JS_CMD_DATA                         "JSDATA"
#define JS_CMD_BIN_EXE_FPATH                "JSBEFPATH"
#define JS_NUM_CMDS                         4
#define JS_MSG_SIZE                         1080

// GET_NODE_INFO command
#define JS_CMD_GET_NODE_INFO                "GET_NODE_INFO"
#define JS_DATA_NUM_UP_NODES                "NUM_UP_NODES"
#define JS_DATA_NUM_UP_PROCS                "NUM_UP_PROCS"
#define JS_DATA_NUM_FREE_NODES              "NUM_FREE_NODES"
#define JS_DATA_NUM_FREE_PROCS              "NUM_FREE_PROCS"

// SUBMIT_JOB command
#define JS_CMD_SUBMIT_JOB                   "SUBMIT_JOB"
#define JS_DATA_FULL_JOB_ID                 "FULL_JOB_ID"

// GET_JOB_STATUS command
#define JS_CMD_GET_JOB_STATUS               "GET_JOB_STATUS"
#define JS_DATA_JOB_STATUS                  "JOB_STATUS"

// DEL_JOB command
#define JS_CMD_DEL_JOB                      "DEL_JOB"
#define JS_DATA_RETURN_VALUE                "RET_VAL"

#define JS_JOBSCHED_JOB_NAME                "RNA"
#define JS_JOBSCHED_JOB_SUBMIT_ARGS_ENV     "JS_SUBMIT_ARGS"
#define JS_JOBSCHED_JOB_SUBMIT_BEP_ENV      "JS_BIN_EXE_PATH"
#define JS_JOBSCHED_JOB_ID_ENV              "JS_JOB_ID"

#if JS_JOBSCHED_TYPE==JS_TORQUE
	#define JS_JOBSCHED_PBS_SCRIPT_FPATH        "/home/rna/RNA/pbs/qsub.script"
	#define JS_JOBSCHED_PBS_JOB_RESOURCES_TYPE  "nodes"
	#define JS_JOBSCHED_PBS_JOB_RESOURCES_VALUE "1:ppn=1"
	#define JS_JOBSCHED_PBS_JOB_ID_SEPARATOR    '.'
	// JS_JOBSCHED_MAX_JOB_ID_LEN - max length of job id substring before JS_JOBSCHED_PBS_JOB_ID_SEPARATOR
	// PBSPro documentation states < 1,000,000,000,000, or PBS_MAXSEQNUM as defined in pbs_ifl.h
	#define JS_JOBSCHED_MAX_JOB_ID_LEN          12<PBS_MAXSEQNUM?12:PBS_MAXSEQNUM
	// JS_JOBSCHED_MAX_FULL_JOB_ID_LEN - max length of job id including JS_JOBSCHED_PBS_JOB_ID_SEPARATOR and server name
	// PBS_MAXSVRJOBID as defined in pbs_ifl.h
	#define JS_JOBSCHED_MAX_FULL_JOB_ID_LEN     JS_JOBSCHED_MAX_JOB_ID_LEN>PBS_MAXSVRJOBID?JS_JOBSCHED_MAX_JOB_ID_LEN:PBS_MAXSVRJOBID
	
	bool initialize_jobsched_server (char *headnode_server);
#else
	bool initialize_jobsched_server();
#endif

#if JS_JOBSCHED_TYPE!=JS_NONE
	void finalize_jobsched_server();
#endif

bool is_js_server_cmd (const char *cmd);

#if JS_JOBSCHED_TYPE==JS_TORQUE
	bool scheduler_interface (long port, char *headnode_server);
#elif JS_JOBSCHED_TYPE==JS_SLURM
	bool scheduler_interface (long port);
#else
	bool scheduler_interface();
#endif

#endif // RNA_C_JOBSCHED_SERVER_H
#endif
