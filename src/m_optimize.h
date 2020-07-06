#ifndef RNA_M_OPTIMIZE_H
#define RNA_M_OPTIMIZE_H

#include <stdbool.h>
#include "util.h"
#include "simclist.h"
#include "sequence.h"
#include "limits.h"

void optimize_seq_bp (ntp_seq_bp restrict seq_bp);
bool optimize_seq_bp_by_constraint (const nt_model *restrict model,
                                    ntp_seq restrict seq, ntp_stack restrict this_stack,
                                    const nt_stack_size this_stack_size);

#endif //RNA_M_OPTIMIZE_H
