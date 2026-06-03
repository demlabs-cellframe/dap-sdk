/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * DAP (Distributed Applications Platform) is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 */

#pragma once

#include <stddef.h>

/**
 * @file dap_http_client_simple.h
 * @brief Asynchronous HTTP client for WASM — client-side counterpart to dap_http_simple
 *
 * Provides a non-blocking HTTP POST from any thread. The caller builds the URL and body,
 * then calls dap_http_client_simple_request(). A detached pthread performs the actual
 * synchronous XHR, and the callback is invoked from that pthread with the response.
 *
 * This avoids blocking DAP worker threads and eliminates boilerplate pthread management
 * from transport implementations.
 */

/**
 * @brief Callback invoked when async HTTP request completes.
 * @param a_response    Response body (NULL on error). Valid only during callback scope.
 * @param a_response_size  Size of response in bytes
 * @param a_error_code  0 on success (HTTP 2xx), negative HTTP status or -1 on failure
 * @param a_user_data   Opaque pointer passed through from the request call
 * @note a_response is freed automatically after callback returns. Copy if needed.
 */
typedef void (*dap_http_client_simple_callback_t)(void *a_response, size_t a_response_size,
                                                   int a_error_code, void *a_user_data);

/**
 * @brief Send an async HTTP POST request.
 *
 * Spawns a detached pthread that performs a synchronous XHR (WASM) or equivalent,
 * then invokes a_callback from that thread.
 *
 * @param a_url           Full URL (copied internally)
 * @param a_content_type  Content-Type header value (copied, may be NULL)
 * @param a_body          Request body (copied internally, may be NULL if a_body_size==0)
 * @param a_body_size     Body size in bytes
 * @param a_extra_headers Extra headers as "Name: Value\r\nName: Value" (copied, may be NULL)
 * @param a_callback      Response callback (required)
 * @param a_user_data     Opaque user pointer passed to callback
 * @return 0 on successful dispatch, -1 on error (callback will NOT be called)
 */
int dap_http_client_simple_request(const char *a_url,
                                    const char *a_content_type,
                                    const void *a_body, size_t a_body_size,
                                    const char *a_extra_headers,
                                    dap_http_client_simple_callback_t a_callback,
                                    void *a_user_data);
