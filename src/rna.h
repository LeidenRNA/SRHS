#ifndef RNA_RNA_H
#define RNA_RNA_H

#define SCAN_MODE_ARG_SHORT         "s"
#define SCAN_MODE_ARG_LONG          "scan"
#define SS_ARG_LONG                 "ss"
#define POS_VAR_ARG_LONG            "pos-var"
#define SEQ_NT_ARG_LONG             "seq-nt"
#define SCAN_WORK_SLEEP_MS 	    5
#if JS_JOBSCHED_TYPE!=JS_NONE
	#define MPI_PORT_NAME_ARG_LONG      "mpi-port-name"
	#define SCHED_JOB_ID_ARG_LONG       "sched-job-id"
#endif

#endif //RNA_RNA_H
