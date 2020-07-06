#ifndef RNA_UTIL_H
#define RNA_UTIL_H

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "simclist.h"

#define EXEC_SUCCESS    0
#define EXEC_FAILURE    -1

typedef list_t nt_list, *ntp_list, nt_count_list, *ntp_count_list;

bool initialize_utils();
void finalize_utils();

/*
 * multithreaded customization
 */

/* when defined, allows for background ("latent")
 * deallocation of memory; experimental feature
 * and currently not set by default
 */
//#define MULTITHREADED_ON

#ifdef MULTITHREADED_ON
	#define MAX_THREADS                         100         // limited by uchar
	#define MALLOC_THREAD_DESTRUCTION_SLEEP_S   1
	
	unsigned long get_malloc_t_flag();
	void clear_malloc_t_flag();
#endif

/*
 * cluster job time format
 */
#define JOB_TIME_FMT "%Y%b%d%H%M%S"

/*
 * RNA search customization
 */
#define MIN_STACK_LEN   1
#define MAX_STACK_LEN   20

#define MAX_FILE_SIZE_BYTES 1000000000LLU

/*
 * number of bytes (x2) required to convert real-time clock info to bytes,
 * equals DS_OBJ_ID_LENGTH in datastore.h for handling object IDs
 */
#define NUM_RT_BYTES         24

typedef uint16_t       	     ushort;
typedef uint32_t             nt_abs_seq_len, nt_abs_count, nt_abs_seq_posn,
        *ntp_abs_count, nt_seq_count, nt_list_posn, nt_hit_count;
typedef uint64_t             nt_model_size;
typedef uint8_t              nt_rel_seq_len, nt_rel_count, nt_rel_seq_posn,
        *ntp_rel_count, uchar,
        nt_stack_size, *ntp_stack_size, nt_element_count, nt_stack_idist,
        *ntp_stack_idist, nt_seg_size, *ntp_seg_size;
typedef int16_t              nt_s_rel_count, nt_s_stack_size, nt_s_stack_idist;

typedef int64_t              nt_file_size, nt_q_size, nt_int;
typedef char                 nt;
typedef const char          *ntp_seq;
typedef FILE                *ntp_file;
typedef char                 nt_rt_bytes[NUM_RT_BYTES + 1];
#define MAX_STACK_DIST       UCHAR_MAX

/*
 * DEBUG customization
 */

//main debug switch: comment out to switch off all debug traces
//#define DEBUG_ON

//if defined don't use register variables even when DEBUG_ON
//#define NO_REGISTER

//if defined time-consuming semantic checks are disabled,
//such as list duplicate-entry checking in ntp_list_init_with_tag
#define NO_FULL_CHECKS

// enumerate and stringify debug topics, used for both
// immediate debug traces (output to stdout) and when
// using in DEBUG_ON mode;
// the list below toggles functionality-specific debug traces

// debug topic is required (y==switched on, any other literal==switched off);
// y/n switches always apply for DEBUG_NOW, and to COMMIT_DEBUG when enabled
#define y

#define FOREACH_TOPIC(ACTION)                \
        ACTION(FE,                      y)   \
	ACTION(FE_WS,			y)   \
        ACTION(MAIN,                    y)   \
        ACTION(SCAN,                    n)   \
        ACTION(FILTER,                  y)   \
        ACTION(DEBUG,                   n)   \
        ACTION(MEM,                     n)   \
        ACTION(MEM_TAG,                 n)   \
        ACTION(MODEL,                   y)   \
        ACTION(SEQ,                     y)   \
        ACTION(SEARCH_SEQ,              y)   \
        ACTION(SEQ_BP_CACHE,            y)   \
        ACTION(LIST,                    n)   \
        ACTION(SIMCLIST,                n)   \
        ACTION(TESTS,                   n)   \
	ACTION(BATCH_TESTS,		y)   \
        ACTION(UTILS,                   y)   \
        ACTION(DISPATCH,                y)   \
        ACTION(ALLOCATE,                y)   \
        ACTION(SCHED,                   y)   \
        ACTION(DATASTORE, 		y)   \
        ACTION(INTERFACE,               y)   \
        ACTION(MFE,                     y)   \

#define PRI_ACTION_STRING_MAX_LENGTH 	"12"	// maximum action string length for printf format spec

#define GENERATE_ENUM(ENUM,     STATUS) ENUM,
#define GENERATE_STRING(STRING, STATUS) #STRING,
#define GENERATE_STATUS(STRING, STATUS) {#STRING, #STATUS},

typedef enum {
	FOREACH_TOPIC (GENERATE_ENUM)
} DEBUG_TOPIC;
extern const char *DEBUG_TOPIC_STRING[];
extern const char *DEBUG_TOPIC_STATUS[][2];

typedef enum {
	REPORT_NONE = 0, REPORT_ERRORS = 1, REPORT_WARNINGS = 2, REPORT_INFO = 3
} DEBUG_REPORT_LEVEL;

#define DEBUG_LEVEL         REPORT_INFO

// function prototype and macros for immediate (stdout) debug traces
void commit_d_now (DEBUG_REPORT_LEVEL level, DEBUG_TOPIC topic, char *msg);
#define DEBUG_NOW(a, b, c) (commit_d_now ((a), (b), (c)))
#define DEBUG_NOW1(a, b, c, p1)  { const char *_c=(c); char _msg[MAX_MSG_LEN]; sprintf (_msg, _c, (p1)); commit_d_now ((a), (b), _msg); }
#define DEBUG_NOW2(a, b, c, p1, p2) { const char *_c=(c); char _msg[MAX_MSG_LEN]; sprintf (_msg, _c, (p1), (p2)); commit_d_now ((a), (b), _msg); }
#define DEBUG_NOW3(a, b, c, p1, p2, p3) { const char *_c=(c); char _msg[MAX_MSG_LEN]; sprintf (_msg, _c, (p1), (p2), (p3)); commit_d_now ((a), (b), _msg); }
#define DEBUG_NOW4(a, b, c, p1, p2, p3, p4) { const char *_c=(c); char _msg[MAX_MSG_LEN]; sprintf (_msg, _c, (p1), (p2), (p3), (p4)); commit_d_now ((a), (b), _msg); }
#define DEBUG_NOW5(a, b, c, p1, p2, p3, p4, p5) { const char *_c=(c); char _msg[MAX_MSG_LEN]; sprintf (_msg, _c, (p1), (p2), (p3), (p4), (p5)); commit_d_now ((a), (b), _msg); }

#define DEBUG_DATE_TIME_FMT "%Y%b%d.%H%M%S"     // limited to 99 chars. see time_str in initialize_debug

// maximum length of commit messages used in DEBUG_ and COMMIT_
// macros (when using sprintf); also used for debug_mem_entry,
// debug_fn, and malloc_t
#define MAX_MSG_LEN 300

// macros and settings applicable only when
// DEBUG_ON mode is set
#ifdef DEBUG_ON

#define DEBUG_MEM                       // memory utilities-specific debug toggle
#define REGISTER                        // no register variables in DEBUG_ON mode
#define SEARCH_SEQ_DETAIL               // detailed traces for SEARCH_SEQ  (see ACTIONs below)
#define INTERFACE_DETAIL                // detailed traces for INTERFACE   (see ACTIONs below)

#include <stdio.h>
#include <time.h>

#define DEBUG_FN_PREFIX     "RNA2.debug."       	    // debug file and output settings
#define DEBUG_FN_SUFFIX_FMT DEBUG_DATE_TIME_FMT     	// use date/time format as fn suffix

bool is_debug_initialized();
bool initialize_debug();
void finalize_debug();
void persist_debug();
void commit_d (DEBUG_REPORT_LEVEL level, DEBUG_TOPIC topic, char *trace,
               bool is_key, bool no_newline);

#ifdef DEBUG_MEM

void *malloc_d (size_t alloc_size, char *alloc_reason);
void free_d (void *mem, const char *free_reason);
bool malloc_cp (ulong *num_entries, ulong *alloc_size);
void flush_mem_d();
static inline void *MALLOC_D (size_t alloc_size, char *alloc_reason) {
	return malloc_d (alloc_size, alloc_reason);
}
static inline void  FREE_D (void *mem, const char *free_reason) {
	free_d (mem, free_reason);
}

#define MALLOC_CP(e, s) (malloc_cp((e),(s)))
#define FLUSH_MEM_D() (flush_mem_d())
#else
static inline void *MALLOC_D (size_t alloc_size, char *alloc_reason) {
	return malloc (alloc_size);
}
static inline void  FREE_D (void *mem, const char *free_reason) {
	free (mem);
}
#define FLUSH_MEM_D() ({do {} while (0);})
#endif

// convenience macros
#define COMMIT_D(a, b, c, d) (commit_d ((a), (b), (c), (d), false))
#define COMMIT_D1(a, b, c, p1, d)  { const char *_c=(c); char _msg[MAX_MSG_LEN]; sprintf (_msg, _c, (p1)); commit_d ((a), (b), _msg, (d), false); }
#define COMMIT_D2(a, b, c, p1, p2, d)  { const char *_c=(c); char _msg[MAX_MSG_LEN]; sprintf (_msg, _c, (p1), (p2)); commit_d ((a), (b), _msg, (d), false); }
#define COMMIT_D3(a, b, c, l, p1, p2, p3, d)  { const char *_c=(c); char _msg[(l)+1]; sprintf (_msg, _c, (p1), (p2), (p3)); commit_d ((a), (b), _msg, (d), false); }
#define COMMIT_D_NNL(a, b, c, d) (commit_d ((a), (b), (c), (d), true))

#else

// macros and settings for when DEBUG_ON is not set
#ifdef NO_REGISTER
	#define REGISTER
#else
	#define REGISTER             register           // qualify register variables when not in DEBUG_ON mode
#endif

#define COMMIT_D(a, b, c, d)  ({do {} while (0);})
#define COMMIT_D1(a, b, c, p1, d)  ({do {} while (0);})
#define COMMIT_D2(a, b, c, p1, p2, d)  ({do {} while (0);})
#define COMMIT_D3(a, b, c, l, p1, p2, p3, d)  ({do {} while (0);})
#define COMMIT_D_NNL(a, b, c, d)  ({do {} while (0);})
#define FLUSH_MEM_D() ({do {} while (0);})

static inline void *MALLOC_D (size_t alloc_size, char *alloc_reason) {
	return malloc (alloc_size);
}
static inline void  FREE_D (void *mem, const char *free_reason) {
	free (mem);
}
#endif

#define MALLOC_DEBUG(s, r) (MALLOC_D((s), (r)))                         // syntactic sugaring for malloc/free/commit
#define FREE_DEBUG(s, r) (FREE_D((s), (r)))
#define COMMIT_DEBUG(l, t, r, i) COMMIT_D((l), (t), (r), (i))
#define COMMIT_DEBUG1(l, t, r, p1, i) COMMIT_D1((l), (t), (r), (p1), (i))
#define COMMIT_DEBUG2(l, t, r, p1, p2, i) COMMIT_D2((l), (t), (r), (p1), (p2), (i))
#define COMMIT_DEBUG3(l, t, r, len, p1, p2, p3, i) COMMIT_D3((l), (t), (r), (len), (p1), (p2), (p3), (i))
#define COMMIT_DEBUG_NNL(l, t, r, i) COMMIT_D_NNL((l), (t), (r), (i))

#define FLUSH_MEM_DEBUG() (FLUSH_MEM_D())

void *malloc_t (size_t alloc_size, uchar alloc_tag);
bool  free_t (void *mem, uchar alloc_tag);

#define MALLOC_TAG(s, t) malloc_t ((s), (t))
#define FREE_TAG(s, t) free_t ((s), (t))

#ifdef MULTITHREADED_ON
	bool prepare_threaded_free_t_all (unsigned long slot);
	#define PREPARE_THREADED_FREE_T_ALL(s) prepare_threaded_free_t_all(s)
	bool free_t_all (unsigned long slot);
	#define FREE_TAG_ALL(s) free_t_all(s)
#else
	void  free_t_all();
	#define FREE_TAG_ALL() free_t_all ()
#endif

// type-safe min function
#define SAFE_MIN(min_a,min_b) ({ __typeof__ (min_a) _min_a = (min_a); __typeof__ (min_b) _min_b = (min_b); _min_a < _min_b ? _min_a : _min_b; })
#define SAFE_MAX(max_a,max_b) ({ __typeof__ (max_a) _max_a = (max_a); __typeof__ (max_b) _max_b = (max_b); _max_a > _max_b ? _max_a : _max_b; })

unsigned long long get_real_time();
void get_real_time_bytes (nt_rt_bytes *rt_bytes);
void convert_timebytes_to_dec_representation (nt_rt_bytes *src,
                                        nt_rt_bytes *dst);
void convert_dec_to_timebytes_representation (nt_rt_bytes *src,
                                        nt_rt_bytes *dst);
void convert_rt_bytes_to_string (nt_rt_bytes *rt_bytes,
                                 nt_rt_bytes *new_rt_char);
void convert_string_to_rt_bytes (nt_rt_bytes *new_rt_char,
                                 nt_rt_bytes *rt_bytes);
unsigned long long get_total_system_memory();
void sleep_ms (int milliseconds);
void reset_timer();
float get_timer();

#ifdef _WIN32
	#ifdef _WIN64
		#define PRI_SIZET PRIu64
	#else
		#define PRI_SIZET PRIu32
	#endif
#else
	#define PRI_SIZET "zu"
#endif

void GET_SUBSTRING (const char *string, short position, short length,
                    char *substring);

#define MAX_FILENAME_LENGTH 250                         // FILENAME_MAX too large
#define RNA_ALPHA_SIZE 4

#ifndef HOST_NAME_MAX
	#define HOST_NAME_MAX   BSON_HOST_NAME_MAX              // undefined under MinGW
#endif

#endif //RNA_UTIL_H
