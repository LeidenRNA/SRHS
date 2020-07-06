#ifndef RNA_M_BUILD_H
#define RNA_M_BUILD_H

#include <stdbool.h>
#include "util.h"
#include "simclist.h"
#include "sequence.h"
#include "limits.h"

/*
 * model building
 */
ntp_model initialize_model();
ntp_element initialize_element (nt_element_type el_type, nt_element_count min,
                                nt_element_count max);
ntp_constraint initialize_pseudoknot (nt_element_count min,
                                      nt_element_count max);
ntp_constraint initialize_base_triple();
ntp_element add_element_to_model (ntp_model model, ntp_element prev_el,
                                  ntp_element new_el, nt_branch_type br_type);
ntp_constraint add_pseudoknot_to_model (ntp_model restrict model,
                                        ntp_element restrict prev_el_fp, nt_branch_type br_type_fp,
                                        ntp_element restrict prev_el_tp, nt_branch_type br_type_tp,
                                        ntp_constraint restrict new_pk_constraint);
ntp_constraint add_base_triple_to_model (ntp_model restrict model,
                                        ntp_element restrict prev_el_fp, nt_branch_type br_type_fp,
                                        ntp_element restrict prev_el_tp, nt_branch_type br_type_tp,
                                        ntp_element restrict prev_el_single, nt_branch_type br_type_single,
                                        ntp_constraint restrict new_base_triple_constraint);
void finalize_model (ntp_model model);

#endif //RNA_M_BUILD_H
