/**
 * @file dap_http2_stream.c
 * @brief HTTP/2 Stream implementation with dynamic channel callback architecture
 */

#include "dap_http2_stream.h"
#include "dap_http2_session.h"
#include "dap_common.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_http2_stream"

// === STREAM LIFECYCLE ===

/**
 * @brief Create new stream
 */
dap_http2_stream_t* dap_http2_stream_create(dap_http2_session_t *a_session)
{
    if (!a_session) {
        return NULL;
    }
    
    dap_http2_stream_t *l_stream = DAP_NEW_Z(dap_http2_stream_t);
    if (!l_stream) {
        return NULL;
    }
    
    // Basic initialization
    l_stream->session = a_session;
    
    // Initialize callbacks to NULL
    l_stream->read_callback = NULL;
    l_stream->read_callback_context = NULL;
    
    return l_stream;
}

/**
 * @brief Delete stream and cleanup resources
 */
void dap_http2_stream_delete(dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return;
    }
    
    // Clear all channels
    dap_http2_stream_clear_all_channels(a_stream);
    
    // Free receive buffer
    if (a_stream->receive_buffer) {
        DAP_DELETE(a_stream->receive_buffer);
    }
    
    // Free handshake handlers
    if (a_stream->handshake_handlers) {
        DAP_DELETE(a_stream->handshake_handlers);
    }
    
    // Free stream structure
    DAP_DELETE(a_stream);
}

// === MAIN CALLBACK MANAGEMENT ===

/**
 * @brief Set main read callback
 */
void dap_http2_stream_set_read_callback(dap_http2_stream_t *a_stream,
                                       dap_stream_read_callback_t a_callback,
                                       void *a_context)
{
    // TODO: Implementation
}

/**
 * @brief Set event callback
 */
void dap_http2_stream_set_event_callback(dap_http2_stream_t *a_stream,
                                        dap_stream_event_callback_t a_callback,
                                        void *a_context)
{
    // TODO: Implementation
}

/**
 * @brief Set state changed callback
 */
void dap_http2_stream_set_state_changed_callback(dap_http2_stream_t *a_stream,
                                                 dap_stream_state_changed_cb_t a_callback,
                                                 void *a_context)
{
    if (!a_stream) {
        return;
    }
    a_stream->state_changed_cb = a_callback;
    a_stream->state_changed_context = a_context;
}

// === DYNAMIC CHANNEL MANAGEMENT ===



// === PROTOCOL SWITCHING ===

// === DATA PROCESSING ===

/**
 * @brief Process incoming data through current read callback
 */
size_t dap_http2_stream_process_data(dap_http2_stream_t *a_stream,
                                    const void *a_data,
                                    size_t a_data_size)
{
    // TODO: Implementation
    return 0;
}

// === BUILT-IN READ CALLBACKS ===

/**
 * @brief HTTP protocol read callback for CLIENT mode
 */
size_t dap_http2_stream_read_callback_http_client(dap_http2_stream_t *a_stream,
                                                 const void *a_data,
                                                 size_t a_data_size)
{
    // TODO: Implementation - parse HTTP responses
    // - Check if channels are active
    // - If single-stream mode: process HTTP response directly
    // - If multi-channel mode: extract channel info and dispatch
    return 0;
}

/**
 * @brief HTTP protocol read callback for SERVER mode
 */
size_t dap_http2_stream_read_callback_http_server(dap_http2_stream_t *a_stream,
                                                 const void *a_data,
                                                 size_t a_data_size)
{
    // TODO: Implementation - parse HTTP requests
    return 0;
}

/**
 * @brief WebSocket protocol read callback
 */
size_t dap_http2_stream_read_callback_websocket(dap_http2_stream_t *a_stream,
                                               const void *a_data,
                                               size_t a_data_size)
{
    // TODO: Implementation - parse WebSocket frames
    // - Parse WebSocket frame header
    // - Extract channel info if present
    // - Dispatch to appropriate channel or process directly
    return 0;
}

/**
 * @brief Binary data read callback (with channel dispatching)
 */
size_t dap_http2_stream_read_callback_binary(dap_http2_stream_t *a_stream,
                                            const void *a_data,
                                            size_t a_data_size)
{
    // TODO: Implementation - this is the main channel dispatcher
    // - Parse binary packet header
    // - Extract channel_id
    // - Call dap_http2_stream_dispatch_to_channel()
    return 0;
}

/**
 * @brief Server-Sent Events read callback
 */
size_t dap_http2_stream_read_callback_sse(dap_http2_stream_t *a_stream,
                                         const void *a_data,
                                         size_t a_data_size)
{
    // TODO: Implementation - parse SSE events
    return 0;
}

/**
 * @brief Check if stream is in error state
 */
bool dap_http2_stream_is_error(dap_http2_stream_t *a_stream)
{
    // TODO: Implementation
    return false;
}

/**
 * @brief Check if stream is autonomous (can exist without client)
 */
bool dap_http2_stream_is_autonomous(dap_http2_stream_t *a_stream)
{
    // TODO: Implementation
    return false;
}

// === UTILITY FUNCTIONS ===

/**
 * @brief Get current protocol name from active callback
 */
const char* dap_http2_stream_get_protocol_name(const dap_http2_stream_t *a_stream)
{
    if (!a_stream || !a_stream->read_callback) {
        return "Unknown";
    }
    
    if (a_stream->read_callback == dap_http2_stream_read_callback_http_client ||
        a_stream->read_callback == dap_http2_stream_read_callback_http_server) {
        return "HTTP";
    } else if (a_stream->read_callback == dap_http2_stream_read_callback_websocket) {
        return "WebSocket";
    } else if (a_stream->read_callback == dap_http2_stream_read_callback_binary) {
        return "Binary";
    } else if (a_stream->read_callback == dap_http2_stream_read_callback_sse) {
        return "SSE";
    } else {
        return "Custom";
    }
}

// === CONVENIENCE FUNCTIONS ===

void dap_http2_stream_set_http_client_mode(dap_http2_stream_t *a_stream)
{
    // TODO: Set stream->read_callback = dap_http2_stream_read_callback_http_client
}

void dap_http2_stream_set_http_server_mode(dap_http2_stream_t *a_stream)
{
    // TODO: Set stream->read_callback = dap_http2_stream_read_callback_http_server
}

void dap_http2_stream_set_websocket_mode(dap_http2_stream_t *a_stream)
{
    // TODO: Set stream->read_callback = dap_http2_stream_read_callback_websocket
}

void dap_http2_stream_set_binary_mode(dap_http2_stream_t *a_stream)
{
    // TODO: Set stream->read_callback = dap_http2_stream_read_callback_binary
}

// === HANDSHAKE MANAGEMENT ===

int dap_http2_stream_set_handshake_handlers(dap_http2_stream_t *a_stream, 
                                           const dap_stream_handshake_handlers_t *a_handlers)
{
    if (!a_stream || !a_handlers) {
        return -1;
    }
    
    // Allocate handshake handlers if not exists
    if (!a_stream->handshake_handlers) {
        a_stream->handshake_handlers = DAP_NEW_Z(dap_stream_handshake_handlers_t);
        if (!a_stream->handshake_handlers) {
            return -1;
        }
    }
    
    // Copy handlers
    *a_stream->handshake_handlers = *a_handlers;
    return 0;
}

bool dap_http2_stream_has_handshake_handlers(const dap_http2_stream_t *a_stream)
{
    return a_stream && a_stream->handshake_handlers;
}

#ifdef DAP_STREAM_CHANNELS_ENABLED

// === CHANNEL CONTEXT MANAGEMENT ===

dap_stream_channel_context_t* dap_stream_channel_context_create(const dap_stream_channel_template_t *a_template)
{
    if (!a_template) {
        log_it(L_ERROR, "Template is NULL");
        return NULL;
    }
    
    dap_stream_channel_context_t *l_context = DAP_NEW_Z(dap_stream_channel_context_t);
    if (!l_context) {
        log_it(L_ERROR, "Memory allocation failed");
        return NULL;
    }
    
    // TODO: Copy callbacks from template
    log_it(L_DEBUG, "Created channel context %p", l_context);
    return l_context;
}

void dap_stream_channel_context_delete(dap_stream_channel_context_t *a_context)
{
    if (!a_context) {
        return;
    }
    
    log_it(L_DEBUG, "Deleting channel context %p", a_context);
    DAP_DELETE(a_context);
}

void dap_http2_stream_set_channel_context(dap_http2_stream_t *a_stream, dap_stream_channel_context_t *a_context)
{
    if (!a_stream) {
        log_it(L_ERROR, "Stream is NULL");
        return;
    }
    
    log_it(L_DEBUG, "Setting channel context %p for stream %p", a_context, a_stream);
    a_stream->channel_context = a_context;
}

dap_stream_channel_context_t* dap_http2_stream_get_channel_context(dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return NULL;
    }
    return a_stream->channel_context;
}

// === EXTERNAL CHANNEL MANAGEMENT ===

int dap_stream_channel_enable_by_uid(uint64_t a_channel_uid, 
                                    dap_stream_channel_callback_t a_callback,
                                    void *a_context)
{
    // TODO: Implementation - route to worker thread
    log_it(L_DEBUG, "Enable channel UID %llu", a_channel_uid);
    UNUSED(a_callback);
    UNUSED(a_context);
    return -1;
}

int dap_stream_channel_disable_by_uid(uint64_t a_channel_uid)
{
    // TODO: Implementation - route to worker thread
    log_it(L_DEBUG, "Disable channel UID %llu", a_channel_uid);
    return -1;
}

#endif // DAP_STREAM_CHANNELS_ENABLED 

// === PROTOCOL TRANSITIONS ===

int dap_http2_stream_transition_protocol(dap_http2_stream_t *a_stream,
                                        dap_stream_read_callback_t a_new_callback,
                                        void *a_new_context)
{
    if (!a_stream || !a_new_callback) {
        log_it(L_ERROR, "Invalid parameters for protocol transition");
        return -1;
    }
    
    log_it(L_DEBUG, "Transitioning stream %p protocol", a_stream);
    a_stream->read_callback = a_new_callback;
    a_stream->read_callback_context = a_new_context;
    return 0;
}

int dap_http2_stream_request_session_encryption(dap_http2_stream_t *a_stream,
                                               int a_encryption_type,
                                               const void *a_key_data,
                                               size_t a_key_size)
{
    if (!a_stream) {
        log_it(L_ERROR, "Stream is NULL");
        return -1;
    }
    
    // TODO: Request session encryption upgrade
    log_it(L_DEBUG, "Requesting session encryption %d for stream %p", a_encryption_type, a_stream);
    UNUSED(a_key_data);
    UNUSED(a_key_size);
    return -1;
}

// === TIMEOUT MANAGEMENT ===

void dap_http2_stream_set_read_timeout(dap_http2_stream_t *a_stream,
                                       uint64_t a_read_timeout_ms)
{
    if (!a_stream) {
        return;
    }
    a_stream->read_timeout_ms = a_read_timeout_ms;
}

uint64_t dap_http2_stream_get_read_timeout(const dap_http2_stream_t *a_stream)
{
    return a_stream ? a_stream->read_timeout_ms : 0;
}

int dap_http2_stream_start_read_timer(dap_http2_stream_t *a_stream)
{
    if (!a_stream || !a_stream->read_timeout_ms) {
        return -1;
    }
    
    // TODO: Start read timer
    log_it(L_DEBUG, "Starting read timer for stream %p (%llu ms)", a_stream, a_stream->read_timeout_ms);
    return -1;
}

void dap_http2_stream_stop_read_timer(dap_http2_stream_t *a_stream)
{
    if (!a_stream || !a_stream->read_timer) {
        return;
    }
    
    // TODO: Stop read timer
    log_it(L_DEBUG, "Stopping read timer for stream %p", a_stream);
}

int dap_http2_stream_reset_read_timer(dap_http2_stream_t *a_stream)
{
    if (!a_stream || !a_stream->read_timer) {
        return -1;
    }
    
    // TODO: Reset read timer
    log_it(L_DEBUG, "Resetting read timer for stream %p", a_stream);
    return -1;
} 