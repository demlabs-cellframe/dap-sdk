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


#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "dap_net.h"
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_string.h"
#include "dap_events_socket.h"
#include "dap_timerfd.h"
#include "dap_stream_ch_proc.h"
#include "dap_context.h"
#include "dap_server.h"
#include "dap_client.h"
#include "dap_client_pvt.h"
#include "dap_client_http.h"
#include "dap_enc_base64.h"
#include "dap_http_header.h"

#ifndef DAP_NET_CLIENT_NO_SSL
#include <wolfssl/options.h>
#include "wolfssl/ssl.h"
#endif

#define LOG_TAG "dap_client_http"

#define DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT (10 * 1024 * 1024) // 10MB maximum response size

// Static variables
static bool s_debug_more = false;
static uint64_t s_client_timeout_ms;
static uint64_t s_client_timeout_read_after_connect_ms;
static uint32_t s_max_attempts = 5;

#define DAP_CLIENT_HTTP_STREAMING_THRESHOLD_DEFAULT (1024 * 1024) // 1MB
static size_t s_streaming_threshold = DAP_CLIENT_HTTP_STREAMING_THRESHOLD_DEFAULT;

// Smart streaming with optimal buffering constants
// Strategy: Start small → Smart decision → One expansion if needed → Zero further reallocs
#define DAP_CLIENT_HTTP_STREAMING_BUFFER_SIZE (128 * 1024) // 128KB optimal for streaming (headers + max TCP window)
#define DAP_CLIENT_HTTP_MAX_HEADERS_SIZE (16 * 1024)       // 16KB max for headers

#ifndef DAP_NET_CLIENT_NO_SSL
static WOLFSSL_CTX *s_ctx;
#endif

// Streaming mode states
typedef enum {
    DAP_HTTP_STREAMING_UNDETERMINED = 0,  // Not yet determined
    DAP_HTTP_STREAMING_ENABLED = 1,       // Streaming mode active
    DAP_HTTP_STREAMING_DISABLED = 2       // Normal accumulation mode
} dap_http_streaming_mode_t;

// Callback-only context structure for async API
typedef struct dap_client_http_async_context {
    dap_client_http_callback_full_t response_callback;
    dap_client_http_callback_data_t simple_response_callback; // Simple callback (without headers)
    dap_client_http_callback_error_t error_callback;
    dap_client_http_callback_started_t started_callback;
    dap_client_http_callback_progress_t progress_callback;
    void *user_arg;
    size_t streamed_body_size;  // Track streamed body data
    uint8_t redirect_count;  // Redirect counter
    dap_http_streaming_mode_t streaming_mode;   // Streaming mode state (once determined, fixed)
} dap_client_http_async_context_t;

// Forward declarations
static void s_http_connected(dap_events_socket_t * a_esocket); // Connected
#ifndef DAP_NET_CLIENT_NO_SSL
static void s_http_ssl_connected(dap_events_socket_t * a_esocket); // connected SSL callback
#endif
static void s_client_http_delete(dap_client_http_t * a_client_http);
static void s_http_read(dap_events_socket_t * a_es, void * arg);
static void s_http_error(dap_events_socket_t * a_es, int a_arg);
static void s_es_delete(dap_events_socket_t * a_es, void * a_arg);
static bool s_timer_timeout_check(void * a_arg);
static bool s_timer_timeout_after_connected_check(void * a_arg);
static int s_parse_response_header(dap_client_http_t *a_client_http, const char *a_header_line, size_t a_header_len);
static void s_async_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code);
static void s_async_error_callback(int a_error_code, void *a_arg);
static void s_client_http_request_async_impl(
        dap_worker_t * a_worker,
        const char *a_uplink_addr, 
        uint16_t a_uplink_port, 
        const char * a_method,
        const char* a_request_content_type, 
        const char * a_path, 
        const void *a_request, 
        size_t a_request_size,
        char * a_cookie, 
        dap_client_http_async_context_t *a_ctx,
        char *a_custom_headers,
        bool a_is_https,
        bool a_follow_redirects);

static int s_send_http_request(dap_events_socket_t *a_es, dap_client_http_t *a_client_http);
http_status_code_t s_extract_http_code(void *a_response, size_t a_response_size);
static size_t s_parse_chunk_size_line(const char *a_line, size_t a_line_len);
static bool s_process_chunked_data(dap_client_http_t *a_client_http, dap_client_http_async_context_t *a_ctx, dap_events_socket_t *a_es);

/**
 * @brief Extract HTTP status code from response
 */
http_status_code_t s_extract_http_code(void *a_response, size_t a_response_size) {
    char l_ver[16] = { '\0' };
    int l_err = 0, l_ret = sscanf((char*)a_response, "%[^ ] %d", l_ver, &l_err);
    return l_ret == 2 && !dap_strncmp(l_ver, "HTTP/", 5) ? (http_status_code_t)l_err : 0;
}

/**
 * @brief Parse chunk size from hex line (e.g. "1a3\r\n" -> 419)
 * @param a_line Line containing hex chunk size
 * @param a_line_len Length of the line
 * @return Chunk size in bytes, or SIZE_MAX on error
 */
static size_t s_parse_chunk_size_line(const char *a_line, size_t a_line_len) {
    if (!a_line || a_line_len < 3) // At least "0\r\n"
        return SIZE_MAX;
    
    // Find end of hex number (before \r\n or ;)
    size_t l_hex_len = 0;
    for (size_t i = 0; i < a_line_len && l_hex_len == 0; i++) {
        if (a_line[i] == '\r' || a_line[i] == '\n' || a_line[i] == ';') {
            l_hex_len = i;
        }
    }
    
    if (l_hex_len == 0 || l_hex_len > 16) // Sanity check
        return SIZE_MAX;
    
    // Parse hex number
    char l_hex_str[17] = {0};
    strncpy(l_hex_str, a_line, l_hex_len);
    
    char *l_endptr = NULL;
    unsigned long l_size = strtoul(l_hex_str, &l_endptr, 16);
    
    if (l_endptr == l_hex_str || *l_endptr != '\0') {
        log_it(L_WARNING, "Invalid chunk size hex: '%s'", l_hex_str);
        return SIZE_MAX;
    }
    
    return (size_t)l_size;
}

/**
 * @brief Process chunked transfer encoded data
 * @param a_client_http HTTP client
 * @param a_ctx Async context (may be NULL)
 * @param a_es Event socket
 * @return true if processing complete, false if more data needed
 */
static bool s_process_chunked_data(dap_client_http_t *a_client_http, dap_client_http_async_context_t *a_ctx, dap_events_socket_t *a_es)
{
    if (!a_client_http->is_chunked)
        return false;

    uint8_t *l_data = a_client_http->response + a_client_http->header_length;
    size_t l_data_size = a_client_http->response_size - a_client_http->header_length;
    size_t l_processed = 0;

    while (l_processed < l_data_size) {
        if (a_client_http->is_reading_chunk_size) {
            // Look for CRLF to find end of chunk size line
            const char *l_crlf = NULL;
            if (l_data_size > 1) {
                for (size_t i = l_processed; i < l_data_size - 1; i++) {
                    if (l_data[i] == '\r' && l_data[i + 1] == '\n') {
                        l_crlf = (const char *)(l_data + i);
                        break;
                    }
                }
            }
            
            if (!l_crlf) {
                // Need more data to complete chunk size line
                break;
            }
            
            // Parse chunk size
            size_t l_size_line_len = l_crlf - (const char *)(l_data + l_processed) + 2;
            size_t l_chunk_size = s_parse_chunk_size_line((const char *)(l_data + l_processed), l_size_line_len);
            
            if (l_chunk_size == SIZE_MAX) {
                log_it(L_ERROR, "Failed to parse chunk size");
                if (a_ctx && a_ctx->error_callback) {
                    a_ctx->error_callback(DAP_CLIENT_HTTP_ERROR_CHUNKED_PARSE_ERROR, a_ctx->user_arg);
                    a_client_http->were_callbacks_called = true;
                }
                a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
                return false;
            }
            
            l_processed += l_size_line_len;
            a_client_http->current_chunk_size = l_chunk_size;
            a_client_http->current_chunk_read = 0;
            a_client_http->is_reading_chunk_size = false;
            
            if (l_chunk_size == 0) {
                // Last chunk - look for final CRLF
                if (l_processed + 1 < l_data_size && l_processed < l_data_size &&
                    l_data[l_processed] == '\r' && l_data[l_processed + 1] == '\n') {
                    // Chunked transfer complete
                    if (a_ctx && a_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED) {
                        // Streaming complete
                        a_client_http->were_callbacks_called = true;
                        a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
                    }
                    return true;
                }
                break; // Need more data for final CRLF
            }
        } else {
            // Reading chunk data
            size_t l_chunk_remaining = a_client_http->current_chunk_size - a_client_http->current_chunk_read;
            size_t l_data_remaining = l_data_size - l_processed;
            size_t l_to_read = dap_min(l_chunk_remaining, l_data_remaining);
            
            if (l_to_read > 0) {
                // Send data to streaming callback if enabled
                if (a_ctx && a_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED && a_ctx->progress_callback && l_to_read > 0) {
                    a_ctx->progress_callback(
                        l_data + l_processed,
                        l_to_read,
                        SIZE_MAX, // Total size unknown in chunked mode (prevents division by zero)
                        a_ctx->user_arg
                    );
                    a_ctx->streamed_body_size += l_to_read;
                }
                
                l_processed += l_to_read;
                a_client_http->current_chunk_read += l_to_read;
            }
            
            // Check if chunk is complete
            if (a_client_http->current_chunk_read >= a_client_http->current_chunk_size) {
                // Look for trailing CRLF
                if (l_processed + 1 < l_data_size && l_processed < l_data_size &&
                    l_data[l_processed] == '\r' && l_data[l_processed + 1] == '\n') {
                    l_processed += 2;
                    a_client_http->is_reading_chunk_size = true;
                } else {
                    // Need more data for trailing CRLF
                    break;
                }
            }
        }
    }
    
    // Compact buffer by removing processed data
    if (l_processed > 0) {
        size_t l_remaining = l_data_size - l_processed;
        if (l_remaining > 0) {
            memmove(l_data, l_data + l_processed, l_remaining);
        }
        a_client_http->response_size = a_client_http->header_length + l_remaining;
    }
    
    return false; // Need more data
}

/**
 * @brief s_parse_response_header - Parse single HTTP response header line
 * @param a_client_http HTTP client instance
 * @param a_header_line Header line to parse
 * @param a_header_len Length of header line
 * @return 0 on success, -1 on error
 */
static int s_parse_response_header(dap_client_http_t *a_client_http, const char *a_header_line, size_t a_header_len)
{
    if (!a_header_line || a_header_len < 4)
        return -1;
    
    char l_name[DAP_HTTP$SZ_FIELD_NAME];
    char l_value[DAP_HTTP$SZ_FIELD_VALUE];
    
    // Use common parser
    int l_ret = dap_http_header_parse_line(a_header_line, a_header_len, 
                                           l_name, sizeof(l_name),
                                           l_value, sizeof(l_value));
    if(l_ret != 0)
        return l_ret;
    
    // Add to list
    dap_http_header_add(&a_client_http->response_headers, l_name, l_value);
    
    if(s_debug_more)
        log_it(L_DEBUG, "Parsed response header: '%s: %s'", l_name, l_value);
    
    return 0;
}

/**
 * @brief Send HTTP request with properly formatted headers
 * @param a_es Event socket to send request on
 * @param a_client_http HTTP client context
 * @return 0 on success, -1 on error
 */
static int s_send_http_request(dap_events_socket_t *a_es, dap_client_http_t *a_client_http)
{
    if (!a_es || !a_client_http) {
        log_it(L_ERROR, "Invalid arguments in s_send_http_request");
        return -1;
    }

    char l_request_headers[1024] = { [0]='\0' };
    int l_offset = 0;
    size_t l_offset2 = sizeof(l_request_headers);
    
    // Handle POST-specific headers
    if(a_client_http->request && (dap_strcmp(a_client_http->method, "POST") == 0 || 
                                  dap_strcmp(a_client_http->method, "POST_ENC") == 0)) {
        l_offset += a_client_http->request_content_type
                ? snprintf(l_request_headers, l_offset2, "Content-Type: %s\r\n", a_client_http->request_content_type)
                : 0;

        l_offset += snprintf(l_request_headers + l_offset, l_offset2 -= l_offset, "Content-Length: %zu\r\n", 
                            a_client_http->request_size);
    } else if(!dap_strcmp(a_client_http->method, "GET")) {
        // Add User-Agent for GET requests
        l_offset += snprintf(l_request_headers + l_offset, l_offset2 -= l_offset, "User-Agent: Mozilla\r\n");
    }
    
    // Add custom headers (for all methods)
    if(a_client_http->request_custom_headers) {
        l_offset += snprintf(l_request_headers + l_offset, l_offset2 -= l_offset, "%s", a_client_http->request_custom_headers);
    }

    // Setup cookie header (for all methods)
    if(a_client_http->cookie) {
        l_offset += snprintf(l_request_headers + l_offset, l_offset2 -= l_offset, "Cookie: %s\r\n", a_client_http->cookie);
    }

    bool l_get = !dap_strcmp(a_client_http->method, "GET") && a_client_http->request && a_client_http->request_size;
    bool l_post_with_body = (!dap_strcmp(a_client_http->method, "POST") || !dap_strcmp(a_client_http->method, "POST_ENC")) && 
                            a_client_http->request && a_client_http->request_size;
    
    dap_events_socket_write_f_unsafe(a_es, "%s /%s%s%s HTTP/1.1\r\n" "Host: %s\r\n" "%s\r\n%s",
        a_client_http->method, a_client_http->path ? a_client_http->path : "",
        l_get ? "?" : "", l_get ? (char*)a_client_http->request : "",
        a_client_http->uplink_addr,
        l_request_headers,
        l_post_with_body ? (char*)a_client_http->request : "");
    
    debug_if(s_debug_more && l_post_with_body, L_DEBUG, "Sent %s request with %zu bytes body", 
             a_client_http->method, a_client_http->request_size);
    
    return 0;
}

/**
 * @brief Reset HTTP client state for redirect
 * @param a_client_http HTTP client to reset
 * @param a_new_path New path for request
 */
static void s_client_http_reset_for_redirect(dap_client_http_t *a_client_http, const char *a_new_path)
{
    // Clear response data
    a_client_http->response_size = 0;
    a_client_http->header_length = 0;
    a_client_http->content_length = 0;
    a_client_http->is_header_read = false;
    
    a_client_http->is_chunked = false;
    a_client_http->is_reading_chunk_size = false;
    a_client_http->current_chunk_size = 0;
    a_client_http->current_chunk_read = 0;
    
    // Clear response headers
    while(a_client_http->response_headers) {
        dap_http_header_remove(&a_client_http->response_headers, a_client_http->response_headers);
    }
    
    // Update path - always store without leading slash
    if (a_new_path) {
        DAP_DELETE(a_client_http->path);
        a_client_http->path = dap_strdup(a_new_path + (int)(a_new_path[0] == '/'));
    }

    // Increment redirect counter
    a_client_http->redirect_count++;
}

/**
 * @brief Process HTTP redirect
 * @param a_es Event socket
 * @param a_client_http HTTP client
 * @param a_location_value Location header value
 * @return true if redirect was handled, false if error
 */
static bool s_process_http_redirect(dap_events_socket_t *a_es, dap_client_http_t *a_client_http, const char *a_location_value)
{
    // Parse new URL efficiently without temporary copies or dynamic allocation
    char l_new_addr[DAP_HOSTADDR_STRLEN] = {0};
    uint16_t l_new_port = a_client_http->uplink_port;
    const char *l_new_path = NULL;          // Use pointer instead of allocated string
    bool l_is_https = a_client_http->is_over_ssl;
    
    // Check URL type and parse accordingly
    const char *l_url_start = NULL;
    
    if(strncmp(a_location_value, "http://", 7) == 0) {
        l_url_start = a_location_value + 7;
        l_is_https = false;
        l_new_port = 80;  // Default HTTP port
    } else if(strncmp(a_location_value, "https://", 8) == 0) {
        l_url_start = a_location_value + 8;
        l_is_https = true;
        l_new_port = 443;  // Default HTTPS port
    } else {
        // Relative path - use same host and path as-is (slash will be added in HTTP request)
        dap_strncpy(l_new_addr, a_client_http->uplink_addr, DAP_HOSTADDR_STRLEN);
        l_new_path = a_location_value;  // Direct pointer, no allocation
    }
    
    // Parse absolute URL (HTTP/HTTPS) without copying
    if(l_url_start) {
        // Find delimiters in the URL string
        const char *l_path_start = strchr(l_url_start, '/');
        const char *l_port_start = strchr(l_url_start, ':');
        
        // Determine host end position
        const char *l_host_end;
        if(l_port_start && (!l_path_start || l_port_start < l_path_start)) {
            l_host_end = l_port_start;
        } else if(l_path_start) {
            l_host_end = l_path_start;
        } else {
            l_host_end = l_url_start + strlen(l_url_start);
        }
        
        // Extract hostname directly
        size_t l_host_len = l_host_end - l_url_start;
        if(l_host_len >= DAP_HOSTADDR_STRLEN) {
            log_it(L_WARNING, "Hostname too long in redirect URL, truncating");
            l_host_len = DAP_HOSTADDR_STRLEN - 1;
        }
        memcpy(l_new_addr, l_url_start, l_host_len);
        l_new_addr[l_host_len] = '\0';
        
        // Parse port if present
        if(l_port_start && (!l_path_start || l_port_start < l_path_start)) {
            const char *l_port_str = l_port_start + 1;
            const char *l_port_end = l_path_start ? l_path_start : (l_port_str + strlen(l_port_str));
            
            char *l_endptr = NULL;
            long l_port = strtol(l_port_str, &l_endptr, 10);
            if(l_endptr > l_port_str && (l_endptr == l_port_end || *l_endptr == '/') && 
               l_port > 0 && l_port <= 65535) {
                l_new_port = (uint16_t)l_port;
            } else {
                log_it(L_WARNING, "Invalid port in redirect URL, using default");
                // l_new_port already set to default above
            }
        }
        
        // Set path pointer (no allocation)
        l_new_path = l_path_start ? l_path_start : "/";
    }
    
    // Check if we can reuse the connection
    bool l_can_reuse = (strcmp(l_new_addr, a_client_http->uplink_addr) == 0) &&
                       (l_new_port == a_client_http->uplink_port) &&
                       (l_is_https == a_client_http->is_over_ssl);
    
    if (l_can_reuse) {
        log_it(L_INFO, "Reusing connection for redirect to: %s", a_location_value);
        
        // Reset client state for new request
        s_client_http_reset_for_redirect(a_client_http, l_new_path);
        
        // Send new request on the same connection
        s_send_http_request(a_es, a_client_http);

        // No need to delete l_new_path - it's just a pointer now
        
        return true;
    } else {
        // Need new connection
        log_it(L_INFO, "Need new connection for redirect to: %s:%u (SSL: %s)", 
               l_new_addr, l_new_port, l_is_https ? "yes" : "no");
        
        // Close current connection
        a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
        
        // Check if this was an async request originally
        bool l_is_async = (a_client_http->error_callback == s_async_error_callback);
        dap_client_http_async_context_t *l_redirect_ctx = NULL;
        
        if (l_is_async && a_client_http->callbacks_arg) {
            // Reuse existing async context - much more efficient than allocating new one
            l_redirect_ctx = (dap_client_http_async_context_t *)a_client_http->callbacks_arg;
            
            // Reset redirect-specific fields for new request
            l_redirect_ctx->streamed_body_size = 0;  // New request, reset streamed data counter
            l_redirect_ctx->streaming_mode = DAP_HTTP_STREAMING_UNDETERMINED;  // Re-determine for new server
            l_redirect_ctx->redirect_count = a_client_http->redirect_count + 1;
            
            // All callbacks and user_arg remain the same - no copying needed!
        } else {
            // Original request was sync - need to create new async context
            l_redirect_ctx = DAP_NEW_Z(dap_client_http_async_context_t);
            if(!l_redirect_ctx) {
                log_it(L_ERROR, "Failed to allocate redirect context");
                return false;
            }
            
            l_redirect_ctx->response_callback = a_client_http->response_callback_full;
            l_redirect_ctx->simple_response_callback = a_client_http->response_callback;
            l_redirect_ctx->error_callback = a_client_http->error_callback;
            l_redirect_ctx->user_arg = a_client_http->callbacks_arg;
            l_redirect_ctx->redirect_count = a_client_http->redirect_count + 1;
        }
        
        // Make async request with redirect context (l_new_path will be normalized in the new client)
        s_client_http_request_async_impl(
            a_client_http->worker,
            l_new_addr,
            l_new_port,
            a_client_http->method,
            a_client_http->request_content_type,
            l_new_path,  // Path will be normalized in s_client_http_create_and_connect
            a_client_http->request,
            a_client_http->request_size,
            a_client_http->cookie,
            l_redirect_ctx,
            a_client_http->request_custom_headers,
            l_is_https,
            false
        );
        
        // Mark callbacks as called to prevent calling them in delete callback
        a_client_http->were_callbacks_called = true;
        return true;
    }
}

/**
 * @brief Create HTTP client and initiate connection
 * @note This function contains common logic for both sync and async requests
 * @param a_worker Worker thread
 * @param a_uplink_addr Remote address
 * @param a_uplink_port Remote port
 * @param a_method HTTP method
 * @param a_request_content_type Content type
 * @param a_path URL path
 * @param a_request Request body
 * @param a_request_size Request body size
 * @param a_cookie Cookie header
 * @param a_custom_headers Custom headers
 * @param a_over_ssl Use SSL connection
 * @param a_error_callback Error callback
 * @param a_response_callback Response callback (simple)
 * @param a_response_callback_full Full response callback with headers
 * @param a_callbacks_arg Callback argument
 * @param a_redirect_count Redirect counter
 * @param a_follow_redirects Follow redirects flag
 * @param a_error_code Output error code on failure
 * @return dap_client_http_t* on success (for sync), NULL on failure
 */
static dap_client_http_t* s_client_http_create_and_connect(
    dap_worker_t *a_worker,
    const char *a_uplink_addr,
    uint16_t a_uplink_port,
    const char *a_method,
    const char *a_request_content_type,
    const char *a_path,
    const void *a_request,
    size_t a_request_size,
    char *a_cookie,
    char *a_custom_headers,
    bool a_over_ssl,
    dap_client_http_callback_error_t a_error_callback,
    dap_client_http_callback_data_t a_response_callback,
    dap_client_http_callback_full_t a_response_callback_full,
    void *a_callbacks_arg,
    uint8_t a_redirect_count,
    bool a_follow_redirects,
    int *a_error_code)
{
    // Set up socket callbacks
    static dap_events_socket_callbacks_t l_s_callbacks = {
        .connected_callback = s_http_connected,
        .read_callback = s_http_read,
        .error_callback = s_http_error,
        .delete_callback = s_es_delete
    };

    // Create socket
#ifdef DAP_OS_WINDOWS
    SOCKET l_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (l_socket == INVALID_SOCKET) {
        *a_error_code = WSAGetLastError();
        log_it(L_ERROR, "Socket create error: %d", *a_error_code);
        return NULL;
    }
#else
    int l_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (l_socket == -1) {
        *a_error_code = errno;
        log_it(L_ERROR, "Error %d with socket create", *a_error_code);
        return NULL;
    }
#endif

    // Set socket non-blocking
#if defined DAP_OS_WINDOWS
    u_long l_socket_flags = 1;
    if (ioctlsocket((SOCKET)l_socket, (long)FIONBIO, &l_socket_flags)) {
        *a_error_code = WSAGetLastError();
        log_it(L_ERROR, "Error ioctl %d", *a_error_code);
        closesocket(l_socket);
        return NULL;
    }
#else
    int l_socket_flags = fcntl(l_socket, F_GETFL);
    if (l_socket_flags == -1){
        *a_error_code = errno;
        log_it(L_ERROR, "Error %d can't get socket flags", *a_error_code);
        close(l_socket);
        return NULL;
    }
    // Make it non-block
    if (fcntl(l_socket, F_SETFL, l_socket_flags | O_NONBLOCK) == -1){
        *a_error_code = errno;
        log_it(L_ERROR, "Error %d can't set socket flags", *a_error_code);
        close(l_socket);
        return NULL;
    }
#endif

    dap_events_socket_t *l_ev_socket = dap_events_socket_wrap_no_add(l_socket, &l_s_callbacks);
    if (!l_ev_socket) {
        *a_error_code = ENOMEM;
        log_it(L_ERROR, "Can't wrap socket");
#ifdef DAP_OS_WINDOWS
        closesocket(l_socket);
#else
        close(l_socket);
#endif
        return NULL;
    }

    log_it(L_DEBUG,"Created client request socket %"DAP_FORMAT_SOCKET, l_socket);
    
    // Create HTTP client struct
    dap_client_http_t *l_client_http = DAP_NEW_Z(dap_client_http_t);
    if (!l_client_http) {
        *a_error_code = ENOMEM;
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        dap_events_socket_delete_unsafe(l_ev_socket, true);
        return NULL;
    }
    
    l_ev_socket->_inheritor = l_client_http;
    l_client_http->es = l_ev_socket;
    l_client_http->method = dap_strdup(a_method);
    l_client_http->path = a_path ? dap_strdup(a_path + (int)(a_path[0] == '/')) : NULL;
    l_client_http->request_content_type = dap_strdup(a_request_content_type);

    // Set callbacks BEFORE adding to worker (critical for thread safety)
    l_client_http->error_callback = a_error_callback;
    l_client_http->response_callback = a_response_callback;
    l_client_http->response_callback_full = a_response_callback_full;
    l_client_http->callbacks_arg = a_callbacks_arg;
    l_client_http->redirect_count = a_redirect_count;
    l_client_http->follow_redirects = a_follow_redirects;

    if (a_request && a_request_size) {
        l_client_http->request = DAP_NEW_Z_SIZE(byte_t, a_request_size + 1);
        if (!l_client_http->request) {
            *a_error_code = ENOMEM;
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            s_client_http_delete(l_client_http);
            l_ev_socket->_inheritor = NULL;
            dap_events_socket_delete_unsafe(l_ev_socket, true);
            return NULL;
        }
        l_client_http->request_size = a_request_size;
        memcpy(l_client_http->request, a_request, a_request_size);
    }
    
    dap_strncpy(l_client_http->uplink_addr, a_uplink_addr, DAP_HOSTADDR_STRLEN);
    l_client_http->uplink_port = a_uplink_port;
    l_client_http->cookie = dap_strdup(a_cookie);
    l_client_http->request_custom_headers = dap_strdup(a_custom_headers);

    // Always start with small buffer - smart streaming decision will be made later
    // If streaming is determined necessary, we'll expand to optimal size then
    l_client_http->response_size_max = 4096;
    
    l_client_http->response = DAP_NEW_Z_SIZE(uint8_t, l_client_http->response_size_max);
    if (!l_client_http->response) {
        *a_error_code = ENOMEM;
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        s_client_http_delete(l_client_http);
        l_ev_socket->_inheritor = NULL;
        dap_events_socket_delete_unsafe(l_ev_socket, true);
        return NULL;
    }
    
    l_client_http->worker = a_worker ? a_worker : dap_worker_get_current();
    if (!l_client_http->worker)
        l_client_http->worker = dap_worker_get_auto();
    
    l_client_http->is_over_ssl = a_over_ssl;

    // Resolve host
    if (0 > dap_net_resolve_host(a_uplink_addr, dap_itoa(a_uplink_port), false, &l_ev_socket->addr_storage, NULL)) {
        *a_error_code = EHOSTUNREACH;
        log_it(L_ERROR, "Wrong remote address '%s : %u'", a_uplink_addr, a_uplink_port);
        s_client_http_delete(l_client_http);
        l_ev_socket->_inheritor = NULL;
        dap_events_socket_delete_unsafe(l_ev_socket, true);
        return NULL;
    }

    dap_strncpy(l_ev_socket->remote_addr_str, a_uplink_addr, INET6_ADDRSTRLEN - 1);
    l_ev_socket->remote_port = a_uplink_port;

    // Setup socket for connection
    l_ev_socket->flags |= DAP_SOCK_CONNECTING;
    l_ev_socket->type = DESCRIPTOR_TYPE_SOCKET_CLIENT;
    
    if (a_over_ssl) {
#ifndef DAP_NET_CLIENT_NO_SSL
        l_ev_socket->callbacks.connected_callback = s_http_ssl_connected;
#else
        log_it(L_ERROR,"We have no SSL implementation but trying to create SSL connection!");
#endif
    }

    // Setup worker and attempt connection
#ifdef DAP_EVENTS_CAPS_IOCP
    log_it(L_DEBUG, "Connecting to %s:%u", a_uplink_addr, a_uplink_port);
    l_ev_socket->flags &= ~DAP_SOCK_READY_TO_READ;
    l_ev_socket->flags |= DAP_SOCK_READY_TO_WRITE;
    
    dap_events_socket_uuid_t *l_ev_uuid_ptr = DAP_DUP(&l_ev_socket->uuid);
    if (!l_ev_uuid_ptr) {
        *a_error_code = ENOMEM;
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        s_client_http_delete(l_client_http);
        l_ev_socket->_inheritor = NULL;
        dap_events_socket_delete_unsafe(l_ev_socket, true);
        return NULL;
    }
    
    dap_worker_add_events_socket(l_client_http->worker, l_ev_socket);
    l_client_http->timer = dap_timerfd_start_on_worker(l_client_http->worker, s_client_timeout_ms, 
                                                        s_timer_timeout_check, l_ev_uuid_ptr);
    if (!l_client_http->timer) {
        log_it(L_ERROR,"Can't run timer on worker %u for esocket uuid %"DAP_UINT64_FORMAT_U" for timeout check during connection attempt ",
               l_client_http->worker->id, *l_ev_uuid_ptr);
        DAP_DEL_Z(l_ev_uuid_ptr);
    }
    
    *a_error_code = 0;
    return l_client_http;
#else
    l_ev_socket->flags |= DAP_SOCK_READY_TO_WRITE;
    int l_err = connect(l_socket, (struct sockaddr *) &l_ev_socket->addr_storage, sizeof(struct sockaddr_in));
    
    if (l_err == 0){
        log_it(L_DEBUG, "Connected momentaly with %s:%u!", a_uplink_addr, a_uplink_port);
        dap_worker_add_events_socket(l_client_http->worker, l_ev_socket);
        if (a_over_ssl) {
#ifndef DAP_NET_CLIENT_NO_SSL
            s_http_ssl_connected(l_ev_socket);
#endif
        }
        *a_error_code = 0;
        return l_client_http;
    }
#ifdef DAP_OS_WINDOWS
    else if(l_err == SOCKET_ERROR) {
        int l_err2 = WSAGetLastError();
        if (l_err2 == WSAEWOULDBLOCK) {
            log_it(L_DEBUG, "Connecting to %s:%u", a_uplink_addr, a_uplink_port);
            dap_worker_add_events_socket(l_client_http->worker, l_ev_socket);
            
            dap_events_socket_uuid_t *l_ev_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
            if (!l_ev_uuid_ptr) {
                *a_error_code = ENOMEM;
                log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                s_client_http_delete(l_client_http);
                l_ev_socket->_inheritor = NULL;
                dap_events_socket_delete_unsafe(l_ev_socket, true);
                return NULL;
            }
            
            *l_ev_uuid_ptr = l_ev_socket->uuid;
            l_client_http->timer = dap_timerfd_start_on_worker(l_client_http->worker, s_client_timeout_ms, 
                                                                s_timer_timeout_check, l_ev_uuid_ptr);
            if (!l_client_http->timer) {
                log_it(L_ERROR,"Can't run timer on worker %u for esocket uuid %"DAP_UINT64_FORMAT_U" for timeout check during connection attempt ",
                       l_client_http->worker->id, *l_ev_uuid_ptr);
                DAP_DEL_Z(l_ev_uuid_ptr);
            }
            *a_error_code = 0;
            return l_client_http;
        } else {
            *a_error_code = l_err2;
            log_it(L_ERROR, "Socket %zu connecting error: %d", l_ev_socket->socket, l_err2);
            s_client_http_delete(l_client_http);
            l_ev_socket->_inheritor = NULL;
            dap_events_socket_delete_unsafe(l_ev_socket, true);
            return NULL;
        }
    }
#else
    else if(errno == EINPROGRESS && l_err == -1){
        log_it(L_DEBUG, "Connecting to %s:%u", a_uplink_addr, a_uplink_port);
        
        dap_events_socket_uuid_t *l_ev_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
        if (!l_ev_uuid_ptr) {
            *a_error_code = ENOMEM;
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            s_client_http_delete(l_client_http);
            l_ev_socket->_inheritor = NULL;
            dap_events_socket_delete_unsafe(l_ev_socket, true);
            return NULL;
        }
        
        *l_ev_uuid_ptr = l_ev_socket->uuid;
        l_client_http->timer = dap_timerfd_start_on_worker(l_client_http->worker, s_client_timeout_ms, 
                                                            s_timer_timeout_check, l_ev_uuid_ptr);
        if (!l_client_http->timer) {
            log_it(L_ERROR,"Can't run timer on worker %u for esocket uuid %"DAP_UINT64_FORMAT_U" for timeout check during connection attempt ",
                   l_client_http->worker->id, *l_ev_uuid_ptr);
            s_client_http_delete(l_client_http);
            l_ev_socket->_inheritor = NULL;
            dap_events_socket_delete_unsafe(l_ev_socket, true);
            *a_error_code = ENOMEM;
            return NULL;
        }
        
        dap_worker_add_events_socket(l_client_http->worker, l_ev_socket);
        *a_error_code = 0;
        return l_client_http;
    } else {
        *a_error_code = errno;
        log_it(L_ERROR, "Connecting error %d: \"%s\"", errno, dap_strerror(errno));
        s_client_http_delete(l_client_http);
        l_ev_socket->_inheritor = NULL;
        dap_events_socket_delete_unsafe(l_ev_socket, true);
        return NULL;
    }
#endif
#endif
    
    // Should not reach here
    *a_error_code = EINVAL;
    return NULL;
}

/**
 * @brief s_http_connected
 * @param a_esocket
 */
static void s_http_connected(dap_events_socket_t * a_esocket)
{
    assert(a_esocket);
    if (!a_esocket) {
        log_it(L_ERROR, "Invalid arguments in s_http_connected");
        return;
    }
    dap_client_http_t * l_client_http = DAP_CLIENT_HTTP(a_esocket);
    assert(l_client_http);
    assert(l_client_http->worker);
    if (!l_client_http || !l_client_http->worker) {
        log_it(L_ERROR, "Invalid arguments in s_http_connected");
        return;
    }

    log_it(L_INFO, "Remote address connected (%s:%u) with sock_id %"DAP_FORMAT_SOCKET, l_client_http->uplink_addr, l_client_http->uplink_port, a_esocket->socket);
    
    // Clean up existing timer to prevent leak
    if (l_client_http->timer) {
        DAP_DEL_Z(l_client_http->timer->callback_arg);
        dap_timerfd_delete_unsafe(l_client_http->timer);
        l_client_http->timer = NULL;
    }
    
    // add to dap_worker
    dap_events_socket_uuid_t * l_es_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
    if (!l_es_uuid_ptr) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return;
    }
    *l_es_uuid_ptr = a_esocket->uuid;
    l_client_http->timer = dap_timerfd_start_on_worker(l_client_http->worker, (unsigned long)s_client_timeout_read_after_connect_ms, s_timer_timeout_after_connected_check, l_es_uuid_ptr);
    if (!l_client_http->timer) {
        DAP_DELETE(l_es_uuid_ptr);
        log_it(L_ERROR, "Can't run timerfo after connection check on worker id %u", l_client_http->worker->id);
        return;
    }

    // Send HTTP request with properly formatted headers
    s_send_http_request(a_esocket, l_client_http);
}

/**
 * @brief s_timer_timeout_after_connected_check
 * @param a_arg
 * @return
 */
static bool s_timer_timeout_after_connected_check(void * a_arg)
{
    assert(a_arg);
    dap_events_socket_uuid_t * l_es_uuid_ptr = (dap_events_socket_uuid_t *) a_arg;

    dap_worker_t * l_worker = dap_worker_get_current(); // We're in own esocket context
    if (!l_worker) {
        log_it(L_ERROR, "l_woker is NULL");
        return false;
    }
    assert(l_worker);
    dap_events_socket_t * l_es = dap_context_find( l_worker->context, *l_es_uuid_ptr);
    if(l_es){
        dap_client_http_t * l_client_http = DAP_CLIENT_HTTP(l_es);
        assert(l_client_http);
        if ( time(NULL)- l_client_http->ts_last_read >= (time_t) s_client_timeout_read_after_connect_ms){
            log_it(L_WARNING, "Timeout for reading after connect for request http://%s:%u/%s, possible uplink is on heavy load or DPI between you",
                   l_client_http->uplink_addr, l_client_http->uplink_port, l_client_http->path ? l_client_http->path : "");
                   
            l_client_http->timer = NULL;
            
            dap_client_http_async_context_t *l_ctx = NULL;
            if (l_client_http->error_callback == s_async_error_callback) {
                l_ctx = (dap_client_http_async_context_t *)l_client_http->callbacks_arg;
                if (l_ctx && l_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED) {
                    log_it(L_WARNING, "Streaming timeout after %zu bytes received", l_ctx->streamed_body_size);
                }
            }
            
            if(l_client_http->error_callback) {
                l_client_http->error_callback(ETIMEDOUT, l_client_http->callbacks_arg);
                l_client_http->were_callbacks_called = true;
            }
            l_client_http->is_closed_by_timeout = true;
            log_it(L_INFO, "Close %s sock %"DAP_FORMAT_SOCKET" type %d by timeout",
                   l_es->remote_addr_str, l_es->socket, l_es->type);

            
            dap_events_socket_remove_and_delete_unsafe(l_es, true);
        } else
            return true;
    }else{
        if(s_debug_more)
            log_it(L_DEBUG,"Esocket %"DAP_UINT64_FORMAT_U" is finished, close check timer", *l_es_uuid_ptr);
    }

    DAP_DEL_Z(l_es_uuid_ptr);
    return false;
}

/**
 * @brief s_timer_timeout_check
 * @details Returns 'false' to prevent looping the checks
 * @param a_arg
 * @return
 */
static bool s_timer_timeout_check(void * a_arg)
{
    dap_events_socket_uuid_t *l_es_uuid = (dap_events_socket_uuid_t*) a_arg;
    assert(l_es_uuid);

    dap_worker_t * l_worker = dap_worker_get_current(); // We're in own esocket context
    if (!l_worker) {
        log_it(L_ERROR, "l_woker is NULL");
        return false;
    }
    assert(l_worker);
    dap_events_socket_t * l_es = dap_context_find(l_worker->context, *l_es_uuid);
    if(l_es){
        if (l_es->flags & DAP_SOCK_CONNECTING ){
            dap_client_http_t * l_client_http = DAP_CLIENT_HTTP(l_es);
            l_client_http->timer = NULL;
            log_it(L_WARNING,"Connecting timeout for request http://%s:%u/%s, possible network problems or host is down",
                   l_client_http->uplink_addr, l_client_http->uplink_port, l_client_http->path ? l_client_http->path : "");
            
            dap_client_http_async_context_t *l_ctx = NULL;
            if (l_client_http->error_callback == s_async_error_callback) {
                l_ctx = (dap_client_http_async_context_t *)l_client_http->callbacks_arg;
                if (l_ctx && l_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED) {
                    log_it(L_DEBUG, "Connection timeout for streaming request");
                }
            }
            
            if(l_client_http->error_callback) {
                l_client_http->error_callback(ETIMEDOUT, l_client_http->callbacks_arg);
                l_client_http->were_callbacks_called = true;
            }
            l_client_http->is_closed_by_timeout = true;
            log_it(L_INFO, "Close %s sock %"DAP_FORMAT_SOCKET" type %d by timeout",
                   l_es->remote_addr_str, l_es->socket, l_es->type);
            dap_events_socket_remove_and_delete_unsafe(l_es, true);
        }else
            if(s_debug_more)
                log_it(L_DEBUG,"Socket %"DAP_FORMAT_SOCKET" is connected, close check timer", l_es->socket);
    }else
        if(s_debug_more)
            log_it(L_DEBUG,"Esocket %"DAP_UINT64_FORMAT_U" is finished, close check timer", *l_es_uuid);

    DAP_DEL_Z(l_es_uuid);
    return false;
}

/**
 * @brief s_http_read
 * @param a_es
 * @param arg
 */
static void s_http_read(dap_events_socket_t * a_es, void * arg)
{
    UNUSED(arg);
    dap_client_http_t * l_client_http = DAP_CLIENT_HTTP(a_es);
    if(!l_client_http) {
        log_it(L_ERROR, "s_http_read: l_client_http is NULL!");
        return;
    }
    

#define m_http_error_exit(error_code, format, ...) do { \
    log_it(L_ERROR, "s_http_read: " format, ##__VA_ARGS__); \
    if(l_client_http->error_callback) { \
        l_client_http->error_callback(error_code, l_client_http->callbacks_arg); \
    } \
    l_client_http->were_callbacks_called = true; \
    a_es->flags |= DAP_SOCK_SIGNAL_CLOSE; \
    return; \
} while(0)
    
    // Additional safety checks
    if (!a_es->buf_in) {
        m_http_error_exit(EINVAL, "event socket buf_in is NULL for esocket "DAP_FORMAT_ESOCKET_UUID, a_es->uuid);
    }
    
    // Validate buffer consistency
    if (a_es->buf_in_size > a_es->buf_in_size_max) {
        m_http_error_exit(EFAULT, "event socket buffer corruption detected (size %zu > max %zu)", 
                          a_es->buf_in_size, a_es->buf_in_size_max);
    }
    
    if (!l_client_http->response) {
        m_http_error_exit(EINVAL, "HTTP client response buffer is NULL!");
    }
    
    // Validate HTTP client buffer consistency  
    if (l_client_http->response_size > l_client_http->response_size_max) {
        m_http_error_exit(EFAULT, "HTTP client buffer corruption detected (size %zu > max %zu)", 
                          l_client_http->response_size, l_client_http->response_size_max);
    }
    
    l_client_http->ts_last_read = time(NULL);
    
    // Get async context if available (for streaming and other features)
    dap_client_http_async_context_t *l_ctx = NULL;
    if (l_client_http->error_callback == s_async_error_callback) {
        l_ctx = (dap_client_http_async_context_t *)l_client_http->callbacks_arg;
    }
    
    // Check if we need to expand buffer
    size_t l_available_space = l_client_http->response_size_max - l_client_http->response_size;
    size_t l_available_in_socket = a_es->buf_in_size;
    
    if(l_available_space < l_available_in_socket) {
        // Check if we're in streaming mode with optimal buffer (should not need expansion)
        if (l_ctx && l_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED &&
            l_client_http->response_size_max >= DAP_CLIENT_HTTP_STREAMING_BUFFER_SIZE) {
            // Streaming buffer already at optimal size - no reallocation needed!
        } else {
            // Need to expand buffer
            size_t l_new_size;
            
            if (l_ctx && l_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED) {
                // Streaming mode - expand to optimal size (backup expansion, shouldn't normally happen)
                l_new_size = DAP_CLIENT_HTTP_STREAMING_BUFFER_SIZE;
                if(s_debug_more) {
                    log_it(L_DEBUG, "Late expansion to optimal streaming buffer: %zu bytes", l_new_size);
                }
            } else {
                // Standard accumulation mode OR streaming not yet determined - aggressive expansion
                l_new_size = l_client_http->response_size + l_available_in_socket + 4096;
            }
            
            // Apply size limits
            if(l_new_size > DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT) {
                if (l_ctx && l_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED) {
                    m_http_error_exit(DAP_CLIENT_HTTP_ERROR_STREAMING_SIZE_LIMIT, 
                                     "Streaming buffer size exceeds limit of %zu bytes (requested: %zu)", 
                                     (size_t)DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT, l_new_size);
                } else {
                    m_http_error_exit(-413, "Response size exceeds maximum allowed size of %zu bytes (requested: %zu)", 
                                     (size_t)DAP_CLIENT_HTTP_RESPONSE_SIZE_LIMIT, l_new_size);
                }
            }
            
            uint8_t *l_old_response = l_client_http->response;
            uint8_t *l_new_response = DAP_REALLOC(l_old_response, l_new_size);
            if(!l_new_response) {
                // Important: don't free old buffer here - it will be freed in m_http_error_exit
                m_http_error_exit(ENOMEM, "Can't expand response buffer from %zu to %zu bytes", 
                                  l_client_http->response_size_max, l_new_size);
            }
            
            l_client_http->response = l_new_response;
            l_client_http->response_size_max = l_new_size;
            if(s_debug_more) {
                log_it(L_DEBUG, "Expanded response buffer to %zu bytes (%s mode)", 
                       l_new_size, (l_ctx && l_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED) ? "streaming" : "accumulation");
            }
        }
    }
    
    // Additional bounds check before data copy
    size_t l_max_copy_size = l_client_http->response_size_max - l_client_http->response_size;
    if (l_max_copy_size == 0) {
        log_it(L_WARNING, "s_http_read: No space left in response buffer");
        return;
    }
    
    // read data using safe function with additional validation
    size_t l_read_bytes = dap_events_socket_pop_from_buf_in(a_es,
            l_client_http->response + l_client_http->response_size,
            l_max_copy_size);
    
    if (l_read_bytes == 0) {
        // No data was read, which might be normal or indicate an error
        // The pop function will have logged any errors
        return;
    }
    
    // Validate read operation result
    if (l_client_http->response_size + l_read_bytes > l_client_http->response_size_max) {
        m_http_error_exit(EOVERFLOW, "Buffer overflow detected! size %zu + read %zu > max %zu", 
                          l_client_http->response_size, l_read_bytes, l_client_http->response_size_max);
    }
    
    l_client_http->response_size += l_read_bytes;

    // search http header
    if(!l_client_http->is_header_read && l_client_http->response_size > 4 && !l_client_http->content_length) {
        char *l_crlf = strstr((char*)l_client_http->response, "\r\n\r\n");
        if (l_crlf) {
            l_client_http->header_length = l_crlf - (char*)l_client_http->response + 4;
            l_client_http->is_header_read = true;
            
            // Validate header size for potential streaming mode
            // Check progress_callback instead of streaming_mode since mode is determined later
            if (l_ctx && l_ctx->progress_callback && 
                l_client_http->header_length > DAP_CLIENT_HTTP_MAX_HEADERS_SIZE) {
                m_http_error_exit(-414, "HTTP headers too large for streaming mode: %zu bytes (max: %d)", 
                                 l_client_http->header_length, DAP_CLIENT_HTTP_MAX_HEADERS_SIZE);
            }
        }
    }
    
    // process http header
    if(l_client_http->is_header_read) {
        // Clear previous headers if any
        while(l_client_http->response_headers) {
            dap_http_header_remove(&l_client_http->response_headers, l_client_http->response_headers);
        }
        
        // Parse headers line by line
        const char *l_header_start = (const char*)l_client_http->response;
        const char *l_header_end = l_header_start + l_client_http->header_length - 4; // -4 to exclude final CRLF CRLF
        const char *l_line_start = l_header_start;
        
        // Skip first line (status line)
        const char *l_line_end = strstr(l_line_start, "\r\n");
        if(l_line_end) {
            l_line_start = l_line_end + 2;
        }
        
        // Parse each header line
        while(l_line_start < l_header_end) {
            l_line_end = strstr(l_line_start, "\r\n");
            if(!l_line_end || l_line_end > l_header_end) {
                break;
            }
            
            size_t l_line_len = l_line_end - l_line_start + 2; // Include CRLF
            s_parse_response_header(l_client_http, l_line_start, l_line_len);
            
            l_line_start = l_line_end + 2;
        }
        
        // Check for chunked transfer encoding first
        dap_http_header_t *l_transfer_encoding = dap_http_header_find(l_client_http->response_headers, "Transfer-Encoding");
        if (l_transfer_encoding && strstr(l_transfer_encoding->value, "chunked")) {
            l_client_http->is_chunked = true;
            l_client_http->is_reading_chunk_size = true;
            l_client_http->current_chunk_size = 0;
            l_client_http->current_chunk_read = 0;
            l_client_http->content_length = 0; // Not used in chunked mode
            log_it(L_DEBUG, "Chunked transfer encoding detected");
        } else {
            // Extract Content-Length from parsed headers
            dap_http_header_t *l_content_len_hdr = dap_http_header_find(l_client_http->response_headers, "Content-Length");
            if(l_content_len_hdr) {
                char *l_endptr = NULL;
                long l_content_len = strtol(l_content_len_hdr->value, &l_endptr, 10);
                if(l_endptr != l_content_len_hdr->value && *l_endptr == '\0' && l_content_len >= 0) {
                    l_client_http->content_length = (size_t)l_content_len;
                } else {
                    log_it(L_WARNING, "Invalid Content-Length header value: %s", l_content_len_hdr->value);
                    l_client_http->content_length = 0;
                }
            }
        }
        
        l_client_http->is_header_read = false;
        
        // Check for redirect
        http_status_code_t l_status_code = s_extract_http_code(l_client_http->response, l_client_http->response_size);
        if(l_status_code == Http_Status_MovedPermanently || l_status_code == Http_Status_Found || 
           l_status_code == Http_Status_TemporaryRedirect || l_status_code == Http_Status_PermanentRedirect) {
            
            // Check if redirects should be followed
            if(!l_client_http->follow_redirects) {
                log_it(L_INFO, "HTTP %d redirect detected but follow_redirects is disabled, returning response to user", l_status_code);
                
                // Call the callback with current response including redirect status
                if(l_client_http->response_callback_full) {
                    l_client_http->response_callback_full(
                            l_client_http->response + l_client_http->header_length,
                            l_client_http->response_size - l_client_http->header_length,
                            l_client_http->response_headers,
                            l_client_http->callbacks_arg, 
                            l_status_code);
                } else if(l_client_http->response_callback) {
                    l_client_http->response_callback(
                            l_client_http->response + l_client_http->header_length,
                            l_client_http->response_size - l_client_http->header_length,
                            l_client_http->callbacks_arg, 
                            l_status_code);
                }
                
                l_client_http->were_callbacks_called = true;
                a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
                return;
            }
            
            // Check redirect count
            if(l_client_http->redirect_count >= DAP_CLIENT_HTTP_MAX_REDIRECTS) {
                m_http_error_exit(-301, "Too many redirects (%d), stopping", l_client_http->redirect_count);
            }
            
            // Find Location header
            dap_http_header_t *l_location_hdr = dap_http_header_find(l_client_http->response_headers, "Location");
            if(l_location_hdr && l_location_hdr->value[0]) {
                log_it(L_INFO, "HTTP %d redirect to: %s", l_status_code, l_location_hdr->value);
                
                // Process redirect
                if (s_process_http_redirect(a_es, l_client_http, l_location_hdr->value)) {
                    return;
                } else {
                    // Failed to process redirect
                    m_http_error_exit(ENOMEM, "Failed to process redirect");
                }
            } else {
                m_http_error_exit(-302, "HTTP %d redirect but no Location header found", l_status_code);
            }
        }
        
        // SMART STREAMING LOGIC: Check multiple criteria for streaming decision
        // Fixed race conditions and consistency issues from original implementation
        if (l_ctx && l_ctx->progress_callback && l_ctx->streaming_mode == DAP_HTTP_STREAMING_UNDETERMINED) {
            bool l_size_trigger = false;
            bool l_mime_trigger = false;
            bool l_chunked_trigger = false;
            
            // Check by content size (only if Content-Length is known)
            if (l_client_http->content_length > 0 && 
                l_client_http->content_length > s_streaming_threshold) {
                l_size_trigger = true;
            }
            
            // For chunked encoding, always enable streaming if progress callback exists
            if (l_client_http->is_chunked) {
                l_chunked_trigger = true;
            }
            
            // Check by MIME type for binary/large content
            dap_http_header_t *l_content_type = dap_http_header_find(l_client_http->response_headers, "Content-Type");
            if (l_content_type) {
                const char *l_mime = l_content_type->value;
                if (strstr(l_mime, "application/octet-stream") ||
                    strstr(l_mime, "application/zip") ||
                    strstr(l_mime, "application/gzip") ||
                    strstr(l_mime, "application/pdf") ||
                    strstr(l_mime, "video/") ||
                    strstr(l_mime, "audio/") ||
                    strstr(l_mime, "image/")) {
                    l_mime_trigger = true;
                }
            }
            
            // Make streaming decision based on criteria
            bool l_should_stream = l_size_trigger || l_mime_trigger || l_chunked_trigger;
            l_ctx->streaming_mode = l_should_stream ? DAP_HTTP_STREAMING_ENABLED : DAP_HTTP_STREAMING_DISABLED;
            
            // If streaming enabled, expand buffer to optimal size NOW (one-time operation)
            if (l_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED && 
                l_client_http->response_size_max < DAP_CLIENT_HTTP_STREAMING_BUFFER_SIZE) {
                
                size_t l_new_size = DAP_CLIENT_HTTP_STREAMING_BUFFER_SIZE;
                uint8_t *l_old_response = l_client_http->response;
                uint8_t *l_new_response = DAP_REALLOC(l_old_response, l_new_size);
                if (l_new_response) {
                    l_client_http->response = l_new_response;
                    l_client_http->response_size_max = l_new_size;
                    log_it(L_DEBUG, "Streaming enabled for %s, expanded buffer to %zu bytes", 
                           l_client_http->uplink_addr, l_new_size);
                } else {
                    // On error old buffer remains valid - continue operation
                    log_it(L_WARNING, "Failed to expand buffer for streaming, continuing with %zu bytes", 
                           l_client_http->response_size_max);
                }
            }
        }
    }
    
    // process data
    if(l_client_http->content_length || l_client_http->is_chunked) {
        l_client_http->is_header_read = false;

        // Handle chunked transfer encoding
        if (l_client_http->is_chunked) {
            bool l_chunked_complete = s_process_chunked_data(l_client_http, l_ctx, a_es);
            if (l_chunked_complete) {
                // Chunked transfer finished
                if (!l_ctx || l_ctx->streaming_mode != DAP_HTTP_STREAMING_ENABLED) {
                    // Non-streaming mode - call response callback with accumulated data
                    size_t l_body_size = l_client_http->response_size - l_client_http->header_length;
                    http_status_code_t l_status_code = s_extract_http_code(l_client_http->response, l_client_http->response_size);
                    
                    if(l_client_http->response_callback_full) {
                        l_client_http->response_callback_full(
                                l_client_http->response + l_client_http->header_length,
                                l_body_size,
                                l_client_http->response_headers,
                                l_client_http->callbacks_arg, 
                                l_status_code);
                    } else if(l_client_http->response_callback) {
                        l_client_http->response_callback(
                                l_client_http->response + l_client_http->header_length,
                                l_body_size,
                                l_client_http->callbacks_arg, 
                                l_status_code);
                    }
                    l_client_http->were_callbacks_called = true;
                    a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
                }
                // For streaming mode, callbacks were already called in s_process_chunked_data
            }
            return; // Don't process further in chunked mode
        }

        // Handle streaming mode (non-chunked)
        if (l_ctx && l_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED && l_ctx->progress_callback) {
            // In streaming mode, send data directly to progress callback without accumulation
            size_t l_body_received = l_client_http->response_size - l_client_http->header_length;
            
            if (l_body_received > 0) {
                // Send current chunk directly from buffer to progress callback (no copying)
                void *l_body_data = l_client_http->response + l_client_http->header_length;
                l_ctx->progress_callback(
                    l_body_data,
                    l_body_received,
                    l_client_http->content_length,
                    l_ctx->user_arg
                );
                
                // Update total streamed size
                l_ctx->streamed_body_size += l_body_received;
                
                // Reset response buffer keeping only headers
                l_client_http->response_size = l_client_http->header_length;
            }
            
            // Check if all data received (either by Content-Length or when no more data expected)
            bool l_streaming_complete = false;
            if (l_client_http->content_length > 0) {
                // With known Content-Length
                l_streaming_complete = (l_ctx->streamed_body_size >= l_client_http->content_length);
            } else {
                // Without Content-Length - check for connection close or empty read
                l_streaming_complete = (l_body_received == 0 && a_es->buf_in_size == 0);
            }
            
            if (l_streaming_complete) {
                // Streaming complete - no need to call response callbacks
                l_client_http->were_callbacks_called = true;
                a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
            }
            return;
        }
        
        // Standard mode - accumulate data and wait for completion
        // received not enough data
        if ( l_client_http->content_length > l_client_http->response_size - l_client_http->header_length )
            return;
        // process data
        l_client_http->response[dap_min(l_client_http->response_size, l_client_http->response_size_max - 1)] = '\0';
        
        http_status_code_t l_status_code = s_extract_http_code(l_client_http->response, l_client_http->response_size);
        
        // Call appropriate callback
        if(l_client_http->response_callback_full) {
            // Call full callback with headers
            l_client_http->response_callback_full(
                    l_client_http->response + l_client_http->header_length,
                    l_client_http->content_length,
                    l_client_http->response_headers,
                    l_client_http->callbacks_arg, 
                    l_status_code);
        } else if(l_client_http->response_callback) {
            // Call simple callback without headers
            l_client_http->response_callback(
                    l_client_http->response + l_client_http->header_length,
                    l_client_http->content_length,
                    l_client_http->callbacks_arg, 
                    l_status_code);
        }
        
        // In standard mode all data processed, just reset state
        l_client_http->response_size = 0;
        l_client_http->header_length = 0;
        l_client_http->content_length = 0;
        l_client_http->were_callbacks_called = true;
        a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
    }

#undef m_http_error_exit
}


/**
 * @brief s_http_error - Enhanced error handler with streaming support
 * @param a_es Event socket
 * @param a_errno Error code
 */
static void s_http_error(dap_events_socket_t * a_es, int a_errno)
{
    if (!a_es)
        return log_it(L_ERROR, "s_http_error: es is null!");

    log_it( L_WARNING, "Socket %"DAP_FORMAT_SOCKET" %serror %d: %s",
                        a_es->socket, a_es->flags & DAP_SOCK_CONNECTING ? "connecting " : "",
                        a_errno, dap_strerror(a_errno) );

    dap_client_http_t * l_client_http = DAP_CLIENT_HTTP(a_es);

    if(!l_client_http) {
        log_it(L_ERROR, "s_http_error: l_client_http is NULL!");
        return;
    }
    
    dap_client_http_async_context_t *l_ctx = NULL;
    if (l_client_http->error_callback == s_async_error_callback) {
        l_ctx = (dap_client_http_async_context_t *)l_client_http->callbacks_arg;
    }
    
    if (l_ctx && l_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED) {
        // Special handling for interrupted streaming
        log_it(L_WARNING, "Streaming interrupted after %zu bytes (%s mode: %s)",
               l_ctx->streamed_body_size,
               l_client_http->is_chunked ? "chunked" : "content-length",
               l_client_http->is_chunked ? "unknown total" : 
               (l_client_http->content_length > 0 ? "known total" : "unknown total"));
        
        // Calculate completion percentage if possible
        if (!l_client_http->is_chunked && l_client_http->content_length > 0) {
            double l_completion = (double)l_ctx->streamed_body_size * 100.0 / l_client_http->content_length;
            log_it(L_INFO, "Streaming completion: %.1f%% (%zu of %zu bytes)",
                   l_completion, l_ctx->streamed_body_size, l_client_http->content_length);
        }
        
        if(l_client_http->error_callback) {
            int l_error_code = (a_errno == ETIMEDOUT) ? DAP_CLIENT_HTTP_ERROR_STREAMING_TIMEOUT : 
                                                        DAP_CLIENT_HTTP_ERROR_STREAMING_INTERRUPTED;
            l_client_http->error_callback(l_error_code, l_client_http->callbacks_arg);
        }
    } else if (l_ctx && l_ctx->streaming_mode == DAP_HTTP_STREAMING_UNDETERMINED) {
        log_it(L_DEBUG, "Error occurred before streaming mode was determined");
        // Call error callback with original error code
        if(l_client_http->error_callback)
            l_client_http->error_callback(a_errno, l_client_http->callbacks_arg);
    } else {
        log_it(L_DEBUG, "Error in accumulation mode (no streaming active)");
        // Call error callback with original error code
        if(l_client_http->error_callback)
            l_client_http->error_callback(a_errno, l_client_http->callbacks_arg);
    }

    l_client_http->were_callbacks_called = true;

    // close connection.
    a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

/**
 * @brief s_es_delete
 * @param a_es
 */
static void s_es_delete(dap_events_socket_t * a_es, void * a_arg)
{
    if (a_es == NULL) {
        log_it(L_ERROR,"Esocket is NULL for s_es_delete");
        return;
    }
    (void) a_arg;
    dap_client_http_t * l_client_http = DAP_CLIENT_HTTP(a_es);
    if(l_client_http == NULL){
        log_it(L_WARNING, "For some reasons internal object is NULL");
        return;
    }
    dap_client_http_async_context_t *l_ctx = NULL;
    if (l_client_http->error_callback == s_async_error_callback) {
        l_ctx = (dap_client_http_async_context_t *)l_client_http->callbacks_arg;
    }
    
    if (! l_client_http->were_callbacks_called){
        // Enhanced disconnect handling with streaming awareness
        
        size_t l_response_size = l_client_http->response_size> l_client_http->header_length ?
                    l_client_http->response_size - l_client_http->header_length: 0;
        
        if (l_client_http->content_length){
            if (l_ctx && l_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED) {
                log_it(L_WARNING, "Streaming disconnected: received %zu bytes, expected %zu total, streamed %zu",
                       l_response_size, l_client_http->content_length, l_ctx->streamed_body_size);
            } else {
                log_it(L_WARNING, "Remote server disconnected before he sends all data: %zd data in buffer when expected %zd",
                       l_client_http->response_size, l_client_http->content_length);
            }
            l_client_http->error_callback(-6, l_client_http->callbacks_arg); // -6 means remote server disconnected before he sends all
        }else if (l_response_size){
            log_it(L_INFO, "Remote server replied without no content length but we have the response %zd bytes size",
               l_response_size);

            //l_client_http->error_callback(-10 , l_client_http->callbacks_arg);
            http_status_code_t l_status_code = s_extract_http_code(l_client_http->response, l_client_http->response_size);
            
            // Call appropriate callback
            if (l_ctx) {
                // For async context, check streaming mode
                if (l_ctx->streaming_mode == DAP_HTTP_STREAMING_ENABLED) {
                    // In streaming mode, this is unexpected data - call async error callback
                    log_it(L_WARNING, "Streaming disconnected with unexpected response data: %zu bytes", l_response_size);
                    s_async_error_callback(-10, l_client_http->callbacks_arg);
                } else {
                    // For non-streaming async context, use the wrapper callback that will free the context
                    s_async_response_callback(
                            l_client_http->response + l_client_http->header_length,
                            l_response_size,
                            l_client_http->response_headers,
                            l_client_http->callbacks_arg, 
                            l_status_code);
                }
            } else if(l_client_http->response_callback_full) {
                // Call full callback with headers (non-async context)
                l_client_http->response_callback_full(
                        l_client_http->response + l_client_http->header_length,
                        l_response_size,
                        l_client_http->response_headers,
                        l_client_http->callbacks_arg, 
                        l_status_code);
            } else if(l_client_http->response_callback) {
                // Call simple callback without headers (non-async context)
                l_client_http->response_callback(
                        l_client_http->response + l_client_http->header_length,
                        l_response_size,
                        l_client_http->callbacks_arg, 
                        l_status_code);
            }
            
            l_client_http->were_callbacks_called = true;
        }else if (l_client_http->response_size){
            log_it(L_INFO, "Remote server disconnected with reply. Body is empty, only headers are in");
            l_client_http->error_callback(-7 , l_client_http->callbacks_arg); // -667 means remote server replied only with headers
            l_client_http->were_callbacks_called = true;
        }else{
            log_it(L_WARNING, "Remote server disconnected without reply");
            l_client_http->error_callback(-8, l_client_http->callbacks_arg); // -668 means remote server disconnected before he sends anythinh
            l_client_http->were_callbacks_called = true;
        }
    }

#ifndef DAP_NET_CLIENT_NO_SSL
    WOLFSSL *l_ssl = SSL(a_es);
    if (l_ssl) {
        wolfSSL_free(l_ssl);
        a_es->_pvt = NULL;
    }
#endif

    s_client_http_delete(l_client_http);
    a_es->_inheritor = NULL;
}

/**
 * @brief s_client_http_delete
 * @param a_client_http
 */
static void s_client_http_delete(dap_client_http_t * a_client_http)
{
    dap_return_if_fail(a_client_http);
    debug_if(s_debug_more, L_DEBUG, "HTTP client delete");
    if (a_client_http->timer) {
        DAP_DEL_Z(a_client_http->timer->callback_arg);
        dap_timerfd_delete_unsafe(a_client_http->timer);
    }
    
    // Clean up response headers
    while(a_client_http->response_headers) {
        dap_http_header_remove(&a_client_http->response_headers, a_client_http->response_headers);
    }
    
    DAP_DEL_MULTY(a_client_http->method, a_client_http->path, a_client_http->request_content_type, a_client_http->cookie, a_client_http->request, a_client_http->request_custom_headers, a_client_http->response, a_client_http);
}


/**
 * @brief dap_client_http_request_custom
 * @param a_worker
 * @param a_uplink_addr
 * @param a_uplink_port
 * @param a_method GET or POST
 * @param a_request_content_type like "text/text"
 * @param a_path
 * @param a_request
 * @param a_request_size
 * @param a_cookie
 * @param a_response_callback
 * @param a_error_callback
 * @param a_callbacks_arg
 * @param a_custom_headers
 * @param a_over_ssl
 */
dap_client_http_t * dap_client_http_request_custom (
                            dap_worker_t * a_worker,
                            const char *a_uplink_addr,
                            uint16_t a_uplink_port,
                            const char *a_method,
                            const char *a_request_content_type,
                            const char * a_path,
                            const void *a_request,
                            size_t a_request_size,
                            char *a_cookie,
                            dap_client_http_callback_data_t a_response_callback,
                            dap_client_http_callback_error_t a_error_callback,
                            void *a_callbacks_arg,
                            char *a_custom_headers,
                            bool a_over_ssl)
{
    //log_it(L_DEBUG, "HTTP request on url '%s:%d'", a_uplink_addr, a_uplink_port);
    
    int l_error_code = 0;
    dap_client_http_t *l_client_http = s_client_http_create_and_connect(
        a_worker, a_uplink_addr, a_uplink_port, a_method,
        a_request_content_type, a_path, a_request, a_request_size,
        a_cookie, a_custom_headers, a_over_ssl, a_error_callback,
        a_response_callback, NULL, a_callbacks_arg, 0, false, &l_error_code
    );
    
    if (!l_client_http) {
        if (a_error_callback)
            a_error_callback(l_error_code, a_callbacks_arg);
        return NULL;
    }
    
    // Callbacks already set in s_client_http_create_and_connect
    return l_client_http;
}

#ifndef DAP_NET_CLIENT_NO_SSL
static void s_http_ssl_connected(dap_events_socket_t * a_esocket)
{
    assert(a_esocket);
    dap_client_http_t * l_client_http = DAP_CLIENT_HTTP(a_esocket);
    assert(l_client_http);
    dap_worker_t *l_worker = l_client_http->worker;
    assert(l_worker);

    WOLFSSL *l_ssl = wolfSSL_new(s_ctx);
    if (!l_ssl)
        log_it(L_ERROR, "wolfSSL_new error");
    wolfSSL_set_fd(l_ssl, a_esocket->socket);
    a_esocket->_pvt = (void *)l_ssl;
    a_esocket->type = DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL;
    a_esocket->flags |= DAP_SOCK_CONNECTING;
    a_esocket->flags |= DAP_SOCK_READY_TO_WRITE;
    a_esocket->callbacks.connected_callback = s_http_connected;
    // Clean up existing timer to prevent leak (similar to s_http_connected)
    if (l_client_http->timer) {
        DAP_DEL_Z(l_client_http->timer->callback_arg);
        dap_timerfd_delete_unsafe(l_client_http->timer);
        l_client_http->timer = NULL;
    }
    
    dap_events_socket_handle_t * l_ev_socket_handler = DAP_NEW_Z(dap_events_socket_handle_t);
    l_ev_socket_handler->esocket = a_esocket;
    l_ev_socket_handler->esocket_uuid = a_esocket->uuid;
    l_client_http->timer = dap_timerfd_start_on_worker(l_client_http->worker,s_client_timeout_ms, s_timer_timeout_check, l_ev_socket_handler);
}
#endif

/**
 * @brief dap_client_http_request
 * @param a_worker
 * @param a_uplink_addr
 * @param a_uplink_port
 * @param a_method GET or POST
 * @param a_request_content_type like "text/text"
 * @param a_path
 * @param a_request
 * @param a_request_size
 * @param a_cookie
 * @param a_response_callback
 * @param a_error_callback
 * @param a_callbacks_arg
 * @param a_custom_headers
 */
dap_client_http_t *dap_client_http_request(dap_worker_t * a_worker,const char *a_uplink_addr, uint16_t a_uplink_port, const char * a_method,
        const char* a_request_content_type, const char * a_path, const void *a_request, size_t a_request_size,
        char * a_cookie, dap_client_http_callback_data_t a_response_callback,
        dap_client_http_callback_error_t a_error_callback, void *a_callbacks_arg, char *a_custom_headers)
{
    return dap_client_http_request_custom(a_worker, a_uplink_addr, a_uplink_port, a_method, a_request_content_type, a_path,
            a_request, a_request_size, a_cookie, a_response_callback, a_error_callback, a_callbacks_arg,
            a_custom_headers, false);
}

/**
 * @brief dap_client_http_request_full - Make HTTP request with full callback including headers
 * @param a_worker
 * @param a_uplink_addr
 * @param a_uplink_port
 * @param a_method GET or POST
 * @param a_request_content_type like "text/text"
 * @param a_path
 * @param a_request
 * @param a_request_size
 * @param a_cookie
 * @param a_response_callback Full callback that receives headers
 * @param a_error_callback
 * @param a_callbacks_arg
 * @param a_custom_headers
 */
dap_client_http_t *dap_client_http_request_full(dap_worker_t * a_worker,const char *a_uplink_addr, uint16_t a_uplink_port, const char * a_method,
        const char* a_request_content_type, const char * a_path, const void *a_request, size_t a_request_size,
        char * a_cookie, dap_client_http_callback_full_t a_response_callback,
        dap_client_http_callback_error_t a_error_callback, void *a_callbacks_arg, char *a_custom_headers,
        bool a_follow_redirects)
{
    int l_error_code = 0;
    dap_client_http_t *l_client_http = s_client_http_create_and_connect(
        a_worker, a_uplink_addr, a_uplink_port, a_method,
        a_request_content_type, a_path, a_request, a_request_size,
        a_cookie, a_custom_headers, false, // not SSL
        a_error_callback,
        NULL, // No simple callback
        a_response_callback, // Full callback
        a_callbacks_arg, 
        0, // redirect count
        a_follow_redirects,
        &l_error_code
    );
    
    if (!l_client_http) {
        if (a_error_callback)
            a_error_callback(l_error_code, a_callbacks_arg);
        return NULL;
    }
    
    return l_client_http;
}

void dap_client_http_close_unsafe(dap_client_http_t *a_client_http)
{
    if (a_client_http->es) {
        a_client_http->es->callbacks.delete_callback = NULL;
        dap_events_socket_remove_and_delete_unsafe(a_client_http->es, true);
    }
    s_client_http_delete(a_client_http);
}

/**
 * @brief Unified response callback for async API
 */
static void s_async_response_callback(void *a_body, size_t a_body_size,
                                     struct dap_http_header *a_headers,
                                     void *a_arg, http_status_code_t a_status_code)
{
    dap_client_http_async_context_t *l_ctx = (dap_client_http_async_context_t *)a_arg;
    if(!l_ctx)
        return;
        
    // Call appropriate user callback
    if(l_ctx->response_callback) {
        l_ctx->response_callback(a_body, a_body_size, a_headers, l_ctx->user_arg, a_status_code);
    } else if(l_ctx->simple_response_callback) {
        l_ctx->simple_response_callback(a_body, a_body_size, l_ctx->user_arg, a_status_code);
    }
    
    // Free the context
    DAP_DELETE(l_ctx);
}

/**
 * @brief Unified error callback for async API
 */
static void s_async_error_callback(int a_error_code, void *a_arg)
{
    dap_client_http_async_context_t *l_ctx = (dap_client_http_async_context_t *)a_arg;
    if(!l_ctx)
        return;
        
    // Call user's error callback
    if(l_ctx->error_callback) {
        l_ctx->error_callback(a_error_code, l_ctx->user_arg);
    }
    
    // Free the context
    DAP_DELETE(l_ctx);
}

/**
 * @brief Internal async request implementation
 */
static void s_client_http_request_async_impl(
        dap_worker_t * a_worker,
        const char *a_uplink_addr, 
        uint16_t a_uplink_port, 
        const char * a_method,
        const char* a_request_content_type, 
        const char * a_path, 
        const void *a_request, 
        size_t a_request_size,
        char * a_cookie, 
        dap_client_http_async_context_t *a_ctx,
        char *a_custom_headers,
        bool a_is_https,
        bool a_follow_redirects)
{
    // Call started callback if provided BEFORE creating connection
    if(a_ctx->started_callback) {
        a_ctx->started_callback(a_ctx->user_arg);
    }
    
    int l_error_code = 0;
    dap_client_http_t *l_client_http = s_client_http_create_and_connect(
        a_worker, a_uplink_addr, a_uplink_port, a_method,
        a_request_content_type, a_path, a_request, a_request_size,
        a_cookie, a_custom_headers, a_is_https, 
        s_async_error_callback,           // Use async error callback
        a_ctx->simple_response_callback,  // Simple response callback
        s_async_response_callback,        // Full response callback  
        a_ctx,                           // Pass context as callback arg
        a_ctx->redirect_count,           // Pass redirect count
        a_follow_redirects,
        &l_error_code
    );
    
    if (!l_client_http) {
        if (a_ctx->error_callback)
            a_ctx->error_callback(l_error_code, a_ctx->user_arg);
        DAP_DELETE(a_ctx);
        return;
    }
    
    // DO NOT access l_client_http after this point - it's in another thread!
}

/**
 * @brief dap_client_http_request_async - Fully async HTTP request
 * No return value - all interaction through callbacks
 */
void dap_client_http_request_async(
        dap_worker_t * a_worker,
        const char *a_uplink_addr, 
        uint16_t a_uplink_port, 
        const char * a_method,
        const char* a_request_content_type, 
        const char * a_path, 
        const void *a_request, 
        size_t a_request_size,
        char * a_cookie, 
        dap_client_http_callback_full_t a_response_callback,
        dap_client_http_callback_error_t a_error_callback,
        dap_client_http_callback_started_t a_started_callback,
        dap_client_http_callback_progress_t a_progress_callback,
        void *a_callbacks_arg, 
        char *a_custom_headers,
        bool a_follow_redirects)
{
    // Create async context
    dap_client_http_async_context_t *l_ctx = DAP_NEW_Z(dap_client_http_async_context_t);
    if(!l_ctx) {
        log_it(L_CRITICAL, "Can't allocate async context");
        if(a_error_callback) {
            a_error_callback(ENOMEM, a_callbacks_arg);
        }
        return;
    }
    
    l_ctx->response_callback = a_response_callback;
    l_ctx->error_callback = a_error_callback;
    l_ctx->started_callback = a_started_callback;
    l_ctx->progress_callback = a_progress_callback;
    l_ctx->user_arg = a_callbacks_arg;
    
    // Call internal implementation
    s_client_http_request_async_impl(
        a_worker, a_uplink_addr, a_uplink_port, a_method,
        a_request_content_type, a_path, a_request, a_request_size,
        a_cookie, l_ctx, a_custom_headers, false, // false for non-SSL
        a_follow_redirects
    );
}

/**
 * @brief dap_client_http_request_simple_async - Simplified async request
 * Without progress/started callbacks
 */
void dap_client_http_request_simple_async(
        dap_worker_t * a_worker,
        const char *a_uplink_addr, 
        uint16_t a_uplink_port, 
        const char * a_method,
        const char* a_request_content_type, 
        const char * a_path, 
        const void *a_request, 
        size_t a_request_size,
        char * a_cookie, 
        dap_client_http_callback_full_t a_response_callback,
        dap_client_http_callback_error_t a_error_callback,
        void *a_callbacks_arg, 
        char *a_custom_headers,
        bool a_follow_redirects)
{
    dap_client_http_request_async(
        a_worker, a_uplink_addr, a_uplink_port, a_method,
        a_request_content_type, a_path, a_request, a_request_size,
        a_cookie, a_response_callback, a_error_callback,
        NULL, NULL, // No started/progress callbacks
        a_callbacks_arg, a_custom_headers, a_follow_redirects
    );
}

int dap_client_http_set_params(uint64_t a_timeout_ms, uint64_t a_timeout_read_after_connect_ms, size_t a_streaming_threshold_bytes) {
    if ( s_client_timeout_ms != 0 )
        return log_it(L_ERROR, "HTTP client parameters are already set"), -1;
    s_client_timeout_ms = a_timeout_ms;
    s_client_timeout_read_after_connect_ms = a_timeout_read_after_connect_ms;
    s_streaming_threshold = a_streaming_threshold_bytes;
    return 0;
}

/**
 * @brief dap_client_http_init
 * @return
 */
int dap_client_http_init()
{
    s_debug_more = dap_config_get_item_bool_default(g_config,"dap_client","debug_more",false);
    s_max_attempts = dap_config_get_item_uint32_default(g_config,"dap_client","max_tries",5);
    if ( s_client_timeout_ms == 0 ) {
        s_client_timeout_ms = dap_config_get_item_uint32_default(g_config, "dap_client", "timeout", 20) * 1000;
        s_client_timeout_read_after_connect_ms = (time_t) dap_config_get_item_uint64_default(g_config, "dap_client", "timeout_read_after_connect", 5) * 1000;
        s_streaming_threshold = dap_config_get_item_uint32_default(g_config, "dap_client", "streaming_threshold", DAP_CLIENT_HTTP_STREAMING_THRESHOLD_DEFAULT);
    }
#ifndef DAP_NET_CLIENT_NO_SSL
    wolfSSL_Init();
    wolfSSL_Debugging_ON ();
    if ((s_ctx = wolfSSL_CTX_new(wolfTLSv1_2_client_method())) == NULL)
        return -1;
    const char *l_ssl_cert_path = dap_config_get_item_str(g_config, "dap_client", "ssl_cert_path");
    if (l_ssl_cert_path) {
        if (wolfSSL_CTX_load_verify_locations(s_ctx, l_ssl_cert_path, 0) != SSL_SUCCESS)
        return -2;
    } else
        wolfSSL_CTX_set_verify(s_ctx, WOLFSSL_VERIFY_NONE, 0);
    if (wolfSSL_CTX_UseSupportedCurve(s_ctx, WOLFSSL_ECC_SECP256R1) != SSL_SUCCESS) {
        log_it(L_ERROR, "WolfSSL UseSupportedCurve() handle error");
    }
    wolfSSL_CTX_UseSupportedCurve(s_ctx, WOLFSSL_ECC_SECP256R1);
    wolfSSL_CTX_UseSupportedCurve(s_ctx, WOLFSSL_ECC_SECP384R1);
    wolfSSL_CTX_UseSupportedCurve(s_ctx, WOLFSSL_ECC_SECP521R1);
    wolfSSL_CTX_UseSupportedCurve(s_ctx, WOLFSSL_ECC_X25519);
    wolfSSL_CTX_UseSupportedCurve(s_ctx, WOLFSSL_ECC_X448);

    if (s_debug_more) {
        const int l_ciphers_len = 2048;
        char l_buf[l_ciphers_len];
        wolfSSL_get_ciphers(l_buf, l_ciphers_len);
        log_it(L_DEBUG, "WolfSSL cipher list is :\n%s", l_buf);
    }
#endif
    return 0;
}

/**
 * @brief dap_client_http_deinit
 */
void dap_client_http_deinit()
{
#ifndef DAP_NET_CLIENT_NO_SSL
    wolfSSL_CTX_free(s_ctx);
    wolfSSL_Cleanup();
#endif
}


/**
 * @brief dap_client_http_get_connect_timeout_ms
 * @return
 */
uint64_t dap_client_http_get_connect_timeout_ms()
{
    return s_client_timeout_ms;
}

/**
 * @brief dap_client_http_get_read_after_connect_timeout_ms
 * @return
 */
uint64_t dap_client_http_get_read_after_connect_timeout_ms()
{
    return s_client_timeout_read_after_connect_ms;
}