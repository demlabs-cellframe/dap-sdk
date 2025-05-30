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
#include <stddef.h>
#include "dap_worker.h"
#include "http_status_code.h"
#include "dap_http_header.h"  // Include common header for dap_http_header structure
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*dap_client_http_callback_error_t)(int, void *); // Callback for specific http client operations
typedef void (*dap_client_http_callback_error_ext_t)(int,int , void *,size_t, void *); // Callback with extended error processing
typedef void (*dap_client_http_callback_data_t)(void *, size_t, void *, http_status_code_t); // Callback for specific http client operations

// New callback type that includes headers
typedef void (*dap_client_http_callback_full_t)(void *a_body, size_t a_body_size, 
                                                 struct dap_http_header *a_headers, 
                                                 void *a_arg, http_status_code_t a_status_code);

// Callback-only API - no return value, fully async
typedef void (*dap_client_http_callback_started_t)(void *a_arg); // Called when request starts
typedef void (*dap_client_http_callback_progress_t)(size_t a_downloaded, size_t a_total, void *a_arg); // Progress callback

typedef struct dap_client_http {
    // TODO move unnessassary fields to dap_client_http_pvt privat structure
    dap_client_http_callback_data_t response_callback;
    dap_client_http_callback_full_t response_callback_full;  // Full callback with headers
    dap_client_http_callback_error_t error_callback;
    void *callbacks_arg;

    byte_t *request;
    size_t request_size;
    size_t request_sent_size;
    bool is_over_ssl;

    int socket;

    bool is_header_read;
    bool is_closed_by_timeout;
    bool were_callbacks_called;
    size_t header_length;
    size_t content_length;
    time_t ts_last_read;
    uint8_t *response;
    size_t response_size;
    size_t response_size_max;
    
    // Add new fields for headers processing and redirects
    struct dap_http_header *response_headers;   // Parsed response headers
    uint8_t redirect_count;                      // Current redirect count
    #define DAP_CLIENT_HTTP_MAX_REDIRECTS 10    // Maximum allowed redirects

    // Request args
    char uplink_addr[DAP_HOSTADDR_STRLEN];
    uint16_t uplink_port;
    char *method;
    char *request_content_type;
    char * path;
    char *cookie;
    char *request_custom_headers; // Custom headers

    // Request vars
    dap_worker_t *worker;
    dap_timerfd_t *timer;
    dap_events_socket_t *es;

} dap_client_http_t;

#define DAP_CLIENT_HTTP(a) (a ? (dap_client_http_t *) (a)->_inheritor : NULL)


int dap_client_http_init();
void dap_client_http_deinit();

dap_client_http_t *dap_client_http_request_custom(dap_worker_t * a_worker, const char *a_uplink_addr, uint16_t a_uplink_port, const char *a_method,
        const char *a_request_content_type, const char * a_path, const void *a_request, size_t a_request_size, char *a_cookie,
        dap_client_http_callback_data_t a_response_callback, dap_client_http_callback_error_t a_error_callback,                                  
        void *a_callbacks_arg, char *a_custom_headers, bool a_over_ssl);
dap_client_http_t *dap_client_http_request(dap_worker_t * a_worker,const char *a_uplink_addr, uint16_t a_uplink_port, const char * a_method,
        const char* a_request_content_type, const char * a_path, const void *a_request, size_t a_request_size,
        char * a_cookie, dap_client_http_callback_data_t a_response_callback,
        dap_client_http_callback_error_t a_error_callback, void *a_callbacks_arg, char *a_custom_headers);

// New function with full callback including headers
dap_client_http_t *dap_client_http_request_full(dap_worker_t * a_worker,const char *a_uplink_addr, uint16_t a_uplink_port, const char * a_method,
        const char* a_request_content_type, const char * a_path, const void *a_request, size_t a_request_size,
        char * a_cookie, dap_client_http_callback_full_t a_response_callback,
        dap_client_http_callback_error_t a_error_callback, void *a_callbacks_arg, char *a_custom_headers);

uint64_t dap_client_http_get_connect_timeout_ms();
void dap_client_http_set_connect_timeout_ms(uint64_t a_timeout_ms);

void dap_client_http_close_unsafe(dap_client_http_t *a_client_http);

// Callback-only API - thread-safe, no return values
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
        char *a_custom_headers);

// Simplified version without progress/started callbacks
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
        char *a_custom_headers);

#ifdef __cplusplus
}
#endif
