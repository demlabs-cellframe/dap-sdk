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
 * @file dap_net_trans_ws_system_wasm.c
 * @brief WASM WebSocket transport — dual mode:
 *   MT (DAP_WASM_PTHREADS): recv pthread + sem + proxy to main thread
 *   ST (!DAP_WASM_PTHREADS): direct event-driven callbacks on main thread
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
#include "dap_net_trans_websocket_system.h"
#include "dap_http_client_simple.h"
#include "dap_stream.h"
#include "dap_stream_session.h"

#define LOG_TAG "ws_system_wasm"

#define WS_RECV_BUF_SIZE    (256 * 1024)
#define WS_READ_CHUNK       (64 * 1024)
#define WS_MAX_CONNECTIONS  256

#ifdef DAP_WASM_PTHREADS
#include <pthread.h>
#include <semaphore.h>
#include <emscripten/threading.h>
#include <emscripten/proxying.h>
#endif

/* ========================================================================
 * Connection context
 * ======================================================================== */

typedef struct ws_system_conn {
    int                     js_handle;
    dap_ws_system_state_t   state;
    dap_cbuf_t              recv_buf;

    dap_stream_t           *stream;
    void                   *client_ctx;

    char                   *host;
    uint16_t                port;
    uint32_t                session_id;

#ifdef DAP_WASM_PTHREADS
    pthread_t               recv_thread;
    bool                    recv_running;
    pthread_mutex_t         recv_mutex;
    sem_t                   recv_sem;
#endif

    /* ST mode: deferred callback for session_start */
    dap_net_trans_ready_cb_t  ready_callback;

    uint64_t                bytes_sent;
    uint64_t                bytes_received;
} ws_system_conn_t;

static ws_system_conn_t *s_connections[WS_MAX_CONNECTIONS] = {0};

static ws_system_conn_t *s_find_conn(int a_handle)
{
    if (a_handle < 0 || a_handle >= WS_MAX_CONNECTIONS) return NULL;
    return s_connections[a_handle];
}

static void s_register_conn(int a_handle, ws_system_conn_t *a_conn)
{
    if (a_handle >= 0 && a_handle < WS_MAX_CONNECTIONS)
        s_connections[a_handle] = a_conn;
}

static void s_unregister_conn(int a_handle)
{
    if (a_handle >= 0 && a_handle < WS_MAX_CONNECTIONS)
        s_connections[a_handle] = NULL;
}

/* ========================================================================
 * WebSocket JS bridge: extern declarations (impl in library_dap_transport.js)
 * ======================================================================== */

extern int js_ws_create(const char *a_url_ptr);
extern int js_ws_send(int a_handle, const void *a_data, int a_len);
extern void js_ws_close(int a_handle, int a_code);
extern void js_ws_destroy(int a_handle);
extern void js_ws_init_callbacks(void);

#ifdef DAP_WASM_PTHREADS
/* ── MT: proxy wrappers to call JS from worker threads ───────────────── */

typedef struct { const char *url; int result; } ws_create_args_t;
typedef struct { int handle; const void *data; int len; int result; } ws_send_args_t;
typedef struct { int handle; int code; } ws_close_args_t;

static void s_proxy_ws_create(void *a_arg)  { ws_create_args_t *l = a_arg; l->result = js_ws_create(l->url); }
static void s_proxy_ws_send(void *a_arg)    { ws_send_args_t *l = a_arg; l->result = js_ws_send(l->handle, l->data, l->len); }
static void s_proxy_ws_close(void *a_arg)   { ws_close_args_t *l = a_arg; js_ws_close(l->handle, l->code); }
static void s_proxy_ws_destroy(void *a_arg) { ws_close_args_t *l = a_arg; js_ws_destroy(l->handle); }

static int s_ws_create_on_main(const char *a_url) {
    ws_create_args_t l = { .url = a_url, .result = -1 };
    emscripten_proxy_sync(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(), s_proxy_ws_create, &l);
    return l.result;
}
static int s_ws_send_on_main(int a_h, const void *d, int n) {
    ws_send_args_t l = { .handle = a_h, .data = d, .len = n, .result = -1 };
    emscripten_proxy_sync(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(), s_proxy_ws_send, &l);
    return l.result;
}
static void s_ws_close_on_main(int a_h, int c) {
    ws_close_args_t l = { .handle = a_h, .code = c };
    emscripten_proxy_sync(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(), s_proxy_ws_close, &l);
}
static void s_ws_destroy_on_main(int a_h) {
    ws_close_args_t l = { .handle = a_h, .code = 0 };
    emscripten_proxy_sync(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(), s_proxy_ws_destroy, &l);
}

#else /* ST: direct calls — already on main thread */

#define s_ws_create_on_main(url)      js_ws_create(url)
#define s_ws_send_on_main(h, d, n)    js_ws_send(h, d, n)
#define s_ws_close_on_main(h, c)      js_ws_close(h, c)
#define s_ws_destroy_on_main(h)       js_ws_destroy(h)

#endif /* DAP_WASM_PTHREADS */

/* ========================================================================
 * C callbacks from JavaScript (always run on main thread)
 * ======================================================================== */

EMSCRIPTEN_KEEPALIVE
void _ws_on_open(int a_handle)
{
    ws_system_conn_t *l_conn = s_find_conn(a_handle);
    if (!l_conn) return;
    l_conn->state = DAP_WS_SYSTEM_STATE_OPEN;
    log_it(L_NOTICE, "WebSocket connected (handle=%d)", a_handle);

#ifdef DAP_WASM_PTHREADS
    sem_post(&l_conn->recv_sem);
#else
    if (l_conn->ready_callback) {
        l_conn->ready_callback(l_conn->stream, 0);
        l_conn->ready_callback = NULL;
    }
#endif
}

EMSCRIPTEN_KEEPALIVE
void _ws_on_close(int a_handle, int a_code)
{
    ws_system_conn_t *l_conn = s_find_conn(a_handle);
    if (!l_conn) return;
    l_conn->state = DAP_WS_SYSTEM_STATE_CLOSED;
    log_it(L_INFO, "WebSocket closed (handle=%d, code=%d)", a_handle, a_code);

#ifdef DAP_WASM_PTHREADS
    sem_post(&l_conn->recv_sem);
#else
    if (l_conn->ready_callback) {
        l_conn->ready_callback(l_conn->stream, -2);
        l_conn->ready_callback = NULL;
    }
#endif
}

EMSCRIPTEN_KEEPALIVE
void _ws_on_error(int a_handle)
{
    ws_system_conn_t *l_conn = s_find_conn(a_handle);
    if (!l_conn) return;
    log_it(L_ERROR, "WebSocket error (handle=%d)", a_handle);
    l_conn->state = DAP_WS_SYSTEM_STATE_CLOSED;

#ifdef DAP_WASM_PTHREADS
    sem_post(&l_conn->recv_sem);
#else
    if (l_conn->ready_callback) {
        l_conn->ready_callback(l_conn->stream, -3);
        l_conn->ready_callback = NULL;
    }
#endif
}

EMSCRIPTEN_KEEPALIVE
void _ws_on_message(int a_handle, const uint8_t *a_data, int a_len)
{
    ws_system_conn_t *l_conn = s_find_conn(a_handle);
    if (!l_conn || a_len <= 0) return;

    l_conn->bytes_received += (uint64_t)a_len;

#ifdef DAP_WASM_PTHREADS
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
 * MT: recv thread
 * ======================================================================== */

#ifdef DAP_WASM_PTHREADS

static void *s_recv_thread_func(void *a_arg)
{
    ws_system_conn_t *l_conn = (ws_system_conn_t *)a_arg;
    uint8_t l_buf[WS_READ_CHUNK];

    while (l_conn->recv_running) {
        sem_wait(&l_conn->recv_sem);
        if (!l_conn->recv_running || l_conn->state == DAP_WS_SYSTEM_STATE_CLOSED)
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

#endif /* DAP_WASM_PTHREADS */

/* ========================================================================
 * Transport ops: init / deinit
 * ======================================================================== */

#ifdef DAP_WASM_PTHREADS
static void s_proxy_init_callbacks(void *a_arg) { (void)a_arg; js_ws_init_callbacks(); }
#endif

static int s_ws_system_init(dap_net_trans_t *a_trans, dap_config_t *a_config)
{
    (void)a_config; (void)a_trans;
#ifdef DAP_WASM_PTHREADS
    emscripten_proxy_sync(emscripten_proxy_get_system_queue(),
                          emscripten_main_runtime_thread_id(),
                          s_proxy_init_callbacks, NULL);
    log_it(L_NOTICE, "WebSocket System transport initialized (multi-threaded)");
#else
    js_ws_init_callbacks();
    log_it(L_NOTICE, "WebSocket System transport initialized (single-threaded)");
#endif
    return 0;
}

static void s_ws_system_deinit(dap_net_trans_t *a_trans)
{
    (void)a_trans;
    for (int i = 0; i < WS_MAX_CONNECTIONS; i++) {
        if (!s_connections[i]) continue;
        s_ws_destroy_on_main(i);
        ws_system_conn_t *l_conn = s_connections[i];
#ifdef DAP_WASM_PTHREADS
        if (l_conn->recv_running) {
            l_conn->recv_running = false;
            sem_post(&l_conn->recv_sem);
            pthread_join(l_conn->recv_thread, NULL);
        }
        sem_destroy(&l_conn->recv_sem);
        pthread_mutex_destroy(&l_conn->recv_mutex);
#endif
        dap_cbuf_delete(l_conn->recv_buf);
        DAP_DEL_Z(l_conn->host);
        DAP_DELETE(l_conn);
        s_connections[i] = NULL;
    }
    log_it(L_NOTICE, "WebSocket System transport deinitialized");
}

/* ========================================================================
 * stage_prepare: allocate connection context
 * ======================================================================== */

static int s_ws_stage_prepare(dap_net_trans_t *a_trans,
                              const dap_net_stage_prepare_params_t *a_params,
                              dap_net_stage_prepare_result_t *a_result)
{
    if (!a_trans || !a_params || !a_result) return -1;

    ws_system_conn_t *l_conn = DAP_NEW_Z(ws_system_conn_t);
    if (!l_conn) { a_result->error_code = -1; return -1; }

    l_conn->recv_buf = dap_cbuf_create(WS_RECV_BUF_SIZE);
    if (!l_conn->recv_buf) { DAP_DELETE(l_conn); a_result->error_code = -1; return -1; }

#ifdef DAP_WASM_PTHREADS
    pthread_mutex_init(&l_conn->recv_mutex, NULL);
    sem_init(&l_conn->recv_sem, 0, 0);
#endif
    l_conn->js_handle = -1;

    l_conn->host = dap_strdup(a_params->host);
    l_conn->port = a_params->port;
    l_conn->client_ctx = a_params->client_ctx;

    dap_stream_t *l_stream = DAP_NEW_Z(dap_stream_t);
    if (!l_stream) {
        DAP_DELETE(l_conn->host);
        dap_cbuf_delete(l_conn->recv_buf);
#ifdef DAP_WASM_PTHREADS
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

    log_it(L_DEBUG, "WS stage_prepare: conn=%p, stream=%p, host=%s:%u",
           (void *)l_conn, (void *)l_stream, a_params->host, a_params->port);
    return 0;
}

/* ========================================================================
 * Handshake (enc_init) — uses async HTTP, same for both modes
 * ======================================================================== */

typedef struct {
    dap_stream_t                  *stream;
    dap_net_trans_handshake_cb_t   callback;
} ws_handshake_ctx_t;

static void s_ws_handshake_response(void *a_resp, size_t a_resp_size, int a_error, void *a_user_data)
{
    ws_handshake_ctx_t *l_ctx = (ws_handshake_ctx_t *)a_user_data;
    if (l_ctx->callback)
        l_ctx->callback(l_ctx->stream, a_resp, a_resp_size, a_error);
    DAP_DELETE(l_ctx);
}

static int s_ws_handshake_init(dap_stream_t *a_stream,
                               dap_net_handshake_params_t *a_params,
                               dap_net_trans_handshake_cb_t a_callback)
{
    if (!a_stream || !a_params || !a_stream->_server_session) return -1;
    ws_system_conn_t *l_conn = (ws_system_conn_t *)a_stream->_server_session;

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

    ws_handshake_ctx_t *l_ctx = DAP_NEW_Z(ws_handshake_ctx_t);
    if (!l_ctx) { DAP_DELETE(l_b64_body); DAP_DELETE(a_params->alice_pub_key); return -1; }
    l_ctx->stream = a_stream;
    l_ctx->callback = a_callback;

    int l_ret = dap_http_client_simple_request(l_url, "text/text",
                                                l_b64_body, l_b64_len, NULL,
                                                s_ws_handshake_response, l_ctx);
    DAP_DELETE(l_b64_body);
    DAP_DELETE(a_params->alice_pub_key);

    if (l_ret != 0) { DAP_DELETE(l_ctx); return -1; }
    return 0;
}

/* ========================================================================
 * Session create (stream_ctl) — uses async HTTP, same for both modes
 * ======================================================================== */

typedef struct {
    dap_stream_t                 *stream;
    ws_system_conn_t             *conn;
    dap_enc_key_t                *session_key;
    dap_net_trans_session_cb_t    callback;
} ws_session_create_ctx_t;

static void s_ws_session_create_response(void *a_resp, size_t a_resp_size, int a_error, void *a_user_data)
{
    ws_session_create_ctx_t *l_ctx = (ws_session_create_ctx_t *)a_user_data;

    if (a_error != 0 || !a_resp || a_resp_size == 0) {
        log_it(L_ERROR, "stream_ctl XHR failed: %d", a_error);
        if (l_ctx->callback) l_ctx->callback(l_ctx->stream, 0, NULL, 0, -1);
        DAP_DELETE(l_ctx);
        return;
    }

    size_t l_dec_max = a_resp_size + 256;
    char *l_dec = DAP_NEW_Z_SIZE(char, l_dec_max);
    size_t l_dec_len = dap_enc_decode(l_ctx->session_key, a_resp, a_resp_size,
                                       l_dec, l_dec_max, DAP_ENC_DATA_TYPE_RAW);
    if (l_dec_len == 0) {
        log_it(L_ERROR, "stream_ctl decryption failed");
        DAP_DELETE(l_dec);
        if (l_ctx->callback) l_ctx->callback(l_ctx->stream, 0, NULL, 0, -1);
        DAP_DELETE(l_ctx);
        return;
    }
    l_dec[l_dec_len] = '\0';

    uint32_t l_session_id = 0;
    sscanf(l_dec, "%u", &l_session_id);
    l_ctx->conn->session_id = l_session_id;

    log_it(L_NOTICE, "stream_ctl ok, session_id=%u", l_session_id);
    if (l_ctx->callback) l_ctx->callback(l_ctx->stream, l_session_id, l_dec, l_dec_len, 0);
    DAP_DELETE(l_ctx);
}

static int s_ws_session_create(dap_stream_t *a_stream,
                               dap_net_session_params_t *a_params,
                               dap_net_trans_session_cb_t a_callback)
{
    if (!a_stream || !a_params || !a_stream->_server_session) return -1;
    ws_system_conn_t *l_conn = (ws_system_conn_t *)a_stream->_server_session;
    dap_enc_key_t *l_key = a_params->session_key;
    const char *l_key_id = a_params->session_key_id;

    if (!l_key || !l_key_id) {
        log_it(L_ERROR, "stream_ctl: no session key");
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

    ws_session_create_ctx_t *l_ctx = DAP_NEW_Z(ws_session_create_ctx_t);
    if (!l_ctx) { DAP_DELETE(l_b_enc); return -1; }
    l_ctx->stream      = a_stream;
    l_ctx->conn        = l_conn;
    l_ctx->session_key = l_key;
    l_ctx->callback    = a_callback;

    int l_ret = dap_http_client_simple_request(l_url, "application/octet-stream",
                                                l_b_enc, l_b_len, l_headers,
                                                s_ws_session_create_response, l_ctx);
    DAP_DELETE(l_b_enc);
    if (l_ret != 0) { DAP_DELETE(l_ctx); return -1; }
    return 0;
}

/* ========================================================================
 * Session start: open WebSocket, start streaming
 * ======================================================================== */

#ifdef DAP_WASM_PTHREADS

typedef struct {
    dap_stream_t              *stream;
    ws_system_conn_t          *conn;
    uint32_t                   session_id;
    dap_net_trans_ready_cb_t   callback;
} ws_session_start_args_t;

static void *s_session_start_thread(void *a_arg)
{
    ws_session_start_args_t *l_a = (ws_session_start_args_t *)a_arg;
    ws_system_conn_t *l_conn = l_a->conn;

    char l_ws_url[1024];
    snprintf(l_ws_url, sizeof(l_ws_url),
             "wss://%s:%u/stream/globaldb?session_id=%u",
             l_conn->host, l_conn->port, l_a->session_id);

    int l_handle = s_ws_create_on_main(l_ws_url);
    if (l_handle < 0) {
        log_it(L_ERROR, "WebSocket creation failed");
        if (l_a->callback) l_a->callback(l_a->stream, -1);
        DAP_DELETE(l_a);
        return NULL;
    }

    l_conn->js_handle = l_handle;
    l_conn->state = DAP_WS_SYSTEM_STATE_CONNECTING;
    s_register_conn(l_handle, l_conn);

    sem_wait(&l_conn->recv_sem);
    if (l_conn->state != DAP_WS_SYSTEM_STATE_OPEN) {
        log_it(L_ERROR, "WebSocket open failed (state=%d)", l_conn->state);
        s_unregister_conn(l_handle);
        s_ws_destroy_on_main(l_handle);
        if (l_a->callback) l_a->callback(l_a->stream, -2);
        DAP_DELETE(l_a);
        return NULL;
    }

    l_conn->recv_running = true;
    pthread_create(&l_conn->recv_thread, NULL, s_recv_thread_func, l_conn);

    log_it(L_NOTICE, "WebSocket streaming started (session_id=%u)", l_a->session_id);
    if (l_a->callback) l_a->callback(l_a->stream, 0);
    DAP_DELETE(l_a);
    return NULL;
}

static int s_ws_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                              dap_net_trans_ready_cb_t a_callback)
{
    if (!a_stream || !a_stream->_server_session) return -1;

    ws_session_start_args_t *l_args = DAP_NEW_Z(ws_session_start_args_t);
    if (!l_args) return -1;
    l_args->stream     = a_stream;
    l_args->conn       = (ws_system_conn_t *)a_stream->_server_session;
    l_args->session_id = a_session_id;
    l_args->callback   = a_callback;

    pthread_t l_thread;
    pthread_attr_t l_attr;
    pthread_attr_init(&l_attr);
    pthread_attr_setdetachstate(&l_attr, PTHREAD_CREATE_DETACHED);
    int l_ret = pthread_create(&l_thread, &l_attr, s_session_start_thread, l_args);
    pthread_attr_destroy(&l_attr);
    if (l_ret != 0) {
        log_it(L_ERROR, "session_start: pthread_create failed: %d", l_ret);
        DAP_DELETE(l_args);
        return -1;
    }
    return 0;
}

#else /* ST mode */

static int s_ws_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                              dap_net_trans_ready_cb_t a_callback)
{
    if (!a_stream || !a_stream->_server_session) return -1;
    ws_system_conn_t *l_conn = (ws_system_conn_t *)a_stream->_server_session;

    char l_ws_url[1024];
    snprintf(l_ws_url, sizeof(l_ws_url),
             "wss://%s:%u/stream/globaldb?session_id=%u",
             l_conn->host, l_conn->port, a_session_id);

    int l_handle = js_ws_create(l_ws_url);
    if (l_handle < 0) {
        log_it(L_ERROR, "WebSocket creation failed");
        if (a_callback) a_callback(a_stream, -1);
        return 0;
    }

    l_conn->js_handle = l_handle;
    l_conn->state = DAP_WS_SYSTEM_STATE_CONNECTING;
    l_conn->ready_callback = a_callback;
    s_register_conn(l_handle, l_conn);

    /* _ws_on_open will fire callback when WS is connected */
    return 0;
}

#endif /* DAP_WASM_PTHREADS */

/* ========================================================================
 * read / write / close / getters
 * ======================================================================== */

static void *s_ws_get_client_context(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->_server_session) return NULL;
    return ((ws_system_conn_t *)a_stream->_server_session)->client_ctx;
}

static ssize_t s_ws_system_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_stream->_server_session) return -1;
    ws_system_conn_t *l_conn = (ws_system_conn_t *)a_stream->_server_session;
    if (!l_conn->recv_buf) return -1;

#ifdef DAP_WASM_PTHREADS
    pthread_mutex_lock(&l_conn->recv_mutex);
#endif
    size_t l_avail = dap_cbuf_get_size(l_conn->recv_buf);
    if (l_avail == 0) {
#ifdef DAP_WASM_PTHREADS
        pthread_mutex_unlock(&l_conn->recv_mutex);
#endif
        return 0;
    }
    size_t l_to_read = a_size < l_avail ? a_size : l_avail;
    size_t l_read = dap_cbuf_pop(l_conn->recv_buf, l_to_read, a_buffer);
#ifdef DAP_WASM_PTHREADS
    pthread_mutex_unlock(&l_conn->recv_mutex);
#endif
    return (ssize_t)l_read;
}

static ssize_t s_ws_system_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_stream->_server_session) return -1;
    ws_system_conn_t *l_conn = (ws_system_conn_t *)a_stream->_server_session;
    if (l_conn->state != DAP_WS_SYSTEM_STATE_OPEN) return -1;

    int l_sent = s_ws_send_on_main(l_conn->js_handle, a_data, (int)a_size);
    if (l_sent > 0) l_conn->bytes_sent += (uint64_t)l_sent;
    return (ssize_t)l_sent;
}

static void s_ws_system_close(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->_server_session) return;
    ws_system_conn_t *l_conn = (ws_system_conn_t *)a_stream->_server_session;

#ifdef DAP_WASM_PTHREADS
    if (l_conn->recv_running) {
        l_conn->recv_running = false;
        sem_post(&l_conn->recv_sem);
        pthread_join(l_conn->recv_thread, NULL);
    }
#endif

    if (l_conn->state == DAP_WS_SYSTEM_STATE_OPEN ||
        l_conn->state == DAP_WS_SYSTEM_STATE_CONNECTING) {
        s_ws_close_on_main(l_conn->js_handle, 1000);
    }

    s_unregister_conn(l_conn->js_handle);
    s_ws_destroy_on_main(l_conn->js_handle);
    dap_cbuf_delete(l_conn->recv_buf);

    log_it(L_INFO, "WebSocket closed (handle=%d, sent=%" PRIu64 ", recv=%" PRIu64 ")",
           l_conn->js_handle, l_conn->bytes_sent, l_conn->bytes_received);

    DAP_DEL_Z(l_conn->host);
#ifdef DAP_WASM_PTHREADS
    sem_destroy(&l_conn->recv_sem);
    pthread_mutex_destroy(&l_conn->recv_mutex);
#endif
    DAP_DELETE(l_conn);
    a_stream->_server_session = NULL;
}

static uint32_t s_ws_system_get_caps(dap_net_trans_t *a_trans)
{
    (void)a_trans;
    return DAP_NET_TRANS_CAP_RELIABLE
         | DAP_NET_TRANS_CAP_ORDERED
         | DAP_NET_TRANS_CAP_BIDIRECTIONAL;
}

/* ========================================================================
 * Ops table + public API
 * ======================================================================== */

static dap_net_trans_ops_t s_ws_system_ops = {
    .init               = s_ws_system_init,
    .deinit             = s_ws_system_deinit,
    .connect            = NULL,
    .listen             = NULL,
    .accept             = NULL,
    .handshake_init     = s_ws_handshake_init,
    .handshake_process  = NULL,
    .session_create     = s_ws_session_create,
    .session_start      = s_ws_session_start,
    .read               = s_ws_system_read,
    .write              = s_ws_system_write,
    .close              = s_ws_system_close,
    .get_capabilities   = s_ws_system_get_caps,
    .register_server_handlers = NULL,
    .stage_prepare      = s_ws_stage_prepare,
    .get_client_context = s_ws_get_client_context,
    .get_max_packet_size = NULL,
};

dap_net_trans_ws_system_config_t dap_net_trans_ws_system_config_default(void)
{
    return (dap_net_trans_ws_system_config_t) {
        .max_message_size   = 1024 * 1024,
        .ping_interval_ms   = 0,
        .connect_timeout_ms = 10000,
        .subprotocol        = "dap-stream"
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
