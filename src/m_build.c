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
#include "m_seq_bp.h"

/*
 * model building functions
 */
ntp_model initialize_model() {
	COMMIT_DEBUG (REPORT_INFO, MODEL, "initializing model", true);
	REGISTER
	ntp_model restrict model = MALLOC_DEBUG (sizeof (nt_model),
	                                        "model in initialize_model");
	                                        
	if (model) {
		model->first_element = NULL;
		model->first_constraint = NULL;
		COMMIT_DEBUG (REPORT_INFO, MODEL,
		              "model initialized with NULL element in initialize_model", false);
		return model;
	}
	
	else {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize model in initialize_model", false);
		return NULL;
	}
}

ntp_element initialize_element (const nt_element_type el_type,
                                const nt_element_count min, const nt_element_count max) {
	if (el_type == no_element_type ||
	    min < 0 || max < 0 ||
	    min > max) {
		return NULL;
	}
	
	#ifdef DEBUG_ON
	char msg[100];
	
	if (el_type == paired) {
		sprintf (msg,
		         "initializing model element of type paired, min %d, max %d in initialize_element",
		         min, max);
	}
	
	else {
		sprintf (msg,
		         "initializing model element of type unpaired, min %d, max %d in initialize_element",
		         min, max);
	}
	
	COMMIT_DEBUG (REPORT_INFO, MODEL, msg, true);
	#endif
	REGISTER
	ntp_element restrict el = MALLOC_DEBUG (sizeof (nt_element),
	                                        "element in initialize_element");
	                                        
	if (!el) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize element in initialize_element", false);
		return NULL;
	}
	
	el->type = el_type;
	
	if (el_type == paired) {
		REGISTER
		ntp_paired_element restrict paired_element =
		                    MALLOC_DEBUG (sizeof (nt_paired_element),
		                                  "paired_element for element in initialize_element");
		                                  
		if (!paired_element) {
			COMMIT_DEBUG (REPORT_ERRORS, MODEL,
			              "could not initialize paired_element for element in initialize_element", false);
			FREE_DEBUG (el,
			            "element in initialize_element [failed to initialize paired_element]");
			return NULL;
		}
		
		paired_element->min = min;
		paired_element->max = max;
		paired_element->tp_next = NULL;
		paired_element->fp_next = NULL;
		el->paired = paired_element;
	}
	
	else
		if (el_type == unpaired) {
			REGISTER
			ntp_unpaired_element restrict unpaired_element =
			                    MALLOC_DEBUG (sizeof (nt_unpaired_element),
			                                  "unpaired_element for element in initialize_element");
			                                  
			if (!unpaired_element) {
				COMMIT_DEBUG (REPORT_ERRORS, MODEL,
				              "could not initialize unpaired_element for element in initialize_element",
				              false);
				FREE_DEBUG (el,
				            "element in initialize_element [failed to initialize unpaired_element]");
				return NULL;
			}
			
			unpaired_element->min = min;
			unpaired_element->max = max;
			unpaired_element->i_constraint.reference = NULL;
			unpaired_element->i_constraint.element_type = constraint_no_element;
			unpaired_element->prev_type = no_element_type;
			unpaired_element->prev_paired = NULL;
			unpaired_element->prev_unpaired = NULL;
			unpaired_element->prev_branch_type = unbranched;
			unpaired_element->next = NULL;
			el->unpaired = unpaired_element;
		}
		
	return el;
}

ntp_constraint initialize_pseudoknot (const nt_element_count min,
                                      const nt_element_count max) {
	if (min < 0 || max < 0 ||
	    min > max) {
		return NULL;
	}
	
	#ifdef DEBUG_ON
	char msg[100];
	sprintf (msg,
	         "initializing pseudoknot, min %d, max %d in initialize_pseudoknot", min, max);
	COMMIT_DEBUG (REPORT_INFO, MODEL, msg, true);
	#endif
	// pseudoknot constraint
	REGISTER
	ntp_constraint restrict pk_constraint = MALLOC_DEBUG (sizeof (nt_constraint),
	                                        "pseudoknot constraint in initialize_pseudoknot");
	                                        
	if (!pk_constraint) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize pseudoknot constraint in initialize_pseudoknot", false);
		return NULL;
	}
	
	REGISTER
	ntp_pseudoknot restrict pk = MALLOC_DEBUG (sizeof (nt_pseudoknot),
	                                        "pseudoknot in initialize_pseudoknot");
	                                        
	if (!pk) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize pseudoknot in initialize_pseudoknot", false);
		FREE_DEBUG (pk_constraint,
		            "pseudoknot constraint in initialize_pseudoknot [failed to initialize pseudoknot in initialize_element]");
		return NULL;
	}
	
	// 1st element (containing unpaired)
	REGISTER
	ntp_element restrict el = MALLOC_DEBUG (sizeof (nt_element),
	                                        "1st element for pseudoknot in initialize_pseudoknot");
	                                        
	if (!el) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize 1st element for pseudoknot in initialize_pseudoknot",
		              false);
		FREE_DEBUG (pk_constraint,
		            "pseudoknot constraint in initialize_pseudoknot [failed to initialize 1st element for pseudoknot]");
		FREE_DEBUG (pk,
		            "pseudoknot in initialize_pseudoknot [failed to initialize 1st element for pseudoknot]");
		return NULL;
	}
	
	REGISTER
	ntp_unpaired_element restrict unpaired_element =
	                    MALLOC_DEBUG (sizeof (nt_unpaired_element),
	                                  "unpaired_element for 1st pseudoknot element in initialize_pseudoknot");
	                                  
	if (!unpaired_element) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize unpaired_element for 1st pseudoknot element in initialize_pseudoknot",
		              false);
		FREE_DEBUG (pk_constraint,
		            "pseudoknot constraint in initialize_pseudoknot [failed to initialize 1st unpaired_element for pseudoknot]");
		FREE_DEBUG (pk,
		            "pseudoknot in initialize_pseudoknot [failed to initialize 1st unpaired_element for pseudoknot]");
		FREE_DEBUG (el,
		            "1st element for pseudoknot in initialize_pseudoknot [failed to initialize 1st unpaired_element for pseudoknot]");
		return NULL;
	}
	
	unpaired_element->min = min;
	unpaired_element->max = max;
	unpaired_element->i_constraint.reference = pk_constraint;
	unpaired_element->i_constraint.element_type = constraint_fp_element;
	unpaired_element->prev_type = no_element_type;
	unpaired_element->prev_paired = NULL;
	unpaired_element->prev_unpaired = NULL;
	unpaired_element->prev_branch_type = unbranched;
	unpaired_element->next = NULL;
	el->type = unpaired;
	el->unpaired = unpaired_element;
	pk->fp_element = el;
	nt_unpaired_element *restrict tmp_unpaired_element = unpaired_element;
	nt_element *restrict tmp_el = el;
	// 2nd element (containing unpaired)
	el = MALLOC_DEBUG (sizeof (nt_element),
	                   "2nd element for pseudoknot in initialize_pseudoknot");
	                   
	if (!el) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize 2nd element for pseudoknot in initialize_element", false);
		FREE_DEBUG (pk_constraint,
		            "pseudoknot constraint in initialize_pseudoknot [failed to initialize 2nd element for pseudoknot]");
		FREE_DEBUG (pk,
		            "pseudoknot in initialize_pseudoknot [failed to initialize 2nd element for pseudoknot]");
		FREE_DEBUG (tmp_el,
		            "1st element for pseudoknot in initialize_pseudoknot [failed to initialize 2nd element for pseudoknot]");
		FREE_DEBUG (tmp_unpaired_element,
		            "unpaired_element for 1st pseudoknot element in initialize_pseudoknot [failed to initialize 2nd element for pseudoknot]");
		return NULL;
	}
	
	unpaired_element = MALLOC_DEBUG (sizeof (nt_unpaired_element),
	                                 "unpaired_element for 2nd pseudoknot element in initialize_pseudoknot");
	                                 
	if (!unpaired_element) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize unpaired_element for 2nd pseudoknot element in initialize_pseudoknot",
		              false);
		FREE_DEBUG (pk_constraint,
		            "pseudoknot constraint in initialize_pseudoknot [failed to initialize 2nd unpaired_element for pseudoknot]");
		FREE_DEBUG (pk,
		            "pseudoknot in initialize_pseudoknot [failed to initialize 2nd unpaired_element for pseudoknot]");
		FREE_DEBUG (tmp_el,
		            "1st element for pseudoknot in initialize_pseudoknot [failed to initialize 2nd unpaired_element for pseudoknot]");
		FREE_DEBUG (tmp_unpaired_element,
		            "unpaired_element for 1st pseudoknot element in initialize_pseudoknot [failed to initialize 2nd unpaired_element for pseudoknot]");
		FREE_DEBUG (el,
		            "2nd element for pseudoknot in initialize_pseudoknot [failed to initialize 2nd unpaired_element for pseudoknot]");
		return NULL;
	}
	
	unpaired_element->min = min;
	unpaired_element->max = max;
	unpaired_element->i_constraint.reference = pk_constraint;
	unpaired_element->i_constraint.element_type = constraint_tp_element;
	unpaired_element->prev_type = no_element_type;
	unpaired_element->prev_paired = NULL;
	unpaired_element->prev_unpaired = NULL;
	unpaired_element->prev_branch_type = unbranched;
	unpaired_element->next = NULL;
	el->type = unpaired;
	el->unpaired = unpaired_element;
	pk->tp_element = el;
	pk_constraint->type = pseudoknot;
	pk_constraint->pseudoknot = pk;
	pk_constraint->next = NULL;
	return pk_constraint;
}

ntp_constraint initialize_base_triple() {
	#ifdef DEBUG_ON
	char msg[100];
	sprintf (msg, "initializing base triple in initialize_base_triple");
	COMMIT_DEBUG (REPORT_INFO, MODEL, msg, true);
	#endif
	REGISTER
	ntp_constraint restrict bt_constraint = MALLOC_DEBUG (sizeof (nt_constraint),
	                                        "base triple constraint in initialize_base_triple");
	                                        
	if (!bt_constraint) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize base triple constraint in initialize_base_triple", false);
		return NULL;
	}
	
	REGISTER
	ntp_base_triple restrict bt = MALLOC_DEBUG (sizeof (nt_base_triple),
	                                        "base triple in initialize_base_triple");
	                                        
	if (!bt) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize base triple in initialize_base_triple", false);
		FREE_DEBUG (bt_constraint,
		            "base triple constraint in initialize_element [failed to initialize base triple in initialize_base_triple]");
		return NULL;
	}
	
	// 1st element (containing unpaired)
	REGISTER
	ntp_element restrict el = MALLOC_DEBUG (sizeof (nt_element),
	                                        "1st element for base triple in initialize_base_triple");
	                                        
	if (!el) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize 1st element for base triple in initialize_base_triple",
		              false);
		FREE_DEBUG (bt_constraint,
		            "base triple constraint in initialize_base_triple [failed to initialize 1st element for base triple]");
		FREE_DEBUG (bt,
		            "base triple in initialize_base_triple [failed to initialize 1st element for base triple]");
		return NULL;
	}
	
	REGISTER
	ntp_unpaired_element restrict unpaired_element =
	                    MALLOC_DEBUG (sizeof (nt_unpaired_element),
	                                  "unpaired_element for 1st base triple element in initialize_base_triple");
	                                  
	if (!unpaired_element) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize unpaired_element for 1st base triple element in initialize_base_triple",
		              false);
		FREE_DEBUG (bt_constraint,
		            "base triple constraint in initialize_base_triple [failed to initialize 1st unpaired_element for base triple]");
		FREE_DEBUG (bt,
		            "base triple in initialize_base_triple [failed to initialize 1st unpaired_element for base triple]");
		FREE_DEBUG (el,
		            "1st element for base triple in initialize_base_triple [failed to initialize 1st unpaired_element for base triple]");
		return NULL;
	}
	
	// assuming fixed size for base triple (base pair + single element)
	unpaired_element->min = 1;
	unpaired_element->max = 1;
	unpaired_element->i_constraint.reference = bt_constraint;
	unpaired_element->i_constraint.element_type = constraint_fp_element;
	unpaired_element->prev_type = no_element_type;
	unpaired_element->prev_paired = NULL;
	unpaired_element->prev_unpaired = NULL;
	unpaired_element->prev_branch_type = unbranched;
	unpaired_element->next = NULL;
	el->type = unpaired;
	el->unpaired = unpaired_element;
	bt->fp_element = el;
	nt_unpaired_element *restrict tmp_unpaired_element = unpaired_element;
	nt_element *restrict tmp_el = el;
	// 2nd element (containing unpaired)
	el = MALLOC_DEBUG (sizeof (nt_element),
	                   "2nd element for base triple in initialize_base_triple");
	                   
	if (!el) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize 2nd element for base triple in initialize_base_triple",
		              false);
		FREE_DEBUG (bt_constraint,
		            "base triple constraint in initialize_base_triple [failed to initialize 2nd element for base triple]");
		FREE_DEBUG (bt,
		            "base triple in initialize_base_triple [failed to initialize 2nd element for base triple]");
		FREE_DEBUG (tmp_el,
		            "1st element for base triple in initialize_base_triple [failed to initialize 2nd element for base triple]");
		FREE_DEBUG (tmp_unpaired_element,
		            "unpaired_element for 1st base triple element in initialize_base_triple [failed to initialize 2nd element for base triple]");
		return NULL;
	}
	
	unpaired_element = MALLOC_DEBUG (sizeof (nt_unpaired_element),
	                                 "unpaired_element for 2nd base triple element in initialize_base_triple");
	                                 
	if (!unpaired_element) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize unpaired_element for 2nd base triple element in initialize_base_triple",
		              false);
		FREE_DEBUG (bt_constraint,
		            "base triple constraint in initialize_base_triple [failed to initialize 2nd unpaired_element for base triple]");
		FREE_DEBUG (bt,
		            "base triple in initialize_base_triple [failed to initialize 2nd unpaired_element for base triple]");
		FREE_DEBUG (tmp_el,
		            "1st element for base triple in initialize_base_triple [failed to initialize 2nd unpaired_element for base triple]");
		FREE_DEBUG (tmp_unpaired_element,
		            "unpaired_element for 1st base triple element in initialize_base_triple [failed to initialize 2nd unpaired_element for base triple]");
		FREE_DEBUG (el,
		            "2nd element for base triple in initialize_base_triple [failed to initialize 2nd unpaired_element for base triple]");
		return NULL;
	}
	
	unpaired_element->min = 1;
	unpaired_element->max = 1;
	unpaired_element->i_constraint.reference = bt_constraint;
	unpaired_element->i_constraint.element_type = constraint_tp_element;
	unpaired_element->prev_type = no_element_type;
	unpaired_element->prev_paired = NULL;
	unpaired_element->prev_unpaired = NULL;
	unpaired_element->prev_branch_type = unbranched;
	unpaired_element->next = NULL;
	el->type = unpaired;
	el->unpaired = unpaired_element;
	bt->tp_element = el;
	// 3rd, single, element (containing unpaired)
	REGISTER
	nt_unpaired_element *restrict tmp_unpaired_element2 = unpaired_element;
	REGISTER
	nt_element *restrict tmp_el2 = el;
	el = MALLOC_DEBUG (sizeof (nt_element),
	                   "3rd (single) element for base triple in initialize_base_triple");
	                   
	if (!el) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize 3rd (single) element for base triple in initialize_base_triple",
		              false);
		FREE_DEBUG (bt_constraint,
		            "base triple constraint in initialize_base_triple [failed to initialize 3rd (single) element for base triple]");
		FREE_DEBUG (bt,
		            "base triple in initialize_base_triple [failed to initialize 3rd (single) element for base triple]");
		FREE_DEBUG (tmp_el2,
		            "2nd element for base triple in initialize_base_triple [failed to initialize 3rd (single) element for base triple]");
		FREE_DEBUG (tmp_unpaired_element2,
		            "unpaired_element for 2nd base triple element in initialize_base_triple [failed to initialize 3rd (single) element for base triple]");
		FREE_DEBUG (tmp_el,
		            "1st element for base triple in initialize_base_triple [failed to initialize 3rd (single) element for base triple]");
		FREE_DEBUG (tmp_unpaired_element,
		            "unpaired_element for 1st base triple element in initialize_base_triple [failed to initialize 3rd (single) element for base triple]");
		return NULL;
	}
	
	unpaired_element = MALLOC_DEBUG (sizeof (nt_unpaired_element),
	                                 "unpaired_element for 3rd base triple (single) element in initialize_base_triple");
	                                 
	if (!unpaired_element) {
		COMMIT_DEBUG (REPORT_ERRORS, MODEL,
		              "could not initialize unpaired_element for 3rd base triple (single) element in initialize_base_triple",
		              false);
		FREE_DEBUG (bt_constraint,
		            "base triple constraint in initialize_base_triple [failed to initialize 3rd unpaired_element for base triple]");
		FREE_DEBUG (bt,
		            "base triple in initialize_base_triple [failed to initialize 3rd unpaired_element for base triple]");
		FREE_DEBUG (tmp_el2,
		            "2nd element for base triple in initialize_base_triple [failed to initialize 3rd unpaired_element for base triple]");
		FREE_DEBUG (tmp_unpaired_element2,
		            "unpaired_element for 2nd base triple element in initialize_base_triple [failed to initialize 3rd unpaired_element for base triple]");
		FREE_DEBUG (tmp_el,
		            "1st element for base triple in initialize_base_triple [failed to initialize 3rd unpaired_element for base triple]");
		FREE_DEBUG (tmp_unpaired_element,
		            "unpaired_element for 1st base triple element in initialize_base_triple [failed to initialize 3rd unpaired_element for base triple]");
		FREE_DEBUG (el,
		            "2nd element for base triple in initialize_base_triple [failed to initialize 2nd unpaired_element for base triple]");
		return NULL;
	}
	
	unpaired_element->min = 1;
	unpaired_element->max = 1;
	unpaired_element->i_constraint.reference = bt_constraint;
	unpaired_element->i_constraint.element_type = constraint_single_element;
	unpaired_element->prev_type = no_element_type;
	unpaired_element->prev_paired = NULL;
	unpaired_element->prev_unpaired = NULL;
	unpaired_element->prev_branch_type = unbranched;
	unpaired_element->next = NULL;
	el->type = unpaired;
	el->unpaired = unpaired_element;
	bt->single_element = el;
	bt_constraint->type = base_triple;
	bt_constraint->base_triple = bt;
	bt_constraint->next = NULL;
	return bt_constraint;
}

ntp_element add_element_to_model (ntp_model restrict model,
                                  ntp_element restrict prev_el, ntp_element restrict new_el,
                                  nt_branch_type br_type) {
	if (!model || !new_el || (! (new_el->type == paired ||
	                             new_el->type == unpaired))) {
		return NULL;
	}
	
	if (!prev_el) {
		if (br_type != unbranched || model->first_element) {
			return NULL;
		}
		
		if (new_el->type == paired) {
			model->first_element = new_el;
			return new_el;
		}
		
		else {
			// if unpaired and is first model element, wrap a 0-stack-len paired element around unpaired element
			prev_el = add_element_to_model (model, NULL, initialize_element (paired, 0, 0),
			                                unbranched);
			br_type = five_prime;
		}
	}
	
	if (prev_el->type == paired) {
		if (br_type == five_prime && !prev_el->paired->fp_next) {
			prev_el->paired->fp_next = new_el;
			
			if (new_el->type == unpaired) {
				new_el->unpaired->prev_type = paired;
				new_el->unpaired->prev_paired = prev_el;
				new_el->unpaired->prev_branch_type = five_prime;
			}
		}
		
		else
			if (br_type == three_prime && !prev_el->paired->tp_next) {
				prev_el->paired->tp_next = new_el;
				
				if (new_el->type == unpaired) {
					new_el->unpaired->prev_type = paired;
					new_el->unpaired->prev_paired = prev_el;
					new_el->unpaired->prev_branch_type = three_prime;
				}
			}
			
			else {
				return NULL;
			}
	}
	
	else {
		if (br_type != unbranched || prev_el->unpaired->next) {
			return NULL;
		}
		
		prev_el->unpaired->next = new_el;
		
		if (new_el->type == unpaired) {
			new_el->unpaired->prev_type = unpaired;
			new_el->unpaired->prev_unpaired = prev_el;
		}
	}
	
	return new_el;
}

ntp_constraint add_pseudoknot_to_model (ntp_model restrict model,
                                        ntp_element restrict prev_el_fp,
                                        nt_branch_type br_type_fp,
                                        ntp_element restrict prev_el_tp,
                                        nt_branch_type br_type_tp,
                                        ntp_constraint restrict new_pk_constraint) {
	if (!model || !new_pk_constraint || ! (new_pk_constraint->type == pseudoknot) ||
	    !new_pk_constraint->pseudoknot || !new_pk_constraint->pseudoknot->fp_element ||
	    !new_pk_constraint->pseudoknot->tp_element) {
		return NULL;
	}
	
	if (!prev_el_tp) {
		return NULL;
	}
	
	else
		if (!prev_el_fp) {
			if (br_type_fp != unbranched || model->first_element) {
				return NULL;
			}
			
			// if is first model element, wrap a 0-stack-len paired element around pseudoknot
			prev_el_fp = add_element_to_model (model, NULL, initialize_element (paired, 0,
			                                        0), unbranched);
			br_type_fp = five_prime;
		}
		
	if (prev_el_fp->type == unpaired) {
		if (br_type_fp != unbranched || prev_el_fp->unpaired->next) {
			return NULL;
		}
		
		prev_el_fp->unpaired->next = new_pk_constraint->pseudoknot->fp_element;
		new_pk_constraint->pseudoknot->fp_element->unpaired->prev_type = unpaired;
		new_pk_constraint->pseudoknot->fp_element->unpaired->prev_unpaired = prev_el_fp;
	}
	
	else
		if (prev_el_fp->type == paired) {
			if (br_type_fp == five_prime && !prev_el_fp->paired->fp_next) {
				prev_el_fp->paired->fp_next = new_pk_constraint->pseudoknot->fp_element;
				new_pk_constraint->pseudoknot->fp_element->unpaired->prev_type = paired;
				new_pk_constraint->pseudoknot->fp_element->unpaired->prev_paired = prev_el_fp;
				new_pk_constraint->pseudoknot->fp_element->unpaired->prev_branch_type =
				                    five_prime;
			}
			
			else
				if (br_type_fp == three_prime && !prev_el_fp->paired->tp_next) {
					prev_el_fp->paired->tp_next = new_pk_constraint->pseudoknot->fp_element;
					new_pk_constraint->pseudoknot->fp_element->unpaired->prev_type = paired;
					new_pk_constraint->pseudoknot->fp_element->unpaired->prev_paired = prev_el_fp;
					new_pk_constraint->pseudoknot->fp_element->unpaired->prev_branch_type =
					                    three_prime;
				}
				
				else {
					return NULL;
				}
		}
		
		else {
			return NULL;
		}
		
	if (prev_el_tp->type == unpaired) {
		if (br_type_tp != unbranched || prev_el_tp->unpaired->next) {
			return NULL;
		}
		
		prev_el_tp->unpaired->next = new_pk_constraint->pseudoknot->tp_element;
		new_pk_constraint->pseudoknot->tp_element->unpaired->prev_type = unpaired;
		new_pk_constraint->pseudoknot->tp_element->unpaired->prev_unpaired = prev_el_tp;
	}
	
	else
		if (prev_el_tp->type == paired) {
			if (br_type_tp == five_prime && !prev_el_tp->paired->fp_next) {
				prev_el_tp->paired->fp_next = new_pk_constraint->pseudoknot->tp_element;
				new_pk_constraint->pseudoknot->tp_element->unpaired->prev_type = paired;
				new_pk_constraint->pseudoknot->tp_element->unpaired->prev_paired = prev_el_tp;
				new_pk_constraint->pseudoknot->tp_element->unpaired->prev_branch_type =
				                    five_prime;
			}
			
			else
				if (br_type_tp == three_prime && !prev_el_tp->paired->tp_next) {
					prev_el_tp->paired->tp_next = new_pk_constraint->pseudoknot->tp_element;
					new_pk_constraint->pseudoknot->tp_element->unpaired->prev_type = paired;
					new_pk_constraint->pseudoknot->tp_element->unpaired->prev_paired = prev_el_tp;
					new_pk_constraint->pseudoknot->tp_element->unpaired->prev_branch_type =
					                    three_prime;
				}
				
				else {
					return NULL;
				}
		}
		
		else {
			return NULL;
		}
		
	// add this pk to model's list
	if (!model->first_constraint) {
		model->first_constraint = new_pk_constraint;
	}
	
	else {
		ntp_constraint last_constraint = model->first_constraint;
		
		while (last_constraint->next != NULL) {
			last_constraint = last_constraint->next;
		}
		
		last_constraint->next = new_pk_constraint;
	}
	
	return new_pk_constraint;
}

ntp_constraint add_base_triple_to_model (ntp_model restrict model,
                                        ntp_element restrict prev_el_fp,
                                        nt_branch_type br_type_fp,
                                        ntp_element restrict prev_el_tp,
                                        nt_branch_type br_type_tp,
                                        ntp_element restrict prev_el_single,
                                        nt_branch_type br_type_single,
                                        ntp_constraint restrict new_base_triple_constraint) {
	if (!model || !new_base_triple_constraint ||
	    ! (new_base_triple_constraint->type == base_triple) ||
	    !new_base_triple_constraint->base_triple ||
	    !new_base_triple_constraint->base_triple->fp_element ||
	    !new_base_triple_constraint->base_triple->tp_element ||
	    !new_base_triple_constraint->base_triple->single_element) {
		return NULL;
	}
	
	if (!prev_el_fp && !prev_el_single) {
		return NULL;
	}
	
	if (!prev_el_tp) {
		return NULL;
	}
	
	else
		if (!prev_el_fp) {
			if (br_type_fp != unbranched || model->first_element) {
				return NULL;
			}
			
			prev_el_fp = add_element_to_model (model, NULL, initialize_element (paired, 0,
			                                        0), unbranched);
			br_type_fp = five_prime;
		}
		
		else
			if (!prev_el_single) {
				if (br_type_single != unbranched || model->first_element) {
					return NULL;
				}
				
				prev_el_single = add_element_to_model (model, NULL, initialize_element (paired,
				                                        0, 0), unbranched);
				br_type_single = five_prime;
			}
			
	if (prev_el_fp->type == unpaired) {
		if (br_type_fp != unbranched || prev_el_fp->unpaired->next) {
			return NULL;
		}
		
		prev_el_fp->unpaired->next =
		                    new_base_triple_constraint->base_triple->fp_element;
		new_base_triple_constraint->base_triple->fp_element->unpaired->prev_type =
		                    unpaired;
		new_base_triple_constraint->base_triple->fp_element->unpaired->prev_unpaired =
		                    prev_el_fp;
	}
	
	else
		if (prev_el_fp->type == paired) {
			if (br_type_fp == five_prime && !prev_el_fp->paired->fp_next) {
				prev_el_fp->paired->fp_next =
				                    new_base_triple_constraint->base_triple->fp_element;
				new_base_triple_constraint->base_triple->fp_element->unpaired->prev_type =
				                    paired;
				new_base_triple_constraint->base_triple->fp_element->unpaired->prev_paired =
				                    prev_el_fp;
				new_base_triple_constraint->base_triple->fp_element->unpaired->prev_branch_type
				                    = five_prime;
			}
			
			else
				if (br_type_fp == three_prime && !prev_el_fp->paired->tp_next) {
					prev_el_fp->paired->tp_next =
					                    new_base_triple_constraint->base_triple->fp_element;
					new_base_triple_constraint->base_triple->fp_element->unpaired->prev_type =
					                    paired;
					new_base_triple_constraint->base_triple->fp_element->unpaired->prev_paired =
					                    prev_el_fp;
					new_base_triple_constraint->base_triple->fp_element->unpaired->prev_branch_type
					                    = three_prime;
				}
				
				else {
					return NULL;
				}
		}
		
		else {
			return NULL;
		}
		
	if (prev_el_single->type == unpaired) {
		if (br_type_single != unbranched || prev_el_single->unpaired->next) {
			return NULL;
		}
		
		prev_el_single->unpaired->next =
		                    new_base_triple_constraint->base_triple->single_element;
		new_base_triple_constraint->base_triple->single_element->unpaired->prev_type =
		                    unpaired;
		new_base_triple_constraint->base_triple->single_element->unpaired->prev_unpaired
		                    = prev_el_single;
	}
	
	else
		if (prev_el_single->type == paired) {
			if (br_type_single == five_prime && !prev_el_single->paired->fp_next) {
				prev_el_single->paired->fp_next =
				                    new_base_triple_constraint->base_triple->single_element;
				new_base_triple_constraint->base_triple->single_element->unpaired->prev_type =
				                    paired;
				new_base_triple_constraint->base_triple->single_element->unpaired->prev_paired =
				                    prev_el_single;
				new_base_triple_constraint->base_triple->single_element->unpaired->prev_branch_type
				                    = five_prime;
			}
			
			else
				if (br_type_single == three_prime && !prev_el_single->paired->tp_next) {
					prev_el_single->paired->tp_next =
					                    new_base_triple_constraint->base_triple->single_element;
					new_base_triple_constraint->base_triple->single_element->unpaired->prev_type =
					                    paired;
					new_base_triple_constraint->base_triple->single_element->unpaired->prev_paired =
					                    prev_el_single;
					new_base_triple_constraint->base_triple->single_element->unpaired->prev_branch_type
					                    = three_prime;
				}
				
				else {
					return NULL;
				}
		}
		
		else {
			return NULL;
		}
		
	// always do prev_el_tp
	if (prev_el_tp->type == unpaired) {
		if (br_type_tp != unbranched || prev_el_tp->unpaired->next) {
			return NULL;
		}
		
		prev_el_tp->unpaired->next =
		                    new_base_triple_constraint->base_triple->tp_element;
		new_base_triple_constraint->base_triple->tp_element->unpaired->prev_type =
		                    unpaired;
		new_base_triple_constraint->base_triple->tp_element->unpaired->prev_unpaired =
		                    prev_el_tp;
	}
	
	else
		if (prev_el_tp->type == paired) {
			if (br_type_tp == five_prime && !prev_el_tp->paired->fp_next) {
				prev_el_tp->paired->fp_next =
				                    new_base_triple_constraint->base_triple->tp_element;
				new_base_triple_constraint->base_triple->tp_element->unpaired->prev_type =
				                    paired;
				new_base_triple_constraint->base_triple->tp_element->unpaired->prev_paired =
				                    prev_el_tp;
				new_base_triple_constraint->base_triple->tp_element->unpaired->prev_branch_type
				                    = five_prime;
			}
			
			else
				if (br_type_tp == three_prime && !prev_el_tp->paired->tp_next) {
					prev_el_tp->paired->tp_next =
					                    new_base_triple_constraint->base_triple->tp_element;
					new_base_triple_constraint->base_triple->tp_element->unpaired->prev_type =
					                    paired;
					new_base_triple_constraint->base_triple->tp_element->unpaired->prev_paired =
					                    prev_el_tp;
					new_base_triple_constraint->base_triple->tp_element->unpaired->prev_branch_type
					                    = three_prime;
				}
				
				else {
					return NULL;
				}
		}
		
		else {
			return NULL;
		}
		
	// add this bt to model's list
	if (!model->first_constraint) {
		model->first_constraint = new_base_triple_constraint;
	}
	
	else {
		ntp_constraint last_constraint = model->first_constraint;
		
		while (last_constraint->next != NULL) {
			last_constraint = last_constraint->next;
		}
		
		last_constraint->next = new_base_triple_constraint;
	}
	
	return new_base_triple_constraint;
}

static void finalize_model_at (ntp_element element) {
	while (element) {
		if (element->type == unpaired) {
			REGISTER
			ntp_element this_element = element;
			element = element->unpaired->next;
			FREE_DEBUG (this_element->unpaired, NULL);
			FREE_DEBUG (this_element, NULL);
		}
		
		else
			if (element->type == paired) {
				REGISTER
				ntp_element this_element = element;
				
				if (element->paired->fp_next) {
					finalize_model_at (element->paired->fp_next);
				}
				
				if (element->paired->tp_next) {
					finalize_model_at (element->paired->tp_next);
				}
				
				FREE_DEBUG (this_element->paired, NULL);
				FREE_DEBUG (this_element, NULL);
				return;
			}
			
			else {
				return; // COMMIT_DEBUG ERROR
			}
	}
}

void finalize_model (ntp_model restrict model) {
	if (!model) {
		return;
	}
	
	else {
		COMMIT_DEBUG (REPORT_INFO, MODEL, "finalizing model in finalize_model", true);
		COMMIT_DEBUG (REPORT_INFO, MODEL,
		              "purging all cached sequences that reference model in finalize_model", false);
		purge_seq_bp_cache_by_model (model);
		
		if (model->first_element) {
			#ifdef DEBUG_MEM
			unsigned long s_num_entries, e_num_entries, s_alloc_size, e_alloc_size;
			MALLOC_CP (&s_num_entries, &s_alloc_size);
			#endif
			finalize_model_at (model->first_element);
			#ifdef DEBUG_MEM
			MALLOC_CP (&e_num_entries, &e_alloc_size);
			COMMIT_DEBUG2 (REPORT_INFO, MEM,
			               "%lu mem entries freed (total size is %lu) in finalize_model",
			               s_num_entries - e_num_entries, s_alloc_size - e_alloc_size, false);
			#endif
		}
		
		if (model->first_constraint) {
			REGISTER
			ntp_constraint restrict tmp_constraint;
			
			do {
				tmp_constraint = model->first_constraint->next;
				
				switch (model->first_constraint->type) {
					case pseudoknot:
						FREE_DEBUG (model->first_constraint->pseudoknot,
						            "pseudoknot constraint in finalize_model");
						break;
						
					case base_triple:
						FREE_DEBUG (model->first_constraint->base_triple,
						            "base triple constraint in finalize_model");
						break;
						
					default:
						break;
				}
				
				FREE_DEBUG (model->first_constraint, "constraint in finalize_model");
				model->first_constraint = tmp_constraint;
			}
			while (tmp_constraint != NULL);
		}
		
		FREE_DEBUG (model, "model in finalize_model");
	}
}
