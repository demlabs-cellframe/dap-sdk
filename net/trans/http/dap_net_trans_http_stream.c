/*
 * Authors:
 * Roman Khlopkov <roman.khlopkov@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2025
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>


#include "dap_common.h"
#include "dap_config.h"
#include "dap_strfuncs.h"
#include "dap_stream.h"
#include "dap_net_trans.h"
#include "dap_net_trans_http_stream.h"
#include "dap_net_trans_http_server.h"
#include "dap_stream_handshake.h"
#include "dap_enc_base64.h"
#include "dap_enc_ks.h"
#include "dap_enc_http.h"
#include "json.h"
#include "dap_events_socket.h"
#include "dap_net.h"
#include "dap_client_pvt.h"
#include "dap_cert.h"
#include "dap_worker.h"

#define LOG_TAG "dap_stream_trans_http"

// ============================================================================
// Global State
// ============================================================================

static dap_stream_trans_http_config_t s_config = {
    .url_path = "/stream",
    .enc_url_path = "/enc",
    .timeout_ms = 20000,
    .keepalive_ms = 60000,
    .enable_compression = false,
    .enable_tls = false
};

// Debug flag for verbose logging (loaded from config)
static bool s_debug_more = false;

// Forward declarations
static const dap_net_trans_ops_t s_http_trans_ops;
static int s_http_stage_prepare(dap_net_trans_t *a_trans,
                                const dap_net_stage_prepare_params_t *a_params,
                                dap_net_stage_prepare_result_t *a_result);

// Handshake callback wrappers
static void s_http_handshake_response_wrapper(dap_client_t *a_client, void *a_data, size_t a_data_size);
static void s_http_handshake_error_wrapper(dap_client_t *a_client, void *a_arg, int a_error);

// Session callback wrappers (forward declarations)
static void s_http_session_response_wrapper(dap_client_t *a_client, void *a_data, size_t a_data_size);
static void s_http_session_error_wrapper(dap_client_t *a_client, void *a_arg, int a_error);

// Ctx for HTTP requests (to avoid race conditions in client_pvt)
typedef struct {
    dap_client_pvt_t *client_pvt;
    dap_client_callback_data_size_t callback;
    dap_client_callback_int_t error_callback;
    void *callback_arg; // Ctx for the callback
    bool is_encrypted;
} s_http_trans_request_ctx_t;

// HTTP request callbacks (forward declarations)
static void s_http_request_error(int a_err_code, void * a_obj);
static void s_http_request_response(void * a_response, size_t a_response_size, void * a_obj, http_status_code_t a_http_code);
static void s_http_request_enc(dap_client_pvt_t * a_client_internal, dap_net_trans_t *a_trans, const char *a_path,
                        const char *a_sub_url, const char * a_query, void *a_request, size_t a_request_size,
                        dap_client_callback_data_size_t a_response_proc, dap_client_callback_int_t a_response_error, void *a_callbacks_arg);
static int s_http_request(dap_client_pvt_t * a_client_internal, dap_net_trans_t *a_trans, const char * a_path, void * a_request,
        size_t a_request_size, dap_client_callback_data_size_t a_response_proc,
        dap_client_callback_int_t a_response_error);
static void s_http_request_error_unencrypted(int a_err_code, void * a_obj);
static void s_http_request_response_unencrypted(void * a_response, size_t a_response_size, void * a_obj, http_status_code_t a_http_code);

// Ctx for handshake callbacks
typedef struct {
    dap_stream_t *stream;
    dap_net_trans_handshake_cb_t callback;
    dap_client_t *client;  // Store client to verify ctx matches
    void *old_callback_arg;  // Store old callback_arg to restore after use
} s_http_handshake_ctx_t;

// static s_http_handshake_ctx_t s_http_handshake_ctx = {NULL, NULL}; // REMOVED GLOBAL CTX

// Ctx for session create callbacks (per-request, allocated dynamically)
typedef struct {
    dap_stream_t *stream;
    dap_net_trans_session_cb_t callback;
    dap_client_t *client;  // Store client to verify ctx matches
    void *old_callback_arg;  // Store old callback_arg to restore after use
} s_http_session_ctx_t;

// Global ctx for backward compatibility (will be replaced with per-request ctxs)
static s_http_session_ctx_t s_http_session_ctx = {NULL, NULL, NULL, NULL};

// Static HTTP trans instance (initialized once)
static dap_net_trans_t *s_http_trans = NULL;

/**
 * @brief Handshake error callback wrapper
 */
static void s_http_handshake_error_wrapper(dap_client_t *a_client, void *a_arg, int a_error)
{
    if (!a_client) {
        return;
    }
    
    // Get per-request ctx from callback_arg
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(a_client);
    if (!l_client_pvt || !l_client_pvt->callback_arg) {
        log_it(L_WARNING, "s_http_handshake_error_wrapper: no ctx in callback_arg");
        return;
    }
    
    s_http_handshake_ctx_t *l_ctx = (s_http_handshake_ctx_t *)l_client_pvt->callback_arg;
    
    // Verify that the ctx matches this client
    if (l_ctx->client != a_client || !l_ctx->stream) {
        log_it(L_WARNING, "s_http_handshake_error_wrapper: ctx invalid or mismatch");
        return;
    }
    
    // Call trans callback with error
    if (l_ctx->callback) {
        l_ctx->callback(l_ctx->stream, NULL, 0, a_error);
    }
    
    // Free ctx and restore old callback_arg
    void *l_old_arg = l_ctx->old_callback_arg;
    DAP_DELETE(l_ctx);
    l_client_pvt->callback_arg = l_old_arg;
}

/**
 * @brief Handshake response callback wrapper
 */
static void s_http_handshake_response_wrapper(dap_client_t *a_client, void *a_data, size_t a_data_size)
{
    // log_it(L_INFO, "s_http_handshake_response_wrapper: CALLED! client=%p, data=%p, size=%zu", a_client, a_data, a_data_size);
    
    if (!a_client) {
        log_it(L_ERROR, "s_http_handshake_response_wrapper: client is NULL");
        return;
    }
    
    // Get per-request ctx from callback_arg
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(a_client);
    if (!l_client_pvt || !l_client_pvt->callback_arg) {
        log_it(L_ERROR, "s_http_handshake_response_wrapper: no ctx in callback_arg");
        return;
    }
    
    s_http_handshake_ctx_t *l_ctx = (s_http_handshake_ctx_t *)l_client_pvt->callback_arg;
    
    if (l_ctx->client != a_client) {
        log_it(L_WARNING, "s_http_handshake_response_wrapper: client mismatch");
        return;
    }
    
    if (!l_ctx->stream) {
        log_it(L_WARNING, "s_http_handshake_response_wrapper: missing stream ctx");
        return;
    }
    
    // Call trans callback with response data
    if (l_ctx->callback) {
        // log_it(L_INFO, "s_http_handshake_response_wrapper: calling trans callback");
        l_ctx->callback(l_ctx->stream, a_data, a_data_size, 0);
    } else {
        log_it(L_WARNING, "s_http_handshake_response_wrapper: callback is NULL");
    }
    
    // Free ctx and restore old callback_arg
    void *l_old_arg = l_ctx->old_callback_arg;
    DAP_DELETE(l_ctx);
    l_client_pvt->callback_arg = l_old_arg;
}

/**
 * @brief Wrapper callback that extracts ctx from callbacks_arg and calls s_http_session_response_wrapper
 */
static void s_http_session_response_wrapper_with_ctx(void *a_data, size_t a_data_size, void *a_callbacks_arg, UNUSED_ARG http_status_code_t a_status)
{
    if (!a_callbacks_arg) {
        log_it(L_ERROR, "s_http_session_response_wrapper_with_ctx: callbacks_arg is NULL");
        return;
    }
    
    s_http_session_ctx_t *l_session_ctx = (s_http_session_ctx_t *)a_callbacks_arg;
    
    if (!l_session_ctx->client) {
        log_it(L_ERROR, "s_http_session_response_wrapper_with_ctx: client is NULL in ctx");
        return;
    }
    
    // Call the actual wrapper with client and data
    s_http_session_response_wrapper(l_session_ctx->client, a_data, a_data_size);
}

/**
 * @brief Session create response callback wrapper
 */
static void s_http_session_response_wrapper(dap_client_t *a_client, void *a_data, size_t a_data_size)
{
    if (!a_client) {
        log_it(L_ERROR, "s_http_session_response_wrapper: a_client is NULL");
        return;
    }
    
    // Get per-request ctx from callback_arg
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(a_client);
    if (!l_client_pvt || !l_client_pvt->callback_arg) {
        log_it(L_ERROR, "s_http_session_response_wrapper: no ctx in callback_arg. Pvt: %p, Arg: %p", 
               l_client_pvt, l_client_pvt ? l_client_pvt->callback_arg : NULL);
        return;
    }
    
    s_http_session_ctx_t *l_session_ctx = (s_http_session_ctx_t *)l_client_pvt->callback_arg;
    
    // Verify that the ctx matches this client (prevent race conditions)
    if (l_session_ctx->client != a_client) {
        log_it(L_WARNING, "s_http_session_response_wrapper: client mismatch (expected %p, got %p) - ctx overwritten by another request", 
               l_session_ctx->client, a_client);
        return;
    }
    
    if (!l_session_ctx->stream || !l_session_ctx->callback) {
        log_it(L_ERROR, "s_http_session_response_wrapper: invalid ctx (stream=%p, callback=%p)", 
               l_session_ctx->stream, (void*)l_session_ctx->callback);
        return;
    }
    
    debug_if(s_debug_more, L_DEBUG, "s_http_session_response_wrapper: received response, data_size=%zu", a_data_size);
    
    // Get encryption ctx from client_pvt (session_key is stored there after handshake)
    // ALWAYS use session_key from client_pvt. Trans session_key is shared/global and unsafe for parallel clients.
    if (l_client_pvt && l_client_pvt->session_key) {
        debug_if(s_debug_more, L_DEBUG, "s_http_session_response_wrapper: using session_key from client_pvt");
    } else {
        dap_net_trans_t *l_trans = l_session_ctx->stream->trans;
        log_it(L_WARNING, "s_http_session_response_wrapper: no session_key found in client_pvt (trans=%p)", 
               l_trans);
    }
    
    // Parse session response to extract session_id
    uint32_t l_session_id = 0;
    char *l_response_data = NULL;
    size_t l_response_size = 0;
    
    if (a_data && a_data_size > 0) {
        // Response is already decrypted by s_http_request_response if encryption was enabled
        char *l_response_str = (char*)a_data;
        
        // Check if response starts with "ERROR" or looks invalid
        // But generally we expect "session_id stream_key ..."
                
                // Parse response format: "session_id stream_key ..."
        int l_parsed = sscanf(l_response_str, "%u", &l_session_id);
                debug_if(s_debug_more, L_DEBUG, "s_http_session_response_wrapper: parsed session_id=%u (parsed_count=%d)", 
                       l_session_id, l_parsed);
                
                // Allocate and copy full response data for trans callback
        // Ensure null-termination just in case
            l_response_data = DAP_NEW_Z_SIZE(char, a_data_size + 1);
            if (l_response_data) {
                memcpy(l_response_data, a_data, a_data_size);
                l_response_data[a_data_size] = '\0';
                l_response_size = a_data_size;
            }
        
        if (l_parsed < 1) {
             log_it(L_WARNING, "s_http_session_response_wrapper: failed to parse session_id from response (len=%zu): %.100s", 
                   a_data_size, (char*)a_data);
        }
    } else {
        log_it(L_WARNING, "s_http_session_response_wrapper: empty response data");
    }
    
    debug_if(s_debug_more, L_DEBUG, "s_http_session_response_wrapper: calling callback with session_id=%u, response_size=%zu", 
           l_session_id, l_response_size);
    
    // Save ctx data before calling callback (callback might free ctx)
    dap_stream_t *l_stream = l_session_ctx->stream;
    dap_net_trans_session_cb_t l_callback = l_session_ctx->callback;
    void *l_old_callback_arg = l_session_ctx->old_callback_arg;
    
    // Call trans callback with session_id and full response data
    if (l_callback) {
        debug_if(s_debug_more, L_DEBUG, "s_http_session_response_wrapper: calling callback stream=%p, session_id=%u", 
               l_stream, l_session_id);
        l_callback(l_stream, l_session_id, l_response_data, l_response_size, 0);
        debug_if(s_debug_more, L_DEBUG, "s_http_session_response_wrapper: callback returned");
    } else {
        log_it(L_ERROR, "s_http_session_response_wrapper: callback is NULL!");
    }
    
    // Free per-request ctx and restore old callback_arg AFTER callback completes
    // Note: callback should not use ctx after this point
    // Note: l_response_data is freed by callback (s_session_create_callback_wrapper), don't free it here
    if (l_client_pvt && l_session_ctx) {
        debug_if(s_debug_more, L_DEBUG, "s_http_session_response_wrapper: freeing ctx and restoring callback_arg");
        DAP_DELETE(l_session_ctx);
        l_client_pvt->callback_arg = l_old_callback_arg;  // Restore old value
    } else if (l_session_ctx) {
        debug_if(s_debug_more, L_DEBUG, "s_http_session_response_wrapper: freeing ctx (no client_pvt)");
        DAP_DELETE(l_session_ctx);
    }
}

/**
 * @brief Session create error callback wrapper
 */
static void s_http_session_error_wrapper(dap_client_t *a_client, void *a_arg, int a_error)
{
    if (!a_client) {
        return;
    }
    
    // Get per-request ctx from callback_arg
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(a_client);
    if (!l_client_pvt || !l_client_pvt->callback_arg) {
        log_it(L_WARNING, "s_http_session_error_wrapper: no ctx in callback_arg");
        return;
    }
    
    s_http_session_ctx_t *l_session_ctx = (s_http_session_ctx_t *)l_client_pvt->callback_arg;
    
    // Verify that the ctx matches this client
    if (l_session_ctx->client != a_client || !l_session_ctx->stream || !l_session_ctx->callback) {
        log_it(L_WARNING, "s_http_session_error_wrapper: ctx invalid or mismatch (stream=%p, callback=%p, client=%p vs %p)", 
               l_session_ctx->stream, (void*)l_session_ctx->callback, l_session_ctx->client, a_client);
        return;
    }
    
    // Call trans callback with error
    if (l_session_ctx->callback) {
        l_session_ctx->callback(l_session_ctx->stream, 0, NULL, 0, a_error);
    }
    
    // Free per-request ctx and restore old callback_arg
    if (l_client_pvt && l_session_ctx) {
        void *l_old_callback_arg = l_session_ctx->old_callback_arg;
        DAP_DELETE(l_session_ctx);
        l_client_pvt->callback_arg = l_old_callback_arg;  // Restore old value
    } else if (l_session_ctx) {
        DAP_DELETE(l_session_ctx);
    }
}

// ============================================================================
// Trans Operations Implementation
// ============================================================================

/**
 * @brief Initialize HTTP trans instance
 */
static int s_http_trans_init(dap_net_trans_t *a_trans, dap_config_t *a_config)
{
    if (!a_trans) {
        log_it(L_ERROR, "Invalid trans pointer");
        return -1;
    }
    
    // Load debug_more flag from config
    if (g_config) {
        s_debug_more = dap_config_get_item_bool_default(g_config, "dap_client", "debug_more", false);
    }
    
    // Allocate private data
    dap_stream_trans_http_private_t *l_priv = DAP_NEW_Z(dap_stream_trans_http_private_t);
    if (!l_priv) {
        log_it(L_CRITICAL, "Failed to allocate HTTP trans private data");
        return -2;
    }
    
    // Set defaults from config
    l_priv->protocol_version = DAP_PROTOCOL_VERSION;
    l_priv->enc_type = DAP_ENC_KEY_TYPE_IAES;
    l_priv->pkey_exchange_type = DAP_ENC_KEY_TYPE_MSRLN;
    l_priv->pkey_exchange_size = 1184; // MSRLN_PKA_BYTES
    l_priv->block_key_size = 32;
    l_priv->sign_count = 0;
    
    a_trans->_inheritor = l_priv;
    
    // Store HTTP trans instance statically
    s_http_trans = a_trans;
    
    UNUSED(a_config); // Config not used in legacy HTTP trans
    log_it(L_DEBUG, "HTTP trans initialized");
    return 0;
}

/**
 * @brief Deinitialize HTTP trans instance
 */
static void s_http_trans_deinit(dap_net_trans_t *a_trans)
{
    if (!a_trans || !a_trans->_inheritor) {
        return;
    }
    
    dap_stream_trans_http_private_t *l_priv = 
        (dap_stream_trans_http_private_t*)a_trans->_inheritor;
    
    // Free handshake buffer if allocated
    if (l_priv->handshake_buffer) {
        DAP_DELETE(l_priv->handshake_buffer);
        l_priv->handshake_buffer = NULL;
    }
    
    // Don't free enc_key - it's managed by enc_ks
    // Don't free http_client/http_server - they're managed externally
    
    DAP_DELETE(l_priv);
    a_trans->_inheritor = NULL;
    
    // Clear static HTTP trans instance
    if (s_http_trans == a_trans) {
        s_http_trans = NULL;
    }
    
    log_it(L_DEBUG, "HTTP trans deinitialized");
}

/**
 * @brief Connect HTTP trans (client-side)
 */
static int s_http_trans_connect(dap_stream_t *a_stream,
                                     const char *a_host,
                                     uint16_t a_port,
                                     dap_net_trans_connect_cb_t a_callback)
{
    if (!a_stream || !a_host) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // In HTTP trans, connection is handled by HTTP client
    // We just store the parameters for later use
    log_it(L_INFO, "HTTP trans connecting to %s:%u", a_host, a_port);
    
    // Connection is established by HTTP layer
    // We mark it as connected when we get the first HTTP callback
    // UNUSED(a_callback);
    
    // Notify client that we are "connected" (ready to send requests)
    if (a_callback) {
        a_callback(a_stream, 0);
    }
    
    return 0;
}

/**
 * @brief Listen on HTTP trans (server-side)
 */
static int s_http_trans_listen(dap_net_trans_t *a_trans,
                                     const char *a_addr,
                                     uint16_t a_port,
                                     dap_server_t *a_server)
{
    if (!a_trans) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    dap_stream_trans_http_private_t *l_priv = 
        (dap_stream_trans_http_private_t*)a_trans->_inheritor;
    
    if (!l_priv) {
        log_it(L_ERROR, "HTTP trans not initialized");
        return -2;
    }
    
    log_it(L_INFO, "HTTP trans listening on %s:%u", 
           a_addr ? a_addr : "any", a_port);
    
    // Server is already listening via HTTP server
    // This is just a notification
    UNUSED(a_server);
    return 0;
}

/**
 * @brief Accept connection on HTTP trans (server-side)
 */
static int s_http_trans_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out)
{
    if (!a_listener || !a_stream_out) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // HTTP server handles accept internally via dap_http_server
    // Stream is created by HTTP layer when connection is accepted
    // This function is called after stream is already created
    log_it(L_DEBUG, "HTTP trans connection accepted");
    
    // Stream is created by HTTP layer, we just validate it
    return 0;
}

/**
 * @brief Initialize handshake (client-side)
 * 
 * For HTTP trans, handshake is performed via HTTP POST to /enc_init endpoint.
 * This function wraps the legacy HTTP infrastructure (dap_client_pvt_request) 
 * behind the trans abstraction layer.
 */
static int s_http_trans_handshake_init(dap_stream_t *a_stream,
                                             dap_net_handshake_params_t *a_params,
                                             dap_net_trans_handshake_cb_t a_callback)
{
    if (!a_stream || !a_params || !a_callback) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // Get client_pvt from stream esocket
    if (!a_stream->trans_ctx->esocket || !a_stream->trans_ctx->esocket->_inheritor) {
        log_it(L_ERROR, "Stream esocket has no client ctx");
        return -2;
    }
    
    dap_client_t *l_client = (dap_client_t*)a_stream->trans_ctx->esocket->_inheritor;
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    if (!l_client_pvt) {
        log_it(L_ERROR, "Invalid client_pvt");
        return -3;
    }
    
    // Prepare handshake data (alice public key with signatures)
    size_t l_data_size = a_params->alice_pub_key_size;
    uint8_t *l_data = DAP_DUP_SIZE(a_params->alice_pub_key, l_data_size);
    if (!l_data) {
        log_it(L_ERROR, "Failed to allocate handshake data");
        return -4;
    }
    
    // Add certificates signatures
    size_t l_sign_count = 0;
    dap_cert_t *l_node_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
    
    if (a_params->auth_cert) {
        l_sign_count += dap_cert_add_sign_to_data(a_params->auth_cert, &l_data, &l_data_size,
                                                   a_params->alice_pub_key, a_params->alice_pub_key_size);
    }
    
    if (l_node_cert) {
        l_sign_count += dap_cert_add_sign_to_data(l_node_cert, &l_data, &l_data_size,
                                                   a_params->alice_pub_key, a_params->alice_pub_key_size);
    }
    
    // Encode to base64
    size_t l_data_str_size_max = DAP_ENC_BASE64_ENCODE_SIZE(l_data_size);
    char *l_data_str = DAP_NEW_Z_SIZE(char, l_data_str_size_max + 1);
    if (!l_data_str) {
        DAP_DELETE(l_data);
        log_it(L_ERROR, "Failed to allocate base64 buffer");
        return -5;
    }
    
    size_t l_data_str_enc_size = dap_enc_base64_encode(l_data, l_data_size, l_data_str, DAP_ENC_DATA_TYPE_B64);
    DAP_DELETE(l_data);
    
    // Build URL with query parameters
    char l_enc_init_url[1024] = { '\0' };
    snprintf(l_enc_init_url, sizeof(l_enc_init_url), DAP_UPLINK_PATH_ENC_INIT
                 "/gd4y5yh78w42aaagh" "?enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zu,block_key_size=%zu,protocol_version=%d,sign_count=%zu",
                 a_params->enc_type, a_params->pkey_exchange_type, a_params->pkey_exchange_size,
                 a_params->block_key_size, a_params->protocol_version, l_sign_count);
    
    log_it(L_DEBUG, "HTTP handshake init: sending POST to %s:%u%s", 
           l_client->link_info.uplink_addr, l_client->link_info.uplink_port, l_enc_init_url);
    
    // Store callback ctx dynamically
    s_http_handshake_ctx_t *l_ctx = DAP_NEW_Z(s_http_handshake_ctx_t);
    if (!l_ctx) {
        log_it(L_ERROR, "Failed to allocate handshake ctx");
        DAP_DELETE(l_data_str);
        return -6;
    }
    l_ctx->stream = a_stream;
    l_ctx->callback = a_callback;
    l_ctx->client = l_client;
    l_ctx->old_callback_arg = l_client_pvt->callback_arg;
    
    // Set ctx as callback arg
    l_client_pvt->callback_arg = l_ctx;
    
    // Use static HTTP trans instance
    dap_net_trans_t *l_trans = s_http_trans;
    if (!l_trans) {
        log_it(L_ERROR, "HTTP trans not initialized");
        DAP_DELETE(l_data_str);
        l_client_pvt->callback_arg = l_ctx->old_callback_arg;
        DAP_DELETE(l_ctx);
        return -6;
    }
    
    // Make HTTP request using legacy infrastructure
    int l_res = s_http_request(l_client_pvt, l_trans, l_enc_init_url,
                               l_data_str, l_data_str_enc_size, 
                               s_http_handshake_response_wrapper, 
                               s_http_handshake_error_wrapper);
    
    DAP_DELETE(l_data_str);
    
    if (l_res < 0) {
        log_it(L_ERROR, "Failed to create HTTP request for enc_init (return code: %d)", l_res);
        l_client_pvt->callback_arg = l_ctx->old_callback_arg;
        DAP_DELETE(l_ctx);
        return -6;
    }
    
    log_it(L_DEBUG, "HTTP handshake init request sent successfully");
    return 0;
}

/**
 * @brief Process handshake response/request (server-side)
 */
static int s_http_trans_handshake_process(dap_stream_t *a_stream,
                                                const void *a_data, size_t a_data_size,
                                                void **a_response, size_t *a_response_size)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream pointer");
        return -1;
    }
    
    // HTTP handshake processing is done by enc_server
    // This function is called on server side to process client handshake request
    log_it(L_DEBUG, "HTTP trans handshake process: %zu bytes", a_data_size);
    
    // Processing is done by enc_server infrastructure
    // Response is generated there
    UNUSED(a_data);
    UNUSED(a_response);
    UNUSED(a_response_size);
    
    // Server-side handshake handled by existing enc_server
    return 0;
}

/**
 * @brief Create session after handshake
 * 
 * For HTTP trans, session creation is performed via HTTP POST to /stream_ctl endpoint.
 * This function wraps the HTTP request infrastructure behind the trans abstraction layer.
 */
static int s_http_trans_session_create(dap_stream_t *a_stream,
                                             dap_net_session_params_t *a_params,
                                             dap_net_trans_session_cb_t a_callback)
{
    if (!a_stream || !a_params || !a_callback) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // Get client_pvt from stream esocket
    if (!a_stream->trans_ctx->esocket || !a_stream->trans_ctx->esocket->_inheritor) {
        log_it(L_ERROR, "Stream esocket has no client ctx");
        return -2;
    }
    
    dap_client_t *l_client = (dap_client_t*)a_stream->trans_ctx->esocket->_inheritor;
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    if (!l_client_pvt) {
        log_it(L_ERROR, "Invalid client_pvt");
        return -3;
    }
    
    // Prepare request data (protocol version)
    char l_request[16];
    size_t l_request_size = snprintf(l_request, sizeof(l_request), "%d", DAP_CLIENT_PROTOCOL_VERSION);
    
    // Prepare sub_url based on protocol version
    uint32_t l_least_common_dap_protocol = dap_min(l_client_pvt->remote_protocol_version,
                                                   l_client_pvt->uplink_protocol_version);
    
    char *l_suburl;
    if (l_least_common_dap_protocol < 23) {
        l_suburl = dap_strdup_printf("stream_ctl,channels=%s", a_params->channels);
    } else {
        l_suburl = dap_strdup_printf("channels=%s,enc_type=%d,enc_key_size=%zu,enc_headers=%d",
                                     a_params->channels, a_params->enc_type,
                                     a_params->enc_key_size, a_params->enc_headers ? 1 : 0);
    }
    
    log_it(L_DEBUG, "HTTP session create: sending POST to %s:%u%s/%s", 
           l_client->link_info.uplink_addr, l_client->link_info.uplink_port, 
           DAP_UPLINK_PATH_STREAM_CTL, l_suburl);
    
    // Allocate per-request ctx dynamically
    s_http_session_ctx_t *l_session_ctx = DAP_NEW_Z(s_http_session_ctx_t);
    if (!l_session_ctx) {
        log_it(L_ERROR, "Failed to allocate session ctx");
        DAP_DELETE(l_suburl);
        return -4;
    }
    
    l_session_ctx->stream = a_stream;
    l_session_ctx->callback = a_callback;
    l_session_ctx->client = l_client;
    
    // Use static HTTP trans instance
    dap_net_trans_t *l_trans = s_http_trans;
    if (!l_trans) {
        log_it(L_ERROR, "HTTP trans not initialized");
        DAP_DELETE(l_suburl);
        DAP_DELETE(l_session_ctx);
        return -6;
    }
    
    // Store ctx in callback_arg temporarily for s_http_request_enc
    // It will be passed to dap_client_http_request via callbacks_arg
    void *l_old_callback_arg = l_client_pvt->callback_arg;
    l_session_ctx->old_callback_arg = l_old_callback_arg; // Save old callback arg
    l_client_pvt->callback_arg = l_session_ctx;
    
    // Make HTTP request using legacy infrastructure
    // Pass ctx through callbacks_arg parameter
    // Note: callback_arg will be restored in s_http_session_response_wrapper or s_http_session_error_wrapper
    s_http_request_enc(l_client_pvt, l_trans, DAP_UPLINK_PATH_STREAM_CTL,
                                l_suburl, "type=tcp,maxconn=4", l_request, l_request_size,
                                s_http_session_response_wrapper, 
                                s_http_session_error_wrapper, l_session_ctx);
    
    // Don't restore callback_arg here - it will be restored in callback after request completes
    
    DAP_DELETE(l_suburl);
    
    log_it(L_DEBUG, "HTTP session create request sent successfully");
    return 0;
}

/**
 * @brief Start streaming after session creation
 */
static int s_http_trans_session_start(dap_stream_t *a_stream,
                                            uint32_t a_session_id,
                                            dap_net_trans_ready_cb_t a_callback)
{
    if (!a_stream || !a_stream->trans_ctx->esocket || !a_stream->trans_ctx->esocket->_inheritor) {
        log_it(L_ERROR, "Invalid stream or client ctx");
        return -1;
    }
    
    dap_client_t *l_client = (dap_client_t*)a_stream->trans_ctx->esocket->_inheritor;
    
    log_it(L_DEBUG, "HTTP trans session start: session_id=%u", a_session_id);
    
    // Construct HTTP GET request for streaming
    char l_full_path[2048];
    snprintf(l_full_path, sizeof(l_full_path), "%s/globaldb?session_id=%u", DAP_UPLINK_PATH_STREAM, a_session_id);
    
    // Write request to socket
    // Note: stream->trans_ctx->esocket is the raw TCP socket created in stage_prepare
    size_t l_sent = dap_events_socket_write_f_unsafe(a_stream->trans_ctx->esocket, 
                                     "GET /%s HTTP/1.1\r\n"
                                     "Host: %s:%d\r\n"
                                     "\r\n",
                                     l_full_path, 
                                     l_client->link_info.uplink_addr, 
                                     l_client->link_info.uplink_port);
                                     
    if (l_sent == 0) {
        log_it(L_ERROR, "Failed to write HTTP GET request to stream socket");
        return -1;
    }
    
    // Signal readiness (request sent)
    if (a_callback) {
        a_callback(a_stream, 0);
    }
    
    return 0;
}

/**
 * @brief Read data from HTTP trans
 */
static ssize_t s_http_trans_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    UNUSED(a_buffer);
    UNUSED(a_size);

    if (!a_stream || !a_stream->trans_ctx->esocket) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    dap_events_socket_t *l_es = a_stream->trans_ctx->esocket;
    
    // Check if we need to skip HTTP headers (only if buffer starts with "HTTP/")
    if (l_es->buf_in_size >= 5 && memcmp(l_es->buf_in, "HTTP/", 5) == 0) {
        // Search for double CRLF (end of headers)
        char *l_headers_end = NULL;
        size_t l_search_len = l_es->buf_in_size;
        
        // Find end of headers safely
        for (size_t i = 0; i < l_search_len - 3; i++) {
            if (l_es->buf_in[i] == '\r' && l_es->buf_in[i+1] == '\n' &&
                l_es->buf_in[i+2] == '\r' && l_es->buf_in[i+3] == '\n') {
                l_headers_end = (char*)l_es->buf_in + i;
                break;
            }
        }
        
        if (l_headers_end) {
            size_t l_headers_size = (l_headers_end - (char*)l_es->buf_in) + 4;
            log_it(L_DEBUG, "Skipping HTTP headers (%zu bytes)", l_headers_size);
    
            // Return header size so caller (dap_client_pvt) can shrink buffer
            // Next call will process data after headers
            return (ssize_t)l_headers_size;
        } else {
            // Headers incomplete. Return 0 to wait for more data.
    return 0;
        }
    }
    
    // No headers (or already skipped). Process stream packets.
    return (ssize_t)dap_stream_data_proc_read(a_stream);
}

/**
 * @brief Write data to HTTP trans
 */
static ssize_t s_http_trans_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // HTTP trans writing is done via dap_http_client write
    // Data is queued and sent via HTTP connection
    log_it(L_DEBUG, "HTTP trans write: %zu bytes", a_size);
    
    // Writing is handled by HTTP infrastructure
    // Return size to indicate success
    return (ssize_t)a_size;
}

/**
 * @brief Send unencrypted HTTP request (public API)
 * 
 * This is a public wrapper for internal HTTP request functionality.
 * Used by dap_client_request() for thread-safe requests.
 * 
 * @param a_client_pvt Client private structure
 * @param a_path HTTP path
 * @param a_request Request data
 * @param a_request_size Request data size
 * @param a_response_proc Response callback
 * @param a_response_error Error callback
 * @return 0 on success, -1 on failure
 */
int dap_net_trans_http_request(dap_client_pvt_t * a_client_internal, const char * a_path, void * a_request,
        size_t a_request_size, dap_client_callback_data_size_t a_response_proc,
        dap_client_callback_int_t a_response_error)
{
    // Use static HTTP trans instance
    dap_net_trans_t *l_trans = s_http_trans;
    if (!l_trans) {
        log_it(L_ERROR, "HTTP trans not initialized");
        return -1;
    }
    
    return s_http_request(a_client_internal, l_trans, a_path, a_request, a_request_size, a_response_proc, a_response_error);
}

/**
 * @brief Send encrypted HTTP request (public API)
 * 
 * This is a public wrapper for internal HTTP encrypted request functionality.
 * Used by dap_client_request_enc() for thread-safe encrypted requests.
 * 
 * @param a_client_pvt Client private structure
 * @param a_path HTTP path
 * @param a_sub_url Sub-URL (will be encrypted)
 * @param a_query Query string (will be encrypted)
 * @param a_request Request data (will be encrypted)
 * @param a_request_size Request data size
 * @param a_response_proc Response callback
 * @param a_response_error Error callback
 */
void dap_net_trans_http_request_enc(dap_client_pvt_t * a_client_internal, const char *a_path,
                        const char *a_sub_url, const char * a_query, void *a_request, size_t a_request_size,
                        dap_client_callback_data_size_t a_response_proc, dap_client_callback_int_t a_response_error)
{
    // Use static HTTP trans instance
    dap_net_trans_t *l_trans = s_http_trans;
    if (!l_trans) {
        log_it(L_ERROR, "HTTP trans not initialized");
        if (a_response_error) {
            a_response_error(a_client_internal->client, a_client_internal->callback_arg, -1);
        }
        return;
    }
    
    s_http_request_enc(a_client_internal, l_trans, a_path, a_sub_url, a_query, a_request, a_request_size, a_response_proc, a_response_error, NULL);
}

/**
 * @brief Send unencrypted HTTP request
 * 
 * This function is HTTP-specific and encapsulates the unencrypted HTTP request logic.
 * It's used internally by HTTP trans for handshake (unencrypted requests).
 */
static int s_http_request(dap_client_pvt_t * a_client_internal, dap_net_trans_t *a_trans, const char * a_path, void * a_request,
        size_t a_request_size, dap_client_callback_data_size_t a_response_proc,
        dap_client_callback_int_t a_response_error)
{
    log_it(L_INFO, "s_http_request: CALLED! path='%s', request_size=%zu, worker=%p", 
             a_path, a_request_size, a_client_internal->worker);
    debug_if(s_debug_more, L_DEBUG, "s_http_request: response_proc=%p, response_error=%p", 
             (void*)a_response_proc, (void*)a_response_error);
    
    // Create per-request ctx
    s_http_trans_request_ctx_t *l_ctx = DAP_NEW_Z(s_http_trans_request_ctx_t);
    l_ctx->client_pvt = a_client_internal;
    l_ctx->callback = a_response_proc;
    l_ctx->error_callback = a_response_error;
    l_ctx->callback_arg = a_client_internal->callback_arg; // Store current callback arg
    l_ctx->is_encrypted = false;
    
    // Get HTTP trans private from trans parameter
    dap_stream_trans_http_private_t *l_priv = NULL;
    if (a_trans && a_trans->type == DAP_NET_TRANS_HTTP) {
        l_priv = (dap_stream_trans_http_private_t*)a_trans->_inheritor;
    }
    
    log_it(L_INFO, "s_http_request: calling dap_client_http_request for path='%s'", a_path);
    dap_client_http_t *l_http_client = dap_client_http_request(a_client_internal->worker, 
                                            a_client_internal->client->link_info.uplink_addr,
                                            a_client_internal->client->link_info.uplink_port,
                                            a_request ? "POST" : "GET", "text/text", a_path, a_request,
                                            a_request_size, NULL, s_http_request_response_unencrypted, 
                                            s_http_request_error_unencrypted, l_ctx, NULL);
    
    if (l_http_client == NULL) {
        log_it(L_ERROR, "s_http_request: dap_client_http_request returned NULL for path='%s'", a_path);
        DAP_DELETE(l_ctx);
    } else {
        log_it(L_INFO, "s_http_request: dap_client_http_request succeeded for path='%s', http_client=%p", a_path, (void*)l_http_client);
        // Store HTTP client instance in trans private
        if (l_priv) {
            l_priv->client_http_instance = l_http_client;
        }
    }
    
    return l_http_client == NULL;
}

/**
 * @brief Unencrypted HTTP request error callback
 */
static void s_http_request_error_unencrypted(int a_err_code, void * a_obj)
{
    if (a_obj == NULL) {
        log_it(L_ERROR,"Object is NULL for s_http_request_error_unencrypted");
        return;
    }
    
    s_http_trans_request_ctx_t *l_ctx = (s_http_trans_request_ctx_t *)a_obj;
    dap_client_pvt_t * l_client_pvt = l_ctx->client_pvt;
    assert(l_client_pvt);
    
    if (l_ctx->error_callback) {
          // Temporarily set callback_arg for the callback execution
          void *l_old_callback_arg = l_client_pvt->callback_arg;
          l_client_pvt->callback_arg = l_ctx->callback_arg;
          
          l_ctx->error_callback(l_client_pvt->client, l_client_pvt->callback_arg, a_err_code);
          
          // Restore callback_arg
          l_client_pvt->callback_arg = l_old_callback_arg;
    }
          
    DAP_DELETE(l_ctx);
}

/**
 * @brief Unencrypted HTTP request response callback
 */
static void s_http_request_response_unencrypted(void * a_response, size_t a_response_size, void * a_obj, UNUSED_ARG http_status_code_t a_http_code)
{
    s_http_trans_request_ctx_t *l_ctx = (s_http_trans_request_ctx_t *)a_obj;
    assert(l_ctx);
    dap_client_pvt_t * l_client_pvt = l_ctx->client_pvt;
    assert(l_client_pvt);
    
    log_it(L_INFO, "s_http_request_response_unencrypted: CALLED! response_size=%zu, callback=%p, client=%p", 
             a_response_size, (void*)l_ctx->callback, l_client_pvt->client);
    
    debug_if(s_debug_more, L_DEBUG, "s_http_request_response_unencrypted: is_encrypted=%d", l_ctx->is_encrypted);
    
    if ( !l_ctx->callback ) {
        log_it(L_ERROR, "No request_response_callback in client!");
        DAP_DELETE(l_ctx);
        return;
    }
    
    // Temporarily set callback_arg for the callback execution
    void *l_old_callback_arg = l_client_pvt->callback_arg;
    l_client_pvt->callback_arg = l_ctx->callback_arg;
    
    if (a_response && a_response_size) {
        log_it(L_INFO, "s_http_request_response_unencrypted: calling callback with response (size=%zu)", a_response_size);
        l_ctx->callback(l_client_pvt->client, a_response, a_response_size);
    } else {
        log_it(L_WARNING, "s_http_request_response_unencrypted: empty response (response=%p, size=%zu)", a_response, a_response_size);
    }
    
    // Restore callback_arg
    l_client_pvt->callback_arg = l_old_callback_arg;
    
    DAP_DELETE(l_ctx);
}

/**
 * @brief Send encrypted HTTP request
 * 
 * This function is HTTP-specific and encapsulates the encryption and HTTP request logic.
 * It's used internally by HTTP trans for session creation and other encrypted requests.
 */
static void s_http_request_enc(dap_client_pvt_t * a_client_internal, dap_net_trans_t *a_trans, const char *a_path,
                        const char *a_sub_url, const char * a_query, void *a_request, size_t a_request_size,
                        dap_client_callback_data_size_t a_response_proc, dap_client_callback_int_t a_response_error, void *a_callbacks_arg)
{
    bool is_query_enc = true; // if true, then encode a_query string  [Why do we even need this?]
    debug_if(s_debug_more, L_DEBUG, "Encrypt request: sub_url '%s' query '%s'",
             a_sub_url ? a_sub_url : "", a_query ? a_query : "");
    dap_enc_data_type_t l_enc_type = a_client_internal->uplink_protocol_version >= 21
        ? DAP_ENC_DATA_TYPE_B64_URLSAFE : DAP_ENC_DATA_TYPE_B64;
    char *l_path = NULL, *l_request_enc = NULL;
    if (a_path && *a_path) {
        size_t l_suburl_len = a_sub_url && *a_sub_url ? dap_strlen(a_sub_url) : 0,
               l_suburl_enc_size = dap_enc_code_out_size(a_client_internal->session_key, l_suburl_len, l_enc_type),
               l_query_len = a_query && *a_query ? dap_strlen(a_query) : 0,
               l_query_enc_size = dap_enc_code_out_size(a_client_internal->session_key, l_query_len, l_enc_type),
               l_path_size = dap_strlen(a_path) + l_suburl_enc_size + l_query_enc_size + 3;
        l_path = DAP_NEW_Z_SIZE(char, l_path_size);
        char *l_offset = dap_strncpy(l_path, a_path, l_path_size);
        *l_offset++ = '/';
        if (l_suburl_enc_size) {
            l_offset += dap_enc_code(a_client_internal->session_key, a_sub_url, l_suburl_len,
                                     l_offset, l_suburl_enc_size, l_enc_type);
            if (l_query_enc_size) {
                *l_offset++ = '?';
                dap_enc_code(a_client_internal->session_key, a_query, l_query_len,
                             l_offset, l_query_enc_size, l_enc_type);
            }
        }
    }
    size_t l_req_enc_size = 0;
    if (a_request && a_request_size) {
        l_req_enc_size = dap_enc_code_out_size(a_client_internal->session_key, a_request_size, l_enc_type) + 1;
        l_request_enc = DAP_NEW_Z_SIZE(char, l_req_enc_size);
        dap_enc_code(a_client_internal->session_key, a_request, a_request_size,
                     l_request_enc, l_req_enc_size, DAP_ENC_DATA_TYPE_RAW);
    }
    char *l_custom = dap_strdup_printf("KeyID: %s\r\n%s",
        a_client_internal->session_key_id ? a_client_internal->session_key_id : "NULL",
        a_client_internal->is_close_session ? "SessionCloseAfterRequest: true\r\n" : "");

    // Create per-request ctx to avoid race conditions
    s_http_trans_request_ctx_t *l_ctx = DAP_NEW_Z(s_http_trans_request_ctx_t);
    l_ctx->client_pvt = a_client_internal;
    l_ctx->callback = a_response_proc;
    l_ctx->error_callback = a_response_error;
    l_ctx->callback_arg = a_callbacks_arg; // Store ctx for callback
    l_ctx->is_encrypted = true;
    
    // Get HTTP trans private from trans parameter
    dap_stream_trans_http_private_t *l_priv = NULL;
    if (a_trans && a_trans->type == DAP_NET_TRANS_HTTP) {
        l_priv = (dap_stream_trans_http_private_t*)a_trans->_inheritor;
    }
    
    dap_client_http_t *l_http_client = dap_client_http_request(a_client_internal->worker,
        a_client_internal->client->link_info.uplink_addr, a_client_internal->client->link_info.uplink_port,
        a_request ? "POST" : "GET", "text/text", l_path, l_request_enc, l_req_enc_size, NULL,
        s_http_request_response, s_http_request_error, l_ctx, l_custom);
    
    // NOTE: a_callbacks_arg parameter is ignored here because we use l_ctx for callback ctx.
    // The actual ctx needed by callback (like session_ctx) should be stored in client_pvt->callback_arg
    // or added to s_http_trans_request_ctx_t if thread safety requires it.
    UNUSED(a_callbacks_arg);
    
    if (!l_http_client) {
        log_it(L_ERROR, "Failed to create HTTP client for encrypted request");
        DAP_DELETE(l_ctx);
    } else {
        // Store HTTP client instance in trans private
        if (l_priv) {
            l_priv->client_http_instance = l_http_client;
        }
    }
    
    DAP_DEL_MULTY(l_path, l_request_enc, l_custom);
}

/**
 * @brief HTTP request error callback
 */
static void s_http_request_error(int a_err_code, void * a_obj)
{
    if (a_obj == NULL) {
        log_it(L_ERROR,"Object is NULL for s_http_request_error");
        return;
    }
    
    s_http_trans_request_ctx_t *l_ctx = (s_http_trans_request_ctx_t *)a_obj;
    dap_client_pvt_t * l_client_pvt = l_ctx->client_pvt;
    assert(l_client_pvt);
    
    if (l_ctx->error_callback) {
          // Temporarily set callback_arg for the callback execution
          void *l_old_callback_arg = l_client_pvt->callback_arg;
          l_client_pvt->callback_arg = l_ctx->callback_arg;
          
          l_ctx->error_callback(l_client_pvt->client, l_client_pvt->callback_arg, a_err_code);
          
          // Restore callback_arg
          l_client_pvt->callback_arg = l_old_callback_arg;
    }
          
    DAP_DELETE(l_ctx);
}

/**
 * @brief HTTP request response callback
 */
static void s_http_request_response(void * a_response, size_t a_response_size, void * a_obj, UNUSED_ARG http_status_code_t a_http_code)
{
    s_http_trans_request_ctx_t *l_ctx = (s_http_trans_request_ctx_t *)a_obj;
    assert(l_ctx);
    dap_client_pvt_t * l_client_pvt = l_ctx->client_pvt;
    assert(l_client_pvt);
    
    debug_if(s_debug_more, L_DEBUG, "s_http_request_response: response_size=%zu, is_encrypted=%d, callback=%p", 
             a_response_size, l_ctx->is_encrypted, (void*)l_ctx->callback);
    
    if (!l_ctx->callback) {
        log_it(L_ERROR, "No request_response_callback in request ctx!");
        DAP_DELETE(l_ctx);
        return;
    }
    
    // Temporarily set callback_arg for the callback execution
    void *l_old_callback_arg = l_client_pvt->callback_arg;
    l_client_pvt->callback_arg = l_ctx->callback_arg;
    
    if (a_response && a_response_size) {
        if (l_ctx->is_encrypted) {
            if (!l_client_pvt->session_key) {
                log_it(L_ERROR, "No session key in encrypted client!");
                l_client_pvt->callback_arg = l_old_callback_arg; // Restore
                DAP_DELETE(l_ctx);
                return;
            }
            // Use RAW by default as server response usually is RAW encrypted stream (or encoded inside)
            // If we use B64 here, we might fail if server sends RAW.
            dap_enc_data_type_t l_enc_type = DAP_ENC_DATA_TYPE_RAW;

            // Calculate expected output size
            size_t l_len_calc = dap_enc_decode_out_size(l_client_pvt->session_key, a_response_size, l_enc_type);
            // Allocate slightly more to be safe (some implementations might require alignment or check buffer > required)
            // Using a_response_size as lower bound for allocation if it's larger than calc (unlikely for B64 but safe)
            size_t l_len_buf = dap_max(l_len_calc, a_response_size) + 32; 
            
            char *l_response = DAP_NEW_Z_SIZE(char, l_len_buf);
            
            // Pass buffer size, not just expected size
            size_t l_len = dap_enc_decode(l_client_pvt->session_key, a_response, a_response_size,
                                   l_response, l_len_buf, l_enc_type);
            
            // Ensure null-termination (dap_enc_decode might not do it)
            if (l_len < l_len_buf) l_response[l_len] = '\0';
            else l_response[l_len_buf - 1] = '\0'; // Should not happen if size matches
            
            debug_if(s_debug_more, L_DEBUG, "s_http_request_response: calling request_response_callback client=%p, callback=%p, len=%zu (buf=%zu)", 
                   l_client_pvt->client, (void*)l_ctx->callback, l_len, l_len_buf);
            // Log first few bytes of response to debug "garbage" issue
            if (s_debug_more && l_len > 0) {
                char l_preview[64] = {0};
                size_t l_preview_len = l_len < 63 ? l_len : 63;
                memcpy(l_preview, l_response, l_preview_len);
                // Replace non-printable with '.'
                for (size_t i = 0; i < l_preview_len; i++) {
                    if (l_preview[i] < 32 || l_preview[i] > 126) l_preview[i] = '.';
                }
                debug_if(s_debug_more, L_DEBUG, "Decrypted response preview: '%s'", l_preview);
            }

            l_ctx->callback(l_client_pvt->client, l_response, l_len);
            debug_if(s_debug_more, L_DEBUG, "s_http_request_response: request_response_callback returned");
            DAP_DELETE(l_response);
        } else {
            debug_if(s_debug_more, L_DEBUG, "s_http_request_response: calling callback with unencrypted response (size=%zu)", a_response_size);
            l_ctx->callback(l_client_pvt->client, a_response, a_response_size);
        }
    } else {
        log_it(L_WARNING, "s_http_request_response: empty response (response=%p, size=%zu)", a_response, a_response_size);
    }
    
    // Restore callback_arg
    l_client_pvt->callback_arg = l_old_callback_arg;
    
    DAP_DELETE(l_ctx);
}

/**
 * @brief Close HTTP trans connection
 */
static void s_http_trans_close(dap_stream_t *a_stream)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream pointer");
        return;
    }
    
    log_it(L_DEBUG, "HTTP trans connection closed");
    
    // HTTP trans doesn't need special close handling
    // The connection is managed by HTTP client infrastructure
}

/**
 * @brief Prepare TCP socket for HTTP trans (client-side stage preparation)
 * 
 * Fully prepares esocket: creates, sets callbacks, connects, and adds to worker.
 * Trans is responsible for complete esocket lifecycle management.
 */
static int s_http_stage_prepare(dap_net_trans_t *a_trans,
                                const dap_net_stage_prepare_params_t *a_params,
                                dap_net_stage_prepare_result_t *a_result)
{
    if (!a_trans || !a_params || !a_result) {
        log_it(L_ERROR, "Invalid arguments for HTTP stage_prepare");
        return -1;
    }
    
    if (!a_params->worker) {
        log_it(L_ERROR, "Worker is required for HTTP stage_prepare");
        a_result->error_code = -1;
        return -1;
    }
    
    // Initialize result
    a_result->esocket = NULL;
    a_result->error_code = 0;
    
    // Create TCP socket using platform-independent function
    dap_events_socket_t *l_es = dap_events_socket_create_platform(PF_INET, SOCK_STREAM, 0, a_params->callbacks);
    if (!l_es) {
        log_it(L_ERROR, "Failed to create HTTP TCP socket");
        a_result->error_code = -1;
        return -1;
    }
    
    l_es->type = DESCRIPTOR_TYPE_SOCKET_CLIENT;
    l_es->_inheritor = a_params->client_ctx;
    
    // Resolve host and set address using centralized function
    if (dap_events_socket_resolve_and_set_addr(l_es, a_params->host, a_params->port) < 0) {
        log_it(L_ERROR, "Failed to resolve address for HTTP trans");
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    // Set CONNECTING flag and initiate connection
    l_es->flags |= DAP_SOCK_CONNECTING;
#ifndef DAP_EVENTS_CAPS_IOCP
    l_es->flags |= DAP_SOCK_READY_TO_WRITE;
#endif
    l_es->is_initalized = false; // Ensure new_callback will be called
    
    // Initiate connection using platform-independent function
    int l_connect_err = 0;
    if (dap_events_socket_connect(l_es, &l_connect_err) != 0) {
        log_it(L_ERROR, "Failed to connect HTTP socket: error %d", l_connect_err);
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    // Add socket to worker - connection will complete asynchronously
    dap_worker_add_events_socket(a_params->worker, l_es);
    
    a_result->esocket = l_es;
    a_result->error_code = 0;
    log_it(L_DEBUG, "HTTP TCP socket prepared and connected for %s:%u", a_params->host, a_params->port);
    return 0;
}

/**
 * @brief Get HTTP trans capabilities
 */
static uint32_t s_http_trans_get_capabilities(dap_net_trans_t *a_trans)
{
    (void)a_trans;
    
    return DAP_NET_TRANS_CAP_RELIABLE |
           DAP_NET_TRANS_CAP_ORDERED |
           DAP_NET_TRANS_CAP_BIDIRECTIONAL;
    // HTTP doesn't natively support compression or multiplexing in our impl
}

// ============================================================================
// Trans Operations Table
// ============================================================================

static const dap_net_trans_ops_t s_http_trans_ops = {
    .init = s_http_trans_init,
    .deinit = s_http_trans_deinit,
    .connect = s_http_trans_connect,
    .listen = s_http_trans_listen,
    .accept = s_http_trans_accept,
    .handshake_init = s_http_trans_handshake_init,
    .handshake_process = s_http_trans_handshake_process,
    .session_create = s_http_trans_session_create,
    .session_start = s_http_trans_session_start,
    .read = s_http_trans_read,
    .write = s_http_trans_write,
    .close = s_http_trans_close,
    .get_capabilities = s_http_trans_get_capabilities,
    .stage_prepare = s_http_stage_prepare,
    .register_server_handlers = NULL  // HTTP trans doesn't need additional handlers
};

// ============================================================================
// Registration Functions
// ============================================================================

/**
 * @brief Register HTTP trans adapter
 */
int dap_net_trans_http_stream_register(void)
{
    log_it(L_DEBUG, "dap_net_trans_http_stream_register: Starting HTTP trans registration");
    // Initialize HTTP server module first (registers server operations)
    int l_ret = dap_net_trans_http_server_init();
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize HTTP server module: %d", l_ret);
        return l_ret;
    }
    
    log_it(L_DEBUG, "dap_net_trans_http_stream_register: HTTP server module initialized, registering trans");
    
    // Register HTTP trans operations
    int l_ret_trans = dap_net_trans_register("HTTP", 
                                                DAP_NET_TRANS_HTTP,
                                                &s_http_trans_ops,
                                                DAP_NET_TRANS_SOCKET_TCP,
                                                NULL);  // No inheritor needed at registration
    if (l_ret_trans < 0) {
        log_it(L_ERROR, "Failed to register HTTP trans: %d", l_ret_trans);
        dap_net_trans_http_server_deinit();
        return l_ret_trans;
    }
    
    log_it(L_NOTICE, "HTTP trans adapter registered");
    return 0;
}

/**
 * @brief Unregister HTTP trans adapter
 */
int dap_net_trans_http_stream_unregister(void)
{
    log_it(L_DEBUG, "dap_net_trans_http_stream_unregister: Starting HTTP trans unregistration");
    
    int l_ret = dap_net_trans_unregister(DAP_NET_TRANS_HTTP);
    if (l_ret < 0) {
        log_it(L_WARNING, "Failed to unregister HTTP trans");
        return l_ret;
    }
    
    // Deinitialize HTTP server module (unregisters server operations)
    log_it(L_DEBUG, "dap_net_trans_http_stream_unregister: Deinitializing HTTP server module");
    dap_net_trans_http_server_deinit();
    
    log_it(L_NOTICE, "HTTP trans adapter unregistered successfully");
    return 0;
}

// ============================================================================
// Protocol Translation Functions
// ============================================================================

/**
 * @brief Parse HTTP query string to handshake parameters
 */
int dap_stream_trans_http_parse_query_params(
    const char *a_query_string,
    dap_net_handshake_params_t *a_params)
{
    if (!a_query_string || !a_params) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // Initialize with defaults
    a_params->enc_type = DAP_ENC_KEY_TYPE_IAES;
    a_params->pkey_exchange_type = DAP_ENC_KEY_TYPE_MSRLN;
    a_params->pkey_exchange_size = 1184;
    a_params->block_key_size = 32;
    a_params->protocol_version = DAP_PROTOCOL_VERSION;
    
    // Parse query string
    // Format: "enc_type=X,pkey_exchange_type=Y,pkey_exchange_size=Z,block_key_size=W,protocol_version=V,sign_count=S"
    int l_enc_type = 0, l_pkey_type = 0, l_protocol_version = 0;
    size_t l_pkey_size = 0, l_block_size = 0, l_sign_count = 0;
    
    int l_parsed = sscanf(a_query_string,
                          "enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zu,"
                          "block_key_size=%zu,protocol_version=%d,sign_count=%zu",
                          &l_enc_type, &l_pkey_type, &l_pkey_size,
                          &l_block_size, &l_protocol_version, &l_sign_count);
    
    if (l_parsed < 2) {
        log_it(L_WARNING, "Failed to parse query string, using defaults");
        return 0; // Not fatal, use defaults
    }
    
    // Apply parsed values
    if (l_enc_type > 0) a_params->enc_type = (dap_enc_key_type_t)l_enc_type;
    if (l_pkey_type > 0) a_params->pkey_exchange_type = (dap_enc_key_type_t)l_pkey_type;
    if (l_pkey_size > 0) a_params->pkey_exchange_size = l_pkey_size;
    if (l_block_size > 0) a_params->block_key_size = l_block_size;
    if (l_protocol_version > 0) a_params->protocol_version = l_protocol_version;
    
    log_it(L_DEBUG, "Parsed query params: enc=%d, pkey=%d, pkey_size=%zu, block=%zu, ver=%d",
           a_params->enc_type, a_params->pkey_exchange_type, 
           a_params->pkey_exchange_size, a_params->block_key_size,
           a_params->protocol_version);
    
    return 0;
}

/**
 * @brief Convert handshake parameters to HTTP query string
 */
int dap_stream_trans_http_format_query_params(
    const dap_net_handshake_params_t *a_params,
    char *a_query_string_out,
    size_t a_buf_size)
{
    if (!a_params || !a_query_string_out || a_buf_size == 0) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    int l_written = snprintf(a_query_string_out, a_buf_size,
                             "enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zu,"
                             "block_key_size=%zu,protocol_version=%d,sign_count=%zu",
                             a_params->enc_type,
                             a_params->pkey_exchange_type,
                             a_params->pkey_exchange_size,
                             a_params->block_key_size,
                             a_params->protocol_version,
                             a_params->sign_count);
    
    if (l_written < 0 || (size_t)l_written >= a_buf_size) {
        log_it(L_ERROR, "Query string buffer too small");
        return -2;
    }
    
    return l_written;
}

// ============================================================================
// Configuration Functions
// ============================================================================

/**
 * @brief Get default HTTP trans configuration
 */
dap_stream_trans_http_config_t dap_stream_trans_http_config_default(void)
{
    return s_config;
}

/**
 * @brief Set HTTP trans configuration
 */
int dap_stream_trans_http_set_config(const dap_stream_trans_http_config_t *a_config)
{
    if (!a_config) {
        log_it(L_ERROR, "Invalid config pointer");
        return -1;
    }
    
    s_config = *a_config;
    log_it(L_INFO, "HTTP trans configuration updated");
    return 0;
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Get HTTP trans private data from stream
 */
dap_stream_trans_http_private_t* dap_stream_trans_http_get_private(
    dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans) {
        return NULL;
    }
    
    if (a_stream->trans->type != DAP_NET_TRANS_HTTP) {
        return NULL;
    }
    
    return (dap_stream_trans_http_private_t*)a_stream->trans->_inheritor;
}

/**
 * @brief Check if stream is using HTTP trans
 */
bool dap_stream_trans_is_http(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans) {
        return false;
    }
    
    return a_stream->trans->type == DAP_NET_TRANS_HTTP;
}

/**
 * @brief Get HTTP client from stream
 */
dap_http_client_t* dap_stream_trans_http_get_client(dap_stream_t *a_stream)
{
    dap_stream_trans_http_private_t *l_priv = 
        dap_stream_trans_http_get_private(a_stream);
    
    return l_priv ? l_priv->http_client : NULL;
}

// ============================================================================
// HTTP Server Integration (Backward Compatibility)
// ============================================================================

/**
 * @brief Add HTTP stream processor to HTTP server
 * 
 * This is the backward-compatible entry point
 */
void dap_stream_trans_http_add_proc(dap_http_server_t *a_http_server,
                                         const char *a_url_path)
{
    if (!a_http_server || !a_url_path) {
        log_it(L_ERROR, "Invalid parameters for HTTP proc");
        return;
    }
    
    // Delegate to original dap_stream_add_proc_http
    dap_stream_add_proc_http(a_http_server, a_url_path);
    
    log_it(L_INFO, "HTTP stream processor registered for path: %s", a_url_path);
}

/**
 * @brief Add HTTP encryption processor
 */
void dap_stream_trans_http_add_enc_proc(dap_http_server_t *a_http_server,
                                              const char *a_url_path)
{
    if (!a_http_server || !a_url_path) {
        log_it(L_ERROR, "Invalid parameters for HTTP enc proc");
        return;
    }
    
    // Delegate to original enc_http_add_proc
    enc_http_add_proc(a_http_server, a_url_path);
    
    log_it(L_INFO, "HTTP encryption processor registered for path: %s", a_url_path);
}

// ============================================================================
// Translation Functions (HTTP ↔ TLV)
// ============================================================================

/**
 * @brief Translate TLV handshake request to HTTP format
 */
int dap_stream_trans_http_translate_request_to_http(
    const dap_stream_handshake_request_t *a_request,
    uint8_t *a_http_data_out,
    size_t *a_size)
{
    if (!a_request || !a_http_data_out || !a_size) {
        log_it(L_ERROR, "Invalid parameters for HTTP translation");
        return -1;
    }
    
    // Serialize TLV handshake request
    void *l_tlv_data = NULL;
    size_t l_tlv_size = 0;
    int l_ret = dap_stream_handshake_request_create(a_request, &l_tlv_data, &l_tlv_size);
    if (l_ret != 0 || !l_tlv_data) {
        log_it(L_ERROR, "Failed to create TLV handshake request");
        return -2;
    }
    
    // Base64 encode for HTTP trans
    size_t l_encoded_size = DAP_ENC_BASE64_ENCODE_SIZE(l_tlv_size);
    if (l_encoded_size > *a_size) {
        log_it(L_ERROR, "Output buffer too small (%zu needed, %zu available)", 
               l_encoded_size, *a_size);
        DAP_DELETE(l_tlv_data);
        return -3;
    }
    
    size_t l_actual_size = dap_enc_base64_encode(l_tlv_data, l_tlv_size, 
                                                   (char*)a_http_data_out, DAP_ENC_DATA_TYPE_B64);
    DAP_DELETE(l_tlv_data);
    
    if (l_actual_size == 0) {
        log_it(L_ERROR, "Base64 encoding failed");
        return -4;
    }
    
    *a_size = l_actual_size;
    log_it(L_DEBUG, "Translated TLV to HTTP: %zu bytes → %zu base64 bytes", 
           l_tlv_size, l_actual_size);
    return 0;
}

/**
 * @brief Translate HTTP response to TLV format
 */
int dap_stream_trans_http_translate_response_from_http(
    const uint8_t *a_http_data,
    size_t a_size,
    dap_stream_handshake_response_t *a_response_out)
{
    if (!a_http_data || a_size == 0 || !a_response_out) {
        log_it(L_ERROR, "Invalid parameters for HTTP response translation");
        return -1;
    }
    
    // Base64 decode HTTP response
    size_t l_decoded_size = DAP_ENC_BASE64_DECODE_SIZE(a_size);
    uint8_t *l_tlv_data = DAP_NEW_Z_SIZE(uint8_t, l_decoded_size + 1);
    if (!l_tlv_data) {
        log_it(L_CRITICAL, "Failed to allocate decode buffer");
        return -2;
    }
    
    l_decoded_size = dap_enc_base64_decode((const char*)a_http_data, a_size, 
                                            l_tlv_data, DAP_ENC_DATA_TYPE_B64);
    if (l_decoded_size == 0) {
        log_it(L_ERROR, "Base64 decoding failed");
        DAP_DELETE(l_tlv_data);
        return -3;
    }
    
    // Parse TLV handshake response
    dap_stream_handshake_response_t *l_response = NULL;
    int l_ret = dap_stream_handshake_response_parse(l_tlv_data, l_decoded_size, &l_response);
    DAP_DELETE(l_tlv_data);
    
    if (l_ret != 0 || !l_response) {
        log_it(L_ERROR, "Failed to parse TLV handshake response");
        return -4;
    }
    
    // Copy parsed response to output
    memcpy(a_response_out, l_response, sizeof(dap_stream_handshake_response_t));
    
    // Transfer ownership of bob_pub_key (don't free it twice)
    l_response->bob_pub_key = NULL;
    dap_stream_handshake_response_free(l_response);
    
    log_it(L_DEBUG, "Translated HTTP to TLV: %zu base64 bytes → %zu bytes", 
           a_size, l_decoded_size);
    return 0;
}

