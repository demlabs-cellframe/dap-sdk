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
 * @file dap_http2_stream.h
 * @brief HTTP/2 Stream Layer - HTTP parsing, response processing, buffer management
 * 
 * Responsibilities:
 * - HTTP response parsing (headers, body, chunked transfer)
 * - Protocol-specific data processing (HTTP, WebSocket, SSE, Binary)
 * - Buffer management for stream data
 * - Stream state management and transitions
 * - Channel-based data processing with callback switching
 */

#pragma once

#include "dap_common.h"
#include "dap_events_socket.h"
#include "dap_enc_key.h"

typedef struct dap_http2_session dap_http2_session_t;
typedef struct dap_http2_stream dap_http2_stream_t;

// Maximum number of channels per stream
#define DAP_HTTP2_STREAM_MAX_CHANNELS 16

// Stream states
typedef enum {
    DAP_HTTP2_STREAM_STATE_IDLE,
    DAP_HTTP2_STREAM_STATE_REQUEST_SENT,
    DAP_HTTP2_STREAM_STATE_HEADERS,
    DAP_HTTP2_STREAM_STATE_BODY,
    DAP_HTTP2_STREAM_STATE_COMPLETE,
    DAP_HTTP2_STREAM_STATE_ERROR,
    DAP_HTTP2_STREAM_STATE_UPGRADED,
    DAP_HTTP2_STREAM_STATE_CLOSING,
    DAP_HTTP2_STREAM_STATE_CLOSED
} dap_http2_stream_state_t;

// Protocol types for different processing modes
typedef enum {
    DAP_HTTP2_PROTOCOL_HTTP,
    DAP_HTTP2_PROTOCOL_WEBSOCKET,
    DAP_HTTP2_PROTOCOL_SSE,
    DAP_HTTP2_PROTOCOL_BINARY,
    DAP_HTTP2_PROTOCOL_RAW
} dap_http2_protocol_type_t;

// HTTP parser state
typedef enum {
    DAP_HTTP_PARSER_STATE_NONE,
    DAP_HTTP_PARSER_STATE_HEADERS,
    DAP_HTTP_PARSER_STATE_BODY,
    DAP_HTTP_PARSER_STATE_CHUNKED,
    DAP_HTTP_PARSER_STATE_COMPLETE
} dap_http_parser_state_t;

// === CALLBACK TYPES ===

/**
 * @brief Main stream read callback - processes incoming data
 * @param a_stream Stream instance
 * @param a_data Incoming data buffer
 * @param a_data_size Size of incoming data
 * @return Number of bytes processed
 */
typedef size_t (*dap_stream_read_callback_t)(dap_http2_stream_t *a_stream, 
                                             const void *a_data, 
                                             size_t a_data_size);

/**
 * @brief Channel callback - processes data for specific channel
 * @param a_stream Stream instance
 * @param a_channel_id Channel identifier
 * @param a_data Channel-specific data
 * @param a_data_size Size of channel data
 * @return Number of bytes processed
 */
typedef size_t (*dap_stream_channel_callback_t)(dap_http2_stream_t *a_stream,
                                                uint8_t a_channel_id,
                                                const void *a_data,
                                                size_t a_data_size);

/**
 * @brief Stream event callback - handles stream events
 * @param a_stream Stream instance
 * @param a_event Event type
 */
typedef void (*dap_stream_event_callback_t)(dap_http2_stream_t *a_stream, 
                                            int a_event);

// === MAIN STREAM STRUCTURE ===

typedef struct dap_http2_stream {
    // === BASIC STREAM INFO ===
    uint32_t stream_id;
    dap_http2_stream_state_t state;
    dap_http2_session_t *session;
    dap_http2_protocol_type_t current_protocol;
    
    // === UNIFIED BUFFER ===
    uint8_t *receive_buffer;
    size_t receive_buffer_size;
    size_t receive_buffer_capacity;
    
    // === CALLBACK SYSTEM ===
    // Current active read callback (switches based on protocol/mode)
    dap_stream_read_callback_t read_callback;
    void *read_callback_context;
    
    // Channel callbacks array (indexed by channel ID)
    dap_stream_channel_callback_t channel_callbacks[DAP_HTTP2_STREAM_MAX_CHANNELS];
    void *channel_contexts[DAP_HTTP2_STREAM_MAX_CHANNELS];
    
    // Event callback
    dap_stream_event_callback_t event_callback;
    void *event_callback_context;
    
    // === HTTP PARSER STATE (for HTTP protocol) ===
    dap_http_parser_state_t parser_state;
    size_t content_length;
    size_t content_received;
    bool is_chunked;
    
    // === STREAM MANAGEMENT ===
    bool is_autonomous;  // Can exist without client
    
} dap_http2_stream_t;

// === STREAM LIFECYCLE ===

/**
 * @brief Create new stream
 * @param a_session Parent session
 * @param a_initial_protocol Initial protocol type
 * @return New stream instance or NULL on error
 */
dap_http2_stream_t* dap_http2_stream_create(dap_http2_session_t *a_session,
                                           dap_http2_protocol_type_t a_initial_protocol);

/**
 * @brief Delete stream and cleanup resources
 * @param a_stream Stream to delete
 */
void dap_http2_stream_delete(dap_http2_stream_t *a_stream);

// === CALLBACK MANAGEMENT ===

/**
 * @brief Set main read callback
 * @param a_stream Stream instance
 * @param a_callback Read callback function
 * @param a_context Callback context
 */
void dap_http2_stream_set_read_callback(dap_http2_stream_t *a_stream,
                                       dap_stream_read_callback_t a_callback,
                                       void *a_context);

/**
 * @brief Set channel callback for specific channel ID
 * @param a_stream Stream instance
 * @param a_channel_id Channel identifier (0-15)
 * @param a_callback Channel callback function
 * @param a_context Callback context
 * @return 0 on success, negative on error
 */
int dap_http2_stream_set_channel_callback(dap_http2_stream_t *a_stream,
                                         uint8_t a_channel_id,
                                         dap_stream_channel_callback_t a_callback,
                                         void *a_context);

/**
 * @brief Set event callback
 * @param a_stream Stream instance
 * @param a_callback Event callback function
 * @param a_context Callback context
 */
void dap_http2_stream_set_event_callback(dap_http2_stream_t *a_stream,
                                        dap_stream_event_callback_t a_callback,
                                        void *a_context);

// === PROTOCOL SWITCHING ===

/**
 * @brief Switch stream to different protocol mode
 * @param a_stream Stream instance
 * @param a_new_protocol New protocol type
 * @return 0 on success, negative on error
 */
int dap_http2_stream_switch_protocol(dap_http2_stream_t *a_stream,
                                    dap_http2_protocol_type_t a_new_protocol);

// === DATA PROCESSING ===

/**
 * @brief Process incoming data through current read callback
 * @param a_stream Stream instance
 * @param a_data Incoming data
 * @param a_data_size Data size
 * @return Number of bytes processed
 */
size_t dap_http2_stream_process_data(dap_http2_stream_t *a_stream,
                                    const void *a_data,
                                    size_t a_data_size);

// === BUILT-IN READ CALLBACKS ===

/**
 * @brief HTTP protocol read callback for CLIENT mode (parses HTTP responses)
 */
size_t dap_http2_stream_read_callback_http_client(dap_http2_stream_t *a_stream,
                                                 const void *a_data,
                                                 size_t a_data_size);

/**
 * @brief HTTP protocol read callback for SERVER mode (parses HTTP requests)
 */
size_t dap_http2_stream_read_callback_http_server(dap_http2_stream_t *a_stream,
                                                 const void *a_data,
                                                 size_t a_data_size);

/**
 * @brief WebSocket protocol read callback (universal for client/server)
 */
size_t dap_http2_stream_read_callback_websocket(dap_http2_stream_t *a_stream,
                                               const void *a_data,
                                               size_t a_data_size);

/**
 * @brief Binary data read callback (with channel dispatching, universal)
 */
size_t dap_http2_stream_read_callback_binary(dap_http2_stream_t *a_stream,
                                            const void *a_data,
                                            size_t a_data_size);

/**
 * @brief Server-Sent Events read callback (universal)
 */
size_t dap_http2_stream_read_callback_sse(dap_http2_stream_t *a_stream,
                                         const void *a_data,
                                         size_t a_data_size);

// === STATE MANAGEMENT ===

/**
 * @brief Get current stream state
 * @param a_stream Stream instance
 * @return Current state
 */
dap_http2_stream_state_t dap_http2_stream_get_state(dap_http2_stream_t *a_stream);

/**
 * @brief Set stream state
 * @param a_stream Stream instance
 * @param a_state New state
 */
void dap_http2_stream_set_state(dap_http2_stream_t *a_stream,
                               dap_http2_stream_state_t a_state);

/**
 * @brief Check if stream is in error state
 * @param a_stream Stream instance
 * @return true if in error state
 */
bool dap_http2_stream_is_error(dap_http2_stream_t *a_stream);

/**
 * @brief Check if stream is autonomous (can exist without client)
 * @param a_stream Stream instance
 * @return true if autonomous
 */
bool dap_http2_stream_is_autonomous(dap_http2_stream_t *a_stream);

// === UTILITY FUNCTIONS ===

/**
 * @brief Get protocol type string representation
 * @param a_protocol Protocol type
 * @return Protocol name string
 */
const char* dap_http2_stream_protocol_to_str(dap_http2_protocol_type_t a_protocol);

/**
 * @brief Get state string representation
 * @param a_state Stream state
 * @return State name string
 */
const char* dap_http2_stream_state_to_str(dap_http2_stream_state_t a_state);

// === CONVENIENCE FUNCTIONS ===

/**
 * @brief Set stream to HTTP client mode
 * @param a_stream Stream instance
 */
void dap_http2_stream_set_http_client_mode(dap_http2_stream_t *a_stream);

/**
 * @brief Set stream to HTTP server mode
 * @param a_stream Stream instance
 */
void dap_http2_stream_set_http_server_mode(dap_http2_stream_t *a_stream);

/**
 * @brief Set stream to WebSocket mode
 * @param a_stream Stream instance
 */
void dap_http2_stream_set_websocket_mode(dap_http2_stream_t *a_stream);

/**
 * @brief Set stream to binary mode with channels
 * @param a_stream Stream instance
 */
void dap_http2_stream_set_binary_mode(dap_http2_stream_t *a_stream); 