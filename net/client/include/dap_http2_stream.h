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
 * @brief HTTP/2 Stream Layer - Single stream with channel multiplexing
 * 
 * Responsibilities:
 * - Single stream per session (replaces multiple streams)
 * - Channel-based multiplexing for different protocols
 * - Protocol-specific data processing via channels
 * - Buffer management for unified stream data
 * - Stream state management and transitions
 */

#pragma once

#include "dap_common.h"
#include "dap_events_socket.h"
#include "dap_enc_key.h"
#include "dap_timerfd.h"
#include "dap_stream_callbacks.h"

// === CHANNEL UID CONSTANTS ===
#define CHANNEL_UID_WORKER_BITS     8
#define CHANNEL_UID_ESOCKET_BITS    32  
#define CHANNEL_UID_RESERVED_BITS   8
#define CHANNEL_UID_CHANNEL_BITS    16

#define CHANNEL_UID_WORKER_SHIFT    56
#define CHANNEL_UID_ESOCKET_SHIFT   24
#define CHANNEL_UID_RESERVED_SHIFT  16
#define CHANNEL_UID_CHANNEL_SHIFT   0

#define CHANNEL_UID_WORKER_MASK     0xFF00000000000000UL
#define CHANNEL_UID_ESOCKET_MASK    0x00FFFFFFFF000000UL
#define CHANNEL_UID_RESERVED_MASK   0x0000000000FF0000UL
#define CHANNEL_UID_CHANNEL_MASK    0x000000000000FFFFUL

// === CHANNEL UID UTILITIES ===
static inline uint8_t dap_channel_uid_extract_worker_id(uint64_t channel_uid) {
    return (uint8_t)(channel_uid >> CHANNEL_UID_WORKER_SHIFT);
}

static inline uint32_t dap_channel_uid_extract_esocket_uid(uint64_t channel_uid) {
    return (uint32_t)((channel_uid & CHANNEL_UID_ESOCKET_MASK) >> CHANNEL_UID_ESOCKET_SHIFT);
}

static inline uint16_t dap_channel_uid_extract_channel_id(uint64_t channel_uid) {
    return (uint16_t)(channel_uid & CHANNEL_UID_CHANNEL_MASK);
}

static inline uint64_t dap_channel_uid_compose(uint8_t worker_id, uint32_t esocket_uid, uint16_t channel_id) {
    return ((uint64_t)worker_id << CHANNEL_UID_WORKER_SHIFT) |
           ((uint64_t)esocket_uid << CHANNEL_UID_ESOCKET_SHIFT) |
           ((uint64_t)channel_id << CHANNEL_UID_CHANNEL_SHIFT);
}

static inline uint64_t dap_channel_uid_to_stream_uid(uint64_t channel_uid) {
    uint8_t worker_id = dap_channel_uid_extract_worker_id(channel_uid);
    uint32_t esocket_uid = dap_channel_uid_extract_esocket_uid(channel_uid);
    return ((uint64_t)worker_id << CHANNEL_UID_WORKER_SHIFT) | 
           ((uint64_t)esocket_uid << CHANNEL_UID_ESOCKET_SHIFT);
}

static inline uint64_t dap_stream_get_channel_uid(uint64_t stream_uid, uint16_t channel_id) {
    uint8_t worker_id = (uint8_t)(stream_uid >> CHANNEL_UID_WORKER_SHIFT);
    uint32_t esocket_uid = (uint32_t)((stream_uid & CHANNEL_UID_ESOCKET_MASK) >> CHANNEL_UID_ESOCKET_SHIFT);
    return dap_channel_uid_compose(worker_id, esocket_uid, channel_id);
}

// === STREAM UID UTILITIES ===
static inline uint64_t dap_stream_uid_compose(uint8_t worker_id, uint32_t esocket_uid) {
    return ((uint64_t)worker_id << CHANNEL_UID_WORKER_SHIFT) |
           ((uint64_t)esocket_uid << CHANNEL_UID_ESOCKET_SHIFT);
}

static inline uint8_t dap_stream_uid_extract_worker_id(uint64_t stream_uid) {
    return (uint8_t)(stream_uid >> CHANNEL_UID_WORKER_SHIFT);
}

static inline uint32_t dap_stream_uid_extract_esocket_uid(uint64_t stream_uid) {
    return (uint32_t)((stream_uid & CHANNEL_UID_ESOCKET_MASK) >> CHANNEL_UID_ESOCKET_SHIFT);
}

// NOTE: Stream states are now protocol-specific and defined by each protocol
// For HTTP example:
// #define HTTP_STREAM_STATE_IDLE           0
// #define HTTP_STREAM_STATE_REQUEST_SENT   1
// #define HTTP_STREAM_STATE_HEADERS        2
// etc.

// === CHANNEL MULTIPLEXING ===
// Single stream with channel-based protocol multiplexing:
// - Channel 0: Reserved for control
// - Channel 1-N: Application protocols (HTTP, WebSocket, Binary, etc.)
// - Each channel has its own callback and context
// - Channels are enabled/disabled dynamically

// HTTP parser state
typedef enum {
    DAP_HTTP_PARSER_STATE_NONE,
    DAP_HTTP_PARSER_STATE_HEADERS,
    DAP_HTTP_PARSER_STATE_BODY,
    DAP_HTTP_PARSER_STATE_CHUNKED,
    DAP_HTTP_PARSER_STATE_COMPLETE
} dap_http_parser_state_t;

// Channel event types
typedef enum {
    DAP_HTTP2_STREAM_CHANNEL_EVENT_ADDED,    // Channel was added
    DAP_HTTP2_STREAM_CHANNEL_EVENT_REMOVED,  // Channel was removed
    DAP_HTTP2_STREAM_CHANNEL_EVENT_CLEARED   // All channels were cleared
} dap_http2_stream_channel_event_t;

struct dap_http2_session;
struct dap_http2_stream;
struct dap_http2_stream_private;

// === CALLBACK TYPES ===
// NOTE: Basic callback types are defined in dap_stream_callbacks.h

/**
 * @brief Stream state change callback - called when stream state changes
 * @param a_stream Stream instance
 * @param a_old_state Previous state (protocol-specific)
 * @param a_new_state New state (protocol-specific)
 * @param a_context User context
 */
typedef void (*dap_stream_state_changed_cb_t)(struct dap_http2_stream *a_stream,
    dap_stream_state_t a_old_state,
    dap_stream_state_t a_new_state,
    void *a_context);

/**
* @brief Channel callback - processes data for specific channel
* @param a_stream Stream instance
* @param a_channel_id Channel identifier
* @param a_data Channel-specific data
* @param a_data_size Size of channel data
* @return Number of bytes processed
*/
typedef size_t (*dap_stream_channel_callback_t)(struct dap_http2_stream *a_stream,
      uint8_t a_channel_id,
      const void *a_data,
      size_t a_data_size);

/**
* @brief Channel event callback - called when channels are added/removed
* @param a_stream Stream instance
* @param a_event Event type
* @param a_channel_id Channel ID (for ADDED/REMOVED events)
* @param a_channels_count Current total number of active channels
*/
typedef void (*dap_stream_channel_event_callback_t)(struct dap_http2_stream *a_stream,
         dap_http2_stream_channel_event_t a_event,
         uint8_t a_channel_id,
         size_t a_channels_count);

// Custom protocol handlers (optional - only for protocols with handshake)
typedef struct dap_stream_handshake_handlers {
    dap_stream_read_callback_t detect_callback;       // Analyze server response (HEADERS state)
    dap_stream_read_callback_t handshake_callback;    // Perform key exchange (UPGRADED state)
    dap_stream_read_callback_t ready_callback;        // Process encrypted data (COMPLETE state)
} dap_stream_handshake_handlers_t;

// === STREAM PROFILE (for embedded transitions) ===
// NOTE: dap_stream_profile_t is now defined in dap_stream_callbacks.h

// === MAIN STREAM STRUCTURE ===

typedef struct dap_http2_stream {
    // === PERFORMANCE CRITICAL PUBLIC INTERFACE ===
    _Atomic uint64_t uid;                 // Stream UID (worker_id + stream_id)
    dap_stream_state_t state;             // Protocol-specific state (non-atomic, worker thread only)
    struct dap_http2_session *session;
    
    // === CALLBACK INTERFACE (PUBLIC для zero-copy производительности) ===
    dap_http2_stream_callbacks_t callbacks;
    void *callback_context;
    
    // === PRIVATE DATA (User Idea V2 оптимизация) ===
    struct dap_http2_stream_private *private_data;  // Типизированный указатель
    
} dap_http2_stream_t;

// === PRIVATE DATA ===
// Note: Private data structure is defined only in .c file for true encapsulation
// Direct access: stream->private_data (const pointer, structure contents can be modified)

// === OPTIONAL CHANNEL API ===
#ifdef DAP_STREAM_CHANNELS_ENABLED

// === CHANNEL CONTEXT ===
typedef struct dap_stream_channel_context {
    dap_stream_channel_callback_t channel_callbacks[256];
    void *channel_contexts[256];
} dap_stream_channel_context_t;

// === CHANNEL TEMPLATE ===
typedef struct dap_stream_channel_template {
    dap_stream_channel_callback_t callbacks[256];
    void *contexts[256];
    uint8_t initial_active_channels[32];
    size_t initial_active_count;
} dap_stream_channel_template_t;

#endif // DAP_STREAM_CHANNELS_ENABLED

// === STREAM LIFECYCLE ===

/**
 * @brief Create new stream
 * @param a_session Parent session
 * @return New stream instance or NULL on error
 */
dap_http2_stream_t *dap_http2_stream_create(struct dap_http2_session *a_session);

/**
 * @brief Get stream UID (atomic read)
 * @param a_stream Stream instance
 * @return Stream UID or 0 if invalid stream
 */
uint64_t dap_http2_stream_get_uid(const dap_http2_stream_t *a_stream);

/**
 * @brief Get worker ID from stream UID
 * @param a_stream Stream instance
 * @return Worker ID or 0 if invalid
 */
uint8_t dap_http2_stream_get_worker_id(const dap_http2_stream_t *a_stream);

/**
 * @brief Get esocket UID from stream UID
 * @param a_stream Stream instance  
 * @return Esocket UID or 0 if invalid
 */
uint32_t dap_http2_stream_get_esocket_uid(const dap_http2_stream_t *a_stream);

/**
 * @brief Delete stream and cleanup resources
 * @param a_stream Stream to delete
 */
void dap_http2_stream_delete(dap_http2_stream_t *a_stream);

// === READ TIMEOUT MANAGEMENT (OSI Application Layer) ===

/**
 * @brief Set stream read timeout
 * @param a_stream Stream instance
 * @param a_read_timeout_ms Read timeout in milliseconds
 */
void dap_http2_stream_set_read_timeout(dap_http2_stream_t *a_stream,
                                       uint64_t a_read_timeout_ms);

/**
 * @brief Get stream read timeout
 * @param a_stream Stream instance
 * @return Read timeout in milliseconds
 */
uint64_t dap_http2_stream_get_read_timeout(const dap_http2_stream_t *a_stream);

/**
 * @brief Start read timer
 * @param a_stream Stream instance
 * @return 0 on success, negative on error
 */
int dap_http2_stream_start_read_timer(dap_http2_stream_t *a_stream);

/**
 * @brief Stop read timer
 * @param a_stream Stream instance
 */
void dap_http2_stream_stop_read_timer(dap_http2_stream_t *a_stream);

/**
 * @brief Reset read timer (restart with same timeout)
 * @param a_stream Stream instance
 * @return 0 on success, negative on error
 */
int dap_http2_stream_reset_read_timer(dap_http2_stream_t *a_stream);

// === MAIN CALLBACK MANAGEMENT ===

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
 * @brief Set event callback
 * @param a_stream Stream instance
 * @param a_callback Event callback function
 * @param a_context Callback context
 */
void dap_http2_stream_set_event_callback(dap_http2_stream_t *a_stream,
                                        dap_stream_event_callback_t a_callback,
                                        void *a_context);

/**
 * @brief Set state change callback
 * @param a_stream Stream instance
 * @param a_callback State change callback function
 * @param a_context Callback context
 */
void dap_http2_stream_set_state_changed_callback(dap_http2_stream_t *a_stream,
                                                 dap_stream_state_changed_cb_t a_callback,
                                                 void *a_context);

#ifdef DAP_STREAM_CHANNELS_ENABLED

// === CHANNEL CONTEXT MANAGEMENT ===

/**
 * @brief Create channel context from template
 * @param a_template Channel template with callbacks and initial state
 * @return New channel context or NULL on error
 */
dap_stream_channel_context_t* dap_stream_channel_context_create(const dap_stream_channel_template_t *a_template);

/**
 * @brief Delete channel context
 * @param a_context Context to delete
 */
void dap_stream_channel_context_delete(dap_stream_channel_context_t *a_context);

/**
 * @brief Set channel context for stream
 * @param a_stream Stream instance
 * @param a_context Channel context (ownership transfers to stream)
 */
void dap_http2_stream_set_channel_context(dap_http2_stream_t *a_stream, dap_stream_channel_context_t *a_context);

/**
 * @brief Get channel context from stream
 * @param a_stream Stream instance
 * @return Channel context or NULL if not set
 */
dap_stream_channel_context_t* dap_http2_stream_get_channel_context(dap_http2_stream_t *a_stream);

// === UNSAFE CHANNEL MANAGEMENT (Worker Thread Only) ===

/**
 * @brief Enable channel (unsafe - worker thread only)
 * @param a_context Channel context
 * @param a_channel_id Channel ID to enable
 * @param a_callback Channel callback
 * @param a_context_ptr Channel context
 */
static inline void dap_stream_channel_enable_unsafe(dap_stream_channel_context_t *a_context, 
                                                    uint8_t a_channel_id,
                                                    dap_stream_channel_callback_t a_callback,
                                                    void *a_context_ptr) {
    a_context->channel_callbacks[a_channel_id] = a_callback;
    a_context->channel_contexts[a_channel_id] = a_context_ptr;
}

/**
 * @brief Disable channel (unsafe - worker thread only)
 * @param a_context Channel context
 * @param a_channel_id Channel ID to disable
 */
static inline void dap_stream_channel_disable_unsafe(dap_stream_channel_context_t *a_context, 
                                                     uint8_t a_channel_id) {
    a_context->channel_callbacks[a_channel_id] = NULL;
    a_context->channel_contexts[a_channel_id] = NULL;
}

/**
 * @brief Check if channel is active
 * @param a_context Channel context
 * @param a_channel_id Channel ID to check
 * @return true if channel is active
 */
static inline bool dap_stream_channel_is_active(const dap_stream_channel_context_t *a_context, 
                                               uint8_t a_channel_id) {
    return a_context->channel_callbacks[a_channel_id] != NULL;
}

// === EXTERNAL CHANNEL MANAGEMENT (Thread-Safe) ===

/**
 * @brief Enable channel externally (thread-safe)
 * @param a_channel_uid Channel UID
 * @param a_callback Channel callback
 * @param a_context Channel context
 * @return 0 on success, negative on error
 */
int dap_stream_channel_enable_by_uid(uint64_t a_channel_uid, 
                                    dap_stream_channel_callback_t a_callback,
                                    void *a_context);

/**
 * @brief Disable channel externally (thread-safe)
 * @param a_channel_uid Channel UID
 * @return 0 on success, negative on error
 */
int dap_stream_channel_disable_by_uid(uint64_t a_channel_uid);

#endif // DAP_STREAM_CHANNELS_ENABLED

// === PROTOCOL SWITCHING ===

/**
 * @brief Switch stream protocol after handshake detection
 * @param a_stream Stream instance
 * @param a_new_callback New read callback for detected protocol
 * @param a_context New context for callback
 * @return 0 on success, negative on error
 */
int dap_http2_stream_switch_protocol(dap_http2_stream_t *a_stream,
                                    dap_stream_read_callback_t a_new_callback,
                                    void *a_context);

// === DATA PROCESSING ===

/**
 * @brief Process incoming data through current read callback
 * @param a_stream Stream instance
 * @param a_data Incoming data
 * @param a_data_size Size of data
 * @return Number of bytes processed
 */
size_t dap_http2_stream_process_data(dap_http2_stream_t *a_stream,
                                    const void *a_data,
                                    size_t a_data_size);

// REMOVED: dap_http2_stream_write_data - use dap_http2_session_write_direct_stream directly

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
 * @brief Get current stream state (protocol-specific interpretation)
 * @param a_stream Stream instance
 * @return Current state
 */
dap_stream_state_t dap_http2_stream_get_state(dap_http2_stream_t *a_stream);

/**
 * @brief Set stream state (protocol-specific interpretation)
 * @param a_stream Stream instance
 * @param a_state New state
 */
void dap_http2_stream_set_state(dap_http2_stream_t *a_stream,
                               dap_stream_state_t a_state);

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
 * @brief Get current protocol name from active callback
 * @param a_stream Stream instance
 * @return Protocol name string ("HTTP", "WebSocket", "Binary", "SSE", "Custom", "Unknown")
 */
const char* dap_http2_stream_get_protocol_name(const dap_http2_stream_t *a_stream);

// NOTE: State string functions are now protocol-specific
// Each protocol should implement its own state_to_str function

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

// === HANDSHAKE MANAGEMENT (for custom protocols) ===

/**
 * @brief Set handshake handlers for custom protocol handling
 * @param a_stream Stream instance
 * @param a_handlers Handshake handlers configuration
 * @return 0 on success, negative on error
 */
int dap_http2_stream_set_handshake_handlers(dap_http2_stream_t *a_stream, 
                                           const dap_stream_handshake_handlers_t *a_handlers);

/**
 * @brief Check if stream has handshake handlers (is custom protocol)
 * @param a_stream Stream instance
 * @return true if has handshake handlers
 */
bool dap_http2_stream_has_handshake_handlers(const dap_http2_stream_t *a_stream);

// === EMBEDDED TRANSITIONS API ===

/**
 * @brief Simple protocol transition (for use in read_callbacks)
 * @param a_stream Stream instance
 * @param a_new_callback New read callback
 * @param a_new_context New context for callback
 * @return 0 on success, negative on error
 */
int dap_http2_stream_transition_protocol(dap_http2_stream_t *a_stream,
                                        dap_stream_read_callback_t a_new_callback,
                                        void *a_new_context);

/**
 * @brief Request session encryption upgrade from stream
 * @param a_stream Stream instance
 * @param a_encryption_type Target encryption type
 * @param a_key_data Encryption key data (optional)
 * @param a_key_size Key data size
 * @return 0 on success, negative on error
 */
int dap_http2_stream_request_session_encryption(dap_http2_stream_t *a_stream,
                                               int a_encryption_type,  // dap_session_encryption_type_t
                                               const void *a_key_data,
                                               size_t a_key_size);

#ifdef __cplusplus
}
#endif 