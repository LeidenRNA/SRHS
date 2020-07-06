#ifndef RNA_M_ANALYSE_H
#define RNA_M_ANALYSE_H

#include <stdbool.h>
#include "util.h"
#include "simclist.h"
#include "sequence.h"
#include "limits.h"

bool get_next_model_partition (ntp_model model,
                               ntp_element *restrict model_partitions,
                               nt_element_count num_current_partitions);

bool get_model_size (const nt_model *restrict model, ntp_element el,
                     nt_model_size *restrict model_size);

char get_element_pos_var_range (const struct _nt_element *restrict el);

bool get_paired_element_relative_index (const nt_element *restrict current_el,
                                        const nt_element *restrict target_el, ushort *restrict target_idx);

bool get_stack_distances_in_paired_element (const nt_model *restrict model,
                                        const nt_element *restrict el,
                                        const nt_element *restrict prev_el,
                                        nt_stack_idist *restrict min_stack_dist,
                                        nt_stack_idist *restrict max_stack_dist, short *restrict in_extrusion);

bool get_stack_distances (const nt_model *restrict model, ntp_element el,
                          ntp_list *restrict min_stack_dist, ntp_list *restrict max_stack_dist,
                          ntp_list *restrict in_extrusion, ntp_list *restrict dist_els);

bool get_next_constraint_offset_and_dist_by_element
(nt_s_rel_count *restrict constraint_fp_offset_min,
 nt_s_rel_count *restrict constraint_fp_offset_max, bool *fp_overlaps,
 nt_s_rel_count *restrict constraint_tp_dist_min,
 nt_s_rel_count *restrict constraint_tp_dist_max, bool *tp_overlaps,
 nt_s_rel_count *restrict constraint_single_dist_min,
 nt_s_rel_count *restrict constraint_single_dist_max, bool *single_overlaps,
 const nt_model *restrict model, ntp_element restrict this_element,
 const nt_element *restrict target_element,
 ntp_constraint restrict this_constraint);

bool get_contained_paired_elements_by_element
(ntp_element *restrict contained_paired_elements,
 nt_rel_count *restrict paired_elements_fp_dist_min,
 nt_rel_count *restrict paired_elements_fp_dist_max,
 nt_rel_count *restrict paired_elements_tp_dist_min,
 nt_rel_count *restrict paired_elements_tp_dist_max,
 nt_stack_idist *restrict paired_elements_idist_min,
 nt_stack_idist *restrict paired_elements_idist_max,
 short *restrict paired_elements_in_extrusion,
 nt_rel_count *restrict next_paired_element_min,
 nt_rel_count *restrict next_paired_element_max,
 ushort *num_contained_paired_elements,
 const nt_model *restrict model, nt_element *restrict this_element,
 const nt_element *restrict target_element,
 const nt_stack_size stack_size, const nt_stack_idist idist);

bool get_containing_paired_element_dist_by_element
(ntp_element *restrict containing_paired_element,
 nt_rel_count *restrict paired_element_fp_dist_min,
 nt_rel_count *restrict paired_element_fp_dist_max,
 nt_rel_count *restrict paired_element_tp_dist_min,
 nt_rel_count *restrict paired_element_tp_dist_max,
 nt_element *restrict last_paired_element, const nt_model *restrict model,
 nt_element *restrict this_element,
 const nt_element *restrict target_el,
 const nt_stack_size stack_size, const nt_stack_idist idist);

bool get_next_constraint_offsets_by_element
(nt_s_rel_count *restrict constraint_fp_offset_min,
 nt_s_rel_count *restrict constraint_fp_offset_max,
 nt_s_rel_count *restrict constraint_tp_offset_min,
 nt_s_rel_count *restrict constraint_tp_offset_max,
 nt_s_rel_count *restrict constraint_single_offset_min,
 nt_s_rel_count *restrict constraint_single_offset_max,
 const nt_model *restrict model, ntp_element restrict this_element,
 const nt_element *restrict target_element,
 ntp_constraint restrict this_constraint,
 const nt_stack_size stack_size, const nt_stack_idist stack_idist);

#endif //RNA_M_ANALYSE_H
