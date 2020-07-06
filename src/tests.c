#include <assert.h>
#include "simclist.h"
#include "interface.h"
#include "m_list.h"
#include "m_build.h"
#include "m_search.h"
#include "tests.h"

/*
 * validate_test:
 *          run a single query for the input sequence against
 *          the given model, and test against the provided
 *          results
 *
 * args:    integer test identifier,
 *          nucleotide sequence,
 *          cssd model,
 *          array of first bp expected per hit returned,
 *          number of hits expected,
 *          boolean option to dump hits
 *
 * returns: elapsed time
 */
float validate_test (ushort test_id, const char *seq, ntp_model model,
                     ntp_bp results, ushort num_results, bool dump) {
	DEBUG_NOW1 (REPORT_INFO, TESTS, "starting test #%d", test_id);
	float elapsed_time;
	REGISTER ntp_list found_list = search_seq (seq, model, &elapsed_time
	                                        #ifdef SEARCH_SEQ_DETAIL
	                                        , results, num_results
	                                        #endif
	                                          );
	REGISTER ushort num_found = 0;
	
	if (found_list) {
		num_found = (ushort)found_list->numels;
	}
	
	REGISTER bool test_failed = false;
	
	for (REGISTER ushort i = 0; i < num_results; i++) {
		if (!num_found) {
			DEBUG_NOW2 (REPORT_WARNINGS, TESTS,
			            "test #%d failed (%d hits expected, none found)",
			            test_id, num_results);
			test_failed = true;
			break;
		}
		
		if (list_iterator_start (found_list)) {
			REGISTER bool matched_result = false;
			
			while (list_iterator_hasnext (found_list)) {
				ntp_linked_bp linked_bp = (ntp_linked_bp) list_iterator_next (found_list);
				
				if (linked_bp) {
					while (linked_bp->prev_linked_bp != NULL) {
						linked_bp = linked_bp->prev_linked_bp;          // match on outermost pair
					}
					
					if (linked_bp->bp->fp_posn == results[i].fp_posn &&
					    linked_bp->bp->tp_posn == results[i].tp_posn) {
						matched_result = true;
						break;
					}
				}
			}
			
			list_iterator_stop (found_list);
			
			if (!matched_result) {
				DEBUG_NOW3 (REPORT_WARNINGS, TESTS, "test #%d failed (unmatched {%llu,%llu})",
				            test_id, results[i].fp_posn, results[i].tp_posn);
				test_failed = true;
			}
		}
	}
	
	if (num_found > 0 && num_found > num_results) {
		DEBUG_NOW3 (REPORT_WARNINGS, TESTS,
		            "test #%d failed (%d found, %d hits expected)",
		            test_id, num_found, num_results);
		test_failed = true;
	}
	
	if (dump) {
		if (found_list) {
			DEBUG_NOW2 (REPORT_INFO, TESTS, "test #%d: %d found", test_id, num_found);
			
			if (list_iterator_start (found_list)) {
				while (list_iterator_hasnext (found_list)) {
					dump_linked_bp ((ntp_linked_bp) list_iterator_next (found_list), seq);
				}
				
				list_iterator_stop (found_list);
			}
		}
		
		else {
			DEBUG_NOW1 (REPORT_INFO, TESTS, "test #%d: no hits found", test_id);
		}
	}
	
	if (found_list) {
		dispose_linked_bp_copy (model,
		                        found_list,
		                        "linked_bp_copy for safe_copy of found_list in validate_test",
		                        "safe_copy of found_list in validate_test"
		                        #ifndef NO_FULL_CHECKS
		                        , "could not iterate over to free safe_copy of found_list in validate_test"
		                        #endif
		                       );
	}
	
	if (test_failed) {
		elapsed_time = 0.0f;
	}
	
	list_destroy_all_tagged();
	return elapsed_time;
}

/*
 * run_this_test:
 *          run a single query for the input sequence against
 *          the given secondary structure and positional variables data,
 *          and validate against the input number of bps provided
 *
 * args:    integer test identifier,
 *          nucleotide sequence,
 *          secondary structure and positional variables,
 *          array of first bp expected per hit returned,
 *          number of hits expected
 *
 * returns: elapsed time, test result
 */
bool run_this_test (ushort test_id,
                    char seq[MAX_SEQ_LEN + 1], char ss[MAX_MODEL_STRING_LEN + 1],
                    char pos_var[MAX_MODEL_STRING_LEN + 1],
                    ntp_bp results, ushort num_results,
                    float *this_time) {
	ntp_model model = NULL;
	bool ret_val = false;
	char *err_msg = NULL;
	
	if (!convert_CSSD_to_model (ss, pos_var, &model, &err_msg)) {
		DEBUG_NOW3 (REPORT_ERRORS, TESTS,
		            "failed to convert cssd ('%s','%s') to model for test #%d", ss, pos_var,
		            test_id);
		            
		if (err_msg) {
			FREE_DEBUG (err_msg, "err_msg from convert_CSSD_to_model in run_this_test");
		}
	}
	
	else {
		if (compare_CSSD_model_strings (ss, pos_var, model)) {
			*this_time = validate_test (test_id, seq, model, results, num_results, true);
			
			if (*this_time > 0.0f) {
				ret_val = true;
			}
		}
		
		else {
			DEBUG_NOW3 (REPORT_ERRORS, TESTS,
			            "input cssd ('%s', '%s') and stringified model do not match for test number %d",
			            ss, pos_var, test_id);
		}
		
		finalize_model (model);
	}
	
	return ret_val;
}

/*
 * run_all_tests:
 *          run all queries specified by tests.out and validate results;
 *          tests.out is first specified as test.in, and then generated
 *          using scripts/gen_test.py
 */
void run_all_tests() {
	float total_time = 0.0f, this_time;
	char seq[MAX_NUM_TESTS][MAX_SEQ_LEN + 1],
	     ss[MAX_NUM_TESTS][MAX_MODEL_STRING_LEN + 1],
	     pos_var[MAX_NUM_TESTS][MAX_MODEL_STRING_LEN + 1];
	nt_bp results[MAX_NUM_TESTS][MAX_NUM_RESULTS_PER_TEST];
	ushort num_results[MAX_NUM_TESTS], t = 0, r, i = 0;
	// include auto-generate tests
#include "tests.out"
	
	for (i = 0; i < t; i++) {
		DEBUG_NOW1 (REPORT_INFO, BATCH_TESTS,
		            "========================================running test %03d=====================================",
		            i + 1);
		bool success = run_this_test ((ushort) (i + 1), seq[i], ss[i], pos_var[i],
		                              results[i], num_results[i], &this_time);
		DEBUG_NOW2 (REPORT_INFO, BATCH_TESTS,
		            "==================================finished test %03d in %06.2fs===============================\n",
		            i + 1, this_time);
		            
		if (success) {
			total_time += this_time;
		}
		
		else {
			break;
		}
	}
	
	DEBUG_NOW4 (REPORT_INFO, BATCH_TESTS,
	            "%d of %d tests passed (%d%%) in %06.2f seconds\n", i, t,
	            (int) (100.0f * i / (float)t), total_time);
}
