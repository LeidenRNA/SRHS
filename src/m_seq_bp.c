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

#define MAX_CONTAINMENT_ELEMENTS 10

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

/*
 * data structures
 */
typedef struct {
	ntp_list seq_list;
	nt_seq_hash seq_hash;
} nt_seq_bp_cache_entry, *ntp_seq_bp_cache_entry;

/*
 * globals
 */
static ntp_list seq_bp_cache = NULL;

/*
 * cache operations
 */
static inline int seq_hash_seeker (const void *restrict el,
                                   const void *restrict key) {
	const nt_seq_hash entry = ((ntp_seq_bp_cache_entry)el)->seq_hash;
	
	if (entry == * (nt_seq_hash *)key) {
		return 1;
	}
	
	return 0;
}

bool initialize_seq_bp_cache() {
	COMMIT_DEBUG (REPORT_INFO, SEQ_BP_CACHE,
	              "initializing seq_bp_cache in initialize_seq_bp_cache", true);
	// destroy a pre-existing seq bp cache
	finalize_seq_bp_cache();
	#ifdef MULTITHREADED_ON
	
	if (pthread_mutex_lock (&num_destruction_threads_mutex) == 0) {
	#endif
		seq_bp_cache = (ntp_list) MALLOC_DEBUG (sizeof (nt_list),
		                                        "seq_bp_cache in initialize_seq_bp_cache");
		                                        
		if (seq_bp_cache) {
			COMMIT_DEBUG (REPORT_INFO, SEQ_BP_CACHE,
			              "memory allocated for seq_bp_cache in initialize_seq_bp_cache", false);
			              
			if ((list_init (seq_bp_cache) < 0) ||
			    (list_attributes_seeker (seq_bp_cache, &seq_hash_seeker) < 0)) {
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&num_destruction_threads_mutex);
				#endif
				COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
				              "failed to initialize seq_bp_cache/set attribute seeker in initialize_seq_bp_cache",
				              false);
				FREE_DEBUG (seq_bp_cache,
				            "seq_bp_cache in initialize_seq_bp_cache [failed to initialize seq_bp_cache/set attribute seeker]");
				return false;
			}
			
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			#endif
			COMMIT_DEBUG (REPORT_INFO, SEQ_BP_CACHE,
			              "seq_bp_cache initialized and attribute seeker set in initialize_seq_bp_cache",
			              false);
			return true;
		}
		
		else {
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			#endif
			COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
			              "could not allocate memory for seq_bp_cache in initialize_seq_bp_cache", false);
			return false;
		}
		
		#ifdef MULTITHREADED_ON
	}
	
	return false;
		#endif
}

bool finalize_seq_bp_cache() {
	#ifdef MULTITHREADED_ON

	if (pthread_mutex_lock (&num_destruction_threads_mutex) == 0) {
	#endif
	
		if (seq_bp_cache) {
			COMMIT_DEBUG (REPORT_INFO, SEQ_BP_CACHE,
			              "finalizing seq_bp_cache in finalize_seq_bp_cache", true);
			list_iterator_start (seq_bp_cache);
			
			while (list_iterator_hasnext (seq_bp_cache)) {
				REGISTER
				ntp_seq_bp_cache_entry restrict this_cache_entry = (ntp_seq_bp_cache_entry)
				                                        list_iterator_next (seq_bp_cache);
				                                        
				if (this_cache_entry) {
					REGISTER
					ntp_list restrict this_list = this_cache_entry->seq_list;
					#ifdef DEBUG_MEM
					const REGISTER
					nt_seq_hash this_hash = this_cache_entry->seq_hash;
					#endif
					#ifdef DEBUG_ON
					REGISTER
					nt_seq_count seq_cnt = 1;
					#endif
					list_iterator_start (this_list);
					
					while (list_iterator_hasnext (this_list)) {
						#ifdef DEBUG_ON
						COMMIT_DEBUG2 (REPORT_INFO, SEQ_BP_CACHE,
						               "destroying seq (hash %ld) number %u from seq_list in this_cache_entry of seq_bp_cache in finalize_seq_bp_cache",
						               this_hash, seq_cnt, false);
						seq_cnt++;
						#endif
						destroy_seq_bp ((ntp_seq_bp)list_iterator_next (this_list));
					}
					
					list_iterator_stop (this_list);
					list_destroy (this_list);
					FREE_DEBUG (this_list,
					            "seq_list in this_cache_entry of seq_bp_cache in finalize_seq_bp_cache");
					FREE_DEBUG (this_cache_entry,
					            "this_cache_entry of seq_bp_cache in finalize_seq_bp_cache");
				}
			}
			
			list_iterator_stop (seq_bp_cache);
			list_destroy (seq_bp_cache);
			FREE_DEBUG (seq_bp_cache, "seq_bp_cache in finalize_seq_bp_cache");
			seq_bp_cache = NULL;
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			#endif
			COMMIT_DEBUG (REPORT_INFO, SEQ_BP_CACHE,
			              "seq_bp_cache finalized in finalize_seq_bp_cache", false);
			return true;
		}
		
		else {
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			#endif
			return false;
		}
		
		#ifdef MULTITHREADED_ON
	}
	
	return false;
		#endif
}

bool purge_seq_bp_cache_by_model (nt_model *restrict model) {
	#ifdef MULTITHREADED_ON

	if (pthread_mutex_lock (&num_destruction_threads_mutex) == 0) {
	#endif
	
		if (seq_bp_cache) {
			COMMIT_DEBUG (REPORT_INFO, SEQ_BP_CACHE,
			              "purging seq_bp_cache in purge_seq_bp_cache_by_model", true);
			list_iterator_start (seq_bp_cache);
			
			while (list_iterator_hasnext (seq_bp_cache)) {
				REGISTER
				ntp_seq_bp_cache_entry restrict this_cache_entry = (ntp_seq_bp_cache_entry)
				                                        list_iterator_next (seq_bp_cache);
				                                        
				if (this_cache_entry) {
					REGISTER
					ntp_list restrict this_list = this_cache_entry->seq_list;
					REGISTER
					unsigned int num_els = this_list->numels, this_el_idx = 0;
					
					while (this_el_idx < num_els) {
						ntp_seq_bp this_seq_bp = (ntp_seq_bp)list_get_at (this_list, this_el_idx);
						
						if (this_seq_bp->model == model) {
							this_seq_bp = (ntp_seq_bp)list_extract_at (this_list, this_el_idx);
							destroy_seq_bp (this_seq_bp);
							num_els--;
						}
						
						else {
							this_el_idx++;
						}
					}
				}
			}
			
			list_iterator_stop (seq_bp_cache);
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			#endif
			COMMIT_DEBUG (REPORT_INFO, SEQ_BP_CACHE,
			              "seq_bp_cache purged by model in purge_seq_bp_cache_by_model", false);
			return true;
		}
		
		else {
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			#endif
			return false;
		}
		
		#ifdef MULTITHREADED_ON
	}
	
	return false;
		#endif
}

ntp_bp_list_by_element create_seq_bp_stack_by_element (ntp_seq_bp restrict
                                        seq_bp, const uchar idx,
                                        nt_stack_idist  this_stack_idist, short this_in_extrusion,
                                        ntp_element this_element) {
	REGISTER
	bool stack_already_exists = false, list_by_element_already_exists = false;
	REGISTER
	ntp_stack that_stack = NULL, existing_stack = NULL;
	REGISTER
	ntp_bp_list_by_element that_list_by_element = NULL;
	
	if (seq_bp->stacks[idx].numels) {
		list_iterator_start (&seq_bp->stacks[idx]);
		
		while (list_iterator_hasnext (&seq_bp->stacks[idx])) {
			that_stack = (ntp_stack)list_iterator_next (&seq_bp->stacks[idx]);
			
			if (that_stack->stack_idist == this_stack_idist &&
			    that_stack->in_extrusion == this_in_extrusion) {
				stack_already_exists = true;
				existing_stack = that_stack;
				that_list_by_element = that_stack->lists;
				
				while (that_list_by_element) {
					if (that_list_by_element->el == this_element) {
						list_by_element_already_exists = true;
						break;
					}
					
					that_list_by_element = that_list_by_element->next;
				}
				
				if (list_by_element_already_exists) {
					break;
				}
			}
		}
		
		list_iterator_stop (&seq_bp->stacks[idx]);
	}
	
	if (!list_by_element_already_exists) {
		if (!stack_already_exists) {
			that_stack = MALLOC_DEBUG (sizeof (nt_stack),
			                           "stack of seq_bp in create_seq_bp_list");
			                           
			if (!that_stack || 0 > list_append (&seq_bp->stacks[idx], that_stack)) {
				if (that_stack) {
					FREE_DEBUG (that_stack,
					            "stack of seq_bp in create_seq_bp_list [unable to append to seq_bp->stacks]");
				}
				
				return NULL;
			}
			
			that_stack->stack_idist = this_stack_idist;
			that_stack->in_extrusion = this_in_extrusion;
			that_stack->lists = NULL;
		}
		
		else {
			that_stack = existing_stack;
		}
		
		that_list_by_element = MALLOC_DEBUG (sizeof (nt_bp_list_by_element),
		                                     "nt_bp_list_by_element of stack of seq_bp in create_seq_bp_list");
		                                     
		if (!that_list_by_element || list_init (& (that_list_by_element->list)) != 0) {
			if (that_list_by_element) {
				FREE_DEBUG (that_list_by_element,
				            "nt_list_by_element of stack of seq_bp in create_seq_bp_list [unable to initialize list]");
			}
			
			if (!stack_already_exists) {
				list_delete_at (&seq_bp->stacks[idx], (seq_bp->stacks[idx].numels) - 1);
				FREE_DEBUG (that_stack,
				            "stack of seq_bp in create_seq_bp_list [unable to allocate nt_bp_list_by_element]");
			}
			
			return NULL;
		}
		
		that_list_by_element->stack_counts = 0;
		that_list_by_element->el = this_element;
		
		if (that_stack->lists) {
			that_list_by_element->next = that_stack->lists;
		}
		
		else {
			that_list_by_element->next = NULL;
		}
		
		that_stack->lists = that_list_by_element;
	}
	
	return that_list_by_element;
}

bool initialize_seq_bp_stacks (ntp_seq_bp restrict seq_bp, const uchar idx) {
	if (list_init (&seq_bp->stacks[idx]) != 0) {
		return false;
	}
	
	return true;
}

static inline void destroy_seq_bp_stacks (ntp_seq_bp restrict seq_bp,
                                        const uchar idx) {
	list_iterator_start (&seq_bp->stacks[idx]);
	
	while (list_iterator_hasnext (&seq_bp->stacks[idx])) {
		// free ntp_bps
		REGISTER ntp_stack this_stack = list_iterator_next (&seq_bp->stacks[idx]);
		REGISTER ntp_bp_list_by_element this_list_by_element = this_stack->lists;
		
		while (this_list_by_element) {
			REGISTER nt_list *restrict this_list = &this_list_by_element->list;
			list_iterator_start (this_list);
			
			while (list_iterator_hasnext (this_list)) {
				REGISTER
				ntp_bp restrict this_bp = (ntp_bp) list_iterator_next (this_list);
				
				if (this_bp) {
					FREE_DEBUG (this_bp, NULL);
				}
			}
			
			list_iterator_stop (this_list);
			list_destroy (this_list);
			ntp_bp_list_by_element tmp_list_by_element = this_list_by_element;
			this_list_by_element = this_list_by_element->next;
			FREE_DEBUG (tmp_list_by_element,
			            "ntp_list_by_element of stack of seq_bp in destroy_seq_bp_stacks");
		}
		
		FREE_DEBUG (this_stack, "stack of seq_bp in destroy_seq_bp_stacks");
	}
	
	list_iterator_stop (&seq_bp->stacks[idx]);
	list_destroy (&seq_bp->stacks[idx]);
}

void destroy_seq_bp (ntp_seq_bp restrict seq_bp) {
	if (seq_bp) {
		#ifdef DEBUG_MEM
		unsigned long s_num_entries, e_num_entries, s_alloc_size, e_alloc_size;
		MALLOC_CP (&s_num_entries, &s_alloc_size);
		#endif
		
		for (REGISTER nt_stack_size l = 0; l < MAX_STACK_LEN; l++) {
			destroy_seq_bp_stacks (seq_bp, l);
		}
		
		#ifdef DEBUG_MEM
		char msg[100];
		MALLOC_CP (&e_num_entries, &e_alloc_size);
		sprintf (msg,
		         "%lu mem entries freed (total size is %lu) for seq in destroy_seq_bp",
		         s_num_entries - e_num_entries, s_alloc_size - e_alloc_size);
		COMMIT_DEBUG (REPORT_INFO, SEQ_BP_CACHE, msg, false);
		#endif
		FREE_DEBUG ((void *) (seq_bp->sequence), "seq of seq_bp in destroy_seq_bp");
		FREE_DEBUG (seq_bp, "seq_bp in destroy_seq_bp");
	}
}

bool get_seq_bp_from_cache (const ntp_seq restrict seq, const nt_seq_hash hash,
                            const nt_model *restrict model,
                            ntp_list *restrict min_stack_dist, ntp_list *restrict max_stack_dist,
                            ntp_list *restrict in_extrusion, ntp_list *restrict dist_els,
                            REGISTER ntp_seq_bp restrict *seq_bp) {
	COMMIT_DEBUG1 (REPORT_INFO, SEQ_BP_CACHE,
	               "getting seq_bp of seq (hash %lu) from cache in get_seq_bp_from_cache", hash,
	               true);
	               
	if (!seq_bp_cache) {
		COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
		              "seq_bp_cache not initialized in get_seq_bp_from_cache", false);
		*seq_bp = NULL;
		return false;
	}
	
	#ifdef MULTITHREADED_ON
	
	if (pthread_mutex_lock (&num_destruction_threads_mutex) == 0) {
	#endif
		const REGISTER
		nt_seq_bp_cache_entry *restrict entry = (ntp_seq_bp_cache_entry) list_seek (
		                                        seq_bp_cache, &hash);
		                                        
		if (entry) {
			COMMIT_DEBUG1 (REPORT_INFO, SEQ_BP_CACHE,
			               "entry for seq (hash %lu) found in cache in get_seq_bp_from_cache", hash,
			               false);
			REGISTER
			ntp_list restrict this_list = entry->seq_list;
			list_iterator_start (this_list);
			
			while (list_iterator_hasnext (this_list)) {
				*seq_bp = list_iterator_next (this_list);
				
				if ((*seq_bp)->model == model && !strcmp ((*seq_bp)->sequence, seq)) {
					list_iterator_stop (this_list);
					REGISTER
					bool dist_match;
					REGISTER
					nt_stack_size i = 0;
					
					for (; i < MAX_STACK_LEN; i++) {
						dist_match = true;
						list_iterator_start (min_stack_dist[i]);
						list_iterator_start (max_stack_dist[i]);
						list_iterator_start (in_extrusion[i]);
						list_iterator_start (dist_els[i]);
						
						while (list_iterator_hasnext (min_stack_dist[i])) {
							REGISTER
							short this_in_extrusion = * ((short *)list_iterator_next (in_extrusion[i]));
							REGISTER
							nt_stack_idist this_min_stack_dist = * ((ntp_stack_idist)list_iterator_next (
							                                        min_stack_dist[i])),
							                                     this_max_stack_dist = * ((ntp_stack_idist)list_iterator_next (
							                                                                             max_stack_dist[i]));
							REGISTER
							ntp_element this_el = ((ntp_element)list_iterator_next (dist_els[i]));
							
							for (REGISTER nt_stack_idist this_stack_idist = this_min_stack_dist;
							     this_stack_idist <= this_max_stack_dist; this_stack_idist++) {
								dist_match = false;
								list_iterator_start (& (*seq_bp)->stacks[i]);
								
								while (list_iterator_hasnext (& (*seq_bp)->stacks[i])) {
									REGISTER
									ntp_stack that_stack = list_iterator_next (& (*seq_bp)->stacks[i]);
									
									if (that_stack->stack_idist == this_stack_idist &&
									    that_stack->in_extrusion == this_in_extrusion) {
										REGISTER
										ntp_bp_list_by_element that_bp_list_by_element = that_stack->lists;
										
										while (that_bp_list_by_element) {
											if (that_bp_list_by_element->el == this_el) {
												dist_match = true;
												break;
											}
											
											that_bp_list_by_element = that_bp_list_by_element->next;
										}
										
										if (dist_match) {
											break;
										}
									}
								}
								
								list_iterator_stop (& (*seq_bp)->stacks[i]);
								
								if (!dist_match) {
									break;
								}
							}
						}
						
						list_iterator_stop (min_stack_dist[i]);
						list_iterator_stop (max_stack_dist[i]);
						list_iterator_stop (in_extrusion[i]);
						list_iterator_stop (dist_els[i]);
						
						if (!dist_match) {
							break;
						}
					}
					
					if (i == MAX_STACK_LEN) {
						COMMIT_DEBUG1 (REPORT_INFO, SEQ_BP_CACHE,
						               "matching seq nt/seq_bp found in cache entry for seq (hash %lu) in get_seq_bp_from_cache",
						               hash, false);
					}
					
					else {
						#ifdef MULTITHREADED_ON
						pthread_mutex_unlock (&num_destruction_threads_mutex);
						#endif
						COMMIT_DEBUG1 (REPORT_INFO, SEQ_BP_CACHE,
						               "incomplete seq_bp for seq nt found in cache entry for seq (hash %lu) in get_seq_bp_from_cache",
						               hash, false);
						return false;
					}
					
					#ifdef MULTITHREADED_ON
					pthread_mutex_unlock (&num_destruction_threads_mutex);
					#endif
					return true;
				}
			}
			
			list_iterator_stop (this_list);
		}
		
		COMMIT_DEBUG1 (REPORT_INFO, SEQ_BP_CACHE,
		               "seq_bp for given seq nt not found in cache entry for seq (hash %lu) in get_seq_bp_from_cache",
		               hash, false);
		*seq_bp = NULL;
		#ifdef MULTITHREADED_ON
		pthread_mutex_unlock (&num_destruction_threads_mutex);
	}
	
		#endif
	return false;
}

nt_seq_count add_seq_bp_to_cache (const ntp_seq restrict seq,
                                  const nt_seq_hash hash, ntp_seq_bp restrict seq_bp) {
	COMMIT_DEBUG1 (REPORT_INFO, SEQ_BP_CACHE,
	               "adding seq_bp of seq (hash %lu) to cache in add_seq_bp_to_cache", hash, true);
	               
	if (!seq_bp_cache) {
		COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
		              "cache not initialized in add_seq_bp_to_cache", false);
		return 0;
	}
	
	#ifdef MULTITHREADED_ON
	
	if (pthread_mutex_lock (&num_destruction_threads_mutex) == 0) {
	#endif
		// seq already in cache?
		REGISTER
		ntp_seq_bp restrict this_seq_bp = NULL;
		REGISTER
		ntp_seq_bp_cache_entry restrict seq_bp_cache_entry = (ntp_seq_bp_cache_entry)
		                                        list_seek (seq_bp_cache, &hash);
		REGISTER
		ntp_list restrict this_list = NULL;
		
		if (seq_bp_cache_entry) {
			COMMIT_DEBUG1 (REPORT_ERRORS, SEQ_BP_CACHE,
			               "found seq_bp_cache_entry in seq_bp_cache for seq with hash (%lu) in add_seq_bp_to_cache",
			               hash, false);
			this_list = seq_bp_cache_entry->seq_list;
			
			if (this_list) {
				REGISTER nt_list_posn this_list_size = this_list->numels;
				
				while (this_list_size) {
					this_seq_bp = list_get_at (this_list, this_list_size - 1);
					
					if (this_seq_bp->model == seq_bp->model &&
					    !strcmp (this_seq_bp->sequence, seq)) {
						if (this_seq_bp == seq_bp) {
							#ifdef MULTITHREADED_ON
							pthread_mutex_unlock (&num_destruction_threads_mutex);
							#endif
							COMMIT_DEBUG (REPORT_WARNINGS, SEQ_BP_CACHE,
							              "this_seq_bp in seq_list of seq_bp_cache_entry is identical to seq_bp and not replaced in add_seq_bp_to_cache",
							              false);
							return 1; // return positive value, given that original seq_bp is kept in place
						}
						
						destroy_seq_bp (this_seq_bp);
						
						if (list_delete_at (this_list, this_list_size - 1) != 0) {
							#ifdef MULTITHREADED_ON
							pthread_mutex_unlock (&num_destruction_threads_mutex);
							#endif
							COMMIT_DEBUG (REPORT_INFO, SEQ_BP_CACHE,
							              "could not delete (to replace) this_seq_bp in seq_list of seq_bp_cache_entry in seq_bp_cache in add_seq_bp_to_cache",
							              false);
							return 0;
						}
						
						if (list_insert_at (this_list, seq_bp, this_list_size - 1) < 0) {
							COMMIT_DEBUG1 (REPORT_INFO, SEQ_BP_CACHE,
							               "appended seq_bp (at position %lu) to seq_list of seq_bp_cache_entry in seq_bp_cache in add_seq_bp_to_cache",
							               this_list_size, false);
						}
						
						#ifdef MULTITHREADED_ON
						pthread_mutex_unlock (&num_destruction_threads_mutex);
						#endif
						return (nt_seq_count) (list_locate (seq_bp_cache, seq_bp_cache_entry)) + 1;
					}
					
					this_list_size--;
				}
			}
			
			COMMIT_DEBUG1 (REPORT_INFO, SEQ_BP_CACHE,
			               "given seq not found in seq_bp_cache_entry of seq_bp_cache for seq with hash (%lu) in add_seq_bp_to_cache",
			               hash, false);
		}
		
		// create new cache entry if required
		if (!seq_bp_cache_entry) {
			// create new seq entry and add to cache
			seq_bp_cache_entry = MALLOC_DEBUG (sizeof (nt_seq_bp_cache_entry),
			                                   "seq_bp_cache_entry of seq_bp_cache in add_seq_bp_to_cache");
			                                   
			if (!seq_bp_cache_entry) {
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&num_destruction_threads_mutex);
				#endif
				COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
				              "could not allocate memory for seq_bp_cache_entry of seq_bp_cache in add_seq_bp_to_cache",
				              false);
				return 0;
			}
			
			COMMIT_DEBUG (REPORT_INFO, SEQ_BP_CACHE,
			              "allocated seq_bp_cache_entry for seq_bp_cache in add_seq_bp_to_cache", false);
			seq_bp_cache_entry->seq_hash = hash;
			seq_bp_cache_entry->seq_list = MALLOC_DEBUG (sizeof (nt_list),
			                                        "seq_list in seq_bp_cache_entry of seq_bp_cache in add_seq_bp_to_cache");
			this_list = seq_bp_cache_entry->seq_list;
			
			if (this_list) {
				if ((list_init (this_list) < 0) ||
				    (list_append (seq_bp_cache, seq_bp_cache_entry) < 0)) {
					#ifdef MULTITHREADED_ON
					pthread_mutex_unlock (&num_destruction_threads_mutex);
					#endif
					COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
					              "could not init/append seq_list in seq_bp_cache_entry of seq_bp_cache in add_seq_bp_to_cache",
					              false);
					FREE_DEBUG (seq_bp_cache_entry,
					            "seq_bp_cache_entry of seq_bp_cache in add_seq_bp_to_cache [failed to init/append seq_list]");
					return 0;
				}
				
				else {
					COMMIT_DEBUG (REPORT_INFO, SEQ_BP_CACHE,
					              "allocated seq_list in seq_bp_cache_entry of seq_bp_cache in add_seq_bp_to_cache",
					              false);
				}
			}
			
			else {
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&num_destruction_threads_mutex);
				#endif
				COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
				              "could not allocate memory for seq_list in seq_bp_cache_entry of seq_bp_cache in add_seq_bp_to_cache",
				              false);
				FREE_DEBUG (seq_bp_cache_entry,
				            "seq_bp_cache_entry of seq_bp_cache in add_seq_bp_to_cache [failed to allocate seq_list]");
				return 0;
			}
		}
		
		// append nt_seq_bp to seq_list of nt_seq_bp_cache_entry
		REGISTER int l;
		
		if ((list_append (this_list, seq_bp) < 0) ||
		    ((l = list_locate (seq_bp_cache, seq_bp_cache_entry)) < 0)) {
			COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
			              "could not append/confirm seq_bp to seq_list in seq_bp_cache_entry of add_seq_bp_to_cache",
			              false);
			              
			// destroy cache entry if empty
			if (!this_list->numels) {
				list_destroy (this_list);
				FREE_DEBUG (this_list,
				            "seq_list of seq_bp_cache_entry in seq_bp_cache in add_seq_bp_to_cache [failed to append bp to seq_list]");
				FREE_DEBUG (seq_bp_cache_entry,
				            "seq_bp_cache_entry of seq_bp_cache in add_seq_bp_to_cache [failed to append bp to seq_list]");
				COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
				              "removed seq_bp_cache_entry from seq_bp_cache in add_seq_bp_to_cache", false);
			}
			
			#ifdef MULTITHREADED_ON
			pthread_mutex_unlock (&num_destruction_threads_mutex);
			#endif
			return 0;
		}
		
		#ifdef MULTITHREADED_ON
		pthread_mutex_unlock (&num_destruction_threads_mutex);
		#endif
		COMMIT_DEBUG1 (REPORT_INFO, SEQ_BP_CACHE,
		               "appended seq_bp (at position %d) to seq_list of seq_bp_cache_entry in seq_bp_cache in add_seq_bp_to_cache",
		               l + 1, false);
		return ((nt_seq_count) l + 1);
		#ifdef MULTITHREADED_ON
	}
	
		#endif
	return 0;
}

bool get_seq_bp_from_seq (const ntp_seq restrict seq, nt_model *restrict model,
                          ntp_list *restrict min_stack_dist, ntp_list *restrict max_stack_dist,
                          ntp_list *restrict in_extrusion, ntp_list *restrict dist_els,
                          ntp_seq_bp *restrict seq_bp) {
	COMMIT_DEBUG (REPORT_INFO, SEQ_BP_CACHE,
	              "getting seq_bp from seq in get_seq_bp_from_seq", true);
	              
	if (!seq) {
		COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE, "seq is NULL in get_seq_bp_from_seq",
		              false);
		*seq_bp = NULL;
		return false;
	}
	
	const REGISTER
	nt_rel_seq_len seq_len = (nt_rel_seq_len) strlen (seq);
	
	if (!seq_len) {
		COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
		              "seq has 0 length in get_seq_bp_from_seq", false);
		*seq_bp = NULL;
		return false;
	}
	
	COMMIT_DEBUG1 (REPORT_INFO, SEQ_BP_CACHE,
	               "analyzing bps in seq of length %u in get_seq_bp_from_seq", seq_len, true);
	REGISTER
	bool from_cache = *seq_bp;
	#ifdef MULTITHREADED_ON
	
	if (pthread_mutex_lock (&num_destruction_threads_mutex) == 0) {
	#endif
	
		// allocation/initialization of nt_seq_bp
		if (!from_cache) {
			*seq_bp = MALLOC_DEBUG (sizeof (nt_seq_bp), "seq_bp in get_seq_bp_from_seq");
			
			if (!*seq_bp) {
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&num_destruction_threads_mutex);
				#endif
				COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
				              "could not allocate memory for seq_bp in get_seq_bp_from_seq", false);
				return false;
			}
			
			(*seq_bp)->sequence = MALLOC_DEBUG ((size_t) (sizeof (char) * (seq_len + 1)),
			                                    "seq of seq_bp in get_seq_bp_from_seq");
			                                    
			if (! (*seq_bp)->sequence) {
				COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
				              "could not allocate memory for seq of seq_bp in get_seq_bp_from_seq", false);
				FREE_DEBUG (*seq_bp,
				            "seq_bp in get_seq_bp_from_seq [failed to allocate memory for seq of seq_bp in get_seq_bp_from_seq]");
				*seq_bp = NULL;
				#ifdef MULTITHREADED_ON
				pthread_mutex_unlock (&num_destruction_threads_mutex);
				#endif
				return false;
			}
			
			strcpy ((char *) ((*seq_bp)->sequence), seq);
			(*seq_bp)->model = model;
			
			for (REGISTER nt_stack_size i = 0; i < MAX_STACK_LEN; i++) {
				if (!initialize_seq_bp_stacks (*seq_bp, i)) {
					COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
					              "cannot initialize seq_bp_bands in get_seq_bp_from_seq", false);
					// seq_bp not from cache -> can destroy here
					destroy_seq_bp (*seq_bp);
					*seq_bp = NULL;
					#ifdef MULTITHREADED_ON
					pthread_mutex_unlock (&num_destruction_threads_mutex);
					#endif
					return false;
				}
			}
		}
		
		REGISTER nt_stack_size stack_len;
		REGISTER nt fp_nt, tp_nt;
		REGISTER bool to_ignore;
		REGISTER nt_rel_seq_posn p, q;
		#ifdef DEBUG_MEM
		unsigned long s_num_entries, e_num_entries, s_alloc_size, e_alloc_size;
		MALLOC_CP (&s_num_entries, &s_alloc_size);
		#endif
		
		if (from_cache) {
			// clean up existing seq_bp counts that need to change
			for (stack_len = 0; stack_len < MAX_STACK_LEN; stack_len++) {
				REGISTER
				bool already_covered = true;
				list_iterator_start (min_stack_dist[stack_len]);
				list_iterator_start (max_stack_dist[stack_len]);
				list_iterator_start (in_extrusion[stack_len]);
				list_iterator_start (dist_els[stack_len]);
				
				while (list_iterator_hasnext (min_stack_dist[stack_len])) {
					REGISTER
					short this_in_extrusion = * ((short *)list_iterator_next (
					                                        in_extrusion[stack_len]));
					REGISTER
					nt_stack_idist this_min_stack_dist = * ((ntp_stack_idist)list_iterator_next (
					                                        min_stack_dist[stack_len])),
					                                     this_max_stack_dist = * ((ntp_stack_idist)list_iterator_next (
					                                                                             max_stack_dist[stack_len]));
					REGISTER
					ntp_element this_el = ((ntp_element)list_iterator_next (dist_els[stack_len]));
					
					for (REGISTER nt_stack_idist i = this_min_stack_dist; i <= this_max_stack_dist;
					     i++) {
						already_covered = false;
						list_iterator_start (& (*seq_bp)->stacks[stack_len]);
						
						while (list_iterator_hasnext (& (*seq_bp)->stacks[stack_len])) {
							REGISTER
							ntp_stack that_stack = list_iterator_next (& (*seq_bp)->stacks[stack_len]);
							
							// if seq_bp already has counts for min_stack_dist through to max_stack_dist
							// then skip this stack_len; otherwise destroy old seq_bp bands/lists and redo
							if (i == that_stack->stack_idist &&
							    this_in_extrusion == that_stack->in_extrusion) {
								REGISTER
								ntp_bp_list_by_element that_bp_list_by_element = that_stack->lists;
								
								while (that_bp_list_by_element) {
									if (that_bp_list_by_element->el == this_el) {
										already_covered = true;
										break;
									}
									
									that_bp_list_by_element = that_bp_list_by_element->next;
								}
								
								if (already_covered) {
									break;
								}
							}
						}
						
						list_iterator_stop (& (*seq_bp)->stacks[stack_len]);
						
						if (!already_covered) {
							break;
						}
					}
					
					if (!already_covered) {
						break;
					}
				}
				
				list_iterator_stop (min_stack_dist[stack_len]);
				list_iterator_stop (max_stack_dist[stack_len]);
				list_iterator_stop (in_extrusion[stack_len]);
				list_iterator_stop (dist_els[stack_len]);
				
				if (already_covered) {
					continue;
				}
				
				// destroy old seq_bp list before re-initializing a new one
				destroy_seq_bp_stacks (*seq_bp, stack_len);
				
				if (!initialize_seq_bp_stacks (*seq_bp, stack_len)) {
					#ifdef MULTITHREADED_ON
					pthread_mutex_unlock (&num_destruction_threads_mutex);
					#endif
					return false;
				}
			}
		}
		
		// find all stacks of MIN_STACK_LEN<=size<=MAX_STACK_LEN
		for (stack_len = MIN_STACK_LEN - 1; stack_len < MAX_STACK_LEN; stack_len++) {
			if (MIN_IDIST + ((stack_len + 1) * 2) > seq_len) {
				break;
			}
			
			list_iterator_start (min_stack_dist[stack_len]);
			list_iterator_start (max_stack_dist[stack_len]);
			list_iterator_start (in_extrusion[stack_len]);
			list_iterator_start (dist_els[stack_len]);
			
			while (list_iterator_hasnext (min_stack_dist[stack_len])) {
				REGISTER
				short this_in_extrusion = * ((short *)list_iterator_next (
				                                        in_extrusion[stack_len]));
				REGISTER
				nt_stack_idist this_min_stack_dist = * ((ntp_stack_idist)list_iterator_next (
				                                        min_stack_dist[stack_len])),
				                                     this_max_stack_dist = * ((ntp_stack_idist)list_iterator_next (
				                                                                             max_stack_dist[stack_len]));
				REGISTER
				ntp_element this_element = (ntp_element)list_iterator_next (
				                                        dist_els[stack_len]);
				                                        
				for (REGISTER nt_stack_idist this_stack_idist = this_min_stack_dist;
				     this_stack_idist <= this_max_stack_dist; this_stack_idist++) {
					REGISTER
					bool already_done = false;
					list_iterator_start (& (*seq_bp)->stacks[stack_len]);
					
					while (list_iterator_hasnext (& (*seq_bp)->stacks[stack_len])) {
						REGISTER
						ntp_stack that_stack = list_iterator_next (& (*seq_bp)->stacks[stack_len]);
						
						if (this_stack_idist == that_stack->stack_idist &&
						    this_in_extrusion == that_stack->in_extrusion) {
							REGISTER
							ntp_bp_list_by_element that_lists = that_stack->lists;
							
							while (that_lists) {
								if (that_lists->el == this_element) {
									already_done = true;
									break;
								}
								
								that_lists = that_lists->next;
							}
							
							if (already_done) {
								break;
							}
						}
					}
					
					list_iterator_stop (& (*seq_bp)->stacks[stack_len]);
					
					if (already_done) {
						continue;
					}
					
					REGISTER
					ntp_bp_list_by_element new_bp_list_by_element = create_seq_bp_stack_by_element (
					                                        *seq_bp, stack_len, this_stack_idist, this_in_extrusion, this_element
					                                        );
					                                        
					if (!new_bp_list_by_element) {
						COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
						              "could not create new_bp_list_by_element in get_seq_bp_from_seq", false);
						              
						if (!from_cache) {
							// seq_bp not from cache -> can destroy here
							destroy_seq_bp (*seq_bp);
							*seq_bp = NULL;
						}
						
						#ifdef MULTITHREADED_ON
						pthread_mutex_unlock (&num_destruction_threads_mutex);
						#endif
						list_iterator_stop (min_stack_dist[stack_len]);
						list_iterator_stop (max_stack_dist[stack_len]);
						list_iterator_stop (in_extrusion[stack_len]);
						list_iterator_stop (dist_els[stack_len]);
						return false;
					}
					
					REGISTER nt_hit_count hit_count = 0;
					
					/*
					 * if this_in_extrusion != 0 (+ve => intrusion, -ve => extrusion) make sure seq_len covers respective 5' and 3' regions as well;
					 * note that this_stack_idist *includes* any intruded nts
					 */
					if (seq_len + 1 > (this_stack_idist + ((stack_len + 1) * 2) +
					                   (this_in_extrusion < 0 ? - (this_in_extrusion * 2) : 0))) {
						for (p = (nt_rel_seq_posn) (this_in_extrusion < 0 ? -this_in_extrusion : 0);
						     p < seq_len + 1 - this_stack_idist - ((stack_len + 1) * 2) -
						     (this_in_extrusion < 0 ? -this_in_extrusion : 0); p++) {
							for (q = (nt_rel_seq_posn) (p + this_stack_idist + ((stack_len + 1) * 2) - 1);
							     q < SAFE_MIN (seq_len, p + this_stack_idist + ((stack_len + 1) * 2)); q++) {
								to_ignore = 0;
								
								// depending on whether this_in_extrusion is +ve or -ve;
								// add to r (intrude) when +ve
								// subtract from r (extrude) when -ve
								for (REGISTER short r = (short) (this_in_extrusion < 0 ? this_in_extrusion : 0);
								     r <= stack_len + (this_in_extrusion < 0 ? 0 : this_in_extrusion); r++) {
									fp_nt = seq[p + r];
									tp_nt = seq[q - r];
									
									if (! ((fp_nt == 'g' && (tp_nt == 'c' || tp_nt == 'u')) ||
									       (fp_nt == 'a' && tp_nt == 'u') ||
									       (tp_nt == 'g' && (fp_nt == 'c' || fp_nt == 'u')) ||
									       (tp_nt == 'a' && fp_nt == 'u'))) {
										to_ignore = 1;
										break;
									}
								}
								
								if (!to_ignore) {
									hit_count++;
									
									if (MAX_BP_PER_STACK_IDIST >= hit_count) {
										REGISTER
										bool success = false;
										REGISTER
										ntp_bp bp = MALLOC_DEBUG (sizeof (nt_bp), NULL);
										
										if (bp) {
											bp->fp_posn = (nt_rel_seq_posn) (p + 1);         // 1-indexed
											bp->tp_posn = (nt_rel_seq_posn) (q - stack_len + 1); // 1-indexed
											
											//                                            if (list_append (&new_stack->list, bp)>=0)
											if (list_append (&new_bp_list_by_element->list, bp) >= 0) {
												success = true;
											}
										}
										
										if (!success) {
											COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
											              "could not allocate memory for bp/append to list of seq_bp in get_seq_bp_from_seq",
											              false);
											              
											if (bp) {
												FREE_DEBUG (bp, NULL);
											}
											
											if (!from_cache) {
												// seq_bp not from cache -> can destroy here
												destroy_seq_bp (*seq_bp);
												*seq_bp = NULL;
											}
											
											#ifdef MULTITHREADED_ON
											pthread_mutex_unlock (&num_destruction_threads_mutex);
											#endif
											list_iterator_stop (min_stack_dist[stack_len]);
											list_iterator_stop (max_stack_dist[stack_len]);
											list_iterator_stop (in_extrusion[stack_len]);
											list_iterator_stop (dist_els[stack_len]);
											return false;
										}
									}
									
									else {
										COMMIT_DEBUG (REPORT_ERRORS, SEQ_BP_CACHE,
										              "MAX_BP_PER_STACK_IDIST exceeded for bp of seq_bp in get_seq_bp_from_seq",
										              false);
										return false;
									}
								}
							}
						}
					}
					
					new_bp_list_by_element->stack_counts += hit_count;
				}
			}
			
			list_iterator_stop (min_stack_dist[stack_len]);
			list_iterator_stop (max_stack_dist[stack_len]);
			list_iterator_stop (in_extrusion[stack_len]);
			list_iterator_stop (dist_els[stack_len]);
		}
		
		#ifdef DEBUG_MEM
		MALLOC_CP (&e_num_entries, &e_alloc_size);
		COMMIT_DEBUG2 (REPORT_INFO, SEQ_BP_CACHE,
		               "%lu mem entries allocated (total size is %lu) for seq in get_seq_bp_from_seq",
		               e_num_entries - s_num_entries, e_alloc_size - s_alloc_size, false);
		#endif
		#ifdef MULTITHREADED_ON
		pthread_mutex_unlock (&num_destruction_threads_mutex);
	}
	
		#endif
	return true;
}
