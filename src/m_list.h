#ifndef RNA_M_LIST_H
#define RNA_M_LIST_H

#include <stdbool.h>
#include "util.h"
#include "simclist.h"
#include "sequence.h"
#include "limits.h"

extern ntp_list search_seq_list;

#ifdef MULTITHREADED_ON
	bool initialize_list_destruction();
	bool wait_list_destruction();
	void finalize_list_destruction();
#endif

bool list_initialize_tagging();

bool ntp_list_insert (ntp_list restrict dst, ntp_list restrict src,
                      const nt_stack_size stack_len, const uchar track_id);

ntp_list duplicate_list_shallow (ntp_list orig_list, char *debug_msg);

ntp_list ntp_list_concatenate (ntp_list restrict list1, ntp_list restrict list2,
                               const uchar tag);

ntp_count_list ntp_count_list_concatenate (ntp_count_list restrict list1,
                                        ntp_count_list restrict list2);

bool ntp_list_alloc_debug (ntp_list restrict *l, char *debug_msg);

bool ntp_list_linked_bp_seeker (ntp_list restrict list,
                                const nt_bp *restrict linked_bp, const nt_linked_bp *restrict prev_linked_bp);

bool ntp_list_linked_bp_seeker_with_elements (ntp_list restrict list,
                                        ntp_linked_bp restrict linked_bp);

ntp_linked_bp get_linked_bp_root (ntp_linked_bp restrict current_linked_bp,
                                  const char advanced_pair_track_id);

void dump_linked_bp (ntp_linked_bp linked_bp, ntp_seq seq);

bool list_destroy_all_tagged();

bool dispose_linked_bp_copy (nt_model *restrict model, ntp_list list,
                             char *free_bp_reason_msg, char *free_list_reason_msg
#ifndef NO_FULL_CHECKS
	, char *failed_iteration_msg
#endif
                            );
void dispose_linked_bp_copy_by_FE_threshold (nt_model *restrict model,
                                        const char *seq, ntp_list list, float FE_threshold,
                                        char *free_bp_reason_msg, char *free_list_reason_msg);

#endif //RNA_M_LIST_H
