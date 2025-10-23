/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Contributors:
 * Copyright (c) 2017-2025 Demlabs Ltd <https://demlabs.net>
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

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file dap_stream_obfuscation_mimicry.h
 * @brief Protocol Mimicry Module - Make DAP Stream Traffic Look Like Legitimate Protocols
 * 
 * This module implements protocol mimicry techniques to make DAP Stream traffic
 * appear as legitimate protocols (HTTPS, HTTP/2, WebSocket) to DPI systems.
 * 
 * **Mimicry Strategies:**
 * 
 * 1. **HTTPS Mimicry:**
 *    - Wrap packets in TLS record format
 *    - Emulate TLS ClientHello/ServerHello
 *    - Use realistic cipher suites and extensions
 *    - Maintain proper TLS state machine appearance
 * 
 * 2. **HTTP/2 Mimicry:**
 *    - Use HTTP/2 binary framing
 *    - Emulate SETTINGS, HEADERS, DATA frames
 *    - Realistic HPACK header compression
 *    - Flow control window updates
 * 
 * 3. **WebSocket Mimicry:**
 *    - WebSocket frame format (masked/unmasked)
 *    - Proper handshake upgrade sequence
 *    - Ping/pong heartbeats
 *    - Text/binary frame types
 * 
 * **DPI Evasion Techniques:**
 * 
 * - Statistical properties matching target protocol
 * - Packet size distribution emulation
 * - Timing patterns mimicking real browsers/clients
 * - SNI (Server Name Indication) with realistic domains
 * - ALPN (Application-Layer Protocol Negotiation)
 * - Realistic TLS extensions and cipher suites
 * 
 * **Security Considerations:**
 * 
 * - Mimicry must be cryptographically sound (real encryption)
 * - Browser fingerprints must be consistent and realistic
 * - Statistical analysis resistance
 * - Active probing resistance (must respond correctly to probes)
 * 
 * @see dap_stream_obfuscation.h
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Protocol mimicry types
 */
typedef enum dap_stream_mimicry_protocol {
    DAP_STREAM_MIMICRY_NONE        = 0,  ///< No mimicry
    DAP_STREAM_MIMICRY_HTTPS       = 1,  ///< HTTPS (TLS 1.2/1.3)
    DAP_STREAM_MIMICRY_HTTP2       = 2,  ///< HTTP/2
    DAP_STREAM_MIMICRY_WEBSOCKET   = 3,  ///< WebSocket
    DAP_STREAM_MIMICRY_QUIC        = 4   ///< QUIC (UDP-based)
} dap_stream_mimicry_protocol_t;

/**
 * @brief TLS version identifiers
 */
typedef enum dap_stream_tls_version {
    DAP_STREAM_TLS_1_2 = 0x0303,  ///< TLS 1.2
    DAP_STREAM_TLS_1_3 = 0x0304   ///< TLS 1.3
} dap_stream_tls_version_t;

/**
 * @brief Browser fingerprint types
 */
typedef enum dap_stream_browser_type {
    DAP_STREAM_BROWSER_CHROME     = 0,  ///< Google Chrome
    DAP_STREAM_BROWSER_FIREFOX    = 1,  ///< Mozilla Firefox
    DAP_STREAM_BROWSER_SAFARI     = 2,  ///< Apple Safari
    DAP_STREAM_BROWSER_EDGE       = 3   ///< Microsoft Edge
} dap_stream_browser_type_t;

/**
 * @brief TLS record header (5 bytes)
 */
typedef struct dap_stream_tls_record_header {
    uint8_t content_type;     ///< Record content type (0x17 = application data)
    uint16_t version;         ///< TLS version (0x0303 = TLS 1.2)
    uint16_t length;          ///< Payload length (network byte order)
} DAP_ALIGN_PACKED dap_stream_tls_record_header_t;

/**
 * @brief Protocol mimicry configuration
 */
typedef struct dap_stream_mimicry_config {
    dap_stream_mimicry_protocol_t protocol;  ///< Target protocol to mimic
    dap_stream_tls_version_t tls_version;    ///< TLS version (for HTTPS)
    dap_stream_browser_type_t browser;       ///< Browser to emulate
    
    // HTTPS-specific config
    struct {
        const char *sni_hostname;         ///< SNI hostname (e.g. "www.google.com")
        bool use_realistic_cipher_suites; ///< Use browser-like cipher suites
        bool emulate_extensions;          ///< Emulate TLS extensions
        bool add_grease;                  ///< Add GREASE values (anti-fingerprint)
    } https;
    
    // HTTP/2-specific config
    struct {
        uint32_t initial_window_size;     ///< Initial flow control window
        bool use_hpack_compression;       ///< Use HPACK header compression
    } http2;
    
    // WebSocket-specific config
    struct {
        bool mask_client_frames;          ///< Mask client->server frames
        uint32_t ping_interval_ms;        ///< Ping interval
    } websocket;
    
} dap_stream_mimicry_config_t;

/**
 * @brief Protocol mimicry engine instance
 */
typedef struct dap_stream_mimicry dap_stream_mimicry_t;

//=============================================================================
// Public API Functions
//=============================================================================

/**
 * @brief Create protocol mimicry engine with default configuration
 * 
 * Creates a mimicry engine with sensible defaults:
 * - Protocol: HTTPS (TLS 1.3)
 * - Browser: Chrome (most common)
 * - Realistic cipher suites and extensions
 * 
 * @return New mimicry engine instance or NULL on failure
 */
dap_stream_mimicry_t *dap_stream_mimicry_create(void);

/**
 * @brief Create protocol mimicry engine with custom configuration
 * 
 * @param a_config Custom configuration
 * @return New mimicry engine instance or NULL on failure
 */
dap_stream_mimicry_t *dap_stream_mimicry_create_with_config(
    const dap_stream_mimicry_config_t *a_config);

/**
 * @brief Destroy protocol mimicry engine
 * 
 * @param a_mimicry Mimicry engine instance
 */
void dap_stream_mimicry_destroy(dap_stream_mimicry_t *a_mimicry);

/**
 * @brief Wrap data in protocol-specific format
 * 
 * Wraps raw data in the target protocol's format (e.g. TLS record).
 * The output buffer is allocated and must be freed by caller.
 * 
 * @param a_mimicry Mimicry engine instance
 * @param a_data Input data
 * @param a_data_size Input data size
 * @param[out] a_out_data Wrapped output (caller must free with DAP_DELETE)
 * @param[out] a_out_size Wrapped output size
 * @return 0 on success, negative error code on failure
 * 
 * @note For HTTPS: adds TLS record header
 * @note For HTTP/2: adds HTTP/2 frame header
 * @note For WebSocket: adds WebSocket frame header
 */
int dap_stream_mimicry_wrap(dap_stream_mimicry_t *a_mimicry,
                             const void *a_data, size_t a_data_size,
                             void **a_out_data, size_t *a_out_size);

/**
 * @brief Unwrap data from protocol-specific format
 * 
 * Extracts original data from protocol wrapper.
 * The output buffer is allocated and must be freed by caller.
 * 
 * @param a_mimicry Mimicry engine instance
 * @param a_data Wrapped input data
 * @param a_data_size Wrapped input size
 * @param[out] a_out_data Unwrapped output (caller must free with DAP_DELETE)
 * @param[out] a_out_size Unwrapped output size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_mimicry_unwrap(dap_stream_mimicry_t *a_mimicry,
                               const void *a_data, size_t a_data_size,
                               void **a_out_data, size_t *a_out_size);

/**
 * @brief Generate TLS ClientHello message
 * 
 * Generates a realistic TLS ClientHello that mimics the specified browser.
 * Used during connection establishment to appear as legitimate HTTPS.
 * 
 * @param a_mimicry Mimicry engine instance
 * @param[out] a_client_hello ClientHello message (caller must free)
 * @param[out] a_hello_size ClientHello size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_mimicry_generate_client_hello(dap_stream_mimicry_t *a_mimicry,
                                              void **a_client_hello,
                                              size_t *a_hello_size);

/**
 * @brief Generate TLS ServerHello message
 * 
 * Generates a realistic TLS ServerHello response.
 * Used by server to complete HTTPS handshake emulation.
 * 
 * @param a_mimicry Mimicry engine instance
 * @param a_client_hello Client's hello message (for matching)
 * @param a_client_hello_size Client hello size
 * @param[out] a_server_hello ServerHello message (caller must free)
 * @param[out] a_hello_size ServerHello size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_mimicry_generate_server_hello(dap_stream_mimicry_t *a_mimicry,
                                              const void *a_client_hello,
                                              size_t a_client_hello_size,
                                              void **a_server_hello,
                                              size_t *a_hello_size);

/**
 * @brief Set SNI hostname
 * 
 * Sets the Server Name Indication hostname for HTTPS mimicry.
 * Should be a realistic, commonly-used domain.
 * 
 * @param a_mimicry Mimicry engine instance
 * @param a_hostname SNI hostname (e.g. "www.google.com")
 * @return 0 on success, negative error code on failure
 */
int dap_stream_mimicry_set_sni(dap_stream_mimicry_t *a_mimicry,
                                const char *a_hostname);

/**
 * @brief Set target protocol
 * 
 * Changes the target protocol for mimicry.
 * 
 * @param a_mimicry Mimicry engine instance
 * @param a_protocol Target protocol
 * @return 0 on success, negative error code on failure
 */
int dap_stream_mimicry_set_protocol(dap_stream_mimicry_t *a_mimicry,
                                     dap_stream_mimicry_protocol_t a_protocol);

/**
 * @brief Set browser type for fingerprinting
 * 
 * Changes the browser to emulate for fingerprinting.
 * 
 * @param a_mimicry Mimicry engine instance
 * @param a_browser Browser type
 * @return 0 on success, negative error code on failure
 */
int dap_stream_mimicry_set_browser(dap_stream_mimicry_t *a_mimicry,
                                    dap_stream_browser_type_t a_browser);

/**
 * @brief Get current configuration
 * 
 * @param a_mimicry Mimicry engine instance
 * @param[out] a_config Configuration structure to fill
 * @return 0 on success, negative error code on failure
 */
int dap_stream_mimicry_get_config(dap_stream_mimicry_t *a_mimicry,
                                   dap_stream_mimicry_config_t *a_config);

/**
 * @brief Update configuration
 * 
 * @param a_mimicry Mimicry engine instance
 * @param a_config New configuration
 * @return 0 on success, negative error code on failure
 */
int dap_stream_mimicry_set_config(dap_stream_mimicry_t *a_mimicry,
                                   const dap_stream_mimicry_config_t *a_config);

/**
 * @brief Create default configuration for protocol
 * 
 * Helper function to create a configuration with sensible defaults
 * for a specific protocol.
 * 
 * @param a_protocol Target protocol
 * @return Configuration structure
 */
dap_stream_mimicry_config_t dap_stream_mimicry_config_for_protocol(
    dap_stream_mimicry_protocol_t a_protocol);

/**
 * @brief Validate wrapped packet
 * 
 * Validates that a packet matches the expected protocol format.
 * Useful for detecting corrupted/tampered packets.
 * 
 * @param a_mimicry Mimicry engine instance
 * @param a_data Packet data
 * @param a_data_size Packet size
 * @return true if valid, false otherwise
 */
bool dap_stream_mimicry_validate_packet(dap_stream_mimicry_t *a_mimicry,
                                         const void *a_data,
                                         size_t a_data_size);

#ifdef __cplusplus
}
#endif

