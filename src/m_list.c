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
#include "m_list.h"

typedef struct {
	ntp_list list;
	uchar tag;
} nt_search_seq_list_entry, *ntp_search_seq_list_entry;

static nt_rel_count list1_elements[MAX_ELEMENT_MATCHES],
       list2_elements[MAX_ELEMENT_MATCHES];
static nt_rel_seq_posn  list1_chain[MAX_CHAIN_MATCHES],
       list2_chain[MAX_CHAIN_MATCHES];
static nt_stack_size    list1_stacks[MAX_CHAIN_MATCHES / 2],
       list2_stacks[MAX_CHAIN_MATCHES / 2];

#ifdef MULTITHREADED_ON
	#include <pthread.h>
	
	/*
	* list destruction thread handling
	*/
	
	static pthread_t list_destruction_thread_id[MAX_THREADS];
	static bool list_destruction_thread_active[MAX_THREADS];
	static bool list_destruction_thread_joined[MAX_THREADS];
	static ntp_list list_destruction_thread_target[MAX_THREADS];
	
	static uchar num_destruction_threads = 0;
	static pthread_attr_t destruction_thread_attr;
	static pthread_mutex_t num_destruction_threads_mutex;
	static bool list_destruction_init = false;
#endif

ntp_list search_seq_list = NULL;

int seq_search_list_seeker (const void *el, const void *key) {
	const REGISTER nt_list *restrict this_list = ((ntp_search_seq_list_entry)
	                                        el)->list;
	                                        
	if (this_list == key) {
		return 1;
	}
	
	return 0;
}

bool list_initialize_tagging() {
	COMMIT_DEBUG (REPORT_INFO, LIST,
	              "initializing search_seq_list in list_initialize_tagging", true);
	#ifdef MULTITHREADED_ON
	              
	if (pthread_mutex_lock (&num_destruction_threads_mutex) == 0) {
	#endif
	
		if (search_seq_list) {
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "search_seq_list already initialized in list_initialize_tagging", false);
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			#endif
			return false;
		}
		
		search_seq_list = MALLOC_DEBUG (sizeof (nt_list),
		                                "search_seq_list in list_initialize_tagging");
		                                
		if (!search_seq_list) {
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			#endif
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "cannot allocate memory for search_seq_list in list_initialize_tagging", false);
			return NULL;
		}
		
		if ((list_init (search_seq_list) != 0) ||
		    (list_attributes_seeker (search_seq_list, &seq_search_list_seeker) != 0)) {
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			#endif
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "cannot initialize list/set attribute seeker for search_seq_list in list_initialize_tagging",
			              false);
			FREE_DEBUG (search_seq_list,
			            "search_seq_list in list_initialize_tagging [failed to initialize list/set attribute seeker for search_seq_list in list_initialize_tagging]");
			return false;
		}
		
		#ifdef MULTITHREADED_ON
		pthread_mutex_unlock (&num_destruction_threads_mutex);
		#endif
		COMMIT_DEBUG (REPORT_INFO, LIST,
		              "search_seq_list successfully allocated and initialized in list_initialize_tagging",
		              false);
		#ifdef MULTITHREADED_ON
	}
	
		#endif
	return true;
}

bool ntp_list_alloc_debug (ntp_list restrict *l, char *debug_msg) {
	*l = MALLOC_DEBUG (sizeof (nt_list), debug_msg);
	#ifndef NO_FULL_CHECKS
	
	if ((!*l) || (
	#endif
	                        list_init (*l)
                        #ifndef NO_FULL_CHECKS
	                        != 0)) {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "cannot allocate memory/initialize nt_list l in ntp_list_alloc_debug", false);
		              
		if (*l) {
			FREE_DEBUG (*l, debug_msg);
		}
		
		return false;
	}
	
                        #else
	                        ;
                        #endif
	return true;
}

#ifdef NO_FULL_CHECKS
	void clear_search_seq_list (ntp_list *restrict search_seq_list)
#else
	bool clear_search_seq_list (ntp_list *restrict search_seq_list)
#endif
{
	if (!*search_seq_list) {
		return;
	}
	
	#ifndef NO_FULL_CHECKS
	
	if (
	#endif
	                    list_iterator_start (*search_seq_list)
                    #ifndef NO_FULL_CHECKS
	                    == 0) {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "search_seq_list is NULL or cannot be iterated in clear_search_seq_list",
		              false);
		return false;
	}
	
                    #else
	                    ;
                    #endif
	
	while (list_iterator_hasnext (*search_seq_list)) {
		REGISTER
		ntp_search_seq_list_entry restrict search_seq_list_entry =
		                    (ntp_search_seq_list_entry)list_iterator_next (*search_seq_list);
		                    
		if (search_seq_list_entry) {
			list_destroy (search_seq_list_entry->list);
			FREE_DEBUG (search_seq_list_entry,
			            "search_seq_list_entry of search_seq_list in clear_search_seq_list");
		}
	}
	
	list_iterator_stop (*search_seq_list);
	list_destroy (*search_seq_list);
	FREE_DEBUG (*search_seq_list, "search_seq_list in clear_search_seq_list");
	*search_seq_list = NULL;
	#ifndef NO_FULL_CHECKS
	return true;
	#endif
}

#ifdef MULTITHREADED_ON
bool initialize_list_destruction() {
	COMMIT_DEBUG (REPORT_INFO, LIST,
	              "initializing list destruction in initialize_list_destruction", true);
	              
	if (!list_destruction_init) {
		if (pthread_attr_init (&destruction_thread_attr) != 0) {
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "cannot initialize destruction_thread_attr in initialize_list_destruction",
			              false);
			return false;
		}
		
		if (pthread_attr_setdetachstate (&destruction_thread_attr,
		                                 PTHREAD_CREATE_JOINABLE) != 0) {
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "cannot set detach state of destruction_thread_attr in initialize_list_destruction",
			              false);
			return false;
		}
		
		if (pthread_mutex_init (&num_destruction_threads_mutex, NULL) != 0) {
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "cannot initialize num_destruction_threads_mutex in initialize_list_destruction",
			              false);
			return false;
		}
		
		for (uchar t = 0; t < MAX_THREADS; t++) {
			list_destruction_thread_active[t] = false;
			list_destruction_thread_joined[t] = true;
		}
		
		list_destruction_init = true;
		COMMIT_DEBUG (REPORT_INFO, LIST,
		              "list destruction thread attribute set in initialize_list_destruction", false);
	}
	
	else {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "list destruction thread attribute already set in initialize_list_destruction",
		              false);
		return false;
	}
	
	return true;
}

bool wait_list_destruction() {
	COMMIT_DEBUG (REPORT_INFO, LIST,
	              "waiting for all pending threads to join in wait_list_destruction", true);
	              
	if (list_destruction_init) {
		void *status;
		uchar trylock_attempts = MODEL_LIST_DESTRUCTION_LOCK_MAX_ATTEMPTS;
		uchar num_pending = 0;
		
		while (trylock_attempts > 0) {
			if (pthread_mutex_trylock (&num_destruction_threads_mutex) == 0) {
				num_pending = 0;
				COMMIT_DEBUG (REPORT_INFO, LIST,
				              "acquired lock on num_destruction_threads_mutex in wait_list_destruction",
				              false);
				              
				for (uchar t = 0; t < MAX_THREADS; t++) {
					if (list_destruction_thread_active[t] || !list_destruction_thread_joined[t]) {
						num_pending++;      // count threads that are still active or yet to be joined
					}
					
					if (!list_destruction_thread_active[t] && !list_destruction_thread_joined[t]) {
						int ret_val = pthread_join (list_destruction_thread_id[t], &status);
						
						if (ret_val) {
							COMMIT_DEBUG2 (REPORT_ERRORS, LIST,
							               "error code from pthread_join() for thread #%d is %d in wait_list_destruction",
							               t + 1, ret_val, false);
						}
						
						else {
							COMMIT_DEBUG2 (REPORT_INFO, LIST,
							               "completed join with thread #%d having a status of %ld in wait_list_destruction",
							               t + 1, (long) status,
							               false);
							list_destruction_thread_joined[t] = true;
							num_pending--;  // this thread is now both inactive and joined
						}
					}
				}
				
				if (pthread_mutex_unlock (&num_destruction_threads_mutex) == 0) {
					#ifdef DEBUG_ON
					COMMIT_DEBUG (REPORT_INFO, LIST,
					              "released lock on num_destruction_threads_mutex in wait_list_destruction",
					              false);
					#endif
				}
				
				else {
					COMMIT_DEBUG (REPORT_ERRORS, LIST,
					              "cannot release lock on num_destruction_threads_mutex in wait_list_destruction",
					              false);
					return false;
				}
				
				if (!num_pending) {
					break;
				}
				
				else {
					COMMIT_DEBUG2 (REPORT_INFO, LIST,
					               "%d thread(s) active or pending join... sleeping for %d seconds ",
					               num_pending, MODEL_LIST_DESTRUCTION_THREAD_SLEEP_S, false);
					sleep (MODEL_LIST_DESTRUCTION_THREAD_SLEEP_S);
				}
			}
			
			else {
				trylock_attempts--;
				COMMIT_DEBUG1 (REPORT_INFO, LIST,
				               "cannot acquire lock on num_destruction_threads_mutex (%d attempts remaining) in wait_list_destruction",
				               trylock_attempts, false);
				COMMIT_DEBUG1 (REPORT_INFO, LIST,
				               "sleeping for %d second(s) in wait_list_destruction",
				               MODEL_LIST_DESTRUCTION_LOCK_SLEEP_S, false);
				sleep (MODEL_LIST_DESTRUCTION_LOCK_SLEEP_S);
			}
		}
		
		return !num_pending;
	}
	
	else {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "list_destruction_init not initialized in wait_list_destruction", false);
		return false;
	}
}

void finalize_list_destruction() {
	if (list_destruction_init) {
		if (pthread_attr_destroy (&destruction_thread_attr) != 0) {
			#ifdef DEBUG_ON
			DEBUG_NOW (REPORT_ERRORS, LIST, "cannot destroy destruction_thread_attr");
			#endif
		}
		
		if (pthread_mutex_destroy (&num_destruction_threads_mutex) != 0) {
			#ifdef DEBUG_ON
			DEBUG_NOW (REPORT_ERRORS, LIST, "cannot destroy num_destruction_threads_mutex");
			#endif
		}
		
		#ifdef DEBUG_ON
		DEBUG_NOW (REPORT_INFO, LIST, "main thread completed");
		#endif
	}
	
	else {
		#ifdef DEBUG_ON
		DEBUG_NOW (REPORT_WARNINGS, LIST, "list destruction not initialized");
		#endif
		exit (-1);
	}
}

void *list_destruction_thread (void *thread_idx) {
	if (pthread_mutex_lock (&num_destruction_threads_mutex) == 0) {
		uchar this_thread_idx = * (uchar *)thread_idx;
		unsigned long long malloc_t_flag_before, malloc_t_flag_after;
		
		while (1) {
			malloc_t_flag_before = get_malloc_t_flag();
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			sleep (MALLOC_THREAD_DESTRUCTION_SLEEP_S);
			
			if (pthread_mutex_lock (&num_destruction_threads_mutex) == 0) {
				malloc_t_flag_after = get_malloc_t_flag();
				
				if (malloc_t_flag_after == malloc_t_flag_before) {
					// no malloc_t since last sleep
					break;
				}
				
				else {
					pthread_mutex_unlock (&num_destruction_threads_mutex);
				}
			}
		}
		
		ntp_list search_seq_list = list_destruction_thread_target[this_thread_idx];
		clear_search_seq_list (&search_seq_list);
		FREE_TAG_ALL (this_thread_idx);
		num_destruction_threads--;
		list_destruction_thread_target[this_thread_idx] = NULL;
		list_destruction_thread_active[this_thread_idx] =
		                    false;  // mark slot as inactive
		clear_malloc_t_flag();
		pthread_mutex_unlock (&num_destruction_threads_mutex);
	}
	
	pthread_exit (NULL);
}

bool list_destroy_all_tagged() {
	COMMIT_DEBUG (REPORT_INFO, LIST,
	              "destroying all elements of search_seq_list in list_destroy_all_tagged", true);
	              
	if (list_destruction_init) {
		if (pthread_mutex_lock (&num_destruction_threads_mutex) == 0) {
			COMMIT_DEBUG1 (REPORT_INFO, LIST,
			               "acquired lock on num_destruction_threads_mutex (for thread #%d) in list_destroy_all_tagged",
			               num_destruction_threads + 1, false);
			               
			if (num_destruction_threads < MAX_THREADS) {
				COMMIT_DEBUG1 (REPORT_INFO, LIST,
				               "creating thread #%d in list_destroy_all_tagged", num_destruction_threads + 1,
				               false);
				               
				for (uchar t = 0; t < MAX_THREADS; t++) {
					if (!list_destruction_thread_active[t]) {
						COMMIT_DEBUG2 (REPORT_INFO, LIST,
						               "found inactive slot %d for new thread #%d in list_destroy_all_tagged",
						               t, num_destruction_threads + 1, false);
						               
						if (!list_destruction_thread_joined[t]) {
							COMMIT_DEBUG2 (REPORT_INFO, LIST,
							               "joining previous thread in inactive slot %d for new thread #%d in list_destroy_all_tagged",
							               t, num_destruction_threads + 1, false);
							// this slot is not active anymore but thread not yet joined
							void *status;
							int retval = pthread_join (list_destruction_thread_id[t], &status);
							
							if (retval) {
								COMMIT_DEBUG2 (REPORT_ERRORS, LIST,
								               "error code from pthread_join() for thread #%d is %d in list_destroy_all_tagged",
								               num_destruction_threads + 1, retval, false);
								break;
							}
							
							else {
								COMMIT_DEBUG2 (REPORT_INFO, LIST,
								               "completed join with thread #%d having a status of %ld in list_destroy_all_tagged",
								               num_destruction_threads + 1, (long)status, false);
								list_destruction_thread_joined[t] = true;
							}
						}
						
						// found empty thread slot in list_destruction_thread_active
						// set deletion target for this thread
						list_destruction_thread_target[t] = search_seq_list;
						PREPARE_THREADED_FREE_T_ALL (t);
						uchar *t_id = malloc (sizeof (uchar));
						*t_id = t;
						
						if (pthread_create (&list_destruction_thread_id[t], &destruction_thread_attr,
						                    list_destruction_thread, t_id) == 0) {
							COMMIT_DEBUG2 (REPORT_INFO, LIST,
							               "created thread #%d in slot %d in list_destroy_all_tagged",
							               num_destruction_threads + 1, t, false);
							// occupy slot
							list_destruction_thread_active[t] = true;
							// and mark thread as 'unjoined'
							list_destruction_thread_joined[t] = false;
						}
						
						else {
							COMMIT_DEBUG1 (REPORT_ERRORS, LIST,
							               "cannot created thread #%d in list_destroy_all_tagged",
							               num_destruction_threads + 1, false);
						}
						
						// clear search_seq_list
						search_seq_list = NULL;
						break;
					}
				}
				
				num_destruction_threads++;
			}
			
			else {
				COMMIT_DEBUG1 (REPORT_INFO, LIST,
				               "MAX_THREADS (%d) reached in list_destroy_all_tagged", MAX_THREADS, false);
				// clear search_seq_list directly from main thread
				clear_search_seq_list (&search_seq_list);
				PREPARE_THREADED_FREE_T_ALL (MAX_THREADS);
				FREE_TAG_ALL (MAX_THREADS);
				search_seq_list = NULL;
			}
			
			if (pthread_mutex_unlock (&num_destruction_threads_mutex) == 0) {
				#ifdef DEBUG_ON
				COMMIT_DEBUG1 (REPORT_INFO, LIST,
				               "released lock on num_destruction_threads_mutex (for thread #%d) in list_destroy_all_tagged",
				               num_destruction_threads, false);
				#endif
			}
			
			#ifdef DEBUG_ON
			
			else {
				COMMIT_DEBUG1 (REPORT_ERRORS, LIST,
				               "cannot release lock on num_destruction_threads_mutex (for thread #%d) in list_destroy_all_tagged",
				               num_destruction_threads, false);
			}
			
			#endif
		}
		
		else {
			COMMIT_DEBUG1 (REPORT_ERRORS, LIST,
			               "cannot acquire lock on num_destruction_threads_mutex (for thread #%d) in list_destroy_all_tagged",
			               num_destruction_threads + 1, false);
			return false;
		}
	}
	
	else {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "list destruction not initialized in list_destroy_all_tagged", false);
		return false;
	}
	
	return true;
}
#else
bool list_destroy_all_tagged() {
	clear_search_seq_list (&search_seq_list);
	FREE_TAG_ALL();
	return true;
}
#endif

/*
 * private function to append list of base-pairs from src to dst
 *
 * input:   pre-allocated src nt_list containing one or more nt_bp
 *          pre-allocated dst nt_list
 *          track_id
 *
 * output:  initialized and populated dst nt_list of nt_linked_bp,
 *          with prev_linked_bp set to NULL and track_id set to track_id
 *
 * notes:   - src list elements are not modified, but any previous
 *            iteration sessions are terminated
 */
bool ntp_list_insert (ntp_list restrict dst, ntp_list restrict src,
                      const nt_stack_size stack_len, const uchar track_id) {
	REGISTER
	ulong last_list_posn = 0;
	#ifndef NO_FULL_CHECKS
	
	if (!dst) {
		COMMIT_DEBUG (REPORT_ERRORS, LIST, "dst list is NULL in ntp_list_insert",
		              false);
		return false;
	}
	
	if (!src || (!
	#endif
	             list_iterator_start (src)
             #ifndef NO_FULL_CHECKS
	            )) {
		if (src) {
			COMMIT_DEBUG (REPORT_ERRORS, LIST, "src list not iterable in ntp_list_insert",
			              false);
		}
		
		else {
			COMMIT_DEBUG (REPORT_ERRORS, LIST, "src list is NULL", false);
		}
		
		return false;
	}
	
	COMMIT_DEBUG1 (REPORT_INFO, LIST,
	               "copying src list to dst on track_id %u in ntp_list_insert", track_id, true);
             #else
	             ;
             #endif
	               
	while (list_iterator_hasnext (src)) {
		REGISTER
		ntp_bp restrict bp = list_iterator_next (src);
		REGISTER
		ntp_linked_bp restrict linked_bp_cpy = MALLOC_TAG (sizeof (nt_linked_bp),
		                                        track_id);
		#ifndef NO_FULL_CHECKS
		REGISTER
		bool success = false;
		
		if (linked_bp_cpy) {
		#endif
			linked_bp_cpy->bp = bp;
			linked_bp_cpy->stack_len = stack_len;
			linked_bp_cpy->track_id = track_id;
			linked_bp_cpy->fp_elements = NULL;
			linked_bp_cpy->tp_elements = NULL;
			linked_bp_cpy->prev_linked_bp = NULL;
			REGISTER
			ntp_linked_bp last_linked_bp_cpy = list_get_at (dst, last_list_posn);
			
			while (last_linked_bp_cpy) {
				if (last_linked_bp_cpy->bp->fp_posn > bp->fp_posn ||
				    (last_linked_bp_cpy->bp->fp_posn == bp->fp_posn &&
				     last_linked_bp_cpy->bp->tp_posn > bp->tp_posn)) {
					break;
				}
				
				last_list_posn++;
				last_linked_bp_cpy = list_get_at (dst, last_list_posn);
			}
			
			#ifndef NO_FULL_CHECKS
			
			if (
			#endif
			                    list_insert_at (dst, linked_bp_cpy, last_list_posn)
		                    #ifndef NO_FULL_CHECKS
			                    < 0) {
				FREE_TAG (linked_bp_cpy, track_id);
			}
			
			else {
				success = true;
			}
		}
		
		if (!success) {
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "could not allocate memory for linked_bp_cpy/append to dst list in ntp_list_insert",
			              false);
			list_iterator_stop (src);
			
			if (!list_iterator_start (dst)) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "could not release memory for dst list in ntp_list_insert", false);
			}
			
			while (list_iterator_hasnext (dst)) {
				FREE_TAG (list_iterator_next (dst), track_id);
			}
			
			list_iterator_stop (dst);
			list_destroy (dst);
			return false;
		}
		
		                    #else
			                    ;
		                    #endif
		last_list_posn++;
	}
	
	list_iterator_stop (src);
	return true;
}

int ntp_bp_seeker (const void *el, const void *key) {
	const REGISTER nt_bp *restrict bp_entry = ((ntp_linked_bp)el)->bp;
	
	if (bp_entry == (((ntp_linked_bp)key)->bp)) {
		return 1;
	}
	
	return 0;
}

int ntp_count_seeker (const void *el, const void *key) {
	const REGISTER nt_rel_count *restrict count_entry = ((ntp_rel_count)el);
	
	if (*count_entry == * ((ntp_rel_count)key)) {
		return 1;
	}
	
	return 0;
}

bool ntp_list_linked_bp_seeker (ntp_list restrict list,
                                const nt_bp *restrict linked_bp, const nt_linked_bp *restrict prev_linked_bp) {
	#ifndef NO_FULL_CHECKS
                                
	if (list == NULL || linked_bp == NULL) {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "list or linked_bp is NULL in ntp_list_linked_bp_seeker", false);
		return false;
	}
	
	if (
	#endif
	                    list_iterator_start (list)
                    #ifndef NO_FULL_CHECKS
	                    == 0) {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "cannot iterate list in ntp_list_linked_bp_seeker", false);
		return false;
	}
	
                    #else
	                    ;
                    #endif
	
	while (list_iterator_hasnext (list)) {
		REGISTER
		ntp_linked_bp restrict this_linked_bp = list_iterator_next (list);
		#ifndef NO_FULL_CHECKS
		
		if (!this_linked_bp) {
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "cannot iterate over next element in list in ntp_list_linked_bp_seeker", false);
			list_iterator_stop (list);
			return false;
		}
		
		#endif
		
		if (this_linked_bp->bp == linked_bp) {
			while (1) {
				if (this_linked_bp->prev_linked_bp != prev_linked_bp) {
					break;
				}
				
				if (!prev_linked_bp) {
					list_iterator_stop (list);
					return true;
				}
				
				this_linked_bp = this_linked_bp->prev_linked_bp;
				prev_linked_bp = prev_linked_bp->prev_linked_bp;
			}
		}
	}
	
	list_iterator_stop (list);
	return false;
}

bool ntp_list_linked_bp_seeker_with_elements (ntp_list restrict list,
                                        ntp_linked_bp restrict linked_bp) {
	#ifndef NO_FULL_CHECKS
                                        
	if (list == NULL || linked_bp == NULL) {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "list or linked_bp is NULL in ntp_list_linked_bp_seeker_with_elements", false);
		return false;
	}
	
	if (
	#endif
	                    list_iterator_start (list)
                    #ifndef NO_FULL_CHECKS
	                    == 0) {
		COMMIT_DEBUG (REPORT_ERRORS, LIST,
		              "cannot iterate list in ntp_list_linked_bp_seeker_with_elements", false);
		return false;
	}
	
                    #else
	                    ;
                    #endif
	
	while (list_iterator_hasnext (list)) {
		REGISTER
		ntp_linked_bp restrict this_linked_bp = list_iterator_next (list);
		#ifndef NO_FULL_CHECKS
		
		if (!this_linked_bp) {
			COMMIT_DEBUG (REPORT_ERRORS, LIST,
			              "cannot iterate over next element in list in ntp_list_linked_bp_seeker_with_elements",
			              false);
			list_iterator_stop (list);
			return false;
		}
		
		#endif
		
		if (this_linked_bp->bp == linked_bp->bp) {
			REGISTER
			ntp_linked_bp restrict orig_linked_bp = linked_bp;
			
			while (1) {
				// if one exists, check whether chain of fp/tp elements are identical
				ntp_element this_fp_elements = this_linked_bp->fp_elements,
				            orig_fp_elements = orig_linked_bp->fp_elements;
				            
				while (this_fp_elements == orig_fp_elements && this_fp_elements != NULL) {
					this_fp_elements = this_fp_elements->unpaired->next;
					
					if (orig_fp_elements) {
						orig_fp_elements = orig_fp_elements->unpaired->next;
					}
				}
				
				if (this_fp_elements != NULL || orig_fp_elements != NULL) {
					break;
				}
				
				REGISTER
				ntp_element this_tp_elements = this_linked_bp->tp_elements,
				            orig_tp_elements = orig_linked_bp->tp_elements;
				            
				while (this_tp_elements == orig_tp_elements && this_tp_elements != NULL) {
					this_tp_elements = this_tp_elements->unpaired->next;
					
					if (orig_tp_elements) {
						orig_tp_elements = orig_tp_elements->unpaired->next;
					}
				}
				
				if (this_tp_elements != NULL || orig_tp_elements != NULL) {
					break;
				}
				
				// then check prev_linked_bps
				if (this_linked_bp->prev_linked_bp != orig_linked_bp->prev_linked_bp) {
					break;
				}
				
				if (!orig_linked_bp->prev_linked_bp) {
					list_iterator_stop (list);
					return true;
				}
				
				this_linked_bp = this_linked_bp->prev_linked_bp;
				orig_linked_bp = orig_linked_bp->prev_linked_bp;
			}
		}
	}
	
	list_iterator_stop (list);
	return false;
}

ntp_linked_bp duplicate_linked_bp (ntp_linked_bp this_linked_bp,
                                   const uchar tag) {
	REGISTER
	ntp_linked_bp first_linked_bp_copy = NULL, prev_linked_bp_copy = NULL;
	
	while (this_linked_bp) {
		REGISTER
		ntp_linked_bp linked_bp_copy = MALLOC_TAG (sizeof (nt_list), tag);
		
		if (!first_linked_bp_copy) {
			first_linked_bp_copy = linked_bp_copy;
		}
		
		linked_bp_copy->stack_len = this_linked_bp->stack_len;
		linked_bp_copy->track_id = this_linked_bp->track_id;
		REGISTER
		ntp_bp bp_copy = MALLOC_TAG (sizeof (nt_bp), tag);
		bp_copy->fp_posn = this_linked_bp->bp->fp_posn;
		bp_copy->tp_posn = this_linked_bp->bp->tp_posn;
		linked_bp_copy->bp = bp_copy;
		linked_bp_copy->prev_linked_bp = NULL;
		
		if (this_linked_bp->fp_elements) {
			REGISTER
			ntp_element first_element_copy = NULL;
			REGISTER
			ntp_element orig_fp_element = this_linked_bp->fp_elements;
			REGISTER
			ntp_unpaired_element prev_unpaired_element_copy = NULL;
			
			do {
				REGISTER
				ntp_element element_copy = MALLOC_TAG (sizeof (nt_element), tag);
				
				if (!first_element_copy) {
					first_element_copy = element_copy;
				}
				
				REGISTER
				ntp_unpaired_element unpaired_element_copy = MALLOC_TAG (sizeof (
				                                        nt_unpaired_element), tag);
				element_copy->type = unpaired;
				element_copy->unpaired = unpaired_element_copy;
				unpaired_element_copy->i_constraint.reference =
				                    orig_fp_element->unpaired->i_constraint.reference;
				unpaired_element_copy->i_constraint.element_type =
				                    orig_fp_element->unpaired->i_constraint.element_type;
				unpaired_element_copy->next_linked_bp = this_linked_bp;
				unpaired_element_copy->length = orig_fp_element->unpaired->length;
				unpaired_element_copy->dist = orig_fp_element->unpaired->dist;
				unpaired_element_copy->next = NULL;
				
				if (prev_unpaired_element_copy) {
					prev_unpaired_element_copy->next = element_copy;
				}
				
				prev_unpaired_element_copy = unpaired_element_copy;
				orig_fp_element = orig_fp_element->unpaired->next;
			}
			while (orig_fp_element);
			
			linked_bp_copy->fp_elements = first_element_copy;
		}
		
		else {
			linked_bp_copy->fp_elements = NULL;
		}
		
		if (this_linked_bp->tp_elements) {
			REGISTER
			ntp_element first_element_copy = NULL;
			REGISTER
			ntp_element orig_tp_element = this_linked_bp->tp_elements;
			REGISTER
			ntp_unpaired_element prev_unpaired_element_copy = NULL;
			
			do {
				REGISTER
				ntp_element element_copy = MALLOC_TAG (sizeof (nt_element), tag);
				
				if (!first_element_copy) {
					first_element_copy = element_copy;
				}
				
				REGISTER
				ntp_unpaired_element unpaired_element_copy = MALLOC_TAG (sizeof (
				                                        nt_unpaired_element), tag);
				element_copy->type = unpaired;
				element_copy->unpaired = unpaired_element_copy;
				unpaired_element_copy->i_constraint.reference =
				                    orig_tp_element->unpaired->i_constraint.reference;
				unpaired_element_copy->i_constraint.element_type =
				                    orig_tp_element->unpaired->i_constraint.element_type;
				unpaired_element_copy->next_linked_bp = this_linked_bp;
				unpaired_element_copy->length = orig_tp_element->unpaired->length;
				unpaired_element_copy->dist = orig_tp_element->unpaired->dist;
				unpaired_element_copy->next = NULL;
				
				if (prev_unpaired_element_copy) {
					prev_unpaired_element_copy->next = element_copy;
				}
				
				prev_unpaired_element_copy = unpaired_element_copy;
				orig_tp_element = orig_tp_element->unpaired->next;
			}
			while (orig_tp_element);
			
			linked_bp_copy->tp_elements = first_element_copy;
		}
		
		else {
			linked_bp_copy->tp_elements = NULL;
		}
		
		if (prev_linked_bp_copy) {
			prev_linked_bp_copy->prev_linked_bp = linked_bp_copy;
		}
		
		prev_linked_bp_copy = linked_bp_copy;
		this_linked_bp = this_linked_bp->prev_linked_bp;
	}
	
	return first_linked_bp_copy;
}

ntp_list duplicate_list (ntp_list orig_list, const uchar tag) {
	ntp_list list_copy;
	ntp_list_alloc_debug (&list_copy, "list_copy in duplicate_list");
	list_iterator_start (orig_list);
	
	while (list_iterator_hasnext (orig_list)) {
		REGISTER
		ntp_linked_bp linked_bp_copy = duplicate_linked_bp (list_iterator_next (
		                                        orig_list), tag);
		                                        
		if (linked_bp_copy) {
			list_append (list_copy, linked_bp_copy);
		}
		
		else {
			list_destroy (list_copy);
			FREE_DEBUG (list_copy, "list_copy in duplicate_list");
			list_iterator_stop (orig_list);
			return NULL;
		}
	}
	
	list_iterator_stop (orig_list);
	return list_copy;
}

ntp_list duplicate_list_shallow (ntp_list orig_list, char *debug_msg) {
	ntp_list list_copy;
	ntp_list_alloc_debug (&list_copy, debug_msg);
	list_iterator_start (orig_list);
	
	while (list_iterator_hasnext (orig_list)) {
		list_append (list_copy, list_iterator_next (orig_list));
	}
	
	list_iterator_stop (orig_list);
	return list_copy;
}

ntp_list ntp_list_concatenate (ntp_list restrict list1, ntp_list restrict list2,
                               const uchar tag) {
	if (!list1 || !list1->numels) {
		if (!list2 || !list2->numels) {
			return NULL;
		}
		
		else {
			return duplicate_list (list2, tag);
		}
	}
	
	else
		if (!list2 || !list2->numels || list1 == list2) {
			return duplicate_list (list1, tag);
		}
		
		else {
			REGISTER ntp_list restrict new_list = MALLOC_DEBUG (sizeof (nt_list),
			                                        "new_list in ntp_list_concatenate");
			#ifndef NO_FULL_CHECKS
			                                        
			if (!new_list || (
			#endif
			                        list_init (new_list)
		                        #ifndef NO_FULL_CHECKS
			                        < 0)) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "cannot allocate memory/initialize new_list in ntp_list_concatenate", false);
				              
				if (new_list) {
					FREE_DEBUG (new_list, "new_list in ntp_list_concatenate");
				}
				
				return NULL;
			}
			
			if (
		                        #else
			                        ;
		                        #endif
			                    list_attributes_seeker (new_list, &ntp_bp_seeker)
		                    #ifndef NO_FULL_CHECKS
			                    < 0) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "could not set attributes seeker for new_list in ntp_list_concatenate", false);
				list_destroy (new_list);
				FREE_DEBUG (new_list, "new_list in ntp_list_concatenate");
				return NULL;
			}
			
		                    #else
			                    ;
		                    #endif
			#ifndef NO_FULL_CHECKS
			
			if (
			#endif
			                    list_iterator_start (list1)
		                    #ifndef NO_FULL_CHECKS
			                    == 0) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "cannot iterate list1 in ntp_list_concatenate", false);
				list_destroy (new_list);
				FREE_DEBUG (new_list, "new_list in ntp_list_concatenate");
				return NULL;
			}
			
		                    #else
			                    ;
		                    #endif
			#ifndef NO_FULL_CHECKS
			
			if (
			#endif
			                    list_iterator_start (list2)
		                    #ifndef NO_FULL_CHECKS
			                    == 0) {
				list_iterator_stop (list1);
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "cannot iterate list2 in ntp_list_concatenate", false);
				list_destroy (new_list);
				FREE_DEBUG (new_list, "new_list in ntp_list_concatenate");
				return NULL;
			}
			
		                    #else
			                    ;
		                    #endif
			REGISTER
			ntp_linked_bp   list1_linked_bp = list_iterator_next (list1),
			                list2_linked_bp = list_iterator_next (list2),
			                tmp1_linked_bp = list1_linked_bp,
			                tmp2_linked_bp = list2_linked_bp,
			                new_linked_bp = NULL;
			REGISTER
			ntp_element tmp_element;
			REGISTER
			uchar   list1_linked_bp_chain_length = 0,
			        // length of chain of bp
			        list2_linked_bp_chain_length = 0,
			        list1_linked_bp_elements_length = 0,
			        // length of chain of elements, if any
			        list2_linked_bp_elements_length = 0,
			        list1_linked_bp_elements_num = 0,
			        // number of elements, if any
			        list2_linked_bp_elements_num = 0;
			        
			// sum up bps/elements chain length, and number of elements, if any, for first ntp_linked_bp
			while (tmp1_linked_bp) {
				list1_linked_bp_chain_length += 2;
				
				if (tmp1_linked_bp->fp_elements) {
					list1_linked_bp_elements_num++;
					tmp_element = tmp1_linked_bp->fp_elements;
					
					while (tmp_element) {
						list1_linked_bp_elements_length += 2;
						tmp_element = tmp_element->unpaired->next;
					}
				}
				
				if (tmp1_linked_bp->tp_elements) {
					list1_linked_bp_elements_num++;
					tmp_element = tmp1_linked_bp->tp_elements;
					
					while (tmp_element) {
						list1_linked_bp_elements_length += 2;
						tmp_element = tmp_element->unpaired->next;
					}
				}
				
				tmp1_linked_bp = tmp1_linked_bp->prev_linked_bp;
			}
			
			// sum up bps/elements chain length, and number of elements, if any, for second ntp_linked_bp
			while (tmp2_linked_bp) {
				list2_linked_bp_chain_length += 2;
				
				if (tmp2_linked_bp->fp_elements) {
					list2_linked_bp_elements_num++;
					tmp_element = tmp2_linked_bp->fp_elements;
					
					while (tmp_element) {
						list2_linked_bp_elements_length += 2;
						tmp_element = tmp_element->unpaired->next;
					}
				}
				
				if (tmp2_linked_bp->tp_elements) {
					list2_linked_bp_elements_num++;
					tmp_element = tmp2_linked_bp->tp_elements;
					
					while (tmp_element) {
						list2_linked_bp_elements_length += 2;
						tmp_element = tmp_element->unpaired->next;
					}
				}
				
				tmp2_linked_bp = tmp2_linked_bp->prev_linked_bp;
			}
			
			#ifndef NO_FULL_CHECKS
			
			if (list1_linked_bp_chain_length != list2_linked_bp_chain_length) {
				list_iterator_stop (list1);
				list_iterator_stop (list2);
				COMMIT_DEBUG2 (REPORT_ERRORS, LIST,
				               "list1 and list2 have different ntp_linked_bp chain lengths (%d,%d respectively) in ntp_list_concatenate",
				               list1_linked_bp_chain_length, list2_linked_bp_chain_length, false);
				list_destroy (new_list);
				FREE_DEBUG (new_list, "new_list in ntp_list_concatenate");
				return NULL;
			}
			
			if (list1_linked_bp_chain_length > MAX_CHAIN_MATCHES) {
				list_iterator_stop (list1);
				list_iterator_stop (list2);
				COMMIT_DEBUG1 (REPORT_ERRORS, LIST,
				               "list1/list2 have greater chain lengths (%d) than supported in ntp_list_concatenate",
				               list1_linked_bp_chain_length, false);
				list_destroy (new_list);
				FREE_DEBUG (new_list, "new_list in ntp_list_concatenate");
				return NULL;
			}
			
			#endif
			#ifndef NO_FULL_CHECKS
			
			if (list1_linked_bp_elements_num != list2_linked_bp_elements_num) {
				// though the chain of fp_/tp_elements instances per ntp_linked_bp may vary, the number of non-NULL fp_/tp_elements should be the same
				list_iterator_stop (list1);
				list_iterator_stop (list2);
				COMMIT_DEBUG2 (REPORT_ERRORS, LIST,
				               "list1 and list2 have a different number of ntp_linked_bp elements (%d,%d respectively) in ntp_list_concatenate",
				               list1_linked_bp_elements_num, list2_linked_bp_elements_num, false);
				list_destroy (new_list);
				FREE_DEBUG (new_list, "new_list in ntp_list_concatenate");
				return NULL;
			}
			
			#endif
			REGISTER
			uchar i, list1_elements_idx, list2_elements_idx, s;
			tmp1_linked_bp = NULL;
			tmp2_linked_bp = NULL;
			
			// now populate list1_/list2_* with bp/dist/length values
			do {
				if (!tmp1_linked_bp) {
					tmp1_linked_bp = list1_linked_bp;
					list1_elements_idx = 0;
					s = 0;
					
					for (i = 0; i < list1_linked_bp_chain_length; i += 2) {
						list1_chain[i] = tmp1_linked_bp->bp->fp_posn;
						list1_chain[i + 1] = tmp1_linked_bp->bp->tp_posn;
						list1_stacks[s] = tmp1_linked_bp->stack_len;
						s++;
						
						if (tmp1_linked_bp->fp_elements) {
							tmp_element = tmp1_linked_bp->fp_elements;
							
							while (tmp_element) {
								list1_elements[list1_elements_idx] = tmp_element->unpaired->dist;
								list1_elements[list1_elements_idx + 1] = tmp_element->unpaired->length;
								list1_elements_idx += 2;
								tmp_element = tmp_element->unpaired->next;
							}
						}
						
						if (tmp1_linked_bp->tp_elements) {
							tmp_element = tmp1_linked_bp->tp_elements;
							
							while (tmp_element) {
								list1_elements[list1_elements_idx] = tmp_element->unpaired->dist;
								list1_elements[list1_elements_idx + 1] = tmp_element->unpaired->length;
								list1_elements_idx += 2;
								tmp_element = tmp_element->unpaired->next;
							}
						}
						
						tmp1_linked_bp = tmp1_linked_bp->prev_linked_bp;
					}
				}
				
				if (!tmp2_linked_bp) {
					tmp2_linked_bp = list2_linked_bp;
					list2_elements_idx = 0;
					s = 0;
					
					for (i = 0; i < list2_linked_bp_chain_length; i += 2) {
						list2_chain[i] = tmp2_linked_bp->bp->fp_posn;
						list2_chain[i + 1] = tmp2_linked_bp->bp->tp_posn;
						list2_stacks[s] = tmp2_linked_bp->stack_len;
						s++;
						
						if (tmp2_linked_bp->fp_elements) {
							tmp_element = tmp2_linked_bp->fp_elements;
							
							while (tmp_element) {
								list2_elements[list2_elements_idx] = tmp_element->unpaired->dist;
								list2_elements[list2_elements_idx + 1] = tmp_element->unpaired->length;
								list2_elements_idx += 2;
								tmp_element = tmp_element->unpaired->next;
							}
						}
						
						if (tmp2_linked_bp->tp_elements) {
							tmp_element = tmp2_linked_bp->tp_elements;
							
							while (tmp_element) {
								list2_elements[list2_elements_idx] = tmp_element->unpaired->dist;
								list2_elements[list2_elements_idx + 1] = tmp_element->unpaired->length;
								list2_elements_idx += 2;
								tmp_element = tmp_element->unpaired->next;
							}
						}
						
						tmp2_linked_bp = tmp2_linked_bp->prev_linked_bp;
					}
				}
				
				i = 0;
				s = 0;
				
				while (i < list1_linked_bp_chain_length) {
					if (list1_chain[i] < list2_chain[i]) {
						// list1_linked_bp is 'before' list2_linked_bp -> add it
						new_linked_bp = list1_linked_bp;
						list1_linked_bp = list_iterator_next (list1);
						// signal that chain/elements of list1 need refresh
						tmp1_linked_bp = NULL;
						// reposition tmp2_linked_bp to 'start' of list2_linked_bp chain
						tmp2_linked_bp = list2_linked_bp;
						break;
					}
					
					else
						if (list2_chain[i] < list1_chain[i]) {
							// list2_linked_bp is 'before' list1_linked_bp -> add it
							new_linked_bp = list2_linked_bp;
							list2_linked_bp = list_iterator_next (list2);
							// signal that chain/elements of list2 need refresh
							tmp2_linked_bp = NULL;
							// reposition tmp1_linked_bp to 'start' of list1_linked_bp chain
							tmp1_linked_bp = list1_linked_bp;
							break;
						}
						
						else
							if (0 == (i % 2)) {
								// when fps and tps are equal, check for different stack lengths (only once - say, at the fp posn)
								if (list1_stacks[s] != list2_stacks[s]) {
									if (list1_stacks[s] < list2_stacks[s]) {
										// equal fp/tp, but list1 bp has smaller stack than list2 bp
										new_linked_bp = list1_linked_bp;
										list1_linked_bp = list_iterator_next (list1);
										tmp1_linked_bp = NULL;
										// reposition tmp2_linked_bp to 'start' of list2_linked_bp chain
										tmp2_linked_bp = list2_linked_bp;
										break;
									}
									
									else {
										// equal fp/tp, but list2 bp has smaller stack than list1 bp
										new_linked_bp = list2_linked_bp;
										list2_linked_bp = list_iterator_next (list2);
										tmp2_linked_bp = NULL;
										// reposition tmp1_linked_bp to 'start' of list1_linked_bp chain
										tmp1_linked_bp = list1_linked_bp;
										break;
									}
								}
								
								s++;
							}
							
					i++;
				}
				
				if (i == list1_linked_bp_chain_length) {
					// ntp_linked_bp chains are identical, but check for any discrepant fp_/tp_elements
					if (list1_linked_bp_elements_num) {
						// here we first use the # of fp_/tp_elements as rank/order
						if (list1_linked_bp_elements_length < list2_linked_bp_elements_length) {
							new_linked_bp = list1_linked_bp;
							list1_linked_bp = list_iterator_next (list1);
							tmp1_linked_bp = NULL;
							tmp2_linked_bp = list2_linked_bp;
						}
						
						else
							if (list2_linked_bp_elements_length < list1_linked_bp_elements_length) {
								new_linked_bp = list2_linked_bp;
								list2_linked_bp = list_iterator_next (list2);
								tmp2_linked_bp = NULL;
								tmp1_linked_bp = list1_linked_bp;
							}
							
							else {
								// same number of fp_/tp_elements -> check if identical or not
								i = 0;
								
								while (i < list1_linked_bp_elements_length) {
									if (list1_elements[i + 1] != list2_elements[i + 1] ||
									    // element lengths are different or
									    list1_elements[i] != list2_elements[i]) {     // element distances are different
										break;
									}
									
									i += 2;
								}
								
								if (i == list1_linked_bp_elements_length) {
									// exact same bp elements chain here
									new_linked_bp = list1_linked_bp;
									list1_linked_bp = list_iterator_next (list1);
									list2_linked_bp = list_iterator_next (list2);
									tmp1_linked_bp = NULL;
									tmp2_linked_bp = NULL;
								}
								
								else
									if (list1_elements[i] <
									    list2_elements[i]) {   // at least one of list1_element is less than the corresponding list2_element
										new_linked_bp = list1_linked_bp;
										list1_linked_bp = list_iterator_next (list1);
										tmp1_linked_bp = NULL;
										tmp2_linked_bp = list2_linked_bp;
									}
									
									else {
										// at least one of list2_element is less than the corresponding list1_element
										new_linked_bp = list2_linked_bp;
										list2_linked_bp = list_iterator_next (list2);
										tmp2_linked_bp = NULL;
										tmp1_linked_bp = list1_linked_bp;
									}
							}
					}
					
					else {
						// exact same bp chain here -> does not matter which one we add
						new_linked_bp = list1_linked_bp;
						list1_linked_bp = list_iterator_next (list1);
						list2_linked_bp = list_iterator_next (list2);
						tmp1_linked_bp = NULL;
						tmp2_linked_bp = NULL;
					}
				}
				
				#ifndef NO_FULL_CHECKS
				
				if (
				#endif
				                    list_append (new_list, duplicate_linked_bp (new_linked_bp, tag))
			                    #ifndef NO_FULL_CHECKS
				                    != 1) {
					COMMIT_DEBUG (REPORT_ERRORS, LIST,
					              "cannot append list1 or list2 element to new_list in ntp_list_concatenate",
					              false);
					list_iterator_stop (list1);
					list_iterator_stop (list2);
					// assumes bp's are allocated by tag, so no need to free, just destroy
					list_destroy (new_list);
					FREE_DEBUG (new_list, "new_list in ntp_list_concatenate");
					return NULL;
				}
				
			                    #else
				                    ;
			                    #endif
			}
			while (list1_linked_bp && list2_linked_bp);
			
			if (list1_linked_bp) {
				do {
					#ifndef NO_FULL_CHECKS
				
					if (
					#endif
					                    list_append (new_list, duplicate_linked_bp (list1_linked_bp, tag))
				                    #ifndef NO_FULL_CHECKS
					                    != 1) {
						COMMIT_DEBUG (REPORT_ERRORS, LIST,
						              "cannot append list1 element to new_list in ntp_list_concatenate", false);
						list_iterator_stop (list1);
						list_iterator_stop (list2);
						// assumes bp's are allocated by tag, so no need to free, just destroy
						list_destroy (new_list);
						FREE_DEBUG (new_list, "new_list in ntp_list_concatenate");
						return NULL;
					}
					
				                    #else
					                    ;
				                    #endif
					list1_linked_bp = list_iterator_next (list1);
				}
				while (list1_linked_bp);
			}
			
			else
				if (list2_linked_bp) {
					do {
						#ifndef NO_FULL_CHECKS
					
						if (
						#endif
						                    list_append (new_list, duplicate_linked_bp (list2_linked_bp, tag))
					                    #ifndef NO_FULL_CHECKS
						                    != 1) {
							COMMIT_DEBUG (REPORT_ERRORS, LIST,
							              "cannot append list2 element to new_list in ntp_list_concatenate", false);
							list_iterator_stop (list1);
							list_iterator_stop (list2);
							// assumes bp's are allocated by tag, so no need to free, just destroy
							list_destroy (new_list);
							FREE_DEBUG (new_list, "new_list in ntp_list_concatenate");
							return NULL;
						}
						
					                    #else
						                    ;
					                    #endif
						list2_linked_bp = list_iterator_next (list2);
					}
					while (list2_linked_bp);
				}
				
			list_iterator_stop (list1);
			list_iterator_stop (list2);
			return new_list;
		}
}

ntp_count_list ntp_count_list_concatenate (ntp_count_list restrict list1,
                                        ntp_count_list restrict list2) {
	if (!list1) {
		return list2;
	}
	
	else
		if (!list2) {
			return list1;
		}
		
		else {
			REGISTER ntp_count_list restrict new_list = MALLOC_DEBUG (sizeof (
			                                        nt_count_list), "new_list in ntp_count_list_concatenate");
			#ifndef NO_FULL_CHECKS
			                                        
			if (!new_list || (
			#endif
			                        list_init (new_list)
		                        #ifndef NO_FULL_CHECKS
			                        < 0)) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "cannot allocate memory/initialize new_list in ntp_count_list_concatenate",
				              false);
				              
				if (new_list) {
					FREE_DEBUG (new_list, "new_list in ntp_count_list_concatenate");
				}
				
				return NULL;
			}
			
			if (
		                        #else
			                        ;
		                        #endif
			                    list_attributes_seeker (new_list, &ntp_count_seeker)
		                    #ifndef NO_FULL_CHECKS
			                    < 0) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "could not set attributes seeker for new_list in ntp_count_list_concatenate",
				              false);
				list_destroy (new_list);
				FREE_DEBUG (new_list, "new_list in ntp_count_list_concatenate");
				return NULL;
			}
			
			if (
		                    #else
			                    ;
		                    #endif
			                    list_iterator_start (list1)
		                    #ifndef NO_FULL_CHECKS
			                    == 0) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "cannot iterate list1 in ntp_count_list_concatenate", false);
				list_destroy (new_list);
				FREE_DEBUG (new_list, "new_list in ntp_count_list_concatenate");
				return NULL;
			}
			
		                    #else
			                    ;
		                    #endif
			
			while (list_iterator_hasnext (list1)) {
				#ifndef NO_FULL_CHECKS
			
				if (
				#endif
				                    list_append (new_list, list_iterator_next (list1))
			                    #ifndef NO_FULL_CHECKS
				                    != 1) {
					COMMIT_DEBUG (REPORT_ERRORS, LIST,
					              "cannot append list1 element to new_list in ntp_count_list_concatenate", false);
					list_iterator_stop (list1);
					list_destroy (new_list);
					FREE_DEBUG (new_list, "new_list in ntp_count_list_concatenate");
					return NULL;
				}
				
			                    #else
				                    ;
			                    #endif
			}
			
			list_iterator_stop (list1);
			#ifndef NO_FULL_CHECKS
			
			if (
			#endif
			                    list_iterator_start (list2)
		                    #ifndef NO_FULL_CHECKS
			                    == 0) {
				COMMIT_DEBUG (REPORT_ERRORS, LIST,
				              "cannot iterate list2 in ntp_count_list_concatenate", false);
				list_destroy (new_list);
				FREE_DEBUG (new_list, "new_list in ntp_count_list_concatenate");
				return NULL;
			}
			
		                    #else
			                    ;
		                    #endif
			
			while (list_iterator_hasnext (list2)) {
				const REGISTER nt_rel_count *next_ntp_count = list_iterator_next (list2);
				
				if (!list_seek (new_list, next_ntp_count)) { // no double entries
					#ifndef NO_FULL_CHECKS
					if (
					#endif
					                    list_append (new_list, next_ntp_count)
				                    #ifndef NO_FULL_CHECKS
					                    != 1) {
						COMMIT_DEBUG (REPORT_ERRORS, LIST,
						              "cannot append list2 element to new_list in ntp_count_list_concatenate", false);
						list_iterator_stop (list2);
						list_destroy (new_list);
						FREE_DEBUG (new_list, "new_list in ntp_count_list_concatenate");
						return NULL;
					}
					
				                    #else
					                    ;
				                    #endif
				}
			}
			
			list_iterator_stop (list2);
			return new_list;
		}
}

ntp_linked_bp get_linked_bp_root (ntp_linked_bp restrict current_linked_bp,
                                  const char advanced_pair_track_id) {
	REGISTER
	ntp_linked_bp restrict root_linked_bp = current_linked_bp;
	
	while (root_linked_bp && root_linked_bp->track_id != advanced_pair_track_id) {
		root_linked_bp = root_linked_bp->prev_linked_bp;
	}
	
	return root_linked_bp;
}

void dump_linked_bp (ntp_linked_bp linked_bp, ntp_seq seq) {
	REGISTER
	ntp_linked_bp mfe_linked_bp = linked_bp;
	
	if (linked_bp) {
		do {
			printf ("%d,%d(%d) ", linked_bp->bp->fp_posn, linked_bp->bp->tp_posn,
			        linked_bp->stack_len);
			fflush (stdout);
			
			if (linked_bp->fp_elements) {
				REGISTER
				ntp_element this_element = linked_bp->fp_elements;
				printf ("fp elements [ ");
				fflush (stdout);
				
				do {
					if ((this_element->unpaired->i_constraint.reference->type == pseudoknot &&
					     this_element->unpaired->i_constraint.element_type == constraint_fp_element) ||
					    (this_element->unpaired->i_constraint.reference->type == base_triple &&
					     this_element->unpaired->i_constraint.element_type == constraint_fp_element)) {
						printf ("f_pk");
						fflush (stdout);
					}
					
					else
						if ((this_element->unpaired->i_constraint.reference->type == pseudoknot &&
						     this_element->unpaired->i_constraint.element_type == constraint_tp_element) ||
						    (this_element->unpaired->i_constraint.reference->type == base_triple &&
						     this_element->unpaired->i_constraint.element_type == constraint_tp_element)) {
							printf ("t_pk");
							fflush (stdout);
						}
						
						else
							if (this_element->unpaired->i_constraint.reference->type == base_triple &&
							    this_element->unpaired->i_constraint.element_type ==
							    constraint_single_element) {
								printf ("single");
								fflush (stdout);
							}
							
					printf ("(dist=%d,length=%d) ", this_element->unpaired->dist,
					        this_element->unpaired->length);
					fflush (stdout);
					this_element = this_element->unpaired->next;
				}
				while (this_element);
				
				printf ("] ");
				fflush (stdout);
			}
			
			if (linked_bp->tp_elements) {
				REGISTER
				ntp_element this_element = linked_bp->tp_elements;
				printf ("tp elements [ ");
				fflush (stdout);
				
				do {
					if ((this_element->unpaired->i_constraint.reference->type == pseudoknot &&
					     this_element->unpaired->i_constraint.element_type == constraint_fp_element) ||
					    (this_element->unpaired->i_constraint.reference->type == base_triple &&
					     this_element->unpaired->i_constraint.element_type == constraint_fp_element)) {
						printf ("f_pk");
						fflush (stdout);
					}
					
					else
						if ((this_element->unpaired->i_constraint.reference->type == pseudoknot &&
						     this_element->unpaired->i_constraint.element_type == constraint_tp_element) ||
						    (this_element->unpaired->i_constraint.reference->type == base_triple &&
						     this_element->unpaired->i_constraint.element_type == constraint_tp_element)) {
							printf ("t_pk");
							fflush (stdout);
						}
						
						else
							if (this_element->unpaired->i_constraint.reference->type == base_triple &&
							    this_element->unpaired->i_constraint.element_type ==
							    constraint_single_element) {
								printf ("single");
								fflush (stdout);
							}
							
					printf ("(dist=%d,length=%d) ", this_element->unpaired->dist,
					        this_element->unpaired->length);
					fflush (stdout);
					this_element = this_element->unpaired->next;
				}
				while (this_element);
				
				printf ("] ");
				fflush (stdout);
			}
			
			if (!linked_bp->fp_elements && !linked_bp->tp_elements) {
				printf ("no elements ");
				fflush (stdout);
			}
			
			linked_bp = linked_bp->prev_linked_bp;
			
			if (linked_bp) {
				printf ("-> ");
				fflush (stdout);
			}
			
			else {
				break;
			}
		}
		while (1);
		
		if (seq) {
			float this_mfe = get_turner_mfe_estimate (mfe_linked_bp, seq);
			
			if (this_mfe != STACK_MFE_FAILED) {
				printf ("\tMFE=%f", this_mfe);
				fflush (stdout);
			}
		}
		
		printf ("\n");
		fflush (stdout);
	}
}

bool dispose_linked_bp_copy (nt_model *restrict model, ntp_list list,
                             char *free_bp_reason_msg, char *free_list_reason_msg
#ifndef NO_FULL_CHECKS
	, char *failed_iteration_msg
#endif
                            ) {
	#ifndef NO_FULL_CHECKS
                            
	if (
	#endif
	                    list_iterator_start (list)
                    #ifndef NO_FULL_CHECKS
	) {
                    #else
	                    ;
                    #endif
		REGISTER
		ntp_linked_bp restrict linked_bp_copy, tmp_bp_copy;
		
		while (list_iterator_hasnext (list)) {
			linked_bp_copy = list_iterator_next (list);
			#ifndef NO_FULL_CHECKS
			
			if (linked_bp_copy) {
			#endif
			
				do {
					tmp_bp_copy = linked_bp_copy;
					
					if (tmp_bp_copy->fp_elements) {
						do {
							REGISTER
							ntp_element tmp_elements = tmp_bp_copy->fp_elements->unpaired->next;
							FREE_DEBUG (tmp_bp_copy->fp_elements->unpaired, free_list_reason_msg);
							FREE_DEBUG (tmp_bp_copy->fp_elements, free_list_reason_msg);
							tmp_bp_copy->fp_elements = tmp_elements;
						}
						while (tmp_bp_copy->fp_elements);
					}
					
					if (tmp_bp_copy->tp_elements) {
						do {
							REGISTER
							ntp_element tmp_elements = tmp_bp_copy->tp_elements->unpaired->next;
							FREE_DEBUG (tmp_bp_copy->tp_elements->unpaired, free_list_reason_msg);
							FREE_DEBUG (tmp_bp_copy->tp_elements, free_list_reason_msg);
							tmp_bp_copy->tp_elements = tmp_elements;
						}
						while (tmp_bp_copy->tp_elements);
					}
					
					FREE_DEBUG ((void *)linked_bp_copy->bp, free_bp_reason_msg);
					linked_bp_copy = linked_bp_copy->prev_linked_bp;
					FREE_DEBUG (tmp_bp_copy, free_bp_reason_msg);
				}
				while (linked_bp_copy);
				
				#ifndef NO_FULL_CHECKS
			}
			
				#endif
		}
		
		list_iterator_stop (list);
		list_destroy (list);
		FREE_DEBUG (list, free_list_reason_msg);
		return true;
		#ifndef NO_FULL_CHECKS
	}
	
	else {
		COMMIT_DEBUG (REPORT_ERRORS, SEARCH_SEQ, failed_iteration_msg, false);
	}
	
	return false;
		#endif
}

void dispose_linked_bp_copy_by_FE_threshold (nt_model *restrict model,
                                        const char *seq, ntp_list list, float FE_threshold,
                                        char *free_bp_reason_msg, char *free_list_reason_msg) {
	REGISTER
	nt_hit_count list_size = list ? (nt_hit_count) list->numels : 0,
	             current_list_idx = 0, current_num_hits = 0;
	REGISTER
	ntp_linked_bp restrict linked_bp_copy, tmp_bp_copy;
	
	while (current_list_idx < list_size) {
		linked_bp_copy = list_get_at (list, current_list_idx);
		float this_mfe = get_turner_mfe_estimate (linked_bp_copy, seq);
		
		if (current_num_hits < MAX_HITS_RETURNED &&
		    this_mfe <= FE_threshold) {
			/*
			 * hit is within both FE threshold and MAX_HITS_RETURNED limits
			 */
			current_list_idx++;
			current_num_hits++;
		}
		
		else {
			/*
			 * allowed number of hits exceeded or MFE is above threshold
			 */
			linked_bp_copy = list_extract_at (list, current_list_idx);
			
			do {
				tmp_bp_copy = linked_bp_copy;
				
				if (tmp_bp_copy->fp_elements) {
					do {
						REGISTER
						ntp_element tmp_elements = tmp_bp_copy->fp_elements->unpaired->next;
						FREE_DEBUG (tmp_bp_copy->fp_elements->unpaired, free_list_reason_msg);
						FREE_DEBUG (tmp_bp_copy->fp_elements, free_list_reason_msg);
						tmp_bp_copy->fp_elements = tmp_elements;
					}
					while (tmp_bp_copy->fp_elements);
				}
				
				if (tmp_bp_copy->tp_elements) {
					do {
						REGISTER
						ntp_element tmp_elements = tmp_bp_copy->tp_elements->unpaired->next;
						FREE_DEBUG (tmp_bp_copy->tp_elements->unpaired, free_list_reason_msg);
						FREE_DEBUG (tmp_bp_copy->tp_elements, free_list_reason_msg);
						tmp_bp_copy->tp_elements = tmp_elements;
					}
					while (tmp_bp_copy->tp_elements);
				}
				
				FREE_DEBUG ((void *) linked_bp_copy->bp, free_bp_reason_msg);
				linked_bp_copy = linked_bp_copy->prev_linked_bp;
				FREE_DEBUG (tmp_bp_copy, free_bp_reason_msg);
			}
			while (linked_bp_copy);
			
			// reduce list size
			list_size--;
		}
	}
}
