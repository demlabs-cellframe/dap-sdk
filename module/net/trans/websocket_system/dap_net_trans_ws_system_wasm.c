/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
 * All rights reserved.

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

/**
 * @file dap_net_trans_ws_system_wasm.c
 * @brief WASM/Emscripten implementation of WebSocket System Transport
 *
 * Uses EM_JS to call browser's native WebSocket API. Data flows:
 *   C (dap_stream) -> write() -> EM_JS ws_send() -> JS WebSocket.send()
 *   JS WebSocket.onmessage -> EM_ASM callback -> C read buffer -> dap_stream
 */

#ifdef __EMSCRIPTEN__

#include <stdlib.h>
#include <string.h>
#include <emscripten.h>
#include <emscripten/em_js.h>

#include "dap_common.h"
#include "dap_net_trans_websocket_system.h"
#include "dap_net_trans.h"
#include "dap_net_trans_types.h"
#include "dap_ring_buffer.h"

#define LOG_TAG "ws_system_wasm"

#define WS_DEFAULT_MAX_MSG_SIZE   (1024 * 1024)
#define WS_DEFAULT_RECV_BUF_SIZE  (256 * 1024)

typedef struct ws_system_conn {
    int js_handle;
    dap_ws_system_state_t state;
    dap_ring_buffer_t *recv_buf;
    dap_net_trans_ws_system_config_t config;

    dap_net_trans_connect_cb_t connect_cb;
    struct dap_stream *connect_stream;

    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t msgs_sent;
    uint64_t msgs_received;
} ws_system_conn_t;

/* ========================================================================
 * JavaScript WebSocket bridge via EM_JS
 * ======================================================================== */

EM_JS(int, js_ws_create, (const char *url_ptr, const char *proto_ptr), {
    var url = UTF8ToString(url_ptr);
    var proto = proto_ptr ? UTF8ToString(proto_ptr) : null;

    if (!Module._ws_pool) {
        Module._ws_pool = {};
        Module._ws_next_id = 1;
    }

    var id = Module._ws_next_id++;
    var ws;
    try {
        ws = proto ? new WebSocket(url, proto) : new WebSocket(url);
    } catch (e) {
        console.error("WebSocket create error:", e);
        return -1;
    }
    ws.binaryType = "arraybuffer";

    Module._ws_pool[id] = {
        ws: ws,
        state: 0 /* CONNECTING */
    };

    ws.onopen = function() {
        var entry = Module._ws_pool[id];
        if (entry) entry.state = 1;
        if (Module._ws_on_open) Module._ws_on_open(id);
    };

    ws.onclose = function(ev) {
        var entry = Module._ws_pool[id];
        if (entry) entry.state = 3;
        if (Module._ws_on_close) Module._ws_on_close(id, ev.code);
    };

    ws.onerror = function() {
        if (Module._ws_on_error) Module._ws_on_error(id);
    };

    ws.onmessage = function(ev) {
        if (typeof ev.data === "string") {
            var enc = new TextEncoder();
            var arr = enc.encode(ev.data);
            var buf = Module._malloc(arr.length);
            Module.HEAPU8.set(arr, buf);
            if (Module._ws_on_message) Module._ws_on_message(id, buf, arr.length);
            Module._free(buf);
        } else {
            var arr = new Uint8Array(ev.data);
            var buf = Module._malloc(arr.length);
            Module.HEAPU8.set(arr, buf);
            if (Module._ws_on_message) Module._ws_on_message(id, buf, arr.length);
            Module._free(buf);
        }
    };

    return id;
});

EM_JS(int, js_ws_send, (int handle, const void *data, int len), {
    var entry = Module._ws_pool ? Module._ws_pool[handle] : null;
    if (!entry || entry.state !== 1) return -1;
    try {
        var view = Module.HEAPU8.subarray(data, data + len);
        entry.ws.send(view.slice().buffer);
        return len;
    } catch (e) {
        console.error("WebSocket send error:", e);
        return -1;
    }
});

EM_JS(void, js_ws_close, (int handle, int code), {
    var entry = Module._ws_pool ? Module._ws_pool[handle] : null;
    if (!entry) return;
    try {
        entry.ws.close(code);
    } catch (e) { /* ignore */ }
    entry.state = 2;
});

EM_JS(void, js_ws_destroy, (int handle), {
    if (!Module._ws_pool) return;
    var entry = Module._ws_pool[handle];
    if (entry) {
        try { entry.ws.close(); } catch (e) { /* ignore */ }
        delete Module._ws_pool[handle];
    }
});

EM_JS(int, js_ws_get_state, (int handle), {
    var entry = Module._ws_pool ? Module._ws_pool[handle] : null;
    if (!entry) return 3;
    return entry.state;
});

/* ========================================================================
 * C callbacks invoked from JavaScript
 * ======================================================================== */

static ws_system_conn_t *s_connections[256] = {0};

static ws_system_conn_t *s_find_conn(int a_handle)
{
    if (a_handle < 0 || a_handle >= 256) return NULL;
    return s_connections[a_handle];
}

static void s_register_conn(int a_handle, ws_system_conn_t *a_conn)
{
    if (a_handle >= 0 && a_handle < 256)
        s_connections[a_handle] = a_conn;
}

static void s_unregister_conn(int a_handle)
{
    if (a_handle >= 0 && a_handle < 256)
        s_connections[a_handle] = NULL;
}

EMSCRIPTEN_KEEPALIVE
void _ws_on_open(int a_handle)
{
    ws_system_conn_t *l_conn = s_find_conn(a_handle);
    if (!l_conn) return;

    l_conn->state = DAP_WS_SYSTEM_STATE_OPEN;
    log_it(L_NOTICE, "WebSocket System connection opened (handle=%d)", a_handle);

    if (l_conn->connect_cb && l_conn->connect_stream) {
        l_conn->connect_cb(l_conn->connect_stream, 0);
        l_conn->connect_cb = NULL;
    }
}

EMSCRIPTEN_KEEPALIVE
void _ws_on_close(int a_handle, int a_code)
{
    ws_system_conn_t *l_conn = s_find_conn(a_handle);
    if (!l_conn) return;

    l_conn->state = DAP_WS_SYSTEM_STATE_CLOSED;
    log_it(L_INFO, "WebSocket System connection closed (handle=%d, code=%d)", a_handle, a_code);

    if (l_conn->connect_cb && l_conn->connect_stream) {
        l_conn->connect_cb(l_conn->connect_stream, -1);
        l_conn->connect_cb = NULL;
    }
}

EMSCRIPTEN_KEEPALIVE
void _ws_on_error(int a_handle)
{
    ws_system_conn_t *l_conn = s_find_conn(a_handle);
    if (!l_conn) return;

    log_it(L_ERROR, "WebSocket System error (handle=%d)", a_handle);

    if (l_conn->connect_cb && l_conn->connect_stream) {
        l_conn->connect_cb(l_conn->connect_stream, -2);
        l_conn->connect_cb = NULL;
    }
}

EMSCRIPTEN_KEEPALIVE
void _ws_on_message(int a_handle, const uint8_t *a_data, int a_len)
{
    ws_system_conn_t *l_conn = s_find_conn(a_handle);
    if (!l_conn || !l_conn->recv_buf || a_len <= 0) return;

    size_t l_written = dap_ring_buffer_write(l_conn->recv_buf, a_data, (size_t)a_len);
    if (l_written < (size_t)a_len) {
        log_it(L_WARNING, "WebSocket recv buffer overflow: %d bytes dropped",
               a_len - (int)l_written);
    }

    l_conn->bytes_received += l_written;
    l_conn->msgs_received++;
}

/* ========================================================================
 * Register JS callbacks at module init
 * ======================================================================== */

EM_JS(void, js_ws_register_callbacks, (), {
    Module._ws_on_open    = Module.cwrap('_ws_on_open',    null, ['number']);
    Module._ws_on_close   = Module.cwrap('_ws_on_close',   null, ['number', 'number']);
    Module._ws_on_error   = Module.cwrap('_ws_on_error',   null, ['number']);
    Module._ws_on_message = Module.cwrap('_ws_on_message', null, ['number', 'number', 'number']);
});

/* ========================================================================
 * Trans ops implementation
 * ======================================================================== */

static int s_ws_system_init(dap_net_trans_t *a_trans, dap_config_t *a_config)
{
    (void)a_config;
    (void)a_trans;
    js_ws_register_callbacks();
    log_it(L_NOTICE, "WebSocket System transport initialized (WASM)");
    return 0;
}

static void s_ws_system_deinit(dap_net_trans_t *a_trans)
{
    (void)a_trans;
    for (int i = 0; i < 256; i++) {
        if (s_connections[i]) {
            js_ws_destroy(i);
            if (s_connections[i]->recv_buf)
                dap_ring_buffer_delete(s_connections[i]->recv_buf);
            DAP_FREE(s_connections[i]);
            s_connections[i] = NULL;
        }
    }
    log_it(L_NOTICE, "WebSocket System transport deinitialized");
}

static int s_ws_system_connect(dap_stream_t *a_stream,
                               const char *a_host,
                               uint16_t a_port,
                               dap_net_trans_connect_cb_t a_callback)
{
    if (!a_stream || !a_host) return -1;

    ws_system_conn_t *l_conn = DAP_NEW_Z(ws_system_conn_t);
    if (!l_conn) return -1;

    l_conn->config = dap_net_trans_ws_system_config_default();
    l_conn->recv_buf = dap_ring_buffer_create(WS_DEFAULT_RECV_BUF_SIZE);
    if (!l_conn->recv_buf) {
        DAP_FREE(l_conn);
        return -1;
    }

    char l_url[512];
    snprintf(l_url, sizeof(l_url), "wss://%s:%u/stream", a_host, a_port);

    l_conn->connect_cb = a_callback;
    l_conn->connect_stream = a_stream;

    int l_handle = js_ws_create(l_url, l_conn->config.subprotocol);
    if (l_handle < 0) {
        dap_ring_buffer_delete(l_conn->recv_buf);
        DAP_FREE(l_conn);
        return -1;
    }

    l_conn->js_handle = l_handle;
    l_conn->state = DAP_WS_SYSTEM_STATE_CONNECTING;
    s_register_conn(l_handle, l_conn);

    a_stream->_server_session = l_conn;

    log_it(L_INFO, "WebSocket System connecting to %s (handle=%d)", l_url, l_handle);
    return 0;
}

static ssize_t s_ws_system_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_stream->_server_session) return -1;
    ws_system_conn_t *l_conn = (ws_system_conn_t *)a_stream->_server_session;
    if (!l_conn->recv_buf) return -1;

    size_t l_available = dap_ring_buffer_get_used(l_conn->recv_buf);
    if (l_available == 0) return 0;

    size_t l_to_read = a_size < l_available ? a_size : l_available;
    return (ssize_t)dap_ring_buffer_read(l_conn->recv_buf, a_buffer, l_to_read);
}

static ssize_t s_ws_system_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_stream->_server_session) return -1;
    ws_system_conn_t *l_conn = (ws_system_conn_t *)a_stream->_server_session;
    if (l_conn->state != DAP_WS_SYSTEM_STATE_OPEN) return -1;

    int l_sent = js_ws_send(l_conn->js_handle, a_data, (int)a_size);
    if (l_sent > 0) {
        l_conn->bytes_sent += l_sent;
        l_conn->msgs_sent++;
    }
    return (ssize_t)l_sent;
}

static void s_ws_system_close(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->_server_session) return;
    ws_system_conn_t *l_conn = (ws_system_conn_t *)a_stream->_server_session;

    if (l_conn->state == DAP_WS_SYSTEM_STATE_OPEN ||
        l_conn->state == DAP_WS_SYSTEM_STATE_CONNECTING) {
        js_ws_close(l_conn->js_handle, 1000);
    }

    s_unregister_conn(l_conn->js_handle);
    js_ws_destroy(l_conn->js_handle);

    if (l_conn->recv_buf)
        dap_ring_buffer_delete(l_conn->recv_buf);

    log_it(L_INFO, "WebSocket System closed (handle=%d, sent=%" PRIu64 ", recv=%" PRIu64 ")",
           l_conn->js_handle, l_conn->bytes_sent, l_conn->bytes_received);

    DAP_FREE(l_conn);
    a_stream->_server_session = NULL;
}

static uint32_t s_ws_system_get_caps(dap_net_trans_t *a_trans)
{
    (void)a_trans;
    return DAP_NET_TRANS_CAP_RELIABLE
         | DAP_NET_TRANS_CAP_ORDERED
         | DAP_NET_TRANS_CAP_BIDIRECTIONAL;
}

static dap_net_trans_ops_t s_ws_system_ops = {
    .init               = s_ws_system_init,
    .deinit             = s_ws_system_deinit,
    .connect            = s_ws_system_connect,
    .listen             = NULL,
    .accept             = NULL,
    .handshake_init     = NULL,
    .handshake_process  = NULL,
    .session_create     = NULL,
    .session_start      = NULL,
    .read               = s_ws_system_read,
    .write              = s_ws_system_write,
    .close              = s_ws_system_close,
    .get_capabilities   = s_ws_system_get_caps,
    .register_server_handlers = NULL,
    .stage_prepare      = NULL,
    .get_max_packet_size = NULL,
};

/* ========================================================================
 * Public API
 * ======================================================================== */

dap_net_trans_ws_system_config_t dap_net_trans_ws_system_config_default(void)
{
    return (dap_net_trans_ws_system_config_t) {
        .max_message_size  = WS_DEFAULT_MAX_MSG_SIZE,
        .ping_interval_ms  = 0,
        .connect_timeout_ms = 10000,
        .subprotocol       = "dap-stream"
    };
}

int dap_net_trans_websocket_system_register(void)
{
    return dap_net_trans_register(
        "websocket-system",
        DAP_NET_TRANS_WEBSOCKET_SYSTEM,
        &s_ws_system_ops,
        DAP_NET_TRANS_SOCKET_OTHER,
        NULL
    );
}

int dap_net_trans_websocket_system_unregister(void)
{
    return dap_net_trans_unregister(DAP_NET_TRANS_WEBSOCKET_SYSTEM);
}

bool dap_net_trans_is_websocket_system(const dap_stream_t *a_stream)
{
    return a_stream && a_stream->trans &&
           a_stream->trans->type == DAP_NET_TRANS_WEBSOCKET_SYSTEM;
}

#endif /* __EMSCRIPTEN__ */
