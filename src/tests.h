#ifndef RNA_TESTS_H
#define RNA_TESTS_H

#include <stdbool.h>
#include "m_model.h"

#define MAX_NUM_TESTS 		 100
#define MAX_NUM_RESULTS_PER_TEST 100

void run_all_tests();
float validate_test (ushort test_id, const char *seq, ntp_model model,
                     ntp_bp results, ushort num_results, bool dump);

#endif //RNA_TESTS_H
