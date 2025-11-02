/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file dap_stream_transport_tls.h
 * @brief Direct TLS Transport Adapter for DAP Stream Protocol
 * 
 * This module implements a direct TLS 1.3 transport layer for DAP Stream,
 * providing encrypted communication without HTTP overhead. This transport
 * looks like standard TLS traffic to DPI systems.
 * 
 * **Features:**
 * - TLS 1.3 with modern cipher suites
 * - Direct TCP+TLS connection (no HTTP overhead)
 * - Certificate-based authentication (optional)
 * - Session resumption (0-RTT)
 * - SNI (Server Name Indication) for domain fronting
 * - ALPN (Application-Layer Protocol Negotiation) for protocol detection
 * - Perfect Forward Secrecy (PFS)
 * 
 * **Use Cases:**
 * - High-performance encrypted communication
 * - Low-latency connections (no HTTP overhead)
 * - DPI evasion (looks like standard HTTPS/TLS)
 * - Domain fronting capabilities
 * - Certificate pinning for enhanced security
 * 
 * **Security Features:**
 * - TLS 1.3 only (no TLS 1.2/1.1/1.0)
 * - Strong cipher suites (AEAD only)
 * - Certificate verification
 * - Hostname validation
 * - OCSP stapling support
 * - Session ticket encryption
 * 
 * **Architecture:**
 * ```
 * Application
 *     ↓
 * DAP Stream
 *     ↓
 * Transport Abstraction Layer
 *     ↓
 * TLS Direct Transport ← This module
 *     ↓
 * OpenSSL/WolfSSL (TLS 1.3)
 *     ↓
 * TCP Socket (dap_events_socket_t)
 *     ↓
 * Network (TCP/IP)
 * ```
 * 
 * **TLS Record Format:**
 * ```
 *  0                   1                   2
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Content Type  |   Version (0x0303 for TLS 1.2)|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           Length (16-bit)                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           Encrypted Data ...                  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ```
 * 
 * @see dap_stream_transport.h
 * @see RFC 8446 - The Transport Layer Security (TLS) Protocol Version 1.3
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dap_stream_transport.h"
#include "dap_events_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// TLS Configuration Constants
// ============================================================================

/// TLS protocol versions
typedef enum dap_tls_version {
    DAP_TLS_VERSION_1_2 = 0x0303,  ///< TLS 1.2 (for compatibility)
    DAP_TLS_VERSION_1_3 = 0x0304   ///< TLS 1.3 (preferred)
} dap_tls_version_t;

/// TLS authentication modes
typedef enum dap_tls_auth_mode {
    DAP_TLS_AUTH_NONE           = 0,  ///< No certificate verification (insecure!)
    DAP_TLS_AUTH_OPTIONAL       = 1,  ///< Certificate verification optional
    DAP_TLS_AUTH_REQUIRED       = 2   ///< Certificate verification required
} dap_tls_auth_mode_t;

/// Recommended cipher suites (TLS 1.3)
#define DAP_TLS_CIPHER_SUITES \
    "TLS_AES_256_GCM_SHA384:" \
    "TLS_CHACHA20_POLY1305_SHA256:" \
    "TLS_AES_128_GCM_SHA256"

// ============================================================================
// Configuration Structures
// ============================================================================

/**
 * @brief TLS transport configuration
 */
typedef struct dap_stream_transport_tls_config {
    dap_tls_version_t min_version;       ///< Minimum TLS version
    dap_tls_version_t max_version;       ///< Maximum TLS version
    dap_tls_auth_mode_t auth_mode;       ///< Certificate authentication mode
    
    // Certificate files
    char *cert_file;                     ///< Server certificate file path (PEM)
    char *key_file;                      ///< Server private key file path (PEM)
    char *ca_file;                       ///< CA certificate file path (PEM)
    char *ca_path;                       ///< CA certificate directory path
    
    // Certificate pinning
    bool enable_cert_pinning;            ///< Enable certificate pinning
    uint8_t *pinned_cert_hash;           ///< SHA256 hash of pinned certificate
    size_t pinned_cert_hash_size;        ///< Hash size (should be 32 for SHA256)
    
    // SNI and ALPN
    char *sni_hostname;                  ///< SNI hostname (for domain fronting)
    char *alpn_protocols;                ///< ALPN protocols (comma-separated, e.g., "dap-stream,h2,http/1.1")
    
    // Session resumption
    bool enable_session_tickets;         ///< Enable TLS session tickets (0-RTT)
    uint32_t session_lifetime_sec;       ///< Session ticket lifetime (seconds)
    
    // Cipher suites
    char *cipher_suites;                 ///< TLS 1.3 cipher suites (colon-separated)
    char *cipher_list;                   ///< TLS 1.2 cipher list (for compat)
    
    // Timeouts
    uint32_t handshake_timeout_ms;       ///< TLS handshake timeout (milliseconds)
    uint32_t renegotiation_timeout_ms;   ///< Renegotiation timeout (milliseconds)
    
    // Security options
    bool verify_hostname;                ///< Enable hostname verification
    bool enable_ocsp_stapling;           ///< Enable OCSP stapling
    bool allow_insecure_ciphers;         ///< Allow weak ciphers (NOT RECOMMENDED)
} dap_stream_transport_tls_config_t;

/**
 * @brief TLS connection state
 */
typedef enum dap_tls_state {
    DAP_TLS_STATE_DISCONNECTED  = 0,  ///< No connection
    DAP_TLS_STATE_CONNECTING    = 1,  ///< TCP connection in progress
    DAP_TLS_STATE_HANDSHAKING   = 2,  ///< TLS handshake in progress
    DAP_TLS_STATE_CONNECTED     = 3,  ///< TLS connection established
    DAP_TLS_STATE_SHUTDOWN      = 4   ///< Shutdown in progress
} dap_tls_state_t;

/**
 * @brief TLS transport private data
 */
typedef struct dap_stream_transport_tls_private {
    dap_stream_transport_tls_config_t config;  ///< Configuration
    dap_tls_state_t state;                     ///< Connection state
    
    // TLS context (OpenSSL SSL_CTX or WolfSSL WOLFSSL_CTX)
    void *tls_ctx;                             ///< TLS context
    void *tls_session;                         ///< TLS session (SSL or WOLFSSL)
    
    // Connection info
    char *peer_hostname;                       ///< Peer hostname (for SNI/verification)
    uint16_t peer_port;                        ///< Peer port
    
    // Certificate info
    void *peer_cert;                           ///< Peer certificate
    bool cert_verified;                        ///< Certificate verification result
    
    // Session resumption
    void *session_ticket;                      ///< Session ticket data
    size_t session_ticket_size;                ///< Session ticket size
    
    // Events socket
    dap_events_socket_t *esocket;              ///< Underlying events socket
    
    // Statistics
    uint64_t bytes_sent;                       ///< Total bytes sent
    uint64_t bytes_received;                   ///< Total bytes received
    uint64_t handshakes_completed;             ///< Total handshakes completed
    uint64_t session_resumptions;              ///< Total session resumptions (0-RTT)
} dap_stream_transport_tls_private_t;

// ============================================================================
// Registration Functions
// ============================================================================

/**
 * @brief Register TLS Direct transport adapter
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_tls_register(void);

/**
 * @brief Unregister TLS Direct transport adapter
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_tls_unregister(void);

// ============================================================================
// Configuration Functions
// ============================================================================

/**
 * @brief Get default TLS transport configuration
 * @return Default configuration structure
 */
dap_stream_transport_tls_config_t dap_stream_transport_tls_config_default(void);

/**
 * @brief Set TLS transport configuration
 * @param a_transport TLS transport instance
 * @param a_config Configuration structure
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_tls_set_config(dap_stream_transport_t *a_transport,
                                         const dap_stream_transport_tls_config_t *a_config);

/**
 * @brief Get TLS transport configuration
 * @param a_transport TLS transport instance
 * @param a_config Output configuration structure
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_tls_get_config(dap_stream_transport_t *a_transport,
                                         dap_stream_transport_tls_config_t *a_config);

// ============================================================================
// Certificate Management
// ============================================================================

/**
 * @brief Load server certificate and key
 * @param a_transport TLS transport instance
 * @param a_cert_file Certificate file path (PEM format)
 * @param a_key_file Private key file path (PEM format)
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_tls_load_cert(dap_stream_transport_t *a_transport,
                                        const char *a_cert_file,
                                        const char *a_key_file);

/**
 * @brief Load CA certificates for verification
 * @param a_transport TLS transport instance
 * @param a_ca_file CA certificate file path (PEM format)
 * @param a_ca_path CA certificate directory path
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_tls_load_ca(dap_stream_transport_t *a_transport,
                                      const char *a_ca_file,
                                      const char *a_ca_path);

/**
 * @brief Pin certificate by SHA256 hash
 * @param a_transport TLS transport instance
 * @param a_cert_hash SHA256 hash of certificate (32 bytes)
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_tls_pin_cert(dap_stream_transport_t *a_transport,
                                       const uint8_t *a_cert_hash);

// ============================================================================
// Session Management
// ============================================================================

/**
 * @brief Save TLS session for resumption
 * @param a_stream Stream instance
 * @param a_session_data Output: session data (caller must free)
 * @param a_session_size Output: session data size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_tls_save_session(const dap_stream_t *a_stream,
                                           void **a_session_data,
                                           size_t *a_session_size);

/**
 * @brief Restore TLS session for 0-RTT resumption
 * @param a_stream Stream instance
 * @param a_session_data Session data
 * @param a_session_size Session data size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_tls_restore_session(dap_stream_t *a_stream,
                                              const void *a_session_data,
                                              size_t a_session_size);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Check if stream is using TLS transport
 * @param a_stream Stream to check
 * @return true if TLS transport, false otherwise
 */
bool dap_stream_transport_is_tls(const dap_stream_t *a_stream);

/**
 * @brief Get TLS private data from stream
 * @param a_stream Stream instance
 * @return TLS private data or NULL
 */
dap_stream_transport_tls_private_t* dap_stream_transport_tls_get_private(dap_stream_t *a_stream);

/**
 * @brief Get TLS connection info
 * @param a_stream Stream instance
 * @param a_version Output: TLS version negotiated
 * @param a_cipher Output: Cipher suite name (buffer must be >= 256 bytes)
 * @param a_cipher_size Cipher buffer size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_tls_get_info(const dap_stream_t *a_stream,
                                       dap_tls_version_t *a_version,
                                       char *a_cipher,
                                       size_t a_cipher_size);

/**
 * @brief Get peer certificate info
 * @param a_stream Stream instance
 * @param a_subject Output: certificate subject (buffer must be >= 256 bytes)
 * @param a_subject_size Subject buffer size
 * @param a_issuer Output: certificate issuer (buffer must be >= 256 bytes)
 * @param a_issuer_size Issuer buffer size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_tls_get_peer_cert(const dap_stream_t *a_stream,
                                            char *a_subject, size_t a_subject_size,
                                            char *a_issuer, size_t a_issuer_size);

/**
 * @brief Get TLS statistics
 * @param a_stream Stream instance
 * @param a_bytes_sent Output: bytes sent
 * @param a_bytes_received Output: bytes received
 * @param a_handshakes Output: handshakes completed
 * @param a_resumptions Output: session resumptions (0-RTT)
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_tls_get_stats(const dap_stream_t *a_stream,
                                        uint64_t *a_bytes_sent,
                                        uint64_t *a_bytes_received,
                                        uint64_t *a_handshakes,
                                        uint64_t *a_resumptions);

#ifdef __cplusplus
}
#endif

