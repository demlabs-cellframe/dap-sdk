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

/**
 * @file dap_http_client_simple_wasm.c
 * @brief WASM implementation of async HTTP client via EM_JS + pthread
 */

#ifdef __EMSCRIPTEN__

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <emscripten.h>
#include <emscripten/em_js.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_http_client_simple.h"

#define LOG_TAG "http_client_simple"

/* ========================================================================
 * EM_JS: Synchronous HTTP POST (runs on calling pthread's Web Worker)
 * ======================================================================== */

EM_JS(int, js_http_post_sync, (const char *a_url_ptr,
                                const char *a_content_type_ptr,
                                const void *a_body, int a_body_len,
                                const char *a_extra_headers_ptr,
                                int a_out_ptr_addr, int a_out_len_addr), {
    var url = UTF8ToString(a_url_ptr);
    var contentType = a_content_type_ptr ? UTF8ToString(a_content_type_ptr) : null;
    var extraHeaders = a_extra_headers_ptr ? UTF8ToString(a_extra_headers_ptr) : null;

    var xhr = new XMLHttpRequest();
    xhr.open("POST", url, false);
    xhr.responseType = "arraybuffer";
    if (contentType) xhr.setRequestHeader("Content-Type", contentType);

    if (extraHeaders) {
        var lines = extraHeaders.split("\r\n");
        for (var i = 0; i < lines.length; i++) {
            var sep = lines[i].indexOf(":");
            if (sep > 0) {
                xhr.setRequestHeader(lines[i].substring(0, sep).trim(),
                                     lines[i].substring(sep + 1).trim());
            }
        }
    }

    if (a_body && a_body_len > 0) {
        xhr.send(HEAPU8.slice(a_body, a_body + a_body_len));
    } else {
        xhr.send();
    }

    if (xhr.status >= 200 && xhr.status < 300) {
        var response = new Uint8Array(xhr.response);
        if (response.length > 0) {
            var ptr = _malloc(response.length + 1);
            HEAPU8.set(response, ptr);
            HEAPU8[ptr + response.length] = 0;
            setValue(a_out_ptr_addr, ptr, '*');
            setValue(a_out_len_addr, response.length, 'i32');
        } else {
            setValue(a_out_ptr_addr, 0, '*');
            setValue(a_out_len_addr, 0, 'i32');
        }
        return 0;
    }
    return -xhr.status || -1;
});

/* ========================================================================
 * Async request: spawns detached pthread that calls js_http_post_sync
 * ======================================================================== */

typedef struct {
    char                              *url;
    char                              *content_type;
    void                              *body;
    size_t                             body_size;
    char                              *extra_headers;
    dap_http_client_simple_callback_t  callback;
    void                              *user_data;
} s_request_t;

static void *s_request_thread(void *a_arg)
{
    s_request_t *l_req = (s_request_t *)a_arg;

    void *l_resp = NULL;
    int l_resp_len = 0;
    int l_rc = js_http_post_sync(l_req->url, l_req->content_type,
                                  l_req->body, (int)l_req->body_size,
                                  l_req->extra_headers,
                                  (int)(uintptr_t)&l_resp,
                                  (int)(uintptr_t)&l_resp_len);
    if (l_req->callback) {
        if (l_rc == 0 && l_resp)
            l_req->callback(l_resp, (size_t)l_resp_len, 0, l_req->user_data);
        else
            l_req->callback(NULL, 0, l_rc ? l_rc : -1, l_req->user_data);
    }

    free(l_resp);
    DAP_DELETE(l_req->url);
    DAP_DELETE(l_req->content_type);
    DAP_DELETE(l_req->body);
    DAP_DELETE(l_req->extra_headers);
    DAP_DELETE(l_req);
    return NULL;
}

int dap_http_client_simple_request(const char *a_url,
                                    const char *a_content_type,
                                    const void *a_body, size_t a_body_size,
                                    const char *a_extra_headers,
                                    dap_http_client_simple_callback_t a_callback,
                                    void *a_user_data)
{
    if (!a_url || !a_callback) return -1;

    s_request_t *l_req = DAP_NEW_Z(s_request_t);
    if (!l_req) return -1;

    l_req->url           = dap_strdup(a_url);
    l_req->content_type  = a_content_type ? dap_strdup(a_content_type) : NULL;
    l_req->extra_headers = a_extra_headers ? dap_strdup(a_extra_headers) : NULL;
    l_req->callback      = a_callback;
    l_req->user_data     = a_user_data;

    if (a_body && a_body_size > 0) {
        l_req->body = DAP_DUP_SIZE(a_body, a_body_size);
        l_req->body_size = a_body_size;
    }

    pthread_t l_thread;
    pthread_attr_t l_attr;
    pthread_attr_init(&l_attr);
    pthread_attr_setdetachstate(&l_attr, PTHREAD_CREATE_DETACHED);
    int l_ret = pthread_create(&l_thread, &l_attr, s_request_thread, l_req);
    pthread_attr_destroy(&l_attr);

    if (l_ret != 0) {
        log_it(L_ERROR, "dap_http_client_simple_request: pthread_create failed: %d", l_ret);
        DAP_DELETE(l_req->url);
        DAP_DELETE(l_req->content_type);
        DAP_DELETE(l_req->extra_headers);
        DAP_DELETE(l_req->body);
        DAP_DELETE(l_req);
        return -1;
    }

    return 0;
}

#endif /* __EMSCRIPTEN__ */
