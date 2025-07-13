/**
 * @file dap_http2_stream.c
 * @brief HTTP/2 Stream implementation with dynamic channel callback architecture
 */

#include <stdatomic.h>  // For atomic_store
#include <inttypes.h>   // For PRIu64
#include "dap_http2_stream.h"
#include "dap_http2_session.h"
#include "dap_common.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_http2_stream"

// === PRIVATE IMPLEMENTATION (Hidden from header) ===
typedef struct dap_http2_stream_private {
    // === UNIFIED BUFFER ===
    uint8_t *receive_buffer;
    size_t receive_buffer_size;
    size_t receive_buffer_capacity;
    
    // === HTTP PARSER STATE ===
    dap_http_parser_state_t parser_state;
    size_t content_length;
    size_t content_received;
    bool is_chunked;
    
    // === STREAM MANAGEMENT ===
    bool is_autonomous;
    
    // === APPLICATION TIMEOUTS ===
    void *read_timer;  // dap_timerfd_t
    uint64_t read_timeout_ms;
    
    // === EVENT CALLBACKS ===
    dap_stream_event_callback_t event_callback;
    void *event_callback_context;
    
    dap_stream_state_changed_cb_t state_changed_cb;
    void *state_changed_context;
    
    // === HANDSHAKE HANDLERS ===
    dap_stream_handshake_handlers_t *handshake_handlers;
    
#ifdef DAP_STREAM_CHANNELS_ENABLED
    // === CHANNEL MULTIPLEXING ===
    dap_stream_channel_context_t *channel_context;
#endif
    
} dap_http2_stream_private_t;

// === USER IDEA V2 CONSTANTS ===
static const size_t s_stream_pvt_alignof = _Alignof(dap_http2_stream_private_t),
    s_stream_pvt_offt = (sizeof(dap_http2_stream_t) + s_stream_pvt_alignof - 1) & ~(s_stream_pvt_alignof - 1),
    s_stream_full_size = s_stream_pvt_offt + sizeof(dap_http2_stream_private_t);

// === PRIVATE DATA ACCESS ===
// Прямой доступ через stream->private_data (типизированный указатель)

// === STREAM LIFECYCLE ===

/**
 * @brief Get stream UID (atomic read)
 * @param a_stream Stream instance
 * @return Stream UID or 0 if invalid stream
 */
uint64_t dap_http2_stream_get_uid(const dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return 0;
    }
    return atomic_load(&a_stream->uid);
}

/**
 * @brief Get worker ID from stream UID
 * @param a_stream Stream instance
 * @return Worker ID or 0 if invalid
 */
uint8_t dap_http2_stream_get_worker_id(const dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return 0;
    }
    uint64_t l_uid = atomic_load(&a_stream->uid);
    return dap_stream_uid_extract_worker_id(l_uid);
}

/**
 * @brief Get esocket UID from stream UID
 * @param a_stream Stream instance  
 * @return Esocket UID or 0 if invalid
 */
uint32_t dap_http2_stream_get_esocket_uid(const dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return 0;
    }
    uint64_t l_uid = atomic_load(&a_stream->uid);
    return dap_stream_uid_extract_esocket_uid(l_uid);
}

/**
 * @brief Create new stream with User Idea V2 private data allocation
 */
dap_http2_stream_t* dap_http2_stream_create(dap_http2_session_t *a_session)
{
    if (!a_session) {
        return NULL;
    }
    
    // === User Idea V2 Single Block Allocation ===
    dap_http2_stream_t *l_stream = DAP_NEW_Z_SIZE(dap_http2_stream_t, s_stream_full_size);
    if (!l_stream) {
        return NULL;
    }
    
    // USER IDEA V2: private_data points to offset from STRUCTURE  
    l_stream->private_data = (struct dap_http2_stream_private*)((uint8_t*)l_stream + s_stream_pvt_offt);
    
    // Basic initialization
    l_stream->session = a_session;
    
    // === SET STREAM UID (worker_id + esocket_uid) ===
    // Get session worker and esocket through internal API
    dap_worker_t *l_worker = dap_http2_session_get_worker(a_session);
    dap_events_socket_t *l_es = dap_http2_session_get_events_socket(a_session);
    
    if (l_es) {
        // Extract worker_id and esocket_uid from session
        uint8_t l_worker_id = l_worker ? l_worker->id : 0;
        uint32_t l_esocket_uid = l_es->uuid;  // esocket UUID is our unique identifier
        
        // Compose Stream UID: worker_id(8 bits) + esocket_uid(32 bits) + reserved(24 bits)
        uint64_t l_stream_uid = dap_stream_uid_compose(l_worker_id, l_esocket_uid);
        
        // Set atomic stream UID
        atomic_store(&l_stream->uid, l_stream_uid);
        
        log_it(L_DEBUG, "Stream UID set: worker_id=%u, esocket_uid=%u, stream_uid=%"PRIu64,
               l_worker_id, l_esocket_uid, l_stream_uid);
    } else {
        log_it(L_WARNING, "Session has no event socket - Stream UID set to 0");
        atomic_store(&l_stream->uid, 0);
    }
    
    log_it(L_DEBUG, "Stream %p created with User Idea V2: private_data=%p", 
           l_stream, l_stream->private_data);
    
    return l_stream;
}

/**
 * @brief Delete stream and cleanup User Idea V2 single block
 */
void dap_http2_stream_delete(dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return;
    }
    
    // Get private data before cleanup
    dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (l_private) {
        // Free receive buffer
        if (l_private->receive_buffer) {
            DAP_DELETE(l_private->receive_buffer);
        }
        
        // Free handshake handlers
        if (l_private->handshake_handlers) {
            DAP_DELETE(l_private->handshake_handlers);
        }
        
        // Clear all channels if enabled
        #ifdef DAP_STREAM_CHANNELS_ENABLED
        if (l_private->channel_context) {
            dap_stream_channel_context_delete(l_private->channel_context);
        }
        #endif
    }
    
    // USER IDEA V2: Free single memory block (structure is at the beginning)
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
    if (!a_stream) {
        return;
    }
    
    // Callbacks remain PUBLIC for performance
    a_stream->callbacks.read_cb = a_callback;
    a_stream->callback_context = a_context;
    
    log_it(L_DEBUG, "Stream %p read callback set to %p", a_stream, a_callback);
}

/**
 * @brief Set event callback
 */
void dap_http2_stream_set_event_callback(dap_http2_stream_t *a_stream,
                                        dap_stream_event_callback_t a_callback,
                                        void *a_context)
{
    if (!a_stream) {
        return;
    }
    
    dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (!l_private) {
        return;
    }
    
    l_private->event_callback = a_callback;
    l_private->event_callback_context = a_context;
    
    log_it(L_DEBUG, "Stream %p event callback set to %p", a_stream, a_callback);
}

/**
 * @brief Set state change callback
 */
void dap_http2_stream_set_state_changed_callback(dap_http2_stream_t *a_stream,
                                                 dap_stream_state_changed_cb_t a_callback,
                                                 void *a_context)
{
    if (!a_stream) {
        return;
    }
    
    dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (!l_private) {
        return;
    }
    
    l_private->state_changed_cb = a_callback;
    l_private->state_changed_context = a_context;
    
    log_it(L_DEBUG, "Stream %p state change callback set to %p", a_stream, a_callback);
}

/**
 * @brief Set stream state with callback notification
 */
void dap_http2_stream_set_state(dap_http2_stream_t *a_stream,
                               dap_stream_state_t a_state)
{
    if (!a_stream) {
        return;
    }
    
    dap_stream_state_t l_old_state = a_stream->state;
    a_stream->state = a_state;
    
    // Call state changed callback if set
    dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (l_private && l_private->state_changed_cb) {
        l_private->state_changed_cb(a_stream, l_old_state, a_state, l_private->state_changed_context);
    }
}

/**
 * @brief Get current stream state
 */
dap_stream_state_t dap_http2_stream_get_state(dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return -1;  // Invalid state
    }
    return a_stream->state;
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
    if (!a_stream) {
        log_it(L_ERROR, "Stream is NULL in dap_http2_stream_process_data");
        return 0;
    }
    
    if (!a_data || a_data_size == 0) {
        log_it(L_WARNING, "Empty data in dap_http2_stream_process_data");
        return 0;
    }
    
    if (!a_stream->callbacks.read_cb) {
        log_it(L_ERROR, "Stream %p has no read callback", a_stream);
        return 0;
    }
    
    log_it(L_DEBUG, "Processing %zu bytes through stream %p read callback", a_data_size, a_stream);
    
    // Forward to current read callback
    return a_stream->callbacks.read_cb(a_stream, a_data, a_data_size);
}

// REMOVED: dap_http2_stream_write_data - redundant wrapper
// Use dap_http2_session_write_direct_stream directly as the single universal write function

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
 * @brief Check if stream is autonomous
 */
bool dap_http2_stream_is_autonomous(dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return false;
    }
    
    const dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (!l_private) {
        return false;
    }
    
    return l_private->is_autonomous;
}

// === UTILITY FUNCTIONS ===

/**
 * @brief Get current protocol name from active callback
 */
const char* dap_http2_stream_get_protocol_name(const dap_http2_stream_t *a_stream)
{
    if (!a_stream || !a_stream->callbacks.read_cb) {
        return "Unknown";
    }
    
    if (a_stream->callbacks.read_cb == dap_http2_stream_read_callback_http_client ||
        a_stream->callbacks.read_cb == dap_http2_stream_read_callback_http_server) {
        return "HTTP";
    } else if (a_stream->callbacks.read_cb == dap_http2_stream_read_callback_websocket) {
        return "WebSocket";
    } else if (a_stream->callbacks.read_cb == dap_http2_stream_read_callback_binary) {
        return "Binary";
    } else if (a_stream->callbacks.read_cb == dap_http2_stream_read_callback_sse) {
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

/**
 * @brief Set handshake handlers
 */
int dap_http2_stream_set_handshake_handlers(dap_http2_stream_t *a_stream, 
                                           const dap_stream_handshake_handlers_t *a_handlers)
{
    if (!a_stream || !a_handlers) {
        return -1;
    }
    
    dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (!l_private) {
        return -1;
    }
    
    // Allocate handlers if not already allocated
    if (!l_private->handshake_handlers) {
        l_private->handshake_handlers = DAP_NEW_Z(dap_stream_handshake_handlers_t);
        if (!l_private->handshake_handlers) {
            return -1;
        }
    }
    
    // Copy handlers
    *l_private->handshake_handlers = *a_handlers;
    
    log_it(L_DEBUG, "Stream %p handshake handlers set", a_stream);
    return 0;
}

/**
 * @brief Check if stream has handshake handlers
 */
bool dap_http2_stream_has_handshake_handlers(const dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return false;
    }
    
    const dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (!l_private) {
        return false;
    }
    
    return l_private->handshake_handlers != NULL;
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

/**
 * @brief Set channel context for stream
 */
void dap_http2_stream_set_channel_context(dap_http2_stream_t *a_stream, dap_stream_channel_context_t *a_context)
{
    if (!a_stream) {
        return;
    }
    
    dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (!l_private) {
        return;
    }
    
    l_private->channel_context = a_context;
}

/**
 * @brief Get channel context from stream
 */
dap_stream_channel_context_t* dap_http2_stream_get_channel_context(const dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return NULL;
    }
    
    const dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (!l_private) {
        return NULL;
    }
    
    return l_private->channel_context;
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
    a_stream->callbacks.read_cb = a_new_callback;
    a_stream->callback_context = a_new_context;
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

/**
 * @brief Set read timeout
 */
void dap_http2_stream_set_read_timeout(dap_http2_stream_t *a_stream,
                                       uint64_t a_read_timeout_ms)
{
    if (!a_stream) {
        return;
    }
    
    dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (!l_private) {
        return;
    }
    
    l_private->read_timeout_ms = a_read_timeout_ms;
    
    log_it(L_DEBUG, "Stream %p read timeout set to %"PRIu64" ms", a_stream, a_read_timeout_ms);
}

/**
 * @brief Get read timeout
 */
uint64_t dap_http2_stream_get_read_timeout(const dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return 0;
    }
    
    const dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (!l_private) {
        return 0;
    }
    
    return l_private->read_timeout_ms;
}

/**
 * @brief Start read timer
 */
int dap_http2_stream_start_read_timer(dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return -1;
    }
    
    dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (!l_private || !l_private->read_timer) {
        return -1;
    }
    
    // TODO: Implementation - start timer
    return -1;
}

/**
 * @brief Stop read timer
 */
void dap_http2_stream_stop_read_timer(dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return;
    }
    
    dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (!l_private || !l_private->read_timer) {
        return;
    }
    
    // TODO: Implementation - stop timer
}

/**
 * @brief Reset read timer (restart with same timeout)
 */
int dap_http2_stream_reset_read_timer(dap_http2_stream_t *a_stream)
{
    if (!a_stream) {
        return -1;
    }
    
    dap_http2_stream_private_t *l_private = a_stream->private_data;
    if (!l_private || !l_private->read_timer) {
        return -1;
    }
    
    // TODO: Reset read timer
    log_it(L_DEBUG, "Resetting read timer for stream %p", a_stream);
    return -1;
} 