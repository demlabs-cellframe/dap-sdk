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
#include <stdatomic.h>
#include "dap_common.h"
#include "dap_worker.h"
#include "dap_stream_callbacks.h"
#include "dap_http2_session.h"
#include "dap_http2_stream.h"
#include "dap_http_header.h"
#include "http_status_code.h"

// === UID CONSTANTS ===
#define INVALID_STREAM_UID     0x0000000000000000UL
#define MIN_VALID_STREAM_UID   0x0000000000000001UL
#define MAX_WORKERS            256

// === HTTP METHODS ===
// Using common enum from dap_http_header.h

// === HTTP CLIENT STATES ===
typedef enum {
    DAP_HTTP2_CLIENT_STATE_IDLE,           // Клиент создан, но запрос не отправлен
    DAP_HTTP2_CLIENT_STATE_REQUESTING,     // Запрос отправляется/отправлен
    DAP_HTTP2_CLIENT_STATE_RECEIVING,      // Получаем ответ
    DAP_HTTP2_CLIENT_STATE_COMPLETE,       // Ответ получен полностью
    DAP_HTTP2_CLIENT_STATE_ERROR,          // Ошибка в процессе
    DAP_HTTP2_CLIENT_STATE_CANCELLED       // Запрос отменён пользователем
} dap_http2_client_state_t;

// === HTTP CLIENT ERROR CODES ===
typedef enum {
    DAP_HTTP2_CLIENT_ERROR_NONE = 0,
    DAP_HTTP2_CLIENT_ERROR_INVALID_URL,
    DAP_HTTP2_CLIENT_ERROR_INVALID_METHOD,
    DAP_HTTP2_CLIENT_ERROR_CONNECTION_FAILED,
    DAP_HTTP2_CLIENT_ERROR_TIMEOUT,
    DAP_HTTP2_CLIENT_ERROR_CANCELLED,
    DAP_HTTP2_CLIENT_ERROR_INTERNAL,
    DAP_HTTP2_CLIENT_ERROR_TOO_MANY_REDIRECTS,
    DAP_HTTP2_CLIENT_ERROR_INVALID_REDIRECT_URL,
    DAP_HTTP2_CLIENT_ERROR_REDIRECT_LOOP,
    DAP_HTTP2_CLIENT_ERROR_REDIRECT_WITHOUT_LOCATION  // Redirect status without Location header
} dap_http2_client_error_t;

// === REDIRECT CONSTANTS ===
#define DAP_HTTP2_CLIENT_MAX_REDIRECTS_DEFAULT    5
#define DAP_HTTP2_CLIENT_MAX_LOCATION_LENGTH      2048

// === EFFICIENT UTILITY FUNCTIONS ===

// HTTP method functions are now in dap_http_header.h

// === UID UTILITIES ===
static inline uint8_t dap_http2_extract_worker_id(uint64_t stream_uid) {
    return dap_stream_uid_extract_worker_id(stream_uid);
}

static inline uint32_t dap_http2_extract_esocket_uid(uint64_t stream_uid) {
    return dap_stream_uid_extract_esocket_uid(stream_uid);
}

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct dap_http2_client dap_http2_client_t;
// NOTE: dap_stream_profile_t is defined in dap_stream_callbacks.h

// === CALLBACK TYPES ===

typedef void (*dap_http2_client_response_cb_t)(dap_http2_client_t *a_client,
                                               int a_status_code,
                                               const void *a_data,
                                               size_t a_data_size);

typedef void (*dap_http2_client_error_cb_t)(dap_http2_client_t *a_client,
                                            dap_http2_client_error_t a_error);

typedef void (*dap_http2_client_progress_cb_t)(dap_http2_client_t *a_client,
                                               size_t a_bytes_received,
                                               size_t a_total_bytes);

// Client callbacks
typedef struct dap_http2_client_callbacks {
    dap_http2_client_response_cb_t response_cb;
    dap_http2_client_error_cb_t error_cb;
    dap_http2_client_progress_cb_t progress_cb;
} dap_http2_client_callbacks_t;

// === CONFIGURATION ===

typedef struct dap_http2_client_config {
    // Timeouts
    uint64_t connect_timeout_ms;
    uint64_t read_timeout_ms;
    
    // Limits
    size_t max_response_size;
    size_t max_redirects;
    
    // Options
    bool follow_redirects;
    bool verify_ssl;
    bool enable_compression;
    
    // SSL
    char *ssl_cert_path;
    char *ssl_key_path;
    char *ssl_ca_path;
} dap_http2_client_config_t;

// === REQUEST STRUCTURE ===

typedef struct dap_http2_client_request {
    // Request details
    dap_http_method_t method;        // EFFICIENT: enum instead of string
    char *host;
    char *path;                      // Path component from URL (without leading slash)
    char *query_string;              // Query string with ? (e.g., "?name=john&page=2")
    uint16_t port;
    bool use_ssl;
    
    // Headers
    dap_http_header_t *headers;
    size_t headers_size;
    
    // Body (for POST/PUT/PATCH only)
    void *body_data;
    size_t body_size;
} dap_http2_client_request_t;

// === MAIN CLIENT STRUCTURE ===

typedef struct dap_http2_client {
    // === UID MANAGEMENT ===
    _Atomic uint64_t stream_uid;              // Composite UID (worker_id + stream_id)
    
    // === CLIENT STATE ===
    _Atomic dap_http2_client_state_t state;
    
    // === CONFIGURATION ===
    dap_http2_client_config_t config;
    
    // === CURRENT REQUEST ===
    dap_http2_client_request_t *current_request;
    
    // === CALLBACKS ===
    dap_http2_client_callbacks_t callbacks;
    void *callbacks_arg;
    
} dap_http2_client_t;

// === GLOBAL INITIALIZATION ===

/**
 * @brief Initialize HTTP2 client module
 * @return 0 on success, negative on error
 */
int dap_http2_client_init(void);

/**
 * @brief Deinitialize HTTP2 client module
 */
void dap_http2_client_deinit(void);

// === CLIENT LIFECYCLE ===

/**
 * @brief Create new HTTP2 client with default timeouts
 * @return New client instance or NULL on error
 */
dap_http2_client_t *dap_http2_client_create(dap_worker_t *a_worker);

/**
 * @brief Create new HTTP2 client with custom timeouts
 * @param a_worker Worker thread
 * @param a_connect_timeout_ms Connect timeout in milliseconds
 * @param a_read_timeout_ms Read timeout in milliseconds
 * @return New client instance or NULL on error
 */
dap_http2_client_t *dap_http2_client_create_with_timeouts(dap_worker_t *a_worker,
                                                          uint64_t a_connect_timeout_ms,
                                                          uint64_t a_read_timeout_ms);

/**
 * @brief Delete client and cleanup resources
 * @param a_client Client to delete
 */
void dap_http2_client_delete(dap_http2_client_t *a_client);

// === CONFIGURATION ===

/**
 * @brief Set client configuration
 * @param a_client Client instance
 * @param a_config Configuration structure
 */
void dap_http2_client_set_config(dap_http2_client_t *a_client, const dap_http2_client_config_t *a_config);

/**
 * @brief Get client configuration
 * @param a_client Client instance
 * @return Configuration structure
 */
dap_http2_client_config_t *dap_http2_client_get_config(dap_http2_client_t *a_client);

/**
 * @brief Set client callbacks
 * @param a_client Client instance
 * @param a_callbacks Callbacks structure
 * @param a_callbacks_arg User argument for callbacks
 */
void dap_http2_client_set_callbacks(dap_http2_client_t *a_client,
                                    const dap_http2_client_callbacks_t *a_callbacks,
                                    void *a_callbacks_arg);

// === REQUEST MANAGEMENT ===

/**
 * @brief Create new request
 * @return New request instance or NULL on error
 */
dap_http2_client_request_t *dap_http2_client_request_create(void);

/**
 * @brief Delete request and cleanup resources
 * @param a_request Request to delete
 */
void dap_http2_client_request_delete(dap_http2_client_request_t *a_request);

/**
 * @brief Parse URL and update request fields (smart handling for redirects)
 * @param a_request Request instance
 * @param a_url URL to parse (can be absolute or relative)
 * @param a_redirect_status HTTP redirect status code (0 for non-redirect usage)
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_parse_url(dap_http2_client_request_t *a_request, const char *a_url, size_t a_url_size, http_status_code_t a_redirect_status);

/**
 * @brief Set request method (string version)
 * @param a_request Request instance
 * @param a_method HTTP method string (GET, POST, etc.)
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_set_method(dap_http2_client_request_t *a_request, const char *a_method);

/**
 * @brief Set request method (EFFICIENT: enum version)
 * @param a_request Request instance
 * @param a_method HTTP method enum
 */
static inline void dap_http2_client_request_set_method_enum(dap_http2_client_request_t *a_request, dap_http_method_t a_method) {
    if (a_request && a_method < HTTP_METHOD_COUNT) {
        a_request->method = a_method;
    }
}

/**
 * @brief Add single header to request
 * @param a_request Request instance
 * @param a_name Header name
 * @param a_value Header value
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_add_header(dap_http2_client_request_t *a_request, const char *a_name, const char *a_value);

/**
 * @brief Set request headers (legacy compatibility - parses header string)
 * @param a_request Request instance
 * @param a_headers Headers string
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_set_headers(dap_http2_client_request_t *a_request, const char *a_headers);

/**
 * @brief Set request body
 * @param a_request Request instance
 * @param a_data Body data
 * @param a_size Body size
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_set_body(dap_http2_client_request_t *a_request, const void *a_data, size_t a_size);

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
                                  int *a_status_code);

/**
 * @brief Execute asynchronous request
 * @param a_client Client instance
 * @param a_request Request to execute
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_async(dap_http2_client_t *a_client,
                                   const dap_http2_client_request_t *a_request);

/**
 * @brief Cancel ongoing request
 * @param a_client Client instance
 */
void dap_http2_client_cancel(dap_http2_client_t *a_client);

/**
 * @brief Close client connection
 * @param a_client Client instance
 */
void dap_http2_client_close(dap_http2_client_t *a_client);

// === STATE QUERIES ===

/**
 * @brief Get current client state
 * @param a_client Client instance
 * @return Current client state
 */
dap_http2_client_state_t dap_http2_client_get_state(const dap_http2_client_t *a_client);

/**
 * @brief Check if client is busy
 * @param a_client Client instance
 * @return true if busy
 */
bool dap_http2_client_is_busy(const dap_http2_client_t *a_client);

/**
 * @brief Check if request is complete
 * @param a_client Client instance
 * @return true if complete
 */
bool dap_http2_client_is_complete(const dap_http2_client_t *a_client);

/**
 * @brief Check if client is in error state
 * @param a_client Client instance
 * @return true if in error state
 */
bool dap_http2_client_is_error(const dap_http2_client_t *a_client);

// === STATISTICS ===

/**
 * @brief Get stream UID from client
 * @param a_client Client instance
 * @return Stream UID or INVALID_STREAM_UID if not assigned
 */
uint64_t dap_http2_client_get_stream_uid(const dap_http2_client_t *a_client);

/**
 * @brief Check if client is in async mode (has callbacks)
 * @param a_client Client instance
 * @return true if async mode
 */
bool dap_http2_client_is_async(const dap_http2_client_t *a_client);

/**
 * @brief Check if client is cancelled (from state)
 * @param a_client Client instance
 * @return true if cancelled
 */
bool dap_http2_client_is_cancelled(const dap_http2_client_t *a_client);

// === STREAM INITIALIZATION ===

/**
 * @brief Create client with stream profile (embedded transitions)
 * @param a_worker Worker thread
 * @param a_profile Stream profile with all callbacks
 * @return New client instance or NULL on error
 */
dap_http2_client_t *dap_http2_client_create_with_profile(dap_worker_t *a_worker,
                                                         const dap_stream_profile_t *a_profile);

// === CONVENIENCE FUNCTIONS ===

/**
 * @brief Execute simple GET request synchronously
 * @param a_worker Worker thread
 * @param a_url URL to request
 * @param a_response_data Output: response data (caller must free)
 * @param a_response_size Output: response size
 * @param a_status_code Output: HTTP status code
 * @return 0 on success, negative on error
 */
int dap_http2_client_get_sync(dap_worker_t *a_worker,
                              const char *a_url,
                              void **a_response_data,
                              size_t *a_response_size,
                              int *a_status_code);

/**
 * @brief Execute simple POST request synchronously
 * @param a_worker Worker thread
 * @param a_url URL to request
 * @param a_body_data Request body data
 * @param a_body_size Request body size
 * @param a_content_type Content type header
 * @param a_response_data Output: response data (caller must free)
 * @param a_response_size Output: response size
 * @param a_status_code Output: HTTP status code
 * @return 0 on success, negative on error
 */
int dap_http2_client_post_sync(dap_worker_t *a_worker,
                               const char *a_url,
                               const void *a_body_data,
                               size_t a_body_size,
                               const char *a_content_type,
                               void **a_response_data,
                               size_t *a_response_size,
                               int *a_status_code);

/**
 * @brief Execute simple GET request asynchronously
 * @param a_worker Worker thread
 * @param a_url URL to request
 * @param a_response_cb Response callback
 * @param a_error_cb Error callback
 * @param a_callbacks_arg User argument for callbacks
 * @return 0 on success, negative on error
 */
int dap_http2_client_get_async(dap_worker_t *a_worker,
                               const char *a_url,
                               dap_http2_client_response_cb_t a_response_cb,
                               dap_http2_client_error_cb_t a_error_cb,
                               void *a_callbacks_arg);

// === CONFIGURATION HELPERS ===

/**
 * @brief Get default configuration
 * @return Default configuration structure
 */
dap_http2_client_config_t dap_http2_client_config_default(void);

/**
 * @brief Set configuration timeouts
 * @param a_config Configuration structure
 * @param a_connect_timeout_ms Connect timeout in milliseconds
 * @param a_read_timeout_ms Read timeout in milliseconds
 */
void dap_http2_client_config_set_timeouts(dap_http2_client_config_t *a_config,
                                          uint64_t a_connect_timeout_ms,
                                          uint64_t a_read_timeout_ms);

// === UTILITY FUNCTIONS ===

/**
 * @brief Get client state string representation
 * @param a_state Client state
 * @return State name string
 */
const char* dap_http2_client_state_to_str(dap_http2_client_state_t a_state);

/**
 * @brief Get client error string representation
 * @param a_error Client error
 * @return Error name string
 */
const char* dap_http2_client_error_to_str(dap_http2_client_error_t a_error);

#ifdef __cplusplus
}
#endif 