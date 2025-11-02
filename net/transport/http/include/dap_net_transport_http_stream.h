/*
 * Authors:
 * Dmitriy Gerasimov ceo@cellframe.net
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

/**
 * @file dap_net_transport_http_stream.h
 * @brief HTTP Transport Adapter for DAP Stream
 * 
 * This module provides backward compatibility by wrapping the existing HTTP
 * implementation into the new Transport Abstraction Layer.
 * 
 * **Purpose:**
 * - Maintain 100% compatibility with legacy HTTP-based clients
 * - Bridge between old HTTP code and new transport architecture
 * - Translate HTTP query parameters to/from TLV handshake protocol
 * - Allow gradual migration from HTTP to alternative transports
 * 
 * **Architecture:**
 * ```
 * Legacy HTTP Code
 *        ↓
 * HTTP Transport Adapter ← You are here
 *        ↓
 * Transport Abstraction Layer
 *        ↓
 * Generic Stream Code
 * ```
 * 
 * **Key Features:**
 * - Zero-copy where possible
 * - Automatic protocol translation (HTTP ↔ TLV)
 * - Session management compatibility
 * - Encryption handshake bridging
 * 
 * @date 2025-10-22
 * @version 1.0.0
 * @ingroup dap_stream_transport
 */

#ifndef DAP_STREAM_TRANSPORT_HTTP_H
#define DAP_STREAM_TRANSPORT_HTTP_H

#include "dap_stream_transport.h"
#include "dap_stream_handshake.h"
#include "dap_http_server.h"
#include "dap_http_client.h"
#include "dap_enc.h"
#include "dap_enc_key.h"

/**
 * @defgroup dap_stream_transport_http HTTP Transport Adapter
 * @ingroup dap_stream_transport
 * @brief HTTP transport adapter for backward compatibility
 * @{
 */

/**
 * @brief HTTP transport private data structure
 * 
 * Contains HTTP-specific connection state and references
 * to legacy HTTP infrastructure.
 */
typedef struct dap_stream_transport_http_private {
    dap_http_client_t *http_client;         ///< HTTP client instance
    dap_http_server_t *http_server;         ///< HTTP server instance (server-side)
    dap_enc_key_t *enc_key;                 ///< Encryption key for this session
    
    // Handshake state
    bool handshake_completed;               ///< Handshake completion flag
    uint8_t *handshake_buffer;              ///< Buffer for handshake data
    size_t handshake_buffer_size;           ///< Size of handshake buffer
    
    // Session parameters from HTTP query string
    dap_enc_key_type_t enc_type;            ///< Encryption algorithm type
    dap_enc_key_type_t pkey_exchange_type;  ///< Public key exchange type
    size_t pkey_exchange_size;              ///< Public key size
    size_t block_key_size;                  ///< Block cipher key size
    int protocol_version;                   ///< DAP protocol version
    size_t sign_count;                      ///< Number of signatures
    
    // Backward compatibility
    void *legacy_context;                   ///< Pointer to legacy HTTP context
} dap_stream_transport_http_private_t;

/**
 * @brief HTTP transport configuration
 */
typedef struct dap_stream_transport_http_config {
    const char *url_path;                   ///< HTTP URL path for stream endpoint
    const char *enc_url_path;               ///< HTTP URL path for encryption endpoint
    uint32_t timeout_ms;                    ///< Connection timeout in milliseconds
    uint32_t keepalive_ms;                  ///< Keepalive interval in milliseconds
    bool enable_compression;                ///< Enable HTTP compression
    bool enable_tls;                        ///< Enable TLS for HTTP
} dap_stream_transport_http_config_t;

// ============================================================================
// Transport Registration
// ============================================================================

/**
 * @brief Register HTTP transport adapter
 * 
 * Registers the HTTP transport implementation with the transport registry.
 * Should be called during system initialization, before any HTTP streams
 * are created.
 * 
 * @return 0 on success, negative error code on failure
 * 
 * @code
 * // During initialization
 * if (dap_stream_transport_http_register() < 0) {
 *     log_it(L_CRITICAL, "Failed to register HTTP transport");
 *     return -1;
 * }
 * @endcode
 */
int dap_net_transport_http_stream_register(void);

/**
 * @brief Unregister HTTP transport adapter
 * 
 * Removes the HTTP transport from the transport registry.
 * Should be called during system shutdown, after all HTTP
 * streams are closed.
 * 
 * @return 0 on success, negative error code on failure
 */
int dap_net_transport_http_stream_unregister(void);

// ============================================================================
// HTTP Server Integration
// ============================================================================

/**
 * @brief Add HTTP stream processor to HTTP server
 * 
 * This is the backward-compatible entry point that replaces the old
 * `dap_stream_add_proc_http()` function. It registers HTTP callbacks
 * but routes them through the new transport layer.
 * 
 * @param[in] a_http_server HTTP server instance
 * @param[in] a_url_path URL path for stream endpoint (e.g., "/stream")
 * 
 * @code
 * dap_http_server_t *server = dap_http_server_new(...);
 * dap_stream_transport_http_add_proc(server, "/stream");
 * @endcode
 */
void dap_stream_transport_http_add_proc(dap_http_server_t *a_http_server,
                                         const char *a_url_path);

/**
 * @brief Add HTTP encryption processor
 * 
 * Registers the encryption handshake endpoint. This is backward-compatible
 * with the old `enc_http_add_proc()` function.
 * 
 * @param[in] a_http_server HTTP server instance
 * @param[in] a_url_path URL path for encryption endpoint (e.g., "/enc")
 */
void dap_stream_transport_http_add_enc_proc(dap_http_server_t *a_http_server,
                                              const char *a_url_path);

// ============================================================================
// Protocol Translation
// ============================================================================

/**
 * @brief Parse HTTP query string to handshake parameters
 * 
 * Extracts encryption parameters from HTTP query string format:
 * "enc_type=X,pkey_exchange_type=Y,pkey_exchange_size=Z,..."
 * 
 * @param[in] a_query_string HTTP query string
 * @param[out] a_params Handshake parameters structure
 * 
 * @return 0 on success, negative error code on failure
 * 
 * Example query string:
 * "enc_type=2,pkey_exchange_type=5,pkey_exchange_size=1184,block_key_size=32"
 */
int dap_stream_transport_http_parse_query_params(
    const char *a_query_string,
    dap_stream_handshake_params_t *a_params
);

/**
 * @brief Convert handshake parameters to HTTP query string
 * 
 * Creates an HTTP query string from handshake parameters for
 * backward compatibility with HTTP clients.
 * 
 * @param[in] a_params Handshake parameters
 * @param[out] a_query_string_out Buffer for query string
 * @param[in] a_buf_size Buffer size
 * 
 * @return Number of bytes written, or negative error code
 */
int dap_stream_transport_http_format_query_params(
    const dap_stream_handshake_params_t *a_params,
    char *a_query_string_out,
    size_t a_buf_size
);

/**
 * @brief Translate TLV handshake request to HTTP format
 * 
 * Converts a generic TLV handshake request into the HTTP-specific
 * format expected by legacy code.
 * 
 * @param[in] a_request TLV handshake request
 * @param[out] a_http_data_out Buffer for HTTP data
 * @param[in,out] a_size Input: buffer size, Output: data size
 * 
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_http_translate_request_to_http(
    const dap_stream_handshake_request_t *a_request,
    uint8_t *a_http_data_out,
    size_t *a_size
);

/**
 * @brief Translate HTTP response to TLV format
 * 
 * Converts HTTP encryption response (JSON format) into
 * a generic TLV handshake response.
 * 
 * @param[in] a_http_data HTTP response data (JSON)
 * @param[in] a_size Size of HTTP data
 * @param[out] a_response_out TLV handshake response
 * 
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_http_translate_response_from_http(
    const uint8_t *a_http_data,
    size_t a_size,
    dap_stream_handshake_response_t *a_response_out
);

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Get default HTTP transport configuration
 * 
 * @return Default configuration structure
 */
dap_stream_transport_http_config_t dap_stream_transport_http_config_default(void);

/**
 * @brief Set HTTP transport configuration
 * 
 * @param[in] a_config Configuration structure
 * 
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_http_set_config(const dap_stream_transport_http_config_t *a_config);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Get HTTP transport instance from stream
 * 
 * @param[in] a_stream Stream instance
 * 
 * @return HTTP transport private data, or NULL if not HTTP transport
 */
dap_stream_transport_http_private_t* dap_stream_transport_http_get_private(
    dap_stream_t *a_stream
);

/**
 * @brief Check if stream is using HTTP transport
 * 
 * @param[in] a_stream Stream instance
 * 
 * @return true if HTTP transport, false otherwise
 */
bool dap_stream_transport_is_http(dap_stream_t *a_stream);

/**
 * @brief Get HTTP client from stream
 * 
 * For backward compatibility with code that needs direct
 * access to HTTP client.
 * 
 * @param[in] a_stream Stream instance
 * 
 * @return HTTP client instance, or NULL
 */
dap_http_client_t* dap_stream_transport_http_get_client(dap_stream_t *a_stream);

/** @} */ // end of dap_stream_transport_http group

#endif // DAP_STREAM_TRANSPORT_HTTP_H

