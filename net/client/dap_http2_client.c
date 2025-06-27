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

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_http2_client.h"
#include "dap_http2_session.h"
#include "dap_http2_stream.h"

#define LOG_TAG "dap_http2_client"

// === EFFICIENT HTTP METHOD IMPLEMENTATION ===

// Fast method-to-string conversion table (O(1) access)
const char* const DAP_HTTP_METHOD_STRINGS[DAP_HTTP_METHOD_COUNT] = {
    [DAP_HTTP_METHOD_GET]     = "GET",
    [DAP_HTTP_METHOD_POST]    = "POST", 
    [DAP_HTTP_METHOD_PUT]     = "PUT",
    [DAP_HTTP_METHOD_DELETE]  = "DELETE",
    [DAP_HTTP_METHOD_HEAD]    = "HEAD",
    [DAP_HTTP_METHOD_OPTIONS] = "OPTIONS",
    [DAP_HTTP_METHOD_PATCH]   = "PATCH",
    [DAP_HTTP_METHOD_CONNECT] = "CONNECT",
    [DAP_HTTP_METHOD_TRACE]   = "TRACE"
};

/**
 * @brief Parse string to HTTP method enum (optimized with early exit)
 * @param a_method_str Method string
 * @return Method enum or DAP_HTTP_METHOD_COUNT if invalid
 */
dap_http_method_t dap_http_method_from_string(const char *a_method_str)
{
    if (!a_method_str) {
        return DAP_HTTP_METHOD_COUNT;
    }
    
    // Optimized: check first character for early filtering
    switch (a_method_str[0]) {
        case 'G':
            if (strcmp(a_method_str, "GET") == 0) return DAP_HTTP_METHOD_GET;
            break;
        case 'P':
            if (strcmp(a_method_str, "POST") == 0) return DAP_HTTP_METHOD_POST;
            if (strcmp(a_method_str, "PUT") == 0) return DAP_HTTP_METHOD_PUT;
            if (strcmp(a_method_str, "PATCH") == 0) return DAP_HTTP_METHOD_PATCH;
            break;
        case 'D':
            if (strcmp(a_method_str, "DELETE") == 0) return DAP_HTTP_METHOD_DELETE;
            break;
        case 'H':
            if (strcmp(a_method_str, "HEAD") == 0) return DAP_HTTP_METHOD_HEAD;
            break;
        case 'O':
            if (strcmp(a_method_str, "OPTIONS") == 0) return DAP_HTTP_METHOD_OPTIONS;
            break;
        case 'C':
            if (strcmp(a_method_str, "CONNECT") == 0) return DAP_HTTP_METHOD_CONNECT;
            break;
        case 'T':
            if (strcmp(a_method_str, "TRACE") == 0) return DAP_HTTP_METHOD_TRACE;
            break;
    }
    
    return DAP_HTTP_METHOD_COUNT; // Invalid method
}

// Default configuration values
#define DEFAULT_CONNECT_TIMEOUT_MS    20000  // 20 seconds
#define DEFAULT_READ_TIMEOUT_MS       5000   // 5 seconds
#define DEFAULT_MAX_RESPONSE_SIZE     (10 * 1024 * 1024)  // 10MB
#define DEFAULT_MAX_REDIRECTS         5

// Debug flag
static bool s_debug_more = false;

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
    DAP_DEL_MULTY(a_client->config.default_user_agent,
                  a_client->config.default_accept,
                  a_client->config.ssl_cert_path,
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
        
        // Headers (will be allocated when set)
        .default_user_agent = dap_strdup("DAP-HTTP2-Client/1.0"),
        .default_accept = dap_strdup("*/*"),
        
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
    DAP_DEL_MULTY(a_client->config.default_user_agent,
                  a_client->config.default_accept,
                  a_client->config.ssl_cert_path,
                  a_client->config.ssl_key_path,
                  a_client->config.ssl_ca_path);
    
    // Copy configuration
    a_client->config = *a_config;
    
    // Duplicate strings
    a_client->config.default_user_agent = dap_strdup(a_config->default_user_agent);
    a_client->config.default_accept = dap_strdup(a_config->default_accept);
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
        
        // TODO: Cancel session/stream через UID routing
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
    
    // TODO: Close session через UID routing
    // uint64_t l_stream_uid = atomic_load(&a_client->stream_uid);
    // if (l_stream_uid != INVALID_STREAM_UID) {
    //     dap_http2_session_close_by_stream_uid(l_stream_uid);
    // }
    
    atomic_store(&a_client->stream_uid, INVALID_STREAM_UID);
    atomic_store(&a_client->state, DAP_HTTP2_CLIENT_STATE_IDLE);
    
    log_it(L_DEBUG, "HTTP2 client connection closed");
}

// === UTILITY FUNCTIONS ===

/**
 * @brief Get client state string representation
 * @param a_state Client state
 * @return State name string
 */
const char* dap_http2_client_state_to_str(dap_http2_client_state_t a_state)
{
    switch (a_state) {
        case DAP_HTTP2_CLIENT_STATE_IDLE:       return "IDLE";
        case DAP_HTTP2_CLIENT_STATE_REQUESTING: return "REQUESTING";
        case DAP_HTTP2_CLIENT_STATE_RECEIVING:  return "RECEIVING";
        case DAP_HTTP2_CLIENT_STATE_COMPLETE:   return "COMPLETE";
        case DAP_HTTP2_CLIENT_STATE_ERROR:      return "ERROR";
        case DAP_HTTP2_CLIENT_STATE_CANCELLED:  return "CANCELLED";
        default:                                return "UNKNOWN";
    }
}

/**
 * @brief Get client error string representation
 * @param a_error Client error
 * @return Error name string
 */
const char* dap_http2_client_error_to_str(dap_http2_client_error_t a_error)
{
    switch (a_error) {
        case DAP_HTTP2_CLIENT_ERROR_NONE:               return "NONE";
        case DAP_HTTP2_CLIENT_ERROR_INVALID_URL:        return "INVALID_URL";
        case DAP_HTTP2_CLIENT_ERROR_INVALID_METHOD:     return "INVALID_METHOD";
        case DAP_HTTP2_CLIENT_ERROR_CONNECTION_FAILED:  return "CONNECTION_FAILED";
        case DAP_HTTP2_CLIENT_ERROR_TIMEOUT:            return "TIMEOUT";
        case DAP_HTTP2_CLIENT_ERROR_CANCELLED:          return "CANCELLED";
        case DAP_HTTP2_CLIENT_ERROR_INTERNAL:           return "INTERNAL";
        default:                                        return "UNKNOWN";
    }
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
    l_request->method = DAP_HTTP_METHOD_GET;  // Default to GET (enum, no allocation)
    l_request->url = NULL;
    l_request->host = NULL;
    l_request->port = 80;  // Default HTTP port
    l_request->use_ssl = false;
    l_request->content_type = NULL;
    l_request->custom_headers = NULL;
    l_request->body_data = NULL;
    l_request->body_size = 0;

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
    DAP_DEL_MULTY(a_request->url,
                  a_request->host,
                  a_request->content_type,
                  a_request->custom_headers,
                  a_request->body_data);

    DAP_DELETE(a_request);
}

/**
 * @brief EFFICIENT URL parser - accepts NULL for unneeded parameters
 * @param a_url URL to parse
 * @param a_host Output: host (allocated) - can be NULL
 * @param a_port Output: port - can be NULL
 * @param a_path Output: path (allocated) - can be NULL
 * @param a_use_ssl Output: SSL flag - can be NULL
 * @return 0 on success, negative on error
 */
static int s_parse_url(const char *a_url, char **a_host, uint16_t *a_port, char **a_path, bool *a_use_ssl)
{
    if (!a_url) {
        log_it(L_ERROR, "URL is NULL in s_parse_url");
        return -1;
    }

    // EFFICIENT: Calculate length once and set end pointer
    size_t l_url_len = strlen(a_url);
    const char *l_url_end = a_url + l_url_len;

    // Default values
    uint16_t l_default_port = 80;
    bool l_is_ssl = false;

    // Check protocol (readable and efficient)
    const char *l_url_start;
    if (l_url_len >= 7 && strncasecmp(a_url, "http://", 7) == 0) {
        l_url_start = a_url + 7;
        l_default_port = 80;
        l_is_ssl = false;
    } else if (l_url_len >= 8 && strncasecmp(a_url, "https://", 8) == 0) {
        l_url_start = a_url + 8;
        l_default_port = 443;
        l_is_ssl = true;
    } else {
        log_it(L_ERROR, "URL must start with http:// or https://");
        return -2;
    }

    // EFFICIENT: Find delimiters in single pass using pointer arithmetic
    const char *l_path_start = NULL;
    const char *l_port_start = NULL;
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
    
    // If port found but no path, adjust host end
    if (l_port_start && !l_path_start) {
        l_host_end = l_port_start;
    }

    // EFFICIENT: Extract hostname only if needed
    if (a_host) {
        size_t l_host_len = l_host_end - l_url_start;
        if (l_host_len == 0 || l_host_len >= DAP_HOSTADDR_STRLEN) {
            log_it(L_ERROR, "Invalid hostname length: %zu", l_host_len);
            return -3;
        }
        
        *a_host = DAP_NEW_Z_SIZE(char, l_host_len + 1);
        if (!*a_host) {
            log_it(L_CRITICAL, "Failed to allocate memory for hostname");
            return -4;
        }
        memcpy(*a_host, l_url_start, l_host_len);
        (*a_host)[l_host_len] = '\0';
    }

    // EFFICIENT: Parse port only if needed
    if (a_port) {
        *a_port = l_default_port;
        
        if (l_port_start) {
            const char *l_port_str = l_port_start + 1;
            const char *l_port_end = l_path_start ? l_path_start : l_url_end;
            
            // EFFICIENT: Manual port parsing with pointer arithmetic
            uint16_t l_port_val = 0;
            bool l_valid_port = true;
            
            for (const char *p = l_port_str; p < l_port_end && *p >= '0' && *p <= '9'; p++) {
                uint16_t l_new_val = l_port_val * 10 + (*p - '0');
                if (l_new_val < l_port_val || l_new_val > 65535) {
                    l_valid_port = false;
                    break;
                }
                l_port_val = l_new_val;
            }
            
            if (l_valid_port && l_port_val > 0) {
                *a_port = l_port_val;
            } else {
                log_it(L_WARNING, "Invalid port in URL, using default %u", l_default_port);
            }
        }
    }

    // EFFICIENT: Extract path only if needed
    if (a_path && !(*a_path = dap_strdup(l_path_start ? l_path_start : "/"))) {
        log_it(L_CRITICAL, "Failed to allocate memory for path");
        if (a_host) {
            DAP_DEL_Z(*a_host);  // EFFICIENT: no NULL check needed, auto-nullify
        }
        return -5;
    }

    // Set SSL flag only if needed
    if (a_use_ssl) {
        *a_use_ssl = l_is_ssl;
    }

    if (s_debug_more && a_host && a_port && a_path && a_use_ssl) {
        log_it(L_DEBUG, "Parsed URL: host='%s', port=%u, path='%s', ssl=%s",
               *a_host, *a_port, *a_path, *a_use_ssl ? "enabled" : "disabled");
    }

    return 0;
}

/**
 * @brief Set request URL (EFFICIENT: minimal allocations)
 * @param a_request Request instance
 * @param a_url URL to set
 * @return 0 on success, negative on error
 */
int dap_http2_client_request_set_url(dap_http2_client_request_t *a_request, const char *a_url)
{
    if (!a_request || !a_url) {
        log_it(L_ERROR, "Invalid arguments in dap_http2_client_request_set_url");
        return -1;
    }

    // Clean up old URL data
    DAP_DEL_MULTY(a_request->url, a_request->host);

    // Store full URL
    a_request->url = dap_strdup(a_url);
    if (!a_request->url) {
        log_it(L_CRITICAL, "Failed to allocate memory for URL");
        return -2;
    }

    // EFFICIENT: Parse only needed components (no path allocation)
    int l_result = s_parse_url(a_url, 
                                        &a_request->host,    // Need host
                                        &a_request->port,    // Need port
                                        NULL,                // Don't need path (saves allocation!)
                                        &a_request->use_ssl); // Need SSL flag
    if (l_result < 0) {
        DAP_DEL_Z(a_request->url);  // EFFICIENT: no NULL check needed, auto-nullify
        return l_result;
    }

    log_it(L_DEBUG, "Set request URL: %s", a_url);
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
    dap_http_method_t l_method_enum = dap_http_method_from_string(a_method);
    if (l_method_enum == DAP_HTTP_METHOD_COUNT) {
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

    // Clean up old headers
    DAP_DEL_Z(a_request->custom_headers);  // EFFICIENT: no NULL check needed

    // Set new headers (can be NULL)
    if (a_headers) {
        a_request->custom_headers = dap_strdup(a_headers);
        if (!a_request->custom_headers) {
            log_it(L_CRITICAL, "Failed to allocate memory for headers");
            return -2;
        }
        log_it(L_DEBUG, "Set request headers: %s", a_headers);
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
        log_it(L_DEBUG, "Set request body: %zu bytes", a_size);
    } else {
        log_it(L_DEBUG, "Cleared request body");
    }

    return 0;
}
