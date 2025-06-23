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
dap_http2_stream_t* dap_http2_stream_create(dap_http2_session_t *a_session,
                                           dap_http2_protocol_type_t a_initial_protocol)
{
    // TODO: Implementation
    // - Allocate stream structure
    // - Initialize with capacity=1 for channels array
    // - Set session reference
    // - Set initial protocol
    // - Initialize all callbacks to NULL
    return NULL;
}

/**
 * @brief Delete stream and cleanup resources
 */
void dap_http2_stream_delete(dap_http2_stream_t *a_stream)
{
    // TODO: Implementation
    // - Clear all channels
    // - Free channels array
    // - Free receive buffer
    // - Free stream structure
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
 * @brief Set channel event callback
 */
void dap_http2_stream_set_channel_event_callback(dap_http2_stream_t *a_stream,
                                                dap_stream_channel_event_callback_t a_callback,
                                                void *a_context)
{
    // TODO: Implementation
}

// === DYNAMIC CHANNEL MANAGEMENT ===

/**
 * @brief Set channel callback (creates channel if not exists)
 */
int dap_http2_stream_set_channel_callback(dap_http2_stream_t *a_stream,
                                         uint8_t a_channel_id,
                                         dap_stream_channel_callback_t a_callback,
                                         void *a_context)
{
    // TODO: Implementation
    // - Find existing channel or create new
    // - Expand channels array if needed
    // - Set callback and context
    // - Fire ADDED event
    return -1;
}

/**
 * @brief Add multiple channels from array
 */
int dap_http2_stream_add_channels_array(dap_http2_stream_t *a_stream,
                                       const dap_stream_channel_config_t *a_configs,
                                       size_t a_count)
{
    // TODO: Implementation
    // - Calculate total memory needed
    // - Expand channels array once
    // - Add all channels in loop
    // - Fire ADDED events for each
    return -1;
}

/**
 * @brief Remove channel callback and cleanup
 */
int dap_http2_stream_remove_channel_callback(dap_http2_stream_t *a_stream,
                                           uint8_t a_channel_id)
{
    // TODO: Implementation
    // - Find channel by ID
    // - Mark as inactive
    // - Fire REMOVED event
    // - Clear last_used_channel cache if needed
    return -1;
}

/**
 * @brief Clear all channel callbacks and free memory
 */
void dap_http2_stream_clear_all_channels(dap_http2_stream_t *a_stream)
{
    // TODO: Implementation
    // - Free channels array
    // - Reset capacity and count
    // - Clear last_used_channel cache
    // - Fire CLEARED event
}

// === CHANNEL QUERIES ===

/**
 * @brief Check if stream has any active channels
 */
bool dap_http2_stream_has_channels(const dap_http2_stream_t *a_stream)
{
    // TODO: Implementation
    return false;
}

/**
 * @brief Get count of active channels
 */
size_t dap_http2_stream_get_active_channels_count(const dap_http2_stream_t *a_stream)
{
    // TODO: Implementation
    return 0;
}

/**
 * @brief Check if specific channel is active
 */
bool dap_http2_stream_is_channel_active(const dap_http2_stream_t *a_stream,
                                       uint8_t a_channel_id)
{
    // TODO: Implementation
    return false;
}

/**
 * @brief Get list of active channel IDs
 */
size_t dap_http2_stream_get_active_channels(const dap_http2_stream_t *a_stream,
                                          uint8_t *a_channels_out,
                                          size_t a_max_channels)
{
    // TODO: Implementation
    return 0;
}

// === CHANNEL HELPERS ===

/**
 * @brief Helper to dispatch data to channel (for use in read_callback implementations)
 */
size_t dap_http2_stream_dispatch_to_channel(dap_http2_stream_t *a_stream,
                                           uint8_t a_channel_id,
                                           const void *a_data,
                                           size_t a_data_size)
{
    // TODO: Implementation
    // - Check last_used_channel cache first
    // - Find channel by ID
    // - Call channel callback
    // - Update last_used_channel cache
    return 0;
}

/**
 * @brief Helper to check if stream is in single-stream mode
 */
bool dap_http2_stream_is_single_stream_mode(const dap_http2_stream_t *a_stream)
{
    // TODO: Implementation
    return true;
}

// === PROTOCOL SWITCHING ===

/**
 * @brief Switch stream to different protocol mode
 */
int dap_http2_stream_switch_protocol(dap_http2_stream_t *a_stream,
                                    dap_http2_protocol_type_t a_new_protocol)
{
    // TODO: Implementation
    return -1;
}

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

// === STATE MANAGEMENT ===

/**
 * @brief Get current stream state
 */
dap_http2_stream_state_t dap_http2_stream_get_state(dap_http2_stream_t *a_stream)
{
    // TODO: Implementation
    return DAP_HTTP2_STREAM_STATE_IDLE;
}

/**
 * @brief Set stream state
 */
void dap_http2_stream_set_state(dap_http2_stream_t *a_stream,
                               dap_http2_stream_state_t a_state)
{
    // TODO: Implementation
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
 * @brief Get protocol type string representation
 */
const char* dap_http2_stream_protocol_to_str(dap_http2_protocol_type_t a_protocol)
{
    switch (a_protocol) {
        case DAP_HTTP2_PROTOCOL_HTTP:       return "HTTP";
        case DAP_HTTP2_PROTOCOL_WEBSOCKET:  return "WebSocket";
        case DAP_HTTP2_PROTOCOL_SSE:        return "SSE";
        case DAP_HTTP2_PROTOCOL_BINARY:     return "Binary";
        case DAP_HTTP2_PROTOCOL_RAW:        return "Raw";
        default:                            return "Unknown";
    }
}

/**
 * @brief Get state string representation
 */
const char* dap_http2_stream_state_to_str(dap_http2_stream_state_t a_state)
{
    switch (a_state) {
        case DAP_HTTP2_STREAM_STATE_IDLE:           return "IDLE";
        case DAP_HTTP2_STREAM_STATE_REQUEST_SENT:   return "REQUEST_SENT";
        case DAP_HTTP2_STREAM_STATE_HEADERS:        return "HEADERS";
        case DAP_HTTP2_STREAM_STATE_BODY:           return "BODY";
        case DAP_HTTP2_STREAM_STATE_COMPLETE:       return "COMPLETE";
        case DAP_HTTP2_STREAM_STATE_ERROR:          return "ERROR";
        case DAP_HTTP2_STREAM_STATE_UPGRADED:       return "UPGRADED";
        case DAP_HTTP2_STREAM_STATE_CLOSING:        return "CLOSING";
        case DAP_HTTP2_STREAM_STATE_CLOSED:         return "CLOSED";
        default:                                    return "Unknown";
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