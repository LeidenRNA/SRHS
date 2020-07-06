#include <ulfius.h>
#include <unistd.h>
#include <stdatomic.h>
#include <pthread.h>
#include <openssl/sha.h>
#include "frontend.h"
#include "filter.h"
#include "datastore.h"
#include "jansson.h"
#include "interface.h"
#include "microhttpd.h"
#include "m_build.h"
#include "sequence.h"
#include "util.h"

#define SSL_PRIVKEY_PATH "/etc/letsencrypt/live/rna.liacs.nl/privkey.pem"
#define SSL_CERT_PATH "/etc/letsencrypt/live/rna.liacs.nl/cert.pem"

#ifndef _WIN32
	#include <signal.h>
#endif

static pthread_spinlock_t fe_spinlock, fe_ws_spinlock;

#define FE_LOCK_S    if (pthread_spin_lock (&fe_spinlock)) { DEBUG_NOW (REPORT_ERRORS, FE, "could not acquire frontend spinlock"); } else {
#define FE_LOCK_E    if (pthread_spin_unlock (&fe_spinlock)) { DEBUG_NOW (REPORT_ERRORS, FE, "could not release frontend spinlock"); pthread_exit (NULL); } }

#define FE_WS_LOCK_S    if (pthread_spin_lock (&fe_ws_spinlock)) { DEBUG_NOW (REPORT_ERRORS, FE, "could not acquire ws spinlock"); } else {
#define FE_WS_LOCK_E    if (pthread_spin_unlock (&fe_ws_spinlock)) { DEBUG_NOW (REPORT_ERRORS, FE, "could not release ws spinlock"); pthread_exit (NULL); } }

static atomic_bool frontend_shutting_down = false;

static bool
websocket_client_has_heartbeat[FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS];

static bool websocket_client_active[FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS];

static atomic_uint websocket_num_clients = 0;

// indicates the next client slot, after the slot last used
static atomic_uint next_client_slot_to_check = 0;

static ds_int32_field ref_ids[FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS];

// array of (linked list of websocket indices) for active users
static list_t *ref_id_to_websockets[FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS];

// number of active users (<= active websockets)
static atomic_int num_ref_ids;

static ds_int32_field
websocket_to_ref_id[FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS];

static char
websocket_to_access_token[FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS][FRONTEND_AUTH_MAX_ACCESS_TOKEN_LENGTH
                                        + 1];

// set to true when curation is enabled
static bool
ref_id_to_curator_status[FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS];

static struct _websocket_manager
	*websocket_managers[FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS];

// denote guest IDs by -ve integers;
static int last_guest_ref_id = FRONTEND_GUEST_TEMPLATE_REF_ID;

// last known guest ref id is used to yield the next guest
// id after decrementing by 1; it is assumed that the
// FRONTEND_GUEST_TEMPLATE_REF_ID is either 0 or -ve
static pthread_t ds_deq_thread;

static struct _u_instance frontend_instance;

static json_t *NULL_JSON = NULL;

// MIME types that will define the static files
// (https://github.com/babelouest/ulfius/blob/master/example_programs/sheep_counter/sheep_counter.c)
static struct _u_map mime_types;
// HTTP redirection rules
static struct _u_map redirects;

// enums used for hanlding user topic/capability assignment
enum TOPIC { SEQUENCES = 0, CSSD = 1, JOBS = 2 };
enum CAPABILITY { NEW = 0, DELETE = 1, ORDER = 2, SHOW = 3, SEARCH = 4, EDIT = 5 };

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

void websocket_onclose_callback (const struct _u_request *request,
                                 struct _websocket_manager *websocket_manager,
                                 void *websocket_onclose_user_data) {
	int this_client = * (int *)websocket_onclose_user_data;
	
	if (INVALID_WS_SLOT != this_client &&
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS > this_client) {

		DEBUG_NOW1 (REPORT_INFO, FE_WS, "freeing websocket slot %d on close", this_client);

		// if not capacity exceeded
		REGISTER
		int attempts = FRONTEND_MAX_WEBSOCKET_MESSAGE_ATTEMPTS;
		REGISTER
		struct _websocket_manager *this_manager = NULL;
		FE_WS_LOCK_S
		this_manager = websocket_managers[this_client];
		FE_WS_LOCK_E
		
		while (this_manager && attempts--) {
			// ideally close websocket only after websocket_manager_callback is done,
			// at the end of which websocket_managers[this_client] is set to NULL
			sleep_ms (FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS_TIMEOUT_MS);
			FE_WS_LOCK_S
			this_manager = websocket_managers[this_client];
			FE_WS_LOCK_E
		}
		
		if (FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS > this_client) {
			websocket_num_clients--;
			FE_WS_LOCK_S
			websocket_client_has_heartbeat[this_client] = false;
			websocket_client_active[this_client] = false;
			// clear webscoket_managers anyhow in case timeout of above loop was exceeded
			websocket_managers[this_client] = NULL;
			FE_WS_LOCK_E
		}
		
		free (websocket_onclose_user_data);
	}
	else {
                DEBUG_NOW1 (REPORT_WARNINGS, FE_WS, "cannot free invalid websocket slot %d on close", this_client);
	}
}

/*
 * don't need to 'unbind' access token here; will be overridden eventually
 */
static inline void freeWebsocketRefid (int websocket_slot) {
	REGISTER
	bool freed = false;
	REGISTER
	ds_int32_field this_ref_id = INVALID_REF_ID;

	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[websocket_slot];
	websocket_to_ref_id[websocket_slot] = INVALID_REF_ID;
	websocket_managers[websocket_slot] = NULL;

	for (REGISTER int i = 0; i < FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS; i++) {
		if (ref_ids[i] == this_ref_id) {
			if (ref_id_to_websockets[i]) {
				REGISTER
				int ws_posn = list_locate (ref_id_to_websockets[i], &websocket_slot);
				
				if (0 <= ws_posn) {
					int *x = list_extract_at (ref_id_to_websockets[i], (unsigned int) ws_posn);
					
					if (x) {
						free (x);
						freed = true;
					}
				}
				
				if (!list_size (ref_id_to_websockets[i])) {
					ref_ids[i] = INVALID_REF_ID;
					num_ref_ids--;
				}
			}
		}
	}
	
	if (this_ref_id == last_guest_ref_id &&
	    FRONTEND_GUEST_TEMPLATE_REF_ID > this_ref_id) {
		last_guest_ref_id++;  // prevent every decreasing last known guest ref id
	}
	
	FE_WS_LOCK_E

	if (freed) {
		DEBUG_NOW2 (REPORT_INFO, FE_WS,
		            "freeing user (reference id %d) from websocket %d",
		            this_ref_id, websocket_slot);
	}
	
	else {
		if (INVALID_REF_ID != this_ref_id) {
			DEBUG_NOW2 (REPORT_ERRORS, FE_WS,
				    "could not free user (reference id %d) from websocket %d", this_ref_id,
				    websocket_slot);
		} else {
			DEBUG_NOW1 (REPORT_ERRORS, FE_WS,
				    "no user to free before closing websocket %d", websocket_slot);
		}
	}
	
	if (FRONTEND_GUEST_TEMPLATE_REF_ID > this_ref_id) {
		bool purged = true;
		purged = delete_all_sequences (this_ref_id);
		purged = delete_all_jobs (this_ref_id) && purged;
		purged = delete_all_results (this_ref_id) && purged;
		
		if (!purged) {
			DEBUG_NOW2 (REPORT_ERRORS, FE_WS,
			            "could not purge data for guest user (reference id %d) before closing websocket %d", this_ref_id, websocket_slot);
		}
	}
}

int l_int_comparator (const void *a, const void *b) {
	if (* (int *)a > * (int *)b) {
		return -1;
	}
	
	else
		if (* (int *)a == * (int *)b) {
			return 0;
		}
		
		else {
			return 1;
		}
}

static inline bool isValidWebsocketslotAccesstokenPair (
                    const char *access_token, const ds_int32_field websocket_slot) {
	if (0 > websocket_slot ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS <= websocket_slot) {
		DEBUG_NOW1 (REPORT_ERRORS, FE_WS, "invalid websocket slot %d", websocket_slot);
		return false;
	}
	
	if (!access_token || !strlen (access_token)) {
		DEBUG_NOW (REPORT_ERRORS, FE_WS, "invalid token");
		return false;
	}
	
	int cmp_res = -1;
	FE_WS_LOCK_S
	cmp_res = strncmp (websocket_to_access_token[websocket_slot], access_token,
	                   FRONTEND_AUTH_MAX_ACCESS_TOKEN_LENGTH);
	FE_WS_LOCK_E
	
	if (0 != cmp_res) {
		DEBUG_NOW2 (REPORT_WARNINGS, FE_WS,
		            "token '%s' and websocket slot %d are not paired", access_token,
		            websocket_slot);
		return false;
	}
	
	return true;
}

static inline bool bindRefidWebsocketAccesstoken (ds_int32_field ref_id,
                                        int websocket_slot, char *accessToken) {
	// change notification: change (1) + collection (1) + change op type (1) + 0 (1)
	// (no requirement to send DS_OBJ_ID_LENGTH here)
	char hb_msg[1 + 1 + 1 + 1];
	REGISTER
	struct _websocket_manager *this_manager = NULL;
	REGISTER
	bool foundFirstFree = false;
	REGISTER
	int i = 0, firstFree = -1;
	FE_WS_LOCK_S
	
	for (; i < FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS; i++) {
		if (ref_ids[i] == ref_id) {
			break;
		}
		
		else
			if (!foundFirstFree && ref_ids[i] == INVALID_REF_ID) {
				firstFree = i;
				foundFirstFree = true;
			}
	}
	
	if (FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS == i) {
		// TODO: memory checks, capacities, uniqueness, etc.
		i = firstFree;
		ref_ids[i] = ref_id;
		num_ref_ids++;
	}
	
	if (NULL == ref_id_to_websockets[i]) {
		ref_id_to_websockets[i] = malloc (sizeof (list_t));
		list_init (ref_id_to_websockets[i]);
		list_attributes_comparator (ref_id_to_websockets[i], &l_int_comparator);
	}
	
	REGISTER
	int *this_ws_slot = malloc (sizeof (int));
	*this_ws_slot = websocket_slot;
	list_append (ref_id_to_websockets[i], this_ws_slot);
	websocket_to_ref_id[websocket_slot] = ref_id;
	strncpy (websocket_to_access_token[websocket_slot], accessToken,
	         FRONTEND_AUTH_MAX_ACCESS_TOKEN_LENGTH);
	hb_msg[0] = FRONTEND_WEBSOCKET_CHANGE_NOTIFICATION;
	hb_msg[1] = DS_COL_SEQUENCES | DS_COL_CSSD | DS_COL_JOBS | DS_COL_RESULTS;
	hb_msg[2] = DS_NOTIFY_OP_UPDATE;
	// notify *all* sequences|CSSDs|jobs|results; don't send any object id here
	hb_msg[3] = 0;
	this_manager = websocket_managers[websocket_slot];
	ref_id_to_curator_status[i] = false;
	FE_WS_LOCK_E
	
	if (this_manager) {
		ulfius_websocket_send_message (this_manager, U_WEBSOCKET_OPCODE_TEXT, 4,
		                               (char *) hb_msg);
		DEBUG_NOW3 (REPORT_INFO, FE_WS,
		            "binding user with token '%s' (reference id %d) to websocket slot %d",
		            accessToken,
		            ref_id, websocket_slot);
		return true;
	}
	
	else {
		DEBUG_NOW3 (REPORT_ERRORS, FE_WS,
		            "could not bind user with token '%s' (reference id %d) to websocket slot %d",
		            accessToken, ref_id, websocket_slot);
		return false;
	}
}

static void *ds_deq_thread_start (void *arg) {
	REGISTER
	ds_notification_event *notif_event;
	REGISTER
	ds_int32_field notif_ref_id;
	REGISTER
	DS_COLLECTION notif_collection;
	REGISTER
	bool msg_ok;
	// change notification: change (1) + collection (1) + change op type (1) + object id or 0 (DS_OBJ_ID_LENGTH)
	static char u_msg[1 + 1 + 1 + DS_OBJ_ID_LENGTH];
	u_msg[0] = FRONTEND_WEBSOCKET_CHANGE_NOTIFICATION;
	
	while (1) {
		notif_event = deq_ds();
		
		if (NULL != notif_event) {
			msg_ok = false;
			notif_ref_id = notif_event->ref_id;
			notif_collection = notif_event->collection;
			u_msg[1] = notif_collection;
			u_msg[2] = notif_event->op_type;
			strncpy (u_msg + 3, notif_event->oid_string, DS_OBJ_ID_LENGTH);
			REGISTER
			list_t *wss = NULL;
			REGISTER int i = 0;
			char msg[100];
			FE_WS_LOCK_S
			
			for (; i < FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS; i++) {
				if (ref_ids[i] == notif_ref_id) {
					wss = ref_id_to_websockets[i];
					break;
				}
			}
			
			if (FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS > i) {
				if (NULL != wss && list_size (wss)) {
					if (list_iterator_start (wss)) {
						msg_ok = true;
						
						while (list_iterator_hasnext (wss)) {
							REGISTER
							int this_websocket = * (int *) list_iterator_next (wss);
							REGISTER
							struct _websocket_manager *this_manager = NULL;
							this_manager = websocket_managers[this_websocket];
							
							if (this_manager) {
								ulfius_websocket_send_message (this_manager, U_WEBSOCKET_OPCODE_TEXT,
								                               1 + 1 + 1 + DS_OBJ_ID_LENGTH, (char *) u_msg);
							}
						}
						
						list_iterator_stop (wss);
					}
					
					else {
						sprintf (msg, "could not start iterator for reference id %d", notif_ref_id);
					}
				}
			}
			
			else {
				/*
				 * no need to alert user - ref_id not found is common when purging data for expired guest users
				 */
				msg_ok = true;
			}
			
			FE_WS_LOCK_E
			free (notif_event);
			
			if (!msg_ok) {
				DEBUG_NOW (REPORT_ERRORS, FE_WS, msg);
			}
		}
		
		else
			if (frontend_shutting_down) {
				break;
			}
			
			else {
				sleep_ms (FRONTEND_DS_NOTIFICATION_SLEEP_MS);
			}
	}
	
	pthread_exit (NULL);
}

static inline void notify_all_ref_ids (const char *oid_string,
                                       const DS_COLLECTION collection, const DS_NOTIFY_OP_TYPE op_type) {
	// change notification: change (1) + collection (1) + change op type (1) + object id or 0 (DS_OBJ_ID_LENGTH)
	static char u_msg[1 + 1 + 1 + DS_OBJ_ID_LENGTH];
	u_msg[0] = FRONTEND_WEBSOCKET_CHANGE_NOTIFICATION;
	u_msg[1] = collection;
	u_msg[2] = op_type;
	strncpy (u_msg + 3, oid_string, DS_OBJ_ID_LENGTH);
	REGISTER
	list_t *wss = NULL;
	REGISTER
	int i = 0;
	char msg[100];
	REGISTER
	bool msg_ok;       
	
	for (; i < FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS; i++) {
		msg_ok = true;  
		FE_WS_LOCK_S
		
		if (INVALID_REF_ID != ref_ids[i]) {
			wss = ref_id_to_websockets[i];
			msg_ok = false; 
			
			if (NULL != wss && list_size (wss)) {
				if (list_iterator_start (wss)) {
					while (list_iterator_hasnext (wss)) {
						REGISTER
						int this_websocket = * (int *) list_iterator_next (wss);
						REGISTER
						struct _websocket_manager *this_manager = NULL;
						this_manager = websocket_managers[this_websocket];
						
						if (this_manager) {
							ulfius_websocket_send_message (this_manager, U_WEBSOCKET_OPCODE_TEXT,
							                               1 + 1 + 1 + DS_OBJ_ID_LENGTH, (char *) u_msg);
						}
					}
					
					list_iterator_stop (wss);
					msg_ok = true;
				}
				
				else {
					sprintf (msg, "could not start iterator for reference id %d", ref_ids[i]);
				}
			}
			
			else {
				sprintf (msg, "no websocket active for reference id %d", ref_ids[i]);
			}
		}
		
		FE_WS_LOCK_E
		
		if (!msg_ok) { 
			DEBUG_NOW (REPORT_ERRORS, FE_WS, msg);
		}
	}
}

static inline void notify_ref_id (const int ref_id, const char *oid_string,
                                  const DS_COLLECTION collection, const DS_NOTIFY_OP_TYPE op_type) {
	// change notification: change (1) + collection (1) + change op type (1) + object id or 0 (DS_OBJ_ID_LENGTH)
	static char u_msg[1 + 1 + 1 + DS_OBJ_ID_LENGTH];
	u_msg[0] = FRONTEND_WEBSOCKET_CHANGE_NOTIFICATION;
	u_msg[1] = collection;
	u_msg[2] = op_type;
	strncpy (u_msg + 3, oid_string, DS_OBJ_ID_LENGTH);
	REGISTER
	list_t *wss = NULL;
	REGISTER
	int i = 0;
	char msg[100];
	REGISTER
	bool found_ref_id = false;
	REGISTER
	bool msg_ok;  
	
	for (; i < FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS; i++) {
		msg_ok = true; 
		FE_WS_LOCK_S
		
		if (ref_id == ref_ids[i]) {
			wss = ref_id_to_websockets[i];
			msg_ok = false;   
			
			if (NULL != wss && list_size (wss)) {
				if (list_iterator_start (wss)) {
					while (list_iterator_hasnext (wss)) {
						REGISTER
						int this_websocket = * (int *) list_iterator_next (wss);
						REGISTER
						struct _websocket_manager *this_manager = NULL;
						this_manager = websocket_managers[this_websocket];
						
						if (this_manager) {
							ulfius_websocket_send_message (this_manager, U_WEBSOCKET_OPCODE_TEXT,
							                               1 + 1 + 1 + DS_OBJ_ID_LENGTH, (char *) u_msg);
						}
					}
					
					list_iterator_stop (wss);
					msg_ok = true; 
				}
				
				else {
					sprintf (msg, "could not start iterator for reference id %d", ref_ids[i]);
				}
			}
			
			else {
				sprintf (msg, "no websocket active for reference id %d", ref_ids[i]);
			}
			
			found_ref_id = true;
		}
		
		FE_WS_LOCK_E
		
		if (!msg_ok) { 
			DEBUG_NOW (REPORT_ERRORS, FE_WS, msg);
		}
		
		if (found_ref_id) {
			break;
		}
	}
}

void websocket_incoming_message_callback (const struct _u_request *request,
                                        struct _websocket_manager *websocket_manager,
                                        const struct _websocket_message *last_message,
                                        void *websocket_incoming_message_user_data) {
	int this_client = * (int *)websocket_incoming_message_user_data;
	
	if (INVALID_WS_SLOT != this_client &&
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS >
	    this_client) {
		// if not capacity exceeded
		// received pong
		if (U_WEBSOCKET_OPCODE_TEXT == last_message->opcode) {
			if (1 == last_message->data_len) {
				// control message: HEARTBEAT/CLOSE REQUEST
				if (FRONTEND_WEBSOCKET_CLOSE_REQUEST == last_message->data[0]) {
					 DEBUG_NOW1 (REPORT_INFO, FE_WS,
                                                    "received close request from user on websocket slot %d. sending back ack", this_client);
					// ack close request message
					static char hb_msg[1];
					hb_msg[0] = FRONTEND_WEBSOCKET_CLOSE_REQUEST;
					ulfius_websocket_send_message (websocket_manager, U_WEBSOCKET_OPCODE_TEXT, 1,
					                               (char *) hb_msg);
					// do not register heartbeat; satisfy close request
					FE_WS_LOCK_S
					websocket_client_active[this_client] = false;
					FE_WS_LOCK_E
				}
				
				else {
					DEBUG_NOW1 (REPORT_INFO, FE_WS,
                                                    "received heartbeat from user on websocket slot %d. sending back ack", this_client);
					// heartbeat
					FE_WS_LOCK_S
					websocket_client_has_heartbeat[this_client] = true;
					FE_WS_LOCK_E
				}
			}
			
			else
				if (2 < last_message->data_len &&
				    FRONTEND_WEBSOCKET_ACCESS_TOKEN_REFRESH == last_message->data[0] &&
				    FRONTEND_WEBSOCKET_ACCESS_TOKEN_REFRESH_DELIMITER == last_message->data[1]) {
					char new_access_token[FRONTEND_AUTH_MAX_ACCESS_TOKEN_LENGTH + 1];
					g_memcpy (new_access_token, (last_message->data) + 2,
					          last_message->data_len - 2);
					new_access_token[last_message->data_len - 2] = '\0';
					DEBUG_NOW3 (REPORT_INFO, FE_WS,
					            "user on websocket slot %d requested token switch from '%s' to '%s'", this_client,
					            websocket_to_access_token[this_client], new_access_token);
					FE_WS_LOCK_S
					g_memcpy (websocket_to_access_token[this_client], new_access_token,
					          last_message->data_len - 1);
					FE_WS_LOCK_E
				}
				
				else {
					// new connection accessToken
					REGISTER
					bool isGuest = true;
					
					for (REGISTER unsigned short i = 0; i < last_message->data_len; i++) {
						if (FRONTEND_AUTH_GUEST_ACCESS_TOKEN_CHAR != last_message->data[i]) {
							isGuest = false;
							break;
						}
					}
					
					if (isGuest) {
						DEBUG_NOW1 (REPORT_INFO, FE_WS, "message received from guest user on websocket slot %d",
                                                            this_client);

						if (bindRefidWebsocketAccesstoken (--last_guest_ref_id, this_client,
						                                   FRONTEND_AUTH_GUEST_ACCESS_TOKEN_STRING)) {
							DEBUG_NOW2 (REPORT_INFO, FE_WS, "guest user (reference id %d) logged in on websocket slot %d",
							            last_guest_ref_id, this_client);
							bool purged = true;
							purged = delete_all_sequences (last_guest_ref_id);
							purged = delete_all_jobs (last_guest_ref_id) && purged;
							purged = delete_all_results (last_guest_ref_id) && purged;
							
							if (!purged) {
								DEBUG_NOW1 (REPORT_WARNINGS, FE_WS,
								            "could not purge data for guest user (reference id %d)", last_guest_ref_id);
							}
						}
						
						else {
							last_guest_ref_id++;
							DEBUG_NOW2 (REPORT_ERRORS, FE_WS,
							            "could not log in guest user (reference id %d) on websocket slot %d",
							            last_guest_ref_id, this_client);
						}
					}
					
					else {
						char bearerToken[9 + last_message->data_len];
						char accessToken[1 + last_message->data_len];
						struct _u_map req_headers;
						struct _u_request req;
						struct _u_response response;
						ds_int32_field registered_user_ref_id = INVALID_REF_ID;
						int res;
						ulfius_init_request (&req);
						u_map_init (&req_headers);
						ulfius_init_response (&response);
						g_memcpy (bearerToken, "Bearer ", 7);
						g_memcpy (bearerToken + 7, last_message->data, last_message->data_len);
						bearerToken[8 + last_message->data_len] = '\0';
						g_memcpy (accessToken, last_message->data, last_message->data_len);
						accessToken[last_message->data_len] = '\0';
						DEBUG_NOW4 (REPORT_INFO, FE_WS,
						            "message '%.*s' received from user with token '%s' on websocket slot %d",
						            (int)last_message->data_len, last_message->data, accessToken, this_client);
						req.http_url = o_strdup (FRONTEND_AUTH_SERVER_USER_INFO_URL);
						req.http_verb = o_strdup (FRONTEND_AUTH_SERVER_USER_INFO_VERB);
						req.timeout = FRONTEND_AUTH_SERVER_USER_INFO_TIMEOUT_S;
						u_map_put (&req_headers, "Authorization", bearerToken);
						u_map_copy_into (req.map_header, &req_headers);
						res = ulfius_send_http_request (&req, &response);
						
						if (U_OK == res) {
							bool success = false, is_curator = false;
							
							if (response.protocol &&
							    (!strcmp (response.protocol, FRONTEND_AUTH_SERVER_RESPONSE_STATUS_LINE_OK) ||
							     !strcmp (response.protocol,
							              FRONTEND_AUTH_SERVER_RESPONSE_STATUS_LINE_TOO_MANY_REQS))) {
								if (!strcmp (response.protocol,
								             FRONTEND_AUTH_SERVER_RESPONSE_STATUS_LINE_TOO_MANY_REQS)) {
									DEBUG_NOW (REPORT_ERRORS, FE_WS, "too many requests sent to auth server");
								}
								
								else {
									json_t *response_json = json_loadb (response.binary_body,
									                                    response.binary_body_length, 0, NULL);
									                                    
									if (NULL == response_json || !json_is_object (response_json)) {
										DEBUG_NOW (REPORT_ERRORS, FE_WS, "could not parse response body json from auth server");
									}
									
									else {
										json_t *name_json = json_object_get (
										                                        response_json,
										                                        FRONTEND_AUTH_SERVER_RESPONSE_NAME_KEY);
										json_t *sub_json = json_object_get (
										                                       response_json,
										                                       FRONTEND_AUTH_SERVER_RESPONSE_SUB_KEY);
										json_t *app_metadata_json = json_object_get (
										                                        response_json,
										                                        FRONTEND_AUTH_SERVER_USER_INFO_APP_METADATA_KEY);
										                                        
										// check for curator status
										if (NULL != app_metadata_json && json_is_object (app_metadata_json)) {
											json_t *role_json = json_object_get (app_metadata_json,
											                                     FRONTEND_AUTH_SERVER_RESPONSE_ROLE_KEY);
											                                     
											if (NULL != role_json) {
												const char *role_value = json_string_value (role_json);
												
												if (!strcmp (role_value, FRONTEND_AUTH_SERVER_RESPONSE_CURATOR_VALUE)) {
													is_curator = true;
												}
											}
										}
										
										if (NULL != name_json && NULL != sub_json) {
											const char *name_value = json_string_value (name_json),
											            *sub_value = json_string_value (sub_json);
											            
											if (NULL != name_value && NULL != sub_value) {
												SHA256_CTX context;
												uchar sub_md[SHA256_DIGEST_LENGTH];
												g_memset (sub_md, 0, SHA256_DIGEST_LENGTH);
												
												if (1 != SHA256_Init (&context) ||
												    1 != SHA256_Update (&context, sub_value, strlen (sub_value)) ||
												    1 != SHA256_Final (sub_md, &context)) {
													DEBUG_NOW1 (REPORT_ERRORS, FE_WS,
													            "could not generate message digest for user '%s'", name_value);
												}
												
												else {
													// ascii equivalent of SHA256 digest string, assumes ds_generic_field is large enough
													ds_generic_field
													asc_md;
													int p = 0;
													
													for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
														char t[4];
														sprintf (t, "%d", sub_md[i]);
														
														for (int j = 0; j < strlen (t); j++) {
															asc_md[p] = t[j];
															p++;
														}
													}
													
													asc_md[p] = '\0';
													dsp_dataset user_dataset;
													
													if (!read_user ((char *)asc_md, &user_dataset)) {
														DEBUG_NOW1 (REPORT_ERRORS, FE_WS,
														            "could not get user '%s' details from datastore", name_value);
													}
													
													else {
														if (!user_dataset->num_records) {
															// user not found
															ds_object_id_field new_user_object_id;
															ds_int32_field new_user_ref_id = INVALID_REF_ID;
															
															if (!create_user ((char *)name_value, (char *)asc_md, &new_user_ref_id,
															                  &new_user_object_id)) {
																DEBUG_NOW2 (REPORT_ERRORS, FE_WS,
																            "could not create new user '%s' (reference id %d)",
																            name_value, new_user_ref_id);
															}
															
															else {
																DEBUG_NOW2 (REPORT_INFO, FE_WS,
																            "creating new user '%s' (reference id %d)",
																            name_value, new_user_ref_id);
																            
																if (bindRefidWebsocketAccesstoken (new_user_ref_id, this_client, accessToken)) {
																	DEBUG_NOW3 (REPORT_INFO, FE_WS,
																	            "new user '%s' (reference id %d) successfully logged in on websocket slot %d",
																	            name_value, new_user_ref_id, this_client);
																	success = true;
																	registered_user_ref_id = new_user_ref_id;
																}
																
																else {
																	DEBUG_NOW3 (REPORT_ERRORS, FE_WS,
																	            "could not log new user '%s' (reference id %d) in on websocket slot %d",
																	            name_value, new_user_ref_id, this_client);
																	success = false;
																}
															}
														}
														
														else {
															if (bindRefidWebsocketAccesstoken (atoi (user_dataset->data[0]), this_client,
															                                   accessToken)) {
																DEBUG_NOW3 (REPORT_INFO, FE_WS,
																            "user '%s' (reference id %d) successfully logged in on websocket slot %d",
																            name_value, atoi (user_dataset->data[0]), this_client);
																success = true;
																registered_user_ref_id = atoi (user_dataset->data[0]);
															}
															
															else {
																DEBUG_NOW3 (REPORT_ERRORS, FE_WS,
																            "could not log user '%s' (reference id %d) in on webscoket slot %d",
																            name_value, atoi (user_dataset->data[0]), this_client);
																success = false;
															}
														}
													}
													
													free_dataset (user_dataset);
												}
											}
											
											else {
												DEBUG_NOW (REPORT_ERRORS, FE_WS,
												           "could not get user name or sub value from response json of auth server");
											}
										}
										
										else {
											DEBUG_NOW (REPORT_ERRORS, FE_WS,
											           "could not get user name or sub value from response json of auth server");
										}
									}
									
									if (response_json) {
										json_decref (response_json);
									}
								}
							}
							
							else
								if (response.protocol &&
								    !strcmp (response.protocol,
								             FRONTEND_AUTH_SERVER_RESPONSE_STATUS_LINE_UNAUTHORIZED)) {
									DEBUG_NOW1 (REPORT_WARNINGS, FE_WS, "unauthorized access attempt on websocket slot %d. sending close request", this_client);

									// unauthorized - send close request message
									static char hb_msg[1];
									hb_msg[0] = FRONTEND_WEBSOCKET_CLOSE_REQUEST;
									ulfius_websocket_send_message (websocket_manager, U_WEBSOCKET_OPCODE_TEXT, 1,
												       (char *) hb_msg);
									// and mark as inactive
									FE_WS_LOCK_S
									websocket_client_active[this_client] = false;
									FE_WS_LOCK_E
								}
								
								else {
									DEBUG_NOW1 (REPORT_ERRORS, FE_WS, "unrecognized http response protocol '%s' from auth server",
									            response.protocol);
								}
								
							if (!success) {
								// drop connection
								FE_WS_LOCK_S
								websocket_client_active[this_client] = false;
								FE_WS_LOCK_E
							}
							
							else {
								// update curator status for registered users having the right role set in auth0
								if (is_curator && INVALID_REF_ID != registered_user_ref_id) {
									DEBUG_NOW2 (REPORT_INFO, FE_WS,
									            "user (reference id %d) granted curator status on websocket slot %d",
									            registered_user_ref_id, this_client);
									FE_WS_LOCK_S
									ref_id_to_curator_status[this_client] = true;
									FE_WS_LOCK_E
								}
							}
						}
						
						else {
							DEBUG_NOW2 (REPORT_ERRORS, FE_WS,
							            "authorization (userinfo) request for reference id %d returned error code %d from auth server",
							            registered_user_ref_id, res);
						}
						
						ulfius_clean_response (&response);
						u_map_clean (&req_headers);
						ulfius_clean_request (&req);
					}
				}
		}
	}
	else {
		DEBUG_NOW1 (REPORT_ERRORS, FE_WS,
			    "message received on invalid websocket slot %d", this_client);
	}
}

void websocket_manager_callback (const struct _u_request *request,
                                 struct _websocket_manager *websocket_manager,
                                 void *websocket_manager_user_data) {
	/*
	 * as per ulfius API doc, websocket_manager_callbacks are run in separate threads (per ws
	 * connection) so we only need to synchronize our own shared data structures
	 */
	char hb_msg[3];
	REGISTER
	int this_client = * (int *)websocket_manager_user_data;

	DEBUG_NOW1 (REPORT_INFO, FE_WS, "new websocket request on slot %d", this_client);

	if (INVALID_WS_SLOT != this_client &&
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS >
	    this_client) {
		// if not capacity exceeded
		hb_msg[0] = FRONTEND_WEBSOCKET_HEARTBEAT;
		hb_msg[1] = (char) ((this_client >> 8) & 0xFF);
		hb_msg[2] = (char) (this_client & 0xFF);
		REGISTER
		unsigned int num_recv_tries = 0, num_send_tries = 0;
		REGISTER
		bool cont = true;
		FE_WS_LOCK_S
		websocket_managers[this_client] = websocket_manager;
		websocket_client_has_heartbeat[this_client] = true;
		cont = !frontend_shutting_down && websocket_client_active[this_client];
		FE_WS_LOCK_E

		// send first heartbeat response even before getting first one from client;
		// so that client updates websocket slot	
                DEBUG_NOW1 (REPORT_INFO, FE_WS, "sending first heartbeat message for websocket slot %d", this_client);

		if (ulfius_websocket_send_message (websocket_manager, U_WEBSOCKET_OPCODE_TEXT,
						   3, (char *) hb_msg) != U_OK) {
			num_send_tries++;
		}
	
		while (cont) {
			sleep_ms (FRONTEND_WEBSOCKET_HEARTBEAT_TIMEOUT_MS);
			FE_WS_LOCK_S
			
			if (!websocket_client_has_heartbeat[this_client]) {
		                DEBUG_NOW1 (REPORT_INFO, FE_WS, "missed heartbeat message from user on websocket slot %d", this_client);

				num_recv_tries++;
				
				if (num_recv_tries >= FRONTEND_MAX_WEBSOCKET_MESSAGE_ATTEMPTS) {
					DEBUG_NOW2 (REPORT_INFO, FE_WS, "no heartbeat messages received from user on websocket slot %d after %d tries", 
						    this_client, num_recv_tries);

					// did not get pong after FRONTEND_MAX_WEBSOCKET_MESSAGE_ATTEMPTS
					cont = false;
				}
			}
			
			else {
				// got pong, so reset status before sending new ping
				websocket_client_has_heartbeat[this_client] = false;
				num_recv_tries = 0;
			}
			
			cont = cont && (!frontend_shutting_down &&
			                websocket_client_active[this_client]);
			FE_WS_LOCK_E
			
			if (cont) {
				if (ulfius_websocket_send_message (websocket_manager, U_WEBSOCKET_OPCODE_TEXT,
				                                   3, (char *) hb_msg) != U_OK) {
					num_send_tries++;
				
					DEBUG_NOW1 (REPORT_INFO, FE_WS, "failed to send heartbeat ack to user on websocket slot %d",
                                                            this_client);

					if (FRONTEND_MAX_WEBSOCKET_MESSAGE_ATTEMPTS <= num_send_tries) {
				                DEBUG_NOW2 (REPORT_INFO, FE_WS, "could not send heartbeat ack to user on websocket slot %d after %d tries", 
							    this_client, num_send_tries);

						cont = false;
					}
				}
			}
		}
		
		freeWebsocketRefid (this_client);
		// allow onclose callback to complete
		FE_WS_LOCK_S
		websocket_managers[this_client] = NULL;
		FE_WS_LOCK_E
	}
	
	else {
                DEBUG_NOW1 (REPORT_WARNINGS, FE_WS, "invalid websocket slot %d or at max capacity", this_client);

		// inform client we're at max capacity
		hb_msg[0] = FRONTEND_WEBSOCKET_LIMIT_REACHED;
		ulfius_websocket_send_message (websocket_manager, U_WEBSOCKET_OPCODE_TEXT, 1,
		                               (char *) hb_msg);
	}
}

int callback_websocket (const struct _u_request *request,
                        struct _u_response *response, void *user_data) {
        DEBUG_NOW (REPORT_INFO, FE_WS, "new request for websocket comms received");

	/*
	 * pause if necessary, to get access to a websocket 'slot'
	 */
	int num_tries = FRONTEND_MAX_WEBSOCKET_MESSAGE_ATTEMPTS;
	
	while (FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < websocket_num_clients &&
	       num_tries--) {
		sleep_ms (FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS_TIMEOUT_MS);
	}
	
	int cb_ret_val = U_CALLBACK_ERROR;
	FE_WS_LOCK_S
	/*
	 * manage (using callbacks) up to FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS connections
	 */
	REGISTER
	int *this_client = malloc (sizeof (int)), ret_val = U_ERROR;
	*this_client = INVALID_WS_SLOT;
	
	if (FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS > websocket_num_clients) {
		/*
		 * but only allow up to FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS active connections;
		 * manage capacity limits inside manager callback function; here simply assign next client...
		*/
		websocket_num_clients++;
		REGISTER
		bool found_client = false;
		
		do {
			for (REGISTER unsigned int i = next_client_slot_to_check;
			     i < FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS; i++) {
				if (!websocket_client_active[i]) {
					*this_client = i;
					next_client_slot_to_check = i + 1;
					websocket_client_active[i] = true;
					found_client = true;
				}
				
				if (found_client) {
					DEBUG_NOW1 (REPORT_INFO, FE_WS, "free slot found. assigning websocket slot %d", *this_client);

					break;
				}
			}
			
			if (INVALID_WS_SLOT == *this_client) {
				next_client_slot_to_check = 0;  // wrap around if required
			}
			
			else {
				break;
			}
			
			// while should be iterated over either once or twice,
			// depending on whether next_client_slot_to_check
			// wraps around FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS
		}
		while (1);

		if (!found_client) {
		        DEBUG_NOW (REPORT_WARNINGS, FE_WS, "no free slots available. dropping request");
		}
	}
	
	ret_val = ulfius_set_websocket_response (
	                              response, NULL, NULL,
	                              &websocket_manager_callback, this_client,
	                              &websocket_incoming_message_callback, this_client,
	                              &websocket_onclose_callback, this_client
	          );
	          
	if (U_OK == ret_val) {
		cb_ret_val = U_CALLBACK_CONTINUE;
	}
	
	FE_WS_LOCK_E
	return cb_ret_val;
}

/*
 * ulfius_add_endpoint_by_val(&frontend_instance, "GET", "*", NULL, 1, &callback_static_file, &mime_types);
 * (adapted from https://github.com/babelouest/ulfius/blob/master/example_programs/sheep_counter/sheep_counter.c)
 */
static inline void get_filename_encoding_extension (const char *path,
                                        char extension[MAX_FILENAME_LENGTH + 1],
                                        char content_encoding[MAX_FILENAME_LENGTH + 1]) {
	g_memcpy (extension, "*", 1);
	extension[1] = '\0';
	g_memcpy (content_encoding, FRONTEND_CONTENT_ENCODING_IDENTITY,
	          strlen (FRONTEND_CONTENT_ENCODING_IDENTITY));
	content_encoding [strlen (FRONTEND_CONTENT_ENCODING_IDENTITY)] = '\0';
	char *dot = o_strrchr (path, FRONTEND_STATIC_FILE_EXT_SEPARATOR);
	
	if (dot && dot != path) {
		if (!o_strcasecmp (FRONTEND_COMPRESSED_STATIC_FILE_EXT, dot + 1)) {
			g_memcpy (content_encoding, FRONTEND_CONTENT_ENCODING_COMPRESSED,
			          strlen (FRONTEND_CONTENT_ENCODING_COMPRESSED));
			content_encoding[strlen (FRONTEND_CONTENT_ENCODING_COMPRESSED)] = '\0';
			char sub_path[dot - path + 1];
			g_memcpy (sub_path, path, dot - path);
			sub_path[dot - path] = '\0';
			dot = o_strrchr (sub_path, FRONTEND_STATIC_FILE_EXT_SEPARATOR);
			
			if (dot && dot != sub_path) {
				g_memcpy (extension, dot + 1, strlen (dot + 1)); // found ext; with compression
				extension[strlen (dot + 1)] = '\0';
			}
		}
		
		else {
			g_memcpy (extension, dot + 1, strlen (dot + 1)); // found ext; no compression
			extension[strlen (dot + 1)] = '\0';
		}
	}
}

int callback_static_file (const struct _u_request *request,
                          struct _u_response *response, void *user_data) {
	void *buffer = NULL;
	// init length and read_length to be different, for later comparison
	long length = -1;
	size_t read_length = 0;
	FILE *f;
	const char *http_url;
	
	// redirect if required
	if (u_map_has_key (&redirects, request->http_url)) {
		http_url = u_map_get (&redirects, request->http_url);
	}
	
	else {
		http_url = request->http_url;
	}
	
	char  *file_path = msprintf ("%s%s", FRONTEND_STATIC_FOLDER, http_url);
	const char *content_type;
	
	if (access (file_path, F_OK) != -1) {
		f = fopen (file_path, "rb");
		
		if (f) {
			fseek (f, 0, SEEK_END);
			length = ftell (f);
			fseek (f, 0, SEEK_SET);
			buffer = o_malloc ((size_t) length);
			
			if (buffer) {
				read_length = fread (buffer, 1, (size_t) length, f);
			}
			
			fclose (f);
		}
		
		if (read_length == length && buffer) {
			char extension[MAX_FILENAME_LENGTH + 1],
			     content_encoding[MAX_FILENAME_LENGTH + 1];
			get_filename_encoding_extension (http_url, extension, content_encoding);
			content_type = u_map_get ((struct _u_map *)user_data, extension);
			response->binary_body = buffer;
			response->binary_body_length = (size_t) length;
			response->status = MHD_HTTP_OK;
			ulfius_add_header_to_response (response, MHD_HTTP_HEADER_CONTENT_TYPE,
			                               content_type);
			ulfius_add_header_to_response (response, MHD_HTTP_HEADER_CONTENT_ENCODING,
			                               content_encoding);
		}
		
		else {
			response->status = MHD_HTTP_NOT_FOUND;
		}
	}
	
	else {
		response->status = MHD_HTTP_NOT_FOUND;
	}
	
	o_free (file_path);
	return U_CALLBACK_CONTINUE;
}

bool dataset_to_json (restrict dsp_dataset ds, const char *const keys[],
                      json_t **json_arr) {
	// TODO: check size of keys against num_fields_per_record
	*json_arr = json_array();
	
	if (!*json_arr) {
		DEBUG_NOW (REPORT_ERRORS, FE, "failed to create json array");
		return false;
	}
	
	if (ds && ds->data) {
		switch (ds->record_type) {
			case DS_REC_TYPE_STRING: {
					REGISTER ulong idx = 0, i, j;
					
					for (i = 0; i < ds->num_records; i++) {
						json_t *json_obj = json_object();
						
						if (!json_obj) {
							size_t a_index;
							json_t *a_value;
							json_array_foreach (*json_arr, a_index, a_value) {
								json_object_clear (a_value);
							}
							json_array_clear (*json_arr);
							json_decref (*json_arr);
							return false;
						}
						
						for (j = 0; j < ds->num_fields_per_record; j++) {
							json_object_set_new (json_obj, keys[j],
							                     json_string (((char **) ds->data)[idx++]));
						}
						
						json_array_append_new (*json_arr, json_obj);
					}
					
					return true;
					break;
				}
				
			default:
				break;
		}
	}
	
	return true;
}

int callback_get_data (const struct _u_request *request,
                       struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	const char *accessToken = u_map_get (request->map_cookie,
	                                     FRONTEND_KEY_ACCESS_TOKEN);
	const char *ws_slot_string = u_map_get (request->map_cookie,
	                                        FRONTEND_KEY_WEBSOCKET_SLOT);
	                                        
	if (!ws_slot_string) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
	}
	
	else {
		REGISTER
		const ds_int32_field ws_slot = atoi (ws_slot_string);
		REGISTER
		ds_int32_field this_ref_id = INVALID_REF_ID;
		FE_WS_LOCK_S
		this_ref_id = websocket_to_ref_id[ws_slot];
		FE_WS_LOCK_E
		
		if (0 > ws_slot ||
		    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot ||
		    INVALID_REF_ID == this_ref_id ||
		    !isValidWebsocketslotAccesstokenPair (accessToken, ws_slot)) {
			ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		}
		
		else {
			// TODO: error handling
			bool ret_val = true;
			const char *start_record_val = u_map_get (request->map_url,
			                                        FRONTEND_KEY_START_RECORD);
			ds_int32_field start = 0;
			
			if (start_record_val) {
				start = atoi (start_record_val);
			}
			
			const char *record_limit_val = u_map_get (request->map_url,
			                                        FRONTEND_KEY_RECORD_LIMIT);
			ulong limit = 0;
			
			if (record_limit_val) {
				limit = atoi (record_limit_val);
			}
			
			const char *record_order_val = u_map_get (request->map_url,
			                                        FRONTEND_KEY_RECORD_ORDER);
			ds_int32_field order = 0;
			
			if (record_order_val) {
				order = atoi (record_order_val);
			}
			
			const char *order_by = u_map_get (request->map_url,
			                                  FRONTEND_KEY_RECORD_ORDER_BY);
			ds_object_id_field job_id;
			g_memset (job_id, 0, sizeof (ds_object_id_field));
			const char *job_id_val = u_map_get (request->map_url,
			                                    FRONTEND_KEY_RECORD_JOB_ID);
			                                    
			if (job_id_val) {
				sprintf (&job_id[0], "%s", job_id_val);
			}
		
                        const char *hit_position_val = u_map_get (request->map_url,
                                                                FRONTEND_KEY_HIT_POSITION);
                        ds_int32_field hit_position = 0;

                        if (hit_position_val) {
                                hit_position = atoi (hit_position_val);
                        }

                        const char *hit_fe_val = u_map_get (request->map_url,
                                                                FRONTEND_KEY_HIT_FE);
                        ds_double_field hit_fe = 0.0f;

                        if (hit_fe_val) {
                                hit_fe = atof (hit_fe_val);
                        }

                        ulong hit_index=1;  // default hit index to first hit
	
			dsp_dataset ids_dataset = NULL;
			// by default only get published consensa
			ds_boolean_field get_published = true;
			
			switch (* (uchar *)user_data) {
				case 0  :
					DEBUG_NOW1 (REPORT_INFO, FE_WS,
					            "user (reference id %d) requested sequence data",
					            this_ref_id);
					ret_val = read_sequences_by_ref_id (this_ref_id, start, limit, order,
					                                    &ids_dataset);
					                                    
					if (ret_val) {
						/*
						 * if it's a guest, then merge template dataset with currently available (i.e. actual ref_id)
						 */
						dsp_dataset guest_ids_dataset = NULL;
						ret_val = read_sequences_by_ref_id (FRONTEND_GUEST_TEMPLATE_REF_ID, start,
						                                    limit, order, &guest_ids_dataset);
						                                    
						if (!ret_val && guest_ids_dataset) {
							free_dataset (guest_ids_dataset);
						}
						
						else {
							if (!ids_dataset || !ids_dataset->num_records) {
								free_dataset (ids_dataset);
								ids_dataset = guest_ids_dataset;
							}
							
							else
								if (guest_ids_dataset) {
									dsp_dataset merged_ids_dataset = NULL;
									ret_val = merge_datasets (ids_dataset, guest_ids_dataset, &merged_ids_dataset);
									free_dataset (ids_dataset);
									free_dataset (guest_ids_dataset);
									ids_dataset = merged_ids_dataset;
								}
						}
					}
					
					break;
					
				case 1 :
					FE_WS_LOCK_S
					if (ref_id_to_curator_status[ws_slot]) {
						get_published = false;
					}
					
					FE_WS_LOCK_E
					DEBUG_NOW2 (REPORT_INFO, FE_WS,
					            "user (reference id %d) requested cssd data (published=%d)", this_ref_id,
					            get_published);
					ret_val = read_cssds_by_published_status (get_published, start, limit, order,
					                                        &ids_dataset);
					break;
					
				case 2  :
					DEBUG_NOW1 (REPORT_INFO, FE_WS,
					            "user (reference id %d) requested jobs data",
					            this_ref_id);
					ret_val = read_aggregate_jobs_by_ref_id (this_ref_id, start, limit, order,
					                                        &ids_dataset);
					break;
					
				case 3 :
					DEBUG_NOW1 (REPORT_INFO, FE_WS,
					            "user (reference id %d) requested hits data",
					            this_ref_id);
					            
					if (!strcmp (DS_COL_RESULTS_HIT_POSITION, order_by)) {
						ret_val = read_results_by_job_id (&job_id, start, limit,
						                                  DS_COL_RESULTS_HIT_POSITION, DS_COL_RESULTS_HIT_FE, order, &ids_dataset);
					}
					
					else
						if (!strcmp (DS_COL_RESULTS_HIT_FE, order_by)) {
							ret_val = read_results_by_job_id (&job_id, start, limit, DS_COL_RESULTS_HIT_FE,
							                                  DS_COL_RESULTS_HIT_POSITION, order, &ids_dataset);
						}
						
						else {
							ret_val = false;
						}
						
					break;

				case 4 :
                                        DEBUG_NOW1 (REPORT_INFO, FE_WS,
                                                    "user (reference id %d) requested result index",
                                                    this_ref_id);

                                        if (!strcmp (DS_COL_RESULTS_HIT_POSITION, order_by)) {
						ret_val = get_result_index (&job_id, DS_COL_RESULTS_HIT_POSITION, DS_COL_RESULTS_HIT_FE, order,
								            hit_position, hit_fe, &hit_index);
                                        }

                                        else
                                                if (!strcmp (DS_COL_RESULTS_HIT_FE, order_by)) {
							ret_val = get_result_index (&job_id, DS_COL_RESULTS_HIT_FE, DS_COL_RESULTS_HIT_POSITION, order,
                                                                            hit_position, hit_fe, &hit_index);
                                                }

                                                else {
                                                        ret_val = false;
                                                }

                                        break;

					
				default :
					break;
			}
			
			if (ret_val) {
				if (ids_dataset || (4==* (uchar *)user_data)) {
					json_t *json_arr = NULL;
					
					switch (* (uchar *)user_data) {
						case 0  :
							ret_val = dataset_to_json (ids_dataset, DS_COLS_SEQUENCES, &json_arr);
							break;
							
						case 1  :
							ret_val = dataset_to_json (ids_dataset, DS_COLS_CSSD, &json_arr);
							break;
							
						case 2  :
							ret_val = dataset_to_json (ids_dataset, DS_COLS_JOBS, &json_arr);
							break;
							
						case 3  :
							ret_val = dataset_to_json (ids_dataset, DS_COLS_RESULTS, &json_arr);
							break;
				
						case 4  :
        						json_arr = json_object();	// TODO: error handling
							json_object_set_new (json_arr, FRONTEND_KEY_HIT_INDEX,
									     json_integer (hit_index));
							break;
					}

					if (ret_val) {
						char *json_arr_strn = json_dumps (json_arr, 0);
						ulfius_set_string_body_response (response, MHD_HTTP_OK, json_arr_strn);
						/*
						 * clean up the json_arr json array of objects [ note: all data objects have stolen references ]
						 */
						if (4==* (uchar *)user_data) {
							json_object_clear (json_arr);
						}
						else {
							size_t a_index;
							json_t *a_value;
							json_array_foreach (json_arr, a_index, a_value) {
								json_object_clear (a_value);
							}
							json_array_clear (json_arr);
						}
						json_decref (json_arr);
						
						/*
						 * also clean up original datastore-provided dataset
						 */
						if (ids_dataset) {
							free_dataset (ids_dataset);
						}
						
						free (json_arr_strn);
					}
				}
				
				if (!ret_val) {
					json_t *j_null = json_null();
					char *j_null_strn = json_dumps (j_null, 0);
					ulfius_set_string_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
					                                 j_null_strn);
					json_decref (j_null);
					
					if (ids_dataset) {
						free_dataset (ids_dataset);
					}
					
					free (j_null_strn);
					cb_ret_val = U_CALLBACK_ERROR;
				}
			}
			
			else {
				json_t *j_null = json_null();
				char *j_null_strn = json_dumps (j_null, 0);
				ulfius_set_string_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
				                                 j_null_strn);
				json_decref (j_null);
				
				if (ids_dataset) {
					free_dataset (ids_dataset);
				}
				
				free (j_null_strn);
				cb_ret_val = U_CALLBACK_ERROR;
			}
		}
	}
	
	FE_LOCK_E
	return cb_ret_val;
}

/*
 *  ulfius_add_endpoint_by_val (&frontend_instance, "GET", "/tc", NULL, 0, &callback_get_tc, NULL);
 */
int callback_get_tc (const struct _u_request *request,
                     struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	const char *accessToken = u_map_get (request->map_cookie,
	                                     FRONTEND_KEY_ACCESS_TOKEN);
	const char *ws_slot_string = u_map_get (request->map_cookie,
	                                        FRONTEND_KEY_WEBSOCKET_SLOT);
	const char *topic_strn = u_map_get (request->map_url, FRONTEND_KEY_TOPIC),
	            *capability_strn = u_map_get (request->map_url, FRONTEND_KEY_CAPABILITY);
	const int  topic_int = atoi (topic_strn),
	           capability_int = atoi (capability_strn);
	           
	if (!ws_slot_string) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	REGISTER
	int this_ref_id = INVALID_REF_ID;
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	const ds_int32_field ws_slot_int = atoi (ws_slot_string);
	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[ws_slot_int];
	FE_WS_LOCK_E
	
	if (0 > ws_slot_int ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot_int ||
	    INVALID_REF_ID == this_ref_id ||
	    !isValidWebsocketslotAccesstokenPair (accessToken, ws_slot_int)) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	#endif
	bool isCapable = false;
	
	if (FRONTEND_GUEST_TEMPLATE_REF_ID > this_ref_id) {
		// process request from guest
		switch (topic_int) {
			case SEQUENCES:
				switch (capability_int) {
					case 	NEW:
					case    SEARCH:
					case	DELETE:
						isCapable = true;
						break;
						
					default:
						break;
				}
				
				break;
				
			case CSSD:
				switch (capability_int) {
					case 	SEARCH:
						isCapable = true;
						break;
						
					default:
						break;	// no CSSD changes for guest
				}
				
				break;
				
			case JOBS:
				switch (capability_int) {
					case 	DELETE:
					case	ORDER:
					case 	SEARCH:
					case 	SHOW:
						isCapable = true;
						break;
						
					default:
						break;
				}
				
				break;
		}
	}
	
	else {
		// process request from regular user
		switch (topic_int) {
			case SEQUENCES:
				switch (capability_int) {
					case	NEW:
					case 	SEARCH:
					case 	DELETE:
						isCapable = true;
						break;
						
					default:
						break;
				}
				
				break;
				
			case CSSD:
				switch (capability_int) {
					case    NEW:
					case 	EDIT:
					case 	DELETE:
						FE_WS_LOCK_S
						if (ref_id_to_curator_status[ws_slot_int]) {
							isCapable = true;
						}
						
						FE_WS_LOCK_E
						break;
						
					case	SEARCH:
						isCapable = true;
						break;
						
					default:
						break; // no CSSD changes for regular user either
				}
				
				break;
				
			case JOBS:
				switch (capability_int) {
					case 	DELETE:
					case 	ORDER:
					case	SEARCH:
					case 	SHOW:
						isCapable = true;
						break;
						
					default:
						break;
				}
				
				break;
		}
	}
	
	json_t *json_response = json_object();
	
	if (!json_response) {
		ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
		                               NULL_JSON);
		cb_ret_val = U_CALLBACK_ERROR;
		goto cb_ret;
	}
	
	json_object_set_new (json_response, FRONTEND_KEY_TOPIC,
	                     json_integer (topic_int));
	json_object_set_new (json_response, FRONTEND_KEY_CAPABILITY,
	                     json_integer (capability_int));
	                     
	if (isCapable) {
		json_object_set_new (json_response, FRONTEND_KEY_STATUS,
		                     json_string (FRONTEND_STATUS_SUCCESS));
		ulfius_set_json_body_response (response, MHD_HTTP_OK, json_response);
	}
	
	else {
		json_object_set_new (json_response, FRONTEND_KEY_STATUS,
		                     json_string (FRONTEND_STATUS_FAIL));
		ulfius_set_json_body_response (response, MHD_HTTP_OK, json_response);
	}
	
	json_decref (json_response);
cb_ret:
	FE_LOCK_E
	return cb_ret_val;
}

/*
 *  ulfius add_endpoint_by_val (%frontend_instance, "GET", "/job", job_URI_vars_format, 0, &callback_get_job, NULL);
 */
int callback_get_job (const struct _u_request *request,
                      struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	const char *accessToken = u_map_get (request->map_cookie,
	                                     FRONTEND_KEY_ACCESS_TOKEN);
	const char *ws_slot_string = u_map_get (request->map_cookie,
	                                        FRONTEND_KEY_WEBSOCKET_SLOT);
	ds_object_id_field job_id;
	g_memset (job_id, 0, sizeof (ds_object_id_field));
	const char *job_id_val = u_map_get (request->map_url,
	                                    FRONTEND_KEY_RECORD_JOB_ID);
	                                    
	if (job_id_val) {
		sprintf (&job_id[0], "%s", job_id_val);
	}
	
	if (!ws_slot_string) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	REGISTER
	int this_ref_id = INVALID_REF_ID;
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	const ds_int32_field ws_slot_int = atoi (ws_slot_string);
	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[ws_slot_int];
	FE_WS_LOCK_E
	
	if (0 > ws_slot_int ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot_int ||
	    INVALID_REF_ID == this_ref_id ||
	    !isValidWebsocketslotAccesstokenPair (accessToken, ws_slot_int)) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	#endif
	json_t *json_response = json_object();
	
	if (!json_response) {
		ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
		                               NULL_JSON);
		cb_ret_val = U_CALLBACK_ERROR;
		goto cb_ret;
	}
	
	dsp_dataset ids_dataset = NULL;
	
	if (!read_job (&job_id, &ids_dataset)) {
		ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
		                               NULL_JSON);
		                               
		if (ids_dataset) {
			free_dataset (ids_dataset);
		}
		
		json_decref (json_response);
		cb_ret_val = U_CALLBACK_ERROR;
		goto cb_ret;
	}
	
	json_t *json_arr = NULL;
	
	if (!dataset_to_json (ids_dataset, DS_COLS_JOB, &json_arr)) {
		ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
		                               NULL_JSON);
		json_decref (json_response);
		free_dataset (ids_dataset);
		cb_ret_val = U_CALLBACK_ERROR;
		goto cb_ret;
	}
	
	char *json_arr_strn = json_dumps (json_arr, 0);
	ulfius_set_string_body_response (response, MHD_HTTP_OK, json_arr_strn);
	/*
	 * clean up the json_arr json array of objects [ note: all data objects have stolen references ]
	 */
	size_t a_index;
	json_t *a_value;
	json_array_foreach (json_arr, a_index, a_value) {
		json_object_clear (a_value);
	}
	json_array_clear (json_arr);
	json_decref (json_arr);
	json_decref (json_response);
	free_dataset (ids_dataset);
cb_ret:
	FE_LOCK_E
	return cb_ret_val;
}

/*
 *  ulfius add_endpoint_by_val (%frontend_instance, "GET", "/results", results_URI_vars_format, 0, &callback_get_result_count, NULL);
 */
int callback_get_result_count (const struct _u_request *request,
                               struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	const char *accessToken = u_map_get (request->map_cookie,
	                                     FRONTEND_KEY_ACCESS_TOKEN);
	const char *ws_slot_string = u_map_get (request->map_cookie,
	                                        FRONTEND_KEY_WEBSOCKET_SLOT);
	ds_object_id_field job_id;
	g_memset (job_id, 0, sizeof (ds_object_id_field));
	const char *job_id_val = u_map_get (request->map_url,
	                                    FRONTEND_KEY_RECORD_JOB_ID);
	                                    
	if (job_id_val) {
		sprintf (&job_id[0], "%s", job_id_val);
	}
	
	if (!ws_slot_string) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	REGISTER
	int this_ref_id = INVALID_REF_ID;
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	const ds_int32_field ws_slot_int = atoi (ws_slot_string);
	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[ws_slot_int];
	FE_WS_LOCK_E
	
	if (0 > ws_slot_int ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot_int ||
	    INVALID_REF_ID == this_ref_id ||
	    !isValidWebsocketslotAccesstokenPair (accessToken, ws_slot_int)) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	#endif
	json_t *json_response = json_object();
	nt_hit_count count = 0;
	
	if (!json_response || !read_result_count_by_job_id (&job_id, &count)) {
		ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
		                               NULL_JSON);
		                               
		if (json_response) {
			json_decref (json_response);
		}
		
		cb_ret_val = U_CALLBACK_ERROR;
		goto cb_ret;
	}
	
	json_object_set_new (json_response, FRONTEND_KEY_COUNT, json_integer (count));
	json_object_set_new (json_response, FRONTEND_KEY_STATUS,
	                     json_string (FRONTEND_STATUS_SUCCESS));
	ulfius_set_json_body_response (response, MHD_HTTP_OK, json_response);
	json_decref (json_response);
cb_ret:
	FE_LOCK_E
	return cb_ret_val;
}

/*
 *  ulfius add_endpoint_by_val (%frontend_instance, "GET", "/results-summary", job_URI_vars_format, 0, &callback_get_results_summary, NULL);
 */
int callback_get_results_summary (const struct _u_request *request,
                                  struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	const char *accessToken = u_map_get (request->map_cookie,
	                                     FRONTEND_KEY_ACCESS_TOKEN);
	const char *ws_slot_string = u_map_get (request->map_cookie,
	                                        FRONTEND_KEY_WEBSOCKET_SLOT);
	ds_object_id_field job_id;
	g_memset (job_id, 0, sizeof (ds_object_id_field));
	const char *job_id_val = u_map_get (request->map_url,
	                                    FRONTEND_KEY_RECORD_JOB_ID);
	                                    
	if (job_id_val) {
		sprintf (&job_id[0], "%s", job_id_val);
	}
	
	if (!ws_slot_string) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	REGISTER
	int this_ref_id = INVALID_REF_ID;
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	const ds_int32_field ws_slot_int = atoi (ws_slot_string);
	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[ws_slot_int];
	FE_WS_LOCK_E
	
	if (0 > ws_slot_int ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot_int ||
	    INVALID_REF_ID == this_ref_id ||
	    !isValidWebsocketslotAccesstokenPair (accessToken, ws_slot_int)) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	#endif
	json_t *json_response = json_object();
	dsp_dataset results_summary = NULL;
	
	if (!json_response ||
	    !read_results_fe_distribution (&job_id, &results_summary)) {
		ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
		                               NULL_JSON);
		                               
		if (json_response) {
			json_decref (json_response);
		}
		
		cb_ret_val = U_CALLBACK_ERROR;
		goto cb_ret;
	}
	
	if (results_summary) {
		json_t *json_arr = NULL;
		REGISTER
		bool ret_val = dataset_to_json (results_summary, DS_COLS_RESULTS_SUMMARY,
		                                &json_arr);
		                                
		if (ret_val) {
			char *json_arr_strn = json_dumps (json_arr, 0);
			ulfius_set_string_body_response (response, MHD_HTTP_OK, json_arr_strn);
			/*
			 * clean up the json_arr json array of objects
			* note: all data objects have stolen references
			 */
			size_t a_index;
			json_t *a_value;
			json_array_foreach (json_arr, a_index, a_value) {
				json_object_clear (a_value);
			}
			json_array_clear (json_arr);
			json_decref (json_arr);
			/*
			 * also clean up original datastore-provided dataset
			 */
			free (json_arr_strn);
		}
		
		else {
			json_t *j_null = json_null();
			char *j_null_strn = json_dumps (j_null, 0);
			ulfius_set_string_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
			                                 j_null_strn);
			json_decref (j_null);
			free (j_null_strn);
			cb_ret_val = U_CALLBACK_ERROR;
		}
		
		free_dataset (results_summary);
	}
	
	else {
		json_t *j_null = json_null();
		char *j_null_strn = json_dumps (j_null, 0);
		ulfius_set_string_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
		                                 j_null_strn);
		json_decref (j_null);
		free (j_null_strn);
		cb_ret_val = U_CALLBACK_ERROR;
	}
	
cb_ret:
	FE_LOCK_E
	return cb_ret_val;
}

/*
 *  ulfius add_endpoint_by_val (%frontend_instance, "GET", "/result", result_URI_vars_format, 0, &callback_get_result_count, NULL);
 */
int callback_get_result_total_time (const struct _u_request *request,
                                    struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	const char *accessToken = u_map_get (request->map_cookie,
	                                     FRONTEND_KEY_ACCESS_TOKEN);
	const char *ws_slot_string = u_map_get (request->map_cookie,
	                                        FRONTEND_KEY_WEBSOCKET_SLOT);
	ds_object_id_field job_id;
	g_memset (job_id, 0, sizeof (ds_object_id_field));
	const char *job_id_val = u_map_get (request->map_url,
	                                    FRONTEND_KEY_RECORD_JOB_ID);
	                                    
	if (job_id_val) {
		sprintf (&job_id[0], "%s", job_id_val);
	}
	
	if (!ws_slot_string) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	REGISTER
	int this_ref_id = INVALID_REF_ID;
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	const ds_int32_field ws_slot_int = atoi (ws_slot_string);
	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[ws_slot_int];
	FE_WS_LOCK_E
	
	if (0 > ws_slot_int ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot_int ||
	    INVALID_REF_ID == this_ref_id ||
	    !isValidWebsocketslotAccesstokenPair (accessToken, ws_slot_int)) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	#endif
	json_t *json_response = json_object();
	float total_time = 0.0f;
	
	if (!json_response ||
	    !read_result_total_time_by_job_id (&job_id, &total_time)) {
		ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
		                               NULL_JSON);
		                               
		if (json_response) {
			json_decref (json_response);
		}
		
		cb_ret_val = U_CALLBACK_ERROR;
		goto cb_ret;
	}
	
	json_object_set_new (json_response, FRONTEND_KEY_TIME, json_real (total_time));
	json_object_set_new (json_response, FRONTEND_KEY_STATUS,
	                     json_string (FRONTEND_STATUS_SUCCESS));
	ulfius_set_json_body_response (response, MHD_HTTP_OK, json_response);
	json_decref (json_response);
cb_ret:
	FE_LOCK_E
	return cb_ret_val;
}

/*
 *  ulfius_add_endpoint_by_val (&frontend_instance, "DELETE", "/sequence", sequence_URI_vars_format, 0, &callback_delete_sequence, NULL);
 */
int callback_delete_sequence (const struct _u_request *request,
                              struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	const char *accessToken = u_map_get (request->map_cookie,
	                                     FRONTEND_KEY_ACCESS_TOKEN);
	const char *ws_slot_string = u_map_get (request->map_cookie,
	                                        FRONTEND_KEY_WEBSOCKET_SLOT);
	const char *sequence_id_strn = u_map_get (request->map_url,
	                                        FRONTEND_KEY_SEQUENCE_ID);
	                                        
	if (!ws_slot_string) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	REGISTER
	int this_ref_id = INVALID_REF_ID;
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	const ds_int32_field ws_slot_int = atoi (ws_slot_string);
	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[ws_slot_int];
	FE_WS_LOCK_E
	
	if (0 > ws_slot_int ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot_int ||
	    INVALID_REF_ID == this_ref_id ||
	    !isValidWebsocketslotAccesstokenPair (accessToken, ws_slot_int)) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	#endif
	json_t *json_response = json_object();
	
	if (!json_response) {
		ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
		                               NULL_JSON);
		cb_ret_val = U_CALLBACK_ERROR;
		goto cb_ret;
	}
	
	if (delete_sequence_by_id ((ds_object_id_field *)sequence_id_strn,
	                           this_ref_id)) {
		json_object_set_new (json_response, FRONTEND_KEY_STATUS,
		                     json_string (FRONTEND_STATUS_SUCCESS));
		ulfius_set_json_body_response (response, MHD_HTTP_OK, json_response);
		dsp_dataset ids_dataset = NULL;
		
		if (!read_job_ids_by_sequence_id ((ds_object_id_field *)sequence_id_strn,
		                                  &ids_dataset)) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "could not fetch job ids for user (reference id %d)",
			            this_ref_id);
		}
		
		else {
			if (ids_dataset) {
				for (unsigned long i = 0; i < ids_dataset->num_records; i++) {
					delete_job ((ds_object_id_field *) (ids_dataset->data[i]), this_ref_id);
					delete_results_by_job_id ((ds_object_id_field *) (ids_dataset->data[i]),
					                          this_ref_id);
				}
				
				fflush (stdout);
			}
		}
		
		if (ids_dataset) {
			free_dataset (ids_dataset);
		}
	}
	
	else {
		/*
		 * presumed not found
		 */
		json_object_set_new (json_response, FRONTEND_KEY_STATUS,
		                     json_string (FRONTEND_STATUS_FAIL));
		ulfius_set_json_body_response (response, MHD_HTTP_NOT_FOUND, json_response);
	}
	
	json_decref (json_response);
cb_ret:
	FE_LOCK_E
	return cb_ret_val;
}

/*
 *  ulfius_add_endpoint_by_val (&frontend_instance, "DELETE", "/job-w-results", job_URI_vars_format, 0, &callback_delete_job_w_results, NULL);
 */
int callback_delete_job_w_results (const struct _u_request *request,
                                   struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	const char *accessToken = u_map_get (request->map_cookie,
	                                     FRONTEND_KEY_ACCESS_TOKEN);
	const char *ws_slot_string = u_map_get (request->map_cookie,
	                                        FRONTEND_KEY_WEBSOCKET_SLOT);
	const char *job_id_strn = u_map_get (request->map_url,
	                                     FRONTEND_KEY_RECORD_JOB_ID);
	                                     
	if (!ws_slot_string) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	REGISTER
	int this_ref_id = INVALID_REF_ID;
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	const ds_int32_field ws_slot_int = atoi (ws_slot_string);
	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[ws_slot_int];
	FE_WS_LOCK_E
	
	if (0 > ws_slot_int ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot_int ||
	    INVALID_REF_ID == this_ref_id ||
	    !isValidWebsocketslotAccesstokenPair (accessToken, ws_slot_int)) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	#endif
	json_t *json_response = json_object();
	
	if (!json_response) {
		ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
		                               NULL_JSON);
		cb_ret_val = U_CALLBACK_ERROR;
		goto cb_ret;
	}
	
	if (delete_job ((ds_object_id_field *)job_id_strn, this_ref_id) &&
	    delete_results_by_job_id ((ds_object_id_field *)job_id_strn, this_ref_id)) {
		// note: assumes that results and job documents are consistent and
		//       that no results exist for a non-existant job (id)
		json_object_set_new (json_response, FRONTEND_KEY_STATUS,
		                     json_string (FRONTEND_STATUS_SUCCESS));
		ulfius_set_json_body_response (response, MHD_HTTP_OK, json_response);
	}
	
	else {
		/*
		 * presumed not found
		 */
		json_object_set_new (json_response, FRONTEND_KEY_STATUS,
		                     json_string (FRONTEND_STATUS_FAIL));
		ulfius_set_json_body_response (response, MHD_HTTP_NOT_FOUND, json_response);
	}
	
	json_decref (json_response);
cb_ret:
	FE_LOCK_E
	return cb_ret_val;
}

/*
 *  ulfius_add_endpoint_by_val (&frontend_instance, "POST", "/job", NULL, 0, &callback_post_job, NULL);
 */
int callback_post_job (const struct _u_request *request,
                       struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	// TODO: use cookies for authentication....
	json_error_t error;
	json_t *json_body_request = ulfius_get_json_body_request (request, &error);
	
	if (!json_body_request || json_typeof (json_body_request) != JSON_OBJECT ||
	    JOB_POST_NUM_KEYS != json_object_size (json_body_request)) {
		if (!json_body_request) {
			DEBUG_NOW1 (REPORT_ERRORS, FE, "json error '%s' found when posting job",
			            error.text);
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, FE, "malformed json request when posting job");
		}
		
		ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	json_t *cssd_id = json_object_get (json_body_request, DS_COL_JOB_CSSD_ID);
	json_t *seq_id = json_object_get (json_body_request, DS_COL_JOB_SEQUENCE_ID);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	json_t *ws_slot = json_object_get (json_body_request,
	                                   FRONTEND_KEY_WEBSOCKET_SLOT);
	json_t *access_token = json_object_get (json_body_request,
	                                        FRONTEND_KEY_ACCESS_TOKEN);
	#endif
	                                        
	if (!cssd_id || !seq_id
    #if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	    || !ws_slot || !access_token
    #endif
	   ) {
		if (!cssd_id) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "'%s' not found in json object when posting job",
			            DS_COL_JOB_CSSD_ID);
		}
		
		if (!seq_id) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "'%s' not found in json object when posting job",
			            DS_COL_JOB_SEQUENCE_ID);
		}
		
		#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
		
		if (!access_token) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "'%s' not found in json object when posting job",
			            FRONTEND_KEY_ACCESS_TOKEN);
		}
		
		if (!ws_slot) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "'%s' not found in json object when posting job",
			            FRONTEND_KEY_WEBSOCKET_SLOT);
		}
		
		#endif
		ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	// json_incref on cssd_id, seq_id - so will eventually
	// be freed with json_response, or on error
	json_incref (cssd_id);
	json_incref (seq_id);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	json_incref (ws_slot);
	json_incref (access_token);
	#endif
	const char *cssd_id_strn = json_string_value (cssd_id),
	            *seq_id_strn = json_string_value (seq_id)
	                           #if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	                           , *access_token_strn = json_string_value (access_token)
	                           #endif
	                           ;
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	const ds_int32_field ws_slot_int = json_integer_value (ws_slot);
	REGISTER
	int this_ref_id = INVALID_REF_ID;
	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[ws_slot_int];
	FE_WS_LOCK_E
	
	if (0 > ws_slot_int ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot_int ||
	    INVALID_REF_ID == this_ref_id ||
	    !isValidWebsocketslotAccesstokenPair (access_token_strn, ws_slot_int)) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		json_decref (seq_id);
		json_decref (cssd_id);
		json_decref (ws_slot);
		json_decref (access_token);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	#endif
	char ds_cssd_id[DS_OBJ_ID_LENGTH + 1], ds_seq_id[DS_OBJ_ID_LENGTH + 1];
	
	if (strlen (seq_id_strn) != DS_OBJ_ID_LENGTH) {
		DEBUG_NOW1 (REPORT_ERRORS, FE,
		            "sequence id string has incorrect length (%d) when posting job",
		            strlen (seq_id_strn));
		ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		json_decref (seq_id);
		json_decref (cssd_id);
		#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
		json_decref (ws_slot);
		json_decref (access_token);
		#endif
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	else {
		g_memcpy ((void *)ds_seq_id, seq_id_strn, DS_OBJ_ID_LENGTH + 1);
	}
	
	if (strlen (cssd_id_strn) != DS_OBJ_ID_LENGTH) {
		DEBUG_NOW1 (REPORT_ERRORS, FE,
		            "cssd id string has incorrect length (%d) when posting job",
		            strlen (cssd_id_strn));
		ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		json_decref (seq_id);
		json_decref (cssd_id);
		#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
		json_decref (ws_slot);
		json_decref (access_token);
		#endif
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	else {
		g_memcpy ((void *)ds_cssd_id, cssd_id_strn, DS_OBJ_ID_LENGTH + 1);
	}
	
	json_t *json_response = json_object();
	dsp_dataset seq_dataset = NULL, cssd_dataset = NULL;
	
	if (!json_response || !read_sequence_by_id (&ds_seq_id, &seq_dataset) ||
	    !read_cssd_by_id (&ds_cssd_id, &cssd_dataset)) {
		if (json_response) {
			json_decref (json_response);
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, FE,
			           "could not create json response object when posting job");
			ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
			                               NULL_JSON);
			cb_ret_val = U_CALLBACK_ERROR;
		}
		
		if (seq_dataset) {
			free_dataset (seq_dataset);
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, FE,
			           "could not read sequence dataset when posting job");
		}
		
		if (cssd_dataset) {
			free_dataset (cssd_dataset);
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, FE,
			           "could not read cssd dataset when posting job");
		}
		
		json_decref (json_body_request);
		json_decref (cssd_id);
		json_decref (seq_id);
		#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
		json_decref (ws_slot);
		json_decref (access_token);
		#endif
		goto cb_ret;
	}
	
	if (seq_dataset->num_records > 0 && cssd_dataset->num_records > 0) {
		//
		// process request by starting filter
		//
		ntp_model model = NULL;
		char *ss, *pos_var;
		bool success = false;
		ss = malloc (MAX_MODEL_STRING_LEN + 1);
		pos_var = malloc (MAX_MODEL_STRING_LEN + 1);
		
		if (ss && pos_var) {
			split_cssd ((char *) cssd_dataset->data[DS_COL_CSSD_STRING_IDX - 1], &ss,
			            &pos_var);
			char *err_msg = NULL;
			
			if (convert_CSSD_to_model (ss, pos_var, &model, &err_msg)) {
				if (compare_CSSD_model_strings (ss, pos_var, model)) {
					nt_seg_size fp_lead_min_span, fp_lead_max_span, tp_trail_min_span,
					            tp_trail_max_span;
					nt_stack_size stack_min_size, stack_max_size;
					nt_stack_idist stack_min_idist, stack_max_idist;
					ntp_element el_with_largest_stack = NULL;
					
					if (get_model_limits (model,
					                      &fp_lead_min_span, &fp_lead_max_span,
					                      &stack_min_size, &stack_max_size,
					                      &stack_min_idist, &stack_max_idist,
					                      &tp_trail_min_span, &tp_trail_max_span, &el_with_largest_stack)) {
						ds_object_id_field new_job_object_id;
						
						if (create_job (&ds_seq_id, &ds_cssd_id, DS_JOB_STATUS_INIT, DS_JOB_ERROR_OK,
						                this_ref_id, &new_job_object_id)) {
							convert_timebytes_to_dec_representation (&new_job_object_id,
							                                        &new_job_object_id);
							ds_object_id_field new_job_object_id_char;
							convert_rt_bytes_to_string (&new_job_object_id, &new_job_object_id_char);
							const char *this_seq = (char *) seq_dataset->data[DS_COL_SEQUENCE_3P_UTR_IDX -
							                                                                   1];
							DEBUG_NOW5 (REPORT_INFO, FE,
							            "user (reference id %d) posted new job '%.*s' (sequence '%s', cssd '%s')",
							            this_ref_id, NUM_RT_BYTES, new_job_object_id_char, ds_seq_id, ds_cssd_id);
							            
							if (filter_seq (this_seq,
							                strlen (this_seq),
							                model,
							                el_with_largest_stack,
							                fp_lead_min_span, fp_lead_max_span,
							                stack_min_size, stack_max_size,
							                stack_min_idist, stack_max_idist,
							                tp_trail_min_span, tp_trail_max_span,
							                new_job_object_id_char)) {
								success = true;
							}
						}
						
						else {
							DEBUG_NOW2 (REPORT_ERRORS, FE,
							            "failed to create job for sequence id %s and cssd id %s", ds_seq_id,
							            ds_cssd_id);
						}
					}
				}
				
				finalize_model (model);
			}
			
			else {
				if (err_msg) {
					DEBUG_NOW1 (REPORT_ERRORS, FE,
					            "failed to convert cssd to model ('%s') when posting job",
					            err_msg);
					FREE_DEBUG (err_msg, "err_msg from convert_CSSD_to_model in callback_post_job");
				}
			}
		}
		
		if (ss) {
			free (ss);
		}
		
		if (pos_var) {
			free (pos_var);
		}
		
		if (success) {
			json_object_set_new (json_response, FRONTEND_KEY_STATUS,
			                     // _set_new steals reference to new string
			                     json_string (FRONTEND_STATUS_SUCCESS));
			json_object_set_new (json_response, DS_COL_SEQUENCE_3P_UTR,
			                     json_string ((char *) seq_dataset->data[DS_COL_SEQUENCE_3P_UTR_IDX - 1]));
			json_object_set_new (json_response, DS_COL_CSSD_STRING,
			                     json_string ((char *) cssd_dataset->data[DS_COL_CSSD_STRING_IDX - 1]));
			// include requested seq_/cssd_id
			json_object_set (json_response, DS_COL_JOB_SEQUENCE_ID, seq_id);
			json_object_set (json_response, DS_COL_JOB_CSSD_ID, cssd_id);
			ulfius_set_json_body_response (response, MHD_HTTP_ACCEPTED, json_response);
		}
		
		else {
			json_object_set_new (json_response, FRONTEND_KEY_STATUS,
			                     json_string (FRONTEND_STATUS_FAIL));
			ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
			                               json_response);
		}
		
		json_decref (cssd_id);
		json_decref (seq_id);
		#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
		json_decref (ws_slot);
		json_decref (access_token);
		#endif
	}
	
	else {
		json_object_set_new (json_response, FRONTEND_KEY_STATUS,
		                     json_string (FRONTEND_STATUS_FAIL));
		ulfius_set_json_body_response (response, MHD_HTTP_ACCEPTED, json_response);
		json_decref (cssd_id);
		json_decref (seq_id);
		#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
		json_decref (ws_slot);
		json_decref (access_token);
		#endif
	}
	
	json_decref (json_body_request);
	json_decref (json_response);
	free_dataset (seq_dataset);
	free_dataset (cssd_dataset);
	cb_ret_val = U_CALLBACK_CONTINUE;
cb_ret:
	FE_LOCK_E
	return cb_ret_val;
}

/*
 *  ulfius_add_endpoint_by_val (&frontend_instance, "POST", "/sequence", NULL, 0, &callback_post_sequence, NULL);
 */
int callback_post_sequence (const struct _u_request *request,
                            struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	// TODO: use cookies for authentication....
	json_error_t error;
	json_t *json_body_request = ulfius_get_json_body_request (request, &error);
	
	if (!json_body_request || json_typeof (json_body_request) != JSON_OBJECT ||
	    SEQUENCE_POST_NUM_KEYS != json_object_size (json_body_request)) {
		if (!json_body_request) {
			DEBUG_NOW1 (REPORT_ERRORS, FE, "json error ('%s') when posting sequence",
			            error.text);
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, FE, "malformed json request when posting sequence");
		}
		
		ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	json_t *accession = json_object_get (json_body_request,
	                                     DS_COL_SEQUENCE_ACCESSION);
	json_t *definition = json_object_get (json_body_request,
	                                      DS_COL_SEQUENCE_DEFINITION);
	json_t *seqnt = json_object_get (json_body_request, DS_COL_SEQUENCE_3P_UTR);
	json_t *group = json_object_get (json_body_request, DS_COL_SEQUENCE_GROUP);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	json_t *ws_slot = json_object_get (json_body_request,
	                                   FRONTEND_KEY_WEBSOCKET_SLOT);
	json_t *access_token = json_object_get (json_body_request,
	                                        FRONTEND_KEY_ACCESS_TOKEN);
	#endif
	                                        
	if (!accession || !definition || !seqnt || !group
    #if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	    || !ws_slot || !access_token
    #endif
	   ) {
		if (!accession) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when posting sequence",
			            DS_COL_SEQUENCE_ACCESSION);
		}
		
		if (!definition) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when posting sequence",
			            DS_COL_SEQUENCE_DEFINITION);
		}
		
		if (!seqnt) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when posting sequence",
			            DS_COL_SEQUENCE_3P_UTR);
		}
		
		if (!group) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when posting sequence",
			            DS_COL_SEQUENCE_GROUP);
		}
		
		#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
		
		if (!access_token) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when posting sequence",
			            FRONTEND_KEY_ACCESS_TOKEN);
		}
		
		if (!ws_slot) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when posting sequence",
			            FRONTEND_KEY_WEBSOCKET_SLOT);
		}
		
		#endif
		ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	// validate definition string
	char *err = NULL;
	const char *definition_strn = json_string_value (definition);
	
	if (!is_valid_definition (definition_strn, &err)) {
		if (NULL != err) {
			json_t *json_body = json_object();
			json_object_set_new (json_body, FRONTEND_KEY_ERROR, json_string (err));
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, json_body);
			json_decref (json_body);
			FREE_DEBUG (err, "err message string in is_valid_definition");
		}
		
		else {
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		}
		
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	// validate accession string
	const char *accession_strn = json_string_value (accession);
	
	if (!is_valid_accession (accession_strn, &err)) {
		if (NULL != err) {
			json_t *json_body = json_object();
			json_object_set_new (json_body, FRONTEND_KEY_ERROR, json_string (err));
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, json_body);
			json_decref (json_body);
			FREE_DEBUG (err, "err message string in is_valid_accession");
		}
		
		else {
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		}
		
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	// validate group string
	const char *group_strn = json_string_value (group);
	
	if (!is_valid_group (group_strn, &err)) {
		if (NULL != err) {
			json_t *json_body = json_object();
			json_object_set_new (json_body, FRONTEND_KEY_ERROR, json_string (err));
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, json_body);
			json_decref (json_body);
			FREE_DEBUG (err, "err message string in is_valid_group");
		}
		
		else {
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		}
		
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	// validate sequence string
	const char *seqnt_strn = json_string_value (seqnt);
	
	if (!is_valid_sequence (seqnt_strn, &err)) {
		if (NULL != err) {
			json_t *json_body = json_object();
			json_object_set_new (json_body, FRONTEND_KEY_ERROR, json_string (err));
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, json_body);
			json_decref (json_body);
			FREE_DEBUG (err, "err message string in is_valid_sequence");
		}
		
		else {
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		}
		
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	// json_incref on cssd_id, seq_id - so will eventually
	// be freed with json_response, or on error
	json_incref (accession);
	json_incref (definition);
	json_incref (group);
	json_incref (seqnt);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	json_incref (ws_slot);
	json_incref (access_token);
	#endif
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	const char *access_token_strn = json_string_value (access_token);
	const ds_int32_field ws_slot_int = json_integer_value (ws_slot);
	REGISTER
	int this_ref_id = INVALID_REF_ID;
	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[ws_slot_int];
	FE_WS_LOCK_E
	
	if (0 > ws_slot_int ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot_int ||
	    INVALID_REF_ID == this_ref_id ||
	    !isValidWebsocketslotAccesstokenPair (access_token_strn, ws_slot_int)) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		json_decref (accession);
		json_decref (definition);
		json_decref (group);
		json_decref (seqnt);
		json_decref (ws_slot);
		json_decref (access_token);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	#endif
	json_t *json_response = json_object();
	char new_sequence_ojb_id[NUM_RT_BYTES + 1];
	// only necessary to keep valgrind silent
	g_memset (new_sequence_ojb_id, 0, NUM_RT_BYTES + 1);
	
	if (!json_response ||
	    !create_sequence ((char *)group_strn, (char *)definition_strn,
	                      (char *)accession_strn, (char *)seqnt_strn, this_ref_id,
	                      &new_sequence_ojb_id)) {
		if (json_response) {
			json_decref (json_response);
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, FE,
			           "could not create json response object when posting sequence");
			ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
			                               NULL_JSON);
			cb_ret_val = U_CALLBACK_ERROR;
		}
		
		json_decref (json_body_request);
		json_decref (accession);
		json_decref (definition);
		json_decref (group);
		json_decref (seqnt);
		#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
		json_decref (ws_slot);
		json_decref (access_token);
		#endif
		goto cb_ret;
	}
	
	ulfius_set_json_body_response (response, MHD_HTTP_ACCEPTED, NULL_JSON);
	json_decref (accession);
	json_decref (definition);
	json_decref (group);
	json_decref (seqnt);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	json_decref (ws_slot);
	json_decref (access_token);
	#endif
	cb_ret_val = U_CALLBACK_CONTINUE;
cb_ret:
	FE_LOCK_E
	return cb_ret_val;
}

/*
 *  ulfius_add_endpoint_by_val (&frontend_instance, "POST", "/cssd", NULL, 0, &callback_post_cssd, NULL);
 */
int callback_post_cssd (const struct _u_request *request,
                        struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	// TODO: use cookies for authentication....
	json_error_t error;
	json_t *json_body_request = ulfius_get_json_body_request (request, &error);
	
	if (!json_body_request || json_typeof (json_body_request) != JSON_OBJECT ||
	    CSSD_POST_NUM_KEYS != json_object_size (json_body_request)) {
		if (!json_body_request) {
			DEBUG_NOW1 (REPORT_ERRORS, FE, "json error ('%s') when posting cssd",
			            error.text);
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, FE, "malformed json request when posting cssd");
		}
		
		ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	json_t *cssd = json_object_get (json_body_request, DS_COL_CSSD_STRING);
	json_t *name = json_object_get (json_body_request, DS_COL_CSSD_NAME);
	json_t *published = json_object_get (json_body_request, DS_COL_CSSD_PUBLISHED);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	json_t *ws_slot = json_object_get (json_body_request,
	                                   FRONTEND_KEY_WEBSOCKET_SLOT);
	json_t *access_token = json_object_get (json_body_request,
	                                        FRONTEND_KEY_ACCESS_TOKEN);
	#endif
	                                        
	if (!cssd || !name || !published
    #if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	    || !ws_slot || !access_token
    #endif
	   ) {
		if (!cssd) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when posting cssd",
			            DS_COL_CSSD_STRING);
		}
		
		if (!name) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when posting cssd", DS_COL_CSSD_NAME);
		}
		
		if (!published) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when posting cssd",
			            DS_COL_CSSD_PUBLISHED);
		}
		
		#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
		
		if (!access_token) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when posting cssd",
			            FRONTEND_KEY_ACCESS_TOKEN);
		}
		
		if (!ws_slot) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when posting cssd",
			            FRONTEND_KEY_WEBSOCKET_SLOT);
		}
		
		#endif
		ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	// json_incref on cssd_id, seq_id - so will eventually
	// be freed with json_response, or on error
	json_incref (cssd);
	json_incref (name);
	json_incref (published);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	json_incref (ws_slot);
	json_incref (access_token);
	#endif
	const char *cssd_strn = json_string_value (cssd),
	            *name_strn = json_string_value (name)
	                         #if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	                         , *access_token_strn = json_string_value (access_token)
	                         #endif
	                         ;
	const bool published_value = json_boolean_value (published);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	const ds_int32_field ws_slot_int = json_integer_value (ws_slot);
	REGISTER
	int this_ref_id = INVALID_REF_ID;
	bool is_curator = false;
	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[ws_slot_int];
	is_curator = ref_id_to_curator_status[ws_slot_int];
	FE_WS_LOCK_E
	
	if (!is_curator ||
	    0 > ws_slot_int ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot_int ||
	    INVALID_REF_ID == this_ref_id ||
	    !isValidWebsocketslotAccesstokenPair (access_token_strn, ws_slot_int)) {
		if (!is_curator) {
			DEBUG_NOW1 (REPORT_WARNINGS, FE,
			            "user (reference id %d) attempting cssd creation without curator status",
			            this_ref_id);
		}
		
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		json_decref (cssd);
		json_decref (name);
		json_decref (published);
		json_decref (ws_slot);
		json_decref (access_token);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	#endif
	ntp_model model = NULL;
	char *err_msg = NULL;
	char *ss = NULL, *pos_var = NULL;
	ss = malloc (MAX_MODEL_STRING_LEN + 1);
	pos_var = malloc (MAX_MODEL_STRING_LEN + 1);
	bool cssd_success = false;
	
	if (ss && pos_var) {
		split_cssd (cssd_strn, &ss, &pos_var);
		
		if (convert_CSSD_to_model (ss, pos_var, &model, &err_msg)) {
			if (compare_CSSD_model_strings (ss, pos_var, model)) {
				cssd_success = true;
			}
			
			finalize_model (model);
		}
		
		free (ss);
		free (pos_var);
	}
	
	if (!cssd_success) {
		if (NULL != err_msg) {
			json_t *json_body = json_object();
			json_object_set_new (json_body, FRONTEND_KEY_ERROR, json_string (err_msg));
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, json_body);
			json_decref (json_body);
			FREE_DEBUG (err_msg,
			            "err_msg from convert_CSSD_to_model in callback_post_cssd");
		}
		
		else {
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		}
		
		json_decref (cssd);
		json_decref (name);
		json_decref (published);
		json_decref (ws_slot);
		json_decref (access_token);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	json_t *json_response = json_object();
	char new_cs_ojb_id[NUM_RT_BYTES + 1];
	g_memset (new_cs_ojb_id, 0,
	          NUM_RT_BYTES + 1); // only necessary to keep valgrind silent
	          
	if (!json_response ||
	    !create_cssd ((char *)cssd_strn, (char *)name_strn, this_ref_id,
	                  published_value, &new_cs_ojb_id)) {
		if (json_response) {
			json_decref (json_response);
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, FE,
			           "could not create json response object when posting cssd");
			ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
			                               NULL_JSON);
			cb_ret_val = U_CALLBACK_ERROR;
		}
		
		json_decref (json_body_request);
		json_decref (cssd);
		json_decref (name);
		json_decref (published);
		#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
		json_decref (ws_slot);
		json_decref (access_token);
		#endif
		goto cb_ret;
	}
	
	ulfius_set_json_body_response (response, MHD_HTTP_ACCEPTED, NULL_JSON);
	json_decref (cssd);
	json_decref (name);
	json_decref (published);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	json_decref (ws_slot);
	json_decref (access_token);
	#endif
	cb_ret_val = U_CALLBACK_CONTINUE;
	
	if (published_value) {
		// notify all ref_ids only when public CSSD created
		notify_all_ref_ids ((char *)new_cs_ojb_id, DS_COL_CSSD, DS_NOTIFY_OP_INSERT);
	}
	
cb_ret:
	FE_LOCK_E
	return cb_ret_val;
}

/*
 *  ulfius_add_endpoint_by_val (&frontend_instance, "PUT", "/cssd", NULL, 0, &callback_put_cssd, NULL);
 */
int callback_put_cssd (const struct _u_request *request,
                       struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	// TODO: use cookies for authentication....
	json_error_t error;
	json_t *json_body_request = ulfius_get_json_body_request (request, &error);
	
	if (!json_body_request || json_typeof (json_body_request) != JSON_OBJECT ||
	    CSSD_PUT_NUM_KEYS != json_object_size (json_body_request)) {
		if (!json_body_request) {
			DEBUG_NOW1 (REPORT_ERRORS, FE, "json error ('%s') when putting cssd",
			            error.text);
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, FE, "malformed json request when putting cssd");
		}
		
		ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	json_t *id = json_object_get (json_body_request, DS_COL_CSSD_ID);
	json_t *cssd = json_object_get (json_body_request, DS_COL_CSSD_STRING);
	json_t *name = json_object_get (json_body_request, DS_COL_CSSD_NAME);
	json_t *published = json_object_get (json_body_request, DS_COL_CSSD_PUBLISHED);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	json_t *ws_slot = json_object_get (json_body_request,
	                                   FRONTEND_KEY_WEBSOCKET_SLOT);
	json_t *access_token = json_object_get (json_body_request,
	                                        FRONTEND_KEY_ACCESS_TOKEN);
	#endif
	                                        
	if (!id || !cssd || !name || !published
    #if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	    || !ws_slot || !access_token
    #endif
	   ) {
		if (!id) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when putting cssd", DS_COL_CSSD_ID);
		}
		
		if (!cssd) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when putting cssd",
			            DS_COL_CSSD_STRING);
		}
		
		if (!name) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when putting cssd", DS_COL_CSSD_NAME);
		}
		
		if (!published) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when putting cssd",
			            DS_COL_CSSD_PUBLISHED);
		}
		
		#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
		
		if (!access_token) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when putting cssd",
			            FRONTEND_KEY_ACCESS_TOKEN);
		}
		
		if (!ws_slot) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "%s not found in json object when putting cssd",
			            FRONTEND_KEY_WEBSOCKET_SLOT);
		}
		
		#endif
		ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	// json_incref on cssd_id, seq_id - so will eventually
	// be freed with json_response, or on error
	json_incref (id);
	json_incref (cssd);
	json_incref (name);
	json_incref (published);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	json_incref (ws_slot);
	json_incref (access_token);
	#endif
	const char *id_strn = json_string_value (id),
	            *cssd_strn = json_string_value (cssd),
	             *name_strn = json_string_value (name)
	                          #if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	                          , *access_token_strn = json_string_value (access_token)
	                          #endif
	                          ;
	const bool published_value = json_boolean_value (published);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	const ds_int32_field ws_slot_int = json_integer_value (ws_slot);
	REGISTER
	int this_ref_id = INVALID_REF_ID;
	bool is_curator = false;
	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[ws_slot_int];
	is_curator = ref_id_to_curator_status[ws_slot_int];
	FE_WS_LOCK_E
	
	if (!is_curator ||
	    NUM_RT_BYTES != strlen (id_strn) ||
	    0 > ws_slot_int ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot_int ||
	    INVALID_REF_ID == this_ref_id ||
	    !isValidWebsocketslotAccesstokenPair (access_token_strn, ws_slot_int)) {
		if (!is_curator) {
			DEBUG_NOW1 (REPORT_WARNINGS, FE,
			            "user (reference id %d) attempting to modify cssd without curator status",
			            this_ref_id);
		}
		
		if (NUM_RT_BYTES != strlen (id_strn)) {
			DEBUG_NOW2 (REPORT_ERRORS, FE,
			            "invalid %s string length (%d) found when putting cssd", DS_COL_CSSD_ID,
			            strlen (id_strn));
		}
		
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		json_decref (id);
		json_decref (cssd);
		json_decref (name);
		json_decref (published);
		json_decref (ws_slot);
		json_decref (access_token);
		json_decref (json_body_request);
		goto cb_ret;
	}
	
	#endif
	ds_boolean_field prev_published_status = false;
	get_cssd_published_status ((ds_object_id_field *)id_strn,
	                           &prev_published_status);
	json_t *json_response = json_object();
	
	if (!json_response ||
	    !update_cssd ((ds_object_id_field *)id_strn, (char *)cssd_strn,
	                  (char *)name_strn, this_ref_id, published_value)) {
		if (json_response) {
			json_decref (json_response);
			ulfius_set_json_body_response (response, MHD_HTTP_BAD_REQUEST, NULL_JSON);
		}
		
		else {
			DEBUG_NOW (REPORT_ERRORS, FE,
			           "could not create json response object when putting cssd");
			ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
			                               NULL_JSON);
			cb_ret_val = U_CALLBACK_ERROR;
		}
		
		json_decref (json_body_request);
		json_decref (id);
		json_decref (cssd);
		json_decref (name);
		json_decref (published);
		#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
		json_decref (ws_slot);
		json_decref (access_token);
		#endif
		goto cb_ret;
	}
	
	dsp_dataset ids_dataset = NULL;
	
	if (!read_job_ids_by_cssd_id ((ds_object_id_field *)id_strn, &ids_dataset)) {
		DEBUG_NOW (REPORT_ERRORS, FE,
		           "could not read job ids  when putting cssd");
	}
	
	else {
		if (ids_dataset) {
			for (unsigned long i = 0; i < 2 * ids_dataset->num_records; i += 2) {
				delete_all_results_by_job_id ((ds_object_id_field *) (ids_dataset->data[i]));
				delete_all_jobs_by_job_id ((ds_object_id_field *) (ids_dataset->data[i]));
				// only notify for job deletion (hits notification implied)
				notify_ref_id (atoi (ids_dataset->data[i + 1]), ids_dataset->data[i],
				               DS_COL_JOBS,
				               DS_NOTIFY_OP_DELETE);
			}
		}
	}
	
	if (ids_dataset) {
		free_dataset (ids_dataset);
	}
	
	// notify all users iff new CSSD status is public, or previously publc
	if (published_value || prev_published_status) {
		// notify all ref_ids only when public CSSD created
		notify_all_ref_ids ((char *)id_strn, DS_COL_CSSD, DS_NOTIFY_OP_UPDATE);
	}
	
	ulfius_set_json_body_response (response, MHD_HTTP_ACCEPTED, NULL_JSON);
	json_decref (id);
	json_decref (cssd);
	json_decref (name);
	json_decref (published);
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	json_decref (ws_slot);
	json_decref (access_token);
	#endif
	cb_ret_val = U_CALLBACK_CONTINUE;
cb_ret:
	FE_LOCK_E
	return cb_ret_val;
}

/*
 *  ulfius_add_endpoint_by_val (&frontend_instance, "DELETE", "/cssd", cssd_URI_vars_format, 0, &callback_delete_cssd, NULL);
 */
int callback_delete_cssd (const struct _u_request *request,
                          struct _u_response *response, void *user_data) {
	int cb_ret_val = U_CALLBACK_COMPLETE;
	FE_LOCK_S
	cb_ret_val = U_CALLBACK_COMPLETE;
	const char *accessToken = u_map_get (request->map_cookie,
	                                     FRONTEND_KEY_ACCESS_TOKEN);
	const char *ws_slot_string = u_map_get (request->map_cookie,
	                                        FRONTEND_KEY_WEBSOCKET_SLOT);
	const char *cssd_id_strn = u_map_get (request->map_url, FRONTEND_KEY_CSSD_ID);
	
	if (!ws_slot_string) {
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	REGISTER
	int this_ref_id = INVALID_REF_ID;
	bool is_curator = false;
	#if FRONTEND_AUTH_ENABLE_ACCESS_PROFILES
	const ds_int32_field ws_slot_int = atoi (ws_slot_string);
	FE_WS_LOCK_S
	this_ref_id = websocket_to_ref_id[ws_slot_int];
	is_curator = ref_id_to_curator_status[ws_slot_int];
	FE_WS_LOCK_E
	
	if (!is_curator ||
	    0 > ws_slot_int ||
	    FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS < ws_slot_int ||
	    INVALID_REF_ID == this_ref_id ||
	    !isValidWebsocketslotAccesstokenPair (accessToken, ws_slot_int)) {
		if (!is_curator) {
			DEBUG_NOW1 (REPORT_ERRORS, FE,
			            "user (reference id %d) attempting to delete cssd without curator status",
			            this_ref_id);
		}
		
		ulfius_set_json_body_response (response, MHD_HTTP_UNAUTHORIZED, NULL_JSON);
		goto cb_ret;
	}
	
	#endif
	json_t *json_response = json_object();
	
	if (!json_response) {
		ulfius_set_json_body_response (response, MHD_HTTP_INTERNAL_SERVER_ERROR,
		                               NULL_JSON);
		cb_ret_val = U_CALLBACK_ERROR;
		goto cb_ret;
	}
	
	if (delete_cssd ((ds_object_id_field *)cssd_id_strn, this_ref_id)) {
		json_object_set_new (json_response, FRONTEND_KEY_STATUS,
		                     json_string (FRONTEND_STATUS_SUCCESS));
		ulfius_set_json_body_response (response, MHD_HTTP_OK, json_response);
		dsp_dataset ids_dataset = NULL;
		
		if (!read_job_ids_by_cssd_id ((ds_object_id_field *)cssd_id_strn,
		                              &ids_dataset)) {
			DEBUG_NOW (REPORT_ERRORS, FE,
			           "could not read job ids when putting cssd");
		}
		
		else {
			if (ids_dataset) {
				for (unsigned long i = 0; i < 2 * ids_dataset->num_records; i += 2) {
					delete_all_results_by_job_id ((ds_object_id_field *) (ids_dataset->data[i]));
					delete_all_jobs_by_job_id ((ds_object_id_field *) (ids_dataset->data[i]));
					// only notify for job deletion (hits notific
					notify_ref_id (atoi (ids_dataset->data[i + 1]), ids_dataset->data[i],
					               DS_COL_JOBS,
					               DS_NOTIFY_OP_DELETE);
				}
			}
		}
		
		if (ids_dataset) {
			free_dataset (ids_dataset);
		}
		
		// notify all ref_ids
		notify_all_ref_ids ((char *)cssd_id_strn, DS_COL_CSSD, DS_NOTIFY_OP_UPDATE);
	}
	
	else {
		/*
		 * presumed not found
		 */
		json_object_set_new (json_response, FRONTEND_KEY_STATUS,
		                     json_string (FRONTEND_STATUS_FAIL));
		ulfius_set_json_body_response (response, MHD_HTTP_NOT_FOUND, json_response);
	}
	
	json_decref (json_response);
cb_ret:
	FE_LOCK_E
	return cb_ret_val;
}

#ifdef _WIN32
BOOL WINAPI frontend_sig_handler (DWORD dwType) {
	switch (dwType) {
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		default:
			DEBUG_NOW (REPORT_INFO, FE,
			           "control signal event received. frontend shutting down...");
			frontend_shutting_down = true;
			break;
	}
	
	return TRUE;
}
#else
void frontend_sig_handler (int signum, siginfo_t *info, void *ptr) {
	DEBUG_NOW (REPORT_INFO, FE,
	           "control signal event received. frontend shutting down...");
	frontend_shutting_down = true;
}
#endif

// adapted from https://codesearch.isocpp.org/actcd19/main/u/ulfius/ulfius_2.5.2-1/example_programs/websocket_example/websocket_server.c
static char *read_file (const char *filename) {
	char *buffer = NULL;
	long length;
	FILE *f;
	
	if (filename != NULL) {
		f = fopen (filename, "rb");
		
		if (f) {
			fseek (f, 0, SEEK_END);
			length = ftell (f);
			fseek (f, 0, SEEK_SET);
			buffer = o_malloc (length + 1);
			
			if (buffer) {
				fread (buffer, 1, length, f);
				buffer[length] = '\0';
			}
			
			fclose (f);
		}
		
		return buffer;
	}
	
	else {
		return NULL;
	}
}

bool initialize_frontend (unsigned short frontend_port, char *backend_server,
                          unsigned short backend_port, char *ds_server, const unsigned short ds_port) {
	if (!initialize_utils()) {
		printf ("failed to initialize utils for frontend\n");
		fflush (stdout);
		return false;
	}

	DEBUG_NOW (REPORT_INFO, FE, "initializing frontend");
	DEBUG_NOW (REPORT_INFO, FE, "initializing spinlocks");
	
	if (pthread_spin_init (&fe_spinlock, PTHREAD_PROCESS_PRIVATE)) {
		DEBUG_NOW (REPORT_ERRORS, FE, "could not initialize spinlocks");
		finalize_utils();
		return false;
	}
	
	if (pthread_spin_init (&fe_ws_spinlock, PTHREAD_PROCESS_PRIVATE)) {
		DEBUG_NOW (REPORT_ERRORS, FE, "could not intialize spinlocks");
		pthread_spin_destroy (&fe_spinlock);
                finalize_utils();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, FE, "checking access to jansson library");
	NULL_JSON = json_object();
	
	if (!NULL_JSON) {
		DEBUG_NOW (REPORT_ERRORS, FE, "cannot access jansson library");
		DEBUG_NOW (REPORT_INFO, FE, "finalizing spinlocks");
		pthread_spin_destroy (&fe_spinlock);
		pthread_spin_destroy (&fe_ws_spinlock);
                finalize_utils();
		return false;
	}
	
	#ifdef DEBUG_ON
	DEBUG_NOW (REPORT_INFO, FE, "initializing debug");
	
	if (!initialize_debug()) {
		DEBUG_NOW (REPORT_ERRORS, FE, "failed to intialize debug");
		DEBUG_NOW (REPORT_INFO, FE, "finalizing spinlocks");
		pthread_spin_destroy (&fe_spinlock);
		pthread_spin_destroy (&fe_ws_spinlock);
		json_decref (NULL_JSON);
		json_decref (NULL_JSON);
                finalize_utils();
		return false;
	}
	
	#endif
	
	#ifdef MULTITHREADED_ON
	
	if (!initialize_list_destruction()) {
		#ifdef DEBUG_ON
		persist_debug();
		finalize_debug();
		#endif
		pthread_spin_destroy (&fe_spinlock);
		pthread_spin_destroy (&fe_ws_spinlock);
		json_decref (NULL_JSON);
                finalize_utils();
		return false;
	}
	
	#endif
	DEBUG_NOW (REPORT_INFO, FE, "initializing filter");
	
	if (!initialize_filter (backend_server, backend_port)) {
		DEBUG_NOW (REPORT_ERRORS, FE, "failed to initialize filter");
		#ifdef DEBUG_ON
		persist_debug();
		finalize_debug();
		#endif
		#ifdef MULTITHREADED_ON
		finalize_list_destruction();
		#endif
		DEBUG_NOW (REPORT_INFO, FE, "finalizing spinlocks");
		pthread_spin_destroy (&fe_spinlock);
		pthread_spin_destroy (&fe_ws_spinlock);
		json_decref (NULL_JSON);
                finalize_utils();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, FE, "initializing datastore");
	
	if (!initialize_datastore (ds_server, ds_port, true)) {
		DEBUG_NOW (REPORT_ERRORS, FE, "failed to intialize datastore");
		DEBUG_NOW (REPORT_INFO, FE, "finalizing filter");
		finalize_filter();
		#ifdef DEBUG_ON
		persist_debug();
		finalize_debug();
		#endif
		#ifdef MULTITHREADED_ON
		finalize_list_destruction();
		#endif
		DEBUG_NOW (REPORT_INFO, FE, "finalizing spinlocks");
		pthread_spin_destroy (&fe_spinlock);
		pthread_spin_destroy (&fe_ws_spinlock);
		json_decref (NULL_JSON);
                finalize_utils();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, FE, "launching datastore dequeue thread");
	
	if (pthread_create (&ds_deq_thread, NULL, ds_deq_thread_start, NULL)) {
		DEBUG_NOW (REPORT_ERRORS, FE, "failed to launch datastore dequeue thread");
		DEBUG_NOW (REPORT_INFO, FE, "finalizing datastore");
		finalize_datastore();
		DEBUG_NOW (REPORT_INFO, FE, "finalizing filter");
		finalize_filter();
		#ifdef DEBUG_ON
		persist_debug();
		finalize_debug();
		#endif
		#ifdef MULTITHREADED_ON
		finalize_list_destruction();
		#endif
		DEBUG_NOW (REPORT_INFO, FE, "finalizing spinlocks");
		pthread_spin_destroy (&fe_spinlock);
		pthread_spin_destroy (&fe_ws_spinlock);
		json_decref (NULL_JSON);
                finalize_utils();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, FE, "registering signal handler");
	#ifdef _WIN32
	
	if (!SetConsoleCtrlHandler ((PHANDLER_ROUTINE)frontend_sig_handler, TRUE)) {
	#else
	static struct sigaction _sigact;
	g_memset (&_sigact, 0, sizeof (_sigact));
	_sigact.sa_sigaction = frontend_sig_handler;
	_sigact.sa_flags = SA_SIGINFO;
	
	if (sigaction (SIGINT, &_sigact, NULL) != 0 ||
	    sigaction (SIGTERM, &_sigact, NULL) != 0 ||
	    sigaction (SIGQUIT, &_sigact, NULL) != 0 ||
	    sigaction (SIGHUP, &_sigact, NULL) != 0) {
	#endif
		DEBUG_NOW (REPORT_ERRORS, FE, "failed to register signal handler");
		frontend_shutting_down = true;
		pthread_join (ds_deq_thread, NULL);
		DEBUG_NOW (REPORT_INFO, FE, "finalizing datastore");
		finalize_datastore();
		DEBUG_NOW (REPORT_INFO, FE, "finalizing filter");
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
		finalize_list_destruction();
		#endif
		DEBUG_NOW (REPORT_INFO, FE, "finalizing spinlocks");
		pthread_spin_destroy (&fe_spinlock);
		pthread_spin_destroy (&fe_ws_spinlock);
		json_decref (NULL_JSON);
                finalize_utils();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, FE, "intializing ulfius");
	
	if (ulfius_init_instance (&frontend_instance, frontend_port, NULL,
	                          NULL) != U_OK) {
		DEBUG_NOW (REPORT_ERRORS, FE, "could not intialize ulfius instance");
		frontend_shutting_down = true;
		pthread_join (ds_deq_thread, NULL);
		DEBUG_NOW (REPORT_INFO, FE, "finalizing datastore");
		finalize_datastore();
		DEBUG_NOW (REPORT_INFO, FE, "finalizing filter");
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
		finalize_list_destruction();
		#endif
		DEBUG_NOW (REPORT_INFO, FE, "finalizing spinlocks");
		pthread_spin_destroy (&fe_spinlock);
		pthread_spin_destroy (&fe_ws_spinlock);
		json_decref (NULL_JSON);
                finalize_utils();
		return false;
	}
	
	DEBUG_NOW (REPORT_INFO, FE, "setting up MIME types");
	u_map_init (&mime_types);
	u_map_put (&mime_types, "html", "text/html");
	u_map_put (&mime_types, "css", "text/css");
	u_map_put (&mime_types, "js", "application/javascript");
	u_map_put (&mime_types, "png", "image/png");
	u_map_put (&mime_types, "jpeg", "image/jpeg");
	u_map_put (&mime_types, "jpg", "image/jpeg");
	u_map_put (&mime_types, "*", "application/octet-stream");
	DEBUG_NOW (REPORT_INFO, FE, "setting up redirection rules");
	u_map_init (&redirects);
	u_map_put (&redirects, "/", "/index.html.gz");
	DEBUG_NOW (REPORT_INFO, FE, "registering URL endpoints");
	// WS
	ulfius_add_endpoint_by_val (&frontend_instance, "GET",
	                            FRONTEND_PREFIX_WEBSOCKET, NULL, 0, &callback_websocket, NULL);
	// sequences
	char generic_URI_vars_format[MAX_URL_LENGTH + 1];
	sprintf (generic_URI_vars_format, "/:%s/:%s/:%s/:%s",
	         FRONTEND_KEY_START_RECORD,
	         FRONTEND_KEY_RECORD_LIMIT,
	         FRONTEND_KEY_RECORD_ORDER,
	         FRONTEND_KEY_RECORD_RESTRICT);
	char results_URI_vars_format[MAX_URL_LENGTH + 1];
	sprintf (results_URI_vars_format, "/:%s/:%s/:%s/:%s/:%s/:%s",
	         FRONTEND_KEY_START_RECORD,
	         FRONTEND_KEY_RECORD_LIMIT,
	         FRONTEND_KEY_RECORD_ORDER_BY,
	         FRONTEND_KEY_RECORD_ORDER,
	         FRONTEND_KEY_RECORD_RESTRICT,
	         FRONTEND_KEY_RECORD_JOB_ID);
	char result_URI_vars_format[MAX_URL_LENGTH + 1];
	sprintf (result_URI_vars_format, "/:%s", FRONTEND_KEY_RECORD_JOB_ID);
	char result_index_URI_vars_format[MAX_URL_LENGTH + 1];
	sprintf (result_index_URI_vars_format, "/:%s/:%s/:%s/:%s/:%s",
                 FRONTEND_KEY_RECORD_ORDER_BY,
                 FRONTEND_KEY_RECORD_ORDER,
                 FRONTEND_KEY_RECORD_JOB_ID,
		 FRONTEND_KEY_HIT_POSITION,
		 FRONTEND_KEY_HIT_FE);
	char job_URI_vars_format[MAX_URL_LENGTH + 1];
	sprintf (job_URI_vars_format, "/:%s", FRONTEND_KEY_RECORD_JOB_ID);
	char tc_URI_vars_format[MAX_URL_LENGTH + 1];
	sprintf (tc_URI_vars_format, "/:%s/:%s", FRONTEND_KEY_TOPIC,
	         FRONTEND_KEY_CAPABILITY);
	char sequence_URI_vars_format[MAX_URL_LENGTH + 1];
	sprintf (sequence_URI_vars_format, "/:%s", FRONTEND_KEY_SEQUENCE_ID);
	char cssd_URI_vars_format[MAX_URL_LENGTH + 1];
	sprintf (cssd_URI_vars_format, "/:%s", FRONTEND_KEY_CSSD_ID);
	uchar  *ud_sequences = malloc (sizeof (uchar)),
	        *ud_cssds = malloc (sizeof (uchar)),
	         *ud_jobs = malloc (sizeof (uchar)), *ud_results = malloc (sizeof (uchar)),
	          *ud_hit_index = malloc (sizeof (uchar));
	*ud_sequences = 0;
	*ud_cssds = 1;
	*ud_jobs = 2;
	*ud_results = 3;
	*ud_hit_index = 4;
	// topic/capabilities check
	ulfius_add_endpoint_by_val (&frontend_instance, "GET", FRONTEND_PREFIX_TC,
	                            tc_URI_vars_format, 0, &callback_get_tc, NULL);
	// sequences retrieval
	ulfius_add_endpoint_by_val (&frontend_instance, "GET",
	                            FRONTEND_PREFIX_SEQUENCES, generic_URI_vars_format, 0, &callback_get_data,
	                            ud_sequences);
	// CSSDs retrieval
	ulfius_add_endpoint_by_val (&frontend_instance, "GET", FRONTEND_PREFIX_CSSDS,
	                            generic_URI_vars_format, 0, &callback_get_data, ud_cssds);
	// multiple/single job retrieval
	ulfius_add_endpoint_by_val (&frontend_instance, "GET", FRONTEND_PREFIX_JOBS,
	                            generic_URI_vars_format, 0, &callback_get_data, ud_jobs);
	ulfius_add_endpoint_by_val (&frontend_instance, "GET", FRONTEND_PREFIX_JOB,
	                            job_URI_vars_format, 0, &callback_get_job, NULL);
	// hit index retrieval
	ulfius_add_endpoint_by_val (&frontend_instance, "GET", FRONTEND_PREFIX_RESULT_INDEX,
                                    result_index_URI_vars_format, 0, &callback_get_data, ud_hit_index);
	// job creation
	ulfius_add_endpoint_by_val (&frontend_instance, "POST", FRONTEND_PREFIX_JOB,
	                            NULL, 0, &callback_post_job, NULL);
	ulfius_add_endpoint_by_val (&frontend_instance, "GET", FRONTEND_PREFIX_RESULTS,
	                            results_URI_vars_format, 0, &callback_get_data, ud_results);
	ulfius_add_endpoint_by_val (&frontend_instance, "GET", FRONTEND_PREFIX_RESULTS,
	                            result_URI_vars_format, 0, &callback_get_result_count, NULL);
	ulfius_add_endpoint_by_val (&frontend_instance, "GET",
	                            FRONTEND_PREFIX_RESULTS_SUMMARY, job_URI_vars_format, 0,
	                            &callback_get_results_summary, NULL);
	ulfius_add_endpoint_by_val (&frontend_instance, "GET", FRONTEND_PREFIX_RESULT,
	                            result_URI_vars_format, 0, &callback_get_result_total_time, NULL);
	// sequence creation
	ulfius_add_endpoint_by_val (&frontend_instance, "POST",
	                            FRONTEND_PREFIX_SEQUENCE, NULL, 0, &callback_post_sequence, NULL);
	// cssd creation and deletion
	ulfius_add_endpoint_by_val (&frontend_instance, "POST", FRONTEND_PREFIX_CSSD,
	                            NULL, 0, &callback_post_cssd, NULL);
	ulfius_add_endpoint_by_val (&frontend_instance, "PUT",  FRONTEND_PREFIX_CSSD,
	                            NULL, 0, &callback_put_cssd, NULL);
	ulfius_add_endpoint_by_val (&frontend_instance, "DELETE", FRONTEND_PREFIX_CSSD,
	                            cssd_URI_vars_format, 0, &callback_delete_cssd, NULL);
	// sequence deletion
	ulfius_add_endpoint_by_val (&frontend_instance, "DELETE",
	                            FRONTEND_PREFIX_SEQUENCE, sequence_URI_vars_format, 0,
	                            &callback_delete_sequence, NULL);
	// job (and corresponding results) deletion
	ulfius_add_endpoint_by_val (&frontend_instance, "DELETE",
	                            FRONTEND_PREFIX_JOB_W_RESULTS, job_URI_vars_format, 0,
	                            &callback_delete_job_w_results, NULL);
	// rest of the world
	ulfius_add_endpoint_by_val (&frontend_instance, "GET", "*", NULL, 1,
	                            &callback_static_file, &mime_types);
	                            
	// initialize keepalive (heartbeat) status for websocket clients
	for (uchar i = 0; i < FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS; i++) {
		websocket_client_has_heartbeat[i] = false;
		websocket_client_active[i] = false;
	}
	
	websocket_num_clients = 0;
	
	/*
	 * set up ref_ids
	 */
	for (uchar i = 0; i < FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS; i++) {
		ref_ids[i] = INVALID_REF_ID;
		ref_id_to_websockets[i] = NULL;
		ref_id_to_curator_status[i] = false;
		websocket_to_ref_id[i] = INVALID_REF_ID;
		websocket_to_access_token[i][0] = '\0';
		websocket_managers[i] = NULL;
	}
	
	num_ref_ids = 0;
	
	for (uchar i = FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS;
	     i < FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS; i++) {
		websocket_managers[i] = NULL;
	}
	
	DEBUG_NOW (REPORT_INFO, FE, "starting frontend framework");
	char *key_pem = read_file (SSL_PRIVKEY_PATH),
	      *cert_pem = read_file (SSL_CERT_PATH);
	      
	if (ulfius_start_secure_framework (&frontend_instance, key_pem,
	                                   cert_pem) == U_OK) {
		DEBUG_NOW1 (REPORT_INFO, FE,
		            "secure frontend launched on port %d",
		            frontend_instance.port);
		REGISTER
		bool cont = true;
		
		while (cont) {
			sleep (FRONTEND_SLEEP_S);
			cont = !frontend_shutting_down;
		}
		
		DEBUG_NOW (REPORT_INFO, FE, "finalizing frontend framework");
		u_map_clean (&redirects);
		u_map_clean (&mime_types);
		ulfius_stop_framework (&frontend_instance);
		ulfius_clean_instance (&frontend_instance);
	}
	
	else {
		u_map_clean (&redirects);
		u_map_clean (&mime_types);
		DEBUG_NOW1 (REPORT_ERRORS, FE, "failed to launch frontend framework on port %d",
		            frontend_instance.port);
	}
	
	if (key_pem) {
		o_free (key_pem);
	}
	
	if (cert_pem) {
		o_free (cert_pem);
	}
	
	free (ud_sequences);
	free (ud_cssds);
	free (ud_jobs);
	free (ud_results);
	free (ud_hit_index);
	DEBUG_NOW (REPORT_INFO, FE, "finalizing datastore");
	finalize_datastore();
	DEBUG_NOW (REPORT_INFO, FE, "finalizing filter");
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
	finalize_list_destruction();
	#endif
	json_decref (NULL_JSON);
	DEBUG_NOW (REPORT_INFO, FE, "finalizing spinlocks");
	pthread_spin_destroy (&fe_spinlock);
	pthread_spin_destroy (&fe_ws_spinlock);
	pthread_join (ds_deq_thread, NULL);
	
	/*
	 * cleanup any pending ref_ids data
	 */
	for (int i = 0; i < FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS; i++) {
		if (NULL != ref_id_to_websockets[i]) {
			if (list_iterator_start (ref_id_to_websockets[i])) {
				while (list_iterator_hasnext (ref_id_to_websockets[i])) {
					free (list_iterator_next (ref_id_to_websockets[i]));
				}
				
				list_iterator_stop (ref_id_to_websockets[i]);
			}
			
			list_destroy (ref_id_to_websockets[i]);
			free (ref_id_to_websockets[i]);
		}
	}

	finalize_utils();
	
	return true;
}
