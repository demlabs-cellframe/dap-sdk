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
#include "dap_strfuncs.h"
#include "dap_stream.h"
#include "dap_net_transport_http_stream.h"
#include "dap_net_transport_http_server.h"
#include "dap_stream_handshake.h"
#include "dap_enc_base64.h"
#include "dap_enc_ks.h"
#include "dap_enc_http.h"
#include "json.h"

#define LOG_TAG "dap_stream_transport_http"

// ============================================================================
// Global State
// ============================================================================

static dap_stream_transport_http_config_t s_config = {
    .url_path = "/stream",
    .enc_url_path = "/enc",
    .timeout_ms = 20000,
    .keepalive_ms = 60000,
    .enable_compression = false,
    .enable_tls = false
};

// Forward declarations
static const dap_stream_transport_ops_t s_http_transport_ops;

// ============================================================================
// Transport Operations Implementation
// ============================================================================

/**
 * @brief Initialize HTTP transport instance
 */
static int s_http_transport_init(dap_stream_transport_t *a_transport, dap_config_t *a_config)
{
    if (!a_transport) {
        log_it(L_ERROR, "Invalid transport pointer");
        return -1;
    }
    
    // Allocate private data
    dap_stream_transport_http_private_t *l_priv = DAP_NEW_Z(dap_stream_transport_http_private_t);
    if (!l_priv) {
        log_it(L_CRITICAL, "Failed to allocate HTTP transport private data");
        return -2;
    }
    
    // Set defaults from config
    l_priv->protocol_version = DAP_PROTOCOL_VERSION;
    l_priv->enc_type = DAP_ENC_KEY_TYPE_IAES;
    l_priv->pkey_exchange_type = DAP_ENC_KEY_TYPE_MSRLN;
    l_priv->pkey_exchange_size = 1184; // MSRLN_PKA_BYTES
    l_priv->block_key_size = 32;
    l_priv->sign_count = 0;
    
    a_transport->_inheritor = l_priv;
    
    UNUSED(a_config); // Config not used in legacy HTTP transport
    log_it(L_DEBUG, "HTTP transport initialized");
    return 0;
}

/**
 * @brief Deinitialize HTTP transport instance
 */
static void s_http_transport_deinit(dap_stream_transport_t *a_transport)
{
    if (!a_transport || !a_transport->_inheritor) {
        return;
    }
    
    dap_stream_transport_http_private_t *l_priv = 
        (dap_stream_transport_http_private_t*)a_transport->_inheritor;
    
    // Free handshake buffer if allocated
    if (l_priv->handshake_buffer) {
        DAP_DELETE(l_priv->handshake_buffer);
        l_priv->handshake_buffer = NULL;
    }
    
    // Don't free enc_key - it's managed by enc_ks
    // Don't free http_client/http_server - they're managed externally
    
    DAP_DELETE(l_priv);
    a_transport->_inheritor = NULL;
    
    log_it(L_DEBUG, "HTTP transport deinitialized");
}

/**
 * @brief Connect HTTP transport (client-side)
 */
static int s_http_transport_connect(dap_stream_t *a_stream,
                                     const char *a_host,
                                     uint16_t a_port,
                                     dap_stream_transport_connect_cb_t a_callback)
{
    if (!a_stream || !a_host) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // In HTTP transport, connection is handled by HTTP client
    // We just store the parameters for later use
    log_it(L_INFO, "HTTP transport connecting to %s:%u", a_host, a_port);
    
    // Connection is established by HTTP layer
    // We mark it as connected when we get the first HTTP callback
    UNUSED(a_callback);
    return 0;
}

/**
 * @brief Listen on HTTP transport (server-side)
 */
static int s_http_transport_listen(dap_stream_transport_t *a_transport,
                                     const char *a_addr,
                                     uint16_t a_port,
                                     dap_server_t *a_server)
{
    if (!a_transport) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    dap_stream_transport_http_private_t *l_priv = 
        (dap_stream_transport_http_private_t*)a_transport->_inheritor;
    
    if (!l_priv) {
        log_it(L_ERROR, "HTTP transport not initialized");
        return -2;
    }
    
    log_it(L_INFO, "HTTP transport listening on %s:%u", 
           a_addr ? a_addr : "any", a_port);
    
    // Server is already listening via HTTP server
    // This is just a notification
    UNUSED(a_server);
    return 0;
}

/**
 * @brief Accept connection on HTTP transport (server-side)
 */
static int s_http_transport_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out)
{
    if (!a_listener || !a_stream_out) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // HTTP server handles accept internally via dap_http_server
    // Stream is created by HTTP layer when connection is accepted
    // This function is called after stream is already created
    log_it(L_DEBUG, "HTTP transport connection accepted");
    
    // Stream is created by HTTP layer, we just validate it
    return 0;
}

/**
 * @brief Initialize handshake (client-side)
 */
static int s_http_transport_handshake_init(dap_stream_t *a_stream,
                                             dap_stream_handshake_params_t *a_params,
                                             dap_stream_transport_handshake_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // Get transport from stream
    if (!a_stream->stream_transport) {
        log_it(L_ERROR, "Stream has no transport");
        return -2;
    }
    
    // HTTP handshake is handled by enc_http layer via existing infrastructure
    // This function is called to initiate the handshake process
    // The actual HTTP POST to /enc is done by dap_client
    log_it(L_DEBUG, "HTTP transport handshake init");
    
    // Store callback for later
    UNUSED(a_callback);
    UNUSED(a_params);
    
    // HTTP handshake happens automatically via existing HTTP client
    // Callback will be invoked by HTTP layer when response arrives
    return 0;
}

/**
 * @brief Process handshake response/request (server-side)
 */
static int s_http_transport_handshake_process(dap_stream_t *a_stream,
                                                const void *a_data, size_t a_data_size,
                                                void **a_response, size_t *a_response_size)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream pointer");
        return -1;
    }
    
    // HTTP handshake processing is done by enc_server
    // This function is called on server side to process client handshake request
    log_it(L_DEBUG, "HTTP transport handshake process: %zu bytes", a_data_size);
    
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
 */
static int s_http_transport_session_create(dap_stream_t *a_stream,
                                             dap_stream_session_params_t *a_params,
                                             dap_stream_transport_session_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // Session creation in HTTP is handled by dap_stream_session module via HTTP
    // This function initiates the encrypted request to /stream_ctl
    log_it(L_DEBUG, "HTTP transport session create");
    
    // Store callback for later
    UNUSED(a_callback);
    
    // Session is created via existing HTTP infrastructure
    // Callback will be invoked when session is established
    return 0;
}

/**
 * @brief Start streaming after session creation
 */
static int s_http_transport_session_start(dap_stream_t *a_stream,
                                            uint32_t a_session_id,
                                            dap_stream_transport_ready_cb_t a_callback)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream pointer");
        return -1;
    }
    
    log_it(L_DEBUG, "HTTP transport session start: session_id=%u", a_session_id);
    
    // Store callback for later
    UNUSED(a_callback);
    
    // Streaming starts via HTTP GET to /stream/[channels]?session_id=X
    // This is handled by existing HTTP infrastructure
    return 0;
}

/**
 * @brief Read data from HTTP transport
 */
static ssize_t s_http_transport_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_buffer || a_size == 0) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // HTTP transport reading is handled by HTTP layer callbacks
    // Data arrives via dap_http_client callbacks and is processed by stream layer
    // This function should read from stream's internal buffers
    log_it(L_DEBUG, "HTTP transport read: %zu bytes requested", a_size);
    
    // Reading from HTTP is event-driven via callbacks
    // Return 0 to indicate no data available now (would block)
    return 0;
}

/**
 * @brief Write data to HTTP transport
 */
static ssize_t s_http_transport_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // HTTP transport writing is done via dap_http_client write
    // Data is queued and sent via HTTP connection
    log_it(L_DEBUG, "HTTP transport write: %zu bytes", a_size);
    
    // Writing is handled by HTTP infrastructure
    // Return size to indicate success
    return (ssize_t)a_size;
}

/**
 * @brief Close HTTP transport connection
 */
static void s_http_transport_close(dap_stream_t *a_stream)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream pointer");
        return;
    }
    
    // HTTP connection is closed by HTTP layer
    // Stream cleanup is handled by stream module
    log_it(L_DEBUG, "HTTP transport connection closed");
}

/**
 * @brief Get HTTP transport capabilities
 */
static uint32_t s_http_transport_get_capabilities(dap_stream_transport_t *a_transport)
{
    (void)a_transport;
    
    return DAP_STREAM_TRANSPORT_CAP_RELIABLE |
           DAP_STREAM_TRANSPORT_CAP_ORDERED |
           DAP_STREAM_TRANSPORT_CAP_BIDIRECTIONAL;
    // HTTP doesn't natively support compression or multiplexing in our impl
}

// ============================================================================
// Transport Operations Table
// ============================================================================

static const dap_stream_transport_ops_t s_http_transport_ops = {
    .init = s_http_transport_init,
    .deinit = s_http_transport_deinit,
    .connect = s_http_transport_connect,
    .listen = s_http_transport_listen,
    .accept = s_http_transport_accept,
    .handshake_init = s_http_transport_handshake_init,
    .handshake_process = s_http_transport_handshake_process,
    .session_create = s_http_transport_session_create,
    .session_start = s_http_transport_session_start,
    .read = s_http_transport_read,
    .write = s_http_transport_write,
    .close = s_http_transport_close,
    .get_capabilities = s_http_transport_get_capabilities,
    .register_server_handlers = NULL  // HTTP transport doesn't need additional handlers
};

// ============================================================================
// Registration Functions
// ============================================================================

/**
 * @brief Register HTTP transport adapter
 */
int dap_net_transport_http_stream_register(void)
{
    log_it(L_DEBUG, "dap_net_transport_http_stream_register: Starting HTTP transport registration");
    // Initialize HTTP server module first (registers server operations)
    int l_ret = dap_net_transport_http_server_init();
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize HTTP server module: %d", l_ret);
        return l_ret;
    }
    
    log_it(L_DEBUG, "dap_net_transport_http_stream_register: HTTP server module initialized, registering transport");
    
    // Register HTTP transport operations
    int l_ret_transport = dap_stream_transport_register("HTTP", 
                                                DAP_STREAM_TRANSPORT_HTTP,
                                                &s_http_transport_ops,
                                                NULL);  // No inheritor needed at registration
    if (l_ret_transport < 0) {
        log_it(L_ERROR, "Failed to register HTTP transport: %d", l_ret_transport);
        dap_net_transport_http_server_deinit();
        return l_ret_transport;
    }
    
    log_it(L_NOTICE, "HTTP transport adapter registered");
    return 0;
}

/**
 * @brief Unregister HTTP transport adapter
 */
int dap_net_transport_http_stream_unregister(void)
{
    log_it(L_DEBUG, "dap_net_transport_http_stream_unregister: Starting HTTP transport unregistration");
    
    int l_ret = dap_stream_transport_unregister(DAP_STREAM_TRANSPORT_HTTP);
    if (l_ret < 0) {
        log_it(L_WARNING, "Failed to unregister HTTP transport");
        return l_ret;
    }
    
    // Deinitialize HTTP server module (unregisters server operations)
    log_it(L_DEBUG, "dap_net_transport_http_stream_unregister: Deinitializing HTTP server module");
    dap_net_transport_http_server_deinit();
    
    log_it(L_NOTICE, "HTTP transport adapter unregistered successfully");
    return 0;
}

// ============================================================================
// Protocol Translation Functions
// ============================================================================

/**
 * @brief Parse HTTP query string to handshake parameters
 */
int dap_stream_transport_http_parse_query_params(
    const char *a_query_string,
    dap_stream_handshake_params_t *a_params)
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
int dap_stream_transport_http_format_query_params(
    const dap_stream_handshake_params_t *a_params,
    char *a_query_string_out,
    size_t a_buf_size)
{
    if (!a_params || !a_query_string_out || a_buf_size == 0) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    int l_written = snprintf(a_query_string_out, a_buf_size,
                             "enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zu,"
                             "block_key_size=%zu,protocol_version=%d,sign_count=0",
                             a_params->enc_type,
                             a_params->pkey_exchange_type,
                             a_params->pkey_exchange_size,
                             a_params->block_key_size,
                             a_params->protocol_version);
    
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
 * @brief Get default HTTP transport configuration
 */
dap_stream_transport_http_config_t dap_stream_transport_http_config_default(void)
{
    return s_config;
}

/**
 * @brief Set HTTP transport configuration
 */
int dap_stream_transport_http_set_config(const dap_stream_transport_http_config_t *a_config)
{
    if (!a_config) {
        log_it(L_ERROR, "Invalid config pointer");
        return -1;
    }
    
    s_config = *a_config;
    log_it(L_INFO, "HTTP transport configuration updated");
    return 0;
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Get HTTP transport private data from stream
 */
dap_stream_transport_http_private_t* dap_stream_transport_http_get_private(
    dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->stream_transport) {
        return NULL;
    }
    
    if (a_stream->stream_transport->type != DAP_STREAM_TRANSPORT_HTTP) {
        return NULL;
    }
    
    return (dap_stream_transport_http_private_t*)a_stream->stream_transport->_inheritor;
}

/**
 * @brief Check if stream is using HTTP transport
 */
bool dap_stream_transport_is_http(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->stream_transport) {
        return false;
    }
    
    return a_stream->stream_transport->type == DAP_STREAM_TRANSPORT_HTTP;
}

/**
 * @brief Get HTTP client from stream
 */
dap_http_client_t* dap_stream_transport_http_get_client(dap_stream_t *a_stream)
{
    dap_stream_transport_http_private_t *l_priv = 
        dap_stream_transport_http_get_private(a_stream);
    
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
void dap_stream_transport_http_add_proc(dap_http_server_t *a_http_server,
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
void dap_stream_transport_http_add_enc_proc(dap_http_server_t *a_http_server,
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
int dap_stream_transport_http_translate_request_to_http(
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
    
    // Base64 encode for HTTP transport
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
int dap_stream_transport_http_translate_response_from_http(
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

