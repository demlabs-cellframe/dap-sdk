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

/**
 * @file dap_stream_callbacks.h
 * @brief Stream Callback Types Interface - eliminates circular dependencies
 * 
 * This header contains ONLY callback types and forward declarations.
 * Can be included in both session.h and stream.h without conflicts.
 */

#pragma once

#include "dap_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// === FORWARD DECLARATIONS ===
typedef struct dap_http2_session dap_http2_session_t;
typedef struct dap_http2_stream dap_http2_stream_t;

// === UNIVERSAL STATE TYPES ===
typedef int dap_stream_state_t;   // Protocol-specific state (HTTP, WebSocket, Binary, etc.)
typedef int dap_session_state_t;  // Transport-specific state (connecting, connected, etc.)

// === STREAM CALLBACK TYPES ===

/**
 * @brief Main stream read callback - processes incoming data
 */
typedef size_t (*dap_stream_read_callback_t)(dap_http2_stream_t *a_stream, 
                                             const void *a_data, 
                                             size_t a_data_size);

/**
 * @brief Stream event callback - handles stream events
 */
typedef void (*dap_stream_event_callback_t)(dap_http2_stream_t *a_stream, 
                                            int a_event);

// === SESSION CALLBACK TYPES ===

/**
 * @brief Session callbacks - define role (client/server)
 */
typedef struct dap_http2_session_callbacks {
    void (*connected)(dap_http2_session_t *a_session);
    void (*data_received)(dap_http2_session_t *a_session, const void *a_data, size_t a_size);
    void (*error)(dap_http2_session_t *a_session, int a_error);
    void (*closed)(dap_http2_session_t *a_session);
    void (*assigned_to_worker)(dap_http2_session_t *a_session);
    void (*encryption_ready)(dap_http2_session_t *a_session);
} dap_http2_session_callbacks_t;

// === STREAM PROFILE (Application Context) ===

/**
 * @brief Stream Profile - connects session and stream via callbacks
 * Created in application thread, passed to worker when creating session
 */
typedef struct dap_stream_profile {
    // Session transport callbacks
    dap_http2_session_callbacks_t session_callbacks;
    
    // Stream application callback
    dap_stream_read_callback_t initial_read_callback;
    
    // Common context for all callbacks
    void *callbacks_context;
    
} dap_stream_profile_t;

#ifdef __cplusplus
}
#endif 