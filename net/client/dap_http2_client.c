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

#define _POSIX_C_SOURCE 200112L  // For CLOCK_REALTIME

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_http2_client.h"
#include "dap_http2_session.h" 
#include "dap_http2_stream.h"
#include "dap_worker.h"
#include "dap_http_header.h"
#include "dap_events_socket.h"
#include "http_status_code.h"
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>

#define LOG_TAG "dap_http2_client"

// === HTTP STREAM CONTEXT ===
// Bridge between Client Layer and Stream callbacks
// Contains all HTTP-specific state and logic adapted from dap_client_http.c

typedef enum {
        HTTP_PARSE_HEADERS = 0,
        HTTP_PARSE_BODY = 1,
        HTTP_PARSE_COMPLETE = 2
    } http_parse_state_t;
typedef struct dap_http_client_context {
    // === CLIENT REFERENCES ===
    dap_http2_client_t *client;                    // Back reference to client
    dap_http2_client_request_t *request;           // Current request being processed
    
    // === HTTP RESPONSE STATE (adapted from dap_client_http.c) ===
    http_status_code_t status_code;                // Cached HTTP status code
    // REMOVED: dap_http_header_t *response_headers;  // Lazy parsing - extract only needed values
    size_t content_length;                         // Content-Length header value
    bool is_chunked;                              // Transfer-Encoding: chunked
    char *content_type;                           // Content-Type header value (for streaming decision)
    
    // === HTTP PARSING STATE MACHINE ===
    http_parse_state_t parse_state;                // Current parsing state
    
    // === STREAMING LOGIC (adapted from dap_client_http.c) ===
    bool streaming_enabled;                        // Zero-copy streaming mode
    size_t streaming_threshold;                    // Size threshold for streaming decision
    size_t streamed_body_size;                     // Total bytes streamed (for progress)
    
    // === CHUNKED TRANSFER ENCODING (adapted from dap_client_http.c) ===
    bool reading_chunk_size;                       // Currently reading chunk size line
    size_t current_chunk_size;                     // Size of current chunk
    size_t current_chunk_read;                     // Bytes read from current chunk
    uint64_t current_chunk_id;                     // Unique chunk ID for integrity
    uint64_t next_chunk_id;                        // Counter for generating chunk IDs
    uint8_t chunked_error_count;                   // Error recovery counter
    
    // === RESPONSE BUFFER (adapted from dap_client_http.c) ===
    uint8_t *response_buffer;                      // Response body buffer
    size_t response_size;                          // Current response size
    size_t response_capacity;                      // Buffer capacity
    
    // === REDIRECT SUPPORT ===
    uint8_t redirect_count;                        // Current redirect counter
    uint8_t max_redirects;                         // Maximum allowed redirects (from config)
    bool follow_redirects;                         // Flag to enable/disable redirect following
    
    // === SYNC/ASYNC SUPPORT ===
    sem_t completion_semaphore;                    // Semaphore for sync requests (simpler than mutex+condvar)
    int error_code;                               // Error code for sync requests
    bool request_complete;                        // Completion flag
    
    // === TIMEOUTS ===
    time_t ts_last_read;                          // Last data received timestamp
    
} dap_http_client_context_t;

// === HTTP CONSTANTS (adapted from dap_client_http.c) ===
#define DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT (10 * 1024 * 1024) // 10MB maximum
#define DAP_CLIENT_HTTP_STREAMING_THRESHOLD_DEFAULT (1024 * 1024) // 1MB
#define DAP_CLIENT_HTTP_STREAMING_BUFFER_SIZE (128 * 1024) // 128KB optimal for streaming
#define DAP_CLIENT_HTTP_MAX_HEADERS_SIZE (16 * 1024) // 16KB max for headers
#define MAX_CHUNKED_PARSE_ERRORS 3
#define MAX_HTTP_REDIRECTS 5

// === CLEAR RETURN CODES ===
typedef enum {
    HTTP_PROCESS_SUCCESS = 1,         // Data processed successfully, can continue
    HTTP_PROCESS_NEED_MORE_DATA = 0,  // Need more data, wait for next call
    HTTP_PROCESS_ERROR = -1,          // Processing error, terminate connection
    HTTP_PROCESS_COMPLETE = -2,       // Processing complete, close connection
    HTTP_PROCESS_TRANSITION = -3      // Transition to another handler required
} http_process_result_t;

// === HTTP STREAM STATES (protocol-specific) ===
typedef enum {
    DAP_HTTP_STREAM_STATE_IDLE = 0,           // Stream created, no request sent
    DAP_HTTP_STREAM_STATE_REQUEST_SENT = 1,   // HTTP request sent, waiting for response
    DAP_HTTP_STREAM_STATE_HEADERS = 2,        // Receiving/parsing HTTP headers
    DAP_HTTP_STREAM_STATE_BODY = 3,           // Receiving HTTP body
    DAP_HTTP_STREAM_STATE_COMPLETE = 4,       // HTTP response complete
    DAP_HTTP_STREAM_STATE_ERROR = 5           // Error state
} dap_http_stream_state_t;

// === FORWARD DECLARATIONS ===
static dap_http_client_context_t* s_create_http_context(dap_http2_client_t *a_client, 
                                                        dap_http2_client_request_t *a_request);
static void s_destroy_http_context(dap_http_client_context_t *a_context);

// Session callbacks (define client role)
static void s_http_session_connected(dap_http2_session_t *a_session);
static void s_http_session_data_received(dap_http2_session_t *a_session, const void *a_data, size_t a_size);
static void s_http_session_error(dap_http2_session_t *a_session, int a_error);
static void s_http_session_closed(dap_http2_session_t *a_session);

// Specialized stream read callbacks (decomposed for efficiency)
static size_t s_http_stream_read_headers(dap_http2_stream_t *a_stream, const void *a_data, size_t a_size);
static size_t s_http_stream_initial_write(dap_http2_stream_t *a_stream, const void *a_data, size_t a_size);
static size_t s_http_stream_read_accumulation(dap_http2_stream_t *a_stream, const void *a_data, size_t a_size);
static size_t s_http_stream_read_streaming(dap_http2_stream_t *a_stream, const void *a_data, size_t a_size);
static size_t s_http_stream_read_chunked_streaming(dap_http2_stream_t *a_stream, const void *a_data, size_t a_size);

// HTTP protocol functions (adapted from dap_client_http.c)
static size_t s_request_formatted_size(const dap_http2_client_request_t *a_request);
static size_t s_calculate_formatted_size(const dap_http2_client_request_t *a_request);
static size_t s_format_http_request_to_buffer(const dap_http2_client_request_t *a_request, void *a_buffer, size_t a_buffer_size);
static http_process_result_t s_parse_http_headers(dap_http_client_context_t *a_context, const void *a_data, size_t a_size, size_t *a_consumed);
static http_process_result_t s_process_chunked_data(dap_http_client_context_t *a_context, const void *a_data, size_t a_size, size_t *a_consumed);
static http_process_result_t s_process_chunked_data_streaming(dap_http_client_context_t *a_context, const void *a_data, size_t a_size, size_t *a_consumed);

// Additional HTTP parsing functions

static const char s_http_header_location[] = "Location:", s_http_header_content_type[] = "Content-Type:", s_http_header_content_length[] = "Content-Length:",
    s_http_header_transfer_encoding[] = "Transfer-Encoding:";




// Completion handlers
static void s_complete_http_request(dap_http_client_context_t *a_context, int a_error_code);

// Redirect handlers
static int s_process_http_redirect(dap_http_client_context_t *a_context);
static bool s_is_redirect_status_code(http_status_code_t a_status);

// === HTTP METHOD IMPLEMENTATION ===
// Using common functions from dap_http_header.h

/**
 * @brief Calculate size of formatted HTTP request
 * @param a_request Request structure
 * @return Size of formatted request in bytes
 */
static size_t s_calculate_formatted_size(const dap_http2_client_request_t *a_request)
{
    if (!a_request || !a_request->path) {
        return 0;
    }
    
    const char *l_method_str = dap_http_method_to_str(a_request->method);
    if (!l_method_str) {
        return 0;
    }
    
    return strlen(l_method_str) + 1 +                                        // "METHOD "
           strlen(a_request->path) +                                         // "path"
           (a_request->query_string ? strlen(a_request->query_string) : 0) + // "?query" (for any method)
           11 +                                                             // " HTTP/1.1\r\n"
           a_request->headers_size +                                        // All headers (incl User-Agent, Content-Length)
           2 +                                                              // "\r\n" (final)
           (a_request->method != HTTP_GET ? a_request->body_size : 0);      // Body (for non-GET)
}

/**
 * @brief Get pre-calculated formatted size from request
 * @param a_request Request structure
 * @return Size of formatted request
 */
static size_t s_get_request_formatted_size(const dap_http2_client_request_t *a_request)
{
    return s_calculate_formatted_size(a_request);
}

// Default configuration values
#define DEFAULT_CONNECT_TIMEOUT_MS    20000  // 20 seconds
#define DEFAULT_READ_TIMEOUT_MS       5000   // 5 seconds
#define DEFAULT_MAX_RESPONSE_SIZE     (10 * 1024 * 1024)  // 10MB
#define DEFAULT_MAX_REDIRECTS         5

// Debug flag
static bool s_debug_more = false;

// === HTTP FORMATTING ===
// Минималистичный подход: строим запрос по частям

// === GLOBAL INITIALIZATION ===

/**
 * @brief Initialize HTTP2 client module
 * @return 0 on success, negative on error
 */
int dap_http2_client_init(void)
{
    log_it(L_INFO, "HTTP2 client module initialized");
    return 0;
}

/**
 * @brief Deinitialize HTTP2 client module
 */
void dap_http2_client_deinit(void)
{
    log_it(L_INFO, "HTTP2 client module deinitialized");
}

// === CLIENT LIFECYCLE ===

/**
 * @brief Create new HTTP2 client with default timeouts
 * @return New client instance or NULL on error
 */
dap_http2_client_t *dap_http2_client_create(dap_worker_t *a_worker)
{
    return dap_http2_client_create_with_timeouts(a_worker, 
                                                 DEFAULT_CONNECT_TIMEOUT_MS,
                                                 DEFAULT_READ_TIMEOUT_MS);
}

/**
 * @brief Create new HTTP2 client with custom timeouts
 * @param a_worker Worker thread
 * @param a_connect_timeout_ms Connect timeout in milliseconds
 * @param a_read_timeout_ms Read timeout in milliseconds
 * @return New client instance or NULL on error
 */
dap_http2_client_t *dap_http2_client_create_with_timeouts(dap_worker_t *a_worker,
                                                          uint64_t a_connect_timeout_ms,
                                                          uint64_t a_read_timeout_ms)
{
    if (!a_worker) {
        log_it(L_ERROR, "Worker is NULL");
        return NULL;
    }

    dap_http2_client_t *l_client = DAP_NEW_Z(dap_http2_client_t);
    if (!l_client) {
        log_it(L_CRITICAL, "Failed to allocate memory for HTTP2 client");
        return NULL;
    }

    // Initialize UID as invalid (will be set when session/stream created)
    atomic_store(&l_client->stream_uid, INVALID_STREAM_UID);
    
    // Initialize state
    atomic_store(&l_client->state, DAP_HTTP2_CLIENT_STATE_IDLE);
    
    // Set default configuration
    l_client->config = dap_http2_client_config_default();
    l_client->config.connect_timeout_ms = a_connect_timeout_ms;
    l_client->config.read_timeout_ms = a_read_timeout_ms;
    
    log_it(L_DEBUG, "Created HTTP2 client with timeouts: connect=%"DAP_UINT64_FORMAT_U"ms, read=%"DAP_UINT64_FORMAT_U"ms",
           a_connect_timeout_ms, a_read_timeout_ms);
    
    return l_client;
}

/**
 * @brief Delete client and cleanup resources
 * @param a_client Client to delete
 */
void dap_http2_client_delete(dap_http2_client_t *a_client)
{
    if (!a_client) {
        return;
    }
    
    log_it(L_DEBUG, "Deleting HTTP2 client");
    
    // Cancel any ongoing request
    dap_http2_client_cancel(a_client);
    
    // Clean up current request
    if (a_client->current_request) {
        dap_http2_client_request_delete(a_client->current_request);
        a_client->current_request = NULL;
    }
    
    // Clean up configuration strings
    DAP_DEL_MULTY(a_client->config.ssl_cert_path,
                  a_client->config.ssl_key_path,
                  a_client->config.ssl_ca_path);
    
    DAP_DELETE(a_client);
}

// === CONFIGURATION ===

/**
 * @brief Get default configuration (EFFICIENT: designated initializer)
 * @return Default configuration structure
 */
dap_http2_client_config_t dap_http2_client_config_default(void)
{
    return (dap_http2_client_config_t) {
        // Timeouts
        .connect_timeout_ms = DEFAULT_CONNECT_TIMEOUT_MS,
        .read_timeout_ms = DEFAULT_READ_TIMEOUT_MS,
        
        // Limits
        .max_response_size = DEFAULT_MAX_RESPONSE_SIZE,
        .max_redirects = DEFAULT_MAX_REDIRECTS,
        
        // Options
        .follow_redirects = true,
        .verify_ssl = true,
        .enable_compression = false,
        
        // SSL paths (NULL by default)
        .ssl_cert_path = NULL,
        .ssl_key_path = NULL,
        .ssl_ca_path = NULL
    };
}

/**
 * @brief Set client configuration
 * @param a_client Client instance
 * @param a_config Configuration structure
 */
void dap_http2_client_set_config(dap_http2_client_t *a_client, const dap_http2_client_config_t *a_config)
{
    if (!a_client || !a_config) {
        log_it(L_ERROR, "Invalid arguments in dap_http2_client_set_config");
        return;
    }
    
    // Clean up old config strings
    DAP_DEL_MULTY(a_client->config.ssl_cert_path,
                  a_client->config.ssl_key_path,
                  a_client->config.ssl_ca_path);
    
    // Copy configuration
    a_client->config = *a_config;
    
    // Duplicate strings
    a_client->config.ssl_cert_path = dap_strdup(a_config->ssl_cert_path);
    a_client->config.ssl_key_path = dap_strdup(a_config->ssl_key_path);
    a_client->config.ssl_ca_path = dap_strdup(a_config->ssl_ca_path);
    
    log_it(L_DEBUG, "Updated HTTP2 client configuration");
}

/**
 * @brief Get client configuration
 * @param a_client Client instance
 * @return Configuration structure
 */
dap_http2_client_config_t *dap_http2_client_get_config(dap_http2_client_t *a_client)
{
    if (!a_client) {
        log_it(L_ERROR, "Client is NULL in dap_http2_client_get_config");
        return NULL;
    }
    
    return &a_client->config;
}

/**
 * @brief Set client callbacks
 * @param a_client Client instance
 * @param a_callbacks Callbacks structure
 * @param a_callbacks_arg User argument for callbacks
 */
void dap_http2_client_set_callbacks(dap_http2_client_t *a_client,
                                    const dap_http2_client_callbacks_t *a_callbacks,
                                    void *a_callbacks_arg)
{
    if (!a_client) {
        log_it(L_ERROR, "Client is NULL in dap_http2_client_set_callbacks");
        return;
    }
    
    if (a_callbacks) {
        a_client->callbacks = *a_callbacks;
    } else {
        memset(&a_client->callbacks, 0, sizeof(a_client->callbacks));
    }
    
    a_client->callbacks_arg = a_callbacks_arg;
    
    log_it(L_DEBUG, "Set HTTP2 client callbacks: response=%p, error=%p, progress=%p",
           (void*)a_client->callbacks.response_cb,
           (void*)a_client->callbacks.error_cb,
           (void*)a_client->callbacks.progress_cb);
}

/**
 * @brief Set configuration timeouts
 * @param a_config Configuration structure
 * @param a_connect_timeout_ms Connect timeout in milliseconds
 * @param a_read_timeout_ms Read timeout in milliseconds
 */
void dap_http2_client_config_set_timeouts(dap_http2_client_config_t *a_config,
                                          uint64_t a_connect_timeout_ms,
                                          uint64_t a_read_timeout_ms)
{
    if (!a_config) {
        log_it(L_ERROR, "Config is NULL in dap_http2_client_config_set_timeouts");
        return;
    }
    
    a_config->connect_timeout_ms = a_connect_timeout_ms;
    a_config->read_timeout_ms = a_read_timeout_ms;
    
    log_it(L_DEBUG, "Set timeouts: connect=%"DAP_UINT64_FORMAT_U"ms, read=%"DAP_UINT64_FORMAT_U"ms",
           a_connect_timeout_ms, a_read_timeout_ms);
}

// === STATE QUERIES ===

/**
 * @brief Get current client state
 * @param a_client Client instance
 * @return Current client state
 */
dap_http2_client_state_t dap_http2_client_get_state(const dap_http2_client_t *a_client)
{
    if (!a_client) {
        return DAP_HTTP2_CLIENT_STATE_ERROR;
    }
    
    return atomic_load(&a_client->state);
}

/**
 * @brief Check if client is busy
 * @param a_client Client instance
 * @return true if busy
 */
bool dap_http2_client_is_busy(const dap_http2_client_t *a_client)
{
    if (!a_client) {
        return false;
    }
    
    dap_http2_client_state_t l_state = atomic_load(&a_client->state);
    return (l_state == DAP_HTTP2_CLIENT_STATE_REQUESTING || 
            l_state == DAP_HTTP2_CLIENT_STATE_RECEIVING);
}

/**
 * @brief Check if request is complete
 * @param a_client Client instance
 * @return true if complete
 */
bool dap_http2_client_is_complete(const dap_http2_client_t *a_client)
{
    if (!a_client) {
        return false;
    }
    
    dap_http2_client_state_t l_state = atomic_load(&a_client->state);
    return (l_state == DAP_HTTP2_CLIENT_STATE_COMPLETE);
}

/**
 * @brief Check if client is in error state
 * @param a_client Client instance
 * @return true if in error state
 */
bool dap_http2_client_is_error(const dap_http2_client_t *a_client)
{
    if (!a_client) {
        return true;
    }
    
    dap_http2_client_state_t l_state = atomic_load(&a_client->state);
    return (l_state == DAP_HTTP2_CLIENT_STATE_ERROR);
}

/**
 * @brief Check if client is in async mode (has callbacks)
 * @param a_client Client instance
 * @return true if async mode
 */
bool dap_http2_client_is_async(const dap_http2_client_t *a_client)
{
    if (!a_client) {
        return false;
    }
    
    return (a_client->callbacks.response_cb != NULL || 
            a_client->callbacks.error_cb != NULL);
}

/**
 * @brief Check if client is cancelled (from state)
 * @param a_client Client instance
 * @return true if cancelled
 */
bool dap_http2_client_is_cancelled(const dap_http2_client_t *a_client)
{
    if (!a_client) {
        return false;
    }
    
    dap_http2_client_state_t l_state = atomic_load(&a_client->state);
    return (l_state == DAP_HTTP2_CLIENT_STATE_CANCELLED);
}

/**
 * @brief Get stream UID from client
 * @param a_client Client instance
 * @return Stream UID or INVALID_STREAM_UID if not assigned
 */
uint64_t dap_http2_client_get_stream_uid(const dap_http2_client_t *a_client)
{
    if (!a_client) {
        return INVALID_STREAM_UID;
    }
    
    return atomic_load(&a_client->stream_uid);
}

/**
 * @brief Cancel ongoing request
 * @param a_client Client instance
 */
void dap_http2_client_cancel(dap_http2_client_t *a_client)
{
    if (!a_client) {
        return;
    }
    
    dap_http2_client_state_t l_old_state = atomic_load(&a_client->state);
    
    if (l_old_state == DAP_HTTP2_CLIENT_STATE_REQUESTING || 
        l_old_state == DAP_HTTP2_CLIENT_STATE_RECEIVING) {
        
        atomic_store(&a_client->state, DAP_HTTP2_CLIENT_STATE_CANCELLED);
        
        // TODO: Cancel session/stream via UID routing
        // uint64_t l_stream_uid = atomic_load(&a_client->stream_uid);
        // if (l_stream_uid != INVALID_STREAM_UID) {
        //     dap_http2_stream_cancel_by_uid(l_stream_uid);
        // }
        
        log_it(L_INFO, "HTTP2 client request cancelled");
        
        if (a_client->callbacks.error_cb) {
            a_client->callbacks.error_cb(a_client, DAP_HTTP2_CLIENT_ERROR_CANCELLED);
        }
    }
}

/**
 * @brief Close client connection
 * @param a_client Client instance
 */
void dap_http2_client_close(dap_http2_client_t *a_client)
{
    if (!a_client) {
        return;
    }
    
    // Cancel first
    dap_http2_client_cancel(a_client);
    
    // TODO: Close session via UID routing
    // uint64_t l_stream_uid = atomic_load(&a_client->stream_uid);
    // if (l_stream_uid != INVALID_STREAM_UID) {
    //     dap_http2_session_close_by_stream_uid(l_stream_uid);
    // }
    
    atomic_store(&a_client->stream_uid, INVALID_STREAM_UID);
    atomic_store(&a_client->state, DAP_HTTP2_CLIENT_STATE_IDLE);
    
    log_it(L_DEBUG, "HTTP2 client connection closed");
}

// === UTILITY FUNCTIONS ===

// === STRING REPRESENTATIONS (EFFICIENT: indexed arrays) ===

static const char* s_client_state_strings[] = {
    [DAP_HTTP2_CLIENT_STATE_IDLE] = "IDLE",
    [DAP_HTTP2_CLIENT_STATE_REQUESTING] = "REQUESTING", 
    [DAP_HTTP2_CLIENT_STATE_RECEIVING] = "RECEIVING",
    [DAP_HTTP2_CLIENT_STATE_COMPLETE] = "COMPLETE",
    [DAP_HTTP2_CLIENT_STATE_ERROR] = "ERROR",
    [DAP_HTTP2_CLIENT_STATE_CANCELLED] = "CANCELLED"
};

static const char* s_client_error_strings[] = {
    [DAP_HTTP2_CLIENT_ERROR_NONE] = "NONE",
    [DAP_HTTP2_CLIENT_ERROR_INVALID_URL] = "INVALID_URL",
    [DAP_HTTP2_CLIENT_ERROR_INVALID_METHOD] = "INVALID_METHOD",
    [DAP_HTTP2_CLIENT_ERROR_CONNECTION_FAILED] = "CONNECTION_FAILED",
    [DAP_HTTP2_CLIENT_ERROR_TIMEOUT] = "TIMEOUT",
    [DAP_HTTP2_CLIENT_ERROR_CANCELLED] = "CANCELLED",
    [DAP_HTTP2_CLIENT_ERROR_INTERNAL] = "INTERNAL",
    [DAP_HTTP2_CLIENT_ERROR_TOO_MANY_REDIRECTS] = "TOO_MANY_REDIRECTS",
    [DAP_HTTP2_CLIENT_ERROR_INVALID_REDIRECT_URL] = "INVALID_REDIRECT_URL",
    [DAP_HTTP2_CLIENT_ERROR_REDIRECT_LOOP] = "REDIRECT_LOOP",
    [DAP_HTTP2_CLIENT_ERROR_REDIRECT_WITHOUT_LOCATION] = "REDIRECT_WITHOUT_LOCATION"
};

/**
 * @brief Get client state string representation (EFFICIENT: O(1) array access)
 * @param a_state Client state
 * @return State name string
 */
const char* dap_http2_client_state_to_str(dap_http2_client_state_t a_state)
{
    if (a_state >= 0 && a_state < (int)(sizeof(s_client_state_strings) / sizeof(s_client_state_strings[0]))) {
        return s_client_state_strings[a_state];
    }
    return "UNKNOWN";
}

/**
 * @brief Get client error string representation (EFFICIENT: O(1) array access)
 * @param a_error Client error
 * @return Error name string
 */
const char* dap_http2_client_error_to_str(dap_http2_client_error_t a_error)
{
    if (a_error >= 0 && a_error < (int)(sizeof(s_client_error_strings) / sizeof(s_client_error_strings[0]))) {
        return s_client_error_strings[a_error];
    }
    return "UNKNOWN";
}

// === REQUEST MANAGEMENT ===

/**
 * @brief Create new request (EFFICIENT: enum initialization)
 * @return New request instance or NULL on error
 */
dap_http2_client_request_t *dap_http2_client_request_create(void)
{
    dap_http2_client_request_t *l_request = DAP_NEW_Z(dap_http2_client_request_t);
    if (!l_request) {
        log_it(L_CRITICAL, "Failed to allocate memory for HTTP2 client request");
        return NULL;
    }

    // EFFICIENT: Initialize with default values (no string allocations)
    l_request->method = HTTP_GET;  // Default to GET (enum, no allocation)
    l_request->port = 80;  // Default HTTP port

    // Add standard headers immediately
    dap_http2_client_request_add_header(l_request, "User-Agent", "Mozilla/5.0");

    log_it(L_DEBUG, "Created HTTP2 client request");
    return l_request;
}

/**
 * @brief Delete request and cleanup resources (EFFICIENT: no method cleanup)
 * @param a_request Request to delete
 */
void dap_http2_client_request_delete(dap_http2_client_request_t *a_request)
{
    if (!a_request) {
        return;
    }

    log_it(L_DEBUG, "Deleting HTTP2 client request");

    // EFFICIENT: Free only allocated strings (method is enum, no cleanup needed)
    DAP_DEL_MULTY(a_request->host,
                  a_request->path,
                  a_request->query_string,
                  a_request->body_data);
                  
    // Clean up headers list
    dap_http_headers_remove_all(&a_request->headers);
    DAP_DELETE(a_request);
}

/**
 * @brief Add single header to request
 * @param a_request Request instance
 * @param a_name Header name
 * @param a_value Header value
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_add_header(dap_http2_client_request_t *a_request, const char *a_name, const char *a_value)
{
    if (!a_request || !a_name || !a_value) {
        log_it(L_ERROR, "Invalid arguments in dap_http2_client_request_add_header");
        return -1;
    }

    uint32_t l_header_size = 0;
    dap_http_header_t *l_header = dap_http_header_add_ex(&a_request->headers, a_name, a_value, &l_header_size);
    if (!l_header) {
        log_it(L_ERROR, "Failed to add header: %s: %s", a_name, a_value);
        return -2;
    }

    a_request->headers_size += l_header_size;
    log_it(L_DEBUG, "Added header: %s: %s (total size: %zu)", a_name, a_value, a_request->headers_size);
    return 0;
}

/**
 * @brief Set request method (EFFICIENT: uses enum)
 * @param a_request Request instance
 * @param a_method HTTP method string
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_set_method(dap_http2_client_request_t *a_request, const char *a_method)
{
    if (!a_request || !a_method) {
        log_it(L_ERROR, "Invalid arguments in dap_http2_client_request_set_method");
        return -1;
    }

    // EFFICIENT: Parse to enum (optimized with early exit)
    dap_http_method_t l_method_enum = dap_http_method_from_str(a_method);
    if (l_method_enum == HTTP_INVALID) {
        log_it(L_ERROR, "Invalid HTTP method: %s", a_method);
        return -2;
    }

    // EFFICIENT: Store enum (no memory allocation needed)
    a_request->method = l_method_enum;

    log_it(L_DEBUG, "Set request method: %s", a_method);
    return 0;
}

/**
 * @brief Set request headers
 * @param a_request Request instance
 * @param a_headers Headers string
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_set_headers(dap_http2_client_request_t *a_request, const char *a_headers)
{
    if (!a_request) {
        log_it(L_ERROR, "Request is NULL in dap_http2_client_request_set_headers");
        return -1;
    }

    // Legacy compatibility: parse header string and add individual headers
    if (a_headers) {
        // TODO: Parse a_headers string and call dap_http2_client_request_add_header for each
        log_it(L_WARNING, "Legacy set_headers not fully implemented - use add_header instead");
        return -1;
    } else {
        log_it(L_DEBUG, "Cleared request headers");
    }

    return 0;
}

/**
 * @brief Set request body
 * @param a_request Request instance
 * @param a_data Body data
 * @param a_size Body size
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_set_body(dap_http2_client_request_t *a_request, const void *a_data, size_t a_size)
{
    if (!a_request) {
        log_it(L_ERROR, "Request is NULL in dap_http2_client_request_set_body");
        return -1;
    }

    // Clean up old body
    DAP_DEL_Z(a_request->body_data);  // EFFICIENT: no NULL check needed
    a_request->body_size = 0;

    // Set new body (can be NULL for GET requests)
    if (a_data && a_size > 0) {
        a_request->body_data = DAP_NEW_Z_SIZE(uint8_t, a_size);
        if (!a_request->body_data) {
            log_it(L_CRITICAL, "Failed to allocate memory for body data");
            return -2;
        }
        memcpy(a_request->body_data, a_data, a_size);
        a_request->body_size = a_size;
        
        // Add Content-Length header for non-GET methods
        if (a_request->method != HTTP_GET) {
            char l_length_str[32];
            snprintf(l_length_str, sizeof(l_length_str), "%zu", a_size);
            dap_http2_client_request_add_header(a_request, "Content-Length", l_length_str);
        }
        
        log_it(L_DEBUG, "Set request body: %zu bytes", a_size);
    } else {
        log_it(L_DEBUG, "Cleared request body");
    }

    return 0;
}

/**
 * @brief Set request content type (adds Content-Type header)
 * @param a_request Request instance
 * @param a_content_type Content type string (e.g., "application/json")
 * @return 0 on success, negative on error
 */
static int s_request_set_content_type(dap_http2_client_request_t *a_request, const char *a_content_type)
{
    if (!a_request) {
        log_it(L_ERROR, "Request is NULL in s_request_set_content_type");
        return -1;
    }

    // Add content type header using new system
    if (a_content_type) {
        return dap_http2_client_request_add_header(a_request, "Content-Type", a_content_type);
    }

    return 0;
}

// === HTTP CONTEXT MANAGEMENT ===

/**
 * @brief Create HTTP client context for session/stream callbacks
 * @param a_client HTTP client instance
 * @param a_request Request to process
 * @return New HTTP context or NULL on error
 */
static dap_http_client_context_t* s_create_http_context(dap_http2_client_t *a_client, 
                                                        dap_http2_client_request_t *a_request)
{
    if (!a_client || !a_request) {
        log_it(L_ERROR, "Invalid arguments for HTTP context creation");
        return NULL;
    }
    
    dap_http_client_context_t *l_context = DAP_NEW_Z(dap_http_client_context_t);
    if (!l_context) {
        log_it(L_CRITICAL, "Failed to allocate HTTP context");
        return NULL;
    }
    
    // === CLIENT REFERENCES ===
    l_context->client = a_client;
    l_context->request = a_request;
    
    // === ONLY NON-ZERO FIELDS (DAP_NEW_Z already zeroed everything) ===
    l_context->streaming_threshold = DAP_CLIENT_HTTP_STREAMING_THRESHOLD_DEFAULT;
    l_context->reading_chunk_size = true;  // Start reading chunk size for chunked mode
    l_context->ts_last_read = time(NULL);
    
    // === REDIRECT INITIALIZATION ===
    l_context->redirect_count = 0;
    l_context->max_redirects = a_client->config.max_redirects > 0 ? 
                              a_client->config.max_redirects : 
                              DAP_HTTP2_CLIENT_MAX_REDIRECTS_DEFAULT;
    l_context->follow_redirects = a_client->config.follow_redirects;
    
    // === HTTP REQUEST FORMATTING MOVED TO STREAM LAYER ===
    // HTTP request will be formatted and sent via initial_write callback
    
    // === SYNC SUPPORT ===
    if (sem_init(&l_context->completion_semaphore, 0, 0) != 0) {
        log_it(L_ERROR, "Failed to initialize completion semaphore: %s", strerror(errno));
        DAP_DELETE(l_context);
        return NULL;
    }
    
    log_it(L_DEBUG, "Created HTTP context for %s %s%s (formatting deferred to stream layer)", 
           dap_http_method_to_str(a_request->method), a_request->host, 
           a_request->path ? a_request->path : "");
    
    return l_context;
}

/**
 * @brief Destroy HTTP client context and cleanup resources
 * @param a_context HTTP context to destroy
 */
static void s_destroy_http_context(dap_http_client_context_t *a_context)
{
    if (!a_context) {
        return;
    }
    
    log_it(L_DEBUG, "Destroying HTTP context");
    sem_destroy(&a_context->completion_semaphore);
    dap_http_headers_remove_all(&a_context->request->headers);
    DAP_DEL_MULTY(a_context->content_type, a_context->response_buffer,
        a_context->request->host, a_context->request->path, a_context->request->query_string,
        a_context->request->body_data, a_context);
}

/**
 * @brief Complete HTTP request and signal waiting threads
 * @param a_context HTTP context
 * @param a_error_code Error code (0 = success)
 */
static void s_complete_http_request(dap_http_client_context_t *a_context, int a_error_code)
{
    if (!a_context) {
        return;
    }
    
    log_it(L_DEBUG, "Completing HTTP request with error code: %d", a_error_code);
    
    a_context->error_code = a_error_code;
    a_context->request_complete = true;
    
    // Signal semaphore to unblock sync requests
    sem_post(&a_context->completion_semaphore);
    
    // Call client callbacks for async requests
    if (a_context->client) {
        if (a_error_code == 0 && a_context->client->callbacks.response_cb) {
            a_context->client->callbacks.response_cb(
                a_context->client,
                a_context->status_code,
                a_context->response_buffer,
                a_context->response_size
            );
        } else if (a_error_code != 0 && a_context->client->callbacks.error_cb) {
            a_context->client->callbacks.error_cb(
                a_context->client,
                (dap_http2_client_error_t)a_error_code
            );
        }
    }
}

// === HTTP REQUEST FORMATTING ===

/**
 * @brief Format HTTP request string (DEPRECATED: replaced by s_format_http_request_to_buffer)
 * This function is kept for compatibility but should not be used in new code.
 * @param a_request Request to format
 * @param a_request_size Output: size of formatted request (excluding null terminator)
 * @return Formatted HTTP request string (caller must free) or NULL on error
 */
static char* s_format_http_request(const dap_http2_client_request_t *a_request, size_t *a_request_size)
{
    if (!a_request || !a_request_size) {
        log_it(L_ERROR, "Invalid request for HTTP formatting");
        return NULL;
    }
    
    // Use new formatting function
    size_t l_needed_size = s_calculate_formatted_size(a_request);
    if (l_needed_size == 0) {
        return NULL;
    }
    
    char *l_buffer = DAP_NEW_SIZE(char, l_needed_size + 1);
    if (!l_buffer) {
        log_it(L_CRITICAL, "Failed to allocate HTTP request buffer (%zu bytes)", l_needed_size);
        return NULL;
    }
    
    size_t l_actual_size = s_format_http_request_to_buffer(a_request, l_buffer, l_needed_size);
    if (l_actual_size == 0) {
        DAP_DELETE(l_buffer);
        return NULL;
    }
    
    l_buffer[l_actual_size] = '\0';
    *a_request_size = l_actual_size;
    
    return l_buffer;
}

/**
 * @brief Format HTTP request directly to buffer (zero-copy approach)
 * @param a_request Request to format
 * @param a_buffer Target buffer
 * @param a_buffer_size Buffer size
 * @return Number of bytes written to buffer, or 0 on error
 */
static size_t s_format_http_request_to_buffer(const dap_http2_client_request_t *a_request, void *a_buffer, size_t a_buffer_size)
{
    if (!a_request || !a_buffer || a_buffer_size == 0) {
        log_it(L_ERROR, "Invalid arguments for HTTP formatting");
        return 0;
    }
    
    // Check buffer size early
    size_t l_needed_size = s_get_request_formatted_size(a_request);
    if (l_needed_size > a_buffer_size) {
        log_it(L_ERROR, "Buffer too small: need %zu, have %zu", l_needed_size, a_buffer_size);
        return 0;
    }
    
    const char *l_method_str = dap_http_method_to_str(a_request->method);
    if (!l_method_str) {
        log_it(L_ERROR, "Invalid HTTP method: %d", a_request->method);
        return 0;
    }
    
    char *l_buf = (char *)a_buffer;
    size_t l_pos = 0;
    
    // 1. First line: "METHOD path HTTP/1.1\r\n"
    l_pos += snprintf(l_buf + l_pos, a_buffer_size - l_pos, "%s %s", 
                      l_method_str, a_request->path ? a_request->path : "/");
    
    // Add query string (for any method)
    if (a_request->query_string) {
        l_pos += snprintf(l_buf + l_pos, a_buffer_size - l_pos, "%s", a_request->query_string);
    }
    
    l_pos += snprintf(l_buf + l_pos, a_buffer_size - l_pos, " HTTP/1.1\r\n");
    
    // 2. Headers (все заголовки уже добавлены через новую систему)
    l_pos += dap_http_headers_print(a_request->headers, l_buf + l_pos, a_buffer_size - l_pos);
    
    // 3. Final CRLF
    l_pos += snprintf(l_buf + l_pos, a_buffer_size - l_pos, "\r\n");
    
    // 6. Body for non-GET
    if (a_request->method != HTTP_GET && a_request->body_data && a_request->body_size > 0) {
        if (l_pos + a_request->body_size > a_buffer_size) {
            log_it(L_ERROR, "Body doesn't fit in buffer");
            return 0;
        }
        memcpy(l_buf + l_pos, a_request->body_data, a_request->body_size);
        l_pos += a_request->body_size;
    }
    
    log_it(L_DEBUG, "Formatted HTTP request (%zu bytes): %s %s", l_pos, l_method_str, a_request->path);
    return l_pos;
}

// === SESSION CALLBACKS (define client role) ===

/**
 * @brief Session connected callback - send HTTP request
 * @param a_session Connected session
 */
static void s_http_session_connected(dap_http2_session_t *a_session)
{
    if (!a_session) {
        log_it(L_ERROR, "Session is NULL in connected callback");
        return;
    }
    
    // Get HTTP context from session callbacks
    dap_http_client_context_t *l_context = (dap_http_client_context_t*)a_session->callbacks_arg;
    if (!l_context) {
        log_it(L_ERROR, "HTTP context is NULL in session connected");
        return;
    }
    
    dap_http2_stream_t *l_stream = dap_http2_session_get_stream(a_session);
    if (!l_stream) {
        log_it(L_ERROR, "Failed to create HTTP stream");
        s_complete_http_request(l_context, DAP_HTTP2_CLIENT_ERROR_INTERNAL);
        return;
    }
    
    // Call write_cb of the stream (which contains initial_write_callback)
    size_t l_bytes_sent = s_http_stream_initial_write(l_stream, NULL, 0);
    if ( l_bytes_sent == 0 ) {
        log_it(L_ERROR, "Failed to send HTTP request via write callback");
        s_complete_http_request(l_context, DAP_HTTP2_CLIENT_ERROR_CONNECTION_FAILED);
        return;
    }
    log_it(L_DEBUG, "HTTP request sent successfully (%zu bytes)", l_bytes_sent);
}

/**
 * @brief Session data received callback - forward to stream
 * @param a_session Session instance
 * @param a_data Received data
 * @param a_size Data size
 */
static void s_http_session_data_received(dap_http2_session_t *a_session, const void *a_data, size_t a_size)
{
    if (!a_session || !a_data || a_size == 0) {
        return;
    }
    
    // Get stream from session and forward data
    dap_http2_stream_t *l_stream = dap_http2_session_get_stream(a_session);
    if (l_stream) {
        // Data will be processed by stream read callback
        dap_http2_stream_process_data(l_stream, a_data, a_size);
    }
}

/**
 * @brief Session error callback - handle connection errors
 * @param a_session Session instance
 * @param a_error Error code
 */
static void s_http_session_error(dap_http2_session_t *a_session, int a_error)
{
    if (!a_session) {
        return;
    }
    
    dap_http_client_context_t *l_context = (dap_http_client_context_t*)a_session->callbacks_arg;
    if (!l_context) {
        return;
    }
    
    log_it(L_WARNING, "HTTP session error: %d (%s)", a_error, strerror(abs(a_error)));
    
    // Map system errors to client errors
    dap_http2_client_error_t l_client_error;
    switch (a_error) {
        case ETIMEDOUT:
            l_client_error = DAP_HTTP2_CLIENT_ERROR_TIMEOUT;
            break;
        case ECONNREFUSED:
        case EHOSTUNREACH:
        case ENETUNREACH:
            l_client_error = DAP_HTTP2_CLIENT_ERROR_CONNECTION_FAILED;
            break;
        default:
            l_client_error = DAP_HTTP2_CLIENT_ERROR_INTERNAL;
            break;
    }
    
    // Update client state
    atomic_store(&l_context->client->state, DAP_HTTP2_CLIENT_STATE_ERROR);
    
    // Set stream state to ERROR
    dap_http2_stream_t *l_stream = dap_http2_session_get_stream(a_session);
    if (l_stream) {
        dap_http2_stream_set_state(l_stream, DAP_HTTP_STREAM_STATE_ERROR);
    }
    
    // Complete request with error
    s_complete_http_request(l_context, l_client_error);
}

/**
 * @brief Session closed callback - handle connection close
 * @param a_session Session instance
 */
static void s_http_session_closed(dap_http2_session_t *a_session)
{
    if (!a_session) {
        return;
    }
    
    dap_http_client_context_t *l_context = (dap_http_client_context_t*)a_session->callbacks_arg;
    if (!l_context) {
        return;
    }
    
    log_it(L_DEBUG, "HTTP session closed");
    
    // If request not completed yet, this is premature close
    if (!l_context->request_complete) {
        log_it(L_WARNING, "Session closed before HTTP request completed");
        s_complete_http_request(l_context, DAP_HTTP2_CLIENT_ERROR_CONNECTION_FAILED);
    }
    
    // Cleanup context
    s_destroy_http_context(l_context);
}

/**
 * @brief ZERO-COPY buffer allocation - streaming or accumulation
 * Zero-copy (no buffer allocation) for:
 * - Streaming mode (chunked or large content)
 * - Small content (<= socket buffer size) - work directly with buf_in
 * @param a_context HTTP context
 * @return 0 on success, negative on error
 */
static int s_allocate_response_buffer(dap_http_client_context_t *a_context)
{
    if (!a_context) {
        return -1;
    }
    
    DAP_DELETE(a_context->response_buffer);
    
    // Special case: Chunked WITHOUT progress callback
    // Need buffer for accumulation even though streaming_enabled = true
    bool l_chunked_fallback = (a_context->is_chunked && 
                              a_context->streaming_enabled && 
                              !a_context->client->callbacks.progress_cb);
    
    // ZERO-COPY MODE: No buffer allocation for streaming (except chunked fallback)
    if (a_context->streaming_enabled && !l_chunked_fallback) {
        log_it(L_DEBUG, "Zero-copy streaming mode: no buffer allocation");
        a_context->response_buffer = NULL;
        a_context->response_capacity = 0;
        a_context->response_size = 0;
        return 0;
    }
    
    // ACCUMULATION MODE: Allocate buffer
    size_t l_buffer_size;
    
    if (l_chunked_fallback) {
        // Chunked fallback: use maximum size (same as unknown size case)
        l_buffer_size = DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT;
        log_it(L_DEBUG, "Allocating chunked fallback buffer: %zu bytes (maximum size, no realloc)", l_buffer_size);
    } else if (a_context->content_length > 0) {
        // Known size: exact allocation
        l_buffer_size = a_context->content_length;
        if (l_buffer_size > DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT) {
            log_it(L_ERROR, "Content-Length %zu exceeds limit", l_buffer_size);
            return -1;
        }
        log_it(L_DEBUG, "Allocating exact buffer: %zu bytes (Content-Length)", l_buffer_size);
    } else {
        // Unknown size: maximum allocation
        l_buffer_size = DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT;
        log_it(L_DEBUG, "Allocating maximum buffer: %zu bytes (unknown size)", l_buffer_size);
    }
    
    a_context->response_buffer = DAP_NEW_Z_SIZE(uint8_t, l_buffer_size + 1);
    if (!a_context->response_buffer) {
        log_it(L_ERROR, "Failed to allocate %zu bytes for response buffer", l_buffer_size);
        return -1;
    }
    
    a_context->response_capacity = l_buffer_size;
    a_context->response_size = 0;
    
    if (l_chunked_fallback) {
        log_it(L_DEBUG, "Chunked fallback buffer allocated: %zu bytes (maximum size, no expansion)", l_buffer_size);
    } else {
        log_it(L_DEBUG, "Buffer allocated: %zu bytes (accumulation mode)", l_buffer_size);
    }
    
    return 0;
}



// === SPECIALIZED STREAM CALLBACKS (decomposed for efficiency) ===

/**
 * @brief Initial write callback - formats and sends HTTP request
 * @param a_stream Stream instance
 * @param a_data Unused (initial write doesn't need external data)
 * @param a_size Unused (initial write doesn't need external data)
 * @return Number of bytes sent or 0 on error
 */
static size_t s_http_stream_initial_write(dap_http2_stream_t *a_stream, UNUSED_ARG const void *a_data, UNUSED_ARG size_t a_size)
{   
    if (!a_stream) {
        log_it(L_ERROR, "Stream is NULL in initial write");
        return 0;
    }
    
    // Get HTTP context from stream callback context
    dap_http_client_context_t *l_context = (dap_http_client_context_t*)a_stream->callback_context;
    if (!l_context || !l_context->request) {
        log_it(L_ERROR, "HTTP context or request is NULL in initial write");
        return 0;
    }
    
    log_it(L_DEBUG, "HTTP request initial write using NEW ARCHITECTURE");
    
    // Use new architecture - call the single universal write function
    size_t l_bytes_written = dap_http2_session_write_direct_stream(a_stream->session);
    
    if (l_bytes_written > 0) {
        log_it(L_DEBUG, "HTTP request sent successfully (%zu bytes): %s %s%s", 
               l_bytes_written, 
               dap_http_method_to_str(l_context->request->method),
               l_context->request->host,
               l_context->request->path ? l_context->request->path : "");
        
        // Update client and stream state
        atomic_store(&l_context->client->state, DAP_HTTP2_CLIENT_STATE_REQUESTING);
        dap_http2_stream_set_state(a_stream, DAP_HTTP_STREAM_STATE_REQUEST_SENT);
        
        return l_bytes_written;
    } else {
        log_it(L_ERROR, "Failed to send HTTP request via new architecture");
        return 0;
    }
}

/**
 * @brief HTTP Request write callback - NEW ARCHITECTURE
 * Formats HTTP request into provided buffer
 * @param a_stream Stream instance
 * @param a_buffer Buffer to write into (provided by Session)
 * @param a_buffer_size Available buffer size
 * @param a_context HTTP context
 * @return > 0: bytes written, = 0: need more space, < 0: error
 */
static ssize_t s_http_request_write_cb(dap_http2_stream_t *a_stream, 
                                      void *a_buffer, 
                                      size_t a_buffer_size, 
                                      void *a_context)
{
    if (!a_stream || !a_buffer || !a_context) {
        log_it(L_ERROR, "Invalid arguments in HTTP request write callback");
        return STREAM_WRITE_ERROR_INVALID;
    }
    
    dap_http_client_context_t *l_http_context = (dap_http_client_context_t*)a_context;
    if (!l_http_context->request) {
        log_it(L_ERROR, "HTTP request is NULL");
        return STREAM_WRITE_ERROR_INVALID;
    }
    
    // Calculate required size
    size_t l_needed_size = s_get_request_formatted_size(l_http_context->request);
    if (l_needed_size == 0) {
        log_it(L_ERROR, "Failed to calculate HTTP request size");
        return STREAM_WRITE_ERROR_FORMAT;
    }
    
    // Check if buffer is large enough
    if (l_needed_size > a_buffer_size) {
        log_it(L_DEBUG, "Buffer too small: need %zu, have %zu", l_needed_size, a_buffer_size);
        return 0;  // Request retry with larger buffer
    }
    
    // Format request into buffer
    size_t l_written = s_format_http_request_to_buffer(l_http_context->request, a_buffer, a_buffer_size);
    if (l_written == 0) {
        log_it(L_ERROR, "Failed to format HTTP request");
        return STREAM_WRITE_ERROR_FORMAT;
    }
    
    log_it(L_DEBUG, "HTTP request formatted: %zu bytes", l_written);
    return (ssize_t)l_written;
}

/**
 * @brief Headers parsing callback - determines processing mode and transitions
 * @param a_stream Stream instance
 * @param a_data Received data
 * @param a_size Data size
 * @return Number of bytes processed
 */
static size_t s_http_stream_read_headers(dap_http2_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        return 0;
    }
    
    // Get HTTP context from stream callback context
    dap_http_client_context_t *l_context = (dap_http_client_context_t*)a_stream->callback_context;
    if (!l_context) {
        log_it(L_ERROR, "HTTP context is NULL in headers callback");
        return 0;
    }
    
    // Set stream state to HEADERS processing
    dap_http2_stream_set_state(a_stream, DAP_HTTP_STREAM_STATE_HEADERS);
    
    // Update last read time
    l_context->ts_last_read = time(NULL);
    
    // Parse headers from incoming data
    size_t l_headers_consumed = 0;
    http_process_result_t l_result = s_parse_http_headers(l_context, a_data, a_size, &l_headers_consumed);
    
    switch (l_result) {
        case HTTP_PROCESS_ERROR:
            log_it(L_ERROR, "HTTP header parsing failed");
            dap_http2_stream_set_state(a_stream, DAP_HTTP_STREAM_STATE_ERROR);
            s_complete_http_request(l_context, DAP_HTTP2_CLIENT_ERROR_INTERNAL);
            return 0;
            
        case HTTP_PROCESS_NEED_MORE_DATA:
            // Need more data for complete headers
            return 0;
            
        case HTTP_PROCESS_SUCCESS:
            // Headers parsed successfully - allocate buffer and transition
            l_context->parse_state = HTTP_PARSE_BODY;
            atomic_store(&l_context->client->state, DAP_HTTP2_CLIENT_STATE_RECEIVING);
            
            // Set stream state to BODY processing
            dap_http2_stream_set_state(a_stream, DAP_HTTP_STREAM_STATE_BODY);
            
            // CRITICAL: Buffer allocation AFTER header parsing
            if (s_allocate_response_buffer(l_context) < 0) {
                log_it(L_ERROR, "Failed to allocate response buffer");
                s_complete_http_request(l_context, DAP_HTTP2_CLIENT_ERROR_INTERNAL);
                return 0;
            }
            
            // Choose appropriate body processing callback based on mode
            dap_stream_read_callback_t l_new_callback;
            
            if (l_context->is_chunked) {
                // Chunked ALWAYS uses streaming (accumulation is exotic)
                l_new_callback = s_http_stream_read_chunked_streaming;
                log_it(L_DEBUG, "Transitioning to chunked streaming mode");
            } else {
                if (l_context->streaming_enabled) {
                    l_new_callback = s_http_stream_read_streaming;
                    log_it(L_DEBUG, "Transitioning to streaming mode (Content-Length: %zu)", l_context->content_length);
                } else {
                    l_new_callback = s_http_stream_read_accumulation;
                    log_it(L_DEBUG, "Transitioning to accumulation mode (Content-Length: %zu)", l_context->content_length);
                }
            }
            
            // Perform transition to specialized body processor
            int l_transition_result = dap_http2_stream_transition_protocol(a_stream, l_new_callback, l_context);
            if (l_transition_result < 0) {
                log_it(L_ERROR, "Failed to transition stream protocol");
                s_complete_http_request(l_context, DAP_HTTP2_CLIENT_ERROR_INTERNAL);
                return 0;
            }
            
            return l_headers_consumed; // Headers processed, body processing will continue with new callback
            
        case HTTP_PROCESS_TRANSITION:
            // Redirect detected and redirects are ENABLED - process redirect
            log_it(L_DEBUG, "Redirect transition detected - processing redirect");
            
            // Process redirect
            int l_redirect_result = s_process_http_redirect(l_context);
            if (l_redirect_result == HTTP_PROCESS_TRANSITION) {
                // Redirect processed successfully - new session will be created
                log_it(L_INFO, "Redirect processed - creating new connection");
                s_complete_http_request(l_context, 0); // Success - redirect handled
                return l_headers_consumed;
            }
            else if (l_redirect_result < 0) {
                log_it(L_ERROR, "Failed to process redirect: %d", l_redirect_result);
                s_complete_http_request(l_context, DAP_HTTP2_CLIENT_ERROR_INTERNAL);
                return 0;
            }
            else {
                // This should not happen - if redirects are enabled, we should get TRANSITION
                log_it(L_ERROR, "Unexpected redirect result: %d", l_redirect_result);
                s_complete_http_request(l_context, DAP_HTTP2_CLIENT_ERROR_INTERNAL);
                return 0;
            }
            
        default:
            log_it(L_ERROR, "Unexpected result from header parsing: %d", l_result);
            s_complete_http_request(l_context, DAP_HTTP2_CLIENT_ERROR_INTERNAL);
            return 0;
    }
}

/**
 * @brief Accumulation mode callback - stores data in response buffer
 * @param a_stream Stream instance
 * @param a_data Body data
 * @param a_size Data size
 * @return Number of bytes processed
 */
static size_t s_http_stream_read_accumulation(dap_http2_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        return 0;
    }
    
    dap_http_client_context_t *l_context = (dap_http_client_context_t*)a_stream->callback_context;
    if (!l_context) {
        return 0;
    }
    
    l_context->ts_last_read = time(NULL);
    
    // CRITICAL: Buffer must be allocated in s_http_stream_read_headers!
    if (!l_context->response_buffer) {
        log_it(L_ERROR, "Response buffer not allocated - architecture error");
        s_complete_http_request(l_context, DAP_HTTP2_CLIENT_ERROR_INTERNAL);
        return 0;
    }
    
    // NO REALLOCATION! Buffer is already correct size
    // Check only overflow (protection from errors)
    if (l_context->response_size + a_size > l_context->response_capacity) {
        log_it(L_ERROR, "Response buffer overflow: %zu + %zu > %zu (single allocation strategy violated)", 
               l_context->response_size, a_size, l_context->response_capacity);
        s_complete_http_request(l_context, DAP_HTTP2_CLIENT_ERROR_INTERNAL);
        return 0;
    }
    
    // Copy data to buffer (efficient single copy)
    size_t l_copy_size = dap_min(a_size, l_context->response_capacity - l_context->response_size);
    memcpy(l_context->response_buffer + l_context->response_size, a_data, l_copy_size);
    l_context->response_size += l_copy_size;
    
    // Null-terminate for safety
    l_context->response_buffer[l_context->response_size] = '\0';
    
    // Call progress callback
    if (l_context->client->callbacks.progress_cb) {
        l_context->client->callbacks.progress_cb(
            l_context->client,
            l_context->response_size,
            l_context->content_length
        );
    }
    
    // Check completion
    if (l_context->content_length > 0 && l_context->response_size >= l_context->content_length) {
        log_it(L_INFO, "HTTP response complete: %zu bytes accumulated", l_context->response_size);
        l_context->parse_state = HTTP_PARSE_COMPLETE;
        atomic_store(&l_context->client->state, DAP_HTTP2_CLIENT_STATE_COMPLETE);
        
        // Set stream state to COMPLETE
        dap_http2_stream_set_state(a_stream, DAP_HTTP_STREAM_STATE_COMPLETE);
        
        // Call response callback with accumulated data
        if (l_context->client->callbacks.response_cb) {
            l_context->client->callbacks.response_cb(
                l_context->client,
                l_context->status_code,
                l_context->response_buffer,
                l_context->response_size
            );
        }
        
        s_complete_http_request(l_context, 0); // Success
    }
    
    return l_copy_size;
}

/**
 * @brief Streaming mode callback - zero-copy data forwarding
 * @param a_stream Stream instance
 * @param a_data Body data (zero-copy pointer)
 * @param a_size Data size
 * @return Number of bytes processed
 */
static size_t s_http_stream_read_streaming(dap_http2_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        return 0;
    }
    
    dap_http_client_context_t *l_context = (dap_http_client_context_t*)a_stream->callback_context;
    if (!l_context) {
        return 0;
    }
    
    l_context->ts_last_read = time(NULL);
    
    // Check size limits for streaming
    if (l_context->streamed_body_size + a_size > (10 * 1024 * 1024)) { // 10MB limit
        log_it(L_ERROR, "Zero-copy streaming exceeds size limit");
        s_complete_http_request(l_context, DAP_HTTP2_CLIENT_ERROR_INTERNAL);
        return 0;
    }
    
    // Update streamed size first
    l_context->streamed_body_size += a_size;
    
    // ZERO-COPY: Call progress callback with updated size
    if (l_context->client->callbacks.progress_cb) {
        l_context->client->callbacks.progress_cb(
            l_context->client,
            l_context->streamed_body_size,  // Updated size
            l_context->content_length       // Total size (known for non-chunked)
        );
    }
    
    // Check completion for known content length
    if (l_context->content_length > 0 && l_context->streamed_body_size >= l_context->content_length) {
        log_it(L_INFO, "Zero-copy streaming complete: %zu bytes", l_context->streamed_body_size);
        l_context->parse_state = HTTP_PARSE_COMPLETE;
        atomic_store(&l_context->client->state, DAP_HTTP2_CLIENT_STATE_COMPLETE);
        
        // Set stream state to COMPLETE
        dap_http2_stream_set_state(a_stream, DAP_HTTP_STREAM_STATE_COMPLETE);
        
        // Call response callback with NULL data (already streamed)
        if (l_context->client->callbacks.response_cb) {
            l_context->client->callbacks.response_cb(
                l_context->client,
                l_context->status_code,
                NULL, // No accumulated data in streaming mode
                0     // No accumulated size
            );
        }
        
        s_complete_http_request(l_context, 0); // Success
    }
    
    return a_size; // All data processed (streamed)
}

/**
 * @brief Chunked accumulation mode callback
 * @param a_stream Stream instance
 * @param a_data Chunk data
 * @param a_size Data size
 * @return Number of bytes processed
 */
static size_t s_http_stream_read_chunked_streaming(dap_http2_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        return 0;
    }
    
    dap_http_client_context_t *l_context = (dap_http_client_context_t*)a_stream->callback_context;
    if (!l_context) {
        return 0;
    }
    
    l_context->ts_last_read = time(NULL);
    
    // Process chunked data with zero-copy streaming
    size_t l_consumed = 0;
    http_process_result_t l_result = s_process_chunked_data_streaming(l_context, a_data, a_size, &l_consumed);
    bool l_complete = (l_result == HTTP_PROCESS_COMPLETE);
    if (l_complete) {
        log_it(L_DEBUG, "Chunked streaming complete");
        l_context->parse_state = HTTP_PARSE_COMPLETE;
        atomic_store(&l_context->client->state, DAP_HTTP2_CLIENT_STATE_COMPLETE);
        
        // Set stream state to COMPLETE
        dap_http2_stream_set_state(a_stream, DAP_HTTP_STREAM_STATE_COMPLETE);
        
        // Call response callback with NULL data (already streamed)
        if (l_context->client->callbacks.response_cb) {
            l_context->client->callbacks.response_cb(
                l_context->client,
                l_context->status_code,
                NULL, // No accumulated data in streaming mode
                0     // No accumulated size
            );
        }
        
        s_complete_http_request(l_context, 0); // Success
    }
    
    return a_size; // Chunked processor handles all data
}

/**
 * @brief Process chunked data with zero-copy streaming
 * @param a_context HTTP client context
 * @param a_data Chunk data
 * @param a_size Data size
 * @return true if processing complete, false if need more data
 */
static http_process_result_t s_process_chunked_data_streaming(dap_http_client_context_t *a_context, const void *a_data, size_t a_size, size_t *a_consumed)
{
    if (!a_context || !a_data || a_size == 0 || !a_consumed) {
        return HTTP_PROCESS_ERROR;
    }
    
    const uint8_t *l_data = (const uint8_t*)a_data;
    size_t l_processed = 0;
    
    while (l_processed < a_size) {
        if (a_context->reading_chunk_size) {
            // Find CRLF for chunk size line (minimal parsing)
            const char *l_remaining = (const char*)(l_data + l_processed);
            size_t l_remaining_size = a_size - l_processed;
            const char *l_crlf = dap_memmem_n(l_remaining, l_remaining_size, "\r\n", 2);
            
            if (!l_crlf) {
                break; // Need more data
            }
            
            size_t l_size_line_len = l_crlf - l_remaining;
            
            // Quick hex parsing (streamlined for performance)
            unsigned long l_chunk_size = 0;
            bool l_parse_ok = false;
            
            if (l_size_line_len > 0 && l_size_line_len <= 16) {
                l_parse_ok = true;
                
                for (size_t i = 0; i < l_size_line_len && l_parse_ok; i++) {
                    char l_c = l_remaining[i];
                    
                    // Stop at extension separator (streaming doesn't care about extensions)
                    if (l_c == ';' || l_c == ' ') break;
                    
                    int l_digit = -1;
                    if (l_c >= '0' && l_c <= '9') l_digit = l_c - '0';
                    else if (l_c >= 'A' && l_c <= 'F') l_digit = l_c - 'A' + 10;
                    else if (l_c >= 'a' && l_c <= 'f') l_digit = l_c - 'a' + 10;
                    else { l_parse_ok = false; break; }
                    
                    // Simple overflow check
                    if (l_chunk_size > (ULONG_MAX - l_digit) / 16) {
                        l_parse_ok = false; break;
                    }
                    
                    l_chunk_size = l_chunk_size * 16 + l_digit;
                }
            }
            
            if (!l_parse_ok) {
                log_it(L_ERROR, "Invalid chunk size in streaming mode");
                return HTTP_PROCESS_ERROR;
            }
            
            // Global size limit check (streaming)
            if (l_chunk_size > 0 && 
                a_context->streamed_body_size + l_chunk_size > DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT) {
                log_it(L_ERROR, "Streaming chunk would exceed global limit");
                return HTTP_PROCESS_ERROR;
            }
            
            l_processed += l_size_line_len + 2; // Skip size line + CRLF
            a_context->current_chunk_size = (size_t)l_chunk_size;
            a_context->current_chunk_read = 0;
            a_context->reading_chunk_size = false;
            
            if (l_chunk_size == 0) {
                // Last chunk - handle trailers minimally (streaming doesn't need them)
                while (l_processed < a_size) {
                    const char *l_trailer_data = (const char*)(l_data + l_processed);
                    size_t l_trailer_remaining = a_size - l_processed;
                    const char *l_trailer_crlf = dap_memmem_n(l_trailer_data, l_trailer_remaining, "\r\n", 2);
                    
                    if (!l_trailer_crlf) break;
                    
                    size_t l_trailer_len = l_trailer_crlf - l_trailer_data;
                    if (l_trailer_len == 0) {
                        // Final CRLF - streaming complete
                        l_processed += 2; // Skip final \r\n
                        *a_consumed = l_processed;
                        
                        // Signal completion
                        if (a_context->client->callbacks.progress_cb) {
                            // Progress callback was used - signal completion with final total
                            a_context->client->callbacks.progress_cb(
                                a_context->client,
                                a_context->streamed_body_size, // Final total
                                a_context->streamed_body_size  // Known total now
                            );
                        } else {
                            // No progress callback - call response callback with accumulated data
                            log_it(L_INFO, "Chunked transfer complete: %zu bytes accumulated", a_context->response_size);
                            
                            if (a_context->client->callbacks.response_cb) {
                                a_context->client->callbacks.response_cb(
                                    a_context->client,
                                    a_context->status_code,
                                    a_context->response_buffer,
                                    a_context->response_size
                                );
                            }
                        }
                        
                        return HTTP_PROCESS_COMPLETE;
                    } else {
                        // Skip trailer header (streaming ignores them)
                        l_processed += l_trailer_len + 2;
                    }
                }
                break; // Need more data for final CRLF
            }
            
        } else {
            // Reading chunk data - ZERO-COPY STREAMING
            size_t l_chunk_remaining = a_context->current_chunk_size - a_context->current_chunk_read;
            size_t l_data_remaining = a_size - l_processed;
            size_t l_to_stream = dap_min(l_chunk_remaining, l_data_remaining);
            
            if (l_to_stream > 0) {
                a_context->streamed_body_size += l_to_stream;
                
                if (a_context->client->callbacks.progress_cb) {
                    // ZERO-COPY: pass pointer directly to progress callback
                    a_context->client->callbacks.progress_cb(
                        a_context->client,
                        a_context->streamed_body_size,
                        0  // Total unknown for chunked
                    );
                    // Note: Progress callback handles data immediately - no buffering needed
                    // TODO: Add data pointer to progress callback signature for true zero-copy
                } else {
                    // FALLBACK: No progress callback - accumulate in response buffer for final response callback
                    // This handles the case where user only provided response_cb but not progress_cb
                    
                    // Buffer should already be allocated by s_allocate_response_buffer (maximum size)
                    if (!a_context->response_buffer) {
                        log_it(L_ERROR, "Chunked fallback buffer not allocated - architecture error");
                        return HTTP_PROCESS_ERROR;
                    }
                    
                    // Check overflow (no realloc - buffer is already maximum size)
                    if (a_context->response_size + l_to_stream > a_context->response_capacity) {
                        log_it(L_ERROR, "Chunked response exceeds maximum buffer size: %zu + %zu > %zu", 
                               a_context->response_size, l_to_stream, a_context->response_capacity);
                        return HTTP_PROCESS_ERROR;
                    }
                    
                    // Copy chunk data to accumulation buffer
                    memcpy(a_context->response_buffer + a_context->response_size, 
                           (const uint8_t*)a_data + l_processed, l_to_stream);
                    a_context->response_size += l_to_stream;
                }
                
                l_processed += l_to_stream;
                a_context->current_chunk_read += l_to_stream;
            }
            
            // Check chunk completion
            if (a_context->current_chunk_read >= a_context->current_chunk_size) {
                // Look for trailing CRLF after chunk data
                if (l_processed + 1 < a_size && 
                    l_data[l_processed] == '\r' && l_data[l_processed + 1] == '\n') {
                    
                    l_processed += 2; // Skip trailing CRLF
                    a_context->reading_chunk_size = true; // Ready for next chunk
                    log_it(L_DEBUG, "Completed chunk %llu (%zu bytes)", 
                           a_context->current_chunk_id, a_context->current_chunk_size);
                } else {
                    // Need more data for trailing CRLF
                    break;
                }
            }
        }
    }
    
    *a_consumed = l_processed;
    return HTTP_PROCESS_NEED_MORE_DATA; // Need more data
}

// === EMBEDDED TRANSITIONS IMPLEMENTATION ===

/**
 * @brief Transition stream protocol (CORRECT - without breaking encapsulation)
 * @param a_stream Stream instance
 * @param a_new_callback New read callback function
 * @param a_new_context New callback context
 * @return 0 on success, negative on error
 * 
 * @note Data reprocessing should happen OUTSIDE this function,
 *       in calling code that knows about remaining data
 */
int dap_http2_stream_transition_protocol(dap_http2_stream_t *a_stream,
                                        dap_stream_read_callback_t a_new_callback,
                                        void *a_new_context)
{
    if (!a_stream || !a_new_callback) {
        log_it(L_ERROR, "Invalid arguments for stream transition");
        return -1;
    }
    
    // Set new callback and context using proper API
    dap_http2_stream_set_read_callback(a_stream, a_new_callback, a_new_context);
    
    log_it(L_DEBUG, "Stream protocol transitioned successfully");
    
    return 0;
}

// === REQUEST EXECUTION ===

/**
 * @brief Execute synchronous request
 * @param a_client Client instance
 * @param a_request Request to execute
 * @param a_response_data Output: response data (caller must free)
 * @param a_response_size Output: response size
 * @param a_status_code Output: HTTP status code
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_sync(dap_http2_client_t *a_client,
                                  const dap_http2_client_request_t *a_request,
                                  void **a_response_data,
                                  size_t *a_response_size,
                                  int *a_status_code)
{
    if (!a_client || !a_request || !a_response_data || !a_response_size || !a_status_code) {
        log_it(L_ERROR, "Invalid arguments in dap_http2_client_request_sync");
        return -1;
    }
    
    // Check client state
    dap_http2_client_state_t l_current_state = atomic_load(&a_client->state);
    if (l_current_state != DAP_HTTP2_CLIENT_STATE_IDLE) {
        log_it(L_ERROR, "Client is busy (state: %s)", dap_http2_client_state_to_str(l_current_state));
        return -2;
    }
    
    // Create HTTP context
    dap_http_client_context_t *l_context = s_create_http_context(a_client, a_request);
    if (!l_context) {
        log_it(L_ERROR, "Failed to create HTTP context");
        return -3;
    }
    
    // Initialize semaphore for sync operation
    if (sem_init(&l_context->completion_semaphore, 0, 0) != 0) {
        log_it(L_ERROR, "Failed to initialize completion semaphore");
        s_destroy_http_context(l_context);
        return -4;
    }
    
    // Create session with HTTP profile
    dap_stream_profile_t l_profile = {
        .session_callbacks = {
            .connected = s_http_session_connected,
            .data_received = s_http_session_data_received,
            .error = s_http_session_error,
            .closed = s_http_session_closed
        },
        .stream_callbacks = DAP_NEW(dap_http2_stream_callbacks_t),
        .profile_context = l_context
    };
    *l_profile.stream_callbacks = (dap_http2_stream_callbacks_t) {
        .read_cb = s_http_stream_read_headers,
        .write_cb = s_http_request_write_cb
    };
    
    // Create session (this will be assigned to worker)
    dap_http2_session_t *l_session = dap_http2_session_create(NULL, a_client->config.connect_timeout_ms);
    if (!l_session) {
        log_it(L_ERROR, "Failed to create HTTP session");
        sem_destroy(&l_context->completion_semaphore);
        s_destroy_http_context(l_context);
        return -5;
    }
    
    // Set session callbacks
    dap_http2_session_set_callbacks(l_session, &l_profile.session_callbacks, l_context);
    
    // Connect to server
    int l_connect_result = dap_http2_session_connect(l_session, a_request->host, a_request->port, a_request->use_ssl);
    if (l_connect_result != 0) {
        log_it(L_ERROR, "Failed to connect to %s:%u", a_request->host, a_request->port);
        dap_http2_session_delete(l_session);
        sem_destroy(&l_context->completion_semaphore);
        s_destroy_http_context(l_context);
        return -6;
    }
    
    // Update client state
    atomic_store(&a_client->state, DAP_HTTP2_CLIENT_STATE_REQUESTING);
    
    // Wait for completion (with timeout)
    struct timespec l_timeout;
    clock_gettime(CLOCK_REALTIME, &l_timeout);
    l_timeout.tv_sec += (a_client->config.read_timeout_ms / 1000);
    l_timeout.tv_nsec += ((a_client->config.read_timeout_ms % 1000) * 1000000);
    
    int l_sem_result = sem_timedwait(&l_context->completion_semaphore, &l_timeout);
    if (l_sem_result != 0) {
        if (errno == ETIMEDOUT) {
            log_it(L_ERROR, "Request timeout");
            atomic_store(&a_client->state, DAP_HTTP2_CLIENT_STATE_ERROR);
            dap_http2_session_close(l_session);
            sem_destroy(&l_context->completion_semaphore);
            s_destroy_http_context(l_context);
            return -7;
        } else {
            log_it(L_ERROR, "Semaphore wait failed: %s", strerror(errno));
            dap_http2_session_close(l_session);
            sem_destroy(&l_context->completion_semaphore);
            s_destroy_http_context(l_context);
            return -8;
        }
    }
    
    // Extract results
    int l_result = l_context->error_code;
    if (l_result == 0) {
        // Success - copy response data
        *a_status_code = l_context->status_code;
        
        if (l_context->response_buffer && l_context->response_size > 0) {
            *a_response_data = DAP_NEW_SIZE(uint8_t, l_context->response_size + 1);
            if (*a_response_data) {
                memcpy(*a_response_data, l_context->response_buffer, l_context->response_size);
                ((uint8_t*)*a_response_data)[l_context->response_size] = '\0'; // Null-terminate
                *a_response_size = l_context->response_size;
            } else {
                log_it(L_ERROR, "Failed to allocate response data copy");
                l_result = -9;
            }
        } else {
            // No response body
            *a_response_data = NULL;
            *a_response_size = 0;
        }
    }
    
    // Cleanup
    dap_http2_session_delete(l_session);
    sem_destroy(&l_context->completion_semaphore);
    s_destroy_http_context(l_context);
    
    return l_result;
}

/**
 * @brief Execute asynchronous request
 * @param a_client Client instance
 * @param a_request Request to execute
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_async(dap_http2_client_t *a_client,
                                   const dap_http2_client_request_t *a_request)
{
    if (!a_client || !a_request) {
        log_it(L_ERROR, "Invalid arguments in dap_http2_client_request_async");
        return -1;
    }
    
    // Check client state
    dap_http2_client_state_t l_current_state = atomic_load(&a_client->state);
    if (l_current_state != DAP_HTTP2_CLIENT_STATE_IDLE) {
        log_it(L_ERROR, "Client is busy (state: %s)", dap_http2_client_state_to_str(l_current_state));
        return -2;
    }
    
    // Create HTTP context (no semaphore for async)
    dap_http_client_context_t *l_context = s_create_http_context(a_client, a_request);
    if (!l_context) {
        log_it(L_ERROR, "Failed to create HTTP context");
        return -3;
    }
    
    // Create session with HTTP profile
    dap_stream_profile_t l_profile = {
        .session_callbacks = {
            .connected = s_http_session_connected,
            .data_received = s_http_session_data_received,
            .error = s_http_session_error,
            .closed = s_http_session_closed
        },
        .stream_callbacks = DAP_NEW(dap_http2_stream_callbacks_t),
        .profile_context = l_context
    };
    *l_profile.stream_callbacks = (dap_http2_stream_callbacks_t) {
        .read_cb = s_http_stream_read_headers,
        .write_cb = s_http_request_write_cb
    };
    
    // Create session (this will be assigned to worker)
    dap_http2_session_t *l_session = dap_http2_session_create(NULL, a_client->config.connect_timeout_ms);
    if (!l_session) {
        log_it(L_ERROR, "Failed to create HTTP session");
        s_destroy_http_context(l_context);
        return -4;
    }
    
    // Set session callbacks
    dap_http2_session_set_callbacks(l_session, &l_profile.session_callbacks, l_context);
    
    // Connect to server
    int l_connect_result = dap_http2_session_connect(l_session, a_request->host, a_request->port, a_request->use_ssl);
    if (l_connect_result != 0) {
        log_it(L_ERROR, "Failed to connect to %s:%u", a_request->host, a_request->port);
        dap_http2_session_delete(l_session);
        s_destroy_http_context(l_context);
        return -5;
    }
    
    // Update client state
    atomic_store(&a_client->state, DAP_HTTP2_CLIENT_STATE_REQUESTING);
    
    log_it(L_DEBUG, "Async request started to %s:%u", a_request->host, a_request->port);
    
    return 0; // Success - request is now running asynchronously
}

/**
 * @brief HTTP headers parsing with embedded analysis - EFFICIENT single-pass
 * @param a_context HTTP context
 * @param a_data Incoming data
 * @param a_size Data size
 * @param a_consumed Output: number of bytes consumed
 * @return Process result code
 */
static http_process_result_t s_parse_http_headers(dap_http_client_context_t *a_context, const void *a_data, size_t a_size, size_t *a_consumed)
{
    if (!a_context || !a_data || a_size == 0 || !a_consumed) {
        return HTTP_PROCESS_ERROR;
    }
    
    // Find end of headers
    const char *l_data = (const char*)a_data;
    const char *l_headers_end = dap_memmem_n(l_data, a_size, "\r\n\r\n", 4);
    
    if (!l_headers_end) {
        // Check headers size limit
        if (a_size > DAP_CLIENT_HTTP_MAX_HEADERS_SIZE) {
            log_it(L_ERROR, "HTTP headers exceed maximum size (%zu > %zu)", 
                   a_size, (size_t)DAP_CLIENT_HTTP_MAX_HEADERS_SIZE);
            return HTTP_PROCESS_ERROR;
        }
        *a_consumed = 0;
        return HTTP_PROCESS_NEED_MORE_DATA;
    }
    
    size_t l_headers_length = l_headers_end - l_data + 4; // +4 for "\r\n\r\n"
    *a_consumed = l_headers_length;
    
    // Extract status code
    a_context->status_code = http_status_code_from_response(l_data, l_headers_length);
    if (a_context->status_code == 0) {
        log_it(L_ERROR, "Failed to extract HTTP status code");
        return HTTP_PROCESS_ERROR;
    }
    
    log_it(L_DEBUG, "HTTP status: %d (%s)", a_context->status_code, http_status_reason_phrase(a_context->status_code));
    
    // Clear old headers
    // REMOVED: dap_http_headers_remove_all(&a_context->response_headers);
    
    // Parse headers line by line
    const char *l_line_start = l_data;
    const char *l_headers_limit = l_data + l_headers_length;
    
    // Skip status line
    const char *l_line_end = dap_memmem_n(l_line_start, l_headers_limit - l_line_start, "\r\n", 2);
    if (l_line_end) {
        l_line_start = l_line_end + 2;
    }
    
    // Check for redirect FIRST - before any analysis
    bool l_is_redirect = s_is_redirect_status_code(a_context->status_code);
    
    // Direct substring search in headers
    size_t l_headers_size = l_headers_limit - l_line_start;
    
    if (l_is_redirect) {
        // For redirects, only search for Location header
        const char *l_location_pos = dap_memmem_n(l_line_start, l_headers_size, s_http_header_location, sizeof(s_http_header_location) - 1);
        if (l_location_pos) {
            l_location_pos += sizeof(s_http_header_location) - 1; // Skip "Location:"
            
            // Skip whitespace after colon
            while (l_location_pos < l_line_start + l_headers_size && isspace(*l_location_pos)) {
                l_location_pos++;
            }
            
            // Find end of line
            const char *l_location_end = dap_memmem_n(l_location_pos, l_line_start + l_headers_size - l_location_pos, "\r\n", 2);
            if (l_location_end) {
                // Remove trailing whitespace - move back from end
                const char *l_value_end = l_location_end;
                while (l_value_end > l_location_pos && isspace(*(l_value_end - 1))) {
                    l_value_end--;
                }
                size_t l_location_len = l_value_end - l_location_pos;
                
                // Directly update request with new URL
                int l_parse_result = dap_http2_client_request_parse_url(a_context->request, l_location_pos, l_location_len, a_context->status_code);
                if (l_parse_result == 0) {
                    log_it(L_DEBUG, "Successfully updated request with redirect URL");
                } else {
                    log_it(L_ERROR, "Failed to parse redirect URL: %d", l_parse_result);
                }
            }
        }
        
    } else {
        // For normal responses, search for needed headers
        
        // 1. Content-Length
        const char *l_cl_pos = dap_memmem_n(l_line_start, l_headers_size, s_http_header_content_length, sizeof(s_http_header_content_length) - 1);
        if (l_cl_pos) {
            l_cl_pos += sizeof(s_http_header_content_length) - 1; // Skip "Content-Length:"
            while (l_cl_pos < l_line_start + l_headers_size && isspace(*l_cl_pos)) {
                l_cl_pos++;
            }
            a_context->content_length = strtoul(l_cl_pos, NULL, 10);
            log_it(L_DEBUG, "Found Content-Length: %zu", a_context->content_length);
        }
        
        // 2. Transfer-Encoding
        const char *l_te_pos = dap_memmem_n(l_line_start, l_headers_size, s_http_header_transfer_encoding, sizeof(s_http_header_transfer_encoding) - 1);
        if (l_te_pos) {
            l_te_pos += sizeof(s_http_header_transfer_encoding) - 1; // Skip "Transfer-Encoding:"
            while (l_te_pos < l_line_start + l_headers_size && isspace(*l_te_pos)) {
                l_te_pos++;
            }
            if (l_te_pos + 7 <= l_line_start + l_headers_size && 
                strncasecmp(l_te_pos, "chunked", 7) == 0) {
                a_context->is_chunked = true;
                a_context->content_length = 0; // Override Content-Length
                log_it(L_DEBUG, "Found Transfer-Encoding: chunked");
            }
        }
        
        // 3. Content-Type
        const char *l_ct_pos = dap_memmem_n(l_line_start, l_headers_size, s_http_header_content_type, sizeof(s_http_header_content_type) - 1);
        if (l_ct_pos) {
            l_ct_pos += sizeof(s_http_header_content_type) - 1; // Skip "Content-Type:"
            while (l_ct_pos < l_line_start + l_headers_size && isspace(*l_ct_pos)) {
                l_ct_pos++;
            }
            const char *l_ct_end = dap_memmem_n(l_ct_pos, l_line_start + l_headers_size - l_ct_pos, "\r\n", 2);
            if (l_ct_end) {
                // Remove trailing whitespace - move back from end
                const char *l_value_end = l_ct_end;
                while (l_value_end > l_ct_pos && isspace(*(l_value_end - 1))) {
                    l_value_end--;
                }
                size_t l_ct_len = l_value_end - l_ct_pos;
                DAP_DELETE(a_context->content_type);
                asprintf(&a_context->content_type, "%.*s", (int)l_ct_len, l_ct_pos);
                log_it(L_DEBUG, "Found Content-Type: %s", a_context->content_type);
            }
        }
    }
    
    // Check if redirect detected and handle based on settings
    if (l_is_redirect) {
        // Check if redirects are enabled
        if (a_context->follow_redirects) {
            log_it(L_DEBUG, "Redirect detected: %d (request updated)", a_context->status_code);
            return HTTP_PROCESS_TRANSITION; // Signal redirect to caller
        } else {
            log_it(L_DEBUG, "Redirects disabled - treating as normal response");
            // Treat as normal response - continue processing
        }
    }
    
    // Final streaming decision for non-redirects (or redirects when disabled)
    if (!a_context->streaming_enabled && !l_is_redirect) {
        // Check if we have progress callback for streaming
        bool l_has_progress_callback = (a_context->client && a_context->client->callbacks.progress_cb);
        
        // Check for binary MIME types that benefit from streaming
        if (a_context->content_type) {
            bool l_is_binary_mime = (strstr(a_context->content_type, "application/octet-stream") ||
                                   strstr(a_context->content_type, "application/zip") ||
                                   strstr(a_context->content_type, "application/gzip") ||
                                   strstr(a_context->content_type, "video/") ||
                                   strstr(a_context->content_type, "audio/") ||
                                   strstr(a_context->content_type, "image/"));
            
            // Only enable streaming for binary MIME if progress callback available
            if (l_is_binary_mime && l_has_progress_callback) {
                a_context->streaming_enabled = true;
                log_it(L_DEBUG, "Binary MIME type '%s' with progress callback -> streaming mode", a_context->content_type);
            } else if (l_is_binary_mime) {
                log_it(L_DEBUG, "Binary MIME type '%s' without progress callback -> accumulation mode", a_context->content_type);
            }
        }
        
        if (!a_context->streaming_enabled) {
            if (a_context->is_chunked) {
                // Chunked: ALWAYS use streaming (accumulation is exotic and rarely needed)
                a_context->streaming_enabled = true;
                if (l_has_progress_callback) {
                    log_it(L_DEBUG, "Chunked with progress callback -> streaming mode");
                } else {
                    log_it(L_DEBUG, "Chunked without progress callback -> streaming mode (no progress tracking)");
                }
            } else if (a_context->content_length > 0 && a_context->content_length > DAP_CLIENT_HTTP_STREAMING_THRESHOLD_DEFAULT) {
                // Large content: only stream if progress callback available
                if (l_has_progress_callback) {
                    a_context->streaming_enabled = true;
                    log_it(L_DEBUG, "Large content (%zu bytes) with progress callback -> streaming mode", a_context->content_length);
                } else {
                    log_it(L_DEBUG, "Large content (%zu bytes) without progress callback -> accumulation mode", a_context->content_length);
                    // streaming_enabled remains false
                }
            }
        }
    }
    
    // Check for empty body responses (optimization)
    if (a_context->content_length == 0 && !a_context->is_chunked) {
        // HTTP responses with no body (204 No Content, 304 Not Modified, etc.)
        // or explicit Content-Length: 0
        log_it(L_DEBUG, "Empty body response detected (Content-Length: 0)");
        a_context->streaming_enabled = false; // No streaming needed for empty body
    }
    
    return HTTP_PROCESS_SUCCESS;
}




/**
 * @brief Process chunked transfer encoded data with comprehensive edge case handling
 * @param a_context HTTP client context
 * @param a_data Input data buffer
 * @param a_size Size of input data
 * @param a_consumed Output: number of bytes consumed
 * @return HTTP_PROCESS_* result code
 */
static http_process_result_t s_process_chunked_data(dap_http_client_context_t *a_context, const void *a_data, size_t a_size, size_t *a_consumed)
{
    if (!a_context || !a_data || a_size == 0 || !a_consumed) {
        return HTTP_PROCESS_ERROR;
    }
    
    const uint8_t *l_data = (const uint8_t*)a_data;
    size_t l_processed = 0;
    
    while (l_processed < a_size) {
        if (a_context->reading_chunk_size) {
            // Reset chunk read counter for new chunk
            a_context->current_chunk_read = 0;
            
            // Find CRLF to complete chunk size line (safe binary search)
            const char *l_remaining_data = (const char*)(l_data + l_processed);
            size_t l_remaining_size = a_size - l_processed;
            const char *l_crlf = dap_memmem_n(l_remaining_data, l_remaining_size, "\r\n", 2);
            
            if (!l_crlf) {
                // Need more data to complete size line
                break;
            }
            
            size_t l_size_line_len = l_crlf - l_remaining_data;
            
            // Parse chunk size from hex directly (zero-copy) with enhanced validation
            if (l_size_line_len == 0) {
                a_context->chunked_error_count++;
                log_it(L_ERROR, "Empty chunk size line (error #%d)", a_context->chunked_error_count);
                
                if (a_context->chunked_error_count >= MAX_CHUNKED_PARSE_ERRORS) {
                    log_it(L_ERROR, "Too many chunked parsing errors, aborting");
                    return HTTP_PROCESS_ERROR;
                }
                
                // Recovery: skip empty line
                l_processed += 2; // Skip \r\n
                continue;
            }
            
            if (l_size_line_len > 16) {
                a_context->chunked_error_count++;
                log_it(L_ERROR, "Chunk size line too long: %zu bytes (error #%d)", 
                       l_size_line_len, a_context->chunked_error_count);
                
                if (a_context->chunked_error_count >= MAX_CHUNKED_PARSE_ERRORS) {
                    return HTTP_PROCESS_ERROR;
                }
                
                // Recovery: skip problematic line
                l_processed += l_size_line_len + 2;
                continue;
            }
            
            // Direct hex parsing without copying - enhanced with better validation
            unsigned long l_chunk_size = 0;
            size_t l_hex_len = 0;
            bool l_found_extension = false;
            
            // Parse hex digits directly from buffer
            for (size_t i = 0; i < l_size_line_len; i++) {
                char l_c = l_remaining_data[i];
                
                // Stop at chunk extension separator, whitespace, or control chars
                if (l_c == ';' || l_c == ' ' || l_c == '\t' || l_c < 0x20) {
                    l_found_extension = true;
                    break;
                }
                
                // Convert hex digit
                int l_digit;
                if (l_c >= '0' && l_c <= '9') {
                    l_digit = l_c - '0';
                } else if (l_c >= 'A' && l_c <= 'F') {
                    l_digit = l_c - 'A' + 10;
                } else if (l_c >= 'a' && l_c <= 'f') {
                    l_digit = l_c - 'a' + 10;
                } else {
                    // Invalid hex character
                    log_it(L_ERROR, "Invalid hex character '%c' (0x%02X) in chunk size (error #%d)", 
                           l_c, (unsigned char)l_c, a_context->chunked_error_count + 1);
                    break;
                }
                
                // Check for overflow before shifting
                if (l_chunk_size > (ULONG_MAX - l_digit) / 16) {
                    log_it(L_ERROR, "Chunk size overflow during parsing");
                    l_hex_len = 0; // Mark as error
                    break;
                }
                
                l_chunk_size = l_chunk_size * 16 + l_digit;
                l_hex_len++;
            }
            
            // Validation: must have parsed at least one hex digit
            if (l_hex_len == 0) {
                a_context->chunked_error_count++;
                log_it(L_ERROR, "No valid hex digits in chunk size line (error #%d)", 
                       a_context->chunked_error_count);
                
                if (a_context->chunked_error_count >= MAX_CHUNKED_PARSE_ERRORS) {
                    return HTTP_PROCESS_ERROR;
                }
                
                // Recovery: skip problematic line
                l_processed += l_size_line_len + 2;
                continue;
            }
            
            // Size limit check
            if (l_chunk_size > DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT) {
                log_it(L_ERROR, "Chunk size %lu exceeds limit %zu", 
                       l_chunk_size, (size_t)DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT);
                return HTTP_PROCESS_ERROR;
            }
            
            l_processed += l_size_line_len + 2; // +2 for CRLF
            a_context->current_chunk_size = (size_t)l_chunk_size;
            a_context->current_chunk_read = 0;
            a_context->current_chunk_id = ++a_context->next_chunk_id;
            a_context->reading_chunk_size = false;
            
            // Reset error counter on successful parsing
            a_context->chunked_error_count = 0;
            
            if (l_found_extension) {
                log_it(L_DEBUG, "Chunk %llu has extensions (ignored)", a_context->current_chunk_id);
            }
            
            if (l_chunk_size == 0) {
                // Last chunk - now need to handle trailer headers and final CRLF
                log_it(L_DEBUG, "Processing last chunk (0-size)");
                
                // Look for trailer headers or final CRLF
                // Format: "0\r\n[trailer-headers]\r\n"
                // We already consumed "0\r\n", now look for final "\r\n"
                
                while (l_processed < a_size) {
                    // Look for next CRLF
                    const char *l_trailer_data = (const char*)(l_data + l_processed);
                    size_t l_trailer_remaining = a_size - l_processed;
                    const char *l_trailer_crlf = dap_memmem_n(l_trailer_data, l_trailer_remaining, "\r\n", 2);
                    
                    if (!l_trailer_crlf) {
                        // Need more data for trailer/final CRLF
                        break;
                    }
                    
                    size_t l_trailer_line_len = l_trailer_crlf - l_trailer_data;
                    
                    if (l_trailer_line_len == 0) {
                        // Empty line - this is the final CRLF
                        l_processed += 2; // Skip final \r\n
                        *a_consumed = l_processed;
                        log_it(L_DEBUG, "Chunked transfer complete");
                        return HTTP_PROCESS_COMPLETE;
                    } else {
                        // Non-empty line - this is a trailer header
                        log_it(L_DEBUG, "Skipping trailer header: %.*s", (int)l_trailer_line_len, l_trailer_data);
                        l_processed += l_trailer_line_len + 2; // Skip trailer + CRLF
                        // Continue looking for final CRLF
                    }
                }
                
                // Need more data for final CRLF or trailer completion
                break;
            }
            
        } else {
            // Reading chunk data
            if (a_context->current_chunk_size == 0) {
                log_it(L_ERROR, "Invalid state: reading chunk data but chunk size is 0");
                return HTTP_PROCESS_ERROR;
            }
            
            size_t l_chunk_remaining = a_context->current_chunk_size - a_context->current_chunk_read;
            size_t l_data_remaining = a_size - l_processed;
            size_t l_to_read = dap_min(l_chunk_remaining, l_data_remaining);
            
            if (l_to_read > 0) {
                // Enhanced chunk integrity check
                if (a_context->current_chunk_read + l_to_read > a_context->current_chunk_size) {
                    log_it(L_ERROR, "Chunk overflow detected (chunk %llu): %zu + %zu > %zu", 
                           a_context->current_chunk_id,
                           a_context->current_chunk_read, l_to_read, a_context->current_chunk_size);
                    return HTTP_PROCESS_ERROR;
                }
                
                // Process chunk data (copy to buffer or streaming)
                if (!a_context->streaming_enabled) {
                    // Accumulation mode - copy to buffer with overflow protection
                    if (a_context->response_size + l_to_read > a_context->response_capacity) {
                        log_it(L_ERROR, "Response buffer overflow in chunked accumulation: %zu + %zu > %zu",
                               a_context->response_size, l_to_read, a_context->response_capacity);
                        return HTTP_PROCESS_ERROR;
                    }
                    
                    memcpy(a_context->response_buffer + a_context->response_size, 
                           l_data + l_processed, l_to_read);
                    a_context->response_size += l_to_read;
                } else {
                    // Streaming mode - ZERO-COPY transfer via progress callback
                    if (a_context->client->callbacks.progress_cb) {
                        // Check global streaming limit
                        if (a_context->streamed_body_size + l_to_read > DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT) {
                            log_it(L_ERROR, "Streaming would exceed global limit: %zu + %zu > %zu",
                                   a_context->streamed_body_size, l_to_read, 
                                   (size_t)DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT);
                            return HTTP_PROCESS_ERROR;
                        }
                        
                        a_context->streamed_body_size += l_to_read;
                        a_context->client->callbacks.progress_cb(
                            a_context->client,
                            a_context->streamed_body_size, // Updated size
                            0  // For chunked, total size is unknown
                        );
                    }
                }
                
                // Update progress
                l_processed += l_to_read;
                a_context->current_chunk_read += l_to_read;
            }
            
            // Check chunk completion
            if (a_context->current_chunk_read >= a_context->current_chunk_size) {
                // Look for trailing CRLF after chunk data
                if (l_processed + 1 < a_size && 
                    l_data[l_processed] == '\r' && l_data[l_processed + 1] == '\n') {
                    
                    l_processed += 2; // Skip trailing CRLF
                    a_context->reading_chunk_size = true; // Ready for next chunk
                    log_it(L_DEBUG, "Completed chunk %llu (%zu bytes)", 
                           a_context->current_chunk_id, a_context->current_chunk_size);
                } else {
                    // Need more data for trailing CRLF
                    break;
                }
            }
        }
    }
    
    *a_consumed = l_processed;
    return HTTP_PROCESS_NEED_MORE_DATA; // Need more data
}

// === REDIRECT HANDLING FUNCTIONS ===

/**
 * @brief Check if status code indicates a redirect
 * @param a_status HTTP status code
 * @return true if redirect status
 */
static bool s_is_redirect_status_code(http_status_code_t a_status)
{
    return (a_status == Http_Status_MovedPermanently ||    // 301
            a_status == Http_Status_Found ||               // 302  
            a_status == Http_Status_SeeOther ||            // 303
            a_status == Http_Status_TemporaryRedirect ||   // 307
            a_status == Http_Status_PermanentRedirect);    // 308
}

/**
 * @brief Process HTTP redirect response (request already updated in header parsing)
 * @param a_context HTTP context  
 * @return HTTP_PROCESS_TRANSITION on success (redirect initiated), negative on error
 */
static int s_process_http_redirect(dap_http_client_context_t *a_context)
{
    if (!a_context) {
        log_it(L_ERROR, "Invalid context");
        return HTTP_PROCESS_ERROR;
    }
    
    // Check redirect limits
    if (a_context->redirect_count >= a_context->max_redirects) {
        log_it(L_ERROR, "Maximum redirects exceeded: %d", a_context->max_redirects);
        return DAP_HTTP2_CLIENT_ERROR_TOO_MANY_REDIRECTS;
    }
    
    // Check redirect enabled
    if (!a_context->follow_redirects) {
        log_it(L_DEBUG, "Redirects disabled, stopping at %d", a_context->status_code);
        return HTTP_PROCESS_SUCCESS; // Not an error, continue processing normally
    }
    
    // Increment redirect counter
    a_context->redirect_count++;
    
    log_it(L_DEBUG, "Processing redirect #%d (request already updated)", a_context->redirect_count);
    
    // Return special code to indicate redirect transition is needed
    // The caller will handle session creation and new connection
    return HTTP_PROCESS_TRANSITION;
}

/**
 * @brief Parse URL and update request fields (smart handling for redirects)
 * @param a_request Request instance
 * @param a_url URL to parse (can be absolute or relative)
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_parse_url(dap_http2_client_request_t *a_request, const char *a_url, size_t a_url_size, http_status_code_t a_redirect_status)
{
    if (!a_request || !a_url) {
        log_it(L_ERROR, "Invalid arguments in dap_http2_client_request_parse_url");
        return -1;
    }

    // EFFICIENT: Calculate length once and set end pointer
    size_t l_url_len = a_url_size ? a_url_size : strlen(a_url);
    const char *l_url_end = a_url + l_url_len;
    
    // Check if URL is absolute (starts with http:// or https://)
    bool l_is_absolute = false;
    const char *l_url_start = a_url;
    uint16_t l_default_port = 80;
    bool l_is_ssl = false;
    
    if (l_url_len >= 7 && strncasecmp(a_url, "http://", 7) == 0) {
        l_is_absolute = true;
        l_url_start = a_url + 7;
        l_default_port = 80;
        l_is_ssl = false;
    } else if (l_url_len >= 8 && strncasecmp(a_url, "https://", 8) == 0) {
        l_is_absolute = true;
        l_url_start = a_url + 8;
        l_default_port = 443;
        l_is_ssl = true;
    }
    
    if (l_is_absolute) {
        // === ABSOLUTE URL PARSING ===
        // Clean up old URL data completely
        DAP_DEL_MULTY(a_request->host, a_request->path, a_request->query_string);
        
        // EFFICIENT: Find delimiters in single pass using pointer arithmetic
        const char *l_path_start = NULL;
        const char *l_port_start = NULL;
        const char *l_query_start = NULL;
        const char *l_host_end = l_url_end;  // Default: host ends at URL end
        
        for (const char *p = l_url_start; p < l_url_end; p++) {
            if (*p == '/' && !l_path_start) {
                l_path_start = p;
                l_host_end = p;
                break;
            } else if (*p == ':' && !l_port_start && !l_path_start) {
                l_port_start = p;
            }
        }
        
        // Find query string separator after path
        if (l_path_start) {
            for (const char *p = l_path_start; p < l_url_end; p++) {
                if (*p == '?' && !l_query_start) {
                    l_query_start = p;
                    break;
                }
            }
        }
        
        // If port found but no path, adjust host end
        if (l_port_start && !l_path_start) {
            l_host_end = l_port_start;
        }

        // EFFICIENT: Extract hostname
        size_t l_host_len = l_host_end - l_url_start;
        if (l_host_len == 0 || l_host_len >= DAP_HOSTADDR_STRLEN) {
            log_it(L_ERROR, "Invalid hostname length: %zu", l_host_len);
            return -3;
        }
        
        a_request->host = DAP_NEW_Z_SIZE(char, l_host_len + 1);
        if (!a_request->host) {
            log_it(L_CRITICAL, "Failed to allocate memory for hostname");
            return -4;
        }
        memcpy(a_request->host, l_url_start, l_host_len);
        a_request->host[l_host_len] = '\0';

        // EFFICIENT: Parse port
        a_request->port = l_default_port;
        
        if (l_port_start) {
            const char *l_port_str = l_port_start + 1;
            const char *l_port_end = l_path_start ? l_path_start : l_url_end;
            
            // EFFICIENT: Manual port parsing with pointer arithmetic
            uint16_t l_port_val = 0;
            bool l_valid_port = true;
            
            for (const char *p = l_port_str; p < l_port_end && *p >= '0' && *p <= '9'; p++) {
                uint16_t l_new_val = l_port_val * 10 + (*p - '0');
                if (l_new_val < l_port_val) {
                    l_valid_port = false;
                    break;
                }
                l_port_val = l_new_val;
            }
            
            if (l_valid_port && l_port_val > 0) {
                a_request->port = l_port_val;
            } else {
                log_it(L_WARNING, "Invalid port in URL, using default %u", l_default_port);
            }
        }

        // Set SSL flag
        a_request->use_ssl = l_is_ssl;
        
        // EFFICIENT: Extract path (up to query string)
        const char *l_path_end = l_query_start ? l_query_start : l_url_end;
        if (l_path_start) {
            size_t l_path_len = l_path_end - l_path_start;
            a_request->path = DAP_NEW_Z_SIZE(char, l_path_len + 1);
            if (!a_request->path) {
                log_it(L_CRITICAL, "Failed to allocate memory for path");
                DAP_DEL_Z(a_request->host);
                return -5;
            }
            memcpy(a_request->path, l_path_start, l_path_len);
            a_request->path[l_path_len] = '\0';
        } else {
            a_request->path = dap_strdup("/");
            if (!a_request->path) {
                log_it(L_CRITICAL, "Failed to allocate memory for default path");
                DAP_DEL_Z(a_request->host);
                return -5;
            }
        }
        
        // EFFICIENT: Extract query string (including '?')
        if (l_query_start) {
            size_t l_query_len = l_url_end - l_query_start;
            a_request->query_string = DAP_NEW_Z_SIZE(char, l_query_len + 1);
            if (!a_request->query_string) {
                log_it(L_CRITICAL, "Failed to allocate memory for query string");
                DAP_DEL_MULTY(a_request->host, a_request->path);
                return -6;
            }
            memcpy(a_request->query_string, l_query_start, l_query_len);
            a_request->query_string[l_query_len] = '\0';
        } else {
            a_request->query_string = NULL;
        }
        
        // Add Host header automatically when URL is set
        if (a_request->host) {
            dap_http2_client_request_add_header(a_request, "Host", a_request->host);
        }
        
        log_it(L_DEBUG, "Parsed absolute URL: host='%s', port=%u, path='%s', ssl=%s",
               a_request->host, a_request->port, a_request->path, 
               a_request->use_ssl ? "enabled" : "disabled");
               
    } else {
        // === RELATIVE URL PARSING ===
        // Only update path and query_string, preserve host/port/ssl
        DAP_DEL_MULTY(a_request->path, a_request->query_string);
        
        // Find query string separator
        const char *l_query_start = NULL;
        for (const char *p = l_url_start; p < l_url_end; p++) {
            if (*p == '?' && !l_query_start) {
                l_query_start = p;
                break;
            }
        }
        
        // Extract path (up to query string)
        const char *l_path_end = l_query_start ? l_query_start : l_url_end;
        size_t l_path_len = l_path_end - l_url_start;
        
        if (l_path_len > 0) {
            a_request->path = DAP_NEW_Z_SIZE(char, l_path_len + 1);
            if (!a_request->path) {
                log_it(L_CRITICAL, "Failed to allocate memory for relative path");
                return -7;
            }
            memcpy(a_request->path, l_url_start, l_path_len);
            a_request->path[l_path_len] = '\0';
        } else {
            a_request->path = dap_strdup("/");
            if (!a_request->path) {
                log_it(L_CRITICAL, "Failed to allocate memory for default path");
                return -7;
            }
        }
        
        // Extract query string (including '?')
        if (l_query_start) {
            size_t l_query_len = l_url_end - l_query_start;
            a_request->query_string = DAP_NEW_Z_SIZE(char, l_query_len + 1);
            if (!a_request->query_string) {
                log_it(L_CRITICAL, "Failed to allocate memory for query string");
                DAP_DEL_Z(a_request->path);
                return -8;
            }
            memcpy(a_request->query_string, l_query_start, l_query_len);
            a_request->query_string[l_query_len] = '\0';
        } else {
            a_request->query_string = NULL;
        }
        
        log_it(L_DEBUG, "Parsed relative URL: path='%s', query='%s' (host/port/ssl preserved)",
               a_request->path, a_request->query_string ? a_request->query_string : "none");
    }

    // Handle HTTP method changes for redirects (RFC 7231)
    if (a_redirect_status != 0) {
        if (a_redirect_status == 303 && a_request->method != HTTP_HEAD) {
            // 303 See Other: MUST change to GET (except HEAD)
            a_request->method = HTTP_GET;
            DAP_DEL_Z(a_request->body_data);
            a_request->body_size = 0;
            log_it(L_DEBUG, "303 redirect: changed method to GET");
        } else if ((a_redirect_status == 301 || a_redirect_status == 302) && a_request->method == HTTP_POST) {
            // 301/302 with POST: change to GET for compatibility
            a_request->method = HTTP_GET;
            DAP_DEL_Z(a_request->body_data);
            a_request->body_size = 0;
            log_it(L_DEBUG, "%d redirect: changed POST to GET for compatibility", a_redirect_status);
        }
        // 307/308: method NEVER changes (not handled here - already correct)
    }

    log_it(L_DEBUG, "Parsed URL successfully: %s", a_url);
    return 0;
}

