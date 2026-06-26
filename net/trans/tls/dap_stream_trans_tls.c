/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * TLS Mimicry Transport
 *
 * Implements DAP_NET_TRANS_TLS_DIRECT as a TLS 1.3 mimicry layer.
 * On the wire DPI sees a standard TLS 1.3 handshake (ClientHello,
 * ServerHello, ChangeCipherSpec, Application Data records). No real
 * TLS crypto is performed -- this is purely a framing transport.
 *
 * The DAP stream layer with DSHP handshake and dap_enc encryption
 * runs ON TOP of this transport, treating it as a transparent byte pipe.
 *
 * Stack:
 *   TCP -> TLS mimicry handshake -> DSHP handshake -> DAP stream (dap_enc)
 */

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "dap_common.h"
#include "dap_config.h"
#include "dap_strfuncs.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_net.h"
#include "dap_net_trans.h"
#include "dap_client.h"
#include "dap_client_fsm.h"
#include "dap_stream.h"
#include "dap_stream_pkt.h"
#include "dap_tls_mimicry.h"
#include "dap_stream_trans_tls.h"
#include "dap_net_trans_tls_server.h"

#define LOG_TAG "dap_stream_trans_tls"

typedef struct tls_mimicry_ctx {
    dap_tls_mimicry_t *mimicry;
    char              *sni_hostname;
} tls_mimicry_ctx_t;



static dap_stream_trans_tls_config_t s_config;
static bool s_debug_more = false;

static int     s_tls_init(dap_net_trans_t *a_trans, dap_config_t *a_cfg);
static void    s_tls_deinit(dap_net_trans_t *a_trans);
static int     s_tls_stage_prepare(dap_net_trans_t *a_trans,
                                   const dap_net_stage_prepare_params_t *a_params,
                                   dap_net_stage_prepare_result_t *a_result);
static ssize_t s_tls_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static ssize_t s_tls_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size);
static void    s_tls_close(dap_stream_t *a_stream);
static uint32_t s_tls_get_caps(dap_net_trans_t *a_trans);

static int s_tls_handshake_init(dap_stream_t *a_stream,
                                dap_net_handshake_params_t *a_params,
                                dap_net_trans_handshake_cb_t a_callback);
static int s_tls_session_create(dap_stream_t *a_stream, dap_net_session_params_t *a_params,
                                dap_net_trans_session_cb_t a_callback);

static const dap_net_trans_ops_t s_tls_ops = {
    .init             = s_tls_init,
    .deinit           = s_tls_deinit,
    .stage_prepare    = s_tls_stage_prepare,
    .write            = s_tls_write,
    .read             = s_tls_read,
    .close            = s_tls_close,
    .get_capabilities = s_tls_get_caps,
    .handshake_init   = s_tls_handshake_init,
    .session_create   = s_tls_session_create,
    .session_start    = NULL
};

/* ========================================================================== */
/*  Registration                                                              */
/* ========================================================================== */

int dap_stream_trans_tls_register(void)
{
    s_config = dap_stream_trans_tls_config_default();

    /* Initialize TLS server module first (registers server operations) */
    int l_srv_rc = dap_net_trans_tls_server_init();
    if (l_srv_rc != 0) {
        log_it(L_WARNING, "TLS server module init failed (rc=%d), server-side TLS unavailable", l_srv_rc);
        /* Continue — client-side TLS still works */
    }

    int l_rc = dap_net_trans_register(
        "tls_mimicry",
        DAP_NET_TRANS_TLS_DIRECT,
        &s_tls_ops,
        DAP_NET_TRANS_SOCKET_TCP,
        NULL);
    if (l_rc == 0)
        log_it(L_NOTICE, "TLS Mimicry transport registered");
    return l_rc;
}

int dap_stream_trans_tls_unregister(void)
{
    dap_net_trans_tls_server_deinit();
    DAP_DEL_Z(s_config.sni_hostname);
    return dap_net_trans_unregister(DAP_NET_TRANS_TLS_DIRECT);
}

/* ========================================================================== */
/*  Configuration                                                             */
/* ========================================================================== */

dap_stream_trans_tls_config_t dap_stream_trans_tls_config_default(void)
{
    return (dap_stream_trans_tls_config_t){
        .sni_hostname = NULL,
    };
}

int dap_stream_trans_tls_set_config(dap_net_trans_t *a_trans,
                                    const dap_stream_trans_tls_config_t *a_config)
{
    UNUSED(a_trans);
    if (!a_config) return -1;
    DAP_DEL_Z(s_config.sni_hostname);
    if (a_config->sni_hostname)
        s_config.sni_hostname = dap_strdup(a_config->sni_hostname);
    return 0;
}

int dap_stream_trans_tls_get_config(dap_net_trans_t *a_trans,
                                    dap_stream_trans_tls_config_t *a_config)
{
    UNUSED(a_trans);
    if (!a_config) return -1;
    memcpy(a_config, &s_config, sizeof(s_config));
    return 0;
}

/* ========================================================================== */
/*  Transport ops                                                             */
/* ========================================================================== */

static int s_tls_init(dap_net_trans_t *a_trans, dap_config_t *a_cfg)
{
    UNUSED(a_trans);
    if (a_cfg) {
        s_debug_more = dap_config_get_item_bool_default(a_cfg, "tls", "debug_more", false);
        const char *l_sni = dap_config_get_item_str(a_cfg, "tls", "sni_hostname");
        if (l_sni) {
            DAP_DEL_Z(s_config.sni_hostname);
            s_config.sni_hostname = dap_strdup(l_sni);
        }
    }
    log_it(L_INFO, "TLS Mimicry transport initialized (SNI=%s)",
           s_config.sni_hostname ? s_config.sni_hostname : "<none>");
    return 0;
}

static void s_tls_deinit(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    DAP_DEL_Z(s_config.sni_hostname);
    debug_if(s_debug_more, L_DEBUG, "TLS Mimicry transport deinitialized");
}

static int s_tls_stage_prepare(dap_net_trans_t *a_trans,
                               const dap_net_stage_prepare_params_t *a_params,
                               dap_net_stage_prepare_result_t *a_result)
{
    UNUSED(a_trans);
    if (!a_params || !a_result) return -1;

    dap_events_socket_callbacks_t *l_cbs = a_params->callbacks;
    dap_events_socket_t *l_es = dap_events_socket_create_platform(PF_INET, SOCK_STREAM, 0, l_cbs);
    if (!l_es) {
        log_it(L_ERROR, "Failed to create TCP socket for TLS mimicry transport");
        a_result->error_code = -1;
        return -1;
    }

    l_es->_inheritor = a_params->client_ctx;
    dap_events_socket_resolve_and_set_addr(l_es, a_params->host, a_params->port);
    l_es->flags |= DAP_SOCK_CONNECTING | DAP_SOCK_READY_TO_WRITE;
#ifdef DAP_EVENTS_CAPS_IOCP
    l_es->flags &= ~DAP_SOCK_READY_TO_READ;
#else
    int l_err = 0;
    dap_events_socket_connect(l_es, &l_err);
    if (l_err) {
        log_it(L_ERROR, "TLS Mimicry: TCP connect error: %d", l_err);
        a_result->error_code = l_err;
        return -1;
    }
#endif

    if (a_params->worker)
        dap_worker_add_events_socket(a_params->worker, l_es);

    a_result->esocket = l_es;
    a_result->error_code = 0;

    const char *l_sni = s_config.sni_hostname ? s_config.sni_hostname : a_params->host;
    log_it(L_INFO, "TLS Mimicry: TCP connect to %s:%u initiated (SNI=%s)",
           a_params->host, a_params->port, l_sni);
    return 0;
}

static ssize_t s_tls_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_stream->esocket || !a_data || a_size == 0)
        return -1;

    void *l_wrapped = NULL;
    size_t l_wrapped_size = 0;
    tls_mimicry_ctx_t *l_ctx = (tls_mimicry_ctx_t *)a_stream->trans_ctx->transport_priv;

    if (l_ctx && l_ctx->mimicry
        && dap_tls_mimicry_get_state(l_ctx->mimicry) == DAP_TLS_MIMICRY_STATE_ESTABLISHED) {
        if (dap_tls_mimicry_wrap(l_ctx->mimicry, a_data, a_size,
                                 &l_wrapped, &l_wrapped_size) != 0) {
            log_it(L_ERROR, "TLS record wrap failed");
            return -1;
        }
        ssize_t l_ret = dap_events_socket_write_unsafe(
            a_stream->esocket, l_wrapped, l_wrapped_size);
        DAP_DELETE(l_wrapped);
        return l_ret >= 0 ? (ssize_t)a_size : -1;
    }

    return dap_events_socket_write_unsafe(a_stream->esocket, a_data, a_size);
}

static ssize_t s_tls_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_stream->esocket || !a_buffer || a_size == 0)
        return -1;

    dap_events_socket_t *l_es = a_stream->esocket;
    size_t l_avail = l_es->buf_in_size;
    if (l_avail == 0)
        return 0;

    tls_mimicry_ctx_t *l_ctx = (tls_mimicry_ctx_t *)a_stream->trans_ctx->transport_priv;

    if (l_ctx && l_ctx->mimicry) {
        dap_tls_mimicry_state_t l_state = dap_tls_mimicry_get_state(l_ctx->mimicry);

        /* Process TLS handshake messages until we reach ESTABLISHED */
        while (l_state != DAP_TLS_MIMICRY_STATE_ESTABLISHED && l_avail > 0) {
            if (l_state == DAP_TLS_MIMICRY_STATE_CLIENT_HELLO_SENT) {
                /* Expecting ServerHello + CCS + fake extensions from server */
                void *l_response = NULL;
                size_t l_response_size = 0;
                int l_rc = dap_tls_mimicry_process_server_hello(l_ctx->mimicry,
                                                                  l_es->buf_in, l_avail,
                                                                  &l_response, &l_response_size);
                if (l_rc == 0) {
                    /* ServerHello processed — state is now ESTABLISHED.
                     * Send client CCS + fake Finished back to server. */
                    if (l_response && l_response_size > 0) {
                        dap_events_socket_write_unsafe(a_stream->esocket,
                                                       l_response, l_response_size);
                        DAP_DELETE(l_response);
                    }
                    /* Clear consumed data from buffer (entire handshake consumed) */
                    l_es->buf_in_size = 0;
                    l_avail = 0;
                    l_state = dap_tls_mimicry_get_state(l_ctx->mimicry);
                    debug_if(s_debug_more, L_DEBUG, "TLS handshake: ServerHello processed, state=ESTABLISHED");
                } else {
                    /* Incomplete or error — wait for more data */
                    DAP_DELETE(l_response);
                    break;
                }
            } else {
                break;
            }
        }

        /* After handshake: unwrap TLS records */
        if (l_state == DAP_TLS_MIMICRY_STATE_ESTABLISHED && l_avail > 0) {
            void *l_unwrapped = NULL;
            size_t l_unwrapped_size = 0, l_consumed = 0;
            int l_rc = dap_tls_mimicry_unwrap(l_ctx->mimicry,
                                               l_es->buf_in, l_avail,
                                               &l_unwrapped, &l_unwrapped_size, &l_consumed);
            if (l_rc < 0) {
                log_it(L_ERROR, "TLS record unwrap failed");
                return -1;
            }
            if (l_consumed > 0 && l_consumed <= l_avail) {
                memmove(l_es->buf_in, l_es->buf_in + l_consumed, l_avail - l_consumed);
                l_es->buf_in_size -= l_consumed;
            }
            if (l_rc == 1 || l_unwrapped_size == 0)
                return 0;

            size_t l_to_copy = (l_unwrapped_size < a_size) ? l_unwrapped_size : a_size;
            memcpy(a_buffer, l_unwrapped, l_to_copy);
            DAP_DELETE(l_unwrapped);
            return (ssize_t)l_to_copy;
        }
    }

    /* Fallback: raw read (pre-handshake or no mimicry context) */
    size_t l_to_read = (l_avail < a_size) ? l_avail : a_size;
    memcpy(a_buffer, l_es->buf_in, l_to_read);
    return (ssize_t)l_to_read;
}

static void s_tls_close(dap_stream_t *a_stream)
{
    if (!a_stream)
        return;
    debug_if(s_debug_more, L_DEBUG, "TLS Mimicry: closing stream");

    if (a_stream->trans_ctx) {
        tls_mimicry_ctx_t *l_ctx = (tls_mimicry_ctx_t *)a_stream->trans_ctx->transport_priv;
        if (l_ctx) {
            dap_tls_mimicry_free(l_ctx->mimicry);
            DAP_DEL_Z(l_ctx->sni_hostname);
            DAP_DELETE(l_ctx);
            a_stream->trans_ctx->transport_priv = NULL;
        }
    }
}

static uint32_t s_tls_get_caps(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return DAP_NET_TRANS_CAP_RELIABLE
         | DAP_NET_TRANS_CAP_ORDERED
         | DAP_NET_TRANS_CAP_OBFUSCATION
         | DAP_NET_TRANS_CAP_BIDIRECTIONAL
         | DAP_NET_TRANS_CAP_HIGH_THROUGHPUT
         | DAP_NET_TRANS_CAP_MIMICRY;
}

/* ========================================================================== */
/*  Client-side TLS handshake (fake TLS for DPI evasion)                     */
/* ========================================================================== */

/**
 * @brief Perform fake TLS handshake on client side
 *
 * Creates TLS mimicry context, generates ClientHello, sends it to server,
 * waits for fake ServerHello, and transitions to ESTABLISHED state.
 * After this, s_tls_write/s_tls_read wrap/unwrap data in TLS records.
 *
 * The FSM then sends enc_init through the TLS channel — DPI sees
 * TLS Application Data records containing the DAP handshake.
 *
 * Wire layout:
 *   Client -> Server:  TLS Record(ClientHello)     — fake
 *   Server -> Client:  TLS Record(ServerHello)     — fake
 *                    + TLS Record(ChangeCipherSpec) — fake
 *   Client -> Server:  TLS Record(ChangeCipherSpec) — fake
 *                    + enc_init (DAP handshake)     — REAL encryption
 */
static int s_tls_handshake_init(dap_stream_t *a_stream,
                                dap_net_handshake_params_t *a_params,
                                dap_net_trans_handshake_cb_t a_callback)
{
    if (!a_stream || !a_params || !a_callback) {
        log_it(L_ERROR, "TLS handshake_init: invalid parameters");
        return -1;
    }

    if (!a_stream->trans_ctx || !a_stream->esocket) {
        log_it(L_ERROR, "TLS handshake_init: stream has no trans_ctx or esocket");
        return -2;
    }

    /* Create TLS mimicry context */
    tls_mimicry_ctx_t *l_ctx = DAP_NEW_Z(tls_mimicry_ctx_t);
    if (!l_ctx) {
        log_it(L_ERROR, "TLS handshake_init: failed to allocate mimicry ctx");
        return -3;
    }

    l_ctx->mimicry = dap_tls_mimicry_new(false); /* client mode */
    if (!l_ctx->mimicry) {
        log_it(L_ERROR, "TLS handshake_init: failed to create mimicry engine");
        DAP_DELETE(l_ctx);
        return -4;
    }

    /* Set SNI hostname for mimicry */
    const char *l_sni = s_config.sni_hostname ? s_config.sni_hostname : "";
    if (l_sni && *l_sni)
        dap_tls_mimicry_set_sni(l_ctx->mimicry, l_sni);

    /* Generate fake ClientHello */
    void *l_client_hello = NULL;
    size_t l_client_hello_size = 0;
    int l_rc = dap_tls_mimicry_create_client_hello(l_ctx->mimicry,
                                                    &l_client_hello, &l_client_hello_size);
    if (l_rc != 0 || !l_client_hello || l_client_hello_size == 0) {
        log_it(L_ERROR, "TLS handshake_init: failed to create ClientHello (rc=%d)", l_rc);
        dap_tls_mimicry_free(l_ctx->mimicry);
        DAP_DELETE(l_ctx);
        return -5;
    }

    /* Send ClientHello to server */
    ssize_t l_written = dap_events_socket_write_unsafe(a_stream->esocket,
                                                       l_client_hello, l_client_hello_size);
    DAP_DELETE(l_client_hello);

    if (l_written < 0) {
        log_it(L_ERROR, "TLS handshake_init: failed to send ClientHello");
        dap_tls_mimicry_free(l_ctx->mimicry);
        DAP_DELETE(l_ctx);
        return -6;
    }

    /* Store mimicry context in transport_priv — s_tls_read will process ServerHello */
    a_stream->trans_ctx->transport_priv = l_ctx;

    log_it(L_NOTICE, "TLS handshake_init: ClientHello sent (%zd bytes), awaiting ServerHello",
           l_written);

    /* Signal handshake complete — FSM proceeds to enc_init.
     * The fake ServerHello from server will be processed by s_tls_read
     * on the next read cycle, transitioning mimicry to ESTABLISHED.
     * enc_init data sent via s_tls_write will be wrapped in TLS records
     * after the ServerHello is processed. */
    a_callback(a_stream, NULL, 0, 0);

    return 0;
}

/* ========================================================================== */
/*  Session creation (stream_ctl through TLS channel)                         */
/* ========================================================================== */

/**
 * @brief Create DAP session through TLS channel
 *
 * Sends stream_ctl HTTP POST through the TLS channel. The HTTP POST
 * is wrapped in TLS records by s_tls_write (called by DAP stream layer).
 * Server processes it through its internal HTTP server after TLS unwrap.
 *
 * This is independent of the HTTP transport module — TLS has its own
 * internal HTTP server (created in dap_net_trans_tls_server.c).
 */
static int s_tls_session_create(dap_stream_t *a_stream, dap_net_session_params_t *a_params,
                                dap_net_trans_session_cb_t a_callback)
{
    if (!a_stream || !a_params || !a_callback) {
        log_it(L_ERROR, "TLS session_create: invalid parameters");
        return -1;
    }

    if (!a_stream->trans_ctx) {
        log_it(L_ERROR, "TLS session_create: stream has no trans_ctx");
        return -2;
    }

    /* Build stream_ctl request body (protocol version) */
    char l_request[16];
    size_t l_request_size = snprintf(l_request, sizeof(l_request), "%d", DAP_CLIENT_PROTOCOL_VERSION);

    /* Build stream_ctl URL with channel parameters */
    char *l_suburl = dap_strdup_printf("channels=%s,enc_type=%d,enc_key_size=%zu,enc_headers=%d",
                                        a_params->channels, a_params->enc_type,
                                        a_params->enc_key_size, a_params->enc_headers ? 1 : 0);

    char l_stream_ctl_url[1024] = { '\0' };
    snprintf(l_stream_ctl_url, sizeof(l_stream_ctl_url), "%s/%s",
             DAP_UPLINK_PATH_STREAM_CTL, l_suburl);

    debug_if(s_debug_more, L_DEBUG, "TLS session_create: sending stream_ctl via TLS channel: %s",
           l_stream_ctl_url);

    /* Build complete HTTP POST request */
    char l_http_post[2048];
    size_t l_http_post_size = snprintf(l_http_post, sizeof(l_http_post),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: text/text\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "%s",
        l_stream_ctl_url,
        "localhost",  /* Host header (server processes by path) */
        l_request_size,
        l_request);

    DAP_DELETE(l_suburl);

    debug_if(s_debug_more, L_DEBUG, "TLS session_create: HTTP POST %zu bytes via TLS channel",
           l_http_post_size);

    /* Send through DAP stream transport layer — s_tls_write wraps in TLS records.
     * Server's internal HTTP server processes after TLS unwrap. */
    ssize_t l_sent = dap_stream_trans_write_unsafe(a_stream, l_http_post, l_http_post_size);

    if (l_sent < 0) {
        log_it(L_ERROR, "TLS session_create: failed to send stream_ctl via TLS");
        return -3;
    }

    debug_if(s_debug_more, L_DEBUG, "TLS session_create: stream_ctl sent (%zd bytes) via TLS", l_sent);
    return 0;
}

/* ========================================================================== */
/*  Utility                                                                   */
/* ========================================================================== */

bool dap_stream_trans_is_tls(const dap_stream_t *a_stream)
{
    return a_stream && a_stream->trans
        && a_stream->trans->type == DAP_NET_TRANS_TLS_DIRECT;
}
