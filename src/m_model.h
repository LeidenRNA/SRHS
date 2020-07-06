#ifndef RNA_M_MODEL_H
#define RNA_M_MODEL_H

#include <stdbool.h>
#include "util.h"
#include "simclist.h"
#include "sequence.h"
#include "limits.h"

/*
 * model customization
 */
#define MIN_IDIST                     3
#define MAX_CONSTRAINT_MATCHES        100
#define MAX_CHAIN_MATCHES             50                    // limited by uchar
#define MAX_ELEMENT_MATCHES           250                   //      ditto
#define MAX_BP_PER_STACK_IDIST        1000
#define MAX_MODEL_STRING_LEN          200
#define MAX_POS_VAR                   25

#define MAX_BRANCHES_PER_JUNCTION     3                     // max number of branches allowed in a junction, excluding base helix

#ifdef SEARCH_SEQ_DETAIL
	#define MAX_TARGETS                   10                // maximum targets (hits) when searching for specific bps using check_bp_candidates_for_targets
#endif

#define MAX_MODEL_SIZE                10000000              // maximum size of (model) search space allowed in one iteration;
// larger values inherently split a model into sub-space models

#define MAX_MODEL_PARTITIONS          100                   // maximum number of model partitions into which a given model can be split (partitioned)
// when dealing with model search spaces that exceed MAX_MODEL_SIZE

#define MAX_HITS_RETURNED             500                   // maximum number of hits returned by search_seq; if exceeded hit list
// will be filtered by FE

#ifdef MULTITHREADED_ON
	#define MODEL_LIST_DESTRUCTION_LOCK_SLEEP_S         1
	#define MODEL_LIST_DESTRUCTION_THREAD_SLEEP_S       1
	#define MODEL_LIST_DESTRUCTION_LOCK_MAX_ATTEMPTS    10  // limited by uchar
#endif

/*
 * data structures
 */

typedef struct _nt_element *ntp_element;
typedef struct _nt_linked_bp *ntp_linked_bp;
typedef struct _nt_constraint *ntp_constraint;
typedef struct _nt_unpaired_element *ntp_unpaired_element;
typedef struct _nt_paired_element *ntp_paired_element;

typedef struct {
	ntp_element first_element;
	ntp_constraint first_constraint;
} nt_model, *ntp_model;

typedef enum element_type {unpaired = 0, paired = 1, no_element_type = -1} nt_element_type;

typedef enum branch_type  {three_prime = 0, five_prime = 1, unbranched = 2, no_branch_type = -1} nt_branch_type,
*ntp_branch_type;
typedef enum constraint_type {pseudoknot = 0, base_triple = 1, no_constraint_type = -1} nt_constraint_type;
typedef enum constraint_element_type {
	constraint_single_element, constraint_fp_element, constraint_tp_element, constraint_no_element
} nt_constraint_element_type;

typedef struct _nt_unpaired_element {
	union {
		struct {
			nt_element_count min, max;
		};
		struct {
			uchar dist, length;
			struct {
				ntp_constraint reference;
				nt_constraint_element_type element_type;
			} i_constraint;
			ntp_linked_bp next_linked_bp;
		};
	};
	nt_element_type  prev_type;
	union {
		ntp_element prev_unpaired;
		struct {
			ntp_element prev_paired;
			enum branch_type prev_branch_type;
		};
	};
	ntp_element next;
} nt_unpaired_element;

typedef struct _nt_paired_element {
	nt_element_count min, max;
	ntp_element      fp_next, tp_next;
} nt_paired_element;

typedef struct _nt_element {
	nt_element_type type;
	union {
		ntp_unpaired_element unpaired;
		ntp_paired_element paired;
	};
} nt_element;

typedef struct {
	ntp_element fp_element, tp_element;
} nt_pseudoknot, *ntp_pseudoknot;

typedef struct {
	ntp_element fp_element, tp_element, single_element;
} nt_base_triple, *ntp_base_triple;

typedef struct _nt_constraint {
	nt_constraint_type type;
	union {
		ntp_pseudoknot pseudoknot;
		ntp_base_triple base_triple;
	};
	ntp_constraint next;
} nt_constraint;

typedef struct {
	nt_rel_seq_posn fp_posn, tp_posn;
} nt_bp, *ntp_bp;

typedef struct _nt_linked_bp {
	const nt_bp *restrict bp;
	nt_stack_size stack_len;
	uchar track_id;
	ntp_element fp_elements;
	ntp_element tp_elements;
	struct _nt_linked_bp *restrict prev_linked_bp;
} nt_linked_bp;

static const nt_bp NULL_BP = {0, 0};
static const nt_stack_size NULL_STACK_LEN = 0;

typedef struct _nt_bp_list_by_element *ntp_bp_list_by_element;
typedef struct _nt_bp_list_by_element {
	nt_list list;
	nt_hit_count stack_counts;
	ntp_element el;
	ntp_bp_list_by_element next;
} nt_bp_list_by_element;

typedef struct {
	ntp_bp_list_by_element lists;
	nt_stack_idist stack_idist;
	short in_extrusion;
} nt_stack, *ntp_stack;

typedef struct {
	ntp_seq sequence;
	ntp_model model;
	nt_list stacks[MAX_STACK_LEN];
} nt_seq_bp, *ntp_seq_bp;

#endif //RNA_M_MODEL_H
