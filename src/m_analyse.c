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

char get_element_pos_var_range (const struct _nt_element *restrict el) {
	#ifndef NO_FULL_CHECKS

	if (el) {
	#endif
	
		if (el->type == unpaired) {
			return el->unpaired->max - el->unpaired->min;
		}
		
		else
		#ifndef NO_FULL_CHECKS
			if (el->type == paired)
		#endif
			{
				return el->paired->max - el->paired->min;
			}
			
		#ifndef NO_FULL_CHECKS
			
			else {
				COMMIT_DEBUG (REPORT_ERRORS, MODEL,
				              "no_element_type element_type retrieved for ntp_element el in get_element_pos_var_ul",
				              true);
			}
	}
	
	else {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "NULL ntp_element el in get_element_pos_var_ul", true);
	}
	
	return -1;
		#endif
}

bool traverse_and_count_in_paired_element
(const nt_element *restrict el,
 nt_stack_idist *min_count,
 nt_stack_idist *max_count) {
	if (el->type == paired) {
		#ifndef NO_FULL_CHECKS
	
		if (el->paired->fp_next != NULL) {
		#endif
			nt_stack_idist min_fp_count = 0, max_fp_count = 0;
			
			if (traverse_and_count_in_paired_element (el->paired->fp_next, &min_fp_count,
			                                        &max_fp_count)) {
				*min_count += min_fp_count;
				*max_count += max_fp_count;
			}
			
			else {
				return false;
			}
			
			#ifndef NO_FULL_CHECKS
		}
		
		else {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "fp_next of el is NULL in traverse_and_count_in_paired_element", false);
			return false;
		}
		
			#endif
		*min_count += el->paired->min * 2;
		*max_count += el->paired->max * 2;
		
		if (el->paired->tp_next != NULL) {
			nt_stack_idist min_tp_count = 0, max_tp_count = 0;
			
			if (traverse_and_count_in_paired_element (el->paired->tp_next, &min_tp_count,
			                                        &max_tp_count)) {
				*min_count += min_tp_count;
				*max_count += max_tp_count;
			}
			
			else {
				return false;
			}
		}
		
		return true;
	}
	
	else
	#ifndef NO_FULL_CHECKS
		if (el->type == unpaired)
	#endif
		{
			*min_count += el->unpaired->min;
			*max_count += el->unpaired->max;
			
			if (el->unpaired->next != NULL) {
				nt_stack_idist min_next_count = 0, max_next_count = 0;
				
				if (traverse_and_count_in_paired_element (el->unpaired->next, &min_next_count,
				                                        &max_next_count)) {
					*min_count += min_next_count;
					*max_count += max_next_count;
				}
				
				else {
					return false;
				}
			}
			
			return true;
		}
		
	#ifndef NO_FULL_CHECKS
	COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
	              "element type is neither paired nor unpaired in traverse_and_count_in_paired_element",
	              false);
	return false;
	#endif
}

ntp_element get_containing_paired_element (nt_element *restrict
                                        last_paired_element, nt_element *restrict this_element,
                                        const nt_element *restrict target_el) {
	// TODO: assumes fp_next for paired is never NULL
	if (this_element->type == paired) {
		if (this_element == target_el) {
			return last_paired_element;
		}
		
		REGISTER
		ntp_element fp_element = get_containing_paired_element (this_element,
		                                        this_element->paired->fp_next, target_el);
		                                        
		if (fp_element) {
			return fp_element;
		}
		
		else
			if (this_element->paired->tp_next) {
				return get_containing_paired_element (last_paired_element,
				                                      this_element->paired->tp_next, target_el);
			}
			
			else {
				return NULL;
			}
	}
	
	else {
		if (this_element->unpaired->next) {
			return get_containing_paired_element (last_paired_element,
			                                      this_element->unpaired->next, target_el);
		}
		
		else {
			return NULL;
		}
	}
}

bool get_paired_element_relative_index (const nt_element *restrict current_el,
                                        const nt_element *restrict target_el, ushort *restrict target_idx) {
	// traverse paired element hierarchy -> only increment current_idx when going into (fp), "skipping" all unpaired elements
	if (current_el->type == paired) {
		if (current_el == target_el) {
			return true;
		}
		
		if (current_el->paired->fp_next) {
			(*target_idx)++;
			
			if (get_paired_element_relative_index (current_el->paired->fp_next, target_el,
			                                       target_idx)) {
				return true;
			}
		}
		
		if (current_el->paired->tp_next) {
			if (get_paired_element_relative_index (current_el->paired->tp_next, target_el,
			                                       target_idx)) {
				return true;
			}
		}
		
		return false;
	}
	
	else {
		if (current_el->unpaired->next) {
			return get_paired_element_relative_index (current_el->unpaired->next, target_el,
			                                        target_idx);
		}
		
		else {
			return false;
		}
	}
}

static inline ntp_element get_tp_element (ntp_element el) {
	while (NULL != el) {
		if (el->type == unpaired) {
			if (NULL == el->unpaired->next) {
				return el;
			}
			
			else {
				el = el->unpaired->next;
			}
		}
		
		else {
			if (NULL == el->paired->tp_next) {
				return el;
			}
			
			else {
				el = el->paired->tp_next;
			}
		}
	}
	
	return NULL;
}

bool get_stack_distances_in_paired_element (const nt_model *restrict model,
                                        const nt_element *restrict el,
                                        const nt_element *restrict prev_el,
                                        nt_stack_idist *restrict min_stack_dist,
                                        nt_stack_idist *restrict max_stack_dist, short *restrict in_extrusion) {
	*min_stack_dist = 0;
	*max_stack_dist = 0;
	*in_extrusion = 0;
	REGISTER
	ntp_element fp_next = el->paired->fp_next, tp_next = el->paired->tp_next;
	
	if (!fp_next) {
		return false;
	}
	
	REGISTER
	bool success = traverse_and_count_in_paired_element (fp_next, min_stack_dist,
	                                        max_stack_dist);
	                                        
	if (success) {
		if (fp_next->type == unpaired &&
		    fp_next->unpaired->i_constraint.reference &&
		    fp_next->unpaired->i_constraint.reference->type == base_triple &&
		    fp_next->unpaired->i_constraint.element_type == constraint_fp_element) {
			const ntp_element tp_element = get_tp_element (fp_next);
			
			if (tp_element &&
			    tp_element->type == unpaired &&
			    tp_element->unpaired->i_constraint.reference &&
			    tp_element->unpaired->i_constraint.reference->type == base_triple &&
			    tp_element->unpaired->i_constraint.element_type ==
			    constraint_tp_element) {
				(*in_extrusion)++;
				
				if (fp_next->unpaired->next && fp_next->unpaired->next->type == paired) {
					(*in_extrusion) += fp_next->unpaired->next->paired->min;
				}
			}
		}
		
		else
			if (tp_next &&
			    tp_next->type == unpaired &&
			    tp_next->unpaired->i_constraint.reference &&
			    tp_next->unpaired->i_constraint.reference->type == base_triple &&
			    tp_next->unpaired->i_constraint.element_type == constraint_tp_element &&
			    NULL != prev_el && prev_el->type == unpaired &&
			    prev_el->unpaired->i_constraint.reference &&
			    prev_el->unpaired->i_constraint.reference->type == base_triple &&
			    prev_el->unpaired->i_constraint.element_type ==
			    constraint_fp_element) {
				(*in_extrusion)--;
				ntp_element containing_element = get_containing_paired_element (NULL,
				                                        model->first_element, el);
				                                        
				if (containing_element &&
				    containing_element->paired->fp_next->type == unpaired &&
				    containing_element->paired->fp_next == prev_el &&
				    NULL == tp_next->unpaired->next) {
					(*in_extrusion) -= containing_element->paired->min;
				}
			}
	}
	
	return success;
}

void get_nested_pair_distances (nt_rel_count *restrict
                                paired_elements_fp_dist_min, nt_rel_count *restrict paired_elements_fp_dist_max,
                                nt_rel_count *restrict paired_elements_tp_dist_min,
                                nt_rel_count *restrict paired_elements_tp_dist_max,
                                const nt_model *restrict model,
                                nt_element *restrict parent_paired_element,
                                nt_element *restrict child_paired_element) {
	*paired_elements_fp_dist_min = 0;
	*paired_elements_fp_dist_max = 0;
	REGISTER
	ntp_element prev_element = parent_paired_element,
	            this_element = parent_paired_element->paired->fp_next;
	            
	while (this_element) {
		if (this_element != child_paired_element) {
			if (this_element->type == unpaired) {
				*paired_elements_fp_dist_min += this_element->unpaired->min;
				*paired_elements_fp_dist_max += this_element->unpaired->max;
				prev_element = this_element;		
				this_element = this_element->unpaired->next;
			}
			
			else {
				nt_stack_idist min_stack_idist = 0, max_stack_idist = 0;
				short in_extrusion = 0;
				get_stack_distances_in_paired_element (model, this_element,
				                                       prev_element,
				                                       &min_stack_idist, &max_stack_idist, &in_extrusion);
				(*paired_elements_fp_dist_min) += (this_element->paired->min * 2) +
				                                  min_stack_idist;
				(*paired_elements_fp_dist_max) += (this_element->paired->max * 2) +
				                                  max_stack_idist;
				prev_element = this_element;				
				this_element = this_element->paired->tp_next;
			}
		}
		
		else {
			break;
		}
	}
	
	if (this_element) {
		*paired_elements_tp_dist_min = 0;
		*paired_elements_tp_dist_max = 0;
		prev_element = this_element;		
		this_element = this_element->paired->tp_next; // move past child_paired_element
		
		while (this_element) {
			if (this_element != child_paired_element) {
				if (this_element->type == unpaired) {
					*paired_elements_tp_dist_min += this_element->unpaired->min;
					*paired_elements_tp_dist_max += this_element->unpaired->max;
					prev_element = this_element;		
					this_element = this_element->unpaired->next;
				}
				
				else {
					nt_stack_idist min_stack_idist = 0, max_stack_idist = 0;
					short in_extrusion = 0;
					get_stack_distances_in_paired_element (model, this_element,
					                                       prev_element, 
					                                       &min_stack_idist, &max_stack_idist, &in_extrusion);
					(*paired_elements_tp_dist_min) += (this_element->paired->min * 2) +
					                                  min_stack_idist;
					(*paired_elements_tp_dist_max) += (this_element->paired->max * 2) +
					                                  max_stack_idist;
					prev_element = this_element; 		
					this_element = this_element->paired->tp_next;
				}
			}
			
			else {
				break;
			}
		}
	}
}

/*
 * note: requires iniitialization of constraint_*_dist_* to zero, besides initialization of found_constraint_* to false and current_element to the model's first element
 */
bool get_constraint_dist_from_fp (bool *found_constraint_fp,
                                  bool *found_constraint_tp, bool *found_constraint_single,
                                  nt_s_rel_count *restrict constraint_tp_dist_min,
                                  nt_s_rel_count *restrict constraint_tp_dist_max,
                                  nt_s_rel_count *restrict constraint_single_dist_min,
                                  nt_s_rel_count *restrict constraint_single_dist_max,
                                  ntp_element restrict current_element, ntp_constraint restrict this_constraint) {
	if (current_element->type == unpaired &&
	    current_element->unpaired->i_constraint.reference == this_constraint) {
		if (!*found_constraint_fp &&
		    current_element->unpaired->i_constraint.element_type == constraint_fp_element) {
			*found_constraint_fp = true;
		}
		
		else
			if (!*found_constraint_tp &&
			    current_element->unpaired->i_constraint.element_type == constraint_tp_element) {
				*found_constraint_tp = true;
			}
			
			else
				if (!*found_constraint_single &&
				    current_element->unpaired->i_constraint.element_type ==
				    constraint_single_element) {
					*found_constraint_single = true;
				}
	}
	
	if (current_element->type == paired ||
	    current_element->unpaired->i_constraint.reference != this_constraint) {
		nt_s_rel_count this_min, this_max;
		
		if (current_element->type == unpaired) {
			this_min = current_element->unpaired->min;
			this_max = current_element->unpaired->max;
		}
		
		else {
			this_min = current_element->paired->min;
			this_max = current_element->paired->max;
		}
		
		if (*found_constraint_single && !*found_constraint_fp) {
			*constraint_single_dist_min += -this_min;
			*constraint_single_dist_max += -this_max;
		}
		
		if (*found_constraint_fp && base_triple == this_constraint->type &&
		    !*found_constraint_single) {
			*constraint_single_dist_min += this_min;
			*constraint_single_dist_max += this_max;
		}
		
		if (*found_constraint_fp && !*found_constraint_tp) {
			*constraint_tp_dist_min += this_min;
			*constraint_tp_dist_max += this_max;
		}
	}
	
	if ((base_triple == this_constraint->type && *found_constraint_single &&
	     *found_constraint_tp) || *found_constraint_tp) {
		return true;
	}
	
	else {
		if (current_element->type == unpaired) {
			if (current_element->unpaired->next) {
				return get_constraint_dist_from_fp (found_constraint_fp, found_constraint_tp,
				                                    found_constraint_single,
				                                    constraint_tp_dist_min, constraint_tp_dist_max,
				                                    constraint_single_dist_min, constraint_single_dist_max,
				                                    current_element->unpaired->next, this_constraint);
			}
		}
		
		else {
			if (!get_constraint_dist_from_fp (found_constraint_fp, found_constraint_tp,
			                                  found_constraint_single,
			                                  constraint_tp_dist_min, constraint_tp_dist_max,
			                                  constraint_single_dist_min, constraint_single_dist_max,
			                                  current_element->paired->fp_next, this_constraint)) {
				if (*found_constraint_single && !*found_constraint_fp) {
					*constraint_single_dist_min += -current_element->paired->min;
					*constraint_single_dist_max += -current_element->paired->max;
				}
				
				if (*found_constraint_fp && base_triple == this_constraint->type &&
				    !*found_constraint_single) {
					*constraint_single_dist_min += current_element->paired->min;
					*constraint_single_dist_max += current_element->paired->max;
				}
				
				if (*found_constraint_fp && !*found_constraint_tp) {
					*constraint_tp_dist_min += current_element->paired->min;
					*constraint_tp_dist_max += current_element->paired->max;
				}
				
				if (current_element->paired->tp_next) {
					return get_constraint_dist_from_fp (found_constraint_fp, found_constraint_tp,
					                                    found_constraint_single,
					                                    constraint_tp_dist_min, constraint_tp_dist_max,
					                                    constraint_single_dist_min, constraint_single_dist_max,
					                                    current_element->paired->tp_next, this_constraint);
				}
				
				return false;
			}
			
			else {
				return true;
			}
		}
	}
	
	return false;
}

/*
 * note: get_constraint_*_offset_details_from_element differ from get_constraint_fp_offsets_from_elements in that the
 * 	 latter include stack size and idist; also, the former do not include constraint stack distances
 */
bool get_constraint_fp_offset_details_from_element (bool *found_el_fp,
                                        bool *found_constraint_fp,
                                        nt_s_rel_count *restrict constraint_fp_offset_min,
                                        nt_s_rel_count *restrict constraint_fp_offset_max,
                                        ntp_element restrict current_element, ntp_element restrict this_element,
                                        ntp_constraint restrict this_constraint) {
	if (current_element->type == unpaired &&
	    current_element->unpaired->i_constraint.reference == this_constraint &&
	    !*found_constraint_fp) {
		if (current_element->unpaired->i_constraint.element_type ==
		    constraint_fp_element) {
			*found_constraint_fp = true;
		}
	}
	
	else
		if (current_element->type == paired && !*found_el_fp &&
		    current_element == this_element) {
			*found_el_fp = true;
			
			if (!*found_constraint_fp) {
				*constraint_fp_offset_min = 0;
				*constraint_fp_offset_max = 0;
			}
		}
		
	if (current_element != this_element) {
		if ((*found_el_fp && !*found_constraint_fp) || (!*found_el_fp &&
		                                        *found_constraint_fp)) {
			if (current_element->type == unpaired) {
				if (*found_el_fp) {
					*constraint_fp_offset_min += current_element->unpaired->min;
					*constraint_fp_offset_max += current_element->unpaired->max;
				}
				
				else
					if (current_element->unpaired->i_constraint.reference != this_constraint ||
					    current_element->unpaired->i_constraint.element_type != constraint_fp_element) {
						*constraint_fp_offset_min += -current_element->unpaired->min;
						*constraint_fp_offset_max += -current_element->unpaired->max;
					}
			}
			
			else {
				if (*found_el_fp) {
					*constraint_fp_offset_min += current_element->paired->min;
					*constraint_fp_offset_max += current_element->paired->max;
				}
				
				else {
					*constraint_fp_offset_min += -current_element->paired->min;
					*constraint_fp_offset_max += -current_element->paired->max;
				}
			}
		}
	}
	
	if (*found_constraint_fp && *found_el_fp) {
		return true;
	}
	
	else {
		if (current_element->type == unpaired) {
			if (current_element->unpaired->next) {
				return get_constraint_fp_offset_details_from_element (found_el_fp,
				                                        found_constraint_fp,
				                                        constraint_fp_offset_min, constraint_fp_offset_max,
				                                        current_element->unpaired->next, this_element, this_constraint);
			}
		}
		
		else {
			if (current_element == this_element ||
			    !get_constraint_fp_offset_details_from_element (found_el_fp,
			                                            found_constraint_fp,
			                                            constraint_fp_offset_min, constraint_fp_offset_max,
			                                            current_element->paired->fp_next, this_element, this_constraint)) {
				if ((*found_el_fp && !*found_constraint_fp) || (!*found_el_fp &&
				                                        *found_constraint_fp)) {
					if (*found_el_fp) {
						if (current_element != this_element) {
							*constraint_fp_offset_min += current_element->paired->min;
							*constraint_fp_offset_max += current_element->paired->max;
						}
					}
					
					else {
						if (current_element != this_element) {
							*constraint_fp_offset_min += -current_element->paired->min;
							*constraint_fp_offset_max += -current_element->paired->max;
						}
					}
				}
				
				if (current_element->paired->tp_next) {
					return get_constraint_fp_offset_details_from_element (found_el_fp,
					                                        found_constraint_fp,
					                                        constraint_fp_offset_min, constraint_fp_offset_max,
					                                        current_element->paired->tp_next, this_element, this_constraint);
				}
				
				return false;
			}
			
			else {
				return true;
			}
		}
	}
	
	return false;
}

bool get_constraint_tp_offset_details_from_element (bool *found_el_fp,
                                        bool *found_constraint_tp,
                                        nt_s_rel_count *restrict constraint_tp_offset_min,
                                        nt_s_rel_count *restrict constraint_tp_offset_max,
                                        ntp_element restrict current_element, ntp_element restrict this_element,
                                        ntp_constraint restrict this_constraint) {
	if (current_element->type == unpaired &&
	    current_element->unpaired->i_constraint.reference == this_constraint &&
	    !*found_constraint_tp) {
		if (current_element->unpaired->i_constraint.element_type ==
		    constraint_tp_element) {
			*found_constraint_tp = true;
		}
	}
	
	else
		if (current_element->type == paired && !*found_el_fp &&
		    current_element == this_element) {
			*found_el_fp = true;
			
			if (!*found_constraint_tp) {
				*constraint_tp_offset_min = 0;
				*constraint_tp_offset_max = 0;
			}
		}
		
	if (current_element != this_element) {
		if ((*found_el_fp && !*found_constraint_tp) || (!*found_el_fp &&
		                                        *found_constraint_tp)) {
			if (current_element->type == unpaired) {
				if (*found_el_fp) {
					*constraint_tp_offset_min += current_element->unpaired->min;
					*constraint_tp_offset_max += current_element->unpaired->max;
				}
				
				else
					if (current_element->unpaired->i_constraint.reference != this_constraint ||
					    current_element->unpaired->i_constraint.element_type != constraint_tp_element) {
						*constraint_tp_offset_min += -current_element->unpaired->min;
						*constraint_tp_offset_max += -current_element->unpaired->max;
					}
			}
			
			else {
				if (*found_el_fp) {
					*constraint_tp_offset_min += current_element->paired->min;
					*constraint_tp_offset_max += current_element->paired->max;
				}
				
				else {
					*constraint_tp_offset_min += -current_element->paired->min;
					*constraint_tp_offset_max += -current_element->paired->max;
				}
			}
		}
	}
	
	if (*found_constraint_tp && *found_el_fp) {
		return true;
	}
	
	else {
		if (current_element->type == unpaired) {
			if (current_element->unpaired->next) {
				return get_constraint_tp_offset_details_from_element (found_el_fp,
				                                        found_constraint_tp,
				                                        constraint_tp_offset_min, constraint_tp_offset_max,
				                                        current_element->unpaired->next, this_element, this_constraint);
			}
		}
		
		else {
			if (current_element == this_element ||
			    !get_constraint_tp_offset_details_from_element (found_el_fp,
			                                            found_constraint_tp,
			                                            constraint_tp_offset_min, constraint_tp_offset_max,
			                                            current_element->paired->fp_next, this_element, this_constraint)) {
				if ((*found_el_fp && !*found_constraint_tp) || (!*found_el_fp &&
				                                        *found_constraint_tp)) {
					if (*found_el_fp) {
						if (current_element != this_element) {
							*constraint_tp_offset_min += current_element->paired->min;
							*constraint_tp_offset_max += current_element->paired->max;
						}
					}
					
					else {
						if (current_element != this_element) {
							*constraint_tp_offset_min += -current_element->paired->min;
							*constraint_tp_offset_max += -current_element->paired->max;
						}
					}
				}
				
				if (current_element->paired->tp_next) {
					return get_constraint_tp_offset_details_from_element (found_el_fp,
					                                        found_constraint_tp,
					                                        constraint_tp_offset_min, constraint_tp_offset_max,
					                                        current_element->paired->tp_next, this_element, this_constraint);
				}
				
				return false;
			}
			
			else {
				return true;
			}
		}
	}
	
	return false;
}

bool get_constraint_single_offset_details_from_element (bool *found_el_fp,
                                        bool *found_constraint_single,
                                        nt_s_rel_count *restrict constraint_single_offset_min,
                                        nt_s_rel_count *restrict constraint_single_offset_max,
                                        ntp_element restrict current_element, ntp_element restrict this_element,
                                        ntp_constraint restrict this_constraint) {
	if (current_element->type == unpaired &&
	    current_element->unpaired->i_constraint.reference == this_constraint &&
	    !*found_constraint_single) {
		if (current_element->unpaired->i_constraint.element_type ==
		    constraint_single_element) {
			*found_constraint_single = true;
		}
	}
	
	else
		if (current_element->type == paired && !*found_el_fp &&
		    current_element == this_element) {
			*found_el_fp = true;
		}
		
	if (current_element != this_element) {
		if ((*found_el_fp && !*found_constraint_single) || (!*found_el_fp &&
		                                        *found_constraint_single)) {
			if (current_element->type == unpaired) {
				if (*found_el_fp) {
					*constraint_single_offset_min += current_element->unpaired->min;
					*constraint_single_offset_max += current_element->unpaired->max;
				}
				
				else
					if (current_element->unpaired->i_constraint.reference != this_constraint ||
					    current_element->unpaired->i_constraint.element_type !=
					    constraint_single_element) {
						*constraint_single_offset_min += -current_element->unpaired->min;
						*constraint_single_offset_max += -current_element->unpaired->max;
					}
			}
			
			else {
				if (*found_el_fp) {
					*constraint_single_offset_min += current_element->paired->min;
					*constraint_single_offset_max += current_element->paired->max;
				}
				
				else {
					*constraint_single_offset_min += -current_element->paired->min;
					*constraint_single_offset_max += -current_element->paired->max;
				}
			}
		}
	}
	
	if (*found_constraint_single && *found_el_fp) {
		return true;
	}
	
	else {
		if (current_element->type == unpaired) {
			if (current_element->unpaired->next) {
				return get_constraint_single_offset_details_from_element (found_el_fp,
				                                        found_constraint_single,
				                                        constraint_single_offset_min, constraint_single_offset_max,
				                                        current_element->unpaired->next, this_element, this_constraint);
			}
		}
		
		else {
			if (current_element == this_element ||
			    !get_constraint_single_offset_details_from_element (found_el_fp,
			                                            found_constraint_single,
			                                            constraint_single_offset_min, constraint_single_offset_max,
			                                            current_element->paired->fp_next, this_element, this_constraint)) {
				if ((*found_el_fp && !*found_constraint_single) || (!*found_el_fp &&
				                                        *found_constraint_single)) {
					if (*found_el_fp) {
						if (current_element != this_element) {
							*constraint_single_offset_min += current_element->paired->min;
							*constraint_single_offset_max += current_element->paired->max;
						}
					}
					
					else {
						if (current_element != this_element) {
							*constraint_single_offset_min += -current_element->paired->min;
							*constraint_single_offset_max += -current_element->paired->max;
						}
					}
				}
				
				if (current_element->paired->tp_next) {
					return get_constraint_single_offset_details_from_element (found_el_fp,
					                                        found_constraint_single,
					                                        constraint_single_offset_min, constraint_single_offset_max,
					                                        current_element->paired->tp_next, this_element, this_constraint);
				}
				
				return false;
			}
			
			else {
				return true;
			}
		}
	}
	
	return false;
}

bool get_constraint_fp_offsets_from_element (bool *found_el_fp,
                                        bool *found_constraint_fp,
                                        nt_s_rel_count *restrict constraint_fp_offset_min,
                                        nt_s_rel_count *restrict constraint_fp_offset_max,
                                        ntp_element restrict current_element, ntp_element restrict this_element,
                                        ntp_constraint restrict this_constraint,
                                        const nt_stack_size stack_size, const nt_stack_idist stack_idist) {
	if (current_element->type == unpaired &&
	    current_element->unpaired->i_constraint.reference == this_constraint &&
	    !*found_constraint_fp) {
		if (!*found_constraint_fp &&
		    current_element->unpaired->i_constraint.element_type == constraint_fp_element) {
			*found_constraint_fp = true;
		}
	}
	
	else
		if (current_element->type == paired && !*found_el_fp &&
		    current_element == this_element) {
			*found_el_fp = true;
			
			if (!*found_constraint_fp) {
				*constraint_fp_offset_min = stack_size;
				*constraint_fp_offset_max = stack_size;
			}
		}
		
	if (current_element != this_element) {
		if ((*found_el_fp && !*found_constraint_fp) || (!*found_el_fp &&
		                                        *found_constraint_fp)) {
			if (current_element->type == unpaired) {
				if (*found_el_fp) {
					*constraint_fp_offset_min += current_element->unpaired->min;
					*constraint_fp_offset_max += current_element->unpaired->max;
				}
				
				else {
					*constraint_fp_offset_min += -current_element->unpaired->min;
					*constraint_fp_offset_max += -current_element->unpaired->max;
				}
			}
			
			else {
				if (*found_el_fp) {
					*constraint_fp_offset_min += current_element->paired->min;
					*constraint_fp_offset_max += current_element->paired->max;
				}
				
				else {
					*constraint_fp_offset_min += -current_element->paired->min;
					*constraint_fp_offset_max += -current_element->paired->max;
				}
			}
		}
	}
	
	if (*found_constraint_fp && *found_el_fp) {
		return true;
	}
	
	else {
		if (current_element->type == unpaired) {
			if (current_element->unpaired->next) {
				return get_constraint_fp_offsets_from_element (found_el_fp, found_constraint_fp,
				                                        constraint_fp_offset_min, constraint_fp_offset_max,
				                                        current_element->unpaired->next, this_element, this_constraint, stack_size,
				                                        stack_idist);
			}
		}
		
		else {
			if (current_element == this_element ||
			    !get_constraint_fp_offsets_from_element (found_el_fp, found_constraint_fp,
			                                            constraint_fp_offset_min, constraint_fp_offset_max,
			                                            current_element->paired->fp_next, this_element, this_constraint, stack_size,
			                                            stack_idist)) {
				if ((*found_el_fp && !*found_constraint_fp) || (!*found_el_fp &&
				                                        *found_constraint_fp)) {
					if (*found_el_fp) {
						if (current_element != this_element) {
							*constraint_fp_offset_min += current_element->paired->min;
							*constraint_fp_offset_max += current_element->paired->max;
						}
						
						else {
							*constraint_fp_offset_min += stack_idist + stack_size;
							*constraint_fp_offset_max += stack_idist + stack_size;
						}
					}
					
					else {
						if (current_element != this_element) {
							*constraint_fp_offset_min += -current_element->paired->min;
							*constraint_fp_offset_max += -current_element->paired->max;
						}
						
						else {
							*constraint_fp_offset_min += -stack_idist - stack_size;
							*constraint_fp_offset_max += -stack_idist - stack_size;
						}
					}
				}
				
				if (current_element->paired->tp_next) {
					return get_constraint_fp_offsets_from_element (found_el_fp, found_constraint_fp,
					                                        constraint_fp_offset_min, constraint_fp_offset_max,
					                                        current_element->paired->tp_next, this_element, this_constraint, stack_size,
					                                        stack_idist);
				}
				
				return false;
			}
			
			else {
				return true;
			}
		}
	}
	
	return false;
}

bool get_constraint_tp_offsets_from_element (bool *found_el_fp,
                                        bool *found_constraint_tp,
                                        nt_s_rel_count *restrict constraint_tp_offset_min,
                                        nt_s_rel_count *restrict constraint_tp_offset_max,
                                        ntp_element restrict current_element, ntp_element restrict this_element,
                                        ntp_constraint restrict this_constraint,
                                        const nt_stack_size stack_size, const nt_stack_idist stack_idist) {
	if (current_element->type == unpaired &&
	    current_element->unpaired->i_constraint.reference == this_constraint &&
	    !*found_constraint_tp) {
		if (!*found_constraint_tp &&
		    current_element->unpaired->i_constraint.element_type == constraint_tp_element) {
			*found_constraint_tp = true;
		}
	}
	
	else
		if (current_element->type == paired && !*found_el_fp &&
		    current_element == this_element) {
			*found_el_fp = true;
			
			if (!*found_constraint_tp) {
				*constraint_tp_offset_min = stack_size;
				*constraint_tp_offset_max = stack_size;
			}
		}
		
	if (current_element != this_element) {
		if ((*found_el_fp && !*found_constraint_tp) || (!*found_el_fp &&
		                                        *found_constraint_tp)) {
			if (current_element->type == unpaired) {
				if (*found_el_fp) {
					*constraint_tp_offset_min += current_element->unpaired->min;
					*constraint_tp_offset_max += current_element->unpaired->max;
				}
				
				else {
					*constraint_tp_offset_min += -current_element->unpaired->min;
					*constraint_tp_offset_max += -current_element->unpaired->max;
				}
			}
			
			else {
				if (*found_el_fp) {
					*constraint_tp_offset_min += current_element->paired->min;
					*constraint_tp_offset_max += current_element->paired->max;
				}
				
				else {
					*constraint_tp_offset_min += -current_element->paired->min;
					*constraint_tp_offset_max += -current_element->paired->max;
				}
			}
		}
	}
	
	if (*found_constraint_tp && *found_el_fp) {
		return true;
	}
	
	else {
		if (current_element->type == unpaired) {
			if (current_element->unpaired->next) {
				return get_constraint_tp_offsets_from_element (found_el_fp, found_constraint_tp,
				                                        constraint_tp_offset_min, constraint_tp_offset_max,
				                                        current_element->unpaired->next, this_element, this_constraint, stack_size,
				                                        stack_idist);
			}
		}
		
		else {
			if (current_element == this_element ||
			    !get_constraint_tp_offsets_from_element (found_el_fp, found_constraint_tp,
			                                            constraint_tp_offset_min, constraint_tp_offset_max,
			                                            current_element->paired->fp_next, this_element, this_constraint, stack_size,
			                                            stack_idist)) {
				if ((*found_el_fp && !*found_constraint_tp) || (!*found_el_fp &&
				                                        *found_constraint_tp)) {
					if (*found_el_fp) {
						if (current_element != this_element) {
							*constraint_tp_offset_min += current_element->paired->min;
							*constraint_tp_offset_max += current_element->paired->max;
						}
						
						else {
							*constraint_tp_offset_min += stack_idist + stack_size;
							*constraint_tp_offset_max += stack_idist + stack_size;
						}
					}
					
					else {
						if (current_element != this_element) {
							*constraint_tp_offset_min += -current_element->paired->min;
							*constraint_tp_offset_max += -current_element->paired->max;
						}
						
						else {
							*constraint_tp_offset_min += -stack_idist - stack_size;
							*constraint_tp_offset_max += -stack_idist - stack_size;
						}
					}
				}
				
				if (current_element->paired->tp_next) {
					return get_constraint_tp_offsets_from_element (found_el_fp, found_constraint_tp,
					                                        constraint_tp_offset_min, constraint_tp_offset_max,
					                                        current_element->paired->tp_next, this_element, this_constraint, stack_size,
					                                        stack_idist);
				}
				
				return false;
			}
			
			else {
				return true;
			}
		}
	}
	
	return false;
}

bool get_constraint_single_offsets_from_element (bool *found_el_fp,
                                        bool *found_constraint_single,
                                        nt_s_rel_count *restrict constraint_single_offset_min,
                                        nt_s_rel_count *restrict constraint_single_offset_max,
                                        ntp_element restrict current_element, ntp_element restrict this_element,
                                        ntp_constraint restrict this_constraint,
                                        const nt_stack_size stack_size, const nt_stack_idist stack_idist) {
	if (current_element->type == unpaired &&
	    current_element->unpaired->i_constraint.reference == this_constraint &&
	    !*found_constraint_single) {
		if (!*found_constraint_single &&
		    current_element->unpaired->i_constraint.element_type ==
		    constraint_single_element) {
			*found_constraint_single = true;
		}
	}
	
	else
		if (current_element->type == paired && !*found_el_fp &&
		    current_element == this_element) {
			*found_el_fp = true;
			
			if (!*found_constraint_single) {
				*constraint_single_offset_min = stack_size;
				*constraint_single_offset_max = stack_size;
			}
		}
		
	if (current_element != this_element) {
		if ((*found_el_fp && !*found_constraint_single) || (!*found_el_fp &&
		                                        *found_constraint_single)) {
			if (current_element->type == unpaired) {
				if (*found_el_fp) {
					*constraint_single_offset_min += current_element->unpaired->min;
					*constraint_single_offset_max += current_element->unpaired->max;
				}
				
				else {
					*constraint_single_offset_min += -current_element->unpaired->min;
					*constraint_single_offset_max += -current_element->unpaired->max;
				}
			}
			
			else {
				if (*found_el_fp) {
					*constraint_single_offset_min += current_element->paired->min;
					*constraint_single_offset_max += current_element->paired->max;
				}
				
				else {
					*constraint_single_offset_min += -current_element->paired->min;
					*constraint_single_offset_max += -current_element->paired->max;
				}
			}
		}
	}
	
	if (*found_constraint_single && *found_el_fp) {
		return true;
	}
	
	else {
		if (current_element->type == unpaired) {
			if (current_element->unpaired->next) {
				return get_constraint_single_offsets_from_element (found_el_fp,
				                                        found_constraint_single,
				                                        constraint_single_offset_min, constraint_single_offset_max,
				                                        current_element->unpaired->next, this_element, this_constraint, stack_size,
				                                        stack_idist);
			}
		}
		
		else {
			if (current_element == this_element ||
			    !get_constraint_single_offsets_from_element (found_el_fp,
			                                            found_constraint_single,
			                                            constraint_single_offset_min, constraint_single_offset_max,
			                                            current_element->paired->fp_next, this_element, this_constraint, stack_size,
			                                            stack_idist)) {
				if ((*found_el_fp && !*found_constraint_single) || (!*found_el_fp &&
				                                        *found_constraint_single)) {
					if (*found_el_fp) {
						if (current_element != this_element) {
							*constraint_single_offset_min += current_element->paired->min;
							*constraint_single_offset_max += current_element->paired->max;
						}
						
						else {
							*constraint_single_offset_min += stack_idist + stack_size;
							*constraint_single_offset_max += stack_idist + stack_size;
						}
					}
					
					else {
						if (current_element != this_element) {
							*constraint_single_offset_min += -current_element->paired->min;
							*constraint_single_offset_max += -current_element->paired->max;
						}
						
						else {
							*constraint_single_offset_min += -stack_idist - stack_size;
							*constraint_single_offset_max += -stack_idist - stack_size;
						}
					}
				}
				
				if (current_element->paired->tp_next) {
					return get_constraint_single_offsets_from_element (found_el_fp,
					                                        found_constraint_single,
					                                        constraint_single_offset_min, constraint_single_offset_max,
					                                        current_element->paired->tp_next, this_element, this_constraint, stack_size,
					                                        stack_idist);
				}
				
				return false;
			}
			
			else {
				return true;
			}
		}
	}
	
	return false;
}

void which_constraint_elements_overlap_this_paired_element (
                    ntp_element restrict this_element, ntp_constraint restrict this_constraint,
                    bool *have_constraint_fp_element, bool *have_constraint_tp_element,
                    bool *have_constraint_single_element) {
	while (this_element) {
		if (this_element->type == unpaired) {
			if (this_element->unpaired->i_constraint.reference == this_constraint) {
				switch (this_element->unpaired->i_constraint.element_type) {
					case constraint_fp_element:
						*have_constraint_fp_element = true;
						break;
						
					case constraint_tp_element:
						*have_constraint_tp_element = true;
						break;
						
					case constraint_single_element:
						*have_constraint_single_element = true;
						break;
						
					default:
						break;
				}
			}
			
			this_element = this_element->unpaired->next;
		}
		
		else {
			which_constraint_elements_overlap_this_paired_element
			(this_element->paired->fp_next, this_constraint, have_constraint_fp_element,
			 have_constraint_tp_element, have_constraint_single_element);
			this_element = this_element->paired->tp_next;
		}
	}
}

void get_constraint_offsets_inside_paired_element (bool *found_constraint,
                                        nt_s_rel_count *restrict idist_before_min,
                                        nt_s_rel_count *restrict idist_before_max,
                                        nt_s_rel_count *restrict idist_after_min,
                                        nt_s_rel_count *restrict idist_after_max,
                                        ntp_element restrict current_element,
                                        ntp_constraint restrict this_constraint,
                                        nt_constraint_element_type this_constraint_element_type) {
	while (current_element->type == unpaired) {
		if (!*found_constraint) {
			if (current_element->unpaired->i_constraint.reference == this_constraint &&
			    current_element->unpaired->i_constraint.element_type ==
			    this_constraint_element_type) {
				*found_constraint = true;
			}
			
			else {
				*idist_before_min += current_element->unpaired->min;
				*idist_before_max += current_element->unpaired->max;
			}
		}
		
		else {
			*idist_after_min += current_element->unpaired->min;
			*idist_after_max += current_element->unpaired->max;
		}
		
		if (current_element->unpaired->next) {
			current_element = current_element->unpaired->next;
		}
		
		else {
			return;
		}
	}
	
	if (!*found_constraint) {
		*idist_before_min += current_element->paired->min;
		*idist_before_max += current_element->paired->max;
	}
	
	else {
		*idist_after_min += current_element->paired->min;
		*idist_after_max += current_element->paired->max;
	}
	
	get_constraint_offsets_inside_paired_element (found_constraint,
	                                        idist_before_min, idist_before_max, idist_after_min, idist_after_max,
	                                        current_element->paired->fp_next, this_constraint,
	                                        this_constraint_element_type);
	                                        
	if (!*found_constraint) {
		*idist_before_min += current_element->paired->min;
		*idist_before_max += current_element->paired->max;
	}
	
	else {
		*idist_after_min += current_element->paired->min;
		*idist_after_max += current_element->paired->max;
	}
	
	if (current_element->paired->tp_next) {
		get_constraint_offsets_inside_paired_element (found_constraint,
		                                        idist_before_min, idist_before_max, idist_after_min, idist_after_max,
		                                        current_element->paired->tp_next, this_constraint,
		                                        this_constraint_element_type);
	}
}

/*
 * note: 	get_next_constraint_offset_details_by_elements retrieves offset details with respect to a target element (including fp/tp/single min/max
 * 		distances and overlap with target element (t/f)); whereas the get_next_constraint_details_by_elements retrieves only partial information
 * 		(min/max) with respect the target element's specific stack size and idist; get_next_constraint_offset_and_dist_by_element retrieves
 * 		fp offset and relative dist (idist) between fp and tp, and fp and single (when applicable)
 */
bool get_next_constraint_offset_details_by_element
(nt_s_rel_count *restrict constraint_fp_offset_min,
 nt_s_rel_count *restrict constraint_fp_offset_max, bool *fp_overlaps,
 nt_s_rel_count *restrict constraint_tp_offset_min,
 nt_s_rel_count *restrict constraint_tp_offset_max, bool *tp_overlaps,
 nt_s_rel_count *restrict constraint_single_offset_min,
 nt_s_rel_count *restrict constraint_single_offset_max, bool *single_overlaps,
 const nt_model *restrict model, ntp_element restrict this_element,
 const nt_element *restrict target_element,
 ntp_constraint restrict this_constraint) {
	if (this_element->type == paired) {
		if (this_element == target_element) {
			*constraint_fp_offset_min = 0;
			*constraint_fp_offset_max = 0;
			*constraint_tp_offset_min = 0;
			*constraint_tp_offset_max = 0;
			*constraint_single_offset_min = 0;
			*constraint_single_offset_max = 0;
			bool have_constraint_fp_element = false, have_constraint_tp_element = false,
			     have_constraint_single_element = false;
			which_constraint_elements_overlap_this_paired_element (
			                    this_element->paired->fp_next, this_constraint,
			                    &have_constraint_fp_element, &have_constraint_tp_element,
			                    &have_constraint_single_element);
			                    
			if (have_constraint_fp_element) {
				bool found_constraint = false;
				nt_s_rel_count idist_before_min = 0, idist_before_max = 0, idist_after_min = 0,
				               idist_after_max = 0;
				get_constraint_offsets_inside_paired_element (&found_constraint,
				                                        &idist_before_min, &idist_before_max, &idist_after_min, &idist_after_max,
				                                        this_element->paired->fp_next, this_constraint, constraint_fp_element);
				*constraint_fp_offset_min = idist_before_min;
				*constraint_fp_offset_max = idist_before_max;
				*fp_overlaps = true;
			}
			
			else {
				bool found_el_fp = false, found_constraint_fp = false;
				get_constraint_fp_offset_details_from_element (&found_el_fp,
				                                        &found_constraint_fp,
				                                        constraint_fp_offset_min, constraint_fp_offset_max,
				                                        model->first_element, this_element, this_constraint);
				*fp_overlaps = false;
			}
			
			if (have_constraint_tp_element) {
				bool found_constraint = false;
				nt_s_rel_count idist_before_min = 0, idist_before_max = 0, idist_after_min = 0,
				               idist_after_max = 0;
				get_constraint_offsets_inside_paired_element (&found_constraint,
				                                        &idist_before_min, &idist_before_max, &idist_after_min, &idist_after_max,
				                                        this_element->paired->fp_next, this_constraint, constraint_tp_element);
				*constraint_tp_offset_min = idist_before_min;
				*constraint_tp_offset_max = idist_before_max;
				*tp_overlaps = true;
			}
			
			else {
				bool found_el_fp = false, found_constraint_tp = false;
				get_constraint_tp_offset_details_from_element (&found_el_fp,
				                                        &found_constraint_tp,
				                                        constraint_tp_offset_min, constraint_tp_offset_max,
				                                        model->first_element, this_element, this_constraint);
				*tp_overlaps = false;
			}
			
			if (this_constraint->type == base_triple) {
				if (have_constraint_single_element) {
					bool found_constraint = false;
					nt_s_rel_count idist_before_min = 0, idist_before_max = 0, idist_after_min = 0,
					               idist_after_max = 0;
					get_constraint_offsets_inside_paired_element (&found_constraint,
					                                        &idist_before_min, &idist_before_max, &idist_after_min, &idist_after_max,
					                                        this_element->paired->fp_next, this_constraint, constraint_single_element);
					*constraint_single_offset_min = idist_before_min;
					*constraint_single_offset_max = idist_before_max;
					*single_overlaps = true;
				}
				
				else {
					bool found_el_fp = false, found_constraint_single = false;
					get_constraint_single_offset_details_from_element (&found_el_fp,
					                                        &found_constraint_single,
					                                        constraint_single_offset_min, constraint_single_offset_max,
					                                        model->first_element, this_element, this_constraint);
					*single_overlaps = false;
				}
			}
			
			return true;
		}
		
		if (this_element->paired->fp_next &&
		    get_next_constraint_offset_details_by_element
		    (constraint_fp_offset_min, constraint_fp_offset_max, fp_overlaps,
		     constraint_tp_offset_min, constraint_tp_offset_max, tp_overlaps,
		     constraint_single_offset_min, constraint_single_offset_max, single_overlaps,
		     model, this_element->paired->fp_next, target_element, this_constraint)) {
			return true;
		}
		
		if (this_element->paired->tp_next &&
		    get_next_constraint_offset_details_by_element
		    (constraint_fp_offset_min, constraint_fp_offset_max, fp_overlaps,
		     constraint_tp_offset_min, constraint_tp_offset_max, tp_overlaps,
		     constraint_single_offset_min, constraint_single_offset_max, single_overlaps,
		     model, this_element->paired->tp_next, target_element, this_constraint)) {
			return true;
		}
		
		return false;
	}
	
	else
		if (this_element->unpaired->next &&
		    get_next_constraint_offset_details_by_element
		    (constraint_fp_offset_min, constraint_fp_offset_max, fp_overlaps,
		     constraint_tp_offset_min, constraint_tp_offset_max, tp_overlaps,
		     constraint_single_offset_min, constraint_single_offset_max, single_overlaps,
		     model, this_element->unpaired->next, target_element, this_constraint)) {
			return true;
		}
		
	return false;
}

/*
 * note: 	get_next_constraint_offset_and_dist_by_elements retrieves fp offset details with respect to a target element
 * 		and overlap with target element (t/f)); whereas the next function get_next_constraint_details_by_elements retrieves only partial information
 * 		(min/max) with respect the target element's specific stack size and idist
 */
bool get_next_constraint_offset_and_dist_by_element
(nt_s_rel_count *restrict constraint_fp_offset_min,
 nt_s_rel_count *restrict constraint_fp_offset_max, bool *fp_overlaps,
 nt_s_rel_count *restrict constraint_tp_dist_min,
 nt_s_rel_count *restrict constraint_tp_dist_max, bool *tp_overlaps,
 nt_s_rel_count *restrict constraint_single_dist_min,
 nt_s_rel_count *restrict constraint_single_dist_max, bool *single_overlaps,
 const nt_model *restrict model, ntp_element restrict this_element,
 const nt_element *restrict target_element,
 ntp_constraint restrict this_constraint) {
	if (this_element->type == paired) {
		if (this_element == target_element) {
			*constraint_fp_offset_min = 0;
			*constraint_fp_offset_max = 0;
			*constraint_tp_dist_min = 0;
			*constraint_tp_dist_max = 0;
			*constraint_single_dist_min = 0;
			*constraint_single_dist_max = 0;
			bool have_constraint_fp_element = false, have_constraint_tp_element = false,
			     have_constraint_single_element = false;
			which_constraint_elements_overlap_this_paired_element (
			                    this_element->paired->fp_next, this_constraint,
			                    &have_constraint_fp_element, &have_constraint_tp_element,
			                    &have_constraint_single_element);
			                    
			if (have_constraint_fp_element) {
				bool found_constraint = false;
				nt_s_rel_count idist_before_min = 0, idist_before_max = 0, idist_after_min = 0,
				               idist_after_max = 0;
				get_constraint_offsets_inside_paired_element (&found_constraint,
				                                        &idist_before_min, &idist_before_max, &idist_after_min, &idist_after_max,
				                                        this_element->paired->fp_next, this_constraint, constraint_fp_element);
				*constraint_fp_offset_min = idist_before_min;
				*constraint_fp_offset_max = idist_before_max;
				*fp_overlaps = true;
			}
			
			else {
				bool found_el_fp = false, found_constraint_fp = false;
				get_constraint_fp_offset_details_from_element (&found_el_fp,
				                                        &found_constraint_fp,
				                                        constraint_fp_offset_min, constraint_fp_offset_max,
				                                        model->first_element, this_element, this_constraint);
				*fp_overlaps = false;
			}
			
			*tp_overlaps = have_constraint_tp_element;
			*single_overlaps = have_constraint_single_element;
			have_constraint_fp_element = false;
			have_constraint_tp_element = false;
			have_constraint_single_element = false;
			get_constraint_dist_from_fp (&have_constraint_fp_element,
			                             &have_constraint_tp_element, &have_constraint_single_element,
			                             constraint_tp_dist_min, constraint_tp_dist_max,
			                             constraint_single_dist_min, constraint_single_dist_max,
			                             model->first_element, this_constraint);
			return true;
		}
		
		if (this_element->paired->fp_next &&
		    get_next_constraint_offset_and_dist_by_element
		    (constraint_fp_offset_min, constraint_fp_offset_max, fp_overlaps,
		     constraint_tp_dist_min, constraint_tp_dist_max, tp_overlaps,
		     constraint_single_dist_min, constraint_single_dist_max, single_overlaps,
		     model, this_element->paired->fp_next, target_element, this_constraint)) {
			return true;
		}
		
		if (this_element->paired->tp_next &&
		    get_next_constraint_offset_and_dist_by_element
		    (constraint_fp_offset_min, constraint_fp_offset_max, fp_overlaps,
		     constraint_tp_dist_min, constraint_tp_dist_max, tp_overlaps,
		     constraint_single_dist_min, constraint_single_dist_max, single_overlaps,
		     model, this_element->paired->tp_next, target_element, this_constraint)) {
			return true;
		}
		
		return false;
	}
	
	else
		if (this_element->unpaired->next &&
		    get_next_constraint_offset_and_dist_by_element
		    (constraint_fp_offset_min, constraint_fp_offset_max, fp_overlaps,
		     constraint_tp_dist_min, constraint_tp_dist_max, tp_overlaps,
		     constraint_single_dist_min, constraint_single_dist_max, single_overlaps,
		     model, this_element->unpaired->next, target_element, this_constraint)) {
			return true;
		}
		
	return false;
}

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
 const nt_stack_size stack_size, const nt_stack_idist stack_idist) {
	// TODO: assumes fp_next for paired is never NULL
	if (this_element->type == paired) {
		if (this_element == target_element) {
			*constraint_fp_offset_min = 0;
			*constraint_fp_offset_max = 0;
			*constraint_tp_offset_min = 0;
			*constraint_tp_offset_max = 0;
			*constraint_single_offset_min = 0;
			*constraint_single_offset_max = 0;
			bool have_constraint_fp_element = false, have_constraint_tp_element = false,
			     have_constraint_single_element = false;
			REGISTER
			nt_rel_seq_len min_constraint_len = (nt_rel_seq_len) (this_constraint->type ==
			                                        pseudoknot ? this_constraint->pseudoknot->fp_element->unpaired->min : 1),
			                                    max_constraint_len = (nt_rel_seq_len) (this_constraint->type == pseudoknot ?
			                                                                            this_constraint->pseudoknot->fp_element->unpaired->max : 1);
			which_constraint_elements_overlap_this_paired_element (
			                    this_element->paired->fp_next, this_constraint,
			                    &have_constraint_fp_element, &have_constraint_tp_element,
			                    &have_constraint_single_element);
			                    
			if (have_constraint_fp_element) {
				bool found_constraint = false;
				nt_s_rel_count idist_before_min = 0, idist_before_max = 0, idist_after_min = 0,
				               idist_after_max = 0;
				get_constraint_offsets_inside_paired_element (&found_constraint,
				                                        &idist_before_min, &idist_before_max, &idist_after_min, &idist_after_max,
				                                        this_element->paired->fp_next, this_constraint, constraint_fp_element);
				                                        
				if (idist_before_min + min_constraint_len + idist_after_min > stack_idist ||
				    idist_before_max + max_constraint_len + idist_after_max < stack_idist) {
					*constraint_fp_offset_min = 0;
					*constraint_fp_offset_max = 0;
					*constraint_tp_offset_min = 0;
					*constraint_tp_offset_max = 0;
					*constraint_single_offset_min = 0;
					*constraint_single_offset_max = 0;
					return true;
				}
				
				else {
					*constraint_fp_offset_min = stack_size + (SAFE_MAX (stack_idist -
					                                        idist_after_max - max_constraint_len, idist_before_min));
					*constraint_fp_offset_max = stack_size + (SAFE_MIN (stack_idist -
					                                        idist_after_min - min_constraint_len, idist_before_max));
				}
			}
			
			else {
				bool found_el_fp = false, found_constraint_fp = false;
				get_constraint_fp_offsets_from_element (&found_el_fp, &found_constraint_fp,
				                                        constraint_fp_offset_min, constraint_fp_offset_max,
				                                        model->first_element, this_element, this_constraint, stack_size, stack_idist);
			}
			
			if (have_constraint_tp_element) {
				bool found_constraint = false;
				nt_s_rel_count idist_before_min = 0, idist_before_max = 0, idist_after_min = 0,
				               idist_after_max = 0;
				get_constraint_offsets_inside_paired_element (&found_constraint,
				                                        &idist_before_min, &idist_before_max, &idist_after_min, &idist_after_max,
				                                        this_element->paired->fp_next, this_constraint, constraint_tp_element);
				                                        
				if (idist_before_min + min_constraint_len + idist_after_min > stack_idist ||
				    idist_before_max + max_constraint_len + idist_after_max < stack_idist) {
					*constraint_fp_offset_min = 0;
					*constraint_fp_offset_max = 0;
					*constraint_tp_offset_min = 0;
					*constraint_tp_offset_max = 0;
					*constraint_single_offset_min = 0;
					*constraint_single_offset_max = 0;
					return true;
				}
				
				else {
					*constraint_tp_offset_min = stack_size + (SAFE_MAX (stack_idist -
					                                        idist_after_max - max_constraint_len, idist_before_min));
					*constraint_tp_offset_max = stack_size + (SAFE_MIN (stack_idist -
					                                        idist_after_min - min_constraint_len, idist_before_max));
				}
			}
			
			else {
				bool found_el_fp = false, found_constraint_tp = false;
				get_constraint_tp_offsets_from_element (&found_el_fp, &found_constraint_tp,
				                                        constraint_tp_offset_min, constraint_tp_offset_max,
				                                        model->first_element, this_element, this_constraint, stack_size, stack_idist);
			}
			
			if (this_constraint->type == base_triple) {
				if (have_constraint_single_element) {
					bool found_constraint = false;
					nt_s_rel_count idist_before_min = 0, idist_before_max = 0, idist_after_min = 0,
					               idist_after_max = 0;
					get_constraint_offsets_inside_paired_element (&found_constraint,
					                                        &idist_before_min, &idist_before_max, &idist_after_min, &idist_after_max,
					                                        this_element->paired->fp_next, this_constraint, constraint_single_element);
					                                        
					if (idist_before_min + min_constraint_len + idist_after_min > stack_idist ||
					    idist_before_max + max_constraint_len + idist_after_max < stack_idist) {
						*constraint_fp_offset_min = 0;
						*constraint_fp_offset_max = 0;
						*constraint_tp_offset_min = 0;
						*constraint_tp_offset_max = 0;
						*constraint_single_offset_min = 0;
						*constraint_single_offset_max = 0;
						return true;
					}
					
					else {
						*constraint_single_offset_min = stack_size + (SAFE_MAX (
						                                        stack_idist - idist_after_max - max_constraint_len, idist_before_min));
						*constraint_single_offset_max = stack_size + (SAFE_MIN (
						                                        stack_idist - idist_after_min - min_constraint_len, idist_before_max));
					}
				}
				
				else {
					bool found_el_fp = false, found_constraint_single = false;
					get_constraint_single_offsets_from_element (&found_el_fp,
					                                        &found_constraint_single,
					                                        constraint_single_offset_min, constraint_single_offset_max,
					                                        model->first_element, this_element, this_constraint, stack_size, stack_idist);
				}
			}
			
			return true;
		}
		
		if (this_element->paired->fp_next && get_next_constraint_offsets_by_element
		    (constraint_fp_offset_min, constraint_fp_offset_max,
		     constraint_tp_offset_min, constraint_tp_offset_max,
		     constraint_single_offset_min, constraint_single_offset_max,
		     model, this_element->paired->fp_next, target_element,
		     this_constraint, stack_size, stack_idist)) {
			return true;
		}
		
		if (this_element->paired->tp_next && get_next_constraint_offsets_by_element
		    (constraint_fp_offset_min, constraint_fp_offset_max,
		     constraint_tp_offset_min, constraint_tp_offset_max,
		     constraint_single_offset_min, constraint_single_offset_max,
		     model, this_element->paired->tp_next, target_element,
		     this_constraint, stack_size, stack_idist)) {
			return true;
		}
		
		return false;
	}
	
	else
		if (this_element->unpaired->next && get_next_constraint_offsets_by_element
		    (constraint_fp_offset_min, constraint_fp_offset_max,
		     constraint_tp_offset_min, constraint_tp_offset_max,
		     constraint_single_offset_min, constraint_single_offset_max,
		     model, this_element->unpaired->next, target_element,
		     this_constraint, stack_size, stack_idist)) {
			return true;
		}
		
	return false;
}

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
 const nt_stack_size stack_size, const nt_stack_idist idist) {
	// TODO: assumes fp_next for paired is never NULL
	if (this_element->type == paired) {
		if (this_element == target_element) {
			REGISTER
			ntp_element prev_element = this_element,
			            this_contained_element = this_element->paired->fp_next;
			REGISTER
			nt_rel_count this_next_paired_element_min = 0, this_next_paired_element_max = 0;
			REGISTER
			bool found_contained_paired_element = false;
			
			while (this_contained_element) {
				if (this_contained_element->type == paired) {
					contained_paired_elements[*num_contained_paired_elements] =
					                    this_contained_element;
					get_nested_pair_distances (
					                    &paired_elements_fp_dist_min[*num_contained_paired_elements],
					                    &paired_elements_fp_dist_max[*num_contained_paired_elements],
					                    &paired_elements_tp_dist_min[*num_contained_paired_elements],
					                    &paired_elements_tp_dist_max[*num_contained_paired_elements],
					                    model,
					                    this_element, this_contained_element);
					get_stack_distances_in_paired_element (model, this_contained_element,
					                                       prev_element,
					                                       &paired_elements_idist_min[*num_contained_paired_elements],
					                                       &paired_elements_idist_max[*num_contained_paired_elements],
					                                       &paired_elements_in_extrusion[*num_contained_paired_elements]);
					                                       
					if (*num_contained_paired_elements) {
						next_paired_element_min[ (*num_contained_paired_elements) - 1] =
						                    this_next_paired_element_min;
						next_paired_element_max[ (*num_contained_paired_elements) - 1] =
						                    this_next_paired_element_max;
					}
					
					this_next_paired_element_min = 0;
					this_next_paired_element_max = 0;
					prev_element = this_contained_element;
					this_contained_element = this_contained_element->paired->tp_next;
					(*num_contained_paired_elements)++;
					found_contained_paired_element = true;
				}
				
				else {
					this_next_paired_element_min += this_contained_element->unpaired->min;
					this_next_paired_element_max += this_contained_element->unpaired->max;
					prev_element = this_contained_element;
					this_contained_element = this_contained_element->unpaired->next;
					
					if (!this_contained_element && *num_contained_paired_elements) {
						next_paired_element_min[ (*num_contained_paired_elements) - 1] =
						                    this_next_paired_element_min;
						next_paired_element_max[ (*num_contained_paired_elements) - 1] =
						                    this_next_paired_element_max;
					}
				}
			}
			
			if (*num_contained_paired_elements) {
				next_paired_element_min[ (*num_contained_paired_elements) - 1] =
				                    this_next_paired_element_min;
				next_paired_element_max[ (*num_contained_paired_elements) - 1] =
				                    this_next_paired_element_max;
			}
			
			else {
				next_paired_element_min[0] = 0;
				next_paired_element_max[0] = 0;
			}
			
			return found_contained_paired_element;
		}
		
		if (this_element->paired->fp_next && get_contained_paired_elements_by_element
		    (contained_paired_elements,
		     paired_elements_fp_dist_min, paired_elements_fp_dist_max,
		     paired_elements_tp_dist_min, paired_elements_tp_dist_max,
		     paired_elements_idist_min,
		     paired_elements_idist_max,
		     paired_elements_in_extrusion,
		     next_paired_element_min, next_paired_element_max,
		     num_contained_paired_elements,
		     model, this_element->paired->fp_next, target_element,
		     stack_size, idist)) {
			return true;
		}
		
		if (this_element->paired->tp_next && get_contained_paired_elements_by_element
		    (contained_paired_elements,
		     paired_elements_fp_dist_min, paired_elements_fp_dist_max,
		     paired_elements_tp_dist_min, paired_elements_tp_dist_max,
		     paired_elements_idist_min,
		     paired_elements_idist_max,
		     paired_elements_in_extrusion,
		     next_paired_element_min, next_paired_element_max,
		     num_contained_paired_elements,
		     model, this_element->paired->tp_next, target_element,
		     stack_size, idist)) {
			return true;
		}
		
		return false;
	}
	
	else
		if (this_element->unpaired->next && get_contained_paired_elements_by_element
		    (contained_paired_elements,
		     paired_elements_fp_dist_min, paired_elements_fp_dist_max,
		     paired_elements_tp_dist_min, paired_elements_tp_dist_max,
		     paired_elements_idist_min,
		     paired_elements_idist_max,
		     paired_elements_in_extrusion,
		     next_paired_element_min, next_paired_element_max,
		     num_contained_paired_elements,
		     model, this_element->unpaired->next, target_element,
		     stack_size, idist)) {
			return true;
		}
		
	return false;
}

bool get_containing_paired_element_dist_by_element
(ntp_element *restrict containing_paired_element,
 nt_rel_count *restrict paired_element_fp_dist_min,
 nt_rel_count *restrict paired_element_fp_dist_max,
 nt_rel_count *restrict paired_element_tp_dist_min,
 nt_rel_count *restrict paired_element_tp_dist_max,
 nt_element *restrict last_paired_element, const nt_model *restrict model,
 nt_element *restrict this_element,
 const nt_element *restrict target_el,
 const nt_stack_size stack_size, const nt_stack_idist idist) {
	// TODO: assumes fp_next for paired is never NULL
	if (this_element->type == paired) {
		if (this_element == target_el) {
			if (last_paired_element &&
			    last_paired_element->paired->max) {  // need containing paired element which is not a wrapper bp
				*containing_paired_element = last_paired_element;
				get_nested_pair_distances (paired_element_fp_dist_min,
				                           paired_element_fp_dist_max,
				                           paired_element_tp_dist_min,
				                           paired_element_tp_dist_max,
				                           model,
				                           last_paired_element, this_element);
			}
			
			else {
				return false;
			}
			
			return true;
		}
		
		if (get_containing_paired_element_dist_by_element (containing_paired_element,
		                                        paired_element_fp_dist_min, paired_element_fp_dist_max,
		                                        paired_element_tp_dist_min, paired_element_tp_dist_max,
		                                        this_element, model, this_element->paired->fp_next, target_el,
		                                        stack_size, idist)) {
			return true;
		}
		
		if (this_element->paired->tp_next) {
			return  get_containing_paired_element_dist_by_element (
			                            containing_paired_element,
			                            paired_element_fp_dist_min, paired_element_fp_dist_max,
			                            paired_element_tp_dist_min, paired_element_tp_dist_max,
			                            last_paired_element, model, this_element->paired->tp_next, target_el,
			                            stack_size, idist);
		}
	}
	
	else
		if (this_element->unpaired->next) {
			return  get_containing_paired_element_dist_by_element (
			                            containing_paired_element,
			                            paired_element_fp_dist_min, paired_element_fp_dist_max,
			                            paired_element_tp_dist_min, paired_element_tp_dist_max,
			                            last_paired_element, model, this_element->unpaired->next, target_el,
			                            stack_size, idist);
		}
		
	return false;
}

bool traverse_and_count (const nt_model *restrict model,
                         const nt_element *restrict el,
                         const nt_element *restrict prev_el,
                         nt_stack_idist *restrict min_count,
                         nt_stack_idist *restrict max_count,
                         ntp_list *restrict min_stack_dist,
                         ntp_list *restrict max_stack_dist,
                         ntp_list *restrict in_extrusion,
                         ntp_list *restrict dist_els) {
	if (el->type == paired) {
		#ifndef NO_FULL_CHECKS
	
		if (el->paired->fp_next != NULL) {
		#endif
			nt_stack_idist min_fp_count = 0, max_fp_count = 0;
			REGISTER
			ntp_element fp_next = el->paired->fp_next, tp_next = el->paired->tp_next;
			
			if (traverse_and_count (model, fp_next,
			                        el,
			                        &min_fp_count, &max_fp_count, min_stack_dist, max_stack_dist, in_extrusion,
			                        dist_els)) {
				*min_count += min_fp_count;
				*max_count += max_fp_count;
				REGISTER
				short this_in_extrusion = 0;
				
				if (fp_next->type == unpaired &&
				    fp_next->unpaired->i_constraint.reference &&
				    fp_next->unpaired->i_constraint.reference->type == base_triple &&
				    fp_next->unpaired->i_constraint.element_type == constraint_fp_element) {
					const ntp_element tp_element = get_tp_element (fp_next);
					
					if (tp_element &&								
					    tp_element->type == unpaired &&				
					    tp_element->unpaired->i_constraint.reference &&
					    tp_element->unpaired->i_constraint.reference->type == base_triple &&
					    tp_element->unpaired->i_constraint.element_type ==
					    constraint_tp_element) { 
						this_in_extrusion++;
						
						if (fp_next->unpaired->next && fp_next->unpaired->next->type == paired) {
							this_in_extrusion += fp_next->unpaired->next->paired->min;
						}
					}
				}
				
				else
					if (tp_next &&
					    tp_next->type == unpaired &&
					    tp_next->unpaired->i_constraint.reference &&
					    tp_next->unpaired->i_constraint.reference->type == base_triple &&
					    tp_next->unpaired->i_constraint.element_type == constraint_tp_element &&
					    NULL != prev_el && prev_el->type == unpaired &&	
					    prev_el->unpaired->i_constraint.reference && 	
					    prev_el->unpaired->i_constraint.reference->type == base_triple &&	
					    prev_el->unpaired->i_constraint.element_type ==
					    constraint_fp_element) {
						this_in_extrusion--;
						REGISTER
						ntp_element containing_element = get_containing_paired_element (NULL,
						                                        model->first_element, el);
						                                        
						if (containing_element &&
						    containing_element->paired->fp_next->type == unpaired &&
						    containing_element->paired->fp_next == prev_el &&
						    NULL == tp_next->unpaired->next) {
							this_in_extrusion -= containing_element->paired->min;
						}
					}
					
				for (REGISTER nt_stack_size i = SAFE_MAX (el->paired->min, (nt_stack_size)1);
				     i <= el->paired->max; i++) {
					REGISTER
					bool already_found = false;
					list_iterator_start (min_stack_dist[i - 1]);
					
					while (list_iterator_hasnext (min_stack_dist[i - 1])) {
						if (* ((ntp_stack_idist)list_iterator_next (min_stack_dist[i - 1])) ==
						    min_fp_count) {
							already_found = true;
							break;
						}
					}
					
					list_iterator_stop (min_stack_dist[i - 1]);
					
					if (already_found) {
						already_found = false;
						list_iterator_start (max_stack_dist[i - 1]);
						
						while (list_iterator_hasnext (max_stack_dist[i - 1])) {
							if (* ((ntp_stack_idist)list_iterator_next (max_stack_dist[i - 1])) ==
							    max_fp_count) {
								already_found = true;
								break;
							}
						}
						
						list_iterator_stop (max_stack_dist[i - 1]);
						
						if (already_found) {
							already_found = false;
							list_iterator_start (in_extrusion[i - 1]);
							
							while (list_iterator_hasnext (in_extrusion[i - 1])) {
								if (* ((short *)list_iterator_next (in_extrusion[i - 1])) ==
								    this_in_extrusion) {
									already_found = true;
									break;
								}
							}
							
							list_iterator_stop (in_extrusion[i - 1]);
							
							if (already_found) {
								already_found = false;
								list_iterator_start (dist_els[i - 1]);
								
								while (list_iterator_hasnext (dist_els[i - 1])) {
									if (((ntp_element)list_iterator_next (dist_els[i - 1])) == el) {
										already_found = true;
										break;
									}
								}
								
								list_iterator_stop (dist_els[i - 1]);
							}
						}
					}
					
					if (!already_found) {
						REGISTER
						ntp_stack_idist new_min, new_max;
						short *new_in_extrusion;
						// TODO: error handling
						new_min = MALLOC_DEBUG (sizeof (nt_stack_idist),
						                        "ntp_stack_idist for min_stack_dist in traverse_and_count");
						new_max = MALLOC_DEBUG (sizeof (nt_stack_idist),
						                        "ntp_stack_idist for max_stack_dist in traverse_and_count");
						new_in_extrusion = MALLOC_DEBUG (sizeof (short),
						                                 "short for in_extrusion in traverse_and_count");
						*new_min = min_fp_count;
						*new_max = max_fp_count;
						*new_in_extrusion = this_in_extrusion;
						list_append (min_stack_dist[i - 1], new_min);
						list_append (max_stack_dist[i - 1], new_max);
						list_append (in_extrusion[i - 1], new_in_extrusion);
						list_append (dist_els[i - 1], el);
					}
				}
			}
			
			else {
				return false;
			}
			
			#ifndef NO_FULL_CHECKS
		}
		
		else {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "fp_next of el is NULL in traverse_and_count", false);
			return false;
		}
		
			#endif
		*min_count += el->paired->min * 2;
		*max_count += el->paired->max * 2;
		
		if (tp_next != NULL) {
			nt_stack_idist min_tp_count = 0, max_tp_count = 0;
			
			if (traverse_and_count (model, tp_next,
			                        el,
			                        &min_tp_count, &max_tp_count, min_stack_dist, max_stack_dist, in_extrusion,
			                        dist_els)) {
				*min_count += min_tp_count;
				*max_count += max_tp_count;
			}
			
			else {
				return false;
			}
		}
		
		return true;
	}
	
	else
	#ifndef NO_FULL_CHECKS
		if (el->type == unpaired)
	#endif
		{
			*min_count += el->unpaired->min;
			*max_count += el->unpaired->max;
			
			if (el->unpaired->next != NULL) {
				nt_stack_idist min_next_count = 0, max_next_count = 0;
				
				if (traverse_and_count (model, el->unpaired->next,
				                        el,
				                        &min_next_count, &max_next_count, min_stack_dist, max_stack_dist, in_extrusion,
				                        dist_els)) {
					*min_count += min_next_count;
					*max_count += max_next_count;
				}
				
				else {
					return false;
				}
			}
			
			return true;
		}
		
	#ifndef NO_FULL_CHECKS
	COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
	              "element type is neither paired nor unpaired in traverse_and_count", false);
	return false;
	#endif
}

bool get_stack_distances (const nt_model *restrict model, ntp_element el,
                          ntp_list *restrict min_stack_dist, ntp_list *restrict max_stack_dist,
                          ntp_list *restrict in_extrusion, ntp_list *restrict dist_els) {
	nt_stack_idist min_count = 0, max_count = 0;
	bool success = traverse_and_count (model, el,
	                                   NULL,
	                                   &min_count, &max_count, min_stack_dist, max_stack_dist, in_extrusion, dist_els);
	return success;
}

bool traverse_and_size (const nt_model *restrict model,
                        const nt_element *restrict el,
                        nt_model_size *restrict model_size) {
	if (el->type == paired) {
		#ifndef NO_FULL_CHECKS
	
		if (el->paired->fp_next != NULL) {
		#endif
			*model_size *= (el->paired->max - el->paired->min + 1);
			REGISTER
			ntp_element fp_next = el->paired->fp_next, tp_next = el->paired->tp_next;
			
			if (!traverse_and_size (model, fp_next, model_size)) {
				return false;
			}
			
			#ifndef NO_FULL_CHECKS
		}
		
		else {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "fp_next of el is NULL in traverse_and_count", false);
			return false;
		}
		
			#endif
		
		if (tp_next != NULL) {
			if (!traverse_and_size (model, tp_next, model_size)) {
				return false;
			}
		}
		
		return true;
	}
	
	else
	#ifndef NO_FULL_CHECKS
		if (el->type == unpaired)
	#endif
		{
			if (!el->unpaired->i_constraint.reference ||
			    el->unpaired->i_constraint.element_type ==
			    constraint_fp_element) { // in case of constraints, only consider fp_elements to avoid duplicates
				*model_size *= (el->unpaired->max - el->unpaired->min + 1);
			}
			
			if (el->unpaired->next != NULL) {
				if (!traverse_and_size (model, el->unpaired->next, model_size)) {
					return false;
				}
			}
			
			return true;
		}
		
	#ifndef NO_FULL_CHECKS
	COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
	              "element type is neither paired nor unpaired in traverse_and_size", false);
	return false;
	#endif
}

bool get_model_size (const nt_model *restrict model, ntp_element el,
                     nt_model_size *restrict model_size) {
	*model_size = 1;
	return traverse_and_size (model, el, model_size);
}

void traverse_and_partition (const nt_model *restrict model,
                             nt_element *restrict el, nt_element_count *restrict curr_size,
                             ntp_element *restrict model_partitions,
                             nt_element_count num_current_partitions) {
	if (el->type == paired) {
		#ifndef NO_FULL_CHECKS
	
		if (el->paired->fp_next != NULL) {
		#endif
		
			if (el->paired->min &&
			    // only consider pos_var ranges that do not include '0' - this is to avoid the min==max==0 scenario;
                // and, by definition, only pos_vars where max>min
			    el->paired->max >
			    el->paired->min) { 
				REGISTER
				nt_element_count this_size = (nt_element_count) (el->paired->max -
				                                        el->paired->min + 1);
				                                        
				if (this_size >= *curr_size) {
					REGISTER
					bool already_exists = false;
					
					for (REGISTER ushort i = 0; i < num_current_partitions; i++) {
						if (el == model_partitions[i]) {
							already_exists = true;
							break;
						}
					}
					
					if (!already_exists) {
						*curr_size = this_size;
						model_partitions[num_current_partitions] = el;
					}
				}
			}
			
			REGISTER
			ntp_element fp_next = el->paired->fp_next, tp_next = el->paired->tp_next;
			traverse_and_partition (model, fp_next, curr_size, model_partitions,
			                        num_current_partitions);
			#ifndef NO_FULL_CHECKS
		}
		
		else {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "fp_next of el is NULL in traverse_and_partition", false);
			return;
		}
		
			#endif
		
		if (tp_next != NULL) {
			traverse_and_partition (model, tp_next, curr_size, model_partitions,
			                        num_current_partitions);
		}
	}
	
	else
	#ifndef NO_FULL_CHECKS
		if (el->type == unpaired)
	#endif
		{
			if (!el->unpaired->i_constraint.reference ||
			    el->unpaired->i_constraint.element_type ==
			    constraint_fp_element) { // in case of constraints, only consider fp_elements to avoid duplicates
				if (el->unpaired->min &&
				    // only consider pos_var ranges that do not include '0' - this is to avoid the min==max==0 scenario;
                    // and, by definition, only pos_vars where max>min
				    el->unpaired->max >
				    el->unpaired->min) { 
					REGISTER
					nt_element_count this_size = (nt_element_count) (el->unpaired->max -
					                                        el->unpaired->min + 1);
					                                        
					if (this_size >= *curr_size) {
						REGISTER
						bool already_exists = false;
						
						for (REGISTER ushort i = 0; i < num_current_partitions; i++) {
							if (el == model_partitions[i]) {
								already_exists = true;
								break;
							}
						}
						
						if (!already_exists) {
							*curr_size = this_size;
							model_partitions[num_current_partitions] = el;
						}
					}
				}
			}
			
			if (el->unpaired->next != NULL) {
				traverse_and_partition (model, el->unpaired->next, curr_size, model_partitions,
				                        num_current_partitions);
			}
		}
		
	#ifndef NO_FULL_CHECKS
	COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
	              "element type is neither paired nor unpaired in traverse_and_partition", false);
	return false;
	#endif
}

bool get_next_model_partition (ntp_model model,
                               ntp_element *restrict model_partitions,
                               nt_element_count num_current_partitions) {
	nt_element_count curr_size = 0;
	traverse_and_partition (model, model->first_element, &curr_size,
	                        model_partitions, num_current_partitions);
	// if no new partition found (curr_size==0) return false
	return curr_size;
}
