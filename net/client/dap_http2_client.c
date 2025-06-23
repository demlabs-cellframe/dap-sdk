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

#include "dap_http2_client.h"
#include "http_status_code.h"
#include "dap_common.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_http2_client"

// Global initialization
int dap_http2_client_init(void)
{
    // TODO: Implement global initialization
    return 0;
}

void dap_http2_client_deinit(void)
{
    // TODO: Implement global deinitialization
}

// Client lifecycle
dap_http2_client_t *dap_http2_client_create(dap_worker_t *a_worker)
{
    // Validate worker parameter
    if (!a_worker) {
        a_worker = dap_worker_get_current();
        if (!a_worker) {
            a_worker = dap_worker_get_auto();
        }
        if (!a_worker) {
            log_it(L_ERROR, "No worker available for HTTP2 client");
            return NULL;
        }
    }

    // Allocate client structure
    dap_http2_client_t *l_client = DAP_NEW_Z(dap_http2_client_t);
    if (!l_client) {
        log_it(L_CRITICAL, "Failed to allocate memory for HTTP2 client");
        return NULL;
    }

    // Initialize client state
    l_client->state = DAP_HTTP2_CLIENT_STATE_IDLE;
    
    // Set default configuration
    l_client->config = dap_http2_client_config_default();
    
    // Create session for this client
    l_client->session = dap_http2_session_create(a_worker);
    if (!l_client->session) {
        log_it(L_ERROR, "Failed to create session for HTTP2 client");
        DAP_DEL_Z(l_client);
        return NULL;
    }

    log_it(L_DEBUG, "Created HTTP2 client %p with session %p on worker %u", 
           l_client, l_client->session, a_worker->id);

    return l_client;
}

void dap_http2_client_delete(dap_http2_client_t *a_client)
{
    if (!a_client) {
        return;
    }

    log_it(L_DEBUG, "Deleting HTTP2 client %p", a_client);

    // Close ongoing request if any
    if (a_client->state == DAP_HTTP2_CLIENT_STATE_REQUESTING || 
        a_client->state == DAP_HTTP2_CLIENT_STATE_RECEIVING) {
        dap_http2_client_cancel(a_client);
    }

    // Delete current request if exists
    if (a_client->current_request) {
        dap_http2_client_request_delete(a_client->current_request);
        a_client->current_request = NULL;
    }

    // Delete stream if exists
    if (a_client->stream) {
        dap_http2_stream_delete(a_client->stream);
        a_client->stream = NULL;
    }

    // Delete session if exists
    if (a_client->session) {
        dap_http2_session_delete(a_client->session);
        a_client->session = NULL;
    }

    // Clean up configuration strings
    if (a_client->config.default_user_agent) {
        DAP_DELETE(a_client->config.default_user_agent);
    }
    if (a_client->config.default_accept) {
        DAP_DELETE(a_client->config.default_accept);
    }
    if (a_client->config.ssl_cert_path) {
        DAP_DELETE(a_client->config.ssl_cert_path);
    }
    if (a_client->config.ssl_key_path) {
        DAP_DELETE(a_client->config.ssl_key_path);
    }
    if (a_client->config.ssl_ca_path) {
        DAP_DELETE(a_client->config.ssl_ca_path);
    }

    // Free client structure
    DAP_DELETE(a_client);
}

// Configuration
void dap_http2_client_set_config(dap_http2_client_t *a_client, const dap_http2_client_config_t *a_config)
{
    // TODO: Implement config setting
    UNUSED(a_client);
    UNUSED(a_config);
}

dap_http2_client_config_t *dap_http2_client_get_config(dap_http2_client_t *a_client)
{
    // TODO: Implement config retrieval
    UNUSED(a_client);
    return NULL;
}

void dap_http2_client_set_callbacks(dap_http2_client_t *a_client,
                                    const dap_http2_client_callbacks_t *a_callbacks,
                                    void *a_callbacks_arg)
{
    // TODO: Implement callback setting
    UNUSED(a_client);
    UNUSED(a_callbacks);
    UNUSED(a_callbacks_arg);
}

// Request management
dap_http2_client_request_t *dap_http2_client_request_create(void)
{
    dap_http2_client_request_t *l_request = DAP_NEW_Z(dap_http2_client_request_t);
    if (!l_request) {
        log_it(L_CRITICAL, "Failed to allocate memory for HTTP2 client request");
        return NULL;
    }

    // Initialize with default values
    l_request->method = NULL;
    l_request->url = NULL;
    l_request->host = NULL;
    l_request->port = 80;
    l_request->use_ssl = false;
    l_request->content_type = NULL;
    l_request->custom_headers = NULL;
    l_request->body_data = NULL;
    l_request->body_size = 0;

    log_it(L_DEBUG, "Created HTTP2 client request %p", l_request);
    return l_request;
}

void dap_http2_client_request_delete(dap_http2_client_request_t *a_request)
{
    if (!a_request) {
        return;
    }

    log_it(L_DEBUG, "Deleting HTTP2 client request %p", a_request);

    // Free all allocated strings
    if (a_request->method) {
        DAP_DELETE(a_request->method);
    }
    if (a_request->url) {
        DAP_DELETE(a_request->url);
    }
    if (a_request->host) {
        DAP_DELETE(a_request->host);
    }
    if (a_request->content_type) {
        DAP_DELETE(a_request->content_type);
    }
    if (a_request->custom_headers) {
        DAP_DELETE(a_request->custom_headers);
    }
    if (a_request->body_data) {
        DAP_DELETE(a_request->body_data);
    }

    // Free request structure
    DAP_DELETE(a_request);
}

int dap_http2_client_request_set_url(dap_http2_client_request_t *a_request, const char *a_url)
{
    // TODO: Implement URL setting
    UNUSED(a_request);
    UNUSED(a_url);
    return -1;
}

int dap_http2_client_request_set_method(dap_http2_client_request_t *a_request, const char *a_method)
{
    // TODO: Implement method setting
    UNUSED(a_request);
    UNUSED(a_method);
    return -1;
}

int dap_http2_client_request_set_headers(dap_http2_client_request_t *a_request, const char *a_headers)
{
    // TODO: Implement header setting
    UNUSED(a_request);
    UNUSED(a_headers);
    return -1;
}

int dap_http2_client_request_set_body(dap_http2_client_request_t *a_request, const void *a_data, size_t a_size)
{
    // TODO: Implement body setting
    UNUSED(a_request);
    UNUSED(a_data);
    UNUSED(a_size);
    return -1;
}

// Synchronous requests
int dap_http2_client_request_sync(dap_http2_client_t *a_client,
                                  const dap_http2_client_request_t *a_request,
                                  void **a_response_data,
                                  size_t *a_response_size,
                                  int *a_status_code)
{
    // TODO: Implement synchronous request
    UNUSED(a_client);
    UNUSED(a_request);
    UNUSED(a_response_data);
    UNUSED(a_response_size);
    UNUSED(a_status_code);
    return -1;
}

// Asynchronous requests
int dap_http2_client_request_async(dap_http2_client_t *a_client,
                                   const dap_http2_client_request_t *a_request)
{
    // TODO: Implement asynchronous request
    UNUSED(a_client);
    UNUSED(a_request);
    return -1;
}

// Control operations
void dap_http2_client_cancel(dap_http2_client_t *a_client)
{
    if (!a_client) {
        return;
    }

    log_it(L_DEBUG, "Cancelling HTTP2 client %p request", a_client);

    // Update client state
    if (a_client->state == DAP_HTTP2_CLIENT_STATE_REQUESTING ||
        a_client->state == DAP_HTTP2_CLIENT_STATE_RECEIVING) {
        a_client->state = DAP_HTTP2_CLIENT_STATE_CANCELLED;
    }

    // Close session if active
    if (a_client->session) {
        dap_http2_session_close(a_client->session);
    }

    // Call error callback if set and not already called
    if (a_client->callbacks.error_cb) {
        a_client->callbacks.error_cb(a_client, DAP_HTTP2_CLIENT_ERROR_CANCELLED);
    }
}

void dap_http2_client_close(dap_http2_client_t *a_client)
{
    // TODO: Implement client close
    UNUSED(a_client);
}

// State queries
dap_http2_client_state_t dap_http2_client_get_state(const dap_http2_client_t *a_client)
{
    // TODO: Implement state query
    UNUSED(a_client);
    return DAP_HTTP2_CLIENT_STATE_IDLE;
}

bool dap_http2_client_is_busy(const dap_http2_client_t *a_client)
{
    // TODO: Implement busy check
    UNUSED(a_client);
    return false;
}

bool dap_http2_client_is_complete(const dap_http2_client_t *a_client)
{
    // TODO: Implement completion check
    UNUSED(a_client);
    return false;
}

bool dap_http2_client_is_error(const dap_http2_client_t *a_client)
{
    // TODO: Implement error check
    UNUSED(a_client);
    return false;
}

// Statistics functions now delegated to session layer

// Convenience functions
int dap_http2_client_get_sync(dap_worker_t *a_worker,
                              const char *a_url,
                              void **a_response_data,
                              size_t *a_response_size,
                              int *a_status_code)
{
    // TODO: Implement convenience GET
    UNUSED(a_worker);
    UNUSED(a_url);
    UNUSED(a_response_data);
    UNUSED(a_response_size);
    UNUSED(a_status_code);
    return -1;
}

int dap_http2_client_post_sync(dap_worker_t *a_worker,
                               const char *a_url,
                               const void *a_body_data,
                               size_t a_body_size,
                               const char *a_content_type,
                               void **a_response_data,
                               size_t *a_response_size,
                               int *a_status_code)
{
    // TODO: Implement convenience POST
    UNUSED(a_worker);
    UNUSED(a_url);
    UNUSED(a_body_data);
    UNUSED(a_body_size);
    UNUSED(a_content_type);
    UNUSED(a_response_data);
    UNUSED(a_response_size);
    UNUSED(a_status_code);
    return -1;
}

int dap_http2_client_get_async(dap_worker_t *a_worker,
                               const char *a_url,
                               dap_http2_client_response_cb_t a_response_cb,
                               dap_http2_client_error_cb_t a_error_cb,
                               void *a_callbacks_arg)
{
    // TODO: Implement convenience async GET
    UNUSED(a_worker);
    UNUSED(a_url);
    UNUSED(a_response_cb);
    UNUSED(a_error_cb);
    UNUSED(a_callbacks_arg);
    return -1;
}

// Default configuration helpers
dap_http2_client_config_t dap_http2_client_config_default(void)
{
    dap_http2_client_config_t l_config = {0};
    
    // Default timeouts (based on old HTTP client)
    l_config.connect_timeout_ms = 30000;     // 30 seconds
    l_config.read_timeout_ms = 60000;        // 60 seconds  
    l_config.total_timeout_ms = 300000;      // 5 minutes
    
    // Default limits
    l_config.max_response_size = 100 * 1024 * 1024;  // 100MB
    l_config.max_redirects = 5;
    
    // Default options
    l_config.follow_redirects = true;
    l_config.verify_ssl = true;
    l_config.enable_compression = true;
    
    // Default headers (will be allocated when set)
    l_config.default_user_agent = dap_strdup("DAP-HTTP2-Client/1.0");
    l_config.default_accept = dap_strdup("*/*");
    
    // SSL paths initially NULL
    l_config.ssl_cert_path = NULL;
    l_config.ssl_key_path = NULL;
    l_config.ssl_ca_path = NULL;
    
    return l_config;
}

void dap_http2_client_config_set_timeouts(dap_http2_client_config_t *a_config,
                                          uint64_t a_connect_timeout_ms,
                                          uint64_t a_read_timeout_ms)
{
    // TODO: Implement timeout configuration
    UNUSED(a_config);
    UNUSED(a_connect_timeout_ms);
    UNUSED(a_read_timeout_ms);
}

dap_worker_t *dap_http2_client_get_worker(const dap_http2_client_t *a_client)
{
    // TODO: Return a_client->session->worker
    return NULL;
}

bool dap_http2_client_is_async(const dap_http2_client_t *a_client)
{
    // TODO: Check if a_client->callbacks has non-NULL callbacks
    return false;
}

bool dap_http2_client_is_cancelled(const dap_http2_client_t *a_client)
{
    // TODO: Check if a_client->state == DAP_HTTP2_CLIENT_STATE_CANCELLED
    return false;
} 