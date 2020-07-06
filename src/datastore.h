#ifndef RNA_DATASTORE_H
#define RNA_DATASTORE_H

#include <limits.h>
#include <stdbool.h>
#include <bson.h>
#include "util.h"

#define DS_MIN_PORT                     1024                // the lower limit is based on standard (RFC793) ranges for registered/user ports, but the upper limit is derived from
#define DS_MAX_PORT                     61000               // "/proc/sys/net/ipv4/ip_local_port_range" on "Linux node-003 3.2.0-5-amd64 #1 SMP Debian 3.2.96-3 x86_64 GNU/Linux")

typedef enum {
	DS_COL_NONE = 0,
	DS_COL_SEQUENCES = 1,
	DS_COL_CSSD = 2,
	DS_COL_JOBS = 4,
	DS_COL_RESULTS = 8
} DS_COLLECTION;

typedef enum {
	DS_NOTIFY_OP_UNKNOWN = 0,
	DS_NOTIFY_OP_INSERT = 1,
	DS_NOTIFY_OP_UPDATE = 2,
	DS_NOTIFY_OP_DELETE = 3
} DS_NOTIFY_OP_TYPE;

#define DS_COLLECTION_NUM_NOTIF_PARAMS	4					// number of notification paramaeters: (1,2,4,8) or 2^(4-1)==8

#define MAX_DS_Q_SIZE                   100000

#define DATASTORE_TYPE                  1                   // 0==VIRTUAL, 1==MONGODB, 2==MONETDB

#define DS_COL_SEQUENCE_ID              "_id"               // TODO: _id fields are specific to MONGODB
#define DS_COL_SEQUENCE_DEFINITION      "definition"
#define DS_COL_SEQUENCE_REF_ID          "ref_id"
#define DS_COL_SEQUENCE_REF_ID_IDX      5                   // 0-indices are into datastore cols; less one for ds_dataset
#define DS_COL_SEQUENCE_3P_UTR          "3\'UTR"
#define DS_COL_SEQUENCE_3P_UTR_ALT      "seqnt"             // TODO: replace 3P_UTR with 3P_UTR_ALT ("seq-nt"), the hyphen
#define DS_COL_SEQUENCE_3P_UTR_IDX      3                   //       removal is required because of knockoutjs limitation
#define DS_COL_SEQUENCE_ACCESSION       "accession"
#define DS_COL_SEQUENCE_GROUP           "group"
extern const char *const DS_COLS_SEQUENCES[5];

#define DS_COL_CSSD_ID                  "_id"
#define DS_COL_CSSD_PUBLISHED 		    "published"
#define DS_COL_CSSD_PUBLISHED_IDX	    4
#define DS_COL_CSSD_REF_ID              "ref_id"
#define DS_COL_CSSD_REF_ID_IDX          3
#define DS_COL_CSSD_STRING              "cs"
#define DS_COL_CSSD_STRING_IDX          1
#define DS_COL_CSSD_NAME                "name"
#define DS_COL_CSSD_NAME_IDX            2
extern const char *const DS_COLS_CSSD[5];

#define DS_COL_JOB_ID                   "_id"
#define DS_COL_JOB_REF_ID               "ref_id"
#define DS_COL_JOB_REF_ID_IDX           8
#define DS_COL_JOB_SEQUENCE_ID          "sequence_id"
#define DS_COL_JOB_SEQUENCE_ID_IDX      1
#define DS_COL_JOB_CSSD_ID              "cssd_id"
#define DS_COL_JOB_CSSD_ID_IDX          2
#define DS_COL_JOB_STATUS               "status"
#define DS_COL_JOB_STATUS_IDX		    3
#define DS_COL_JOB_ERROR		        "error"
#define DS_COL_JOB_ERROR_IDX 		    4
#define DS_COL_JOB_NUM_WINDOWS       	"num_windows"
#define DS_COL_JOB_NUM_WINDOWS_IDX   	5
#define DS_COL_JOB_NUM_WINDOWS_SUCCESS 	"num_windows_success"
#define DS_COL_JOB_NUM_WINDOWS_SUCCESS_IDX 	6
#define DS_COL_JOB_NUM_WINDOWS_FAIL 	"num_windows_fail"
#define DS_COL_JOB_NUM_WINDOWS_FAIL_IDX 7

extern const char *const DS_COLS_JOB[9];
extern const char *const
DS_COLS_JOBS[15];                                           // aggregate dataset over jobs, sequences, and cs documents

#define DS_COL_USER_NAME                "name"
#define DS_COL_USER_SUB_ID              "sub_id"
#define DS_COL_USER_REF_ID              "ref_id"

#define DS_COL_RESULTS_ID               "_id"
#define DS_COL_RESULTS_JOB_ID           "job_id"
#define DS_COL_RESULTS_JOB_ID_IDX       1
#define DS_COL_RESULTS_HIT_TIME         "time"
#define DS_COL_RESULTS_HIT_TIME_IDX     2
#define DS_COL_RESULTS_HIT_POSITION     "position"
#define DS_COL_RESULTS_HIT_POSITION_IDX 3
#define DS_COL_RESULTS_HIT_FE           "fe"
#define DS_COL_RESULTS_HIT_FE_IDX       4
#define DS_COL_RESULTS_HIT_STRING       "hit_string"
#define DS_COL_RESULTS_HIT_STRING_IDX   5
#define DS_COL_RESULTS_REF_ID           "ref_id"
#define DS_COL_RESULTS_REF_ID_IDX       6
extern const char *const DS_COLS_RESULTS[6];

// "computed" fields when returning hit stats
#define DS_COL_RESULTS_HIT_MIN_FE	    "min_fe"
#define DS_COL_RESULTS_HIT_MAX_FE       "max_fe"
#define DS_COL_RESULTS_HIT_AVG_FE	    "avg_fe"
#define DS_COL_RESULTS_HIT_STD_FE	    "std_fe"
#define DS_COL_RESULTS_HIT_COUNT	    "count"
extern const char *const DS_COLS_RESULTS_SUMMARY[6];

#define DS_COLLECTION_WATCH_DURATION_MS 50
#define DS_COLLECTION_WATCH_SLEEP_MS    5

#define DS_COLLECTION_NAME_LENGTH       9
#define DS_COLLECTION_SEQUENCES         "sequences"
#define DS_COLLECTION_SEQUENCES_NFIELDS 6
#define DS_COLLECTION_JOBS              "jobs"
#define DS_COLLECTION_JOBS_NFIELDS      9
#define DS_COLLECTION_CSSD              "cs"
#define DS_COLLECTION_CSSD_NFIELDS      5
#define DS_COLLECTION_RESULTS           "results"
#define DS_COLLECTION_RESULTS_NFIELDS   7
#define DS_COLLECTION_USERS             "users"
#define DS_COLLECTION_USERS_NFIELDS     4
#define DS_COLLECTION_COUNTERS          "counters"
#define DS_OBJ_ID_LENGTH                24                  // %02d string representation of 12-byte oid
#define DS_GENERIC_FIELD_LENGTH         10000               // must be able to contain DS object ids and sequences
#define DS_JOB_RESULT_HIT_FIELD_LENGTH  1000                // has dependency on MAX_MODEL_STRING_LEN

#define DS_JOB_STATUS_INIT              0					// job is properly initalized
#define DS_JOB_STATUS_PENDING           1					// more windows by pending filter_threads may be pending
#define DS_JOB_STATUS_SUBMITTED 	    2				    // all filter_threads have completed, and any ROI submitted to search
#define DS_JOB_STATUS_DONE              3					// all search workers have returned (possibly nil) results
#define DS_JOB_STATUS_UNDEFINED         INT_MIN				// uninitialized status

#define DS_JOB_ERROR_OK			        0
#define DS_JOB_ERROR_FAIL	           -1				    // one or more windows were NOT successfully submitted to (worker) search
#define DS_JOB_ERROR_UNDEFINED          INT_MIN				// uninitialized status

#define DS_USER_REF_ID_UNDEFINED        INT_MIN

#define DS_INT64_MAX_STRING_LENGTH      20                  // max length of string representing int64 number
// max length of string representing double number
// (assuming 15 decimal places)
#define DS_DOUBLE_MAX_STRING_LENGTH     20
#define DS_INT32_MAX_STRING_LENGTH      10                  // max length of string representing int32 number

#define DS_NA_VALUE                     "n/a"

typedef enum {
	DS_REC_TYPE_STRING
} ds_record_type;

typedef struct ds_dataset {
	ds_record_type record_type;
	ulong num_records;
	uchar num_fields_per_record;
	void **data;
} *dsp_dataset;

typedef char ds_generic_field[DS_GENERIC_FIELD_LENGTH + 1];
typedef char ds_result_hit_field[DS_JOB_RESULT_HIT_FIELD_LENGTH + 1];
typedef char ds_object_id_field[DS_OBJ_ID_LENGTH + 1];
typedef int32_t ds_int32_field;
typedef int64_t ds_int64_field;
typedef double ds_double_field;
typedef bool ds_boolean_field;

// ds notification event struct
typedef struct {
	unsigned long long time;
	DS_NOTIFY_OP_TYPE op_type;			//	operation type and oid string used only for
	char oid_string[DS_OBJ_ID_LENGTH +
	                                 1];	//	high-frequency updates (to jobs, results);
	int ref_id;					// 	ref_id and collection apply to all doc types
	DS_COLLECTION collection;
} ds_notification_event;

ds_notification_event *deq_ds();

/*
 * sequence operations
 */
bool create_sequence (ds_generic_field group, ds_generic_field definition,
                      ds_generic_field accession, ds_generic_field nt, ds_int32_field ref_id,
                      nt_rt_bytes *new_object_id);
bool read_sequence_by_id (ds_object_id_field *object_id,
                          dsp_dataset *ids_dataset);
bool read_sequence_by_accession (ds_generic_field *accession,
                                 dsp_dataset *ids_dataset);
bool read_sequence_id_by_string (const char *seq_string,
                                 ds_object_id_field *seq_oid);
bool read_sequences_by_ref_id (ds_int32_field ref_id, ds_int32_field start,
                               ds_int32_field size, ds_int32_field order, dsp_dataset *ids_dataset);
bool delete_sequence_by_id (ds_object_id_field *object_id,
                            ds_int32_field ref_id);
bool delete_sequence_by_accession (ds_generic_field *accession,
                                   ds_int32_field ref_id);
bool delete_all_sequences (ds_int32_field ref_id);

/*
 * CSSD operations
 */
bool create_cssd_with_pos_var (ds_generic_field ss, ds_generic_field pos_var,
                               ds_generic_field name, ds_int32_field ref_id, ds_boolean_field published,
                               ds_object_id_field *new_object_id);
bool create_cssd (ds_generic_field cssd, ds_generic_field name,
                  ds_int32_field ref_id, ds_boolean_field published,
                  ds_object_id_field *new_object_id);
bool update_cssd (ds_object_id_field *object_id, ds_generic_field cssd,
                  ds_generic_field name, ds_int32_field ref_id, ds_boolean_field published);
bool read_cssd_by_id (ds_object_id_field *object_id, dsp_dataset *ids_dataset);
bool read_cssd_by_name (ds_generic_field *name, dsp_dataset *ids_dataset);
bool read_cssd_id_by_string (const char *cssd_string,
                             ds_object_id_field *cssd_oid);
bool read_cssds_by_ref_id (ds_int32_field ref_id, ds_int32_field start,
                           ds_int32_field limit, ds_int32_field order, dsp_dataset *ids_dataset);
bool read_cssds_by_published_status (ds_boolean_field published,
                                     ds_int32_field start, ds_int32_field limit, ds_int32_field order,
                                     dsp_dataset *ids_dataset);
bool get_cssd_published_status (ds_object_id_field *object_id,
                                ds_boolean_field *status);
bool delete_cssd_by_id (ds_object_id_field *object_id, ds_int32_field ref_id);
bool delete_cssd (ds_object_id_field *object_id, ds_int32_field ref_id);
bool delete_cssd_by_name (ds_generic_field *name, ds_int32_field ref_id);

/*
 * job operations
 */
bool create_job (ds_object_id_field *sequence_id, ds_object_id_field *cssd_id,
                 ds_int32_field status, ds_int32_field error,
                 ds_int32_field ref_id, ds_object_id_field *new_object_id);
bool read_job (ds_object_id_field *object_id, dsp_dataset *ids_dataset);
bool read_jobs_by_ref_id (ds_int32_field ref_id, ds_int32_field start,
                          ds_int32_field limit, ds_int32_field order, dsp_dataset *ids_dataset);
bool read_job_ids_by_cssd_id (ds_object_id_field *cssd_id,
                              dsp_dataset *ids_dataset);
bool read_job_ids_by_sequence_id (ds_object_id_field *sequence_id,
                                  dsp_dataset *ids_dataset);
bool read_aggregate_jobs_by_ref_id (ds_int32_field ref_id, ds_int32_field start,
                                    ds_int32_field limit, ds_int32_field order, dsp_dataset *ids_dataset);
bool read_job_windows (ds_object_id_field *object_id,
                       ds_int32_field *num_windows, ds_int32_field *num_windows_success,
                       ds_int32_field *num_windows_fail);
bool read_job_status (ds_object_id_field *object_id, ds_int32_field ref_id,
                      ds_int32_field *status);
bool update_job_status (ds_object_id_field *object_id, ds_int32_field ref_id,
                        ds_int32_field status);
bool update_job_error (ds_object_id_field *object_id, ds_int32_field ref_id,
                       ds_int32_field error);
bool update_job_num_windows (ds_object_id_field *object_id,
                             ds_int32_field ref_id, ds_int32_field num_windows);
bool update_job_num_windows_success (ds_object_id_field *object_id,
                                     ds_int32_field ref_id, ds_int32_field num_windows_success);
bool update_job_num_windows_fail (ds_object_id_field *object_id,
                                  ds_int32_field ref_id, ds_int32_field num_windows_fail);
bool delete_job (ds_object_id_field *object_id, ds_int32_field ref_id);
bool delete_all_jobs_by_job_id (ds_object_id_field *job_id);
bool delete_all_jobs (ds_int32_field ref_id);

/*
 * hit/result operations
 */
bool create_result (ds_object_id_field *job_id, ds_result_hit_field *hit,
                    ds_int32_field ref_id, ds_object_id_field *new_object_id);
bool read_results_by_job_id (ds_object_id_field *job_id, ds_int32_field start,
                             ds_int32_field limit, const char *order_by_field1, const char *order_by_field2,
                             ds_int32_field order, dsp_dataset *ids_dataset);
bool get_result_index (ds_object_id_field *job_id,
                       const char *order_by_field1, const char *order_by_field2, 
		       ds_int32_field order,
		       ds_int32_field position, ds_double_field fe, ulong *index);
bool read_result_count_by_job_id (ds_object_id_field *job_id,
                                  nt_hit_count *count);
bool read_result_total_time_by_job_id (ds_object_id_field *job_id,
                                       float *total_time);
bool read_results_fe_distribution (ds_object_id_field *job_id,
                                   dsp_dataset *fe_dataset);
bool delete_results_by_job_id (ds_object_id_field *job_id,
                               ds_int32_field ref_id);
bool delete_all_results_by_job_id (ds_object_id_field *job_id);
bool delete_all_results (ds_int32_field ref_id);

/*
 * user operations
 */
bool create_user (ds_generic_field user_name, ds_generic_field sub_id,
                  ds_int32_field *ref_id, ds_object_id_field *new_object_id);
bool read_user (ds_generic_field sub_id, dsp_dataset *ids_dataset);
bool delete_user (ds_int32_field ref_id);

/*
 * initialization and misc
 */
bool initialize_datastore (char *ds_server, unsigned short ds_port,
                           bool notify);
void print_dataset (restrict dsp_dataset ds);
dsp_dataset clone_dataset (restrict dsp_dataset ds);
void free_dataset (restrict dsp_dataset ds);
bool merge_datasets (const restrict dsp_dataset ds1,
                     const restrict dsp_dataset ds2, dsp_dataset restrict *merged_ds);
void finalize_datastore();

bool get_next_sequence (char *counter_name,
                        ds_int32_field *seq);       // note: get_next_sequence is not thread-safe

#endif //RNA_DATASTORE_H
