#ifndef RNA_FILTER_H
#define RNA_FILTER_H

#include <stdbool.h>
#include "util.h"
#include "datastore.h"
#include "m_model.h"

#define PRIVILEGED_SCHED    false

#define FILTER_MSG_SIZE     32

#if PRIVILEGED_SCHED
	#define THREAD_SCHED_PRIO   50
#endif

bool initialize_filter (const char *server, unsigned short port);
void finalize_filter();

bool filter_seq (const char *seq_buff, nt_abs_count seq_buff_size,
                 const ntp_model model,
                 const ntp_element el_with_largest_stack,
                 nt_rel_count fp_lead_min_span, nt_rel_count fp_lead_max_span,
                 nt_stack_size stack_min_size, nt_stack_size stack_max_size,
                 nt_stack_idist stack_min_idist, nt_stack_idist stack_max_idist,
                 nt_rel_count tp_trail_min_span, nt_rel_count tp_trail_max_span,
                 nt_rt_bytes job_id);

bool filter_seq_from_file (const char *fn, const ntp_model model,
                           const ntp_element el_with_largest_stack,
                           nt_rel_count fp_lead_min_span, nt_rel_count fp_lead_max_span,
                           nt_stack_size stack_min_size, nt_stack_size stack_max_size,
                           nt_stack_idist stack_min_idist, nt_stack_idist stack_max_idist,
                           nt_rel_count tp_trail_min_span, nt_rel_count tp_trail_max_span
                          );

#endif //RNA_FILTER_H
