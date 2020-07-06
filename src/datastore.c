#include <mongoc/mongoc.h>
#include <pthread.h>
#include <jansson.h>
#include <string.h>
#include <stdatomic.h>
#include <limits.h>
#include <math.h>
#include "datastore.h"
#include "util.h"
#define JSMN_STATIC
#include "jsmn.h"

/*
 * CRUD operations for rna sequences, cssd, query jobs and results
 *
 * mongodb operations are implemented fully, whereas empty stubs are
 * set up for monetdb and a virtual datastore. the latter would be
 * intended to be used for testing/debugging against a canned dataset
 */

// spinlock for CRUD operations
static pthread_spinlock_t ds_spinlock;

// datastore queue setup for when notifications are requested
static ds_notification_event *ds_q[MAX_DS_Q_SIZE];
static nt_q_size ds_q_num_items = -1;
static nt_q_size ds_q_head_posn = 0, ds_q_tail_posn = 0;
static bool ds_q_forward = true;
static pthread_spinlock_t ds_q_spinlock;
static pthread_t ds_enq_thread[4];
#define DS_Q_NOT_ACTIVE  -1
#define DS_Q_ACTIVE       0
#define MAX_DS_Q_SIZE_LESS_ONE  MAX_DS_Q_SIZE-1
static atomic_bool ds_thread_shutting_down = false;
static bool do_notify = false;

// JSMN parsing of JSON docs retrieved from datastore
#define MAX_JSON_PARSE_TOKENS	30
static jsmn_parser jsmn_p;
// parser will assume no more than MAX_JSON_PARSE_TOKENS tokens
static jsmntok_t
jsmn_t[MAX_JSON_PARSE_TOKENS];

// data structure passed on to each ds_enq_thread
typedef struct {
	char ds_server[HOST_NAME_MAX + 1];
	unsigned short ds_port;
	DS_COLLECTION this_collection;
	char this_collection_name[HOST_NAME_MAX + 1];
} ds_enq_thread_args;
ds_enq_thread_args *thread_args[4];

/*
 * a simple shallow cache, with entries ordered
 * by frequency;
 *
 * note that cache is write-once, that is deletion
 * operations do not update the cache, so only
 * useful for immutable entities like sequences
 */
#define DS_MAX_SC_ENTRIES 10

typedef struct {
	union {
		ds_object_id_field oid_field;
		char *strn_field;
	};
	dsp_dataset ds;
	ulong frequency;
} sc_entry, sc_entries[DS_MAX_SC_ENTRIES];

static sc_entries sc_sequence_by_id,
       sc_cssd_by_id,
       sc_job_by_id;

/*
 * define collections of field names for each
 * entity (sequence, cssd, job, results); for
 * use in frontend processing
 */

// TODO: fix DS_COL_SEQUENCE_3P_UTR_ALT - see .h
const char *const DS_COLS_SEQUENCES[] =
{ DS_COL_SEQUENCE_ID, DS_COL_SEQUENCE_ACCESSION, DS_COL_SEQUENCE_DEFINITION, DS_COL_SEQUENCE_3P_UTR_ALT, DS_COL_SEQUENCE_GROUP };
const char *const DS_COLS_CSSD[] =
{ DS_COL_CSSD_ID, DS_COL_CSSD_STRING, DS_COL_CSSD_NAME, DS_COL_CSSD_REF_ID, DS_COL_CSSD_PUBLISHED };
const char *const DS_COLS_JOB[] =
{ DS_COL_JOB_ID, DS_COL_JOB_SEQUENCE_ID, DS_COL_JOB_CSSD_ID, DS_COL_JOB_STATUS, DS_COL_JOB_ERROR, DS_COL_JOB_NUM_WINDOWS, DS_COL_JOB_NUM_WINDOWS_SUCCESS, DS_COL_JOB_NUM_WINDOWS_FAIL, DS_COL_JOB_REF_ID };
const char *const DS_COLS_JOBS[] = {
	DS_COL_JOB_ID, DS_COL_CSSD_STRING, DS_COL_CSSD_NAME, DS_COL_JOB_REF_ID, DS_COL_SEQUENCE_ACCESSION, DS_COL_SEQUENCE_DEFINITION,
	DS_COL_SEQUENCE_3P_UTR_ALT, DS_COL_SEQUENCE_GROUP, DS_COL_JOB_SEQUENCE_ID, DS_COL_JOB_CSSD_ID, DS_COL_JOB_STATUS, DS_COL_JOB_ERROR,
	DS_COL_JOB_NUM_WINDOWS, DS_COL_JOB_NUM_WINDOWS_SUCCESS, DS_COL_JOB_NUM_WINDOWS_FAIL
};
const char *const DS_COLS_RESULTS[] = {
	DS_COL_RESULTS_ID, DS_COL_RESULTS_HIT_TIME, DS_COL_RESULTS_HIT_POSITION,
	DS_COL_RESULTS_HIT_FE, DS_COL_RESULTS_HIT_STRING, DS_COL_RESULTS_REF_ID
};
const char *const DS_COLS_RESULTS_SUMMARY[] =
{ DS_COL_RESULTS_HIT_POSITION, DS_COL_RESULTS_HIT_MIN_FE, DS_COL_RESULTS_HIT_MAX_FE, DS_COL_RESULTS_HIT_AVG_FE, DS_COL_RESULTS_HIT_STD_FE, DS_COL_RESULTS_HIT_COUNT  };

#define DS_LOCK_S    if (pthread_spin_lock (&ds_spinlock)) { DEBUG_NOW (REPORT_ERRORS, DATASTORE, "could not acquire datastore spinlock"); } else {
#define DS_LOCK_E    if (pthread_spin_unlock (&ds_spinlock)) { DEBUG_NOW (REPORT_ERRORS, DATASTORE, "could not release datastore spinlock"); pthread_exit (NULL); } }

#define DS_Q_LOCK_S  if (pthread_spin_lock (&ds_q_spinlock)) { DEBUG_NOW (REPORT_ERRORS, DATASTORE, "could not acquire datastore q spinlock"); } else {
#define DS_Q_LOCK_E  if (pthread_spin_unlock (&ds_q_spinlock)) { DEBUG_NOW (REPORT_ERRORS, DATASTORE, "could not release datastore q spinlock"); pthread_exit (NULL); } }

#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
#elif DATASTORE_TYPE==1         // MongoDB
	#define MONGODB_URI             "mongodb://%s:%d"
	#define MONGODB_APP_NAME        "RNAscan"
	#define MONGODB_DB_NAME         "scoRNA"
#elif DATASTORE_TYPE==2         // MonetDB
#endif

#if DATASTORE_TYPE==1                       // MongoDB
	#include "mongoc/mongoc.h"
	#include "m_model.h"
	#include "interface.h"
	
	#define MONGODB_SEQUENCES_COLLECTION_NAME   DS_COLLECTION_SEQUENCES
	#define MONGODB_CSSD_COLLECTION_NAME        DS_COLLECTION_CSSD
	#define MONGODB_JOBS_COLLECTION_NAME        DS_COLLECTION_JOBS
	#define MONGODB_RESULTS_COLLECTION_NAME     DS_COLLECTION_RESULTS
	#define MONGODB_USERS_COLLECTION_NAME       DS_COLLECTION_USERS
	#define MONGODB_COUNTERS_COLLECTION_NAME    DS_COLLECTION_COUNTERS
	
	#define MONGODB_OBJECT_ID_FIELD             "_id"
	
	mongoc_uri_t *mongo_uri = NULL;
	mongoc_client_t *mongo_client = NULL;
	mongoc_database_t *mongo_database = NULL;
	mongoc_collection_t *mongo_sequences_collection = NULL,
	*mongo_cssd_collection = NULL,
	*mongo_jobs_collection = NULL,
	*mongo_results_collection = NULL,
	*mongo_users_collection = NULL,
	*mongo_counters_collection = NULL;
	
	#define MONGODB_OID_LENGTH                  24
#endif

/*
 * static, inline replacemens for memset/memcpy - silences google sanitizers
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

/*
 *  DS q operations
 */
static bool initialize_ds_q() {
	if (ds_q_num_items >= DS_Q_ACTIVE) {
		return false;
	}
	
	else {
		ds_q_num_items = DS_Q_ACTIVE;
		return true;
	}
}

static bool enq_ds (ds_notification_event *e) {
	bool ret_val = false;
	// NOTE: for speed, do_notify is not checked here. up to caller to verify that lock is instantiated (i.e. do_notify is true)
	DS_Q_LOCK_S
	
	if (ds_q_num_items >= DS_Q_ACTIVE && ds_q_num_items < MAX_DS_Q_SIZE) {
		if (ds_q_forward) {
			if (ds_q_tail_posn < MAX_DS_Q_SIZE) {
				ds_q[ds_q_tail_posn++] = e;
			}
			
			else {
				ds_q_tail_posn = 1;
				ds_q[0] = e;
				ds_q_forward = false;
			}
		}
		
		else {
			ds_q[ds_q_tail_posn++] = e;
		}
		
		ds_q_num_items++;
		ret_val = true;
	}
	
	else {
		ret_val = false;
	}
	
	DS_Q_LOCK_E
	return ret_val;
}

ds_notification_event *deq_ds() {
	ds_notification_event *ret_val = NULL;
	DS_Q_LOCK_S
	
	if (ds_q_num_items > DS_Q_ACTIVE) {
		ds_q_num_items--;
		
		if (ds_q_forward || ds_q_head_posn < MAX_DS_Q_SIZE_LESS_ONE) {
			ret_val = ds_q[ds_q_head_posn++];
		}
		
		else {
			ds_q_head_posn = 0;
			ds_q_forward = true;
			ret_val = ds_q[MAX_DS_Q_SIZE_LESS_ONE];
		}
	}
	
	else {
		ret_val = NULL;
	}
	
	DS_Q_LOCK_E
	return ret_val;
}

static void finalize_ds_q() {
	DS_Q_LOCK_S
	ds_q_num_items = DS_Q_NOT_ACTIVE;
	ds_q_forward = true;
	ds_q_head_posn = 0;
	ds_q_tail_posn = 0;
	DS_Q_LOCK_E
}

static void *ds_enq_thread_start (void *arg) {
	// TODO: thread setup error checking/handling (in calling process)
	#ifndef NO_FULL_CHECKS
	if (NULL == arg) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE, "no collection specifier in q argument");
		return NULL;
	}
	
	#endif
	ds_enq_thread_args *args = (ds_enq_thread_args *)arg;
	char mongodb_uri[MAX_FILENAME_LENGTH + 1];
	sprintf (mongodb_uri, MONGODB_URI, args->ds_server, args->ds_port);
	mongoc_uri_t *mongo_uri = mongoc_uri_new_with_error (mongodb_uri, NULL);
	
	if (!mongo_uri) {
		DEBUG_NOW1 (REPORT_ERRORS, DATASTORE, "failed to parse MongoDB URI '%s'",
		            mongodb_uri);
		return NULL;
	}
	
	// mongoc_client_ts are not thread-safe; instantiate one per ds_enq_thread
	mongoc_client_t *mongo_client = mongoc_client_new_from_uri (mongo_uri);
	
	if (NULL == mongo_client) {
		DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
		            "failed to create mongo_client for MongoDB collection '%s'",
		            args->this_collection_name);
		return NULL;
	}
	
	mongoc_collection_t *this_collection = mongoc_client_get_collection (
	                                        mongo_client, MONGODB_DB_NAME, args->this_collection_name);
	                                        
	if (NULL == this_collection) {
		DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
		            "failed to create this_collection for MongoDB collection '%s'",
		            args->this_collection_name);
		mongoc_client_destroy (mongo_client);
		return NULL;
	}
	
	mongoc_change_stream_t *this_stream;
	bson_t empty = BSON_INITIALIZER;
	bson_t opts = BSON_INITIALIZER;
	BSON_APPEND_INT64 (&opts, "maxAwaitTimeMS", DS_COLLECTION_WATCH_DURATION_MS);
	BSON_APPEND_UTF8 (&opts, "fullDocument", "updateLookup");
	
	if (NULL == (this_stream = mongoc_collection_watch (this_collection, &empty,
	                                        &opts))) {
		DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
		            "failed to create stream for MongoDB collection '%s'",
		            args->this_collection_name);
		mongoc_collection_destroy (this_collection);
		mongoc_client_destroy (mongo_client);
		return NULL;
	}
	
	const bson_t *this_doc;
	REGISTER
	uchar opType = 0;
	
	if (!strcmp (args->this_collection_name, MONGODB_JOBS_COLLECTION_NAME)) {
		opType = 1;
	}
	
	else
		if (!strcmp (args->this_collection_name, MONGODB_RESULTS_COLLECTION_NAME)) {
			opType = 2;
		}
		
	bson_iter_t iter, desc_iter;
	char *ot, oid_string[DS_OBJ_ID_LENGTH + 1];
	int32_t ri;
	REGISTER
	bool success;
	
	while (1) {
		while (mongoc_change_stream_next (this_stream, &this_doc) &&
		       !ds_thread_shutting_down) {
			switch (opType) {
				case 0 :
					break;
					
				case 1 : 	// process job changes:
					//
					// note: delete notifications are handled directly by delete_* functions. this is because the document is lost
					// 	 post deletion and therefore the ref_id reference is also lost
					success = false;
					
					if (bson_iter_init (&iter, this_doc) &&
					    bson_iter_find_descendant (&iter, "operationType", &desc_iter)) {
						ot = (char *)bson_iter_utf8 (&desc_iter, NULL);
						
						if (bson_iter_init (&iter, this_doc) &&
						    bson_iter_find_descendant (&iter, "fullDocument._id", &desc_iter)) {
							bson_oid_to_string (bson_iter_oid (&desc_iter), oid_string);
							
							if (bson_iter_init (&iter, this_doc) &&
							    bson_iter_find_descendant (&iter, "fullDocument.ref_id", &desc_iter)) {
								ri = bson_iter_int32 (&desc_iter);
								success = true;
							}
						}
					}
					
					if (success) {
						ds_notification_event *this_event = (ds_notification_event *)malloc (sizeof (
						                                        ds_notification_event));
						                                        
						if (this_event) {
							this_event->time = get_real_time();
							this_event->ref_id = ri;
							this_event->collection = args->this_collection;
							
							if (!strcmp (ot, "insert")) {
								this_event->op_type = DS_NOTIFY_OP_INSERT;
							}
							
							else
								if (!strcmp (ot, "update")) {
									this_event->op_type = DS_NOTIFY_OP_UPDATE;
								}
								
								else {
									this_event->op_type = DS_NOTIFY_OP_UNKNOWN;
								}
								
							strncpy (this_event->oid_string, oid_string, DS_OBJ_ID_LENGTH);
							
							if (!enq_ds (this_event)) {
								DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
								            "cannot enq notification event of '%s'",
								            args->this_collection_name);
							}
						}
						
						else {
							DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
							            "cannot allocate memory for notification event of '%s'",
							            args->this_collection_name);
						}
					}
					
					continue;
					
				case 2 :	// process result changes:
					//
					// note: same approach to delete notifications as above for job changes
					success = false;
					
					if (bson_iter_init (&iter, this_doc) &&
					    bson_iter_find_descendant (&iter, "operationType", &desc_iter)) {
						ot = (char *)bson_iter_utf8 (&desc_iter, NULL);
						
						if (bson_iter_init (&iter, this_doc) &&
						    bson_iter_find_descendant (&iter, "fullDocument.job_id", &desc_iter)) {
							bson_oid_to_string (bson_iter_oid (&desc_iter), oid_string);
							
							if (bson_iter_init (&iter, this_doc) &&
							    bson_iter_find_descendant (&iter, "fullDocument.ref_id", &desc_iter)) {
								ri = bson_iter_int32 (&desc_iter);
								success = true;
							}
						}
					}
					
					if (success) {
						ds_notification_event *this_event = (ds_notification_event *)malloc (sizeof (
						                                        ds_notification_event));
						                                        
						if (this_event) {
							this_event->time = get_real_time();
							this_event->ref_id = ri;
							this_event->collection = args->this_collection;
							
							if (!strcmp (ot, "insert")) {
								this_event->op_type = DS_NOTIFY_OP_INSERT;
							}
							
							else
								if (!strcmp (ot, "update")) {
									this_event->op_type = DS_NOTIFY_OP_UPDATE;
								}
								
								else {
									this_event->op_type = DS_NOTIFY_OP_UNKNOWN;
								}
								
							strncpy (this_event->oid_string, oid_string, DS_OBJ_ID_LENGTH);
							
							if (!enq_ds (this_event)) {
								DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
								            "cannot enq notification event of '%s'",
								            args->this_collection_name);
							}
						}
						
						else {
							DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
							            "cannot allocate memory for notification event of '%s'",
							            args->this_collection_name);
						}
					}
					
					continue;
					
				default :
					break;
			}
			
			if (bson_has_field (this_doc, "fullDocument.ref_id")) {
				bson_iter_t iter;
				
				if (bson_iter_init (&iter, this_doc)) {
					bson_iter_t desc_iter;
					bson_iter_find_descendant (&iter, "fullDocument.ref_id", &desc_iter);
					int this_ref_id = (int)bson_iter_int32 (&desc_iter);
					ds_notification_event *this_event = (ds_notification_event *)malloc (sizeof (
					                                        ds_notification_event));
					                                        
					if (this_event) {
						unsigned long long now = get_real_time();
						this_event->time = now;
						this_event->ref_id = this_ref_id;
						this_event->collection = args->this_collection;
						// operation type and oid string used only for
						// jobs and results, not sequences or CSSDs
						this_event->op_type = DS_NOTIFY_OP_UNKNOWN;
						this_event->oid_string[0] = 0;
						
						if (!enq_ds (this_event)) {
							DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
							            "cannot enq notification event of '%s'",
							            args->this_collection_name);
						}
					}
					
					else {
						DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
						            "cannot allocate memory for notification event of '%s'",
						            args->this_collection_name);
					}
				}
				
				else {
					DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
					            "cannot iterate over notification document for '%s'",
					            args->this_collection_name);
				}
			}
		}
		
		if (ds_thread_shutting_down) {
			break;
		}
		
		else {
			sleep_ms (DS_COLLECTION_WATCH_SLEEP_MS);
		}
	}
	
	mongoc_change_stream_destroy (this_stream);
	mongoc_collection_destroy (this_collection);
	mongoc_client_destroy (mongo_client);
	mongoc_uri_destroy (mongo_uri);
	bson_destroy (&opts);
	return NULL;
}

/*
 * cache operations
 */
static inline short get_sc_entry_by_oid (sc_entries *sc,
                                        ds_object_id_field *oid_field) {
	for (ulong i = 0; i < DS_MAX_SC_ENTRIES; i++) {
		if ((*sc)[i].frequency) {
			// start matching backwards, from the last oid char to the front, as the forward way is highly likely
			int j = DS_OBJ_ID_LENGTH - 1;
			
			// to match in the first few chars
			for (; j >= 0; j--) {
				if ((*oid_field)[j] != (*sc)[i].oid_field[j]) {
					break;   // mismatch -> skip cache entry
				}
			}
			
			if (0 <= j) {
				continue;
			}
			
			// match; check if frequency bump required and return matching index
			if (i) {
				if ((*sc)[i].frequency + 1 > (*sc)[i - 1].frequency) {
					ulong pi = i - (ushort) 1;
					
					while (0 < pi && (*sc)[i].frequency + 1 > (*sc)[pi - 1].frequency) {
						pi--;
					}
					
					sc_entry tmp = (*sc)[pi];
					(*sc)[pi] = (*sc)[i];
					(*sc)[i] = tmp;
					return pi;
				}
			}
			
			return i;
		}
		
		else {
			return -1;  // no more cache entries
		}
	}
	
	return -1;
}

static inline bool put_sc_entry_by_oid (sc_entries *sc,
                                        ds_object_id_field *oid_field, dsp_dataset ds) {
	for (ulong i = 0; i < DS_MAX_SC_ENTRIES; i++) {
		#ifndef NO_FULL_CHECKS
	
		// make sure entry is not already in cache
		if ((*sc)[i].frequency) {
			int j = DS_OBJ_ID_LENGTH - 1;
			
			for (; j >= 0; j--) {
				if ((*oid_field)[j] != (*sc)[i].oid_field[j]) {
					break;   // mismatch -> skip cache entry
				}
			}
			
			if (0 <= j) {
				// match; entry already in cache...
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "entry already found in cache");
				return false;
			}
			
			else {
				continue;
			}
		}
		
		else
		#else
		if (! (*sc)[i].frequency)
		#endif
		{
			(*sc)[i].frequency = 1;
			g_memcpy ((*sc)[i].oid_field, *oid_field, DS_OBJ_ID_LENGTH);
			(*sc)[i].ds = ds;
			return true;
		}
	}
	
	// cache full -> swap out last entry
	(*sc)[DS_MAX_SC_ENTRIES - 1].frequency = 1;
	g_memcpy ((*sc)[DS_MAX_SC_ENTRIES - 1].oid_field, *oid_field, DS_OBJ_ID_LENGTH);
	free_dataset ((*sc)[DS_MAX_SC_ENTRIES -
	                                      1].ds);  // free previously cached dataset
	(*sc)[DS_MAX_SC_ENTRIES - 1].ds = ds;
	return true;
}

// NOTE: the following CRUD operations are not thread-safe

/*
 * sequence collection methods
 */
bool create_sequence (ds_generic_field group, ds_generic_field definition,
                      ds_generic_field accession, ds_generic_field nt, ds_int32_field ref_id,
                      ds_object_id_field *new_object_id) {
	bool ret_value = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *sequence_doc = bson_new();
	
	if (!sequence_doc) {
		ret_value = false;
	}
	
	else {
		get_real_time_bytes (new_object_id);
		bson_oid_t new_oid;
		bson_oid_init_from_data (&new_oid, (unsigned char *) new_object_id);
		BSON_APPEND_OID (sequence_doc, MONGODB_OBJECT_ID_FIELD, &new_oid);
		BSON_APPEND_UTF8 (sequence_doc, DS_COL_SEQUENCE_ACCESSION, accession);
		BSON_APPEND_UTF8 (sequence_doc, DS_COL_SEQUENCE_DEFINITION, definition);
		BSON_APPEND_UTF8 (sequence_doc, DS_COL_SEQUENCE_3P_UTR, nt);
		BSON_APPEND_UTF8 (sequence_doc, DS_COL_SEQUENCE_GROUP, group);
		BSON_APPEND_INT32 (sequence_doc, DS_COL_SEQUENCE_REF_ID, ref_id);
	
		if (!mongoc_collection_insert_one (mongo_sequences_collection, sequence_doc,
		                                   NULL, NULL, NULL)) {
			ret_value = false;
		}
	
		bson_destroy (sequence_doc);
	}
	
	#else
	#endif
	DS_LOCK_E
	return ret_value;
}

bool read_sequence_by_id (ds_object_id_field *object_id,
                          dsp_dataset *ids_dataset) {
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "sequence IDs dataset is NULL when reading sequence");
		return false;
	}
	
	*ids_dataset = NULL;
	int sc_entry_idx;
	bool ret_val = true;
	DS_LOCK_S
	sc_entry_idx = get_sc_entry_by_oid (&sc_sequence_by_id, object_id);
	
	if (0 <= sc_entry_idx) {
		*ids_dataset = clone_dataset (sc_sequence_by_id[sc_entry_idx].ds);
		
		if (!*ids_dataset) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to clone sequence IDs dataset when reading sequence");
			ret_val = false;
		}
	}
	
	else {
		#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
		#elif DATASTORE_TYPE==1         // MongoDB
		mongoc_cursor_t *cursor = NULL;
		const bson_t *doc = NULL;
		bson_t *query, *opts;
		query = bson_new();
	
		if (!query) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create bson query when reading sequence");
			ret_val = false;
		}
	
		else {
			bson_oid_t oid;
			bson_oid_init_from_string (&oid, (char *) object_id);
			BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
			// sort by seqeunce group, definition, accession
			opts = BCON_NEW ("sort", "{", DS_COL_SEQUENCE_GROUP, BCON_INT32 (1),
			                 DS_COL_SEQUENCE_DEFINITION, BCON_INT32 (1), DS_COL_SEQUENCE_ACCESSION,
			                 BCON_INT32 (1), "}", "collation", "{", "locale", BCON_UTF8 ("en_US"),
			                 "strength", BCON_INT32 (1), "}");
	
			if (!opts) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create opts when reading sequence");
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				*ids_dataset = malloc (sizeof (struct ds_dataset));
	
				if (!*ids_dataset) {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "failed to create IDs dataset when reading sequence");
					bson_destroy (opts);
					bson_destroy (query);
					ret_val = false;
				}
	
				else {
					(*ids_dataset)->data = malloc (
					                                        sizeof (char *)*DS_COLLECTION_SEQUENCES_NFIELDS -
					                                        1);    // 1 record x group,definition,accession,3'UTR
	
					if (! ((*ids_dataset)->data)) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to create data element of IDs dataset when reading sequence");
						free (*ids_dataset);
						*ids_dataset = NULL;
						bson_destroy (opts);
						bson_destroy (query);
						ret_val = false;
					}
	
					else {
						cursor = mongoc_collection_find_with_opts (mongo_sequences_collection, query,
						                                        opts, NULL);
	
						if (!cursor) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "failed to create cursor when reading sequence");
							free ((*ids_dataset)->data);
							free (*ids_dataset);
							*ids_dataset = NULL;
							bson_destroy (opts);
							bson_destroy (query);
							ret_val = false;
						}
	
						else {
							(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
							bson_iter_t iter;
							g_memset (&iter, 0, sizeof (bson_iter_t));
	
							if (mongoc_cursor_next (cursor, &doc) &&
							    bson_iter_init (&iter, doc)) {
								bson_iter_next (&iter);             // skip _id
								REGISTER ulong i = 0;
	
								while (bson_iter_next (&iter)) {
									if (i == DS_COL_SEQUENCE_REF_ID_IDX - 1) {
										// handle int32 field
										char tmp_val[DS_INT32_MAX_STRING_LENGTH + 1];
										sprintf (tmp_val, "%d", bson_iter_int32 (&iter));
										const ulong sz = strlen (tmp_val) + 1;
										(*ids_dataset)->data[i] = malloc (sz);
	
										if ((*ids_dataset)->data[i]) {
											strncpy ((*ids_dataset)->data[i], tmp_val, sz);
											i++;
										}
	
										else {
											ret_val = false;
										}
									}
	
									else {
										// handle utf8 field
										const char *tmp_val = bson_iter_utf8 (&iter, NULL);
										const ulong sz = bson_strnlen (tmp_val, DS_GENERIC_FIELD_LENGTH) + 1;
										(*ids_dataset)->data[i] = malloc (sz);
	
										if ((*ids_dataset)->data[i]) {
											strncpy ((*ids_dataset)->data[i], tmp_val, sz);
											i++;
										}
	
										else {
											ret_val = false;
										}
									}
	
									if (!ret_val) {
										for (REGISTER ulong j = 0; j < i; j++) {
											free ((*ids_dataset)->data[j]);
										}
	
										free ((*ids_dataset)->data);
										free (*ids_dataset);
										*ids_dataset = NULL;
										bson_destroy (opts);
										bson_destroy (query);
										mongoc_cursor_destroy (cursor);
										break;
									}
								}
	
								if (ret_val) {
									if (i != DS_COLLECTION_SEQUENCES_NFIELDS - 1) {
										DEBUG_NOW (REPORT_WARNINGS, DATASTORE,
										           "unexpected number of fields found when reading sequence");
	
										for (REGISTER ulong j = 0; j < i; j++) {
											free ((*ids_dataset)->data[j]);
										}
	
										free ((*ids_dataset)->data);
										free (*ids_dataset);
										*ids_dataset = NULL;
										bson_destroy (opts);
										bson_destroy (query);
										mongoc_cursor_destroy (cursor);
										ret_val = false;
									}
	
									else {
										(*ids_dataset)->num_records = 1;
										(*ids_dataset)->num_fields_per_record = DS_COLLECTION_SEQUENCES_NFIELDS - 1;
									}
								}
							}
	
							else {
								(*ids_dataset)->num_records = 0;
								(*ids_dataset)->num_fields_per_record = 0;
							}
	
							if (ret_val) {
								ret_val = !mongoc_cursor_error (cursor, NULL);
								bson_destroy (opts);
								bson_destroy (query);
								mongoc_cursor_destroy (cursor);
							}
						}
					}
				}
			}
		}
	
		#elif DATASTORE_TYPE==2         // MonetDB
		#else
		#endif
	
		if (ret_val && *ids_dataset) {
			dsp_dataset sc_entry_ds = clone_dataset (*ids_dataset);
			
			if (!sc_entry_ds) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to clone cache entry when reading sequence");
				ret_val = false;
			}
			
			ret_val = put_sc_entry_by_oid (&sc_sequence_by_id, object_id, sc_entry_ds);
		}
	}
	
	DS_LOCK_E
	return ret_val;
}

bool read_sequence_id_by_string (const char *seq_string,
                                 ds_object_id_field *seq_oid) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading sequence");
		ret_val = false;
	}
	
	else {
		BSON_APPEND_UTF8 (query, DS_COL_SEQUENCE_3P_UTR, seq_string);
		opts = BCON_NEW ("sort", "{", MONGODB_OBJECT_ID_FIELD, BCON_INT32 (-1), "}");
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when reading sequence");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			cursor = mongoc_collection_find_with_opts (mongo_sequences_collection, query,
			                                        opts, NULL);
	
			if (!cursor) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create cursor when reading sequence");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				bson_iter_t iter;
	
				if (mongoc_cursor_next (cursor, &doc) && bson_iter_init (&iter, doc) &&
				    bson_iter_next (&iter)) {
					bson_oid_to_string (bson_iter_oid (&iter), (char *) seq_oid);
				}
	
				ret_val = !mongoc_cursor_error (cursor, NULL);
				bson_destroy (opts);
				bson_destroy (query);
				mongoc_cursor_destroy (cursor);
			}
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool read_sequence_by_accession (ds_generic_field *accession,
                                 dsp_dataset *ids_dataset) {
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "sequence IDs dataset is NULL when reading sequence");
		return false;
	}
	
	*ids_dataset = NULL;
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading sequence");
		ret_val = false;
	}
	
	else {
		BSON_APPEND_UTF8 (query, DS_COL_SEQUENCE_ACCESSION, *accession);
		// sort by sequence group, definition, accession
		opts = BCON_NEW ("sort", "{", DS_COL_SEQUENCE_GROUP, BCON_INT32 (1),
		                 DS_COL_SEQUENCE_DEFINITION, BCON_INT32 (1), DS_COL_SEQUENCE_ACCESSION,
		                 BCON_INT32 (1), "}", "collation", "{", "locale", BCON_UTF8 ("en_US"),
		                 "strength", BCON_INT32 (1), "}");
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when reading sequence");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			*ids_dataset = malloc (sizeof (struct ds_dataset));
	
			if (!*ids_dataset) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create IDs dataset when reading sequence");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				(*ids_dataset)->data = malloc (sizeof (char *)*DS_COLLECTION_SEQUENCES_NFIELDS -
				                               1);      // 1 record x group,definition,accession,3'UTR
	
				if (! ((*ids_dataset)->data)) {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "failed to create data element of IDs dataset when reading sequence");
					free (*ids_dataset);
					*ids_dataset = NULL;
					bson_destroy (opts);
					bson_destroy (query);
					ret_val = false;
				}
	
				else {
					cursor = mongoc_collection_find_with_opts (mongo_sequences_collection, query,
					                                        opts, NULL);
	
					if (!cursor) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to create cursor when reading sequence");
						free ((*ids_dataset)->data);
						free (*ids_dataset);
						*ids_dataset = NULL;
						bson_destroy (opts);
						bson_destroy (query);
						ret_val = false;
					}
	
					else {
						bson_iter_t iter;
	
						if (mongoc_cursor_next (cursor, &doc) && bson_iter_init (&iter, doc)) {
							bson_iter_next (&iter);             // skip _id
							REGISTER ushort i = 0;
	
							while (bson_iter_next (&iter)) {
								if (i == DS_COL_SEQUENCE_REF_ID_IDX - 1) {
									// handle int32 field
									char tmp_val[DS_INT32_MAX_STRING_LENGTH + 1];
									sprintf (tmp_val, "%d", bson_iter_int32 (&iter));
									const ushort sz = (ushort) (strlen (tmp_val) + 1);
									(*ids_dataset)->data[i] = malloc (sz);
	
									if ((*ids_dataset)->data[i]) {
										strncpy ((*ids_dataset)->data[i], tmp_val, sz);
										i++;
									}
	
									else {
										ret_val = false;
									}
								}
	
								else {
									const char *tmp_val = bson_iter_utf8 (&iter, NULL);
									const ushort sz = (ushort) (bson_strnlen (tmp_val,
									                                        DS_GENERIC_FIELD_LENGTH) + 1);
									(*ids_dataset)->data[i] = malloc (sz);
	
									if ((*ids_dataset)->data[i]) {
										strncpy ((*ids_dataset)->data[i], tmp_val, sz);
										i++;
									}
	
									else {
										ret_val = false;
									}
								}
	
								if (!ret_val) {
									for (REGISTER ushort j = 0; j < i; j++) {
										free ((*ids_dataset)->data[j]);
									}
	
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (opts);
									bson_destroy (query);
									mongoc_cursor_destroy (cursor);
									break;
								}
							}
	
							if (ret_val) {
								if (i != DS_COLLECTION_SEQUENCES_NFIELDS - 1) {
									DEBUG_NOW (REPORT_WARNINGS, DATASTORE,
									           "unexpected number of fields found when reading sequence");
	
									for (REGISTER ushort j = 0; j < i; j++) {
										free ((*ids_dataset)->data[j]);
									}
	
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (opts);
									bson_destroy (query);
									mongoc_cursor_destroy (cursor);
									ret_val = false;
								}
	
								else {
									(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
									(*ids_dataset)->num_records = 1;
									(*ids_dataset)->num_fields_per_record = DS_COLLECTION_SEQUENCES_NFIELDS - 1;
								}
							}
						}
	
						else {
							(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
							(*ids_dataset)->num_records = 0;
							(*ids_dataset)->num_fields_per_record = 0;
						}
	
						if (ret_val) {
							ret_val = !mongoc_cursor_error (cursor, NULL);
							bson_destroy (opts);
							bson_destroy (query);
							mongoc_cursor_destroy (cursor);
						}
					}
				}
			}
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

/*
 * note:
 * start is 1-indexed
 * limit 0 -> all sequences
 */
bool read_sequences_by_ref_id (ds_int32_field ref_id, ds_int32_field start,
                               ds_int32_field limit, ds_int32_field order, dsp_dataset *ids_dataset) {
	// TODO: assumes that caller checked sequence count against start and limit; currently ret_val would simply return as false
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "sequence IDs dataset is NULL when reading sequences");
		return false;
	}
	
	if (start < 1) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "invalid start index found when reading sequences");
		return false;
	}
	
	// limit==0 -> return all sequences
	if (limit < 0) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "invalid dataset limit found when reading sequences");
		return false;
	}
	
	*ids_dataset = NULL;
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading sequences");
		ret_val = false;
	}
	
	else {
		BSON_APPEND_INT32 (query, DS_COL_SEQUENCE_REF_ID, ref_id);
		opts = BCON_NEW ("skip", BCON_INT32 (start - 1));
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when reading sequences");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			if (limit) {
				BCON_APPEND (opts, "limit", BCON_INT32 (limit));
			}
	
			ds_int32_field sequence_count = mongoc_collection_count_documents (
			                                        mongo_sequences_collection, query, opts, NULL, NULL, NULL);
	
			if (sequence_count == -1) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to get collection count when reading sequences");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				if (limit > sequence_count) {
					DEBUG_NOW2 (REPORT_ERRORS, DATASTORE,
					            "specified limit (%d) is larger than sequence count (%d) when reading sequences",
					            limit, sequence_count);
					bson_destroy (opts);
					bson_destroy (query);
					ret_val = false;
				}
	
				else {
					*ids_dataset = malloc (sizeof (struct ds_dataset));
	
					if (!*ids_dataset) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to create IDs dataset when reading sequences");
						bson_destroy (opts);
						bson_destroy (query);
						ret_val = false;
					}
	
					else {
						(*ids_dataset)->data = malloc (sizeof (char *) *
						                               (DS_COLLECTION_SEQUENCES_NFIELDS - 1) * (size_t) sequence_count);
	
						if (! ((*ids_dataset)->data)) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "failed to create data element of IDs dataset when reading sequences");
							free (*ids_dataset);
							*ids_dataset = NULL;
							bson_destroy (opts);
							bson_destroy (query);
							ret_val = false;
						}
	
						else {
							if (!sequence_count) {
								(*ids_dataset)->num_records = 0;
								(*ids_dataset)->num_fields_per_record = 0;
								(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
								bson_destroy (opts);
								bson_destroy (query);
							}
	
							else {
								// sort by sequence group, definition, accession
								BCON_APPEND (opts, "sort", "{", DS_COL_SEQUENCE_GROUP, BCON_INT32 (order),
								             DS_COL_SEQUENCE_DEFINITION, BCON_INT32 (order), DS_COL_SEQUENCE_ACCESSION,
								             BCON_INT32 (order), "}", "collation", "{", "locale", BCON_UTF8 ("en_US"),
								             "strength", BCON_INT32 (1), "}");
								cursor = mongoc_collection_find_with_opts (mongo_sequences_collection, query,
								                                        opts, NULL);
	
								if (!cursor) {
									DEBUG_NOW (REPORT_ERRORS, DATASTORE,
									           "failed to create cursor when reading sequences");
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (opts);
									bson_destroy (query);
									ret_val = false;
								}
	
								else {
									bson_iter_t iter;
									REGISTER ulong j = 0;
	
									while (ret_val && mongoc_cursor_next (cursor, &doc) &&
									       bson_iter_init (&iter, doc)) {
										REGISTER ulong i = 0;
	
										while (bson_iter_next (&iter)) {
											if (DS_COL_SEQUENCE_REF_ID_IDX == i) {
												i++;
												continue;       // skip ref_id in read_sequences_by_ref_id (as ref_id is used in querying)
											}
	
											else {
												if (i) {
													const char *tmp_val = bson_iter_utf8 (&iter, NULL);
													const ulong sz = (ulong) (bson_strnlen (tmp_val, DS_GENERIC_FIELD_LENGTH));
													(*ids_dataset)->data[j] = malloc (sz + 1);
	
													if ((*ids_dataset)->data[j]) {
														strncpy ((*ids_dataset)->data[j], tmp_val, sz);
														((char *) (*ids_dataset)->data[j])[sz] = '\0';
														i++;
														j++;
													}
	
													else {
														DEBUG_NOW (REPORT_ERRORS, DATASTORE,
														           "could not allocate memory for sequences document when reading sequences");
														ret_val = false;
													}
												}
	
												else {
													char oid_string[DS_OBJ_ID_LENGTH + 1];
													const bson_oid_t *tmp_oid = bson_iter_oid (&iter);
													bson_oid_to_string (tmp_oid, oid_string);
													(*ids_dataset)->data[j] = malloc (DS_OBJ_ID_LENGTH + 1);
	
													if ((*ids_dataset)->data[j]) {
														strncpy ((*ids_dataset)->data[j], oid_string, DS_OBJ_ID_LENGTH + 1);
														((char *) (*ids_dataset)->data[j])[DS_OBJ_ID_LENGTH] = '\0';
														i++;
														j++;
													}
	
													else {
														DEBUG_NOW (REPORT_ERRORS, DATASTORE,
														           "could not allocate memory for data element of sequences document when reading sequences");
														ret_val = false;
													}
												}
											}
	
											if (!ret_val) {
												for (REGISTER ulong k = 0; k < j; k++) {
													free ((*ids_dataset)->data[j]);
												}
	
												free ((*ids_dataset)->data);
												free (*ids_dataset);
												*ids_dataset = NULL;
												bson_destroy (opts);
												bson_destroy (query);
												mongoc_cursor_destroy (cursor);
												break;
											}
										}
	
										if (ret_val) {
											if (DS_COLLECTION_SEQUENCES_NFIELDS != i) {
												DEBUG_NOW (REPORT_WARNINGS, DATASTORE,
												           "incorrect number of fields found when reading sequences");
	
												for (REGISTER ulong k = 0; k < i; k++) {
													free ((*ids_dataset)->data[k]);
												}
	
												free ((*ids_dataset)->data);
												free (*ids_dataset);
												*ids_dataset = NULL;
												bson_destroy (opts);
												bson_destroy (query);
												mongoc_cursor_destroy (cursor);
												ret_val = false;
											}
										}
									}
	
									if (ret_val) {
										ret_val = !mongoc_cursor_error (cursor, NULL) &&
										          (j / (DS_COLLECTION_SEQUENCES_NFIELDS - 1) == sequence_count);
	
										if (ret_val) {
											(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
											(*ids_dataset)->num_records = (nt_seq_count) sequence_count;
											(*ids_dataset)->num_fields_per_record = DS_COLLECTION_SEQUENCES_NFIELDS - 1;
										}
	
										else {
											DEBUG_NOW (REPORT_ERRORS, DATASTORE,
											           "could not get cursor when reading sequences");
	
											for (REGISTER ulong k = 0; k < j; k++) {
												free ((*ids_dataset)->data[k]);
											}
	
											free ((*ids_dataset)->data);
											free (*ids_dataset);
											*ids_dataset = NULL;
										}
	
										bson_destroy (opts);
										bson_destroy (query);
										mongoc_cursor_destroy (cursor);
									}
								}
							}
						}
					}
				}
			}
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool delete_sequence_by_id (ds_object_id_field *object_id,
                            ds_int32_field ref_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "could not create bson query when deleting sequence");
		ret_val = false;
	}
	
	else {
		bson_t reply;
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, (char *) object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		BSON_APPEND_INT32 (query, DS_COL_SEQUENCE_REF_ID, ref_id);
	
		if (!mongoc_collection_delete_one (mongo_sequences_collection, query, NULL,
		                                   &reply, NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete document when deleting sequence");
			ret_val = false;
		}
	
		else {
			char *reply_json = bson_as_canonical_extended_json (&reply, NULL);
			jsmn_init (&jsmn_p);
			uchar num_tokens_parsed = jsmn_parse (&jsmn_p, reply_json, strlen (reply_json),
			                                      jsmn_t, MAX_JSON_PARSE_TOKENS);
			/*
			 * expect JSON string having format { "deletedCount" : { "$numberInt" : "1" } }
			 */
			bool success = false;
	
			if (5 == num_tokens_parsed) {
				if (jsmn_t[0].type == JSMN_OBJECT && jsmn_t[1].type == JSMN_STRING) {
					char *strn = reply_json + jsmn_t[1].start;
	
					if (!strncmp (strn, "deletedCount", jsmn_t[1].end - jsmn_t[1].start) &&
					    jsmn_t[2].type == JSMN_OBJECT && jsmn_t[3].type == JSMN_STRING) {
						strn = reply_json + jsmn_t[3].start;
	
						if (!strncmp (strn, "$numberInt", jsmn_t[3].end - jsmn_t[3].start) &&
						    jsmn_t[4].type == JSMN_STRING) {
							strn = reply_json + jsmn_t[4].start;
	
							if (!strncmp (strn, "1", jsmn_t[4].end - jsmn_t[4].start)) {
								success = true;
							}
						}
					}
				}
			}
	
			if (!success) {
				// do not log any messages - just signal that deletion not successful (presumed not found)
				ret_val = false;
			}
	
			else
				if (do_notify) {
					// for delete events, ref_id will not be available to watch -> notify directly here
					ds_notification_event *this_event = (ds_notification_event *)malloc (sizeof (
					                                        ds_notification_event));
	
					if (this_event) {
						unsigned long long now = get_real_time();
						this_event->time = now;
						this_event->ref_id = ref_id;
						this_event->collection = DS_COL_SEQUENCES;
						this_event->op_type = DS_NOTIFY_OP_DELETE;
						strncpy (this_event->oid_string, (char *)object_id, DS_OBJ_ID_LENGTH);
	
						if (!enq_ds (this_event)) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "cannot enqueue notification event after deleting sequence");
						}
					}
	
					else {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "cannot allocate memory for notification event after deleting sequence");
					}
				}
		}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool delete_all_sequences (ds_int32_field ref_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when deleting all sequences for reference id");
		ret_val = false;
	}
	
	else {
		BSON_APPEND_INT32 (query, DS_COL_SEQUENCE_REF_ID, ref_id);
	
		/*
		 * delete all sequences for this ref_id; do not signal failure to delete any sequences
		 */
		if (!mongoc_collection_delete_many (mongo_sequences_collection, query, NULL,
		                                    NULL, NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete document when deleting all sequences for reference id");
			ret_val = false;
		}
	
		else
			if (do_notify) {
				// for delete events, ref_id will not be available to watch -> notify directly here
				ds_notification_event *this_event = (ds_notification_event *)malloc (sizeof (
				                                        ds_notification_event));
	
				if (this_event) {
					unsigned long long now = get_real_time();
					this_event->time = now;
					this_event->ref_id = ref_id;
					this_event->collection = DS_COL_SEQUENCES;
					this_event->op_type = DS_NOTIFY_OP_DELETE;
					// for notify on all sequences, null-terminate oid
					this_event->oid_string[0] = 0;
	
					if (!enq_ds (this_event)) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "cannot enqueue notification event after deleting all sequences for reference id");
					}
				}
	
				else {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "cannot allocate memory for notification event after deleting all sequences for reference id");
				}
			}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

/*
 * TODO: add notification to delete_sequence_by_accession;
 * 	     currently no notifiication message is enqueued
 */
bool delete_sequence_by_accession (ds_generic_field *accession,
                                   ds_int32_field ref_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when deleting sequence");
		ret_val = false;
	}
	
	else {
		bson_t reply;
		BSON_APPEND_UTF8 (query, DS_COL_SEQUENCE_ACCESSION, *accession);
		BSON_APPEND_INT32 (query, DS_COL_SEQUENCE_REF_ID, ref_id);
	
		if (!mongoc_collection_delete_one (mongo_sequences_collection, query, NULL,
		                                   &reply, NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete document when deleting sequence");
			ret_val = false;
		}
	
		else {
			char *reply_json = bson_as_canonical_extended_json (&reply, NULL);
			jsmn_init (&jsmn_p);
			uchar num_tokens_parsed = jsmn_parse (&jsmn_p, reply_json, strlen (reply_json),
			                                      jsmn_t, MAX_JSON_PARSE_TOKENS);
			/*
			 * expect JSON string having format { "deletedCount" : { "$numberInt" : "1" } }
			 */
			bool success = false;
	
			if (5 == num_tokens_parsed) {
				if (jsmn_t[0].type == JSMN_OBJECT && jsmn_t[1].type == JSMN_STRING) {
					char *strn = reply_json + jsmn_t[1].start;
	
					if (!strncmp (strn, "deletedCount", jsmn_t[1].end - jsmn_t[1].start) &&
					    jsmn_t[2].type == JSMN_OBJECT && jsmn_t[3].type == JSMN_STRING) {
						strn = reply_json + jsmn_t[3].start;
	
						if (!strncmp (strn, "$numberInt", jsmn_t[3].end - jsmn_t[3].start) &&
						    jsmn_t[4].type == JSMN_STRING) {
							strn = reply_json + jsmn_t[4].start;
	
							if (!strncmp (strn, "1", jsmn_t[4].end - jsmn_t[4].start)) {
								success = true;
							}
						}
					}
				}
			}
	
			if (!success) {
				// do not log any messages - just signal that deletion not successful (presumed not found)
				ret_val = false;
			}
		}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

/*
 * cssd collection methods
 */
bool create_cssd_with_pos_var (ds_generic_field ss, ds_generic_field pos_var,
                               ds_generic_field name, ds_int32_field ref_id, ds_boolean_field published,
                               ds_object_id_field *new_object_id) {
	char *cssd = NULL;
	// cssd = cs + '\n' + pos_var (+ 1)
	cssd = malloc (sizeof (char) * (MAX_MODEL_STRING_LEN * 2) + 2);
	
	if (pos_var && strlen (pos_var)) {
		join_cssd (ss, pos_var, &cssd);
	}
	
	else {
		sprintf (cssd, "%s", ss);
	}
	
	bool ret_val = create_cssd (cssd, name, ref_id, published, new_object_id);
	free (cssd);
	return ret_val;
}

bool create_cssd (ds_generic_field cssd, ds_generic_field name,
                  ds_int32_field ref_id, ds_boolean_field published,
                  ds_object_id_field *new_object_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *cssd_doc = bson_new();
	
	if (!cssd_doc) {
		ret_val = false;
	}
	
	else {
		get_real_time_bytes (new_object_id);
		bson_oid_t new_oid;
		bson_oid_init_from_data (&new_oid, (unsigned char *) new_object_id);
		BSON_APPEND_OID (cssd_doc, MONGODB_OBJECT_ID_FIELD, &new_oid);
		BSON_APPEND_UTF8 (cssd_doc, DS_COL_CSSD_STRING, cssd);
		BSON_APPEND_UTF8 (cssd_doc, DS_COL_CSSD_NAME, name);
		BSON_APPEND_INT32 (cssd_doc, DS_COL_CSSD_REF_ID, ref_id);
		BSON_APPEND_BOOL (cssd_doc, DS_COL_CSSD_PUBLISHED, published);
	
		if (!mongoc_collection_insert_one (mongo_cssd_collection, cssd_doc, NULL, NULL,
		                                   NULL)) {
			ret_val = false;
		}
	
		bson_destroy (cssd_doc);
	}
	
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool update_cssd (ds_object_id_field *object_id, ds_generic_field cssd,
                  ds_generic_field name, ds_int32_field ref_id, ds_boolean_field published) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when updating cssd");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		bson_t *cssd_doc_update = BCON_NEW ("$set", "{",
		                                    DS_COL_CSSD_STRING, BCON_UTF8 (cssd),
		                                    DS_COL_CSSD_NAME, BCON_UTF8 (name),
		                                    DS_COL_CSSD_REF_ID, BCON_INT32 (ref_id),
		                                    DS_COL_CSSD_PUBLISHED, BCON_BOOL (published),
		                                    "}"
		                                   );
	
		if (!cssd_doc_update) {
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			if (!mongoc_collection_update_one (mongo_cssd_collection, query,
			                                   cssd_doc_update, NULL, NULL, NULL)) {
				ret_val = false;
			}
	
			bson_destroy (query);
			bson_destroy (cssd_doc_update);
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool read_cssd_by_id (ds_object_id_field *object_id, dsp_dataset *ids_dataset) {
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "NULL IDs dataset found when reading cssd");
		return false;
	}
	
	*ids_dataset = NULL;
	bool ret_val = true;
	DS_LOCK_S {
		#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
		#elif DATASTORE_TYPE==1         // MongoDB
		mongoc_cursor_t *cursor = NULL;
		const bson_t *doc = NULL;
		bson_t *query, *opts;
	
		query = bson_new();
	
		if (!query) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create bson query when reading cssd");
			ret_val = false;
		}
	
		else {
			bson_oid_t oid;
			bson_oid_init_from_string (&oid, *object_id);
			BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
			opts = BCON_NEW ("sort", "{", DS_COL_CSSD_NAME, BCON_INT32 (1), "}",
			                 "collation", "{", "locale", BCON_UTF8 ("en_US"), "strength", BCON_INT32 (1),
			                 "}");
	
			if (!opts) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create opts when reading cssd");
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				*ids_dataset = malloc (sizeof (struct ds_dataset));
	
				if (!*ids_dataset) {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "failed to create IDs dataset when reading cssd");
					bson_destroy (opts);
					bson_destroy (query);
					ret_val = false;
				}
	
				else {
					(*ids_dataset)->data = malloc (sizeof (char *)*DS_COLLECTION_CSSD_NFIELDS -
					                               1);      // all but id field
	
					if (! ((*ids_dataset)->data)) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to create data element of IDs dataset when reading cssd");
						free (*ids_dataset);
						*ids_dataset = NULL;
						bson_destroy (opts);
						bson_destroy (query);
						ret_val = false;
					}
	
					else {
						cursor = mongoc_collection_find_with_opts (mongo_cssd_collection, query, opts,
						                                        NULL);
	
						if (!cursor) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "failed to create cursor when reading cssd");
							free ((*ids_dataset)->data);
							free (*ids_dataset);
							*ids_dataset = NULL;
							bson_destroy (opts);
							bson_destroy (query);
							ret_val = false;
						}
	
						else {
							bson_iter_t iter;
	
							if (mongoc_cursor_next (cursor, &doc) && bson_iter_init (&iter, doc)) {
								bson_iter_next (&iter);             // skip _id
								REGISTER ushort i = 0;
	
								while (bson_iter_next (&iter)) {
									if (i == DS_COL_CSSD_REF_ID_IDX - 1) {
										// handle int32 field
										char tmp_val[DS_INT32_MAX_STRING_LENGTH + 1];
										sprintf (tmp_val, "%d", bson_iter_int32 (&iter));
										const ushort sz = (ushort) (strlen (tmp_val) + 1);
										(*ids_dataset)->data[i] = malloc (sz);
	
										if ((*ids_dataset)->data[i]) {
											strncpy ((*ids_dataset)->data[i], tmp_val, sz);
											i++;
										}
	
										else {
											ret_val = false;
										}
									}
	
									else
										if (i == DS_COL_CSSD_PUBLISHED_IDX - 1) {
											// handle boolean field
											bool published = bson_iter_bool (&iter);
											(*ids_dataset)->data[i] = malloc (2);
	
											if ((*ids_dataset)->data[i]) {
												sprintf ((*ids_dataset)->data[i], "%d", published);
												i++;
											}
	
											else {
												ret_val = false;
											}
										}
	
										else {
											const char *tmp_val = bson_iter_utf8 (&iter, NULL);
											const ushort sz = (ushort) (bson_strnlen (tmp_val,
											                                        DS_GENERIC_FIELD_LENGTH) + 1);
											(*ids_dataset)->data[i] = malloc (sz);
	
											if ((*ids_dataset)->data[i]) {
												strncpy ((*ids_dataset)->data[i], tmp_val, sz);
												i++;
											}
	
											else {
												ret_val = false;
											}
										}
	
									if (!ret_val) {
										for (REGISTER ushort j = 0; j < i; j++) {
											free ((*ids_dataset)->data[j]);
										}
	
										free ((*ids_dataset)->data);
										free (*ids_dataset);
										*ids_dataset = NULL;
										bson_destroy (opts);
										bson_destroy (query);
										mongoc_cursor_destroy (cursor);
										break;
									}
								}
	
								if (ret_val) {
									if (i != DS_COLLECTION_CSSD_NFIELDS - 1) {
										DEBUG_NOW (REPORT_WARNINGS, DATASTORE,
										           "incorrect number of fields found when reading cssd");
	
										for (REGISTER ushort j = 0; j < i; j++) {
											free ((*ids_dataset)->data[j]);
										}
	
										free ((*ids_dataset)->data);
										free (*ids_dataset);
										*ids_dataset = NULL;
										bson_destroy (opts);
										bson_destroy (query);
										mongoc_cursor_destroy (cursor);
										ret_val = false;
									}
	
									else {
										(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
										(*ids_dataset)->num_records = 1;
										(*ids_dataset)->num_fields_per_record = DS_COLLECTION_CSSD_NFIELDS - 1;
									}
								}
							}
	
							else {
								(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
								(*ids_dataset)->num_records = 0;
								(*ids_dataset)->num_fields_per_record = 0;
							}
	
							if (ret_val) {
								ret_val = !mongoc_cursor_error (cursor, NULL);
								bson_destroy (opts);
								bson_destroy (query);
								mongoc_cursor_destroy (cursor);
							}
						}
					}
				}
			}
		}
	
		#elif DATASTORE_TYPE==2         // MonetDB
		#else
		#endif
	}
	DS_LOCK_E
	return ret_val;
}

/*
 * read cssds by reference id
 *
 * note on args:
 *      start is 1-indexed
 *      limit 0 -> retrieves all cssds available
 */
bool read_cssds_by_ref_id (ds_int32_field ref_id, ds_int32_field start,
                           ds_int32_field limit, ds_int32_field order, dsp_dataset *ids_dataset) {
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "NULL IDs dataset found when readings cssds");
		return false;
	}
	
	if (start < 1) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "invalid start index found when readings cssds");
		return false;
	}
	
	// limit==0 -> return all cssds
	if (limit < 0) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "invalid dataset limit found when readings cssds");
		return false;
	}
	
	*ids_dataset = NULL;
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when readings cssds");
		ret_val = false;
	}
	
	else {
		BSON_APPEND_INT32 (query, DS_COL_CSSD_REF_ID, ref_id);
		opts = BCON_NEW ("skip",
		                 BCON_INT32 (start - 1));           // TODO: check allocation
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when readings cssds");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			if (limit) {
				BCON_APPEND (opts, "limit", BCON_INT32 (limit));
			}
	
			ds_int32_field cssd_count = mongoc_collection_count_documents (
			                                        mongo_cssd_collection, query, opts, NULL, NULL, NULL);
	
			if (cssd_count == -1) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to retrieve collection count when readings cssds");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				if (limit > cssd_count) {
					DEBUG_NOW2 (REPORT_ERRORS, DATASTORE,
					            "specified limit (%d) is larger than cssd count (%d) when reading cssds",
					            limit, cssd_count);
					bson_destroy (opts);
					bson_destroy (query);
					ret_val = false;
				}
	
				else {
					*ids_dataset = malloc (sizeof (struct ds_dataset));
	
					if (!*ids_dataset) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to create IDs dataset when readings cssds");
						bson_destroy (opts);
						bson_destroy (query);
						ret_val = false;
					}
	
					else {
						(*ids_dataset)->data = malloc (
						                                        sizeof (char *)*
						                                        DS_COLLECTION_CSSD_NFIELDS * (size_t) cssd_count);
	
						if (! ((*ids_dataset)->data)) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "failed to create data element for IDs dataset when readings cssds");
							free (*ids_dataset);
							*ids_dataset = NULL;
							bson_destroy (opts);
							bson_destroy (query);
							ret_val = false;
						}
	
						else {
							if (!cssd_count) {
								(*ids_dataset)->num_records = 0;
								(*ids_dataset)->num_fields_per_record = 0;
								(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
								bson_destroy (opts);
								bson_destroy (query);
							}
	
							else {
								BCON_APPEND (opts, "sort", "{", DS_COL_CSSD_NAME, BCON_INT32 (1), "}",
								             "collation", "{", "locale", BCON_UTF8 ("en_US"), "strength", BCON_INT32 (1),
								             "}");
								cursor = mongoc_collection_find_with_opts (mongo_cssd_collection, query, opts,
								                                        NULL);
	
								if (!cursor) {
									DEBUG_NOW (REPORT_ERRORS, DATASTORE,
									           "failed to create cursor when readings cssds");
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (opts);
									bson_destroy (query);
									ret_val = false;
								}
	
								else {
									bson_iter_t iter;
									REGISTER ulong j = 0;
	
									while (ret_val && mongoc_cursor_next (cursor, &doc) &&
									       bson_iter_init (&iter, doc)) {
										REGISTER ulong i = 0;
	
										while (bson_iter_next (&iter)) {
											// include ref_id in read_cssd_by_ref_id (as ref_id is used to distinguish between guest/private consensa)
											if (i == DS_COL_CSSD_REF_ID_IDX) {
												int64_t this_ref_id = bson_iter_int64 (&iter);
												(*ids_dataset)->data[j] = malloc (snprintf (0, 0, "%"PRId64, this_ref_id) + 1);
	
												if ((*ids_dataset)->data[j]) {
													sprintf ((*ids_dataset)->data[j], "%"PRId64, this_ref_id);
													i++;
													j++;
												}
	
												else {
													DEBUG_NOW (REPORT_ERRORS, DATASTORE,
													           "could not allocate memory for data element of IDs dataset when readings cssds");
													ret_val = false;
												}
											}
	
											else
												if (i == DS_COL_CSSD_PUBLISHED_IDX) {
													bool published = bson_iter_bool (&iter);
													(*ids_dataset)->data[j] = malloc (2);
	
													if ((*ids_dataset)->data[j]) {
														sprintf ((*ids_dataset)->data[j], "%d", published);
														i++;
														j++;
													}
	
													else {
														DEBUG_NOW (REPORT_ERRORS, DATASTORE,
														           "could not allocate memory for cssd document when readings cssds");
														ret_val = false;
													}
												}
	
												else
													if (i) {
														const char *tmp_val = bson_iter_utf8 (&iter, NULL);
														const ulong sz = (ulong) (bson_strnlen (tmp_val, DS_GENERIC_FIELD_LENGTH));
														(*ids_dataset)->data[j] = malloc (sz + 1);
	
														if ((*ids_dataset)->data[j]) {
															strncpy ((*ids_dataset)->data[j], tmp_val, sz);
															((char *) (*ids_dataset)->data[j])[sz] = '\0';
															i++;
															j++;
														}
	
														else {
															DEBUG_NOW (REPORT_ERRORS, DATASTORE,
															           "could not allocate memory for cssd document when readings cssds");
															ret_val = false;
														}
													}
	
													else {
														char oid_string[DS_OBJ_ID_LENGTH + 1];
														const bson_oid_t *tmp_oid = bson_iter_oid (&iter);
														bson_oid_to_string (tmp_oid, oid_string);
														(*ids_dataset)->data[j] = malloc (DS_OBJ_ID_LENGTH + 1);
	
														if ((*ids_dataset)->data[j]) {
															strncpy ((*ids_dataset)->data[j], oid_string, DS_OBJ_ID_LENGTH + 1);
															((char *) (*ids_dataset)->data[j])[DS_OBJ_ID_LENGTH] = '\0';
															i++;
															j++;
														}
	
														else {
															DEBUG_NOW (REPORT_ERRORS, DATASTORE,
															           "could not allocate memory for cssd document when readings cssds");
															ret_val = false;
														}
													}
	
											if (!ret_val) {
												for (REGISTER ulong k = 0; k < j; k++) {
													free ((*ids_dataset)->data[j]);
												}
	
												free ((*ids_dataset)->data);
												free (*ids_dataset);
												*ids_dataset = NULL;
												bson_destroy (opts);
												bson_destroy (query);
												mongoc_cursor_destroy (cursor);
												break;
											}
										}
	
										if (ret_val) {
											if (DS_COLLECTION_CSSD_NFIELDS != i) {
												DEBUG_NOW (REPORT_WARNINGS, DATASTORE,
												           "unexpected number of fields found when readings cssds");
	
												for (REGISTER ulong k = 0; k < i; k++) {
													free ((*ids_dataset)->data[k]);
												}
	
												free ((*ids_dataset)->data);
												free (*ids_dataset);
												*ids_dataset = NULL;
												bson_destroy (opts);
												bson_destroy (query);
												mongoc_cursor_destroy (cursor);
												ret_val = false;
											}
										}
									}
	
									if (ret_val) {
										ret_val = !mongoc_cursor_error (cursor, NULL) &&
										          ((j / DS_COLLECTION_CSSD_NFIELDS) == cssd_count);
	
										if (ret_val) {
											(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
											(*ids_dataset)->num_records = (ulong) cssd_count;
											(*ids_dataset)->num_fields_per_record = DS_COLLECTION_CSSD_NFIELDS;
										}
	
										else {
											DEBUG_NOW (REPORT_ERRORS, DATASTORE,
											           "could not retrieve cursor when readings cssds");
	
											for (REGISTER ulong k = 0; k < j; k++) {
												free ((*ids_dataset)->data[k]);
											}
	
											free ((*ids_dataset)->data);
											free (*ids_dataset);
											*ids_dataset = NULL;
										}
	
										bson_destroy (opts);
										bson_destroy (query);
										mongoc_cursor_destroy (cursor);
									}
								}
							}
						}
					}
				}
			}
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

/*
 * read cssds by published status
 *
 * yields either
 * a) only the CSSDs that are marked as published, irrespective of ref_id, or
 * b) all CSSDs
 * in either case, the dataset retrieved is within given start/limit/order
 */
bool read_cssds_by_published_status (ds_boolean_field published,
                                     ds_int32_field start, ds_int32_field limit, ds_int32_field order,
                                     dsp_dataset *ids_dataset) {
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "NULL IDs dataset found when reading cssds by published status");
		return false;
	}
	
	if (start < 1) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "invalid start index found when reading cssds by published status");
		return false;
	}
	
	// limit==0 -> return all cssds
	if (limit < 0) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "invalid dataset limit found when reading cssds by published status");
		return false;
	}
	
	*ids_dataset = NULL;
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading cssds by published status");
		ret_val = false;
	}
	
	else {
		if (published) {
			// retrieve only CSSDs that are made public
			BSON_APPEND_BOOL (query, DS_COL_CSSD_PUBLISHED, published);
		}
	
		opts = BCON_NEW ("skip",
		                 BCON_INT32 (start - 1));           // TODO: check allocation
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when reading cssds by published status");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			if (limit) {
				BCON_APPEND (opts, "limit", BCON_INT32 (limit));
			}
	
			ds_int32_field cssd_count = mongoc_collection_count_documents (
			                                        mongo_cssd_collection, query, opts, NULL, NULL, NULL);
	
			if (cssd_count == -1) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to retrieve collection count when reading cssds by published status");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				if (limit > cssd_count) {
					DEBUG_NOW2 (REPORT_ERRORS, DATASTORE,
					            "specified limit (%d) is larger than cssd count (%d) when reading cssds by published status",
					            limit, cssd_count);
					bson_destroy (opts);
					bson_destroy (query);
					ret_val = false;
				}
	
				else {
					*ids_dataset = malloc (sizeof (struct ds_dataset));
	
					if (!*ids_dataset) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to create IDs dataset when reading cssds by published status");
						bson_destroy (opts);
						bson_destroy (query);
						ret_val = false;
					}
	
					else {
						(*ids_dataset)->data = malloc (
						                                        sizeof (char *)*
						                                        DS_COLLECTION_CSSD_NFIELDS * (size_t) cssd_count);
	
						if (! ((*ids_dataset)->data)) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "failed to create data element for IDs dataset when reading cssds by published status");
							free (*ids_dataset);
							*ids_dataset = NULL;
							bson_destroy (opts);
							bson_destroy (query);
							ret_val = false;
						}
	
						else {
							if (!cssd_count) {
								(*ids_dataset)->num_records = 0;
								(*ids_dataset)->num_fields_per_record = 0;
								(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
								bson_destroy (opts);
								bson_destroy (query);
							}
	
							else {
								BCON_APPEND (opts, "sort", "{", DS_COL_CSSD_NAME, BCON_INT32 (1), "}",
								             "collation", "{", "locale", BCON_UTF8 ("en_US"), "strength", BCON_INT32 (1),
								             "}");
								cursor = mongoc_collection_find_with_opts (mongo_cssd_collection, query, opts,
								                                        NULL);
	
								if (!cursor) {
									DEBUG_NOW (REPORT_ERRORS, DATASTORE,
									           "failed to create cursor when reading cssds by published status");
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (opts);
									bson_destroy (query);
									ret_val = false;
								}
	
								else {
									bson_iter_t iter;
									REGISTER ulong j = 0;
	
									while (ret_val && mongoc_cursor_next (cursor, &doc) &&
									       bson_iter_init (&iter, doc)) {
										REGISTER ulong i = 0;
	
										while (bson_iter_next (&iter)) {
											if (i == DS_COL_CSSD_REF_ID_IDX) {
												int64_t this_ref_id = bson_iter_int64 (&iter);
												(*ids_dataset)->data[j] = malloc (snprintf (0, 0, "%"PRId64, this_ref_id) + 1);
	
												if ((*ids_dataset)->data[j]) {
													sprintf ((*ids_dataset)->data[j], "%"PRId64, this_ref_id);
													i++;
													j++;
												}
	
												else {
													DEBUG_NOW (REPORT_ERRORS, DATASTORE,
													           "could not allocate memory for cssds document when reading cssds by published status");
													ret_val = false;
												}
											}
	
											else
												if (i == DS_COL_CSSD_PUBLISHED_IDX) {
													bool published = bson_iter_bool (&iter);
													(*ids_dataset)->data[j] = malloc (2);
	
													if ((*ids_dataset)->data[j]) {
														sprintf ((*ids_dataset)->data[j], "%d", published);
														i++;
														j++;
													}
	
													else {
														DEBUG_NOW (REPORT_ERRORS, DATASTORE,
														           "could not allocate memory for cssds document when reading cssds by published status");
														ret_val = false;
													}
												}
	
												else
													if (i) {
														const char *tmp_val = bson_iter_utf8 (&iter, NULL);
														const ulong sz = (ulong) (bson_strnlen (tmp_val, DS_GENERIC_FIELD_LENGTH));
														(*ids_dataset)->data[j] = malloc (sz + 1);
	
														if ((*ids_dataset)->data[j]) {
															strncpy ((*ids_dataset)->data[j], tmp_val, sz);
															((char *) (*ids_dataset)->data[j])[sz] = '\0';
															i++;
															j++;
														}
	
														else {
															DEBUG_NOW (REPORT_ERRORS, DATASTORE,
															           "could not allocate memory for cssds document when reading cssds by published status");
															ret_val = false;
														}
													}
	
													else {
														char oid_string[DS_OBJ_ID_LENGTH + 1];
														const bson_oid_t *tmp_oid = bson_iter_oid (&iter);
														bson_oid_to_string (tmp_oid, oid_string);
														(*ids_dataset)->data[j] = malloc (DS_OBJ_ID_LENGTH + 1);
	
														if ((*ids_dataset)->data[j]) {
															strncpy ((*ids_dataset)->data[j], oid_string, DS_OBJ_ID_LENGTH + 1);
															((char *) (*ids_dataset)->data[j])[DS_OBJ_ID_LENGTH] = '\0';
															i++;
															j++;
														}
	
														else {
															DEBUG_NOW (REPORT_ERRORS, DATASTORE,
															           "could not allocate memory for cssds document when reading cssds by published status");
															ret_val = false;
														}
													}
	
											if (!ret_val) {
												for (REGISTER ulong k = 0; k < j; k++) {
													free ((*ids_dataset)->data[j]);
												}
	
												free ((*ids_dataset)->data);
												free (*ids_dataset);
												*ids_dataset = NULL;
												bson_destroy (opts);
												bson_destroy (query);
												mongoc_cursor_destroy (cursor);
												break;
											}
										}
	
										if (ret_val) {
											if (DS_COLLECTION_CSSD_NFIELDS != i) {
												DEBUG_NOW (REPORT_WARNINGS, DATASTORE,
												           "unexpected number of fields found when reading cssds by published status");
	
												for (REGISTER ulong k = 0; k < i; k++) {
													free ((*ids_dataset)->data[k]);
												}
	
												free ((*ids_dataset)->data);
												free (*ids_dataset);
												*ids_dataset = NULL;
												bson_destroy (opts);
												bson_destroy (query);
												mongoc_cursor_destroy (cursor);
												ret_val = false;
											}
										}
									}
	
									if (ret_val) {
										ret_val = !mongoc_cursor_error (cursor, NULL) &&
										          ((j / DS_COLLECTION_CSSD_NFIELDS) == cssd_count);
	
										if (ret_val) {
											(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
											(*ids_dataset)->num_records = (ulong) cssd_count;
											(*ids_dataset)->num_fields_per_record = DS_COLLECTION_CSSD_NFIELDS;
										}
	
										else {
											DEBUG_NOW (REPORT_ERRORS, DATASTORE,
											           "cursor error found when reading cssds by published status");
	
											for (REGISTER ulong k = 0; k < j; k++) {
												free ((*ids_dataset)->data[k]);
											}
	
											free ((*ids_dataset)->data);
											free (*ids_dataset);
											*ids_dataset = NULL;
										}
	
										bson_destroy (opts);
										bson_destroy (query);
										mongoc_cursor_destroy (cursor);
									}
								}
							}
						}
					}
				}
			}
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool read_cssd_id_by_string (const char *cssd_string,
                             ds_object_id_field *cssd_oid) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading cssds by string");
		ret_val = false;
	}
	
	else {
		BSON_APPEND_UTF8 (query, DS_COL_CSSD_STRING, cssd_string);
		opts = BCON_NEW ("sort", "{", MONGODB_OBJECT_ID_FIELD, BCON_INT32 (-1), "}");
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when reading cssds by string");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			cursor = mongoc_collection_find_with_opts (mongo_sequences_collection, query,
			                                        opts, NULL);
	
			if (!cursor) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create cursor when reading cssds by string");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				bson_iter_t iter;
	
				if (mongoc_cursor_next (cursor, &doc) && bson_iter_init (&iter, doc) &&
				    bson_iter_next (&iter)) {
					bson_oid_to_string (bson_iter_oid (&iter), (char *) cssd_oid);
				}
	
				ret_val = !mongoc_cursor_error (cursor, NULL);
				bson_destroy (opts);
				bson_destroy (query);
				mongoc_cursor_destroy (cursor);
			}
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool read_cssd_by_name (ds_generic_field *name, dsp_dataset *ids_dataset) {
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "NULL IDs dataset found when reading cssds by name");
		return false;
	}
	
	*ids_dataset = NULL;
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading cssds by name");
		ret_val = false;
	}
	
	else {
		BSON_APPEND_UTF8 (query, DS_COL_CSSD_NAME, *name);
		opts = BCON_NEW ("sort", "{", MONGODB_OBJECT_ID_FIELD, BCON_INT32 (-1), "}");
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when reading cssds by name");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			*ids_dataset = malloc (sizeof (struct ds_dataset));
	
			if (!*ids_dataset) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create IDs dataset when reading cssds by name");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				(*ids_dataset)->data = malloc (sizeof (char *)*DS_COLLECTION_CSSD_NFIELDS -
				                               1);      // 1 record x group,definition,accession,3'UTR
	
				if (! ((*ids_dataset)->data)) {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "failed to create data element for IDs dataset when reading cssds by name");
					free (*ids_dataset);
					*ids_dataset = NULL;
					bson_destroy (opts);
					bson_destroy (query);
					ret_val = false;
				}
	
				else {
					cursor = mongoc_collection_find_with_opts (mongo_cssd_collection, query, opts,
					                                        NULL);
	
					if (!cursor) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to create cursor when reading cssds by name");
						free ((*ids_dataset)->data);
						free (*ids_dataset);
						*ids_dataset = NULL;
						bson_destroy (opts);
						bson_destroy (query);
						ret_val = false;
					}
	
					else {
						bson_iter_t iter;
	
						if (mongoc_cursor_next (cursor, &doc) && bson_iter_init (&iter, doc)) {
							bson_iter_next (&iter);             // skip _id
							REGISTER ulong i = 0;
	
							while (bson_iter_next (&iter)) {
								if (i == DS_COL_CSSD_REF_ID_IDX - 1) {
									// handle int32 field
									char tmp_val[DS_INT32_MAX_STRING_LENGTH + 1];
									sprintf (tmp_val, "%d", bson_iter_int32 (&iter));
									const ulong sz = strlen (tmp_val) + 1;
									(*ids_dataset)->data[i] = malloc (sz);
	
									if ((*ids_dataset)->data[i]) {
										strncpy ((*ids_dataset)->data[i], tmp_val, sz);
										i++;
									}
	
									else {
										ret_val = false;
									}
								}
	
								else
									if (i == DS_COL_CSSD_PUBLISHED_IDX - 1) {
										bool published = bson_iter_bool (&iter);
										(*ids_dataset)->data[i] = malloc (2);
	
										if ((*ids_dataset)->data[i]) {
											sprintf ((*ids_dataset)->data[i], "%d", published);
											i++;
										}
	
										else {
											ret_val = false;
										}
									}
	
									else {
										const char *tmp_val = bson_iter_utf8 (&iter, NULL);
										const ulong sz = bson_strnlen (tmp_val, DS_GENERIC_FIELD_LENGTH) + 1;
										(*ids_dataset)->data[i] = malloc (sz);
	
										if ((*ids_dataset)->data[i]) {
											strncpy ((*ids_dataset)->data[i], tmp_val, sz);
											i++;
										}
	
										else {
											ret_val = false;
										}
									}
	
								if (!ret_val) {
									for (REGISTER ulong j = 0; j < i; j++) {
										free ((*ids_dataset)->data[j]);
									}
	
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (opts);
									bson_destroy (query);
									mongoc_cursor_destroy (cursor);
									break;
								}
							}
	
							if (ret_val) {
								if (i != DS_COLLECTION_CSSD_NFIELDS - 1) {
									DEBUG_NOW (REPORT_WARNINGS, DATASTORE,
									           "unexpected number of fields found when reading cssds by name");
	
									for (REGISTER ulong j = 0; j < i; j++) {
										free ((*ids_dataset)->data[j]);
									}
	
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (opts);
									bson_destroy (query);
									mongoc_cursor_destroy (cursor);
									ret_val = false;
								}
	
								else {
									(*ids_dataset)->num_records = 1;
									(*ids_dataset)->num_fields_per_record = DS_COLLECTION_CSSD_NFIELDS - 1;
									(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
								}
							}
						}
	
						else {
							(*ids_dataset)->num_records = 0;
							(*ids_dataset)->num_fields_per_record = 0;
							(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
						}
	
						if (ret_val) {
							ret_val = !mongoc_cursor_error (cursor, NULL);
							bson_destroy (opts);
							bson_destroy (query);
							mongoc_cursor_destroy (cursor);
						}
					}
				}
			}
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool delete_cssd_by_id (ds_object_id_field *object_id, ds_int32_field ref_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when deleting cssd");
		ret_val = false;
	}
	
	else {
		bson_t reply;
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		BSON_APPEND_INT32 (query, DS_COL_CSSD_REF_ID, ref_id);
	
		if (!mongoc_collection_delete_one (mongo_cssd_collection, query, NULL, &reply,
		                                   NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete document when deleting cssd");
			ret_val = false;
		}
	
		else {
			char *reply_json = bson_as_canonical_extended_json (&reply, NULL);
			jsmn_init (&jsmn_p);
			uchar num_tokens_parsed = jsmn_parse (&jsmn_p, reply_json, strlen (reply_json),
			                                      jsmn_t, MAX_JSON_PARSE_TOKENS);
			/*
			 * expect JSON string having format { "deletedCount" : { "$numberInt" : "1" } }
			 */
			bool success = false;
	
			if (5 == num_tokens_parsed) {
				if (jsmn_t[0].type == JSMN_OBJECT && jsmn_t[1].type == JSMN_STRING) {
					char *strn = reply_json + jsmn_t[1].start;
	
					if (!strncmp (strn, "deletedCount", jsmn_t[1].end - jsmn_t[1].start) &&
					    jsmn_t[2].type == JSMN_OBJECT && jsmn_t[3].type == JSMN_STRING) {
						strn = reply_json + jsmn_t[3].start;
	
						if (!strncmp (strn, "$numberInt", jsmn_t[3].end - jsmn_t[3].start) &&
						    jsmn_t[4].type == JSMN_STRING) {
							strn = reply_json + jsmn_t[4].start;
	
							if (!strncmp (strn, "1", jsmn_t[4].end - jsmn_t[4].start)) {
								success = true;
							}
						}
					}
				}
			}
	
			if (!success) {
				// do not log any messages - just signal that deletion not successful (presumed not found)
				ret_val = false;
			}
	
			else
				if (do_notify) {
					// for delete events, ref_id will not be available to watch -> notify directly here
					ds_notification_event *this_event = (ds_notification_event *)malloc (sizeof (
					                                        ds_notification_event));
	
					if (this_event) {
						unsigned long long now = get_real_time();
						this_event->time = now;
						this_event->ref_id = ref_id;
						this_event->collection = DS_COL_CSSD;
						this_event->op_type = DS_NOTIFY_OP_DELETE;
						strncpy (this_event->oid_string, (char *)object_id, DS_OBJ_ID_LENGTH);
	
						// enq after the fact, since enq_ds acquires DS_LOCK
						if (!enq_ds (this_event)) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "failed to enqueue notification event after deleting cssd");
						}
					}
	
					else {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to allocate memory for notification event after deleting cssd");
					}
				}
		}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

/*
 * delete_cssd differs from delete_cssd_by_id in that it is not query restricted
 * to use ref_id; however, ref_id is still passed as an argument for notification
 */
bool delete_cssd (ds_object_id_field *object_id, ds_int32_field ref_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when deleting cssd");
		ret_val = false;
	}
	
	else {
		bson_t reply;
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
	
		if (!mongoc_collection_delete_one (mongo_cssd_collection, query, NULL, &reply,
		                                   NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete document when deleting cssd");
			ret_val = false;
		}
	
		else {
			char *reply_json = bson_as_canonical_extended_json (&reply, NULL);
			jsmn_init (&jsmn_p);
			uchar num_tokens_parsed = jsmn_parse (&jsmn_p, reply_json, strlen (reply_json),
			                                      jsmn_t, MAX_JSON_PARSE_TOKENS);
			/*
			 * expect JSON string having format { "deletedCount" : { "$numberInt" : "1" } }
			 */
			bool success = false;
	
			if (5 == num_tokens_parsed) {
				if (jsmn_t[0].type == JSMN_OBJECT && jsmn_t[1].type == JSMN_STRING) {
					char *strn = reply_json + jsmn_t[1].start;
	
					if (!strncmp (strn, "deletedCount", jsmn_t[1].end - jsmn_t[1].start) &&
					    jsmn_t[2].type == JSMN_OBJECT && jsmn_t[3].type == JSMN_STRING) {
						strn = reply_json + jsmn_t[3].start;
	
						if (!strncmp (strn, "$numberInt", jsmn_t[3].end - jsmn_t[3].start) &&
						    jsmn_t[4].type == JSMN_STRING) {
							strn = reply_json + jsmn_t[4].start;
	
							if (!strncmp (strn, "1", jsmn_t[4].end - jsmn_t[4].start)) {
								success = true;
							}
						}
					}
				}
			}
	
			if (!success) {
				// do not log any messages - just signal that deletion not successful (presumed not found)
				ret_val = false;
			}
	
			else
				if (do_notify) {
					// for delete events, ref_id will not be available to watch -> notify directly here
					ds_notification_event *this_event = (ds_notification_event *)malloc (sizeof (
					                                        ds_notification_event));
	
					if (this_event) {
						unsigned long long now = get_real_time();
						this_event->time = now;
						this_event->ref_id = ref_id;
						this_event->collection = DS_COL_CSSD;
						this_event->op_type = DS_NOTIFY_OP_DELETE;
						strncpy (this_event->oid_string, (char *)object_id, DS_OBJ_ID_LENGTH);
	
						// enq after the fact, since enq_ds acquires DS_LOCK
						if (!enq_ds (this_event)) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "cannot enqueue notification event after deleting cssd");
						}
					}
	
					else {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "cannot allocate memory for notification event after deleting cssd");
					}
				}
		}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

/*
 * TODO: publish notification event when deleting cssd by name
 */

bool delete_cssd_by_name (ds_generic_field *name, ds_int32_field ref_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when deleting cssd by name");
		ret_val = false;
	}
	
	else {
		bson_t reply;
		BSON_APPEND_UTF8 (query, DS_COL_CSSD_NAME, *name);
		BSON_APPEND_INT32 (query, DS_COL_CSSD_REF_ID, ref_id);
	
		if (!mongoc_collection_delete_one (mongo_cssd_collection, query, NULL, &reply,
		                                   NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete document when deleting cssd by name");
			ret_val = false;
		}
	
		else {
			char *reply_json = bson_as_canonical_extended_json (&reply, NULL);
			jsmn_init (&jsmn_p);
			uchar num_tokens_parsed = jsmn_parse (&jsmn_p, reply_json, strlen (reply_json),
			                                      jsmn_t, MAX_JSON_PARSE_TOKENS);
			/*
			 * expect JSON string having format { "deletedCount" : { "$numberInt" : "1" } }
			 */
			bool success = false;
	
			if (5 == num_tokens_parsed) {
				if (jsmn_t[0].type == JSMN_OBJECT && jsmn_t[1].type == JSMN_STRING) {
					char *strn = reply_json + jsmn_t[1].start;
	
					if (!strncmp (strn, "deletedCount", jsmn_t[1].end - jsmn_t[1].start) &&
					    jsmn_t[2].type == JSMN_OBJECT && jsmn_t[3].type == JSMN_STRING) {
						strn = reply_json + jsmn_t[3].start;
	
						if (!strncmp (strn, "$numberInt", jsmn_t[3].end - jsmn_t[3].start) &&
						    jsmn_t[4].type == JSMN_STRING) {
							strn = reply_json + jsmn_t[4].start;
	
							if (!strncmp (strn, "1", jsmn_t[4].end - jsmn_t[4].start)) {
								success = true;
							}
						}
					}
				}
			}
	
			if (!success) {
				// do not log any messages - just signal that deletion not successful (presumed not found)
				ret_val = false;
			}
		}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool get_cssd_published_status (ds_object_id_field *object_id,
                                ds_boolean_field *status) {
	bool ret_val = true;
	*status = false;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when getting cssd published status");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		opts = BCON_NEW ("sort", "{", MONGODB_OBJECT_ID_FIELD, BCON_INT32 (-1), "}");
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when getting cssd published status");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			cursor = mongoc_collection_find_with_opts (mongo_cssd_collection, query, opts,
			                                        NULL);
	
			if (!cursor) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create cursor when getting cssd published status");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				bson_iter_t iter;
				g_memset (&iter, 0, sizeof (bson_iter_t));
	
				if (mongoc_cursor_next (cursor, &doc) && bson_iter_init (&iter, doc)) {
					bson_iter_next (&iter);             // skip _id
					REGISTER ulong i = 0;
	
					while (bson_iter_next (&iter)) {
						if (i == DS_COL_CSSD_PUBLISHED_IDX - 1) {
							*status = bson_iter_bool (&iter);
							break;
						}
	
						i++;
					}
				}
	
				if (ret_val) {
					ret_val = !mongoc_cursor_error (cursor, NULL);
					bson_destroy (opts);
					bson_destroy (query);
					mongoc_cursor_destroy (cursor);
				}
			}
		}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
}

DS_LOCK_E

return ret_val;
}
/*
 * jobs collection methods
 */
bool create_job (ds_object_id_field *sequence_id, ds_object_id_field *cssd_id,
                 ds_int32_field status, ds_int32_field error,
                 ds_int32_field ref_id, ds_object_id_field *new_object_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *job_doc = bson_new();
	
	if (!job_doc) {
		ret_val = false;
	}
	
	else {
		get_real_time_bytes (new_object_id);
		bson_oid_t new_oid;
		bson_oid_init_from_data (&new_oid, (unsigned char *) new_object_id);
		BSON_APPEND_OID (job_doc, MONGODB_OBJECT_ID_FIELD, &new_oid);
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, (char *) sequence_id);
		BSON_APPEND_OID (job_doc, DS_COL_JOB_SEQUENCE_ID, &oid);
		bson_oid_init_from_string (&oid, (char *) cssd_id);
		BSON_APPEND_OID (job_doc, DS_COL_JOB_CSSD_ID, &oid);
		BSON_APPEND_INT32 (job_doc, DS_COL_JOB_STATUS, status);
		BSON_APPEND_INT32 (job_doc, DS_COL_JOB_ERROR, error);
		BSON_APPEND_INT32 (job_doc, DS_COL_JOB_NUM_WINDOWS, 0);
		BSON_APPEND_INT32 (job_doc, DS_COL_JOB_NUM_WINDOWS_SUCCESS, 0);
		BSON_APPEND_INT32 (job_doc, DS_COL_JOB_NUM_WINDOWS_FAIL, 0);
		BSON_APPEND_INT32 (job_doc, DS_COL_JOB_REF_ID, ref_id);
	
		if (!mongoc_collection_insert_one (mongo_jobs_collection, job_doc, NULL, NULL,
		                                   NULL)) {
			bson_destroy (job_doc);
			ret_val = false;
		}
	
		bson_destroy (job_doc);
	}
	
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool read_job (ds_object_id_field *object_id, dsp_dataset *ids_dataset) {
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "NULL IDs dataset found when reading job");
		return false;
	}
	
	*ids_dataset = NULL;
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading job");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		opts = BCON_NEW ("sort", "{", MONGODB_OBJECT_ID_FIELD, BCON_INT32 (-1), "}");
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when reading job");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			*ids_dataset = malloc (sizeof (struct ds_dataset));
	
			if (!*ids_dataset) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "cannot allocate memory for IDs dataset when reading job");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				(*ids_dataset)->data = malloc (sizeof (char *) * (DS_COLLECTION_JOBS_NFIELDS));
	
				if (! ((*ids_dataset)->data)) {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "cannot allocate memory for data element of IDs dataset when reading job");
					free (*ids_dataset);
					*ids_dataset = NULL;
					bson_destroy (opts);
					bson_destroy (query);
					ret_val = false;
				}
	
				else {
					cursor = mongoc_collection_find_with_opts (mongo_jobs_collection, query, opts,
					                                        NULL);
	
					if (!cursor) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to create cursor when reading job");
						free ((*ids_dataset)->data);
						free (*ids_dataset);
						*ids_dataset = NULL;
						bson_destroy (opts);
						bson_destroy (query);
						ret_val = false;
					}
	
					else {
						bson_iter_t iter;
						g_memset (&iter, 0, sizeof (bson_iter_t));
	
						if (mongoc_cursor_next (cursor, &doc) && bson_iter_init (&iter, doc)) {
							REGISTER ulong i = 0;
	
							while (bson_iter_next (&iter)) {
								if (i >= DS_COL_JOB_REF_ID_IDX - 4) {
									// handle int32 field
									char tmp_val[DS_INT32_MAX_STRING_LENGTH + 1];
									sprintf (tmp_val, "%d", bson_iter_int32 (&iter));
									const ulong sz = strlen (tmp_val) + 1;
									(*ids_dataset)->data[i] = malloc (sz);
	
									if ((*ids_dataset)->data[i]) {
										strncpy ((*ids_dataset)->data[i], tmp_val, sz);
										i++;
									}
	
									else {
										ret_val = false;
									}
								}
	
								else {
									ds_generic_field tmp_val;
									g_memset (tmp_val, 0, sizeof (ds_generic_field));
	
									if (i < 3) {
										bson_oid_to_string (bson_iter_oid (&iter),
										                    tmp_val);    // job_id, sequence_id and cssd_id
									}
	
									else {
										sprintf (tmp_val, "%d", bson_iter_int32 (&iter));  // status
									}
	
									const ulong sz = bson_strnlen (tmp_val, DS_GENERIC_FIELD_LENGTH) + 1;
									(*ids_dataset)->data[i] = malloc (sz);
	
									if ((*ids_dataset)->data[i]) {
										strncpy ((*ids_dataset)->data[i], tmp_val, sz);
										i++;
									}
	
									else {
										ret_val = false;
									}
								}
	
								if (!ret_val) {
									for (REGISTER ulong j = 0; j < i; j++) {
										free ((*ids_dataset)->data[j]);
									}
	
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (opts);
									bson_destroy (query);
									mongoc_cursor_destroy (cursor);
									break;
								}
							}
	
							if (i != DS_COLLECTION_JOBS_NFIELDS) {
								DEBUG_NOW (REPORT_WARNINGS, DATASTORE,
								           "unexpected number of fields found when reading job");
	
								for (REGISTER ulong j = 0; j < i; j++) {
									free ((*ids_dataset)->data[j]);
								}
	
								free ((*ids_dataset)->data);
								free (*ids_dataset);
								*ids_dataset = NULL;
								bson_destroy (opts);
								bson_destroy (query);
								mongoc_cursor_destroy (cursor);
								ret_val = false;
							}
	
							else {
								(*ids_dataset)->num_records = 1;
								(*ids_dataset)->num_fields_per_record = DS_COLLECTION_JOBS_NFIELDS;
								(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
							}
						}
	
						else {
							(*ids_dataset)->num_records = 0;
							(*ids_dataset)->num_fields_per_record = 0;
							(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
						}
	
						if (ret_val) {
							ret_val = !mongoc_cursor_error (cursor, NULL);
							bson_destroy (opts);
							bson_destroy (query);
							mongoc_cursor_destroy (cursor);
						}
					}
				}
			}
		}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
}

DS_LOCK_E

return ret_val;
}

bool read_job_windows (ds_object_id_field *object_id,
                       ds_int32_field *num_windows, ds_int32_field *num_windows_success,
                       ds_int32_field *num_windows_fail) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading job windows");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		opts = BCON_NEW ("sort", "{", MONGODB_OBJECT_ID_FIELD, BCON_INT32 (-1), "}");
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when reading job windows");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			cursor = mongoc_collection_find_with_opts (mongo_jobs_collection, query, opts,
			                                        NULL);
	
			if (!cursor) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create cursor when reading job windows");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				bson_iter_t iter;
				g_memset (&iter, 0, sizeof (bson_iter_t));
	
				if (mongoc_cursor_next (cursor, &doc) && bson_iter_init (&iter, doc)) {
					bson_iter_next (&iter);             // skip _id
					REGISTER ulong i = 0;
	
					while (bson_iter_next (&iter)) {
						if (i == DS_COL_JOB_NUM_WINDOWS_IDX - 1) {
							*num_windows = bson_iter_int32 (&iter);
						}
	
						else
							if (i == DS_COL_JOB_NUM_WINDOWS_SUCCESS_IDX - 1) {
								*num_windows_success = bson_iter_int32 (&iter);
							}
	
							else
								if (i == DS_COL_JOB_NUM_WINDOWS_FAIL_IDX - 1) {
									*num_windows_fail = bson_iter_int32 (&iter);
								}
	
						i++;
					}
	
					if (i != DS_COLLECTION_JOBS_NFIELDS - 1) {
						DEBUG_NOW (REPORT_WARNINGS, DATASTORE,
						           "unexpected number of fields found when reading job windows");
						bson_destroy (opts);
						bson_destroy (query);
						mongoc_cursor_destroy (cursor);
						ret_val = false;
					}
				}
	
				if (ret_val) {
					ret_val = !mongoc_cursor_error (cursor, NULL);
					bson_destroy (opts);
					bson_destroy (query);
					mongoc_cursor_destroy (cursor);
				}
			}
		}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
}

DS_LOCK_E

return ret_val;
}

/*
 * read jobs by reference id
 *
 * note on args:
 *      start is 1-indexed
 *      limit 0 -> all jobs
 */
bool read_jobs_by_ref_id (ds_int32_field ref_id, ds_int32_field start,
                          ds_int32_field limit, ds_int32_field order, dsp_dataset *ids_dataset) {
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "NULL IDs dataset found when reading jobs by reference id");
		return false;
	}
	
	if (start < 1) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "invalid start index found when reading jobs by reference id");
		return false;
	}
	
	// limit==0 -> return all jobs
	if (limit < 0) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "invalid dataset limit found when reading jobs by reference id");
		return false;
	}
	
	*ids_dataset = NULL;
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading jobs by reference id");
		ret_val = false;
	}
	
	else {
		BSON_APPEND_INT32 (query, DS_COL_JOB_REF_ID, ref_id);
		opts = BCON_NEW ("skip",
		                 BCON_INT32 (start - 1));           // TODO: check allocation
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when reading jobs by reference id");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			if (limit) {
				BCON_APPEND (opts, "limit", BCON_INT32 (limit));
			}
	
			ds_int32_field jobs_count = mongoc_collection_count_documents (
			                                        mongo_jobs_collection, query, opts, NULL, NULL, NULL);
	
			if (jobs_count == -1) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to retrieve collection count when reading jobs by reference id");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				if (limit > jobs_count) {
					DEBUG_NOW2 (REPORT_ERRORS, DATASTORE,
					            "specified limit (%d) is larger than jobs count (%d) when reading jobs by reference id",
					            limit, jobs_count);
					bson_destroy (opts);
					bson_destroy (query);
					ret_val = false;
				}
	
				else {
					*ids_dataset = malloc (sizeof (struct ds_dataset));
	
					if (!*ids_dataset) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to allocate memory for IDs dataset when reading jobs by reference id");
						bson_destroy (opts);
						bson_destroy (query);
						ret_val = false;
					}
	
					else {
						(*ids_dataset)->data = malloc (
						                                        sizeof (char *)*
						                                        (DS_COLLECTION_JOBS_NFIELDS - 1) * (size_t) jobs_count);
	
						if (! ((*ids_dataset)->data)) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "failed to allocate data element for IDs dataset when reading jobs by reference id");
							free (*ids_dataset);
							*ids_dataset = NULL;
							bson_destroy (opts);
							bson_destroy (query);
							ret_val = false;
						}
	
						else {
							if (!jobs_count) {
								(*ids_dataset)->num_records = 0;
								(*ids_dataset)->num_fields_per_record = 0;
								(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
								bson_destroy (opts);
								bson_destroy (query);
							}
	
							else {
								BCON_APPEND (opts, "sort", "{",
								             MONGODB_OBJECT_ID_FIELD, BCON_INT32 (order),
								             "}");
								cursor = mongoc_collection_find_with_opts (mongo_jobs_collection, query, opts,
								                                        NULL);
	
								if (!cursor) {
									DEBUG_NOW (REPORT_ERRORS, DATASTORE,
									           "failed to create cursor when reading jobs by reference id");
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (opts);
									bson_destroy (query);
									ret_val = false;
								}
	
								else {
									bson_iter_t iter;
									REGISTER ulong j = 0;
	
									while (ret_val && mongoc_cursor_next (cursor, &doc) &&
									       bson_iter_init (&iter, doc)) {
										REGISTER ulong i = 0;
	
										while (bson_iter_next (&iter)) {
											if (DS_COL_JOB_REF_ID_IDX == i) {
												i++;
												continue;       // skip ref_id
											}
	
											else {
												if (i) {
													const char *tmp_val = bson_iter_utf8 (&iter, NULL);
													const ulong sz = bson_strnlen (tmp_val, DS_GENERIC_FIELD_LENGTH);
													(*ids_dataset)->data[j] = malloc (sz + 1);
	
													if ((*ids_dataset)->data[j]) {
														strncpy ((*ids_dataset)->data[j], tmp_val, sz);
														((char *) (*ids_dataset)->data[j])[sz] = '\0';
														i++;
														j++;
													}
	
													else {
														DEBUG_NOW (REPORT_ERRORS, DATASTORE,
														           "failed to allocate memory for jobs document when reading jobs by reference id");
														ret_val = false;
													}
												}
	
												else {
													char oid_string[DS_OBJ_ID_LENGTH + 1];
													const bson_oid_t *tmp_oid = bson_iter_oid (&iter);
													bson_oid_to_string (tmp_oid, oid_string);
													(*ids_dataset)->data[j] = malloc (DS_OBJ_ID_LENGTH + 1);
	
													if ((*ids_dataset)->data[j]) {
														strncpy ((*ids_dataset)->data[j], oid_string, DS_OBJ_ID_LENGTH + 1);
														((char *) (*ids_dataset)->data[j])[DS_OBJ_ID_LENGTH] = '\0';
														i++;
														j++;
													}
	
													else {
														DEBUG_NOW (REPORT_ERRORS, DATASTORE,
														           "failed to allocate memory for jobs document when reading jobs by reference id");
														ret_val = false;
													}
												}
											}
	
											if (!ret_val) {
												for (REGISTER ulong k = 0; k < j; k++) {
													free ((*ids_dataset)->data[j]);
												}
	
												free ((*ids_dataset)->data);
												free (*ids_dataset);
												*ids_dataset = NULL;
												bson_destroy (opts);
												bson_destroy (query);
												mongoc_cursor_destroy (cursor);
												break;
											}
										}
	
										if (ret_val) {
											if (DS_COLLECTION_JOBS_NFIELDS != i) {
												DEBUG_NOW (REPORT_WARNINGS, DATASTORE,
												           "incorrect number of fields found when reading jobs by reference id");
	
												for (REGISTER ulong k = 0; k < i; k++) {
													free ((*ids_dataset)->data[k]);
												}
	
												free ((*ids_dataset)->data);
												free (*ids_dataset);
												*ids_dataset = NULL;
												bson_destroy (opts);
												bson_destroy (query);
												mongoc_cursor_destroy (cursor);
												ret_val = false;
											}
										}
									}
	
									if (ret_val) {
										ret_val = !mongoc_cursor_error (cursor, NULL) &&
										          (j / (DS_COLLECTION_JOBS_NFIELDS - 1) == jobs_count);
	
										if (ret_val) {
											(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
											(*ids_dataset)->num_records = jobs_count;
											(*ids_dataset)->num_fields_per_record = DS_COLLECTION_JOBS_NFIELDS - 1;
										}
	
										else {
											DEBUG_NOW (REPORT_ERRORS, DATASTORE,
											           "cursor error when reading jobs by reference id");
	
											for (REGISTER ulong k = 0; k < j; k++) {
												free ((*ids_dataset)->data[k]);
											}
	
											free ((*ids_dataset)->data);
											free (*ids_dataset);
											*ids_dataset = NULL;
										}
	
										bson_destroy (opts);
										bson_destroy (query);
										mongoc_cursor_destroy (cursor);
									}
								}
							}
						}
					}
				}
			}
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool read_job_ids_by_cssd_id (ds_object_id_field *cssd_id,
                              dsp_dataset *ids_dataset) {
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "NULL IDs dataset found when reading jobs by cssd id");
		return false;
	}
	
	*ids_dataset = NULL;
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading jobs by cssd id");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, (char *) cssd_id);
		BSON_APPEND_OID (query, DS_COL_JOB_CSSD_ID, &oid);
		ds_int32_field jobs_count = mongoc_collection_count_documents (
		                                        mongo_jobs_collection, query, NULL, NULL, NULL, NULL);
	
		if (jobs_count == -1) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to retrieve collection count when reading jobs by cssd id");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			*ids_dataset = malloc (sizeof (struct ds_dataset));
	
			if (!*ids_dataset) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create IDs dataset when reading jobs by cssd id");
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				(*ids_dataset)->data = malloc (2 * sizeof (char *) * (size_t)jobs_count);
	
				if (! ((*ids_dataset)->data)) {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "failed to create data element for IDs dataset when reading jobs by cssd id");
					free (*ids_dataset);
					*ids_dataset = NULL;
					bson_destroy (query);
					ret_val = false;
				}
	
				else {
					if (!jobs_count) {
						(*ids_dataset)->num_records = 0;
						(*ids_dataset)->num_fields_per_record = 0;
						(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
						bson_destroy (query);
					}
	
					else {
						cursor = mongoc_collection_find_with_opts (mongo_jobs_collection, query, NULL,
						                                        NULL);
	
						if (!cursor) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "cursor error when reading jobs by cssd id");
							free ((*ids_dataset)->data);
							free (*ids_dataset);
							*ids_dataset = NULL;
							bson_destroy (query);
							ret_val = false;
						}
	
						else {
							bson_iter_t iter;
							REGISTER ulong j = 0;
	
							while (ret_val && mongoc_cursor_next (cursor, &doc) &&
							       bson_iter_init (&iter, doc)) {
								REGISTER ulong i = 0;
	
								while (bson_iter_next (&iter)) {
									if (!i) {
										char oid_string[DS_OBJ_ID_LENGTH + 1];
										const bson_oid_t *tmp_oid = bson_iter_oid (&iter);
										bson_oid_to_string (tmp_oid, oid_string);
										(*ids_dataset)->data[j] = malloc (DS_OBJ_ID_LENGTH + 1);
	
										if ((*ids_dataset)->data[j]) {
											strncpy ((*ids_dataset)->data[j], oid_string, DS_OBJ_ID_LENGTH + 1);
											((char *) (*ids_dataset)->data[j])[DS_OBJ_ID_LENGTH] = '\0';
											i++;
											j++;
										}
	
										else {
											DEBUG_NOW (REPORT_ERRORS, DATASTORE,
											           "could not allocate memory for jobs document when reading jobs by cssd id");
											ret_val = false;
										}
									}
	
									else
										if (DS_COL_JOB_REF_ID_IDX == i) {
											// handle int32 field
											char tmp_val[DS_INT32_MAX_STRING_LENGTH + 1];
											sprintf (tmp_val, "%d", bson_iter_int32 (&iter));
											const ulong sz = strlen (tmp_val) + 1;
											(*ids_dataset)->data[j] = malloc (sz);
	
											if ((*ids_dataset)->data[j]) {
												strncpy ((*ids_dataset)->data[j], tmp_val, sz);
												i++;
												j++;
											}
	
											else {
												ret_val = false;
											}
										}
	
										else {
											i++;
										}
								}
	
								if (!ret_val || DS_COLLECTION_JOBS_NFIELDS != i) {
									for (REGISTER ulong k = 0; k < j; k++) {
										free ((*ids_dataset)->data[j]);
									}
	
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (query);
									mongoc_cursor_destroy (cursor);
									break;
								}
							}
	
							if (ret_val) {
								ret_val = !mongoc_cursor_error (cursor, NULL) && (j == jobs_count * 2);
	
								if (ret_val) {
									(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
									(*ids_dataset)->num_records = jobs_count;
									(*ids_dataset)->num_fields_per_record = 2;
								}
	
								else {
									DEBUG_NOW (REPORT_ERRORS, DATASTORE,
									           "cursor error when reading jobs by cssd id");
	
									for (REGISTER ulong k = 0; k < j; k++) {
										free ((*ids_dataset)->data[k]);
									}
	
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
								}
	
								bson_destroy (query);
								mongoc_cursor_destroy (cursor);
							}
						}
					}
				}
			}
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool read_job_ids_by_sequence_id (ds_object_id_field *sequence_id,
                                  dsp_dataset *ids_dataset) {
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "NULL IDs dataset found when reading jobs by sequence id");
		return false;
	}
	
	*ids_dataset = NULL;
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading jobs by sequence id");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, (char *) sequence_id);
		BSON_APPEND_OID (query, DS_COL_JOB_SEQUENCE_ID, &oid);
		ds_int32_field jobs_count = mongoc_collection_count_documents (
		                                        mongo_jobs_collection, query, NULL, NULL, NULL, NULL);
	
		if (jobs_count == -1) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to retrieve collection count when reading jobs by sequence id");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			*ids_dataset = malloc (sizeof (struct ds_dataset));
	
			if (!*ids_dataset) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create IDs dataset when reading jobs by sequence id");
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				(*ids_dataset)->data = malloc (sizeof (char *) * (size_t)jobs_count);
	
				if (! ((*ids_dataset)->data)) {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "failed to create data element for IDs dataset when reading jobs by sequence id");
					free (*ids_dataset);
					*ids_dataset = NULL;
					bson_destroy (query);
					ret_val = false;
				}
	
				else {
					if (!jobs_count) {
						(*ids_dataset)->num_records = 0;
						(*ids_dataset)->num_fields_per_record = 0;
						(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
						bson_destroy (query);
					}
	
					else {
						cursor = mongoc_collection_find_with_opts (mongo_jobs_collection, query, NULL,
						                                        NULL);
	
						if (!cursor) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "failed to create create cursor when reading jobs by sequence id");
							free ((*ids_dataset)->data);
							free (*ids_dataset);
							*ids_dataset = NULL;
							bson_destroy (query);
							ret_val = false;
						}
	
						else {
							bson_iter_t iter;
							REGISTER ulong j = 0;
	
							while (ret_val && mongoc_cursor_next (cursor, &doc) &&
							       bson_iter_init (&iter, doc)) {
								REGISTER ulong i = 0;
	
								while (bson_iter_next (&iter)) {
									if (!i) {
										char oid_string[DS_OBJ_ID_LENGTH + 1];
										const bson_oid_t *tmp_oid = bson_iter_oid (&iter);
										bson_oid_to_string (tmp_oid, oid_string);
										(*ids_dataset)->data[j] = malloc (DS_OBJ_ID_LENGTH + 1);
	
										if ((*ids_dataset)->data[j]) {
											strncpy ((*ids_dataset)->data[j], oid_string, DS_OBJ_ID_LENGTH + 1);
											((char *) (*ids_dataset)->data[j])[DS_OBJ_ID_LENGTH] = '\0';
											i++;
											j++;
										}
	
										else {
											DEBUG_NOW (REPORT_ERRORS, DATASTORE,
											           "could not allocate memory for jobs document when reading jobs by sequence id");
											ret_val = false;
										}
									}
	
									else {
										i++;
										continue;
									}
								}
	
								if (!ret_val || DS_COLLECTION_JOBS_NFIELDS != i) {
									for (REGISTER ulong k = 0; k < j; k++) {
										free ((*ids_dataset)->data[j]);
									}
	
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (query);
									mongoc_cursor_destroy (cursor);
									break;
								}
							}
	
							if (ret_val) {
								ret_val = !mongoc_cursor_error (cursor, NULL) && (j == jobs_count);
	
								if (ret_val) {
									(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
									(*ids_dataset)->num_records = jobs_count;
									(*ids_dataset)->num_fields_per_record = 1;
								}
	
								else {
									DEBUG_NOW (REPORT_ERRORS, DATASTORE,
									           "cursor error when reading jobs by sequence id");
	
									for (REGISTER ulong k = 0; k < j; k++) {
										free ((*ids_dataset)->data[k]);
									}
	
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
								}
	
								bson_destroy (query);
								mongoc_cursor_destroy (cursor);
							}
						}
					}
				}
			}
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

/*
 * read aggregate jobs by reference id
 *
 * uses aggregation to retrieve jobs along with sequence and cssd data
 * (similar to read_jobs_by_ref_id but aggregates over sequences and cssds)
 *
 * note on args:
 *      start is 1-indexed
 *      limit 0 -> all jobs
 */
bool read_aggregate_jobs_by_ref_id (ds_int32_field ref_id, ds_int32_field start,
                                    ds_int32_field limit, ds_int32_field order, dsp_dataset *ids_dataset) {
	// TODO: include start/limit/order into pipeline
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "NULL IDs dataset found when reading aggregate jobs by reference id");
		return false;
	}
	
	if (start < 1) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "invalid start index found when reading aggregate jobs by reference id");
		return false;
	}
	
	// limit==0 -> return all cssds
	if (limit < 0) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "invalid dataset limit found when reading aggregate jobs by reference id");
		return false;
	}
	
	*ids_dataset = NULL;
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	bson_t *pipeline;
	pipeline = BCON_NEW ("pipeline",
	                     "[",
	                     "{", "$match", "{", "ref_id", BCON_INT32 (ref_id), "}", "}",
	                     "{", "$lookup", "{",
	                     "from", BCON_UTF8 ("sequences"),
	                     "let", "{", "s_id", BCON_UTF8 ("$sequence_id"), "}",
	                     "pipeline", "[",
	                     "{", "$match", "{", "$expr", "{", "$eq", "[", BCON_UTF8 ("$_id"),
	                     BCON_UTF8 ("$$s_id"), "]", "}", "}", "}",
	                     "]",
	                     "as", BCON_UTF8 ("seq"),
	                     "}", "}",
	                     "{", "$match", "{", "$expr", "{", "$ne", "[", BCON_UTF8 ("$seq"), "[", "]", "]",
	                     "}", "}", "}",
	                     "{", "$replaceRoot", "{", "newRoot",
	                     "{", "$mergeObjects", "[", "{", "$arrayElemAt", "[", BCON_UTF8 ("$seq"),
	                     BCON_INT32 (0), "]", "}", BCON_UTF8 ("$$ROOT"), "]", "}",
	                     "}", "}",
	                     "{", "$project", "{", "seq", BCON_INT32 (0), "}", "}",
	                     "{", "$lookup", "{",
	                     "from", BCON_UTF8 ("cs"),
	                     "let", "{", "c_id", BCON_UTF8 ("$cssd_id"), "}",
	                     "pipeline", "[",
	                     "{", "$match", "{", "$expr", "{", "$eq", "[", BCON_UTF8 ("$_id"),
	                     BCON_UTF8 ("$$c_id"), "]", "}", "}", "}",
	                     "]",
	                     "as", BCON_UTF8 ("cssd"),
	                     "}", "}",
	                     "{", "$match", "{", "$expr", "{", "$ne", "[", BCON_UTF8 ("$cssd"), "[", "]",
	                     "]", "}", "}", "}",
	                     "{", "$replaceRoot", "{", "newRoot",
	                     "{", "$mergeObjects", "[", "{", "$arrayElemAt", "[", BCON_UTF8 ("$cssd"),
	                     BCON_INT32 (0), "]", "}", BCON_UTF8 ("$$ROOT"), "]", "}",
	                     "}", "}",
	                     "{", "$project", "{", "cssd", BCON_INT32 (0), "}", "}", "]");
	
	/*
	 * the above pipeline returns the following aggregate (with corresponding indices)
	 *
	 * 	0	"_id"
	 *	1	"cs"
	 *	2	"name"
	 *	3	"ref_id"
	 *	4	"published"
	 *	5	"accession"
	 *	6	"definition"
	 *	7	"3'UTR"
	 *	8	"group"
	 *	9	"sequence_id"
	 *	10	"cssd_id"
	 *	11	"status"
	 *	12	"error"
	 *	13	"num_windows"
	 *	14	"num_windows_success"
	 *	15	"num_windows_fail"
	 *
	 *	note: published (index 4) is ignored from the returned dataset below
	 */
	
	if (!pipeline) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create pipeline when reading aggregate jobs by reference id");
		ret_val = false;
	}
	
	else {
		cursor = mongoc_collection_aggregate (mongo_jobs_collection, MONGOC_QUERY_NONE,
		                                      pipeline, NULL, NULL);
	
		if (!cursor) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create cursor when reading aggregate jobs by reference id");
			ret_val = false;
		}
	
		else {
			bson_error_t error;
	
			if (mongoc_cursor_error (cursor, &error)) {
				DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
				            "cursor error '%s' when reading aggregate jobs by reference id", error.message);
				ret_val = false;
			}
	
			else {
				const bson_t *doc = NULL;
				list_t this_list;
				list_init (&this_list);
				REGISTER ulong i = 0;
	
				while (mongoc_cursor_next (cursor, &doc)) {
					bson_iter_t iter;
	
					if (bson_iter_init (&iter, doc)) {
						// TODO: memory allocation checks, document validation checks
						i = 0;
	
						while (bson_iter_next (&iter)) {
							switch (i) {
								case 0:  // the object ids
								case 9:
								case 10: {
										const bson_oid_t *oid = bson_iter_oid (&iter);
										char *oid_str = (char *) malloc (sizeof (char) * DS_OBJ_ID_LENGTH + 1);
										bson_oid_to_string (oid, oid_str);
										list_append (&this_list, oid_str);
										i++;
										break;
									}
	
								case 1:  // strings
								case 2:
								case 5:
								case 6:
								case 7:
								case 8: {
										const char *this_str = bson_iter_utf8 (&iter, NULL);
										char *generic_str = (char *) malloc (sizeof (char) * strlen (this_str) + 1);
										strcpy (generic_str, this_str);
										list_append (&this_list, generic_str);
										i++;
										break;
									}
	
								case 3: { // int (ref_id)
										const int32_t this_ref_id = bson_iter_int32 (&iter);
										char *int_str = (char *) malloc (sizeof (char) * DS_INT32_MAX_STRING_LENGTH +
										                                 1);
										sprintf (int_str, "%"PRId32, this_ref_id);
										list_append (&this_list, int_str);
										i++;
										break;
									}
	
								case 11: // int (status fields)
								case 12:
								case 13:
								case 14:
								case 15: {
										const int32_t this_status = bson_iter_int32 (&iter);
										char *int_str = (char *) malloc (sizeof (char) * DS_INT32_MAX_STRING_LENGTH +
										                                 1);
										sprintf (int_str, "%"PRId32, this_status);
										list_append (&this_list, int_str);
										i++;
										break;
									}
	
								case 4: {
										i++; // skip published
										break;
									}
	
								default:
									break;
							}
						}
					}
	
					else {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "cannot iterate on document when reading aggregate jobs by reference id");
						ret_val = false;
						break;
					}
				}
	
				*ids_dataset = malloc (sizeof (struct ds_dataset));
	
				if (!*ids_dataset) {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "failed to create IDs dataset when reading aggregate jobs by reference id");
					ret_val = false;
				}
	
				else {
					size_t list_sz = list_size (&this_list);
					(*ids_dataset)->data = malloc (sizeof (char *) * list_sz);
	
					if (! ((*ids_dataset)->data)) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to create data element for IDs dataset when reading aggregate jobs by reference id");
						ret_val = false;
					}
	
					else {
						if (!i) {
							(*ids_dataset)->num_records = 0;
							(*ids_dataset)->num_fields_per_record = 0;
							(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
						}
	
						else {
							if (list_iterator_start (&this_list)) {
								// need space for all fields bar published status
								(*ids_dataset)->num_records = list_sz / (i - 1);
								(*ids_dataset)->num_fields_per_record = (i - 1);
								(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
								i = 0;
	
								while (list_iterator_hasnext (&this_list)) {
									(*ids_dataset)->data[i++] = list_iterator_next (&this_list);
								}
	
								list_iterator_stop (&this_list);
							}
	
							else {
								DEBUG_NOW (REPORT_ERRORS, DATASTORE,
								           "failed to iterate over list of data elements when reading aggregate jobs by reference id");
								ret_val = false;
							}
						}
					}
				}
	
				list_destroy (&this_list);
			}
		}
	
		mongoc_cursor_destroy (cursor);
		bson_destroy (pipeline);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool read_job_status (ds_object_id_field *object_id, ds_int32_field ref_id,
                      ds_int32_field *status) {
	bool ret_val = true;
	*status = DS_JOB_STATUS_UNDEFINED;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading job status");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		BSON_APPEND_INT32 (query, DS_COL_JOB_REF_ID, ref_id);
		opts = BCON_NEW ("sort", "{", MONGODB_OBJECT_ID_FIELD, BCON_INT32 (-1), "}");
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when reading job status");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			cursor = mongoc_collection_find_with_opts (mongo_jobs_collection, query, opts,
			                                        NULL);
	
			if (!cursor) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create cursor when reading job status");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				bson_iter_t iter;
				g_memset (&iter, 0, sizeof (bson_iter_t));
	
				if (mongoc_cursor_next (cursor, &doc) && bson_iter_init (&iter, doc)) {
					bson_iter_next (&iter);             // skip _id
					REGISTER ulong i = 0;
	
					while (bson_iter_next (&iter)) {
						if (i == DS_COL_JOB_STATUS_IDX - 1) {
							*status = bson_iter_int32 (&iter);
							break;
						}
	
						i++;
					}
				}
	
				if (ret_val) {
					ret_val = !mongoc_cursor_error (cursor, NULL);
					bson_destroy (opts);
					bson_destroy (query);
					mongoc_cursor_destroy (cursor);
				}
			}
		}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
}

DS_LOCK_E

return ret_val;
}

bool update_job_status (ds_object_id_field *object_id, ds_int32_field ref_id,
                        ds_int32_field status) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when updating job status");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		BSON_APPEND_INT32 (query, DS_COL_JOB_REF_ID, ref_id);
		bson_t *job_doc_update = BCON_NEW ("$set", "{", DS_COL_JOB_STATUS,
		                                   BCON_INT32 (status), "}");
	
		if (!job_doc_update) {
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			if (!mongoc_collection_update_one (mongo_jobs_collection, query, job_doc_update,
			                                   NULL, NULL, NULL)) {
				ret_val = false;
			}
	
			bson_destroy (query);
			bson_destroy (job_doc_update);
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E

	return ret_val;
}

bool update_job_error (ds_object_id_field *object_id, ds_int32_field ref_id,
                       ds_int32_field error) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when updating job error field");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		BSON_APPEND_INT32 (query, DS_COL_JOB_REF_ID, ref_id);
		bson_t *job_doc_update = BCON_NEW ("$set", "{", DS_COL_JOB_ERROR,
		                                   BCON_INT32 (error), "}");
	
		if (!job_doc_update) {
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			if (!mongoc_collection_update_one (mongo_jobs_collection, query, job_doc_update,
			                                   NULL, NULL, NULL)) {
				ret_val = false;
			}
	
			bson_destroy (query);
			bson_destroy (job_doc_update);
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool update_job_num_windows (ds_object_id_field *object_id,
                             ds_int32_field ref_id, ds_int32_field num_windows) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when updating job number of windows");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		BSON_APPEND_INT32 (query, DS_COL_JOB_REF_ID, ref_id);
		bson_t *job_doc_update = BCON_NEW ("$set", "{", DS_COL_JOB_NUM_WINDOWS,
		                                   BCON_INT32 (num_windows), "}");
	
		if (!job_doc_update) {
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			if (!mongoc_collection_update_one (mongo_jobs_collection, query, job_doc_update,
			                                   NULL, NULL, NULL)) {
				ret_val = false;
			}
	
			bson_destroy (query);
			bson_destroy (job_doc_update);
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool update_job_num_windows_success (ds_object_id_field *object_id,
                                     ds_int32_field ref_id, ds_int32_field num_windows_success) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when updating job number of successful windows");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		BSON_APPEND_INT32 (query, DS_COL_JOB_REF_ID, ref_id);
		bson_t *job_doc_update = BCON_NEW ("$set", "{", DS_COL_JOB_NUM_WINDOWS_SUCCESS,
		                                   BCON_INT32 (num_windows_success), "}");
	
		if (!job_doc_update) {
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			if (!mongoc_collection_update_one (mongo_jobs_collection, query, job_doc_update,
			                                   NULL, NULL, NULL)) {
				ret_val = false;
			}
	
			bson_destroy (query);
			bson_destroy (job_doc_update);
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool update_job_num_windows_fail (ds_object_id_field *object_id,
                                  ds_int32_field ref_id, ds_int32_field num_windows_fail) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when updating job number of failed windows");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		BSON_APPEND_INT32 (query, DS_COL_JOB_REF_ID, ref_id);
		bson_t *job_doc_update = BCON_NEW ("$set", "{", DS_COL_JOB_NUM_WINDOWS_FAIL,
		                                   BCON_INT32 (num_windows_fail), "}");
	
		if (!job_doc_update) {
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			if (!mongoc_collection_update_one (mongo_jobs_collection, query, job_doc_update,
			                                   NULL, NULL, NULL)) {
				ret_val = false;
			}
	
			bson_destroy (query);
			bson_destroy (job_doc_update);
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool delete_job (ds_object_id_field *object_id, ds_int32_field ref_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when deleting job");
		ret_val = false;
	}
	
	else {
		bson_t reply;
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *object_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
		BSON_APPEND_INT32 (query, DS_COL_JOB_REF_ID, ref_id);
	
		if (!mongoc_collection_delete_one (mongo_jobs_collection, query, NULL, &reply,
		                                   NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete document when deleting job");
			ret_val = false;
		}
	
		else {
			char *reply_json = bson_as_canonical_extended_json (&reply, NULL);
			jsmn_init (&jsmn_p);
			uchar num_tokens_parsed = jsmn_parse (&jsmn_p, reply_json, strlen (reply_json),
			                                      jsmn_t, MAX_JSON_PARSE_TOKENS);
			/*
			 * expect JSON string having format { "deletedCount" : { "$numberInt" : "1" } }
			 */
			bool success = false;
	
			if (5 == num_tokens_parsed) {
				if (jsmn_t[0].type == JSMN_OBJECT && jsmn_t[1].type == JSMN_STRING) {
					char *strn = reply_json + jsmn_t[1].start;
	
					if (!strncmp (strn, "deletedCount", jsmn_t[1].end - jsmn_t[1].start) &&
					    jsmn_t[2].type == JSMN_OBJECT && jsmn_t[3].type == JSMN_STRING) {
						strn = reply_json + jsmn_t[3].start;
	
						if (!strncmp (strn, "$numberInt", jsmn_t[3].end - jsmn_t[3].start) &&
						    jsmn_t[4].type == JSMN_STRING) {
							strn = reply_json + jsmn_t[4].start;
	
							if (!strncmp (strn, "1", jsmn_t[4].end - jsmn_t[4].start)) {
								success = true;
							}
						}
					}
				}
			}
	
			if (!success) {
				// do not log any messages - just signal that deletion not successful (presumed not found)
				ret_val = false;
			}
	
			else
				if (do_notify) {
					// for delete events, ref_id will not be available to watch -> notify directly here
					ds_notification_event *this_event = (ds_notification_event *)malloc (sizeof (
					                                        ds_notification_event));
	
					if (this_event) {
						unsigned long long now = get_real_time();
						this_event->time = now;
						this_event->ref_id = ref_id;
						this_event->collection = DS_COL_JOBS;
						this_event->op_type = DS_NOTIFY_OP_DELETE;
						strncpy (this_event->oid_string, (char *)object_id, DS_OBJ_ID_LENGTH);
	
						if (!enq_ds (this_event)) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "failed to enqueue notification event after deleting job");
						}
					}
	
					else {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "cannot allocate memory for enqueue notification event after deleting job");
					}
				}
		}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool delete_all_jobs_by_job_id (ds_object_id_field *job_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when deleting all jobs by job id");
		ret_val = false;
	}
	
	else {
		bson_t reply;
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *job_id);
		BSON_APPEND_OID (query, MONGODB_OBJECT_ID_FIELD, &oid);
	
		if (!mongoc_collection_delete_one (mongo_jobs_collection, query, NULL, &reply,
		                                   NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete document when deleting all jobs by job id");
			ret_val = false;
		}
	
		else {
			char *reply_json = bson_as_canonical_extended_json (&reply, NULL);
			jsmn_init (&jsmn_p);
			uchar num_tokens_parsed = jsmn_parse (&jsmn_p, reply_json, strlen (reply_json),
			                                      jsmn_t, MAX_JSON_PARSE_TOKENS);
			/*
			 * expect JSON string having format { "deletedCount" : { "$numberInt" : "1" } }
			 */
			bool success = false;
	
			if (5 == num_tokens_parsed) {
				if (jsmn_t[0].type == JSMN_OBJECT && jsmn_t[1].type == JSMN_STRING) {
					char *strn = reply_json + jsmn_t[1].start;
	
					if (!strncmp (strn, "deletedCount", jsmn_t[1].end - jsmn_t[1].start) &&
					    jsmn_t[2].type == JSMN_OBJECT && jsmn_t[3].type == JSMN_STRING) {
						strn = reply_json + jsmn_t[3].start;
	
						if (!strncmp (strn, "$numberInt", jsmn_t[3].end - jsmn_t[3].start) &&
						    jsmn_t[4].type == JSMN_STRING) {
							strn = reply_json + jsmn_t[4].start;
	
							if (!strncmp (strn, "1", jsmn_t[4].end - jsmn_t[4].start)) {
								success = true;
							}
						}
					}
				}
			}
	
			if (!success) {
				// do not log any messages - just signal that deletion not successful (presumed not found)
				ret_val = false;
			}
		}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool delete_all_jobs (ds_int32_field ref_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when deleting all jobs by reference id");
		ret_val = false;
	}
	
	else {
		BSON_APPEND_INT32 (query, DS_COL_JOB_REF_ID, ref_id);
	
		/*
		 * delete all jobs; do not signal failure if no jobs are deleted
		 */
		if (!mongoc_collection_delete_many (mongo_jobs_collection, query, NULL, NULL,
		                                    NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete document when deleting all jobs by reference id");
			ret_val = false;
		}
	
		else
			if (do_notify) {
				// for delete events, ref_id will not be available to watch -> notify directly here
				ds_notification_event *this_event = (ds_notification_event *)malloc (sizeof (
				                                        ds_notification_event));
	
				if (this_event) {
					unsigned long long now = get_real_time();
					this_event->time = now;
					this_event->ref_id = ref_id;
					this_event->collection = DS_COL_JOBS;
					this_event->op_type = DS_NOTIFY_OP_DELETE;
					this_event->oid_string[0] =
					                    0;			// null-terminate oid string to notify on all jobs
	
					if (!enq_ds (this_event)) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to enqueue notification event when deleting all jobs by reference id");
					}
				}
	
				else {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "failed to allocate memory for enqueue notification event when deleting all jobs by reference id");
				}
			}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

/*
 * users collection methods
 */
bool create_user (ds_generic_field user_name, ds_generic_field sub_id,
                  ds_int32_field *ref_id, ds_object_id_field *new_object_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *user_doc = bson_new();
	
	if (!user_doc) {
		ret_val = false;
	}
	
	else {
		get_real_time_bytes (new_object_id);
		bson_oid_t new_oid;
		bson_oid_init_from_data (&new_oid, (unsigned char *) new_object_id);
		BSON_APPEND_OID (user_doc, MONGODB_OBJECT_ID_FIELD, &new_oid);
		BSON_APPEND_UTF8 (user_doc, DS_COL_USER_NAME, user_name);
		BSON_APPEND_UTF8 (user_doc, DS_COL_USER_SUB_ID, sub_id);
	
		if (1 > *ref_id) {
			if (!get_next_sequence (DS_COL_USER_REF_ID, ref_id)) {
				ret_val = false;
			}
		}
	
		if (ret_val) {
			BSON_APPEND_INT32 (user_doc, DS_COL_USER_REF_ID, *ref_id);
	
			if (!mongoc_collection_insert_one (mongo_users_collection, user_doc, NULL, NULL,
			                                   NULL)) {
				bson_destroy (user_doc);
				ret_val = false;
			}
		}
	
		bson_destroy (user_doc);
	}
	
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool read_user (ds_generic_field sub_id, dsp_dataset *ids_dataset) {
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "NULL IDs dataset found when reading user");
		return false;
	}
	
	*ids_dataset = NULL;
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, *opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading user");
		ret_val = false;
	}
	
	else {
		BSON_APPEND_UTF8 (query, DS_COL_USER_SUB_ID, sub_id);
		opts = BCON_NEW ("sort", "{", MONGODB_OBJECT_ID_FIELD, BCON_INT32 (-1), "}");
	
		if (!opts) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create opts when reading user");
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			*ids_dataset = malloc (sizeof (struct ds_dataset));
	
			if (!*ids_dataset) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create IDs dataset when reading user");
				bson_destroy (opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else {
				(*ids_dataset)->data = malloc (sizeof (char *) * (DS_COLLECTION_USERS_NFIELDS -
				                                        1));      // 1 record x sequence_id,cssd_id,status
	
				if (! ((*ids_dataset)->data)) {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "failed to allocate IDs dataset when reading user");
					free (*ids_dataset);
					*ids_dataset = NULL;
					bson_destroy (opts);
					bson_destroy (query);
					ret_val = false;
				}
	
				else {
					cursor = mongoc_collection_find_with_opts (mongo_users_collection, query, opts,
					                                        NULL);
	
					if (!cursor) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to create cursor when reading user");
						free ((*ids_dataset)->data);
						free (*ids_dataset);
						*ids_dataset = NULL;
						bson_destroy (opts);
						bson_destroy (query);
						ret_val = false;
					}
	
					else {
						bson_iter_t iter;
	
						if (mongoc_cursor_next (cursor, &doc) && bson_iter_init (&iter, doc)) {
							bson_iter_next (&iter);             // skip _id
							bson_iter_next (&iter);             // skip name
							bson_iter_next (&iter);             // skip sub_id
							REGISTER ulong i = 0;
	
							while (bson_iter_next (&iter)) {
								ds_generic_field tmp_val;
								sprintf (tmp_val, "%d", bson_iter_int32 (&iter));  // status
								const ulong sz = bson_strnlen (tmp_val, DS_GENERIC_FIELD_LENGTH) + 1;
								(*ids_dataset)->data[i] = malloc (sz);
	
								if (! (*ids_dataset)->data[i]) {
									for (REGISTER ulong j = 0; j < i; j++) {
										free ((*ids_dataset)->data[j]);
									}
	
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (opts);
									bson_destroy (query);
									mongoc_cursor_destroy (cursor);
									ret_val = false;
									break;
								}
	
								else {
									strncpy ((*ids_dataset)->data[i], tmp_val, sz);
									i++;
								}
							}
	
							if (i != DS_COLLECTION_USERS_NFIELDS - 3) {
								DEBUG_NOW (REPORT_WARNINGS, DATASTORE,
								           "unexpected number of fields found when reading user");
	
								for (REGISTER ulong j = 0; j < i; j++) {
									free ((*ids_dataset)->data[j]);
								}
	
								free ((*ids_dataset)->data);
								free (*ids_dataset);
								*ids_dataset = NULL;
								bson_destroy (opts);
								bson_destroy (query);
								mongoc_cursor_destroy (cursor);
								ret_val = false;
							}
	
							else {
								(*ids_dataset)->num_records = 1;
								(*ids_dataset)->num_fields_per_record = DS_COLLECTION_USERS_NFIELDS - 3;
								(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
							}
						}
	
						else {
							(*ids_dataset)->num_records = 0;
							(*ids_dataset)->num_fields_per_record = 0;
							(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
						}
	
						if (ret_val) {
							ret_val = !mongoc_cursor_error (cursor, NULL);
							bson_destroy (opts);
							bson_destroy (query);
							mongoc_cursor_destroy (cursor);
						}
					}
				}
			}
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool delete_user (ds_int32_field ref_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when deleting user");
		ret_val = false;
	}
	
	else {
		bson_t reply;
		BSON_APPEND_INT32 (query, DS_COL_USER_REF_ID, ref_id);
	
		if (!mongoc_collection_delete_one (mongo_users_collection, query, NULL, &reply,
		                                   NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete document when deleting user");
			ret_val = false;
		}
	
		else {
			char *reply_json = bson_as_canonical_extended_json (&reply, NULL);
			jsmn_init (&jsmn_p);
			uchar num_tokens_parsed = jsmn_parse (&jsmn_p, reply_json, strlen (reply_json),
			                                      jsmn_t, MAX_JSON_PARSE_TOKENS);
			/*
			 * expect JSON string having format { "deletedCount" : { "$numberInt" : "1" } }
			 */
			bool success = false;
	
			if (5 == num_tokens_parsed) {
				if (jsmn_t[0].type == JSMN_OBJECT && jsmn_t[1].type == JSMN_STRING) {
					char *strn = reply_json + jsmn_t[1].start;
	
					if (!strncmp (strn, "deletedCount", jsmn_t[1].end - jsmn_t[1].start) &&
					    jsmn_t[2].type == JSMN_OBJECT && jsmn_t[3].type == JSMN_STRING) {
						strn = reply_json + jsmn_t[3].start;
	
						if (!strncmp (strn, "$numberInt", jsmn_t[3].end - jsmn_t[3].start) &&
						    jsmn_t[4].type == JSMN_STRING) {
							strn = reply_json + jsmn_t[4].start;
	
							if (!strncmp (strn, "1", jsmn_t[4].end - jsmn_t[4].start)) {
								success = true;
							}
						}
					}
				}
			}
	
			if (!success) {
				// do not log any messages - just signal that deletion not successful (presumed not found)
				ret_val = false;
			}
		}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

/*
 * results collection methods
 */
bool create_result (ds_object_id_field *job_id, ds_result_hit_field *hit,
                    ds_int32_field ref_id, ds_object_id_field *new_object_id) {
	bool ret_val = true;
	DS_LOCK_S
	// split hit into time,position,fe,hit(string) fields
	ds_double_field time = 0.0f, fe = 0.0f;
	ds_int32_field position = 0;
	ds_result_hit_field hit_string;
	ulong tabs[3];
	ulong tab_idx = 0;

	for (ulong i = 0; i < strlen ((char *)hit); i++) {
		if (S_HIT_SEPARATOR == ((char *)hit)[i]) {
			tabs[tab_idx++] = i;
			
			if (tab_idx == 3) {
				break;
			}
		}
	}

	if (tab_idx != 3) {
		ret_val = false;
	}
	
	else {
		char tmp[20]; // enough to cater for all above field types
		g_memcpy (tmp, ((char *)hit), tabs[0]);
		tmp[tabs[0]] = '\0';
		time = atof (tmp);
		g_memcpy (tmp, ((char *)hit) + tabs[0] + 1, tabs[1] - tabs[0] - 1);
		tmp[tabs[1] - tabs[0] - 1] = '\0';
		position = atoi (tmp);
		g_memcpy (tmp, ((char *)hit) + tabs[1] + 1, tabs[2] - tabs[1] - 1);
		tmp[tabs[2] - tabs[1] - 1] = '\0';
		fe = strtod (tmp, NULL);
		g_memcpy (hit_string, ((char *)hit) + tabs[2] + 1,
		          strlen ((char *)hit) - tabs[2]);
		hit_string[strlen ((char *)hit) - tabs[2]] = '\0';
	}
	
	if (ret_val) {
		#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
		#elif DATASTORE_TYPE==1         // MongoDB
		bson_t *job_doc = bson_new();
	
		if (!job_doc) {
			ret_val = false;
		}
	
		else {
			get_real_time_bytes (new_object_id);
			bson_oid_t new_oid;
			bson_oid_init_from_data (&new_oid, (unsigned char *) new_object_id);
			BSON_APPEND_OID (job_doc, MONGODB_OBJECT_ID_FIELD, &new_oid);
			bson_oid_t oid;
			bson_oid_init_from_string (&oid, (char *) job_id);
			BSON_APPEND_OID (job_doc, DS_COL_RESULTS_JOB_ID, &oid);
			BSON_APPEND_DOUBLE (job_doc, DS_COL_RESULTS_HIT_TIME, time);
			BSON_APPEND_INT32 (job_doc, DS_COL_RESULTS_HIT_POSITION, position);
			BSON_APPEND_DOUBLE (job_doc, DS_COL_RESULTS_HIT_FE, fe);
			BSON_APPEND_UTF8 (job_doc, DS_COL_RESULTS_HIT_STRING, hit_string);
			BSON_APPEND_INT32 (job_doc, DS_COL_JOB_REF_ID, ref_id);
	
			if (!mongoc_collection_insert_one (mongo_results_collection, job_doc, NULL,
			                                   NULL, NULL)) {
				// only report as error (and return false) if not unique index violated;
				// redundant hits across multiple windows can be expected
				ret_val = false;
			}
	
			bson_destroy (job_doc);
		}
	
		#else
		#endif
	}
	
	DS_LOCK_E
	return ret_val;
}

bool read_results_by_job_id (ds_object_id_field *job_id,
                             ds_int32_field start, ds_int32_field limit, const char *order_by_field1,
                             const char *order_by_field2,
                             ds_int32_field order, dsp_dataset *ids_dataset) {
	if (!ids_dataset) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "NULL IDs dataset found when reading results by job id");
		return false;
	}
	
	if (start < 1) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "invalid start index found when reading results by job id");
		return false;
	}
	
	// limit==0 -> return all sequences
	if (limit < 0) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "invalid dataset limit found when reading results by job id");
		return false;
	}
	
	bool ret_val = true;
	DS_LOCK_S
	*ids_dataset = NULL;
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	const bson_t *doc = NULL;
	bson_t *query, opts;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading results by job id");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *job_id);
		BSON_APPEND_OID (query, DS_COL_RESULTS_JOB_ID, &oid);
		bson_init (&opts);
	
		if (!limit) {
			BCON_APPEND (&opts, "skip", BCON_INT32 (start - 1));
		}
	
		else {
			BCON_APPEND (&opts, "skip", BCON_INT32 (start - 1), "limit",
			             BCON_INT32 (limit));
		}
	
		bson_error_t err;
		int32_t hits_count = (size_t) mongoc_collection_count_documents (
		                                        mongo_results_collection, query, &opts, NULL, NULL, &err);
	
		if (hits_count == -1) {
			DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
			            "failed to get collection count ('%s') when reading results by job id",
			            err.message);
			bson_destroy (&opts);
			bson_destroy (query);
			ret_val = false;
		}
	
		else {
			BCON_APPEND (&opts, "sort", "{", order_by_field1, BCON_INT32 (order),
			             order_by_field2, BCON_INT32 (order), "}");
			*ids_dataset = malloc (sizeof (struct ds_dataset));
	
			if (!*ids_dataset) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to create IDs dataset when reading results by job id");
				bson_destroy (&opts);
				bson_destroy (query);
				ret_val = false;
			}
	
			else
				if (!hits_count) {
					(*ids_dataset)->data = NULL;
					(*ids_dataset)->num_records = 0;
					(*ids_dataset)->num_fields_per_record = 0;
					(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
					bson_destroy (&opts);
					bson_destroy (query);
				}
	
				else {
					(*ids_dataset)->data = malloc (sizeof (char *) * (size_t) (hits_count *
					                                        (DS_COLLECTION_RESULTS_NFIELDS - 1)));
	
					if (! ((*ids_dataset)->data)) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to create data element for IDs dataset when reading results by job id");
						free (*ids_dataset);
						*ids_dataset = NULL;
						bson_destroy (&opts);
						bson_destroy (query);
						ret_val = false;
					}
	
					else {
						cursor = mongoc_collection_find_with_opts (mongo_results_collection, query,
						                                        &opts, NULL);
	
						if (!cursor) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "failed to create cursor when reading results by job id");
							free ((*ids_dataset)->data);
							free (*ids_dataset);
							*ids_dataset = NULL;
							bson_destroy (&opts);
							bson_destroy (query);
							ret_val = false;
						}
	
						else {
							bson_iter_t iter;
							REGISTER ulong i = 0, j;
	
							while (mongoc_cursor_next (cursor, &doc) && bson_iter_init (&iter, doc)) {
								j = 0;
								bson_iter_next (&iter);             // search is by job_id, so include _id
								const bson_oid_t *hit_oid = bson_iter_oid (&iter);
								(*ids_dataset)->data[i] = malloc (sizeof (char) * (DS_OBJ_ID_LENGTH + 1));
	
								if ((*ids_dataset)->data[i]) {
									bson_oid_to_string (hit_oid, (*ids_dataset)->data[i++]);
									bson_iter_next (&iter);             // skip job_id
	
									while (bson_iter_next (&iter)) {
										if (DS_COL_RESULTS_REF_ID_IDX - 2 == j ||
										    // object id and job id not adding to j => -2
										    DS_COL_RESULTS_HIT_POSITION_IDX - 2 == j) {
											// handle int32 field
											char tmp_val[DS_INT32_MAX_STRING_LENGTH + 1];
											sprintf (tmp_val, "%"PRId32, bson_iter_int32 (&iter));
											const ulong sz = strlen (tmp_val) + 1;
											(*ids_dataset)->data[i] = malloc (sz);
	
											if ((*ids_dataset)->data[i]) {
												strncpy ((*ids_dataset)->data[i], tmp_val, sz);
												i++;
												j++;
											}
	
											else {
												ret_val = false;
											}
										}
	
										else
											if (DS_COL_RESULTS_HIT_TIME_IDX - 2 == j ||
											    // object id and job id not adding to j => -2
											    DS_COL_RESULTS_HIT_FE_IDX - 2 == j) {
												// handle double field
												char tmp_val[DS_DOUBLE_MAX_STRING_LENGTH + 1];
												sprintf (tmp_val, "%f", bson_iter_double (&iter));
												const ulong sz = strlen (tmp_val) + 1;
												(*ids_dataset)->data[i] = malloc (sz);
	
												if ((*ids_dataset)->data[i]) {
													strncpy ((*ids_dataset)->data[i], tmp_val, sz);
													i++;
													j++;
												}
	
												else {
													ret_val = false;
												}
											}
	
											else {
												const char *tmp_val = bson_iter_utf8 (&iter, NULL);
												const ulong sz = bson_strnlen (tmp_val, DS_JOB_RESULT_HIT_FIELD_LENGTH) + 1;
												(*ids_dataset)->data[i] = malloc (sz);
	
												if ((*ids_dataset)->data[i]) {
													strncpy ((*ids_dataset)->data[i], tmp_val, sz);
													i++;
													j++;
												}
	
												else {
													ret_val = false;
												}
											}
	
										if (!ret_val) {
											for (j = 0; j < i; j++) {
												free ((*ids_dataset)->data[j]);
											}
	
											free ((*ids_dataset)->data);
											free (*ids_dataset);
											*ids_dataset = NULL;
											bson_destroy (&opts);
											bson_destroy (query);
											mongoc_cursor_destroy (cursor);
											break;
										}
									}
								}
	
								else {
									for (j = 0; j < i; j++) {
										free ((*ids_dataset)->data[j]);
									}
	
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (&opts);
									bson_destroy (query);
									mongoc_cursor_destroy (cursor);
									ret_val = false;
									break;
								}
							}
	
							if (ret_val) {
								if (i != hits_count * (DS_COLLECTION_RESULTS_NFIELDS - 1)) {
									DEBUG_NOW (REPORT_WARNINGS, DATASTORE,
									           "incorrect number of fields found when reading results by job id");
	
									for (j = 0; j < i; j++) {
										free ((*ids_dataset)->data[j]);
									}
	
									free ((*ids_dataset)->data);
									free (*ids_dataset);
									*ids_dataset = NULL;
									bson_destroy (&opts);
									bson_destroy (query);
									mongoc_cursor_destroy (cursor);
									ret_val = false;
								}
	
								else {
									(*ids_dataset)->num_records = (size_t) hits_count;
									(*ids_dataset)->num_fields_per_record = (DS_COLLECTION_RESULTS_NFIELDS - 1);
									(*ids_dataset)->record_type = DS_REC_TYPE_STRING;
									bson_destroy (&opts);
									bson_destroy (query);
									mongoc_cursor_destroy (cursor);
								}
							}
						}
					}
				}
		}
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool get_result_index (ds_object_id_field *job_id,
		       const char *order_by_field1,
                       const char *order_by_field2,
                       ds_int32_field order,
		       ds_int32_field position,
		       ds_double_field fe, 
		       ulong *index) {
	if (!index) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "NULL index found when getting result index");
		return false;
	}
	
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_oid_t oid;
        bson_oid_init_from_string (&oid, (char *) job_id);
	bson_t *query, opts;

	// hit FE values are stored rounded to 2 decimal places, so its useful here
	// to also keep the same precision for more accurate hit alignment;
	// moreover, subtract 0.001 to ensure that no close FE hits are missed out
	fe = ((float) ((int)((fe * 100) + 0.5 - (fe < 0.0f))) / 100) - 0.001;

	// first count all hits prior to the target value for the primary field we are ordering by
	if (DS_COL_RESULTS_HIT_POSITION==order_by_field1)
	{
		query = BCON_NEW (DS_COL_RESULTS_HIT_POSITION, "{", "$lt", BCON_INT32 (position), "}");
	} else {
        	query = BCON_NEW (DS_COL_RESULTS_HIT_FE, "{", "$lt", BCON_DOUBLE (fe), "}");
	}

	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when getting result index");
		ret_val = false;
	}
	
	else {
        	BSON_APPEND_OID (query, DS_COL_RESULTS_JOB_ID, &oid);

		bson_init (&opts);
		BCON_APPEND (&opts, "sort", "{", order_by_field1, BCON_INT32 (order), order_by_field2, BCON_INT32 (order), "}");

        	const bson_t *doc = NULL;
		mongoc_cursor_t *cursor = mongoc_collection_find_with_opts (mongo_results_collection, query, &opts, NULL);
	
		if (NULL==cursor) {	// TODO: improve cursor error handling
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create cursor when getting result index");
			ret_val = false;
		}
		else {
			*index = 0; 
			while (mongoc_cursor_next (cursor, &doc)) {
				(*index)++;
			}
			mongoc_cursor_destroy (cursor);

			bson_destroy (query);

			// then, count any hits prior to the target value for the secondary field we are ordering by
			if (DS_COL_RESULTS_HIT_POSITION==order_by_field1)
			{
				query = BCON_NEW (DS_COL_RESULTS_HIT_POSITION, BCON_INT32 (position));
			} else {
				// note that we need $gte for FE, since the target FE might not be an exact match
				query = BCON_NEW (DS_COL_RESULTS_HIT_FE, "{", "$gte", BCON_DOUBLE (fe), "}");
			}
			BSON_APPEND_OID (query, DS_COL_RESULTS_JOB_ID, &oid);

			// TODO: error handling for query
			mongoc_cursor_t *cursor = mongoc_collection_find_with_opts (mongo_results_collection, query, &opts, NULL);

			if (NULL==cursor) {     // TODO: improve cursor error handling
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					   "failed to create cursor when getting result index");
				ret_val = false;
			}
			else {
				bson_iter_t iter;
				while (mongoc_cursor_next (cursor, &doc)) {
					bson_iter_init (&iter, doc); 	// TODO: error handling
					bson_iter_next (&iter);
					bson_iter_next (&iter);
					bson_iter_next (&iter);
					bson_iter_next (&iter);
					if (DS_COL_RESULTS_HIT_POSITION==order_by_field1) {
						bson_iter_next (&iter);
						double d=bson_iter_double (&iter);
						if (d>fe || ((fe-d)<0.01f)) {  // diff must be < 2 decimal places (or larger value)
							break;
						}
					} else {
						if (bson_iter_int32 (&iter)==position) {
							break;
						}
					}
					(*index)++;
				}
				mongoc_cursor_destroy (cursor);
			}
		}

		bson_destroy (&opts);
		bson_destroy (query);
	}

	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool read_result_count_by_job_id (ds_object_id_field *job_id,
                                  nt_hit_count *count) {
	bool ret_val = true;
	*count = 0;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query;
	query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading result count by job id");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *job_id);
		BSON_APPEND_OID (query, DS_COL_RESULTS_JOB_ID, &oid);
		bson_error_t err;
		int32_t hits_count = (size_t) mongoc_collection_count_documents (
		                                        mongo_results_collection, query, NULL, NULL, NULL, &err);
	
		if (hits_count == -1) {
			DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
			            "failed to get collection count ('%s') when reading result count by job id",
			            err.message);
			ret_val = false;
		}
	
		else {
			*count = hits_count;
		}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool read_result_total_time_by_job_id (ds_object_id_field *job_id,
                                       float *total_time) {
	bool ret_val = true;
	*total_time = 0.0f;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	bson_t *pipeline;
	bson_oid_t oid;
	bson_oid_init_from_string (&oid, (char *)job_id);
	pipeline = BCON_NEW ("pipeline",
	                     "[",
	                     "{", "$match", "{", DS_COL_RESULTS_JOB_ID, BCON_OID (&oid), "}", "}",
	                     "{", "$group", "{",
	                     "_id", "null",
	                     "totalTime", "{", "$sum", BCON_UTF8 ("$time"), "}", "}", "}",
	                     "{", "$project", "{",
	                     "_id", BCON_INT32 (0), "totalTime", BCON_INT32 (1), "}", "}", "]");
	
	if (!pipeline) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create pipeline when reading result total time by job id");
		ret_val = false;
	}
	
	else {
		cursor = mongoc_collection_aggregate (mongo_results_collection,
		                                      MONGOC_QUERY_NONE, pipeline, NULL, NULL);
	
		if (!cursor) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create cursor when reading result total time by job id");
			ret_val = false;
		}
	
		else {
			bson_error_t error;
	
			if (mongoc_cursor_error (cursor, &error)) {
				DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
				            "cursor error ('%s') when reading result total time by job id", error.message);
				ret_val = false;
			}
	
			else {
				const bson_t *doc = NULL;
	
				if (mongoc_cursor_next (cursor, &doc)) {
					bson_iter_t iter;
	
					if (bson_iter_init (&iter, doc) && bson_iter_next (&iter)) {
						*total_time = bson_iter_double (&iter);
					}
	
					else {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "cannot iterate on document when reading result total time by job id");
						ret_val = false;
					}
				}
			}
		}
	
		mongoc_cursor_destroy (cursor);
		bson_destroy (pipeline);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

/*
 * read results FE distribution
 *
 * use aggregation to read job results and return summary statistics group by position:
 *      minimum FE
 *      average FE
 *      maximum FE
 *      (sample) standard deviation
 */
bool read_results_fe_distribution (ds_object_id_field *job_id,
                                   dsp_dataset *fe_dataset) {
	bool ret_val = true;
	*fe_dataset = NULL;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	mongoc_cursor_t *cursor = NULL;
	bson_t *pipeline;
	bson_oid_t oid;
	bson_oid_init_from_string (&oid, (char *)job_id);
	pipeline = BCON_NEW ("pipeline",
	                     "[",
	                     "{",
	                     "$match", "{", DS_COL_RESULTS_JOB_ID, BCON_OID (&oid), "}",
	                     "}",
	                     "{",
	                     "$group",
	                     "{",
	                     "_id", BCON_UTF8 ("$position"),
	                     "minFE", "{", "$min", BCON_UTF8 ("$fe"), "}",
	                     "maxFE", "{", "$max", BCON_UTF8 ("$fe"), "}",
	                     "avg", "{", "$avg", BCON_UTF8 ("$fe"), "}",
	                     "std", "{", "$stdDevSamp", BCON_UTF8 ("$fe"), "}",
	                     "count", "{", "$sum", BCON_INT32 (1), "}",
	                     "}",
	                     "}",
	                     "{", "$sort", "{", "_id", BCON_INT32 (1), "}", "}",
	                     "]");
	
	if (!pipeline) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create pipeline when reading results FE distribution");
		ret_val = false;
	}
	
	else {
		cursor = mongoc_collection_aggregate (mongo_results_collection,
		                                      MONGOC_QUERY_NONE, pipeline, NULL, NULL);
	
		if (!cursor) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to create cursor when reading results FE distribution");
			ret_val = false;
		}
	
		else {
			bson_error_t error;
	
			if (mongoc_cursor_error (cursor, &error)) {
				DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
				            "cursor error ('%s') when reading results FE distribution", error.message);
				ret_val = false;
			}
	
			else {
				const bson_t *doc = NULL;
				bson_iter_t iter;
				REGISTER
				bool ret_val = true;
				list_t pos_list, min_fe_list, max_fe_list, avg_fe_list, std_fe_list, count_list;
				list_init (&pos_list);
				list_init (&min_fe_list);
				list_init (&max_fe_list);
				list_init (&avg_fe_list);
				list_init (&std_fe_list);
				list_init (&count_list);
	
				while (ret_val && mongoc_cursor_next (cursor, &doc) &&
				       bson_iter_init (&iter, doc)) {
					REGISTER
					int i = 0;
	
					while (bson_iter_next (&iter)) {
						char tmp_val[DS_INT32_MAX_STRING_LENGTH + 1];
	
						if (!i || 5 == i) {
							sprintf (tmp_val, "%d", bson_iter_int32 (&iter));
						}
	
						else {
							sprintf (tmp_val, "%f", bson_iter_double (&iter));
						}
	
						const ulong sz = (ulong) (bson_strnlen (tmp_val, DS_GENERIC_FIELD_LENGTH));
						char *this_data = malloc (sz + 1);
	
						if (this_data) {
							strncpy (this_data, tmp_val, sz);
							this_data[sz] = '\0';
	
							switch (i) {
								case 0:
									list_append (&pos_list, this_data);
									break;
	
								case 1:
									list_append (&min_fe_list, this_data);
									break;
	
								case 2:
									list_append (&max_fe_list, this_data);
									break;
	
								case 3:
									list_append (&avg_fe_list, this_data);
									break;
	
								case 4:
									list_append (&std_fe_list, this_data);
									break;
	
								case 5:
									list_append (&count_list, this_data);
									break;
							}
	
							i++;
						}
	
						else {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "failed to allocate memory for distribution document when reading results FE distribution");
							ret_val = false;
							break;
						}
					}
	
					if (!ret_val) {
						while (pos_list.numels) {
							free (list_extract_at (&pos_list, 0));
						}
	
						while (min_fe_list.numels) {
							free (list_extract_at (&min_fe_list, 0));
						}
	
						while (max_fe_list.numels) {
							free (list_extract_at (&max_fe_list, 0));
						}
	
						while (avg_fe_list.numels) {
							free (list_extract_at (&avg_fe_list, 0));
						}
	
						while (std_fe_list.numels) {
							free (list_extract_at (&std_fe_list, 0));
						}
	
						while (count_list.numels) {
							free (list_extract_at (&count_list, 0));
						}
	
						break;
					}
				}
	
				if (ret_val) {
					*fe_dataset = malloc (sizeof (struct ds_dataset));
	
					if (!*fe_dataset) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to create FE dataset when reading results FE distribution");
						ret_val = false;
					}
	
					else {
						const size_t list_sz = min_fe_list.numels;
						(*fe_dataset)->data = malloc (6 * sizeof (char *) * list_sz);
	
						if (! ((*fe_dataset)->data)) {
							DEBUG_NOW (REPORT_ERRORS, DATASTORE,
							           "failed to create data element for FE dataset when reading results FE distribution");
							ret_val = false;
						}
	
						else {
							if (!list_sz) {
								(*fe_dataset)->num_records = 0;
								(*fe_dataset)->num_fields_per_record = 0;
								(*fe_dataset)->record_type = DS_REC_TYPE_STRING;
							}
	
							else {
								REGISTER
								int i = 0;
								(*fe_dataset)->num_records = list_sz; 	 // all fields less published status
								(*fe_dataset)->num_fields_per_record = 6;
								(*fe_dataset)->record_type = DS_REC_TYPE_STRING;
	
								while (pos_list.numels &&
								       min_fe_list.numels) {
									(*fe_dataset)->data[i++] = list_extract_at (&pos_list, 0);
									(*fe_dataset)->data[i++] = list_extract_at (&min_fe_list, 0);
									(*fe_dataset)->data[i++] = list_extract_at (&max_fe_list, 0);
									(*fe_dataset)->data[i++] = list_extract_at (&avg_fe_list, 0);
									(*fe_dataset)->data[i++] = list_extract_at (&std_fe_list, 0);
									(*fe_dataset)->data[i++] = list_extract_at (&count_list, 0);
								}
							}
						}
					}
				}
	
				list_destroy (&pos_list);
				list_destroy (&min_fe_list);
				list_destroy (&max_fe_list);
				list_destroy (&avg_fe_list);
				list_destroy (&std_fe_list);
				list_destroy (&count_list);
			}
		}
	
		mongoc_cursor_destroy (cursor);
		bson_destroy (pipeline);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool delete_results_by_job_id (ds_object_id_field *job_id,
                               ds_int32_field ref_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading results by job id");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *job_id);
		BSON_APPEND_OID (query, DS_COL_RESULTS_JOB_ID, &oid);
		BSON_APPEND_INT32 (query, DS_COL_RESULTS_REF_ID, ref_id);
	
		/*
		 * attempt deletion; do not signal failure if no results are found/deleted
		 */
		if (!mongoc_collection_delete_many (mongo_results_collection, query, NULL, NULL,
		                                    NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete document when reading results by job id");
			ret_val = false;
		}
	
		else
			if (do_notify) {
				// for delete events, ref_id will not be available to watch -> notify directly here
				ds_notification_event *this_event = (ds_notification_event *)malloc (sizeof (
				                                        ds_notification_event));
	
				if (this_event) {
					unsigned long long now = get_real_time();
					this_event->time = now;
					this_event->ref_id = ref_id;
					this_event->collection = DS_COL_RESULTS;
					this_event->op_type = DS_NOTIFY_OP_DELETE;
					strncpy (this_event->oid_string, (char *)job_id, DS_OBJ_ID_LENGTH);
	
					if (!enq_ds (this_event)) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to enqueue notification event when reading results by job id");
					}
				}
	
				else {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "cannot allocate memory for enqueue notification event when reading results by job id");
				}
			}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool delete_all_results_by_job_id (ds_object_id_field *job_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading all results by job id");
		ret_val = false;
	}
	
	else {
		bson_oid_t oid;
		bson_oid_init_from_string (&oid, *job_id);
		BSON_APPEND_OID (query, DS_COL_RESULTS_JOB_ID, &oid);
	
		/*
		 * attempt deletion; do not signal failure if no results are found/deleted
		 */
		if (!mongoc_collection_delete_many (mongo_results_collection, query, NULL, NULL,
		                                    NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete document when reading all results by job id");
			ret_val = false;
		}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}

bool delete_all_results (ds_int32_field ref_id) {
	bool ret_val = true;
	DS_LOCK_S
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *query = bson_new();
	
	if (!query) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to create bson query when reading all results by reference id");
		ret_val = false;
	}
	
	else {
		BSON_APPEND_INT32 (query, DS_COL_RESULTS_REF_ID, ref_id);
	
		/*
		 * do not signal failure if no results are deleted
		 */
		if (!mongoc_collection_delete_many (mongo_results_collection, query, NULL, NULL,
		                                    NULL)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "failed to delete documents when reading all results by reference id");
			ret_val = false;
		}
	
		else
			if (do_notify) {
				// for delete events, ref_id will not be available to watch -> notify directly here
				ds_notification_event *this_event = (ds_notification_event *)malloc (sizeof (
				                                        ds_notification_event));
	
				if (this_event) {
					unsigned long long now = get_real_time();
					this_event->time = now;
					this_event->ref_id = ref_id;
					this_event->collection = DS_COL_RESULTS;
					this_event->op_type = DS_NOTIFY_OP_DELETE;
					this_event->oid_string[0] = 0;			// null-terminate to notify on all messages
	
					if (!enq_ds (this_event)) {
						DEBUG_NOW (REPORT_ERRORS, DATASTORE,
						           "failed to enqueue notification event when reading all results by reference id");
					}
				}
	
				else {
					DEBUG_NOW (REPORT_ERRORS, DATASTORE,
					           "failed to allocate memory for enqueue notification event when reading all results by reference id");
				}
			}
	
		bson_destroy (query);
	}
	
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	DS_LOCK_E
	return ret_val;
}
/*
 * counters collection methods
 */
bool get_next_sequence (char *counter_name,
                        ds_int32_field
                        *seq) {              // NOTE: get_next_sequence is not thread-safe
	bool ret_val = true;
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	bson_t *counter_doc = bson_new();
	
	if (!counter_doc) {
		ret_val = false;
	}
	
	else {
		bson_t *query = BCON_NEW (MONGODB_OBJECT_ID_FIELD, BCON_UTF8 (counter_name));
		bson_t *update = BCON_NEW ("$inc", "{", "seq", BCON_INT32 (1), "}");
		bson_t reply;
	
		if (!mongoc_collection_find_and_modify (mongo_counters_collection, query, NULL,
		                                        update, NULL, false, false, true, &reply, NULL)) {
			ret_val = false;
		}
	
		else {
			bson_iter_t iter, desc_iter;
	
			if (!bson_iter_init (&iter, &reply) ||
			    !bson_iter_find_descendant (&iter, "value.seq", &desc_iter) ||
			    BSON_TYPE_DOUBLE != bson_iter_value (&desc_iter)->value_type) {
				DEBUG_NOW (REPORT_ERRORS, DATASTORE,
				           "failed to get sequence counter from datastore");
				ret_val = false;
			}
	
			else {
				*seq = (ds_int32_field)bson_iter_as_int64 (&desc_iter);
			}
		}
	
		bson_destroy (query);
		bson_destroy (update);
		bson_destroy (&reply);
		bson_destroy (counter_doc);
	}
	
	#else
	#endif
	return ret_val;
}

void free_dataset (restrict dsp_dataset ds) {
	if (ds) {
		if (ds->data) {
			switch (ds->record_type) {
				case DS_REC_TYPE_STRING:    {
						REGISTER ulong idx = 0, i, j;
						
						for (i = 0; i < ds->num_records; i++) {
							for (j = 0; j < ds->num_fields_per_record; j++) {
								free (((char **) ds->data)[idx++]);
							}
						}
						
						break;
					}
					
				default:
					break;
			}
			
			free (ds->data);
		}
		
		free (ds);
	}
}

bool merge_datasets (const restrict dsp_dataset ds1,
                     const restrict dsp_dataset ds2, dsp_dataset restrict *merged_ds) {
	/*
	 * assumes user handled trivial cases (either ds1 or ds2 is empty)
	 */
	if (!ds1 || !ds2 || *merged_ds ||
	    ds1->num_fields_per_record != ds2->num_fields_per_record ||
	    ds1->record_type != ds2->record_type || !ds1->num_records) {
		return false;
	}
	
	*merged_ds = malloc (sizeof (struct ds_dataset));
	
	if (!*merged_ds) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to merge datasets");
		return NULL;
	}
	
	(*merged_ds)->data = malloc ((size_t) (sizeof (void *) *
	                                       (ds1->num_records + ds2->num_records) * (ds1->num_fields_per_record)));
	                                       
	if (! ((*merged_ds)->data)) {
		free (*merged_ds);
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to merge datasets");
		return NULL;
	}
	
	REGISTER
	ulong idx = 0, last_idx = 0;
	
	for (char iter = 0; iter < 2; iter++) {
		REGISTER
		dsp_dataset ds;
		
		if (!iter) {
			ds = ds1;
			(*merged_ds)->record_type = ds1->record_type;
			(*merged_ds)->num_fields_per_record = ds1->num_fields_per_record;
		}
		
		else {
			ds = ds2;
			last_idx = idx;
		}
		
		switch (ds->record_type) {
			case DS_REC_TYPE_STRING:    {
					REGISTER ulong i, j;
					
					for (i = 0; i < ds->num_records; i++) {
						for (j = 0; j < ds->num_fields_per_record; j++) {
							(*merged_ds)->data[idx] = malloc (strlen (((char **) ds->data)[idx - last_idx])
							                                  + 1);
							                                  
							if (! ((*merged_ds)->data[idx])) {
								for (ulong k = 0; k <= idx; k++) {
									free ((*merged_ds)->data[k]);
								}
								
								free ((*merged_ds)->data);
								free (*merged_ds);
								DEBUG_NOW (REPORT_ERRORS, DATASTORE,
								           "failed to merge datasets");
								return false;
							}
							
							strcpy ((*merged_ds)->data[idx], ((char **) ds->data)[idx - last_idx]);
							idx++;
						}
					}
					
					break;
				}
				
			default:
				break;
		}
	}
	
	(*merged_ds)->num_records = ds1->num_records + ds2->num_records;
	return true;
}

void print_dataset (restrict dsp_dataset ds) {
	if (ds && ds->data) {
		switch (ds->record_type) {
			case DS_REC_TYPE_STRING:    {
					REGISTER ulong idx = 0, i, j;
					DEBUG_NOW1 (REPORT_INFO, DATASTORE, "========dataset (%06d records)========",
					            ds->num_records);
					           
					for (i = 0; i < ds->num_records; i++) {
						for (j = 0; j < ds->num_fields_per_record; j++) {
							if (MAX_MSG_LEN-4<strlen (ds->data[idx])) {
								// truncate any data larger than max msg len - annotation chars
								char t[MAX_MSG_LEN-4];
								g_memcpy (t, ds->data[idx++], MAX_MSG_LEN-5);
								t[MAX_MSG_LEN-5]='\0';
								DEBUG_NOW2 (REPORT_INFO, DATASTORE, "%02d: %s", j, t);
							}
							else {
							 	DEBUG_NOW2 (REPORT_INFO, DATASTORE, "%02d: %s", j, ds->data[idx++]);
							}
						}
					}
					
					DEBUG_NOW (REPORT_INFO, DATASTORE, "========================================");
					break;
				}
				
			default:
				break;
		}
	}
	
	else {
		DEBUG_NOW (REPORT_WARNINGS, DATASTORE, "NULL or empty dataset found");
	}
}

/*
 * NOTE: can clone sequence, cssd, job data, but not results. result datasets may
 *       hold a NULL data entry, so the cloning procedure below would fail
 */
dsp_dataset clone_dataset (restrict dsp_dataset ds) {
	if (ds && ds->data) {
		dsp_dataset cds = malloc (sizeof (struct ds_dataset));
		
		if (!cds) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE, "failed to clone dataset");
			return NULL;
		}
		
		cds->data = malloc ((size_t) (sizeof (void *) * (ds->num_records) *
		                              (ds->num_fields_per_record)));
		                              
		if (!cds->data) {
			free (cds);
			DEBUG_NOW (REPORT_ERRORS, DATASTORE, "failed to clone dataset");
			return NULL;
		}
		
		switch (ds->record_type) {
			case DS_REC_TYPE_STRING:    {
					REGISTER ulong idx = 0, i, j;
					cds->record_type = DS_REC_TYPE_STRING;
					
					for (i = 0; i < ds->num_records; i++) {
						for (j = 0; j < ds->num_fields_per_record; j++) {
							cds->data[idx] = malloc (strlen (((char **) ds->data)[idx]) + 1);
							
							if (!cds->data[idx]) {
								for (ulong k = 0; k < j; k++) {
									free (cds->data[k]);
								}
								
								free (cds->data);
								free (cds);
								DEBUG_NOW (REPORT_ERRORS, DATASTORE, "failed to clone dataset field");
								return NULL;
							}
							
							strcpy (cds->data[idx], ((char **) ds->data)[idx]);
							idx++;
						}
					}
					
					break;
				}
				
			default:
				break;
		}
		
		cds->num_records = ds->num_records;
		cds->num_fields_per_record = ds->num_fields_per_record;
		return cds;
	}
	
	else {
		return NULL;
	}
}

bool initialize_datastore (char *ds_server, unsigned short ds_port,
                           bool notify) {
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	return true;
	#elif DATASTORE_TYPE==1         // MongoDB
	do_notify = notify;
	ds_thread_shutting_down = false;
	DEBUG_NOW (REPORT_INFO, DATASTORE, "initializing MongoDB interface");
	
	if (mongo_uri) {
		DEBUG_NOW (REPORT_WARNINGS, DATASTORE, "MongoDB already initialized");
		return false;
	}
	
	if (pthread_spin_init (&ds_spinlock, PTHREAD_PROCESS_PRIVATE)) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE, "failed to initialize datastore spinlock");
		return false;
	}
	
	// init shallow cache
	for (ulong i = 0; i < DS_MAX_SC_ENTRIES; i++) {
		sc_sequence_by_id[i].ds = NULL;
		sc_sequence_by_id[i].frequency = 0;
		sc_sequence_by_id[i].oid_field[0] = '\0';
		sc_cssd_by_id[i].ds = NULL;
		sc_cssd_by_id[i].frequency = 0;
		sc_cssd_by_id[i].oid_field[0] = '\0';
		sc_job_by_id[i].ds = NULL;
		sc_job_by_id[i].frequency = 0;
		sc_job_by_id[i].oid_field[0] = '\0';
	}
	
	mongoc_init();
	char mongodb_uri[MAX_FILENAME_LENGTH + 1];
	sprintf (mongodb_uri, MONGODB_URI, ds_server, ds_port);
	DEBUG_NOW (REPORT_INFO, DATASTORE, "establishing MongoDB URI");
	mongo_uri = mongoc_uri_new_with_error (mongodb_uri, NULL);
	
	if (!mongo_uri) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE, "failed to parse MongoDB URI");
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
		mongoc_cleanup();
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
		pthread_spin_destroy (&ds_spinlock);
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, DATASTORE, "connecting to MongoDB instance");
	mongo_client = mongoc_client_new_from_uri (mongo_uri);
	
	if (!mongo_client) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE, "failed to connect to MongoDB");
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
		mongoc_uri_destroy (mongo_uri);
		mongoc_cleanup();
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
		pthread_spin_destroy (&ds_spinlock);
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, DATASTORE, "setting up client application for MongoDB");
	
	if (!mongoc_client_set_appname (mongo_client, MONGODB_APP_NAME)) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE,
		           "failed to set up client application for MongoDB");
		DEBUG_NOW (REPORT_INFO, DATASTORE, "disconnecting from MongoDB instance");
		mongoc_client_destroy (mongo_client);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
		mongoc_uri_destroy (mongo_uri);
		mongoc_cleanup();
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
		pthread_spin_destroy (&ds_spinlock);
		return false;
	}
	
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "accessing MongoDB database '%s'",
	            MONGODB_DB_NAME);
	mongo_database = mongoc_client_get_database (mongo_client, MONGODB_DB_NAME);
	
	if (!mongo_database) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE, "failed to access MongoDB database");
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing client application for MongoDB");
		mongoc_client_destroy (mongo_client);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
		mongoc_uri_destroy (mongo_uri);
		mongoc_cleanup();
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
		pthread_spin_destroy (&ds_spinlock);
		return false;
	}
	
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "getting MongoDB collection '%s'",
	            MONGODB_SEQUENCES_COLLECTION_NAME);
	mongo_sequences_collection = mongoc_client_get_collection (mongo_client,
	                                        MONGODB_DB_NAME, MONGODB_SEQUENCES_COLLECTION_NAME);
	
	if (!mongo_sequences_collection) {
		DEBUG_NOW1 (REPORT_ERRORS, DATASTORE, "failed to get MongoDB collection '%s'",
		            MONGODB_SEQUENCES_COLLECTION_NAME);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "relinquishing MongoDB database '%s'",
		            MONGODB_DB_NAME);
		mongoc_database_destroy (mongo_database);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing client application for MongoDB");
		mongoc_client_destroy (mongo_client);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
		mongoc_uri_destroy (mongo_uri);
		mongoc_cleanup();
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
		pthread_spin_destroy (&ds_spinlock);
		return false;
	}
	
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "getting MongoDB collection '%s'",
	            MONGODB_CSSD_COLLECTION_NAME);
	mongo_cssd_collection = mongoc_client_get_collection (mongo_client,
	                                        MONGODB_DB_NAME, MONGODB_CSSD_COLLECTION_NAME);
	
	if (!mongo_cssd_collection) {
		DEBUG_NOW1 (REPORT_ERRORS, DATASTORE, "failed to get MongoDB collection '%s'",
		            MONGODB_CSSD_COLLECTION_NAME);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_SEQUENCES_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_sequences_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "relinquishing MongoDB database '%s'",
		            MONGODB_DB_NAME);
		mongoc_database_destroy (mongo_database);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing client application for MongoDB");
		mongoc_client_destroy (mongo_client);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
		mongoc_uri_destroy (mongo_uri);
		mongoc_cleanup();
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
		pthread_spin_destroy (&ds_spinlock);
		return false;
	}
	
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "getting MongoDB collection '%s'",
	            MONGODB_JOBS_COLLECTION_NAME);
	mongo_jobs_collection = mongoc_client_get_collection (mongo_client,
	                                        MONGODB_DB_NAME, MONGODB_JOBS_COLLECTION_NAME);
	
	if (!mongo_jobs_collection) {
		DEBUG_NOW1 (REPORT_ERRORS, DATASTORE, "failed to get MongoDB collection '%s'",
		            MONGODB_JOBS_COLLECTION_NAME);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_CSSD_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_cssd_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_SEQUENCES_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_sequences_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "relinquishing MongoDB database '%s'",
		            MONGODB_DB_NAME);
		mongoc_database_destroy (mongo_database);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing client application for MongoDB");
		mongoc_client_destroy (mongo_client);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
		mongoc_uri_destroy (mongo_uri);
		mongoc_cleanup();
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
		pthread_spin_destroy (&ds_spinlock);
		return false;
	}
	
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "getting MongoDB collection '%s'",
	            MONGODB_RESULTS_COLLECTION_NAME);
	mongo_results_collection = mongoc_client_get_collection (mongo_client,
	                                        MONGODB_DB_NAME, MONGODB_RESULTS_COLLECTION_NAME);
	
	if (!mongo_results_collection) {
		DEBUG_NOW1 (REPORT_ERRORS, DATASTORE, "failed to get MongoDB collection '%s'",
		            MONGODB_RESULTS_COLLECTION_NAME);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_JOBS_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_jobs_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_CSSD_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_cssd_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_SEQUENCES_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_sequences_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "relinquishing MongoDB database '%s'",
		            MONGODB_DB_NAME);
		mongoc_database_destroy (mongo_database);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing client application for MongoDB");
		mongoc_client_destroy (mongo_client);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
		mongoc_uri_destroy (mongo_uri);
		mongoc_cleanup();
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
		pthread_spin_destroy (&ds_spinlock);
		return false;
	}
	
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "getting MongoDB collection '%s'",
	            MONGODB_USERS_COLLECTION_NAME);
	mongo_users_collection = mongoc_client_get_collection (mongo_client,
	                                        MONGODB_DB_NAME, MONGODB_USERS_COLLECTION_NAME);
	
	if (!mongo_users_collection) {
		DEBUG_NOW1 (REPORT_ERRORS, DATASTORE, "failed to get MongoDB collection '%s'",
		            MONGODB_USERS_COLLECTION_NAME);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_RESULTS_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_results_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_JOBS_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_jobs_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_CSSD_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_cssd_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_SEQUENCES_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_sequences_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "relinquishing MongoDB database '%s'",
		            MONGODB_DB_NAME);
		mongoc_database_destroy (mongo_database);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing client application for MongoDB");
		mongoc_client_destroy (mongo_client);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
		mongoc_uri_destroy (mongo_uri);
		mongoc_cleanup();
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
		pthread_spin_destroy (&ds_spinlock);
		return false;
	}
	
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "getting MongoDB collection '%s'",
	            MONGODB_COUNTERS_COLLECTION_NAME);
	mongo_counters_collection = mongoc_client_get_collection (mongo_client,
	                                        MONGODB_DB_NAME, MONGODB_COUNTERS_COLLECTION_NAME);
	
	if (!mongo_counters_collection) {
		DEBUG_NOW1 (REPORT_ERRORS, DATASTORE, "failed to get MongoDB collection '%s'",
		            MONGODB_COUNTERS_COLLECTION_NAME);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_USERS_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_users_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_RESULTS_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_results_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_JOBS_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_jobs_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_CSSD_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_cssd_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_SEQUENCES_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_sequences_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "relinquishing MongoDB database '%s'",
		            MONGODB_DB_NAME);
		mongoc_database_destroy (mongo_database);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing client application for MongoDB");
		mongoc_client_destroy (mongo_client);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
		mongoc_uri_destroy (mongo_uri);
		mongoc_cleanup();
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
		pthread_spin_destroy (&ds_spinlock);
		return false;
	}
	
	char cd_id[DS_OBJ_ID_LENGTH + 1];
	g_memset (cd_id, 0, DS_OBJ_ID_LENGTH + 1);
	dsp_dataset ids_dataset = NULL;
	DEBUG_NOW (REPORT_INFO, DATASTORE, "validating MongoDB interface");
	
	if (!read_sequence_by_id (&cd_id,
	                          &ids_dataset)) {
		DEBUG_NOW (REPORT_ERRORS, DATASTORE, "read test on sequence collection failed");
	
		if (ids_dataset) {
			free_dataset (ids_dataset);
		}
	
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_COUNTERS_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_counters_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_USERS_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_users_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_RESULTS_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_results_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_JOBS_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_jobs_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_CSSD_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_cssd_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
		            MONGODB_SEQUENCES_COLLECTION_NAME);
		mongoc_collection_destroy (mongo_sequences_collection);
		DEBUG_NOW1 (REPORT_INFO, DATASTORE, "relinquishing MongoDB database '%s'",
		            MONGODB_DB_NAME);
		mongoc_database_destroy (mongo_database);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing client application for MongoDB");
		mongoc_client_destroy (mongo_client);
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
		mongoc_uri_destroy (mongo_uri);
		mongoc_cleanup();
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
		pthread_spin_destroy (&ds_spinlock);
		return false;
	}
	
	if (ids_dataset) {
		free_dataset (ids_dataset);
	}
	
	if (do_notify) {
		for (uchar i = 0; i < 4; i++) {
			if (NULL == (thread_args[i] = malloc (sizeof (ds_enq_thread_args)))) {
				for (uchar j = 0; j < i; j++) {
					free (thread_args[j]);
				}
			}
	
			strcpy (thread_args[i]->ds_server, ds_server);
			thread_args[i]->ds_port = ds_port;
		}
	
		strcpy (thread_args[0]->this_collection_name,
		        MONGODB_SEQUENCES_COLLECTION_NAME);
		thread_args[0]->this_collection = DS_COL_SEQUENCES;
		strcpy (thread_args[1]->this_collection_name, MONGODB_CSSD_COLLECTION_NAME);
		thread_args[1]->this_collection = DS_COL_CSSD;
		strcpy (thread_args[2]->this_collection_name, MONGODB_JOBS_COLLECTION_NAME);
		thread_args[2]->this_collection = DS_COL_JOBS;
		strcpy (thread_args[3]->this_collection_name, MONGODB_RESULTS_COLLECTION_NAME);
		thread_args[3]->this_collection = DS_COL_RESULTS;
		DEBUG_NOW (REPORT_INFO, DATASTORE,
		           "initializing notification queue");
	
		if (!initialize_ds_q()) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE,
			           "could not initialize notification queue");
	
			for (uchar j = 0; j < 4; j++) {
				free (thread_args[j]);
			}
	
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
			            MONGODB_COUNTERS_COLLECTION_NAME);
			mongoc_collection_destroy (mongo_counters_collection);
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
			            MONGODB_USERS_COLLECTION_NAME);
			mongoc_collection_destroy (mongo_users_collection);
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
			            MONGODB_RESULTS_COLLECTION_NAME);
			mongoc_collection_destroy (mongo_results_collection);
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
			            MONGODB_JOBS_COLLECTION_NAME);
			mongoc_collection_destroy (mongo_jobs_collection);
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
			            MONGODB_CSSD_COLLECTION_NAME);
			mongoc_collection_destroy (mongo_cssd_collection);
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
			            MONGODB_SEQUENCES_COLLECTION_NAME);
			mongoc_collection_destroy (mongo_sequences_collection);
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "relinquishing MongoDB database '%s'",
			            MONGODB_DB_NAME);
			mongoc_database_destroy (mongo_database);
			DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing client application for MongoDB");
			mongoc_client_destroy (mongo_client);
			DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
			mongoc_uri_destroy (mongo_uri);
			mongoc_cleanup();
			DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
			pthread_spin_destroy (&ds_spinlock);
			return false;
		}
	
		DEBUG_NOW (REPORT_INFO, DATASTORE, "initializing queue spinlock");
	
		if (pthread_spin_init (&ds_q_spinlock, PTHREAD_PROCESS_PRIVATE)) {
			DEBUG_NOW (REPORT_ERRORS, DATASTORE, "failed to initialize queue spinlock");
			DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing notification queue");
			finalize_ds_q();
	
			for (uchar j = 0; j < 4; j++) {
				free (thread_args[j]);
			}
	
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
			            MONGODB_COUNTERS_COLLECTION_NAME);
			mongoc_collection_destroy (mongo_counters_collection);
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
			            MONGODB_USERS_COLLECTION_NAME);
			mongoc_collection_destroy (mongo_users_collection);
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
			            MONGODB_RESULTS_COLLECTION_NAME);
			mongoc_collection_destroy (mongo_results_collection);
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
			            MONGODB_JOBS_COLLECTION_NAME);
			mongoc_collection_destroy (mongo_jobs_collection);
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
			            MONGODB_CSSD_COLLECTION_NAME);
			mongoc_collection_destroy (mongo_cssd_collection);
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
			            MONGODB_SEQUENCES_COLLECTION_NAME);
			mongoc_collection_destroy (mongo_sequences_collection);
			DEBUG_NOW1 (REPORT_INFO, DATASTORE, "relinquishing MongoDB database '%s'",
			            MONGODB_DB_NAME);
			mongoc_database_destroy (mongo_database);
			DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing client application for MongoDB");
			mongoc_client_destroy (mongo_client);
			DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
			mongoc_uri_destroy (mongo_uri);
			mongoc_cleanup();
			DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
			pthread_spin_destroy (&ds_spinlock);
			return false;
		}
	
		DEBUG_NOW (REPORT_INFO, DATASTORE, "initializing notification queue threads");
	
		for (uchar tnum = 0; tnum < 4; tnum++) {
			if (0 != pthread_create (&ds_enq_thread[tnum], NULL, ds_enq_thread_start,
			                         thread_args[tnum])) {
				DEBUG_NOW1 (REPORT_ERRORS, DATASTORE,
				            "failed to initialize notification queue thread for %s",
				            thread_args[tnum]->this_collection_name);
				ds_thread_shutting_down = true;
	
				for (uchar t = 0; t < tnum; t++) {
					pthread_join (ds_enq_thread[t], NULL);
				}
	
				for (ushort j = 0; j < 4; j++) {
					free (thread_args[j]);
				}
	
				DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing queue spinlock");
				pthread_spin_destroy (&ds_q_spinlock);
				DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing notification queue");
				finalize_ds_q();
				DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
				            MONGODB_COUNTERS_COLLECTION_NAME);
				mongoc_collection_destroy (mongo_counters_collection);
				DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
				            MONGODB_USERS_COLLECTION_NAME);
				mongoc_collection_destroy (mongo_users_collection);
				DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
				            MONGODB_RESULTS_COLLECTION_NAME);
				mongoc_collection_destroy (mongo_results_collection);
				DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
				            MONGODB_JOBS_COLLECTION_NAME);
				mongoc_collection_destroy (mongo_jobs_collection);
				DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
				            MONGODB_CSSD_COLLECTION_NAME);
				mongoc_collection_destroy (mongo_cssd_collection);
				DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
				            MONGODB_SEQUENCES_COLLECTION_NAME);
				mongoc_collection_destroy (mongo_sequences_collection);
				DEBUG_NOW1 (REPORT_INFO, DATASTORE, "relinquishing MongoDB database '%s'",
				            MONGODB_DB_NAME);
				mongoc_database_destroy (mongo_database);
				DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing client application for MongoDB");
				mongoc_client_destroy (mongo_client);
				DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
				mongoc_uri_destroy (mongo_uri);
				mongoc_cleanup();
				DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
				pthread_spin_destroy (&ds_spinlock);
				return false;
			}
		}
	}
	
	return true;
	#elif DATASTORE_TYPE==2         // MonetDB
	return true;
	#else
	return false;
	#endif
}

void finalize_datastore() {
	#if DATASTORE_TYPE==0           // simple, 'virtual' datastore for testing purposes
	#elif DATASTORE_TYPE==1         // MongoDB
	ds_thread_shutting_down = true;

	// join threads before releasing collections
	if (do_notify) {
		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing notification queue");
		finalize_ds_q();
		DEBUG_NOW (REPORT_INFO, DATASTORE, "joining queue threads");

		for (uchar t = 0; t < 4; t++) {
			pthread_join (ds_enq_thread[t], NULL);
			free (thread_args[t]);
		}

		DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing queue spinlock");
		pthread_spin_destroy (&ds_q_spinlock);
	}

	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
	            MONGODB_COUNTERS_COLLECTION_NAME);
	mongoc_collection_destroy (mongo_counters_collection);
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
	            MONGODB_USERS_COLLECTION_NAME);
	mongoc_collection_destroy (mongo_users_collection);
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
	            MONGODB_RESULTS_COLLECTION_NAME);
	mongoc_collection_destroy (mongo_results_collection);
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
	            MONGODB_JOBS_COLLECTION_NAME);
	mongoc_collection_destroy (mongo_jobs_collection);
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
	            MONGODB_CSSD_COLLECTION_NAME);
	mongoc_collection_destroy (mongo_cssd_collection);
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "disposing of MongoDB collection '%s'",
	            MONGODB_SEQUENCES_COLLECTION_NAME);
	mongoc_collection_destroy (mongo_sequences_collection);
	DEBUG_NOW1 (REPORT_INFO, DATASTORE, "relinquishing MongoDB database '%s'",
	            MONGODB_DB_NAME);
	mongoc_database_destroy (mongo_database);
	DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing client application for MongoDB");
	mongoc_client_destroy (mongo_client);
	DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing MongoDB interface");
	mongoc_uri_destroy (mongo_uri);
	mongoc_cleanup();
	DEBUG_NOW (REPORT_INFO, DATASTORE, "finalizing datastore spinlock");
	pthread_spin_destroy (&ds_spinlock);
	/*
	 * NOTE: the MongoDB project at github as at 04022019 states that due to the nature of libcrypto and libSSL and
	 *       their use in libmongoc, memcheck might (will) report memory leaks. The suppression file committed at
	 *       https://github.com/mongodb/mongo-c-driver/blob/master/valgrind.suppressions
	 *       is currently used to suppress these reports
	 */
	#elif DATASTORE_TYPE==2         // MonetDB
	#else
	#endif
	pthread_spin_destroy (&ds_spinlock);
	
	for (uchar i = 0; i < DS_MAX_SC_ENTRIES; i++) {
		if (sc_sequence_by_id[i].frequency) {
			if (sc_sequence_by_id[i].ds) {
				free_dataset (sc_sequence_by_id[i].ds);
			}
		}
		
		if (sc_cssd_by_id[i].frequency) {
			if (sc_cssd_by_id[i].ds) {
				free_dataset (sc_cssd_by_id[i].ds);
			}
		}
		
		if (sc_job_by_id[i].frequency) {
			if (sc_job_by_id[i].ds) {
				free_dataset (sc_job_by_id[i].ds);
			}
		}
	}
}
