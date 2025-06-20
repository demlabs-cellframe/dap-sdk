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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dap_http2_stream dap_http2_stream_t;

// Session states
typedef enum {
    DAP_HTTP2_SESSION_STATE_IDLE,
    DAP_HTTP2_SESSION_STATE_CONNECTING,
    DAP_HTTP2_SESSION_STATE_CONNECTED,
    DAP_HTTP2_SESSION_STATE_CLOSING,
    DAP_HTTP2_SESSION_STATE_CLOSED,
    DAP_HTTP2_SESSION_STATE_ERROR
} dap_http2_session_state_t;

// Session error types
typedef enum {
    DAP_HTTP2_SESSION_ERROR_NONE,
    DAP_HTTP2_SESSION_ERROR_CONNECT_TIMEOUT,
    DAP_HTTP2_SESSION_ERROR_READ_TIMEOUT,
    DAP_HTTP2_SESSION_ERROR_NETWORK,
    DAP_HTTP2_SESSION_ERROR_SSL,
    DAP_HTTP2_SESSION_ERROR_RESOLVE
} dap_http2_session_error_t;

// Session callbacks (define the role!)
typedef struct dap_http2_session_callbacks {
    void (*connected)(struct dap_http2_session *a_session);
    void (*data_received)(struct dap_http2_session *a_session, const void *a_data, size_t a_size);
    void (*error)(struct dap_http2_session *a_session, dap_http2_session_error_t a_error);
    void (*closed)(struct dap_http2_session *a_session);
} dap_http2_session_callbacks_t;

// Main session structure (universal for client and server)
typedef struct dap_http2_session {
    // === CONNECTION MANAGEMENT ===
    dap_events_socket_t *es;              // Contains sockaddr_storage
    dap_worker_t *worker;
    dap_http2_session_state_t state;
    
    // === SSL FLAG ===
    bool is_ssl;
    
    // === UNIVERSAL TIMERS ===
    dap_timerfd_t *connect_timer;         // NULL for server sessions
    dap_timerfd_t *read_timer;            // Used by all sessions
    
    // === UNIVERSAL SESSION STATE ===
    time_t ts_created;
    time_t ts_established;                // connect() or accept() time
    
    // === SINGLE STREAM MANAGEMENT ===
    dap_http2_stream_t *current_stream;   // Current active stream
    uint32_t next_stream_id;              // For stream ID generation
    
    // === CALLBACKS (define client/server role) ===
    dap_http2_session_callbacks_t callbacks;
    void *callbacks_arg;
    
} dap_http2_session_t;

// === SESSION LIFECYCLE ===

/**
 * @brief Create new client session
 * @param a_worker Worker thread to assign session to
 * @return New session instance or NULL on error
 */
dap_http2_session_t *dap_http2_session_create(dap_worker_t *a_worker);

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
 * @brief Set session timeouts by configuring timers directly
 * @param a_session Session instance
 * @param a_connect_timeout_ms Connect timeout in milliseconds (ignored for server)
 * @param a_read_timeout_ms Read timeout in milliseconds
 */
void dap_http2_session_set_timeouts(dap_http2_session_t *a_session,
                                    uint64_t a_connect_timeout_ms,
                                    uint64_t a_read_timeout_ms);

/**
 * @brief Set session callbacks (defines client/server role)
 * @param a_session Session instance
 * @param a_callbacks Callbacks structure
 * @param a_callbacks_arg User argument for callbacks
 */
void dap_http2_session_set_callbacks(dap_http2_session_t *a_session,
                                     const dap_http2_session_callbacks_t *a_callbacks,
                                     void *a_callbacks_arg);

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
 * @brief Get current session state
 * @param a_session Session instance
 * @return Current session state
 */
dap_http2_session_state_t dap_http2_session_get_state(const dap_http2_session_t *a_session);

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
 * @brief Check if session is client mode (has connect_timer)
 * @param a_session Session instance
 * @return true if client mode
 */
bool dap_http2_session_is_client_mode(const dap_http2_session_t *a_session);

/**
 * @brief Check if session is server mode (no connect_timer)
 * @param a_session Session instance
 * @return true if server mode
 */
bool dap_http2_session_is_server_mode(const dap_http2_session_t *a_session);

// === STREAM MANAGEMENT ===

/**
 * @brief Set current stream for session
 * @param a_session Session instance
 * @param a_stream Stream to set as current
 */
void dap_http2_session_set_stream(dap_http2_session_t *a_session, dap_http2_stream_t *a_stream);

/**
 * @brief Get current stream from session
 * @param a_session Session instance
 * @return Current stream or NULL if none
 */
dap_http2_stream_t *dap_http2_session_get_stream(const dap_http2_session_t *a_session);

/**
 * @brief Generate next stream ID
 * @param a_session Session instance
 * @return New unique stream ID
 */
uint32_t dap_http2_session_next_stream_id(dap_http2_session_t *a_session);

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

/**
 * @brief Get session state string representation
 * @param a_state Session state
 * @return State name string
 */
const char* dap_http2_session_state_to_str(dap_http2_session_state_t a_state);

/**
 * @brief Get session error string representation
 * @param a_error Session error
 * @return Error name string
 */
const char* dap_http2_session_error_to_str(dap_http2_session_error_t a_error);

#ifdef __cplusplus
}
#endif 