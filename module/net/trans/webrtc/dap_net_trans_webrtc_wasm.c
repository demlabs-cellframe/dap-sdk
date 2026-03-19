/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
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
 * @file dap_net_trans_webrtc_wasm.c
 * @brief WebRTC Data Channel transport — WASM/Browser implementation
 *
 * Architecture (same threading model as WebSocket transport):
 *   - RTCPeerConnection + RTCDataChannel live on browser main thread
 *   - Each staged op (handshake_init, session_create, session_start) spawns a
 *     detached pthread so the DAP worker event loop is never blocked
 *   - XHR (enc_init, stream_ctl, signaling) runs synchronously inside those pthreads
 *   - Recv pthread: sem_wait -> cbuf -> dap_stream_data_proc_read_ext
 *   - Write: proxy dataChannel.send() to main thread
 */

#ifdef __EMSCRIPTEN__

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <semaphore.h>

#include <emscripten.h>
#include <emscripten/em_js.h>
#include <emscripten/threading.h>
#include <emscripten/proxying.h>

#include "dap_common.h"
#include "dap_cbuf.h"
#include "dap_base64.h"
#include "dap_enc.h"
#include "dap_enc_key.h"
#include "dap_json.h"
#include "dap_cert.h"
#include "dap_net_trans.h"
#include "dap_net_trans_types.h"
#include "dap_net_trans_webrtc.h"
#include "dap_http_client_simple.h"
#include "dap_stream.h"
#include "dap_stream_session.h"

#define LOG_TAG "webrtc_wasm"

extern int js_http_post_sync(const char *a_url_ptr,
                              const char *a_content_type_ptr,
                              const void *a_body, int a_body_len,
                              const char *a_extra_headers_ptr,
                              int a_out_ptr_addr, int a_out_len_addr);

#define RTC_RECV_BUF_SIZE   (256 * 1024)
#define RTC_READ_CHUNK      (64 * 1024)
#define RTC_MAX_CONNECTIONS 64

/* ========================================================================
 * Connection context
 * ======================================================================== */

typedef struct rtc_conn {
    int                     js_peer_id;
    dap_webrtc_state_t      state;
    dap_cbuf_t              recv_buf;

    dap_stream_t           *stream;
    void                   *client_ctx;

    char                   *host;
    uint16_t                port;
    uint32_t                session_id;

    pthread_t               recv_thread;
    bool                    recv_running;
    pthread_mutex_t         recv_mutex;
    sem_t                   recv_sem;

    uint64_t                bytes_sent;
    uint64_t                bytes_received;
} rtc_conn_t;

static rtc_conn_t *s_connections[RTC_MAX_CONNECTIONS] = {0};

static rtc_conn_t *s_find_conn(int a_id)
{
    if (a_id < 0 || a_id >= RTC_MAX_CONNECTIONS) return NULL;
    return s_connections[a_id];
}

static void s_register_conn(int a_id, rtc_conn_t *a_conn)
{
    if (a_id >= 0 && a_id < RTC_MAX_CONNECTIONS)
        s_connections[a_id] = a_conn;
}

static void s_unregister_conn(int a_id)
{
    if (a_id >= 0 && a_id < RTC_MAX_CONNECTIONS)
        s_connections[a_id] = NULL;
}

/* ========================================================================
 * EM_JS: WebRTC operations (must run on main thread)
 * ======================================================================== */

EM_JS(int, js_rtc_create_peer, (const char *a_stun_ptr), {
    var stun = a_stun_ptr ? UTF8ToString(a_stun_ptr) : "stun:stun.l.google.com:19302";
    if (!Module._rtc_pool) {
        Module._rtc_pool = {};
        Module._rtc_next_id = 1;
    }
    var id = Module._rtc_next_id++;
    var config = {};
    config.iceServers = [{}];
    config.iceServers[0].urls = stun;

    var pc;
    try { pc = new RTCPeerConnection(config); }
    catch (e) { return -1; }

    var entry = {};
    entry.pc = pc;
    entry.dc = null;
    entry.state = 0;
    entry.ice_candidates = [];
    entry.ice_done = false;
    Module._rtc_pool[id] = entry;

    pc.onicecandidate = function(ev) {
        if (ev.candidate) {
            entry.ice_candidates.push(JSON.stringify(ev.candidate));
        } else {
            entry.ice_done = true;
        }
    };

    pc.onconnectionstatechange = function() {
        if (pc.connectionState === "connected") {
            entry.state = 2;
            if (Module.__rtc_on_connected) Module.__rtc_on_connected(id);
        } else if (pc.connectionState === "failed" || pc.connectionState === "closed") {
            entry.state = 5;
            if (Module.__rtc_on_closed) Module.__rtc_on_closed(id);
        }
    };

    return id;
});

EM_JS(int, js_rtc_create_dc, (int a_peer_id, const char *a_label_ptr), {
    var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
    if (!entry) return -1;
    var label = a_label_ptr ? UTF8ToString(a_label_ptr) : "dap-stream";
    var opts = {};
    opts.ordered = true;
    var dc;
    try { dc = entry.pc.createDataChannel(label, opts); }
    catch (e) { return -1; }
    dc.binaryType = "arraybuffer";
    entry.dc = dc;

    dc.onopen = function() {
        entry.state = 2;
        if (Module.__rtc_on_dc_open) Module.__rtc_on_dc_open(a_peer_id);
    };
    dc.onclose = function() {
        if (Module.__rtc_on_dc_close) Module.__rtc_on_dc_close(a_peer_id);
    };
    dc.onmessage = function(ev) {
        var arr = new Uint8Array(ev.data);
        var buf = _malloc(arr.length);
        HEAPU8.set(arr, buf);
        if (Module.__rtc_on_dc_message) Module.__rtc_on_dc_message(a_peer_id, buf, arr.length);
        _free(buf);
    };
    return 0;
});

EM_JS(int, js_rtc_create_offer, (int a_peer_id, int a_out_ptr), {
    var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
    if (!entry) return -1;
    var pc = entry.pc;

    var done = false;
    var result = -1;
    pc.createOffer().then(function(offer) {
        return pc.setLocalDescription(offer);
    }).then(function() {
        var sdp = pc.localDescription.sdp;
        var len = lengthBytesUTF8(sdp) + 1;
        var ptr = _malloc(len);
        stringToUTF8(sdp, ptr, len);
        setValue(a_out_ptr, ptr, '*');
        result = 0;
        done = true;
    }).catch(function(e) {
        result = -1;
        done = true;
    });

    while (!done) {}
    return result;
});

EM_JS(int, js_rtc_set_remote_answer, (int a_peer_id, const char *a_sdp_ptr), {
    var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
    if (!entry) return -1;
    var sdp = UTF8ToString(a_sdp_ptr);

    var done = false;
    var result = -1;
    var desc = {};
    desc.type = "answer";
    desc.sdp = sdp;
    entry.pc.setRemoteDescription(desc).then(function() {
        result = 0;
        done = true;
    }).catch(function(e) {
        result = -1;
        done = true;
    });

    while (!done) {}
    return result;
});

EM_JS(int, js_rtc_add_ice, (int a_peer_id, const char *a_candidate_ptr), {
    var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
    if (!entry) return -1;
    var cand_json = UTF8ToString(a_candidate_ptr);
    var cand = JSON.parse(cand_json);

    var done = false;
    var result = -1;
    entry.pc.addIceCandidate(cand).then(function() {
        result = 0;
        done = true;
    }).catch(function(e) {
        result = -1;
        done = true;
    });

    while (!done) {}
    return result;
});

EM_JS(int, js_rtc_get_ice_candidates, (int a_peer_id, int a_out_ptr), {
    var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
    if (!entry) return -1;
    var json = "[" + entry.ice_candidates.join(",") + "]";
    var len = lengthBytesUTF8(json) + 1;
    var ptr = _malloc(len);
    stringToUTF8(json, ptr, len);
    setValue(a_out_ptr, ptr, '*');
    return entry.ice_candidates.length;
});

EM_JS(int, js_rtc_dc_send, (int a_peer_id, const void *a_data, int a_len), {
    var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
    if (!entry || !entry.dc || entry.dc.readyState !== "open") return -1;
    try {
        entry.dc.send(HEAPU8.slice(a_data, a_data + a_len).buffer);
        return a_len;
    } catch (e) { return -1; }
});

EM_JS(void, js_rtc_close, (int a_peer_id), {
    var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
    if (!entry) return;
    if (entry.dc) try { entry.dc.close(); } catch(e) {}
    try { entry.pc.close(); } catch(e) {}
    delete Module._rtc_pool[a_peer_id];
});

EM_JS(void, js_rtc_init_callbacks, (void), {
    Module.__rtc_on_connected  = Module.cwrap('_rtc_on_connected', null, ['number']);
    Module.__rtc_on_closed     = Module.cwrap('_rtc_on_closed', null, ['number']);
    Module.__rtc_on_dc_open    = Module.cwrap('_rtc_on_dc_open', null, ['number']);
    Module.__rtc_on_dc_close   = Module.cwrap('_rtc_on_dc_close', null, ['number']);
    Module.__rtc_on_dc_message = Module.cwrap('_rtc_on_dc_message', null, ['number', 'number', 'number']);
});

/* ========================================================================
 * C callbacks from JavaScript (run on main thread)
 * ======================================================================== */

EMSCRIPTEN_KEEPALIVE
void _rtc_on_connected(int a_id)
{
    rtc_conn_t *l_conn = s_find_conn(a_id);
    if (l_conn) l_conn->state = DAP_WEBRTC_STATE_CONNECTED;
    log_it(L_NOTICE, "WebRTC peer connected (id=%d)", a_id);
}

EMSCRIPTEN_KEEPALIVE
void _rtc_on_closed(int a_id)
{
    rtc_conn_t *l_conn = s_find_conn(a_id);
    if (!l_conn) return;
    l_conn->state = DAP_WEBRTC_STATE_CLOSED;
    log_it(L_INFO, "WebRTC peer closed (id=%d)", a_id);
    sem_post(&l_conn->recv_sem);
}

EMSCRIPTEN_KEEPALIVE
void _rtc_on_dc_open(int a_id)
{
    rtc_conn_t *l_conn = s_find_conn(a_id);
    if (!l_conn) return;
    l_conn->state = DAP_WEBRTC_STATE_CONNECTED;
    log_it(L_NOTICE, "WebRTC data channel open (id=%d)", a_id);
    sem_post(&l_conn->recv_sem);
}

EMSCRIPTEN_KEEPALIVE
void _rtc_on_dc_close(int a_id)
{
    rtc_conn_t *l_conn = s_find_conn(a_id);
    if (!l_conn) return;
    l_conn->state = DAP_WEBRTC_STATE_CLOSED;
    log_it(L_INFO, "WebRTC data channel closed (id=%d)", a_id);
    sem_post(&l_conn->recv_sem);
}

EMSCRIPTEN_KEEPALIVE
void _rtc_on_dc_message(int a_id, const uint8_t *a_data, int a_len)
{
    rtc_conn_t *l_conn = s_find_conn(a_id);
    if (!l_conn || !l_conn->recv_buf || a_len <= 0) return;
    pthread_mutex_lock(&l_conn->recv_mutex);
    dap_cbuf_push(l_conn->recv_buf, a_data, (size_t)a_len);
    l_conn->bytes_received += (uint64_t)a_len;
    pthread_mutex_unlock(&l_conn->recv_mutex);
    sem_post(&l_conn->recv_sem);
}

/* ========================================================================
 * Proxy wrappers for main-thread JS calls
 * ======================================================================== */

typedef struct { const char *stun; int result; } rtc_create_peer_args_t;
typedef struct { int peer_id; const char *label; int result; } rtc_create_dc_args_t;
typedef struct { int peer_id; char *out; int result; } rtc_offer_args_t;
typedef struct { int peer_id; const char *sdp; int result; } rtc_answer_args_t;
typedef struct { int peer_id; const char *cand; int result; } rtc_ice_args_t;
typedef struct { int peer_id; const void *data; int len; int result; } rtc_send_args_t;
typedef struct { int peer_id; } rtc_close_args_t;

static void s_proxy_create_peer(void *a) { rtc_create_peer_args_t *p = a; p->result = js_rtc_create_peer(p->stun); }
static void s_proxy_create_dc(void *a) { rtc_create_dc_args_t *p = a; p->result = js_rtc_create_dc(p->peer_id, p->label); }
static void s_proxy_create_offer(void *a) { rtc_offer_args_t *p = a; p->result = js_rtc_create_offer(p->peer_id, (int)(uintptr_t)&p->out); }
static void s_proxy_set_answer(void *a) { rtc_answer_args_t *p = a; p->result = js_rtc_set_remote_answer(p->peer_id, p->sdp); }
static void s_proxy_add_ice(void *a) { rtc_ice_args_t *p = a; p->result = js_rtc_add_ice(p->peer_id, p->cand); }
static void s_proxy_get_ice(void *a) { rtc_offer_args_t *p = a; p->result = js_rtc_get_ice_candidates(p->peer_id, (int)(uintptr_t)&p->out); }
static void s_proxy_dc_send(void *a) { rtc_send_args_t *p = a; p->result = js_rtc_dc_send(p->peer_id, p->data, p->len); }
static void s_proxy_close(void *a) { rtc_close_args_t *p = a; js_rtc_close(p->peer_id); }

static void s_proxy_init_callbacks(void *a) { (void)a; js_rtc_init_callbacks(); }

#define RTC_PROXY_SYNC(fn, args) \
    emscripten_proxy_sync(emscripten_proxy_get_system_queue(), \
                          emscripten_main_runtime_thread_id(), \
                          fn, args)

/* ========================================================================
 * Recv thread
 * ======================================================================== */

static void *s_recv_thread_func(void *a_arg)
{
    rtc_conn_t *l_conn = (rtc_conn_t *)a_arg;
    uint8_t l_buf[RTC_READ_CHUNK];

    while (l_conn->recv_running) {
        sem_wait(&l_conn->recv_sem);
        if (!l_conn->recv_running || l_conn->state >= DAP_WEBRTC_STATE_DISCONNECTED)
            break;

        for (;;) {
            pthread_mutex_lock(&l_conn->recv_mutex);
            size_t l_avail = dap_cbuf_get_size(l_conn->recv_buf);
            if (l_avail == 0) {
                pthread_mutex_unlock(&l_conn->recv_mutex);
                break;
            }
            size_t l_chunk = l_avail < sizeof(l_buf) ? l_avail : sizeof(l_buf);
            dap_cbuf_pop(l_conn->recv_buf, l_chunk, l_buf);
            pthread_mutex_unlock(&l_conn->recv_mutex);

            if (l_conn->stream)
                dap_stream_data_proc_read_ext(l_conn->stream, l_buf, l_chunk);
        }
    }
    return NULL;
}

/* ========================================================================
 * Staged transport ops (async — each spawns a detached pthread)
 * ======================================================================== */

/* ========================================================================
 * WebRTC signaling via HTTP REST
 * ======================================================================== */

static int s_do_signaling(rtc_conn_t *a_conn)
{
    rtc_create_peer_args_t l_peer_args = { .stun = NULL, .result = -1 };
    RTC_PROXY_SYNC(s_proxy_create_peer, &l_peer_args);
    if (l_peer_args.result < 0) { log_it(L_ERROR, "RTCPeerConnection creation failed"); return -1; }
    a_conn->js_peer_id = l_peer_args.result;
    s_register_conn(a_conn->js_peer_id, a_conn);

    rtc_create_dc_args_t l_dc_args = { .peer_id = a_conn->js_peer_id, .label = "dap-stream", .result = -1 };
    RTC_PROXY_SYNC(s_proxy_create_dc, &l_dc_args);
    if (l_dc_args.result < 0) { log_it(L_ERROR, "DataChannel creation failed"); return -1; }

    rtc_offer_args_t l_offer_args = { .peer_id = a_conn->js_peer_id, .out = NULL, .result = -1 };
    RTC_PROXY_SYNC(s_proxy_create_offer, &l_offer_args);
    if (l_offer_args.result < 0 || !l_offer_args.out) { log_it(L_ERROR, "SDP offer creation failed"); return -1; }

    char l_url[1024];
    snprintf(l_url, sizeof(l_url), "https://%s:%u/rtc/offer", a_conn->host, a_conn->port);

    void *l_resp = NULL;
    int l_resp_len = 0;
    int l_rc = js_http_post_sync(l_url, "application/sdp",
                                  l_offer_args.out, (int)strlen(l_offer_args.out),
                                  NULL, (int)(uintptr_t)&l_resp,
                                  (int)(uintptr_t)&l_resp_len);
    free(l_offer_args.out);
    if (l_rc != 0 || !l_resp) { log_it(L_ERROR, "SDP offer POST failed: %d", l_rc); return -1; }

    rtc_answer_args_t l_ans_args = { .peer_id = a_conn->js_peer_id, .sdp = (const char *)l_resp, .result = -1 };
    RTC_PROXY_SYNC(s_proxy_set_answer, &l_ans_args);
    free(l_resp);
    if (l_ans_args.result < 0) { log_it(L_ERROR, "setRemoteDescription failed"); return -1; }

    rtc_offer_args_t l_ice_args = { .peer_id = a_conn->js_peer_id, .out = NULL, .result = -1 };
    RTC_PROXY_SYNC(s_proxy_get_ice, &l_ice_args);
    if (l_ice_args.result > 0 && l_ice_args.out) {
        snprintf(l_url, sizeof(l_url), "https://%s:%u/rtc/ice?session_id=%u",
                 a_conn->host, a_conn->port, a_conn->session_id);

        void *l_ice_resp = NULL;
        int l_ice_resp_len = 0;
        l_rc = js_http_post_sync(l_url, "application/json",
                                  l_ice_args.out, (int)strlen(l_ice_args.out),
                                  NULL, (int)(uintptr_t)&l_ice_resp,
                                  (int)(uintptr_t)&l_ice_resp_len);

        if (l_rc == 0 && l_ice_resp && l_ice_resp_len > 2) {
            dap_json_t *l_arr = dap_json_parse_string((const char *)l_ice_resp);
            if (l_arr) {
                /* TODO: iterate JSON array and add remote ICE candidates via proxy */
                dap_json_object_free(l_arr);
            }
        }
        if (l_ice_resp) free(l_ice_resp);
    }
    if (l_ice_args.out) free(l_ice_args.out);

    log_it(L_NOTICE, "WebRTC signaling completed (peer_id=%d)", a_conn->js_peer_id);
    return 0;
}

/* ========================================================================
 * stage_prepare: allocate conn + stream, return esocket=NULL
 * ======================================================================== */

static int s_rtc_stage_prepare(dap_net_trans_t *a_trans,
                               const dap_net_stage_prepare_params_t *a_params,
                               dap_net_stage_prepare_result_t *a_result)
{
    if (!a_trans || !a_params || !a_result) return -1;

    rtc_conn_t *l_conn = DAP_NEW_Z(rtc_conn_t);
    if (!l_conn) { a_result->error_code = -1; return -1; }

    l_conn->recv_buf = dap_cbuf_create(RTC_RECV_BUF_SIZE);
    if (!l_conn->recv_buf) { DAP_DELETE(l_conn); a_result->error_code = -1; return -1; }

    pthread_mutex_init(&l_conn->recv_mutex, NULL);
    sem_init(&l_conn->recv_sem, 0, 0);
    l_conn->js_peer_id = -1;

    l_conn->host = dap_strdup(a_params->host);
    l_conn->port = a_params->port;
    l_conn->client_ctx = a_params->client_ctx;

    dap_stream_t *l_stream = DAP_NEW_Z(dap_stream_t);
    if (!l_stream) {
        DAP_DELETE(l_conn->host);
        dap_cbuf_delete(l_conn->recv_buf);
        sem_destroy(&l_conn->recv_sem);
        pthread_mutex_destroy(&l_conn->recv_mutex);
        DAP_DELETE(l_conn);
        a_result->error_code = -1;
        return -1;
    }

    l_stream->trans = a_trans;
    l_stream->_server_session = l_conn;
    l_conn->stream = l_stream;

    a_result->esocket = NULL;
    a_result->stream = l_stream;
    a_result->error_code = 0;

    log_it(L_DEBUG, "RTC stage_prepare: conn=%p, stream=%p, host=%s:%u",
           (void *)l_conn, (void *)l_stream, a_params->host, a_params->port);
    return 0;
}

/* ========================================================================
 * handshake_init: enc_init XHR via dap_http_client_simple
 * ======================================================================== */

typedef struct {
    dap_stream_t                  *stream;
    dap_net_trans_handshake_cb_t   callback;
} rtc_handshake_ctx_t;

static void s_rtc_handshake_response(void *a_resp, size_t a_resp_size, int a_error, void *a_user_data)
{
    rtc_handshake_ctx_t *l_ctx = (rtc_handshake_ctx_t *)a_user_data;
    if (l_ctx->callback)
        l_ctx->callback(l_ctx->stream, a_resp, a_resp_size, a_error);
    DAP_DELETE(l_ctx);
}

static int s_rtc_handshake_init(dap_stream_t *a_stream,
                                dap_net_handshake_params_t *a_params,
                                dap_net_trans_handshake_cb_t a_callback)
{
    if (!a_stream || !a_params || !a_stream->_server_session) return -1;
    rtc_conn_t *l_conn = (rtc_conn_t *)a_stream->_server_session;

    size_t l_b64_size = DAP_BASE64_ENCODE_SIZE(a_params->alice_pub_key_size) + 1;
    char *l_b64_body = DAP_NEW_Z_SIZE(char, l_b64_size);
    size_t l_b64_len = dap_enc_base64_encode(a_params->alice_pub_key,
                                              a_params->alice_pub_key_size,
                                              l_b64_body, DAP_ENC_DATA_TYPE_B64);

    char l_url[1024];
    snprintf(l_url, sizeof(l_url),
             "https://%s:%u/enc_init/gd4y5yh78w42aaagh"
             "?enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zu"
             ",block_key_size=%zu,protocol_version=%d,sign_count=%zu",
             l_conn->host, l_conn->port,
             a_params->enc_type, a_params->pkey_exchange_type,
             a_params->pkey_exchange_size, a_params->block_key_size,
             a_params->protocol_version, a_params->sign_count);

    rtc_handshake_ctx_t *l_ctx = DAP_NEW_Z(rtc_handshake_ctx_t);
    if (!l_ctx) { DAP_DELETE(l_b64_body); DAP_DELETE(a_params->alice_pub_key); return -1; }
    l_ctx->stream = a_stream;
    l_ctx->callback = a_callback;

    int l_ret = dap_http_client_simple_request(l_url, "text/text",
                                                l_b64_body, l_b64_len, NULL,
                                                s_rtc_handshake_response, l_ctx);
    DAP_DELETE(l_b64_body);
    DAP_DELETE(a_params->alice_pub_key);

    if (l_ret != 0) { DAP_DELETE(l_ctx); return -1; }
    return 0;
}

/* ========================================================================
 * session_create: stream_ctl XHR via dap_http_client_simple
 * ======================================================================== */

typedef struct {
    dap_stream_t                 *stream;
    rtc_conn_t                   *conn;
    dap_enc_key_t                *session_key;
    dap_net_trans_session_cb_t    callback;
} rtc_session_create_ctx_t;

static void s_rtc_session_create_response(void *a_resp, size_t a_resp_size, int a_error, void *a_user_data)
{
    rtc_session_create_ctx_t *l_ctx = (rtc_session_create_ctx_t *)a_user_data;

    if (a_error != 0 || !a_resp || a_resp_size == 0) {
        log_it(L_ERROR, "RTC stream_ctl XHR failed: %d", a_error);
        if (l_ctx->callback) l_ctx->callback(l_ctx->stream, 0, NULL, 0, -1);
        DAP_DELETE(l_ctx);
        return;
    }

    size_t l_dec_max = a_resp_size + 256;
    char *l_dec = DAP_NEW_Z_SIZE(char, l_dec_max);
    size_t l_dec_len = dap_enc_decode(l_ctx->session_key, a_resp, a_resp_size,
                                       l_dec, l_dec_max, DAP_ENC_DATA_TYPE_RAW);
    if (l_dec_len == 0) {
        log_it(L_ERROR, "RTC stream_ctl decryption failed");
        DAP_DELETE(l_dec);
        if (l_ctx->callback) l_ctx->callback(l_ctx->stream, 0, NULL, 0, -1);
        DAP_DELETE(l_ctx);
        return;
    }
    l_dec[l_dec_len] = '\0';

    uint32_t l_session_id = 0;
    sscanf(l_dec, "%u", &l_session_id);
    l_ctx->conn->session_id = l_session_id;

    log_it(L_NOTICE, "RTC stream_ctl ok, session_id=%u", l_session_id);
    if (l_ctx->callback) l_ctx->callback(l_ctx->stream, l_session_id, l_dec, l_dec_len, 0);
    DAP_DELETE(l_ctx);
}

static int s_rtc_session_create(dap_stream_t *a_stream,
                                dap_net_session_params_t *a_params,
                                dap_net_trans_session_cb_t a_callback)
{
    if (!a_stream || !a_params || !a_stream->_server_session) return -1;
    rtc_conn_t *l_conn = (rtc_conn_t *)a_stream->_server_session;
    dap_enc_key_t *l_key = a_params->session_key;
    const char *l_key_id = a_params->session_key_id;

    if (!l_key || !l_key_id) {
        log_it(L_ERROR, "RTC stream_ctl: no session key");
        if (a_callback) a_callback(a_stream, 0, NULL, 0, -1);
        return 0;
    }

    char l_sub_plain[512];
    snprintf(l_sub_plain, sizeof(l_sub_plain),
             "channels=%s,enc_type=%d,enc_key_size=%zu,enc_headers=%d",
             a_params->channels ? a_params->channels : "A",
             a_params->enc_type, a_params->enc_key_size,
             a_params->enc_headers ? 1 : 0);

    const char *l_query_plain = "type=tcp,maxconn=4";
    char l_body_plain[32];
    snprintf(l_body_plain, sizeof(l_body_plain), "%u", a_params->protocol_version);

    size_t l_sub_max = dap_enc_code_out_size(l_key, strlen(l_sub_plain), DAP_ENC_DATA_TYPE_B64_URLSAFE);
    char *l_sub_enc = DAP_NEW_Z_SIZE(char, l_sub_max + 1);
    size_t l_sub_len = dap_enc_code(l_key, l_sub_plain, strlen(l_sub_plain),
                                     l_sub_enc, l_sub_max, DAP_ENC_DATA_TYPE_B64_URLSAFE);

    size_t l_q_max = dap_enc_code_out_size(l_key, strlen(l_query_plain), DAP_ENC_DATA_TYPE_B64_URLSAFE);
    char *l_q_enc = DAP_NEW_Z_SIZE(char, l_q_max + 1);
    size_t l_q_len = dap_enc_code(l_key, l_query_plain, strlen(l_query_plain),
                                   l_q_enc, l_q_max, DAP_ENC_DATA_TYPE_B64_URLSAFE);

    size_t l_b_max = dap_enc_code_out_size(l_key, strlen(l_body_plain), DAP_ENC_DATA_TYPE_RAW);
    uint8_t *l_b_enc = DAP_NEW_Z_SIZE(uint8_t, l_b_max + 1);
    size_t l_b_len = dap_enc_code(l_key, l_body_plain, strlen(l_body_plain),
                                   l_b_enc, l_b_max, DAP_ENC_DATA_TYPE_RAW);

    l_sub_enc[l_sub_len] = '\0';
    l_q_enc[l_q_len] = '\0';
    char l_url[2048];
    snprintf(l_url, sizeof(l_url), "https://%s:%u/stream_ctl/%s?%s",
             l_conn->host, l_conn->port, l_sub_enc, l_q_enc);
    DAP_DELETE(l_sub_enc);
    DAP_DELETE(l_q_enc);

    char l_headers[512];
    snprintf(l_headers, sizeof(l_headers),
             "KeyID: %s\r\nSessionCloseAfterRequest: true", l_key_id);

    rtc_session_create_ctx_t *l_ctx = DAP_NEW_Z(rtc_session_create_ctx_t);
    if (!l_ctx) { DAP_DELETE(l_b_enc); return -1; }
    l_ctx->stream      = a_stream;
    l_ctx->conn        = l_conn;
    l_ctx->session_key = l_key;
    l_ctx->callback    = a_callback;

    int l_ret = dap_http_client_simple_request(l_url, "application/octet-stream",
                                                l_b_enc, l_b_len, l_headers,
                                                s_rtc_session_create_response, l_ctx);
    DAP_DELETE(l_b_enc);
    if (l_ret != 0) { DAP_DELETE(l_ctx); return -1; }
    return 0;
}

/* ========================================================================
 * session_start: async pthread does signaling + waits for DC open
 * ======================================================================== */

typedef struct {
    dap_stream_t              *stream;
    rtc_conn_t                *conn;
    uint32_t                   session_id;
    dap_net_trans_ready_cb_t   callback;
} rtc_session_start_args_t;

static void *s_rtc_session_start_thread(void *a_arg)
{
    rtc_session_start_args_t *l_a = (rtc_session_start_args_t *)a_arg;
    rtc_conn_t *l_conn = l_a->conn;

    if (s_do_signaling(l_conn) != 0) {
        log_it(L_ERROR, "WebRTC signaling failed");
        if (l_a->callback) l_a->callback(l_a->stream, -1);
        DAP_DELETE(l_a);
        return NULL;
    }

    l_conn->state = DAP_WEBRTC_STATE_CONNECTING;
    sem_wait(&l_conn->recv_sem);

    if (l_conn->state != DAP_WEBRTC_STATE_CONNECTED) {
        log_it(L_ERROR, "WebRTC data channel didn't open (state=%d)", l_conn->state);
        s_unregister_conn(l_conn->js_peer_id);
        rtc_close_args_t cl = { .peer_id = l_conn->js_peer_id };
        RTC_PROXY_SYNC(s_proxy_close, &cl);
        if (l_a->callback) l_a->callback(l_a->stream, -2);
        DAP_DELETE(l_a);
        return NULL;
    }

    l_conn->recv_running = true;
    pthread_create(&l_conn->recv_thread, NULL, s_recv_thread_func, l_conn);

    log_it(L_NOTICE, "WebRTC streaming started (session_id=%u, peer_id=%d)",
           l_a->session_id, l_conn->js_peer_id);
    if (l_a->callback) l_a->callback(l_a->stream, 0);
    DAP_DELETE(l_a);
    return NULL;
}

static int s_rtc_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                               dap_net_trans_ready_cb_t a_callback)
{
    if (!a_stream || !a_stream->_server_session) return -1;

    rtc_session_start_args_t *l_args = DAP_NEW_Z(rtc_session_start_args_t);
    if (!l_args) return -1;
    l_args->stream     = a_stream;
    l_args->conn       = (rtc_conn_t *)a_stream->_server_session;
    l_args->session_id = a_session_id;
    l_args->callback   = a_callback;

    pthread_t l_thread;
    pthread_attr_t l_attr;
    pthread_attr_init(&l_attr);
    pthread_attr_setdetachstate(&l_attr, PTHREAD_CREATE_DETACHED);
    int l_ret = pthread_create(&l_thread, &l_attr, s_rtc_session_start_thread, l_args);
    pthread_attr_destroy(&l_attr);
    if (l_ret != 0) {
        log_it(L_ERROR, "RTC session_start: pthread_create failed: %d", l_ret);
        DAP_DELETE(l_args);
        return -1;
    }
    return 0;
}

static void *s_rtc_get_client_context(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->_server_session) return NULL;
    rtc_conn_t *l_conn = (rtc_conn_t *)a_stream->_server_session;
    return l_conn->client_ctx;
}

/* ========================================================================
 * Transport ops
 * ======================================================================== */

static int s_rtc_init(dap_net_trans_t *a_trans, dap_config_t *a_config)
{
    (void)a_trans; (void)a_config;
    RTC_PROXY_SYNC(s_proxy_init_callbacks, NULL);
    log_it(L_NOTICE, "WebRTC transport initialized (WASM)");
    return 0;
}

static void s_rtc_deinit(dap_net_trans_t *a_trans)
{
    (void)a_trans;
    for (int i = 0; i < RTC_MAX_CONNECTIONS; i++) {
        rtc_conn_t *l_conn = s_connections[i];
        if (!l_conn) continue;
        if (l_conn->recv_running) {
            l_conn->recv_running = false;
            sem_post(&l_conn->recv_sem);
            pthread_join(l_conn->recv_thread, NULL);
        }
        rtc_close_args_t cl = { .peer_id = i };
        RTC_PROXY_SYNC(s_proxy_close, &cl);
        dap_cbuf_delete(l_conn->recv_buf);
        DAP_DEL_Z(l_conn->host);
        sem_destroy(&l_conn->recv_sem);
        pthread_mutex_destroy(&l_conn->recv_mutex);
        DAP_DELETE(l_conn);
        s_connections[i] = NULL;
    }
}

static ssize_t s_rtc_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_stream->_server_session) return -1;
    rtc_conn_t *l_conn = (rtc_conn_t *)a_stream->_server_session;
    if (!l_conn->recv_buf) return -1;

    pthread_mutex_lock(&l_conn->recv_mutex);
    size_t l_avail = dap_cbuf_get_size(l_conn->recv_buf);
    if (l_avail == 0) { pthread_mutex_unlock(&l_conn->recv_mutex); return 0; }
    size_t l_r = a_size < l_avail ? a_size : l_avail;
    size_t l_read = dap_cbuf_pop(l_conn->recv_buf, l_r, a_buffer);
    pthread_mutex_unlock(&l_conn->recv_mutex);
    return (ssize_t)l_read;
}

static ssize_t s_rtc_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_stream->_server_session) return -1;
    rtc_conn_t *l_conn = (rtc_conn_t *)a_stream->_server_session;
    if (l_conn->state != DAP_WEBRTC_STATE_CONNECTED) return -1;

    rtc_send_args_t l_args = { .peer_id = l_conn->js_peer_id, .data = a_data, .len = (int)a_size, .result = -1 };
    RTC_PROXY_SYNC(s_proxy_dc_send, &l_args);
    if (l_args.result > 0)
        l_conn->bytes_sent += (uint64_t)l_args.result;
    return (ssize_t)l_args.result;
}

static void s_rtc_close(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->_server_session) return;
    rtc_conn_t *l_conn = (rtc_conn_t *)a_stream->_server_session;

    if (l_conn->recv_running) {
        l_conn->recv_running = false;
        sem_post(&l_conn->recv_sem);
        pthread_join(l_conn->recv_thread, NULL);
    }

    s_unregister_conn(l_conn->js_peer_id);
    rtc_close_args_t cl = { .peer_id = l_conn->js_peer_id };
    RTC_PROXY_SYNC(s_proxy_close, &cl);

    dap_cbuf_delete(l_conn->recv_buf);

    log_it(L_INFO, "WebRTC closed (peer_id=%d, sent=%" PRIu64 ", recv=%" PRIu64 ")",
           l_conn->js_peer_id, l_conn->bytes_sent, l_conn->bytes_received);

    DAP_DEL_Z(l_conn->host);
    sem_destroy(&l_conn->recv_sem);
    pthread_mutex_destroy(&l_conn->recv_mutex);
    DAP_DELETE(l_conn);
    a_stream->_server_session = NULL;
}

static uint32_t s_rtc_get_caps(dap_net_trans_t *a_trans)
{
    (void)a_trans;
    return DAP_NET_TRANS_CAP_RELIABLE
         | DAP_NET_TRANS_CAP_ORDERED
         | DAP_NET_TRANS_CAP_BIDIRECTIONAL
         | DAP_NET_TRANS_CAP_LOW_LATENCY;
}

static dap_net_trans_ops_t s_rtc_ops = {
    .init               = s_rtc_init,
    .deinit             = s_rtc_deinit,
    .connect            = NULL,
    .listen             = NULL,
    .accept             = NULL,
    .handshake_init     = s_rtc_handshake_init,
    .handshake_process  = NULL,
    .session_create     = s_rtc_session_create,
    .session_start      = s_rtc_session_start,
    .read               = s_rtc_read,
    .write              = s_rtc_write,
    .close              = s_rtc_close,
    .get_capabilities   = s_rtc_get_caps,
    .register_server_handlers = NULL,
    .stage_prepare      = s_rtc_stage_prepare,
    .get_client_context = s_rtc_get_client_context,
    .get_max_packet_size = NULL,
};

/* ========================================================================
 * Public API
 * ======================================================================== */

dap_net_trans_webrtc_config_t dap_net_trans_webrtc_config_default(void)
{
    return (dap_net_trans_webrtc_config_t) {
        .stun_server     = "stun:stun.l.google.com:19302",
        .turn_server     = NULL,
        .turn_username   = NULL,
        .turn_credential = NULL,
        .ordered         = true,
        .max_retransmits = -1
    };
}

int dap_net_trans_webrtc_register(void)
{
    return dap_net_trans_register("webrtc", DAP_NET_TRANS_WEBRTC,
                                  &s_rtc_ops, DAP_NET_TRANS_SOCKET_OTHER, NULL);
}

int dap_net_trans_webrtc_unregister(void)
{
    return dap_net_trans_unregister(DAP_NET_TRANS_WEBRTC);
}

bool dap_net_trans_is_webrtc(const dap_stream_t *a_stream)
{
    return a_stream && a_stream->trans && a_stream->trans->type == DAP_NET_TRANS_WEBRTC;
}

#endif /* __EMSCRIPTEN__ */
