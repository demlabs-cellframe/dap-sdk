/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2017-2020
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dap_common.h"


#include "dap_enc_key.h"
#include "dap_enc_base64.h"
#include "dap_enc.h"
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_cert.h"
#include "dap_context.h"
#include "dap_timerfd.h"
#include "dap_client_pvt.h"
#include "dap_enc_ks.h"
#include "dap_stream.h"
#include "dap_stream_worker.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_pkt.h"
#include "dap_net.h"
#include "dap_net_transport.h"
#include "dap_stream_handshake.h"

#define LOG_TAG "dap_client_pvt"

#ifndef DAP_ENC_KS_KEY_ID_SIZE
#define DAP_ENC_KS_KEY_ID_SIZE 33
#endif

static int s_max_attempts = 3;
static int s_timeout = 20;
static bool s_debug_more = false;
static time_t s_client_timeout_active_after_connect_seconds = 15;


static void s_stage_status_after(dap_client_pvt_t * a_client_internal);
static int s_json_multy_obj_parse_str(const char *a_key, const char *a_val, int a_count, ...);

// ENC stage callbacks
static void s_enc_init_response(dap_client_t *a_client, const void *a_data, size_t a_data_size);
static void s_enc_init_error(dap_client_t *a_client, void *a_arg, int a_error);

// STREAM_CTL stage callbacks
static void s_stream_ctl_response(dap_client_t *a_client, void *a_data, size_t a_data_size);
static void s_stream_ctl_error(dap_client_t *a_client, void *a_arg, int a_error);
static void s_stage_stream_streaming(dap_client_t *a_client, void *a_arg);

// stream callbacks
static void s_stream_es_callback_connected(dap_events_socket_t * a_es);
static void s_stream_connected(dap_client_pvt_t * a_client_pvt);
static void s_stream_es_callback_delete(dap_events_socket_t * a_es, void *a_arg);
static void s_stream_es_callback_read(dap_events_socket_t * a_es, void *a_arg);
static bool s_stream_es_callback_write(dap_events_socket_t * a_es, void *a_arg);
static void s_stream_es_callback_error(dap_events_socket_t * a_es, int a_error);

// Handshake callback wrapper
static void s_handshake_callback_wrapper(dap_stream_t *a_stream, const void *a_data, size_t a_data_size, int a_error);
// Session create callback wrapper
static void s_session_create_callback_wrapper(dap_stream_t *a_stream, uint32_t a_session_id, const char *a_response_data, size_t a_response_size, int a_error);

// Timer callbacks
static bool s_stream_timer_timeout_check(void * a_arg);
static bool s_stream_timer_timeout_after_connected_check(void * a_arg);

// Helper function to add transport to tried list with automatic array expansion
static int s_add_tried_transport(dap_client_pvt_t *a_client_pvt, dap_net_transport_type_t a_transport_type)
{
    if (!a_client_pvt) {
        return -1;
    }
    
    // Check if already tried
    for (size_t i = 0; i < a_client_pvt->tried_transport_count; i++) {
        if (a_client_pvt->tried_transports[i] == a_transport_type) {
            return 0; // Already tried
        }
    }
    
    // Expand array if needed
    if (a_client_pvt->tried_transport_count >= a_client_pvt->tried_transport_capacity) {
        size_t l_new_capacity = a_client_pvt->tried_transport_capacity * 2;
        if (l_new_capacity < 4) {
            l_new_capacity = 4;
        }
        
        dap_net_transport_type_t *l_new_array = DAP_REALLOC(a_client_pvt->tried_transports,
                                                             sizeof(dap_net_transport_type_t) * l_new_capacity);
        if (!l_new_array) {
            log_it(L_ERROR, "Failed to expand tried_transports array");
            return -1;
        }
        
        a_client_pvt->tried_transports = l_new_array;
        a_client_pvt->tried_transport_capacity = l_new_capacity;
    }
    
    // Add transport to list
    a_client_pvt->tried_transports[a_client_pvt->tried_transport_count++] = a_transport_type;
    return 0;
}

// Helper function to check if transport was already tried
static bool s_is_transport_tried(dap_client_pvt_t *a_client_pvt, dap_net_transport_type_t a_transport_type)
{
    if (!a_client_pvt || !a_client_pvt->tried_transports) {
        return false;
    }
    
    for (size_t i = 0; i < a_client_pvt->tried_transport_count; i++) {
        if (a_client_pvt->tried_transports[i] == a_transport_type) {
            return true;
        }
    }
    
    return false;
}

// Helper function to retry handshake with fallback transport
static int s_retry_handshake_with_fallback(dap_client_pvt_t *a_client_pvt)
{
    if (!a_client_pvt || !a_client_pvt->client) {
        return -1;
    }
    
    // Get all available transports from registry
    dap_list_t *l_all_transports = dap_net_transport_list_all();
    if (!l_all_transports) {
        log_it(L_ERROR, "No transports available in registry");
        return -1;
    }
    
    // Iterate through transports in registry order
    // This automatically includes any new transports added to the registry
    dap_net_transport_type_t l_next_transport = 0;
    bool l_found = false;
    
    for (dap_list_t *l_item = l_all_transports; l_item != NULL; l_item = l_item->next) {
        dap_net_transport_t *l_transport = (dap_net_transport_t *)l_item->data;
        if (!l_transport || !l_transport->ops || !l_transport->ops->handshake_init) {
            continue; // Skip unavailable transports
        }
        
        dap_net_transport_type_t l_transport_type = l_transport->type;
        
        // Check if not already tried
        if (!s_is_transport_tried(a_client_pvt, l_transport_type)) {
            l_next_transport = l_transport_type;
            l_found = true;
            break;
        }
    }
    
    // Free transport list
    dap_list_free_full(l_all_transports, NULL);
    
    if (!l_found) {
        log_it(L_WARNING, "No more untried transports available");
        return -1;
    }
    
    // Add to tried list
    if (s_add_tried_transport(a_client_pvt, l_next_transport) != 0) {
        log_it(L_ERROR, "Failed to add transport to tried list");
        return -1;
    }
    
    log_it(L_INFO, "Retrying handshake with fallback transport: %s (type=%d)",
           dap_net_transport_type_to_str(l_next_transport), l_next_transport);
    
    // Set the client's transport type to the next fallback
    a_client_pvt->client->transport_type = l_next_transport;
    
    // Reset stage to BEGIN to restart the connection process with the new transport
    a_client_pvt->stage = STAGE_BEGIN;
    a_client_pvt->stage_status = STAGE_STATUS_COMPLETE;
    s_stage_status_after(a_client_pvt); // Trigger FSM to restart
    return 0; // Successfully initiated retry
}

static void s_handshake_callback_wrapper(dap_stream_t *a_stream, const void *a_data, size_t a_data_size, int a_error)
{
    if (!a_stream || !a_stream->esocket || !a_stream->esocket->_inheritor) {
        return;
    }
    
    dap_client_t *l_client = (dap_client_t*)a_stream->esocket->_inheritor;
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    if (!l_client_pvt) {
        return;
    }
    
    // Check if this is a temporary stream (for HTTP/WebSocket handshake) or main stream (for UDP/DNS)
    // Temporary stream is not the main stream, main stream is stored in l_client_pvt->stream
    bool l_is_temporary_stream = (a_stream != l_client_pvt->stream);
    
    if (a_error != 0) {
        log_it(L_WARNING, "Handshake failed with error: %d, trying fallback transport", a_error);
        
        // Cleanup temporary stream before error handling
        if (l_is_temporary_stream) {
            log_it(L_DEBUG, "Cleaning up temporary stream for handshake (error case)");
            dap_stream_delete_unsafe(a_stream);
        }
        
        // Try next fallback transport if available
        if (s_retry_handshake_with_fallback(l_client_pvt) == 0) {
            // Successfully retried with fallback, callback will be called again
            return;
        }
        
        // No more fallback transports or retry failed
        log_it(L_ERROR, "All transport attempts failed, giving up");
        l_client_pvt->stage_status = STAGE_STATUS_ERROR;
        if (a_error == ETIMEDOUT) {
            l_client_pvt->last_error = ERROR_NETWORK_CONNECTION_TIMEOUT;
        } else {
            l_client_pvt->last_error = ERROR_NETWORK_CONNECTION_REFUSE;
        }
        s_stage_status_after(l_client_pvt);
        return;
    }
    
    // Process handshake response BEFORE deleting temporary stream
    // This ensures that s_enc_init_response can access stream if needed
    if (a_data && a_data_size > 0) {
        // For HTTP/WebSocket, we need to pass the temporary stream to s_enc_init_response
        // so it can load encryption context into the transport
        // Temporarily store the stream pointer if it's a temporary stream
        dap_stream_t *l_original_stream = l_client_pvt->stream;
        if (l_is_temporary_stream && a_stream) {
            // For temporary stream, use it to access transport for encryption context loading
            // The transport is stored in the temporary stream's stream_transport
            l_client_pvt->stream = a_stream;
        }
        
        s_enc_init_response(l_client, a_data, a_data_size);
        
        // Restore original stream (may be NULL for HTTP/WebSocket during handshake)
        l_client_pvt->stream = l_original_stream;
    } else {
        // For UDP/DNS transports, handshake callback may be called without data
        // Check if transport uses UDP socket type
        dap_net_transport_t *l_transport = l_client_pvt->stream ? l_client_pvt->stream->stream_transport : NULL;
        if (l_transport && l_transport->socket_type == DAP_NET_TRANSPORT_SOCKET_UDP) {
            // UDP/DNS handshake callback called without data - this is normal
            // Handshake is handled by transport protocol, so we just mark stage as done
            log_it(L_DEBUG, "UDP/DNS handshake completed via transport protocol, marking stage as done");
            
            // For UDP/DNS, if we're in STAGE_STREAM_SESSION, we need to create session after handshake
            // (for HTTP/WebSocket, session_create happens in STAGE_STREAM_CTL)
            if (l_client_pvt->stage == STAGE_STREAM_SESSION && l_transport->ops && l_transport->ops->session_create) {
                log_it(L_DEBUG, "UDP/DNS handshake completed, creating session");
                
                // Prepare session parameters
                dap_net_session_params_t l_session_params = {
                    .channels = l_client_pvt->client->active_channels,
                    .enc_type = l_client_pvt->session_key_type,
                    .enc_key_size = l_client_pvt->session_key_block_size,
                    .enc_headers = false,
                    .protocol_version = DAP_CLIENT_PROTOCOL_VERSION
                };
                
                // Call transport session_create
                int l_session_ret = l_transport->ops->session_create(l_client_pvt->stream, &l_session_params, 
                                                                     s_session_create_callback_wrapper);
                
                if (l_session_ret != 0) {
                    log_it(L_ERROR, "Failed to initiate session create via transport for UDP/DNS: %d", l_session_ret);
                    l_client_pvt->stage_status = STAGE_STATUS_ERROR;
                    l_client_pvt->last_error = ERROR_STREAM_ABORTED;
                    s_stage_status_after(l_client_pvt);
                    return;
                }
                
                // Session create is async, callback will handle response
                // Set done callback to advance to next stage when session create completes
                l_client_pvt->stage_status_done_callback = dap_client_pvt_stage_fsm_advance;
                l_client_pvt->stage_status = STAGE_STATUS_IN_PROGRESS;
            } else {
                // For STAGE_ENC_INIT, just mark as done
                l_client_pvt->stage_status = STAGE_STATUS_DONE;
                s_stage_status_after(l_client_pvt);
            }
        } else {
            log_it(L_ERROR, "Handshake completed but no response data for non-UDP transport");
            l_client_pvt->stage_status = STAGE_STATUS_ERROR;
            l_client_pvt->last_error = ERROR_ENC_NO_KEY;
            s_stage_status_after(l_client_pvt);
        }
    }
    
    // Cleanup temporary stream AFTER processing response (for HTTP/WebSocket handshake)
    // For UDP/DNS, handshake uses main stream, so don't delete it
    if (l_is_temporary_stream) {
        log_it(L_DEBUG, "Cleaning up temporary stream for handshake");
        dap_stream_delete_unsafe(a_stream);
    }
}

// Session create callback wrapper
static void s_session_create_callback_wrapper(dap_stream_t *a_stream, uint32_t a_session_id, const char *a_response_data, size_t a_response_size, int a_error)
{
    if (!a_stream || !a_stream->esocket || !a_stream->esocket->_inheritor) {
        return;
    }
    
    dap_client_t *l_client = (dap_client_t*)a_stream->esocket->_inheritor;
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    if (!l_client_pvt) {
        return;
    }
    
    // Check if this is a temporary stream (for TCP-based transports session create) or main stream
    // Temporary stream is not the main stream, main stream is stored in l_client_pvt->stream
    bool l_is_temporary_stream = (a_stream != l_client_pvt->stream);
    
    // Cleanup temporary stream only (for TCP-based transports session create)
    if (l_is_temporary_stream) {
        log_it(L_DEBUG, "Cleaning up temporary stream for session create");
        dap_stream_delete_unsafe(a_stream);
    }
    
    if (a_error != 0) {
        log_it(L_ERROR, "Session create failed with error: %d", a_error);
        l_client_pvt->stage_status = STAGE_STATUS_ERROR;
        if (a_error == ETIMEDOUT) {
            l_client_pvt->last_error = ERROR_NETWORK_CONNECTION_TIMEOUT;
        } else {
            l_client_pvt->last_error = ERROR_STREAM_CTL_ERROR;
        }
        s_stage_status_after(l_client_pvt);
        return;
    }
    
    // Process session create response
    // Transport provides full response data if available (for TCP-based transports),
    // or only session_id (for UDP/DNS transports)
    if (a_session_id != 0) {
        if (a_response_data && a_response_size > 0) {
            // Use full response data provided by transport
            s_stream_ctl_response(l_client, (void*)a_response_data, a_response_size);
            // Free response data (transport allocated it for us)
            DAP_DELETE(a_response_data);
        } else {
            // Transport provided only session_id (UDP/DNS transports)
            // Construct minimal response format: "session_id stream_key"
            // stream_key is required by s_stream_ctl_response parser, but for UDP/DNS
            // we don't have a real stream_key from server response
            // Use a dummy stream_key (empty string) to satisfy parser requirements
            // The actual stream connection for UDP/DNS happens via transport protocol
            char l_response_str[4096];
            snprintf(l_response_str, sizeof(l_response_str), "%u ", a_session_id);
            s_stream_ctl_response(l_client, l_response_str, strlen(l_response_str));
        }
    } else {
        log_it(L_ERROR, "Session create completed but no session_id");
        l_client_pvt->stage_status = STAGE_STATUS_ERROR;
        l_client_pvt->last_error = ERROR_STREAM_CTL_ERROR_RESPONSE_FORMAT;
        s_stage_status_after(l_client_pvt);
    }
}



/**
 * @brief dap_client_internal_init
 * @return
 */
int dap_client_pvt_init()
{
    s_max_attempts = dap_config_get_item_int32_default(g_config, "dap_client", "max_tries", s_max_attempts);
    s_timeout = dap_config_get_item_int32_default(g_config, "dap_client", "timeout", s_timeout);
    s_debug_more = dap_config_get_item_bool_default(g_config, "dap_client", "debug_more", false);
    s_client_timeout_active_after_connect_seconds = (time_t) dap_config_get_item_uint32_default(g_config,
                                                  "dap_client","timeout_active_after_connect", s_client_timeout_active_after_connect_seconds);
    return 0;
}

/**
 * @brief dap_client_internal_deinit
 */
void dap_client_pvt_deinit()
{
}

/**
 * @brief dap_client_internal_new
 * @param a_client_internal
 */
void dap_client_pvt_new(dap_client_pvt_t * a_client_pvt)
{
    a_client_pvt->session_key_type = DAP_ENC_KEY_TYPE_SALSA2012 ;
    a_client_pvt->session_key_open_type = DAP_ENC_KEY_TYPE_KEM_KYBER512;
    a_client_pvt->session_key_block_size = 32;

    a_client_pvt->stage = STAGE_BEGIN; // start point of state machine
    a_client_pvt->stage_status = STAGE_STATUS_COMPLETE;
    a_client_pvt->uplink_protocol_version = DAP_PROTOCOL_VERSION;
}


static void s_client_internal_clean(dap_client_pvt_t *a_client_pvt)
{
    if (a_client_pvt->reconnect_timer) {
        dap_timerfd_delete_unsafe(a_client_pvt->reconnect_timer);
        a_client_pvt->reconnect_timer = NULL;
    }
    // http_client is now managed by transport layer, no need to clean it up here
    if (a_client_pvt->stream_es) {
        dap_stream_delete_unsafe(a_client_pvt->stream);
        a_client_pvt->stream = NULL;
        a_client_pvt->stream_es = NULL;
        a_client_pvt->stream_key = NULL;
        a_client_pvt->stream_id = 0;
    }

    DAP_DEL_Z(a_client_pvt->session_key_id);
    if (a_client_pvt->session_key_open) {
        dap_enc_key_delete(a_client_pvt->session_key_open);
        a_client_pvt->session_key_open = NULL;
    }
    if (a_client_pvt->session_key) {
        dap_enc_key_delete(a_client_pvt->session_key);
        a_client_pvt->session_key = NULL;
    }

    a_client_pvt->is_closed_by_timeout = false;
    a_client_pvt->is_encrypted = false;
    a_client_pvt->is_encrypted_headers = false;
    a_client_pvt->is_close_session = false;
    a_client_pvt->remote_protocol_version = 0;
    a_client_pvt->ts_last_active = 0;

    a_client_pvt->last_error = ERROR_NO_ERROR;
    a_client_pvt->stage = STAGE_BEGIN;
    a_client_pvt->stage_status = STAGE_STATUS_COMPLETE;
    
    // Free tried transports array
    DAP_DEL_Z(a_client_pvt->tried_transports);
    a_client_pvt->tried_transport_count = 0;
    a_client_pvt->tried_transport_capacity = 0;
}

/**
 * @brief dap_client_pvt_delete_unsafe
 * @param a_client_pvt
 */
void dap_client_pvt_delete_unsafe(dap_client_pvt_t * a_client_pvt)
{
    assert(a_client_pvt);
    debug_if(s_debug_more, L_INFO, "dap_client_pvt_delete 0x%p", a_client_pvt);
    s_client_internal_clean(a_client_pvt);
    DAP_DELETE(a_client_pvt);
}

/**
 * @brief s_stream_transport_connect_callback
 * Callback wrapper for transport connect operations (UDP/DNS)
 * Converts transport connect callback signature to client callback
 * For UDP/DNS transports, also initiates handshake after successful connect
 */
static void s_stream_transport_connect_callback(dap_stream_t *a_stream, int a_error_code)
{
    if (!a_stream || !a_stream->esocket) {
        log_it(L_ERROR, "Invalid stream or esocket in transport connect callback");
        return;
    }
    
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_stream->esocket);
    if (!l_client) {
        log_it(L_ERROR, "Invalid client in transport connect callback");
        return;
    }
    
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    if (!l_client_pvt) {
        log_it(L_ERROR, "Invalid client_pvt in transport connect callback");
        return;
    }
    
    if (a_error_code != 0) {
        log_it(L_ERROR, "Transport connect failed with error code: %d", a_error_code);
        l_client_pvt->stage_status = STAGE_STATUS_ERROR;
        l_client_pvt->last_error = ERROR_STREAM_CONNECT;
        s_stage_status_after(l_client_pvt);
        return;
    }
    
    // For UDP/DNS transports, handshake happens after connect
    // Check if this is UDP/DNS transport and handshake is needed
    dap_net_transport_t *l_transport = l_client_pvt->stream->stream_transport;
    if (l_transport && l_transport->socket_type == DAP_NET_TRANSPORT_SOCKET_UDP &&
        l_transport->ops && l_transport->ops->handshake_init) {
        
        // Generate session key if not already generated (should be done in STAGE_ENC_INIT)
        if (!l_client_pvt->session_key_open) {
            l_client_pvt->session_key_open = dap_enc_key_new_generate(
                l_client_pvt->session_key_open_type, NULL, 0, NULL, 0,
                l_client_pvt->session_key_block_size);
            if (!l_client_pvt->session_key_open) {
                log_it(L_ERROR, "Failed to generate session key for UDP/DNS handshake");
                l_client_pvt->stage_status = STAGE_STATUS_ERROR;
                l_client_pvt->last_error = ERROR_OUT_OF_MEMORY;
                s_stage_status_after(l_client_pvt);
                return;
            }
        }
        
        // Prepare handshake parameters
        size_t l_data_size = l_client_pvt->session_key_open->pub_key_data_size;
        uint8_t *l_alice_pub_key = DAP_DUP_SIZE((uint8_t*)l_client_pvt->session_key_open->pub_key_data, l_data_size);
        if (!l_alice_pub_key) {
            log_it(L_ERROR, "Failed to allocate alice public key");
            l_client_pvt->stage_status = STAGE_STATUS_ERROR;
            l_client_pvt->last_error = ERROR_OUT_OF_MEMORY;
            s_stage_status_after(l_client_pvt);
            return;
        }
        
        // Add certificates signatures
        dap_cert_t *l_node_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
        size_t l_sign_count = 0;
        if (l_client_pvt->client->auth_cert)
            l_sign_count += dap_cert_add_sign_to_data(l_client_pvt->client->auth_cert, &l_alice_pub_key, &l_data_size,
                                                       l_client_pvt->session_key_open->pub_key_data,
                                                       l_client_pvt->session_key_open->pub_key_data_size);
        if (l_node_cert)
            l_sign_count += dap_cert_add_sign_to_data(l_node_cert, &l_alice_pub_key, &l_data_size,
                                                      l_client_pvt->session_key_open->pub_key_data,
                                                      l_client_pvt->session_key_open->pub_key_data_size);
        
        dap_net_handshake_params_t l_handshake_params = {
            .enc_type = l_client_pvt->session_key_type,
            .pkey_exchange_type = l_client_pvt->session_key_open_type,
            .pkey_exchange_size = l_client_pvt->session_key_open->pub_key_data_size,
            .block_key_size = l_client_pvt->session_key_block_size,
            .protocol_version = DAP_CLIENT_PROTOCOL_VERSION,
            .auth_cert = l_client_pvt->client->auth_cert,
            .alice_pub_key = l_alice_pub_key,
            .alice_pub_key_size = l_data_size
        };
        
        // Initiate handshake via transport
        log_it(L_INFO, "Initiating UDP/DNS handshake after connect");
        int l_handshake_ret = l_transport->ops->handshake_init(a_stream, &l_handshake_params, 
                                                               s_handshake_callback_wrapper);
        
        if (l_handshake_ret != 0) {
            log_it(L_ERROR, "Failed to initiate UDP/DNS handshake: %d", l_handshake_ret);
            DAP_DELETE(l_alice_pub_key);
            l_client_pvt->stage_status = STAGE_STATUS_ERROR;
            l_client_pvt->last_error = ERROR_STREAM_ABORTED;
            s_stage_status_after(l_client_pvt);
            return;
        }
        
        // Handshake is async, callback will handle response and transition to next stage
        log_it(L_DEBUG, "UDP/DNS handshake initiated, waiting for response");
        return; // Don't call s_stream_connected yet - wait for handshake callback
    }
    
    // For TCP transports (HTTP/WebSocket), connection successful, proceed with normal flow
    log_it(L_INFO, "Transport connect succeeded, calling stream connected callback");
    s_stream_connected(l_client_pvt);
}

/**
 * @brief s_stream_connected
 * @param a_client_pvt
 */
static void s_stream_connected(dap_client_pvt_t * a_client_pvt)
{
    if (!a_client_pvt->client)
        return;

    log_it(L_INFO, "[client:%p] Remote address connected for streaming on (%s:%u) with Socket #%"DAP_FORMAT_SOCKET" (assign on worker #%u)",
                            a_client_pvt->client, a_client_pvt->client->link_info.uplink_addr, a_client_pvt->client->link_info.uplink_port,
                            a_client_pvt->stream_es->socket, a_client_pvt->stream_worker->worker->id);

    a_client_pvt->stage_status = STAGE_STATUS_DONE;

    s_stage_status_after(a_client_pvt);
    dap_events_socket_uuid_t * l_es_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
    if (!l_es_uuid_ptr) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return;
    }
    assert(a_client_pvt->stream_es);

    *l_es_uuid_ptr = a_client_pvt->stream_es->uuid;

    if( dap_timerfd_start_on_worker(a_client_pvt->stream_es->worker,
                                    s_client_timeout_active_after_connect_seconds * 1024,
                                    s_stream_timer_timeout_after_connected_check,
                                    l_es_uuid_ptr) == NULL) {
        log_it(L_ERROR,"Can't run timer for stream after connect check for esocket uuid %"DAP_UINT64_FORMAT_U, *l_es_uuid_ptr);
        DAP_DEL_Z(l_es_uuid_ptr);
    }
}

/**
 * @brief s_stream_timer_timeout_check
 * @param a_arg
 * @return
 */
static bool s_stream_timer_timeout_check(void * a_arg)
{
    assert(a_arg);
    if (!a_arg) {
        log_it(L_ERROR, "Invalid arguments in s_stream_timer_timeout_check");
        return false;
    }
    dap_events_socket_uuid_t *l_es_uuid_ptr = (dap_events_socket_uuid_t*) a_arg;
    dap_worker_t *l_worker = dap_worker_get_current();
    assert(l_worker);
    if (!l_worker) {
        log_it(L_ERROR, "Invalid arguments in s_stream_timer_timeout_check");
        return false;
    }

    dap_events_socket_t *l_es = dap_context_find(l_worker->context, *l_es_uuid_ptr);
    if(l_es){
        if (l_es->flags & DAP_SOCK_CONNECTING ){
            dap_client_t *l_client = DAP_ESOCKET_CLIENT(l_es);
            dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
            log_it(L_WARNING,"Connecting timeout for stream uplink request http://%s:%u/, possible network problems or host is down",
                   l_client->link_info.uplink_addr, l_client->link_info.uplink_port);
            l_client_pvt->is_closed_by_timeout = true;
            log_it(L_INFO, "Close %s sock %"DAP_FORMAT_SOCKET" type %d by timeout", l_es->remote_addr_str, l_es->socket, l_es->type);
            if (l_es->callbacks.error_callback)
                l_es->callbacks.error_callback(l_es, ETIMEDOUT);
            dap_events_socket_remove_and_delete_unsafe(l_es, true);
        } else
            debug_if(s_debug_more, L_DEBUG, "Socket %"DAP_FORMAT_SOCKET" is connected, close check timer", l_es->socket);
    }else
        if(s_debug_more)
            log_it(L_DEBUG,"Esocket %"DAP_UINT64_FORMAT_U" is finished, close check timer", *l_es_uuid_ptr);

    DAP_DELETE(l_es_uuid_ptr);
    return false;
}

/**
 * @brief s_stream_timer_timeout_after_connected_check
 * @param a_arg
 * @return
 */
static bool s_stream_timer_timeout_after_connected_check(void * a_arg)
{
    assert(a_arg);
    dap_events_socket_uuid_t *l_es_uuid_ptr = (dap_events_socket_uuid_t*) a_arg;

    dap_worker_t * l_worker = dap_worker_get_current();
    if (!l_worker) {
        log_it(L_ERROR, "l_worker is NULL");
        return false;
    }
    assert(l_worker);

    dap_events_socket_t * l_es = dap_context_find(l_worker->context, *l_es_uuid_ptr);
    if( l_es ){
        dap_client_t *l_client = DAP_ESOCKET_CLIENT(l_es);
        dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
        if (time(NULL) - l_client_pvt->ts_last_active >= s_client_timeout_active_after_connect_seconds) {
            log_it(L_WARNING, "Activity timeout for streaming uplink http://%s:%u/, possible network problems or host is down",
                                l_client->link_info.uplink_addr, l_client->link_info.uplink_port);
            l_client_pvt->is_closed_by_timeout = true;
            if(l_es->callbacks.error_callback)
                l_es->callbacks.error_callback(l_es, ETIMEDOUT);
            log_it(L_INFO, "Close streaming socket %s (%"DAP_FORMAT_SOCKET") by timeout",
                   l_es->remote_addr_str, l_es->socket);
            dap_events_socket_remove_and_delete_unsafe(l_es, true);
        } else
            if(s_debug_more)
                log_it(L_DEBUG,"Streaming socket %"DAP_FORMAT_SOCKET" is connected, close check timer", l_es->socket);
    } else
        debug_if(s_debug_more, L_DEBUG, "Streaming socket %"DAP_UINT64_FORMAT_U" is finished, close check timer", *l_es_uuid_ptr);

    DAP_DELETE(l_es_uuid_ptr);
    return false;
}

void dap_client_pvt_queue_add(dap_client_pvt_t *a_client_pvt, const char a_ch_id, uint8_t a_type, void *a_data, size_t a_data_size)
{
    dap_client_pkt_queue_elm_t *l_pkt = DAP_NEW_Z_SIZE(dap_client_pkt_queue_elm_t, sizeof(dap_client_pkt_queue_elm_t) + a_data_size);
    l_pkt->ch_id = a_ch_id;
    l_pkt->type = a_type;
    l_pkt->data_size = a_data_size;
    memcpy(l_pkt->data, a_data, a_data_size);
    a_client_pvt->pkt_queue = dap_list_append(a_client_pvt->pkt_queue, l_pkt);
}

int dap_client_pvt_queue_clear(dap_client_pvt_t *a_client_pvt)
{
    if (!a_client_pvt->pkt_queue)
        return -2;
    dap_list_free_full(a_client_pvt->pkt_queue, NULL);
    a_client_pvt->pkt_queue = NULL;
    return 0;
}

static bool s_timer_reconnect_callback(void *a_arg)
{
    assert(a_arg);
    ((dap_client_pvt_t *)a_arg)->reconnect_timer = NULL;
    s_stage_status_after(a_arg);
    return false;
}

/**
 * @brief s_client_internal_stage_status_proc
 * @param a_client
 */

static void s_stage_status_after(dap_client_pvt_t *a_client_pvt)
{
    if (!a_client_pvt)
        return;
    dap_worker_t * l_worker= a_client_pvt->worker;
    assert(l_worker);
    // Only assert _inheritor for streaming stages that need stream worker
    if (a_client_pvt->stage >= STAGE_STREAM_SESSION) {
        assert(l_worker->_inheritor);
    }
    //bool l_is_unref = false;
    dap_client_stage_status_t l_stage_status = a_client_pvt->stage_status;
    dap_client_stage_t l_stage = a_client_pvt->stage;

    switch (l_stage_status) {
        case STAGE_STATUS_IN_PROGRESS: {
            switch (l_stage) {
                case STAGE_BEGIN:
                    s_client_internal_clean(a_client_pvt);
                    a_client_pvt->reconnect_attempts = 0;
                    s_stage_status_after(a_client_pvt);
                    return;

                case STAGE_ENC_INIT: {
                    log_it(L_INFO, "Go to stage ENC: prepare the request");

                    if (!*a_client_pvt->client->link_info.uplink_addr || !a_client_pvt->client->link_info.uplink_port) {
                        log_it(L_ERROR, "Client remote address is empty");
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_WRONG_ADDRESS;
                        break;
                    }

                    if (a_client_pvt->session_key_open)
                        dap_enc_key_delete(a_client_pvt->session_key_open);
                    a_client_pvt->session_key_open = dap_enc_key_new_generate(a_client_pvt->session_key_open_type, NULL, 0, NULL, 0,
                                                                              a_client_pvt->session_key_block_size);
                    if (!a_client_pvt->session_key_open) {
                        log_it(L_ERROR, "Insufficient memory! May be a huge memory leak present");
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_OUT_OF_MEMORY;
                        break;
                    }
                    
                    // Get transport
                    dap_net_transport_type_t l_transport_type = a_client_pvt->client->transport_type;
                    dap_net_transport_t *l_transport = dap_net_transport_find(l_transport_type);
                    if (!l_transport || !l_transport->ops) {
                        log_it(L_ERROR, "Transport type %d not available", l_transport_type);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                        break;
                    }
                    
                    // Check if transport uses UDP socket type (UDP/DNS)
                    // For UDP/DNS transports, handshake happens in STAGE_STREAM_SESSION via main stream
                    // Skip handshake in STAGE_ENC_INIT for UDP/DNS transports
                    if (l_transport->socket_type == DAP_NET_TRANSPORT_SOCKET_UDP) {
                        log_it(L_DEBUG, "UDP/DNS transport detected, skipping handshake in STAGE_ENC_INIT (will happen in STAGE_STREAM_SESSION)");
                        a_client_pvt->stage_status = STAGE_STATUS_DONE;
                        break;
                    }
                    
                    // For HTTP/WebSocket transports, handshake happens before stream creation
                    // Create temporary stream for handshake via transport's stage_prepare
                    if (!l_transport->ops->handshake_init) {
                        log_it(L_ERROR, "Transport type %d doesn't support handshake_init", l_transport_type);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                        break;
                    }
                    
                    static dap_events_socket_callbacks_t s_handshake_callbacks = {
                        .read_callback = NULL,  // Not used for handshake-only stream
                        .write_callback = NULL, // Not used for handshake-only stream
                        .error_callback = NULL, // Not used for handshake-only stream
                        .delete_callback = NULL, // Not used for handshake-only stream
                        .connected_callback = NULL // Not used for handshake-only stream
                    };
                    
                    dap_net_stage_prepare_params_t l_prepare_params = {
                        .host = a_client_pvt->client->link_info.uplink_addr,
                        .port = a_client_pvt->client->link_info.uplink_port,
                        .callbacks = &s_handshake_callbacks, // Required for socket creation, but not used for handshake
                        .client_context = a_client_pvt->client
                    };
                    
                    dap_net_stage_prepare_result_t l_prepare_result;
                    int l_prepare_ret = dap_net_transport_stage_prepare(l_transport_type, &l_prepare_params, &l_prepare_result);
                    
                    if (l_prepare_ret != 0 || !l_prepare_result.esocket) {
                        log_it(L_ERROR, "Stage prepare failed for handshake: transport type %d, error %d", 
                               l_transport_type, l_prepare_result.error_code);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                        break;
                    }
                    
                    // Create temporary stream for handshake
                    dap_stream_t *l_temp_stream = dap_stream_new_es_client(l_prepare_result.esocket, 
                                                                           &a_client_pvt->client->link_info.node_addr,
                                                                           false);
                    if (!l_temp_stream) {
                        log_it(L_CRITICAL, "Failed to create temporary stream for handshake");
                        dap_events_socket_delete_unsafe(l_prepare_result.esocket, true);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_OUT_OF_MEMORY;
                        break;
                    }
                    
                    l_temp_stream->stream_transport = l_transport;
                    
                    // Prepare handshake parameters
                    size_t l_data_size = a_client_pvt->session_key_open->pub_key_data_size;
                    uint8_t *l_alice_pub_key = DAP_DUP_SIZE((uint8_t*)a_client_pvt->session_key_open->pub_key_data, l_data_size);
                    if (!l_alice_pub_key) {
                        dap_stream_delete_unsafe(l_temp_stream);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_OUT_OF_MEMORY;
                        break;
                    }
                    
                    // Add certificates signatures
                    dap_cert_t *l_node_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
                    size_t l_sign_count = 0;
                    if (a_client_pvt->client->auth_cert)
                        l_sign_count += dap_cert_add_sign_to_data(a_client_pvt->client->auth_cert, &l_alice_pub_key, &l_data_size,
                                                                   a_client_pvt->session_key_open->pub_key_data,
                                                                   a_client_pvt->session_key_open->pub_key_data_size);
                    if (l_node_cert)
                        l_sign_count += dap_cert_add_sign_to_data(l_node_cert, &l_alice_pub_key, &l_data_size,
                                                                  a_client_pvt->session_key_open->pub_key_data,
                                                                  a_client_pvt->session_key_open->pub_key_data_size);
                    
                    dap_net_handshake_params_t l_handshake_params = {
                        .enc_type = a_client_pvt->session_key_type,
                        .pkey_exchange_type = a_client_pvt->session_key_open_type,
                        .pkey_exchange_size = a_client_pvt->session_key_open->pub_key_data_size,
                        .block_key_size = a_client_pvt->session_key_block_size,
                        .protocol_version = DAP_CLIENT_PROTOCOL_VERSION,
                        .auth_cert = a_client_pvt->client->auth_cert,
                        .alice_pub_key = l_alice_pub_key,
                        .alice_pub_key_size = l_data_size
                    };
                    
                    // Call transport handshake_init
                    int l_handshake_ret = l_transport->ops->handshake_init(l_temp_stream, &l_handshake_params, 
                                                                          s_handshake_callback_wrapper);
                    
                    // Cleanup alice_pub_key will be done by handshake_init or callback
                    if (l_handshake_ret != 0) {
                        log_it(L_ERROR, "Failed to initiate handshake via transport: %d", l_handshake_ret);
                        dap_stream_delete_unsafe(l_temp_stream);
                        DAP_DELETE(l_alice_pub_key);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                        break;
                    }
                    
                    // Handshake is async, callback will handle response
                    // Set done callback to advance to next stage when handshake completes
                    a_client_pvt->stage_status_done_callback = dap_client_pvt_stage_fsm_advance;
                    a_client_pvt->stage_status = STAGE_STATUS_IN_PROGRESS;
                    s_stage_status_after(a_client_pvt);
                } break;

                case STAGE_STREAM_CTL: {
                    log_it(L_INFO, "Go to stage STREAM_CTL: prepare the request");

                    // Check if transport supports transport-specific handshake (UDP/DNS)
                    // For UDP/DNS transports, STREAM_CTL is not needed - handshake happens via transport protocol
                    dap_net_transport_type_t l_transport_type = a_client_pvt->client->transport_type;
                    dap_net_transport_t *l_transport = dap_net_transport_find(l_transport_type);
                    
                    if (l_transport && l_transport->socket_type == DAP_NET_TRANSPORT_SOCKET_UDP) {
                        // UDP/DNS transports skip HTTP STREAM_CTL stage
                        // Handshake and session creation happen via transport protocol
                        log_it(L_DEBUG, "UDP/DNS transport detected, skipping HTTP STREAM_CTL stage");
                        a_client_pvt->stage_status = STAGE_STATUS_DONE;
                        break;
                    }

                    // For HTTP/WebSocket transports, session creation is performed via transport abstraction
                    // Create temporary stream for session creation to use transport abstraction fully
                    if (!l_transport || !l_transport->ops || !l_transport->ops->session_create) {
                        log_it(L_ERROR, "Transport type %d doesn't support session_create", l_transport_type);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                        break;
                    }

                    // Prepare session parameters
                    dap_net_session_params_t l_session_params = {
                        .channels = a_client_pvt->client->active_channels,
                        .enc_type = a_client_pvt->session_key_type,
                        .enc_key_size = a_client_pvt->session_key_block_size,
                        .enc_headers = false,
                        .protocol_version = DAP_CLIENT_PROTOCOL_VERSION
                    };

                    // For HTTP/WebSocket transports, we need a temporary stream for session creation
                    // Create temporary esocket for session creation
                    static dap_events_socket_callbacks_t s_handshake_callbacks = {
                        .read_callback = NULL,  // Not used for session create-only stream
                        .write_callback = NULL, // Not used for session create-only stream
                        .error_callback = NULL, // Not used for session create-only stream
                        .delete_callback = NULL, // Not used for session create-only stream
                        .connected_callback = NULL // Not used for session create-only stream
                    };

                    dap_net_stage_prepare_params_t l_prepare_params = {
                        .host = a_client_pvt->client->link_info.uplink_addr,
                        .port = a_client_pvt->client->link_info.uplink_port,
                        .callbacks = &s_handshake_callbacks,
                        .client_context = a_client_pvt->client
                    };

                    dap_net_stage_prepare_result_t l_prepare_result;
                    int l_prepare_ret = dap_net_transport_stage_prepare(l_transport_type, &l_prepare_params, &l_prepare_result);
                    
                    if (l_prepare_ret != 0 || !l_prepare_result.esocket) {
                        log_it(L_ERROR, "Stage prepare failed for session create: %d", l_prepare_result.error_code);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                        break;
                    }

                    // Create temporary stream for session creation
                    dap_stream_t *l_temp_stream = dap_stream_new_es_client(l_prepare_result.esocket, 
                                                                           &a_client_pvt->client->link_info.node_addr,
                                                                           false);
                    if (!l_temp_stream) {
                        log_it(L_CRITICAL, "Failed to create temporary stream for session create");
                        dap_events_socket_delete_unsafe(l_prepare_result.esocket, true);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_OUT_OF_MEMORY;
                        break;
                    }

                    // Call transport session_create
                    int l_session_ret = l_transport->ops->session_create(l_temp_stream, &l_session_params, 
                                                                         s_session_create_callback_wrapper);
                    
                    if (l_session_ret != 0) {
                        log_it(L_ERROR, "Failed to initiate session create via transport: %d", l_session_ret);
                        dap_stream_delete_unsafe(l_temp_stream);
                        dap_events_socket_delete_unsafe(l_prepare_result.esocket, true);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                        break;
                    }
                    
                    // Session create is async, callback will handle response
                    // Set done callback to advance to next stage when session create completes
                    a_client_pvt->stage_status_done_callback = dap_client_pvt_stage_fsm_advance;
                    a_client_pvt->stage_status = STAGE_STATUS_IN_PROGRESS;
                } break;

                case STAGE_STREAM_SESSION: {
                    log_it(L_INFO, "Go to stage STREAM_SESSION: process the state ops");

                    // Prepare socket via transport layer (SOLID principle)
                    // Transport layer handles socket type (TCP/UDP) and creation
                    dap_net_transport_type_t l_transport_type = a_client_pvt->client->transport_type;
                    
                    // Setup callbacks
                    static dap_events_socket_callbacks_t l_s_callbacks = {
                        .read_callback = s_stream_es_callback_read,
                        .write_callback = s_stream_es_callback_write,
                        .error_callback = s_stream_es_callback_error,
                        .delete_callback = s_stream_es_callback_delete,
                        .connected_callback = s_stream_es_callback_connected
                    };
                    
                    // Prepare stage parameters
                    dap_net_stage_prepare_params_t l_prepare_params = {
                        .host = a_client_pvt->client->link_info.uplink_addr,
                        .port = a_client_pvt->client->link_info.uplink_port,
                        .callbacks = &l_s_callbacks,
                        .client_context = a_client_pvt->client
                    };
                    
                    dap_net_stage_prepare_result_t l_prepare_result;
                    int l_prepare_ret = dap_net_transport_stage_prepare(l_transport_type, &l_prepare_params, &l_prepare_result);
                    
                    if (l_prepare_ret != 0 || !l_prepare_result.esocket) {
                        log_it(L_ERROR, "Stage prepare failed for transport type %d: %d", l_transport_type, l_prepare_result.error_code);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                        s_stage_status_after(a_client_pvt);
                        break;
                    }
                    
                    dap_events_socket_t *l_es = l_prepare_result.esocket;
                    a_client_pvt->stream_es = l_es;
                    
                    l_es->flags |= DAP_SOCK_CONNECTING;
                #ifndef DAP_EVENTS_CAPS_IOCP
                    l_es->flags |= DAP_SOCK_READY_TO_WRITE;
                #endif

                    a_client_pvt->stream = dap_stream_new_es_client(l_es, &a_client_pvt->client->link_info.node_addr,
                                                                    a_client_pvt->authorized);
                    if (!a_client_pvt->stream) {
                        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                        s_stage_status_after(a_client_pvt);
                        return;
                    }
                    a_client_pvt->stream->session = dap_stream_session_pure_new(); // may be from in packet?

                    // new added, whether it is necessary?
                    a_client_pvt->stream->session->key = a_client_pvt->stream_key;
                    if (l_worker->_inheritor) {
                        a_client_pvt->stream_worker = DAP_STREAM_WORKER(l_worker);
                        a_client_pvt->stream->stream_worker = a_client_pvt->stream_worker;
                    } else {
                        log_it(L_WARNING, "Stream worker not initialized, stream functionality may be limited");
                        a_client_pvt->stream_worker = NULL;
                        a_client_pvt->stream->stream_worker = NULL;
                    }

                    // Initialize transport layer based on client's transport type
                    log_it(L_INFO, "Initializing transport type: %d", l_transport_type);
                    
                    dap_net_transport_t *l_transport = dap_net_transport_find(l_transport_type);
                    if (l_transport) {
                        a_client_pvt->stream->stream_transport = l_transport;
                        log_it(L_INFO, "Stream transport set to %d", l_transport_type);
                    } else {
                        // Fail-fast: configured transport type not available
                        log_it(L_ERROR, "Transport type %d not available, aborting connection", l_transport_type);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                        s_stage_status_after(a_client_pvt);
                        return;
                    }

                    // Check if transport uses connectionless protocol (UDP/DNS)
                    // Use socket_type from transport instead of hardcoding transport type checks
                    bool l_is_udp_transport = (l_transport && 
                                               l_transport->socket_type == DAP_NET_TRANSPORT_SOCKET_UDP);

                    // Connect based on transport socket type
                    if (l_is_udp_transport && l_transport && l_transport->ops && l_transport->ops->connect) {
                        // Use transport-specific connect for UDP/DNS (connectionless)
                        log_it(L_DEBUG, "Using transport connect for UDP/DNS transport type: %d", l_transport_type);
                        
                        // Add socket to worker first
                        dap_worker_add_events_socket(l_worker, l_es);
                        
                        // Use transport connect callback
                        int l_connect_ret = l_transport->ops->connect(
                            a_client_pvt->stream,
                            a_client_pvt->client->link_info.uplink_addr,
                            a_client_pvt->client->link_info.uplink_port,
                            s_stream_transport_connect_callback);
                        
                        if (l_connect_ret != 0) {
                            log_it(L_ERROR, "Transport connect failed for transport type %d: %d", l_transport_type, l_connect_ret);
                            a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                            a_client_pvt->last_error = ERROR_STREAM_CONNECT;
                            s_stage_status_after(a_client_pvt);
                            break;
                        }
                        
                        // UDP/DNS connect callback is called immediately (connectionless)
                        // So we don't need to wait for connection
                    } else {
                        // Use standard TCP connect for HTTP/WebSocket
                        // connect
                    #ifdef DAP_EVENTS_CAPS_IOCP
                        log_it(L_DEBUG, "Stream connecting to remote %s : %u", a_client_pvt->client->link_info.uplink_addr, a_client_pvt->client->link_info.uplink_port);
                        dap_events_socket_uuid_t *l_stream_es_uuid_ptr = DAP_DUP(&a_client_pvt->stream_es->uuid);
                        a_client_pvt->stream_es->flags &= ~DAP_SOCK_READY_TO_READ;
                        a_client_pvt->stream_es->flags |= DAP_SOCK_READY_TO_WRITE;
                        dap_worker_add_events_socket(l_worker, a_client_pvt->stream_es);
                        if (!dap_timerfd_start_on_worker(a_client_pvt->worker,
                                                        (unsigned long)s_client_timeout_active_after_connect_seconds * 1000,
                                                        s_stream_timer_timeout_check, l_stream_es_uuid_ptr)) {
                            log_it(L_ERROR, "Can't run timer on worker %u for es %p : %"DAP_UINT64_FORMAT_U,
                                    a_client_pvt->worker->id, a_client_pvt->stream_es, *l_stream_es_uuid_ptr);
                                DAP_DELETE(l_stream_es_uuid_ptr);
                                a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                                a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                                s_stage_status_after(a_client_pvt);
                                return;
                        }
                    #else

                        int l_err = 0;
                        if((l_err = connect(l_es->socket, (struct sockaddr *) &l_es->addr_storage,
                                sizeof(struct sockaddr_in))) ==0) {
                            log_it(L_INFO, "Connected momentaly with %s:%u", a_client_pvt->client->link_info.uplink_addr, a_client_pvt->client->link_info.uplink_port);
                            // add to dap_worker
                            dap_worker_add_events_socket(l_worker, l_es);

                            // Add check timer
                            dap_events_socket_uuid_t * l_stream_es_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
                            if (!l_stream_es_uuid_ptr) {
                                log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                                a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                                a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                                s_stage_status_after(a_client_pvt);
                                return;
                            }
                            *l_stream_es_uuid_ptr  = l_es->uuid;
                            dap_timerfd_start_on_worker(a_client_pvt->worker, (unsigned long)s_client_timeout_active_after_connect_seconds * 1000,
                                                        s_stream_timer_timeout_check,l_stream_es_uuid_ptr);
                        }
                        else if (l_err != EINPROGRESS && l_err != -1){
                            log_it(L_ERROR, "Remote address can't connect (%s:%hu) with sock_id %"DAP_FORMAT_SOCKET": \"%s\" (code %d)",
                                            a_client_pvt->client->link_info.uplink_addr, a_client_pvt->client->link_info.uplink_port,
                                            l_es->socket, dap_strerror(l_err), l_err);
                            dap_events_socket_delete_unsafe(l_es, true);
                            a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                            a_client_pvt->last_error = ERROR_STREAM_CONNECT;
                        } else {
                            log_it(L_INFO, "Connecting stream to remote %s:%u", a_client_pvt->client->link_info.uplink_addr, a_client_pvt->client->link_info.uplink_port);
                            // add to dap_worker
                            dap_worker_add_events_socket(l_worker, l_es);
                            dap_events_socket_uuid_t * l_stream_es_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
                            if (!l_stream_es_uuid_ptr) {
                                log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                                a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                                a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                                s_stage_status_after(a_client_pvt);
                                return;
                            }
                            *l_stream_es_uuid_ptr = l_es->uuid;
                            dap_timerfd_start_on_worker(a_client_pvt->worker, (unsigned long)s_client_timeout_active_after_connect_seconds * 1000,
                                                        s_stream_timer_timeout_check,l_stream_es_uuid_ptr);
                        }
                    #endif
                    }
                    if (a_client_pvt->stage_status == STAGE_STATUS_ERROR)
                        s_stage_status_after(a_client_pvt);
                } break;

                case STAGE_STREAM_CONNECTED: {
                    log_it(L_INFO, "Go to stage STAGE_STREAM_CONNECTED");
                    if(!a_client_pvt->stream){
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                        s_stage_status_after(a_client_pvt);
                        return;
                    }

                    size_t l_count_channels = dap_strlen(a_client_pvt->client->active_channels);
                    for(size_t i = 0; i < l_count_channels; i++)
                        dap_stream_ch_new(a_client_pvt->stream, (uint8_t)a_client_pvt->client->active_channels[i]);

                    char l_full_path[2048];
                    snprintf(l_full_path, sizeof(l_full_path), "%s/globaldb?session_id=%u", DAP_UPLINK_PATH_STREAM,
                                                dap_client_get_stream_id(a_client_pvt->client));

                    dap_events_socket_write_f_unsafe( a_client_pvt->stream_es, "GET /%s HTTP/1.1\r\n"
                                                                        "Host: %s:%d\r\n"
                                                                        "\r\n",
                                               l_full_path, a_client_pvt->client->link_info.uplink_addr, a_client_pvt->client->link_info.uplink_port);

                    a_client_pvt->stage_status = STAGE_STATUS_DONE;
                    s_stage_status_after(a_client_pvt);
                } break;

                case STAGE_STREAM_STREAMING: {
                    log_it(L_INFO, "Go to stage STAGE_STREAM_STREAMING");
                    a_client_pvt->reconnect_attempts = 0;

                    a_client_pvt->stage_status = STAGE_STATUS_DONE;
                    s_stage_status_after(a_client_pvt);

                } break;

                default: {
                    log_it(L_ERROR, "Undefined proccessing actions for stage status %s",
                            dap_client_stage_status_str(a_client_pvt->stage_status));
                    a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                    s_stage_status_after(a_client_pvt); // be carefull to not to loop!
                }
            }
        } break;

        case STAGE_STATUS_ERROR: {
            // limit the number of attempts
            bool l_is_last_attempt = a_client_pvt->reconnect_attempts >= s_max_attempts ? true : false;
            if (!l_is_last_attempt) {
                if (!a_client_pvt->reconnect_attempts) {
                    log_it(L_ERROR, "Error state(%s), doing callback if present", dap_client_error_str(a_client_pvt->last_error));
                    if (a_client_pvt->client->stage_status_error_callback)
                        a_client_pvt->client->stage_status_error_callback(a_client_pvt->client, (void *)l_is_last_attempt);
                }
                // Trying the step again
                a_client_pvt->stage_status = STAGE_STATUS_IN_PROGRESS;
            } else {
                log_it(L_ERROR, "Disconnect state(%s), doing callback if present", dap_client_error_str(a_client_pvt->last_error));
                if (a_client_pvt->client->stage_status_error_callback)
                    a_client_pvt->client->stage_status_error_callback(a_client_pvt->client, (void *)l_is_last_attempt);
                if (a_client_pvt->client->always_reconnect) {
                    log_it(L_INFO, "Too many attempts, reconnect attempt in %d seconds with %s:%u", s_timeout,
                           a_client_pvt->client->link_info.uplink_addr, a_client_pvt->client->link_info.uplink_port);                    // Trying the step again
                    a_client_pvt->stage_status = STAGE_STATUS_IN_PROGRESS;
                    a_client_pvt->reconnect_attempts = 0;
                } else
                    log_it(L_ERROR, "Connect to %s:%u failed", a_client_pvt->client->link_info.uplink_addr, a_client_pvt->client->link_info.uplink_port);
            }
            ++a_client_pvt->reconnect_attempts;
            if (a_client_pvt->stage_status == STAGE_STATUS_IN_PROGRESS) {
                s_client_internal_clean(a_client_pvt);
                a_client_pvt->stage_status = STAGE_STATUS_IN_PROGRESS;
                a_client_pvt->stage = STAGE_ENC_INIT;
                if (!l_is_last_attempt) {
                    log_it(L_INFO, "Reconnect attempt %d in 0.3 seconds with %s:%u", a_client_pvt->reconnect_attempts,
                           a_client_pvt->client->link_info.uplink_addr, a_client_pvt->client->link_info.uplink_port);
                    // small delay before next request
                    a_client_pvt->reconnect_timer = dap_timerfd_start_on_worker(
                                a_client_pvt->worker, 300, s_timer_reconnect_callback, a_client_pvt);
                    if (!a_client_pvt->reconnect_timer)
                        log_it(L_ERROR ,"Can't run timer for small delay before the next enc_init request");
                } else {
                    // bigger delay before next request
                    a_client_pvt->reconnect_timer = dap_timerfd_start_on_worker(
                                a_client_pvt->worker, s_timeout * 1000, s_timer_reconnect_callback, a_client_pvt);
                    if (!a_client_pvt->reconnect_timer)
                        log_it(L_ERROR,"Can't run timer for bigger delay before the next enc_init request");
                }
            } else {
                // Final error without reconnection - clean up resources
                s_client_internal_clean(a_client_pvt);
            }
        } break;

        case STAGE_STATUS_DONE: {
            log_it(L_INFO, "Stage status %s is done", dap_client_stage_str(a_client_pvt->stage));
            bool l_is_last_stage = (a_client_pvt->stage == a_client_pvt->client->stage_target);
            if (l_is_last_stage) {
                a_client_pvt->stage_status = STAGE_STATUS_COMPLETE;
                dap_stream_add_to_list(a_client_pvt->stream);
                if (a_client_pvt->client->stage_target_done_callback) {
                    log_it(L_NOTICE, "Stage %s is achieved", dap_client_stage_str(a_client_pvt->stage));
                    a_client_pvt->client->stage_target_done_callback(a_client_pvt->client, a_client_pvt->client->callbacks_arg);
                }
                if (a_client_pvt->stage == STAGE_STREAM_STREAMING) {
                    // Send all pkts in queue
                    for (dap_list_t *it = a_client_pvt->pkt_queue; it; it = it->next) {
                        dap_client_pkt_queue_elm_t *l_pkt = it->data;
                        dap_client_write_unsafe(a_client_pvt->client, l_pkt->ch_id, l_pkt->type, l_pkt->data, l_pkt->data_size);
                    }
                    dap_list_free_full(a_client_pvt->pkt_queue, NULL);
                    a_client_pvt->pkt_queue = NULL;
                }
            } else {
                // Go to next stage via callback
                if (!a_client_pvt->stage_status_done_callback) {
                    log_it(L_ERROR, "Stage %s completed but stage_status_done_callback is NULL", 
                           dap_client_stage_str(a_client_pvt->stage));
                    a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                    a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                } else {
                    a_client_pvt->stage_status_done_callback(a_client_pvt->client, NULL);
                }
            }
        } break;

        case STAGE_STATUS_COMPLETE:
            break;

        default:
            log_it(L_ERROR, "Undefined proccessing actions for stage status %s",
                    dap_client_stage_status_str(a_client_pvt->stage_status));
    }

    return;
}


/**
 * @brief dap_client_internal_stage_transaction_begin
 * @param a_client_internal
 * @param a_stage_next
 * @param a_done_callback
 */
void dap_client_pvt_stage_transaction_begin(dap_client_pvt_t * a_client_internal, dap_client_stage_t a_stage_next,
        dap_client_callback_t a_done_callback)
{
    assert(a_client_internal);
    debug_if(s_debug_more, L_DEBUG, "Begin transaction for client %p to the next stage %s",
             a_client_internal->client, dap_client_stage_str(a_stage_next));

    a_client_internal->stage_status_done_callback = a_done_callback;
    a_client_internal->stage = a_stage_next;
    a_client_internal->stage_status = STAGE_STATUS_IN_PROGRESS;
    s_stage_status_after(a_client_internal);
}

/**
 * @brief s_json_str_multy_obj_parse - check a_key and copy a_val to args. Args count should be even.
 * @param a_key - compare key
 * @param a_val - coping value
 * @param a_count - args count
 * @return count of success copies
 */
int s_json_multy_obj_parse_str(const char *a_key, const char *a_val, int a_count, ...)
{
    dap_return_val_if_pass(!a_key || !a_val || a_count % 2, 0);
    int l_ret = 0;
    va_list l_args;
    va_start(l_args, a_count);
    for (int i = 0; i < a_count / 2; ++i) {
        const char *l_key = va_arg(l_args, const char *);
        char **l_pointer = va_arg(l_args, char **);
        if(!strcmp(a_key, l_key)) {
            DAP_DEL_Z(*l_pointer);
            *l_pointer = dap_strdup(a_val);
            l_ret++;
        }
    }
    va_end(l_args);
    return l_ret;
}

/**
 * @brief s_enc_init_response
 * @param a_client
 * @param a_response
 * @param a_response_size
 */
static void s_enc_init_response(dap_client_t *a_client, const void *a_data, size_t a_data_size)
{
// sanity check
    dap_client_pvt_t * l_client_pvt = DAP_CLIENT_PVT(a_client);
    dap_return_if_pass(!l_client_pvt || !a_data);
// func work
    char *l_data = (char*)a_data, *l_session_id_b64 = NULL,
         *l_bob_message_b64 = NULL, *l_node_sign_b64 = NULL, *l_bob_message = NULL;
    l_client_pvt->last_error = ERROR_NO_ERROR;
    while(l_client_pvt->last_error == ERROR_NO_ERROR) {
        // first checks
        if (!l_client_pvt->session_key_open){
            log_it(L_ERROR, "m_enc_init_response: session is NULL!");
            l_client_pvt->last_error = ERROR_ENC_SESSION_CLOSED ;
            break;
        }
        if (a_data_size <= 10) {
            log_it(L_ERROR, "ENC: Wrong response (size %zu data '%s')", a_data_size, l_data);
            l_client_pvt->last_error = ERROR_ENC_NO_KEY;
            break;
        }
        size_t l_bob_message_size = 0;
        int l_json_parse_count = 0;
        // parse data
        struct json_object *jobj = json_tokener_parse(l_data);
        if(jobj) {
            // parse encrypt_id & encrypt_msg
            json_object_object_foreach(jobj, key, val)
            {
                if(json_object_get_type(val) == json_type_string) {
                    const char *l_str = json_object_get_string(val);
                    l_json_parse_count += s_json_multy_obj_parse_str( key, l_str, 6,
                                            "encrypt_id", &l_session_id_b64, 
                                            "encrypt_msg",  &l_bob_message_b64,
                                            "node_sign", &l_node_sign_b64);
                }
                if(json_object_get_type(val) == json_type_int) {
                    int val_int = json_object_get_int(val);
                    if(!strcmp(key, "dap_protocol_version")) {
                        l_client_pvt->remote_protocol_version = val_int;
                        l_json_parse_count++;
                    }
                }
            }
            // free jobj
            json_object_put(jobj);
            if(!l_client_pvt->remote_protocol_version)
                l_client_pvt->remote_protocol_version = DAP_PROTOCOL_VERSION_DEFAULT;
        }
        // check data
        if (l_json_parse_count < 2 || l_json_parse_count > 4) {
            l_client_pvt->last_error = ERROR_ENC_NO_KEY;
            log_it(L_ERROR, "ENC: Wrong response (size %zu data '%s')", a_data_size, l_data);
            break;
        }
        if (!l_session_id_b64 || !l_bob_message_b64) {
            l_client_pvt->last_error = ERROR_ENC_NO_KEY;
            log_it(L_WARNING,"ENC: no %s session id in base64", !l_session_id_b64 ? "session" : "bob message");
            break;
        }
        // decode session key id
        size_t l_len = strlen(l_session_id_b64), l_decoded_len;
        l_client_pvt->session_key_id = DAP_NEW_Z_SIZE_RET_IF_FAIL(char, DAP_ENC_BASE64_DECODE_SIZE(l_len) + 1, l_session_id_b64, l_bob_message_b64, l_node_sign_b64);
        l_decoded_len = dap_enc_base64_decode(l_session_id_b64, l_len, l_client_pvt->session_key_id, DAP_ENC_DATA_TYPE_B64);
        log_it(L_DEBUG, "ENC: session Key ID %s", l_client_pvt->session_key_id);
        // decode bob message
        l_len = strlen(l_bob_message_b64);
        l_bob_message = DAP_NEW_Z_SIZE_RET_IF_FAIL(char, DAP_ENC_BASE64_DECODE_SIZE(l_len) + 1, l_session_id_b64, l_bob_message_b64, l_node_sign_b64, l_client_pvt->session_key_id);
        l_bob_message_size = dap_enc_base64_decode(l_bob_message_b64, l_len, l_bob_message, DAP_ENC_DATA_TYPE_B64);
        if (!l_bob_message_size) {
            log_it(L_WARNING, "ENC: Can't decode bob message from base64");
            l_client_pvt->last_error = ERROR_ENC_WRONG_KEY;
            break;
        }
        // gen alice shared key
        if (!l_client_pvt->session_key_open->gen_alice_shared_key(
                l_client_pvt->session_key_open, l_client_pvt->session_key_open->priv_key_data,
                l_bob_message_size, (unsigned char*) l_bob_message)) {
            log_it(L_WARNING, "ENC: Can't generate private key from bob message");
            l_client_pvt->last_error = ERROR_ENC_WRONG_KEY;
            break;
        }
        // generate session key
        l_client_pvt->session_key = dap_enc_key_new_generate(l_client_pvt->session_key_type,
                l_client_pvt->session_key_open->priv_key_data, // shared key
                l_client_pvt->session_key_open->priv_key_data_size,
                l_client_pvt->session_key_id, l_decoded_len, l_client_pvt->session_key_block_size);

        if (l_client_pvt->stage != STAGE_ENC_INIT) { // We are in wrong stage
            l_client_pvt->last_error = ERROR_WRONG_STAGE;
            log_it(L_WARNING, "ENC: initialized encryption but current stage is %s (%s)",
                    dap_client_get_stage_str(a_client), dap_client_get_stage_status_str(a_client));
            break;
        }
        // verify node sign
        if (l_node_sign_b64) {
            l_len = strlen(l_node_sign_b64);
            dap_sign_t *l_sign = DAP_NEW_Z_SIZE_RET_IF_FAIL(dap_sign_t, DAP_ENC_BASE64_DECODE_SIZE(l_len) + 1,
                l_session_id_b64, l_bob_message_b64, l_node_sign_b64, l_bob_message, l_client_pvt->session_key_id);
            l_decoded_len = dap_enc_base64_decode(l_node_sign_b64, l_len, l_sign, DAP_ENC_DATA_TYPE_B64);
            if ( !dap_sign_verify_all(l_sign, l_decoded_len, l_bob_message, l_bob_message_size) ) {
                dap_stream_node_addr_t l_sign_node_addr = dap_stream_node_addr_from_sign(l_sign);
                if (l_sign_node_addr.uint64 != a_client->link_info.node_addr.uint64) {
                    log_it(L_WARNING, "Unverified stream to node "NODE_ADDR_FP_STR" signed by "NODE_ADDR_FP_STR"\n", NODE_ADDR_FP_ARGS_S(a_client->link_info.node_addr), NODE_ADDR_FP_ARGS_S(l_sign_node_addr));
                    l_client_pvt->authorized = false;
                } else {
                    log_it(L_INFO, "Verified stream sign from node "NODE_ADDR_FP_STR"\n", NODE_ADDR_FP_ARGS_S(l_sign_node_addr));
                    l_client_pvt->authorized = true;
                }
            } else {
                log_it(L_WARNING, "ENC: Invalid node sign");
                l_client_pvt->authorized = false;
            }
            DAP_DELETE(l_sign);
        } else {
            log_it(L_INFO, "Unverified stream to node "NODE_ADDR_FP_STR"\n", NODE_ADDR_FP_ARGS_S(a_client->link_info.node_addr));
            l_client_pvt->authorized = false;
        }
        break;
    }

    DAP_DEL_MULTY(l_session_id_b64, l_bob_message_b64, l_node_sign_b64, l_bob_message);
    if (l_client_pvt->last_error == ERROR_NO_ERROR) {
        l_client_pvt->stage_status = STAGE_STATUS_DONE;
        
        // Load encryption context into transport after successful handshake
        // Use the transport from the temporary stream if available (for HTTP/WebSocket)
        // or from the main stream (for UDP/DNS)
        dap_net_transport_t *l_transport = NULL;
        if (l_client_pvt->stream && l_client_pvt->stream->stream_transport) {
            l_transport = l_client_pvt->stream->stream_transport;
        } else {
            // Fallback: find transport by client's transport type
            l_transport = dap_net_transport_find(l_client_pvt->client->transport_type);
        }
        
        if (l_transport) {
            l_transport->session_key = l_client_pvt->session_key;
            if (l_client_pvt->session_key_id) {
                l_transport->session_key_id = dap_strdup(l_client_pvt->session_key_id);
            }
            l_transport->uplink_protocol_version = l_client_pvt->uplink_protocol_version;
            l_transport->remote_protocol_version = l_client_pvt->remote_protocol_version;
            l_transport->is_close_session = l_client_pvt->is_close_session;
        }
    } else {
        DAP_DEL_Z(l_client_pvt->session_key_id);
        l_client_pvt->stage_status = STAGE_STATUS_ERROR;
    }
    dap_enc_key_delete(l_client_pvt->session_key_open);
    l_client_pvt->session_key_open = NULL;
    s_stage_status_after(l_client_pvt);
}

/**
 * @brief s_enc_init_error
 * @param a_client
 * @param a_err_code
 */
static void s_enc_init_error(dap_client_t * a_client, UNUSED_ARG void *a_arg, int a_err_code)
{
    dap_client_pvt_t * l_client_pvt = DAP_CLIENT_PVT(a_client);
    log_it(L_ERROR, "ENC: Can't init encryption session, err code %d", a_err_code);
    if (!l_client_pvt) return;
    if (a_err_code == ETIMEDOUT) {
        l_client_pvt->last_error = ERROR_NETWORK_CONNECTION_TIMEOUT;
    } else {
        l_client_pvt->last_error = ERROR_NETWORK_CONNECTION_REFUSE;
    }
    l_client_pvt->stage_status = STAGE_STATUS_ERROR;
    s_stage_status_after(l_client_pvt);
}

/**
 * @brief s_stream_ctl_response
 * @param a_client
 * @param a_data
 * @param a_data_size
 */
static void s_stream_ctl_response(dap_client_t * a_client, void *a_data, size_t a_data_size)
{
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(a_client);
    if (!l_client_pvt) return;
    if(s_debug_more)
        log_it(L_DEBUG, "STREAM_CTL response %zu bytes length recieved", a_data_size);
    char *l_response_str = (char*)a_data; // The caller must ensure it's a null-terminated string
    
    if(a_data_size < 4) {
        log_it(L_ERROR, "STREAM_CTL Wrong reply: '%s'", l_response_str);
        l_client_pvt->last_error = ERROR_STREAM_CTL_ERROR_RESPONSE_FORMAT;
        l_client_pvt->stage_status = STAGE_STATUS_ERROR;
        s_stage_status_after(l_client_pvt);
    } else if ( !strncmp(l_response_str, "ERROR", a_data_size) ) {
        log_it(L_WARNING, "STREAM_CTL Got ERROR from the remote site,expecting thats ERROR_AUTH");
        l_client_pvt->last_error = ERROR_STREAM_CTL_ERROR_AUTH;
        l_client_pvt->stage_status = STAGE_STATUS_ERROR;
        s_stage_status_after(l_client_pvt);
    } else {
        int l_arg_count;
        char l_stream_key[4096 + 1] = { '\0' };
        uint32_t l_remote_protocol_version;
        dap_enc_key_type_t l_enc_type = l_client_pvt->session_key_type;
        int l_enc_headers = 0;
        uint32_t l_stream_id_int = 0;

        l_arg_count = sscanf(l_response_str, "%u %4096s %u %d %d",
                                             &l_stream_id_int, l_stream_key,
                                             &l_remote_protocol_version,
                                             &l_enc_type, &l_enc_headers);
        if(l_arg_count < 2) {
            log_it(L_WARNING, "STREAM_CTL Need at least 2 arguments in reply (got %d)", l_arg_count);
            l_client_pvt->last_error = ERROR_STREAM_CTL_ERROR_RESPONSE_FORMAT;
            l_client_pvt->stage_status = STAGE_STATUS_ERROR;
            s_stage_status_after(l_client_pvt);
        } else {
            if(l_arg_count > 2) {
                l_client_pvt->uplink_protocol_version = l_remote_protocol_version;
                log_it(L_DEBUG, "Uplink protocol version %u", l_remote_protocol_version);
            } else
                log_it(L_WARNING, "No uplink protocol version, use default version %d",
                                  l_client_pvt->uplink_protocol_version = DAP_PROTOCOL_VERSION_DEFAULT);
            if(l_stream_id_int) {
                //log_it(L_DEBUG, "Stream server id %s, stream key length(base64 encoded) %u"
                //       ,l_stream_id,strlen(l_stream_key) );
                log_it(L_DEBUG, "Stream server id %u", l_stream_id_int);

                // Delete old key if present
                if(l_client_pvt->stream_key)
                    dap_enc_key_delete(l_client_pvt->stream_key);

                l_client_pvt->stream_id = l_stream_id_int;
                l_client_pvt->stream_key =
                    dap_enc_key_new_generate(l_enc_type, l_stream_key, strlen(l_stream_key), NULL, 0, 32);

                l_client_pvt->is_encrypted_headers = l_enc_headers;

                if(l_client_pvt->stage == STAGE_STREAM_CTL) { // We are on the right stage
                    l_client_pvt->stage_status = STAGE_STATUS_DONE;
                    s_stage_status_after(l_client_pvt);
                } else {
                    log_it(L_WARNING, "Expected to be stage STREAM_CTL but current stage is %s (%s)",
                            dap_client_get_stage_str(a_client), dap_client_get_stage_status_str(a_client));

                }
            } else {
                log_it(L_WARNING, "Wrong stream id response");
                l_client_pvt->last_error = ERROR_STREAM_CTL_ERROR_RESPONSE_FORMAT;
                l_client_pvt->stage_status = STAGE_STATUS_ERROR;
                s_stage_status_after(l_client_pvt);
            }
        }
    }
}

/**
 * @brief s_stream_ctl_error
 * @param a_client
 * @param a_error
 */
static void s_stream_ctl_error(dap_client_t * a_client, UNUSED_ARG void *a_arg, int a_error)
{
    log_it(L_WARNING, "STREAM_CTL error %d", a_error);

    dap_client_pvt_t * l_client_pvt = DAP_CLIENT_PVT(a_client);
    assert(l_client_pvt);

    if (a_error == ETIMEDOUT) {
        l_client_pvt->last_error = ERROR_NETWORK_CONNECTION_TIMEOUT;
    } else {
        l_client_pvt->last_error = ERROR_STREAM_CTL_ERROR;
    }
    l_client_pvt->stage_status = STAGE_STATUS_ERROR;

    s_stage_status_after(l_client_pvt);

}

/**
 * @brief s_stage_stream_opened
 * @param a_client
 * @param arg
 */
static void s_stage_stream_streaming(UNUSED_ARG dap_client_t *a_client, UNUSED_ARG void *a_arg)
{
    log_it(L_INFO, "Stream  is opened");
}

/**
 * @brief s_stream_es_callback_new
 * @param a_es
 * @param arg
 */
static void s_stream_es_callback_connected(dap_events_socket_t * a_es)
{
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    if (!l_client_pvt) {
        log_it(L_ERROR, "Invalid client!");
        return;
    }
    s_stream_connected(l_client_pvt);
}

/**
 * @brief s_es_stream_delete
 * @param a_es
 * @param arg
 */
static void s_stream_es_callback_delete(dap_events_socket_t *a_es, UNUSED_ARG void *a_arg)
{
    log_it(L_INFO, "Stream events socket delete callback");
    if (a_es == NULL) {
        log_it(L_ERROR,"Esocket is NULL for s_stream_es_callback_delete");
        return;
    }
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    if (!l_client)
        return;
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    l_client_pvt->stage_status = STAGE_STATUS_ERROR;
    l_client_pvt->last_error = ERROR_STREAM_ABORTED;
    l_client_pvt->stream->esocket = NULL; // Prevent to delete twice
    s_stage_status_after(l_client_pvt);
    a_es->_inheritor = NULL; // To prevent delete in reactor
}

/**
 * @brief s_es_stream_read
 * @param a_es
 * @param arg
 */
static void s_stream_es_callback_read(dap_events_socket_t * a_es, void * arg)
{
    (void) arg;
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);

    l_client_pvt->ts_last_active = time(NULL);
    switch (l_client_pvt->stage) {
        case STAGE_STREAM_SESSION:
            dap_client_go_stage(l_client_pvt->client, STAGE_STREAM_STREAMING, s_stage_stream_streaming);
        break;
        case STAGE_STREAM_CONNECTED: { // Collect HTTP headers before streaming
            if(a_es->buf_in_size > 1) {
                char * l_pos_endl;
                l_pos_endl = (char*) memchr(a_es->buf_in, '\r', a_es->buf_in_size - 1);
                if(l_pos_endl) {
                    if(*(l_pos_endl + 1) == '\n') {
                        dap_events_socket_shrink_buf_in(a_es, l_pos_endl - (char*)a_es->buf_in);
                        log_it(L_DEBUG, "Header passed, go to streaming (%zu bytes already are in input buffer",
                                a_es->buf_in_size);

                        l_client_pvt->stage = STAGE_STREAM_STREAMING;
                        l_client_pvt->stage_status = STAGE_STATUS_DONE;
                        s_stage_status_after(l_client_pvt);

                        size_t l_bytes_read = dap_stream_data_proc_read(l_client_pvt->stream);
                        dap_events_socket_shrink_buf_in(a_es, l_bytes_read);
                    }
                }
            }
        }
            break;
        case STAGE_STREAM_STREAMING: { // if streaming - process data with stream processor
            size_t l_bytes_read = dap_stream_data_proc_read(l_client_pvt->stream);
            dap_events_socket_shrink_buf_in(a_es, l_bytes_read);
        }
            break;
        default: {
        }
    }
}

/**
 * @brief s_es_stream_write
 * @param a_es
 * @param arg
 */
static bool s_stream_es_callback_write(dap_events_socket_t * a_es, UNUSED_ARG void *a_arg)
{
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    bool l_ret = false;
    if (l_client_pvt->stage_status == STAGE_STATUS_ERROR || !l_client_pvt->stream)
        return false;
    switch (l_client_pvt->stage) {
        case STAGE_STREAM_STREAMING: {
            //  log_it(DEBUG,"Process channels data output (%u channels)",STREAM(sh)->channel_count);
            for (size_t i = 0; i < l_client_pvt->stream->channel_count; i++) {
                dap_stream_ch_t *ch = l_client_pvt->stream->channel[i];
                if (ch->ready_to_write && ch->proc->packet_out_callback)
                    l_ret |= ch->proc->packet_out_callback(ch, NULL);
            }
        } break;
        default: {}
    }
    return l_ret;
}

/**
 * @brief s_stream_es_callback_error
 * @param a_es
 * @param a_error
 */
static void s_stream_es_callback_error(dap_events_socket_t * a_es, int a_error)
{
    if ( !a_es || !a_es->_inheritor )
        return log_it(L_ERROR, "Stream error on undefined client. How on earth is that possible?");

    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    log_it(L_WARNING, "STREAM error %d: \"%s\"", a_error, dap_strerror(a_error));
#ifdef DAP_OS_WINDOWS
    if (a_error == WSAETIMEDOUT || a_error == ERROR_SEM_TIMEOUT)
        a_error = ETIMEDOUT;
#endif
    l_client_pvt->last_error = a_error == ETIMEDOUT
        ? ERROR_NETWORK_CONNECTION_TIMEOUT : ERROR_STREAM_RESPONSE_WRONG;
    l_client_pvt->stage_status = STAGE_STATUS_ERROR;
    l_client_pvt->stream->esocket = NULL;   // Prevent to delete twice
    s_stage_status_after(l_client_pvt);
    a_es->_inheritor = NULL;                // To prevent delete in reactor
}
