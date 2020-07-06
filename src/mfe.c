#include <math.h>
#include "mfe.h"
#include "mfe_params.h"

const static float FLOAT_DELTA = 0.1;      // delta for float/int roundoff

static unsigned short  stack_mfe_sze[STACK_MFE_LIMITS_MAX_LENGTH -
                                                                 STACK_MFE_LIMITS_MIN_LENGTH + 1];
static float          *stack_mfe_val[STACK_MFE_LIMITS_MAX_LENGTH -
                                                                 STACK_MFE_LIMITS_MIN_LENGTH + 1];
static long long      *stack_mfe_cnt[STACK_MFE_LIMITS_MAX_LENGTH -
                                                                 STACK_MFE_LIMITS_MIN_LENGTH + 1];
ulong                  stack_mfe_total_cnt[STACK_MFE_LIMITS_MAX_LENGTH -
                                                                    STACK_MFE_LIMITS_MIN_LENGTH + 1];

// maximum number of "virtual" stacked bps ->
// obtained after aggregated branch stacks that
// are only separated by single nt bulges
#define MAX_CUMUL_STACK_LENGTH              30
typedef struct {
	ntp_list nested_helices;
	float mfe_estimate;
} nt_branch, *ntp_branch;

typedef struct {
	ntp_list branches;
	ntp_branch base;
	float mfe_estimate;
} nt_junction, *ntp_junction;

typedef struct {
	bool is_junction;
	union {
		ntp_junction junction;
		ntp_list branches;
	};
} nt_fe_element, *ntp_fe_element;

const static float STACK_MFE_LIMITS[STACK_MFE_LIMITS_MAX_LENGTH -
                                    STACK_MFE_LIMITS_MIN_LENGTH + 1][2] = {
	{   -3.400000f,  1.300000f  },
	{   -6.700000f,  1.600000f  },
	{  -10.000000f,  2.900000f  },
	{  -13.299999f,  3.200000f  },
	{  -16.600000f,  4.500000f  },
	{  -19.900000f,  4.800000f  },
	{  -23.199999f,  6.100000f  },
	{  -26.499998f,  6.400001f  },
	{  -29.799997f,  7.700001f  },
	{  -33.099998f,  8.000001f  },
	{  -36.399998f,  9.300001f  },
	{  -39.699997f,  9.600001f  },
	{  -42.999996f, 10.900002f  },
	{  -46.299995f, 11.200002f  }
};

const static char STACK_MFE_DISTRIB_FN[STACK_MFE_LIMITS_MAX_LENGTH -
                                       STACK_MFE_LIMITS_MIN_LENGTH + 1][MAX_FILENAME_LENGTH + 1] = {
	"mfe2stack.dist",
	"mfe3stack.dist",
	"mfe4stack.dist",
	"mfe5stack.dist",
	"mfe6stack.dist",
	"mfe7stack.dist",
	"mfe8stack.dist",
	"mfe9stack.dist",
	"mfe10stack.dist",
	"mfe11stack.dist",
	"mfe12stack.dist",
	"mfe13stack.dist",
	"mfe14stack.dist",
	"mfe15stackMOD.dist"
};

/*
 * MFE functions
 */

float get_stack_mfe_percentile (unsigned short stack_len, float mfe) {
	if (stack_len < STACK_MFE_LIMITS_MIN_LENGTH ||
	    stack_len > STACK_MFE_LIMITS_MAX_LENGTH ||
	    !stack_mfe_sze[stack_len - STACK_MFE_LIMITS_MIN_LENGTH]) {
		return -1.0f;
	}
	
	else
		if (mfe < STACK_MFE_LIMITS[stack_len - STACK_MFE_LIMITS_MIN_LENGTH][0]) {
			return 1.0f;
		}
		
		else
			if (mfe > STACK_MFE_LIMITS[stack_len - STACK_MFE_LIMITS_MIN_LENGTH][1]) {
				return 0.0f;
			}
			
			else {
				unsigned long long this_cnt = 0;
				
				for (unsigned short i = 0;
				     i < stack_mfe_sze[stack_len - STACK_MFE_LIMITS_MIN_LENGTH]; i++) {
					if (mfe >= stack_mfe_val[stack_len - STACK_MFE_LIMITS_MIN_LENGTH][i]) {
						break;
					}
					
					else {
						this_cnt += stack_mfe_cnt[stack_len - STACK_MFE_LIMITS_MIN_LENGTH][i];
					}
				}
				
				return this_cnt / (float) stack_mfe_total_cnt[stack_len -
				                                                  STACK_MFE_LIMITS_MIN_LENGTH];
			}
}

float get_simple_mfe_estimate (ntp_linked_bp restrict linked_bp,
                               const char *seq) {
	#ifndef NO_FULL_CHECKS
                               
	if (!linked_bp) {
		COMMIT_DEBUG (REPORT_ERRORS, MFE, "NULL linked_bp passed to get_mfe_estimate",
		              false);
		return STACK_MFE_FAILED;
	}
	
	if (!seq || !strlen (seq)) {
		COMMIT_DEBUG (REPORT_ERRORS, MFE,
		              "NULL or empty seq passed to get_simple_mfe_estimate", false);
		return STACK_MFE_FAILED;
	}
	
	const nt_seq_len seq_len = strlen (seq);
	#endif
	ntp_linked_bp this_linked_bp = linked_bp;
	uchar this_fp_nt, this_tp_nt, prev_fp_nt = 0, prev_tp_nt = 0;
	REGISTER
	nt_rel_count this_bp_count;
	REGISTER
	float total_mfe = 0.0f, partial_mfe;
	REGISTER
	uchar i;
	bool success = false;
	
	while (this_linked_bp) {
		REGISTER
		uchar this_stack_len = this_linked_bp->stack_len;
		
		if (this_stack_len > 1) {  // skip any stacks with len less than 2
			#ifndef NO_FULL_CHECKS
			if (this_linked_bp->bp->fp_posn < 1 || this_linked_bp->bp->fp_posn > seq_len ||
			    this_linked_bp->bp->tp_posn < 1 || this_linked_bp->bp->tp_posn > seq_len) {
				COMMIT_DEBUG (REPORT_ERRORS, MFE,
				              "fp_posn or tp_posn of this_linked_bp is outside the string boundaries of seq passed to get_simple_mfe_estimate",
				              false);
				return STACK_MFE_FAILED;
			}
			
			#endif
			this_bp_count = 0;
			partial_mfe = 0.0f;
			
			for (i = 0; i < this_stack_len; i++) {
				this_fp_nt = (uchar) seq[this_linked_bp->bp->fp_posn - 1 + i];
				this_tp_nt = (uchar) seq[this_linked_bp->bp->tp_posn - 1 + this_stack_len - i -
				                                                     1];
				                                                     
				if (this_bp_count) {
					partial_mfe +=
					                    STACK_ENERGIES[MAP_RNA[prev_fp_nt]]
					                    [MAP_RNA[prev_tp_nt]]
					                    [MAP_RNA[this_fp_nt]]
					                    [MAP_RNA[this_tp_nt]];
					success = true;
				}
				
				prev_fp_nt = this_fp_nt;
				prev_tp_nt = this_tp_nt;
				this_bp_count++;
			}
			
			total_mfe += partial_mfe;
		}
		
		this_linked_bp = this_linked_bp->prev_linked_bp;
	}
	
	if (success) {
		return total_mfe;
	}
	
	else {
		// need at least 2bp to return 'valid' MFE result
		return STACK_MFE_FAILED;
	}
}

ntp_list get_fe_elements (ntp_linked_bp restrict linked_bp) {
	ntp_linked_bp current_linked_bp = linked_bp;
	ntp_list fe_elements, branch_list;
	/*
	 * fe_elements - list of nodes of type fe_element (i.e. branch/junction). fe_element of type branch itself contains a list of branches
	 */
	fe_elements = malloc (sizeof (nt_list)); // TODO: error handling
	/*
	 * branch_list - working list of branches which are progressively merged into junctions, or finally included in the fe_elements' branch list
	 */
	branch_list = malloc (sizeof (nt_list));
	list_init (fe_elements);
	list_init (branch_list);
	
	do {
		/*
		 * check for, and if it exists, skip a 0-length helix (which can only happen when a posvar sits on a single bp-helix)
		 */
		while (current_linked_bp && !current_linked_bp->stack_len) {
			current_linked_bp = current_linked_bp->prev_linked_bp;
		}
		
		if (!current_linked_bp) {
			break;
		}
		
		/*
		 * check if more than one of the previously visited ntp_linked_bps, stored as separate
		 * branches in branch_list or as base branches in junctions, are spatially contained in
		 * current_linked_bp. if so, then set up the current fe_element structure as a junction
		 * and link in the branches identified
		 */
		// check branches in branch_list
		list_iterator_start (branch_list);
		ushort contained_branches_idx[MAX_BRANCHES_PER_JUNCTION];
		ushort current_idx = 0, num_branches_contained = 0;
		
		while (list_iterator_hasnext (branch_list)) {
			ntp_branch that_branch = list_iterator_next (branch_list);
			ntp_linked_bp that_linked_bp = list_get_at (that_branch->nested_helices, 0);
			
			if (that_linked_bp->bp->fp_posn > current_linked_bp->bp->fp_posn &&
			    that_linked_bp->bp->tp_posn < current_linked_bp->bp->tp_posn) {
				contained_branches_idx[num_branches_contained] = current_idx;
				num_branches_contained++;
			}
			
			current_idx++;
		}
		
		list_iterator_stop (branch_list);
		
		if (num_branches_contained == 1) {
			ntp_branch that_branch = list_get_at (branch_list, contained_branches_idx[0]);
			list_insert_at (that_branch->nested_helices, current_linked_bp, 0);
		}
		
		// if 2 or more branches are contained in current_linked_bp -> extract from branch_list and start conversion to a junction fe_element
		else
			if (num_branches_contained > 1) {
				ntp_list junction_branch_list = malloc (sizeof (
				                                        nt_list));   // TODO: error handling
				list_init (junction_branch_list);
				
				for (ushort i = 0; i < num_branches_contained; i++) {
					list_append (junction_branch_list, list_extract_at (branch_list,
					                                        contained_branches_idx[i] - i));
				}
				
				// set up a base branch for current_linked_bp
				ntp_branch this_branch = malloc (sizeof (nt_branch));   // TODO: error handling
				this_branch->mfe_estimate = 0.0f;
				ntp_list this_nested_helices = malloc (sizeof (nt_list));
				list_init (this_nested_helices);
				list_append (this_nested_helices, current_linked_bp);
				this_branch->nested_helices = this_nested_helices;
				ntp_fe_element this_fe_element = malloc (sizeof (
				                                        nt_fe_element)); // TODO: error handling
				this_fe_element->is_junction = true;
				ntp_junction this_junction = malloc (sizeof (
				                                        nt_junction)); // TODO: error handling
				this_junction->branches = junction_branch_list;
				this_junction->base = this_branch;
				this_fe_element->junction = this_junction;
				list_append (fe_elements, this_fe_element);
			}
			
			else {
				// check fe_elements for a junction that has a base contained within current_linked_bp
				// note: branches, in any remain, are added to fe_elements only at the very end, so in here,
				//       all fe_elements nodes are junctions
				bool junction_found = false;
				list_iterator_start (fe_elements);
				
				while (list_iterator_hasnext (fe_elements)) {
					ntp_junction that_junction = ((ntp_fe_element) list_iterator_next (
					                                        fe_elements))->junction;
					ntp_linked_bp that_linked_bp = list_get_at (that_junction->base->nested_helices,
					                                        0);
					                                        
					if (that_linked_bp->bp->fp_posn > current_linked_bp->bp->fp_posn &&
					    that_linked_bp->bp->tp_posn < current_linked_bp->bp->tp_posn) {
						list_insert_at (that_junction->base->nested_helices, current_linked_bp, 0);
						junction_found = true;
					}
				}
				
				list_iterator_stop (fe_elements);
				
				if (!junction_found) {
					// set up a new branch in branch_list
					ntp_branch this_branch = malloc (sizeof (nt_branch));   // TODO: error handling
					this_branch->mfe_estimate = 0.0f;
					ntp_list this_nested_helices = malloc (sizeof (nt_list));
					list_init (this_nested_helices);
					list_append (this_nested_helices, current_linked_bp);
					this_branch->nested_helices = this_nested_helices;
					list_append (branch_list, this_branch);
				}
			}
			
		current_linked_bp = current_linked_bp->prev_linked_bp;
	}
	while (current_linked_bp);
	
	if (branch_list->numels) {
		ntp_fe_element this_fe_element = malloc (sizeof (
		                                        nt_fe_element)); // TODO: error handling
		this_fe_element->is_junction = false;
		// this is what remains of the original branch_list
		this_fe_element->branches = branch_list;
		list_append (fe_elements, this_fe_element);
	}
	
	else {
		list_destroy (branch_list);
		free (branch_list);
	}
	
	return fe_elements;
}

void destroy_fe_elements (ntp_list restrict fe_elements) {
	list_iterator_start (fe_elements);
	
	while (list_iterator_hasnext (fe_elements)) {
		ntp_fe_element this_fe_element = list_iterator_next (fe_elements);
		
		if (this_fe_element->is_junction) {
			ntp_junction this_junction = this_fe_element->junction;
			ntp_list this_nested_helices = this_junction->base->nested_helices;
			list_destroy (this_nested_helices);
			free (this_nested_helices);
			free (this_junction->base);
			ntp_list this_branches = this_junction->branches;
			list_iterator_start (this_branches);
			
			while (list_iterator_hasnext (this_branches)) {
				ntp_branch this_branch = list_iterator_next (this_branches);
				this_nested_helices = this_branch->nested_helices;
				list_destroy (this_nested_helices);
				free (this_nested_helices);
				free (this_branch);
			}
			
			list_iterator_stop (this_branches);
			list_destroy (this_branches);
			free (this_branches);
			free (this_junction);
		}
		
		else {
			ntp_list this_branches = this_fe_element->branches;
			list_iterator_start (this_branches);
			
			while (list_iterator_hasnext (this_branches)) {
				ntp_branch this_branch = list_iterator_next (this_branches);
				ntp_list this_nested_helices = this_branch->nested_helices;
				list_destroy (this_nested_helices);
				free (this_nested_helices);
				free (this_branch);
			}
			
			list_iterator_stop (this_branches);
			list_destroy (this_branches);
			free (this_branches);
		}
		
		free (this_fe_element);
	}
	
	list_iterator_stop (fe_elements);
	list_destroy (fe_elements);
	free (fe_elements);
}

static inline void score_branch (ntp_branch this_branch, bool score_loop,
                                 const char *seq) {
	// see https://rna.urmc.rochester.edu/NNDB/turner04/wc.html
	//     https://rna.urmc.rochester.edu/NNDB/turner04/gu.html
	//     https://rna.urmc.rochester.edu/NNDB/turner04/tm.html
	//     https://rna.urmc.rochester.edu/NNDB/turner04/hairpin.html
	ntp_list this_nested_helices = this_branch->nested_helices;
	ntp_linked_bp this_linked_bp = NULL, next_linked_bp = NULL;
	float cumul_fe = 0.0f;
	ushort this_cumul_stack_len;
	uchar cumul_5p_nt_stack[MAX_CUMUL_STACK_LENGTH];
	uchar cumul_3p_nt_stack[MAX_CUMUL_STACK_LENGTH];
	ushort cumul_single_nt_bulge_idx[MAX_CUMUL_STACK_LENGTH],
	       num_cumul_single_nt_bulges;
	short cumul_single_nt_bulge_polarity[MAX_CUMUL_STACK_LENGTH];
	uchar cumul_single_nt_bulge[MAX_CUMUL_STACK_LENGTH];
	bool have_internal_loop = false;
	ushort i = 0;
	ushort num_cumul_stacks = 0;
	
	while (i < this_nested_helices->numels) {
		this_linked_bp = list_get_at (this_nested_helices, i);
		
		if (this_linked_bp->stack_len >= 1 && this_linked_bp->bp->fp_posn) {
			float this_fe = 0.0f;
			this_cumul_stack_len = 0;
			num_cumul_single_nt_bulges = 0;
			ushort num_5p_nt_in_between = 0, num_3p_nt_in_between = 0;
			
			/*
			 * acquire the next "virtual" or cumulative stack:
			 * - aggregate together adjacent helices that are
			 *   separated only by single nt bulge loops
			 * - track single nt bulge loops along the way
			 */
			do {
				// copy seq nt's for this_linked_bp stack
				for (ushort j = 0; j < this_linked_bp->stack_len; j++) {
					cumul_5p_nt_stack[this_cumul_stack_len + j] = (uchar)
					                                        seq[this_linked_bp->bp->fp_posn + j - 1];
					cumul_3p_nt_stack[this_cumul_stack_len + j] = (uchar)
					                                        seq[this_linked_bp->bp->tp_posn + this_linked_bp->stack_len - j - 2];
				}
				
				this_cumul_stack_len += this_linked_bp->stack_len;
				
				/*
				 * check if (just) a base triple separates this branch stack and the next
				 */
				if (NULL != this_linked_bp->fp_elements &&
				    NULL != this_linked_bp->fp_elements->unpaired->i_constraint.reference &&
				    this_linked_bp->fp_elements->unpaired->i_constraint.reference->type ==
				    base_triple &&
				    !this_linked_bp->fp_elements->unpaired->dist &&
				    NULL == this_linked_bp->fp_elements->unpaired->next) {
					// if so, integrate (and afterwards score) base triple as part of virtual stack
					cumul_5p_nt_stack[this_cumul_stack_len] = (uchar)
					                                        seq[this_linked_bp->bp->fp_posn + this_linked_bp->stack_len - 1];
					cumul_3p_nt_stack[this_cumul_stack_len] = (uchar)
					                                        seq[this_linked_bp->bp->tp_posn - 2];
					this_cumul_stack_len++;
					
					if (i < this_nested_helices->numels - 1) {
						next_linked_bp = list_get_at (this_nested_helices, i + 1);
					}
				}
				
				else
				
					/*
					 * check for single nt bulges between this stack and (when present) the next
					 */
					if (i < this_nested_helices->numels - 1) {
						next_linked_bp = list_get_at (this_nested_helices, i + 1);
						num_5p_nt_in_between = (ushort) (next_linked_bp->bp->fp_posn -
						                                 (this_linked_bp->bp->fp_posn + this_linked_bp->stack_len));
						num_3p_nt_in_between = (ushort) (this_linked_bp->bp->tp_posn -
						                                 (next_linked_bp->bp->tp_posn + next_linked_bp->stack_len));
						                                 
						if (num_5p_nt_in_between > 0 || num_3p_nt_in_between > 0) {
							// can only add bulge information if NOT both 5p and 3p "in betweens" are 0;
							// if both are 0 then simply aggregate stacks without adding the bulge
							if ((1 == num_5p_nt_in_between && !num_3p_nt_in_between) ||
							    (!num_5p_nt_in_between && 1 == num_3p_nt_in_between)) {
								cumul_single_nt_bulge_idx[num_cumul_single_nt_bulges] = (ushort) (
								                                        this_cumul_stack_len - 1);
								                                        
								if (1 == num_5p_nt_in_between) {
									cumul_single_nt_bulge_polarity[num_cumul_single_nt_bulges] = 1;
								}
								
								else {
									cumul_single_nt_bulge_polarity[num_cumul_single_nt_bulges] = -1;
								}
								
								cumul_single_nt_bulge[num_cumul_single_nt_bulges] = (uchar)
								                                        seq[next_linked_bp->bp->fp_posn - 2];
								num_cumul_single_nt_bulges++;
							}
							
							else {
								break;
							}
						}
					}
					
				if (i == this_nested_helices->numels - 1) {
					break;
				}
				
				this_linked_bp = next_linked_bp;
				i++;
			}
			while (1);
			
			num_cumul_stacks++;
			
			/*
			 * process (virtual/cumulative) stack of bps when this_cumul_stack_len > 1
			 * - include WC helix and GU pair params
			 * - calc GU sequence-dependent context params
			 * - include single nt bulge loop params
			 * - calc initial/final AU/GU pair params
			 * if this_cumul_stack_len == 1
			 * - calc initial/final AU/GU pair params
			 * in either scenario check for internal loops before/after virtual stack
			 */
			if (this_cumul_stack_len > 1) {
				ushort j = 0;
				
				while (j < this_cumul_stack_len - 1) {
					uchar this_fp_nt = cumul_5p_nt_stack[j],
					      this_tp_nt = cumul_3p_nt_stack[j];
					      
					/*
					 * check GU sequence-dependent context
					 */
					if (this_fp_nt == 'g' && this_tp_nt == 'c' &&
					    ((this_cumul_stack_len - j) >= 4)) {
						bool interrupted_by_bulge = false;
						
						for (ushort b = 0; b < num_cumul_single_nt_bulges; b++) {
							if (cumul_single_nt_bulge_idx[b] == j + 1 ||
							    cumul_single_nt_bulge_idx[b] == j + 2) {
								// only include special GU context when (if) sequence not
								// interrupted by single-nt bulge in the middle (at j+1, j+2)
								interrupted_by_bulge = true;
								break;
							}
						}
						
						if (!interrupted_by_bulge &&
						    cumul_5p_nt_stack[j + 1] == 'g' && cumul_3p_nt_stack[j + 1] == 'u' &&
						    cumul_5p_nt_stack[j + 2] == 'u' && cumul_3p_nt_stack[j + 2] == 'g' &&
						    cumul_5p_nt_stack[j + 3] == 'c' && cumul_3p_nt_stack[j + 3] == 'g') {
							// 5'GGUC3' 3'CUGG5' content - see https://rna.urmc.rochester.edu/NNDB/turner04/gu-parameters.html
							this_fe += CONTEXT_5GU3UG_BONUS;
							j += 3;
							continue;
						}
					}
					
					uchar next_fp_nt = cumul_5p_nt_stack[j + 1],
					      next_tp_nt = cumul_3p_nt_stack[j + 1];
					// WC & GU params - see https://rna.urmc.rochester.edu/NNDB/turner04/wc-parameters.html and
					//                      https://rna.urmc.rochester.edu/NNDB/turner04/gu-parameters.html
					this_fe +=
					                    STACK_ENERGIES[MAP_RNA[this_fp_nt]][MAP_RNA[this_tp_nt]][MAP_RNA[next_fp_nt]][MAP_RNA[next_tp_nt]];
					                    
					/*
					 * check for initial and final GU/AU penalty and symmetry compensation
					 */
					if (!j || (j == this_cumul_stack_len - 2)) {
						if (!j) {
							if (((this_fp_nt == 'a' || this_fp_nt == 'g') && this_tp_nt == 'u') ||
							    (this_fp_nt == 'u' && (this_tp_nt == 'a' || this_tp_nt == 'g'))) {
								if (!have_internal_loop) {
									this_fe += PENALTY_AU_GU;
								}
							}
							
							have_internal_loop = false;
						}
						
						if (0 < num_5p_nt_in_between && 0 < num_3p_nt_in_between) {
							have_internal_loop = true;
						}
						
						if (j == this_cumul_stack_len - 2) {
							if (((next_fp_nt == 'a' || next_fp_nt == 'g') && next_tp_nt == 'u') ||
							    (next_fp_nt == 'u' && (next_tp_nt == 'a' || next_tp_nt == 'g'))) {
								if (!have_internal_loop) {
									this_fe += PENALTY_AU_GU;
								}
							}
							
							if (i == this_nested_helices->numels - 1 && 1 == num_cumul_stacks) {
								/*
								 * check for symmetry (only if across all helices -> virtual stack === branch)
								 */
								j = 0;
								
								while (j < this_cumul_stack_len) {
									if (cumul_5p_nt_stack[j] != cumul_3p_nt_stack[this_cumul_stack_len - j - 1]) {
										break;
									}
									
									j++;
								}
								
								if (j == this_cumul_stack_len) {
									this_fe += PENALTY_SYMMETRY;
								}
							}
							
							break;
						}
					}
					
					j++;
				}
				
				/*
				 * also process any single nt bulges
				 * note: single nt bulge processing requires access to a "virtual" cumulative stack
				 *       to access special C bulge conditions and to detect the number of states
				 *       (refer to https://rna.urmc.rochester.edu/NNDB/turner04/bulge.html)
				 */
				if (num_cumul_single_nt_bulges) {
					for (ushort b = 0; b < num_cumul_single_nt_bulges; b++) {
						this_fe += BULGE_INITIATION_PENALTY[1];
						
						if (cumul_single_nt_bulge[b] == 'c' &&
						    ((1 == cumul_single_nt_bulge_polarity[b] &&
						      (cumul_5p_nt_stack[cumul_single_nt_bulge_idx[b]] == 'c' ||
						       cumul_5p_nt_stack[cumul_single_nt_bulge_idx[b] + 1] == 'c')) ||
						     (-1 == cumul_single_nt_bulge_polarity[b] &&
						      (cumul_3p_nt_stack[cumul_single_nt_bulge_idx[b]] == 'c' ||
						       cumul_3p_nt_stack[cumul_single_nt_bulge_idx[b] + 1] == 'c')))) {
							this_fe += SPECIAL_C_BULGE_BONUS;
						}
						
						/*
						 * determine number of states
						 */
						ushort num_states = 1;
						
						if (1 == cumul_single_nt_bulge_polarity[b]) {
							short this_idx = cumul_single_nt_bulge_idx[b];
							uchar this_bulge_nt = cumul_single_nt_bulge[b];
							
							while (this_idx >= 0) {
								if (((this_bulge_nt == 'g' || this_bulge_nt == 'a') &&
								     cumul_3p_nt_stack[this_idx] == 'u') ||
								    ((cumul_3p_nt_stack[this_idx] == 'g' || cumul_3p_nt_stack[this_idx] == 'a') &&
								     this_bulge_nt == 'u') ||
								    (this_bulge_nt == 'g' && cumul_3p_nt_stack[this_idx] == 'c') ||
								    (cumul_3p_nt_stack[this_idx] == 'g' && this_bulge_nt == 'c')) {
									num_states++;
								}
								
								else {
									break;
								}
								
								this_idx--;
							}
							
							this_idx = (short) (cumul_single_nt_bulge_idx[b] + 1);
							
							while (this_idx < this_cumul_stack_len) {
								if (((this_bulge_nt == 'g' || this_bulge_nt == 'a') &&
								     cumul_3p_nt_stack[this_idx] == 'u') ||
								    ((cumul_3p_nt_stack[this_idx] == 'g' || cumul_3p_nt_stack[this_idx] == 'a') &&
								     this_bulge_nt == 'u') ||
								    (this_bulge_nt == 'g' && cumul_3p_nt_stack[this_idx] == 'c') ||
								    (cumul_3p_nt_stack[this_idx] == 'g' && this_bulge_nt == 'c')) {
									num_states++;
								}
								
								else {
									break;
								}
								
								this_idx++;
							}
						}
						
						else {
							short this_idx = cumul_single_nt_bulge_idx[b];
							uchar this_bulge_nt = cumul_single_nt_bulge[b];
							
							while (this_idx >= 0) {
								if (((this_bulge_nt == 'g' || this_bulge_nt == 'a') &&
								     cumul_5p_nt_stack[this_idx] == 'u') ||
								    ((cumul_5p_nt_stack[this_idx] == 'g' || cumul_5p_nt_stack[this_idx] == 'a') &&
								     this_bulge_nt == 'u') ||
								    (this_bulge_nt == 'g' && cumul_5p_nt_stack[this_idx] == 'c') ||
								    (cumul_5p_nt_stack[this_idx] == 'g' && this_bulge_nt == 'c')) {
									num_states++;
								}
								
								else {
									break;
								}
								
								this_idx--;
							}
							
							this_idx = (short) (cumul_single_nt_bulge_idx[b] + 1);
							
							while (this_idx < this_cumul_stack_len) {
								if (((this_bulge_nt == 'g' || this_bulge_nt == 'a') &&
								     cumul_5p_nt_stack[this_idx] == 'u') ||
								    ((cumul_5p_nt_stack[this_idx] == 'g' || cumul_5p_nt_stack[this_idx] == 'a') &&
								     this_bulge_nt == 'u') ||
								    (this_bulge_nt == 'g' && cumul_5p_nt_stack[this_idx] == 'c') ||
								    (cumul_5p_nt_stack[this_idx] == 'g' && this_bulge_nt == 'c')) {
									num_states++;
								}
								
								else {
									break;
								}
								
								this_idx++;
							}
						}
						
						if (num_states > 1) {
							this_fe -= RT_CONSTANT * log (num_states);
						}
					}
				}
			}
			
			else
				if (1 == this_cumul_stack_len) {
					/*
					 * tackle case when a single base pair precedes, succeeds (or both), internal loops or large bulges
					 */
					char this_fp_nt = cumul_5p_nt_stack[0],
					     this_tp_nt = cumul_3p_nt_stack[0];
					     
					if (((this_fp_nt == 'a' || this_fp_nt == 'g') && this_tp_nt == 'u') ||
					    (this_fp_nt == 'u' && (this_tp_nt == 'a' || this_tp_nt == 'g'))) {
						if (!have_internal_loop) {
							this_fe += PENALTY_AU_GU;
						}
					}
					
					have_internal_loop = false;
					
					if (0 < num_5p_nt_in_between && 0 < num_3p_nt_in_between) {
						have_internal_loop = true;
					}
				}
				
			/*
			 * process bulges (length>1) and internal loops;
			 * first check if any constraints present and if so skip loop/bulge processing
			 */
			if (0 < num_5p_nt_in_between || 0 < num_3p_nt_in_between) {
				bool skip = false;
				
				if (this_linked_bp->fp_elements || next_linked_bp->tp_elements) {
					skip = true;
				}
				
				if (!skip) {
					if (have_internal_loop) {
						/*
						 * process internal loop
						 */
						// closing pair 1
						uchar fp_nt1 = cumul_5p_nt_stack[this_cumul_stack_len - 1],
						      tp_nt1 = cumul_3p_nt_stack[this_cumul_stack_len - 1],
						      // mismatch pair 1
						      mm_fp_nt1 = (uchar) seq[next_linked_bp->bp->fp_posn - 3],
						      mm_tp_nt1 = (uchar) seq[next_linked_bp->bp->tp_posn + next_linked_bp->stack_len
						                                                          - 0],
						                  // mismatch pair 2
						                  mm_fp_nt2 = (uchar) seq[next_linked_bp->bp->fp_posn - 2],
						                  mm_tp_nt2 = (uchar) seq[next_linked_bp->bp->tp_posn + next_linked_bp->stack_len
						                                                                                      - 1],
						                              // closing pair 2
						                              fp_nt2 = (uchar) seq[next_linked_bp->bp->fp_posn - 1],
						                              tp_nt2 = (uchar) seq[next_linked_bp->bp->tp_posn + next_linked_bp->stack_len -
						                                                                                                  2];
						                                                                                                  
						/*
						 * 2x2
						 */
						if (2 == num_5p_nt_in_between && 2 == num_3p_nt_in_between) {
							ushort cp1_idx =
							                    INTERNAL_LOOP_2x2_CLOSING_PAIR_IDX[MAP_RNA[fp_nt1]][MAP_RNA[tp_nt1]],
							                    cp2_idx = INTERNAL_LOOP_2x2_CLOSING_PAIR_IDX[MAP_RNA[fp_nt2]][MAP_RNA[tp_nt2]];
							this_fe +=
							                    INTERNAL_LOOP_2x2_PENALTY[INTERNAL_LOOP_2x2_CLOSING_PAIRS_IDX (cp1_idx,
							                                                                                                cp2_idx)]
							                    [INTERNAL_LOOP_2x2_MISMATCH_PAIRS_IDX[MAP_RNA[mm_fp_nt1]][MAP_RNA[mm_tp_nt1]]]
							                    [INTERNAL_LOOP_2x2_MISMATCH_PAIRS_IDX[MAP_RNA[mm_fp_nt2]][MAP_RNA[mm_tp_nt2]]];
						}
						
						/*
						 * 1x2
						 */
						else
							if ((1 == num_5p_nt_in_between && 2 == num_3p_nt_in_between) ||
							    (2 == num_5p_nt_in_between && 1 == num_3p_nt_in_between)) {
								if (1 == num_5p_nt_in_between) {
									ushort cp1_idx =
									                    INTERNAL_LOOP_1x2_CLOSING_PAIR_IDX[MAP_RNA[fp_nt1]][MAP_RNA[tp_nt1]],
									                    cp2_idx = INTERNAL_LOOP_1x2_CLOSING_PAIR_IDX[MAP_RNA[fp_nt2]][MAP_RNA[tp_nt2]];
									this_fe +=           // on the fp side of the loop, for convenience we can use mm_fp_nt2 to represent the single mismatch nt
									                    INTERNAL_LOOP_1x2_PENALTY[INTERNAL_LOOP_1x2_CLOSING_PAIRS_IDX (cp1_idx, cp2_idx,
									                                                                                                MAP_RNA[mm_tp_nt2])]
									                    [MAP_RNA[mm_fp_nt2]][MAP_RNA[mm_tp_nt1]];
								}
								
								else {
									// flip fp/nts in order to use the same 1x2 params, but in 3' -> 5' sense
									ushort cp1_idx =
									                    INTERNAL_LOOP_1x2_CLOSING_PAIR_IDX[MAP_RNA[tp_nt2]][MAP_RNA[fp_nt2]],
									                    cp2_idx = INTERNAL_LOOP_1x2_CLOSING_PAIR_IDX[MAP_RNA[tp_nt1]][MAP_RNA[fp_nt1]];
									this_fe += INTERNAL_LOOP_1x2_PENALTY[INTERNAL_LOOP_1x2_CLOSING_PAIRS_IDX (
									                                                                            cp1_idx, cp2_idx, MAP_RNA[mm_fp_nt1])]
									           [MAP_RNA[mm_tp_nt2]][MAP_RNA[mm_fp_nt2]];
								}
							}
							
							/*
							 * 1x1
							 */
							else
								if (1 == num_5p_nt_in_between && 1 == num_3p_nt_in_between) {
									ushort cp1_idx =
									                    INTERNAL_LOOP_1x1_CLOSING_PAIR_IDX[MAP_RNA[fp_nt1]][MAP_RNA[tp_nt1]],
									                    cp2_idx = INTERNAL_LOOP_1x1_CLOSING_PAIR_IDX[MAP_RNA[fp_nt2]][MAP_RNA[tp_nt2]];
									// on both fp and tp sides, for convenience use mm_fp_nt2 and mm_tp_nt2 to represent the single mismatch nt
									this_fe += INTERNAL_LOOP_1x1_PENALTY[INTERNAL_LOOP_1x1_CLOSING_PAIRS_IDX (
									                                                                            cp1_idx, cp2_idx)]
									           [MAP_RNA[mm_fp_nt2]][MAP_RNA[mm_tp_nt2]];
								}
								
								/*
								 * other lengths
								 */
								else {
									if (num_5p_nt_in_between + num_3p_nt_in_between <=
									    MAX_EXP_HAIRPIN_LOOP_INITIATION_PENALTY) {
										// limit penalty's to MAX_EXP_INTERNAL_LOOP_INITIATION_PENALTY
										this_fe += INTERNAL_LOOP_INITIATION_PENALTY[SAFE_MIN (num_5p_nt_in_between +
										                                                 num_3p_nt_in_between, MAX_EXP_INTERNAL_LOOP_INITIATION_PENALTY)];
									}
									
									else {
										this_fe +=
										                    INTERNAL_LOOP_INITIATION_PENALTY[MAX_EXP_HAIRPIN_LOOP_INITIATION_PENALTY] +
										                    INTERNAL_LOOP_INITIATION_PENALTY_TERM_LN_CONSTANT_A *
										                    log ((num_5p_nt_in_between + num_3p_nt_in_between) / (double)
										                         MAX_EXP_HAIRPIN_LOOP_INITIATION_PENALTY);
									}
									
									if (num_5p_nt_in_between != num_3p_nt_in_between) {
										this_fe += INTERNAL_LOOP_ASYMMETRY_PENALTY * abs (num_5p_nt_in_between -
										                                        num_3p_nt_in_between);
									}
									
									if (((fp_nt1 == 'a' || fp_nt1 == 'g') && (tp_nt1 == 'u')) ||
									    ((tp_nt1 == 'a' || tp_nt1 == 'g') && (fp_nt1 == 'u'))) {
										this_fe += INTERNAL_LOOP_AU_GU_CLOSURE;
									}
									
									if (((fp_nt2 == 'a' || fp_nt2 == 'g') && (tp_nt2 == 'u')) ||
									    ((tp_nt2 == 'a' || tp_nt2 == 'g') && (fp_nt2 == 'u'))) {
										this_fe += INTERNAL_LOOP_AU_GU_CLOSURE;
									}
									
									/*
									 * mismatch bonus - 1x(N-1), 2x3 (symmetric), and other lengths
									 */
									if (1 == num_5p_nt_in_between || 1 == num_3p_nt_in_between) {
										this_fe += INTERNAL_LOOP_1xN_1_TERMINAL_MISMATCH_BONUS;
									}
									
									else
										if ((2 == num_5p_nt_in_between && 3 == num_3p_nt_in_between) ||
										    (3 == num_5p_nt_in_between && 2 == num_3p_nt_in_between)) {
											this_fe += INTERNAL_LOOP_2x3_MISMATCH_BONUS
											           [MAP_RNA[fp_nt1]][MAP_RNA[tp_nt1]]
											           [MAP_RNA[mm_fp_nt1]]
											           [MAP_RNA[ (uchar) seq[ (this_linked_bp->bp->tp_posn) - 2]]];
											this_fe += INTERNAL_LOOP_2x3_MISMATCH_BONUS
											           [MAP_RNA[tp_nt2]][MAP_RNA[fp_nt2]]
											           [MAP_RNA[mm_tp_nt2]]
											           [MAP_RNA[mm_fp_nt2]];
										}
										
										else {
											// all other lengths
											this_fe += INTERNAL_LOOP_OTHER_LENGTH_MISMATCH_BONUS
											           [MAP_RNA[fp_nt1]][MAP_RNA[tp_nt1]]
											           [MAP_RNA[mm_fp_nt1]]
											           [MAP_RNA[ (uchar) seq[ (this_linked_bp->bp->tp_posn) - 2]]];
											this_fe += INTERNAL_LOOP_OTHER_LENGTH_MISMATCH_BONUS
											           [MAP_RNA[tp_nt2]][MAP_RNA[fp_nt2]]
											           [MAP_RNA[mm_tp_nt2]]
											           [MAP_RNA[mm_fp_nt2]];
										}
								}
					}
					
					else
						if ((num_5p_nt_in_between > 1 && !num_3p_nt_in_between) ||
						    (!num_5p_nt_in_between && num_3p_nt_in_between > 1)) {
							/*
							 * process bulge of 2 or more nt
							 */
							ushort bulge_loop_len = num_5p_nt_in_between ? num_5p_nt_in_between :
							                        num_3p_nt_in_between;
							                        
							if (bulge_loop_len <= BULGE_LOOP_LEN_CUTOFF) {
								this_fe += BULGE_INITIATION_PENALTY[bulge_loop_len];
							}
							
							else {
								this_fe += BULGE_INITIATION_PENALTY[BULGE_LOOP_LEN_CUTOFF] +
								           BULGE_LOOP_LEN6PLUS_RT_CONSTANT_A * RT_CONSTANT * log (bulge_loop_len /
								                                                   (double) BULGE_LOOP_LEN_CUTOFF);
							}
						}
				}
			}
			
			/*
			 * process hairpin loop;
			 * first check if any constraints present and if so skip hairpin loop processing
			 */
			if (score_loop && i == this_nested_helices->numels - 1) {
				bool skip = false;
				
				if (this_linked_bp->fp_elements) {
					if (this_linked_bp->fp_elements->unpaired->length > 1) {
						skip = true;
					}
				}
				
				if (!skip) {
					// this is a terminal helix in this branch, with at least MIN_NT_IN_LOOP nucleotides in a loop
					// note: even if we have aggregated a cumulative stack, this_linked_bp remains to be the most
					//       current stack of this branch
					ushort num_nt_in_loop = this_linked_bp->bp->tp_posn -
					                        (this_linked_bp->bp->fp_posn + this_linked_bp->stack_len);
					// first check if a special hairpin sequence is present
					bool special_found = false;
					
					if (num_nt_in_loop <= MAX_SPECIAL_HAIRPIN_LOOP_LENGTH) {
						char this_loop_sequence[num_nt_in_loop + 1];
						
						for (ushort ls = 0; ls < num_nt_in_loop; ls++) {
							this_loop_sequence[ls] = seq[this_linked_bp->bp->fp_posn +
							                             this_linked_bp->stack_len - 1 + ls];
						}
						
						for (ushort hpl = 0; hpl <= MAX_NUM_SPECIAL_HAIRPINS_PER_LENGTH; hpl++) {
							if (!SPECIAL_HAIRPIN_LOOP_PENALTY[num_nt_in_loop][hpl].penalty) {
								break;
							}
							
							else {
								char closing_fp_nt = seq[this_linked_bp->bp->fp_posn + this_linked_bp->stack_len
								                                                     - 2],
								                     closing_tp_nt = seq[this_linked_bp->bp->tp_posn - 1];
								                     
								if (closing_fp_nt == SPECIAL_HAIRPIN_LOOP_PENALTY[num_nt_in_loop][hpl].seq[0] &&
								    closing_tp_nt ==
								    SPECIAL_HAIRPIN_LOOP_PENALTY[num_nt_in_loop][hpl].seq[num_nt_in_loop + 1]) {
									// closing pair match -> check loop sequence
									ushort ls = 0;
									
									while (ls < num_nt_in_loop) {
										if (this_loop_sequence[ls] !=
										    SPECIAL_HAIRPIN_LOOP_PENALTY[num_nt_in_loop][hpl].seq[ls + 1]) {
											break;
										}
										
										ls++;
									}
									
									if (ls == num_nt_in_loop) {
										special_found = true;
										this_fe += SPECIAL_HAIRPIN_LOOP_PENALTY[num_nt_in_loop][hpl].penalty;
										break;
									}
								}
							}
						}
					}
					
					if (!special_found) {
						/*
						 * if no special sequence found in loop apply parameters for 3 and >3 cases separately
						 */
						if (num_nt_in_loop == 3) {
							this_fe += HAIRPIN_LOOP_PENALTY[3];
							char term_fp_nt = seq[this_linked_bp->bp->fp_posn + this_linked_bp->stack_len -
							                                                  1],
							                  term_mid_nt = seq[this_linked_bp->bp->fp_posn + this_linked_bp->stack_len],
							                  term_tp_nt = seq[this_linked_bp->bp->fp_posn + this_linked_bp->stack_len + 1];
							                  
							if (term_fp_nt == 'c' && term_mid_nt == 'c' && term_tp_nt == 'c') {
								this_fe += C3_LOOP_PENALTY;
							}
						}
						
						else
							if (4 <= num_nt_in_loop) {
								// limit penalty's to MAX_EXP_HAIRPIN_LOOP_INITIATION_PENALTY
								this_fe += HAIRPIN_LOOP_PENALTY[SAFE_MIN (num_nt_in_loop,
								                                        MAX_EXP_HAIRPIN_LOOP_INITIATION_PENALTY)];
								uchar closing_fp_nt = (uchar) seq[this_linked_bp->bp->fp_posn +
								                                                              this_linked_bp->stack_len - 2],
								                      closing_tp_nt = (uchar) seq[this_linked_bp->bp->tp_posn - 1],
								                      term_fp_nt = (uchar) seq[this_linked_bp->bp->fp_posn + this_linked_bp->stack_len
								                                                                                          - 1],
								                                   term_tp_nt = (uchar) seq[this_linked_bp->bp->tp_posn - 2];
								this_fe +=
								                    TERMINAL_MISMATCH_ENERGIES[MAP_RNA[closing_fp_nt]][MAP_RNA[closing_tp_nt]][MAP_RNA[term_fp_nt]][MAP_RNA[term_tp_nt]];
								                    
								if (closing_fp_nt == 'g' && closing_tp_nt == 'u' &&
								    this_linked_bp->stack_len >= 3 &&
								    seq[this_linked_bp->bp->fp_posn + this_linked_bp->stack_len - 3] == 'g' &&
								    seq[this_linked_bp->bp->fp_posn + this_linked_bp->stack_len - 4] == 'g') {
									this_fe += SPECIAL_GU_CLOSURE_BONUS;
								}
								
								if (0 != FIRST_MISMATCH_BONUS[MAP_RNA[term_fp_nt]][MAP_RNA[term_tp_nt]]) {
									this_fe += FIRST_MISMATCH_BONUS[MAP_RNA[term_fp_nt]][MAP_RNA[term_tp_nt]];
								}
								
								if (term_fp_nt == 'c' && term_tp_nt == 'c') {
									ushort c = 0;
									bool is_c_loop = true;
									
									while (c < num_nt_in_loop - 2) {
										if (seq[this_linked_bp->bp->fp_posn + this_linked_bp->stack_len + c] != 'c') {
											is_c_loop = false;
											break;
										}
										
										c++;
									}
									
									if (is_c_loop) {
										this_fe += C_LOOP_PENALTY_TERM_A * num_nt_in_loop + C_LOOP_PENALTY_TERM_B;
									}
								}
							}
					}
				}
			}
			
			cumul_fe += this_fe;
		}
		
		i++;
	}
	
	this_branch->mfe_estimate = cumul_fe;
}

static inline bool check_coaxial_stacking (ntp_branch base,
                                        ntp_branch this_branch, float *this_fe, const char *seq) {
	// get last helix of base branch
	ntp_linked_bp base_linked_bp = list_get_at (base->nested_helices,
	                                        base->nested_helices->numels - 1),
	                               // get first helix of multifurcated branch
	                               this_branch_linked_bp = list_get_at (this_branch->nested_helices, 0);
	nt_rel_seq_len fp_nt_diff = this_branch_linked_bp->bp->fp_posn -
	                            (base_linked_bp->bp->fp_posn + base_linked_bp->stack_len),
	                            tp_nt_diff = (nt_rel_seq_len) (base_linked_bp->bp->tp_posn -
	                                                                    (this_branch_linked_bp->bp->tp_posn + this_branch_linked_bp->stack_len - 1) -
	                                                                    1);
	                                                                    
	if (2 > fp_nt_diff || 2 > tp_nt_diff) {
		if (!fp_nt_diff || !tp_nt_diff) {
			// flush coaxial stacking - apply WC helix params
			*this_fe = STACK_ENERGIES[MAP_RNA[ (uchar) seq[base_linked_bp->bp->fp_posn +
			                                        base_linked_bp->stack_len - 2]]]
			           [MAP_RNA[ (uchar) seq[base_linked_bp->bp->tp_posn - 1]]]
			           [MAP_RNA[ (uchar) seq[this_branch_linked_bp->bp->fp_posn - 1]]]
			           [MAP_RNA[ (uchar) seq[this_branch_linked_bp->bp->tp_posn +
			                                 this_branch_linked_bp->stack_len - 2]]];
		}
		
		else {
			/*
			 * mismatch mediated coaxial stacking - apply params as per https://rna.urmc.rochester.edu/NNDB/turner04/coax-parameters.html
			 */
			// use this_branch_linked_bp as "continuous backbone" => use terminal mismatch paramaters with 5' and 3' switched
			*this_fe = TERMINAL_MISMATCH_ENERGIES[MAP_RNA[ (uchar)
			                                                 seq[this_branch_linked_bp->bp->tp_posn + this_branch_linked_bp->stack_len - 2]]]
			           [MAP_RNA[ (uchar) seq[this_branch_linked_bp->bp->fp_posn - 1]]]
			           [MAP_RNA[ (uchar) seq[this_branch_linked_bp->bp->tp_posn +
			                                 this_branch_linked_bp->stack_len - 1]]]
			           [MAP_RNA[ (uchar) seq[this_branch_linked_bp->bp->fp_posn - 2]]];
			*this_fe += COAXIAL_STACKING_DISCONTINUOUS_BACKBONE_BONUS;
			uchar disc_mm_fp_nt = (uchar) seq[this_branch_linked_bp->bp->fp_posn - 2],
			      disc_mm_tp_nt = (uchar) seq[this_branch_linked_bp->bp->tp_posn +
			                                                                     this_branch_linked_bp->stack_len - 1];
			                                                                     
			if ((disc_mm_fp_nt == 'a' && disc_mm_tp_nt == 'u') || (disc_mm_tp_nt == 'a' &&
			                                        disc_mm_fp_nt == 'u') ||
			    (disc_mm_fp_nt == 'g' && disc_mm_tp_nt == 'c') || (disc_mm_tp_nt == 'g' &&
			                                            disc_mm_fp_nt == 'c')) {
				*this_fe += COAXIAL_STACKING_DISCONTINUOUS_WC_PAIR_BONUS;
			}
			
			else
				if ((disc_mm_fp_nt == 'g' && disc_mm_tp_nt == 'u') || (disc_mm_tp_nt == 'g' &&
				                                        disc_mm_fp_nt == 'u')) {
					*this_fe += COAXIAL_STACKING_DISCONTINUOUS_GU_PAIR_BONUS;
				}
		}
		
		return true;
	}
	
	else {
		*this_fe = 0;
		return false;
	}
}

static inline float score_fe_elements (ntp_list restrict fe_elements,
                                       const char *seq) {
	float cumul_fe = 0.0f;
	list_iterator_start (fe_elements);
	
	while (list_iterator_hasnext (fe_elements)) {
		ntp_fe_element this_fe_element = list_iterator_next (fe_elements);
		
		if (this_fe_element->is_junction) {
			ntp_junction this_junction = this_fe_element->junction;
			score_branch (this_junction->base, false, seq);
			cumul_fe += this_junction->base->mfe_estimate;
			bool coaxial_stacking_enabled[MAX_BRANCHES_PER_JUNCTION];
			float coaxial_stacking_fe[MAX_BRANCHES_PER_JUNCTION];
			
			for (ushort i = 0; i < this_junction->branches->numels; i++) {
				coaxial_stacking_enabled[i] = false;
				coaxial_stacking_fe[i] = 0.0f;
			}
			
			ntp_list this_branches = this_junction->branches;
			list_iterator_start (this_branches);
			ushort num_branches = 1;
			
			while (list_iterator_hasnext (this_branches)) {
				num_branches++;
				ntp_branch this_branch = list_iterator_next (this_branches);
				score_branch (this_branch, true, seq);
				coaxial_stacking_enabled[num_branches - 2] = check_coaxial_stacking (
				                                        this_junction->base, this_branch, &coaxial_stacking_fe[num_branches - 2], seq);
				cumul_fe += this_branch->mfe_estimate;
			}
			
			float lowest_coaxial_stacking_fe = (float) USHRT_MAX;
			ushort lowest_branch = 0;
			
			for (ushort i = 0; i < this_junction->branches->numels; i++) {
				if (coaxial_stacking_enabled[i] &&
				    coaxial_stacking_fe[i] < lowest_coaxial_stacking_fe) {
					lowest_coaxial_stacking_fe = coaxial_stacking_fe[i];
					lowest_branch = (ushort) (i + 1);
				}
			}
			
			if (lowest_branch) {
				cumul_fe += lowest_coaxial_stacking_fe;
			}
			
			list_iterator_stop (this_branches);
		}
		
		else {
			ntp_list this_branches = this_fe_element->branches;
			list_iterator_start (this_branches);
			
			while (list_iterator_hasnext (this_branches)) {
				ntp_branch this_branch = list_iterator_next (this_branches);
				score_branch (this_branch, true, seq);
				cumul_fe += this_branch->mfe_estimate;
			}
			
			list_iterator_stop (this_branches);
		}
	}
	
	list_iterator_stop (fe_elements);
	return cumul_fe;
}

static inline float score_constraint_fe (ntp_linked_bp linked_bp,
                                        const char *seq) {
	ntp_linked_bp this_linked_bp = linked_bp, next_linked_bp = NULL;
	short last_element_found_idx = -1, this_element_idx = 0;
	ntp_constraint last_constraint_found = NULL;
	bool first_constraint_element_found = false;
	uchar first_element_idx = 0, first_element_length = 0;
	nt_rel_seq_posn cp1_fp_idx = 0, cp1_tp_idx = 0, cp2_fp_idx = 0,
	                cp2_tp_idx = 0;    // constraint seq indices (0-indexed positions) 1 and 2
	float cum_constraint_fe = 0.0f;
	
	while (this_linked_bp) {
		bool complete_constraint_found = false;
		
		if (this_linked_bp->fp_elements) {
			// start/continue search on this linked_bp's fp_elements
			ntp_element this_fp_element = this_linked_bp->fp_elements;
			
			while (this_fp_element) {
				if (this_fp_element->unpaired->i_constraint.reference->type == pseudoknot) {
					// only tackle PKs -> BTs are handled in score_fe_elements
					// as an integral part of helical structure
					
					// skip any elements already processed in previous iterations
					if (!first_constraint_element_found) {
						if (this_element_idx > last_element_found_idx) {
							last_constraint_found = this_fp_element->unpaired->i_constraint.reference;
							
							if (this_linked_bp->bp->fp_posn) {
								first_element_idx = (uchar) (this_linked_bp->bp->fp_posn +
								                             this_linked_bp->stack_len - 1 + this_fp_element->unpaired->dist);
							}
							
							else {
								// use next_linked_bp if this_linked_bp is a wrapper
								first_element_idx = (uchar) (next_linked_bp->bp->fp_posn - 2 -
								                             this_fp_element->unpaired->dist);
							}
							
							first_element_length = this_fp_element->unpaired->length;
							last_element_found_idx = this_element_idx;
							first_constraint_element_found = true;
						}
					}
					
					else {
						if (this_element_idx > last_element_found_idx &&
						    this_fp_element->unpaired->i_constraint.reference == last_constraint_found) {
							if (this_linked_bp->bp->fp_posn) {
								if (first_element_idx < this_linked_bp->bp->fp_posn + this_linked_bp->stack_len
								    - 1 + this_fp_element->unpaired->dist) {
									cp1_fp_idx = first_element_idx;
									cp1_tp_idx = (nt_rel_seq_posn) (first_element_idx + first_element_length - 1);
									cp2_fp_idx = (nt_rel_seq_posn) (this_linked_bp->bp->fp_posn +
									                                this_linked_bp->stack_len - 1 + this_fp_element->unpaired->dist);
									cp2_tp_idx = (nt_rel_seq_posn) (this_linked_bp->bp->fp_posn +
									                                this_linked_bp->stack_len - 1 + this_fp_element->unpaired->dist +
									                                this_fp_element->unpaired->length - 1);
								}
								
								else {
									cp2_fp_idx = first_element_idx;
									cp2_tp_idx = (nt_rel_seq_posn) (first_element_idx + first_element_length - 1);
									cp1_fp_idx = (nt_rel_seq_posn) (this_linked_bp->bp->fp_posn +
									                                this_linked_bp->stack_len - 1 + this_fp_element->unpaired->dist);
									cp1_tp_idx = (nt_rel_seq_posn) (this_linked_bp->bp->fp_posn +
									                                this_linked_bp->stack_len - 1 + this_fp_element->unpaired->dist +
									                                this_fp_element->unpaired->length - 1);
								}
							}
							
							else {
								if (first_element_idx < next_linked_bp->bp->fp_posn - 2 -
								    this_fp_element->unpaired->dist - this_fp_element->unpaired->length + 1) {
									cp1_fp_idx = first_element_idx;
									cp1_tp_idx = (nt_rel_seq_posn) (first_element_idx + first_element_length - 1);
									cp2_fp_idx = (nt_rel_seq_posn) (next_linked_bp->bp->fp_posn - 2 -
									                                this_fp_element->unpaired->dist - this_fp_element->unpaired->length + 1);
									cp2_tp_idx = (nt_rel_seq_posn) (next_linked_bp->bp->fp_posn - 2 -
									                                this_fp_element->unpaired->dist);
								}
								
								else {
									cp2_fp_idx = first_element_idx;
									cp2_tp_idx = (nt_rel_seq_posn) (first_element_idx + first_element_length - 1);
									cp1_fp_idx = (nt_rel_seq_posn) (next_linked_bp->bp->fp_posn - 2 -
									                                this_fp_element->unpaired->dist - this_fp_element->unpaired->length + 1);
									cp1_tp_idx = (nt_rel_seq_posn) (next_linked_bp->bp->fp_posn - 2 -
									                                this_fp_element->unpaired->dist);
								}
							}
							
							this_element_idx = 0;
							first_constraint_element_found = false;
							complete_constraint_found = true;
							this_linked_bp = linked_bp;
							break;
						}
					}
					
					this_element_idx++;
				}
				
				this_fp_element = this_fp_element->unpaired->next;
			}
		}
		
		if (!complete_constraint_found && this_linked_bp->tp_elements) {
			// continue search on this linked_bp's fp_elements
			ntp_element this_tp_element = this_linked_bp->tp_elements;
			
			while (this_tp_element) {
				if (this_tp_element->unpaired->i_constraint.reference->type == pseudoknot) {
					// only tackle PKs -> BTs are handled in score_fe_elements
					// as an integral part of helical structure
					
					// skip any elements already processed in previous iterations
					if (!first_constraint_element_found) {
						if (this_element_idx > last_element_found_idx) {
							last_constraint_found = this_tp_element->unpaired->i_constraint.reference;
							// note: can safely assume that tp_elements never apply to wrapper bps
							first_element_idx = (uchar) (this_linked_bp->bp->tp_posn +
							                             this_linked_bp->stack_len - 1 + this_tp_element->unpaired->dist);
							first_element_length = this_tp_element->unpaired->length;
							last_element_found_idx = this_element_idx;
							first_constraint_element_found = true;
						}
					}
					
					else {
						if (this_element_idx > last_element_found_idx &&
						    this_tp_element->unpaired->i_constraint.reference == last_constraint_found) {
							if (first_element_idx < this_linked_bp->bp->tp_posn + this_linked_bp->stack_len
							    - 1 + this_tp_element->unpaired->dist) {
								cp1_fp_idx = first_element_idx;
								cp1_tp_idx = (nt_rel_seq_posn) (first_element_idx + first_element_length - 1);
								cp2_fp_idx = (nt_rel_seq_posn) (this_linked_bp->bp->tp_posn +
								                                this_linked_bp->stack_len - 1 + this_tp_element->unpaired->dist);
								cp2_tp_idx = (nt_rel_seq_posn) (this_linked_bp->bp->tp_posn +
								                                this_linked_bp->stack_len - 1 + this_tp_element->unpaired->dist +
								                                this_tp_element->unpaired->length - 1);
							}
							
							else {
								cp2_fp_idx = first_element_idx;
								cp2_tp_idx = (nt_rel_seq_posn) (first_element_idx + first_element_length - 1);
								cp1_fp_idx = (nt_rel_seq_posn) (this_linked_bp->bp->tp_posn +
								                                this_linked_bp->stack_len - 1 + this_tp_element->unpaired->dist);
								cp1_tp_idx = (nt_rel_seq_posn) (this_linked_bp->bp->tp_posn +
								                                this_linked_bp->stack_len - 1 + this_tp_element->unpaired->dist +
								                                this_tp_element->unpaired->length - 1);
							}
							
							this_element_idx = 0;
							first_constraint_element_found = false;
							complete_constraint_found = true;
							this_linked_bp = linked_bp;
							break;
						}
					}
					
					this_element_idx++;
				}
				
				this_tp_element = this_tp_element->unpaired->next;
			}
		}
		
		if (complete_constraint_found) {
			if (cp1_tp_idx - cp1_fp_idx == cp2_tp_idx -
			    cp2_fp_idx) { // TODO: this condition should always be true
				nt_rel_seq_len num_stacks = (nt_rel_seq_len) (cp1_tp_idx - cp1_fp_idx);
				nt_rel_seq_len i = 0;
				float this_fe = 0.0f;
				
				while (i < num_stacks) {
					uchar fp1_nt = (uchar) seq[cp1_fp_idx + i  ],
					      tp1_nt = (uchar) seq[cp2_tp_idx - i  ],
					      fp2_nt = (uchar) seq[cp1_fp_idx + i + 1],
					      tp2_nt = (uchar) seq[cp2_tp_idx - i - 1];
					this_fe += STACK_ENERGIES[MAP_RNA[fp1_nt]]
					           [MAP_RNA[tp1_nt]]
					           [MAP_RNA[fp2_nt]]
					           [MAP_RNA[tp2_nt]];
					i++;
				}
				
				cum_constraint_fe += this_fe;
			}
		}
		
		else {
			next_linked_bp = this_linked_bp;
			this_linked_bp = this_linked_bp->prev_linked_bp;
		}
	}
	
	return cum_constraint_fe;
}

float get_turner_mfe_estimate (ntp_linked_bp restrict linked_bp,
                               const char *seq) {
	#ifndef NO_FULL_CHECKS
                               
	if (!linked_bp) {
		COMMIT_DEBUG (REPORT_ERRORS, MFE, "NULL linked_bp passed to get_mfe_estimate",
		              false);
		return STACK_MFE_FAILED;
	}
	
	if (!seq || !strlen (seq)) {
		COMMIT_DEBUG (REPORT_ERRORS, MFE,
		              "NULL or empty seq passed to get_mfe_estimate", false);
		return STACK_MFE_FAILED;
	}
	
	const nt_seq_len seq_len = strlen (seq);
	#endif
	ntp_list fe_elements = get_fe_elements (linked_bp);
	float turner_estimate = score_fe_elements (fe_elements,
	                                        seq) + score_constraint_fe (linked_bp, seq);
	destroy_fe_elements (fe_elements);
	return turner_estimate;
}

bool initialize_mfe() {
	COMMIT_DEBUG (REPORT_INFO, MFE, "initializing mfe in initialize_mfe", true);
	
	for (int i = 0;
	     i < STACK_MFE_LIMITS_MAX_LENGTH - STACK_MFE_LIMITS_MIN_LENGTH + 1; i++) {
		stack_mfe_sze[i] = 0;
	}
	
	for (int i = STACK_MFE_LIMITS_MIN_LENGTH; i <= STACK_MFE_LIMITS_MAX_LENGTH;
	     i++) {
		char fn[FILENAME_MAX + 1];
		sprintf (fn, "%s%s", STACK_MFE_DIR_PATH,
		         STACK_MFE_DISTRIB_FN[i - STACK_MFE_LIMITS_MIN_LENGTH]);
		COMMIT_DEBUG1 (REPORT_INFO, MFE,
		               "loading mfe stack distribution data from \"%s\" in initialize_mfe", fn, false);
		FILE *f = fopen (fn, "r");
		
		if (f != NULL) {
			unsigned short line_count = 0;
			
			while (!feof (f)) {
				const char ch = (char) fgetc (f);
				
				if (ch == '\n') {
					line_count++;
				}
			}
			
			fclose (f);
			
			if (!line_count) {
				COMMIT_DEBUG1 (REPORT_ERRORS, MFE,
				               "could not read from file \"%s\" or file is empty", fn, false);
				return false;
			}
			
			else {
				f = fopen (fn, "r");
			}
			
			stack_mfe_val[i - STACK_MFE_LIMITS_MIN_LENGTH] = (float *) calloc (line_count,
			                                        sizeof (float));
			                                        
			if (stack_mfe_val[i - STACK_MFE_LIMITS_MIN_LENGTH] == NULL) {
				COMMIT_DEBUG1 (REPORT_ERRORS, MFE,
				               "cannot allocate stack mfe values memory for \"%s\"", fn, false);
				fclose (f);
				finalize_mfe();
				return false;
			}
			
			stack_mfe_cnt[i - STACK_MFE_LIMITS_MIN_LENGTH] = (long long *) calloc (
			                                        line_count, sizeof (unsigned long long));
			                                        
			if (stack_mfe_cnt[i - STACK_MFE_LIMITS_MIN_LENGTH] == NULL) {
				COMMIT_DEBUG1 (REPORT_ERRORS, MFE,
				               "cannot allocate stack mfe counts memory for \"%s\"", fn, false);
				fclose (f);
				free (stack_mfe_val[i -
				                      STACK_MFE_LIMITS_MIN_LENGTH]);  // stack_mfe_sze still not set to line_count -> de-alloc here
				finalize_mfe();
				return false;
			}
			
			stack_mfe_sze[i - STACK_MFE_LIMITS_MIN_LENGTH] = line_count;
			char  this_line[STACK_MFE_MAX_LINE_LENGTH + 1];
			float mfe_limits_min = STACK_MFE_LIMITS[i - STACK_MFE_LIMITS_MIN_LENGTH][0],
			      mfe_limits_max = STACK_MFE_LIMITS[i - STACK_MFE_LIMITS_MIN_LENGTH][1],
			      this_min, this_max, tmp_f = 0.0f;
			unsigned short is_first = 1, line_num = 1;
			ulong total_count = 0;
			long long tmp_i;
			char *junk;
			
			while (fgets (this_line, STACK_MFE_MAX_LINE_LENGTH, f) != NULL) {
				char *tok = strtok (this_line, ",");
				
				if (tok == NULL) {
					COMMIT_DEBUG2 (REPORT_ERRORS, MFE,
					               "mfe value expected at line number %d in \"%s\"", line_num, fn, false);
					fclose (f);
					finalize_mfe();
					return false;
				}
				
				tmp_f = (float) strtod (tok, &junk);
				
				if (is_first) {
					is_first = 0;
					this_max = tmp_f;
					
					if (this_max != mfe_limits_max) {
						COMMIT_DEBUG2 (REPORT_ERRORS, MFE,
						               "the FE value of the first line in \"%s\" does not correspond to max FE (%f)",
						               fn, mfe_limits_max, false);
						fclose (f);
						finalize_mfe();
						return false;
					}
				}
				
				tok = strtok (NULL, ",");
				
				if (tok == NULL) {
					COMMIT_DEBUG2 (REPORT_ERRORS, MFE,
					               "mfe count expected at line number %d in \"%s\"", line_num, fn, false);
					fclose (f);
					finalize_mfe();
					return false;
				}
				
				tmp_i = strtoll (tok, &junk, 10);
				total_count += tmp_i;
				stack_mfe_val[i - STACK_MFE_LIMITS_MIN_LENGTH][line_num - 1] = tmp_f;
				stack_mfe_cnt[i - STACK_MFE_LIMITS_MIN_LENGTH][line_num - 1] = tmp_i;
				line_num++;
			}
			
			this_min = tmp_f;
			
			if (this_min != mfe_limits_min) {
				COMMIT_DEBUG2 (REPORT_ERRORS, MFE,
				               "the FE value of the last line in \"%s\" does not correspond to min FE (%f)",
				               fn, mfe_limits_min, false);
				fclose (f);
				finalize_mfe();
				return false;
			}
			
			const double given_count = pow (6, i);
			
			if (fabs (total_count - given_count) > FLOAT_DELTA) {
				COMMIT_DEBUG3 (REPORT_ERRORS, MFE,
				               "total count (%llu) in \"%s\" does not match expectation (%.0f)", 100,
				               total_count, fn, given_count, false);
				fclose (f);
				finalize_mfe();
				return false;
			}
			
			stack_mfe_total_cnt[i - STACK_MFE_LIMITS_MIN_LENGTH] = total_count;
			fclose (f);
		}
	}
	
	return true;
}

void finalize_mfe() {
	for (int i = 0;
	     i < STACK_MFE_LIMITS_MAX_LENGTH - STACK_MFE_LIMITS_MIN_LENGTH + 1; i++) {
		if (stack_mfe_sze[i]) {
			free (stack_mfe_val[i]);
			free (stack_mfe_cnt[i]);
			stack_mfe_sze[i] = 0;
		}
	}
}
