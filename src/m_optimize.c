#ifdef _WIN32
	#include <mem.h>
	#include <afxres.h>
#else
	#include <memory.h>
#endif

#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <math.h>
#include "util.h"
#include "m_model.h"
#include "interface.h"
#include "mfe.h"
#include "m_analyse.h"
#include "m_optimize.h"

#define MAX_CONTAINMENT_ELEMENTS 10

/*
 * model/seq_bp optimization functions
 */
static inline bool optimize_seq_bp_down (ntp_seq_bp restrict seq_bp,
                                        nt_stack_size this_stack_size, nt_stack_idist this_stack_idist,
                                        ntp_bp_list_by_element restrict this_list_by_element) {
	REGISTER
	bool bps_removed = false;
	ntp_element contained_paired_elements[MAX_CONTAINMENT_ELEMENTS];
	nt_rel_count paired_elements_fp_dist_min[MAX_CONTAINMENT_ELEMENTS],
	             paired_elements_fp_dist_max[MAX_CONTAINMENT_ELEMENTS],
	             paired_elements_tp_dist_min[MAX_CONTAINMENT_ELEMENTS],
	             paired_elements_tp_dist_max[MAX_CONTAINMENT_ELEMENTS],
	             next_paired_elements_min[MAX_CONTAINMENT_ELEMENTS],
	             next_paired_elements_max[MAX_CONTAINMENT_ELEMENTS];
	nt_stack_idist paired_elements_idist_min[MAX_CONTAINMENT_ELEMENTS],
	               paired_elements_idist_max[MAX_CONTAINMENT_ELEMENTS];
	short paired_elements_in_extrusion[MAX_CONTAINMENT_ELEMENTS];
	ushort num_contained_paired_elements = 0;
	bool bp_matched_paired_element_set[MAX_BP_PER_STACK_IDIST];
	uchar bp_matched_paired_element_count[MAX_BP_PER_STACK_IDIST];
	ntp_bp candidate_bp[MAX_CONTAINMENT_ELEMENTS][MAX_ELEMENT_MATCHES];
	nt_stack_size
	candidate_bp_stack_size[MAX_CONTAINMENT_ELEMENTS][MAX_ELEMENT_MATCHES];
	
	for (REGISTER nt_hit_count b = 0; b < this_list_by_element->stack_counts; b++) {
		bp_matched_paired_element_set[b] = false;
		bp_matched_paired_element_count[b] = 0;
	}
	
	if (get_contained_paired_elements_by_element (contained_paired_elements,
	                                        paired_elements_fp_dist_min, paired_elements_fp_dist_max,
	                                        paired_elements_tp_dist_min, paired_elements_tp_dist_max,
	                                        paired_elements_idist_min, paired_elements_idist_max,
	                                        paired_elements_in_extrusion,
	                                        next_paired_elements_min, next_paired_elements_max,
	                                        &num_contained_paired_elements,
	                                        seq_bp->model, seq_bp->model->first_element, this_list_by_element->el,
	                                        (nt_stack_idist) (this_stack_size + 1), this_stack_idist)) {
		for (REGISTER nt_hit_count b = 0; b < this_list_by_element->stack_counts; b++) {
			nt_rel_count
			candidate_bp_next_min[MAX_CONTAINMENT_ELEMENTS][MAX_ELEMENT_MATCHES],
			                      candidate_bp_next_max[MAX_CONTAINMENT_ELEMENTS][MAX_ELEMENT_MATCHES];
			ushort num_candidate_bps[MAX_CONTAINMENT_ELEMENTS];
			REGISTER
			ntp_bp this_bp = list_get_at (&this_list_by_element->list, b);
			
			for (REGISTER ushort i = 0; i < num_contained_paired_elements; i++) {
				num_candidate_bps[i] = 0;
			}
			
			for (REGISTER ushort i = 0; i < num_contained_paired_elements; i++) {
				if (!contained_paired_elements[i]->paired->min) {
					bp_matched_paired_element_count[b]++;
					// skip contained pairs with min stack length of 0
					continue;
				}
				
				REGISTER
				bool this_bp_matched_this_paired_element = false;
				
				for (REGISTER nt_rel_count fp_dist = paired_elements_fp_dist_min[i];
				     fp_dist <= paired_elements_fp_dist_max[i]; fp_dist++) {
					for (REGISTER nt_rel_count tp_dist = paired_elements_tp_dist_min[i];
					     tp_dist <= paired_elements_tp_dist_max[i]; tp_dist++) {
						for (REGISTER nt_rel_count idist = paired_elements_idist_min[i];
						     idist <= paired_elements_idist_max[i]; idist++) {
							for (REGISTER nt_rel_count that_size = (nt_rel_count) (
							                                        contained_paired_elements[i]->paired->min - 1);
							     that_size < contained_paired_elements[i]->paired->max; that_size++) {
								list_iterator_start (&seq_bp->stacks[that_size]);
								
								while (list_iterator_hasnext (&seq_bp->stacks[that_size])) {
									REGISTER
									ntp_stack that_stack = list_iterator_next (&seq_bp->stacks[that_size]);
									
									if (that_stack->stack_idist == idist &&
									    that_stack->in_extrusion == paired_elements_in_extrusion[i]) {
										REGISTER
										ntp_bp_list_by_element that_list_by_element = that_stack->lists;
										
										while (that_list_by_element) {
											if (that_list_by_element->el == contained_paired_elements[i]) {
												if (that_list_by_element->stack_counts) {
													list_iterator_start (&that_list_by_element->list);
													
													while (list_iterator_hasnext (&that_list_by_element->list)) {
														REGISTER
														ntp_bp that_bp = list_iterator_next (&that_list_by_element->list);
														
														if (this_bp->fp_posn + (this_stack_size + 1) + fp_dist < that_bp->fp_posn) {
															/*
															 * those (i.e. that_bp's) bps are assumed to be ordered (5'->3')
															 * such that fp_posn of bp (X+1) >= fp_posn of bp (X) fro any X,
															 * so if that_bp's fp_posn is already > this_bp...fp_dist,
															 * there there is no need to visit that bp or successive ones
															 */
															break;
														}
														
														if (this_bp->fp_posn + (this_stack_size + 1) + fp_dist == that_bp->fp_posn &&
														    this_bp->tp_posn == (that_bp->tp_posn + (that_size + 1) + tp_dist)) {
															if (!this_bp_matched_this_paired_element) {
																this_bp_matched_this_paired_element = true;
																bp_matched_paired_element_count[b]++;
															}
															
															candidate_bp[i][num_candidate_bps[i]] = that_bp;
															candidate_bp_stack_size[i][num_candidate_bps[i]] = that_size;
															candidate_bp_next_min[i][num_candidate_bps[i]] = next_paired_elements_min[i];
															candidate_bp_next_max[i][num_candidate_bps[i]] = next_paired_elements_max[i];
															num_candidate_bps[i]++;
														}
													}
													
													list_iterator_stop (&that_list_by_element->list);
												}
												
												break;
											}
											
											that_list_by_element = that_list_by_element->next;
										}
									}
								}
								
								list_iterator_stop (&seq_bp->stacks[that_size]);
							}
						}
					}
				}
			}
			
			if (bp_matched_paired_element_count[b] == num_contained_paired_elements) {
				ushort candidate_idxs[MAX_CONTAINMENT_ELEMENTS];
				REGISTER ushort i = 0;
				
				for (; i < num_contained_paired_elements; i++) {
					candidate_idxs[i] = 0;
				}
				
				i = 0;
				REGISTER
				ntp_bp prev_bp = NULL, current_bp;
				REGISTER
				nt_stack_size prev_bp_stack_size = 0, current_bp_stack_size;
				REGISTER
				bool fail = false;
				
				while (i < num_contained_paired_elements && !fail) {
					if (!contained_paired_elements[i]->paired->min) {
						i++;
                        // skip pairs with min stack size 0
						continue;					
					}
					
					// move in 5' to 3' direction from first set of candidate bps (for a single
					// contained paired element to the next; moving further down each candidate
					// bp list (using candidate_idxs) every time a fail between any two adjacent
					// contained paired element candidates is detected (i.e. an overlap) until
					// either num_contained_paired_elements are matched or no more candidates
					
					if (!i) {
						do {
							prev_bp = candidate_bp[0][candidate_idxs[0]];
							prev_bp_stack_size = candidate_bp_stack_size[0][candidate_idxs[0]];
							
							if (num_contained_paired_elements == 1 &&
							    (this_bp->fp_posn + this_stack_size + this_stack_idist <
							     prev_bp->tp_posn + prev_bp_stack_size +
							     candidate_bp_next_min[0][candidate_idxs[0]] ||
							     this_bp->fp_posn + this_stack_size + this_stack_idist >
							     prev_bp->tp_posn + prev_bp_stack_size +
							     candidate_bp_next_max[0][candidate_idxs[0]])) {
								if (candidate_idxs[0] < num_candidate_bps[0] - 1) {
									candidate_idxs[0]++;
								}
								
								else {
									fail = true;
									break;
								}
							}
							
							else {
								break;
							}
						}
						while (1);
						
						i = 1;
					}
					
					else {
						current_bp = candidate_bp[i][candidate_idxs[i]];
						current_bp_stack_size = candidate_bp_stack_size[i][candidate_idxs[i]];
						REGISTER
						nt_rel_count diff_between_current_fp_prev_tp = (nt_rel_count) (
						                                        current_bp->fp_posn - (prev_bp->tp_posn + prev_bp_stack_size + 1));
						                                        
						if (diff_between_current_fp_prev_tp >= candidate_bp_next_min[i -
						                                        1][candidate_idxs[i - 1]] &&
						    diff_between_current_fp_prev_tp <= candidate_bp_next_max[i - 1][candidate_idxs[i
						                                            - 1]]) {
							if (i < num_contained_paired_elements - 1 ||
							    (this_bp->fp_posn + this_stack_size + this_stack_idist >=
							     current_bp->tp_posn + current_bp_stack_size +
							     candidate_bp_next_min[i][candidate_idxs[i]] &&
							     this_bp->fp_posn + this_stack_size + this_stack_idist <=
							     current_bp->tp_posn + current_bp_stack_size +
							     candidate_bp_next_max[i][candidate_idxs[i]])) {
								// prev and current bp are compatible -> move further 3'
								prev_bp = current_bp;
								prev_bp_stack_size = current_bp_stack_size;
								i++;
								continue;
							}
						}
						
						do {
							if (candidate_idxs[i] < num_candidate_bps[i] - 1) {
								candidate_idxs[i]++;
								break;
							}
							
							else {
								if (i) {
									candidate_idxs[i] = 0;
									i--;
								}
								
								else {
									fail = true;
									break;
								}
							}
						}
						while (1);
					}
				}
				
				bp_matched_paired_element_set[b] = !fail;
			}
		}
		
		REGISTER nt_hit_count b = 0, p = 0;
		
		while (b < this_list_by_element->stack_counts) {
			if (!bp_matched_paired_element_set[p]) {
				ntp_bp bad_bp = list_extract_at (&this_list_by_element->list, b);
				FREE_DEBUG (bad_bp, NULL);              // TODO: check extracted bp
				this_list_by_element->stack_counts--;
				bps_removed = true;
			}
			
			else {
				b++;
			}
			
			p++;
		}
	}
	
	/*
	 * note: assumes that the_list_by_element cleanup (if stack_counts==0) is done by the caller
	 */
	return bps_removed;
}

static inline bool optimize_seq_bp_up (ntp_seq_bp restrict seq_bp,
                                       nt_stack_size this_size) {
	ntp_element containing_paired_element = NULL;
	nt_rel_count paired_element_fp_dist_min = 0, paired_element_fp_dist_max = 0,
	             paired_element_tp_dist_min = 0, paired_element_tp_dist_max = 0;
	REGISTER
	bool bps_removed = false;
	/*
	 * iterate over all idist, in_extrusion stacks for this element
	 */
	REGISTER
	ntp_list stacks_for_size = &seq_bp->stacks[this_size];
	
	for (REGISTER nt_hit_count t = 0; t < stacks_for_size->numels; t++) {
		REGISTER
		ntp_stack this_stack = list_get_at (stacks_for_size, t);
		REGISTER
		ntp_bp_list_by_element this_list_by_element = this_stack->lists,
		                       prev_list_by_element = NULL;
		                       
		while (this_list_by_element) {
			if (0 >= this_stack->in_extrusion) {
				if (optimize_seq_bp_down (seq_bp, this_size, this_stack->stack_idist,
				                          this_list_by_element)) {
					bps_removed = true;
				}
			}
			
			if (0 <= this_stack->in_extrusion && this_list_by_element->stack_counts) {
				if (get_containing_paired_element_dist_by_element
				    (&containing_paired_element,
				     &paired_element_fp_dist_min, &paired_element_fp_dist_max,
				     &paired_element_tp_dist_min, &paired_element_tp_dist_max,
				     NULL, seq_bp->model, seq_bp->model->first_element, this_list_by_element->el,
				     (nt_stack_size) (this_size + 1), this_stack->stack_idist) &&
				    containing_paired_element) {
					/*
					 * iterate over all this stack's bps testing containment for each containing paired element;
					 * keep bp if containment found in at least one case
					 */
					REGISTER
					ulong this_stack_numels = this_list_by_element->list.numels;
					REGISTER
					ulong b = 0;
					
					while (b < this_stack_numels) {
						ntp_bp this_bp = list_get_at (&this_list_by_element->list, b);
						REGISTER
						bool found_first_containing_stack = false;
						
						/*
						 * for each containing element, iterate over all fp/tp distances between contained and containing paired element
						 */
						for (REGISTER nt_rel_count fp_dist = paired_element_fp_dist_min;
						     fp_dist <= paired_element_fp_dist_max; fp_dist++) {
							for (REGISTER nt_rel_count tp_dist = paired_element_tp_dist_min;
							     tp_dist <= paired_element_tp_dist_max; tp_dist++) {
								/*
								 * start with min pos_var of containing paired element; try all
								 * pos_vars up to max value but can stop if/when a match is found
								 */
								for (REGISTER nt_stack_size s = (nt_stack_size) (
								                                        containing_paired_element->paired->min > 0 ?
								                                        containing_paired_element->paired->min - 1 : 0);
								     s < containing_paired_element->paired->max; s++) {
									REGISTER
									ntp_list stacks_for_that_size = &seq_bp->stacks[s];
									list_iterator_start (stacks_for_that_size);
									
									while (list_iterator_hasnext (stacks_for_that_size)) {
										REGISTER
										ntp_stack that_stack = list_iterator_next (stacks_for_that_size);
										REGISTER
										ntp_bp_list_by_element that_list_by_element = that_stack->lists;
										
										while (that_list_by_element) {
											if (that_list_by_element->el == containing_paired_element) {
												if (that_list_by_element->stack_counts &&
												    that_stack->stack_idist == fp_dist + ((this_size + 1) * 2) +
												    this_stack->stack_idist + tp_dist) {
													list_iterator_start (&that_list_by_element->list);
													
													while (list_iterator_hasnext (&that_list_by_element->list)) {
														ntp_bp that_bp = list_iterator_next (&that_list_by_element->list);
														
														if (that_bp->fp_posn + (s + 1) + fp_dist > this_bp->fp_posn) {
															/*
															 * those (that)bps are assumed to be ordered (5'->3') such
															 * that fp_posn of bp (X+1) >= fp_posn of bp (X) fro any X,
															 * so if that_bp's fp_posn is already > this_bp...fp_dist,
															 * there there is no need to visit that bp or successive ones
															 */
															break;
														}
														
														if (that_bp->fp_posn + (s + 1) + fp_dist == this_bp->fp_posn &&
														    that_bp->tp_posn == (this_bp->tp_posn + (this_size + 1) + tp_dist)) {
															found_first_containing_stack = true;
															break;
														}
													}
													
													list_iterator_stop (&that_list_by_element->list);
													
													if (found_first_containing_stack) {
														break;
													}
												}
												
												break;
											}
											
											that_list_by_element = that_list_by_element->next;
										}
										
										if (found_first_containing_stack) {
											break;
										}
									}
									
									list_iterator_stop (stacks_for_that_size);
									
									if (found_first_containing_stack) {
										break;
									}
								}
								
								if (found_first_containing_stack) {
									break;
								}
							}
							
							if (found_first_containing_stack) {
								break;
							}
						}
						
						if (!found_first_containing_stack) {
							// remove this bp
							ntp_bp bad_bp = list_extract_at (&this_list_by_element->list, b);
							FREE_DEBUG (bad_bp, NULL);              // TODO: check extracted bp
							this_list_by_element->stack_counts--;
							this_stack_numels--;
							bps_removed = true;
						}
						
						else {
							// test next bp
							b++;
						}
					}
				}
			}
			
			/*
			 * clean up any empty list_by_element's
			 */
			if (!this_list_by_element->stack_counts) {
				if (prev_list_by_element) {
					prev_list_by_element->next = this_list_by_element->next;
				}
				
				else {
					this_stack->lists = this_list_by_element->next;
				}
				
				list_destroy (&this_list_by_element->list);
				FREE_DEBUG (this_list_by_element,
				            "ntp_list_by_element of stack of seq_bp in optimize_seq_bp_up");
				            
				if (prev_list_by_element) {
					this_list_by_element = prev_list_by_element->next;
				}
				
				else {
					this_list_by_element = this_stack->lists;
				}
			}
			
			else {
				prev_list_by_element = this_list_by_element;
				this_list_by_element = this_list_by_element->next;
			}
		}
	}
	
	return bps_removed;
}

bool optimize_seq_bp_by_constraint (const nt_model *restrict model,
                                    ntp_seq restrict seq,
                                    ntp_stack restrict this_stack, const nt_stack_size this_stack_size) {
	nt_s_rel_count  constraint_fp_offset_min, constraint_fp_offset_max,
	                constraint_tp_offset_min, constraint_tp_offset_max,
	                constraint_single_offset_min, constraint_single_offset_max;
	REGISTER
	bool bps_removed = false;
	REGISTER
	nt_rel_seq_len seq_len = (nt_rel_seq_len) strlen (seq);
	REGISTER
	ntp_bp_list_by_element this_list_by_element = this_stack->lists,
	                       prev_list_by_element = NULL;
	                       
	while (this_list_by_element) {
		REGISTER nt_hit_count orig_stack_count = this_list_by_element->stack_counts;
		ushort bp_num_constraints_passed[MAX_ELEMENT_MATCHES];
		
		for (REGISTER nt_hit_count i = 0; i < orig_stack_count; i++) {
			bp_num_constraints_passed[i] = 0;
		}
		
		REGISTER
		ushort num_constraints_done = 0;
		REGISTER
		ntp_constraint this_constraint = model->first_constraint;
		
		while (this_constraint) {
			REGISTER
			nt_rel_seq_len min_constraint_len = (nt_rel_seq_len) (this_constraint->type ==
			                                        pseudoknot
			                                        ? this_constraint->pseudoknot->fp_element->unpaired->min : 1),
			                                    max_constraint_len = (nt_rel_seq_len) (this_constraint->type == pseudoknot ?
			                                                                            this_constraint->pseudoknot->fp_element->unpaired->max
			                                                                            : 1);
			constraint_fp_offset_min = 0;
			constraint_fp_offset_max = 0;
			constraint_tp_offset_min = 0;
			constraint_tp_offset_max = 0;
			constraint_single_offset_min = 0;
			constraint_single_offset_max = 0;
			bool bp_passed_this_constraint_set[MAX_ELEMENT_MATCHES];
			
			for (REGISTER nt_hit_count i = 0; i < orig_stack_count; i++) {
				bp_passed_this_constraint_set[i] = false;
			}
			
			REGISTER
			bool found_constraint;
			
			if ((found_constraint = get_next_constraint_offsets_by_element
			                        (&constraint_fp_offset_min, &constraint_fp_offset_max,
			                         &constraint_tp_offset_min, &constraint_tp_offset_max,
			                         &constraint_single_offset_min, &constraint_single_offset_max,
			                         model, model->first_element, this_list_by_element->el, this_constraint,
			                         (nt_stack_size) (this_stack_size + 1), this_stack->stack_idist)) == true) {
				if (!constraint_fp_offset_min) {
					// constraint overlaps element -> automatically pass all bps that have not passed this constraint set yet
					for (REGISTER nt_hit_count b = 0; b < orig_stack_count; b++) {
						if (!bp_passed_this_constraint_set[b]) {
							bp_num_constraints_passed[b]++;
						}
					}
					
					// move on to next constraint (set)
					break;
				}
				
				REGISTER
				nt_s_rel_count orig_constraint_fp_offset_min = constraint_fp_offset_min,
				               orig_constraint_tp_offset_min = constraint_tp_offset_min,
				               orig_constraint_single_offset_min = constraint_single_offset_min;
				               
				if (constraint_fp_offset_min < 0) {
					constraint_fp_offset_min = constraint_fp_offset_max;
					constraint_fp_offset_max = orig_constraint_fp_offset_min;
				}
				
				if (constraint_tp_offset_min < 0) {
					constraint_tp_offset_min = constraint_tp_offset_max;
					constraint_tp_offset_max = orig_constraint_tp_offset_min;
				}
				
				if (constraint_single_offset_min < 0) {
					constraint_single_offset_min = constraint_single_offset_max;
					constraint_single_offset_max = orig_constraint_single_offset_min;
				}
				
				REGISTER nt_hit_count b = 0;
				REGISTER bool one_ore_more_bps_failed_this_constraint = false;
				
				while (b < orig_stack_count) {
					REGISTER
					ntp_bp this_bp = list_get_at (&this_list_by_element->list, b);
					
					if (bp_num_constraints_passed[b] < num_constraints_done ||
					    bp_passed_this_constraint_set[b]) {
						b++;
						continue;
					}
					
					if (1 <= this_bp->fp_posn + orig_constraint_fp_offset_min &&
					    seq_len >= this_bp->fp_posn + orig_constraint_tp_offset_min) {
						for (REGISTER nt_s_rel_count fp_offset = constraint_fp_offset_min;
						     fp_offset <= constraint_fp_offset_max; fp_offset++) {
							for (REGISTER nt_s_rel_count tp_offset = constraint_tp_offset_min;
							     tp_offset <= constraint_tp_offset_max; tp_offset++) {
								if (1 <= this_bp->fp_posn + fp_offset &&
								    seq_len >= this_bp->fp_posn + tp_offset) {
									if (this_constraint->type == pseudoknot) {
										REGISTER
										nt_rel_seq_len this_constraint_len = min_constraint_len;
										
										/*
										 * TODO: iterating over min_ max_constraint_len is redundant (has overlapping nt ranges) -> need to optimize
										 */
										while (1) {
											if (this_bp->fp_posn + tp_offset + this_constraint_len - 2 >= seq_len) {
												break;
											}
											
											REGISTER nt_rel_seq_len i = 0;
											
											while (i < this_constraint_len) {
												if (this_bp->fp_posn + fp_offset - 1 + i < 0 ||
												    this_bp->fp_posn + fp_offset - 1 + i >= seq_len ||
												    this_bp->fp_posn + tp_offset + this_constraint_len - 2 - i < 0 ||
												    this_bp->fp_posn + tp_offset + this_constraint_len - 2 - i >=
												    seq_len) {
													break;
												}
												
												REGISTER
												const char fp_nt = seq[this_bp->fp_posn + fp_offset - 1 + i],
												           tp_nt = seq[this_bp->fp_posn + tp_offset + this_constraint_len - 2 - i];
												           
												if (! ((fp_nt == 'g' && (tp_nt == 'c' || tp_nt == 'u')) ||
												       (fp_nt == 'a' && tp_nt == 'u') ||
												       (tp_nt == 'g' && (fp_nt == 'c' || fp_nt == 'u')) ||
												       (tp_nt == 'a' && fp_nt == 'u'))) {
													break;
												}
												
												i++;
											}
											
											if (i == this_constraint_len) {
												bp_passed_this_constraint_set[b] = true;
												bp_num_constraints_passed[b]++;
												break;
											}
											
											if (this_constraint_len < max_constraint_len) {
												this_constraint_len++;
											}
											
											else {
												break;
											}
										}
										
										if (bp_passed_this_constraint_set[b]) {
											break;
										}
										
										else {
											one_ore_more_bps_failed_this_constraint = true;
										}
									}
									
									else {
										if (this_bp->fp_posn + fp_offset - 1 >= 0 &&
										    this_bp->fp_posn + fp_offset - 1 < seq_len &&
										    this_bp->fp_posn + tp_offset + min_constraint_len - 2 >= 0 &&
										    this_bp->fp_posn + tp_offset + min_constraint_len - 2 < seq_len) {
											REGISTER
											const char fp_nt = seq[this_bp->fp_posn + fp_offset - 1],
											           tp_nt = seq[this_bp->fp_posn + tp_offset + min_constraint_len -
											                                        2]; // note: can use min_constraint_len==1 for BTs
											                                        
											for (REGISTER nt_s_rel_count single_offset = constraint_single_offset_min;
											     single_offset <= constraint_single_offset_max; single_offset++) {
												/*
												 * TODO: iterating single offset over all fp and tp offsets is redundant -> need to optimize
												 */
												REGISTER
												const short this_single_nt_posn = (short) (this_bp->fp_posn + single_offset -
												                                        1);
												                                        
												if (this_single_nt_posn >= 0 && this_single_nt_posn < seq_len) {
													REGISTER
													const char single_nt = seq[this_single_nt_posn];
													
													if (constraint_single_offset_min <
													    constraint_fp_offset_min) {
														// "left-handed base triple"
														if ((single_nt == 'c' && fp_nt == 'g' && (tp_nt == 'c' || tp_nt == 'u')) ||
														    (single_nt == 'u' && fp_nt == 'a' && tp_nt == 'u')) {
															bp_passed_this_constraint_set[b] = true;
															bp_num_constraints_passed[b]++;
															break;
														}
													}
													
													else {
														// "right-handed base triple"
														if (((fp_nt == 'c' || fp_nt == 'u') && tp_nt == 'g' && single_nt == 'c') ||
														    (fp_nt == 'u' && tp_nt == 'a' && single_nt == 'u')) {
															bp_passed_this_constraint_set[b] = true;
															bp_num_constraints_passed[b]++;
															break;
														}
													}
												}
											}
										}
										
										if (!bp_passed_this_constraint_set[b]) {
											one_ore_more_bps_failed_this_constraint = true;
										}
									}
								}
								
								else {
									one_ore_more_bps_failed_this_constraint = true;
								}
								
								if (bp_passed_this_constraint_set[b]) {
									break;
								}
							}
							
							if (bp_passed_this_constraint_set[b]) {
								break;
							}
						}
						
						if (!bp_passed_this_constraint_set[b]) {
							one_ore_more_bps_failed_this_constraint = true;
						}
					}
					
					else {
						one_ore_more_bps_failed_this_constraint = true;
					}
					
					b++;
				}
				
				if (!one_ore_more_bps_failed_this_constraint) {
					// no bps failed this constraint -> no need to go through any remaining constraints
					break;
				}
				
				constraint_fp_offset_min = 0;
				constraint_fp_offset_max = 0;
				constraint_tp_offset_min = 0;
				constraint_tp_offset_max = 0;
				constraint_single_offset_min = 0;
				constraint_single_offset_max = 0;
			}
			
			this_constraint = this_constraint->next;
			
			if (found_constraint) {
				// only increment the constraints done 'counter' if at least one constraint included in this constraint set
				num_constraints_done++;
			}
		}
		
		REGISTER nt_hit_count b = 0;
		
		for (nt_hit_count i = 0; i < orig_stack_count; i++) {
			if (bp_num_constraints_passed[i] < num_constraints_done) {
				ntp_bp bad_bp = list_extract_at (&this_list_by_element->list, b);
				FREE_DEBUG (bad_bp, NULL);              // TODO: check extracted bp
				this_list_by_element->stack_counts--;
				bps_removed = true;
			}
			
			else {
				b++;
			}
		}
		
		/*
		 * clean up any empty list_by_element's
		 */
		if (!this_list_by_element->stack_counts) {
			if (prev_list_by_element) {
				prev_list_by_element->next = this_list_by_element->next;
			}
			
			else {
				this_stack->lists = this_list_by_element->next;
			}
			
			list_destroy (&this_list_by_element->list);
			FREE_DEBUG (this_list_by_element,
			            "ntp_list_by_element of stack of seq_bp in optimize_seq_bp_by_constraint");
			            
			if (prev_list_by_element) {
				this_list_by_element = prev_list_by_element->next;
			}
			
			else {
				this_list_by_element = this_stack->lists;
			}
		}
		
		else {
			prev_list_by_element = this_list_by_element;
			this_list_by_element = this_list_by_element->next;
		}
	}
	
	return bps_removed;
}

void optimize_seq_bp (ntp_seq_bp restrict seq_bp) {
	REGISTER
	bool bps_removed_this_pass;
	REGISTER
	ushort pass = 1;
	
	do {
		bps_removed_this_pass = false;
		
		/*
		 * prune search space using relative positions of bps to any constraints available
		 */
		if (seq_bp->model->first_constraint) {
			for (REGISTER nt_stack_size i = 0; i < MAX_STACK_LEN; i++) {
				REGISTER
				ntp_list this_list = &seq_bp->stacks[i];
				list_iterator_start (this_list);
				
				while (list_iterator_hasnext (this_list)) {
					REGISTER
					ntp_stack this_stack = list_iterator_next (this_list);
					bps_removed_this_pass = optimize_seq_bp_by_constraint (seq_bp->model,
					                                        seq_bp->sequence, this_stack, i) || bps_removed_this_pass;
				}
				
				list_iterator_stop (this_list);
			}
		}
		
		/*
		 * prune search space using relative positions of nested (sub-) helices; both upwards and downwards
		 */
		REGISTER
		bool bps_removed;
		bool previous_iteration_bps_removed[MAX_STACK_LEN];
		
		for (REGISTER ushort i = 0; i < MAX_STACK_LEN; i++) {
			previous_iteration_bps_removed[i] = true; // initialize so as to optimize
		}
		
		do {
			bps_removed = false; // stop only when no bps removed for all stack lengths
			
			for (REGISTER nt_stack_size i = 0; i < MAX_STACK_LEN; i++) {
				// initially, assume that no bp removals for this stack length and iteration;
				previous_iteration_bps_removed[i] =
				                    false;
				                    
				// only need to check for bps in this stack if either bps for one stack longer or shorter have changed in the previous iteration
				if ((MAX_STACK_LEN - 1 > i && previous_iteration_bps_removed[i + 1]) ||
				    (0 < i && previous_iteration_bps_removed[i - 1])) {
					previous_iteration_bps_removed[i] = optimize_seq_bp_up (seq_bp, i);
					
					if (previous_iteration_bps_removed[i]) {
						bps_removed = true;
						bps_removed_this_pass = true;
					}
				}
			}
		}
		while (bps_removed);
		
		pass++;
	}
	while (bps_removed_this_pass);
}
