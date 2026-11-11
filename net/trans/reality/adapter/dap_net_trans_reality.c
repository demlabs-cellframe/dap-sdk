/*
 * @file dap_net_trans_reality.c
 * @brief REALITY (VLESS) client transport adapter — the ONLY file in the
 *        reality module that knows about dap-sdk types.
 *
 * Architecture (per adapter/DESIGN.md):
 *   - reality_bridge (port/) runs the blocking REALITY+VLESS session on a
 *     dedicated thread and exposes it as a byte pipe (socketpair) + ctrl pipe.
 *   - The adapter wraps the data fd in a dap_events_socket (wrap_no_add —
 *     the fd is already "connected"), creates the DAP stream over it and
 *     drives the HTTP-DAP phase machine (POST /enc_init, POST /stream_ctl,
 *     GET /stream + takeover) through the pipe — the same wire format the
 *     node's HTTP listener (behind the Xray front on 127.0.0.1:8079) expects.
 *   - Single read callback owns the whole lifecycle (phase machine), same
 *     discipline as s_tls_read_cb (no callback-swap races).
 *
 * Client role only: listen/accept are NULL (server = Xray front).
 */
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "dap_common.h"
#include "dap_config.h"
#include "dap_events_socket.h"
#include "dap_net_trans.h"
#include "dap_net_trans_ctx.h"
#include "dap_client.h"
#include "dap_stream.h"
#include "dap_cert.h"
#include "dap_enc_key.h"
#include "dap_enc.h"
#include "dap_enc_base64.h"
#include "dap_enc_base58.h"

#include "reality/reality.h"
#include "reality_bridge.h"

/* Stream plaintext tail helper (header-only, shared with TLS transports). */
#include "dap_stream_pkt.h"
#include "../tls/dap_tls_stream_buffer.h"

#define LOG_TAG "dap_net_trans_reality"

/* Reality endpoint defaults (overridable via [reality:<host>:<port>] config) */
#define REALITY_DEFAULT_SNI         "www.samsung.com"
#define REALITY_DEFAULT_DEST_HOST   "127.0.0.1"
#define REALITY_DEFAULT_DEST_PORT   8079    /* node's HTTP-DAP listener behind Xray */
#define REALITY_DEFAULT_PORT        443
#define REALITY_CONNECT_TIMEOUT_SEC 15

typedef enum {
    RPH_CONNECTING = 0,     /* bridge handshake in flight */
    RPH_ENC_WAIT,           /* POST /enc_init sent, awaiting HTTP reply */
    RPH_CTL_IDLE,           /* enc done, FSM has not called session_create yet */
    RPH_CTL_WAIT,           /* POST /stream_ctl sent */
    RPH_STREAM_IDLE,        /* ctl done, FSM has not called session_start yet */
    RPH_STREAM_200_WAIT,    /* GET /stream sent, awaiting "HTTP/1.1 200" */
    RPH_STREAMING,          /* raw bidirectional pipe */
    RPH_CLOSED,
} reality_phase_t;

typedef struct reality_adapter_ctx {
    reality_bridge_t *bridge;
    dap_events_socket_t *es;        /* data esocket (FSM/stream owns the fd) */
    dap_events_socket_t *ctrl_es;   /* ctrl esocket (adapter-owned) */
    dap_stream_t *stream;
    dap_client_t *client;
    reality_bridge_cfg_t cfg;
    reality_phase_t phase;

    /* FSM-provided callbacks (installed at stage_prepare); wrappers chain into
     * them with the ORIGINAL arg so FSM bookkeeping (e.g. handshake uuid)
     * keeps working. */
    dap_events_socket_callbacks_t base_cbs;

    /* pending FSM callbacks */
    dap_net_trans_handshake_cb_t handshake_cb;
    dap_net_trans_session_cb_t session_cb;
    dap_net_trans_ready_cb_t ready_cb;

    /* handshake request (ownership of alice_pub_key taken on success) */
    uint8_t *alice;

    /* HTTP response accumulation (headers + Content-Length body) */
    uint8_t *http;
    size_t http_size;
    size_t headers_size;
    size_t content_len;
    bool have_headers;
    int http_status;

    /* pre-takeover stream writes (keepalive/channel pings): buffered and
     * flushed into the tunnel at takeover, same as HTTP transport queues
     * them in esocket buf_out */
    uint8_t *pending;
    size_t pending_size;

    /* stream plaintext tail (STREAMING) */
    uint8_t *tail;
    size_t tail_size;
} reality_adapter_ctx_t;

static int s_reality_registered = 0;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static const char *s_phase_str(reality_phase_t a_phase)
{
    switch (a_phase) {
    case RPH_CONNECTING:      return "CONNECTING";
    case RPH_ENC_WAIT:        return "ENC_WAIT";
    case RPH_CTL_IDLE:        return "CTL_IDLE";
    case RPH_CTL_WAIT:        return "CTL_WAIT";
    case RPH_STREAM_IDLE:     return "STREAM_IDLE";
    case RPH_STREAM_200_WAIT: return "STREAM_200_WAIT";
    case RPH_STREAMING:       return "STREAMING";
    default:                  return "CLOSED";
    }
}

/* Append bytes to the HTTP accumulation buffer. 0 ok, -1 OOM */
static int s_http_append(reality_adapter_ctx_t *a_ctx, const uint8_t *a_data, size_t a_size)
{
    if (!a_size)
        return 0;
    if (a_ctx->http_size + a_size > 4u * 1024u * 1024u) {
        log_it(L_ERROR, "REALITY: HTTP response exceeds 4 MiB limit");
        return -1;
    }
    uint8_t *l_buf = realloc(a_ctx->http, a_ctx->http_size + a_size);
    if (!l_buf) {
        log_it(L_ERROR, "REALITY: HTTP accumulate OOM (%zu bytes)", a_ctx->http_size + a_size);
        return -1;
    }
    memcpy(l_buf + a_ctx->http_size, a_data, a_size);
    a_ctx->http = l_buf;
    a_ctx->http_size += a_size;
    return 0;
}

/* Buffer a pre-takeover stream write. 0 ok, -1 OOM */
static int s_pending_append(reality_adapter_ctx_t *a_ctx, const void *a_data, size_t a_size)
{
    uint8_t *l_buf = realloc(a_ctx->pending, a_ctx->pending_size + a_size);
    if (!l_buf) {
        log_it(L_ERROR, "REALITY: pending queue OOM (%zu bytes)", a_ctx->pending_size + a_size);
        return -1;
    }
    memcpy(l_buf + a_ctx->pending_size, a_data, a_size);
    a_ctx->pending = l_buf;
    a_ctx->pending_size += a_size;
    return 0;
}

/**
 * Try to complete the accumulated HTTP response.
 * @param a_headers_only  when true, completion = end of the header block and
 *        a_body/a_body_size describe EVERYTHING after it (raw stream bytes).
 *        Used for GET /stream: the node switches the connection to raw stream
 *        mode right after the 200, so Content-Length does NOT describe a body
 *        we may wait for (waiting for it stalled the takeover by ~15 s, with
 *        keepalive pings mistaken for body bytes).
 * @return 1 complete (a_body/a_body_size set), 0 need more, -1 protocol error
 */
static int s_http_try_complete(reality_adapter_ctx_t *a_ctx, bool a_headers_only,
                               const uint8_t **a_body, size_t *a_body_size)
{
    if (!a_ctx->have_headers) {
        char *l_hdr_end = NULL;
        size_t l_search = a_ctx->http_size;
        for (size_t i = 0; i + 3 < l_search; i++) {
            if (a_ctx->http[i] == '\r' && a_ctx->http[i + 1] == '\n' &&
                a_ctx->http[i + 2] == '\r' && a_ctx->http[i + 3] == '\n') {
                l_hdr_end = (char *)a_ctx->http + i;
                a_ctx->headers_size = i + 4;
                break;
            }
        }
        if (!l_hdr_end)
            return 0; /* headers incomplete */
        /* status line */
        char l_status[64] = { 0 };
        size_t l_line = a_ctx->headers_size < sizeof(l_status) - 1 ?
                        a_ctx->headers_size : sizeof(l_status) - 1;
        memcpy(l_status, a_ctx->http, l_line);
        l_status[l_line] = '\0';
        if (sscanf(l_status, "HTTP/%*s %d", &a_ctx->http_status) != 1)
            a_ctx->http_status = 0;
        /* content-length (case-insensitive scan of the header block) */
        static const char s_cl[] = "content-length:";
        char *l_head = (char *)a_ctx->http;
        for (size_t i = 0; i + sizeof(s_cl) - 1 < a_ctx->headers_size; i++) {
            size_t j = 0;
            for (; j < sizeof(s_cl) - 1; j++) {
                char l_c = l_head[i + j];
                if (l_c >= 'A' && l_c <= 'Z')
                    l_c = (char)(l_c - 'A' + 'a');
                if (l_c != s_cl[j])
                    break;
            }
            if (j == sizeof(s_cl) - 1) {
                a_ctx->content_len = (size_t)strtoul(l_head + i + j, NULL, 10);
                break;
            }
        }
        a_ctx->have_headers = true;
        debug_if(true, L_DEBUG, "REALITY: HTTP status %d, content-length %zu",
                 a_ctx->http_status, a_ctx->content_len);
    }
    if (a_ctx->http_status >= 400 || !a_ctx->http_status) {
        log_it(L_ERROR, "REALITY: HTTP error response %d", a_ctx->http_status);
        return -1;
    }
    size_t l_body_size = a_ctx->http_size > a_ctx->headers_size ?
                         a_ctx->http_size - a_ctx->headers_size : 0;
    if (a_headers_only) {
        *a_body = a_ctx->http + a_ctx->headers_size;
        *a_body_size = l_body_size;   /* raw stream bytes, may be 0 */
        return 1;
    }
    if (l_body_size < a_ctx->content_len) {
        debug_if(true, L_DEBUG, "REALITY: HTTP body %zu/%zu — waiting",
                 l_body_size, a_ctx->content_len);
        return 0; /* body incomplete */
    }
    *a_body = a_ctx->http + a_ctx->headers_size;
    *a_body_size = a_ctx->content_len;
    return 1;
}

static void s_http_reset(reality_adapter_ctx_t *a_ctx)
{
    DAP_DEL_Z(a_ctx->http);
    a_ctx->http_size = 0;
    a_ctx->headers_size = 0;
    a_ctx->content_len = 0;
    a_ctx->have_headers = false;
    a_ctx->http_status = 0;
}

/* Tear the tunnel down: fire nothing (FSM learns via esocket delete), just
 * mark the data esocket for close so the FSM error path runs. */
static void s_reality_fail(reality_adapter_ctx_t *a_ctx, const char *a_why)
{
    if (!a_ctx || a_ctx->phase == RPH_CLOSED)
        return;
    log_it(L_ERROR, "REALITY: %s (phase %s)", a_why, s_phase_str(a_ctx->phase));
    a_ctx->phase = RPH_CLOSED;
    if (a_ctx->bridge) {
        reality_bridge_signal_stop(a_ctx->bridge);
    }
    if (a_ctx->es)
        a_ctx->es->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

/* ------------------------------------------------------------------ */
/* esocket callback wrappers                                           */
/* ------------------------------------------------------------------ */

static void s_reality_es_delete(dap_events_socket_t *a_es, void *a_arg);

static void s_reality_es_error(dap_events_socket_t *a_es, int a_err)
{
    reality_adapter_ctx_t *l_ctx = (reality_adapter_ctx_t *)a_es->callbacks.arg;
    log_it(L_WARNING, "REALITY: data socket error %d (phase %s)",
           a_err, l_ctx ? s_phase_str(l_ctx->phase) : "?");
    if (l_ctx && l_ctx->base_cbs.error_callback)
        l_ctx->base_cbs.error_callback(a_es, a_err);
    s_reality_fail(l_ctx, "data socket error");
}

static void s_reality_es_delete(dap_events_socket_t *a_es, void *a_arg)
{
    reality_adapter_ctx_t *l_ctx = a_arg;
    debug_if(true, L_DEBUG, "REALITY: data esocket delete (ctx=%p)", l_ctx);
    if (l_ctx) {
        if (l_ctx->base_cbs.delete_callback)
            l_ctx->base_cbs.delete_callback(a_es, l_ctx->base_cbs.arg);
        if (l_ctx->bridge) {
            reality_bridge_stop(l_ctx->bridge);
            l_ctx->bridge = NULL;
        }
        if (l_ctx->ctrl_es) {
            dap_events_socket_t *l_ctrl = l_ctx->ctrl_es;
            l_ctx->ctrl_es = NULL;
            memset(&l_ctrl->callbacks, 0, sizeof(l_ctrl->callbacks));
            if (l_ctrl->worker)
                dap_events_socket_remove_and_delete_unsafe(l_ctrl, true);
            else
                dap_events_socket_delete_unsafe(l_ctrl, true);
        }
        DAP_DEL_Z(l_ctx->alice);
        s_http_reset(l_ctx);
        DAP_DEL_Z(l_ctx->tail);
        DAP_DEL_Z(l_ctx->pending);
        l_ctx->pending_size = 0;
        DAP_DELETE(l_ctx);
    }
}

static void s_reality_ctrl_read(dap_events_socket_t *a_es, void *a_arg)
{
    reality_adapter_ctx_t *l_ctx = a_arg;
    if (!l_ctx || !a_es->buf_in_size)
        return;
    for (size_t i = 0; i < a_es->buf_in_size; i++) {
        char l_ev = (char)a_es->buf_in[i];
        switch (l_ev) {
        case 'R':
            debug_if(true, L_DEBUG, "REALITY: bridge ready");
            break;
        case 'E': {
            int l_err = 0;
            if (i + 1 < a_es->buf_in_size)
                l_err = -(int)(signed char)a_es->buf_in[++i];
            log_it(L_ERROR, "REALITY: bridge error %d", l_err);
            a_es->buf_in_size = 0;
            s_reality_fail(l_ctx, "bridge error");
            return;
        }
        case 'C':
            a_es->buf_in_size = 0;
            s_reality_fail(l_ctx, "bridge closed");
            return;
        default:
            break;
        }
    }
    a_es->buf_in_size = 0;
}

static void s_reality_ctrl_delete(dap_events_socket_t *a_es, void *a_arg)
{
    reality_adapter_ctx_t *l_ctx = a_arg;
    debug_if(true, L_DEBUG, "REALITY: ctrl esocket delete");
    if (l_ctx)
        l_ctx->ctrl_es = NULL;
    /* no chained callbacks: ctrl socket is adapter-internal */
}

/* ------------------------------------------------------------------ */
/* read callback: single lifecycle phase machine (on buf_in)            */
/* ------------------------------------------------------------------ */

static void s_reality_read_cb(dap_events_socket_t *a_es, void *a_arg)
{
    reality_adapter_ctx_t *l_ctx = a_arg;
    if (!l_ctx || !a_es->buf_in_size)
        return;
    debug_if(true, L_DEBUG, "REALITY: read event %zu bytes (phase %s, http_buf %zu)",
             a_es->buf_in_size, s_phase_str(l_ctx->phase), l_ctx->http_size);

    switch (l_ctx->phase) {
    case RPH_ENC_WAIT:
    case RPH_CTL_WAIT: {
        if (s_http_append(l_ctx, a_es->buf_in, a_es->buf_in_size)) {
            a_es->buf_in_size = 0;
            s_reality_fail(l_ctx, "HTTP accumulate failure");
            return;
        }
        a_es->buf_in_size = 0;

        const uint8_t *l_body = NULL;
        size_t l_body_size = 0;
        int l_rc = s_http_try_complete(l_ctx, false, &l_body, &l_body_size);
        if (l_rc == 0)
            return;             /* need more bytes */
        if (l_rc < 0) {
            s_reality_fail(l_ctx, "bad HTTP response");
            return;
        }

        if (l_ctx->phase == RPH_ENC_WAIT) {
            log_it(L_NOTICE, "REALITY: enc_init response received (%zu bytes)", l_body_size);
            dap_net_trans_handshake_cb_t l_cb = l_ctx->handshake_cb;
            l_ctx->handshake_cb = NULL;
            l_ctx->phase = RPH_CTL_IDLE;
            s_http_reset(l_ctx);
            if (l_cb) {
                /* callback copies synchronously (same contract as TLS) */
                l_cb(l_ctx->stream, l_body, l_body_size, 0);
            }
        } else if (l_ctx->phase == RPH_CTL_WAIT) {
            /* stream_ctl body is encrypted with the symmetric session key */
            dap_net_trans_ctx_t *l_tc = l_ctx->stream ? l_ctx->stream->trans_ctx : NULL;
            if (!l_tc || !l_tc->session_key) {
                s_reality_fail(l_ctx, "no session key for stream_ctl reply");
                return;
            }
            size_t l_calc = dap_enc_decode_out_size(l_tc->session_key, l_body_size,
                                                    DAP_ENC_DATA_TYPE_RAW);
            size_t l_buf_size = dap_max(l_calc, l_body_size) + 32;
            char *l_plain = DAP_NEW_Z_SIZE(char, l_buf_size);
            if (!l_plain) {
                s_reality_fail(l_ctx, "stream_ctl reply OOM");
                return;
            }
            size_t l_plain_size = dap_enc_decode(l_tc->session_key, l_body, l_body_size,
                                                 l_plain, l_buf_size, DAP_ENC_DATA_TYPE_RAW);
            if (!l_plain_size || l_plain_size > l_buf_size) {
                DAP_DELETE(l_plain);
                s_reality_fail(l_ctx, "stream_ctl reply decrypt failed");
                return;
            }
            l_plain[l_plain_size] = '\0';
            log_it(L_NOTICE, "REALITY: stream_ctl response received (%zu bytes)", l_plain_size);
            dap_net_trans_session_cb_t l_cb = l_ctx->session_cb;
            l_ctx->session_cb = NULL;
            l_ctx->phase = RPH_STREAM_IDLE;
            s_http_reset(l_ctx);
            if (l_cb) {
                /* callback takes ownership of the buffer */
                l_cb(l_ctx->stream, 0, l_plain, l_plain_size, 0);
            } else {
                DAP_DELETE(l_plain);
            }
        }
        break;
    }

    case RPH_STREAM_200_WAIT: {
        if (s_http_append(l_ctx, a_es->buf_in, a_es->buf_in_size)) {
            a_es->buf_in_size = 0;
            s_reality_fail(l_ctx, "HTTP accumulate failure");
            return;
        }
        a_es->buf_in_size = 0;

        /* Headers-only completion: the node switches the connection to raw
         * stream mode right after the "200", so everything after the header
         * block is DAP stream data, NOT an HTTP body. Waiting for
         * Content-Length here stalled the takeover by ~15 s (stream keepalive
         * pings were mistaken for body bytes) and discarded real stream data.
         * Same contract as the HTTP transport (s_http_trans_read). */
        const uint8_t *l_raw = NULL;
        size_t l_raw_size = 0;
        int l_rc = s_http_try_complete(l_ctx, true, &l_raw, &l_raw_size);
        if (l_rc == 0)
            return;                 /* header block not complete yet */
        if (l_rc < 0 || l_ctx->http_status < 200 || l_ctx->http_status >= 300) {
            s_reality_fail(l_ctx, "stream GET was not accepted");
            return;
        }

        log_it(L_NOTICE, "REALITY: stream takeover (HTTP %d)", l_ctx->http_status);
        dap_net_trans_ready_cb_t l_cb = l_ctx->ready_cb;
        l_ctx->ready_cb = NULL;
        l_ctx->phase = RPH_STREAMING;

        /* flush writes buffered before takeover (channel pings, keepalive) */
        if (l_ctx->pending_size) {
            debug_if(true, L_DEBUG, "REALITY: flushing %zu pre-takeover bytes", l_ctx->pending_size);
            dap_events_socket_write_unsafe(l_ctx->es, l_ctx->pending, l_ctx->pending_size);
            DAP_DEL_Z(l_ctx->pending);
            l_ctx->pending_size = 0;
        }
        if (l_cb)
            l_cb(l_ctx->stream, 0);
        /* Stream bytes that arrived together with the headers. Feed BEFORE
         * s_http_reset(): l_raw points into the accumulation buffer. */
        if (l_raw_size) {
            debug_if(true, L_DEBUG, "REALITY: %zu stream bytes with takeover headers", l_raw_size);
            if (s_tls_stream_feed(l_ctx->stream, &l_ctx->tail, &l_ctx->tail_size,
                                  l_raw, l_raw_size) != 0) {
                s_http_reset(l_ctx);
                s_reality_fail(l_ctx, "stream feed failure");
                return;
            }
        }
        s_http_reset(l_ctx);
        break;
    }

    case RPH_STREAMING:
    case RPH_CTL_IDLE:
    case RPH_STREAM_IDLE: {
        /* In IDLE phases the node may still push stream packets (e.g. on an
         * already-established session) — feed them, same as STREAMING. */
        if (l_ctx->phase == RPH_STREAMING || l_ctx->phase == RPH_STREAM_IDLE) {
            const uint8_t *l_data = a_es->buf_in;
            size_t l_size = a_es->buf_in_size;
            a_es->buf_in_size = 0;
            if (s_tls_stream_feed(l_ctx->stream, &l_ctx->tail, &l_ctx->tail_size,
                                  l_data, l_size) != 0) {
                s_reality_fail(l_ctx, "stream feed failure");
                return;
            }
        } else {
            /* RPH_CTL_IDLE with unexpected bytes: drop (server push before ctl) */
            log_it(L_WARNING, "REALITY: dropping %zu unexpected bytes in %s",
                   a_es->buf_in_size, s_phase_str(l_ctx->phase));
            a_es->buf_in_size = 0;
        }
        break;
    }

    case RPH_CONNECTING:
    default:
        log_it(L_WARNING, "REALITY: %zu bytes in phase %s — dropped",
               a_es->buf_in_size, s_phase_str(l_ctx->phase));
        a_es->buf_in_size = 0;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* request builders (wire format = node's HTTP-DAP listener)            */
/* ------------------------------------------------------------------ */

static int s_reality_send_enc_init(reality_adapter_ctx_t *a_ctx, dap_net_handshake_params_t *a_params)
{
    if (!a_params->alice_pub_key || !a_params->alice_pub_key_size) {
        log_it(L_ERROR, "REALITY: empty alice public key");
        return -1;
    }
    a_ctx->alice = a_params->alice_pub_key; /* ownership taken (FSM contract) */

    /* Node address: b58 of the stream node addr, legacy placeholder fallback
     * (same logic as the TLS mimicry transport). */
    char l_node_addr_b58[32] = "gd4y5yh78w42aaagh";
    if (a_ctx->stream->node.uint64) {
        uint64_t l_addr_le = a_ctx->stream->node.uint64;
        size_t l_b58_len = dap_enc_base58_encode(&l_addr_le, sizeof(l_addr_le), l_node_addr_b58);
        if (!l_b58_len)
            dap_strncpy(l_node_addr_b58, "gd4y5yh78w42aaagh", sizeof(l_node_addr_b58) - 1);
    }

    size_t l_b64_size = DAP_ENC_BASE64_ENCODE_SIZE(a_params->alice_pub_key_size) + 1;
    char *l_body = DAP_NEW_Z_SIZE(char, l_b64_size);
    if (!l_body)
        return -1;
    size_t l_body_size = dap_enc_base64_encode(a_params->alice_pub_key, a_params->alice_pub_key_size,
                                               l_body, DAP_ENC_DATA_TYPE_B64);

    char *l_req = dap_strdup_printf(
        "POST /%s/%s?enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zu,"
        "block_key_size=%zu,protocol_version=%d,sign_count=%zu HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: text/text\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "%s",
        DAP_UPLINK_PATH_ENC_INIT, l_node_addr_b58,
        a_params->enc_type, a_params->pkey_exchange_type, a_params->pkey_exchange_size,
        a_params->block_key_size, a_params->protocol_version, a_params->sign_count,
        a_ctx->client && a_ctx->client->link_info.uplink_addr[0] ?
            a_ctx->client->link_info.uplink_addr : REALITY_DEFAULT_DEST_HOST,
        l_body_size, l_body);
    DAP_DELETE(l_body);
    if (!l_req) {
        log_it(L_ERROR, "REALITY: enc_init request build failed");
        return -1;
    }
    size_t l_req_size = dap_strlen(l_req);
    ssize_t l_sent = dap_events_socket_write_unsafe(a_ctx->es, l_req, l_req_size);
    DAP_DELETE(l_req);
    if (l_sent != (ssize_t)l_req_size) {
        log_it(L_ERROR, "REALITY: enc_init send failed (%zd/%zu)", l_sent, l_req_size);
        return -1;
    }
    debug_if(true, L_DEBUG, "REALITY: enc_init sent (%zu bytes), host=%s port=%u sni=%s",
             l_req_size, a_ctx->cfg.host, a_ctx->cfg.port, a_ctx->cfg.rcfg.sni);
    return 0;
}

static int s_reality_send_stream_ctl(reality_adapter_ctx_t *a_ctx, dap_net_session_params_t *a_params)
{
    dap_net_trans_ctx_t *l_tc = a_ctx->stream ? a_ctx->stream->trans_ctx : NULL;
    if (!l_tc || !l_tc->session_key) {
        log_it(L_ERROR, "REALITY: stream_ctl without session key");
        return -1;
    }

    char *l_suburl;
    if (l_tc->remote_protocol_version < 23) {
        l_suburl = dap_strdup_printf("stream_ctl,channels=%s", a_params->channels);
    } else {
        l_suburl = dap_strdup_printf("channels=%s,enc_type=%d,enc_key_size=%zu,enc_headers=%d",
                                     a_params->channels, a_params->enc_type,
                                     a_params->enc_key_size, a_params->enc_headers ? 1 : 0);
    }
    if (!l_suburl)
        return -1;
    const char *l_query = "type=tcp,maxconn=4";
    const char *l_path = DAP_UPLINK_PATH_STREAM_CTL;

    dap_enc_data_type_t l_enc_type = l_tc->uplink_protocol_version >= 21
        ? DAP_ENC_DATA_TYPE_B64_URLSAFE : DAP_ENC_DATA_TYPE_B64;

    size_t l_suburl_enc_size = dap_enc_code_out_size(l_tc->session_key, dap_strlen(l_suburl), l_enc_type);
    size_t l_query_enc_size = dap_enc_code_out_size(l_tc->session_key, dap_strlen(l_query), l_enc_type);
    size_t l_path_size = dap_strlen(l_path) + 1 + l_suburl_enc_size + 1 + l_query_enc_size + 1;
    char *l_full_path = DAP_NEW_Z_SIZE(char, l_path_size);
    if (!l_full_path) {
        DAP_DELETE(l_suburl);
        return -1;
    }
    char *l_off = dap_strncpy(l_full_path, l_path, l_path_size);
    *l_off++ = '/';
    l_off += dap_enc_code(l_tc->session_key, l_suburl, dap_strlen(l_suburl),
                          l_off, l_suburl_enc_size, l_enc_type);
    *l_off++ = '?';
    dap_enc_code(l_tc->session_key, l_query, dap_strlen(l_query),
                 l_off, l_query_enc_size, l_enc_type);
    DAP_DELETE(l_suburl);

    char l_body[16];
    size_t l_body_size = (size_t)snprintf(l_body, sizeof(l_body), "%d", DAP_CLIENT_PROTOCOL_VERSION);
    size_t l_body_enc_size = dap_enc_code_out_size(l_tc->session_key, l_body_size,
                                                   DAP_ENC_DATA_TYPE_RAW) + 1;
    char *l_body_enc = DAP_NEW_Z_SIZE(char, l_body_enc_size);
    if (!l_body_enc) {
        DAP_DELETE(l_full_path);
        return -1;
    }
    dap_enc_code(l_tc->session_key, l_body, l_body_size, l_body_enc, l_body_enc_size,
                 DAP_ENC_DATA_TYPE_RAW);

    char *l_req = dap_strdup_printf(
        "POST /%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: text/text\r\n"
        "KeyID: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        l_full_path,
        a_ctx->client && a_ctx->client->link_info.uplink_addr[0] ?
            a_ctx->client->link_info.uplink_addr : REALITY_DEFAULT_DEST_HOST,
        l_tc->session_key_id ? l_tc->session_key_id : "NULL",
        l_body_enc_size - 1);
    if (!l_req) {
        DAP_DELETE(l_full_path);
        DAP_DELETE(l_body_enc);
        return -1;
    }
    size_t l_req_size = dap_strlen(l_req);

    /* queue header, then encrypted body (single write is not guaranteed to
     * fit the esocket output buffer; two writes keep the code simple) */
    ssize_t l_sent = dap_events_socket_write_unsafe(a_ctx->es, l_req, l_req_size);
    if (l_sent == (ssize_t)l_req_size)
        l_sent = dap_events_socket_write_unsafe(a_ctx->es, l_body_enc, l_body_enc_size - 1);
    DAP_DELETE(l_req);
    DAP_DELETE(l_full_path);
    DAP_DELETE(l_body_enc);
    if (l_sent <= 0) {
        log_it(L_ERROR, "REALITY: stream_ctl send failed");
        return -1;
    }
    debug_if(true, L_DEBUG, "REALITY: stream_ctl sent (%zu + %zu bytes)",
             l_req_size, l_body_enc_size - 1);
    return 0;
}

static int s_reality_send_stream_get(reality_adapter_ctx_t *a_ctx, uint32_t a_session_id)
{
    char *l_req = dap_strdup_printf(
        "GET /%s/globaldb?session_id=%u HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        DAP_UPLINK_PATH_STREAM, a_session_id,
        a_ctx->client && a_ctx->client->link_info.uplink_addr[0] ?
            a_ctx->client->link_info.uplink_addr : REALITY_DEFAULT_DEST_HOST);
    if (!l_req)
        return -1;
    size_t l_req_size = dap_strlen(l_req);
    ssize_t l_sent = dap_events_socket_write_unsafe(a_ctx->es, l_req, l_req_size);
    DAP_DELETE(l_req);
    if (l_sent != (ssize_t)l_req_size) {
        log_it(L_ERROR, "REALITY: stream GET send failed");
        return -1;
    }
    log_it(L_INFO, "REALITY: stream GET queued (session_id=%u), waiting for 200", a_session_id);
    return 0;
}

/* ------------------------------------------------------------------ */
/* ops vtable                                                          */
/* ------------------------------------------------------------------ */

static int s_reality_stage_prepare(dap_net_trans_t *a_trans,
                                   const dap_net_stage_prepare_params_t *a_params,
                                   dap_net_stage_prepare_result_t *a_result)
{
    if (!a_params || !a_params->host || !a_result)
        return -1;
    memset(a_result, 0, sizeof(*a_result));
    a_result->error_code = -1;

    dap_client_t *l_client = (dap_client_t *)a_params->client_ctx;
    reality_adapter_ctx_t *l_ctx = DAP_NEW_Z(reality_adapter_ctx_t);
    if (!l_ctx) {
        log_it(L_CRITICAL, "REALITY: ctx OOM");
        return -1;
    }
    l_ctx->client = l_client;
    l_ctx->phase = RPH_CONNECTING;
    l_ctx->base_cbs = *a_params->callbacks;
    l_ctx->cfg.port = a_params->port ? a_params->port : REALITY_DEFAULT_PORT;
    l_ctx->cfg.dest_port = REALITY_DEFAULT_DEST_PORT;
    dap_strncpy(l_ctx->cfg.dest_host, REALITY_DEFAULT_DEST_HOST, sizeof(l_ctx->cfg.dest_host) - 1);
    dap_strncpy(l_ctx->cfg.host, a_params->host, sizeof(l_ctx->cfg.host) - 1);
    l_ctx->cfg.timeout_sec = REALITY_CONNECT_TIMEOUT_SEC;

    /* Endpoint trust: per-client fields first (IPC), config fallback */
    const char *l_uuid = l_client && l_client->reality_uuid[0] ? l_client->reality_uuid : NULL;
    const char *l_pub = l_client && l_client->reality_public_key[0] ? l_client->reality_public_key : NULL;
    const char *l_short = l_client && l_client->reality_short_id[0] ? l_client->reality_short_id : NULL;
    const char *l_sni = l_client && l_client->reality_sni[0] ? l_client->reality_sni : NULL;

    char l_section[512];
    uint32_t l_rate_limit = 0;      /* bytes/sec, 0 = unlimited */
    snprintf(l_section, sizeof(l_section), "reality:%s:%u", a_params->host, l_ctx->cfg.port);
    if (g_config) {
        if (!l_uuid)
            l_uuid = dap_config_get_item_str(g_config, l_section, "uuid");
        if (!l_pub)
            l_pub = dap_config_get_item_str(g_config, l_section, "public_key");
        if (!l_short)
            l_short = dap_config_get_item_str(g_config, l_section, "short_id");
        if (!l_sni)
            l_sni = dap_config_get_item_str(g_config, l_section, "sni");
        const char *l_dest = dap_config_get_item_str(g_config, l_section, "dest_host");
        if (l_dest && *l_dest)
            dap_strncpy(l_ctx->cfg.dest_host, l_dest, sizeof(l_ctx->cfg.dest_host) - 1);
        uint16_t l_dest_port = dap_config_get_item_uint16_default(g_config, l_section, "dest_port", 0);
        if (l_dest_port)
            l_ctx->cfg.dest_port = l_dest_port;
        l_rate_limit = (uint32_t)dap_config_get_item_uint64_default(g_config, l_section, "rate_limit", 0);
        const char *l_iface = dap_config_get_item_str(g_config, l_section, "iface");
        if (l_iface && *l_iface)
            dap_strncpy(l_ctx->cfg.iface, l_iface, sizeof(l_ctx->cfg.iface) - 1);
    }
    /* Per-client cap wins over the config file (GUI setting, delivered via IPC).
     * 0 = unlimited — the default; DPI research suggests 5-6 MB/s on cellular. */
    if (l_client && l_client->reality_rate_limit)
        l_rate_limit = l_client->reality_rate_limit;
    l_ctx->cfg.rcfg.rate_limit_bytes_per_sec = l_rate_limit;
    if (!l_sni || !*l_sni)
        l_sni = REALITY_DEFAULT_SNI;

    if (!l_uuid || strlen(l_uuid) != REALITY_UUID_STR) {
        log_it(L_ERROR, "REALITY: endpoint %s has no valid uuid (config [%s])", a_params->host, l_section);
        DAP_DELETE(l_ctx);
        return -1;
    }
    if (!l_pub || strlen(l_pub) < 40 || strlen(l_pub) > REALITY_KEY_B64URL) {
        log_it(L_ERROR, "REALITY: endpoint %s has no valid public_key (config [%s])", a_params->host, l_section);
        DAP_DELETE(l_ctx);
        return -1;
    }
    if (!l_short || !*l_short || strlen(l_short) > 2 * REALITY_SHORTID_MAX) {
        log_it(L_ERROR, "REALITY: endpoint %s has no valid short_id (config [%s])", a_params->host, l_section);
        DAP_DELETE(l_ctx);
        return -1;
    }
    dap_strncpy(l_ctx->cfg.rcfg.uuid, l_uuid, sizeof(l_ctx->cfg.rcfg.uuid) - 1);
    dap_strncpy(l_ctx->cfg.rcfg.public_key_b64url, l_pub, sizeof(l_ctx->cfg.rcfg.public_key_b64url) - 1);
    dap_strncpy(l_ctx->cfg.rcfg.short_id_hex, l_short, sizeof(l_ctx->cfg.rcfg.short_id_hex) - 1);
    dap_strncpy(l_ctx->cfg.rcfg.sni, l_sni, sizeof(l_ctx->cfg.rcfg.sni) - 1);

    l_ctx->bridge = reality_bridge_start(&l_ctx->cfg);
    if (!l_ctx->bridge) {
        log_it(L_ERROR, "REALITY: bridge start failed for %s:%u", l_ctx->cfg.host, l_ctx->cfg.port);
        DAP_DELETE(l_ctx);
        return -1;
    }
    int l_data_fd = reality_bridge_data_fd(l_ctx->bridge);
    int l_ctrl_fd = reality_bridge_ctrl_fd(l_ctx->bridge);
    if (l_data_fd < 0 || l_ctrl_fd < 0) {
        log_it(L_ERROR, "REALITY: bridge fds unavailable");
        reality_bridge_stop(l_ctx->bridge);
        l_ctx->bridge = NULL;
        DAP_DELETE(l_ctx);
        return -1;
    }

    dap_events_socket_callbacks_t l_cbs = {
        .read_callback   = s_reality_read_cb,
        .write_callback  = NULL,           /* reactor flushes buf_out itself */
        .error_callback  = s_reality_es_error,
        .delete_callback = s_reality_es_delete,
        .connected_callback = NULL,
        .arg             = l_ctx,
    };
    dap_events_socket_t *l_es = dap_events_socket_wrap_no_add((SOCKET)l_data_fd, &l_cbs);
    if (!l_es) {
        log_it(L_ERROR, "REALITY: data esocket wrap failed");
        reality_bridge_stop(l_ctx->bridge);
        l_ctx->bridge = NULL;
        DAP_DELETE(l_ctx);
        return -1;
    }
    l_es->_inheritor = a_params->client_ctx;
    l_es->flags |= DAP_SOCK_READY_TO_WRITE;
    l_ctx->es = l_es;
    if (a_params->worker)
        dap_worker_add_events_socket(a_params->worker, l_es);

    dap_events_socket_callbacks_t l_ctrl_cbs = {
        .read_callback   = s_reality_ctrl_read,
        .write_callback  = NULL,
        .error_callback  = NULL,
        .delete_callback = s_reality_ctrl_delete,
        .connected_callback = NULL,
        .arg             = l_ctx,
    };
    l_ctx->ctrl_es = dap_events_socket_wrap_no_add((SOCKET)l_ctrl_fd, &l_ctrl_cbs);
    if (!l_ctx->ctrl_es) {
        log_it(L_ERROR, "REALITY: ctrl esocket wrap failed");
        memset(&l_es->callbacks, 0, sizeof(l_es->callbacks));
        if (l_es->worker)
            dap_events_socket_remove_and_delete_unsafe(l_es, true);
        else
            dap_events_socket_delete_unsafe(l_es, true);
        l_ctx->es = NULL;
        reality_bridge_stop(l_ctx->bridge);
        l_ctx->bridge = NULL;
        DAP_DELETE(l_ctx);
        return -1;
    }
    if (a_params->worker)
        dap_worker_add_events_socket(a_params->worker, l_ctx->ctrl_es);

    /* Staging trans_ctx (FSM adopts transport_priv into its own ctx). */
    dap_net_trans_ctx_t *l_staging = DAP_NEW_Z(dap_net_trans_ctx_t);
    if (!l_staging) {
        log_it(L_CRITICAL, "REALITY: staging trans_ctx OOM");
        memset(&l_es->callbacks, 0, sizeof(l_es->callbacks));
        if (l_es->worker)
            dap_events_socket_remove_and_delete_unsafe(l_es, true);
        else
            dap_events_socket_delete_unsafe(l_es, true);
        l_ctx->es = NULL;
        memset(&l_ctx->ctrl_es->callbacks, 0, sizeof(l_ctx->ctrl_es->callbacks));
        if (l_ctx->ctrl_es->worker)
            dap_events_socket_remove_and_delete_unsafe(l_ctx->ctrl_es, true);
        else
            dap_events_socket_delete_unsafe(l_ctx->ctrl_es, true);
        l_ctx->ctrl_es = NULL;
        reality_bridge_stop(l_ctx->bridge);
        l_ctx->bridge = NULL;
        DAP_DELETE(l_ctx);
        return -1;
    }
    l_staging->transport_priv = l_ctx;
    l_staging->stream = NULL; /* FSM assigns its own ctx; stream set below */

    dap_stream_t *l_stream = dap_stream_new_es_client(l_es,
                                (dap_stream_node_addr_t *)a_params->node_addr,
                                a_params->authorized);
    if (!l_stream) {
        log_it(L_ERROR, "REALITY: stream creation failed");
        DAP_DELETE(l_staging);
        memset(&l_es->callbacks, 0, sizeof(l_es->callbacks));
        if (l_es->worker)
            dap_events_socket_remove_and_delete_unsafe(l_es, true);
        else
            dap_events_socket_delete_unsafe(l_es, true);
        l_ctx->es = NULL;
        memset(&l_ctx->ctrl_es->callbacks, 0, sizeof(l_ctx->ctrl_es->callbacks));
        if (l_ctx->ctrl_es->worker)
            dap_events_socket_remove_and_delete_unsafe(l_ctx->ctrl_es, true);
        else
            dap_events_socket_delete_unsafe(l_ctx->ctrl_es, true);
        l_ctx->ctrl_es = NULL;
        reality_bridge_stop(l_ctx->bridge);
        l_ctx->bridge = NULL;
        DAP_DELETE(l_ctx);
        return -1;
    }
    l_stream->trans = a_trans;
    l_ctx->stream = l_stream;
    l_staging->stream = l_stream;
    l_staging->esocket = l_es;
    l_staging->trans = a_trans;
    l_stream->trans_ctx = l_staging;

    a_result->esocket = l_es;
    a_result->stream = l_stream;
    a_result->error_code = 0;
    char l_rate_str[32];
    if (l_ctx->cfg.rcfg.rate_limit_bytes_per_sec)
        snprintf(l_rate_str, sizeof(l_rate_str), "%u B/s", l_ctx->cfg.rcfg.rate_limit_bytes_per_sec);
    else
        snprintf(l_rate_str, sizeof(l_rate_str), "unlimited");
    log_it(L_INFO, "REALITY: bridge to %s:%u started (sni=%s, dest=%s:%u, rate_limit=%s), stream created",
           l_ctx->cfg.host, l_ctx->cfg.port, l_ctx->cfg.rcfg.sni,
           l_ctx->cfg.dest_host, l_ctx->cfg.dest_port, l_rate_str);
    return 0;
}

static int s_reality_handshake_init(dap_stream_t *a_stream, dap_net_handshake_params_t *a_params,
                                    dap_net_trans_handshake_cb_t a_callback)
{
    reality_adapter_ctx_t *l_ctx = a_stream && a_stream->trans_ctx ?
                                  a_stream->trans_ctx->transport_priv : NULL;
    if (!l_ctx || !a_params || !a_callback) {
        log_it(L_ERROR, "REALITY: handshake_init invalid args");
        return -1;
    }
    if (l_ctx->phase != RPH_CONNECTING) {
        log_it(L_ERROR, "REALITY: handshake_init in phase %s", s_phase_str(l_ctx->phase));
        return -1;
    }
    l_ctx->handshake_cb = a_callback;
    if (s_reality_send_enc_init(l_ctx, a_params)) {
        l_ctx->handshake_cb = NULL;
        DAP_DEL_Z(l_ctx->alice);
        a_stream->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return -1;
    }
    l_ctx->phase = RPH_ENC_WAIT;
    dap_events_socket_set_readable_unsafe(l_ctx->es, true);
    return 0;
}

static int s_reality_session_create(dap_stream_t *a_stream, dap_net_session_params_t *a_params,
                                    dap_net_trans_session_cb_t a_callback)
{
    reality_adapter_ctx_t *l_ctx = a_stream && a_stream->trans_ctx ?
                                  a_stream->trans_ctx->transport_priv : NULL;
    if (!l_ctx || !a_params || !a_callback || l_ctx->phase != RPH_CTL_IDLE) {
        log_it(L_ERROR, "REALITY: session_create invalid state (phase %s)",
               l_ctx ? s_phase_str(l_ctx->phase) : "no ctx");
        return -1;
    }
    l_ctx->session_cb = a_callback;
    if (s_reality_send_stream_ctl(l_ctx, a_params)) {
        l_ctx->session_cb = NULL;
        a_stream->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return -1;
    }
    l_ctx->phase = RPH_CTL_WAIT;
    dap_events_socket_set_readable_unsafe(l_ctx->es, true);
    return 0;
}

static int s_reality_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                   dap_net_trans_ready_cb_t a_callback)
{
    reality_adapter_ctx_t *l_ctx = a_stream && a_stream->trans_ctx ?
                                  a_stream->trans_ctx->transport_priv : NULL;
    if (!l_ctx || !a_callback || l_ctx->phase != RPH_STREAM_IDLE) {
        log_it(L_ERROR, "REALITY: session_start invalid state (phase %s)",
               l_ctx ? s_phase_str(l_ctx->phase) : "no ctx");
        return -1;
    }
    if (!a_session_id) {
        log_it(L_ERROR, "REALITY: session_start invalid session id");
        return -1;
    }
    l_ctx->ready_cb = a_callback;
    if (s_reality_send_stream_get(l_ctx, a_session_id)) {
        l_ctx->ready_cb = NULL;
        a_stream->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return -1;
    }
    l_ctx->phase = RPH_STREAM_200_WAIT;
    dap_events_socket_set_readable_unsafe(l_ctx->es, true);
    return 0;
}

/* Egress backpressure: dap_events_socket_write_unsafe() returns 0 when buf_out
 * is at its cap. Shed the packet (inner TCP retransmits) instead of returning
 * an error, which the stream layer would treat as a fatal transport failure. */
static void s_reality_shed_log(size_t a_size)
{
    static int64_t s_last_ms;
    static uint64_t s_count;
    s_count++;
    int64_t l_now = dap_get_monotime_ms();
    if (l_now - s_last_ms >= 5000) {
        s_last_ms = l_now;
        log_it(L_WARNING, "REALITY: egress queue full, shedding packet (%zu bytes, %llu shed total)",
               a_size, (unsigned long long)s_count);
    }
}

static ssize_t s_reality_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    reality_adapter_ctx_t *l_ctx = a_stream && a_stream->trans_ctx ?
                                  a_stream->trans_ctx->transport_priv : NULL;
    if (!l_ctx || !a_data || !a_size)
        return -1;
    if (l_ctx->phase != RPH_STREAMING) {
        /* Pre-takeover writes (channel setup / keepalive pings): buffer and
         * flush into the tunnel at takeover — the HTTP transport queues them
         * in esocket buf_out the same way. Refusing them here made the server
         * side idle-close the session mid-negotiation. */
        if (l_ctx->pending_size + a_size > 256u * 1024u) {
            log_it(L_WARNING, "REALITY: pre-takeover pending queue overflow (%zu bytes shed)",
                   a_size);
            return 0;
        }
        if (s_pending_append(l_ctx, a_data, a_size) != 0)
            return 0;
        debug_if(true, L_DEBUG, "REALITY: queued %zu bytes in phase %s (pending %zu)",
                 a_size, s_phase_str(l_ctx->phase), l_ctx->pending_size);
        return (ssize_t)a_size;
    }
    size_t l_sent = dap_events_socket_write_unsafe(l_ctx->es, a_data, a_size);
    if (l_sent == a_size)
        return (ssize_t)a_size;
    if (l_sent == 0) {
        s_reality_shed_log(a_size);
        return 0;                   /* retryable backpressure, session stays up */
    }
    log_it(L_ERROR, "REALITY: partial write %zu/%zu", l_sent, a_size);
    return -1;
}

static void s_reality_close(dap_stream_t *a_stream)
{
    reality_adapter_ctx_t *l_ctx = a_stream && a_stream->trans_ctx ?
                                  a_stream->trans_ctx->transport_priv : NULL;
    if (!l_ctx)
        return;
    debug_if(true, L_DEBUG, "REALITY: close (phase %s)", s_phase_str(l_ctx->phase));
    l_ctx->phase = RPH_CLOSED;
    l_ctx->handshake_cb = NULL;
    l_ctx->session_cb = NULL;
    l_ctx->ready_cb = NULL;
    if (l_ctx->bridge) {
        reality_bridge_signal_stop(l_ctx->bridge);
        /* Do NOT join here: close may run on the FSM thread while the bridge
         * thread may be writing into the pipes; the delete wrapper on the
         * worker thread performs the final stop. */
    }
}

static uint32_t s_reality_caps(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return DAP_NET_TRANS_CAP_RELIABLE
         | DAP_NET_TRANS_CAP_ORDERED
         | DAP_NET_TRANS_CAP_OBFUSCATION
         | DAP_NET_TRANS_CAP_BIDIRECTIONAL
         | DAP_NET_TRANS_CAP_HIGH_THROUGHPUT
         | DAP_NET_TRANS_CAP_MIMICRY;
}

static const dap_net_trans_ops_t s_reality_ops = {
    .init                    = NULL,
    .deinit                  = NULL,
    .connect                 = NULL,
    .listen                  = NULL,   /* server = Xray front */
    .accept                  = NULL,
    .handshake_init          = s_reality_handshake_init,
    .handshake_process       = NULL,
    .session_create          = s_reality_session_create,
    .session_start           = s_reality_session_start,
    .read                    = NULL,
    .write                   = s_reality_write,
    .close                   = s_reality_close,
    .get_capabilities        = s_reality_caps,
    .register_server_handlers = NULL,
    .stage_prepare           = s_reality_stage_prepare,
    .get_client_context      = NULL,
    .get_max_packet_size     = NULL,   /* streaming transport */
};

int dap_net_trans_reality_register(void)
{
    if (s_reality_registered)
        return 0;
    int l_rc = dap_net_trans_register("reality", DAP_NET_TRANS_REALITY,
                                      &s_reality_ops, DAP_NET_TRANS_SOCKET_TCP, NULL);
    if (l_rc) {
        log_it(L_ERROR, "REALITY: transport registration failed (%d)", l_rc);
        return l_rc;
    }
    s_reality_registered = 1;
    log_it(L_NOTICE, "REALITY (VLESS) client transport registered (type 0x%02X)",
           DAP_NET_TRANS_REALITY);
    return 0;
}
