#ifndef RNA_M_SEARCH_H
#define RNA_M_SEARCH_H

#include <stdbool.h>
#include "util.h"
#include "simclist.h"
#include "sequence.h"
#include "limits.h"
#include "m_model.h"

#define MAX_SEARCH_LIST_SIZE 15000

/*
 * sequence search
 */
ntp_list search_seq (ntp_seq restrict seq, nt_model *restrict model,
                     float *elapsed_time
#ifdef SEARCH_SEQ_DETAIL
	, ntp_bp targets, nt_hit_count num_targets
#endif
                    );

#endif //RNA_MODEL_H
