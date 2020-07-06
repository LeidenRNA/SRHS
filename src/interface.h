#ifndef RNA_INTERFACE_H
#define RNA_INTERFACE_H

#include <stdbool.h>
#include "m_model.h"
#include "util.h"

#define MAX_ERR_STRING_LEN      100

#if MAX_POS_VAR>9
	#define MAX_POS_VAR_CHAR        ((char)('a'+(MAX_POS_VAR-10)))
#else
	#define MAX_POS_VAR_CHAR        ((char)('0'+MAX_POS_VAR))
#endif

// interface assumes at least one character is defined for each CSSD symbol type

#define S_MAX_LEN               2               // maximum number of unique literals allowed in S_* (except for S_WHITE_SPACE)

#define S_UNSTRUCTURED_RESIDUE  ":"
#define S_OPEN_MULTI            "("
#define S_CLOSE_MULTI           ")"
#define S_OPEN_TERM             "<"
#define S_CLOSE_TERM            ">"
#define S_OPEN_PK               "[{"            // in S_OPEN_/S_CLOSE_PK chars relate to unique instances (i.e. for each symbol, a single PK instance)
#define S_CLOSE_PK              "]}"
#define S_HAIRPIN_RESIDUE       "_"
#define S_MULTI_RESIDUE         ","
#define S_INTERIOR_RESIDUE      "-"
#define S_BASE_TRIPLE_SINGLE    "."
#define S_BASE_TRIPLE_PAIR      "~"

// "neutral" symbols used for presentation of results/hits
#define SS_NEUTRAL_HAIRPIN_RESIDUE	       S_HAIRPIN_RESIDUE[0]
#define SS_NEUTRAL_UNSTRUCTURED_RESIDUE    S_UNSTRUCTURED_RESIDUE[0]
#define SS_NEUTRAL_UNKNOWN_SYMBOL	         SS_NEUTRAL_HAIRPIN_RESIDUE	//  set hairpin loop residues to represent any ("neutral") symbol
#define SS_NEUTRAL_INTERIOR_RESIDUE 	     S_INTERIOR_RESIDUE[0]
#define SS_NEUTRAL_MULTI_RESIDUE  	       S_MULTI_RESIDUE[0]
#define SS_NEUTRAL_OPEN_TERM    	         S_OPEN_TERM[0]
#define SS_NEUTRAL_CLOSE_TERM   	         S_CLOSE_TERM[0]
#define SS_NEUTRAL_OPEN_MULTI              S_OPEN_MULTI[0]
#define SS_NEUTRAL_CLOSE_MULTI             S_CLOSE_MULTI[0]
#define SS_NEUTRAL_OPEN_PK      	         S_OPEN_PK[0]
#define SS_NEUTRAL_CLOSE_PK     	         S_CLOSE_PK[0]
#define SS_NEUTRAL_BT_SINGLE    	         S_BASE_TRIPLE_SINGLE[0]
#define SS_NEUTRAL_BT_PAIR      	         S_BASE_TRIPLE_PAIR[0]
#define SS_NEUTRAL_PK           	         '*'             // used for when unpaired linked_bps are used to refer to PKs (-> OPEN/CLOSE info is lost)

#define S_WHITE_SPACE                      " \t\r\n"       // the first char (currently, space) is used by various routines to populate single-char whitespace

#define S_HIT_SEPARATOR                    '\t'

/* length in bytes (uchar) of fixed preamble, inserted ahead of result hit strings:
 * S_HIT_DATA_REF_ID_LEN x string representation of ref_id + 1 x S_HIT_SEPARATOR
 * 24 x string representation of job_id + 1 x S_HIT_SEPARATOR
 *  9 x string representation of elapsed_time + 1 x S_HIT_SEPARATOR
 * 11 x string representation of fp_posn for hit + 1 x S_HIT_SEPARATOR
 * 10 x string representation of free energy estimate + 1 x (just before hit) S_HIT_SEPARATOR
 *  1 x string terminator
 */
#define S_HIT_DATA_LENGTH                  79

/*
 * symbolic constraints for sequences, CSSDs attributes (as used in validation functions)
 * note: a-zA-Z0-9 chars assumed as valid
 */
#define S_DEFINITION_SPECIAL_CHARS	       " ,_-.:/'()"
#define S_DEFINITION_MIN_LEN               10
#define S_DEFINITION_MAX_LEN               200
#define S_ACCESSION_SPECIAL_CHARS 	       "_-."
#define S_ACCESSION_MIN_LEN 		           3
#define S_ACCESSION_MAX_LEN 		           20
#define S_GROUP_SPECIAL_CHARS       	     "-"
#define S_GROUP_MIN_LEN             	     3
#define S_GROUP_MAX_LEN              	     12

// note: sequence validity is checked using is_seq_valid (sequences.c); seq length bounds are derived from MIN_STACK_LEN, MIN_IDIST, and MAX_SEQ_LEN

bool convert_CSSD_to_model (const char *restrict ss,
                            const char *restrict pos_var, ntp_model *restrict model,
                            char *restrict *err_msg);
bool compare_CSSD_model_strings (const char *restrict ss,
                                 const char *restrict pos_var_string, ntp_model model);

bool get_model_limits (ntp_model model,
                       ntp_seg_size fp_lead_min_span, ntp_seg_size fp_lead_max_span,
                       ntp_stack_size stack_min_size, ntp_stack_size stack_max_size,
                       ntp_stack_idist stack_min_idist, ntp_stack_idist stack_max_idist,
                       ntp_seg_size tp_trail_min_span, ntp_seg_size tp_trail_max_span,
                       ntp_element *largest_stack_el);

void join_cssd (const char *ss, const char *pos_var, char **cssd);
void split_cssd (const char *cssd, char **ss, char **pos_var);

bool is_valid_definition (const char *restrict definition, char *restrict *err);
bool is_valid_accession (const char *restrict accession, char *restrict *err);
bool is_valid_group (const char *restrict group, char *restrict *err);
bool is_valid_sequence (const char *restrict sequence, char *restrict *err);

#endif //RNA_INTERFACE_H
