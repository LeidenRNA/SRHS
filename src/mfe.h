#ifndef RNA_MFE_H
#define RNA_MFE_H

#include <float.h>
#include "util.h"
#include "m_model.h"

#define STACK_MFE_LIMITS_MIN 0
#define STACK_MFE_LIMITS_MAX 1
#define STACK_MFE_LIMITS_MIN_LENGTH 2
#define STACK_MFE_LIMITS_MAX_LENGTH 15

#define STACK_MFE_DIR_PATH "mfe/"

#define STACK_MFE_MAX_LINE_LENGTH 100

#define STACK_MFE_FAILED FLT_MIN

bool initialize_mfe();
void finalize_mfe();
float get_simple_mfe_estimate (ntp_linked_bp restrict linked_bp,
                               const char *seq);
float get_turner_mfe_estimate (ntp_linked_bp restrict linked_bp,
                               const char *seq);
float get_stack_mfe_percentile (unsigned short stack_len, float mfe);

#endif //RNA_MFE_H
