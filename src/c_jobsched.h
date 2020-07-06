#ifndef RNA_TORQUE_H
#define RNA_TORQUE_H
#include <stdbool.h>
#include "util.h"

bool initialize_jobsched (char *headnode_server);
void finalize_jobsched();
bool get_node_info (ushort *num_up_nodes, ushort *num_up_procs,
                    ushort *num_free_nodes, ushort *num_free_procs);

#endif //RNA_TORQUE_H
