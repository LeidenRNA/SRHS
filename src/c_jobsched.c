#include <stdio.h>
#include <stdlib.h>
#include "c_jobsched.h"

#if JOBSCHED==JS_TORQUE
	#include "pbs_error.h"
	#include "pbs_ifl.h"
#elif TESTING==JS_SLURM
#endif

#define C_ARG_MAX   1000                // ARG_MAX in limits.h is typically too large

#if JOBSCHED==JS_TORQUE                 // TORQUE-based environment settings; should be near-to-identical for PBSPro
	static int pbs_connection_id = -1;
	#define TORQUE_CMD_Q_INFO "qstat"
	#define TORQUE_JOB_FIELD_SEP ','    // char which separates individual jobs in 'pbsnodes' ATTR_NODE_jobs attribute value's field
	
#elif JOBSCHED==JS_SLURM                // TORQUE-based environment settings
	#define SLURM_SSH_CMD_PATH ""
	#define SLURM_SSH_CMD "ssh"
	#define SLURM_CMD_REDIRECT ">/dev/null 2>&1"
	#ifdef _WIN32
		#define SLURM_SSH_USER "alan"
	#else
		#define SLURM_SSH_USER "zammita"
	#endif
	#define SLURM_CMD_Q_INFO "sinfo"
	
#endif

bool initialize_jobsched (char *headnode_server) {
	DEBUG_NOW (REPORT_INFO, SCHED, "initializing job scheduler");
	#if JOBSCHED==JS_TORQUE
	pbs_connection_id = pbs_connect (headnode_server);
	
	if (pbs_connection_id >= 0) {
		DEBUG_NOW1 (REPORT_INFO, SCHED, "connected to headnode '%s'", headnode_server);
		return true;
	}
	
	else {
		DEBUG_NOW1 (REPORT_ERRORS, SCHED, "failed to connect to headnode '%s'",
		            headnode_server);
		return false;
	}
	
	#elif JOBSCHED==JS_SLURM
	DEBUG_NOW2 (REPORT_INFO, SCHED,
	            "validating connection to headnode_server '%s' using command '%s' in initialize_jobsched",
	            headnode_server, SLURM_CMD_Q_INFO);
	char cmd[C_ARG_MAX + 1];
	// cmd <- path|ssh user@headnode_server 'queue info command' redirection
	sprintf (cmd, "%s%s %s@%s \"%s %s\"", SLURM_SSH_CMD_PATH, SLURM_SSH_CMD,
	         SLURM_SSH_USER, headnode_server, SLURM_CMD_Q_INFO, SLURM_CMD_REDIRECT);
	
	if (!system (cmd)) {
		DEBUG_NOW1 (REPORT_INFO, SCHED,
		            "successfully connected to headnode '%s' in initialize_jobsched",
		            headnode_server);
		return true;
	}
	
	else {
		DEBUG_NOW1 (REPORT_ERRORS, SCHED,
		            "failed to connect to headnode '%s' in initialize_jobsched",
		            headnode_server);
		return false;
	}
	
	#else
	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "unspecified job scheduler");
	return false;
	#endif
}

void finalize_jobsched() {
	#if JOBSCHED==JS_TORQUE

	if (pbs_connection_id < 0) {
		DEBUG_NOW (REPORT_WARNINGS, SCHED,
		           "job scheduler not initialized");
		return;
	}
	
	DEBUG_NOW (REPORT_INFO, SCHED,
	           "finalizing job scheduler");
	pbs_disconnect (pbs_connection_id);
	#elif JOBSCHED==JS_SLURM
	DEBUG_NOW (REPORT_INFO, SCHED,
	           "finalizing job scheduler");
	#else
	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "unknown job scheduler");
	#endif
}

bool get_node_info (ushort *num_up_nodes, ushort *num_up_procs,
                    ushort *num_free_nodes, ushort *num_free_procs) {
	*num_up_nodes = 0;
	*num_up_procs = 0;
	*num_free_nodes = 0;
	*num_free_procs = 0;
	#if JOBSCHED==JS_TORQUE
	struct batch_status *bs = pbs_statnode (pbs_connection_id, NULL, NULL, NULL);
	
	if (bs != NULL) {
		struct batch_status *next_bs = bs;
		
		do {
			if (next_bs->attribs != NULL) {
				ushort this_jobs = 0, this_np = 0;
				bool this_up = false, this_free = false, is_cluster_node = false;
				struct attrl *next_attrib = next_bs->attribs;
				
				do {
					if (!strcmp (next_attrib->name, ATTR_NODE_jobs)) {
						ushort j_strn_len = strlen (next_attrib->value);
						
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
								ushort np_strn_len = strlen (next_attrib->value);
								
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
				
				if (is_cluster_node) {
					if (this_up) {
						(*num_up_nodes)++;
						(*num_up_procs) += this_np;
						
						if (this_free) {
							(*num_free_nodes)++;
							(*num_free_procs) += this_np - this_jobs;
						}
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
		           "failed to get node status");
		return false;
	}
	
	#elif JOBSCHED==JS_SLURM
	DEBUG_NOW (REPORT_WARNINGS, SCHED,
	           "SLURM get_node_info interface not yet implemented");
	#else
	DEBUG_NOW (REPORT_ERRORS, SCHED,
	           "unknown job scheduler");
	return false;
	#endif
}
