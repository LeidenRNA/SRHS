#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <math.h>
#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
#else
	#include <netdb.h>
#endif
#include "filter.h"
#include "util.h"
#include "distribute.h"
#include "sequence.h"
#include "m_analyse.h"
#include "m_optimize.h"
#include "m_search.h"
#include "m_seq_bp.h"

#define is_valid_nt_char(c) (((c)=='c' || (c)=='g' || (c)=='u' || (c)=='a'))

#define MIN_NUM_FILTER_THREADS          1U
#define MAX_NUM_FILTER_THREADS          254U                        // currently use 1 byte for transferring thread id+1, so max value here is 254

#define FILTER_NUM_SOCKET_CONNECTIONS   D_Q_NUM_SOCKET_THREADS      // match # socket connections with # q socket (threads)

#define FILTER_SOCKET_CONNECT_WAIT      1                           // ms to wait if all sockets are busy

static pthread_spinlock_t filter_spinlock;
static bool
sockets_used[FILTER_NUM_SOCKET_CONNECTIONS];            // which socket

#ifdef _WIN32
// documented bug in MINGW - need to manually add forward declarations
void WSAAPI freeaddrinfo (struct addrinfo *);
int WSAAPI getaddrinfo (const char *, const char *, const struct addrinfo *,
                        struct addrinfo **);
// documented bug in MINGW - need to manually add forward declarations

static SOCKET win_client_socket[FILTER_NUM_SOCKET_CONNECTIONS];
#else
static int unix_client_socket[FILTER_NUM_SOCKET_CONNECTIONS];
#endif

#define FILTER_LOCK_S    if (pthread_spin_lock (&filter_spinlock)) { DEBUG_NOW (REPORT_ERRORS, FILTER, "could not acquire filter spinlock"); } else {
#define FILTER_LOCK_E    if (pthread_spin_unlock (&filter_spinlock)) { DEBUG_NOW (REPORT_ERRORS, FILTER, "could not release filter spinlock"); pthread_exit (NULL); } }

typedef struct {
	const char     *seq_seg;
	// number of nt this seq seg spans;
	// though relative, uses absolute positioning for data-type range reasons
	nt_abs_count
	seq_seg_span;
	nt_abs_seq_posn
	// (0-indexed) absolute posn of the 1st nt in this segment, in relation to the entire sequence
	seq_seg_abs_posn,
	// (0-indexed) relative posn of the 1st unvisited nt within this segment;
	// though relative, uses absolute positioning for data-type range reasons
	seq_seg_extra_nt_rel_posn;
	nt_stack_size
	stack_min_size;                  // minimum number of bps in (largest found) stack
	nt_stack_size
	stack_max_size;                  // maximum number of bps in (largest found) stack
	nt_stack_idist
	stack_min_idist;                 // minimum number of nt in between stack closing bp
	nt_stack_idist
	stack_max_idist;                 // maximum number of nt in between stack closing bp
	nt_rel_count
	fp_lead_min_span;                // minimum number of leading (fp) nts before start of stack
	nt_rel_count
	fp_lead_max_span;                // maximum number of leading (fp) nts before start of stack
	nt_rel_count
	tp_trail_min_span;               // minimum number of trailing (tp) nts after end of stack
	nt_rel_count
	tp_trail_max_span;               // maximum number of trailing (tp) nts after end of stack
	nt_rt_bytes job_id;
	ntp_model model;
	ntp_element el_with_largest_stack;
	uchar thread_id;
} seq_seg_arg;

/*
 * static, inline replacements for memset/memcpy - silences google sanitizers
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

static void filter_submit_null_job (const nt_rt_bytes job_id) {
	REGISTER
	uchar i = 0, this_socket = 0;
	FILTER_LOCK_S
	
	do {
		if (!sockets_used[i]) {
			sockets_used[i] = true;
			this_socket = i;
			break;
		}
		
		else {
			i++;
			
			if (i == FILTER_NUM_SOCKET_CONNECTIONS) {
				sleep_ms (FILTER_SOCKET_CONNECT_WAIT);
				i = 0;
			}
		}
	}
	while (1);
	
	FILTER_LOCK_E
	uchar buf[FILTER_MSG_SIZE];
	
	for (uchar j = 0; j < 4; j++) {
		buf[  NUM_RT_BYTES + j] = (uchar) ((DISPATCH_NULL_JOB_POSN >> ((
		                                        3 - j) * 8)) & 0xff);
		buf[4 + NUM_RT_BYTES + j] = (uchar) ((DISPATCH_NULL_JOB_POSN >> ((
		                                        3 - j) * 8)) & 0xff);
	}
	
	for (uchar j = 0; j < NUM_RT_BYTES; j++) {
		buf[j] = (uchar) (job_id[j]);
	}
	
	#ifdef _WIN32
	send (win_client_socket[this_socket], buf, FILTER_MSG_SIZE, 0);
	#else
	send (unix_client_socket[this_socket], buf, FILTER_MSG_SIZE, 0);
	#endif
	FILTER_LOCK_S
	sockets_used[this_socket] = false;
	FILTER_LOCK_E
}

bool initialize_filter (const char *server, const unsigned short port) {
	DEBUG_NOW (REPORT_INFO, FILTER, "initializing filter");
	DEBUG_NOW (REPORT_INFO, FILTER, "initializing spinlock");
	
	if (pthread_spin_init (&filter_spinlock, PTHREAD_PROCESS_PRIVATE)) {
		DEBUG_NOW (REPORT_ERRORS, FILTER, "failed to initialize spinlock");
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, FILTER, "initializing sockets");
	#ifdef _WIN32
	// credit: https://docs.microsoft.com/en-us/windows/desktop/winsock/complete-client-code
	WSADATA wsaData;
	int iResult = WSAStartup (MAKEWORD (2, 2), &wsaData);
	
	if (iResult != 0) {
		DEBUG_NOW1 (REPORT_ERRORS, FILTER, "failed to startup WSA (error code %d)",
		            iResult);
		pthread_spin_destroy (&filter_spinlock);
		return false;
	}
	
	struct addrinfo hints, *addr_result;
	
	g_memset (&hints, 0, sizeof (hints));
	
	hints.ai_family = AF_INET;
	
	hints.ai_socktype = SOCK_STREAM;
	
	hints.ai_protocol = IPPROTO_TCP;
	
	hints.ai_flags = AI_PASSIVE;
	
	char port_strn[10];
	
	sprintf (port_strn, "%d", port);
	
	if (getaddrinfo (server, port_strn, &hints, &addr_result) != 0) {
		DEBUG_NOW1 (REPORT_ERRORS, FILTER,
		            "failed to resolve dispatch server address ('%s')", server);
		DEBUG_NOW (REPORT_INFO, FILTER, "finalizing sockets");
		WSACleanup();
		DEBUG_NOW (REPORT_INFO, FILTER, "finalizing spinlock");
		pthread_spin_destroy (&filter_spinlock);
		return false;
	}
	
	for (uchar i = 0; i < FILTER_NUM_SOCKET_CONNECTIONS; i++) {
		win_client_socket[i] = socket (hints.ai_family, hints.ai_socktype,
		                               hints.ai_protocol);
		                               
		if (win_client_socket[i] == INVALID_SOCKET ||
		    connect (win_client_socket[i], addr_result->ai_addr,
		             (int) addr_result->ai_addrlen) == SOCKET_ERROR) {
			DEBUG_NOW2 (REPORT_ERRORS, FILTER,
			            "failed to set up socket connection #%d (error code %d)", i + 1,
			            WSAGetLastError());
			DEBUG_NOW (REPORT_INFO, FILTER, "finalizing sockets");
			
			if (win_client_socket[i] != INVALID_SOCKET) {
				closesocket (win_client_socket[i]);
			}
			
			for (uchar j = 0; j < FILTER_NUM_SOCKET_CONNECTIONS; j++) {
				closesocket (win_client_socket[j]);
			}
			
			freeaddrinfo (addr_result);
			WSACleanup();
			DEBUG_NOW (REPORT_INFO, FILTER, "finalizing spinlock");
			pthread_spin_destroy (&filter_spinlock);
			return false;
		}
	}
	
	freeaddrinfo (addr_result);
	#else
	struct hostent *server_ent = gethostbyname (server);
	
	if (server_ent == NULL) {
		DEBUG_NOW1 (REPORT_ERRORS, FILTER,
		            "failed to resolve dispatch server address ('%s')", server);
		DEBUG_NOW (REPORT_INFO, FILTER, "finalizing spinlock");
		pthread_spin_destroy (&filter_spinlock);
		return false;
	}
	
	struct sockaddr_in serv_addr;
	
	g_memset (&serv_addr, 0, sizeof (serv_addr));
	
	serv_addr.sin_family = AF_INET;
	
	g_memcpy ((char *)&serv_addr.sin_addr.s_addr, (char *)server_ent->h_addr,
	          server_ent->h_length);
	
	serv_addr.sin_port = htons (port);
	
	#if Q_DISABLE_NAGLE
	int flag = 1;
	
	#endif
	for (ushort i = 0; i < FILTER_NUM_SOCKET_CONNECTIONS; i++) {
		if (((unix_client_socket[i] = socket (AF_INET, SOCK_STREAM, 0)) < 0) ||
	    #if Q_DISABLE_NAGLE
		    (setsockopt (unix_client_socket[i], IPPROTO_TCP, TCP_NODELAY, (char *)&flag,
		                 sizeof (int)) < 0) ||
	    #endif
		    (connect (unix_client_socket[i], (struct sockaddr *)&serv_addr,
		              sizeof (serv_addr)) < 0)) {
			DEBUG_NOW1 (REPORT_ERRORS, FILTER,
			            "failed to set up socket connection #%d", i + 1);
			DEBUG_NOW (REPORT_INFO, FILTER, "finalizing sockets");
	
			if (unix_client_socket[i] >= 0) {
				close (unix_client_socket[i]);
			}
	
			for (ushort j = 0; j < FILTER_NUM_SOCKET_CONNECTIONS; j++) {
				close (unix_client_socket[j]);
			}
	
			DEBUG_NOW (REPORT_INFO, FILTER, "finalizing spinlock");
			pthread_spin_destroy (&filter_spinlock);
			return false;
		}
	}
	
	#endif
	
	for (uchar i = 0; i < FILTER_NUM_SOCKET_CONNECTIONS; i++) {
		sockets_used[i] = false;
	}
	
	return true;
}

void finalize_filter() {
	DEBUG_NOW (REPORT_INFO, FILTER, "finalizing filter");
	DEBUG_NOW (REPORT_INFO, FILTER, "finalizing sockets");
	#ifdef _WIN32
	
	for (uchar i = 0; i < FILTER_NUM_SOCKET_CONNECTIONS; i++) {
		closesocket (win_client_socket[i]);
	}
	
	WSACleanup();
	#else
	
	for (ushort i = 0; i < FILTER_NUM_SOCKET_CONNECTIONS; i++) {
		close (unix_client_socket[i]);
	}
	
	#endif
	DEBUG_NOW (REPORT_INFO, FILTER, "finalizing spinlock");
	
	if (pthread_spin_destroy (&filter_spinlock)) {
		DEBUG_NOW (REPORT_ERRORS, FILTER, "could not destroy spinlock");
	}
}

static inline bool is_valid_bp (const char fp_char, const char tp_char) {
	return ((fp_char == 'g' && (tp_char == 'c' || tp_char == 'u')) ||
	        (fp_char == 'a' && tp_char == 'u') ||
	        (tp_char == 'g' && (fp_char == 'c' || fp_char == 'u')) ||
	        (tp_char == 'a' && fp_char == 'u'));
}

static inline bool get_fp_constraint_string (const char *seq,
                                        const nt_abs_seq_len seq_len,
                                        const bool fp_overlaps,
                                        const nt_abs_seq_posn curr_fp_posn_with_lead,
                                        const nt_s_rel_count curr_constraint_fp_offset,
                                        const nt_stack_size curr_constraint_stack_size,
                                        const nt_stack_size curr_stack_size,
                                        const nt_stack_idist curr_stack_idist,
                                        char *fp_strn) {
	if (fp_overlaps) {
		if ((nt_int)curr_constraint_fp_offset + (nt_int)curr_constraint_stack_size >
		    (nt_int)curr_stack_idist) {
			return false;
		}
		
		REGISTER
		const nt_abs_seq_posn // 5' (0-indexed) nt position of first nt 'inside' constraint stack
		curr_fp_posn_inside_stack = curr_fp_posn_with_lead + curr_stack_size;
		
		if ((nt_int)curr_fp_posn_inside_stack + (nt_int)curr_constraint_fp_offset < 0) {
			return false;
		}
		
		for (REGISTER
		     nt_abs_seq_posn p = curr_fp_posn_inside_stack + curr_constraint_fp_offset;
		     p < curr_fp_posn_inside_stack + curr_constraint_fp_offset +
		     curr_constraint_stack_size; p++) {
			fp_strn[p - (curr_fp_posn_inside_stack + curr_constraint_fp_offset)] = seq[p];
		}
	}
	
	else {
		if (curr_constraint_fp_offset < 0) {
			if ((nt_int)curr_fp_posn_with_lead + (nt_int)curr_constraint_fp_offset -
			    (nt_int)curr_constraint_stack_size < 0) {
				return false;
			}
			
			REGISTER
			nt_stack_size constraint_stack_size = 0;
			
			for (REGISTER
			     nt_abs_seq_posn p = curr_fp_posn_with_lead + curr_constraint_fp_offset -
			                         curr_constraint_stack_size;
			     p < curr_fp_posn_with_lead + curr_constraint_fp_offset; p++) {
				fp_strn[constraint_stack_size++] = seq[p];
			}
		}
		
		else {
			REGISTER
			const nt_abs_seq_posn curr_fp_posn_after_stack = curr_fp_posn_with_lead +
			                                        (curr_stack_size * 2) + curr_stack_idist;
			                                        
			if (curr_fp_posn_after_stack +
			    curr_constraint_fp_offset +
			    curr_constraint_stack_size > seq_len) {
				return false;
			}
			
			for (REGISTER
			     nt_abs_seq_posn p = curr_fp_posn_after_stack + curr_constraint_fp_offset;
			     p <= curr_fp_posn_after_stack + curr_constraint_fp_offset +
			     curr_constraint_stack_size; p++) {
				fp_strn[p - (curr_fp_posn_after_stack + curr_constraint_fp_offset)] = seq[p];
			}
		}
	}
	
	return true;
}

static inline bool get_constraint_string_relative_to_fp_posn (const char *seq,
                                        const nt_abs_seq_len seq_len,
                                        const bool el_overlaps,
                                        const nt_abs_seq_posn curr_fp_posn_with_lead,
                                        const nt_abs_seq_posn curr_constraint_fp_posn,
                                        const nt_s_rel_count curr_constraint_dist,
                                        const nt_stack_size curr_constraint_stack_size,
                                        const nt_stack_size curr_stack_size,
                                        const nt_stack_idist curr_stack_idist,
                                        char *el_strn) {
	if (el_overlaps) {
		/*
		 * when a constraint overlaps with a containing element, we must check that
		 * curr_constraint_fp_posn+curr_constraint_dist(plus curr_constraint_stack_size,
		 * depending on whether curr_constraint_dist is -ve or not) lies within
		 * the bounds of the containing element (curr_fp_posn_with_lead+
		 * curr_stack_size+curr_stack_idist)
		 */
		if ((curr_constraint_dist > 0 &&
		     curr_constraint_fp_posn + curr_constraint_stack_size + curr_constraint_dist >
		     curr_fp_posn_with_lead + curr_stack_size + curr_stack_idist) ||
		    (curr_constraint_dist < 0 &&
		     (nt_int)curr_constraint_fp_posn + (nt_int)curr_constraint_dist - (nt_int)1 >
		     curr_fp_posn_with_lead + curr_stack_size + curr_stack_idist)) {
			return false;
		}
	}
	
	if (curr_constraint_dist < 0) {
		REGISTER
		int64_t constraint_start_fp = (nt_int)curr_constraint_fp_posn +
		                              (nt_int)curr_constraint_dist - (nt_int)curr_constraint_stack_size;
		                              
		if (constraint_start_fp < 0 ||
		    seq_len < curr_constraint_fp_posn + curr_constraint_dist) {
			return false;
		}
		
		REGISTER
		nt_stack_size constraint_stack_size = 0;
		
		for (REGISTER nt_abs_seq_posn p = constraint_start_fp;
		     p < curr_constraint_fp_posn + curr_constraint_dist; p++) {
			el_strn[constraint_stack_size++] = seq[p];
		}
	}
	
	else {
		if (curr_constraint_fp_posn +
		    curr_constraint_stack_size +
		    curr_constraint_dist > seq_len ||
		    seq_len < curr_constraint_fp_posn + (curr_constraint_stack_size * 2) +
		    curr_constraint_dist) {
			return false;
		}
		
		REGISTER
		nt_stack_size constraint_stack_size = 0;
		
		for (REGISTER
		     nt_abs_seq_posn p = curr_constraint_fp_posn + curr_constraint_stack_size +
		                         curr_constraint_dist;
		     p < curr_constraint_fp_posn + (curr_constraint_stack_size * 2) +
		     curr_constraint_dist; p++) {
			el_strn[constraint_stack_size++] = seq[p];
		}
	}
	
	return true;
}

static inline bool has_matching_constraint (
                    const char *seq,
                    const nt_abs_seq_len seq_len,
                    const nt_abs_seq_posn curr_fp_posn_with_lead,
                    const nt_stack_size curr_stack_size, const nt_stack_idist curr_stack_idist,
                    const nt_s_rel_count constraint_fp_offset_min,
                    const nt_s_rel_count constraint_fp_offset_max, const bool fp_overlaps,
                    const nt_s_rel_count constraint_tp_dist_min,
                    const nt_s_rel_count constraint_tp_dist_max, const bool tp_overlaps,
                    const nt_s_rel_count constraint_stack_min,
                    const nt_s_rel_count constraint_single_dist_min,
                    const nt_s_rel_count constraint_single_dist_max,
                    const bool has_single, const bool single_overlaps) {
	char fp_strn[MAX_STACK_LEN], tp_strn[MAX_STACK_LEN], single_strn[1];
	REGISTER
	nt_s_rel_count fp_min, fp_max, tp_dist_min, tp_dist_max, single_dist_min,
	               single_dist_max;
	               
	// swap constraint min/max when -ve
	if (constraint_fp_offset_min < constraint_fp_offset_max) {
		fp_min = constraint_fp_offset_min;
		fp_max = constraint_fp_offset_max;
	}
	
	else {
		fp_min = constraint_fp_offset_max;
		fp_max = constraint_fp_offset_min;
	}
	
	if (constraint_tp_dist_min < constraint_tp_dist_max) {
		tp_dist_min = constraint_tp_dist_min;
		tp_dist_max = constraint_tp_dist_max;
	}
	
	else {
		tp_dist_min = constraint_tp_dist_max;
		tp_dist_max = constraint_tp_dist_min;
	}
	
	if (constraint_single_dist_min < constraint_single_dist_max) {
		single_dist_min = constraint_single_dist_min;
		single_dist_max = constraint_single_dist_max;
	}
	
	else {
		single_dist_min = constraint_single_dist_max;
		single_dist_max = constraint_single_dist_min;
	}
	
	// iterate over all fp offset values
	for (REGISTER nt_s_rel_count constraint_fp_offset = fp_min;
	     constraint_fp_offset <= fp_max; constraint_fp_offset++)
	     
		// iterate over all tp dist values
		for (REGISTER nt_s_rel_count constraint_tp_dist = tp_dist_min;
		     constraint_tp_dist <= tp_dist_max; constraint_tp_dist++)
		     
			// iterate over all single dist values (when element present/dist initialized)
			for (REGISTER nt_s_rel_count constraint_single_dist = single_dist_min;
			     constraint_single_dist <= single_dist_max; constraint_single_dist++) {
				if (get_fp_constraint_string (seq, seq_len,
				                              fp_overlaps,
				                              curr_fp_posn_with_lead,
				                              constraint_fp_offset, constraint_stack_min,
				                              curr_stack_size, curr_stack_idist,
				                              fp_strn)) {
					REGISTER
					nt_abs_seq_posn curr_constraint_fp_posn = 0;
					
					if (constraint_fp_offset > 0) {
						if (fp_overlaps) {
							curr_constraint_fp_posn = curr_fp_posn_with_lead + curr_stack_size +
							                          constraint_fp_offset;
						}
						
						else {
							curr_constraint_fp_posn = curr_fp_posn_with_lead + (curr_stack_size * 2) +
							                          curr_stack_idist + constraint_fp_offset;
						}
					}
					
					else {
						if ((nt_int)curr_fp_posn_with_lead + (nt_int)constraint_fp_offset -
						    (nt_int)constraint_stack_min < 0) {
							continue;
						}
						
						curr_constraint_fp_posn = curr_fp_posn_with_lead + constraint_fp_offset -
						                          constraint_stack_min;
					}
					
					if (get_constraint_string_relative_to_fp_posn (seq, seq_len,
					                                        tp_overlaps,
					                                        curr_fp_posn_with_lead,
					                                        curr_constraint_fp_posn,
					                                        constraint_tp_dist,
					                                        constraint_stack_min,
					                                        curr_stack_size,
					                                        curr_stack_idist,
					                                        tp_strn)) {
						if (!has_single ||
						    get_constraint_string_relative_to_fp_posn (seq, seq_len,
						                                            single_overlaps,
						                                            curr_fp_posn_with_lead,
						                                            curr_constraint_fp_posn,
						                                            constraint_single_dist,
						                                            1,
						                                            curr_stack_size,
						                                            curr_stack_idist,
						                                            single_strn)) {
							if (has_single) {
								REGISTER
								char fp_nt, tp_nt, single_nt = single_strn[0];
								
								if (constraint_single_dist > constraint_tp_dist) {
									// "right-handed" base-triple check
									fp_nt = fp_strn[0];
									tp_nt = tp_strn[0];
								}
								
								else {
									// "left-handed" base-triple check;
									// swap fp and tp, and then apply same test as "right-handed" bt
									fp_nt = tp_strn[0];
									tp_nt = fp_strn[0];
								}
								
								// test using "right-handedness" base triple assumption
								//
								// as per email of Rene C.L. Olsthoorn to A.P. Goultiaev on 13 Nov 2019:
								// "Anything can form a triple but U.AU and C.GC are isosteric and different from the rest…"
								//
								if ((fp_nt == 'c' && tp_nt == 'g' && single_nt == 'c') ||
								    (fp_nt == 'u' && tp_nt == 'g' && single_nt == 'c') ||
								    (fp_nt == 'u' && tp_nt == 'a' && single_nt == 'u')) {
									return true;
								}
							}
							
							else {
								REGISTER
								nt_stack_size s = 0;
								
								for (; s < constraint_stack_min; s++) {
									if (!is_valid_bp (fp_strn[s], tp_strn[constraint_stack_min - s - 1])) {
										break;
									}
								}
								
								if (s == constraint_stack_min) {
									return true;
								}
							}
						}
					}
				}
			}
			
	return false;
}

static inline void filter_seq_segment (const uchar thread_id,
                                       char *seq,
                                       const nt_rel_count fp_lead_min_span,
                                       const nt_rel_count fp_lead_max_span,
                                       const nt_stack_size stack_min_size,
                                       const nt_stack_size stack_max_size,
                                       const nt_stack_idist stack_min_idist,
                                       const nt_stack_idist stack_max_idist,
                                       const nt_rel_count tp_trail_min_span,
                                       const nt_rel_count tp_trail_max_span,
                                       const ushort num_constraints,
                                       const int64_t constraints_offset_and_dist[MAX_CONSTRAINT_MATCHES][4][3],
                                       const nt_rt_bytes job_id) {
	const nt_abs_seq_len seq_len = strlen (seq),
	                     // note that in most calculations below, tp_trail_max_span is adopted and assumed as
	                     // a constant, so most comparisons are caried out *exclusive* tp_trail_*_span, such as
	                     // in the following constant which accounts for the largest possible size (length) that
	                     // this model permits (excluded, as stated, tp_trail_*_span)
	                     largest_model_len = fp_lead_max_span + (stack_max_size * 2) + stack_max_idist,
	                     complete_largest_model_len = largest_model_len + tp_trail_max_span;
	REGISTER
	bool have_one_extent = false;
	
	if (seq_len >= (fp_lead_min_span + (stack_min_size * 2) + stack_min_idist +
	                tp_trail_min_span)) {
		REGISTER
		nt_abs_seq_posn curr_fp_posn = 0;
		const unsigned long is_done_size = (seq_len) * (stack_max_size - stack_min_size
		                                        + 1) * (stack_max_idist - stack_min_idist + 1);
		REGISTER
		bool *is_done = malloc (is_done_size * sizeof (bool));
		bool is_fp_posn_matched[seq_len];
		nt_rel_count    curr_matched_fp_lead[seq_len];
		nt_abs_seq_posn curr_matched_fp_tp_extents[seq_len];
		nt_stack_size   curr_matched_stack_size[seq_len];
		nt_stack_idist  curr_matched_stack_idist[seq_len];
		
		if (!is_done) {
			DEBUG_NOW (REPORT_ERRORS, FILTER,
			           "failed to allocate memory to filter segment");
			return;
		}
		
		unsigned long long start_time = get_real_time();
		memset (is_done, false, is_done_size * sizeof (bool));
		memset (is_fp_posn_matched, false, seq_len * sizeof (bool));
		memset (curr_matched_fp_lead, 0, seq_len * sizeof (nt_rel_count));
		// store curr_matched_fp_tp_extents for convenience, though it is re-calculable
		// as curr_matched_fp_posn+curr_matched_fp_lead+(curr_matched_stack_size*2)+curr_matched_stack_idist-1
		memset (curr_matched_fp_tp_extents, 0, seq_len * sizeof (nt_abs_seq_len));
		memset (curr_matched_stack_size, 0, seq_len * sizeof (nt_stack_size));
		memset (curr_matched_stack_idist, 0, seq_len * sizeof (nt_stack_idist));
		REGISTER
		long compared = 0;
		REGISTER
		long skipped = 0;
		REGISTER
		long num_roi_found = 0;
		
		/*
		 * curr_fp_posn marks the (0-indexed) most 5' position for the region of interest currently
		 * under consideration; it ranges between 0 and the most 3' position that might still allow
		 * for the (shortest possible) match against the model
		 * (i.e. seq_len-(fp_lead_min_span+(stack_min_size*2)+stack_min_idist+tp_trail_min_span)+1)
		 */
		for (REGISTER
		     nt_abs_seq_posn curr_fp_posn = 0;
		     curr_fp_posn < seq_len - (fp_lead_min_span + (stack_min_size * 2) +
		                               stack_min_idist + tp_trail_min_span) + 1;
		     curr_fp_posn++) {
			/*
			 * starting from curr_fp_posn, iterate over ranges of fp_lead_span, stack_size, and stack_idist;
			 * such that we prefer the longest (and most 5') possible matching span, that is, relative to
			 * curr_fp_posn:
			 *
			 * a) start from fp_lead_max_span;
			 * b) then from stack_max_size; and
			 * c) then from stack_max_idist.
			 *
			 * note that the largest (longest) possible match is desired so as that we maximize the number of
			 * potential hits with one (worker-based) searh query
			 *
			 * also, tp_trail_max_span is assumed throughout, since this minimizes iterations (and memory alloc)
			 * without incurring much of an 'extra span' penalty
			 */
			
			/*
			 * fp_lead_max_span >= curr_fp_lead >= fp_lead_min_span
			 */
			for (REGISTER nt_s_rel_count curr_fp_lead = fp_lead_max_span;
			     curr_fp_lead >= fp_lead_min_span;
			     curr_fp_lead--) { // need nt_s_rel_count to protect against < 0 case
				/*
				 * stack_max_size >= curr_stack_size >= stack_min_size
				 */
				for (REGISTER nt_s_stack_size curr_stack_size = stack_max_size;
				     curr_stack_size >= stack_min_size; curr_stack_size--) {
					/*
					 * stack_max_idist >= curr_stack_idist >= stack_min_idist
					 */
					for (REGISTER nt_s_stack_idist curr_stack_idist = stack_max_idist;
					     curr_stack_idist >= stack_min_idist; curr_stack_idist--) {
						if (((curr_fp_posn + curr_fp_lead) * (stack_max_size - stack_min_size + 1) *
						     (stack_max_idist - stack_min_idist + 1))
						    + ((curr_stack_size - stack_min_size) * (stack_max_idist - stack_min_idist + 1))
						    + (curr_stack_idist - stack_min_idist) >= is_done_size) {
							continue;
						}
						
						/*
						 * avoid making redundant checks by tracking which absolute position (curr_fp_posn+curr_fp_lead),
						 * relative stack size (curr_stack_size-stack_min_size), and stack idist (curr_stack_idist-stack_min_idist),
						 * have already been visited using is_done
						 *
						 * also if the current "extent" (i.e. relative to the curr_fp_posn and any valid preceeding position, the current
						 * value stored for the longest match) is greater than or equal to
						 * curr_fp_posn+curr_fp_lead+(curr_stack_size*2)+curr_stack_idist-1,
						 * then skip
						 */
						bool to_skip = false;
						nt_abs_seq_posn most_fp_and_valid_position = 0;
						
						// to check against the extent for curr_fp_posn and any preceeding positions, we identify which is the most
						// 5p but still valid (relative to model length) position. the identified position is such that the distance
						// between it and this 3p position (less tp_trail_*_span) is less than or equal to the model's largest length
						//
						// should be noted that this does not preclude future established curr_matched_fp_tp_extents to make redundant
						// previously established curr_matched_fp_tp_extents; any extents made redundant should be checked for and
						// removed below when registering new curr_fp_tp_extents
						if (largest_model_len < curr_fp_posn + curr_fp_lead + (curr_stack_size * 2) +
						    curr_stack_idist) {
							most_fp_and_valid_position = curr_fp_posn + curr_fp_lead +
							                             (curr_stack_size * 2) + curr_stack_idist - largest_model_len;
						}
						
						for (nt_int i = curr_fp_posn; i >= most_fp_and_valid_position; i--) {
							if (is_fp_posn_matched[i] &&
							    curr_matched_fp_tp_extents[i] >= curr_fp_posn + curr_fp_lead +
							    (curr_stack_size * 2) + curr_stack_idist - 1) {
								to_skip = true;
								break;
							}
						}
						
						if (to_skip ||
						    * (is_done + ((curr_fp_posn + curr_fp_lead) * (stack_max_size - stack_min_size +
						                                            1) * (stack_max_idist - stack_min_idist + 1))
						       + ((curr_stack_size - stack_min_size) * (stack_max_idist - stack_min_idist + 1))
						       + (curr_stack_idist - stack_min_idist))) {
							skipped++;
						}
						
						else {
							/*
							 * have not yet visited this position/stack size/idist; visit and mark it as done
							 */
							* (is_done + ((curr_fp_posn + curr_fp_lead) * (stack_max_size - stack_min_size +
							                                        1) * (stack_max_idist - stack_min_idist + 1))
							   + ((curr_stack_size - stack_min_size) * (stack_max_idist - stack_min_idist + 1))
							   + (curr_stack_idist - stack_min_idist)) = true;
							REGISTER
							nt_stack_size s = 0;
							
							for (; s < curr_stack_size; s++) {
								if (curr_fp_posn + curr_fp_lead + (curr_stack_size * 2) + curr_stack_idist - s -
								    1 >= seq_len || 			// exceeds most 3'
								    !is_valid_bp (seq[curr_fp_posn + curr_fp_lead + s],
								                  seq[curr_fp_posn + curr_fp_lead + (curr_stack_size * 2) + curr_stack_idist - s -
								                                   1])) {
									break;
								}
							}
							
							if (s == curr_stack_size) {
								ushort c = 0;
								
								for (; c < num_constraints; c++) {
									/*
									 * for each constraint in the given model, iterate over type of
									 * detail (min/max offsets from the reference stack's fp position
									 * for the constraint's fp element and the dist relative to fp
									 * element for tp/single; for each type of detail an indicator
									 * is available for whether the element 'overlaps' with the
									 * reference stack)
									 *
									 * notes:
									 *
									 * if no single element is present (i.e. not base triple),
									 * then expect to see 0-valued details
									 *
									 * when a constraint fp element 'overlaps' with the reference
									 * stack, then +ve (relative) distances are wrt to the reference
									 * stack's 5' position. if the fp element does not 'overlap'
									 * then (relative) distances are wrt to the reference stack's 3'
									 * position. -ve (relative) distances are always wrt to the
									 * reference stack's 5' element (and in the direction 5'->3')
									 */
									REGISTER
									bool fp_overlaps = false, tp_overlaps = false, single_overlaps = false,
									     has_single = false;
									REGISTER
									nt_stack_size  constraint_stack_min = 0; //, constraint_stack_max=0;
									REGISTER
									nt_s_rel_count constraint_fp_offset_min = 0, constraint_fp_offset_max = 0,
									               constraint_tp_dist_min = 0, constraint_tp_dist_max = 0,
									               // note: init constraint_single_dist_max===constraint_single_dist_min such that
									               //       only one iteration is done over the constraint_single_dist loop below
									               //       whenever single constraint elements are not present
									               constraint_single_dist_min = 0, constraint_single_dist_max = 0;
									/*
									 * for each type of detail, iterate over its min/max values and
									 * whether it overlaps with the fp element's 'next' elements
									 */
									// retrieve offset details (min/max/overlap) of constraint's fp element
									constraint_fp_offset_min = constraints_offset_and_dist[c][0][0];
									constraint_fp_offset_max = constraints_offset_and_dist[c][0][1];
									fp_overlaps = constraints_offset_and_dist[c][0][2];
									// retrieve dist details relative to fp (min/max/overlap) for constraint's tp element
									constraint_tp_dist_min = constraints_offset_and_dist[c][1][0];
									constraint_tp_dist_max = constraints_offset_and_dist[c][1][1];
									tp_overlaps = constraints_offset_and_dist[c][1][2];
									// fp/tp element stack min/max size
									constraint_stack_min = constraints_offset_and_dist[c][2][0];
									//constraint_stack_max=constraints_offset_and_dist[c][2][1];
									
									if (constraints_offset_and_dist[c][3][0] &&
									    constraints_offset_and_dist[c][3][1]) {
										// if constraint's single details are present (i.e. non-zero) get dist details
										// relative to fp (min/max/overlap)
										has_single = true;
										constraint_single_dist_min = constraints_offset_and_dist[c][3][0];
										constraint_single_dist_max = constraints_offset_and_dist[c][3][1];
										single_overlaps = constraints_offset_and_dist[c][3][2];
									}
									
									if (!has_matching_constraint (seq, seq_len,
									                              curr_fp_posn + curr_fp_lead,
									                              curr_stack_size, curr_stack_idist,
									                              constraint_fp_offset_min, constraint_fp_offset_max, fp_overlaps,
									                              constraint_tp_dist_min, constraint_tp_dist_max, tp_overlaps,
									                              constraint_stack_min,
									                              constraint_single_dist_min, constraint_single_dist_max, has_single,
									                              single_overlaps)) {
										break;
									}
								}
								
								if (c == num_constraints) {
									num_roi_found++;
									// register this longer "extent" for curr_fp_posn for comparison in future iterations
									is_fp_posn_matched[curr_fp_posn] = true;
									curr_matched_fp_lead[curr_fp_posn] = curr_fp_lead;
									curr_matched_fp_tp_extents[curr_fp_posn] = curr_fp_posn + curr_fp_lead +
									                                        (curr_stack_size * 2) + curr_stack_idist - 1;
									curr_matched_stack_size[curr_fp_posn] = curr_stack_size;
									curr_matched_stack_idist[curr_fp_posn] = curr_stack_idist;
									
									// test and, if present, remove any fully-overlapping, smaller-length extents; note that in this instance
									// testing is done across the full-length model (i.e. including tp_trail_max_span)
									for (REGISTER nt_abs_seq_posn p = curr_fp_posn + 1;
									     p < SAFE_MIN (seq_len - 1,
									                   curr_matched_fp_tp_extents[curr_fp_posn] + tp_trail_max_span);
									     p++) {
										if (is_fp_posn_matched[p] &&
										    curr_matched_fp_tp_extents[p] <= curr_matched_fp_tp_extents[curr_fp_posn]) {
											is_fp_posn_matched[p] = false;
											curr_matched_fp_tp_extents[p] = 0;
											curr_matched_fp_lead[p] = 0;
											curr_matched_stack_size[p] = 0;
											curr_matched_stack_idist[p] = 0;
										}
									}
								}
							}
							
							compared++;
						}
					}
				}
			}
		}
		
		free (is_done);
		/*
		 * at this stage we have retrieved a number of staggered extents, depicted in the following as
		 * being overlayed on the target sequence segment:
		 *
		 *    ************
		 *       **************
		 *             **************
		 *                        ************
		 *                                       ************
		 *                                              *************
		 * n1..............................................................nM  (1<=seq_len<=M)
		 *
		 * in the next, final step, we minimize the number of sub-segments that need to be submitted to
		 * query by extending each sub-segment (starting from the most 5') to the complete_largest_model_len;
		 * as soon as the next segment is extended to complete_largest_model_len, the subsequent segments
		 * are checked for inclusion (overlap):
		 *
		 *    ************XXXXXXXX    (a)
		 *       **************       (b)
		 *             ************** (c)
		 *                        ************
		 *                                       ************
		 *                                              *************
		 * n1..............................................................nM  (1<=seq_len<=M)
		 *
		 * so in the above depiction (a) is first extended to have a total length of complete_largest_model_len,
		 * (b) is removed from the list of matched extents, and (c) then becomes the current extended segment
		 * against which subsequent checks are made. this is iterated for all remaining extents
		 *
		 * note that once a segment is extended, all that is required to indicate that segment is curr_fp_posn
		 */
		curr_fp_posn = 0;
		REGISTER
		nt_abs_seq_posn curr_complete_tp = 0; // keep track of (complete) tp posn
		
		for (REGISTER ushort i = 0; i < seq_len; i++) {
			if (is_fp_posn_matched[i]) {
				nt_abs_seq_posn curr_matched_complete_tp = curr_matched_fp_tp_extents[i] +
				                                        tp_trail_max_span;
				                                        
				if (curr_matched_complete_tp >= seq_len) {
					curr_matched_complete_tp = seq_len - 1;
				}
				
				if (i >= curr_fp_posn &&
				    //  note: this first condition is redundant, given staggering...
				    curr_matched_complete_tp <= curr_complete_tp) {
					// subsume this match within previous match and continue checking
					is_fp_posn_matched[i] = false;
				}
				
				else {
					// this matched extends beyond current extended match; so switch to the new match and extend...
					// as noted above, at this stage we only need to keep track of is_fp_posn_matched as all matched
					// lengths (and other details) correspond to the longest model conformation
					curr_fp_posn = i;
					
					if (i + complete_largest_model_len < seq_len) {
						curr_complete_tp = i + complete_largest_model_len - 1;
					}
					
					else {
						curr_complete_tp = seq_len - 1;
					}
				}
			}
		}
		
		float elapsed_time = 0.0f;
		#ifdef _WIN32
		elapsed_time = (get_real_time() - start_time) / 1000.0f;
		#else
		elapsed_time = (get_real_time() - start_time) / 10000000.0f;
		#endif
		REGISTER
		nt_abs_seq_posn start_posn, end_posn;
		
		for (REGISTER ushort i = 0; i < seq_len; i++) {
			if (is_fp_posn_matched[i]) {
				have_one_extent = true;
				start_posn = i + 1;
				
				if (i + complete_largest_model_len < seq_len) {
					DEBUG_NOW4 (REPORT_INFO, FILTER,
					            "thread %d found extent %03d-%03d for job '%s'", thread_id, i + 1,
					            i + complete_largest_model_len, job_id);
					end_posn = i + complete_largest_model_len;
				}
				
				else {
					DEBUG_NOW4 (REPORT_INFO, FILTER,
					            "thread %d found extent %03d-%03d for job '%s'", thread_id, i + 1, seq_len,
					            job_id);
					end_posn = seq_len;
				}
				
				uchar s = 0, this_socket = 0;
				FILTER_LOCK_S
				
				do {
					if (!sockets_used[s]) {
						sockets_used[s] = true;
						this_socket = s;
						break;
					}
					
					else {
						s++;
						
						if (s == FILTER_NUM_SOCKET_CONNECTIONS) {
							sleep_ms (FILTER_SOCKET_CONNECT_WAIT);
							s = 0;
						}
					}
				}
				while (1);
				
				FILTER_LOCK_E
				uchar buf[FILTER_MSG_SIZE];
				
				/*
				 * packing data order in (32) bytes : [NUM_RT_BYTES x job_id][4 x start_posn][4 x end_posn]
				 */
				for (uchar j = 0; j < 4; j++) {
					buf[  NUM_RT_BYTES + j] = (uchar) ((start_posn >> ((3 - j) * 8)) & 0xff);
					buf[4 + NUM_RT_BYTES + j] = (uchar) ((end_posn   >> ((3 - j) * 8)) & 0xff);
				}
				
				for (uchar j = 0; j < NUM_RT_BYTES; j++) {
					buf[j] = (uchar) (job_id[j]);
				}
				
				#ifdef _WIN32
				send (win_client_socket[this_socket], buf, FILTER_MSG_SIZE, 0);
				#else
				send (unix_client_socket[this_socket], buf, FILTER_MSG_SIZE, 0);
				#endif
				FILTER_LOCK_S
				sockets_used[this_socket] = false;
				FILTER_LOCK_E
			}
		}
		
		DEBUG_NOW3 (REPORT_INFO, FILTER, "thread %d done with job '%s' in %6.4fs",
		            thread_id, job_id, elapsed_time);
	}
	
	if (!have_one_extent) {
		// in case not a single extent has been identified, send anyhow a 'null' search
		// job request (i.e. a regular job message with start/end posn equal to 0)
		uchar s = 0, this_socket = 0;
		FILTER_LOCK_S
		
		do {
			if (!sockets_used[s]) {
				sockets_used[s] = true;
				this_socket = s;
				break;
			}
			
			else {
				s++;
				
				if (s == FILTER_NUM_SOCKET_CONNECTIONS) {
					sleep_ms (FILTER_SOCKET_CONNECT_WAIT);
					s = 0;
				}
			}
		}
		while (1);
		
		FILTER_LOCK_E
		uchar buf[FILTER_MSG_SIZE];
		
		/*
		 * packing data order in (32) bytes : [NUM_RT_BYTES x job_id][4 x start_posn][4 x end_posn]
		 */
		for (uchar j = 0; j < 4; j++) {
			buf[  NUM_RT_BYTES + j] = 0;
			buf[4 + NUM_RT_BYTES + j] = 0;
		}
		
		for (uchar j = 0; j < NUM_RT_BYTES; j++) {
			buf[j] = (uchar) (job_id[j]);
		}
		
		#ifdef _WIN32
		send (win_client_socket[this_socket], buf, FILTER_MSG_SIZE, 0);
		#else
		send (unix_client_socket[this_socket], buf, FILTER_MSG_SIZE, 0);
		#endif
		FILTER_LOCK_S
		sockets_used[this_socket] = false;
		FILTER_LOCK_E
	}
}

static void *filter_thread (void *arg) {
	bool fp_overlaps = false, tp_overlaps = false, single_overlaps = false;
	nt_s_rel_count constraint_fp_offset_min = 0, constraint_fp_offset_max = 0,
	               constraint_tp_dist_min = 0, constraint_tp_dist_max = 0,
	               constraint_single_dist_min = 0, constraint_single_dist_max = 0;
	const ntp_element target_element = ((seq_seg_arg *)arg)->el_with_largest_stack;
	ntp_constraint this_constraint = ((seq_seg_arg *)arg)->model->first_constraint;
	
	// check validity of initial characters all at once; thereafter check incrementally
	for (REGISTER nt_abs_seq_posn
	     posn = ((seq_seg_arg *)arg)->seq_seg_extra_nt_rel_posn;
	     posn < ((seq_seg_arg *)arg)->seq_seg_span;
	     posn++) {
		if (!is_valid_nt_char (((seq_seg_arg *)arg)->seq_seg[posn])) {
			DEBUG_NOW3 (REPORT_WARNINGS, FILTER,
			            "thread %u found illegal character (%c) at position %u",
			            ((seq_seg_arg *)arg)->thread_id, ((seq_seg_arg *)arg)->seq_seg[posn],
			            ((seq_seg_arg *)arg)->seq_seg_abs_posn + posn + 1);
			return NULL;
		}
	}
	
	DEBUG_NOW5 (REPORT_INFO, FILTER,
	            "thread %d starting on job '%s' (position %d, length %d, extra nt %d)",
	            ((seq_seg_arg *)arg)->thread_id, ((seq_seg_arg *)arg)->job_id,
	            ((seq_seg_arg *)arg)->seq_seg_abs_posn, ((seq_seg_arg *)arg)->seq_seg_span,
	            ((seq_seg_arg *)arg)->seq_seg_extra_nt_rel_posn);
	ushort num_constraints = 0;
	int64_t constraints_offset_and_dist[MAX_CONSTRAINT_MATCHES][4][3];
	
	while (this_constraint) {
		if (get_next_constraint_offset_and_dist_by_element
		    (&constraint_fp_offset_min, &constraint_fp_offset_max, &fp_overlaps,
		     &constraint_tp_dist_min, &constraint_tp_dist_max, &tp_overlaps,
		     &constraint_single_dist_min, &constraint_single_dist_max, &single_overlaps,
		     ((seq_seg_arg *)arg)->model, ((seq_seg_arg *)arg)->model->first_element,
		     target_element, this_constraint)) {
			constraints_offset_and_dist[num_constraints][0][0] = constraint_fp_offset_min;
			constraints_offset_and_dist[num_constraints][0][1] = constraint_fp_offset_max;
			constraints_offset_and_dist[num_constraints][0][2] = fp_overlaps;
			constraints_offset_and_dist[num_constraints][1][0] = constraint_tp_dist_min;
			constraints_offset_and_dist[num_constraints][1][1] = constraint_tp_dist_max;
			constraints_offset_and_dist[num_constraints][1][2] = tp_overlaps;
			
			// note: here we assume base_triple OR pseudoknot types
			if (this_constraint->type == base_triple) {
				constraints_offset_and_dist[num_constraints][2][0] =
				                    this_constraint->base_triple->fp_element->unpaired->min;
				constraints_offset_and_dist[num_constraints][2][1] =
				                    this_constraint->base_triple->fp_element->unpaired->max;
			}
			
			else {
				constraints_offset_and_dist[num_constraints][2][0] =
				                    this_constraint->pseudoknot->fp_element->unpaired->min;
				constraints_offset_and_dist[num_constraints][2][1] =
				                    this_constraint->pseudoknot->fp_element->unpaired->max;
			}
			
			constraints_offset_and_dist[num_constraints][2][2] = 0;
			constraints_offset_and_dist[num_constraints][3][0] = constraint_single_dist_min;
			constraints_offset_and_dist[num_constraints][3][1] = constraint_single_dist_max;
			constraints_offset_and_dist[num_constraints][3][2] = single_overlaps;
			num_constraints++;
		}
		
		else {
			return NULL; // TODO: error handling
		}
		
		this_constraint = this_constraint->next;
	}
	
	REGISTER
	nt_abs_seq_posn curr_seq_start = ((seq_seg_arg *)arg)->seq_seg_abs_posn,
	                curr_seq_end = ((seq_seg_arg *)arg)->seq_seg_span - 1;
	char curr_seq[ ((seq_seg_arg *)arg)->seq_seg_span + 1];
	g_memcpy (curr_seq, & ((seq_seg_arg *)arg)->seq_seg[curr_seq_start],
	          (curr_seq_end - curr_seq_start + 1));
	curr_seq[curr_seq_end - curr_seq_start + 1] = '\0';
	filter_seq_segment (((seq_seg_arg *)arg)->thread_id,
	                    curr_seq,
	                    ((seq_seg_arg *)arg)->fp_lead_min_span,
	                    ((seq_seg_arg *)arg)->fp_lead_max_span,
	                    ((seq_seg_arg *)arg)->stack_min_size,
	                    ((seq_seg_arg *)arg)->stack_max_size,
	                    ((seq_seg_arg *)arg)->stack_min_idist,
	                    ((seq_seg_arg *)arg)->stack_max_idist,
	                    ((seq_seg_arg *)arg)->tp_trail_min_span,
	                    ((seq_seg_arg *)arg)->tp_trail_max_span,
	                    num_constraints,
	                    constraints_offset_and_dist,
	                    ((seq_seg_arg *) arg)->job_id);
	return NULL;
}

bool filter_seq (const char *seq_buff,
                 const nt_abs_count seq_buff_size,
                 const ntp_model model,
                 const ntp_element el_with_largest_stack,
                 const nt_rel_count fp_lead_min_span, const nt_rel_count fp_lead_max_span,
                 const nt_stack_size stack_min_size, const nt_stack_size stack_max_size,
                 const nt_stack_idist stack_min_idist, const nt_stack_idist stack_max_idist,
                 const nt_rel_count tp_trail_min_span, const nt_rel_count tp_trail_max_span,
                 nt_rt_bytes job_id) {
	uchar num_threads = (uchar) SAFE_MAX (SAFE_MIN ((ulong) floor (pow (10,
	                                        ((log10 (seq_buff_size) / M_E) - 1))), MAX_NUM_FILTER_THREADS),
	                                      MIN_NUM_FILTER_THREADS);
	                                      
	if (seq_buff_size < (fp_lead_max_span + (stack_max_size * 2) + stack_max_idist +
	                     tp_trail_max_span) + (num_threads - 1) + num_threads) {
		seq_seg_arg thread_args;
		thread_args.seq_seg = &seq_buff[0];
		thread_args.seq_seg_abs_posn = 0;
		thread_args.fp_lead_min_span = fp_lead_min_span;
		thread_args.fp_lead_max_span = fp_lead_max_span;
		thread_args.stack_min_size = stack_min_size;
		thread_args.stack_max_size = stack_max_size;
		thread_args.stack_min_idist = stack_min_idist;
		thread_args.stack_max_idist = stack_max_idist;
		thread_args.tp_trail_min_span = tp_trail_min_span;
		thread_args.tp_trail_max_span = tp_trail_max_span;
		thread_args.thread_id = 0;
		thread_args.seq_seg_span = (nt_abs_count) seq_buff_size;
		thread_args.seq_seg_extra_nt_rel_posn = 0;
		thread_args.model = model;
		thread_args.el_with_largest_stack = el_with_largest_stack;
		g_memcpy (thread_args.job_id, job_id, NUM_RT_BYTES);
		filter_thread (&thread_args);
		filter_submit_null_job (job_id); // signal end of job
		return true;
	}
	
	/*
	 * note: extra span for each of the num_threads (excluding any trailing nts for the first (rather than, last) thread):
	 *
	 * any 'region of interest' returned by any thread MUST accomodate and encompass the smallest stack size of the
	 * largest stack in the model, in addition to its maximal stack idist, and maximal leading and trailing spans
	*/
	ushort least_span = (ushort) (fp_lead_max_span + (stack_min_size * 2) +
	                              stack_max_idist + tp_trail_max_span);
	ushort extra_span = (ushort) ((seq_buff_size - (num_threads - 1) -
	                               (least_span)) / (float)num_threads);
	ushort thread_span = (ushort) (least_span + extra_span);
	pthread_t thread_handles[num_threads];
	seq_seg_arg thread_args[num_threads];
	nt_abs_seq_posn current_abs_posn = 0;
	#if PRIVILEGED_SCHED
	pthread_attr_t thread_attr;
	int thread_setup_ret_value = pthread_attr_init (&thread_attr);
	
	if (thread_setup_ret_value) {
		DEBUG_NOW1 (REPORT_ERRORS, FILTER,
		            "could not create thread attribute (error code %d)",
		            thread_setup_ret_value);
		return false;
	}
	
	thread_setup_ret_value = pthread_attr_setschedpolicy (&thread_attr, SCHED_FIFO);
	
	if (thread_setup_ret_value) {
		DEBUG_NOW1 (REPORT_ERRORS, FILTER,
		            "could not set scheduler policy (error code %d)",
		            thread_setup_ret_value);
		pthread_attr_destroy (&thread_attr);
		return false;
	}
	
	thread_setup_ret_value = pthread_attr_setinheritsched (&thread_attr,
	                                        PTHREAD_EXPLICIT_SCHED);
	                                        
	if (thread_setup_ret_value) {
		DEBUG_NOW1 (REPORT_ERRORS, FILTER,
		            "could not set inherit scheduler attribute (error code %d)",
		            thread_setup_ret_value);
		pthread_attr_destroy (&thread_attr);
		return false;
	}
	
	struct sched_param sched_param;
	
	thread_setup_ret_value = pthread_attr_getschedparam (&thread_attr,
	                                        &sched_param);
	                                        
	if (thread_setup_ret_value) {
		DEBUG_NOW1 (REPORT_ERRORS, FILTER,
		            "could not retrieve current scheduler parameters (error code %d)",
		            thread_setup_ret_value);
		pthread_attr_destroy (&thread_attr);
		return false;
	}
	
	sched_param.sched_priority = THREAD_SCHED_PRIO;
	thread_setup_ret_value = pthread_attr_setschedparam (&thread_attr,
	                                        &sched_param);
	                                        
	if (thread_setup_ret_value) {
		DEBUG_NOW1 (REPORT_ERRORS, FILTER,
		            "could not set scheduler parameters (error code %d)",
		            thread_setup_ret_value);
		pthread_attr_destroy (&thread_attr);
		return false;
	}
	
	#endif
	
	for (REGISTER uchar i = 0; i < num_threads; i++) {
		thread_args[i].seq_seg = &seq_buff[current_abs_posn];
		thread_args[i].seq_seg_abs_posn = current_abs_posn;
		thread_args[i].fp_lead_min_span = fp_lead_min_span;
		thread_args[i].fp_lead_max_span = fp_lead_max_span;
		thread_args[i].stack_min_size = stack_min_size;
		thread_args[i].stack_max_size = stack_max_size;
		thread_args[i].stack_min_idist = stack_min_idist;
		thread_args[i].stack_max_idist = stack_max_idist;
		thread_args[i].tp_trail_min_span = tp_trail_min_span;
		thread_args[i].tp_trail_max_span = tp_trail_max_span;
		thread_args[i].model = model;
		thread_args[i].el_with_largest_stack = el_with_largest_stack;
		thread_args[i].thread_id = i;
		g_memcpy (thread_args[i].job_id, job_id, NUM_RT_BYTES);
		
		if (!i) {
			// first thread also takes on 'delta' between (Nthreads-1)*thread_span and seq_buff_size
			thread_args[i].seq_seg_span = (nt_abs_seq_len) (seq_buff_size - ((
			                                        num_threads - 1) * (extra_span + 1)));
			thread_args[i].seq_seg_extra_nt_rel_posn = 0;
			current_abs_posn = (nt_abs_seq_len) thread_args[i].seq_seg_span -
			                   (least_span - 1);
		}
		
		else {
			thread_args[i].seq_seg_span = thread_span;
			thread_args[i].seq_seg_extra_nt_rel_posn = (nt_abs_seq_posn) (
			                                        thread_args[i - 1].seq_seg_abs_posn + thread_args[i - 1].seq_seg_span -
			                                        current_abs_posn);
			current_abs_posn += thread_span - (least_span - 1);
		}
		
		#if PRIVILEGED_SCHED
		
		if (pthread_create (&thread_handles[i], &thread_attr, filter_thread,
		                    &thread_args[i]))
		#else
		if (pthread_create (&thread_handles[i], NULL, filter_thread, &thread_args[i]))
		#endif
		{
			DEBUG_NOW1 (REPORT_ERRORS, FILTER,
			            "could not start thread #%u. joining any existing threads", i);
			void *join_ret_value;
			
			for (uchar j = 0; j < i; j++) {
				pthread_join (thread_handles[j], &join_ret_value);
			}
			
			#if PRIVILEGED_SCHED
			pthread_attr_destroy (&thread_attr);
			#endif
			return false;
		}
	}
	
	void *join_ret_value;
	
	for (REGISTER uchar i = 0; i < num_threads; i++) {
		pthread_join (thread_handles[i], &join_ret_value);
	}
	
	filter_submit_null_job (
	                    job_id); // only signal end of job after all threads are finished
	#if PRIVILEGED_SCHED
	pthread_attr_destroy (&thread_attr);
	#endif
	return true;
}

bool filter_seq_from_file (const char *fn,
                           const ntp_model model,
                           const ntp_element el_with_largest_stack,
                           const nt_rel_count fp_lead_min_span, const nt_rel_count fp_lead_max_span,
                           const nt_stack_size stack_min_size, const nt_stack_size stack_max_size,
                           const nt_stack_idist stack_min_idist, const nt_stack_idist stack_max_idist,
                           const nt_rel_count tp_trail_min_span, const nt_rel_count tp_trail_max_span) {
	char *buff = NULL;
	nt_file_size fsize;
	
	if (!read_seq_from_fn (fn, &buff, &fsize)) {
		DEBUG_NOW1 (REPORT_ERRORS, FILTER, "could not read sequence from file '%s'",
		            fn);
		return false;
	}
	
	// TODO: check file size limits
	nt_rt_bytes rt_bytes;
	get_real_time_bytes (&rt_bytes);
	bool success = filter_seq (buff, (nt_abs_count) fsize, model,
	                           el_with_largest_stack,
	                           fp_lead_min_span, fp_lead_max_span, stack_min_size, stack_max_size,
	                           stack_min_idist, stack_max_idist, tp_trail_min_span, tp_trail_max_span,
	                           rt_bytes);
	free (buff);
	return success;
}
