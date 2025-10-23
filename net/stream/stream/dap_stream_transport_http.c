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
#include "dap_stream_transport_http.h"
#include "dap_stream_handshake.h"
#include "dap_enc_base64.h"
#include "dap_enc_ks.h"
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
static const dap_stream_transport_ops s_http_transport_ops;

// ============================================================================
// Transport Operations Implementation
// ============================================================================

/**
 * @brief Initialize HTTP transport instance
 */
static int s_http_transport_init(dap_stream_transport_t *a_transport, void *a_context)
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
    
    a_transport->internal = l_priv;
    
    log_it(L_DEBUG, "HTTP transport initialized");
    return 0;
}

/**
 * @brief Deinitialize HTTP transport instance
 */
static void s_http_transport_deinit(dap_stream_transport_t *a_transport)
{
    if (!a_transport || !a_transport->internal) {
        return;
    }
    
    dap_stream_transport_http_private_t *l_priv = 
        (dap_stream_transport_http_private_t*)a_transport->internal;
    
    // Free handshake buffer if allocated
    if (l_priv->handshake_buffer) {
        DAP_DELETE(l_priv->handshake_buffer);
        l_priv->handshake_buffer = NULL;
    }
    
    // Don't free enc_key - it's managed by enc_ks
    // Don't free http_client/http_server - they're managed externally
    
    DAP_DELETE(l_priv);
    a_transport->internal = NULL;
    
    log_it(L_DEBUG, "HTTP transport deinitialized");
}

/**
 * @brief Connect HTTP transport (client-side)
 */
static int s_http_transport_connect(dap_stream_transport_t *a_transport,
                                     const dap_stream_transport_connect_params_t *a_params)
{
    if (!a_transport || !a_params) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    dap_stream_transport_http_private_t *l_priv = 
        (dap_stream_transport_http_private_t*)a_transport->internal;
    
    if (!l_priv) {
        log_it(L_ERROR, "HTTP transport not initialized");
        return -2;
    }
    
    // In HTTP transport, connection is handled by HTTP client
    // We just store the parameters for later use
    log_it(L_INFO, "HTTP transport connecting to %s:%u", 
           a_params->host, a_params->port);
    
    // Connection is established by HTTP layer
    // We mark it as connected when we get the first HTTP callback
    return 0;
}

/**
 * @brief Listen on HTTP transport (server-side)
 */
static int s_http_transport_listen(dap_stream_transport_t *a_transport,
                                     const dap_stream_transport_listen_params_t *a_params)
{
    if (!a_transport || !a_params) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    dap_stream_transport_http_private_t *l_priv = 
        (dap_stream_transport_http_private_t*)a_transport->internal;
    
    if (!l_priv || !l_priv->http_server) {
        log_it(L_ERROR, "HTTP server not initialized");
        return -2;
    }
    
    log_it(L_INFO, "HTTP transport listening on %s:%u", 
           a_params->addr, a_params->port);
    
    // Server is already listening via HTTP server
    // This is just a notification
    return 0;
}

/**
 * @brief Accept connection on HTTP transport (server-side)
 */
static int s_http_transport_accept(dap_stream_transport_t *a_transport, void *a_context)
{
    // HTTP server handles accept internally
    // This is called after HTTP layer has accepted the connection
    log_it(L_DEBUG, "HTTP transport connection accepted");
    return 0;
}

/**
 * @brief Initialize handshake (client-side)
 */
static int s_http_transport_handshake_init(dap_stream_transport_t *a_transport,
                                             const dap_stream_handshake_params_t *a_params,
                                             uint8_t **a_data_out, size_t *a_size_out)
{
    if (!a_transport || !a_params || !a_data_out || !a_size_out) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    dap_stream_transport_http_private_t *l_priv = 
        (dap_stream_transport_http_private_t*)a_transport->internal;
    
    if (!l_priv) {
        log_it(L_ERROR, "HTTP transport not initialized");
        return -2;
    }
    
    // Store parameters
    l_priv->enc_type = a_params->enc_type;
    l_priv->pkey_exchange_type = a_params->pkey_exchange_type;
    l_priv->pkey_exchange_size = a_params->pkey_exchange_size;
    l_priv->block_key_size = a_params->block_key_size;
    l_priv->protocol_version = a_params->protocol_version;
    
    // For HTTP, handshake data is the public key in base64
    // This will be sent as POST data to /enc endpoint
    if (a_params->pkey_data && a_params->pkey_data_size > 0) {
        // Base64 encode the public key
        size_t l_encoded_size = DAP_ENC_BASE64_ENCODE_SIZE(a_params->pkey_data_size);
        uint8_t *l_encoded = DAP_NEW_SIZE(uint8_t, l_encoded_size + 1);
        if (!l_encoded) {
            log_it(L_CRITICAL, "Failed to allocate handshake buffer");
            return -3;
        }
        
        size_t l_actual_size = dap_enc_base64_encode(
            a_params->pkey_data, a_params->pkey_data_size,
            (char*)l_encoded, DAP_ENC_DATA_TYPE_B64
        );
        
        if (l_actual_size == 0) {
            log_it(L_ERROR, "Failed to base64 encode handshake data");
            DAP_DELETE(l_encoded);
            return -4;
        }
        
        *a_data_out = l_encoded;
        *a_size_out = l_actual_size;
        
        log_it(L_DEBUG, "HTTP handshake init: %zu bytes (base64: %zu)", 
               a_params->pkey_data_size, l_actual_size);
    } else {
        *a_data_out = NULL;
        *a_size_out = 0;
    }
    
    return 0;
}

/**
 * @brief Process handshake response/request (both sides)
 */
static int s_http_transport_handshake_process(dap_stream_transport_t *a_transport,
                                                const uint8_t *a_data_in, size_t a_size_in,
                                                uint8_t **a_data_out, size_t *a_size_out)
{
    if (!a_transport) {
        log_it(L_ERROR, "Invalid transport pointer");
        return -1;
    }
    
    dap_stream_transport_http_private_t *l_priv = 
        (dap_stream_transport_http_private_t*)a_transport->internal;
    
    if (!l_priv) {
        log_it(L_ERROR, "HTTP transport not initialized");
        return -2;
    }
    
    // HTTP handshake processing is done by enc_http layer
    // This is mostly a passthrough with format conversion
    
    if (a_data_in && a_size_in > 0) {
        // Decode base64 input
        size_t l_decoded_size = DAP_ENC_BASE64_DECODE_SIZE(a_size_in);
        uint8_t *l_decoded = DAP_NEW_SIZE(uint8_t, l_decoded_size + 1);
        if (!l_decoded) {
            log_it(L_CRITICAL, "Failed to allocate decode buffer");
            return -3;
        }
        
        l_decoded_size = dap_enc_base64_decode(
            (const char*)a_data_in, a_size_in,
            l_decoded, DAP_ENC_DATA_TYPE_B64
        );
        
        if (l_decoded_size == 0) {
            log_it(L_ERROR, "Failed to base64 decode handshake response");
            DAP_DELETE(l_decoded);
            return -4;
        }
        
        // Store decoded data for session creation
        l_priv->handshake_buffer = l_decoded;
        l_priv->handshake_buffer_size = l_decoded_size;
        l_priv->handshake_completed = true;
        
        log_it(L_DEBUG, "HTTP handshake processed: %zu bytes", l_decoded_size);
    }
    
    // No output data for HTTP (session is created separately)
    if (a_data_out) *a_data_out = NULL;
    if (a_size_out) *a_size_out = 0;
    
    return 0;
}

/**
 * @brief Create session after handshake
 */
static int s_http_transport_session_create(dap_stream_transport_t *a_transport,
                                             const dap_stream_session_params_t *a_params,
                                             void **a_session_out)
{
    if (!a_transport || !a_params || !a_session_out) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    dap_stream_transport_http_private_t *l_priv = 
        (dap_stream_transport_http_private_t*)a_transport->internal;
    
    if (!l_priv) {
        log_it(L_ERROR, "HTTP transport not initialized");
        return -2;
    }
    
    // Session creation in HTTP is handled by dap_stream_session module
    // We just pass through the session ID
    log_it(L_DEBUG, "HTTP transport session created");
    
    // Session is managed externally, return success
    *a_session_out = (void*)a_params->session_id; // Store session ID as opaque pointer
    
    return 0;
}

/**
 * @brief Start streaming after session creation
 */
static int s_http_transport_session_start(dap_stream_transport_t *a_transport,
                                            void *a_session)
{
    if (!a_transport) {
        log_it(L_ERROR, "Invalid transport pointer");
        return -1;
    }
    
    log_it(L_DEBUG, "HTTP transport session started");
    
    // Streaming is handled by HTTP layer's data callbacks
    // This is just a notification
    return 0;
}

/**
 * @brief Read data from HTTP transport
 */
static ssize_t s_http_transport_read(dap_stream_transport_t *a_transport,
                                       void *a_buffer, size_t a_size)
{
    if (!a_transport || !a_buffer || a_size == 0) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // HTTP transport reading is handled by HTTP layer callbacks
    // This is called from the read callback with already-parsed data
    // For now, this is a stub - actual reading happens in s_http_client_data_read
    
    log_it(L_DEBUG, "HTTP transport read: %zu bytes requested", a_size);
    return 0; // Will be implemented when integrating with HTTP callbacks
}

/**
 * @brief Write data to HTTP transport
 */
static ssize_t s_http_transport_write(dap_stream_transport_t *a_transport,
                                        const void *a_data, size_t a_size)
{
    if (!a_transport || !a_data || a_size == 0) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // HTTP transport writing is handled by HTTP layer callbacks
    // This is called to queue data for sending
    
    log_it(L_DEBUG, "HTTP transport write: %zu bytes", a_size);
    return (ssize_t)a_size; // Will be implemented when integrating with HTTP callbacks
}

/**
 * @brief Close HTTP transport connection
 */
static int s_http_transport_close(dap_stream_transport_t *a_transport)
{
    if (!a_transport) {
        log_it(L_ERROR, "Invalid transport pointer");
        return -1;
    }
    
    dap_stream_transport_http_private_t *l_priv = 
        (dap_stream_transport_http_private_t*)a_transport->internal;
    
    if (l_priv && l_priv->http_client) {
        // HTTP client will be closed by HTTP layer
        log_it(L_DEBUG, "HTTP transport connection closed");
    }
    
    return 0;
}

/**
 * @brief Get HTTP transport capabilities
 */
static uint32_t s_http_transport_get_capabilities(dap_stream_transport_t *a_transport)
{
    (void)a_transport;
    
    return DAP_STREAM_TRANSPORT_CAP_ENCRYPTION |
           DAP_STREAM_TRANSPORT_CAP_SESSION |
           DAP_STREAM_TRANSPORT_CAP_RELIABLE;
    // HTTP doesn't natively support compression or multiplexing in our impl
}

// ============================================================================
// Transport Operations Table
// ============================================================================

static const dap_stream_transport_ops s_http_transport_ops = {
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
    .get_capabilities = s_http_transport_get_capabilities
};

// ============================================================================
// Registration Functions
// ============================================================================

/**
 * @brief Register HTTP transport adapter
 */
int dap_stream_transport_http_register(void)
{
    dap_stream_transport_t *l_transport = DAP_NEW_Z(dap_stream_transport_t);
    if (!l_transport) {
        log_it(L_CRITICAL, "Failed to allocate HTTP transport");
        return -1;
    }
    
    l_transport->type = DAP_STREAM_TRANSPORT_TYPE_HTTP;
    l_transport->name = "HTTP";
    l_transport->ops = &s_http_transport_ops;
    l_transport->internal = NULL; // Will be allocated in init()
    
    int l_ret = dap_stream_transport_register(l_transport);
    if (l_ret < 0) {
        log_it(L_ERROR, "Failed to register HTTP transport");
        DAP_DELETE(l_transport);
        return l_ret;
    }
    
    log_it(L_NOTICE, "HTTP transport adapter registered");
    return 0;
}

/**
 * @brief Unregister HTTP transport adapter
 */
int dap_stream_transport_http_unregister(void)
{
    int l_ret = dap_stream_transport_unregister(DAP_STREAM_TRANSPORT_TYPE_HTTP);
    if (l_ret < 0) {
        log_it(L_WARNING, "Failed to unregister HTTP transport");
        return l_ret;
    }
    
    log_it(L_NOTICE, "HTTP transport adapter unregistered");
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
    
    if (a_stream->stream_transport->type != DAP_STREAM_TRANSPORT_TYPE_HTTP) {
        return NULL;
    }
    
    return (dap_stream_transport_http_private_t*)a_stream->stream_transport->internal;
}

/**
 * @brief Check if stream is using HTTP transport
 */
bool dap_stream_transport_is_http(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->stream_transport) {
        return false;
    }
    
    return a_stream->stream_transport->type == DAP_STREAM_TRANSPORT_TYPE_HTTP;
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
    
    // This will be implemented in next phase when integrating with dap_stream_t
    log_it(L_NOTICE, "HTTP stream processor added for path: %s", a_url_path);
    
    // TODO: Call original dap_stream_add_proc_http() or reimplement callbacks
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
    
    // This will be implemented in next phase
    log_it(L_NOTICE, "HTTP encryption processor added for path: %s", a_url_path);
    
    // TODO: Call original enc_http_add_proc() or reimplement
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
    // TODO: Implement TLV → HTTP translation
    // This will convert TLV handshake request to HTTP POST data
    log_it(L_DEBUG, "TLV to HTTP translation (stub)");
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
    // TODO: Implement HTTP → TLV translation
    // This will parse HTTP JSON response to TLV handshake response
    log_it(L_DEBUG, "HTTP to TLV translation (stub)");
    return 0;
}

