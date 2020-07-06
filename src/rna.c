#include <stdio.h>
#include <unistd.h>
#include <mpi.h>
#include "util.h"
#include "simclist.h"
#include "tests.h"
#include "interface.h"
#include "mfe.h"
#include "filter.h"
#include "datastore.h"
#include "ketopt.h"
#include "distribute.h"
#include "frontend.h"
#if JS_JOBSCHED_TYPE!=JS_NONE
	#include "c_jobsched_server.h"
#endif
#include "allocate.h"
#include "m_list.h"
#include "m_build.h"
#include "m_seq_bp.h"
#include "m_search.h"
#include "rna.h"

/*
 * defines needed to establish rna launch mode, as required
 * for use with ketopt.h
 * rna can be launched using either short (-) form command-
 * line args or long (--) form args
 */

/* use an arbitrary positive integer (> largest ascii code char) to
   distinguish short (ascii-coded) from long (enum-coded) modes */
#define LONG_RNA_MODE 1000

typedef enum {
	FRONTEND_S_MODE = LONG_RNA_MODE + 'r',
	DISTRIBUTE_S_MODE = LONG_RNA_MODE + 'd',
	FILTER_MODE = LONG_RNA_MODE + 'f',
	DISPATCH_MODE = LONG_RNA_MODE + 'p',
	SCAN_MODE = LONG_RNA_MODE + 's',
	COLLECTION_MODE = LONG_RNA_MODE + 'c',
	#if JS_JOBSCHED_TYPE!=JS_NONE
	SCHED_I_MODE = LONG_RNA_MODE + 'i',
	#endif
	TEST_MODE = LONG_RNA_MODE + 't',
	HELP_MODE = LONG_RNA_MODE + 'h',
	UNKNOWN_MODE = USHRT_MAX
} RNA_MODE;

#if JS_JOBSCHED_TYPE!=JS_NONE
	// 7 of the 9 modes can be launched in short mode:
	// FRONTEND_S/DISTRIBUTE_S/FILTER/SCAN/SCHED_I/TEST/HELP
	#define NUM_RNA_SHORT_MODES 7
	// single-dash, short option names for the above;
	// note: - the 'X' in "-X" MUST match exactly the 'Y' in "--Y" or "--Y..."
	//       - no short form for --collection, as it requires an argument
	static char RNA_MODE_SHORT[NUM_RNA_SHORT_MODES] =
	"rdfsith";
#else
	// 6 of the 8 modes can be launched in short mode:
	// FRONTEND_S/DISTRIBUTE_S/FILTER/SCAN/TEST/HELP
	#define NUM_RNA_SHORT_MODES 6
	// same as above, less scheduler interface "i"
	static char RNA_MODE_SHORT[NUM_RNA_SHORT_MODES] =
	"rdfsth";
#endif

typedef enum {
	SS,                        // following are used in FILTER and SCAN mode, except for
	POS_VAR,
	SEQ_NT,
	SEQ_FILENAME,
	FRONTEND_PORT,             // PORT used for frontend service
	BACKEND_PORT,              // PORT used for backend (distribution) service
	BACKEND_SERVER,            // server for backend (distribution) service
	HEADNODE_SERVER,           // head node for job scheduler
	DS_PORT,                   // PORT used to connect to datastore server
	DS_SERVER,                 // server name or IP where datastore is located
	#if JS_JOBSCHED_TYPE!=JS_NONE
	SI_PORT,                   // PORT used to connect to scheduler interface server
	SI_SERVER,                 // server name or IP where scheduler interface is located
	MPI_PORT_NAME,             // MPI port_name used for intercommunication between dispatch (allocate) and scan worker job
	SCHED_JOB_ID,              // the (system) scheduler's assigned job id for a given scan worker running on a worker node
	#endif
	RNA_BIN_FILENAME,          // (optional) name of scanner binary file if used by distribute/dispatch
	
	CREATE_OP,                 // CRUD operations allowed in COLLECTION mode
	READ_OP,
	UPDATE_OP,
	DELETE_OP,
	NO_OP,                     // NOOP for CRUD operations, only required for assignment to vars
	
	/* options used in COLLECTION mode: */
	CD_ID,                     // CD_ID is the datastore-assigned id used for all collection documents
	
	/* options used in SEQUENCES mode: */
	SC_GROUP,
	SC_DEFINITION,
	SC_ACCESSION,
	
	CC_CS_NAME,
	CC_PUBLISHED,
	
	/* options used in JOBS mode: */
	JC_SEQ_ID,
	JC_CSSD_ID,
	JC_JOB_ID,
	JC_JOB_STATUS,
	JC_JOB_ERROR,
	
	/* options used in RESULTS mode: */
	RC_HIT,
	
	/* options used in USERS mode: */
	UC_USER_NAME,
	UC_SUB_ID,
	UC_REF_ID
} RNA_OPTION;

#define MAX_OPTION_NAME_LENGTH       20

#define DISPATCH_ARG_LONG            "dispatch"                // command-line arg used for running dispatch service (under distribute mode)
#define BACKEND_PORT_ARG_LONG        "backend-port"            //                       for port used to run dispatch service
#define DS_SERVER_ARG_LONG           "ds-server"               // command-line arg used for running dispatch service (under distribute mode)
#define DS_PORT_ARG_LONG             "ds-port"                 //                       for port used to run dispatch service
#if JS_JOBSCHED_TYPE!=JS_NONE
	#define SI_SERVER_ARG_LONG       "si-server"               // command-line arg used for connecting to scheduler interface service (under distribute mode)
	#define SI_PORT_ARG_LONG         "si-port"                 //                       for port used to connect to scheduler interface service
#endif
#define RNA_BIN_FILENAME_ARG_LONG    "RNA-bin-filename"        // command-line arg used for scanning mode binary filename (optional, defaults to arg[0])
#define HEADNODE_SERVER_ARG_LONG     "headnode-server"         // command-line arg used for running dispatch service (under distribute mode)

/*
 * misc globals
 */
static uchar
hit[DS_JOB_RESULT_HIT_FIELD_LENGTH];        // string for current search hit

/*
 * static, inline replacements for memset/memcpy - silences google sanitizers
 */
static inline void g_memset (void *p, const char v, const int len) {
	REGISTER
	char *pc = (char *) p;
	
	for (REGISTER int i = 0; i < len; i++) {
		pc[i] = v;
	}
}

static inline void g_memcpy (void *p, const void *r, const int len) {
	REGISTER
	char *pc = (char *) p, *rc = (char *) r;
	
	for (REGISTER int i = 0; i < len; i++) {
		pc[i] = rc[i];
	}
}

/*
 * helper functions
 */
static inline void get_option_string (int opt, char *strn) {
	switch (opt) {
		case SS                 :
			sprintf (strn, "secondary structure (--%s)", SS_ARG_LONG);
			break;
			
		case POS_VAR            :
			sprintf (strn, "positional variables (--%s)", POS_VAR_ARG_LONG);
			break;
			
		case SEQ_NT             :
			sprintf (strn, "nucleotides (--%s)", SEQ_NT_ARG_LONG);
			break;
			
		case SEQ_FILENAME       :
			strcpy (strn, "sequence filename (--seq-fn)");
			break;
			
		case FRONTEND_PORT      :
			strcpy (strn, "frontend port (--frontend-port)");
			break;
			
		case BACKEND_PORT       :
			sprintf (strn, "%s (--%s)", BACKEND_PORT_ARG_LONG, BACKEND_PORT_ARG_LONG);
			break;
			
		case BACKEND_SERVER     :
			strcpy (strn, "backend server name or IP address (--backend-server)");
			break;
			
		case DS_PORT            :
			sprintf (strn, "%s (--%s)", DS_PORT_ARG_LONG, DS_PORT_ARG_LONG);
			break;
			
		case DS_SERVER          :
			sprintf (strn, "%s name or IP address (--%s)", DS_SERVER_ARG_LONG,
			         DS_SERVER_ARG_LONG);
			break;
			#if JS_JOBSCHED_TYPE!=JS_NONE
			
		case SI_PORT            :
			sprintf (strn, "%s (--%s)", SI_PORT_ARG_LONG, SI_PORT_ARG_LONG);
			break;
			
		case SI_SERVER          :
			sprintf (strn, "%s name or IP address (--%s)", SI_SERVER_ARG_LONG,
			         SI_SERVER_ARG_LONG);
			break;
			
		case MPI_PORT_NAME      :
			sprintf (strn, "%s (--%s)", MPI_PORT_NAME_ARG_LONG, MPI_PORT_NAME_ARG_LONG);
			break;
			
		case SCHED_JOB_ID       :
			sprintf (strn, "%s (--%s)", SCHED_JOB_ID_ARG_LONG, SCHED_JOB_ID_ARG_LONG);
			break;
			#endif
			
		case RNA_BIN_FILENAME   :
			sprintf (strn, "%s (--%s)", RNA_BIN_FILENAME_ARG_LONG,
			         RNA_BIN_FILENAME_ARG_LONG);
			break;
			
		case HEADNODE_SERVER    :
			sprintf (strn, "%s name or IP address (--%s)", HEADNODE_SERVER_ARG_LONG,
			         HEADNODE_SERVER_ARG_LONG);
			break;
			
		case CREATE_OP          :
			strcpy (strn, "create operation (--create)");
			break;
			
		case READ_OP            :
			strcpy (strn, "read operation (--read)");
			break;
			
		case UPDATE_OP          :
			strcpy (strn, "update operation (--update)");
			break;
			
		case DELETE_OP          :
			strcpy (strn, "delete operation (--delete)");
			break;
			
		case NO_OP              :
			break;
			
		case CD_ID              :
			strcpy (strn, "document ID (--id)");
			break;
			
		case SC_GROUP           :
			strcpy (strn, "group (--group)");
			break;
			
		case SC_DEFINITION      :
			strcpy (strn, "definition (--definition)");
			break;
			
		case SC_ACCESSION       :
			strcpy (strn, "accession (--accession)");
			break;
			
		case CC_CS_NAME         :
			strcpy (strn, "cs name (--cs-name)");
			break;
			
		case CC_PUBLISHED	:
			strcpy (strn, "cs published (--published)");
			break;
			
		case JC_SEQ_ID          :
			strcpy (strn, "sequence ID (--seq-id)");
			break;
			
		case JC_CSSD_ID         :
			strcpy (strn, "secondary structure descriptor ID (--cssd-id)");
			break;
			
		case JC_JOB_ID          :
			strcpy (strn, "job id (--job-id)");
			break;
			
		case JC_JOB_STATUS      :
			strcpy (strn, "job status (--job-status)");
			break;
			
		case JC_JOB_ERROR	:
			strcpy (strn, "job error (--job-error)");
			break;
			
		case RC_HIT             :
			strcpy (strn, "result hit (--result-hit)");
			break;
			
		case UC_USER_NAME       :
			strcpy (strn, "user name (--user-name)");
			break;
			
		case UC_SUB_ID          :
			strcpy (strn, "subscriber id (--sub-id)");
			break;
			
		case UC_REF_ID          :
			strcpy (strn, "reference id (--ref-id)");
			break;
			
		case FRONTEND_S_MODE    :
			strcpy (strn, "frontend server (--frontend-server, -r)");
			break;
			
		case DISTRIBUTE_S_MODE  :
			strcpy (strn, "distribute server (--distribute-server, -d)");
			break;
			
		case DISPATCH_MODE      :
			sprintf (strn, "%s (--%s)", DISPATCH_ARG_LONG, DISPATCH_ARG_LONG);
			break;
			
		case FILTER_MODE        :
			strcpy (strn, "filter (--filter, -f)");
			break;
			
		case SCAN_MODE          :
			sprintf (strn, "%s (--%s, -%s)", SCAN_MODE_ARG_LONG, SCAN_MODE_ARG_LONG,
			         SCAN_MODE_ARG_SHORT);
			break;
			
		case COLLECTION_MODE    :
			strcpy (strn, "collection (--collection)");
			break;
			#if JS_JOBSCHED_TYPE!=JS_NONE
			
		case SCHED_I_MODE       :
			strcpy (strn, "scheduler interface server (--scheduler-server, -i)");
			break;
			#endif
			
		case TEST_MODE          :
			strcpy (strn, "run search algorithm tests (--test, -t)");
			break;
			
		case HELP_MODE          :
			strcpy (strn, "help (--help, -h)");
			break;
			
		default                 :
			break;
	}
}

/*
 * filter:  launch rna from command-line and filter input
 *          sequence (filename) against input secondary
 *          structure and positional variables
 *
 * args:    filter and datastore server/port,
 *          secondary structure and positional variables,
 *          input sequence filename,
 *          user reference id
 *
 * returns: EXEC_SUCCESS/EXEC_FAILURE
 */
static int filter (const char *server, unsigned short port,
                   char *ds_server, const unsigned short ds_port,
                   const char *ss, const char *pos_var,
                   const char *seq_fn, const ds_int32_field ref_id) {
	int ret_val = 0;

        if (!initialize_utils()) {
                printf ("failed to initialize utils for filter\n");
                fflush (stdout);
                return EXEC_FAILURE;
        }

	DEBUG_NOW (REPORT_INFO, MAIN, "starting sequence filter");
	#ifdef DEBUG_ON
	DEBUG_NOW (REPORT_INFO, MAIN, "initializing debug");
	
	if (!initialize_debug()) {
		DEBUG_NOW (REPORT_ERRORS, MAIN, "failed to initialize debug");
		finalize_utils();
		return EXEC_FAILURE;
	}
	
	#endif
	
	#ifdef MULTITHREADED_ON
	DEBUG_NOW (REPORT_INFO, MAIN, "initializing list destruction");
	
	if (!initialize_list_destruction()) {
		DEBUG_NOW (REPORT_ERRORS, MAIN, "failed to initialize list destruction");
		#ifdef DEBUG_ON
		persist_debug();
		finalize_debug();
		#endif
                finalize_utils();
		return EXEC_FAILURE;
	}
	
	#endif
	DEBUG_NOW (REPORT_INFO, MAIN, "initializing filter");
	
	if (!initialize_filter (server, port)) {
		DEBUG_NOW (REPORT_ERRORS, MAIN, "failed to initialize filter");
		#ifdef DEBUG_ON
		persist_debug();
		finalize_debug();
		#endif
		#ifdef MULTITHREADED_ON
		DEBUG_NOW (REPORT_INFO, MAIN, "finalizing list destruction");
		finalize_list_destruction();
		#endif
                finalize_utils();
		return EXEC_FAILURE;
	}
	
	DEBUG_NOW (REPORT_INFO, MAIN, "initializing datastore");
	
	if (!initialize_datastore (ds_server, ds_port, false)) {
		DEBUG_NOW (REPORT_ERRORS, MAIN, "failed to initialize datastore");
		DEBUG_NOW (REPORT_INFO, MAIN, "finalizing filter");
		finalize_filter();
		#ifdef DEBUG_ON
		persist_debug();
		finalize_debug();
		#endif
		#ifdef MULTITHREADED_ON
		DEBUG_NOW (REPORT_INFO, MAIN, "finalizing list destruction");
		finalize_list_destruction();
		#endif
                finalize_utils();
		return EXEC_FAILURE;
	}
	
	char *seq_buff = NULL;
	nt_file_size seq_size;
	DEBUG_NOW1 (REPORT_INFO, MAIN, "reading sequence from %s", seq_fn);
	
	if (!read_seq_from_fn (seq_fn, &seq_buff, &seq_size)) {
		DEBUG_NOW1 (REPORT_ERRORS, MAIN, "could not read sequence from %s", seq_fn);
		DEBUG_NOW (REPORT_INFO, MAIN, "finalizing filter");
		finalize_filter();
		DEBUG_NOW (REPORT_INFO, MAIN, "finalizing datastore");
		finalize_datastore();
		#ifdef DEBUG_ON
		persist_debug();
		finalize_debug();
		#endif
		#ifdef MULTITHREADED_ON
		DEBUG_NOW (REPORT_INFO, MAIN, "finalizing list destruction");
		finalize_list_destruction();
		#endif
                finalize_utils();
		return EXEC_FAILURE;
	}
	
	char seq_obj_id[NUM_RT_BYTES + 1];
	seq_obj_id[0] = '\0';
	DEBUG_NOW (REPORT_INFO, MAIN, "checking sequence in datastore");
	
	if (!read_sequence_id_by_string (seq_buff, &seq_obj_id)) {
		DEBUG_NOW (REPORT_INFO, MAIN, "sequence not found in datastore");
		DEBUG_NOW (REPORT_INFO, MAIN, "creating new sequence in datastore");
		
		if (create_sequence ("test", "test sequence", "test accession", seq_buff,
		                     ref_id, &seq_obj_id)) {
			convert_timebytes_to_dec_representation (&seq_obj_id, &seq_obj_id);
			DEBUG_NOW1 (REPORT_INFO, MAIN, "new sequence created with _id='%s'",
			            seq_obj_id);
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, MAIN, "failed to create new sequence");
			DEBUG_NOW (REPORT_INFO, MAIN, "finalizing filter");
			finalize_filter();
			DEBUG_NOW (REPORT_INFO, MAIN, "finalizing datastore");
			finalize_datastore();
			#ifdef DEBUG_ON
			persist_debug();
			finalize_debug();
			#endif
			#ifdef MULTITHREADED_ON
			DEBUG_NOW (REPORT_INFO, MAIN, "finalizing list destruction");
			finalize_list_destruction();
			#endif
			finalize_utils();
			return EXEC_FAILURE;
		}
	}
	
	reset_timer();
	char *cssd = NULL;
	cssd = malloc (sizeof (char) * (MAX_MODEL_STRING_LEN * 2) + 2);
	char cssd_obj_id[NUM_RT_BYTES + 1];
	
	if (strlen (pos_var)) {
		join_cssd (ss, pos_var, &cssd);
	}
	
	else {
		sprintf (cssd, "%s", ss);
	}
	
	DEBUG_NOW (REPORT_INFO, MAIN, "checking cssd in datastore");
	
	if (!read_cssd_id_by_string (cssd, &cssd_obj_id)) {
		DEBUG_NOW (REPORT_INFO, MAIN, "cssd not found in datastore. creating new cssd");
		
		if (!create_cssd (cssd, cssd, ref_id,
		                  // assume new CSSD does not go public
		                  false,
		                  &cssd_obj_id)) {
			DEBUG_NOW (REPORT_ERRORS, MAIN, "failed to create cssd in datastore");
			DEBUG_NOW (REPORT_INFO, MAIN, "finalizing filter");
			finalize_filter();
			DEBUG_NOW (REPORT_INFO, MAIN, "finalizing datastore");
			finalize_datastore();
			#ifdef DEBUG_ON
			persist_debug();
			finalize_debug();
			#endif
			#ifdef MULTITHREADED_ON
			DEBUG_NOW (REPORT_INFO, MAIN, "finalizing list destruction");
			finalize_list_destruction();
			#endif
			free (cssd);
			finalize_utils();
			return EXEC_FAILURE;
		}
	}
	
	free (cssd);
	ntp_model model = NULL;
	char *err_msg = NULL;
	DEBUG_NOW (REPORT_INFO, MAIN, "converting cssd to model");
	
	if (!convert_CSSD_to_model (ss, pos_var, &model, &err_msg)) {
		DEBUG_NOW1 (REPORT_ERRORS, MAIN, "failed to convert cssd to model '%s'",
		            err_msg);
		FREE_DEBUG (err_msg, "err_msg from convert_CSSD_to_model in filter");
		ret_val = -1;
	}
	
	else {
		DEBUG_NOW (REPORT_INFO, MAIN, "comparing input cssd to generated model");
		
		if (compare_CSSD_model_strings (ss, pos_var, model)) {
			nt_seg_size fp_lead_min_span, fp_lead_max_span, tp_trail_min_span,
			            tp_trail_max_span;
			nt_stack_size stack_min_size, stack_max_size;
			nt_stack_idist stack_min_idist, stack_max_idist;
			ntp_element el_with_largest_stack = NULL;
			DEBUG_NOW (REPORT_INFO, MAIN, "getting limits for generated model");
			
			if (get_model_limits (model,
			                      &fp_lead_min_span, &fp_lead_max_span,
			                      &stack_min_size, &stack_max_size,
			                      &stack_min_idist, &stack_max_idist,
			                      &tp_trail_min_span, &tp_trail_max_span, &el_with_largest_stack)) {
				DEBUG_NOW (REPORT_INFO, MAIN, "running filter");
				filter_seq_from_file (seq_fn,
				                      model,
				                      el_with_largest_stack,
				                      fp_lead_min_span, fp_lead_max_span,
				                      stack_min_size, stack_max_size,
				                      stack_min_idist, stack_max_idist,
				                      tp_trail_min_span, tp_trail_max_span);
			}
			
			else {
				DEBUG_NOW (REPORT_ERRORS, MAIN, "failed to get limits for generated model");
				ret_val = -1;
			}
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, MAIN,
			           "input and generated model strings do not match");
			ret_val = -1;
		}
		
		DEBUG_NOW (REPORT_INFO, MAIN, "finalizing generated model");
		finalize_model (model);
	}
	
	DEBUG_NOW (REPORT_INFO, MAIN, "finalizing datastore");
	finalize_datastore();
	DEBUG_NOW (REPORT_INFO, MAIN, "finalizing filter");
	finalize_filter();
	FLUSH_MEM_DEBUG();
	#ifdef DEBUG_ON
	persist_debug();
	finalize_debug();
	#endif
	#ifdef DEBUG_LIST
	list_mem();
	#endif
	#ifdef MULTITHREADED_ON
	DEBUG_NOW (REPORT_INFO, MAIN, "finalizing list destruction");
	finalize_list_destruction();
	#endif
	finalize_utils();
	return ret_val;
}

/*
 * scan:    launch rna from command-line and directly scan
 *          input sequence (nucleotides) against given
 *          secondary structure and positional variables,
 *          without any pre-filtering
 *
 * args:    secondary structure and positional variables,
 *          input sequence string
 *
 * returns: EXEC_SUCCESS/EXEC_FAILURE
 */
static int scan (const char *ss, const char *pos_var, const char *seq_strn) {
	int ret_val = 0;

	if (!initialize_utils()) {
                printf ("failed to initialize utils for scan\n");
                fflush (stdout);
                return EXEC_FAILURE;
        }

	DEBUG_NOW (REPORT_INFO, SCAN, "starting sequence scanner");

	#ifdef DEBUG_ON
	DEBUG_NOW (REPORT_INFO, SCAN, "initializing debug");
	
	if (!initialize_debug()) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "failed to initialize debug");
                finalize_utils();
		return EXEC_FAILURE;
	}
	
	DEBUG_NOW (REPORT_INFO, SCAN, "debug initialized");
	#endif

	DEBUG_NOW (REPORT_INFO, SCAN, "initializing mfe");
	if (!initialize_mfe()) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "failed to initialize mfe");
		#ifdef DEBUG_ON
		persist_debug();
		finalize_debug();
		#endif
                finalize_utils();
		return EXEC_FAILURE;
	}
	
	DEBUG_NOW (REPORT_INFO, SCAN, "initialized mfe");

	#ifdef MULTITHREADED_ON
	DEBUG_NOW (REPORT_INFO, SCAN, "initializing list destruction");
	
	if (!initialize_list_destruction()) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "failed to initialize list destruction");
		#ifdef DEBUG_ON
		finalize_mfe();
		persist_debug();
		finalize_debug();
		#endif
                finalize_utils();
		return EXEC_FAILURE;
	}
	
	DEBUG_NOW (REPORT_INFO, SCAN, "initialized list destruction");
	#endif

	DEBUG_NOW (REPORT_INFO, SCAN, "initializing sequence bp cache");
	
	if (initialize_seq_bp_cache()) {
		DEBUG_NOW (REPORT_INFO, SCAN, "initialized sequence bp cache");
		ntp_model model = NULL;
		char *err_msg = NULL;
		DEBUG_NOW (REPORT_INFO, SCAN, "converting cssd to model");
		
		if (!convert_CSSD_to_model (ss, pos_var, &model, &err_msg)) {
			DEBUG_NOW (REPORT_ERRORS, SCAN, "failed to convert cssd to model");
			
			if (err_msg) {
				FREE_DEBUG (err_msg, "err_msg from convert_CSSD_to_model in scan");
			}
		}
		
		else {
			DEBUG_NOW (REPORT_INFO, SCAN, "comparing cssd to generated model");
			
			if (compare_CSSD_model_strings (ss, pos_var, model)) {
				nt_bp results[] = {{0, 0}};
				DEBUG_NOW (REPORT_INFO, SCAN, "running query on generated model");
				validate_test (1, seq_strn, model, results, sizeof (results) / sizeof (nt_bp),
				               true);
			}
			
			else {
				DEBUG_NOW (REPORT_ERRORS, SCAN,
				           "input cssd and stringified model do not match");
			}
			
			DEBUG_NOW (REPORT_INFO, SCAN, "finalizing generated model");
			finalize_model (model);
		}
		
		DEBUG_NOW (REPORT_INFO, SCAN, "finalizing sequence bp cache");
		finalize_seq_bp_cache();
		#ifdef MULTITHREADED_ON
		DEBUG (REPORT_INFO, SCAN, "waiting for list destruction");
		
		if (wait_list_destruction()) {
			DEBUG_NOW (REPORT_INFO, SCAN, "list destruction completed");
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, SCAN, "list destruction failed");
		}
		
		#endif
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "could not initialize sequence bp cache");
		ret_val = -1;
	}
	
	finalize_mfe();
	FLUSH_MEM_DEBUG();
	#ifdef DEBUG_ON
	DEBUG_NOW (REPORT_INFO, DEBUG, "finalizing debug");
	persist_debug();
	finalize_debug();
	#endif
	#ifdef DEBUG_LIST
	list_mem();
	#endif
	#ifdef MULTITHREADED_ON
	finalize_list_destruction();
	#endif
	finalize_utils();
	return ret_val;
}

/*
 * finish_hit_stemloop_string:
 *          replace interior loop residues in given hit string
 *
 * args:    starting and ending position of stemloop structure
 *          in given (global var) hit string
 */
static inline void finish_hit_stemloop_string (
                    unsigned short sub_start,
                    unsigned short sub_end) {
	REGISTER
	unsigned short last_open_idx = 0;
	
	for (REGISTER unsigned short i = sub_start; i <= sub_end; i++) {
		if (SS_NEUTRAL_OPEN_TERM == hit[i]) {
			last_open_idx = i;
		}
		
		else
			if (SS_NEUTRAL_CLOSE_TERM == hit[i]) {
				for (REGISTER unsigned short j = sub_start; j <= last_open_idx; j++) {
					if (SS_NEUTRAL_HAIRPIN_RESIDUE == hit[j]) {
						hit[j] = SS_NEUTRAL_INTERIOR_RESIDUE; // replace interior loop residues in "open"
					}
				}
				
				for (REGISTER unsigned short j = i; j <= sub_end; j++) {
					if (SS_NEUTRAL_HAIRPIN_RESIDUE == hit[j]) {
						hit[j] = SS_NEUTRAL_INTERIOR_RESIDUE; // replace interior loop residues in "close"
					}
				}
				
				break;
			}
	}
}

/*
 * finish_hit_substructure_string:
 *          replace neutral symbols in given hit string for a
 *          given substructure, which is either a simple stemloop
 *          or a multifurcation substructure
 *
 * args:    starting and ending position of substructure
 *          in given (global var) hit string
 */
static inline void finish_hit_substructure_string (unsigned short sub_start,
                                        unsigned short sub_end) {
	REGISTER
	bool have_multi = false, found_first_close = false;
	
	// multifurcation substructure?
	for (REGISTER unsigned short i = sub_start; i <= sub_end; i++) {
		if (SS_NEUTRAL_OPEN_TERM == hit[i]) {
			if (found_first_close) {
				have_multi = true;
				break;
			}
		}
		
		else
			if (SS_NEUTRAL_CLOSE_TERM == hit[i]) {
				if (!found_first_close) {
					found_first_close = true;
				}
			}
	}
	
	/*
	 * if not a multifurcation branch, then simply replace default (hairpin
	 * loop) residues with interior loop residue symbols
	 */
	if (!have_multi) {
		finish_hit_stemloop_string (sub_start, sub_end);
	}
	
	else
		/*
		 * for multifurcation branches:
		 * a) replace open/close symbols for "outer" helix;
		 * b) find and replace interior loop residues for each stem (one by one); and
		 * c) replace any present multifurcation residues
		 */
	{
		REGISTER
		unsigned short last_close_idx = 0, bracket_cnt, j, k, last_finished_idx = 0;
		REGISTER unsigned short i = sub_start;
		
		while (i <= sub_end) {
			if (SS_NEUTRAL_OPEN_TERM == hit[i]) {
				// an open term symbol is relevant only
				// once at least one close symbol has
				// been previously seen
				if (last_close_idx) {
					bracket_cnt = 1;
					j = last_close_idx - 1;
					
					while (bracket_cnt && j) {
						if (SS_NEUTRAL_CLOSE_TERM == hit[j]) {
							bracket_cnt++;
						}
						
						else
							if (SS_NEUTRAL_OPEN_TERM == hit[j]) {
								bracket_cnt--;
							}
							
						j--;
					}
					
					j++;
					finish_hit_stemloop_string (j, last_close_idx);	// (b) do this stem loop
					
					if (last_finished_idx) {
						// having done already a stem loop,
						// replace multifurcation residues
						// between this and that one
						for (k = j - 1; k > last_finished_idx; k--) {
							if (SS_NEUTRAL_HAIRPIN_RESIDUE == hit[k]) {
								hit[k] = SS_NEUTRAL_MULTI_RESIDUE;
							}
						}
					}
					
					else {
						// this is the first "inner" substructure, so
						// replace any multifurcation residues, and
						// then fix 5' end of outer stem
						k = j - 1;
						
						while (k >= sub_start && SS_NEUTRAL_OPEN_TERM != hit[k]) {
							if (SS_NEUTRAL_HAIRPIN_RESIDUE == hit[k]) {
								hit[k] = SS_NEUTRAL_MULTI_RESIDUE;
							}
							
							if (k) {
								k--;
							}
							
							else {
								break;
							}
						}
						
						while (k >= sub_start) {
							if (SS_NEUTRAL_HAIRPIN_RESIDUE == hit[k]) {
								hit[k] = SS_NEUTRAL_INTERIOR_RESIDUE;
							}
							
							else
								if (SS_NEUTRAL_OPEN_TERM == hit[k]) {
									hit[k] = SS_NEUTRAL_OPEN_MULTI;
								}
								
							if (k) {
								k--;
							}
							
							else {
								break;
							}
						}
					}
					
					// reset last_finished_idx to point to the end of this last stemloop structure
					last_finished_idx = last_close_idx;
					i = last_close_idx + 1;
					// find the next last_close_idx (if it exists) using bracket balancing
					bracket_cnt = 0;
					// by default, assume no further inner substructures
					last_close_idx = 0;
					
					for (k = last_finished_idx + 1; k <= sub_end; k++) {
						if (SS_NEUTRAL_OPEN_TERM == hit[k]) {
							bracket_cnt++;
						}
						
						else
							if (SS_NEUTRAL_CLOSE_TERM == hit[k]) {
								bracket_cnt--;
								
								if (!bracket_cnt) {
									last_close_idx = k;
									break;
								}
							}
					}
				}
				
				else {
					i++;
				}
			}
			
			else
				if (SS_NEUTRAL_CLOSE_TERM == hit[i]) {
					last_close_idx = i;
					
					if (i == sub_end) {
						// if end of string reached, then
						// replace any multifurcation residues
						// and fix 3' region of outer stem
						k = last_finished_idx + 1;
						
						while (k <= sub_end && SS_NEUTRAL_CLOSE_TERM != hit[k]) {
							if (SS_NEUTRAL_HAIRPIN_RESIDUE == hit[k]) {
								hit[k] = SS_NEUTRAL_MULTI_RESIDUE;
							}
							
							k++;
						}
						
						while (k <= sub_end) {
							if (SS_NEUTRAL_HAIRPIN_RESIDUE == hit[k]) {
								hit[k] = SS_NEUTRAL_INTERIOR_RESIDUE;
							}
							
							else
								if (SS_NEUTRAL_CLOSE_TERM == hit[k]) {
									hit[k] = SS_NEUTRAL_CLOSE_MULTI;
								}
								
							k++;
						}
					}
					
					i++;
				}
				
				else {
					i++;
				}
		}
	}
}

/*
 * finish_hit_string:
 *          replace neutral symbols in given hit string;
 *          hit string may contain on ore more substructures
 *
 * args:    length of given (global var) hit string
 */
static inline void finish_hit_string (unsigned short hit_len) {
	REGISTER
	unsigned short bracket_cnt = 0, sub_start = 0;
	
	for (REGISTER unsigned short i = 0; i < hit_len; i++) {
		if (!bracket_cnt && SS_NEUTRAL_HAIRPIN_RESIDUE == hit[i]) {
			hit[i] = SS_NEUTRAL_UNSTRUCTURED_RESIDUE;		// outside of any brackets, replace with unstructured residues
		}
		
		else
			if (SS_NEUTRAL_OPEN_TERM == hit[i]) {
				if (!bracket_cnt) {
					sub_start = i;	// mark start of new substructure
				}
				
				bracket_cnt++;
			}
			
			else
				if (SS_NEUTRAL_CLOSE_TERM == hit[i]) {
					bracket_cnt--;
					
					if (!bracket_cnt) {
						finish_hit_substructure_string (sub_start, i);	// process substructure
					}
				}
	}
}

/*
 * scan_worker:
 *          launch an rna scan worker as an MPI job,
 *          for the given input sequence (nucleotides) and
 *          secondary structure and positional variables
 *
 * args:    assigned MPI job id, MPI port name (from ompi-server),
*           secondary structure and positional variables,
 *          sequence string
 *
 * returns: EXEC_SUCCESS/EXEC_FAILURE
 */
static int scan_worker (const char *sched_job_id, char *mpi_port_name) {
	char *ss_strn, *pos_var_strn, *seq_strn;
	int ret_val = EXIT_SUCCESS;

        if (!initialize_utils()) {
                printf ("failed to initialize utils for scan worker\n");
                fflush (stdout);
                return EXIT_FAILURE;
        }
	
	if (MPI_SUCCESS != MPI_Init (NULL, NULL)) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "could not initialize MPI");
		finalize_utils();
		return EXIT_FAILURE;
	}
	
	MPI_Comm intercomm;

	if (MPI_SUCCESS != MPI_Comm_connect (mpi_port_name, MPI_INFO_NULL, 0,
	                                     MPI_COMM_SELF, &intercomm)) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "could not connect to dispatch");
                finalize_utils();
		MPI_Finalize();
		return EXIT_FAILURE;
	}

	#ifdef DEBUG_ON
	
	if (!initialize_debug()) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "could not initialize debug");
		DEBUG_NOW (REPORT_INFO, SCAN, "disconnecting from dispatch");
		MPI_Comm_disconnect (&intercomm);
		DEBUG_NOW (REPORT_INFO, SCAN, "finalizing MPI environment");
                finalize_utils();
		MPI_Finalize();
		return EXIT_FAILURE;
	}
	
	#endif
	
	if (!initialize_mfe()) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "failed to initialize mfe");
		#ifdef DEBUG_ON
		persist_debug();
		finalize_debug();
		#endif
		DEBUG_NOW (REPORT_INFO, SCAN, "disconnecting from dispatch");
		MPI_Comm_disconnect (&intercomm);
		DEBUG_NOW (REPORT_INFO, SCAN, "finalizing MPI environment");
                finalize_utils();
		MPI_Finalize();
		return EXIT_FAILURE;
	}
	
	#ifdef MULTITHREADED_ON
	
	if (!initialize_list_destruction()) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "failed to initialize list destruction");
		DEBUG_NOW (REPORT_INFO, SCAN, "finalizing utils");
		#ifdef DEBUG_ON
		finalize_mfe();
		persist_debug();
		finalize_debug();
		#endif
		DEBUG_NOW (REPORT_INFO, SCAN, "disconnecting from dispatch");
		MPI_Comm_disconnect (&intercomm);
		DEBUG_NOW (REPORT_INFO, SCAN, "finalizing MPI environment");
                finalize_utils();
		MPI_Finalize();
		return EXIT_FAILURE;
	}
	
	#endif
	
	if (initialize_seq_bp_cache()) {
		unsigned short d_msg[DISPATCH_MSG_SZ];
		d_msg[0] = 10;
		// MPI message handling flag/request
		int flag;
		MPI_Request request;
		
		if (MPI_SUCCESS != MPI_Irecv (d_msg, DISPATCH_MSG_SZ, DISPATCH_MSG_MPI_TYPE,
		                              MPI_ANY_SOURCE, MPI_ANY_TAG, intercomm, &request)) {
			DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot receive first message from dispatch");
		}
		
		else {
			do {
				// test for next message
				flag = 0;
				
				while (!flag) {
					MPI_Test (&request, &flag, MPI_STATUS_IGNORE);
					sleep_ms (SCAN_WORK_SLEEP_MS);
				}
				
				// message received
				if (DISPATCH_MSG_RUN == d_msg[0]) {
					int dp_msg[d_msg[1]];
					
					if (MPI_SUCCESS != MPI_Irecv (dp_msg, d_msg[1], DISPATCH_MSG_PAYLOAD_TYPE,
					                              MPI_ANY_SOURCE, MPI_ANY_TAG, intercomm, &request)) {
						DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot receive payload from from dispatch");
						break;
					}
					
					else {
						flag = 0;
						
						while (!flag) {
							MPI_Test (&request, &flag, MPI_STATUS_IGNORE);
							sleep_ms (SCAN_WORK_SLEEP_MS);
						}
						
						/*
						 * dp_msg payload unpacking order:
						 *
						 * 4                              // int32 for user ref_id
						 * NUM_RT_BYTES                   // job_id
						 * 2+length(ss)                   // variable-length payload components are preceded by their respective length (2 x uchar)
						 * 2+length(pos_var_strn)
						 * 2+length(seq_strn)
						 * 4                              // absolute starting position wrt original sequence
						 */
						REGISTER unsigned short dp_idx = 4 + 2 + NUM_RT_BYTES, i;
						ds_int32_field ref_id = 0;
						nt_rt_bytes job_id;
						nt_abs_seq_posn start_posn = 0;
						
						for (i = 0; i < 4; i++) {
							ref_id += ((ds_int32_field) dp_msg[i] << ((3 - i) * 8));
						}
						
						for (i = 0; i < NUM_RT_BYTES; i++) {
							job_id[i] = dp_msg[4 + i];
						}
						
						job_id[NUM_RT_BYTES] = '\0';
						const uchar ss_strn_len = (dp_msg[4 + NUM_RT_BYTES] << 8) + dp_msg[4 +
						                                        NUM_RT_BYTES + 1];
						ss_strn = malloc (ss_strn_len + 1);
						
						if (!ss_strn) {
							DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot allocate secondary structure string");
							break;
						}
						
						for (i = 0; i < ss_strn_len; i++) {
							ss_strn[i] = dp_msg[dp_idx++];
						}
						
						ss_strn[ss_strn_len] = '\0';
						const uchar pos_var_strn_len = (dp_msg[dp_idx] << 8) + dp_msg[dp_idx + 1];
						dp_idx += 2;
						pos_var_strn = malloc (pos_var_strn_len + 1);
						
						if (!pos_var_strn) {
							DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot allocate positional variables string");
							break;
						}
						
						for (i = 0; i < pos_var_strn_len; i++) {
							pos_var_strn[i] = dp_msg[dp_idx++];
						}
						
						pos_var_strn[pos_var_strn_len] = '\0';
						const unsigned short seq_strn_len = (dp_msg[dp_idx] << 8) + dp_msg[dp_idx +
						                                        1];
						dp_idx += 2;
						seq_strn = malloc (seq_strn_len + 1);
						
						if (!seq_strn) {
							DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot allocate sequence string");
							break;
						}
						
						for (i = 0; i < seq_strn_len; i++) {
							seq_strn[i] = dp_msg[dp_idx++];
						}
						
						seq_strn[seq_strn_len] = '\0';
						start_posn += dp_msg[dp_idx++] << 24;
						start_posn += dp_msg[dp_idx++] << 16;
						start_posn += dp_msg[dp_idx++] << 8;
						start_posn += dp_msg[dp_idx  ];
						ntp_model model = NULL;
						char *err_msg = NULL;
						
						if (!convert_CSSD_to_model (ss_strn, pos_var_strn, &model, &err_msg)) {
							DEBUG_NOW (REPORT_ERRORS, SCAN, "failed to convert cssd to model");
							
							if (err_msg) {
								FREE_DEBUG (err_msg, "err_msg from convert_CSSD_to_model in scan");
							}
						}
						
						else {
							if (compare_CSSD_model_strings (ss_strn, pos_var_strn, model)) {
								float elapsed_time = 0;
								// execute this query
								REGISTER ntp_list found_list = search_seq (seq_strn, model, &elapsed_time
								                                        #ifdef SEARCH_SEQ_DETAIL
								                                        , NULL, 0
								                                        #endif
								                                          );
								int   last_hit_len = 0;
								uchar last_hit[DS_JOB_RESULT_HIT_FIELD_LENGTH];
								bool  first_hit = true;
								
								if (found_list && list_size (found_list) &&
								    list_iterator_start (found_list)) {
									// distribute time cost evenly across number of found hits
									elapsed_time = elapsed_time / list_size (found_list);
									
									// cap (averaged per hit) search time reports at ~15mins
									if (1000.0f <= elapsed_time) {
										elapsed_time = 999.0f;
									}
									
									bool no_err = true;
									// first store job_id, search time, posn, mfe (S_HIT_DATA_LENGTH-1 chars off DS_JOB_RESULT_HIT_FIELD_LENGTH)
									uchar hit_data[DS_JOB_RESULT_HIT_FIELD_LENGTH];
									
									while (no_err && list_iterator_hasnext (found_list)) {
										unsigned short hit_len = 0;
										nt_abs_seq_posn fp_start = UINT_MAX, bp_fp_start = 0;
										// default all position symbols to SS_NEUTRAL_HAIRPIN_RESIDUE
										g_memset (hit, SS_NEUTRAL_HAIRPIN_RESIDUE, DS_JOB_RESULT_HIT_FIELD_LENGTH);
										ntp_linked_bp linked_bp = (ntp_linked_bp) list_iterator_next (found_list),
										              next_linked_bp = NULL,
										              mfe_linked_bp = linked_bp;
										              
										if (!linked_bp) {
											DEBUG_NOW (REPORT_ERRORS, SCAN, "found NULL linked_bp");
											no_err = false;
											continue;
										}
										
										// find most fp position wrt sequence origin
										while (linked_bp) {
											if (linked_bp->bp->fp_posn < fp_start) {
												if (linked_bp->bp->fp_posn) {
													fp_start = linked_bp->bp->fp_posn;
												}
												
												else {
													// if this is a wrapper bp, then keep track
													// of the (real, bp) 5' position and set fp_start
													// to the relevant  5' position upstream
													bp_fp_start = fp_start;
													nt_rel_seq_posn bp_fp_offset = 0;
													ntp_element this_element = linked_bp->fp_elements;
													
													while (this_element) {
														if (bp_fp_offset < this_element->unpaired->dist +
														    this_element->unpaired->length) {
															bp_fp_offset = this_element->unpaired->dist + this_element->unpaired->length;
														}
														
														this_element = this_element->unpaired->next;
													}
													
													fp_start -= bp_fp_offset;
												}
											}
											
											linked_bp = linked_bp->prev_linked_bp;
										}
										
										linked_bp = mfe_linked_bp;
										void *PK_refs[strlen (S_OPEN_PK)];
										unsigned short num_PKs = 0;
										
										do {
											if (DS_JOB_RESULT_HIT_FIELD_LENGTH - (S_HIT_DATA_LENGTH - 1) <
											    linked_bp->bp->tp_posn + linked_bp->stack_len - 1) {
												no_err = false;
												break;
											}
											
											for (uchar l = 0; l < linked_bp->stack_len; l++) {
												hit[linked_bp->bp->fp_posn + l - fp_start] = (uchar) SS_NEUTRAL_OPEN_TERM;
												hit[linked_bp->bp->tp_posn + l - fp_start] = (uchar) SS_NEUTRAL_CLOSE_TERM;
											}
											
											// calculate hit length
											if (linked_bp->bp->tp_posn &&  // wrapper bps does not influence hit length
											    hit_len < linked_bp->bp->tp_posn + linked_bp->stack_len - fp_start) {
												hit_len = (unsigned short) (linked_bp->bp->tp_posn + linked_bp->stack_len -
												                            fp_start);
											}
											
											uchar el_dist = 0;     // cumulative distance between fp/tp closing bps
											
											for (uchar el_it = 0; el_it < 2; el_it++) {
												if ((el_it == 0 && linked_bp->fp_elements) || (el_it == 1 &&
												                                        linked_bp->tp_elements)) {
													REGISTER
													ntp_element this_element = el_it ? linked_bp->tp_elements :
													                           linked_bp->fp_elements;
													                           
													do {
														uchar this_symbol = SS_NEUTRAL_HAIRPIN_RESIDUE;
														
														if (this_element->unpaired->i_constraint.reference->type ==
														    pseudoknot) {
															// for PKs, do not use "neutral" symbols, but use all available (S_)
															// symbols to provide representational clarity to the user;
															// keep track of i_constraint.references to map to the appropriate symbol
															unsigned short this_pk_idx = 0;
															
															while (this_pk_idx < num_PKs) {
																if (PK_refs[this_pk_idx] == this_element->unpaired->i_constraint.reference) {
																	break;
																}
																
																this_pk_idx++;
															}
															
															// new PK reference -> store for future reference
															if (this_pk_idx == num_PKs) {
																num_PKs++;
																PK_refs[this_pk_idx] = this_element->unpaired->i_constraint.reference;
															}
															
															if (this_element->unpaired->i_constraint.element_type ==
															    constraint_fp_element) {
																this_symbol = S_OPEN_PK[this_pk_idx];
															}
															
															else {
																this_symbol = S_CLOSE_PK[this_pk_idx];
															}
														}
														
														else
															if (this_element->unpaired->i_constraint.reference->type ==
															    base_triple) {
																if (this_element->unpaired->i_constraint.element_type == constraint_fp_element
																    ||
																    this_element->unpaired->i_constraint.element_type == constraint_tp_element) {
																	this_symbol = SS_NEUTRAL_BT_PAIR;
																}
																
																else {
																	this_symbol = SS_NEUTRAL_BT_SINGLE;
																}
															}
															
														if (!next_linked_bp ||
														    this_element->unpaired->next_linked_bp == next_linked_bp) {
															if (el_it && DS_JOB_RESULT_HIT_FIELD_LENGTH - (S_HIT_DATA_LENGTH - 1) <
															    linked_bp->bp->tp_posn + linked_bp->stack_len - 1 +
															    this_element->unpaired->dist - el_dist + this_element->unpaired->length) {
																no_err = false;
																break;
															}
															
															for (uchar l = 0; l < this_element->unpaired->length; l++) {
																if (el_it) {
																	hit[linked_bp->bp->tp_posn + linked_bp->stack_len - fp_start +
																	                           this_element->unpaired->dist + l] =
																	                        this_symbol;
																}
																
																else {
																	if (linked_bp->bp->fp_posn) {
																		hit[linked_bp->bp->fp_posn + linked_bp->stack_len - fp_start +
																		                           this_element->unpaired->dist + l] =
																		                        this_symbol;
																	}
																	
																	else {
																		hit[bp_fp_start - fp_start - this_element->unpaired->dist - l - 1] =
																		                    this_symbol;
																	}
																}
															}
															
															if (el_it &&
															    (hit_len < linked_bp->bp->tp_posn + linked_bp->stack_len - fp_start +
															     this_element->unpaired->dist + this_element->unpaired->length)) {
																hit_len = (ushort) (linked_bp->bp->tp_posn + linked_bp->stack_len - fp_start
																                    +
																                    this_element->unpaired->dist + this_element->unpaired->length);
															}
														}
														
														this_element = this_element->unpaired->next;
													}
													while (this_element);
												}
												
												if (!no_err) {
													break;
												}
											}
											
											next_linked_bp = linked_bp;
											linked_bp = linked_bp->prev_linked_bp;
											
											if (!linked_bp) {
												break;
											}
										}
										while (no_err);
										
										if (no_err) {
											hit[hit_len] = '\0';
											finish_hit_string (hit_len);
											float this_mfe = get_turner_mfe_estimate (mfe_linked_bp, seq_strn);
											
											if (STACK_MFE_FAILED == this_mfe) {
												no_err = false;
												break;
											}
											
											sprintf ((char *) hit_data, "%019"PRId32"%c%s%c%09.5f%c%011d%c%+09.5f%c",
											         ref_id, S_HIT_SEPARATOR,
											         job_id, S_HIT_SEPARATOR,
											         elapsed_time, S_HIT_SEPARATOR,
											         start_posn + fp_start - 1, S_HIT_SEPARATOR,
											         this_mfe, S_HIT_SEPARATOR);
										}
										
										else {
											// error
											sprintf ((char *) hit_data, "%019"PRId32"%c%s%c%09.5f%c%011d%c%+09.5f%c",
											         ref_id, S_HIT_SEPARATOR,
											         job_id, S_HIT_SEPARATOR,
											         elapsed_time, S_HIT_SEPARATOR,
											         0, S_HIT_SEPARATOR,
											         0.0f, S_HIT_SEPARATOR);
											hit_len = 0;
											hit[0] = 0;
										}
										
										if (0 < last_hit_len) {
											if (first_hit) {
												first_hit = false;
												// precede the first hit with a WORKER_STATUS_HAS_RESULT control message
												uchar w_msg[WORKER_MSG_SZ];
												w_msg[0] = WORKER_STATUS_HAS_RESULT;
												w_msg[1] = (uchar) last_hit_len;
												
												// block on send - should not do any further processing before current result set is received by dispatch
												if (MPI_SUCCESS != MPI_Send (w_msg, WORKER_MSG_SZ, WORKER_MSG_MPI_TYPE, 0, 0,
												                             intercomm)) {
													DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot send message to dispatch");
													no_err = false;
													break;
												}
											}
											
											// send previous hit terminated by length of the current hit
											last_hit[last_hit_len - 1] = (uchar) (hit_len + (S_HIT_DATA_LENGTH - 1));
											
											if (MPI_SUCCESS != MPI_Send (last_hit, last_hit_len, WORKER_MSG_PAYLOAD_TYPE,
											                             0, 0, intercomm)) {
												DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot send message to dispatch");
												no_err = false;
												break;
											}
										}
										
										// loose trailing \0 in hit_data
										last_hit_len = hit_len + (S_HIT_DATA_LENGTH - 1);
										g_memcpy (last_hit, hit_data, S_HIT_DATA_LENGTH - 2);
										g_memcpy (&last_hit[S_HIT_DATA_LENGTH - 2], hit, hit_len);
									}
									
									list_iterator_stop (found_list);
								}
								
								else {
									// no results found - send back result control message + 0-length hit data
									uchar w_msg[WORKER_MSG_SZ],
									      hit_data[S_HIT_DATA_LENGTH];
									w_msg[0] = WORKER_STATUS_HAS_RESULT;
									// use trailing string terminator as next hit's len (==0)
									w_msg[1] = S_HIT_DATA_LENGTH;
									
									// block on send - should not do any further processing before current result set is received by dispatch
									if (MPI_SUCCESS != MPI_Send (w_msg, WORKER_MSG_SZ, WORKER_MSG_MPI_TYPE, 0, 0,
									                             intercomm)) {
										DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot send message to dispatch");
										break;
									}
									
									sprintf ((char *) hit_data, "%019"PRId32"%c%s%c%09.5f%c%011d%c%+09.5f%c%d",
									         ref_id, S_HIT_SEPARATOR,
									         job_id, S_HIT_SEPARATOR,
									         elapsed_time, S_HIT_SEPARATOR,
									         0, S_HIT_SEPARATOR,
									         0.0f, S_HIT_SEPARATOR,
									         0);  // next hit length
									         
									if (MPI_SUCCESS != MPI_Send (hit_data, S_HIT_DATA_LENGTH,
									                             WORKER_MSG_PAYLOAD_TYPE, 0, 0, intercomm)) {
										DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot send message to dispatch");
										break;
									}
								}
								
								if (found_list && list_size (found_list)) {
									if (first_hit) {
										// precede the first hit with a WORKER_STATUS_HAS_RESULT control message
										uchar w_msg[WORKER_MSG_SZ];
										w_msg[0] = WORKER_STATUS_HAS_RESULT;
										w_msg[1] = (uchar) last_hit_len;
										
										// block on send - should not do any further processing before current result set is received by dispatch
										if (MPI_SUCCESS != MPI_Send (w_msg, WORKER_MSG_SZ, WORKER_MSG_MPI_TYPE, 0, 0,
										                             intercomm)) {
											DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot send message to dispatch");
											break;
										}
									}
									
									// flush last hit read + 0
									last_hit[last_hit_len - 1] = 0;
									
									if (MPI_SUCCESS != MPI_Send (last_hit, last_hit_len, WORKER_MSG_PAYLOAD_TYPE,
									                             0, 0, intercomm)) {
										DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot send message to dispatch");
										break;
									}
									
									dispose_linked_bp_copy (model,
									                        found_list,
									                        "linked_bp_copy for safe_copy of found_list in validate_test",
									                        "safe_copy of found_list in validate_test"
									                        #ifndef NO_FULL_CHECKS
									                        , "could not iterate over to free safe_copy of found_list in validate_test"
									                        #endif
									                       );
								}
							}
							
							else {
								DEBUG_NOW (REPORT_WARNINGS, SCAN,
								           "input cssd and stringified model do not match");
							}
							
							finalize_model (model);
						}
						
						free (ss_strn);
						free (pos_var_strn);
						free (seq_strn);
						list_destroy_all_tagged();
					}
				}
				
				else
					if (DISPATCH_MSG_SHUTDOWN == d_msg[0]) {
						break;
					}

					else if (DISPATCH_MSG_PING == d_msg[0]) {
						// ping back dispatch - we are still alive and available
						uchar w_msg[WORKER_MSG_SZ];
						w_msg[0] = WORKER_STATUS_AVAILABLE;
						w_msg[1] = 0;

						// block on send - should not do any further processing before ping reply is received by dispatch
						if (MPI_SUCCESS != MPI_Send (w_msg, WORKER_MSG_SZ, WORKER_MSG_MPI_TYPE, 0, 0,
									     intercomm)) {
							DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot send ping reply to dispatch");
							break;
						}
					}
					
					else {
						DEBUG_NOW1 (REPORT_ERRORS, SCAN,
						            "unknown request type (%d) received from dispatch", d_msg[0]);
						break;
					}
					
				if (MPI_SUCCESS !=
				    MPI_Irecv (d_msg, DISPATCH_MSG_SZ, DISPATCH_MSG_MPI_TYPE, MPI_ANY_SOURCE,
				               MPI_ANY_TAG, intercomm, &request)) {
					DEBUG_NOW (REPORT_ERRORS, SCAN, "cannot receive message from dispatch");
					break;
				}
				
				else {
					// test for next message
					flag = 0;
					
					while (!flag) {
						MPI_Test (&request, &flag, MPI_STATUS_IGNORE);
						sleep_ms (SCAN_WORK_SLEEP_MS);
					}
				}
			}
			while (1);
		}
		
		// scan iteration complete
		finalize_seq_bp_cache();
		#ifdef MULTITHREADED_ON
		
		if (!wait_list_destruction()) {
			DEBUG_NOW (REPORT_ERRORS, SCAN, "list destruction failed");
		}
		
		#endif
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "could not initialize sequence bp cache");
		ret_val = EXIT_FAILURE;
	}
	
	finalize_mfe();
	FLUSH_MEM_DEBUG();
	#ifdef DEBUG_ON
	persist_debug();
	finalize_debug();
	#endif
	#ifdef DEBUG_LIST
	list_mem();
	#endif
	#ifdef MULTITHREADED_ON
	finalize_list_destruction();
	#endif
	
	if (MPI_SUCCESS != MPI_Comm_disconnect (&intercomm)) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "failed to disconnect from dispatch");
	}
	
	if (MPI_SUCCESS != MPI_Finalize()) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "failed to finalize MPI environment");
	}
	finalize_utils();
	return ret_val;
}

/*
 * test:
 *          launch rna in testing mode;
 *          run all tests specified in tests.in (and translated
 *          by scripts/gen_tests.py into tests.out)
 *
 */
static int test() {
	int ret_val = 0;

        if (!initialize_utils()) {
                printf ("failed to initialize utils for test\n");
                fflush (stdout);
                return EXEC_FAILURE;
        }

        #ifdef DEBUG_ON
	
	if (!initialize_debug()) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "failed to initialize debug");
		finalize_utils();
		return EXEC_FAILURE;
	}
	
	#endif
	
	if (!initialize_mfe()) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "failed to initialize mfe");
		#ifdef DEBUG_ON
		persist_debug();
		finalize_debug();
		#endif
                finalize_utils();
		return EXEC_FAILURE;
	}
	
	#ifdef MULTITHREADED_ON
	
	if (!initialize_list_destruction()) {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "failed to initialize list destruction");
		DEBUG_NOW (REPORT_INFO, SCAN, "finalizing mfe");
		finalize_mfe();
		#ifdef DEBUG_ON
		persist_debug();
		finalize_debug();
		#endif
                finalize_utils();
		return EXEC_FAILURE;
	}
	
	#endif
	
	if (initialize_seq_bp_cache()) {
		// run canned tests
		run_all_tests();
		finalize_seq_bp_cache();
		#ifdef MULTITHREADED_ON
		
		if (!wait_list_destruction()) {
			DEBUG_NOW (REPORT_ERRORS, SCAN, "list destruction failed");
		}
		
		#endif
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, SCAN, "could not initialize sequence bp cache");
		ret_val = -1;
	}
	
	finalize_mfe();
	FLUSH_MEM_DEBUG();
	#ifdef DEBUG_ON
	persist_debug();
	finalize_debug();
	#endif
	#ifdef DEBUG_LIST
	list_mem();
	#endif
	#ifdef MULTITHREADED_ON
	finalize_list_destruction();
	#endif
	finalize_utils();
	return ret_val;
}

static void explain_args() {
	DEBUG_NOW (REPORT_INFO, SCAN, "Structural RNA Homology Search Tool");
}

int main (int argc, char **argv) {

	/*
	 * initialize utils for immediate debug functionality
	 */
        if (!initialize_utils()) {
                printf ("failed to initialize utils in main\n");
                fflush (stdout);
                return EXEC_FAILURE;
        }

	/*
	 * set up ketopt.h opts/args
	 */
	static ko_longopt_t longopts[] = {
		// launch modes arguments
		{ "frontend-server",        ko_no_argument,         FRONTEND_S_MODE },
		{ "filter",                 ko_no_argument,         FILTER_MODE },
		{ SCAN_MODE_ARG_LONG,       ko_no_argument,         SCAN_MODE },
		{ "distribute-server",      ko_no_argument,         DISTRIBUTE_S_MODE },
		{ DISPATCH_ARG_LONG,        ko_no_argument,         DISPATCH_MODE },
		{ "collection",             ko_required_argument,   COLLECTION_MODE },
		#if JS_JOBSCHED_TYPE!=JS_NONE
		{ "scheduler-server",       ko_no_argument,         SCHED_I_MODE },
		#endif
		{ "test",                   ko_no_argument,         TEST_MODE },
		{ "help",                   ko_no_argument,         HELP_MODE },
		// mode-specific arguments
		{ "ss",                     ko_required_argument,   SS },
		{ "pos-var",                ko_optional_argument,   POS_VAR },
		{ "seq-nt",                 ko_required_argument,   SEQ_NT },
		{ "seq-fn",                 ko_required_argument,   SEQ_FILENAME },
		{ "backend-server",         ko_required_argument,   BACKEND_SERVER },
		{ "frontend-port",          ko_required_argument,   FRONTEND_PORT },
		#if JS_JOBSCHED_TYPE!=JS_NONE
		{ SI_SERVER_ARG_LONG,       ko_required_argument,   SI_SERVER },
		{ SI_PORT_ARG_LONG,         ko_required_argument,   SI_PORT },
		{ MPI_PORT_NAME_ARG_LONG,   ko_required_argument,   MPI_PORT_NAME },
		{ SCHED_JOB_ID_ARG_LONG,    ko_required_argument,   SCHED_JOB_ID },
		#endif
		{ RNA_BIN_FILENAME_ARG_LONG, ko_required_argument,    RNA_BIN_FILENAME },
		{ DS_SERVER_ARG_LONG,       ko_required_argument,   DS_SERVER },
		{ DS_PORT_ARG_LONG,         ko_required_argument,   DS_PORT },
		{ BACKEND_PORT_ARG_LONG,    ko_required_argument,   BACKEND_PORT },
		{ HEADNODE_SERVER_ARG_LONG, ko_required_argument,   HEADNODE_SERVER },
		{ "create",                 ko_no_argument,         CREATE_OP },
		{ "read",                   ko_no_argument,         READ_OP },
		{ "update",                 ko_no_argument,         UPDATE_OP },
		{ "delete",                 ko_no_argument,         DELETE_OP },
		// collection mode-specific arguments
		{ "id",                     ko_required_argument,   CD_ID },
		{ "group",                  ko_required_argument,   SC_GROUP },
		{ "definition",             ko_required_argument,   SC_DEFINITION },
		{ "accession",              ko_required_argument,   SC_ACCESSION },
		{ "cs-name",                ko_required_argument,   CC_CS_NAME },
		{ "published", 		ko_required_argument,   CC_PUBLISHED },
		{ "seq-id",                 ko_required_argument,   JC_SEQ_ID },
		{ "cssd-id",                ko_required_argument,   JC_CSSD_ID },
		{ "job-id",                 ko_required_argument,   JC_JOB_ID },
		{ "job-status",             ko_required_argument,   JC_JOB_STATUS },
		{ "job-error", 		ko_required_argument,   JC_JOB_ERROR },
		{ "result-hit",             ko_required_argument,   RC_HIT },
		{ "user-name",              ko_required_argument,   UC_USER_NAME },
		{ "sub-id",                 ko_optional_argument,   UC_SUB_ID },
		{ "ref-id",                 ko_required_argument,   UC_REF_ID },
		{ NULL, 0, 0 }
	};
	RNA_MODE mode = UNKNOWN_MODE;
	/*
	 * parse any given args
	 */
	bool err = false;
	ketopt_t opt = KETOPT_INIT;
	int i, c, num_opts = 0;
	char    collection[DS_COLLECTION_NAME_LENGTH + 1],
	        ss[MAX_MODEL_STRING_LEN + 1],
	        pos_var[MAX_MODEL_STRING_LEN + 1],
	        seq_strn[MAX_SEQ_LEN + 1],
	        seq_fn[MAX_FILENAME_LENGTH + 1],
	        backend_server[HOST_NAME_MAX + 1],
	        #if JS_JOBSCHED_TYPE!=JS_NONE
	        si_server[HOST_NAME_MAX + 1],
	        mpi_port_name[1000 + 1],
	        sched_job_id[JS_JOBSCHED_MAX_FULL_JOB_ID_LEN + 1],
	        #endif
	        RNA_bin_fn[MAX_FILENAME_LENGTH + 1],
	        ds_server[HOST_NAME_MAX + 1],
	        headnode_server[HOST_NAME_MAX + 1],
	        cd_id[NUM_RT_BYTES + 1],
	        sc_group[DS_GENERIC_FIELD_LENGTH + 1],
	        sc_definition[DS_GENERIC_FIELD_LENGTH + 1],
	        sc_accession[DS_GENERIC_FIELD_LENGTH + 1],
	        cc_cs_name[DS_GENERIC_FIELD_LENGTH + 1],
	        cc_published[DS_GENERIC_FIELD_LENGTH + 1],
	        jc_seq_id[NUM_RT_BYTES + 1],
	        jc_cssd_id[NUM_RT_BYTES + 1],
	        jc_job_id[NUM_RT_BYTES + 1],
	        rc_hit[DS_JOB_RESULT_HIT_FIELD_LENGTH + 1],
	        uc_user_name[DS_GENERIC_FIELD_LENGTH + 1],
	        uc_sub_id[DS_GENERIC_FIELD_LENGTH + 1];
	unsigned short
	collections_op = NO_OP,
	frontend_port = 0,
	backend_port = 0,
	#if JS_JOBSCHED_TYPE!=JS_NONE
	si_port = 0,
	#endif
	ds_port = 0;
	// assume user does not provide DS_JOB_STATUS_UNDEFINED/DS_JOB_ERROR_UNDEFINED (==INT_MIN)
	ds_int32_field
	jc_job_status = DS_JOB_STATUS_UNDEFINED,
	jc_job_error = DS_JOB_ERROR_UNDEFINED;
	ds_int32_field
	uc_ref_id = DS_USER_REF_ID_UNDEFINED;
	ss[0] = '\0';
	pos_var[0] = '\0';
	seq_strn[0] = '\0';
	seq_fn[0] = '\0';
	backend_server[0] = '\0';
	#if JS_JOBSCHED_TYPE!=JS_NONE
	si_server[0] = '\0';
	mpi_port_name[0] = '\0';
	sched_job_id[0] = '\0';
	#endif
	RNA_bin_fn[0] = '\0';
	ds_server[0] = '\0';
	headnode_server[0] = '\0';
	cd_id[0] = '\0';
	sc_group[0] = '\0';
	sc_definition[0] = '\0';
	sc_accession[0] = '\0';
	cc_cs_name[0] = '\0';
	cc_published[0] = '\0';
	jc_seq_id[0] = '\0';
	jc_cssd_id[0] = '\0';
	jc_job_id[0] = '\0';
	rc_hit[0] = '\0';
	uc_user_name[0] = '\0';
	uc_sub_id[0] = '\0';

	while ((c = ketopt (&opt, argc, argv, 1, RNA_MODE_SHORT,
	                    longopts)) >= 0) {
		if (c == '?') {
			DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: unknown option provided", argv[0]);
			err = true;
			break;
		}
		
		else
			if (c == ':') {
				static char option_str[MAX_OPTION_NAME_LENGTH + 1];
				get_option_string (opt.opt, option_str);
				DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s is missing an argument for '%s' option",
				            argv[0], option_str);
				err = true;
				break;
			}
			
		i = 0;
		
		while (i < NUM_RNA_SHORT_MODES) {
			if (c == RNA_MODE_SHORT[i]) {
				c += LONG_RNA_MODE;   // transform c from 'short -' to 'long --'
				break;
			}
			
			i++;
		}
		
		if (c >= LONG_RNA_MODE) {
			if (mode != UNKNOWN_MODE) {
				DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s can only be launched in a single mode",
				            argv[0]);
				err = true;
				break;
			}
			
			else {
				mode = c;
				num_opts++;
				
				if (mode == COLLECTION_MODE) {
					if (opt.arg != NULL) {
						if (!strcmp (opt.arg, DS_COLLECTION_SEQUENCES)) {
							strcpy (collection, DS_COLLECTION_SEQUENCES);
							continue;
						}
						
						else
							if (!strcmp (opt.arg, DS_COLLECTION_JOBS)) {
								strcpy (collection, DS_COLLECTION_JOBS);
								continue;
							}
							
							else
								if (!strcmp (opt.arg, DS_COLLECTION_CSSD)) {
									strcpy (collection, DS_COLLECTION_CSSD);
									continue;
								}
								
								else
									if (!strcmp (opt.arg, DS_COLLECTION_RESULTS)) {
										strcpy (collection, DS_COLLECTION_RESULTS);
										continue;
									}
									
									else
										if (!strcmp (opt.arg, DS_COLLECTION_USERS)) {
											strcpy (collection, DS_COLLECTION_USERS);
											continue;
										}
					}
					
					DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: invalid collection", argv[0]);
					err = true;
					break;
				}
			}
		}
		
		else {
			bool done = false;
			char option[MAX_OPTION_NAME_LENGTH + 1];
			
			switch (c) {
				case SS:
					if (strlen (ss) > 0) {
						get_option_string (SS, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (ss, opt.arg);
						done = true;
						break;
					}
					
				case POS_VAR:
					if (strlen (pos_var) > 0) {
						get_option_string (POS_VAR, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						if (opt.arg) {
							// pos_var's argument is optional, but the option itself
							// needs to be explicitly provided in the command line
							strcpy (pos_var, opt.arg);
						}
						
						done = true;
						break;
					}
					
				case SEQ_NT:
					if (strlen (seq_strn) > 0) {
						get_option_string (SEQ_NT, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (seq_strn, opt.arg);
						done = true;
						break;
					}
					
				case SEQ_FILENAME:
					if (strlen (seq_fn) > 0) {
						get_option_string (SEQ_FILENAME, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (seq_fn, opt.arg);
						done = true;
						break;
					}
					
				case FRONTEND_PORT:
					if (frontend_port) {
						get_option_string (FRONTEND_PORT, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						errno = 0;
						long port_tmp = strtol (opt.arg, NULL, 10);
						
						if (errno || port_tmp < FRONTEND_MIN_PORT || port_tmp > FRONTEND_MAX_PORT) {
							get_option_string (FRONTEND_PORT, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: invalid '%s'", argv[0], option);
							err = true;
							break;
						}
						
						frontend_port = (ushort) port_tmp;
						done = true;
						break;
					}
					
				case BACKEND_PORT:
					if (backend_port) {
						get_option_string (BACKEND_PORT, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						errno = 0;
						long port_tmp = strtol (opt.arg, NULL, 10);
						
						if (errno || port_tmp < BACKEND_MIN_PORT || port_tmp > BACKEND_MAX_PORT) {
							get_option_string (BACKEND_PORT, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: invalid '%s'", argv[0], option);
							err = true;
							break;
						}
						
						backend_port = (ushort) port_tmp;
						done = true;
						break;
					}
					
				case BACKEND_SERVER:
					if (strlen (backend_server) > 0) {
						get_option_string (BACKEND_SERVER, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (backend_server, opt.arg);
						done = true;
						break;
					}
					
				case DS_PORT:
					if (ds_port) {
						get_option_string (DS_PORT, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						errno = 0;
						long port_tmp = strtol (opt.arg, NULL, 10);
						
						if (errno || port_tmp < DS_MIN_PORT || port_tmp > DS_MAX_PORT) {
							get_option_string (DS_PORT, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: invalid '%s'", argv[0], option);
							err = true;
							break;
						}
						
						ds_port = (ushort) port_tmp;
						done = true;
						break;
					}
					
				case DS_SERVER:
					if (strlen (ds_server) > 0) {
						get_option_string (DS_SERVER, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (ds_server, opt.arg);
						done = true;
						break;
					}
					
					#if JS_JOBSCHED_TYPE!=JS_NONE
					
				case SI_PORT:
					if (si_port) {
						get_option_string (SI_PORT, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						errno = 0;
						long port_tmp = strtol (opt.arg, NULL, 10);
						
						if (errno || port_tmp < SI_MIN_PORT || port_tmp > SI_MAX_PORT) {
							get_option_string (SI_PORT, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: invalid '%s'", argv[0], option);
							err = true;
							break;
						}
						
						si_port = (ushort) port_tmp;
						done = true;
						break;
					}
					
				case SI_SERVER:
					if (strlen (si_server) > 0) {
						get_option_string (SI_SERVER, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (si_server, opt.arg);
						done = true;
						break;
					}
					
				case MPI_PORT_NAME:
					if (strlen (mpi_port_name) > 0) {
						get_option_string (MPI_PORT_NAME, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (mpi_port_name, opt.arg);
						done = true;
						break;
					}
					
				case SCHED_JOB_ID:
					if (strlen (sched_job_id) > 0) {
						get_option_string (SCHED_JOB_ID, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (sched_job_id, opt.arg);
						done = true;
						break;
					}
					
					#endif
					
				case RNA_BIN_FILENAME:
					if (strlen (RNA_bin_fn) > 0) {
						get_option_string (RNA_BIN_FILENAME, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (RNA_bin_fn, opt.arg);
						done = true;
						break;
					}
					
				case HEADNODE_SERVER:
					if (strlen (headnode_server) > 0) {
						get_option_string (HEADNODE_SERVER, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (headnode_server, opt.arg);
						done = true;
						break;
					}
					
				case CD_ID:
					if (strlen (cd_id) > 0) {
						get_option_string (CD_ID, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (cd_id, opt.arg);
						done = true;
						break;
					}
					
				case SC_GROUP:
					if (strlen (sc_group) > 0) {
						get_option_string (SC_GROUP, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (sc_group, opt.arg);
						done = true;
						break;
					}
					
				case SC_DEFINITION:
					if (strlen (sc_definition) > 0) {
						get_option_string (SC_DEFINITION, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (sc_definition, opt.arg);
						done = true;
						break;
					}
					
				case SC_ACCESSION:
					if (strlen (sc_accession) > 0) {
						get_option_string (SC_ACCESSION, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (sc_accession, opt.arg);
						done = true;
						break;
					}
					
				case CC_CS_NAME:
					if (strlen (cc_cs_name) > 0) {
						get_option_string (CC_CS_NAME, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (cc_cs_name, opt.arg);
						done = true;
						break;
					}
					
				case CC_PUBLISHED:
					if (strlen (cc_published) > 0) {
						get_option_string (CC_PUBLISHED, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (cc_published, opt.arg);
						done = true;
						break;
					}
					
				case JC_SEQ_ID:
					if (strlen (jc_seq_id) > 0) {
						get_option_string (JC_SEQ_ID, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (jc_seq_id, opt.arg);
						done = true;
						break;
					}
					
				case JC_CSSD_ID:
					if (strlen (jc_cssd_id) > 0) {
						get_option_string (JC_CSSD_ID, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (jc_cssd_id, opt.arg);
						done = true;
						break;
					}
					
				case RC_HIT:
					if (strlen (rc_hit) > 0) {
						get_option_string (RC_HIT, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (rc_hit, opt.arg);
						done = true;
						break;
					}
					
				case JC_JOB_ID:
					if (strlen (jc_job_id)) {
						get_option_string (JC_JOB_ID, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (jc_job_id, opt.arg);
						done = true;
						break;
					}
					
				case JC_JOB_STATUS:
					if (jc_job_status != DS_JOB_STATUS_UNDEFINED) {
						get_option_string (JC_JOB_STATUS, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						errno = 0;
						ds_int32_field jc_job_status_int = strtol (opt.arg, NULL, 0);
						
						if (errno ||
						    (! (jc_job_status_int == DS_JOB_STATUS_INIT ||
						        jc_job_status_int == DS_JOB_STATUS_DONE ||
						        jc_job_status_int == DS_JOB_STATUS_SUBMITTED ||
						        jc_job_status_int == DS_JOB_STATUS_PENDING))) {
							get_option_string (JC_JOB_STATUS, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: invalid '%s'", argv[0], option);
							err = true;
							break;
						}
						
						jc_job_status = jc_job_status_int;
						done = true;
						break;
					}
					
				case JC_JOB_ERROR:
					if (jc_job_error != DS_JOB_ERROR_UNDEFINED) {
						get_option_string (JC_JOB_ERROR, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						errno = 0;
						ds_int32_field jc_job_error_int = strtol (opt.arg, NULL, 0);
						
						if (errno ||
						    (! (jc_job_error_int == DS_JOB_ERROR_OK ||
						        jc_job_error_int == DS_JOB_ERROR_FAIL))) {
							get_option_string (JC_JOB_ERROR, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: invalid '%s'", argv[0], option);
							err = true;
							break;
						}
						
						jc_job_error = jc_job_error_int;
						done = true;
						break;
					}
					
				case UC_USER_NAME:
					if (strlen (uc_user_name) > 0) {
						get_option_string (UC_USER_NAME, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						strcpy (uc_user_name, opt.arg);
						done = true;
						break;
					}
					
				case UC_SUB_ID:
					if (strlen (uc_sub_id) > 0) {
						get_option_string (UC_SUB_ID, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						if (opt.arg) {
							// uc_sub_id's argument is optional, but the option itself
							// needs to be explicitly given in the command line
							strcpy (uc_sub_id, opt.arg);
						}
						
						done = true;
						break;
					}
					
				case UC_REF_ID:
					if (uc_ref_id != DS_USER_REF_ID_UNDEFINED) {
						get_option_string (UC_REF_ID, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						errno = 0;
						ds_int32_field uc_ref_id_int = strtol (opt.arg, NULL, 0);
						
						if (errno || 0 > uc_ref_id_int) {
							get_option_string (UC_REF_ID, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: invalid '%s'", argv[0], option);
							err = true;
							break;
						}
						
						uc_ref_id = uc_ref_id_int;
						done = true;
						break;
					}
					
				case CREATE_OP:
				case READ_OP:
				case UPDATE_OP:
				case DELETE_OP:
					if (collections_op != NO_OP) {
						get_option_string (c, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: duplicate '%s'", argv[0], option);
						err = true;
						break;
					}
					
					else {
						collections_op = (ushort) c;
						done = true;
						break;
					}
					
				default:
					break;
			}
			
			if (err) {
				break;
			}
			
			if (done) {
				num_opts++;
				continue;
			}
		}
	}
	
	if (err) {
		finalize_utils();
		return EXEC_FAILURE;
	}
	
	else {
		/*
		 * process args; launch in mode requested by user
		 */
		if (mode != UNKNOWN_MODE) {
			char option[MAX_OPTION_NAME_LENGTH + 1];
			bool success = false;
			
			switch (mode) {
				case FRONTEND_S_MODE:
					if (!frontend_port) {
						get_option_string (FRONTEND_PORT, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
						            option);
						break;
					}
					
					else
						if (!backend_port) {
							get_option_string (BACKEND_PORT, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
							            option);
							break;
						}
						
						else
							if (!strlen (backend_server)) {
								get_option_string (BACKEND_SERVER, option);
								DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
								            option);
								break;
							}
							
							else
								if (!strlen (ds_server)) {
									get_option_string (DS_SERVER, option);
									DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
									            option);
									break;
								}
								
								else
									if (!ds_port) {
										get_option_string (DS_PORT, option);
										DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
										            option);
										break;
									}
									
									else
										if (num_opts != 6) {
											DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
											            argv[0]);
											break;
										}
					finalize_utils ();
					return initialize_frontend (frontend_port, backend_server, backend_port,
					                            ds_server, ds_port) ?
					       EXEC_SUCCESS : EXEC_FAILURE;
					       
				case HELP_MODE:
					if (num_opts != 1) {
						DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
						            argv[0]);
						break;
					}
					
					explain_args();

					finalize_utils();
					return EXEC_SUCCESS;
					
				case SCAN_MODE:
				case FILTER_MODE:   // note: pos_var can be supplied as an empty string
					if (mode == FILTER_MODE) {
						if (!strlen (ss)) {
							get_option_string (SS, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
							            option);
							break;
						}
						
						if (!strlen (seq_fn)) {
							get_option_string (SEQ_FILENAME, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
							            option);
							break;
						}
						
						else
							if (!strlen (backend_server)) {
								get_option_string (BACKEND_SERVER, option);
								DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
								            option);
								break;
							}
							
							else
								if (!backend_port) {
									get_option_string (BACKEND_PORT, option);
									DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
									            option);
									break;
								}
								
								else
									if (!strlen (ds_server)) {
										get_option_string (DS_SERVER, option);
										DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
										            option);
										break;
									}
									
									else
										if (!ds_port) {
											get_option_string (DS_PORT, option);
											DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
											            option);
											break;
										}
										
										else
											if (uc_ref_id == DS_USER_REF_ID_UNDEFINED) {
												get_option_string (UC_REF_ID, option);
												DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
												            option);
												break;
											}
											
						if (num_opts != 9) {
							DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
							            argv[0]);
							break;
						}
					}
					
					else {
						// scan mode: either ss, pos_var (possibly empty), and seq_nt; or, sched_job_id and mpi_port_name
						// TODO: this way, another argument, and not pos_var, may be supplied and treated as empty pos_var - need FIX
						if (! ((strlen (ss) && strlen (seq_strn) && num_opts == 4) ||
						       (strlen (sched_job_id) && strlen (mpi_port_name) && num_opts == 3))) {
							DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
							            argv[0]);
							break;
						}
					}

                                        finalize_utils();
					
					if (mode == FILTER_MODE) {
						return filter (backend_server, backend_port, ds_server, ds_port, ss, pos_var,
						               seq_fn, uc_ref_id) ?
						       EXEC_SUCCESS : EXEC_FAILURE;
					}
					
					else {
						if (strlen (ss)) {
							return scan (ss, pos_var, seq_strn) ?
							       EXEC_SUCCESS : EXEC_FAILURE;
						}
						
						else {
							return scan_worker (sched_job_id, mpi_port_name) ?
							       EXEC_SUCCESS : EXEC_FAILURE;
						}
					};
					
				case TEST_MODE:
					if (num_opts != 1) {
						DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
						            argv[0]);
						break;
					}

                                        finalize_utils();
					
					return test() ?
					       EXEC_SUCCESS : EXEC_FAILURE;
					       
				case DISTRIBUTE_S_MODE:
					if (!backend_port) {
						get_option_string (BACKEND_PORT, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
						            option);
						break;
					}
					
					else
						if (!strlen (ds_server)) {
							get_option_string (DS_SERVER, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
							            option);
							break;
						}
						
						else
							if (!ds_port) {
								get_option_string (DS_PORT, option);
								DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
								            option);
								break;
							}
							
					int opt_args = 0;
					
					if (strlen (RNA_bin_fn)) {
						opt_args++;
					}
					
					else {
						// by default, set optional scanner binary filename to argv[0]
						strcpy (RNA_bin_fn, argv[0]);
					}
					
					#if JS_JOBSCHED_TYPE!=JS_NONE
					
					if (!strlen (si_server)) {
						get_option_string (SI_SERVER, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
						            option);
						break;
					}
					
					else
						if (!si_port) {
							get_option_string (SI_PORT, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
							            option);
							break;
						}
						
					if (num_opts != 6 + opt_args)
					#else
					if (num_opts != 4 + opt_args)
					#endif
					{
						DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
						            argv[0]);
						break;
					}

                                        finalize_utils();

					#if JS_JOBSCHED_TYPE!=JS_NONE
					return distribute (argv[0], DISPATCH_ARG_LONG,
					                   BACKEND_PORT_ARG_LONG, backend_port,
					                   DS_SERVER_ARG_LONG, ds_server, DS_PORT_ARG_LONG, ds_port,
					                   SI_SERVER_ARG_LONG, si_server, SI_PORT_ARG_LONG, si_port,
					                   RNA_BIN_FILENAME_ARG_LONG, RNA_bin_fn) ?  // launch distribution server
					#else
					return distribute (argv[0], DISPATCH_ARG_LONG,
					                   BACKEND_PORT_ARG_LONG, backend_port,
					                   DS_SERVER_ARG_LONG, ds_server, DS_PORT_ARG_LONG, ds_port,
					                   RNA_BIN_FILENAME_ARG_LONG, RNA_bin_fn) ?  // launch distribution server
					#endif
					       EXEC_SUCCESS : EXEC_FAILURE;
					       
				case DISPATCH_MODE:
					if (!backend_port) {
						get_option_string (BACKEND_PORT, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
						            option);
						break;
					}
					
					else
						if (!strlen (ds_server)) {
							get_option_string (DS_SERVER, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
							            option);
							break;
						}
						
						else
							if (!ds_port) {
								get_option_string (DS_PORT, option);
								DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
								            option);
								break;
							}
							
					opt_args = 0;
					
					if (strlen (RNA_bin_fn)) {
						opt_args++;
					}
					
					else {
						// by default, set optional scanner binary filename to argv[0]
						strcpy (RNA_bin_fn, argv[0]);
					}
					
					#if JS_JOBSCHED_TYPE!=JS_NONE
					
					if (!strlen (si_server)) {
						get_option_string (SI_SERVER, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
						            option);
						break;
					}
					
					else
						if (!si_port) {
							get_option_string (SI_PORT, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
							            option);
							break;
						}
						
					if (num_opts != 6 + opt_args)
					#else
					if (num_opts != 4 + opt_args)
					#endif
					{
						DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
						            argv[0]);
						break;
					}

                                        finalize_utils();
					
					bool ret_val;
					// start dispatching jobs from distribution server
					#if JS_JOBSCHED_TYPE!=JS_NONE
					ret_val = dispatch (backend_port, ds_server, ds_port, si_server, si_port,
					                    RNA_bin_fn);
					#else
					ret_val = dispatch (backend_port, ds_server, ds_port, RNA_bin_fn);
					#endif
					return ret_val ? EXEC_SUCCESS : EXEC_FAILURE;
					#if JS_JOBSCHED_TYPE!=JS_NONE
					
				case SCHED_I_MODE:
					if (!si_port) {
						get_option_string (SI_PORT, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
						            option);
						break;
					}
					
					#if JS_JOBSCHED_TYPE==JS_TORQUE
					
					else
						if (!strlen (headnode_server)) {
							get_option_string (HEADNODE_SERVER, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
							            option);
							break;
						}
						
						else
							if (num_opts != 3) {
								DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
								            argv[0]);
								break;
							}

                                        finalize_utils();
					return scheduler_interface (si_port, headnode_server) ?
					       EXEC_SUCCESS : EXEC_FAILURE;
					#elif JS_JOBSCHED_TYPE==JS_SLURM
					else
						if (num_opts != 2) {
							DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
							            argv[0]);
							break;
						}

                                        finalize_utils();
					return scheduler_interface (si_port) ?
					       EXEC_SUCCESS : EXEC_FAILURE;
					#else
                                        finalize_utils();
					return scheduler_interface()
					       ?     // by default, a 'unsupported-interface' returns false and error msg
					       EXEC_SUCCESS : EXEC_FAILURE;
					#endif
					#endif
					       
				case COLLECTION_MODE:
					if (!strlen (ds_server)) {
						get_option_string (DS_SERVER, option);
						DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
						            option);
						break;
					}
					
					else
						if (!ds_port) {
							get_option_string (DS_PORT, option);
							DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' argument expected", argv[0],
							            option);
							break;
						}
						
					if (!initialize_datastore (ds_server, ds_port, false)) {
						DEBUG_NOW (REPORT_ERRORS, MAIN, "could not initialize datastore");
						finalize_utils();
						return EXEC_FAILURE;
					}
					
					if (!strcmp (collection, DS_COLLECTION_SEQUENCES)) {
						if (collections_op == NO_OP ||
						    (! (collections_op == CREATE_OP || collections_op == READ_OP ||
						        collections_op == DELETE_OP))) {
							DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: valid collections operation required",
							            argv[0]);
							break;
						}
						
						else {
							switch (collections_op) {
									char option2[MAX_OPTION_NAME_LENGTH + 1], option3[MAX_OPTION_NAME_LENGTH + 1];
									
								case CREATE_OP:
									if (!strlen (sc_group)) {
										get_option_string (SC_GROUP, option);
										DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
										break;
									}
									
									if (!strlen (sc_definition)) {
										get_option_string (SC_DEFINITION, option);
										DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
										break;
									}
									
									if (!strlen (sc_accession)) {
										get_option_string (SC_ACCESSION, option);
										DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
										break;
									}
									
									if (!strlen (seq_strn)) {
										get_option_string (SEQ_NT, option);
										DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
										break;
									}
									
									if (uc_ref_id == DS_USER_REF_ID_UNDEFINED) {
										get_option_string (UC_REF_ID, option);
										DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
										break;
									}
									
									if (num_opts != 9) {
										DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
										            argv[0]);
										break;
									}
									
									if (strlen (seq_strn)) {
										char new_sequence_ojb_id[NUM_RT_BYTES + 1];
										g_memset (new_sequence_ojb_id, 0,
										          NUM_RT_BYTES + 1);  // only necessary to keep valgrind silent
										          
										if (create_sequence (sc_group, sc_definition, sc_accession, seq_strn,
										                     uc_ref_id, &new_sequence_ojb_id)) {
											convert_timebytes_to_dec_representation (&new_sequence_ojb_id,
											                                        &new_sequence_ojb_id);
											success = true;
										}
										
										else {
											DEBUG_NOW (REPORT_ERRORS, MAIN, "failed to create sequence");
										}
									}
									
									else {
										DEBUG_NOW (REPORT_ERRORS, MAIN, "unsupported operation");
									}
									
									break;
									
								case READ_OP:
								case DELETE_OP:
									if (!strlen (sc_accession) && !strlen (cd_id) &&
									    (DS_USER_REF_ID_UNDEFINED == uc_ref_id)) {
										get_option_string (SC_ACCESSION, option);
										get_option_string (CD_ID, option2);
										get_option_string (UC_REF_ID, option3);
										DEBUG_NOW4 (REPORT_ERRORS, MAIN,
										            "%s: one of '%s', '%s', or '%s' must be specified",
										            argv[0], option, option2, option3);
										break;
									}
									
									if (collections_op == READ_OP) {
										if (num_opts != 5) {
											DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
											            argv[0]);
											break;
										}
										
										if (strlen (cd_id)) {     // strlen (sc_accession)
											dsp_dataset ids_dataset;
											read_sequence_by_id (&cd_id, &ids_dataset);
											print_dataset (ids_dataset);
											free_dataset (ids_dataset);
											success = true;
										}
										
										else
											if (strlen (sc_accession)) {
												dsp_dataset ids_dataset;
												read_sequence_by_accession (&sc_accession, &ids_dataset);
												print_dataset (ids_dataset);
												free_dataset (ids_dataset);
												success = true;
											}
											
											else {
												dsp_dataset ids_dataset;
												read_sequences_by_ref_id (uc_ref_id, 1, 0, -1, &ids_dataset);
												print_dataset (ids_dataset);
												free_dataset (ids_dataset);
												success = true;
											}
									}
									
									else {
										if (uc_ref_id == DS_USER_REF_ID_UNDEFINED) {
											get_option_string (UC_REF_ID, option);
											DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
											break;
										}
										
										if (num_opts != 6) {
											DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
											            argv[0]);
											break;
										}
										
										if (strlen (sc_accession)) {
											delete_sequence_by_accession (&sc_accession, uc_ref_id);
											success = true;
										}
										
										else {
											delete_sequence_by_id (&cd_id, uc_ref_id);
											success = true;
										}
									}
									
									break;
									
								default:
									break;
							}
						}
					}
					
					else
						if (!strcmp (collection, DS_COLLECTION_CSSD)) {
							if (collections_op == NO_OP ||
							    (! (collections_op == CREATE_OP || collections_op == READ_OP ||
							        collections_op == DELETE_OP))) {
								DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: valid collections operation required",
								            argv[0]);
								break;
							}
							
							else {
								switch (collections_op) {
										char option2[MAX_OPTION_NAME_LENGTH + 1], option3[MAX_OPTION_NAME_LENGTH + 1];
										
									case CREATE_OP:
										if (!strlen (ss)) {
											get_option_string (SS, option);
											DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
											break;
										}
										
										// pos_var may have empty arg as long as is specified on command-line
										if (!strlen (cc_cs_name)) {
											get_option_string (CC_CS_NAME, option);
											DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
											break;
										}
										
										if (!strlen (cc_published) || strlen (cc_published) != 1 ||
										    (cc_published[0] != '0' && cc_published[0] != '1')) {
											get_option_string (CC_PUBLISHED, option);
											DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
											break;
										}
										
										if (DS_USER_REF_ID_UNDEFINED == uc_ref_id) {
											get_option_string (UC_REF_ID, option);
											DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
											break;
										}
										
										if (num_opts != 9) {
											DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
											            argv[0]);
											break;
										}
										
										char new_cssd_ojb_id[NUM_RT_BYTES + 1];
										g_memset (new_cssd_ojb_id, 0,
										          NUM_RT_BYTES + 1);     // necessary to silence valgrind
										          
										if (create_cssd_with_pos_var (ss, pos_var, cc_cs_name, uc_ref_id,
										                              atoi (cc_published), &new_cssd_ojb_id)) {
											convert_timebytes_to_dec_representation (&new_cssd_ojb_id, &new_cssd_ojb_id);
											success = true;
										}
										
										else {
											DEBUG_NOW (REPORT_ERRORS, MAIN, "failed to create cssd");
										}
										
										break;
										
									case READ_OP:
									case DELETE_OP:
										if (!strlen (cc_cs_name) && !strlen (cd_id) &&
										    (DS_USER_REF_ID_UNDEFINED == uc_ref_id)) {
											get_option_string (CC_CS_NAME, option);
											get_option_string (CD_ID, option2);
											get_option_string (UC_REF_ID, option3);
											DEBUG_NOW4 (REPORT_ERRORS, MAIN,
											            "%s: one of '%s' or '%s' options can be specified", argv[0],
											            option, option2, option3);
											break;
										}
										
										if (collections_op == READ_OP) {
											if (num_opts != 5) {
												DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
												            argv[0]);
												break;
											}
											
											if (strlen (cd_id)) {
												dsp_dataset ids_dataset;
												read_cssd_by_id (&cd_id, &ids_dataset);
												print_dataset (ids_dataset);
												free_dataset (ids_dataset);
												success = true;
											}
											
											else
												if (strlen (cc_cs_name)) {
													dsp_dataset ids_dataset;
													read_cssd_by_name (&cc_cs_name, &ids_dataset);
													print_dataset (ids_dataset);
													free_dataset (ids_dataset);
													success = true;
												}
												
												else {
													dsp_dataset ids_dataset;
													read_cssds_by_ref_id (uc_ref_id, 1, 0, 1, &ids_dataset);
													print_dataset (ids_dataset);
													free_dataset (ids_dataset);
													success = true;
												}
										}
										
										else {
											if (DS_USER_REF_ID_UNDEFINED == uc_ref_id) {
												get_option_string (UC_REF_ID, option);
												DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
												break;
											}
											
											if (num_opts != 6) {
												DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
												            argv[0]);
												break;
											}
											
											if (strlen (cc_cs_name)) {
												delete_cssd_by_name (&cc_cs_name, uc_ref_id);
												success = true;
											}
											
											else {
												delete_cssd_by_id (&cd_id, uc_ref_id);
												success = true;
											}
										}
										
										break;
										
									default:
										break;
								}
							}
						}
						
						else
							if (!strcmp (collection, DS_COLLECTION_JOBS)) {
								if (collections_op == NO_OP) {
									DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: valid collections operation required",
									            argv[0]);
									break;
								}
								
								else {
									switch (collections_op) {
										case CREATE_OP:
											if (!strlen (jc_seq_id)) {
												get_option_string (JC_SEQ_ID, option);
												DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
												break;
											}
											
											if (!strlen (jc_cssd_id)) {
												get_option_string (JC_CSSD_ID, option);
												DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
												break;
											}
											
											if (jc_job_status == DS_JOB_STATUS_UNDEFINED) {
												get_option_string (JC_JOB_STATUS, option);
												DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
												break;
											}
											
											if (jc_job_error == DS_JOB_ERROR_UNDEFINED) {
												get_option_string (JC_JOB_ERROR, option);
												DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
												break;
											}
											
											if (DS_USER_REF_ID_UNDEFINED == uc_ref_id) {
												get_option_string (UC_REF_ID, option);
												DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
												break;
											}
											
											if (num_opts != 9) {
												DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
												            argv[0]);
												break;
											}
											
											char new_cssd_ojb_id[NUM_RT_BYTES + 1];
											g_memset (new_cssd_ojb_id, 0,
											          NUM_RT_BYTES + 1);     // necessary to silence valgrind
											          
											if (create_job (&jc_seq_id, &jc_cssd_id, jc_job_status, jc_job_error,
											                uc_ref_id, &new_cssd_ojb_id)) {
												convert_timebytes_to_dec_representation (&new_cssd_ojb_id, &new_cssd_ojb_id);
												success = true;
											}
											
											else {
												DEBUG_NOW (REPORT_ERRORS, MAIN, "failed to create job");
											}
											
											break;
											
										case READ_OP:
										case DELETE_OP:
											if (!strlen (cd_id)) {
												get_option_string (CD_ID, option);
												DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
												break;
											}
											
											if (collections_op == READ_OP) {
												if (num_opts != 5) {
													DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
													            argv[0]);
													break;
												}
												
												dsp_dataset ids_dataset;
												read_job (&cd_id, &ids_dataset);
												print_dataset (ids_dataset);
												free_dataset (ids_dataset);
												success = true;
											}
											
											else {
												if (DS_USER_REF_ID_UNDEFINED == uc_ref_id) {
													get_option_string (UC_REF_ID, option);
													DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
													break;
												}
												
												if (num_opts != 6) {
													DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
													            argv[0]);
													break;
												}
												
												delete_job (&cd_id, uc_ref_id);
												success = true;
											}
											
											break;
											
										case UPDATE_OP:
											if (!strlen (cd_id)) {
												get_option_string (CD_ID, option);
												DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
												break;
											}

											if (DS_USER_REF_ID_UNDEFINED == uc_ref_id) {
												get_option_string (UC_REF_ID, option);
												DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
												break;
											}
											
											if (DS_JOB_STATUS_UNDEFINED == jc_job_status) {
												get_option_string (JC_JOB_STATUS, option);
												DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
												break;
											}
											
											if (DS_JOB_ERROR_UNDEFINED == jc_job_error) {
												get_option_string (JC_JOB_ERROR, option);
												DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
												break;
											}
											
											if (num_opts != 8) {
												DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
												            argv[0]);
												break;
											}
											
											update_job_status (&cd_id, uc_ref_id, jc_job_status);
											update_job_error (&cd_id, uc_ref_id, jc_job_error);
											success = true;
											break;
											
										default:
											break;
									}
								}
							}
							
							else
								if (!strcmp (collection, DS_COLLECTION_RESULTS)) {
									if (collections_op == NO_OP ||
									    (! (collections_op == CREATE_OP || collections_op == READ_OP ||
									        collections_op == DELETE_OP))) {
										DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: valid collections operation required",
										            argv[0]);
										break;
									}
									
									else {
										switch (collections_op) {
											case CREATE_OP:
												if (!strlen (jc_job_id)) {
													get_option_string (JC_JOB_ID, option);
													DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
													break;
												}
												
												if (!strlen (rc_hit)) {
													get_option_string (RC_HIT, option);
													DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
													break;
												}
												
												if (DS_USER_REF_ID_UNDEFINED == uc_ref_id) {
													get_option_string (UC_REF_ID, option);
													DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
													break;
												}
												
												if (num_opts != 7) {
													DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
													            argv[0]);
													break;
												}
												
												char new_job_ojb_id[NUM_RT_BYTES + 1];
												g_memset (new_job_ojb_id, 0,
												          NUM_RT_BYTES + 1);     // necessary to silence valgrind
												          
												if (create_result (&jc_job_id, &rc_hit, uc_ref_id, &new_job_ojb_id)) {
													convert_timebytes_to_dec_representation (&new_job_ojb_id, &new_job_ojb_id);
													success = true;
												}
												
												else {
													DEBUG_NOW (REPORT_ERRORS, MAIN, "failed to create result");
												}
												
												break;
												
											case READ_OP:
											case DELETE_OP:
												if (!strlen (jc_job_id)) {
													get_option_string (JC_JOB_ID, option);
													DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
													break;
												}
												
												if (collections_op == READ_OP) {
													if (num_opts != 5) {
														DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
														            argv[0]);
														break;
													}
													
													dsp_dataset ids_dataset;
													read_results_by_job_id (&jc_job_id, 1, 0,
													                        DS_COL_RESULTS_HIT_FE, DS_COL_RESULTS_HIT_POSITION, 1, &ids_dataset);
													print_dataset (ids_dataset);
													free_dataset (ids_dataset);
													success = true;
												}
												
												else {
													if (DS_USER_REF_ID_UNDEFINED == uc_ref_id) {
														get_option_string (UC_REF_ID, option);
														DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
														break;
													}
													
													if (num_opts != 6) {
														DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
														            argv[0]);
														break;
													}
													
													delete_results_by_job_id (&jc_job_id, uc_ref_id);
													success = true;
												}
												
												break;
												
											default:
												break;
										}
									}
								}
								
								else
									if (!strcmp (collection, DS_COLLECTION_USERS)) {
										if (collections_op == NO_OP) {
											DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: valid collections operation required",
											            argv[0]);
											break;
										}
										
										else {
											switch (collections_op) {
												case CREATE_OP:
													if (!strlen (uc_user_name)) {
														get_option_string (UC_USER_NAME, option);
														DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
														break;
													}
													
													if (uc_ref_id == DS_USER_REF_ID_UNDEFINED) {
														get_option_string (UC_REF_ID, option);
														DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
														break;
													}
													
													if (num_opts != 7) {
														DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
														            argv[0]);
														break;
													}
													
													char new_user_ojb_id[NUM_RT_BYTES + 1];
													g_memset (new_user_ojb_id, 0,
													          NUM_RT_BYTES + 1);     // necessary to silence valgrind
													          
													if (create_user (uc_user_name, uc_sub_id, &uc_ref_id, &new_user_ojb_id)) {
														convert_timebytes_to_dec_representation (&new_user_ojb_id, &new_user_ojb_id);
														success = true;
													}
													
													else {
														DEBUG_NOW (REPORT_ERRORS, MAIN, "failed to create user");
													}
													
													break;
													
												case READ_OP:
												case DELETE_OP:
													if (collections_op == READ_OP && !strlen (uc_sub_id)) {
														get_option_string (UC_SUB_ID, option);
														DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
														break;
													}
													
													else
														if (collections_op == DELETE_OP && DS_USER_REF_ID_UNDEFINED == uc_ref_id) {
															get_option_string (UC_REF_ID, option);
															DEBUG_NOW2 (REPORT_ERRORS, MAIN, "%s: '%s' required", argv[0], option);
															break;
														}
														
													if (num_opts != 5) {
														DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: incorrect number of arguments supplied",
														            argv[0]);
														break;
													}
													
													if (collections_op == READ_OP) {
														dsp_dataset ids_dataset;
														read_user (uc_sub_id, &ids_dataset);
														print_dataset (ids_dataset);
														free_dataset (ids_dataset);
													}
													
													else {
														delete_user (uc_ref_id);
													}
													
													break;
													
												default:
													break;
											}
										}
									}
									
					finalize_datastore();
					
					if (success) {
						finalize_utils();
						return EXEC_SUCCESS;
					}
					
					break;
					
				case UNKNOWN_MODE:
				default:
					DEBUG_NOW (REPORT_ERRORS, MAIN, "unknown mode");
					break;
			}
		}
		
		else {
			DEBUG_NOW1 (REPORT_ERRORS, MAIN, "%s: mode of operation argument missing",
			            argv[0]);
			err = true;
		}

		finalize_utils();
		return EXEC_FAILURE;
	}
}
