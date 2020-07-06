#if JS_JOBSCHED_TYPE!=JS_NONE
#ifndef RNA_C_JOBSCHED_CLIENT_H
#define RNA_C_JOBSCHED_CLIENT_H
#include <stdbool.h>
#include "util.h"
#include "c_jobsched_server.h"

typedef struct {
	int16_t num_up_nodes, num_up_procs, num_free_nodes, num_free_procs;
} js_node_info, *jsp_node_info;

bool js_execute (char *cmd, void *cmd_data, size_t cmd_data_sz,
                 const char *job_bin_fn, void **response_data);
bool initialize_jobsched_client (char *si_server, ushort si_port);
void finalize_jobsched_client();

#endif //RNA_C_JOBSCHED_CLIENT_H
#endif
