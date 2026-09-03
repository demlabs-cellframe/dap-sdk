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
#ifdef DAP_OS_WINDOWS
# include <winsock2.h>
# include <ws2tcpip.h>
#else
# include <sys/socket.h>
# include <netinet/tcp.h>
#endif

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
#include "dap_enc_base58.h"
#include "dap_tls_mimicry.h"
#include "dap_tls_fingerprint.h"
#include "dap_stream_trans_tls.h"
#include "dap_net_trans_tls_server.h"

#define LOG_TAG "dap_stream_trans_tls"

/* Client-side protocol phases, processed in order by the single read callback.
 * The state machine is driven by this enum, NOT by swapping read_callback
 * pointers — so there is no window where a server response can arrive with
 * no callback able to receive it (the race the old swap-on-write design had). */
typedef enum tls_client_phase {
    TLS_PHASE_HANDSHAKE = 0,       /* waiting for ServerHello */
    TLS_PHASE_ENC_INIT_WAIT,       /* sent enc_init, waiting for response */
    TLS_PHASE_STREAM_CTL_WAIT,     /* sent stream_ctl, waiting for response */
    TLS_PHASE_STREAMING,           /* established — DAP stream packets */
} tls_client_phase_t;

typedef struct tls_mimicry_ctx {
    dap_tls_mimicry_t *mimicry;
    char              *sni_hostname;
    dap_net_trans_handshake_cb_t handshake_cb;
    dap_net_handshake_params_t  handshake_params;  /* saved for enc_init after TLS handshake */
    dap_net_trans_session_cb_t  session_create_cb; /* callback for stream_ctl response */
    tls_client_phase_t           phase;            /* current protocol phase */
} tls_mimicry_ctx_t;



static dap_stream_trans_tls_config_t s_config;
static bool s_debug_more = false;

/**
 * @brief Get stream from esocket via client→FSM→trans_ctx chain.
 * _inheritor is dap_client_t* (set by s_stream_new via trans_ctx).
 * We go: esocket->_inheritor → client → fsm → trans_ctx → stream.
 */
static dap_stream_t *s_stream_from_es(dap_events_socket_t *a_es)
{
    if (!a_es || !a_es->_inheritor) return NULL;
    dap_client_t *l_client = (dap_client_t *)a_es->_inheritor;
    dap_client_fsm_t *l_fsm = l_client ? DAP_CLIENT_FSM(l_client) : NULL;
    dap_net_trans_ctx_t *l_tc = l_fsm ? l_fsm->trans_ctx : NULL;
    return l_tc ? l_tc->stream : NULL;
}

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

    /* Disable Nagle's algorithm on the client-side TCP socket.
     * VPN data consists of individual IP packets (~1420 bytes + headers);
     * Nagle delays small writes up to 200 ms waiting for coalescing or an ACK,
     * which adds unacceptable latency on the VPN hot path. */
    {
        int l_nodelay = 1;
        if (setsockopt(l_es->socket, IPPROTO_TCP, TCP_NODELAY,
                        (const char *)&l_nodelay, sizeof(l_nodelay)) < 0)
            log_it(L_WARNING, "TLS transport: failed to set TCP_NODELAY: %s", strerror(errno));
    }

    dap_events_socket_resolve_and_set_addr(l_es, a_params->host, a_params->port);
    l_es->flags |= DAP_SOCK_CONNECTING | DAP_SOCK_READY_TO_WRITE | DAP_SOCK_READY_TO_READ;
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

    /* Create stream from esocket — same as HTTP transport.
     * The stream is needed for the FSM to proceed through
     * enc_init → stream_ctl → stream stages. */
    dap_stream_t *l_stream = dap_stream_new_es_client(l_es,
                                (dap_stream_node_addr_t *)a_params->node_addr,
                                a_params->authorized);
    if (!l_stream) {
        log_it(L_ERROR, "TLS Mimicry: failed to create stream");
        a_result->error_code = -1;
        return -1;
    }
    l_stream->trans = a_trans;

    a_result->esocket = l_es;
    a_result->stream = l_stream;
    a_result->error_code = 0;

    const char *l_sni = s_config.sni_hostname ? s_config.sni_hostname : a_params->host;
    log_it(L_INFO, "TLS Mimicry: TCP connect to %s:%u initiated (SNI=%s)",
           a_params->host, a_params->port, l_sni);
    return 0;
}

static ssize_t s_tls_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_stream->esocket || !a_stream->trans_ctx || !a_data || a_size == 0)
        return -1;

    void *l_wrapped = NULL;
    size_t l_wrapped_size = 0;
    tls_mimicry_ctx_t *l_ctx = (tls_mimicry_ctx_t *)a_stream->trans_ctx->transport_priv;
    if (!l_ctx || !l_ctx->mimicry) {
        /* No mimicry context — server-side TLS streams may not have one.
         * Write raw data; the TLS server's s_tls_write event-loop callback
         * will handle TLS wrapping of buf_out. */
        return dap_events_socket_write_unsafe(a_stream->esocket, a_data, a_size);
    }

    if (dap_tls_mimicry_get_state(l_ctx->mimicry) == DAP_TLS_MIMICRY_STATE_ESTABLISHED) {
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
    if (!a_stream || !a_stream->esocket || !a_stream->trans_ctx || !a_buffer || a_size == 0)
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
                size_t l_response_size = 0, l_hs_consumed = 0;
                int l_rc = dap_tls_mimicry_process_server_hello(l_ctx->mimicry,
                                                                  l_es->buf_in, l_avail,
                                                                  &l_response, &l_response_size,
                                                                  &l_hs_consumed);
                if (l_rc == 0) {
                    /* ServerHello processed — state is now ESTABLISHED.
                     * Send client CCS + fake Finished back to server. */
                    if (l_response && l_response_size > 0) {
                        dap_events_socket_write_unsafe(a_stream->esocket,
                                                       l_response, l_response_size);
                        DAP_DELETE(l_response);
                    }
                    /* Shrink only handshake bytes — keep pipelined APP_DATA */
                    if (l_hs_consumed > 0 && l_hs_consumed < l_es->buf_in_size) {
                        memmove(l_es->buf_in, l_es->buf_in + l_hs_consumed,
                                l_es->buf_in_size - l_hs_consumed);
                        l_es->buf_in_size -= l_hs_consumed;
                    } else {
                        l_es->buf_in_size = 0;
                    }
                    l_avail = l_es->buf_in_size;
                    l_state = dap_tls_mimicry_get_state(l_ctx->mimicry);
                    debug_if(s_debug_more, L_DEBUG, "TLS handshake: ServerHello processed, state=ESTABLISHED");
                    /* TLS handshake complete — notify FSM to proceed with enc_init */
                    if (l_ctx->handshake_cb) {
                        l_ctx->handshake_cb(a_stream, NULL, 0, 0);
                        l_ctx->handshake_cb = NULL; /* one-shot */
                    }
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
 * @brief TLS handshake read callback — processes ServerHello from server
 *
 * This callback is set on the esocket during TLS handshake. When the
 * server sends back ServerHello + CCS + fake extensions, this callback
 * processes them, completes the TLS handshake, and notifies the FSM.
 */
static void s_tls_read_cb(dap_events_socket_t *a_es, void *a_arg);

/**
 * @brief Build and send the enc_init HTTP POST through the TLS channel.
 *
 * Called once, right after the fake TLS handshake completes. Advances
 * l_ctx->phase to TLS_PHASE_ENC_INIT_WAIT so the next incoming TLS record
 * (the server's enc_init reply) is routed to the FSM via handshake_cb.
 *
 * @return 0 on success, -1 on allocation/send failure.
 */
static int s_tls_send_enc_init(dap_stream_t *a_stream, tls_mimicry_ctx_t *a_ctx,
                               dap_events_socket_t *a_es)
{
    /* Node address: base58-encode the stream's node addr (set at stream
     * creation from a_params->node_addr). Fall back to the legacy anonymous
     * placeholder — same logic as the HTTP transport. */
    char l_node_addr_b58[32] = "gd4y5yh78w42aaagh";
    if (a_stream->node.uint64) {
        uint64_t l_addr_le = a_stream->node.uint64;
        size_t l_b58_len = dap_enc_base58_encode(&l_addr_le, sizeof(l_addr_le), l_node_addr_b58);
        if (!l_b58_len)
            dap_strncpy(l_node_addr_b58, "gd4y5yh78w42aaagh", sizeof(l_node_addr_b58) - 1);
    }

    char l_enc_init_url[512];
    snprintf(l_enc_init_url, sizeof(l_enc_init_url),
             "/%s/%s?enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zu,"
             "block_key_size=%zu,protocol_version=%d,sign_count=%zu",
             DAP_UPLINK_PATH_ENC_INIT, l_node_addr_b58,
             a_ctx->handshake_params.enc_type,
             a_ctx->handshake_params.pkey_exchange_type,
             a_ctx->handshake_params.pkey_exchange_size,
             a_ctx->handshake_params.block_key_size,
             a_ctx->handshake_params.protocol_version,
             a_ctx->handshake_params.sign_count);

    /* Body = base64-encoded alice_pub_key */
    size_t l_body_b64_size = ((a_ctx->handshake_params.alice_pub_key_size + 2) / 3) * 4 + 1;
    char *l_body_b64 = DAP_NEW_Z_SIZE(char, l_body_b64_size);
    if (!l_body_b64)
        return -1;

    static const char s_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t l_in_len = a_ctx->handshake_params.alice_pub_key_size;
    const uint8_t *l_in = a_ctx->handshake_params.alice_pub_key;
    size_t l_out_pos = 0;
    for (size_t i = 0; i < l_in_len; i += 3) {
        uint32_t n = (uint32_t)l_in[i] << 16;
        if (i + 1 < l_in_len) n |= (uint32_t)l_in[i + 1] << 8;
        if (i + 2 < l_in_len) n |= (uint32_t)l_in[i + 2];
        l_body_b64[l_out_pos++] = s_b64[(n >> 18) & 0x3F];
        l_body_b64[l_out_pos++] = s_b64[(n >> 12) & 0x3F];
        l_body_b64[l_out_pos++] = (i + 1 < l_in_len) ? s_b64[(n >> 6) & 0x3F] : '=';
        l_body_b64[l_out_pos++] = (i + 2 < l_in_len) ? s_b64[n & 0x3F] : '=';
    }
    l_body_b64[l_out_pos] = '\0';

    char l_http_post[8192];
    int l_post_size = snprintf(l_http_post, sizeof(l_http_post),
        "POST %s HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: text/text\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "%s",
        l_enc_init_url, l_out_pos, l_body_b64);

    int l_ret = 0;
    if (l_post_size < 0 || (size_t)l_post_size >= sizeof(l_http_post)) {
        log_it(L_ERROR, "TLS enc_init: HTTP request too large (%d bytes)", l_post_size);
        l_ret = -1;
    } else {
        ssize_t l_sent = dap_stream_trans_write_unsafe(a_stream, l_http_post, (size_t)l_post_size);
        if (l_sent > 0)
            log_it(L_NOTICE, "TLS enc_init sent (%zd bytes) through TLS channel", l_sent);
        else {
            log_it(L_ERROR, "TLS enc_init: failed to send through TLS channel");
            l_ret = -1;
        }
    }
    DAP_DELETE(l_body_b64);
    return l_ret;
}

/**
 * @brief Single esocket read callback for the whole TLS client lifecycle.
 *
 * The protocol phase is tracked explicitly in l_ctx->phase rather than by
 * swapping read_callback pointers. This removes the race where a fast server
 * reply arrived in buf_in after a write but before the callback was switched,
 * leaving it unread until the connection timeout.
 *
 * Flow:
 *   HANDSHAKE        → process ServerHello, send enc_init → ENC_INIT_WAIT
 *   ENC_INIT_WAIT    → unwrap reply, fire handshake_cb        → STREAM_CTL_WAIT *
 *   STREAM_CTL_WAIT  → unwrap reply, fire session_create_cb   → STREAMING *
 *   STREAMING        → unwrap, feed to DAP stream layer
 *
 * (* next phase entered when the FSM calls s_tls_session_create, not here)
 */
static void s_tls_read_cb(dap_events_socket_t *a_es, void *a_arg)
{
    (void)a_arg;
    if (!a_es || a_es->buf_in_size == 0)
        return;

    /* Get the mimicry context from the client's trans_ctx.
     * During handshake phase, stream may be NULL — we need the mimicry
     * context to process ServerHello even before the stream exists. */
    if (!a_es->_inheritor) {
        log_it(L_ERROR, "TLS read: no _inheritor on esocket");
        return;
    }
    dap_client_t *l_client = (dap_client_t *)a_es->_inheritor;
    dap_client_fsm_t *l_fsm = l_client ? DAP_CLIENT_FSM(l_client) : NULL;
    dap_net_trans_ctx_t *l_tc = l_fsm ? l_fsm->trans_ctx : NULL;
    tls_mimicry_ctx_t *l_ctx = l_tc ? (tls_mimicry_ctx_t *)l_tc->transport_priv : NULL;
    if (!l_ctx || !l_ctx->mimicry) {
        /* Expected before handshake_init sets up transport_priv, or on
         * zombie esockets from a previous failed connection. */
        log_it(L_WARNING, "TLS read: dropping %zu bytes (no mimicry ctx, phase unknown)",
               a_es->buf_in_size);
        a_es->buf_in_size = 0;
        return;
    }

    dap_stream_t *l_stream = l_tc ? l_tc->stream : NULL;

    /* === HANDSHAKE phase: ServerHello uses its own parser (not TLS unwrap) === */
    if (l_ctx->phase == TLS_PHASE_HANDSHAKE) {
        void *l_response = NULL;
        size_t l_response_size = 0, l_hs_consumed = 0;
        int l_rc = dap_tls_mimicry_process_server_hello(l_ctx->mimicry,
                                                          a_es->buf_in, a_es->buf_in_size,
                                                          &l_response, &l_response_size,
                                                          &l_hs_consumed);
        if (l_rc != 0) {
            /* Incomplete or error — wait for more data */
            DAP_DELETE(l_response);
            return;
        }
        /* ServerHello processed — send client CCS + fake Finished */
        if (l_response && l_response_size > 0) {
            dap_events_socket_write_unsafe(a_es, l_response, l_response_size);
            DAP_DELETE(l_response);
        }
        /* Keep any bytes after the handshake (pipelined enc_init reply) */
        if (l_hs_consumed > 0 && l_hs_consumed < a_es->buf_in_size) {
            memmove(a_es->buf_in, a_es->buf_in + l_hs_consumed,
                    a_es->buf_in_size - l_hs_consumed);
            a_es->buf_in_size -= l_hs_consumed;
        } else {
            a_es->buf_in_size = 0;
        }

        log_it(L_NOTICE, "TLS handshake completed — sending enc_init through TLS channel");
        if (s_tls_send_enc_init(l_stream, l_ctx, a_es) == 0) {
            l_ctx->phase = TLS_PHASE_ENC_INIT_WAIT;
            /* Keep EPOLLIN armed; the reactor recv()s the enc_init reply. */
            dap_events_socket_set_readable_unsafe(a_es, true);
        }

        /* Pipelined enc_init reply already in buf_in — process without waiting */
        if (a_es->buf_in_size > 0 && l_ctx->phase == TLS_PHASE_ENC_INIT_WAIT)
            s_tls_read_cb(a_es, a_arg);
        return;
    }

    /* === All other phases: TLS-unwrap incoming records first === */
    void *l_unwrapped = NULL;
    size_t l_unwrapped_size = 0, l_consumed = 0;
    int l_rc = dap_tls_mimicry_unwrap(l_ctx->mimicry,
                                       a_es->buf_in, a_es->buf_in_size,
                                       &l_unwrapped, &l_unwrapped_size, &l_consumed);

    /* Remove consumed bytes from buf_in */
    if (l_consumed > 0 && l_consumed < a_es->buf_in_size) {
        memmove(a_es->buf_in, a_es->buf_in + l_consumed, a_es->buf_in_size - l_consumed);
        a_es->buf_in_size -= l_consumed;
    } else if (l_consumed >= a_es->buf_in_size) {
        a_es->buf_in_size = 0;
    }

    if (l_rc < 0) {
        log_it(L_ERROR, "TLS read (phase %d): unwrap failed (buf_in=%zu)",
               (int)l_ctx->phase, a_es->buf_in_size);
        DAP_DELETE(l_unwrapped);
        return;
    }
    if (l_rc == 1 || !l_unwrapped || l_unwrapped_size == 0) {
        DAP_DELETE(l_unwrapped);
        /* Skipped non-APP_DATA only — try again on remaining bytes */
        if (l_rc == 0 && l_consumed > 0 && a_es->buf_in_size > 0) {
            s_tls_read_cb(a_es, a_arg);
            return;
        }
        if (a_es->buf_in_size >= 5) {
            const uint8_t *b = a_es->buf_in;
            log_it(L_DEBUG, "TLS read (phase %d): incomplete record, buf_in=%zu consumed=%zu "
                   "hdr=%02x %02x %02x %02x %02x — waiting",
                   (int)l_ctx->phase, a_es->buf_in_size, l_consumed,
                   b[0], b[1], b[2], b[3], b[4]);
        } else {
            log_it(L_DEBUG, "TLS read (phase %d): incomplete record, buf_in=%zu consumed=%zu — waiting",
                   (int)l_ctx->phase, a_es->buf_in_size, l_consumed);
        }
        dap_events_socket_set_readable_unsafe(a_es, true);
        return;
    }

    /* === Dispatch the unwrapped payload by phase === */
    switch (l_ctx->phase) {
    case TLS_PHASE_ENC_INIT_WAIT:
        log_it(L_NOTICE, "TLS enc_init response received (%zu bytes)", l_unwrapped_size);
        if (l_ctx->handshake_cb) {
            /* json_tokener_parse needs a NUL-terminated string */
            char *l_json = DAP_NEW_Z_SIZE(char, l_unwrapped_size + 1);
            if (l_json) {
                memcpy(l_json, l_unwrapped, l_unwrapped_size);
                l_ctx->handshake_cb(l_stream, l_json, l_unwrapped_size, 0);
                DAP_DELETE(l_json);
            } else {
                l_ctx->handshake_cb(l_stream, l_unwrapped, l_unwrapped_size, 0);
            }
            l_ctx->handshake_cb = NULL;
        }
        break;

    case TLS_PHASE_STREAM_CTL_WAIT:
        log_it(L_NOTICE, "TLS stream_ctl response received (%zu bytes)", l_unwrapped_size);
        if (l_ctx->session_create_cb) {
            /* Callback takes ownership of l_unwrapped and will DAP_DELETE it. */
            l_ctx->session_create_cb(l_stream, 0,
                                      (const char *)l_unwrapped, l_unwrapped_size, 0);
            l_ctx->session_create_cb = NULL;
            l_ctx->phase = TLS_PHASE_STREAMING;
            l_unwrapped = NULL;  /* ownership transferred */
        }
        break;

    case TLS_PHASE_STREAMING:
        /* Established: feed unwrapped DAP stream packets to the stream layer.
         * If buf_in still has bytes (multiple TLS records coalesced in one
         * read), loop by re-entering so each record is processed in phase. */
        log_it(L_NOTICE, "TLS read (STREAMING): unwrapped %zu bytes, feeding to stream", l_unwrapped_size);
        dap_stream_data_proc_read_ext(l_stream, l_unwrapped, l_unwrapped_size);
        if (a_es->buf_in_size > 0) {
            DAP_DELETE(l_unwrapped);
            s_tls_read_cb(a_es, a_arg);  /* drain remaining records */
            return;
        }
        break;

    default:
        log_it(L_WARNING, "TLS read: unexpected phase %d, dropping %zu bytes",
               (int)l_ctx->phase, l_unwrapped_size);
        break;
    }
    DAP_DELETE(l_unwrapped);
}

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

    /* Set TLS fingerprint profile (TL.8 per-stream rotation) */
    if (a_params->tls_fp_profile_index > 0) {
        const dap_tls_fp_profile_t *l_fp = dap_tls_fp_get_by_index(a_params->tls_fp_profile_index);
        if (l_fp) {
            dap_tls_mimicry_set_profile(l_ctx->mimicry, l_fp);
            debug_if(s_debug_more, L_DEBUG, "TLS handshake_init: using fingerprint profile '%s' (index %u)",
                     l_fp->name, a_params->tls_fp_profile_index);
        }
    } else {
        /* Default: use first profile (chrome_120) if available */
        const dap_tls_fp_profile_t *l_fp = dap_tls_fp_get_by_index(0);
        if (l_fp)
            dap_tls_mimicry_set_profile(l_ctx->mimicry, l_fp);
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

    /* Store mimicry context, handshake callback, and params in transport_priv */
    l_ctx->handshake_cb = a_callback;
    l_ctx->handshake_params = *a_params;
    a_stream->trans_ctx->transport_priv = l_ctx;

    /* Install the single read callback that drives the whole protocol via
     * l_ctx->phase. Do NOT overwrite _inheritor — it's used by
     * s_stream_es_callback_write (expects dap_client_t*) and
     * s_esocket_data_read (expects trans_ctx). The stream is reached via
     * the client→FSM→trans_ctx chain (see s_stream_from_es). */
    a_stream->trans_ctx->stream = a_stream;  /* ensure back-reference exists */
    l_ctx->phase = TLS_PHASE_HANDSHAKE;
    a_stream->esocket->callbacks.read_callback = s_tls_read_cb;

    log_it(L_NOTICE, "TLS handshake_init: ClientHello sent (%zd bytes), awaiting ServerHello",
           l_written);

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
    int l_request_size = snprintf(l_request, sizeof(l_request), "%d", DAP_CLIENT_PROTOCOL_VERSION);
    if (l_request_size < 0 || (size_t)l_request_size >= sizeof(l_request)) {
        log_it(L_ERROR, "TLS session_create: protocol version string too large");
        return -3;
    }

    /* Build stream_ctl URL with channel parameters */
    char *l_suburl = dap_strdup_printf("channels=%s,enc_type=%d,enc_key_size=%zu,enc_headers=%d",
                                        a_params->channels, a_params->enc_type,
                                        a_params->enc_key_size, a_params->enc_headers ? 1 : 0);

    char l_stream_ctl_url[1024] = { '\0' };
    snprintf(l_stream_ctl_url, sizeof(l_stream_ctl_url), "/%s/%s",
             DAP_UPLINK_PATH_STREAM_CTL, l_suburl);

    debug_if(s_debug_more, L_DEBUG, "TLS session_create: sending stream_ctl via TLS channel: %s",
           l_stream_ctl_url);

    /* Build complete HTTP POST request */
    char l_http_post[2048];
    int l_http_post_size = snprintf(l_http_post, sizeof(l_http_post),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: text/text\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "%s",
        l_stream_ctl_url,
        "localhost",  /* Host header (server processes by path) */
        (size_t)l_request_size,
        l_request);

    DAP_DELETE(l_suburl);

    if (l_http_post_size < 0 || (size_t)l_http_post_size >= sizeof(l_http_post)) {
        log_it(L_ERROR, "TLS session_create: HTTP request too large (%d bytes)", l_http_post_size);
        return -3;
    }

    debug_if(s_debug_more, L_DEBUG, "TLS session_create: HTTP POST %d bytes via TLS channel",
           l_http_post_size);

    /* Send through DAP stream transport layer — s_tls_write wraps in TLS records.
     * Server's internal HTTP server processes after TLS unwrap. */
    ssize_t l_sent = dap_stream_trans_write_unsafe(a_stream, l_http_post, (size_t)l_http_post_size);

    if (l_sent < 0) {
        log_it(L_ERROR, "TLS session_create: failed to send stream_ctl via TLS");
        return -3;
    }

    /* Store callback and advance the phase to STREAM_CTL_WAIT. The single
     * read callback s_tls_read_cb already installed on the esocket will route
     * the next unwrapped record to session_create_cb. No callback swap → no
     * race even if the server reply is already in buf_in. */
    tls_mimicry_ctx_t *l_ctx = (tls_mimicry_ctx_t *)a_stream->trans_ctx->transport_priv;
    if (l_ctx) {
        l_ctx->session_create_cb = a_callback;
        l_ctx->phase = TLS_PHASE_STREAM_CTL_WAIT;
        if (a_stream->esocket)
            dap_events_socket_set_readable_unsafe(a_stream->esocket, true);
    }

    log_it(L_NOTICE, "TLS session_create: stream_ctl sent (%zd bytes), awaiting response", l_sent);
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
