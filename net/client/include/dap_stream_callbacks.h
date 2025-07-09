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

#include <stddef.h>

// Forward declarations to avoid circular dependencies
struct dap_http2_stream;
struct dap_http2_session;

// === Stream Callback Types ===
typedef size_t (*dap_stream_read_callback_t)(struct dap_http2_stream *a_stream, const void *a_data, size_t a_size);
typedef size_t (*dap_stream_write_callback_t)(struct dap_http2_stream *a_stream, const void *a_data, size_t a_size);
typedef void (*dap_stream_error_callback_t)(struct dap_http2_stream *a_stream, int a_error);
typedef void (*dap_stream_closed_callback_t)(struct dap_http2_stream *a_stream);

/**
 * @brief Aggregated structure for all stream-level callbacks.
 * Defines the protocol implementation for a stream.
 */
typedef struct dap_http2_stream_callbacks {
    dap_stream_read_callback_t read_cb;
    dap_stream_write_callback_t write_cb;
    dap_stream_error_callback_t error_cb;
    dap_stream_closed_callback_t closed_cb;
} dap_http2_stream_callbacks_t;

// === Session Callback Types ===
typedef void (*dap_http2_session_connected_cb_t)(struct dap_http2_session *a_session);
typedef void (*dap_http2_session_data_received_cb_t)(struct dap_http2_session *a_session, const void *a_data, size_t a_size);
typedef void (*dap_http2_session_error_cb_t)(struct dap_http2_session *a_session, int a_error);
typedef void (*dap_http2_session_closed_cb_t)(struct dap_http2_session *a_session);
typedef void (*dap_http2_session_assigned_to_worker_cb_t)(struct dap_http2_session *a_session);

/**
 * @brief Aggregated structure for all session-level callbacks.
 * Defines the connection management logic.
 */
typedef struct dap_http2_session_callbacks {
    dap_http2_session_assigned_to_worker_cb_t assigned;
    dap_http2_session_connected_cb_t connected;
    dap_http2_session_data_received_cb_t data_received;
    dap_http2_session_error_cb_t error;
    dap_http2_session_closed_cb_t closed;
} dap_http2_session_callbacks_t;

// === UNIVERSAL STATE TYPES ===
typedef int dap_stream_state_t;   // Protocol-specific state (HTTP, WebSocket, Binary, etc.)
typedef int dap_session_state_t;  // Transport-specific state (connecting, connected, etc.)

// === STREAM CALLBACK TYPES ===

/**
 * @brief Stream event callback - handles stream events
 */
typedef void (*dap_stream_event_callback_t)(struct dap_http2_stream *a_stream, 
                                            int a_event);

// === STREAM PROFILE (Application Context) ===

/**
 * @brief Stream Profile - connects session and stream via callbacks
 * Created in application thread, passed to worker when creating session
 */
typedef struct dap_stream_profile {
    // Session transport callbacks
    dap_http2_session_callbacks_t session_callbacks;
    
    // Stream application callbacks
    dap_http2_stream_callbacks_t *stream_callbacks;
    
    // Common context for all callbacks
    void *profile_context;
} dap_stream_profile_t; 