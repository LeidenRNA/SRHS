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
#include "interface.h"
#include "mfe.h"
#include "m_seq_bp.h"
#include "m_analyse.h"
#include "m_list.h"
#include "m_optimize.h"
#include "m_search.h"

// distribution of fe's of any hits returned by search_seq_at - required if
// number of hits exceeds MAX_HITS_RETURNED and filtering by FE is needed;
// 20000 accomodates data in the ranga of -100 to 100 kcal/mol and up
// to two decimal places in precision
static long
hit_fe_distr[20000];

static ntp_element wrapper_constraint_elements[MAX_CONSTRAINT_MATCHES];
static ushort last_wrapper_constraint_track_id;

/*
 * static, inline replacements for memset/memcpy - silences google sanitizers
 */
static inline void g_memset (void *p, const char v, const int len) {
	REGISTER
	char *pc = (char *)p;
	
	for (REGISTER int i = 0; i < len; i++) {
		pc[i] = v;
	}
}

static inline void g_memcpy (void *p, const void *r, const int len) {
	REGISTER
	char *pc = (char *)p, *rc = (char *)r;
	
	for (REGISTER int i = 0; i < len; i++) {
		pc[i] = rc[i];
	}
}

static inline void copy_linked_bp_elements (ntp_linked_bp restrict
                                        linked_bp_copy,
                                        ntp_linked_bp restrict next_linked_bp,
                                        const nt_element *restrict fp_element,
                                        const nt_element *restrict tp_element,
                                        const bool tag_alloc, const uchar tag) {
	if (fp_element) {
		REGISTER
		ntp_element last_fp_element = linked_bp_copy->fp_elements, this_fp_element;
		
		do {
			if (tag_alloc) {
				this_fp_element = MALLOC_TAG (sizeof (nt_element), tag);
				this_fp_element->unpaired = MALLOC_TAG (sizeof (nt_unpaired_element), tag);
			}
			
			else {
				this_fp_element = MALLOC_DEBUG (sizeof (nt_element),
				                                "linked_bp_copy->fp_elements for copy of found_list in search_seq");
				this_fp_element->unpaired = MALLOC_DEBUG (sizeof (nt_unpaired_element),
				                                        "linked_bp_copy->fp_elements->unpaired for copy of found_list in search_seq");
			}
			
			this_fp_element->type = unpaired;
			this_fp_element->unpaired->dist = fp_element->unpaired->dist;
			this_fp_element->unpaired->length = fp_element->unpaired->length;
			this_fp_element->unpaired->next_linked_bp = next_linked_bp;
			this_fp_element->unpaired->i_constraint.reference =
			                    fp_element->unpaired->i_constraint.reference;
			this_fp_element->unpaired->i_constraint.element_type =
			                    fp_element->unpaired->i_constraint.element_type;
			this_fp_element->unpaired->next = NULL;
			
			if (last_fp_element) {
				last_fp_element->unpaired->next = this_fp_element;
				last_fp_element = this_fp_element;
			}
			
			else {
				last_fp_element = this_fp_element;
				linked_bp_copy->fp_elements = this_fp_element;
			}
			
			fp_element = fp_element->unpaired->next;
		}
		while (fp_element);
	}
	
	if (tp_element) {
		REGISTER
		ntp_element last_tp_element = linked_bp_copy->tp_elements, this_tp_element;
		
		do {
			if (tag_alloc) {
				this_tp_element = MALLOC_TAG (sizeof (nt_element), tag);
				this_tp_element->unpaired = MALLOC_TAG (sizeof (nt_unpaired_element), tag);
			}
			
			else {
				this_tp_element = MALLOC_DEBUG (sizeof (nt_element),
				                                "linked_bp_copy->tp_elements for copy of found_list in search_seq");
				this_tp_element->unpaired = MALLOC_DEBUG (sizeof (nt_unpaired_element),
				                                        "linked_bp_copy->tp_elements->unpaired for copy of found_list in search_seq");
			}
			
			this_tp_element->type = unpaired;
			this_tp_element->unpaired->dist = tp_element->unpaired->dist;
			this_tp_element->unpaired->length = tp_element->unpaired->length;
			this_tp_element->unpaired->next_linked_bp = next_linked_bp;
			this_tp_element->unpaired->i_constraint.reference =
			                    tp_element->unpaired->i_constraint.reference;
			this_tp_element->unpaired->i_constraint.element_type =
			                    tp_element->unpaired->i_constraint.element_type;
			this_tp_element->unpaired->next = NULL;
			
			if (last_tp_element) {
				last_tp_element->unpaired->next = this_tp_element;
				last_tp_element = this_tp_element;
			}
			
			else {
				last_tp_element = this_tp_element;
				linked_bp_copy->tp_elements = this_tp_element;
			}
			
			tp_element = tp_element->unpaired->next;
		}
		while (tp_element);
	}
}

static inline void copy_linked_bp_elements3 (ntp_linked_bp restrict
                                        linked_bp_copy,
                                        ntp_linked_bp restrict next_linked_bp,
                                        const nt_element *restrict element,
                                        const enum branch_type type) {
	// TODO: error handling
	if (type == five_prime) {
		REGISTER
		ntp_element last_fp_element = linked_bp_copy->fp_elements, this_fp_element;
		this_fp_element =
		                    MALLOC_DEBUG (sizeof (nt_element),
		                                  "linked_bp_copy->fp_elements for safe_copy of found_list in search_seq");
		this_fp_element->unpaired =
		                    MALLOC_DEBUG (sizeof (nt_unpaired_element),
		                                  "linked_bp_copy->fp_elements->unpaired for safe_copy of found_list in search_seq");
		this_fp_element->type = unpaired;
		this_fp_element->unpaired->dist = element->unpaired->dist;
		this_fp_element->unpaired->length = element->unpaired->length;
		this_fp_element->unpaired->next_linked_bp = next_linked_bp;
		this_fp_element->unpaired->i_constraint.reference =
		                    element->unpaired->i_constraint.reference;
		this_fp_element->unpaired->i_constraint.element_type =
		                    element->unpaired->i_constraint.element_type;
		this_fp_element->unpaired->next = NULL;
		
		if (last_fp_element) {
			while (last_fp_element->unpaired->next) {
				last_fp_element = last_fp_element->unpaired->next;
			}
			
			last_fp_element->unpaired->next = this_fp_element;
		}
		
		else {
			linked_bp_copy->fp_elements = this_fp_element;
		}
	}
	
	else {
		REGISTER
		ntp_element last_tp_element = linked_bp_copy->tp_elements, this_tp_element;
		this_tp_element =
		                    MALLOC_DEBUG (sizeof (nt_element),
		                                  "linked_bp_copy->tp_elements for safe_copy of found_list in search_seq");
		this_tp_element->unpaired =
		                    MALLOC_DEBUG (sizeof (nt_unpaired_element),
		                                  "linked_bp_copy->tp_elements->unpaired for safe_copy of found_list in search_seq");
		this_tp_element->type = unpaired;
		this_tp_element->unpaired->dist = element->unpaired->dist;
		this_tp_element->unpaired->length = element->unpaired->length;
		this_tp_element->unpaired->next_linked_bp = next_linked_bp;
		this_tp_element->unpaired->i_constraint.reference =
		                    element->unpaired->i_constraint.reference;
		this_tp_element->unpaired->i_constraint.element_type =
		                    element->unpaired->i_constraint.element_type;
		this_tp_element->unpaired->next = NULL;
		
		if (last_tp_element) {
			while (last_tp_element->unpaired->next) {
				last_tp_element = last_tp_element->unpaired->next;
			}
			
			last_tp_element->unpaired->next = this_tp_element;
		}
		
		else {
			linked_bp_copy->tp_elements = this_tp_element;
		}
	}
}

static inline bool safe_copy_linked_bp (ntp_list restrict safe_copy,
                                        ntp_linked_bp restrict found_linked_bp,
                                        ntp_linked_bp restrict *next_linked_bp) {
	#ifndef NO_FULL_CHECKS
                                        
	if (!safe_copy) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "safe_copy is NULL in safe_copy_linked_bp", false);
		return false;
	}
	
	if (!list_iterator_start (safe_copy)) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "cannot iterate on safe_copy in safe_copy_linked_bp", false);
		return false;
	}
	
	list_iterator_stop (safe_copy);
	
	if (!found_linked_bp) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "found_linked_bp is NULL in safe_copy_linked_bp", false);
		return false;
	}
	
	if (num_matches && (!matched_linked_bps || !matched_fp_elements ||
	                    !matched_tp_elements)) {
		if (!matched_linked_bps) {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "matched_linked_bps is NULL in safe_copy_linked_bp", false);
		}
		
		if (!matched_fp_elements) {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "matched_fp_elements is NULL in safe_copy_linked_bp", false);
		}
		
		if (!matched_tp_elements) {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "matched_tp_elements is NULL in safe_copy_linked_bp", false);
		}
		
		return false;
	}
	
	#endif
	#ifndef NO_FULL_CHECKS
	
	if (
	#endif
	                    list_iterator_start (safe_copy)
                    #ifndef NO_FULL_CHECKS
	) {
                    #else
	                    ;
                    #endif
		REGISTER
		ntp_linked_bp restrict safe_linked_bp;
		REGISTER
		ntp_linked_bp restrict found_linked_bp_copy = found_linked_bp;
		
		while (list_iterator_hasnext (safe_copy)) {
			safe_linked_bp = list_iterator_next (safe_copy);
			#ifndef NO_FULL_CHECKS
			
			if (safe_linked_bp) {
			#endif
				REGISTER
				bool different_linked_bp = false;
				
				do {
					if (safe_linked_bp->track_id != found_linked_bp->track_id ||
					    safe_linked_bp->stack_len != found_linked_bp->stack_len ||
					    safe_linked_bp->bp->fp_posn != found_linked_bp->bp->fp_posn ||
					    safe_linked_bp->bp->tp_posn != found_linked_bp->bp->tp_posn) {
						different_linked_bp = true;
						break;
					}
					
					if ((safe_linked_bp->fp_elements && !found_linked_bp->fp_elements) ||
					    (!safe_linked_bp->fp_elements && found_linked_bp->fp_elements)) {
						different_linked_bp = true;
						break;
					}
					
					else
						if (safe_linked_bp->fp_elements) {
							REGISTER
							ntp_element restrict safe_fp_element = safe_linked_bp->fp_elements,
							                     found_fp_element = found_linked_bp->fp_elements;
							                     
							do {
								if (safe_fp_element->unpaired->i_constraint.reference !=
								    found_fp_element->unpaired->i_constraint.reference ||
								    safe_fp_element->unpaired->dist != found_fp_element->unpaired->dist ||
								    safe_fp_element->unpaired->length != found_fp_element->unpaired->length) {
									different_linked_bp = true;
									break;
								}
								
								safe_fp_element = safe_fp_element->unpaired->next;
							}
							while (safe_fp_element && found_fp_element);
							
							if (different_linked_bp || (safe_fp_element != found_fp_element)) {
								different_linked_bp = true;
								break;
							}
						}
						
					if ((safe_linked_bp->tp_elements && !found_linked_bp->tp_elements) ||
					    (!safe_linked_bp->tp_elements && found_linked_bp->tp_elements)) {
						different_linked_bp = true;
						break;
					}
					
					else
						if (safe_linked_bp->tp_elements) {
							REGISTER
							ntp_element restrict safe_tp_element = safe_linked_bp->tp_elements,
							                     found_tp_element = found_linked_bp->tp_elements;
							                     
							do {
								if (safe_tp_element->unpaired->i_constraint.reference !=
								    found_tp_element->unpaired->i_constraint.reference ||
								    safe_tp_element->unpaired->dist != found_tp_element->unpaired->dist ||
								    safe_tp_element->unpaired->length != found_tp_element->unpaired->length) {
									different_linked_bp = true;
									break;
								}
								
								safe_tp_element = safe_tp_element->unpaired->next;
							}
							while (safe_tp_element && found_tp_element);
							
							if (different_linked_bp || (safe_tp_element != found_tp_element)) {
								different_linked_bp = true;
								break;
							}
						}
						
					safe_linked_bp = safe_linked_bp->prev_linked_bp;
					found_linked_bp = found_linked_bp->prev_linked_bp;
				}
				while (safe_linked_bp && found_linked_bp);
				
				if (!different_linked_bp && (safe_linked_bp == found_linked_bp)) {
					list_iterator_stop (safe_copy);
					return true;
				}
				
				#ifndef NO_FULL_CHECKS
			}
			
			else {
				COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
				              "safe_linked_bp is NULL in safe_copy_linked_bp", false);
				list_iterator_stop (safe_copy);
				return false;
			}
			
				#endif
		}
		
		list_iterator_stop (safe_copy);
		found_linked_bp = found_linked_bp_copy;
		#ifndef NO_FULL_CHECKS
	}
	
		#endif
	
	REGISTER
	ntp_linked_bp linked_bp_copy = MALLOC_DEBUG (sizeof (nt_linked_bp),
	                                        "linked_bp_copy for safe_copy of found_list in search_seq");
	                                        
	#ifndef NO_FULL_CHECKS
	                                        
	if (!linked_bp_copy || (
	#endif
	
	                        list_append (safe_copy, linked_bp_copy)
	                        
                        #ifndef NO_FULL_CHECKS
	                        < 0)) {
		if (linked_bp_copy) {
			FREE_DEBUG (linked_bp_copy,
			            "linked_bp_copy for safe_copy of found_list in search_seq [failed to append to safe_copy]");
		}
		
		return false;
	}
	
                        #else
	                        ;
                        #endif
	
	do {
		ntp_bp bp = MALLOC_DEBUG (sizeof (nt_bp),
		                          "bp of linked_bp_copy for safe_copy of found_list in search_seq"); // TODO: error handling
		bp->fp_posn = found_linked_bp->bp->fp_posn;
		bp->tp_posn = found_linked_bp->bp->tp_posn;
		linked_bp_copy->bp = bp;
		linked_bp_copy->stack_len = found_linked_bp->stack_len;
		linked_bp_copy->track_id = found_linked_bp->track_id;
		linked_bp_copy->fp_elements = NULL;
		linked_bp_copy->tp_elements = NULL;
		
		while (found_linked_bp->fp_elements) {
			copy_linked_bp_elements3 (linked_bp_copy, *next_linked_bp,
			                          found_linked_bp->fp_elements, five_prime);
			found_linked_bp->fp_elements = found_linked_bp->fp_elements->unpaired->next;
		}
		
		while (found_linked_bp->tp_elements) {
			copy_linked_bp_elements3 (linked_bp_copy, *next_linked_bp,
			                          found_linked_bp->tp_elements, three_prime);
			found_linked_bp->tp_elements = found_linked_bp->tp_elements->unpaired->next;
		}
		
		if (found_linked_bp->prev_linked_bp != NULL) {
			*next_linked_bp = linked_bp_copy;
			found_linked_bp = found_linked_bp->prev_linked_bp;
			linked_bp_copy->prev_linked_bp =
			                    MALLOC_DEBUG (sizeof (nt_linked_bp),
			                                  "linked_bp_copy for safe_copy of found_list in search_seq");
			#ifndef NO_FULL_CHECKS
			                                  
			if (!linked_bp_copy->prev_linked_bp) {
				return false;
			}
			
			else {
			#endif
				linked_bp_copy = linked_bp_copy->prev_linked_bp;
				#ifndef NO_FULL_CHECKS
			}
			
				#endif
		}
		
		else {
			linked_bp_copy->prev_linked_bp = NULL;
			break;
		}
	}
	while (1);
	
	return true;
}

static inline bool satisfy_constraints (const ntp_seq restrict seq,
                                        const nt_constraint *restrict constraint,
                                        ntp_list satisfied_list,
                                        ntp_linked_bp found_linked_bp,
                                        ntp_linked_bp linked_bp,
                                        ntp_linked_bp *restrict next_linked_bp) {
	REGISTER
	ntp_linked_bp this_linked_bp = linked_bp;
	char    constraint_fp_buff[MAX_CONSTRAINT_MATCHES][MAX_SEQ_LEN + 1],
	        constraint_tp_buff[MAX_CONSTRAINT_MATCHES][MAX_SEQ_LEN + 1],
	        constraint_single_buff[MAX_CONSTRAINT_MATCHES][2];
	nt_rel_seq_len  constraint_fp_lengths[MAX_CONSTRAINT_MATCHES],
	                constraint_tp_lengths[MAX_CONSTRAINT_MATCHES];
	REGISTER
	nt_hit_count num_constraint_fp = 0, num_constraint_tp = 0,
	             num_constraint_single = 0;
	REGISTER
	char fp_nt, tp_nt, single_nt;
	REGISTER
	nt_rel_seq_posn fp_nt_posn = 0, single_nt_posn = 0;
	REGISTER
	bool have_constraint_fp = false, have_constraint_tp = false,
	     have_constraint_single = false;
	     
	do {
		REGISTER
		ntp_element restrict this_fp_element = linked_bp->fp_elements;
		REGISTER
		ntp_element restrict this_tp_element = linked_bp->tp_elements;
		REGISTER
		nt_rel_seq_posn this_fp_posn = linked_bp->bp->fp_posn,
		                this_tp_posn = linked_bp->bp->tp_posn;
		REGISTER
		nt_stack_size this_stack_len = linked_bp->stack_len;
		REGISTER
		nt_rel_seq_posn wrapper_next_fp_posn = 0;
		
		if (!this_fp_posn) {
			ntp_linked_bp tmp_linked_bp = found_linked_bp;
			
			while (tmp_linked_bp && tmp_linked_bp->prev_linked_bp != linked_bp) {
				tmp_linked_bp = tmp_linked_bp->prev_linked_bp;
			}
			
			if (tmp_linked_bp) {
				wrapper_next_fp_posn = tmp_linked_bp->bp->fp_posn;
			}
		}
		
		while ((this_fp_element || this_tp_element) &&
		       ((constraint->type == pseudoknot && (!have_constraint_fp ||
		                                               !have_constraint_tp)) ||
		        (constraint->type == base_triple && (!have_constraint_fp ||
		                                                !have_constraint_tp || !have_constraint_single)))) {
			/*
			 * for each constraint matched, copy fp/tp/single elements to the target buffer
			 * for later comparison; but also, keep track of originating fp/tp base-pair,
			 * where the first and second may be opposed (e.g. have a pseudoknot's fp
			 * element in the tp branch of a base-pair sub-structure)
			 */
			if (this_fp_element) {
				do {
					if (this_fp_element->unpaired->i_constraint.reference == constraint) {
						if ((constraint->type == pseudoknot &&
						     this_fp_element->unpaired->i_constraint.element_type == constraint_fp_element)
						    ||
						    (constraint->type == base_triple &&
						     this_fp_element->unpaired->i_constraint.element_type ==
						     constraint_fp_element)) {
							// store in constraint fp_buff
							constraint_fp_lengths[num_constraint_fp] = this_fp_element->unpaired->length;
							
							if (this_fp_posn) {
								g_memcpy (constraint_fp_buff[num_constraint_fp],
								          &seq[this_fp_posn - 1 + this_stack_len + this_fp_element->unpaired->dist],
								          constraint_fp_lengths[num_constraint_fp]);
								// track fp_nt_position and single_nt_position to determine orientation of base triple, when applicable
								fp_nt_posn = (nt_rel_seq_posn) (this_fp_posn - 1 + this_stack_len +
								                                this_fp_element->unpaired->dist);
								num_constraint_fp++;
								have_constraint_fp = true;
							}
							
							else {
								// for wrapper bp scenario, check that calculated sequence idx >= 0
								if (0 <= (wrapper_next_fp_posn - 1 - this_fp_element->unpaired->dist -
								          this_fp_element->unpaired->length)) {
									// track fp_nt_position and single_nt_position to determine orientation of base triple when applicable
									fp_nt_posn = (nt_rel_seq_posn) (wrapper_next_fp_posn - 1 -
									                                this_fp_element->unpaired->dist - this_fp_element->unpaired->length);
									g_memcpy (constraint_fp_buff[num_constraint_fp],
									          &seq[fp_nt_posn],
									          constraint_fp_lengths[num_constraint_fp]);
									num_constraint_fp++;
									have_constraint_fp = true;
								}
							}
						}
						
						else
							if ((constraint->type == pseudoknot &&
							     this_fp_element->unpaired->i_constraint.element_type == constraint_tp_element)
							    ||
							    (constraint->type == base_triple &&
							     this_fp_element->unpaired->i_constraint.element_type ==
							     constraint_tp_element)) {
								// store in constraint tp_buff
								constraint_tp_lengths[num_constraint_tp] = this_fp_element->unpaired->length;
								
								if (this_fp_posn) {
									g_memcpy (constraint_tp_buff[num_constraint_tp],
									          &seq[this_fp_posn - 1 + this_stack_len + this_fp_element->unpaired->dist],
									          constraint_tp_lengths[num_constraint_tp]);
									num_constraint_tp++;
									have_constraint_tp = true;
								}
								
								else {
									// for wrapper bp scenario, check that calculated sequence idx >= 0
									if (0 <= (wrapper_next_fp_posn - 1 - this_fp_element->unpaired->dist -
									          this_fp_element->unpaired->length)) {
										g_memcpy (constraint_tp_buff[num_constraint_tp],
										          &seq[wrapper_next_fp_posn - 1 - this_fp_element->unpaired->dist -
										                                    this_fp_element->unpaired->length],
										          constraint_tp_lengths[num_constraint_tp]);
										num_constraint_tp++;
										have_constraint_tp = true;
									}
								}
							}
							
							else
								if (constraint->type == base_triple &&
								    this_fp_element->unpaired->i_constraint.element_type ==
								    constraint_single_element) {
									// store in constraint single_buff - assume all single lengths are 1
									if (this_fp_posn) {
										g_memcpy (constraint_single_buff[num_constraint_single],
										          &seq[this_fp_posn - 1 + this_stack_len + this_fp_element->unpaired->dist],
										          1);
										single_nt_posn = (nt_rel_seq_posn) (this_fp_posn - 1 + this_stack_len +
										                                    this_fp_element->unpaired->dist);
										num_constraint_single++;
										have_constraint_single = true;
									}
									
									else {
										// for wrapper bp scenario, check that calculated sequence idx >= 0
										if (0 <= (wrapper_next_fp_posn - 1 - this_fp_element->unpaired->dist -
										          this_fp_element->unpaired->length)) {
											// along with fp_nt_posn track single_nt_posn
											single_nt_posn = (nt_rel_seq_posn) (wrapper_next_fp_posn - 1 -
											                                    this_fp_element->unpaired->dist - this_fp_element->unpaired->length);
											g_memcpy (constraint_single_buff[num_constraint_single],
											          &seq[single_nt_posn],
											          1);
											num_constraint_single++;
											have_constraint_single = true;
										}
									}
								}
					}
					
					this_fp_element = this_fp_element->unpaired->next;
				}
				while (this_fp_element);
			}
			
			if (this_tp_element) {
				do {
					if (this_tp_element->unpaired->i_constraint.reference == constraint) {
						if ((constraint->type == pseudoknot &&
						     this_tp_element->unpaired->i_constraint.element_type == constraint_fp_element)
						    ||
						    (constraint->type == base_triple &&
						     this_tp_element->unpaired->i_constraint.element_type ==
						     constraint_fp_element)) {
							// store in constraint fp_buff
							constraint_fp_lengths[num_constraint_fp] = this_tp_element->unpaired->length;
							g_memcpy (constraint_fp_buff[num_constraint_fp],
							          &seq[this_tp_posn - 1 + this_stack_len + this_tp_element->unpaired->dist],
							          constraint_fp_lengths[num_constraint_fp]);
							// track fp_nt_position and single_nt_position to determine orientation of base triple, when applicable
							fp_nt_posn = (nt_rel_seq_posn) (this_tp_posn - 1 + this_stack_len +
							                                this_tp_element->unpaired->dist);
							// mark this tp element
							num_constraint_fp++;
							have_constraint_fp = true;
						}
						
						else
							if ((constraint->type == pseudoknot &&
							     this_tp_element->unpaired->i_constraint.element_type == constraint_tp_element)
							    ||
							    (constraint->type == base_triple &&
							     this_tp_element->unpaired->i_constraint.element_type ==
							     constraint_tp_element)) {
								// store in constraint tp_buff
								constraint_tp_lengths[num_constraint_tp] = this_tp_element->unpaired->length;
								g_memcpy (constraint_tp_buff[num_constraint_tp],
								          &seq[this_tp_posn - 1 + this_stack_len + this_tp_element->unpaired->dist],
								          constraint_tp_lengths[num_constraint_tp]);
								// mark this tp element
								num_constraint_tp++;
								have_constraint_tp = true;
							}
							
							else
								if (constraint->type == base_triple &&
								    this_tp_element->unpaired->i_constraint.element_type ==
								    constraint_single_element) {
									// store in constraint single_buff - assume lengths are 1
									g_memcpy (constraint_single_buff[num_constraint_single],
									          &seq[this_tp_posn - 1 + this_stack_len + this_tp_element->unpaired->dist],
									          1);
									single_nt_posn = (nt_rel_seq_posn) (this_tp_posn - 1 + this_stack_len +
									                                    this_tp_element->unpaired->dist);
									// mark this single element
									num_constraint_single++;
									have_constraint_single = true;
								}
					}
					
					this_tp_element = this_tp_element->unpaired->next;
				}
				while (this_tp_element);
			}
			
			if (constraint->type == pseudoknot && have_constraint_fp &&
			    have_constraint_tp) {
				for (REGISTER nt_hit_count f = 0; f < num_constraint_fp; f++) {
					for (REGISTER nt_hit_count t = 0; t < num_constraint_tp; t++) {
						if (constraint_fp_lengths[f] == constraint_tp_lengths[t]) {
							REGISTER
							bool satisfied = true;
							
							for (REGISTER nt_hit_count i = 0; i < constraint_fp_lengths[f]; i++) {
								fp_nt = constraint_fp_buff[f][i];
								tp_nt = constraint_tp_buff[t][constraint_fp_lengths[f] - i - 1];
								
								if (! ((fp_nt == 'g' && (tp_nt == 'c' || tp_nt == 'u')) ||
								       (fp_nt == 'a' && tp_nt == 'u') ||
								       (tp_nt == 'g' && (fp_nt == 'c' || fp_nt == 'u')) ||
								       (tp_nt == 'a' && fp_nt == 'u'))) {
									satisfied = false;
									break;
								}
							}
							
							if (satisfied) {
								if (!constraint->next) {
									safe_copy_linked_bp (satisfied_list, this_linked_bp, next_linked_bp);
								}
								
								else {
									ntp_linked_bp next_constraint_next_linked_bp = NULL;
									// restart satisfiability test, for next constraint
									return satisfy_constraints (seq, constraint->next, satisfied_list,
									                            found_linked_bp, found_linked_bp, &next_constraint_next_linked_bp);
								}
							}
						}
					}
				}
				
				return true;
			}
			
			else
				if (constraint->type == base_triple && have_constraint_fp &&
				    have_constraint_tp && have_constraint_single) {
					for (REGISTER nt_hit_count f = 0; f < num_constraint_fp; f++) {
						for (REGISTER nt_hit_count t = 0; t < num_constraint_tp; t++) {
							for (REGISTER nt_hit_count s = 0; s < num_constraint_single; s++) {
								if (fp_nt_posn < single_nt_posn) {
									fp_nt = constraint_fp_buff[f][0];
									tp_nt = constraint_tp_buff[t][0];
								}
								
								else {
									// when single_nt is located before fp_nt, then swap fp_nt and tp_nt for the purpose of checking bt constraint
									tp_nt = constraint_fp_buff[f][0];
									fp_nt = constraint_tp_buff[t][0];
								}
								
								single_nt = constraint_single_buff[s][0];
								
								if (! (((fp_nt == 'c' || fp_nt == 'u') && tp_nt == 'g' && single_nt == 'c') ||
								       (fp_nt == 'u' && tp_nt == 'a' && single_nt == 'u'))) {
									break;
								}
								
								if (!constraint->next) {
									safe_copy_linked_bp (satisfied_list, this_linked_bp, next_linked_bp);
								}
								
								else {
									ntp_linked_bp next_constraint_next_linked_bp = NULL;
									// restart satisfiability test, for next constraint
									return satisfy_constraints (seq, constraint->next, satisfied_list,
									                            found_linked_bp, found_linked_bp, &next_constraint_next_linked_bp);
								}
							}
						}
					}
					
					return true;
				}
		}
		
		linked_bp = linked_bp->prev_linked_bp;
	}
	while (linked_bp);
	
	return true;
}

/*
 * private function to advance from the current_list of base-pairs
 * to an updated_list, based on any compatible base-pairs found in
 * a filter_list
 *
 * input:   non-empty current_list of nt_linked_bp
 *          non-empty filter_list of nt_bp
 *          current_stack_len of nt_bp in current_list
 *          skip_count in between current_ and filter_list
 *          track_id for the updated_list of nt_linked_bp
 *
 * output:  updated list of nt_linked_bp
 *
 * notes:   - linked_bps in current_list and bps in filter_list are
 *            iteratively traversed (only once) and compared, such that
 *            bps in filter_list meeting the following criterion are
 *            appended to the updated_list:
 *              5' of filter nt_bp == 5' of current nt_linked_bp +
 *                                 current_stack_len +
 *                                 skip_count
 *          - the comparison procedure is terminated when either
 *            current_list or filter_list is exhausted
 *          - both current_list and filter_list are unmodified, but
 *            any previous iteration sessions are disrupted
 *          - the updated_list contains back pointers to the previous
 *            current_list linked_bps
 *          - track_id stamps all linked_bps in updated_list with a
 *            common tracking identifier
 */
static inline
bool list_advance (const nt_list *restrict current_list,
                   const nt_stack_size current_stack_len,
                   const nt_rel_count match_count,
                   const nt_rel_count skip_count,
                   const nt_list *restrict filter_list,
                   ntp_list *restrict updated_list,
                   const nt_stack_size filter_stack_len,
                   const uchar track_id,
                   const char advanced_pair_track_id,
                   const char containing_pair_track_id) {
	if (!current_list || !filter_list ||
	    !current_list->numels || !filter_list->numels) {
		#ifdef DEBUG_ON
	    
		if (current_list && filter_list) {
			COMMIT_DEBUG (REPORT_WARNINGS, LIST,
			              "no elements found in current_list or filter_list in list_advance", false);
		}
		
		#endif
		return true;
	}
	
	// iterate over current_list and filter_list starting from posn 0 respectively
	REGISTER
	nt_list_posn current_list_size = current_list->numels, current_list_posn = 0,
	             filter_list_size = filter_list->numels, filter_list_posn = 0;
	REGISTER
	ntp_linked_bp restrict current_linked_bp = (ntp_linked_bp)list_get_at (
	                                        current_list, 0);
	#ifndef NO_FULL_CHECKS
	                                        
	if (!current_linked_bp) {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "could not iterate current_list in list_advance", false);
		return false;
	}
	
	#endif
	REGISTER
	ntp_linked_bp restrict root_linked_bp = get_linked_bp_root (current_linked_bp,
	                                        advanced_pair_track_id);
	#ifndef NO_FULL_CHECKS
	                                        
	if (!root_linked_bp) {
		COMMIT_DEBUG1 (REPORT_ERRORS, LIST,
		               "advanced_pair_track_id %d not found for root_linked_bp in list_advance",
		               advanced_pair_track_id, false);
		return false;
	}
	
	#endif
	REGISTER
	ulong updated_list_posn = 0;
	
	if (!root_linked_bp->bp->fp_posn) {
		/*
		 * current list is the wrapper list; advance all available elements in filter list
		 */
		REGISTER
		ntp_bp restrict filter_bp;
		
		do {
			filter_list_posn = 0;
			
			do {
				filter_bp = (ntp_bp)list_get_at (filter_list, filter_list_posn++);
				
				if (!ntp_list_linked_bp_seeker (*updated_list, filter_bp,
				                                current_linked_bp)) {     // not already enlisted?
					REGISTER
					ntp_linked_bp restrict linked_bp = MALLOC_TAG (sizeof (nt_linked_bp), track_id);
					#ifndef NO_FULL_CHECKS
					REGISTER
					bool success = false;
					
					if (linked_bp) {
					#endif
						linked_bp->bp = filter_bp;
						linked_bp->stack_len = filter_stack_len;
						linked_bp->track_id = track_id;
						linked_bp->fp_elements = NULL;
						linked_bp->tp_elements = NULL;
						linked_bp->prev_linked_bp = current_linked_bp;
						REGISTER
						ntp_linked_bp last_linked_bp = list_get_at (*updated_list, updated_list_posn);
						
						while (last_linked_bp) {
							if (last_linked_bp->bp->fp_posn > filter_bp->fp_posn ||
							    (last_linked_bp->bp->fp_posn == filter_bp->fp_posn &&
							     last_linked_bp->bp->tp_posn > filter_bp->tp_posn)) {
								break;
							}
							
							updated_list_posn++;
							last_linked_bp = list_get_at (*updated_list, updated_list_posn);
						}
						
						#ifndef NO_FULL_CHECKS
						
						if (
						#endif
						                    list_insert_at (*updated_list, linked_bp, updated_list_posn)
					                    #ifndef NO_FULL_CHECKS
						                    >= 0) {
							success = true;
						}
					}
					
					                    #else
						                    ;
					                    #endif
					updated_list_posn++;
					#ifndef NO_FULL_CHECKS
					
					if (!success) {
						COMMIT_DEBUG (REPORT_ERRORS, LIST,
						              "could not allocate bp/append to updated_list in list_advance", false);
						              
						if (!list_iterator_start (updated_list)) {
							COMMIT_DEBUG (REPORT_ERRORS, LIST,
							              "could not release memory for updated_list in list_advance", false);
						}
						
						while (list_iterator_hasnext (updated_list)) {
							FREE_TAG (list_iterator_next (updated_list), track_id);
						}
						
						list_iterator_stop (updated_list);
						return false;
					}
					
					#endif
				}
			}
			while (filter_list_posn < filter_list_size);
			
			current_list_posn++;
			
			if (current_list_posn < current_list_size) {
				current_linked_bp = (ntp_linked_bp)list_get_at (current_list,
				                                        current_list_posn);
				#ifndef NO_FULL_CHECKS
				                                        
				if (!current_linked_bp) {
					COMMIT_DEBUG (REPORT_ERRORS, LIST,
					              "could not iterate current_list in list_advance", false);
					return false;
				}
				
				#endif
			}
			
			else {
				break;
			}
		}
		while (true);
		
		return true;
	}
	
	if (match_count && containing_pair_track_id != advanced_pair_track_id) {
		do {
			if (root_linked_bp->bp->tp_posn - root_linked_bp->bp->fp_posn == match_count) {
				break;
			}
			
			else {
				if (current_list_posn < (current_list_size - 1)) {
					current_list_posn++;
					current_linked_bp = (ntp_linked_bp) list_get_at (current_list,
					                                        current_list_posn);
					#ifndef NO_FULL_CHECKS
					                                        
					if (!current_linked_bp) {
						COMMIT_DEBUG (REPORT_ERRORS, LIST,
						              "could not iterate current_list in list_advance", false);
						return false;
					}
					
					#endif
					root_linked_bp = get_linked_bp_root (current_linked_bp, advanced_pair_track_id);
					#ifndef NO_FULL_CHECKS
					
					if (!root_linked_bp) {
						COMMIT_DEBUG1 (REPORT_ERRORS, LIST,
						               "advanced_pair_track_id %d not found for root_linked_bp in list_advance",
						               advanced_pair_track_id, false);
						return false;
					}
					
					#endif
				}
				
				else {
					return true;
				}
			}
		}
		while (1);
	}
	
	filter_list_posn = 0;
	REGISTER
	ntp_bp restrict filter_bp = (ntp_bp)list_get_at (filter_list, 0);
	#ifndef NO_FULL_CHECKS
	
	if (!filter_bp) {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "could not iterate filter_list in list_advance", false);
		return false;
	}
	
	else {
		COMMIT_DEBUG1 (REPORT_INFO, LIST,
		               "filtering current_list with filter_list on advanced_pair_track_id %d in list_advance",
		               advanced_pair_track_id, true);
	}
	
	#endif
	REGISTER
	nt_rel_seq_posn filter_fp_posn = filter_bp->fp_posn,
	                root_fp_posn = root_linked_bp->bp->fp_posn + current_stack_len + match_count +
	                               skip_count;
	                               
	do {
		if (filter_fp_posn < root_fp_posn) {
			do {
				filter_list_posn++;
				filter_bp = (ntp_bp)list_get_at (filter_list, filter_list_posn);
			}
			while (filter_bp && filter_bp->fp_posn < root_fp_posn);
			
			if (filter_bp == NULL) {
				return true;    // no more on filter_list -> finish
			}
			
			else {
				filter_fp_posn = filter_bp->fp_posn;
			}
		}
		
		else
			if (filter_fp_posn > root_fp_posn) {
				// advance current_list_posn
				while (1) {
					if (current_list_posn < (current_list_size - 1)) {
						current_list_posn++;
						current_linked_bp = (ntp_linked_bp) list_get_at (current_list,
						                                        current_list_posn);
						#ifndef NO_FULL_CHECKS
						                                        
						if (!current_linked_bp) {
							COMMIT_DEBUG (REPORT_ERRORS, LIST, "NULL current_linked_bp in list_advance",
							              false);
							return false;
						}
						
						#endif
						root_linked_bp = get_linked_bp_root (current_linked_bp, advanced_pair_track_id);
						#ifndef NO_FULL_CHECKS
						
						if (!root_linked_bp) {
							COMMIT_DEBUG1 (REPORT_ERRORS, LIST,
							               "advanced_pair_track_id %d not found for root_linked_bp in list_advance",
							               advanced_pair_track_id, false);
							return false;
						}
						
						#endif
						
						if (match_count && containing_pair_track_id != advanced_pair_track_id) {
							while (root_linked_bp->bp->tp_posn - root_linked_bp->bp->fp_posn !=
							       match_count) {
								if (current_list_posn < (current_list_size - 1)) {
									current_list_posn++;
									current_linked_bp = (ntp_linked_bp) list_get_at (current_list,
									                                        current_list_posn);
									#ifndef NO_FULL_CHECKS
									                                        
									if (!current_linked_bp) {
										COMMIT_DEBUG (REPORT_ERRORS, LIST, "NULL current_linked_bp in list_advance",
										              false);
										return false;
									}
									
									#endif
									root_linked_bp = get_linked_bp_root (current_linked_bp, advanced_pair_track_id);
									#ifndef NO_FULL_CHECKS
									
									if (!root_linked_bp) {
										COMMIT_DEBUG1 (REPORT_ERRORS, LIST,
										               "advanced_pair_track_id %d not found for root_linked_bp in list_advance",
										               advanced_pair_track_id, false);
										return false;
									}
									
									#endif
								}
								
								else {
									return true;
								}
							}
						}
						
						root_fp_posn = root_linked_bp->bp->fp_posn + current_stack_len + match_count +
						               skip_count;
						               
						if (filter_fp_posn <= root_fp_posn) {
							break;
						}
					}
					
					else {
						return true; // no more on current_list -> finish
					}
				}
			}
			
		if (filter_fp_posn == root_fp_posn) {
			const REGISTER
			nt_list_posn backup_filter_list_posn = filter_list_posn;
			
			do {
				// outer loop: iterate once over all matching current_list bps
				do {
					// inner loop: iterate over all matching filter_list bps for every current_list nt_bp
					if ((filter_bp->fp_posn >= root_linked_bp->bp->tp_posn + current_stack_len) ||
					    // non-overlapping
					    (filter_bp->tp_posn <= root_linked_bp->bp->tp_posn -
					     filter_stack_len)) {              // or properly nested?
						if (!ntp_list_linked_bp_seeker (*updated_list, filter_bp,
						                                current_linked_bp)) {     // not already enlisted?
							REGISTER
							ntp_linked_bp restrict linked_bp = MALLOC_TAG (sizeof (nt_linked_bp), track_id);
							#ifndef NO_FULL_CHECKS
							REGISTER
							bool success = false;
							
							if (linked_bp) {
							#endif
								linked_bp->bp = filter_bp;
								linked_bp->stack_len = filter_stack_len;
								linked_bp->track_id = track_id;
								linked_bp->fp_elements = NULL;
								linked_bp->tp_elements = NULL;
								linked_bp->prev_linked_bp = current_linked_bp;
								ntp_linked_bp last_linked_bp = list_get_at (*updated_list, updated_list_posn);
								
								while (last_linked_bp) {
									if (last_linked_bp->bp->fp_posn > filter_bp->fp_posn ||
									    (last_linked_bp->bp->fp_posn == filter_bp->fp_posn &&
									     last_linked_bp->bp->tp_posn > filter_bp->tp_posn)) {
										break;
									}
									
									updated_list_posn++;
									last_linked_bp = list_get_at (*updated_list, updated_list_posn);
								}
								
								#ifndef NO_FULL_CHECKS
								
								if (
								#endif
								                    list_insert_at (*updated_list, linked_bp, updated_list_posn)
							                    #ifndef NO_FULL_CHECKS
								                    >= 0) {
									success = true;
								}
							}
							
							                    #else
								                    ;
							                    #endif
							updated_list_posn++;
							#ifndef NO_FULL_CHECKS
							
							if (!success) {
								COMMIT_DEBUG (REPORT_ERRORS, LIST,
								              "could not allocate bp/append to updated_list in list_advance", false);
								              
								if (!list_iterator_start (updated_list)) {
									COMMIT_DEBUG (REPORT_ERRORS, LIST,
									              "could not release memory for updated_list in list_advance", false);
								}
								
								while (list_iterator_hasnext (updated_list)) {
									FREE_TAG (list_iterator_next (updated_list), track_id);
								}
								
								list_iterator_stop (updated_list);
								return false;
							}
							
							#endif
						}
					}
					
					else {
						// filter_bp->fp_posn occurs before current_linked_bp->bp_tp(-current_stack_len) and
						// filter_bp->tp_posn occurs after current_linked_bp->bp->tp_posn(-filter_stack_len),
						// so no further filter_bps possible for this iteration of current_linked_bp
						break;
					}
					
					// progress through filter_list for the current_list_posn
					filter_list_posn++;
					
					if (filter_list_posn < filter_list_size) {
						filter_bp = (ntp_bp) list_get_at (filter_list, filter_list_posn);
						#ifndef NO_FULL_CHECKS
						
						if (!filter_bp) {
							COMMIT_DEBUG (REPORT_ERRORS, LIST, "NULL filter_bp in list_advance", false);
							return false;        // free updated_list here?
						}
						
						#endif
						
						if (filter_bp->fp_posn == filter_fp_posn) {
							continue;
						}
					}
					
					break;  // finished this filter_list iteration
				}
				while (1);
				
				// reset filter_list_posn and progress current_list_posn
				filter_list_posn = backup_filter_list_posn;
				filter_bp = (ntp_bp) list_get_at (filter_list, filter_list_posn);
				current_list_posn++;
				
				if (current_list_posn < current_list_size) {
					current_linked_bp = (ntp_linked_bp) list_get_at (current_list,
					                                        current_list_posn);
					#ifndef NO_FULL_CHECKS
					                                        
					if (!current_linked_bp) {
						COMMIT_DEBUG (REPORT_ERRORS, LIST, "NULL current_linked_bp in list_advance",
						              false);
						return false;        // free updated_list here?
					}
					
					#endif
					root_linked_bp = get_linked_bp_root (current_linked_bp, advanced_pair_track_id);
					#ifndef NO_FULL_CHECKS
					
					if (!root_linked_bp) {
						COMMIT_DEBUG1 (REPORT_ERRORS, LIST,
						               "advanced_pair_track_id %d not found for root_linked_bp in list_advance",
						               advanced_pair_track_id, false);
						return false;
					}
					
					#endif
					
					if (match_count && containing_pair_track_id != advanced_pair_track_id) {
						while (root_linked_bp->bp->tp_posn - root_linked_bp->bp->fp_posn !=
						       match_count) {
							if (current_list_posn < (current_list_size - 1)) {
								current_list_posn++;
								current_linked_bp = (ntp_linked_bp) list_get_at (current_list,
								                                        current_list_posn);
								#ifndef NO_FULL_CHECKS
								                                        
								if (!current_linked_bp) {
									COMMIT_DEBUG (REPORT_ERRORS, LIST, "NULL current_linked_bp in list_advance",
									              false);
									return false;        // free updated_list here?
								}
								
								#endif
								root_linked_bp = get_linked_bp_root (current_linked_bp, advanced_pair_track_id);
								#ifndef NO_FULL_CHECKS
								
								if (!root_linked_bp) {
									COMMIT_DEBUG1 (REPORT_ERRORS, LIST,
									               "advanced_pair_track_id %d not found for root_linked_bp in list_advance",
									               advanced_pair_track_id, false);
									return false;
								}
								
								#endif
							}
							
							else {
								return true;
							}
						}
					}
					
					if (root_linked_bp->bp->fp_posn + current_stack_len + match_count + skip_count
					    == root_fp_posn) {
						continue;
					}
					
					else {
						filter_fp_posn = filter_bp->fp_posn;
						root_fp_posn = root_linked_bp->bp->fp_posn + current_stack_len + match_count +
						               skip_count;
						break;
					}
				}
				
				return true;  // no more on current_list -> finish
			}
			while (1);
		}
	}
	while (1);
}

static inline
ntp_list list_null_advance (nt_list *restrict current_list,
                            const uchar track_id) {
	#ifndef NO_FULL_CHECKS
                            
	if (!*current_list || !*current_list->numels) {
		COMMIT_DEBUG (REPORT_WARNINGS, LIST,
		              "no elements found in current_list in list_advance_null", false);
		              
		if (*current_list) {
			list_destroy (*current_list);
			FREE_DEBUG (*current_list, "current_list in list_null_advance");
			*current_list = NULL;
		}
		
		return NULL;
	}
	
	#endif
	REGISTER
	ntp_list restrict updated_list = MALLOC_DEBUG (sizeof (nt_list),
	                                        "updated_list in list_null_advance");
	#ifndef NO_FULL_CHECKS
	                                        
	if ((!updated_list) || (
	#endif
	                        list_init (updated_list)
                        #ifndef NO_FULL_CHECKS
	                        < 0)) {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "could not allocate memory for/initialize updated_list in list_null_advance",
		              false);
		              
		if (updated_list) {
			FREE_DEBUG (updated_list, "updated_list in list_null_advance");
		}
		
		return NULL;
	}
	
                        #else
	                        ;
                        #endif
	// iterate over current_list and filter_list starting from posn 0 respectively
	REGISTER
	nt_list_posn current_list_size = current_list->numels, current_list_posn = 0;
	
	while (current_list_posn < current_list_size) {
		REGISTER
		ntp_linked_bp restrict current_linked_bp = (ntp_linked_bp) list_get_at (
		                                        current_list, current_list_posn);
		#ifndef NO_FULL_CHECKS
		                                        
		if (!current_linked_bp) {
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "could not iterate current_list in list_advance_null", false);
			list_destroy (updated_list);
			FREE_DEBUG (updated_list, "updated_list in list_null_advance");
			return NULL;
		}
		
		#endif
		REGISTER
		ntp_linked_bp restrict linked_bp = MALLOC_TAG (sizeof (nt_linked_bp), track_id);
		#ifndef NO_FULL_CHECKS
		REGISTER
		bool success = false;
		
		if (linked_bp) {
		#endif
			linked_bp->bp = current_linked_bp->bp;
			linked_bp->stack_len = NULL_STACK_LEN;
			linked_bp->track_id = track_id;
			linked_bp->fp_elements = NULL;
			linked_bp->tp_elements = NULL;
			linked_bp->prev_linked_bp = current_linked_bp;
			#ifndef NO_FULL_CHECKS
			
			if (
			#endif
			                    list_append (updated_list, linked_bp)
		                    #ifndef NO_FULL_CHECKS
			                    >= 0) {
				success = true;
			}
		}
		
		                    #else
			                    ;
		                    #endif
		#ifndef NO_FULL_CHECKS
		
		if (!success) {
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "could not allocate bp/append to updated_list in list_null_advance", false);
			              
			if (!list_iterator_start (updated_list)) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "could not release memory for updated_list in list_null_advance", false);
			}
			
			while (list_iterator_hasnext (updated_list)) {
				FREE_TAG (list_iterator_next (updated_list), track_id);
			}
			
			list_iterator_stop (updated_list);
			list_destroy (updated_list);
			FREE_DEBUG (updated_list, "updated_list in list_null_advance");
			return NULL;
		}
		
		#endif
		current_list_posn++;
	}
	
	return updated_list;
}

static inline
bool list_prune (ntp_list restrict *current_list,
                 const nt_stack_size current_stack_len,
                 const nt_rel_count skip_cnt,
                 const nt_rel_count unpaired_cnt,
                 const char advanced_pair_track_id,
                 const char containing_pair_track_id,
                 const uchar track_id) {
	COMMIT_DEBUG1 (REPORT_INFO, LIST,
	               "pruning current_list with track_id %d in list_prune", track_id, true);
	#ifndef NO_FULL_CHECKS
	               
	if (!*current_list || (! (*current_list)->numels)) {
		#ifdef DEBUG_ON
	
		if (*current_list) {
			COMMIT_DEBUG (REPORT_WARNINGS, LIST,
			              "no elements found in current_list in list_prune", false);
			list_destroy (*current_list);
			FREE_DEBUG (*current_list, "current_list in list_prune");
			*current_list = NULL;
		}
		
		else {
			COMMIT_DEBUG (REPORT_WARNINGS, LIST, "current_list is NULL in list_prune",
			              false);
		}
		
		#endif
		return true;
	}
	
	if (advanced_pair_track_id < 0) {
		COMMIT_DEBUG (REPORT_WARNINGS, LIST,
		              "negative advanced_pair_track_id in list_prune", false);
		return true;
	}
	
	if (containing_pair_track_id < 0) {
		COMMIT_DEBUG (REPORT_WARNINGS, LIST,
		              "negative containing_pair_track_id in list_prune", false);
		return true;
	}
	
	if (!
	#endif
	    list_iterator_start (*current_list)
    #ifndef NO_FULL_CHECKS
	   ) {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "could not iterate current_list in list_prune", false);
		list_destroy (*current_list);
		FREE_DEBUG (*current_list, "current_list in list_prune");
		*current_list = NULL;
		return false;
	}
	
    #else
	    ;
    #endif
	REGISTER
	ntp_list restrict updated_list = MALLOC_DEBUG (sizeof (nt_list),
	                                        "updated_list in list_prune");
	#ifndef NO_FULL_CHECKS
	                                        
	if (!updated_list || (
	#endif
	                        list_init (updated_list)
                        #ifndef NO_FULL_CHECKS
	                        < 0)) {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "could not allocate memory/initialize updated_list in list_prune", false);
		list_iterator_stop (*current_list);
		
		if (updated_list) {
			list_destroy (
			                    updated_list);   // assumes bp's are allocated by tag, so can just destroy
			FREE_DEBUG (updated_list, "updated_list in list_prune");
		}
		
		list_destroy (*current_list);
		FREE_DEBUG (*current_list, "current_list in list_prune");
		*current_list = NULL;
		return false;
	}
	
                        #else
	                        ;
                        #endif
	REGISTER
	ntp_linked_bp restrict this_linked_bp, orig_linked_bp, next_linked_bp = NULL;
	
	while (list_iterator_hasnext (*current_list)) {
		this_linked_bp = (ntp_linked_bp)list_iterator_next (*current_list);
		orig_linked_bp = this_linked_bp;
		REGISTER
		nt_rel_seq_posn advanced_pair_tp = 0, advanced_pair_fp = 0,
		                containing_pair_tp = 0;
		                
		while (this_linked_bp) {
			if (!advanced_pair_tp && this_linked_bp->track_id == advanced_pair_track_id) {
				advanced_pair_fp = this_linked_bp->bp->fp_posn;
				advanced_pair_tp = this_linked_bp->bp->tp_posn;
			}
			
			if (!containing_pair_tp &&
			    this_linked_bp->track_id == containing_pair_track_id) {
				containing_pair_tp = this_linked_bp->bp->tp_posn;
				
				if (advanced_pair_tp) {
					break;
				}
			}
			
			this_linked_bp = this_linked_bp->prev_linked_bp;
		}
		
		if (!containing_pair_tp) {
			// presume containing pair is wrapper pair, so no need to prune
			list_iterator_stop (*current_list);
			list_destroy (
			                    updated_list);   // assumes bp's are allocated by tag, so can just destroy
			FREE_DEBUG (updated_list, "updated_list in list_prune");
			return true;
		}
		
		#ifndef NO_FULL_CHECKS
		
		if (!advanced_pair_tp) {
			COMMIT_DEBUG1 (REPORT_ERRORS, LIST,
			               "advanced_pair_track_id %d not found in list_prune", advanced_pair_track_id,
			               false);
			break;  // error
		}
		
		#endif
		
		/*
		 * we have one of two cases:
		 *
		 * 1) containing_pair_track_id == advanced_pair_track_id:
		 *    (advanced_pair_tp-(advanced_pair_fp+current_stack_len)) must be equal to (skip_cnt+unpaired_cnt)
		 *
		 *    or
		 *
		 * 2) containing_pair_track_id != advanced_pair_track_id:
		 *    (advanced_pair_tp+current_stack_len+unpaired_cnt) == containing_pair_tp)
		 *
		 * and for either case, duplicate entries are not allowed
		 */
		
		if ((((containing_pair_track_id == advanced_pair_track_id) &&
		      (advanced_pair_tp - (advanced_pair_fp + current_stack_len) == skip_cnt +
		       unpaired_cnt)) ||
		     ((containing_pair_track_id != advanced_pair_track_id) &&
		      (containing_pair_tp == advanced_pair_tp + current_stack_len + skip_cnt +
		       unpaired_cnt))) &&
		    (!ntp_list_linked_bp_seeker_with_elements (updated_list,
		                                            orig_linked_bp))) {    // no double entries
			REGISTER
			ntp_linked_bp restrict linked_bp = MALLOC_TAG (sizeof (nt_linked_bp), track_id);
			#ifndef NO_FULL_CHECKS
			REGISTER
			bool success = false;
			
			if (linked_bp) {
			#endif
				linked_bp->bp = orig_linked_bp->bp;
				linked_bp->stack_len = orig_linked_bp->stack_len;
				linked_bp->track_id = orig_linked_bp->track_id;
				linked_bp->fp_elements = NULL;
				linked_bp->tp_elements = NULL;
				linked_bp->prev_linked_bp = orig_linked_bp->prev_linked_bp;
				#ifndef NO_FULL_CHECKS
				
				if (
				#endif
				                    list_append (updated_list, linked_bp)
			                    #ifndef NO_FULL_CHECKS
				                    >= 0) {
			                    #else
				                    ;
			                    #endif
				                    
					if (orig_linked_bp->fp_elements || orig_linked_bp->tp_elements) {
						copy_linked_bp_elements (linked_bp, next_linked_bp, orig_linked_bp->fp_elements,
						                         orig_linked_bp->tp_elements,
						                         true, linked_bp->track_id);
					}
					
					#ifndef NO_FULL_CHECKS
					success = true;
				}
			}
			
					#endif
			#ifndef NO_FULL_CHECKS
			
			if (!success) {
				list_iterator_stop (*current_list);
				
				if (!list_iterator_start (updated_list)) {
					COMMIT_DEBUG (REPORT_ERRORS, LIST,
					              "could not release memory for updated_list in list_prune", false);
				}
				
				while (list_iterator_hasnext (updated_list)) {
					FREE_TAG (list_iterator_next (updated_list), track_id);
				}
				
				list_iterator_stop (updated_list);
				list_destroy (updated_list);
				FREE_DEBUG (updated_list, "updated_list in list_prune");
				list_destroy (*current_list);
				FREE_DEBUG (*current_list, "current_list in list_prune");
				*current_list = NULL;
				return false;
			}
			
			#endif
		}
		
		next_linked_bp = this_linked_bp;
	}
	
	list_iterator_stop (*current_list);
	
	// assumes bp's are allocated by tag, so can just destroy
	list_destroy (*current_list);
	FREE_DEBUG (*current_list, "current_list in list_prune");
	*current_list = updated_list;
	
	return true;
}

static inline ntp_list list_sum (ntp_list restrict l,
                                 const nt_rel_count summand, const uchar tag) {
	#ifndef NO_FULL_CHECKS
                                 
	if (l) {
	#endif
	
		if (l->numels == 0) {
			COMMIT_DEBUG (REPORT_INFO, LIST, "setting an initial list_summand in list_sum",
			              false);
			REGISTER nt_rel_count *list_summand = MALLOC_TAG (sizeof (nt_rel_count), tag);
			#ifndef NO_FULL_CHECKS
			
			if (!list_summand) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "cannot allocate memory for list_summand in list_sum", false);
				return NULL;
			}
			
			#endif
			*list_summand = summand;
			#ifndef NO_FULL_CHECKS
			
			if (
			#endif
			                    list_append (l, list_summand)
		                    #ifndef NO_FULL_CHECKS
			                    != 1) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "cannot append initial list_summand to l in list_sum", false);
				FREE_TAG (list_summand, tag);
				return NULL;
			}
			
		                    #else
			                    ;
		                    #endif
			return l;
		}
		
		#ifndef NO_FULL_CHECKS
		
		if (
		#endif
		                    list_iterator_start (l)
	                    #ifndef NO_FULL_CHECKS
		                    != 0) {
	                    #else
		                    ;
	                    #endif
		                    
			while (list_iterator_hasnext (l)) {
				* (nt_rel_count *)list_iterator_next (l) += summand;
			}
			
			#ifndef NO_FULL_CHECKS
		}
		
		else {
			COMMIT_DEBUG (REPORT_ERRORS, LIST, "ntp_list l cannot be iterated in list_sum",
			              false);
			return NULL;
		}
		
			#endif
		list_iterator_stop (l);
		return l;
		#ifndef NO_FULL_CHECKS
	}
	
	else {
		COMMIT_DEBUG (REPORT_ERRORS, LIST, "ntp_list l is NULL in list_sum", false);
		return NULL;
	}
	
		#endif
}

static inline ntp_list list_sum_combine (ntp_list restrict l,
                                        ntp_list restrict s, const uchar tag) {
	if (s == NULL) {
		return l;
	}
	
	REGISTER size_t s_size = s->numels;
	
	switch (s_size) {
		case 0:
			return l;
			
		case 1:
			return list_sum (l, * (nt_rel_count *)list_get_at (s, 0), tag);
			
		default:
			break;
	}
	
	#ifndef NO_FULL_CHECKS
	
	if (l) {
	#endif
	
		if (l->numels == 0) {
			COMMIT_DEBUG (REPORT_INFO, LIST,
			              "setting an initial list_summand in list_sum_combine", false);
			REGISTER
			nt_rel_count *list_summand = MALLOC_TAG (sizeof (nt_rel_count), tag);
			#ifndef NO_FULL_CHECKS
			
			if (!list_summand) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "cannot allocate memory for list_summand in list_sum_combine", false);
				return NULL;
			}
			
			#endif
			*list_summand = 0;
			#ifndef NO_FULL_CHECKS
			
			if (
			#endif
			                    list_append (l, list_summand)
		                    #ifndef NO_FULL_CHECKS
			                    != 1) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "cannot append initial list_summand to l in list_sum_combine", false);
				FREE_TAG (list_summand, tag);
				return NULL;
			}
			
		                    #else
			                    ;
		                    #endif
		}
		
		ntp_list restrict combined_list = NULL;
		#ifndef NO_FULL_CHECKS
		
		if (!
		#endif
		    ntp_list_alloc_debug (&combined_list, "combined_list in list_sum_combine")
	    #ifndef NO_FULL_CHECKS
		   ) {
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "cannot alloc combined_list in list_sum_combine", false);
			return NULL;
		}
		
	    #else
		    ;
	    #endif
		#ifndef NO_FULL_CHECKS
		
		if (
		#endif
		                    list_iterator_start (l)
	                    #ifndef NO_FULL_CHECKS
		                    != 0) {
	                    #else
		                    ;
	                    #endif
		                    
			while (list_iterator_hasnext (l)) {
				const REGISTER nt_rel_count next_l = * (nt_rel_count *)list_iterator_next (l);
				#ifndef NO_FULL_CHECKS
				
				if (
				#endif
				                    list_iterator_start (s)
			                    #ifndef NO_FULL_CHECKS
				                    != 0) {
			                    #else
				                    ;
			                    #endif
				                    
					while (list_iterator_hasnext (s)) {
						REGISTER nt_rel_count *list_summand_combined = MALLOC_TAG (sizeof (
						                                        nt_rel_count), tag);
						#ifndef NO_FULL_CHECKS
						                                        
						if (!list_summand_combined) {
							COMMIT_DEBUG (REPORT_ERRORS, LIST,
							              "cannot allocate memory for list_summand_combined in list_sum_combine", false);
							list_iterator_stop (s);
							list_iterator_stop (l);
							
							if (combined_list) {
								list_destroy (combined_list);
								FREE_DEBUG (combined_list, "combined_list in list_sum_combine");
							}
							
							return NULL;
						}
						
						#endif
						*list_summand_combined = next_l + * (nt_rel_count *)list_iterator_next (s);
						#ifndef NO_FULL_CHECKS
						
						if (
						#endif
						                    list_append (combined_list, list_summand_combined)
					                    #ifndef NO_FULL_CHECKS
						                    != 1) {
							COMMIT_DEBUG (REPORT_ERRORS, LIST,
							              "cannot append list_summand_combined to combined_list in list_sum_combine",
							              false);
							FREE_TAG (list_summand_combined, tag);
							list_iterator_stop (s);
							list_iterator_stop (l);
							
							if (combined_list) {
								list_destroy (combined_list);
								FREE_DEBUG (combined_list, "combined_list in list_sum_combine");
							}
							
							return NULL;
						}
						
					                    #else
						                    ;
					                    #endif
					}
					
					list_iterator_stop (s);
					#ifndef NO_FULL_CHECKS
				}
				
				else {
					COMMIT_DEBUG (REPORT_ERRORS, LIST,
					              "ntp_list s cannot be iterated in list_sum_combine", false);
					list_iterator_stop (l);
					
					if (combined_list) {
						list_destroy (combined_list);
						FREE_DEBUG (combined_list, "combined_list in list_sum_combine");
					}
					
					return NULL;
				}
				
					#endif
			}
			
			list_iterator_stop (l);
			#ifndef NO_FULL_CHECKS
		}
		
		else {
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "ntp_list l cannot be iterated in list_sum_combine", false);
			              
			if (combined_list) {
				list_destroy (combined_list);
				FREE_DEBUG (combined_list, "combined_list in list_sum_combine");
			}
			
			return NULL;
		}
		
			#endif
		
		list_iterator_stop (l);
		
		return combined_list;
		
		#ifndef NO_FULL_CHECKS
	}
	else {
		COMMIT_DEBUG (REPORT_ERRORS, LIST, "ntp_list l is NULL in list_sum_combine",
		              false);
		return NULL;
	}
	
		#endif
}

static inline void free_constraint_links (nt_element *restrict el,
                                        ntp_list restrict *this_list, char advanced_pair_track_id) {
	#ifndef NO_FULL_CHECKS
                                        
	if (!el || !*this_list) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "el or *this_list is NULL in free_constraint_links", false);
		return;
	}
	
	if (el->type != unpaired) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "el is not of nt_element_type unpaired in free_constraint_links", false);
		return;
	}
	
	if (!el->unpaired->constraint) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "el does not have a cosntraint in free_constraint_links", false);
		return;
	}
	
	if (el->unpaired->prev_type == no_element_type) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "el has no prev_type in free_constraint_links", false);
		return;
	}
	
	#endif
	#ifndef NO_FULL_CHECKS
	
	if (
	#endif
	                    list_iterator_start (*this_list)
                    #ifndef NO_FULL_CHECKS
	) {
                    #else
	                    ;
                    #endif
		REGISTER
		bool is_fp_element = false;
		REGISTER
		ntp_element this_element = el;
		
		do {
			if (this_element->unpaired->prev_type == unpaired) {
				this_element = this_element->unpaired->prev_unpaired;
			}
			
			else
			#ifndef NO_FULL_CHECKS
				if (this_element->unpaired->prev_type == paired)
			#endif
				{
					if (this_element->unpaired->prev_branch_type == five_prime) {
						is_fp_element = true;
					}
					
					break;
				}
				
			#ifndef NO_FULL_CHECKS
				
				else {
					COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
					              "el has one of more prev_types that are neither paired nor unpaired in free_constraint_links",
					              false);
					return;
				}
				
			#endif
		}
		while (1);
		
		while (list_iterator_hasnext (*this_list)) {
			REGISTER
			ntp_linked_bp this_linked_bp = list_iterator_next (*this_list);
			
			while (this_linked_bp->track_id != advanced_pair_track_id &&
			       this_linked_bp->prev_linked_bp) {
				this_linked_bp = this_linked_bp->prev_linked_bp;
			}
			
			REGISTER
			ntp_element restrict element = NULL;
			
			if (is_fp_element) {
				element = this_linked_bp->fp_elements;
			}
			
			else {
				element = this_linked_bp->tp_elements;
			}
			
			if (element) {
				REGISTER
				bool found_el = false;
				
				do {
					if (element->unpaired->i_constraint.reference ==
					    el->unpaired->i_constraint.reference &&
					    element->unpaired->i_constraint.element_type ==
					    el->unpaired->i_constraint.element_type) {
						found_el = true;
					}
					
					element = element->unpaired->next;
				}
				while (!found_el && element);
				
				if (found_el) {
					if (is_fp_element) {
						this_linked_bp->fp_elements = element;
					}
					
					else {
						this_linked_bp->tp_elements = element;
					}
				}
			}
		}
		
		list_iterator_stop (*this_list);
		#ifndef NO_FULL_CHECKS
	}
	
		#endif
}

static inline void set_constraint_links (nt_element *restrict el,
                                        ntp_list restrict *this_list,
                                        const nt_rel_count unpaired_cnt,
                                        nt_rel_count skip_cnt,
                                        const uchar track_id,
                                        char advanced_pair_track_id,
                                        bool track_constraints) {
	#ifndef NO_FULL_CHECKS
                                        
	if (!el || !*this_list) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "el or *this_list is NULL in set_constraint_links", false);
		return;
	}
	
	if (el->type != unpaired) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "el is not of nt_element_type unpaired in set_constraint_links", false);
		return;
	}
	
	if (el->unpaired->prev_type == no_element_type) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "el has no prev_type in set_constraint_links", false);
		return;
	}
	
	#endif
	#ifndef NO_FULL_CHECKS
	
	if (
	#endif
	                    list_iterator_start (*this_list)
                    #ifndef NO_FULL_CHECKS
	) {
                    #else
	                    ;
                    #endif
		REGISTER
		bool is_fp_element = false;
		REGISTER
		ntp_element this_element = el;
		
		do {
			if (this_element->unpaired->prev_type == unpaired) {
				this_element = this_element->unpaired->prev_unpaired;
			}
			
			else
			#ifndef NO_FULL_CHECKS
				if (this_element->unpaired->prev_type == paired)
			#endif
				{
					if (this_element->unpaired->prev_branch_type == five_prime) {
						is_fp_element = true;
					}
					
					if (!this_element->unpaired->prev_paired->paired->max) {
						// in contrast to elements for regular bps, wrapper bp elements extend in reverse direction;
						// this is such that the last element added always has a distance (relative to first real bp) of 0
						// note: assumes that wrapper bps always have 0 min/max stack lengths
						skip_cnt = 0;
					}
					
					break;
				}
				
			#ifndef NO_FULL_CHECKS
				
				else {
					COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
					              "el has one of more prev_types that are neither paired nor unpaired in set_constraint_links",
					              false);
					return;
				}
				
			#endif
		}
		while (1);
		
		while (list_iterator_hasnext (*this_list)) {
			REGISTER
			ntp_linked_bp this_linked_bp = list_iterator_next (*this_list);
			
			while (this_linked_bp->track_id != advanced_pair_track_id &&
			       this_linked_bp->prev_linked_bp) {
				this_linked_bp = this_linked_bp->prev_linked_bp;
			}
			
			REGISTER
			ntp_element restrict element = NULL;
			
			if (is_fp_element) {
				element = this_linked_bp->fp_elements;
			}
			
			else {
				element = this_linked_bp->tp_elements;
			}
			
			if (element) {
				REGISTER
				bool already_linked = false;
				
				do {
					if (element->unpaired->i_constraint.reference ==
					    el->unpaired->i_constraint.reference &&
					    element->unpaired->i_constraint.element_type ==
					    el->unpaired->i_constraint.element_type) {
						if (element->unpaired->length == unpaired_cnt &&
						    element->unpaired->dist == skip_cnt) {
							already_linked = true;
							break;
						}
					}
					
					element = element->unpaired->next;
				}
				while (element);
				
				if (already_linked) {
					continue;
				}
			}
			
			REGISTER
			ntp_unpaired_element unpaired_element;
			element = MALLOC_TAG (sizeof (nt_element), track_id);
			unpaired_element = MALLOC_TAG (sizeof (nt_unpaired_element), track_id);
			element->type = unpaired;
			element->unpaired = unpaired_element;
			unpaired_element->i_constraint.reference = el->unpaired->i_constraint.reference;
			unpaired_element->i_constraint.element_type =
			                    el->unpaired->i_constraint.element_type;
			unpaired_element->next_linked_bp = this_linked_bp;
			unpaired_element->length = unpaired_cnt;
			unpaired_element->dist = skip_cnt;
			unpaired_element->next = NULL;
			
			if (is_fp_element) {
				if (this_linked_bp->fp_elements) {
					unpaired_element->next = this_linked_bp->fp_elements;
				}
				
				this_linked_bp->fp_elements = element;
			}
			
			else {
				if (this_linked_bp->tp_elements) {
					unpaired_element->next = this_linked_bp->tp_elements;
				}
				
				this_linked_bp->tp_elements = element;
			}
			
			if (track_constraints) {
				wrapper_constraint_elements[track_id] = element;
			}
		}
		
		list_iterator_stop (*this_list);
		#ifndef NO_FULL_CHECKS
	}
	
		#endif
}

#ifdef SEARCH_SEQ_DETAIL
static inline void check_bp_candidates_for_targets (ntp_list candidate_list,
                                        const nt_bp *restrict targets, const nt_hit_count num_targets,
                                        char *target_msg, const uchar indent) {
	static char tmp_msg[MAX_MSG_LEN];
	static nt_hit_count targets_hit[MAX_TARGETS];
	static nt_hit_count targets_idx[MAX_TARGETS];
	nt_hit_count num_targets_hit = 0, target_idx = 0;
	bool target_already_hit;
	
	if (list_iterator_start (candidate_list)) {
		while (list_iterator_hasnext (candidate_list)) {
			ntp_linked_bp this_candidate = list_iterator_next (candidate_list);
			
			if (this_candidate) {
				char cand_msg[MAX_MSG_LEN];
				sprintf (cand_msg, "\n");
				COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, cand_msg, false);
				
				for (uchar i = 0; i < indent; i++) {
					cand_msg[i] = S_WHITE_SPACE[0];
				}
				
				cand_msg[indent] = '\0';
				COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, cand_msg, false);
				uchar extra_indent = 5;
				sprintf (cand_msg, "%03u: ", target_idx);
				COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, cand_msg, false);
				
				while (this_candidate &&
				       this_candidate->track_id) {   // currently, targets only allowed with track_id 0
					sprintf (cand_msg, "%3u,%3u    ", this_candidate->bp->fp_posn,
					         this_candidate->bp->tp_posn);
					COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, cand_msg, false);
					extra_indent += 11;
					this_candidate = this_candidate->prev_linked_bp;
				}
				
				if (this_candidate) {
					extra_indent += 11;
					sprintf (cand_msg, "%3u,%3u    ", this_candidate->bp->fp_posn,
					         this_candidate->bp->tp_posn);
					COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, cand_msg, false);
					
					for (uchar i = 0; i < MAX_MODEL_STRING_LEN - indent - extra_indent; i++) {
						cand_msg[i] = S_WHITE_SPACE[0];
					}
					
					cand_msg[MAX_MODEL_STRING_LEN - indent - extra_indent] = '\0';
					COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, cand_msg, false);
					
					for (nt_hit_count i = 0; i < num_targets; i++) {
						if (this_candidate->bp->fp_posn == targets[i].fp_posn &&
						    this_candidate->bp->tp_posn == targets[i].tp_posn) {
							target_already_hit = false;
							
							for (nt_hit_count j = 0; j < num_targets_hit; j++) {
								if (targets_hit[j] == i) {
									target_already_hit = true;
									break;
								}
							}
							
							if (!target_already_hit) {
								targets_hit[num_targets_hit] = i;
								targets_idx[num_targets_hit] = target_idx;
								num_targets_hit++;
							}
							
							break;
						}
					}
				}
			}
			
			target_idx++;
		}
		
		list_iterator_stop (candidate_list);
		
		if (!num_targets_hit) {
			sprintf (target_msg, " (no targets hit)");
		}
		
		else {
			for (nt_hit_count i = 0; i < num_targets_hit; i++) {
				if (!i) {
					sprintf (target_msg, " (target %u @ idx %u", i + 1, targets_idx[i]);
				}
				
				else {
					g_memcpy (tmp_msg, target_msg, strlen (target_msg));
					tmp_msg[strlen (target_msg)] = '\0';
					sprintf (target_msg, "%s, %u @ idx %u", tmp_msg, i + 1, targets_idx[i]);
				}
			}
			
			g_memcpy (tmp_msg, target_msg, strlen (target_msg));
			tmp_msg[strlen (target_msg)] = '\0';
			sprintf (target_msg, "%s hit)", tmp_msg);
		}
	}
	
	else {
		sprintf (target_msg, " (could not iterate on list)");
	}
}
#endif

static inline void update_wrapper_constraint_dist (ntp_list restrict
                                        *current_list, nt_element *restrict el, nt_rel_count unpaired_cnt,
                                        uchar track_id, char advanced_pair_track_id,
                                        const char containing_pair_track_id,
                                        const uchar pos_var) {
	if (*current_list && (*current_list)->numels) {
		REGISTER
		bool building_constraints = track_id > last_wrapper_constraint_track_id;
		
		if (building_constraints) {
			// denote that we are still building (adding) constraints between wrapper bp and first 'real' bp;
			// building_constraints will be set to false when iterating over pos_vars and therefore all
			// constraints would have already been set (using set_constraint_links) in the very first invocation
			last_wrapper_constraint_track_id = track_id;
		}
		
		REGISTER
		ntp_linked_bp first_linked_bp = NULL;
		list_iterator_start (*current_list);
		
		while (list_iterator_hasnext (*current_list)) {
			/*
			 * find wrapper bp
			 */
			first_linked_bp = list_iterator_next (*current_list);
			
			while (first_linked_bp && first_linked_bp->bp->fp_posn) {
				first_linked_bp = first_linked_bp->prev_linked_bp;
			}
			
			if (!first_linked_bp) {
				list_iterator_stop (*current_list);
				return;
			}
			
			if (first_linked_bp->fp_elements) {
				if (building_constraints) {
					/*
					 * when still building up constraints, simply add up this constraint's
					 * current pos_var value to all existing (already built) fp_elements distances
					 */
					REGISTER
					ntp_element prev_fp_element = first_linked_bp->fp_elements;
					
					do {
						prev_fp_element->unpaired->dist += unpaired_cnt;
						prev_fp_element = prev_fp_element->unpaired->next;
					}
					while (prev_fp_element);
				}
				
				else {
					if (el->unpaired->min == el->unpaired->max) {
						// if this is a fixed (non-pos-var) element, then just skip over
						continue;
					}
					
					short delta;
					
					if (wrapper_constraint_elements[track_id]) {
						/*
						 * if this element is a constraint element, then we know both the current
						 * pos_var value and the new one (unpaired_cnt); delta should be set to
						 * the difference between the two - to be subsequently used further below
						 * when subtracting it from the current distances of all 5' constraints
						 * relative to this one
						 */
						delta = wrapper_constraint_elements[track_id]->unpaired->length - unpaired_cnt;
						wrapper_constraint_elements[track_id]->unpaired->length = unpaired_cnt;
					}
					
					else {
						/*
						 * if this element is not a constraint element, then we need to deduce the current
						 * 'pos_var' value from the neighbouring constraint elements; or, if this is the
						 * last unpaired element before the 1st 'real' bp simply take the adjacent
						 * element's distance
						 */
						
						// TODO: assumes track_id>1 and that only 1 unpaired exists between any 2 paired elements
						if (track_id == last_wrapper_constraint_track_id) {
							delta = wrapper_constraint_elements[track_id - 1]->unpaired->dist -
							        unpaired_cnt;
						}
						
						else {
							delta = (short) ((wrapper_constraint_elements[track_id - 1]->unpaired->dist -
							                  wrapper_constraint_elements[track_id + 1]->unpaired->dist - 1) -
							                 unpaired_cnt);
						}
					}
					
					if (track_id > 1) {
						/*
						 * if not already the most 5' proximal fp_element, then find
						 * the next fp_element which happens to be a constraint
						 */
						REGISTER
						ntp_element prev_fp_element;
						
						do {
							track_id--;
							prev_fp_element = wrapper_constraint_elements[track_id];
						}
						while (1 < track_id && (!prev_fp_element ||
						                        !prev_fp_element->unpaired->i_constraint.reference));
						                        
						if (prev_fp_element && prev_fp_element->unpaired->i_constraint.reference) {
							/*
							 * shorten this fp_element and any preceding (5' proximal) ones by delta
							 */
							do {
								prev_fp_element->unpaired->dist -= delta;
								prev_fp_element = prev_fp_element->unpaired->next;
							}
							while (prev_fp_element);
						}
					}
				}
			}
		}
		
		list_iterator_stop (*current_list);
		
		/*
		 * if this el is a constraint and still building_constraints, then set_constraint_links
		 */
		if (building_constraints && el->unpaired->i_constraint.reference) {
			set_constraint_links (el, current_list, unpaired_cnt, 0, track_id,
			                      advanced_pair_track_id, true);
		}
	}
}

void destroy_min_max_dist (ntp_list *min_stack_dist, ntp_list *max_stack_dist,
                           ntp_list *in_extrusion, ntp_list *dist_els) {
	for (REGISTER nt_stack_size i = 0; i < MAX_STACK_LEN; i++) {
		// TODO: error handling
		list_iterator_start (min_stack_dist[i]);
		
		while (list_iterator_hasnext (min_stack_dist[i])) {
			FREE_DEBUG (list_iterator_next (min_stack_dist[i]),
			            "nt_stack_idist of min_stack_dist in destroy_min_max_dist");
		}
		
		list_iterator_stop (min_stack_dist[i]);
		list_destroy (min_stack_dist[i]);
		FREE_DEBUG (min_stack_dist[i],
		            "nt_list of min_stack_dist in destroy_min_max_dist");
		list_iterator_start (max_stack_dist[i]);
		
		while (list_iterator_hasnext (max_stack_dist[i])) {
			FREE_DEBUG (list_iterator_next (max_stack_dist[i]),
			            "nt_stack_idist of max_stack_dist in destroy_min_max_dist");
		}
		
		list_iterator_stop (max_stack_dist[i]);
		list_destroy (max_stack_dist[i]);
		FREE_DEBUG (max_stack_dist[i],
		            "nt_list of max_stack_dist in destroy_min_max_dist");
		list_iterator_start (in_extrusion[i]);
		
		while (list_iterator_hasnext (in_extrusion[i])) {
			FREE_DEBUG (list_iterator_next (in_extrusion[i]),
			            "short of in_extrusion in destroy_min_max_dist");
		}
		
		list_iterator_stop (in_extrusion[i]);
		list_destroy (in_extrusion[i]);
		FREE_DEBUG (in_extrusion[i], "nt_list of in_extrusion in destroy_min_max_dist");
		list_destroy (dist_els[i]);
		FREE_DEBUG (dist_els[i], "nt_list of dist_els in destroy_min_max_dist");
	}
	
	FREE_DEBUG (min_stack_dist,
	            "nt_list for min_stack_dist in destroy_min_max_dist");
	FREE_DEBUG (max_stack_dist,
	            "nt_list for max_stack_dist in destroy_min_max_dist");
	FREE_DEBUG (in_extrusion, "nt_list for in_extrusion in destroy_min_max_dist");
	FREE_DEBUG (dist_els, "nt_list for dist_els in destroy_min_max_dist");
}

static inline bool constraint_fp_element_length_matches (
                    ntp_linked_bp restrict this_linked_bp,
                    const nt_constraint *restrict this_constraint,
                    const nt_rel_count constraint_fp_element_length) {
	// use last linked_bp as a template to identify fp element length
	REGISTER
	ntp_linked_bp tmp_linked_bp = this_linked_bp;
	REGISTER
	bool is_pk = this_constraint->type ==
	             pseudoknot; // currently assumes PK *or* BT
	REGISTER
	ntp_pseudoknot this_pseudoknot = NULL;
	REGISTER
	ntp_base_triple this_basetriple = NULL;
	
	if (is_pk) {
		this_pseudoknot = this_constraint->pseudoknot;
	}
	
	else {
		this_basetriple = this_constraint->base_triple;
	}
	
	while (tmp_linked_bp) {
		if (tmp_linked_bp->fp_elements) {
			REGISTER
			ntp_element this_fp_element = tmp_linked_bp->fp_elements;
			
			do {
				if (((is_pk &&
				      this_fp_element->unpaired->i_constraint.reference->pseudoknot ==
				      this_pseudoknot) ||
				     (!is_pk &&
				      this_fp_element->unpaired->i_constraint.reference->base_triple ==
				      this_basetriple)) &&
				    this_fp_element->unpaired->i_constraint.element_type == constraint_fp_element) {
					return constraint_fp_element_length == this_fp_element->unpaired->length;
				}
				
				this_fp_element = this_fp_element->unpaired->next;
			}
			while (this_fp_element);
		}
		
		if (tmp_linked_bp->tp_elements) {
			REGISTER
			ntp_element this_tp_element = tmp_linked_bp->tp_elements;
			
			do {
				if (((is_pk &&
				      this_tp_element->unpaired->i_constraint.reference->pseudoknot ==
				      this_pseudoknot) ||
				     (!is_pk &&
				      this_tp_element->unpaired->i_constraint.reference->base_triple ==
				      this_basetriple)) &&
				    this_tp_element->unpaired->i_constraint.element_type == constraint_fp_element) {
					return constraint_fp_element_length == this_tp_element->unpaired->length;
				}
				
				this_tp_element = this_tp_element->unpaired->next;
			}
			while (this_tp_element);
		}
		
		tmp_linked_bp = tmp_linked_bp->prev_linked_bp;
	}
	
	return false;
}

static inline void match_sequence_constraint (const ntp_seq restrict seq,
                                        ntp_list restrict *current_list, const nt_constraint *restrict this_constraint,
                                        const ushort fp_bp_idx, const nt_constraint_element_type
                                        fp_constraint_element_type, const ushort fp_el_idx,
                                        const ushort tp_bp_idx, const nt_constraint_element_type
                                        tp_constraint_element_type,
                                        const nt_rel_count unpaired_cnt,
                                        const nt_rel_count skip_cnt, const bool is_pk) {
	char constraint_fp_buff[MAX_SEQ_LEN + 1], constraint_tp_buff[MAX_SEQ_LEN + 1];
	REGISTER
	nt_hit_count list_size = (*current_list)->numels, list_idx = 0;
	REGISTER
	nt_rel_seq_len seq_len = (nt_rel_seq_len)strlen (seq);
	
	while (list_idx < list_size) {
		REGISTER
		ntp_linked_bp tp_linked_bp = list_get_at (*current_list, list_idx);
		
		if (!constraint_fp_element_length_matches (tp_linked_bp, this_constraint,
		                                        unpaired_cnt)) {
			list_delete_at (*current_list, list_idx);
			list_size--;
			continue;
		}
		
		REGISTER
		ntp_linked_bp fp_linked_bp = tp_linked_bp, tmp_linked_bp = NULL;
		REGISTER
		ushort this_fp_bp_idx = fp_bp_idx, this_fp_el_idx = fp_el_idx,
		       this_tp_bp_idx = tp_bp_idx;
		REGISTER
		nt_rel_count i, j;
		
		while (this_fp_bp_idx > 0) {
			tmp_linked_bp = fp_linked_bp;
			fp_linked_bp = fp_linked_bp->prev_linked_bp;
			this_fp_bp_idx--;
		}
		
		j = 0;
		
		if (fp_constraint_element_type == constraint_fp_element) {
			REGISTER
			ntp_element fp_element = fp_linked_bp->fp_elements;
			
			while (this_fp_el_idx > 0) {
				fp_element = fp_element->unpaired->next;
				this_fp_el_idx--;
			}
			
			if (!fp_linked_bp->bp->fp_posn) {
				/*
				 * wrapper be -> use tmp_linked_bp to access prev linked_bp
				 */
				
				// given an unpaired_cnt, check that fp_posn negatively offset by dist + unpaired_cnt is greater or equal to 0
				if (0 > tmp_linked_bp->bp->fp_posn - fp_element->unpaired->dist - unpaired_cnt -
				    1) {
					list_delete_at (*current_list, list_idx);
					list_size--;
					continue;
				}
				
				// populate constraint_fp_buff using tmp_linked_bp's fp_posn, fp_element->unpaired->dist, and unpaired_cnt
				for (i = (nt_rel_count) (tmp_linked_bp->bp->fp_posn - fp_element->unpaired->dist
				                         - unpaired_cnt - 1);
				     i < tmp_linked_bp->bp->fp_posn - fp_element->unpaired->dist - 1;
				     i++) {
					constraint_fp_buff[j] = seq[i];
					j++;
				}
			}
			
			else {
				if (fp_linked_bp->bp->fp_posn + fp_linked_bp->stack_len +
				    fp_element->unpaired->dist + unpaired_cnt - 1 > seq_len) {
					list_delete_at (*current_list, list_idx);
					list_size--;
					continue;
				}
				
				for (i = (nt_rel_count) (fp_linked_bp->bp->fp_posn + fp_linked_bp->stack_len +
				                         fp_element->unpaired->dist - 1);
				     i < fp_linked_bp->bp->fp_posn + fp_linked_bp->stack_len +
				     fp_element->unpaired->dist + unpaired_cnt - 1;
				     i++) {
					constraint_fp_buff[j] = seq[i];
					j++;
				}
			}
		}
		
		else {
			REGISTER
			ntp_element tp_element = fp_linked_bp->tp_elements;
			
			while (this_fp_el_idx > 0) {
				tp_element = tp_element->unpaired->next;
				this_fp_el_idx--;
			}
			
			// check sequence length limit
			if (fp_linked_bp->bp->tp_posn + fp_linked_bp->stack_len +
			    tp_element->unpaired->dist + unpaired_cnt - 1 > seq_len) {
				list_delete_at (*current_list, list_idx);
				list_size--;
				continue;
			}
			
			// can only have real (non-wrapper bp) when using tp_elements
			for (i = (nt_rel_count) (fp_linked_bp->bp->tp_posn + fp_linked_bp->stack_len +
			                         tp_element->unpaired->dist - 1);
			     i < fp_linked_bp->bp->tp_posn + fp_linked_bp->stack_len +
			     tp_element->unpaired->dist + unpaired_cnt - 1;
			     i++) {
				constraint_fp_buff[j] = seq[i];
				j++;
			}
		}
		
		while (this_tp_bp_idx > 0) {
			tp_linked_bp = tp_linked_bp->prev_linked_bp;
			this_tp_bp_idx--;
		}
		
		j = 0;
		
		if (tp_constraint_element_type == constraint_fp_element) {
			// check sequence length limit
			if (tp_linked_bp->bp->fp_posn + tp_linked_bp->stack_len + skip_cnt +
			    unpaired_cnt - 1 > seq_len) {
				list_delete_at (*current_list, list_idx);
				list_size--;
				continue;
			}
			
			for (i = (nt_rel_count) (tp_linked_bp->bp->fp_posn + tp_linked_bp->stack_len +
			                         skip_cnt - 1);
			     i < tp_linked_bp->bp->fp_posn + tp_linked_bp->stack_len + skip_cnt +
			     unpaired_cnt - 1;
			     i++) {
				constraint_tp_buff[j] = seq[i];
				j++;
			}
		}
		
		else {
			// check sequence length limit
			if (tp_linked_bp->bp->tp_posn + tp_linked_bp->stack_len + unpaired_cnt +
			    skip_cnt - 1 > seq_len) {
				list_delete_at (*current_list, list_idx);
				list_size--;
				continue;
			}
			
			for (i = (nt_rel_count) (tp_linked_bp->bp->tp_posn + tp_linked_bp->stack_len +
			                         skip_cnt - 1);
			     i < tp_linked_bp->bp->tp_posn + tp_linked_bp->stack_len + unpaired_cnt +
			     skip_cnt - 1;
			     i++) {
				constraint_tp_buff[j] = seq[i];
				j++;
			}
		}
		
		for (i = 0; i < unpaired_cnt; i++) {
			const char fp_nt = constraint_fp_buff[i],
			           tp_nt = constraint_tp_buff[unpaired_cnt - i - 1];
			           
			if (! ((fp_nt == 'g' && (tp_nt == 'c' || tp_nt == 'u')) ||
			       (fp_nt == 'a' && tp_nt == 'u') ||
			       (tp_nt == 'g' && (fp_nt == 'c' || fp_nt == 'u')) ||
			       (tp_nt == 'a' && fp_nt == 'u'))) {
				break;
			}
		}
		
		if (i != unpaired_cnt) {
			// constraint NOT satisfied - delete from current_list
			list_delete_at (*current_list, list_idx);
			list_size--;
		}
		
		else {
			// constraint IS satisfied - move on in current_list
			list_idx++;
		}
	}
}

static inline void list_prune_using_constraint (const nt_model *restrict model,
                                        const ntp_seq restrict seq,
                                        ntp_list restrict *current_list,
                                        nt_element *restrict el,
                                        const nt_rel_count unpaired_cnt,
                                        const nt_rel_count skip_cnt,
                                        char advanced_pair_track_id,
                                        const char containing_pair_track_id) {
	if (*current_list && (*current_list)->numels) {
		REGISTER
		ntp_linked_bp first_linked_bp = list_get_at (*current_list, 0),
		              tmp_linked_bp = first_linked_bp;
		REGISTER
		ushort fp_bp_idx = 0, current_num_bp = 0, fp_el_idx = 0;
		ushort tp_bp_idx = 0;
		REGISTER
		bool found_element;
		REGISTER
		ntp_pseudoknot this_pseudoknot = NULL;
		REGISTER
		ntp_base_triple this_basetriple = NULL;
		REGISTER
		bool is_pk = el->unpaired->i_constraint.reference->type ==
		             pseudoknot; // currently assumes PK *or* BT
		             
		if (is_pk) {
			this_pseudoknot = el->unpaired->i_constraint.reference->pseudoknot;
		}
		
		else {
			this_basetriple = el->unpaired->i_constraint.reference->base_triple;
		}
		
		/*
		 * assumes current (first) bp has 3p constraint element - and depending on whether
		 * containing_ and advanced_pair_track_id are equal or not we can search exclusively
		 * in fp_elements or tp_elements;
		 * if 3p constraint element is not yet present, then assume that it's index is
		 * 'next in line'
		 */
		/*
		 * search fp_elements and tp_elements for fp constraint element
		 */
		// indexing of 3p bp:
		//      cannot index 3p bp by searching in current_list, because element might not be present -> use model information
		//      first identify the model's closest (previous) paired element
		//      then set tp's base pair index to (the current number of base pairs in current_list - 1) - found paired element index;
		//      need -1 since model's paired element index is relative (and the 1st bp relative index is 0)
		REGISTER
		ntp_element prev_paired_element = el;
		
		while (prev_paired_element->unpaired->prev_type == unpaired) {
			prev_paired_element = prev_paired_element->unpaired->prev_unpaired;
		}
		
		prev_paired_element = prev_paired_element->unpaired->prev_paired;
		get_paired_element_relative_index (model->first_element, prev_paired_element,
		                                   &tp_bp_idx);
		                                   
		while (tmp_linked_bp) {
			current_num_bp++;
			tmp_linked_bp = tmp_linked_bp->prev_linked_bp;
		}
		
		tp_bp_idx = (ushort) (current_num_bp - 1) - tp_bp_idx;
		// index 5p bp directly from first item available in current_list;
		// once both 5p and 3p are indexed, invoke match_sequence_constraint
		found_element = false;
		
		while (first_linked_bp) {
			if (first_linked_bp->fp_elements) {
				REGISTER
				ntp_element this_fp_element = first_linked_bp->fp_elements;
				fp_el_idx = 0;
				
				do {
					if (((is_pk &&
					      this_fp_element->unpaired->i_constraint.reference->pseudoknot ==
					      this_pseudoknot) ||
					     (!is_pk &&
					      this_fp_element->unpaired->i_constraint.reference->base_triple ==
					      this_basetriple)) &&
					    this_fp_element->unpaired->i_constraint.element_type == constraint_fp_element) {
						found_element = true;
						break;
					}
					
					this_fp_element = this_fp_element->unpaired->next;
					fp_el_idx++;
				}
				while (this_fp_element);
				
				if (found_element) {
					match_sequence_constraint (seq, current_list,
					                           el->unpaired->i_constraint.reference,
					                           fp_bp_idx, constraint_fp_element, fp_el_idx,
					                           tp_bp_idx, containing_pair_track_id == advanced_pair_track_id ?
					                           constraint_fp_element : constraint_tp_element,
					                           unpaired_cnt,
					                           skip_cnt, is_pk);
					return;
				}
			}
			
			if (first_linked_bp->tp_elements) {
				REGISTER
				ntp_element this_tp_element = first_linked_bp->tp_elements;
				fp_el_idx = 0;
				
				do {
					if (((is_pk &&
					      this_tp_element->unpaired->i_constraint.reference->pseudoknot ==
					      this_pseudoknot) ||
					     (!is_pk &&
					      this_tp_element->unpaired->i_constraint.reference->base_triple ==
					      this_basetriple)) &&
					    this_tp_element->unpaired->i_constraint.element_type == constraint_fp_element) {
						found_element = true;
						break;
					}
					
					this_tp_element = this_tp_element->unpaired->next;
					fp_el_idx++;
				}
				while (this_tp_element);
				
				if (found_element) {
					match_sequence_constraint (seq, current_list,
					                           el->unpaired->i_constraint.reference,
					                           fp_bp_idx, constraint_tp_element, fp_el_idx,
					                           tp_bp_idx, containing_pair_track_id == advanced_pair_track_id ?
					                           constraint_fp_element : constraint_tp_element,
					                           unpaired_cnt,
					                           skip_cnt, is_pk);
					return;
				}
			}
			
			fp_bp_idx++;
			first_linked_bp = first_linked_bp->prev_linked_bp;
		}
	}
}

/*
 * private recursive function to search a sequence starting from a current model
 * nt_element and list of nt_linked_bp
 *
 * input:   sequence hash
 *          model nt_element el
 *          current_list of nt_linked_bp
 *          current_stack_len of bps in the current_list
 *          track_id for target linked_bps
 *          skip_count in between current_list and the relevant filter_list
 *
 * output:  nt_list of enclosing bps matching the input criteria
 *          nt_list of matched_cnt nts for each matching nt_bp
 *
 * notes:   - traverses model elements starting from el, recursively matching
 *            nested bps against the given sequence as required
 */
bool search_seq_at (const nt_model *restrict model,
                    const ntp_seq restrict seq,
                    ntp_seq_bp restrict seq_bp,
                    nt_element *restrict el,
                    nt_element *restrict prev_el,
                    ntp_list restrict *current_list,
                    nt_stack_size current_stack_len,
                    const nt_rel_count match_cnt,
                    const nt_rel_count skip_cnt,
                    const uchar track_id,
                    char advanced_pair_track_id,
                    const char containing_pair_track_id,
                    const uchar pos_var,
                    nt_element *restrict continuation_element,
                    ntp_list restrict *matched_cnts
#ifdef SEARCH_SEQ_DETAIL
	, const uchar indent, const nt_bp *restrict targets,
	const nt_hit_count num_targets
#endif
                   ) {
	if (*current_list && (MAX_SEARCH_LIST_SIZE < (*current_list)->numels)) {
		return false;
	}
	
	if (el->type == unpaired) {
		REGISTER
		nt_rel_count unpaired_cnt = el->unpaired->min +
		                            pos_var;      // track and accumulate unpaired nts over all elements
		                            
		if (el->unpaired->i_constraint.reference &&
		    (el->unpaired->i_constraint.reference->type == pseudoknot ||
		     el->unpaired->i_constraint.reference->type == base_triple) &&
		    el->unpaired->i_constraint.element_type == constraint_tp_element) {
			list_prune_using_constraint (model, seq, current_list, el, unpaired_cnt,
			                             skip_cnt, advanced_pair_track_id, containing_pair_track_id);
			                             
			if (!*current_list || ! ((*current_list)->numels)) {
				COMMIT_DEBUG (REPORT_INFO, SEARCH_SEQ,
				              "backtracking since all current_list elements failed to match unpaired constraint in search_seq_at",
				              false);
				              
				if (*current_list) {
					list_destroy (*current_list);
					FREE_DEBUG (*current_list, "current_list in search_seq_at");
				}
				
				if (*matched_cnts) {
					list_destroy (*matched_cnts);
					FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
				}
				
				*matched_cnts = NULL;
				*current_list = NULL;
				return true;
			}
		}
		
		ntp_list restrict updated_matched_cnts = NULL;
		#ifndef NO_FULL_CHECKS
		
		if (!
		#endif
		    ntp_list_alloc_debug (&updated_matched_cnts,
		                          "updated_matched_cnts in search_seq_at")
	    #ifndef NO_FULL_CHECKS
		   ) {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "cannot alloc updated_matched_cnts in search_seq_at", false);
			              
			if (*current_list) {
				list_destroy (*current_list);
				FREE_DEBUG (*current_list, "current_list in search_seq_at");
			}
			
			if (*matched_cnts) {
				list_destroy (*matched_cnts);
				FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
			}
			
			*matched_cnts = NULL;
			*current_list = NULL;
			return false;
		}
		
	    #else
		    ;
	    #endif
		#ifdef SEARCH_SEQ_DETAIL
		// TODO: update for base triples
		char msg[MAX_MSG_LEN + 1];
		bool is_pk = false, is_fp = false;
		
		if (el->unpaired->i_constraint.reference) {
			if (el->unpaired->i_constraint.reference->type == pseudoknot) {
				is_pk = true;
				
				if (el->unpaired->i_constraint.element_type == constraint_fp_element) {
					is_fp = true;
				};
			}
		}
		
		for (uchar i = 0; i < indent; i++) {
			msg[i] = S_WHITE_SPACE[0];
		}
		
		for (uchar i = indent; i < indent + unpaired_cnt; i++) {
			if (is_pk) {
				if (is_fp) {
					msg[i] = SS_NEUTRAL_OPEN_PK;
				}
				
				else {
					msg[i] = SS_NEUTRAL_CLOSE_PK;
				};
			}
			
			else {
				msg[i] = SS_NEUTRAL_UNSTRUCTURED_RESIDUE;
			}
		}
		
		for (uchar i = (uchar) (indent + unpaired_cnt); i < MAX_MODEL_STRING_LEN; i++) {
			msg[i] = S_WHITE_SPACE[0];
		}
		
		msg[MAX_MODEL_STRING_LEN] = '\0';
		COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
		
		if (is_pk) {
			sprintf (msg, "skipped PK%u\n", unpaired_cnt);
		}
		
		else {
			sprintf (msg, "skipped U%u\n", unpaired_cnt);
		}
		
		COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
		#endif
		REGISTER
		nt_element *restrict unpaired_next = el->unpaired->next;
		
		if (!unpaired_next && continuation_element) {
			unpaired_next = continuation_element;
			continuation_element = NULL;
		}
		
		REGISTER
		bool is_wrapper_constraint = !current_stack_len && !containing_pair_track_id &&
		                             !advanced_pair_track_id;
		                             
		if (is_wrapper_constraint) {
			update_wrapper_constraint_dist (current_list, el, unpaired_cnt, track_id,
			                                advanced_pair_track_id, containing_pair_track_id, pos_var);
		}
		
		REGISTER
		ntp_list restrict updated_list =
		                    NULL;                    // locally updated list of nt_linked_bps
		                    
		if (unpaired_next) {
			REGISTER
			char this_pos_var = get_element_pos_var_range (unpaired_next);
			
			if (!is_wrapper_constraint && el->unpaired->i_constraint.reference &&
			    *current_list && (*current_list)->numels) {
				free_constraint_links (el, current_list, advanced_pair_track_id);
				set_constraint_links (el, current_list, unpaired_cnt, skip_cnt, track_id,
				                      advanced_pair_track_id, false);
			}
			
			do {
				ntp_list restrict this_matched_cnts = NULL;
				#ifndef NO_FULL_CHECKS
				
				if (!
				#endif
				    ntp_list_alloc_debug (&this_matched_cnts, "this_matched_cnts in search_seq_at")
			    #ifndef NO_FULL_CHECKS
				   ) {
					COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
					              "cannot alloc this_matched_cnts in search_seq_at", false);
					              
					if (*current_list) {
						list_destroy (*current_list);
						FREE_DEBUG (*current_list, "current_list in search_seq_at");
					}
					
					if (*matched_cnts) {
						list_destroy (*matched_cnts);
						FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
					}
					
					if (this_matched_cnts) {
						list_destroy (this_matched_cnts);
						FREE_DEBUG (this_matched_cnts, "this_matched_cnts in search_seq_at");
					}
					
					if (updated_matched_cnts) {
						list_destroy (updated_matched_cnts);
						FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
					}
					
					*matched_cnts = NULL;
					*current_list = NULL;
					return false;
				}
				
			    #else
				    ;
			    #endif
				#ifdef SEARCH_SEQ_DETAIL
				
				for (uchar i = 0; i < indent; i++) {
					msg[i] = S_WHITE_SPACE[0];
				}
				
				for (uchar i = indent; i < indent + unpaired_cnt; i++) {
					if (is_pk) {
						if (is_fp) {
							msg[i] = SS_NEUTRAL_OPEN_PK;
						}
						
						else {
							msg[i] = SS_NEUTRAL_CLOSE_PK;
						};
					}
					
					else {
						msg[i] = SS_NEUTRAL_UNSTRUCTURED_RESIDUE;
					}
				}
				
				for (uchar i = (uchar) (indent + unpaired_cnt); i < MAX_MODEL_STRING_LEN; i++) {
					msg[i] = S_WHITE_SPACE[0];
				}
				
				msg[MAX_MODEL_STRING_LEN] = '\0';
				COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
				sprintf (msg, "doing unpaired_next using pos_var %d\n", this_pos_var);
				COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
				#endif
				// TODO: error handling
				ntp_list restrict this_list = duplicate_list_shallow (*current_list,
				                                        "this_list duplicate of current_list in search_seq_at");
				REGISTER
				bool success;
				
				if (unpaired_next->type == unpaired ||
				    unpaired_next->paired->min + this_pos_var > 0) {
					success = search_seq_at (model,
					                         seq,
					                         seq_bp,
					                         unpaired_next,
					                         el,
					                         &this_list,
					                         current_stack_len,
					                         match_cnt,
					                         skip_cnt + unpaired_cnt,
					                         (uchar) (track_id + 1),
					                         (advanced_pair_track_id),
					                         (containing_pair_track_id),
					                         (uchar) (this_pos_var),
					                         // if not NULL, pass on continuation_element to the next element, so that a
					                         // terminal element can eventually process it
					                         continuation_element,
					                         &this_matched_cnts
					                         #ifdef SEARCH_SEQ_DETAIL
					                         , (uchar) (indent + unpaired_cnt), targets, num_targets
					                         #endif
					                        );
				}
				
				else {
					ntp_list previously_this_list = this_list;
					this_list = list_null_advance (this_list,
					                               (uchar) (track_id + 1));
					                               
					if (previously_this_list && previously_this_list != this_list) {
						list_destroy (previously_this_list);
						FREE_DEBUG (previously_this_list, "previously this_list in search_seq_at");
					}
					
					success = search_seq_at (model,
					                         seq,
					                         seq_bp,
					                         // mask/skip 0-pos_var paired element, by jumping straight to its fp_next element,
					                         // if a tp_next element exists, then utilize continuation_element (see the invoked argument further below)
					                         unpaired_next->paired->fp_next,
					                         el,
					                         &this_list,
					                         current_stack_len,
					                         match_cnt,
					                         skip_cnt + unpaired_cnt,
					                         // skip paired with pos_var 0 -> use (track_id+2)
					                         (uchar) (track_id + 2),
					                         (advanced_pair_track_id),
					                         (containing_pair_track_id),
					                         (uchar) (this_pos_var),
					                         // set continuation_element to the skipped pair's tp_next element
					                         unpaired_next->paired->tp_next,
					                         &this_matched_cnts
					                         #ifdef SEARCH_SEQ_DETAIL
					                         , (uchar) (indent + unpaired_cnt), targets, num_targets
					                         #endif
					                        );
				}
				
				if (success) {
					if (this_list && this_list->numels) {
						ntp_list previously_updated_list = updated_list;
						updated_list = ntp_list_concatenate (updated_list, this_list, track_id);
						
						if (previously_updated_list) {
							list_destroy (previously_updated_list);
							FREE_DEBUG (previously_updated_list,
							            "previously updated_list in search_seq_at");
						}
						
						if (this_list) {
							list_destroy (this_list);
							FREE_DEBUG (this_list, "this_list in search_seq_at");
							this_list = NULL;
						}
						
						ntp_list previously_updated_matched_cnts = updated_matched_cnts;
						updated_matched_cnts = ntp_count_list_concatenate (updated_matched_cnts,
						                                        this_matched_cnts);
						                                        
						if (previously_updated_matched_cnts &&
						    previously_updated_matched_cnts != updated_matched_cnts) {
							list_destroy (previously_updated_matched_cnts);
							FREE_DEBUG (previously_updated_matched_cnts,
							            "previously updated_matched_cnts in search_seq_at");
						}
						
						if (this_matched_cnts && this_matched_cnts != updated_matched_cnts) {
							list_destroy (this_matched_cnts);
							FREE_DEBUG (this_matched_cnts, "this_matched_cnts in search_seq_at");
						}
						
						this_matched_cnts = NULL;
					}
				}
				
				else {
					if (*current_list) {
						list_destroy (*current_list);
						FREE_DEBUG (*current_list, "current_list in search_seq_at");
					}
					
					if (*matched_cnts) {
						list_destroy (*matched_cnts);
						FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
					}
					
					if (this_matched_cnts) {
						list_destroy (this_matched_cnts);
						FREE_DEBUG (this_matched_cnts, "this_matched_cnts in search_seq_at");
					}
					
					if (updated_matched_cnts) {
						list_destroy (updated_matched_cnts);
						FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
					}
					
					if (updated_list) {
						list_destroy (updated_list);
						FREE_DEBUG (updated_list, "updated_list in search_seq_at");
					}
					
					if (this_list) {
						list_destroy (this_list);
						FREE_DEBUG (this_list, "this_list in search_seq_at");
					}
					
					*matched_cnts = NULL;
					*current_list = NULL;
					return false;
				}
				
				this_pos_var--;
			}
			while (this_pos_var >= 0);
			
			if (updated_list && updated_list->numels) {
				if (*current_list) {
					list_destroy (*current_list);
					FREE_DEBUG (*current_list, "current_list in search_seq_at");
				}
				
				*current_list = updated_list;
				
				if (*matched_cnts) {
					list_destroy (*matched_cnts);
					FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
				}
				
				*matched_cnts = list_sum (updated_matched_cnts, unpaired_cnt, track_id);
				
				if (updated_matched_cnts != *matched_cnts) {
					list_destroy (updated_matched_cnts);
					FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
				}
				
				updated_matched_cnts = NULL;
			}
			
			else {
				if (*current_list) {
					list_destroy (*current_list);
					FREE_DEBUG (*current_list, "current_list in search_seq_at");
				}
				
				if (*matched_cnts) {
					list_destroy (*matched_cnts);
					FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
				}
				
				*current_list = NULL;
				*matched_cnts = NULL;
			}
		}
		
		else {
			if (advanced_pair_track_id >= 0 && containing_pair_track_id >= 0 &&
			    *current_list && ((*current_list)->numels)) {
				// if containing_pair and advanced_pair track_ids are both 0, then skip pruning
				// (this is only possible when attempting to prune off wrapping model element)
				list_prune (current_list, current_stack_len, skip_cnt, unpaired_cnt,
				            advanced_pair_track_id, containing_pair_track_id, track_id);
				#ifdef SEARCH_SEQ_DETAIL
				char msg[MAX_MSG_LEN + 1];
				
				for (uchar i = 0; i < indent; i++) {
					msg[i] = S_WHITE_SPACE[0];
				}
				
				for (uchar i = indent; i < indent + el->paired->min + pos_var; i++) {
					msg[i] = SS_NEUTRAL_OPEN_TERM;
				}
				
				for (uchar i = (uchar) (indent + el->paired->min + pos_var);
				     i < MAX_MODEL_STRING_LEN; i++) {
					msg[i] = S_WHITE_SPACE[0];
				}
				
				msg[MAX_MODEL_STRING_LEN] = '\0';
				COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
				char target_msg[MAX_MSG_LEN + 1];
				
				if (num_targets && *current_list) {
					check_bp_candidates_for_targets (*current_list, targets, num_targets,
					                                 target_msg, indent + el->paired->min + pos_var);
				}
				
				else {
					target_msg[0] = '\0';
				}
				
				sprintf (msg, "pruned T%d <%d,%d,%d> with %u candidates%s\n",
				         el->paired->min + pos_var,
				         track_id,
				         advanced_pair_track_id,
				         containing_pair_track_id,
				         *current_list ? (*current_list)->numels : 0,
				         target_msg);
				COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
				#endif
			}
			
			if (*current_list && ((*current_list)->numels)) {
				if (el->unpaired->i_constraint.reference) {
					free_constraint_links (el, current_list, advanced_pair_track_id);
					set_constraint_links (el, current_list, unpaired_cnt, skip_cnt, track_id,
					                      advanced_pair_track_id, false);
				}
				
				if (*matched_cnts) {
					list_destroy (*matched_cnts);
					FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
				}
				
				*matched_cnts = NULL;
				*matched_cnts = list_sum (updated_matched_cnts, unpaired_cnt, track_id);
				
				if (updated_matched_cnts != *matched_cnts) {
					list_destroy (updated_matched_cnts);
					FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
				}
				
				updated_matched_cnts = NULL;
			}
			
			else {
				if (*current_list) {
					list_destroy (*current_list);
					FREE_DEBUG (*current_list, "current_list in search_seq_at");
				}
				
				if (*matched_cnts) {
					list_destroy (*matched_cnts);
					FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
				}
				
				*current_list = NULL;
				*matched_cnts = NULL;
			}
		}
		
		if (updated_list && updated_list != *current_list) {
			list_destroy (updated_list);
			FREE_DEBUG (updated_list, "updated_list in search_seq_at");
		}
		
		if (updated_matched_cnts && updated_matched_cnts != *matched_cnts) {
			list_destroy (updated_matched_cnts);
			FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
		}
		
		return true;
	}
	
	else
	#ifndef NO_FULL_CHECKS
		if (el->type == paired)
	#endif
		{
			if (el->paired->min + pos_var > 0) {
				#ifdef SEARCH_SEQ_DETAIL
				char msg[MAX_MSG_LEN + 1];
				
				for (uchar i = 0; i < indent; i++) {
					msg[i] = S_WHITE_SPACE[0];
				}
				
				for (uchar i = indent; i < indent + el->paired->min + pos_var; i++) {
					msg[i] = SS_NEUTRAL_OPEN_TERM;
				}
				
				for (uchar i = (uchar) (indent + el->paired->min + pos_var);
				     i < MAX_MODEL_STRING_LEN; i++) {
					msg[i] = S_WHITE_SPACE[0];
				}
				
				msg[MAX_MODEL_STRING_LEN] = '\0';
				COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
				#endif
				
				if (!*current_list) {
					*current_list = MALLOC_DEBUG (sizeof (nt_list),
					                              "current_list in search_seq_at");
					#ifndef NO_FULL_CHECKS
					                              
					if (*current_list && (!
					#endif
					                      list_init (*current_list)
				                      #ifndef NO_FULL_CHECKS
					                     )) {
				                      #else
					                      ;
				                      #endif
						nt_stack_idist min_stack_dist, max_stack_dist;
						short this_in_extrusion;
						
						if (!get_stack_distances_in_paired_element (
						                        model, el,
						                        prev_el,
						                        &min_stack_dist, &max_stack_dist, &this_in_extrusion)) {
							COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
							              "cannot count stack distances in search_seq_at", false);
							              
							if (*current_list) {
								list_destroy (*current_list);
								FREE_DEBUG (*current_list, "current_list in search_seq_at");
							}
							
							if (*matched_cnts) {
								list_destroy (*matched_cnts);
								FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
							}
							
							*matched_cnts = NULL;
							*current_list = NULL;
							return false;
						}
						
						for (REGISTER nt_stack_idist this_stack_idist = min_stack_dist;
						     this_stack_idist <= max_stack_dist; this_stack_idist++) {
							list_iterator_start (&seq_bp->stacks[el->paired->min + pos_var - 1]);
							
							while (list_iterator_hasnext (&seq_bp->stacks[el->paired->min + pos_var - 1])) {
								REGISTER
								ntp_stack this_stack = list_iterator_next (&seq_bp->stacks[el->paired->min +
								                                                        pos_var - 1]);
								                                                        
								if (this_stack->stack_idist == this_stack_idist &&
								    this_stack->in_extrusion == this_in_extrusion) {
									REGISTER
									ntp_bp_list_by_element this_list_by_element = this_stack->lists;
									
									while (this_list_by_element) {
										if (this_list_by_element->el == el) {
											if (!ntp_list_insert (*current_list, &this_list_by_element->list,
											                      el->paired->min + pos_var, track_id)) {
												COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
												              "could not copy bps into current_list in search_seq_at", false);
												              
												if (*current_list) {
													list_destroy (*current_list);
													FREE_DEBUG (*current_list, "current_list in search_seq_at");
												}
												
												if (*matched_cnts) {
													list_destroy (*matched_cnts);
													FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
												}
												
												*matched_cnts = NULL;
												*current_list = NULL;
												return false;
											}
											
											break;
										}
										
										this_list_by_element = this_list_by_element->next;
									}
								}
							}
							
							list_iterator_stop (&seq_bp->stacks[el->paired->min + pos_var - 1]);
						}
						
						#ifdef SEARCH_SEQ_DETAIL
						char target_msg[MAX_MSG_LEN + 1];
						
						if (num_targets && *current_list) {
							check_bp_candidates_for_targets (*current_list, targets, num_targets,
							                                 target_msg, indent + el->paired->min + pos_var);
						}
						
						else {
							target_msg[0] = '\0';
						}
						
						sprintf (msg, "started on T%d <%d,%d,%d> with %u candidates%s\n",
						         el->paired->min + pos_var,
						         track_id,
						         advanced_pair_track_id,
						         containing_pair_track_id,
						         (*current_list)->numels,
						         target_msg);
						COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
						#endif
						
						if (! (*current_list)->numels) {
							COMMIT_DEBUG (REPORT_INFO, SEARCH_SEQ,
							              "no bps to copy into current_list in search_seq_at", false);
							list_destroy (*current_list);
							FREE_DEBUG (*current_list, "current_list in search_seq_at");
							
							if (*matched_cnts) {
								list_destroy (*matched_cnts);
								FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
							}
							
							*matched_cnts = NULL;
							*current_list = NULL;
							return true;
						}
						
						#ifndef NO_FULL_CHECKS
					}
					
					else {
						COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
						              "could not allocate memory/initialize current_list in search_seq_at", false);
						              
						if (*current_list) {
							list_destroy (*current_list);
							FREE_DEBUG (*current_list, "current_list in search_seq_at");
						}
						
						if (*matched_cnts) {
							list_destroy (*matched_cnts);
							FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
						}
						
						*matched_cnts = NULL;
						*current_list = NULL;
						return false;
					}
					
						#endif
				}
				
				else {
					nt_stack_idist min_stack_dist, max_stack_dist;
					short this_in_extrusion;
					
					if (!get_stack_distances_in_paired_element (model, el,
					                                        prev_el,
					                                        &min_stack_dist, &max_stack_dist, &this_in_extrusion)) {
						COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
						              "cannot count stack distances for list_advance in search_seq_at", false);
						              
						if (*current_list) {
							list_destroy (*current_list);
							FREE_DEBUG (*current_list, "current_list in search_seq_at");
						}
						
						if (*matched_cnts) {
							list_destroy (*matched_cnts);
							FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
						}
						
						*matched_cnts = NULL;
						*current_list = NULL;
						return false;
					}
					
					ntp_list advanced_list = MALLOC_DEBUG (sizeof (nt_list),
					                                       "advanced_list in search_seq_at");
					#ifndef NO_FULL_CHECKS
					                                       
					if ((!advanced_list) || (
					#endif
					                        list_init (advanced_list)
				                        #ifndef NO_FULL_CHECKS
					                        < 0)) {
						COMMIT_DEBUG (REPORT_ERRORS, LIST,
						              "could not allocate memory for/initialize advanced_list for list_advance in search_seq_at",
						              false);
						              
						if (*current_list) {
							list_destroy (*current_list);
							FREE_DEBUG (*current_list, "current_list in search_seq_at");
						}
						
						if (*matched_cnts) {
							list_destroy (*matched_cnts);
							FREE_DEBUG (*current_list, "matched_cnts in search_seq_at");
						}
						
						*matched_cnts = NULL;
						*current_list = NULL;
						return false;
					}
					
				                        #else
					                        ;
				                        #endif
					
					for (REGISTER nt_stack_idist this_stack_idist = min_stack_dist;
					     this_stack_idist <= max_stack_dist; this_stack_idist++) {
						list_iterator_start (&seq_bp->stacks[el->paired->min + pos_var - 1]);
						
						while (list_iterator_hasnext (&seq_bp->stacks[el->paired->min + pos_var - 1])) {
							REGISTER
							ntp_stack this_stack = list_iterator_next (&seq_bp->stacks[el->paired->min +
							                                                        pos_var - 1]);
							                                                        
							if (this_stack->stack_idist == this_stack_idist &&
							    this_stack->in_extrusion == this_in_extrusion) {
								REGISTER
								bool found_list_by_element = false;
								REGISTER
								ntp_list this_list = NULL;
								REGISTER
								ntp_bp_list_by_element this_list_by_element = this_stack->lists;
								
								while (this_list_by_element) {
									if (this_list_by_element->el == el) {
										this_list = &this_list_by_element->list;
										found_list_by_element = true;
										break;
									}
									
									this_list_by_element = this_list_by_element->next;
								}
								
								if (found_list_by_element) {
									REGISTER
									bool success = list_advance (*current_list,
									                             current_stack_len,
									                             match_cnt == 0 ? (nt_rel_count) 0 : (match_cnt + current_stack_len),
									                             skip_cnt,
									                             this_list,
									                             &advanced_list,
									                             el->paired->min + pos_var,
									                             track_id,
									                             advanced_pair_track_id,
									                             containing_pair_track_id);
									                             
									if (!success) {
										COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
										              "could not advance current_list in search_seq_at", false);
										list_iterator_stop (&seq_bp->stacks[el->paired->min + pos_var - 1]);
										
										if (*current_list) {
											list_destroy (*current_list);
											FREE_DEBUG (*current_list, "current_list in search_seq_at");
										}
										
										if (*matched_cnts) {
											list_destroy (*matched_cnts);
											FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
										}
										
										if (this_list) {
											list_destroy (this_list);
											FREE_DEBUG (this_list, "this_list in search_seq_at");
										}
										
										*matched_cnts = NULL;
										*current_list = NULL;
										return false;
									}
									
									break;
								}
							}
						}
						
						list_iterator_stop (&seq_bp->stacks[el->paired->min + pos_var - 1]);
					}
					
					if (*current_list) {
						list_destroy (*current_list);
						FREE_DEBUG (*current_list, "current_list in search_seq_at");
					}
					
					*current_list = advanced_list;
					#ifdef SEARCH_SEQ_DETAIL
					char target_msg[MAX_MSG_LEN + 1];
					
					if (num_targets && *current_list) {
						check_bp_candidates_for_targets (*current_list, targets, num_targets,
						                                 target_msg, indent + el->paired->min + pos_var);
					}
					
					else {
						target_msg[0] = '\0';
					}
					
					sprintf (msg, "advanced to T%d <%d,%d,%d> with %u candidates%s\n",
					         el->paired->min + pos_var,
					         track_id,
					         advanced_pair_track_id,
					         containing_pair_track_id,
					         *current_list ? (*current_list)->numels : 0,
					         target_msg);
					COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
					#endif
					
					if (!*current_list || (! (*current_list)->numels)) {
						if (*current_list) {
							list_destroy (*current_list);
							FREE_DEBUG (*current_list, "current_list in search_seq_at");
						}
						
						if (*matched_cnts) {
							list_destroy (*matched_cnts);
							FREE_DEBUG (*matched_cnts, "*matched_cnts in search_seq_at");
						}
						
						*matched_cnts = NULL;
						*current_list = NULL;
						return true;
					}
				}
				
				current_stack_len = el->paired->min + pos_var;
			}
			
			else {
				/*
				 * construct a "wrapper" bp
				 */
				if (!el->paired->max && !pos_var && !*current_list) {
					*current_list = MALLOC_DEBUG (sizeof (nt_list),
					                              "current_list in search_seq_at");
					#ifndef NO_FULL_CHECKS
					                              
					if (*current_list && (!
					#endif
					                      list_init (*current_list)
				                      #ifndef NO_FULL_CHECKS
					                     )) {
				                      #else
					                      ;
				                      #endif
						REGISTER
						ntp_linked_bp restrict linked_bp = MALLOC_TAG (sizeof (nt_linked_bp), track_id);
						REGISTER
						ntp_bp wrapper_bp;
						
						if (linked_bp) {
							wrapper_bp = MALLOC_TAG (sizeof (nt_bp), track_id);
							
							if (wrapper_bp) {
								wrapper_bp->fp_posn = 0;
								wrapper_bp->tp_posn = 0;
								linked_bp->bp = wrapper_bp;
								linked_bp->stack_len = 0;
								linked_bp->track_id = track_id;
								linked_bp->fp_elements = NULL;
								linked_bp->tp_elements = NULL;
								linked_bp->prev_linked_bp = NULL;
							}
						}
						
						#ifndef NO_FULL_CHECKS
						
						if (!linked_bp || !wrapper_bp || 0 >
						#endif
						    list_append (*current_list, linked_bp)
					    #ifndef NO_FULL_CHECKS
						   ) {
							COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
							              "could not set up wrapper bp for current_list in search_seq_at", false);
						}
						
						if (*current_list) {
							list_destroy (*current_list);
							FREE_DEBUG (*current_list, "current_list in search_seq_at");
						}
						
						if (*matched_cnts) {
							list_destroy (*matched_cnts);
							FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
						}
						
						*matched_cnts = NULL;
						*current_list = NULL;
						return false;
					}
					
					    #else
						    ;
					    #endif
					#ifdef SEARCH_SEQ_DETAIL
					char msg[MAX_MSG_LEN + 1];
					sprintf (msg, "started on T0 <%d,%d,%d> with %u candidates\n",
					         track_id,
					         advanced_pair_track_id,
					         containing_pair_track_id,
					         (*current_list)->numels);
					COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
					#endif
					#ifndef NO_FULL_CHECKS
				}
				
				else {
					COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
					              "could not allocate memory/initialize current_list in search_seq_at", false);
					              
					if (*current_list) {
						list_destroy (*current_list);
						FREE_DEBUG (*current_list, "current_list in search_seq_at");
					}
					
					if (*matched_cnts) {
						list_destroy (*current_list);
						FREE_DEBUG (*current_list, "current_list in search_seq_at");
					}
					
					*matched_cnts = NULL;
					*current_list = NULL;
					return false;
				}
				
					#endif
			}
			
			/*
			 * cannot process an 0-sized helix; return NULL list
			 */
			else {
				if (*current_list) {
					list_destroy (*current_list);
					FREE_DEBUG (*current_list, "current_list in search_seq_at");
				}
				
				if (*matched_cnts) {
					list_destroy (*matched_cnts);
					FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
				}
				
				*matched_cnts = NULL;
				*current_list = NULL;
				return true;
			}
		}
		
	advanced_pair_track_id = track_id;
	
	ntp_list restrict updated_matched_cnts =
	                    NULL;              // accumulate number of nts covered by 5' and/or 3' segments
	                    
	#ifndef NO_FULL_CHECKS
	                    
	if (!
	#endif
	
	    ntp_list_alloc_debug (&updated_matched_cnts,
	                          "updated_matched_cnts in search_seq_at")
	                          
    #ifndef NO_FULL_CHECKS
	   ) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "cannot alloc updated_matched_cnts in search_seq_at", false);
		              
		if (*current_list) {
			list_destroy (*current_list);
			FREE_DEBUG (*current_list, "current_list in search_seq_at");
		}
		
		if (*matched_cnts) {
			list_destroy (*matched_cnts);
			FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
		}
		
		*matched_cnts = NULL;
		*current_list = NULL;
		return false;
	}
	
    #else
	    ;
    #endif
	
	REGISTER
	ntp_element paired_fp_next = el->paired->fp_next;
	
	// locally updated list of nt_linked_bps
	ntp_list restrict updated_list = NULL;
	
	if (paired_fp_next) {
		REGISTER
		char this_pos_var = get_element_pos_var_range (paired_fp_next);
		
		do {
			ntp_list restrict this_matched_cnts = NULL;
			#ifndef NO_FULL_CHECKS
			
			if (!
			#endif
			    ntp_list_alloc_debug (&this_matched_cnts, "this_matched_cnts in search_seq_at")
		    #ifndef NO_FULL_CHECKS
			   ) {
				COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
				              "cannot alloc this_matched_cnts in search_seq_at", false);
				              
				if (*current_list) {
					list_destroy (*current_list);
					FREE_DEBUG (*current_list, "current_list in search_seq_at");
				}
				
				if (*matched_cnts) {
					list_destroy (*matched_cnts);
					FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
				}
				
				if (updated_matched_cnts) {
					list_destroy (updated_matched_cnts);
					FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
				}
				
				if (updated_list) {
					list_destroy (updated_list);
					FREE_DEBUG (updated_list, "updated_list in search_seq_at");
				}
				
				*matched_cnts = NULL;
				*current_list = NULL;
				return false;
			}
			
		    #else
			    ;
		    #endif
			ntp_list restrict this_list = duplicate_list_shallow (*current_list,
			                                        "this_list duplicate of current_list in search_seq_at");
			#ifdef SEARCH_SEQ_DETAIL
			char msg[MAX_MSG_LEN + 1];
			
			for (uchar i = 0; i < indent; i++) {
				msg[i] = S_WHITE_SPACE[0];
			}
			
			for (uchar i = indent; i < indent + el->paired->min + pos_var; i++) {
				msg[i] = SS_NEUTRAL_OPEN_TERM;
			}
			
			for (uchar i = (uchar) (indent + el->paired->min + pos_var);
			     i < MAX_MODEL_STRING_LEN; i++) {
				msg[i] = S_WHITE_SPACE[0];
			}
			
			msg[MAX_MODEL_STRING_LEN] = '\0';
			COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
			sprintf (msg, "doing fp of T%d <%d,%d,%d> using pos_var %d\n",
			         el->paired->min + pos_var,
			         track_id,
			         advanced_pair_track_id,
			         containing_pair_track_id,
			         this_pos_var);
			COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
			#endif
			
			if (search_seq_at (model,
			                   seq,
			                   seq_bp,
			                   paired_fp_next,
			                   el,
			                   &this_list,
			                   current_stack_len,
			                   0,
			                   0,
			                   (uchar) (track_id + 1),
			                   (advanced_pair_track_id),
			                   track_id,
			                   (uchar) (this_pos_var),
			                   NULL,
			                   &this_matched_cnts
		                   #ifdef SEARCH_SEQ_DETAIL
			                   , indent + el->paired->min + pos_var, targets, num_targets
		                   #endif
			                  )) {
				if (this_list && this_list->numels) {
					ntp_list previously_updated_list = updated_list;
					updated_list = ntp_list_concatenate (updated_list, this_list, track_id);
					
					if (previously_updated_list) {
						list_destroy (previously_updated_list);
						FREE_DEBUG (previously_updated_list,
						            "previously updated_list in search_seq_at");
					}
					
					if (this_list) {
						list_destroy (this_list);
						FREE_DEBUG (this_list, "this_list in search_seq_at");
						this_list = NULL;
					}
					
					ntp_list previously_updated_matched_cnts = updated_matched_cnts;
					updated_matched_cnts = ntp_count_list_concatenate (updated_matched_cnts,
					                                        this_matched_cnts);
					                                        
					if (previously_updated_matched_cnts &&
					    previously_updated_matched_cnts != updated_matched_cnts) {
						list_destroy (previously_updated_matched_cnts);
						FREE_DEBUG (previously_updated_matched_cnts,
						            "previously updated_matched_cnts in search_seq_at");
					}
					
					if (this_matched_cnts && this_matched_cnts != updated_matched_cnts) {
						list_destroy (this_matched_cnts);
						FREE_DEBUG (this_matched_cnts, "this_matched_cnts in search_seq_at");
					}
					
					this_matched_cnts = NULL;
				}
			}
			
			else {
				if (*current_list) {
					list_destroy (*current_list);
					FREE_DEBUG (*current_list, "current_list in search_seq_at");
				}
				
				if (*matched_cnts) {
					list_destroy (*matched_cnts);
					FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
				}
				
				if (this_matched_cnts) {
					list_destroy (this_matched_cnts);
					FREE_DEBUG (this_matched_cnts, "this_matched_cnts in search_seq_at");
				}
				
				if (updated_matched_cnts) {
					list_destroy (updated_matched_cnts);
					FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
				}
				
				if (updated_list) {
					list_destroy (updated_list);
					FREE_DEBUG (updated_list, "updated_list in search_seq_at");
				}
				
				if (this_list) {
					list_destroy (this_list);
					FREE_DEBUG (this_list, "this_list in search_seq_at");
				}
				
				*matched_cnts = NULL;
				*current_list = NULL;
				return false;
			}
			
			this_pos_var--;
		}
		while (this_pos_var >= 0);
		
		#ifdef SEARCH_SEQ_DETAIL
		char msg[MAX_MSG_LEN + 1];
		
		for (uchar i = 0; i < indent; i++) {
			msg[i] = S_WHITE_SPACE[0];
		}
		
		for (uchar i = indent; i < indent + el->paired->min + pos_var; i++) {
			msg[i] = SS_NEUTRAL_OPEN_TERM;
		}
		
		for (uchar i = (uchar) (indent + el->paired->min + pos_var);
		     i < MAX_MODEL_STRING_LEN; i++) {
			msg[i] = S_WHITE_SPACE[0];
		}
		
		msg[MAX_MODEL_STRING_LEN] = '\0';
		COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
		char target_msg[MAX_MSG_LEN + 1];
		
		if (num_targets && updated_list) {
			check_bp_candidates_for_targets (updated_list, targets, num_targets, target_msg,
			                                 indent + el->paired->min + pos_var);
		}
		
		else {
			target_msg[0] = '\0';
		}
		
		sprintf (msg, "finished fp of T%d <%d,%d,%d> with %u candidates%s\n",
		         el->paired->min + pos_var,
		         track_id,
		         advanced_pair_track_id,
		         containing_pair_track_id,
		         updated_list ? (updated_list)->numels : 0,
		         target_msg);
		COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
		#endif
	}
	
	if (!updated_list || (! (updated_list)->numels)) {
		if (*current_list) {
			list_destroy (*current_list);
			FREE_DEBUG (*current_list, "current_list in search_seq_at");
		}
		
		if (*matched_cnts) {
			list_destroy (*matched_cnts);
			FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
		}
		
		if (updated_matched_cnts) {
			list_destroy (updated_matched_cnts);
			FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
		}
		
		if (updated_list) {
			list_destroy (updated_list);
			FREE_DEBUG (updated_list, "updated_list in search_seq_at");
		}
		
		*matched_cnts = NULL;
		*current_list = NULL;
		return true;
	}
	
	REGISTER
	ntp_element paired_tp_next = el->paired->tp_next;
	
	if (*current_list) {
		list_destroy (*current_list);
		FREE_DEBUG (*current_list, "current_list in search_seq_at");
	}
	
	// for tp need to set current_list to updated_list, and current_*_cnts to updated_*_cnts
	*current_list = updated_list;
	updated_list = NULL;
	
	REGISTER
	ntp_list current_matched_cnts = updated_matched_cnts;
	
	updated_matched_cnts = NULL;
	
	if (paired_tp_next) {
		REGISTER
		char this_pos_var = get_element_pos_var_range (paired_tp_next);
		
		do {
			ntp_list restrict this_matched_cnts = NULL;
			#ifndef NO_FULL_CHECKS
			
			if (!
			#endif
			    ntp_list_alloc_debug (&this_matched_cnts, "this_matched_cnts in search_seq_at")
		    #ifndef NO_FULL_CHECKS
			   ) {
				COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
				              "cannot alloc this_matched_cnts in search_seq_at", false);
				              
				if (*current_list) {
					list_destroy (*current_list);
					FREE_DEBUG (*current_list, "current_list in search_seq_at");
				}
				
				if (*matched_cnts) {
					list_destroy (*matched_cnts);
					FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
				}
				
				if (updated_matched_cnts) {
					list_destroy (updated_matched_cnts);
					FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
				}
				
				if (updated_list) {
					list_destroy (updated_list);
					FREE_DEBUG (updated_list, "updated_list in search_seq_at");
				}
				
				*matched_cnts = NULL;
				*current_list = NULL;
				return false;
			}
			
		    #else
			    ;
		    #endif
			#ifndef NO_FULL_CHECKS
			
			if (
			#endif
			                    list_iterator_start (current_matched_cnts)
		                    #ifndef NO_FULL_CHECKS
			                    == 0) {
				COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
				              "cannot iterate current_matched_cnts in search_seq_at", false);
				              
				if (*current_list) {
					list_destroy (*current_list);
					FREE_DEBUG (*current_list, "current_list in search_seq_at");
				}
				
				if (*matched_cnts) {
					list_destroy (*matched_cnts);
					FREE_DEBUG (*current_list, "matched_cnts in search_seq_at");
				}
				
				if (this_matched_cnts) {
					list_destroy (this_matched_cnts);
					FREE_DEBUG (this_matched_cnts, "this_matched_cnts in search_seq_at");
				}
				
				if (updated_matched_cnts) {
					list_destroy (updated_matched_cnts);
					FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
				}
				
				if (updated_list) {
					list_destroy (updated_list);
					FREE_DEBUG (updated_list, "updated_list in search_seq_at");
				}
				
				*matched_cnts = NULL;
				*current_list = NULL;
				return false;
			}
			
		                    #else
			                    ;
		                    #endif
			
			while (list_iterator_hasnext (current_matched_cnts)) {
				REGISTER
				nt_rel_count current_matched_cnt = (* (nt_rel_count *) list_iterator_next (
				                                        current_matched_cnts));
				ntp_list restrict this_list = duplicate_list_shallow (*current_list,
				                                        "this_list duplicate of current_list in search_seq_at");
				#ifdef SEARCH_SEQ_DETAIL
				char msg[MAX_MSG_LEN + 1];
				
				for (uchar i = 0; i < indent; i++) {
					msg[i] = S_WHITE_SPACE[0];
				}
				
				for (uchar i = indent; i < indent + el->paired->min + pos_var; i++) {
					msg[i] = SS_NEUTRAL_OPEN_TERM;
				}
				
				for (uchar i = (uchar) (indent + el->paired->min + pos_var);
				     i < MAX_MODEL_STRING_LEN; i++) {
					msg[i] = S_WHITE_SPACE[0];
				}
				
				msg[MAX_MODEL_STRING_LEN] = '\0';
				COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
				sprintf (msg,
				         "doing tp of T%d <%d,%d,%d> using pos_var %d, current_matched_cnt %u\n",
				         el->paired->min + pos_var,
				         track_id,
				         advanced_pair_track_id,
				         containing_pair_track_id,
				         this_pos_var,
				         current_matched_cnt);
				COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
				#endif
				
				if (search_seq_at (model,
				                   seq,
				                   seq_bp,
				                   paired_tp_next,
				                   el,
				                   &this_list,
				                   current_stack_len,
				                   current_matched_cnt,
				                   0,
				                   (uchar) (track_id + 1),
				                   (advanced_pair_track_id),
				                   (containing_pair_track_id),
				                   (uchar) (this_pos_var),
				                   // if not NULL, pass on continuation_element to the next element, so that a
				                   // terminal element can eventually process it
				                   continuation_element,
				                   &this_matched_cnts
			                   #ifdef SEARCH_SEQ_DETAIL
				                   , (uchar) (indent + current_matched_cnt + el->paired->min + pos_var), targets,
				                   num_targets
			                   #endif
				                  )) {
					if (this_list && this_list->numels) {
						ntp_list previously_updated_list = updated_list;
						updated_list = ntp_list_concatenate (updated_list, this_list, track_id);
						
						if (previously_updated_list) {
							list_destroy (previously_updated_list);
							FREE_DEBUG (previously_updated_list,
							            "previously updated_list in search_seq_at");
						}
						
						if (this_list) {
							list_destroy (this_list);
							FREE_DEBUG (this_list, "this_list in search_seq_at");
							this_list = NULL;
						}
						
						ntp_list previously_updated_matched_cnts = updated_matched_cnts;
						updated_matched_cnts = ntp_count_list_concatenate (updated_matched_cnts,
						                                        this_matched_cnts);
						                                        
						if (previously_updated_matched_cnts &&
						    previously_updated_matched_cnts != updated_matched_cnts) {
							list_destroy (previously_updated_matched_cnts);
							FREE_DEBUG (previously_updated_matched_cnts,
							            "previously updated_matched_cnts in search_seq_at");
						}
						
						if (this_matched_cnts && this_matched_cnts != updated_matched_cnts) {
							list_destroy (this_matched_cnts);
							FREE_DEBUG (this_matched_cnts, "this_matched_cnts in search_seq_at");
						}
						
						this_matched_cnts = NULL;
					}
				}
				
				else {
					if (*current_list) {
						list_destroy (*current_list);
						FREE_DEBUG (*current_list, "current_list in search_seq_at");
					}
					
					if (*matched_cnts) {
						list_destroy (*matched_cnts);
						FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
					}
					
					if (this_matched_cnts) {
						list_destroy (this_matched_cnts);
						FREE_DEBUG (this_matched_cnts, "this_matched_cnts in search_seq_at");
					}
					
					if (updated_matched_cnts) {
						list_destroy (updated_matched_cnts);
						FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
					}
					
					if (updated_list) {
						list_destroy (updated_list);
						FREE_DEBUG (updated_list, "updated_list in search_seq_at");
					}
					
					if (this_list) {
						list_destroy (this_list);
						FREE_DEBUG (this_list, "this_list in search_seq_at");
					}
					
					*matched_cnts = NULL;
					*current_list = NULL;
					list_iterator_stop (current_matched_cnts);
					list_destroy (current_matched_cnts);
					FREE_DEBUG (current_matched_cnts, "current_matched_cnts in search_seq_at");
					return false;
				}
			}
			
			list_iterator_stop (current_matched_cnts);
			this_pos_var--;
		}
		while (this_pos_var >= 0);
		
		if (*current_list) {
			list_destroy (*current_list);
			FREE_DEBUG (*current_list, "current_list in search_seq_at");
		}
		
		*current_list = updated_list;
		#ifdef SEARCH_SEQ_DETAIL
		char msg[MAX_MSG_LEN + 1];
		
		for (uchar i = 0; i < indent; i++) {
			msg[i] = S_WHITE_SPACE[0];
		}
		
		for (uchar i = indent; i < indent + el->paired->min + pos_var; i++) {
			msg[i] = SS_NEUTRAL_OPEN_TERM;
		}
		
		for (uchar i = (uchar) (indent + el->paired->min + pos_var);
		     i < MAX_MODEL_STRING_LEN; i++) {
			msg[i] = S_WHITE_SPACE[0];
		}
		
		msg[MAX_MODEL_STRING_LEN] = '\0';
		COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
		char target_msg[MAX_MSG_LEN + 1];
		
		if (num_targets && *current_list) {
			check_bp_candidates_for_targets (*current_list, targets, num_targets,
			                                 target_msg, indent + el->paired->min + pos_var);
		}
		
		else {
			target_msg[0] = '\0';
		}
		
		sprintf (msg, "finished tp of T%d <%d,%d,%d> with %u candidates%s\n",
		         el->paired->min + pos_var,
		         track_id,
		         advanced_pair_track_id,
		         containing_pair_track_id,
		         *current_list ? (*current_list)->numels : 0,
		         target_msg);
		COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
		#endif
		
		if (updated_list && (updated_list)->numels) {
			ntp_list previously_matched_cnts = current_matched_cnts;
			current_matched_cnts = list_sum_combine (current_matched_cnts,
			                                        updated_matched_cnts, track_id);
			                                        
			if (previously_matched_cnts &&
			    previously_matched_cnts != current_matched_cnts) {
				list_destroy (previously_matched_cnts);
				FREE_DEBUG (previously_matched_cnts,
				            "previously current_matched_cnts in search_seq_at");
			}
			
			if (updated_matched_cnts && updated_matched_cnts != current_matched_cnts) {
				list_destroy (updated_matched_cnts);
				FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
			}
			
			updated_matched_cnts = NULL;
		}
		
		else {
			if (*current_list) {
				list_destroy (*current_list);
				FREE_DEBUG (*current_list, "current_list in search_seq_at");
			}
			
			if (*matched_cnts) {
				list_destroy (*matched_cnts);
				FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
			}
			
			if (updated_matched_cnts) {
				list_destroy (updated_matched_cnts);
				FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
			}
			
			if (updated_list) {
				list_destroy (updated_list);
				FREE_DEBUG (updated_list, "updated_list in search_seq_at");
			}
			
			if (current_matched_cnts) {
				list_destroy (current_matched_cnts);
				FREE_DEBUG (current_matched_cnts, "current_matched_cnts in search_seq_at");
			}
			
			*current_list = NULL;
			*matched_cnts = NULL;
			return true;
		}
		
		updated_list = NULL;
	}
	
	else {
		// TODO: error checking
		if (continuation_element == NULL &&
		    advanced_pair_track_id >= 0 && containing_pair_track_id >= 0 && *current_list &&
		    ((*current_list)->numels)) {
			list_prune (current_list, current_stack_len, 0, 0, advanced_pair_track_id,
			            containing_pair_track_id, track_id);
			#ifdef SEARCH_SEQ_DETAIL
			char msg[MAX_MSG_LEN + 1];
			
			for (uchar i = 0; i < indent; i++) {
				msg[i] = S_WHITE_SPACE[0];
			}
			
			for (uchar i = indent; i < indent + el->paired->min + pos_var; i++) {
				msg[i] = SS_NEUTRAL_OPEN_TERM;
			}
			
			for (uchar i = (uchar) (indent + el->paired->min + pos_var);
			     i < MAX_MODEL_STRING_LEN; i++) {
				msg[i] = S_WHITE_SPACE[0];
			}
			
			msg[MAX_MODEL_STRING_LEN] = '\0';
			COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
			char target_msg[MAX_MSG_LEN + 1];
			
			if (num_targets && *current_list) {
				check_bp_candidates_for_targets (*current_list, targets, num_targets,
				                                 target_msg, indent + el->paired->min + pos_var);
			}
			
			else {
				target_msg[0] = '\0';
			}
			
			sprintf (msg, "pruned T%d <%d,%d,%d> with %u candidates%s\n",
			         el->paired->min + pos_var,
			         track_id,
			         advanced_pair_track_id,
			         containing_pair_track_id,
			         *current_list ? (*current_list)->numels : 0,
			         target_msg);
			COMMIT_DEBUG_NNL (REPORT_INFO, SEARCH_SEQ, msg, false);
			#endif
			
			if (!*current_list || ! (*current_list)->numels) {
				if (*current_list) {
					list_destroy (*current_list);
					FREE_DEBUG (*current_list, "current_list in search_seq_at");
				}
				
				if (*matched_cnts) {
					list_destroy (*matched_cnts);
					FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
				}
				
				if (updated_matched_cnts) {
					list_destroy (updated_matched_cnts);
					FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
				}
				
				if (updated_list) {
					list_destroy (updated_list);
					FREE_DEBUG (updated_list, "updated_list in search_seq_at");
				}
				
				if (current_matched_cnts) {
					list_destroy (current_matched_cnts);
					FREE_DEBUG (current_matched_cnts, "current_matched_cnts in search_seq_at");
				}
				
				*current_list = NULL;
				*matched_cnts = NULL;
				return true;
			}
		}
	}
	
	if (*matched_cnts) {
		list_destroy (*matched_cnts);
		FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
	}
	
	*matched_cnts = list_sum (current_matched_cnts,
	                          (nt_rel_count) (current_stack_len * 2), track_id);
	                          
	if (current_matched_cnts != *matched_cnts) {
		list_destroy (current_matched_cnts);
		FREE_DEBUG (current_matched_cnts, "current_matched_cnts in search_seq_at");
	}
	
	if (updated_matched_cnts && updated_matched_cnts != *matched_cnts) {
		list_destroy (updated_matched_cnts);
		FREE_DEBUG (updated_matched_cnts, "updated_matched_cnts in search_seq_at");
	}
	
	if (updated_list && updated_list != *current_list) {
		list_destroy (updated_list);
		FREE_DEBUG (updated_list, "updated_list in search_seq_at");
	}
	
	return true;
}
#ifndef NO_FULL_CHECKS
else {
	COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
	              "nt_element type 'no_element_type' is not supported in search_seq_at", false);
	              
	if (*current_list) {
		list_destroy (*current_list);
		FREE_DEBUG (*current_list, "current_list in search_seq_at");
	}
	
	if (*matched_cnts) {
		list_destroy (*matched_cnts);
		FREE_DEBUG (*matched_cnts, "matched_cnts in search_seq_at");
	}
	
	*matched_cnts = NULL;
	*current_list = NULL;
	return false;
}

#endif
}

static inline bool get_model_bp_span (ntp_element restrict el,
                                      ntp_rel_count restrict span, const bool is_top_level,
                                      const bool already_have_paired) {
	#ifndef NO_FULL_CHECKS
                                      
	if (!el || !span) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "found NULL el or span while getting model span in search_seq", false);
		              
		if (span) {
			*span = 0;
		}
		
		return false;
	}
	
	#endif
	
	if (el->type == paired) {
		nt_rel_count this_span = 0;
		#ifndef NO_FULL_CHECKS
		
		if (el->paired->fp_next) {
		#endif
		
			if (!get_model_bp_span (el->paired->fp_next, &this_span, false, true)) {
				*span = 0;
				return false;
			}
			
			#ifndef NO_FULL_CHECKS
		}
		
		else {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "found NULL fp_next for el paired type while getting model span in search_seq",
			              false);
			*span = 0;
			return false;
		}
		
			#endif
		
		if (el->paired->tp_next) {
			nt_rel_count that_span;
			
			if (!get_model_bp_span (el->paired->tp_next, &that_span, is_top_level, true)) {
				*span = 0;
				return false;
			}
			
			this_span += that_span;
		}
		
		*span = (nt_rel_count) ((el->paired->max * 2) + this_span);
		return true;
	}
	
	else
	#ifndef NO_FULL_CHECKS
		if (el->type == unpaired)
	#endif
		{
			nt_rel_count that_span = 0;
			
			if (el->unpaired->next) {
				if (!get_model_bp_span (el->unpaired->next, &that_span, is_top_level,
				                        already_have_paired)) {
					*span = 0;
					return false;
				}
			}
			
			// do not count any unstructured residues at the 'top-level', as they do not
			// influence the span of bps that need to be captured (including if tertiary contacts)
			if (is_top_level && !that_span) {
				*span = 0;
			}
			
			else {
				if (already_have_paired) {
					*span = el->unpaired->max + that_span;
				}
				
				else {
					// do not include this unpaired element if we have not yet already encountered any pairs
					*span = that_span;
				}
			}
			
			return true;
		}
		
	#ifndef NO_FULL_CHECKS
		
		else {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "el type is neither paired nor unpaired while getting model span in search_seq",
			              false);
			*span = 0;
			return false;
		}
		
	#endif
}

/*
 * search sequence against model
 *
 * input:   sequence hash sequence_hash
 *          model model
 *
 * output:  ntp_list of ntp_linked_bps matching sequence and model
 */
ntp_list search_seq (const ntp_seq restrict seq, nt_model *restrict model,
                     float *elapsed_time
#ifdef SEARCH_SEQ_DETAIL
	, ntp_bp targets, nt_hit_count num_targets
#endif
                    ) {
	COMMIT_DEBUG (REPORT_INFO, SEARCH_SEQ,
	              "searching seq against model in search_seq", true);
	*elapsed_time = 0.0f;
	reset_timer();
	// keep a safe_copy of hits found (see found_list) before finally invoking list destruction after each search iteration
	ntp_list safe_copy = NULL;
	#ifndef NO_FULL_CHECKS
	
	if (!seq)                   {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ, "search seq is NULL in search_seq",
		              false);
		return NULL;
	}
	
	if (!seq_bp_cache)          {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "seq_bp_cache not initialized in search_seq", false);
		return NULL;
	}
	
	if (!strlen (seq))          {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "search seq has 0 length in search_seq", false);
		return NULL;
	}
	
	if (!model)                 {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ, "search model is NULL in search_seq",
		              false);
		return NULL;
	}
	
	if (!model->first_element)  {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "search model has no elements in search_seq", false);
		return NULL;
	}
	
	#endif
	#ifdef MULTITHREADED_ON
	
	if (pthread_mutex_lock (&num_destruction_threads_mutex) == 0) {
	#endif
	
		if (search_seq_list) {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "search_seq_list already initialized in search_seq", false);
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			#endif
			return NULL;
		}
		
		#ifdef MULTITHREADED_ON
		pthread_mutex_unlock (&num_destruction_threads_mutex);
	}
	
		#endif
	REGISTER
	nt_seq_hash seq_hash = get_seq_hash (seq);
	/*
	 * procure and optimize sequence bps
	 */
	REGISTER
	ntp_list *min_stack_dist, *max_stack_dist, *in_extrusion, *dist_els;
	// TODO: error handling
	min_stack_dist = MALLOC_DEBUG (sizeof (ntp_list *)*MAX_STACK_LEN,
	                               "min_stack_dist in search_seq");
	max_stack_dist = MALLOC_DEBUG (sizeof (ntp_list *)*MAX_STACK_LEN,
	                               "max_stack_dist in search_seq");
	in_extrusion = MALLOC_DEBUG (sizeof (ntp_list *)*MAX_STACK_LEN,
	                             "in_extrusion in search_seq");
	dist_els = MALLOC_DEBUG (sizeof (ntp_list *)*MAX_STACK_LEN,
	                         "dist_els in search_seq");
	                         
	for (REGISTER nt_stack_size i = 0; i < MAX_STACK_LEN; i++) {
		min_stack_dist[i] = MALLOC_DEBUG (sizeof (nt_list),
		                                  "nt_list for min_stack_dist in search_seq");
		list_init (min_stack_dist[i]);
		max_stack_dist[i] = MALLOC_DEBUG (sizeof (nt_list),
		                                  "nt_list for max_stack_dist in search_seq");
		list_init (max_stack_dist[i]);
		in_extrusion[i] = MALLOC_DEBUG (sizeof (nt_list),
		                                "nt_list for in_extrusion in search_seq");
		list_init (in_extrusion[i]);
		dist_els[i] = MALLOC_DEBUG (sizeof (nt_list),
		                            "nt_list for dist_els in search_seq");
		list_init (dist_els[i]);
	}
	
	if (!get_stack_distances (model, model->first_element, min_stack_dist,
	                          max_stack_dist, in_extrusion, dist_els)) {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
		              "cannot count stack distances in search_seq", false);
		list_destroy_all_tagged();
		destroy_min_max_dist (min_stack_dist, max_stack_dist, in_extrusion, dist_els);
		return NULL;
	}
	
	ntp_seq_bp seq_bp = NULL;
	REGISTER
	bool cache_success = get_seq_bp_from_cache (seq, seq_hash, model,
	                                        min_stack_dist, max_stack_dist, in_extrusion, dist_els, &seq_bp);
	                                        
	if (!seq_bp || !cache_success) {
		COMMIT_DEBUG1 (REPORT_INFO, SEARCH_SEQ,
		               "seq (hash %lu) not found in cache, build from nt and write to cache required in search_seq",
		               seq_hash, false);
		               
		if (!is_seq_valid (seq)) {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ, "failed to validate seq", false);
			list_destroy_all_tagged();
			destroy_min_max_dist (min_stack_dist, max_stack_dist, in_extrusion, dist_els);
			return NULL;
		}
		
		REGISTER
		bool to_cache =
		                    !seq_bp; // only add to cache when no seq_bp data exists (seq_bp==NULL)
		                    
		if (!get_seq_bp_from_seq (seq, model, min_stack_dist, max_stack_dist,
		                          in_extrusion, dist_els, &seq_bp)) {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "failed to build from nt in search_seq", false);
			list_destroy_all_tagged();
			destroy_min_max_dist (min_stack_dist, max_stack_dist, in_extrusion, dist_els);
			return NULL;
		}
		
		optimize_seq_bp (seq_bp);
		COMMIT_DEBUG1 (REPORT_INFO, SEARCH_SEQ,
		               "seq (hash %lu) built from nt in search_seq", seq_hash, false);
		               
		if (to_cache) {
			REGISTER
			nt_seq_count seq_count = add_seq_bp_to_cache (seq, seq_hash, seq_bp);
			
			if (!seq_count) {
				COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
				              "failed to add seq to cache in search_seq", false);
				list_destroy_all_tagged();
				destroy_min_max_dist (min_stack_dist, max_stack_dist, in_extrusion, dist_els);
				return NULL;
			}
			
			COMMIT_DEBUG1 (REPORT_INFO, SEARCH_SEQ,
			               "seq (hash %lu) written to cache in search_seq", seq_hash, false);
		}
	}
	
	/*
	 * estimate model complexity (in search space terms) and partition model if necessary
	 */
	nt_model_size model_size;
	get_model_size (model, model->first_element, &model_size);
	COMMIT_DEBUG1 (REPORT_INFO, SEARCH_SEQ, "given model has size %llu", model_size,
	               false);
	// for model-based search spaces > MAX_MODEL_SIZE:
	// partition the original model into num_partitions, where
	// each partition splits at a pos_var-model-position into
	// pos_var individual sub-models; get_next_model_partition
	// is used for this purpose and starts partitioning the
	// original model from the largest available "pos_var" thus
	// minimizing the overall number of sub-model iterations needed
	ushort num_partitions = 0;
	ntp_element
	model_partitions[MAX_MODEL_PARTITIONS];                // represent a model partition by the ntp_element at which
	// pos_var sub-models are deployed
	nt_element_count
	model_partition_original_min[MAX_MODEL_PARTITIONS];    // store model's original pos_var min/max values
	nt_element_count model_partition_original_max[MAX_MODEL_PARTITIONS];
	nt_element_count
	model_partition_current_var[MAX_MODEL_PARTITIONS];     // current value within min<=pos_var<=max range for this iteration
	// number of iterations when factoring in pos_var ranges of all partitions;
	// if Rx is the pos_var range (i.e. sum of) of ntp_element (partition) x,
	// and X is the set of all partitions used, then num_iterations is the
	// product of Rx for all x element of X
	REGISTER
	unsigned long long num_iterations = 1;
	COMMIT_DEBUG (REPORT_INFO, SEARCH_SEQ,
	              "partitioning model in up to MAX_MODEL_PARTITIONS", false);
	              
	while (model_size > MAX_MODEL_SIZE &&
	       num_partitions < MAX_MODEL_PARTITIONS - 1) {
		if (get_next_model_partition (model, model_partitions,
		                              (nt_element_count) (num_partitions))) {
			model_partition_original_min[num_partitions] =
			                    model_partitions[num_partitions]->type == unpaired ?
			                    model_partitions[num_partitions]->unpaired->min :
			                    model_partitions[num_partitions]->paired->min;
			model_partition_original_max[num_partitions] =
			                    model_partitions[num_partitions]->type == unpaired ?
			                    model_partitions[num_partitions]->unpaired->max :
			                    model_partitions[num_partitions]->paired->max;
			model_partition_current_var[num_partitions] =
			                    model_partition_original_min[num_partitions];
			num_iterations *= model_partition_original_max[num_partitions] -
			                  model_partition_original_min[num_partitions] + 1;
			                  
			/*
			 * setup model values for the first iteration (fix the specific model element's pos_var to model_partition_current_var)
			 */
			if (model_partitions[num_partitions]->type == unpaired) {
				model_partitions[num_partitions]->unpaired->min =
				                    model_partition_current_var[num_partitions];
				model_partitions[num_partitions]->unpaired->max =
				                    model_partition_current_var[num_partitions];
			}
			
			else {
				model_partitions[num_partitions]->paired->min =
				                    model_partition_current_var[num_partitions];
				model_partitions[num_partitions]->paired->max =
				                    model_partition_current_var[num_partitions];
			}
			
			// after identifying the next element/partition, calculate the resulting reduction in model_size
			model_size /= (model_partition_original_max[num_partitions] -
			               model_partition_original_min[num_partitions] + 1);
			num_partitions++;
		}
		
		else {
			COMMIT_DEBUG1 (REPORT_ERRORS, SEARCH_SEQ,
			               "failed to get next model for partition %d\n",
			               num_partitions,
			               false);
			               
			/*
			 * could not obtain all partitions necessary - reset model pos_var values to originals
			 */
			for (ushort i = 0; i < num_partitions; i++) {
				if (model_partitions[i]->type == unpaired) {
					model_partitions[i]->unpaired->min = model_partition_original_min[i];
					model_partitions[i]->unpaired->max = model_partition_original_max[i];
				}
				
				else {
					model_partitions[i]->paired->min = model_partition_original_min[i];
					model_partitions[i]->paired->max = model_partition_original_max[i];
				}
			}
			
			break;
		}
	}
	
	if (MAX_MODEL_SIZE < model_size) {
		COMMIT_DEBUG (REPORT_WARNINGS, SEARCH_SEQ,
		              "cannot partition model without exceeding capacity limits in search_seq",
		              false);
		return NULL; // cannot partition model without exceeding MAX_MODEL_SIZE -> so just quit and return no hits
	}
	
	else {
		COMMIT_DEBUG1 (REPORT_INFO, SEARCH_SEQ, "partitioned model into %d sub-models",
		               num_partitions, false);
	}
	
	/*
	 * invoke search in num_iterations; based on how many sub-model partitions are required
	 */
	long model_partition_iterator_idx = num_partitions -
	                                    1;  // index (position) of where we are along partition iterator
	                                    
	for (ushort iter = 0; iter < num_iterations; iter++) {
		COMMIT_DEBUG2 (REPORT_INFO, SEARCH_SEQ, "starting model iteration %d of %d",
		               iter + 1, num_partitions, false);
		               
		/*
		 * use search_seq_list in search_seq_at, ntp_list_copy, list_advance, list_prune to keep track of *initialized* lists, for eventual destruction
		 */
		if (!list_initialize_tagging()) {
			COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
			              "cannot initialize list tagging in search_seq", false);
			return NULL;
		}
		
		else {
			COMMIT_DEBUG (REPORT_INFO, SEARCH_SEQ, "initialized list tagging in search_seq",
			              false);
		}
		
		/*
		 * initialize wrapper constraint data structs
		 */
		for (REGISTER ushort i = 0; i < MAX_CONSTRAINT_MATCHES; i++) {
			wrapper_constraint_elements[i] = NULL;
		}
		
		last_wrapper_constraint_track_id = 0;
		/*
		 * prepare for invoking search
		 */
		COMMIT_DEBUG1 (REPORT_INFO, SEARCH_SEQ,
		               "submitted seq (hash %lu) to search in search_seq", seq_hash, false);
		ntp_list restrict matched_cnts = NULL, found_list = NULL;
		#ifdef MULTITHREADED_ON
		
		if (pthread_mutex_lock (&num_destruction_threads_mutex) == 0) {
		#endif
			#ifndef NO_FULL_CHECKS
		
			if (!
			#endif
			    ntp_list_alloc_debug (&matched_cnts, "matched_cnts in search_seq")
		    #ifndef NO_FULL_CHECKS
			   ) {
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&num_destruction_threads_mutex);
				#endif
				COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
				              "cannot alloc matched_cnts in search_seq", false);
				list_destroy_all_tagged();
				return NULL;
			}
			
		    #else
			    ;
		    #endif
			#ifdef SEARCH_SEQ_DETAIL
			COMMIT_DEBUG (REPORT_INFO, SEARCH_SEQ, "tracking sequence search against model",
			              true);
			              
			if (num_targets) {
				for (nt_hit_count i = 0; i < num_targets; i++) {
					COMMIT_DEBUG3 (REPORT_INFO, SEARCH_SEQ,
					               "target %lu set as: track_id=0, fp=%d, tp=%d", 50, i + 1, targets[i].fp_posn,
					               targets[i].tp_posn, false);
				}
			}
			
			else {
				COMMIT_DEBUG (REPORT_INFO, SEARCH_SEQ, "no specific targets set", true);
			}
			
			#endif
			REGISTER
			char this_pos_var = get_element_pos_var_range (model->first_element);
			#ifdef SEARCH_SEQ_DETAIL
			char pos_var = this_pos_var;
			#endif
			nt_rel_count span = 0;
			get_model_bp_span (model->first_element, &span, true, false);
			
			do {
				ntp_list restrict this_matched_cnts = NULL;
				#ifndef NO_FULL_CHECKS
				
				if (!
				#endif
				    ntp_list_alloc_debug (&this_matched_cnts, "this_matched_cnts in search_seq")
			    #ifndef NO_FULL_CHECKS
				   ) {
					#ifdef MULTITHREADED_ON
					pthread_mutex_unlock (&num_destruction_threads_mutex);
					#endif
					COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
					              "cannot alloc this_matched_cnts in search_seq", false);
					              
					if (matched_cnts) {
						list_destroy (matched_cnts);
						FREE_DEBUG (matched_cnts, "matched_cnts in search_seq");
					}
					
					list_destroy_all_tagged();
					return NULL;
				}
				
			    #else
				    ;
			    #endif
				ntp_list restrict this_list = NULL;
				#ifdef SEARCH_SEQ_DETAIL
				
				if (!pos_var || (this_pos_var == pos_var)) {
					COMMIT_DEBUG1 (REPORT_INFO, SEARCH_SEQ,
					               "starting search on initial element using pos_var %d", this_pos_var, true);
				}
				
				else {
					COMMIT_DEBUG1 (REPORT_INFO, SEARCH_SEQ,
					               "continuing search on initial element using pos_var %d", this_pos_var, true);
				}
				
				#endif
				
				if (search_seq_at (model,
				                   seq,
				                   seq_bp,
				                   model->first_element,
				                   NULL,
				                   &this_list, 0, 0, 0, 0, 0, -1, this_pos_var, NULL, &this_matched_cnts
			                   #ifdef SEARCH_SEQ_DETAIL
				                   , 0, targets, num_targets
			                   #endif
				                  )) {
					if (this_list && this_list->numels) {
						ntp_list previously_found_list = found_list;
						found_list = ntp_list_concatenate (found_list, this_list, 0);
						
						if (previously_found_list) {
							list_destroy (previously_found_list);
							FREE_DEBUG (previously_found_list, "previously found_list in search_seq");
						}
						
						if (this_list) {
							list_destroy (this_list);
							FREE_DEBUG (this_list, "this_list in search_seq");
							this_list = NULL;
						}
						
						ntp_list previosuly_matched_cnts = matched_cnts;
						matched_cnts = ntp_count_list_concatenate (matched_cnts, this_matched_cnts);
						
						if (previosuly_matched_cnts && previosuly_matched_cnts != matched_cnts) {
							list_destroy (previosuly_matched_cnts);
							FREE_DEBUG (previosuly_matched_cnts, "previosuly matched_cnts in search_seq");
						}
						
						if (this_matched_cnts && this_matched_cnts != matched_cnts) {
							list_destroy (this_matched_cnts);
							FREE_DEBUG (this_matched_cnts, "this_matched_cnts in search_seq");
						}
					}
					
					else {
						if (this_list) {
							list_destroy (this_list);
							FREE_DEBUG (this_list, "this_list in search_seq");
						}
						
						if (this_matched_cnts) {
							list_destroy (this_matched_cnts);
							FREE_DEBUG (this_matched_cnts, "this_matched_cnts in search_seq");
						}
					}
				}
				
				else {
					#ifdef MULTITHREADED_ON
					pthread_mutex_unlock (&num_destruction_threads_mutex);
					#endif
					
					if (this_list) {
						list_destroy (this_list);
						FREE_DEBUG (this_list, "this_list in search_seq");
					}
					
					if (matched_cnts) {
						list_destroy (matched_cnts);
						FREE_DEBUG (matched_cnts, "matched_cnts in search_seq");
					}
					
					if (this_matched_cnts) {
						list_destroy (this_matched_cnts);
						FREE_DEBUG (this_matched_cnts, "this_matched_cnts in search_seq");
					}
					
					list_destroy_all_tagged();
					destroy_min_max_dist (min_stack_dist, max_stack_dist, in_extrusion, dist_els);
					return NULL;
				}
				
				this_pos_var--;
			}
			while (this_pos_var >= 0);
			
			if (matched_cnts) {
				list_destroy (matched_cnts);
				FREE_DEBUG (matched_cnts, "matched_cnts in search_seq");
			}
			
			if (found_list) {
				COMMIT_DEBUG2 (REPORT_INFO, SEARCH_SEQ,
				               "seq search (hash %lu) returned with found_list of size %u in search_seq",
				               seq_hash,
				               found_list->numels, false);
			}
			
			else {
				COMMIT_DEBUG1 (REPORT_INFO, SEARCH_SEQ,
				               "seq search (hash %lu) returned an empty found_list", seq_hash, false);
			}
			
			if (found_list && list_iterator_start (found_list)) {
				if (!safe_copy) {
					safe_copy = MALLOC_DEBUG (sizeof (nt_list),
					                          "safe_copy of found_list in search_seq");  // TODO: error handling
					                          
					if (!safe_copy || 0 != list_init (safe_copy)) {
						list_iterator_stop (found_list);
						COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
						              "cannot allocate or initialize safe_copy in search_seq", false);
						              
						if (safe_copy) {
							FREE_DEBUG (safe_copy, "safe_copy of found_list in search_seq");
						}
						
						list_destroy (found_list);
						FREE_DEBUG (found_list, "found_list in search_seq_at");
						destroy_min_max_dist (min_stack_dist, max_stack_dist, in_extrusion, dist_els);
						return NULL;
					}
				}
				
				REGISTER
				bool success = true;
				ntp_linked_bp next_linked_bp = NULL;
				
				while (list_iterator_hasnext (found_list)) {
					REGISTER
					ntp_linked_bp found_linked_bp = list_iterator_next (found_list);
					
					if (!found_linked_bp) {
						success = false;
						break;
					}
					
					if (model->first_constraint) {
						if (!satisfy_constraints (seq, model->first_constraint, safe_copy,
						                          found_linked_bp, found_linked_bp, &next_linked_bp)) {
							success = false;
							break;
						}
					}
					
					else
						if (!safe_copy_linked_bp (safe_copy, found_linked_bp, &next_linked_bp)) {
							success = false;
							break;
						}
				}
				
				if (success) {
					COMMIT_DEBUG (REPORT_INFO, SEARCH_SEQ,
					              "generated safe_copy of found_list in search_seq", false);
				}
				
				else {
					COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ,
					              "could not copy/append element to safe_copy of found_list in search_seq",
					              false);
					dispose_linked_bp_copy (model,
					                        safe_copy,
					                        "linked_bp_copy for safe_copy of found_list in search_seq [could not copy/append element]",
					                        "safe_copy of found_list in search_seq [could not copy/append element]"
					                        #ifndef NO_FULL_CHECKS
					                        , "could not iterate over to free safe_copy of found_list in search_seq"
					                        #endif
					                       );
				}
				
				list_iterator_stop (found_list);
				
				if (found_list) {
					list_destroy (found_list);
					FREE_DEBUG (found_list, "found_list in search_seq_at");
				}
			}
			
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			#endif
			#ifdef MULTITHREADED_ON
		}
		
		destroy_min_max_dist (min_stack_dist, max_stack_dist, in_extrusion, dist_els);
		return NULL;
			#endif
		
		if (1 != num_iterations) {
			/*
			 *  update model_partition_iterator for next iteration
			 */
			
			// starting with P1,P2,...,Pnum_partitions, iterate over all partition
			// permutations, where each partition ranges over the respective element's
			// pos_var values; thus, for example, pos_varMIN1 <= P1 <= pos_varMAX1, where
			// pos_varMIN1 and pos_varMAX1 are the min/max pos_var values for the
			// first partition's ntp_element, respectively
			if (model_partition_current_var[model_partition_iterator_idx] <
			    model_partition_original_max[model_partition_iterator_idx]) {
				model_partition_current_var[model_partition_iterator_idx]++;
				
				if (model_partitions[model_partition_iterator_idx]->type == unpaired) {
					model_partitions[model_partition_iterator_idx]->unpaired->min =
					                    model_partition_current_var[model_partition_iterator_idx];
					model_partitions[model_partition_iterator_idx]->unpaired->max =
					                    model_partition_current_var[model_partition_iterator_idx];
				}
				
				else {
					model_partitions[model_partition_iterator_idx]->paired->min =
					                    model_partition_current_var[model_partition_iterator_idx];
					model_partitions[model_partition_iterator_idx]->paired->max =
					                    model_partition_current_var[model_partition_iterator_idx];
				}
				
				if (model_partition_iterator_idx < num_partitions - 1) {
					model_partition_iterator_idx++;
				}
			}
			
			else {
				while (model_partition_iterator_idx > 0) {
					if (model_partitions[model_partition_iterator_idx]->type == unpaired) {
						model_partitions[model_partition_iterator_idx]->unpaired->min =
						                    model_partition_original_min[model_partition_iterator_idx];
						model_partitions[model_partition_iterator_idx]->unpaired->max =
						                    model_partition_original_min[model_partition_iterator_idx];
					}
					
					else {
						model_partitions[model_partition_iterator_idx]->paired->min =
						                    model_partition_original_min[model_partition_iterator_idx];
						model_partitions[model_partition_iterator_idx]->paired->max =
						                    model_partition_original_min[model_partition_iterator_idx];
					}
					
					model_partition_current_var[model_partition_iterator_idx] =
					                    model_partition_original_min[model_partition_iterator_idx];
					model_partition_iterator_idx--;
					
					if (model_partition_current_var[model_partition_iterator_idx] <
					    model_partition_original_max[model_partition_iterator_idx]) {
						model_partition_current_var[model_partition_iterator_idx]++;
						
						if (model_partitions[model_partition_iterator_idx]->type == unpaired) {
							model_partitions[model_partition_iterator_idx]->unpaired->min =
							                    model_partition_current_var[model_partition_iterator_idx];
							model_partitions[model_partition_iterator_idx]->unpaired->max =
							                    model_partition_current_var[model_partition_iterator_idx];
						}
						
						else {
							model_partitions[model_partition_iterator_idx]->paired->min =
							                    model_partition_current_var[model_partition_iterator_idx];
							model_partitions[model_partition_iterator_idx]->paired->max =
							                    model_partition_current_var[model_partition_iterator_idx];
						}
						
						model_partition_iterator_idx = num_partitions - 1;
						break;
					}
				}
			}
			
			/*
			 * clean-up for next search iteration
			 */
			if (iter < num_iterations - 1) {
				list_destroy_all_tagged();
				#ifdef MULTITHREADED_ON
				
				if (!wait_list_destruction()) {
					COMMIT_DEBUG (REPORT_ERRORS, SCAN, "list destruction failed in search_seq",
					              false);
					dispose_linked_bp_copy (model, safe_copy,
					                        "linked_bp_copy for safe_copy of found_list in search_seq",
					                        "safe_copy of found_list in search_seq",
					                        "could not wait for list destruction in search_seq");
					                        
					/*
					 * reset model pos_var values to originals
					 */
					for (ushort i = 0; i < num_partitions; i++) {
						if (model_partitions[i]->type == unpaired) {
							model_partitions[i]->unpaired->min = model_partition_original_min[i];
							model_partitions[i]->unpaired->max = model_partition_original_max[i];
						}
						
						else {
							model_partitions[i]->paired->min = model_partition_original_min[i];
							model_partitions[i]->paired->max = model_partition_original_max[i];
						}
					}
					
					return NULL;
				}
				
				#endif
			}
		}
	}
	
	if (1 != num_iterations) {
		/*
		 * reset model pos_var values to originals
		 */
		for (ushort i = 0; i < num_partitions; i++) {
			if (model_partitions[i]->type == unpaired) {
				model_partitions[i]->unpaired->min = model_partition_original_min[i];
				model_partitions[i]->unpaired->max = model_partition_original_max[i];
			}
			
			else {
				model_partitions[i]->paired->min = model_partition_original_min[i];
				model_partitions[i]->paired->max = model_partition_original_max[i];
			}
		}
	}
	
	destroy_min_max_dist (min_stack_dist, max_stack_dist, in_extrusion, dist_els);
	
	if (safe_copy && MAX_HITS_RETURNED < safe_copy->numels) {
		/*
		 * get distribution of hit FEs within -100.0f and 100.0f kcal/mol
		 * range with up to 2 decimal points precision
		 */
		g_memset (hit_fe_distr, 0, 20000 * sizeof (ulong));
		list_iterator_start (safe_copy);
		
		while (list_iterator_hasnext (safe_copy)) {
			ntp_linked_bp this_linked_bp = list_iterator_next (safe_copy);
			float this_mfe = get_turner_mfe_estimate (this_linked_bp, seq);
			// build approximate distribution of FE for hit list:
			// to index into hit_fe_distr, this_mfe is first shifted
			// by 100, assuming an input FE range of [-100.0f .. 100.0f],
			// and then rounded (and shifted) by 2 decimal places
			hit_fe_distr[ (int) (trunc (roundf ((this_mfe + 100.0f) * 100)))]++;
		}
		
		list_iterator_stop (safe_copy);
		/*
		 * calculate FE threshold based on FE distribution and
		 * maximum limit of MAX_HITS_RETURNED
		 *
		 * anything below threshold value (note: threshold
		 * value might correspond to distribution's first bin)
		 * and within MAX_HITS_RETURNED will be returned
		 */
		REGISTER
		float FE_threshold =
		                    100.0f;    // initialize to accept anything below and including 100kcal/mol
		REGISTER
		nt_hit_count num_hits = 0;      // hit accumulator
		
		for (REGISTER ushort i = 0; i < 20000; i++) {
			num_hits += hit_fe_distr[i];
			
			if (num_hits > MAX_HITS_RETURNED) {
				// only set threshold when MAX_HITS_RETURNED is exceeded;
				// use i+1 for rounding up (down in terms of less favourable, positive FE)
				FE_threshold = ((float) (i + 1) / 100.0f) - 100.0f;
				break;
			}
		}
		
		dispose_linked_bp_copy_by_FE_threshold (model, seq, safe_copy, FE_threshold,
		                                        "filtered out linked_bp of safe_copy in search_seq",
		                                        "filtered out elements of linked_bp of safe_copy in search_seq");
	}
	
	*elapsed_time = get_timer();
	return safe_copy;
}
