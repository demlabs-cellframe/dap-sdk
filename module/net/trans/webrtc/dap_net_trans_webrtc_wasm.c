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
 * @brief WebRTC Data Channel transport — dual mode MT/ST
 */

#ifdef __EMSCRIPTEN__

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <emscripten.h>

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

#ifdef DAP_OS_WASM_MT
#include <pthread.h>
#include <semaphore.h>
#include <emscripten/threading.h>
#include <emscripten/proxying.h>
#endif

extern int js_rtc_create_peer(const char *a_stun_ptr);
extern int js_rtc_create_dc(int a_peer_id, const char *a_label_ptr);
extern int js_rtc_create_offer(int a_peer_id, int a_out_ptr);
extern int js_rtc_set_remote_answer(int a_peer_id, const char *a_sdp_ptr);
extern int js_rtc_add_ice(int a_peer_id, const char *a_candidate_ptr);
extern int js_rtc_get_ice_candidates(int a_peer_id, int a_out_ptr);
extern int js_rtc_dc_send(int a_peer_id, const void *a_data, int a_len);
extern void js_rtc_close(int a_peer_id);
extern void js_rtc_init_callbacks(void);

#ifdef DAP_OS_WASM_MT
extern int js_http_post_sync(const char *a_url_ptr, const char *a_content_type_ptr,
                              const void *a_body, int a_body_len,
                              const char *a_extra_headers_ptr,
                              int a_out_ptr_addr, int a_out_len_addr);
#endif

#define RTC_RECV_BUF_SIZE   (256 * 1024)
#define RTC_READ_CHUNK      (64 * 1024)
#define RTC_MAX_CONNECTIONS 64

/* ========================================================================
 * Connection context
 * ======================================================================== */

EM_JS(int, js_rtc_page_is_secure, (void), {
    if (typeof location === 'undefined') return 0;
    // Main thread: check protocol directly
    if (location.protocol === 'https:') return 1;
    // Worker with blob: URL — parse the origin from blob:https://...
    if (location.protocol === 'blob:' && location.href) {
        if (location.href.startsWith('blob:https://')) return 1;
    }
    return 0;
});

#ifdef DAP_OS_WASM_MT
static int s_rtc_main_document_https = -1;
#endif

typedef struct rtc_conn {
    int                     js_peer_id;
    dap_webrtc_state_t      state;
    dap_cbuf_t              recv_buf;

    dap_stream_t           *stream;
    void                   *client_ctx;

    char                   *host;
    uint16_t                port;
    bool                    use_tls;
    uint32_t                session_id;

#ifdef DAP_OS_WASM_MT
    pthread_mutex_t         recv_mutex;
    sem_t                   recv_sem;
    pthread_t               recv_thread;
    bool                    recv_running;
#endif

    dap_net_trans_ready_cb_t  ready_callback;

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
#ifdef DAP_OS_WASM_MT
    sem_post(&l_conn->recv_sem);
#else
    if (l_conn->ready_callback) {
        l_conn->ready_callback(l_conn->stream, -2);
        l_conn->ready_callback = NULL;
    }
#endif
}

EMSCRIPTEN_KEEPALIVE
void _rtc_on_dc_open(int a_id)
{
    rtc_conn_t *l_conn = s_find_conn(a_id);
    if (!l_conn) return;
    l_conn->state = DAP_WEBRTC_STATE_CONNECTED;
    log_it(L_NOTICE, "WebRTC data channel open (id=%d)", a_id);
#ifdef DAP_OS_WASM_MT
    sem_post(&l_conn->recv_sem);
#else
    if (l_conn->ready_callback) {
        l_conn->ready_callback(l_conn->stream, 0);
        l_conn->ready_callback = NULL;
    }
#endif
}

EMSCRIPTEN_KEEPALIVE
void _rtc_on_dc_close(int a_id)
{
    rtc_conn_t *l_conn = s_find_conn(a_id);
    if (!l_conn) return;
    l_conn->state = DAP_WEBRTC_STATE_CLOSED;
    log_it(L_INFO, "WebRTC data channel closed (id=%d)", a_id);
#ifdef DAP_OS_WASM_MT
    sem_post(&l_conn->recv_sem);
#else
    if (l_conn->ready_callback) {
        l_conn->ready_callback(l_conn->stream, -2);
        l_conn->ready_callback = NULL;
    }
#endif
}

EMSCRIPTEN_KEEPALIVE
void _rtc_on_dc_message(int a_id, const uint8_t *a_data, int a_len)
{
    rtc_conn_t *l_conn = s_find_conn(a_id);
    if (!l_conn || a_len <= 0) return;
    l_conn->bytes_received += (uint64_t)a_len;

#ifdef DAP_OS_WASM_MT
    if (!l_conn->recv_buf) return;
    pthread_mutex_lock(&l_conn->recv_mutex);
    dap_cbuf_push(l_conn->recv_buf, a_data, (size_t)a_len);
    pthread_mutex_unlock(&l_conn->recv_mutex);
    sem_post(&l_conn->recv_sem);
#else
    if (l_conn->stream)
        dap_stream_data_proc_read_ext(l_conn->stream, a_data, (size_t)a_len);
#endif
}

/* ========================================================================
 * MT: proxy wrappers + recv thread + sync signaling
 * ======================================================================== */

#ifdef DAP_OS_WASM_MT

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

#define RTC_PROXY_SYNC(fn, args) do { \
    if (pthread_equal(pthread_self(), emscripten_main_runtime_thread_id())) \
        fn(args); \
    else \
        emscripten_proxy_sync(emscripten_proxy_get_system_queue(), \
                              emscripten_main_runtime_thread_id(), fn, args); \
} while (0)

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
            if (l_avail == 0) { pthread_mutex_unlock(&l_conn->recv_mutex); break; }
            size_t l_chunk = l_avail < sizeof(l_buf) ? l_avail : sizeof(l_buf);
            dap_cbuf_pop(l_conn->recv_buf, l_chunk, l_buf);
            pthread_mutex_unlock(&l_conn->recv_mutex);

            if (l_conn->stream)
                dap_stream_data_proc_read_ext(l_conn->stream, l_buf, l_chunk);
        }
    }
    return NULL;
}

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
    snprintf(l_url, sizeof(l_url), "%s://%s:%u/rtc/offer",
             a_conn->use_tls ? "https" : "http", a_conn->host, a_conn->port);

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
        snprintf(l_url, sizeof(l_url), "%s://%s:%u/rtc/ice?session_id=%u",
                 a_conn->use_tls ? "https" : "http",
                 a_conn->host, a_conn->port, a_conn->session_id);
        void *l_ice_resp = NULL;
        int l_ice_resp_len = 0;
        js_http_post_sync(l_url, "application/json",
                          l_ice_args.out, (int)strlen(l_ice_args.out),
                          NULL, (int)(uintptr_t)&l_ice_resp, (int)(uintptr_t)&l_ice_resp_len);
        if (l_ice_resp) free(l_ice_resp);
    }
    if (l_ice_args.out) free(l_ice_args.out);

    log_it(L_NOTICE, "WebRTC signaling completed (peer_id=%d)", a_conn->js_peer_id);
    return 0;
}

#endif /* DAP_OS_WASM_MT */

/* ========================================================================
 * Transport ops
 * ======================================================================== */

static int s_rtc_init(dap_net_trans_t *a_trans, dap_config_t *a_config)
{
    (void)a_trans; (void)a_config;
#ifdef DAP_OS_WASM_MT
    if (pthread_equal(pthread_self(), emscripten_main_runtime_thread_id())) {
        s_proxy_init_callbacks(NULL);
    } else {
        RTC_PROXY_SYNC(s_proxy_init_callbacks, NULL);
    }
    log_it(L_NOTICE, "WebRTC transport initialized (multi-threaded)");
#else
    js_rtc_init_callbacks();
    log_it(L_NOTICE, "WebRTC transport initialized (single-threaded)");
#endif
    return 0;
}

static void s_rtc_deinit(dap_net_trans_t *a_trans)
{
    (void)a_trans;
    for (int i = 0; i < RTC_MAX_CONNECTIONS; i++) {
        rtc_conn_t *l_conn = s_connections[i];
        if (!l_conn) continue;
#ifdef DAP_OS_WASM_MT
        if (l_conn->recv_running) {
            l_conn->recv_running = false;
            sem_post(&l_conn->recv_sem);
            pthread_join(l_conn->recv_thread, NULL);
        }
        rtc_close_args_t cl = { .peer_id = i };
        RTC_PROXY_SYNC(s_proxy_close, &cl);
        sem_destroy(&l_conn->recv_sem);
        pthread_mutex_destroy(&l_conn->recv_mutex);
#else
        js_rtc_close(i);
#endif
        dap_cbuf_delete(l_conn->recv_buf);
        DAP_DEL_Z(l_conn->host);
        DAP_DELETE(l_conn);
        s_connections[i] = NULL;
    }
}

static int s_rtc_stage_prepare(dap_net_trans_t *a_trans,
                               const dap_net_stage_prepare_params_t *a_params,
                               dap_net_stage_prepare_result_t *a_result)
{
    if (!a_trans || !a_params || !a_result) return -1;

    rtc_conn_t *l_conn = DAP_NEW_Z(rtc_conn_t);
    if (!l_conn) { a_result->error_code = -1; return -1; }

    l_conn->recv_buf = dap_cbuf_create(RTC_RECV_BUF_SIZE);
    if (!l_conn->recv_buf) { DAP_DELETE(l_conn); a_result->error_code = -1; return -1; }

#ifdef DAP_OS_WASM_MT
    pthread_mutex_init(&l_conn->recv_mutex, NULL);
    sem_init(&l_conn->recv_sem, 0, 0);
#endif
    l_conn->js_peer_id = -1;

    l_conn->host = dap_strdup(a_params->host);
    l_conn->port = a_params->port;
#ifdef DAP_OS_WASM_MT
    if (s_rtc_main_document_https < 0) {
        log_it(L_WARNING, "WebRTC: HTTPS detection not initialized, assuming secure context");
        s_rtc_main_document_https = 1;
    }
    l_conn->use_tls = (a_params->port == 443) || (s_rtc_main_document_https > 0);
    log_it(L_DEBUG, "RTC connect: port=%u, https_flag=%d, use_tls=%d",
           a_params->port, s_rtc_main_document_https, l_conn->use_tls);
#else
    l_conn->use_tls = (a_params->port == 443) || js_rtc_page_is_secure();
#endif
    l_conn->client_ctx = a_params->client_ctx;

    dap_stream_t *l_stream = DAP_NEW_Z(dap_stream_t);
    if (!l_stream) {
        DAP_DELETE(l_conn->host);
        dap_cbuf_delete(l_conn->recv_buf);
#ifdef DAP_OS_WASM_MT
        sem_destroy(&l_conn->recv_sem);
        pthread_mutex_destroy(&l_conn->recv_mutex);
#endif
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
    return 0;
}

/* ========================================================================
 * Handshake + session_create: use dap_http_client_simple (works in both modes)
 * ======================================================================== */

typedef struct {
    dap_stream_t                  *stream;
    dap_net_trans_handshake_cb_t   callback;
} rtc_handshake_ctx_t;

static void s_rtc_handshake_response(void *a_resp, size_t a_resp_size, int a_error, void *a_user_data)
{
    rtc_handshake_ctx_t *l_ctx = (rtc_handshake_ctx_t *)a_user_data;
    if (l_ctx->callback) l_ctx->callback(l_ctx->stream, a_resp, a_resp_size, a_error);
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

    const char *l_scheme = l_conn->use_tls ? "https" : "http";
    char l_url[1024];
    snprintf(l_url, sizeof(l_url),
             "%s://%s:%u/enc_init/gd4y5yh78w42aaagh"
             "?enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zu"
             ",block_key_size=%zu,protocol_version=%d,sign_count=%zu",
             l_scheme, l_conn->host, l_conn->port,
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
    dap_enc_code(l_key, l_sub_plain, strlen(l_sub_plain), l_sub_enc, l_sub_max, DAP_ENC_DATA_TYPE_B64_URLSAFE);

    size_t l_q_max = dap_enc_code_out_size(l_key, strlen(l_query_plain), DAP_ENC_DATA_TYPE_B64_URLSAFE);
    char *l_q_enc = DAP_NEW_Z_SIZE(char, l_q_max + 1);
    dap_enc_code(l_key, l_query_plain, strlen(l_query_plain), l_q_enc, l_q_max, DAP_ENC_DATA_TYPE_B64_URLSAFE);

    size_t l_b_max = dap_enc_code_out_size(l_key, strlen(l_body_plain), DAP_ENC_DATA_TYPE_RAW);
    uint8_t *l_b_enc = DAP_NEW_Z_SIZE(uint8_t, l_b_max + 1);
    size_t l_b_len = dap_enc_code(l_key, l_body_plain, strlen(l_body_plain),
                                   l_b_enc, l_b_max, DAP_ENC_DATA_TYPE_RAW);

    char l_url[2048];
    snprintf(l_url, sizeof(l_url), "%s://%s:%u/stream_ctl/%s?%s",
             l_conn->use_tls ? "https" : "http",
             l_conn->host, l_conn->port, l_sub_enc, l_q_enc);
    DAP_DELETE(l_sub_enc);
    DAP_DELETE(l_q_enc);

    char l_headers[512];
    snprintf(l_headers, sizeof(l_headers), "KeyID: %s\r\nSessionCloseAfterRequest: true", l_key_id);

    rtc_session_create_ctx_t *l_ctx = DAP_NEW_Z(rtc_session_create_ctx_t);
    if (!l_ctx) { DAP_DELETE(l_b_enc); return -1; }
    l_ctx->stream = a_stream;
    l_ctx->conn = l_conn;
    l_ctx->session_key = l_key;
    l_ctx->callback = a_callback;

    int l_ret = dap_http_client_simple_request(l_url, "application/octet-stream",
                                                l_b_enc, l_b_len, l_headers,
                                                s_rtc_session_create_response, l_ctx);
    DAP_DELETE(l_b_enc);
    if (l_ret != 0) { DAP_DELETE(l_ctx); return -1; }
    return 0;
}

/* ========================================================================
 * session_start
 * ======================================================================== */

#ifdef DAP_OS_WASM_MT

typedef struct {
    dap_stream_t *stream; rtc_conn_t *conn;
    uint32_t session_id; dap_net_trans_ready_cb_t callback;
} rtc_session_start_args_t;

static void *s_rtc_session_start_thread(void *a_arg)
{
    rtc_session_start_args_t *l_a = (rtc_session_start_args_t *)a_arg;
    rtc_conn_t *l_conn = l_a->conn;

    if (s_do_signaling(l_conn) != 0) {
        if (l_a->callback) l_a->callback(l_a->stream, -1);
        DAP_DELETE(l_a);
        return NULL;
    }

    l_conn->state = DAP_WEBRTC_STATE_CONNECTING;
    sem_wait(&l_conn->recv_sem);

    if (l_conn->state != DAP_WEBRTC_STATE_CONNECTED) {
        s_unregister_conn(l_conn->js_peer_id);
        rtc_close_args_t cl = { .peer_id = l_conn->js_peer_id };
        RTC_PROXY_SYNC(s_proxy_close, &cl);
        if (l_a->callback) l_a->callback(l_a->stream, -2);
        DAP_DELETE(l_a);
        return NULL;
    }

    l_conn->recv_running = true;
    pthread_create(&l_conn->recv_thread, NULL, s_recv_thread_func, l_conn);

    log_it(L_NOTICE, "WebRTC streaming started (peer_id=%d)", l_conn->js_peer_id);
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
    l_args->stream = a_stream;
    l_args->conn = (rtc_conn_t *)a_stream->_server_session;
    l_args->session_id = a_session_id;
    l_args->callback = a_callback;

    pthread_t l_thread;
    pthread_attr_t l_attr;
    pthread_attr_init(&l_attr);
    pthread_attr_setdetachstate(&l_attr, PTHREAD_CREATE_DETACHED);
    int l_ret = pthread_create(&l_thread, &l_attr, s_rtc_session_start_thread, l_args);
    pthread_attr_destroy(&l_attr);
    if (l_ret != 0) { DAP_DELETE(l_args); return -1; }
    return 0;
}

#else /* ST mode: signaling via async JS + async HTTP, fully event-driven */

extern void js_rtc_create_offer_async(int a_peer_id);
extern void js_rtc_set_answer_async(int a_peer_id, const char *a_sdp_ptr);

static void s_st_signaling_offer_response(void *a_resp, size_t a_resp_size, int a_error, void *a_user_data);

static int s_rtc_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                               dap_net_trans_ready_cb_t a_callback)
{
    if (!a_stream || !a_stream->_server_session) return -1;
    rtc_conn_t *l_conn = (rtc_conn_t *)a_stream->_server_session;

    int l_peer = js_rtc_create_peer(NULL);
    if (l_peer < 0) { if (a_callback) a_callback(a_stream, -1); return 0; }
    l_conn->js_peer_id = l_peer;
    s_register_conn(l_peer, l_conn);

    if (js_rtc_create_dc(l_peer, "dap-stream") < 0) {
        if (a_callback) a_callback(a_stream, -1); return 0;
    }

    l_conn->ready_callback = a_callback;
    l_conn->state = DAP_WEBRTC_STATE_CONNECTING;

    js_rtc_create_offer_async(l_peer);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void _rtc_offer_async_callback(int a_peer_id, char *a_sdp_ptr, int a_status)
{
    rtc_conn_t *l_conn = s_find_conn(a_peer_id);
    if (!l_conn) { if (a_sdp_ptr) free(a_sdp_ptr); return; }

    if (a_status != 0 || !a_sdp_ptr) {
        log_it(L_ERROR, "RTC async createOffer failed (peer_id=%d)", a_peer_id);
        if (l_conn->ready_callback) { l_conn->ready_callback(l_conn->stream, -1); l_conn->ready_callback = NULL; }
        return;
    }

    char l_url[1024];
    snprintf(l_url, sizeof(l_url), "%s://%s:%u/rtc/offer",
             l_conn->use_tls ? "https" : "http", l_conn->host, l_conn->port);

    int l_ret = dap_http_client_simple_request(l_url, "application/sdp",
                                                a_sdp_ptr, (int)strlen(a_sdp_ptr), NULL,
                                                s_st_signaling_offer_response, l_conn);
    free(a_sdp_ptr);
    if (l_ret != 0) {
        if (l_conn->ready_callback) { l_conn->ready_callback(l_conn->stream, -1); l_conn->ready_callback = NULL; }
    }
}

static void s_st_signaling_offer_response(void *a_resp, size_t a_resp_size, int a_error, void *a_user_data)
{
    rtc_conn_t *l_conn = (rtc_conn_t *)a_user_data;
    if (a_error != 0 || !a_resp) {
        log_it(L_ERROR, "RTC signaling offer POST failed: %d", a_error);
        if (l_conn->ready_callback) { l_conn->ready_callback(l_conn->stream, -1); l_conn->ready_callback = NULL; }
        return;
    }

    js_rtc_set_answer_async(l_conn->js_peer_id, (const char *)a_resp);
}

EMSCRIPTEN_KEEPALIVE
void _rtc_answer_async_callback(int a_peer_id, int a_status)
{
    rtc_conn_t *l_conn = s_find_conn(a_peer_id);
    if (!l_conn) return;

    if (a_status != 0) {
        log_it(L_ERROR, "setRemoteDescription failed (peer_id=%d)", a_peer_id);
        if (l_conn->ready_callback) { l_conn->ready_callback(l_conn->stream, -1); l_conn->ready_callback = NULL; }
        return;
    }

    log_it(L_NOTICE, "WebRTC signaling completed (ST, peer_id=%d), waiting for DC open...",
           l_conn->js_peer_id);
}

#endif /* DAP_OS_WASM_MT */

/* ========================================================================
 * read / write / close
 * ======================================================================== */

static void *s_rtc_get_client_context(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->_server_session) return NULL;
    return ((rtc_conn_t *)a_stream->_server_session)->client_ctx;
}

static ssize_t s_rtc_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_stream->_server_session) return -1;
    rtc_conn_t *l_conn = (rtc_conn_t *)a_stream->_server_session;
    if (!l_conn->recv_buf) return -1;

#ifdef DAP_OS_WASM_MT
    pthread_mutex_lock(&l_conn->recv_mutex);
#endif
    size_t l_avail = dap_cbuf_get_size(l_conn->recv_buf);
    if (l_avail == 0) {
#ifdef DAP_OS_WASM_MT
        pthread_mutex_unlock(&l_conn->recv_mutex);
#endif
        return 0;
    }
    size_t l_r = a_size < l_avail ? a_size : l_avail;
    size_t l_read = dap_cbuf_pop(l_conn->recv_buf, l_r, a_buffer);
#ifdef DAP_OS_WASM_MT
    pthread_mutex_unlock(&l_conn->recv_mutex);
#endif
    return (ssize_t)l_read;
}

static ssize_t s_rtc_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_stream->_server_session) return -1;
    rtc_conn_t *l_conn = (rtc_conn_t *)a_stream->_server_session;
    if (l_conn->state != DAP_WEBRTC_STATE_CONNECTED) return -1;

#ifdef DAP_OS_WASM_MT
    rtc_send_args_t l_args = { .peer_id = l_conn->js_peer_id, .data = a_data, .len = (int)a_size, .result = -1 };
    RTC_PROXY_SYNC(s_proxy_dc_send, &l_args);
    int l_sent = l_args.result;
#else
    int l_sent = js_rtc_dc_send(l_conn->js_peer_id, a_data, (int)a_size);
#endif
    if (l_sent > 0) l_conn->bytes_sent += (uint64_t)l_sent;
    return (ssize_t)l_sent;
}

static void s_rtc_close(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->_server_session) return;
    rtc_conn_t *l_conn = (rtc_conn_t *)a_stream->_server_session;

#ifdef DAP_OS_WASM_MT
    if (l_conn->recv_running) {
        l_conn->recv_running = false;
        sem_post(&l_conn->recv_sem);
        pthread_join(l_conn->recv_thread, NULL);
    }
    rtc_close_args_t cl = { .peer_id = l_conn->js_peer_id };
    RTC_PROXY_SYNC(s_proxy_close, &cl);
    sem_destroy(&l_conn->recv_sem);
    pthread_mutex_destroy(&l_conn->recv_mutex);
#else
    js_rtc_close(l_conn->js_peer_id);
#endif

    s_unregister_conn(l_conn->js_peer_id);
    dap_cbuf_delete(l_conn->recv_buf);

    log_it(L_INFO, "WebRTC closed (peer_id=%d, sent=%" PRIu64 ", recv=%" PRIu64 ")",
           l_conn->js_peer_id, l_conn->bytes_sent, l_conn->bytes_received);

    DAP_DEL_Z(l_conn->host);
    DAP_DELETE(l_conn);
    a_stream->_server_session = NULL;
}

static uint32_t s_rtc_get_caps(dap_net_trans_t *a_trans)
{
    (void)a_trans;
    return DAP_NET_TRANS_CAP_RELIABLE | DAP_NET_TRANS_CAP_ORDERED
         | DAP_NET_TRANS_CAP_BIDIRECTIONAL | DAP_NET_TRANS_CAP_LOW_LATENCY;
}

static dap_net_trans_ops_t s_rtc_ops = {
    .init               = s_rtc_init,
    .deinit             = s_rtc_deinit,
    .handshake_init     = s_rtc_handshake_init,
    .session_create     = s_rtc_session_create,
    .session_start      = s_rtc_session_start,
    .read               = s_rtc_read,
    .write              = s_rtc_write,
    .close              = s_rtc_close,
    .get_capabilities   = s_rtc_get_caps,
    .stage_prepare      = s_rtc_stage_prepare,
    .get_client_context = s_rtc_get_client_context,
};

dap_net_trans_webrtc_config_t dap_net_trans_webrtc_config_default(void)
{
    return (dap_net_trans_webrtc_config_t) {
        .stun_server = "stun:stun.l.google.com:19302",
        .turn_server = NULL, .turn_username = NULL, .turn_credential = NULL,
        .ordered = true, .max_retransmits = -1
    };
}

int dap_net_trans_webrtc_register(void)
{
#ifdef DAP_OS_WASM_MT
    if (s_rtc_main_document_https < 0)
        s_rtc_main_document_https = js_rtc_page_is_secure();
#endif
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
