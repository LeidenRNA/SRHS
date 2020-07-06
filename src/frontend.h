#ifndef RNA_FRONTEND_H
#define RNA_FRONTEND_H

#include <stdbool.h>
#include "util.h"

#define FRONTEND_MIN_PORT                                      	80
// "/proc/sys/net/ipv4/ip_local_port_range" on "Linux node-003 3.2.0-5-amd64 #1 SMP Debian 3.2.96-3 x86_64 GNU/Linux")
#define FRONTEND_MAX_PORT                                       61000

#define FRONTEND_PREFIX_WEBSOCKET                               "/websocket"
// prefix for topic/capability query
#define FRONTEND_PREFIX_TC					"/tc"
#define FRONTEND_PREFIX_SEQUENCES                               "/sequences"
#define FRONTEND_PREFIX_SEQUENCE                                "/sequence"
#define FRONTEND_PREFIX_CSSDS                                   "/cssds"
#define FRONTEND_PREFIX_CSSD                                    "/cssd"
#define FRONTEND_PREFIX_JOBS                                    "/jobs"
#define FRONTEND_PREFIX_JOB                                     "/job"
#define FRONTEND_PREFIX_JOB_W_RESULTS				"/job-w-results"
#define FRONTEND_PREFIX_RESULTS_SUMMARY 			"/results-summary"
#define FRONTEND_PREFIX_RESULTS                                 "/results"
#define FRONTEND_PREFIX_RESULT 					"/result"
#define FRONTEND_PREFIX_RESULT_INDEX 				"/result-index"
#define FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS               200         // limited by uchar
#define FRONTEND_MAX_WEBSOCKET_CLIENT_CONNECTIONS_TIMEOUT_MS    500
#define FRONTEND_MAX_WEBSOCKET_MESSAGE_ATTEMPTS                 10
// grace period to receive next hearbeat from clients; must allow for comms overhead;
// so give round-trip comms some grace period - currently 20s here, 10s at browser end
#define FRONTEND_WEBSOCKET_HEARTBEAT_TIMEOUT_MS                 20000
#define FRONTEND_WEBSOCKET_HEARTBEAT                            '0'
#define FRONTEND_WEBSOCKET_CLOSE_REQUEST                        '1'
#define FRONTEND_WEBSOCKET_CHANGE_NOTIFICATION                  '2'
#define FRONTEND_WEBSOCKET_LIMIT_REACHED                        '3'
#define FRONTEND_WEBSOCKET_ACCESS_TOKEN_REFRESH			'4'
#define FRONTEND_WEBSOCKET_ACCESS_TOKEN_REFRESH_DELIMITER       ' '
#define FRONTEND_STATIC_FOLDER                                  "frontend/static"
#define FRONTEND_STATIC_FILE_EXT_SEPARATOR                      '.'
#define FRONTEND_COMPRESSED_STATIC_FILE_EXT                     "gz"
#define FRONTEND_CONTENT_ENCODING_COMPRESSED                    "gzip"
#define FRONTEND_CONTENT_ENCODING_IDENTITY                      "identity"
// ref id of guest 'template' as used in DB (referring to template CS/sequences)
#define FRONTEND_GUEST_TEMPLATE_REF_ID 				0
// effectively disables authentication/authorization - use with caution
#define FRONTEND_AUTH_ENABLE_ACCESS_PROFILES                    true
#define FRONTEND_AUTH_GUEST_ACCESS_TOKEN_CHAR                   '0'
#define FRONTEND_AUTH_GUEST_ACCESS_TOKEN_STRING                 "00"
#define FRONTEND_AUTH_MAX_ACCESS_TOKEN_LENGTH                   100
#define FRONTEND_AUTH_SERVER_USER_INFO_URL                      "https://rna.eu.auth0.com/userinfo"
#define FRONTEND_AUTH_SERVER_USER_INFO_VERB                     "GET"
#define FRONTEND_AUTH_SERVER_USER_INFO_TIMEOUT_S                10
// the following key indexes into the user info JSON object
// returned by FRONTEND_AUTH_SERVER_USER_INFO_URL;
// when added by RNA user manually at the auth0 user interface,
// this key yields a KEY-VALUE pair found using
#define FRONTEND_AUTH_SERVER_USER_INFO_APP_METADATA_KEY 	"https://rna:eu:auth0:com/app_metadata"
#define FRONTEND_AUTH_SERVER_RESPONSE_STATUS_LINE_OK            "HTTP/2 200"
#define FRONTEND_AUTH_SERVER_RESPONSE_STATUS_LINE_UNAUTHORIZED  "HTTP/2 401"
#define FRONTEND_AUTH_SERVER_RESPONSE_STATUS_LINE_TOO_MANY_REQS "HTTP/2 429"

#define FRONTEND_AUTH_SERVER_RESPONSE_NAME_KEY                  "name"      // http json response object key to get profile's name
#define FRONTEND_AUTH_SERVER_RESPONSE_SUB_KEY                   "sub"       // http json response object key to get profile's sub(scriber) identity
#define FRONTEND_AUTH_SERVER_RESPONSE_ROLE_KEY 			"role" 	    // http json response object key to app_metadata that is appended to auth0 idToken
#define FRONTEND_AUTH_SERVER_RESPONSE_CURATOR_VALUE 		"curator"   // http json response object value in app_metadata to signal "curator" status
#define FRONTEND_DS_NOTIFICATION_SLEEP_MS                       20

#define FRONTEND_KEY_WEBSOCKET_SLOT                             "ws_slot"
#define FRONTEND_KEY_ACCESS_TOKEN                               "token"
#define FRONTEND_KEY_START_RECORD                               "start"
#define FRONTEND_KEY_RECORD_LIMIT                               "limit"
#define FRONTEND_KEY_RECORD_ORDER                               "order"
#define FRONTEND_KEY_RECORD_ORDER_BY                            "order_by"
#define FRONTEND_KEY_RECORD_RESTRICT                            "restrict"
#define FRONTEND_KEY_RECORD_JOB_ID                              "job_id"
#define FRONTEND_KEY_STATUS					"status"
#define FRONTEND_KEY_TOPIC 					"topic"
#define FRONTEND_KEY_CAPABILITY 				"capability"
#define FRONTEND_KEY_SEQUENCE_ID 				"sequence_id"
#define FRONTEND_KEY_CSSD_ID 					"cssd_id"
#define FRONTEND_KEY_JOB_ID					"job_id"
#define FRONTEND_KEY_COUNT					"count"
#define FRONTEND_KEY_TIME					"time"
#define FRONTEND_KEY_HIT_POSITION 				"position"
#define FRONTEND_KEY_HIT_FE 					"fe"
#define FRONTEND_KEY_HIT_INDEX					"index"
#define FRONTEND_STATUS_SUCCESS     				"success"
#define FRONTEND_STATUS_FAIL        				"fail"

#define JOB_POST_NUM_KEYS           				4
#define SEQUENCE_POST_NUM_KEYS      				6
#define CSSD_POST_NUM_KEYS          				5
#define CSSD_PUT_NUM_KEYS					6

#define INVALID_REF_ID              				INT_MIN
#define INVALID_WS_SLOT 					INT_MIN

// second to idle wait for framework termination
#define FRONTEND_SLEEP_S              				1

#define MAX_JSON_RESPONSE_LENGTH                                200
#define MAX_URL_LENGTH                                          200

#define FRONTEND_KEY_ERROR 					"error"

bool initialize_frontend (ushort frontend_port, char *backend_server,
                          ushort backend_port, char *ds_server, ushort ds_port);

#endif
