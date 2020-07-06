#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "util.h"
#include "simclist.h"
#ifdef WIN32
	#include <windows.h>
#elif _POSIX_C_SOURCE>=199309L
	#include <time.h>   // nanosleep
#else
	#include <unistd.h> // usleep
#endif
#include <pthread.h>

/*
 * define mutex to lock on localtime, and optionally memory
 * de/allocation, when MULTITHREADED_ON is set
 */
static bool is_mutex_init = false;
pthread_mutex_t localtime_mutex;

// synchronize immediate debug activities
static pthread_spinlock_t commit_now_spinlock;

/*
 * experimental feature for latent deallocation of
 * memory. by default not set
 */
#ifdef MULTITHREADED_ON
	pthread_mutex_t mem_tag_list_mutex;
	pthread_mutex_t debug_mem_traces_mutex;
	
	void *mem_tag_list_destruction_target[MAX_THREADS + 1];
	
	ulong mem_tag_list_destruction_num = 0;
	
	ulong  malloc_t_flag = 0;
#endif

/*
 * generate DEBUG topic strings from
 * defines in .h
 */
const char *DEBUG_TOPIC_STRING[] = {
	FOREACH_TOPIC (GENERATE_STRING)
};

const char *DEBUG_TOPIC_STATUS[][2] = {
	FOREACH_TOPIC (GENERATE_STATUS)
};

/*
 * set up DEBUG trace file and data structures
 * when DEBUG_ON is defined; optionally also
 * trace memory de/allocation for debugging
 */
#ifdef DEBUG_ON
#include "simclist.h"

ntp_file debug_file = NULL;
ntp_list debug_traces = NULL;
bool is_debug_init = false;

typedef struct {
	DEBUG_REPORT_LEVEL level;
	DEBUG_TOPIC topic;
	time_t timestamp;
	char *trace;
	bool is_key;
	bool has_newline;
} nt_debug_trace, *ntp_debug_trace;

#ifdef DEBUG_MEM
typedef struct {
	char   alloc_reason[MAX_MSG_LEN];
	void  *alloc_address;
	size_t alloc_size;
} nt_debug_mem_entry, *ntp_debug_mem_entry;

ntp_list debug_mem_traces = NULL;
#endif
#endif

typedef struct {
	uchar tag;
	nt_list list;
} nt_tagged_mem_entry, *ntp_tagged_mem_entry;

ntp_list mem_tag_list = NULL;

#ifdef _WIN32
	#include <winnt.h>
	#include <afxres.h>
	
	LARGE_INTEGER StartingTime, EndingTime, ElapsedMicroseconds;
	LARGE_INTEGER Frequency;
#else
	#define _XOPEN_SOURCE 700   // POSIX 2008
	
	#include <unistd.h>
	#include <time.h>
	
	struct timespec start, finish;
#endif

/*
 * convenience data structures for translating
 * timebytes to/from decimal representation
 */
static unsigned int tohexbytes[256] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 64,
                                       65, 66, 67, 68, 69, 70, 71, 72, 73, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 112, 113, 114, 115, 116,
                                       117, 118, 119, 120, 121, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 256, 257, 258,
                                       259, 260, 261, 262, 263, 264, 265, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 304,
                                       305, 306, 307, 308, 309, 310, 311, 312, 313, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 336, 337, 338, 339, 340, 341, 342, 343, 344,
                                       345, 352, 353, 354, 355, 356, 357, 358, 359, 360, 361, 368, 369, 370, 371, 372, 373, 374, 375, 376, 377, 384, 385, 386, 387, 388, 389, 390,
                                       391, 392, 393, 400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 512, 513, 514, 515, 516, 517, 518, 519, 520, 521, 528, 529, 530, 531, 532,
                                       533, 534, 535, 536, 537, 544, 545, 546, 547, 548, 549, 550, 551, 552, 553, 560, 561, 562, 563, 564, 565, 566, 567, 568, 569, 576, 577, 578,
                                       579, 580, 581, 582, 583, 584, 585, 592, 593, 594, 595, 596, 597
                                      };

static unsigned int todecbytes[122] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0, 0, 0, 0, 0, 0, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 0, 0, 0, 0,
                                       0, 0, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 0, 0, 0, 0, 0, 0, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 0, 0, 0, 0, 0, 0, 50, 51, 52, 53, 54, 55, 56, 57,
                                       58, 59, 0, 0, 0, 0, 0, 0, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 0, 0, 0, 0, 0, 0, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79
                                      };

/*
 * static inline replacements for memset/memcpy - silences google sanitizers
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
 * initialize_utils:
 *          initialize mutex. mandatory before calling utils functions
 *
 * returns: boolean success flag
 */
bool initialize_utils() {
        if (!is_mutex_init && pthread_spin_init (&commit_now_spinlock, PTHREAD_PROCESS_PRIVATE)) {
                printf ("could not initialize immediate commit spinlock for utils\n");
                return false;
        }

	if (is_mutex_init) {
		DEBUG_NOW (REPORT_WARNINGS, UTILS, "mutex already initialized");
		return false;
	}

	#ifdef MULTITHREADED_ON
	
	if (pthread_mutex_init (&mem_tag_list_mutex, NULL) != 0) {
		DEBUG_NOW (REPORT_ERRORS, UTILS, "cannot initialize mem_tag_list_mutex");
		DEBUG_NOW (REPORT_INFO, UTILS, "finalizing commit spinlock");
		pthread_spin_destroy (&commit_now_spinlock);
		return false;
	}
	
	if (pthread_mutex_init (&debug_mem_traces_mutex, NULL) != 0) {
		DEBUG_NOW (REPORT_ERRORS, UTILS, "cannot initialize debug_mem_traces_mutex");
		DEBUG_NOW (REPORT_INFO, UTILS, "destroying mem_tag_list_mutex");
		pthread_mutex_destroy (&mem_tag_list_mutex);
		DEBUG_NOW (REPORT_INFO, UTILS, "finalizing commit spinlock");
		pthread_spin_destroy (&commit_now_spinlock);
		return false;
	}
	
	for (uchar i = 0; i <= MAX_THREADS; i++) {
		mem_tag_list_destruction_target[i] = NULL;
	}
	
	mem_tag_list_destruction_num = 0;
	#endif
	
	if (pthread_mutex_init (&localtime_mutex, NULL) != 0) {
		DEBUG_NOW (REPORT_ERRORS, UTILS, "cannot initialize localtime_mutex");
		#ifdef MULTITHREADED_ON
		DEBUG_NOW (REPORT_INFO, UTILS, "destroying mem_tag_list_mutex");
		pthread_mutex_destroy (&mem_tag_list_mutex);
		DEBUG_NOW (REPORT_INFO, UTILS, "debug_mem_traces_mutex mem_tag_list_mutex");
		pthread_mutex_destroy (&debug_mem_traces_mutex);
		DEBUG_NOW (REPORT_INFO, UTILS, "finalizing commit spinlock");
		pthread_spin_destroy (&commit_now_spinlock);
		#endif
		return false;
	}
	
	is_mutex_init = true;
	return true;
}

/*
 * finalize_utils:
 *          finalize mutex structures
 */
void finalize_utils() {
	if (!is_mutex_init) {
		DEBUG_NOW (REPORT_WARNINGS, UTILS, "mutex not yet initialized");
		return;
	}
	
	if (
                    #ifdef MULTITHREADED_ON
	                    pthread_mutex_destroy (&mem_tag_list_mutex) != 0 ||
	                    pthread_mutex_destroy (&debug_mem_traces_mutex) != 0 ||
                    #endif
	                    pthread_mutex_destroy (&localtime_mutex) != 0) {
		DEBUG_NOW (REPORT_ERRORS, UTILS, "cannot finalize mutex");
	}
	
	if (pthread_spin_destroy (&commit_now_spinlock)) {
		DEBUG_NOW (REPORT_ERRORS, UTILS, "cannot finalize immediate commit spinlock");
	}

	is_mutex_init = false;
}

// credit f/sleep_ms: https://stackoverflow.com/questions/1157209/is-there-an-alternative-sleep-function-in-c-to-milliseconds
void sleep_ms (int milliseconds) {
	#ifdef WIN32
	Sleep (milliseconds);
	#elif _POSIX_C_SOURCE >= 199309L
	struct timespec ts;
	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = (milliseconds % 1000) * 1000000;
	nanosleep (&ts, NULL);
	#else
	usleep (milliseconds * 1000);
	#endif
}

void reset_timer() {
	#ifdef _WIN32
	QueryPerformanceFrequency (&Frequency);
	QueryPerformanceCounter (&StartingTime);
	#else
	clock_gettime (CLOCK_REALTIME, &start);
	#endif
}

float get_timer() {
	#ifdef _WIN32
	QueryPerformanceCounter (&EndingTime);
	ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;
	ElapsedMicroseconds.QuadPart *= 1000000;
	ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;
	return ElapsedMicroseconds.QuadPart / 1000000.0f;
	#else
	clock_gettime (CLOCK_REALTIME, &finish);
	
	if ((finish.tv_nsec - start.tv_nsec) < 0) {
		return (finish.tv_sec - start.tv_sec - 1) + (1000000000.0f - start.tv_nsec +
		                                        finish.tv_nsec) / 1000000000.0f;
	}
	
	else {
		return (finish.tv_sec - start.tv_sec) + (finish.tv_nsec - start.tv_nsec) /
		       1000000000.0f;
	}
	
	#endif
}

/*
 * commit_d_now:
 *          log a message for a given topic and reporting_level to stdout
 *
 * args:    DEBUG_REPORT_LEVEL reporting_level,
 *          DEBUG_TOPIC topic,
 *          string message of arbitrary length
 */
void commit_d_now (DEBUG_REPORT_LEVEL reporting_level, DEBUG_TOPIC topic,
                   char *msg) {
	if (DEBUG_LEVEL >= reporting_level && msg && (strlen (msg) > 0)) {
		if (DEBUG_TOPIC_STATUS[topic][1][0] != 'y') {
			return;
		}

		if (pthread_spin_lock (&commit_now_spinlock)) {
			printf ("cannot lock on spinlock for immediate commit\n");
			fflush (stdout);
			return;
		}

		time_t ts = time (NULL);
		
		if (ts == (time_t) (-1)) {
			printf ("cannot get timestamp for debug message: level %d, msg '%s'\n",
			        reporting_level, msg);
			fflush (stdout);
			if (pthread_spin_unlock (&commit_now_spinlock)) {
				printf ("cannot unlock spinlock for immediate commit\n");
				fflush (stdout);
			}
			return;
		}
		
		#ifndef VALGRIND_TEST
		REGISTER
		char time_str[100];
		time_str[0] = '\0';
		struct tm *tm = localtime (&ts);
		
		if (tm) {
			strftime (time_str, sizeof (time_str), DEBUG_DATE_TIME_FMT, tm);
		}
		
		uchar i = 0;
		
		while (i < strlen (time_str)) {
			time_str[i] = (char) toupper (time_str[i]);
			++i;
		}
		
		#endif
		REGISTER
		char level[2];
		level[0] = '\0';
		level[1] = '\0';
		
		switch (reporting_level) {
			case REPORT_ERRORS   :
				level[0] = 'E';
				break;
				
			case REPORT_WARNINGS :
				level[0] = 'W';
				break;
				
			case REPORT_INFO     :
				level[0] = 'I';
				break;
				
			default              :
				break;
		}
		
		#ifndef VALGRIND_TEST
		printf ("%s %s %-"PRI_ACTION_STRING_MAX_LENGTH"s %s\n", time_str, level,
		        DEBUG_TOPIC_STRING[topic], msg);
		#else
		printf ("%s %-"PRI_ACTION_STRING_MAX_LENGTH"s %s\n", level,
		        DEBUG_TOPIC_STRING[topic], msg);
		#endif
		fflush (stdout);

		if (pthread_spin_unlock (&commit_now_spinlock)) {
			printf ("cannot unlock spinlock for immediate commit\n");
			fflush (stdout);
		}
	}
}

#ifdef DEBUG_ON
/*
 * debug functions
 *
 * log message and allocation events to debug file;
 * any committed messages are only persisted to the
 * filesystem when persist_debug is invoked
 */
#ifdef DEBUG_MEM
int debug_mem_entry_seeker (const void *debug_mem_list_el,
                            const void *alloc_address) {
	ntp_debug_mem_entry mem_entry = (ntp_debug_mem_entry)debug_mem_list_el;
	
	if (mem_entry->alloc_address == (void *)alloc_address) {
		return 1;
	}
	
	return 0;
}
#endif

bool is_debug_initialized() {
	return is_debug_init;
}

bool initialize_debug() {
	finalize_debug();
	
	if (DEBUG_LEVEL > REPORT_NONE) {
		// create debug list
		debug_traces = malloc (sizeof (nt_list));
		
		if (!debug_traces || (list_init (debug_traces) < 0)) {
			if (debug_traces) {
				free (debug_traces);
				debug_traces = NULL;
			}
			
			return false;
		}
		
		#ifndef VALGRIND_TEST
		// get filename timestamp
		time_t tt = time (NULL);
		
		if (tt == (time_t) (-1)) {
			list_destroy (debug_traces);
			free (debug_traces);
			debug_traces = NULL;
			return false;
		}
		
		struct tm *tm = localtime (&tt);
		
		if (!tm) {
			list_destroy (debug_traces);
			free (debug_traces);
			debug_traces = NULL;
			return false;
		}
		
		char time_str[100];
		
		if (!strftime (time_str, sizeof (time_str), DEBUG_FN_SUFFIX_FMT, tm)) {
			list_destroy (debug_traces);
			free (debug_traces);
			debug_traces = NULL;
			return false;
		}
		
		uchar i = 0;
		
		while (i < strlen (time_str)) {
			time_str[i] = (char)toupper (time_str[i]);
			++i;
		}
		
		#endif
		// open file in append mode
		char debug_fn[MAX_MSG_LEN];
		strcpy (debug_fn, DEBUG_FN_PREFIX);
		#ifndef VALGRIND_TEST
		strcat (debug_fn, time_str);
		#endif
		debug_file = fopen (debug_fn, "a");
		
		if (!debug_file) {
			list_destroy (debug_traces);
			free (debug_traces);
			debug_traces = NULL;
			return false;
		}
		
		#ifdef DEBUG_MEM
		debug_mem_traces = malloc (sizeof (nt_list));
		
		if (!debug_mem_traces  || (list_init (debug_mem_traces) < 0)) {
			if (debug_mem_traces) {
				free (debug_mem_traces);
				debug_mem_traces = NULL;
			}
			
			list_destroy (debug_traces);
			free (debug_traces);
			debug_traces = NULL;
			fclose (debug_file);
			debug_file = NULL;
			return false;
		}
		
		list_attributes_seeker (debug_mem_traces, &debug_mem_entry_seeker);
		#endif
		is_debug_init = true;
	}
	
	return true;
}

void finalize_debug() {
	is_debug_init = false;
	
	if (debug_file) {
		fclose (debug_file);
		debug_file = NULL;
	}
	
	if (debug_traces) {
		list_iterator_start (debug_traces);
		
		while (list_iterator_hasnext (debug_traces)) {
			free (list_iterator_next (debug_traces));
		}
		
		list_iterator_stop (debug_traces);
		list_destroy (debug_traces);
		free (debug_traces);
		debug_traces = NULL;
	}
	
	#ifdef DEBUG_MEM
	
	if (debug_mem_traces) {
		list_iterator_start (debug_mem_traces);
		
		while (list_iterator_hasnext (debug_mem_traces)) {
			free (list_iterator_next (debug_mem_traces));
		}
		
		list_iterator_stop (debug_mem_traces);
		list_destroy (debug_mem_traces);
		free (debug_mem_traces);
		debug_mem_traces = NULL;
	}
	
	#endif
}

void commit_d (DEBUG_REPORT_LEVEL level, DEBUG_TOPIC topic, char *trace,
               bool is_key, bool no_newline) {
	if (DEBUG_LEVEL >= level && debug_traces && trace && (strlen (trace) > 0)) {
		if (DEBUG_TOPIC_STATUS[topic][1][0] != 'y') {
			return;
		}
		
		time_t ts = time (NULL);
		
		if (ts == (time_t) (-1)) {
			DEBUG_NOW2 (REPORT_ERRORS, DEBUG,
			            "cannot get timestamp for debug trace: level %d, trace '%s'", level,
			            trace);
			return;
		}
		
		ntp_debug_trace debug_trace = malloc (sizeof (nt_debug_trace));
		
		if (!debug_trace) {
			DEBUG_NOW2 (REPORT_ERRORS, DEBUG,
			            "cannot allocate memory for debug trace structure: level %d, trace '%s'",
			            level, trace);
			return;
		}
		
		debug_trace->trace = malloc (sizeof (char) * (strlen (trace) + 1));
		
		if (!debug_trace->trace) {
			DEBUG_NOW2 (REPORT_ERRORS, DEBUG,
			            "cannot allocate memory for debug trace string: level %d, trace '%s'",
			            level, trace);
			free (debug_trace);
			return;
		}
		
		debug_trace->level = level;
		debug_trace->topic = topic;
		debug_trace->timestamp = ts;
		strcpy (debug_trace->trace, trace);
		debug_trace->is_key = is_key;
		debug_trace->has_newline = !no_newline;
		
		if (list_append (debug_traces, debug_trace) < 0) {
			DEBUG_NOW2 (REPORT_ERRORS, DEBUG,
			            "failed to append debug trace: level %d, trace '%s'", level, trace);
			free (debug_trace->trace);
			free (debug_trace);
		}
	}
}

void persist_debug() {
	if (debug_traces && debug_file) {
		// pad topic strings up to the length of the longest
		uchar longest_topic_len = 0, this_topic_len;
		
		if (sizeof (DEBUG_TOPIC_STRING)) {
			for (uchar i = 0;
			     i < sizeof (DEBUG_TOPIC_STRING) / sizeof (DEBUG_TOPIC_STRING[0]); i++) {
				this_topic_len = (uchar)strlen (DEBUG_TOPIC_STRING[i]);
				
				if (this_topic_len > longest_topic_len) {
					longest_topic_len = this_topic_len;
				}
			}
		}
		
		list_iterator_start (debug_traces);
		
		while (list_iterator_hasnext (debug_traces)) {
			ntp_debug_trace debug_trace = list_iterator_next (debug_traces);
			
			if (debug_trace->has_newline) {
				#ifndef VALGRIND_TEST
				char time_str[100];
				time_str[0] = '\0';
				struct tm *tm = localtime (&debug_trace->timestamp);
				
				if (tm) {
					strftime (time_str, sizeof (time_str), DEBUG_FN_SUFFIX_FMT, tm);
				}
				
				uchar i = 0;
				
				while (i < strlen (time_str)) {
					time_str[i] = (char) toupper (time_str[i]);
					++i;
				}
				
				#endif
				char level[2];
				level[0] = '\0';
				level[1] = '\0';
				
				switch (debug_trace->level) {
					case REPORT_ERRORS   :
						level[0] = 'E';
						break;
						
					case REPORT_WARNINGS :
						level[0] = 'W';
						break;
						
					case REPORT_INFO     :
						level[0] = 'I';
						break;
						
					default              :
						break;
				}
				
				// pad topic string up to length of longest
				this_topic_len = (uchar) strlen (DEBUG_TOPIC_STRING[debug_trace->topic]);
				char topic_string[longest_topic_len + 1];
				sprintf (topic_string, "%s", DEBUG_TOPIC_STRING[debug_trace->topic]);
				
				while (this_topic_len < longest_topic_len) {
					topic_string[this_topic_len] = ' ';
					++this_topic_len;
				}
				
				topic_string[this_topic_len] = '\0';
				// dump
				char key_indicator = ' ';
				
				if (debug_trace->is_key) {
					key_indicator = '>';
				}
				
				#ifndef VALGRIND_TEST
				
				if (fprintf (debug_file, "%s %s %s %c%s\n", time_str, level, topic_string,
				             key_indicator, debug_trace->trace) < 0) {
					printf ("%s %s %s %s\n", time_str, level, topic_string, debug_trace->trace);
				}
				
				#else
				
				if (fprintf (debug_file, "%s %s %c%s\n", level, topic_string, key_indicator,
				             debug_trace->trace) < 0) {
					printf ("%s %s %s\n", level, topic_string, debug_trace->trace);
				}
				
				#endif
			}
			
			else {
				// if no newline was requested, simply dump debug_trace message verbatim, with no other information
				if (fprintf (debug_file, "%s", debug_trace->trace) < 0) {
					printf ("%s", debug_trace->trace);
				}
			}
			
			free (debug_trace->trace);
			free (debug_trace);
		}
		
		list_iterator_stop (debug_traces);
		list_destroy (debug_traces);
	}
}

#ifdef DEBUG_MEM
/*
 * debug memory
 */
void *malloc_d (size_t alloc_size, char *alloc_reason) {
	#ifdef MULTITHREADED_ON

	if (pthread_mutex_lock (&debug_mem_traces_mutex) == 0) {
	#endif
	
		if (debug_mem_traces && alloc_size) {
			void *mem = malloc (alloc_size);
			
			if (mem) {
				// only commit debug trace if alloc_reason provided (not NULL)
				if (alloc_reason) {
					uchar len = 50;
					len += strlen (alloc_reason);
					COMMIT_DEBUG3 (REPORT_INFO, MEM, "allocated %u (%p) for '%s'", len, alloc_size,
					               mem, alloc_reason, false);
				}
				
				ntp_debug_mem_entry debug_mem_entry = malloc (sizeof (nt_debug_mem_entry));
				
				if (!debug_mem_entry) {
					uchar len = 50;
					
					if (alloc_reason) {
						len += strlen (alloc_reason);
					}
					
					char msg[len];
					
					if (alloc_reason) {
						sprintf (msg,
						         "could not allocate memory for debug_mem_entry ('%s') in malloc_d",
						         alloc_reason);
					}
					
					else {
						sprintf (msg, "could not allocate memory for debug_mem_entry in malloc_d");
					}
					
					#ifdef MULTITHREADED_ON
					pthread_mutex_unlock (&debug_mem_traces_mutex);
					#endif
					COMMIT_DEBUG (REPORT_ERRORS, MEM, msg, false);
					return mem;
				}
				
				debug_mem_entry->alloc_address = mem;
				debug_mem_entry->alloc_size = alloc_size;
				
				if (alloc_reason) {
					strcpy (debug_mem_entry->alloc_reason, alloc_reason);
				}
				
				else {
					debug_mem_entry->alloc_reason[0] = '\0';
				}
				
				if (list_append (debug_mem_traces, debug_mem_entry) < 0) {
					free (debug_mem_entry);
					DEBUG_NOW (REPORT_ERRORS, MEM,
					           "could not append debug_mem_entry to debug_mem_traces");
				}
				
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&debug_mem_traces_mutex);
				#endif
				return mem;
			}
			
			else {
				uchar len = 50;
				
				if (alloc_reason) {
					len += strlen (alloc_reason);
				}
				
				char msg[len];
				
				if (alloc_reason) {
					sprintf (msg, "could not allocate %"PRI_SIZET" for '%s' in malloc_d",
					         alloc_size, alloc_reason);
				}
				
				else {
					sprintf (msg, "could not allocate %"PRI_SIZET" in malloc_d", alloc_size);
				}
				
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&debug_mem_traces_mutex);
				#endif
				COMMIT_DEBUG (REPORT_ERRORS, MEM, msg, false);
				return NULL;
			}
		}
		
		else
			if (alloc_size) {
				void *mem = malloc (alloc_size);
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&debug_mem_traces_mutex);
				#endif
				return mem;
			}
			
		#ifdef MULTITHREADED_ON
		pthread_mutex_unlock (&debug_mem_traces_mutex);
		#endif
		return NULL;
		#ifdef MULTITHREADED_ON
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, MEM,
		           "cannot acquire lock on debug_mem_traces_mutex");
		return NULL;
	}
	
		#endif
}

void free_d (void *mem, const char *free_reason) {
	#ifdef MULTITHREADED_ON

	if (pthread_mutex_lock (&debug_mem_traces_mutex) == 0) {
	#endif
	
		if (!debug_mem_traces && mem) {
			free (mem);
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&debug_mem_traces_mutex);
			#endif
			return;
		}
		
		if (!mem) {
			uchar len = 27;
			
			if (free_reason) {
				len += strlen (free_reason);
			}
			
			char err[len];
			
			if (free_reason) {
				sprintf (err, "attempt to free NULL mem at '%s' in free_d", free_reason);
			}
			
			else {
				sprintf (err, "attempt to free NULL mem in free_d");
			}
			
			DEBUG_NOW (REPORT_ERRORS, MEM, err);
		}
		
		else {
			ntp_debug_mem_entry debug_mem_entry = (ntp_debug_mem_entry) list_seek (
			                                        debug_mem_traces, mem);
			                                        
			if (!debug_mem_entry) {
				uchar len = 30;
				
				if (free_reason) {
					len += strlen (free_reason);
				}
				
				char err[len];
				
				if (free_reason) {
					sprintf (err, "freed unknown mem (%p) at '%s' in free_d", mem, free_reason);
				}
				
				else {
					sprintf (err, "freed unknown mem (%p) in free_d", mem);
				}
				
				DEBUG_NOW (REPORT_ERRORS, MEM, err);
			}
			
			else {
				uchar len = 79;
				
				if (free_reason) {
					len += strlen (free_reason);
					
					if (debug_mem_entry->alloc_reason[0] != '\0') {
						len += strlen (debug_mem_entry->alloc_reason);
					}
				}
				
				if (free_reason) {
					DEBUG_NOW3 (REPORT_ERRORS, MEM,
					            "mem previously allocated (%p, '%s') successfully freed ('%s')", len,
					            debug_mem_entry->alloc_address, debug_mem_entry->alloc_reason, free_reason);
				}
				
				list_delete (debug_mem_traces, debug_mem_entry);
				free (debug_mem_entry);
			}
			
			free (mem); // free memory, regardless of whether it is known or unknown to debug_mem_traces
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&debug_mem_traces_mutex);
			#endif
		}
		
		#ifdef MULTITHREADED_ON
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, MEM, "cannot acquire lock on debug_mem_traces_mutex");
	}
	
		#endif
}

/*
 * checkpoint MEM DEBUG current number of entries and total allocated size
 */
bool malloc_cp (ulong *num_entries, ulong *alloc_size) {
	if (!debug_mem_traces) {
		return false;
	}
	
	#ifdef MULTITHREADED_ON
	
	if (pthread_mutex_lock (&debug_mem_traces_mutex) == 0) {
	#endif
		*num_entries = 0;
		*alloc_size = 0;
		list_iterator_start (debug_mem_traces);
		
		while (list_iterator_hasnext (debug_mem_traces)) {
			ntp_debug_mem_entry debug_mem_entry = (ntp_debug_mem_entry) list_iterator_next (
			                                        debug_mem_traces);
			                                        
			if (debug_mem_entry) {
				(*num_entries)++;
				(*alloc_size) += debug_mem_entry->alloc_size;
			}
			
			else {
				DEBUG_NOW (REPORT_ERRORS, MEM, "found null debug_mem_entry");
				list_iterator_stop (debug_mem_traces);
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&debug_mem_traces_mutex);
				#endif
				return false;
			}
		}
		
		list_iterator_stop (debug_mem_traces);
		#ifdef MULTITHREADED_ON
		pthread_mutex_unlock (&debug_mem_traces_mutex);
		#endif
		return true;
		#ifdef MULTITHREADED_ON
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, MEM, "cannot acquire lock on debug_mem_traces_mutex");
		return false;
	}
	
		#endif
}

void flush_mem_d() {
	if (!debug_mem_traces) {
		return;
	}
	
	ulong num_items = 0, total_size = 0;
	list_iterator_start (debug_mem_traces);
	
	while (list_iterator_hasnext (debug_mem_traces)) {
		ntp_debug_mem_entry debug_mem_entry = (ntp_debug_mem_entry)list_iterator_next (
		                                        debug_mem_traces);
		int len = 45;
		
		if (debug_mem_entry->alloc_reason[0] != '\0') {
			len += strlen (debug_mem_entry->alloc_reason);
		}
		
		char tmp[100];
		sprintf (tmp, "%%p: %%s (%%%s) in flush_mem_d", PRI_SIZET);
		COMMIT_DEBUG3 (REPORT_INFO, MEM, tmp, len,
		               debug_mem_entry->alloc_address, debug_mem_entry->alloc_reason,
		               debug_mem_entry->alloc_size, false);
		num_items++;
		total_size += debug_mem_entry->alloc_size;
	}
	
	list_iterator_stop (debug_mem_traces);
	
	if (num_items) {
		COMMIT_DEBUG2 (REPORT_WARNINGS, MEM,
		               "%llu items (total size %llu) found in debug memory in flush_mem_d", num_items,
		               total_size, false);
	}
	
	else {
		COMMIT_DEBUG (REPORT_INFO, MEM, "no items found in debug memory in flush_mem_d",
		              false);
	}
	
	COMMIT_DEBUG (REPORT_INFO, MEM, "finished mem dump to debug in flush_mem_d",
	              false);
}
#endif
#endif

int tagged_mem_entry_seeker (const void *mem_tag_list_el, const void *mem_tag) {
	ntp_tagged_mem_entry tagged_mem_entry = (ntp_tagged_mem_entry)mem_tag_list_el;
	
	if (tagged_mem_entry->tag == * (uchar *)mem_tag) {
		return 1;
	}
	
	return 0;
}

int tagged_mem_entry_list_seeker (const void *mem_tag_list_el,
                                  const void *mem) {
	if (mem_tag_list_el == mem) {
		return 1;
	}
	
	return 0;
}

#ifdef MULTITHREADED_ON
inline ulong get_malloc_t_flag() {
	return malloc_t_flag;
}

inline void clear_malloc_t_flag() {
	malloc_t_flag = 0;
}
#endif

void *malloc_t (size_t alloc_size, uchar alloc_tag) {
	#ifdef MULTITHREADED_ON

	if (pthread_mutex_lock (&mem_tag_list_mutex) == 0) {
	#endif
	
		if (!mem_tag_list) {
			mem_tag_list = MALLOC_DEBUG (sizeof (nt_list), "mem_tag_list in malloc_t");
			
			if (mem_tag_list && (list_init (mem_tag_list) >= 0)) {
				COMMIT_DEBUG (REPORT_INFO, MEM_TAG,
				              "allocated and initialized mem_tag_list in malloc_t", true);
				              
				if (!list_attributes_seeker (mem_tag_list, &tagged_mem_entry_seeker)) {
					COMMIT_DEBUG (REPORT_INFO, MEM_TAG,
					              "tagged_mem_entry_seeker for mem_tag_list successfully set in malloc_t", false);
				}
				
				else {
					#ifdef MULTITHREADED_ON
					pthread_mutex_unlock (&mem_tag_list_mutex);
					#endif
					DEBUG_NOW (REPORT_ERRORS, MEM_TAG,
					           "could not set tagged_mem_entry_seeker for mem_tag_list");
					return NULL;
				}
			}
			
			else {
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&mem_tag_list_mutex);
				#endif
				DEBUG_NOW (REPORT_ERRORS, MEM_TAG,
				           "could not allocate or initialize mem_tag_list");
				return NULL;
			}
		}
		
		ntp_tagged_mem_entry tagged_mem_entry = (ntp_tagged_mem_entry) list_seek (
		                                        mem_tag_list, &alloc_tag);
		#ifdef DEBUG_MEM
		char msg[MAX_MSG_LEN];
		#endif
		bool revert = false;
		
		if (!tagged_mem_entry) {
			#ifdef DEBUG_MEM
			sprintf (msg, "tagged_mem_entry of mem_tag_list with tag %u in malloc_t",
			         alloc_tag);
			tagged_mem_entry = MALLOC_DEBUG (sizeof (nt_tagged_mem_entry), msg);
			#else
			tagged_mem_entry = MALLOC_DEBUG (sizeof (nt_tagged_mem_entry), NULL);
			#endif
			
			if (tagged_mem_entry && (list_init (&tagged_mem_entry->list) >= 0) &&
			    (list_append (mem_tag_list, tagged_mem_entry) >= 0)) {
				tagged_mem_entry->tag = alloc_tag;
				COMMIT_DEBUG1 (REPORT_INFO, MEM_TAG,
				               "allocated/initialized/appended tagged_mem_entry and list with tag %u in malloc_t",
				               alloc_tag,
				               false);
				               
				if (!list_attributes_seeker (&tagged_mem_entry->list,
				                             &tagged_mem_entry_list_seeker)) {
					COMMIT_DEBUG1 (REPORT_INFO, MEM_TAG,
					               "tagged_mem_entry_list_seeker for tagged_mem_entry with tag %u in mem_tag_list successfully set in malloc_t",
					               alloc_tag,
					               false);
				}
				
				else {
					DEBUG_NOW1 (REPORT_ERRORS, MEM_TAG,
					            "could not set tagged_mem_entry_list_seeker for tagged_mem_entry with tag %u in mem_tag_list",
					            alloc_tag);
					#ifdef DEBUG_MEM
					sprintf (msg,
					         "tagged_mem_entry of mem_tag_list with tag %u in malloc_t [failed to set tagged_mem_entry_list_seeker]",
					         alloc_tag);
					FREE_DEBUG (tagged_mem_entry, msg);
					#else
					FREE_DEBUG (tagged_mem_entry, NULL);
					#endif
					#ifdef MULTITHREADED_ON
					pthread_mutex_unlock (&mem_tag_list_mutex);
					#endif
					return NULL;
				}
				
				revert = true;
			}
			
			else {
				if (tagged_mem_entry) {
					#ifdef DEBUG_MEM
					sprintf (msg,
					         "tagged_mem_entry of mem_tag_list with tag %u in malloc_t [failed to initialize/append]",
					         alloc_tag);
					FREE_DEBUG (tagged_mem_entry, msg);
					#else
					FREE_DEBUG (tagged_mem_entry, NULL);
					#endif
				}
				
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&mem_tag_list_mutex);
				#endif
				DEBUG_NOW (REPORT_ERRORS, MEM_TAG,
				           "could not allocate/initialize/append tagged_mem_entry and list");
				return NULL;
			}
		}
		
		#ifdef DEBUG_MEM
		sprintf (msg, "mem in tagged_mem_entry of mem_tag_list with tag %u in malloc_t",
		         alloc_tag);
		void *mem = MALLOC_DEBUG (alloc_size, msg);
		#else
		void *mem = MALLOC_DEBUG (alloc_size, NULL);
		#endif
		
		if (!mem) {
			#ifndef DEBUG_MEM
			DEBUG_NOW1 (REPORT_ERRORS, MEM_TAG,
			            "failed to allocate requested mem for tagged_mem_entry list with tag %u",
			            alloc_tag);
			#endif
		}
		
		else
			if (list_append (&tagged_mem_entry->list, mem) < 0) {
				DEBUG_NOW1 (REPORT_ERRORS, MEM_TAG,
				            "failed to append mem to tagged_mem_entry list with tag %u",
				            alloc_tag);
				#ifdef DEBUG_MEM
				sprintf (msg,
				         "tagged_mem_entry with tag %u in malloc_t [failed to append mem to tagged_mem_entry list]",
				         alloc_tag);
				FREE_DEBUG (mem, msg);
				#else
				FREE_DEBUG (mem, NULL);
				#endif
				
				if (revert) {
					list_destroy (&tagged_mem_entry->list);
					#ifdef DEBUG_MEM
					sprintf (msg, "tagged_mem_entry of mem_tag_list with tag %u",
					         alloc_tag);
					FREE_DEBUG (tagged_mem_entry, msg);
					#else
					FREE_DEBUG (tagged_mem_entry, NULL);
					#endif
				}
				
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&mem_tag_list_mutex);
				#endif
				return NULL;
			}
			
			else {
				COMMIT_DEBUG1 (REPORT_INFO, MEM_TAG,
				               "successfully appended mem to tagged_mem_entry list with tag %u in malloc_t",
				               alloc_tag, false);
			}
			
		#ifdef MULTITHREADED_ON
		pthread_mutex_unlock (&mem_tag_list_mutex);
		// indicate successful malloc_t
		malloc_t_flag++;
		#endif
		return mem;
		#ifdef MULTITHREADED_ON
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, MEM_TAG, "cannot acquire lock on mem_tag_list_mutex");
		return NULL;
	}
	
		#endif
}

bool free_t (void *mem, uchar alloc_tag) {
	#ifdef MULTITHREADED_ON

	if (pthread_mutex_lock (&mem_tag_list_mutex) == 0) {
	#endif
	
		if (mem_tag_list) {
			ntp_tagged_mem_entry tagged_mem_entry = (ntp_tagged_mem_entry) list_seek (
			                                        mem_tag_list, &alloc_tag);
			                                        
			if (tagged_mem_entry) {
				int l = list_locate (&tagged_mem_entry->list, mem);
				
				if (l >= 0) {
					if (!list_delete_at (&tagged_mem_entry->list, (unsigned int) l)) {
						COMMIT_DEBUG1 (REPORT_INFO, MEM_TAG,
						               "delisted memory element in tagged_mem_entry with tag %u of mem_tag_list in free_t",
						               alloc_tag, false);
					}
					
					else {
						#ifdef MULTITHREADED_ON
						pthread_mutex_unlock (&mem_tag_list_mutex);
						#endif
						DEBUG_NOW1 (REPORT_ERRORS, MEM_TAG,
						            "could not delist memory element in tagged_mem_entry with tag %u of mem_tag_list",
						            alloc_tag);
						return false;
					}
					
					#ifdef DEBUG_MEM
					char msg[MAX_MSG_LEN];
					sprintf (msg, "mem in tagged_mem_entry of mem_tag_list with tag %u in free_t",
					         alloc_tag);
					FREE_DEBUG (mem, msg);
					#else
					FREE_DEBUG (mem, NULL);
					#endif
					#ifdef MULTITHREADED_ON
					pthread_mutex_unlock (&mem_tag_list_mutex);
					#endif
					return true;
				}
			}
			
			else {
				DEBUG_NOW2 (REPORT_ERRORS, MEM_TAG,
				            "could not find memory element %p with tag %u", mem, alloc_tag);
			}
		}
		
		#ifdef MULTITHREADED_ON
		pthread_mutex_unlock (&mem_tag_list_mutex);
		#endif
		return false;
		#ifdef MULTITHREADED_ON
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, MEM_TAG,
		           "cannot acquire lock on mem_tag_list_mutex");
		return false;
	}
	
		#endif
}

#ifdef MULTITHREADED_ON
bool prepare_threaded_free_t_all (ulong slot) {
	if (pthread_mutex_lock (&mem_tag_list_mutex) == 0) {
		if (slot > MAX_THREADS) {
			pthread_mutex_unlock (&mem_tag_list_mutex);
			DEBUG_NOW2 (REPORT_ERRORS, MEM_TAG, "slot (%lu) exceeds MAX_THREADS+1 (%d)",
			            slot,
			            MAX_THREADS + 1);
			return false;
		}
		
		if (mem_tag_list_destruction_target[slot] != NULL) {
			DEBUG_NOW2 (REPORT_ERRORS, MEM_TAG,
			            "slot (%lu) for mem_tag_list_destruction_target is not NULL (%p)",
			            slot, mem_tag_list_destruction_target[slot]);
			pthread_mutex_unlock (&mem_tag_list_mutex);
			return false;
		}
		
		if (!mem_tag_list) {
			pthread_mutex_unlock (&mem_tag_list_mutex);
			DEBUG_NOW (REPORT_ERRORS, MEM_TAG, "mem_tag_list is NULL");
			return false;
		}
		
		mem_tag_list_destruction_target[slot] = mem_tag_list;
		mem_tag_list = NULL;
		pthread_mutex_unlock (&mem_tag_list_mutex);
		return true;
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, MEM_TAG, "cannot acquire lock on mem_tag_list_mutex");
	}
	
	return false;
}
#endif

#ifdef MULTITHREADED_ON
	bool free_t_all (ulong slot)
#else
	void free_t_all()
#endif
{
	#ifdef MULTITHREADED_ON

	if (pthread_mutex_lock (&mem_tag_list_mutex) == 0) {
		if (slot > MAX_THREADS) {
			pthread_mutex_unlock (&mem_tag_list_mutex);
			DEBUG_NOW2 (REPORT_ERRORS, MEM_TAG,
			            "slot (%lu) exceeds MAX_THREADS+1 (%d)", slot, MAX_THREADS + 1);
			return false;
		}
		
		if (mem_tag_list_destruction_target[slot] == NULL) {
			DEBUG_NOW1 (REPORT_ERRORS, MEM_TAG,
			            "slot (%lu) for mem_tag_list_destruction_target is NULL", slot);
			pthread_mutex_unlock (&mem_tag_list_mutex);
			return false;
		}
		
		if (mem_tag_list) {
			DEBUG_NOW1 (REPORT_ERRORS, MEM_TAG,
			            "mem_tag_list is not NULL (%p)", mem_tag_list);
			pthread_mutex_unlock (&mem_tag_list_mutex);
			return false;
		}
		
		mem_tag_list = mem_tag_list_destruction_target[slot];
		mem_tag_list_destruction_target[slot] = NULL;
	#endif
		
		if (mem_tag_list && list_iterator_start (mem_tag_list)) {
			while (list_iterator_hasnext (mem_tag_list)) {
				ntp_tagged_mem_entry tagged_mem_entry = (ntp_tagged_mem_entry)
				                                        list_iterator_next (mem_tag_list);
				                                        
				if (tagged_mem_entry) {
					if (list_iterator_start (&tagged_mem_entry->list)) {
						ulong cnt = 0;
						#ifdef DEBUG_MEM
						uchar tag = tagged_mem_entry->tag;
						#endif
						
						while (list_iterator_hasnext (&tagged_mem_entry->list)) {
							#ifdef DEBUG_MEM
							char msg[MAX_MSG_LEN];
							sprintf (msg,
							         "mem in tagged_mem_entry of mem_tag_list with tag %u in free_t_all", tag);
							FREE_DEBUG (list_iterator_next (&tagged_mem_entry->list), msg);
							#else
							void *p = list_iterator_next (&tagged_mem_entry->list);
							FREE_DEBUG (p, NULL);
							#endif
							cnt++;
						}
						
						list_iterator_stop (&tagged_mem_entry->list);
						list_destroy (&tagged_mem_entry->list);
						FREE_DEBUG (tagged_mem_entry, "tagged_mem_entry of mem_tag_list in free_t_all");
						COMMIT_DEBUG2 (REPORT_INFO, MEM_TAG,
						               "freed %llu memory elements in tagged_mem_entry with tag %u of mem_tag_list in free_t_all",
						               cnt, tag, false);
					}
				}
			}
			
			list_iterator_stop (mem_tag_list);
			list_destroy (mem_tag_list);
			FREE_DEBUG (mem_tag_list, "mem_tag_list in free_t_all");
			mem_tag_list = NULL;
		}
		
		else {
			if (!mem_tag_list) {
				DEBUG_NOW (REPORT_WARNINGS, MEM_TAG,
				           "mem_tag_list not initialized");
			}
			
			else {
				DEBUG_NOW (REPORT_WARNINGS, MEM_TAG,
				           "cannot iterate on mem_tag_list");
			}
		}
		
		#ifdef MULTITHREADED_ON
		pthread_mutex_unlock (&mem_tag_list_mutex);
		return true;
	}
	
	else {
		DEBUG_NOW (REPORT_ERRORS, MEM_TAG,
		           "cannot acquire lock on mem_tag_list_mutex");
		return false;
	}
	
		#endif
}

void GET_SUBSTRING (const char *string, const short position,
                    const short length, char *substring) {
	if (position < 0 || length < 0 || position >= strlen (string)) {
		substring[0] = '\0';
	}
	
	else
		if (position + length > strlen (string)) {
			g_memcpy (substring, &string[position], strlen (string) - position);
			substring[strlen (string) - position] = '\0';
		}
		
		else {
			g_memcpy (substring, &string[position], (size_t)length);
			substring[length] = '\0';
		}
}

unsigned long long get_real_time() {
	#ifdef _WIN32

	if (pthread_mutex_lock (&localtime_mutex) == 0) {
		SYSTEMTIME this_time;
		GetLocalTime (&this_time);
		pthread_mutex_unlock (&localtime_mutex);
	}
	
	// only using ms precision under win, so ensure we skip at least 1 ms
	sleep_ms (1);
	return (((unsigned long long) (this_time.wYear - 2000)) * 10000000000000) +
	       ((unsigned long long) (this_time.wMonth) * 100000000000) +
	       ((unsigned long long) (this_time.wDay) * 1000000000) +
	       ((unsigned long long) (this_time.wHour) * 10000000) +
	       ((unsigned long long) (this_time.wMinute) * 100000) +
	       ((unsigned long long) (this_time.wSecond) * 1000) +
	       ((unsigned long long) (this_time.wMilliseconds));
	#else
	struct tm *l_tm;
	struct timespec time_spec;
	unsigned long long t = 0;
	       
	if (pthread_mutex_lock (&localtime_mutex) == 0) {
		time_spec.tv_sec = 0;
		clock_gettime (CLOCK_REALTIME, &time_spec);
		l_tm = localtime (&time_spec.tv_sec);
		t = (((unsigned long long) (l_tm->tm_year - 100)) * 100000000000000000) +
		    ((unsigned long long) (l_tm->tm_mon + 1) * 1000000000000000) +
		    ((unsigned long long) (l_tm->tm_mday) * 10000000000000) +
		    ((unsigned long long) (l_tm->tm_hour) * 100000000000) +
		    ((unsigned long long) (l_tm->tm_min) * 1000000000) +
		    ((unsigned long long) (l_tm->tm_sec) * 10000000) +
		    time_spec.tv_nsec /
		    100; // loose last 2 digits of nanosecond precision to fit within unsigned long long
		pthread_mutex_unlock (&localtime_mutex);
	}
	       
	else {
		DEBUG_NOW (REPORT_ERRORS, MEM_TAG,
		           "cannot acquire lock on debug_mem_traces_mutex");
		return 0;
	}
	       
	return t;
	#endif
}

void get_real_time_bytes (nt_rt_bytes *rt_bytes) {
	if (pthread_mutex_lock (&localtime_mutex) == 0) {
		g_memset (*rt_bytes, 0, NUM_RT_BYTES);
		#ifdef _WIN32
		SYSTEMTIME this_time;
		GetLocalTime (&this_time);
		(*rt_bytes)[0] = (char) tohexbytes[ (this_time.wYear - 2000)];
		(*rt_bytes)[1] = (char) tohexbytes[ (this_time.wMonth)];
		(*rt_bytes)[2] = (char) tohexbytes[ (this_time.wDay)];
		(*rt_bytes)[3] = (char) tohexbytes[ (this_time.wHour)];
		(*rt_bytes)[4] = (char) tohexbytes[ (this_time.wMinute)];
		(*rt_bytes)[5] = (char) tohexbytes[ (this_time.wSecond)];
		(*rt_bytes)[6] = (char) tohexbytes[ (((this_time.wMilliseconds) & 0xF000) >>
		                                     12)];
		(*rt_bytes)[7] = (char) tohexbytes[ (this_time.wMilliseconds & 0x0F00) >> 8];
		(*rt_bytes)[8] = (char) tohexbytes[ (this_time.wMilliseconds & 0x00F0) >> 4];
		(*rt_bytes)[9] = (char) tohexbytes[ (this_time.wMilliseconds & 0x0000)];
		#else
		struct timespec time_spec;
		clock_gettime (CLOCK_REALTIME, &time_spec);
		time_t t = time_spec.tv_sec;
		struct tm *l_tm = localtime (&t);
		(*rt_bytes)[ 0] = (char)tohexbytes[ (l_tm->tm_year - 100)];
		(*rt_bytes)[ 1] = (char)tohexbytes[ (l_tm->tm_mon + 1)];
		(*rt_bytes)[ 2] = (char)tohexbytes[ (l_tm->tm_mday)];
		(*rt_bytes)[ 3] = (char)tohexbytes[ (l_tm->tm_hour)];
		(*rt_bytes)[ 4] = (char)tohexbytes[ (l_tm->tm_min)];
		(*rt_bytes)[ 5] = (char)tohexbytes[ (l_tm->tm_sec)];
		(*rt_bytes)[ 6] = (char)tohexbytes[ (((time_spec.tv_nsec) & 0xF0000000) >> 28)];
		(*rt_bytes)[ 7] = (char)tohexbytes[ (((time_spec.tv_nsec) & 0x0F000000) >> 24)];
		(*rt_bytes)[ 8] = (char)tohexbytes[ (((time_spec.tv_nsec) & 0x00F00000) >> 20)];
		(*rt_bytes)[ 9] = (char)tohexbytes[ (((time_spec.tv_nsec) & 0x0000F000) >> 16)];
		(*rt_bytes)[10] = (char)tohexbytes[ (((time_spec.tv_nsec) & 0x0000F000) >> 12)];
		(*rt_bytes)[11] = (char)tohexbytes[ (((time_spec.tv_nsec) & 0x00000F00) >> 8)];
		#endif
		pthread_mutex_unlock (&localtime_mutex);
	}
}

void convert_timebytes_to_dec_representation (nt_rt_bytes *src,
                                        nt_rt_bytes *dst) {
	for (uchar i = 0; i < NUM_RT_BYTES; i++) {
		(*dst)[i] = (uchar)todecbytes[ (uchar) (*src)[i]];
	}
}

void convert_dec_to_timebytes_representation (nt_rt_bytes *src,
                                        nt_rt_bytes *dst) {
	for (uchar i = 0; i < NUM_RT_BYTES; i++) {
		(*dst)[i] = (uchar)tohexbytes[ (uchar) (*src)[i]];
	}
}

void convert_rt_bytes_to_string (nt_rt_bytes *rt_bytes,
                                 nt_rt_bytes *new_rt_char) {
	for (uchar i = 0; i < NUM_RT_BYTES / 2; i++) {
		(*new_rt_char)[ (i * 2) + 0] = (char) ('0' + ((*rt_bytes)[i] / 10));
		(*new_rt_char)[ (i * 2) + 1] = (char) ('0' + ((*rt_bytes)[i] % 10));
	}
}

void convert_string_to_rt_bytes (nt_rt_bytes *new_rt_char,
                                 nt_rt_bytes *rt_bytes) {
	for (uchar i = 0; i < NUM_RT_BYTES; i += 2) {
		(*rt_bytes)[i / 2] = (char) ((((*new_rt_char)[i] - '0') * 10) + (((
		                                        *new_rt_char)[i + 1] - '0')));
	}
}
