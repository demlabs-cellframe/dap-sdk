/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net
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
 * @file dap_stream_transport.h
 * @brief Transport Abstraction Layer for DAP Stream
 * 
 * This file defines a pluggable transport abstraction system that allows
 * DAP Stream to operate over multiple network transports (HTTP, UDP, WebSocket, etc.)
 * with optional traffic obfuscation for DPI bypass.
 * 
 * Design Goals:
 * - Transport-agnostic stream protocol
 * - Pluggable transport implementations
 * - Optional obfuscation engine integration
 * - Backward compatibility with existing HTTP-based protocol
 * - Support for multiple simultaneous transports
 * 
 * Architecture:
 * Level 4: Applications (VPN, Services, Channels)
 *          ↓
 * Level 3: DAP Stream Protocol (dap_stream_t)
 *          ↓
 * Level 2: Transport Abstraction Layer (THIS FILE)
 *          ├─ dap_stream_transport_t
 *          ├─ dap_stream_transport_ops
 *          └─ Obfuscation Engine Hook
 *          ↓
 * Level 1: Network Transports (HTTP, UDP, WebSocket, TLS, DNS, Obfs4)
 * 
 * @date 2025-10-23
 * @author Cellframe Team
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "uthash.h"
#include "dap_common.h"
#include "dap_config.h"
#include "dap_enc_key.h"

// Forward declarations
typedef struct dap_stream dap_stream_t;
typedef struct dap_stream_obfuscation dap_stream_obfuscation_t;
typedef struct dap_server dap_server_t;
typedef struct dap_events_socket dap_events_socket_t;
typedef struct dap_cert dap_cert_t;
typedef struct dap_stream_session dap_stream_session_t;

/**
 * @brief Transport type enumeration
 * 
 * Defines all supported transport protocols. Each transport has unique
 * characteristics and capabilities.
 */
typedef enum dap_stream_transport_type {
    DAP_STREAM_TRANSPORT_HTTP           = 0x01,  ///< HTTP/HTTPS (current default, legacy)
    DAP_STREAM_TRANSPORT_UDP_BASIC      = 0x02,  ///< Basic UDP (unreliable, low latency)
    DAP_STREAM_TRANSPORT_UDP_RELIABLE   = 0x03,  ///< UDP with ARQ (reliable, ordered)
    DAP_STREAM_TRANSPORT_UDP_QUIC_LIKE  = 0x04,  ///< QUIC-inspired multiplexed UDP
    DAP_STREAM_TRANSPORT_WEBSOCKET      = 0x05,  ///< WebSocket (HTTP upgrade)
    DAP_STREAM_TRANSPORT_TLS_DIRECT     = 0x06,  ///< Direct TLS connection
    DAP_STREAM_TRANSPORT_DNS_TUNNEL     = 0x07,  ///< DNS-based tunneling
    DAP_STREAM_TRANSPORT_OBFS4          = 0x08   ///< Tor-style obfuscation (obfs4)
} dap_stream_transport_type_t;

/**
 * @brief Transport capability flags
 * 
 * Bitfield flags indicating transport features and characteristics.
 * Used for runtime capability detection and transport selection.
 */
typedef enum dap_stream_transport_cap {
    DAP_STREAM_TRANSPORT_CAP_RELIABLE         = 0x0001,  ///< Guaranteed delivery
    DAP_STREAM_TRANSPORT_CAP_ORDERED          = 0x0002,  ///< In-order delivery
    DAP_STREAM_TRANSPORT_CAP_OBFUSCATION      = 0x0004,  ///< Built-in obfuscation
    DAP_STREAM_TRANSPORT_CAP_PADDING          = 0x0008,  ///< Random padding support
    DAP_STREAM_TRANSPORT_CAP_MIMICRY          = 0x0010,  ///< Protocol mimicry
    DAP_STREAM_TRANSPORT_CAP_MULTIPLEXING     = 0x0020,  ///< Multiple streams per connection
    DAP_STREAM_TRANSPORT_CAP_BIDIRECTIONAL    = 0x0040,  ///< Full duplex
    DAP_STREAM_TRANSPORT_CAP_LOW_LATENCY      = 0x0080,  ///< Optimized for latency
    DAP_STREAM_TRANSPORT_CAP_HIGH_THROUGHPUT  = 0x0100   ///< Optimized for bandwidth
} dap_stream_transport_cap_t;

// Forward declaration of transport structure
typedef struct dap_stream_transport dap_stream_transport_t;

/**
 * @brief Callback invoked when connection completes
 * @param a_stream Stream that was connected
 * @param a_error_code 0 on success, negative error code on failure
 */
typedef void (*dap_stream_transport_connect_cb_t)(dap_stream_t *a_stream, int a_error_code);

/**
 * @brief Callback invoked when handshake completes
 * @param a_stream Stream that completed handshake
 * @param a_response Server response data (may be NULL)
 * @param a_response_size Size of response data in bytes
 * @param a_error_code 0 on success, negative error code on failure
 */
typedef void (*dap_stream_transport_handshake_cb_t)(dap_stream_t *a_stream, 
                                                     const void *a_response, 
                                                     size_t a_response_size, 
                                                     int a_error_code);

/**
 * @brief Callback invoked when session is created
 * @param a_stream Stream with new session
 * @param a_session_id Assigned session identifier
 * @param a_error_code 0 on success, negative error code on failure
 */
typedef void (*dap_stream_transport_session_cb_t)(dap_stream_t *a_stream, 
                                                   uint32_t a_session_id, 
                                                   int a_error_code);

/**
 * @brief Callback invoked when stream is ready for data transfer
 * @param a_stream Stream that is ready
 * @param a_error_code 0 on success, negative error code on failure
 */
typedef void (*dap_stream_transport_ready_cb_t)(dap_stream_t *a_stream, int a_error_code);

/**
 * @brief Parameters for handshake initialization
 * 
 * Contains all parameters needed to initiate encryption handshake.
 * Used by transport->ops->handshake_init().
 */
typedef struct dap_stream_handshake_params {
    dap_enc_key_type_t enc_type;          ///< Symmetric encryption algorithm
    dap_enc_key_type_t pkey_exchange_type;///< Public key exchange algorithm
    size_t pkey_exchange_size;             ///< Public key size in bytes
    size_t block_key_size;                 ///< Symmetric key block size
    uint32_t protocol_version;             ///< DAP protocol version
    dap_cert_t *auth_cert;                ///< Optional authentication certificate
    uint8_t *alice_pub_key;               ///< Client public key (allocated by caller)
    size_t alice_pub_key_size;            ///< Public key size
} dap_stream_handshake_params_t;

/**
 * @brief Parameters for session creation
 * 
 * Contains parameters for creating a streaming session with specified channels.
 * Used by transport->ops->session_create().
 */
typedef struct dap_stream_session_params {
    const char *channels;                  ///< Active channel IDs (e.g., "C,F,N")
    dap_enc_key_type_t enc_type;          ///< Stream encryption type
    size_t enc_key_size;                   ///< Encryption key size
    bool enc_headers;                      ///< Encrypt packet headers flag
    uint32_t protocol_version;             ///< Protocol version
} dap_stream_session_params_t;

/**
 * @brief Transport operations interface
 * 
 * This is the core abstraction interface. Each transport implementation
 * provides a vtable of these operations. All operations use async callbacks
 * for non-blocking I/O.
 */
typedef struct dap_stream_transport_ops {
    /**
     * @brief Initialize transport-specific resources
     * @param a_transport Transport instance to initialize
     * @param a_config Configuration parameters
     * @return 0 on success, negative error code on failure
     * @note Called once during transport registration
     */
    int (*init)(dap_stream_transport_t *a_transport, dap_config_t *a_config);

    /**
     * @brief Cleanup transport-specific resources
     * @param a_transport Transport instance to deinitialize
     * @note Called during shutdown or transport unregistration
     */
    void (*deinit)(dap_stream_transport_t *a_transport);

    /**
     * @brief Establish connection to remote host
     * @param a_stream Stream to connect
     * @param a_host Hostname or IP address
     * @param a_port Port number
     * @param a_callback Callback invoked when connection completes
     * @return 0 on success (async), negative error code on immediate failure
     * @note Callback invoked in worker thread context
     */
    int (*connect)(dap_stream_t *a_stream, 
                   const char *a_host, 
                   uint16_t a_port, 
                   dap_stream_transport_connect_cb_t a_callback);

    /**
     * @brief Start listening for incoming connections (server-side)
     * @param a_transport Transport instance
     * @param a_addr Address to bind (NULL = any)
     * @param a_port Port to listen on
     * @param a_server Server instance
     * @return 0 on success, negative error code on failure
     */
    int (*listen)(dap_stream_transport_t *a_transport, 
                  const char *a_addr, 
                  uint16_t a_port, 
                  dap_server_t *a_server);

    /**
     * @brief Accept incoming connection (server-side)
     * @param a_listener Listener socket
     * @param a_stream_out Output parameter for new stream
     * @return 0 on success with new stream, negative error code on failure
     * @note Called from listener socket callback
     */
    int (*accept)(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out);

    /**
     * @brief Initiate transport-specific handshake (client-side)
     * @param a_stream Stream to perform handshake on
     * @param a_params Handshake parameters (encryption settings, keys)
     * @param a_callback Callback invoked when handshake completes
     * @return 0 on success (async), negative error code on failure
     * @note Replaces HTTP POST /enc/gd4y5yh78w42aaagh
     */
    int (*handshake_init)(dap_stream_t *a_stream, 
                          dap_stream_handshake_params_t *a_params, 
                          dap_stream_transport_handshake_cb_t a_callback);

    /**
     * @brief Process handshake request (server-side)
     * @param a_stream Stream receiving handshake
     * @param a_data Handshake request data
     * @param a_data_size Size of request data
     * @param a_response Output parameter for response data (caller must free)
     * @param a_response_size Output parameter for response size
     * @return 0 on success with response data, negative error code on failure
     * @note Server processes client handshake_init request
     */
    int (*handshake_process)(dap_stream_t *a_stream, 
                             const void *a_data, 
                             size_t a_data_size, 
                             void **a_response, 
                             size_t *a_response_size);

    /**
     * @brief Create stream session with specified channels (client-side)
     * @param a_stream Stream to create session on
     * @param a_params Session parameters (channels, encryption)
     * @param a_callback Callback invoked when session created
     * @return 0 on success (async), negative error code on failure
     * @note Replaces HTTP encrypted request to /stream_ctl
     */
    int (*session_create)(dap_stream_t *a_stream, 
                          dap_stream_session_params_t *a_params, 
                          dap_stream_transport_session_cb_t a_callback);

    /**
     * @brief Start streaming with session ID
     * @param a_stream Stream to start
     * @param a_session_id Session identifier
     * @param a_callback Callback invoked when stream ready
     * @return 0 on success (async), negative error code on failure
     * @note Replaces HTTP GET /stream/globaldb?session_id=X
     */
    int (*session_start)(dap_stream_t *a_stream, 
                         uint32_t a_session_id, 
                         dap_stream_transport_ready_cb_t a_callback);

    /**
     * @brief Read data from transport
     * @param a_stream Stream to read from
     * @param a_buffer Buffer to read into
     * @param a_size Maximum bytes to read
     * @return Bytes read (>0), 0 on EOF, negative error code on failure
     * @note May return partial read, caller handles reassembly
     */
    ssize_t (*read)(dap_stream_t *a_stream, void *a_buffer, size_t a_size);

    /**
     * @brief Write data to transport
     * @param a_stream Stream to write to
     * @param a_data Data to write
     * @param a_size Number of bytes to write
     * @return Bytes written (>0), negative error code on failure
     * @note May return partial write, caller handles buffering
     */
    ssize_t (*write)(dap_stream_t *a_stream, const void *a_data, size_t a_size);

    /**
     * @brief Close stream connection
     * @param a_stream Stream to close
     * @note Cleanup transport-specific stream data
     */
    void (*close)(dap_stream_t *a_stream);

    /**
     * @brief Query transport capabilities
     * @param a_transport Transport instance
     * @return Bitfield of DAP_STREAM_TRANSPORT_CAP_* flags
     */
    uint32_t (*get_capabilities)(dap_stream_transport_t *a_transport);
} dap_stream_transport_ops_t;

/**
 * @brief Transport instance structure
 * 
 * Represents a registered transport implementation. Multiple transports
 * can be registered simultaneously. Stored in global registry hash table.
 */
struct dap_stream_transport {
    dap_stream_transport_type_t type;      ///< Transport type enum
    const dap_stream_transport_ops_t *ops; ///< Operations table (vtable)
    void *_inheritor;                      ///< Transport-specific private data
    dap_stream_obfuscation_t *obfuscation; ///< Optional obfuscation engine
    uint32_t capabilities;                 ///< Capability flags cache
    char name[64];                         ///< Human-readable transport name
    UT_hash_handle hh;                     ///< Hash table handle (keyed by type)
};

/**
 * @brief Initialize transport abstraction system
 * 
 * Must be called before any transport operations. Initializes global
 * transport registry and default transports.
 * 
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_init(void);

/**
 * @brief Cleanup transport abstraction system
 * 
 * Unregisters all transports and cleans up global state.
 */
void dap_stream_transport_deinit(void);

/**
 * @brief Register a new transport implementation
 * 
 * Adds transport to global registry. Multiple transports can be registered.
 * 
 * @param a_name Human-readable transport name (max 63 chars)
 * @param a_type Transport type identifier
 * @param a_ops Operations table (must remain valid)
 * @param a_inheritor Transport-specific private data (optional)
 * @return 0 on success, negative error code on failure
 * @note Fails if transport type already registered
 */
int dap_stream_transport_register(const char *a_name, 
                                    dap_stream_transport_type_t a_type, 
                                    const dap_stream_transport_ops_t *a_ops, 
                                    void *a_inheritor);

/**
 * @brief Unregister a transport implementation
 * 
 * Removes transport from global registry. Calls deinit() if provided.
 * 
 * @param a_type Transport type to unregister
 * @return 0 on success, negative error code if not found
 */
int dap_stream_transport_unregister(dap_stream_transport_type_t a_type);

/**
 * @brief Find registered transport by type
 * 
 * @param a_type Transport type to find
 * @return Transport instance or NULL if not found
 */
dap_stream_transport_t *dap_stream_transport_find(dap_stream_transport_type_t a_type);

/**
 * @brief Find registered transport by name
 * 
 * @param a_name Transport name to find
 * @return Transport instance or NULL if not found
 */
dap_stream_transport_t *dap_stream_transport_find_by_name(const char *a_name);

/**
 * @brief Get list of all registered transports
 * 
 * @return Linked list of dap_stream_transport_t* (caller must free list, not contents)
 */
dap_list_t *dap_stream_transport_list_all(void);

/**
 * @brief Attach obfuscation engine to transport
 * 
 * Enables traffic obfuscation for the specified transport. Obfuscation
 * engine will be invoked for all I/O operations.
 * 
 * @param a_transport Transport to attach obfuscation to
 * @param a_obfuscation Obfuscation engine instance
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_attach_obfuscation(dap_stream_transport_t *a_transport, 
                                              dap_stream_obfuscation_t *a_obfuscation);

/**
 * @brief Detach obfuscation engine from transport
 * 
 * Disables traffic obfuscation for the specified transport.
 * 
 * @param a_transport Transport to detach obfuscation from
 */
void dap_stream_transport_detach_obfuscation(dap_stream_transport_t *a_transport);

/**
 * @brief Write data through transport with automatic obfuscation
 * 
 * If obfuscation engine is attached to the transport, this function
 * automatically applies obfuscation (padding, mixing, mimicry) before writing.
 * Otherwise, performs direct write through transport.
 * 
 * @param a_stream Stream to write to
 * @param a_data Data to write
 * @param a_size Size of data in bytes
 * @return Number of original bytes written (>0), or negative error code
 * 
 * @note Returns original data size, not obfuscated size (transparent to caller)
 * @note Obfuscated data is automatically freed after write
 */
ssize_t dap_stream_transport_write_obfuscated(dap_stream_t *a_stream, 
                                               const void *a_data, 
                                               size_t a_size);

/**
 * @brief Read data through transport with automatic deobfuscation
 * 
 * If obfuscation engine is attached to the transport, this function
 * automatically removes obfuscation (unpad, demix, unwrap mimicry) after reading.
 * Otherwise, performs direct read through transport.
 * 
 * @param a_stream Stream to read from
 * @param a_buffer Buffer to read into
 * @param a_size Maximum bytes to read
 * @return Number of deobfuscated bytes read (>0), 0 on EOF, or negative error code
 * 
 * @note Allocates temporary buffer for obfuscated data internally
 * @note Handles protocol mimicry headers, padding, and mixing automatically
 */
ssize_t dap_stream_transport_read_deobfuscated(dap_stream_t *a_stream, 
                                                void *a_buffer, 
                                                size_t a_size);

