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
#include "dap_time.h"
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
#include "dap_tls_dap.h"

#define LOG_TAG "dap_stream_trans_tls"
#include <json-c/json.h>
#include "dap_tls_stream_buffer.h"

static dap_stream_trans_tls_config_t s_config;
static bool s_debug_more = false;

/* Per-layer flow counters (client hot path diagnostics).
 * Counted here: inbound TLS records + unwrapped bytes + partial-record waits.
 * A 10-second summary line replaces the former per-record logs. */
static _Atomic uint64_t s_flow_records = 0;
static _Atomic uint64_t s_flow_bytes = 0;
static _Atomic uint64_t s_flow_partial = 0;
static _Atomic int64_t  s_flow_last_ms = 0;

static int64_t s_flow_now_ms(void)
{
    struct timespec l_ts;
    clock_gettime(CLOCK_MONOTONIC, &l_ts);
    return (int64_t)l_ts.tv_sec * 1000LL + (int64_t)l_ts.tv_nsec / 1000000LL;
}

static void s_flow_count_record(size_t a_bytes, bool a_partial)
{
    if (a_partial) {
        atomic_fetch_add(&s_flow_partial, 1);
        return;
    }
    atomic_fetch_add(&s_flow_records, 1);
    atomic_fetch_add(&s_flow_bytes, a_bytes);

    int64_t l_now = s_flow_now_ms();
    int64_t l_last = atomic_load(&s_flow_last_ms);
    if (l_last == 0) {
        atomic_store(&s_flow_last_ms, l_now);
        return;
    }
    if (l_now - l_last >= 10000 &&
        atomic_compare_exchange_strong(&s_flow_last_ms, &l_last, l_now)) {
        uint64_t l_recs = atomic_load(&s_flow_records);
        uint64_t l_bytes = atomic_load(&s_flow_bytes);
        uint64_t l_partial = atomic_load(&s_flow_partial);
        log_it(L_INFO, "TLS-FLOW: %" DAP_UINT64_FORMAT_U " records, %" DAP_UINT64_FORMAT_U
               " B, %" DAP_UINT64_FORMAT_U " partial waits (window %ld ms)",
               l_recs, l_bytes, l_partial, (long)(l_now - l_last));
    }
}

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
/* Socket-level throughput report for the real WSS/TLS13 transport (5 s).
 * Three boundaries are measured end-to-end (NE flow, stream worker, socket);
 * comparing them localizes a freeze to a single hop. */
static void s_tls_stats_maybe_log(dap_events_socket_t *a_es)
{
    static atomic_llong s_last_log_ms;
    int64_t l_now = dap_get_monotime_ms();
    int64_t l_prev = atomic_load(&s_last_log_ms);
    if (l_now - l_prev < 5000 ||
        !atomic_compare_exchange_strong(&s_last_log_ms, &l_prev, l_now))
        return;
    static atomic_ullong s_prev_out, s_prev_in;
    /* buf_out/buf_in are instantaneous; cumulative byte counters live on the
     * esocket as bytes_sent/bytes_received where available. */
    log_it(L_INFO, "TLS/WSS sock: buf_out=%zu buf_in=%zu (prev out=%llu in=%llu), flags=0x%x",
           a_es ? a_es->buf_out_size : 0, a_es ? a_es->buf_in_size : 0,
           (unsigned long long)atomic_load(&s_prev_out),
           (unsigned long long)atomic_load(&s_prev_in),
           a_es ? a_es->flags : 0);
    atomic_store(&s_prev_out, a_es ? a_es->buf_out_size : 0);
    atomic_store(&s_prev_in, a_es ? a_es->buf_in_size : 0);
}

static ssize_t s_tls_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static ssize_t s_tls_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size);
static void    s_tls_close(dap_stream_t *a_stream);

static int s_tls_handshake_init(dap_stream_t *a_stream,
                                dap_net_handshake_params_t *a_params,
                                dap_net_trans_handshake_cb_t a_callback);
static int s_tls_session_create(dap_stream_t *a_stream, dap_net_session_params_t *a_params,
                                dap_net_trans_session_cb_t a_callback);
static int s_tls_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                               dap_net_trans_ready_cb_t a_callback);

static const dap_net_trans_ops_t s_tls_ops = {
    .init             = s_tls_init,
    .deinit           = s_tls_deinit,
    .stage_prepare    = dap_tls_dap_stage_prepare,
    .write            = s_tls_write,
    .read             = s_tls_read,
    .close            = s_tls_close,
    .get_capabilities = dap_tls_dap_get_caps,
    .handshake_init   = s_tls_handshake_init,
    .session_create   = s_tls_session_create,
    .session_start    = s_tls_session_start
};

/* ========================================================================== */
/*  Registration                                                              */
/* ========================================================================== */

#ifdef DAP_TLS13_OPENSSL
#include "dap_net_trans_tls13.h"
#endif
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
#ifdef DAP_TLS13_OPENSSL
    if (!l_rc) l_rc = dap_net_trans_tls13_register();
#endif
    return l_rc;
}

int dap_stream_trans_tls_unregister(void)
{
    dap_net_trans_tls_server_deinit();
    DAP_DEL_Z(s_config.sni_hostname);
#ifdef DAP_TLS13_OPENSSL
    dap_net_trans_tls13_unregister();
#endif
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
        dap_tls_dap_set_debug_more(s_debug_more);
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

static ssize_t s_tls_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_stream->esocket || !a_data || a_size == 0)
        return -1;

    if (a_stream->esocket->server)
        return dap_net_trans_tls_server_write_unsafe(a_stream->esocket, a_data, a_size);
    if (!a_stream->trans_ctx)
        return -1;

    void *l_wrapped = NULL;
    size_t l_wrapped_size = 0;
    tls_mimicry_ctx_t *l_ctx = (tls_mimicry_ctx_t *)a_stream->trans_ctx->transport_priv;
    if (!l_ctx || !l_ctx->mimicry) {
        return -1;
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
        s_tls_stats_maybe_log(a_stream->esocket);
        return l_ret == (ssize_t)l_wrapped_size ? (ssize_t)a_size : -1;
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
    s_tls_stats_maybe_log(l_es);

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
            free(l_ctx->stream_tail);
            free(l_ctx->enc_reply);
            DAP_DEL_Z(l_ctx->sni_hostname);
            DAP_DELETE(l_ctx);
            a_stream->trans_ctx->transport_priv = NULL;
        }
    }
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
        if (dap_tls_dap_send_enc_init(l_stream, l_ctx, a_es) == 0) {
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
        a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
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
            debug_if(s_debug_more, L_DEBUG, "TLS read (phase %d): incomplete record, buf_in=%zu consumed=%zu "
                   "hdr=%02x %02x %02x %02x %02x — waiting",
                   (int)l_ctx->phase, a_es->buf_in_size, l_consumed,
                   b[0], b[1], b[2], b[3], b[4]);
        } else {
            debug_if(s_debug_more, L_DEBUG, "TLS read (phase %d): incomplete record, buf_in=%zu consumed=%zu — waiting",
                   (int)l_ctx->phase, a_es->buf_in_size, l_consumed);
        }
        s_flow_count_record(0, true);
        dap_events_socket_set_readable_unsafe(a_es, true);
        return;
    }

    /* === Dispatch the unwrapped payload by phase === */
    switch (l_ctx->phase) {
    case TLS_PHASE_ENC_INIT_WAIT: {
        /* A TLS record is not a control-message boundary. Keep the original
         * JSON bytes for the existing DAP callback, including signatures. */
        const size_t limit = 1024U * 1024U;
        if (l_unwrapped_size > limit - l_ctx->enc_reply_size) {
            a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
            break;
        }
        size_t total = l_ctx->enc_reply_size + l_unwrapped_size;
        char *reply = realloc(l_ctx->enc_reply, total + 1);
        if (!reply) {
            a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
            break;
        }
        memcpy(reply + l_ctx->enc_reply_size, l_unwrapped, l_unwrapped_size);
        reply[total] = '\0';
        l_ctx->enc_reply = reply;
        l_ctx->enc_reply_size = total;
        struct json_tokener *tok = json_tokener_new();
        if (!tok) {
            a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
            break;
        }
        json_object *obj = json_tokener_parse_ex(tok, reply, (int)total);
        enum json_tokener_error err = json_tokener_get_error(tok);
        size_t end = json_tokener_get_parse_end(tok);
        while (end < total && (reply[end] == ' ' || reply[end] == '\r' ||
                              reply[end] == '\n' || reply[end] == '\t')) ++end;
        bool complete = err == json_tokener_success && obj &&
                        json_object_is_type(obj, json_type_object) && end == total;
        json_object_put(obj);
        json_tokener_free(tok);
        if (err == json_tokener_continue) break;
        if (!complete) {
            a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
            break;
        }
        log_it(L_NOTICE, "TLS enc_init response received (%zu bytes)", l_unwrapped_size);
        if (l_ctx->handshake_cb) {
            dap_net_trans_handshake_cb_t cb = l_ctx->handshake_cb;
            l_ctx->handshake_cb = NULL;
            l_ctx->enc_reply = NULL;
            l_ctx->enc_reply_size = 0;
            cb(l_stream, reply, total, 0);
            free(reply);
        }
        break;
    }

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
        s_flow_count_record(l_unwrapped_size, false);
        if (s_tls_stream_feed(l_stream, &l_ctx->stream_tail, &l_ctx->stream_tail_size,
                              l_unwrapped, l_unwrapped_size) != 0) {
            a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
            DAP_DELETE(l_unwrapped);
            return;
        }
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
 * Sends stream_ctl HTTP POST through the TLS channel (shared builder
 * dap_tls_dap_send_session_request). The HTTP POST is wrapped in TLS records
 * by s_tls_write (called by DAP stream layer). Server processes it through its
 * internal HTTP server after TLS unwrap.
 */
static int s_tls_session_create(dap_stream_t *a_stream, dap_net_session_params_t *a_params,
                                 dap_net_trans_session_cb_t a_callback)
{
    if (!a_stream || !a_stream->trans_ctx || !a_callback) return -1;
    tls_mimicry_ctx_t *l_ctx = a_stream->trans_ctx->transport_priv;
    if (!l_ctx) return -1;
    l_ctx->session_create_cb = a_callback;
    l_ctx->phase = TLS_PHASE_STREAM_CTL_WAIT;
    int rc = dap_tls_dap_send_session_request(a_stream, a_params);
    if (rc) {
        l_ctx->session_create_cb = NULL;
        if (a_stream->esocket) a_stream->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return rc;
    }

    /* Store callback and advance the phase to STREAM_CTL_WAIT. The single
     * read callback s_tls_read_cb already installed on the esocket will route
     * the next unwrapped record to session_create_cb. No callback swap → no
     * race even if the server reply is already in buf_in. */
    if (l_ctx) {
        l_ctx->session_create_cb = a_callback;
        l_ctx->phase = TLS_PHASE_STREAM_CTL_WAIT;
        if (a_stream->esocket)
            dap_events_socket_set_readable_unsafe(a_stream->esocket, true);
    }

    log_it(L_NOTICE, "TLS session_create: stream_ctl sent, awaiting response");
    return 0;
}

/**
 * @brief Mark the direct TLS stream ready after stream_ctl
 *
 * The TLS server creates and binds the DAP stream while processing stream_ctl.
 * Unlike HTTP and WebSocket, TLS continues on the same socket and must not
 * issue a second GET /stream request.
 */
static int s_tls_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                               dap_net_trans_ready_cb_t a_callback)
{
    if(!a_stream || !a_stream->trans_ctx || !a_stream->esocket)
    {
        log_it(L_ERROR, "TLS session_start: stream is not attached");
        return -1;
    }

    tls_mimicry_ctx_t *l_ctx =
        (tls_mimicry_ctx_t *)a_stream->trans_ctx->transport_priv;
    if(!l_ctx || !l_ctx->mimicry ||
       dap_tls_mimicry_get_state(l_ctx->mimicry) != DAP_TLS_MIMICRY_STATE_ESTABLISHED ||
       l_ctx->phase != TLS_PHASE_STREAMING)
    {
        log_it(L_ERROR, "TLS session_start: transport is not ready (phase=%d)",
               l_ctx ? (int)l_ctx->phase : -1);
        return -2;
    }

    if(a_session_id == 0)
    {
        log_it(L_ERROR, "TLS session_start: invalid session ID");
        return -3;
    }

    debug_if(s_debug_more, L_DEBUG,
             "TLS session_start: direct stream ready (session_id=%u)", a_session_id);
    if(a_callback)
        a_callback(a_stream, 0);
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

