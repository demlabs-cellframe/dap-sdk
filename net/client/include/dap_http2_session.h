/*
 * Authors:
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net

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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "dap_common.h"
#include "dap_events_socket.h"
#include "dap_timerfd.h"
#include "dap_worker.h"
#include "dap_stream_callbacks.h"

#ifdef __cplusplus
extern "C" {
#endif

// Encryption types
typedef enum {
    DAP_SESSION_ENCRYPTION_NONE,
    DAP_SESSION_ENCRYPTION_TLS,
    DAP_SESSION_ENCRYPTION_CUSTOM,
    DAP_SESSION_ENCRYPTION_TLS_CUSTOM
} dap_session_encryption_type_t;

// NOTE: Session states are now transport-specific and defined by transport layer
// For TCP example:
// #define TCP_SESSION_STATE_IDLE        0
// #define TCP_SESSION_STATE_CONNECTING  1
// #define TCP_SESSION_STATE_CONNECTED   2
// etc.

// Session error types
typedef enum {
    DAP_HTTP2_SESSION_ERROR_NONE,
    DAP_HTTP2_SESSION_ERROR_CONNECT_TIMEOUT,
    DAP_HTTP2_SESSION_ERROR_READ_TIMEOUT,
    DAP_HTTP2_SESSION_ERROR_NETWORK,
    DAP_HTTP2_SESSION_ERROR_SSL,
    DAP_HTTP2_SESSION_ERROR_RESOLVE
} dap_http2_session_error_t;

// NOTE: dap_http2_session_callbacks_t is now defined in dap_stream_callbacks.h

struct dap_http2_stream;

// Main session structure (universal for client and server)
typedef struct dap_http2_session {
    // === CONNECTION MANAGEMENT ===
    dap_events_socket_t *es;              // Contains sockaddr_storage
    dap_worker_t *worker;
    dap_session_state_t state;            // Transport-specific state (non-atomic, worker thread only)
    
    // === ENCRYPTION (unified) ===
    dap_session_encryption_type_t encryption_type;
    void *encryption_context;
    
    // === CONNECTION TIMEOUTS ===
    dap_timerfd_t *connect_timer;             // Connect timeout timer
    uint64_t connect_timeout_ms;              // Connect timeout value
    
    // === UNIVERSAL SESSION STATE ===
    time_t ts_created;
    time_t ts_established;                // connect() or accept() time
    
    // === SINGLE STREAM MANAGEMENT ===
    struct dap_http2_stream *stream;           // Single stream per session
    dap_http2_stream_callbacks_t *stream_callbacks;
    
    // === CALLBACKS (define client/server role) ===
    dap_http2_session_callbacks_t callbacks;
    void *callbacks_arg;                    // For user callbacks (connected, data_received, etc.)
    
    // === FACTORY PATTERN SUPPORT ===
    void *worker_assignment_context;        // For assigned_to_worker callback only
    
} dap_http2_session_t;

// Session upgrade interface (minimal)
typedef struct dap_session_upgrade_context {
    void (*upgraded_data_callback)(dap_http2_session_t *a_session, const void *a_data, size_t a_size);
    dap_session_encryption_type_t encryption_type;
    const void *key_data;
    size_t key_size;
    void *callbacks_context;
} dap_session_upgrade_context_t;

typedef struct dap_session_upgrade_interface {
    int (*setup_custom_encryption)(dap_http2_session_t *session, const void *key_data, size_t key_size);
    bool (*is_encrypted)(const dap_http2_session_t *session);
} dap_session_upgrade_interface_t;

// === SESSION LIFECYCLE ===

/**
 * @brief Create new client session
 * @param a_worker Worker thread to assign session to
 * @param a_connect_timeout_ms Connect timeout in milliseconds (0 = default)
 * @return New session instance or NULL on error
 */
dap_http2_session_t *dap_http2_session_create(dap_worker_t *a_worker, uint64_t a_connect_timeout_ms);

/**
 * @brief Create new client session with default timeout
 * @param a_worker Worker thread to assign session to
 * @return New session instance or NULL on error
 */
dap_http2_session_t *dap_http2_session_create_default(dap_worker_t *a_worker);

/**
 * @brief Create server session from accepted socket
 * @param a_worker Worker thread to assign session to
 * @param a_client_socket Already accepted client socket
 * @return New session instance or NULL on error
 */
dap_http2_session_t *dap_http2_session_create_from_socket(dap_worker_t *a_worker, 
                                                          SOCKET a_client_socket);

/**
 * @brief Connect session to remote host (client mode)
 * @param a_session Session instance
 * @param a_addr Remote address
 * @param a_port Remote port
 * @param a_use_ssl Use SSL/TLS connection
 * @return 0 on success, negative on error
 */
int dap_http2_session_connect(dap_http2_session_t *a_session, 
                              const char *a_addr, 
                              uint16_t a_port, 
                              bool a_use_ssl);

/**
 * @brief Close session connection
 * @param a_session Session to close
 */
void dap_http2_session_close(dap_http2_session_t *a_session);

/**
 * @brief Delete session and cleanup resources
 * @param a_session Session to delete
 */
void dap_http2_session_delete(dap_http2_session_t *a_session);

// === CONFIGURATION ===

/**
 * @brief Set session connection timeout
 * @param a_session Session instance
 * @param a_connect_timeout_ms Timeout in milliseconds
 */
void dap_http2_session_set_connect_timeout(dap_http2_session_t *a_session,
                                           uint64_t a_connect_timeout_ms);

/**
 * @brief Get session connection timeout
 * @param a_session Session instance
 * @return Timeout in milliseconds
 */
uint64_t dap_http2_session_get_connect_timeout(const dap_http2_session_t *a_session);

/**
 * @brief Set session callbacks (defines client/server role)
 * @param a_session Session instance
 * @param a_callbacks Callbacks structure
 * @param a_callbacks_arg User argument for callbacks
 */
void dap_http2_session_set_callbacks(dap_http2_session_t *a_session,
                                     const dap_http2_session_callbacks_t *a_callbacks,
                                     void *a_callbacks_arg);

/**
 * @brief Upgrade session with encryption and new data callback
 * @param a_session Session instance
 * @param a_upgrade_context Upgrade context with callback and encryption params
 * @return 0 on success, negative on error
 */
int dap_http2_session_upgrade(dap_http2_session_t *a_session,
                             const dap_session_upgrade_context_t *a_upgrade_context);



// === DATA OPERATIONS ===

/**
 * @brief Send data through session
 * @param a_session Session instance
 * @param a_data Data to send
 * @param a_size Data size
 * @return Number of bytes sent or negative on error
 */
int dap_http2_session_send(dap_http2_session_t *a_session, const void *a_data, size_t a_size);

/**
 * @brief Get direct write buffer info for zero-copy operations
 * @param a_session Session instance
 * @param a_write_ptr Output: pointer to write position in buffer
 * @param a_size_ptr Output: pointer to current buffer size (for direct increment)
 * @param a_available_space Output: available space in buffer
 * @return 0 on success, negative on error
 */
int dap_http2_session_get_write_buffer_info(dap_http2_session_t *a_session, 
                                           void **a_write_ptr, 
                                           size_t **a_size_ptr, 
                                           size_t *a_available_space);

/**
 * @brief Process incoming data from session socket
 * @param a_session Session instance
 * @param a_data Incoming data
 * @param a_size Data size
 * @return Number of bytes processed
 */
size_t dap_http2_session_process_data(dap_http2_session_t *a_session, 
                                      const void *a_data, 
                                      size_t a_size);

// === STATE QUERIES ===

/**
 * @brief Get current session state (transport-specific interpretation)
 * @param a_session Session instance
 * @return Current session state
 */
dap_session_state_t dap_http2_session_get_state(const dap_http2_session_t *a_session);

/**
 * @brief Check if session is connected
 * @param a_session Session instance
 * @return true if connected
 */
bool dap_http2_session_is_connected(const dap_http2_session_t *a_session);

/**
 * @brief Check if session is in error state
 * @param a_session Session instance
 * @return true if in error state
 */
bool dap_http2_session_is_error(const dap_http2_session_t *a_session);

/**
 * @brief Check if session is client mode (created without socket)
 * @param a_session Session instance
 * @return true if client mode
 */
bool dap_http2_session_is_client_mode(const dap_http2_session_t *a_session);

/**
 * @brief Check if session is server mode (created from socket)
 * @param a_session Session instance
 * @return true if server mode
 */
bool dap_http2_session_is_server_mode(const dap_http2_session_t *a_session);

// === STREAM MANAGEMENT ===

/**
 * @brief Create a single stream for this session.
 * The session will act as a factory for the stream.
 * @param a_session Session instance.
 * @param a_callbacks The protocol implementation for the stream.
 * @param a_callback_arg The context for the stream callbacks.
 * @return New stream instance or NULL on error.
 */
struct dap_http2_stream *dap_http2_session_create_stream(dap_http2_session_t *a_session,
                                                     const dap_http2_stream_callbacks_t *a_callbacks);

/**
 * @brief Get the single stream associated with this session.
 * @param a_session Session instance
 * @return Stream or NULL if none
 */
struct dap_http2_stream *dap_http2_session_get_stream(const dap_http2_session_t *a_session);

// === UTILITY FUNCTIONS ===

/**
 * @brief Get remote address from session
 * @param a_session Session instance
 * @param a_addr_buf Buffer for address string
 * @param a_buf_size Buffer size
 * @return 0 on success, negative on error
 */
int dap_http2_session_get_remote_addr(const dap_http2_session_t *a_session,
                                      char *a_addr_buf, 
                                      size_t a_buf_size);

/**
 * @brief Get remote port from session
 * @param a_session Session instance
 * @return Remote port or 0 on error
 */
uint16_t dap_http2_session_get_remote_port(const dap_http2_session_t *a_session);

/**
 * @brief Get last activity time from session
 * @param a_session Session instance
 * @return Last activity timestamp
 */
time_t dap_http2_session_get_last_activity(const dap_http2_session_t *a_session);

// NOTE: State string functions are now transport-specific
// Each transport should implement its own state_to_str function

/**
 * @brief Get session error string representation
 * @param a_error Session error
 * @return Error name string
 */
const char* dap_http2_session_error_to_str(dap_http2_session_error_t a_error);



// === ENCRYPTION MANAGEMENT ===

/**
 * @brief Get current encryption type
 * @param a_session Session instance
 * @return Current encryption type
 */
dap_session_encryption_type_t dap_http2_session_get_encryption_type(const dap_http2_session_t *a_session);

/**
 * @brief Get session upgrade interface for Stream communication
 * @param a_session Session instance
 * @return Upgrade interface pointer
 */
dap_session_upgrade_interface_t* dap_http2_session_get_upgrade_interface(dap_http2_session_t *a_session);

/**
 * @brief Get session upgrade interface for Stream communication
 * @param a_session Session instance
 * @return Upgrade interface pointer
 */
dap_session_upgrade_interface_t* dap_http2_session_get_upgrade_interface(dap_http2_session_t *a_session);

#ifdef __cplusplus
}
#endif 