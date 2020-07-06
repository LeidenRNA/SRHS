#ifndef RNA_M_SEQ_BP_H
#define RNA_M_SEQ_BP_H

#include <stdbool.h>
#include "util.h"
#include "simclist.h"
#include "sequence.h"
#include "limits.h"
#include "m_model.h"

bool initialize_seq_bp_cache();
bool purge_seq_bp_cache_by_model (nt_model *restrict model);
ntp_bp_list_by_element create_seq_bp_stack_by_element (ntp_seq_bp restrict
                                        seq_bp, const uchar idx, nt_stack_idist  this_stack_idist,
                                        short this_in_extrusion, ntp_element this_element);
bool initialize_seq_bp_stacks (ntp_seq_bp restrict seq_bp, const uchar idx);
bool get_seq_bp_from_cache (const ntp_seq restrict seq, const nt_seq_hash hash,
                            const nt_model *restrict model,
                            ntp_list *restrict min_stack_dist, ntp_list *restrict max_stack_dist,
                            ntp_list *restrict in_extrusion, ntp_list *restrict dist_els,
                            REGISTER ntp_seq_bp restrict *seq_bp);
bool get_seq_bp_from_seq (const ntp_seq restrict seq, nt_model *restrict model,
                          ntp_list *restrict min_stack_dist, ntp_list *restrict max_stack_dist,
                          ntp_list *restrict in_extrusion, ntp_list *restrict dist_els,
                          ntp_seq_bp *restrict seq_bp);
nt_seq_count add_seq_bp_to_cache (const ntp_seq restrict seq,
                                  const nt_seq_hash hash, ntp_seq_bp restrict seq_bp);
bool finalize_seq_bp_cache();

void destroy_seq_bp (ntp_seq_bp restrict seq_bp);

#endif //RNA_M_SEQ_BP_H
