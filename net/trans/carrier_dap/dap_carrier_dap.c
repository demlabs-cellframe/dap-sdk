/**
 * @file dap_carrier_dap.c
 * @brief Shared HTTP-DAP phase machine (extracted from the REALITY adapter).
 *        Protocol-independent: drives POST /enc_init, POST /stream_ctl,
 *        GET /stream + takeover over the carrier byte pipe, then raw DAP stream.
 *        Single read callback owns the whole lifecycle (no callback-swap races).
 */

#include "dap_carrier_dap.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "dap_config.h"
#include "dap_cert.h"
#include "dap_enc_key.h"
#include "dap_enc.h"
#include "dap_enc_base64.h"
#include "dap_enc_base58.h"
#include "dap_stream_pkt.h"

/* Stream plaintext tail helper (header-only, shared with TLS transports). */
#include "../tls/dap_tls_stream_buffer.h"

#define LOG_TAG "dap_carrier_dap"

const char *dap_carrier_phase_str(dap_carrier_phase_t a_phase)
{
    switch (a_phase) {
    case DAP_CARRIER_PHASE_CONNECTING:      return "CONNECTING";
    case DAP_CARRIER_PHASE_ENC_WAIT:        return "ENC_WAIT";
    case DAP_CARRIER_PHASE_CTL_IDLE:        return "CTL_IDLE";
    case DAP_CARRIER_PHASE_CTL_WAIT:        return "CTL_WAIT";
    case DAP_CARRIER_PHASE_STREAM_IDLE:     return "STREAM_IDLE";
    case DAP_CARRIER_PHASE_STREAM_200_WAIT: return "STREAM_200_WAIT";
    case DAP_CARRIER_PHASE_STREAMING:       return "STREAMING";
    default:                                return "CLOSED";
    }
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* Append bytes to the HTTP accumulation buffer. 0 ok, -1 OOM */
static int s_http_append(dap_carrier_dap_ctx_t *a_ctx, const uint8_t *a_data, size_t a_size)
{
    if (!a_size)
        return 0;
    if (a_ctx->http_size + a_size > 4u * 1024u * 1024u) {
        log_it(L_ERROR, "%s: HTTP response exceeds 4 MiB limit", a_ctx->log_tag);
        return -1;
    }
    uint8_t *l_buf = realloc(a_ctx->http, a_ctx->http_size + a_size);
    if (!l_buf) {
        log_it(L_ERROR, "%s: HTTP accumulate OOM (%zu bytes)", a_ctx->log_tag, a_ctx->http_size + a_size);
        return -1;
    }
    memcpy(l_buf + a_ctx->http_size, a_data, a_size);
    a_ctx->http = l_buf;
    a_ctx->http_size += a_size;
    return 0;
}

/* Buffer a pre-takeover stream write. 0 ok, -1 OOM */
static int s_pending_append(dap_carrier_dap_ctx_t *a_ctx, const void *a_data, size_t a_size)
{
    uint8_t *l_buf = realloc(a_ctx->pending, a_ctx->pending_size + a_size);
    if (!l_buf) {
        log_it(L_ERROR, "%s: pending queue OOM (%zu bytes)", a_ctx->log_tag, a_ctx->pending_size + a_size);
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
static int s_http_try_complete(dap_carrier_dap_ctx_t *a_ctx, bool a_headers_only,
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
        debug_if(true, L_DEBUG, "%s: HTTP status %d, content-length %zu",
                 a_ctx->log_tag, a_ctx->http_status, a_ctx->content_len);
    }
    if (a_ctx->http_status >= 400 || !a_ctx->http_status) {
        log_it(L_ERROR, "%s: HTTP error response %d", a_ctx->log_tag, a_ctx->http_status);
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
        debug_if(true, L_DEBUG, "%s: HTTP body %zu/%zu — waiting",
                 a_ctx->log_tag, l_body_size, a_ctx->content_len);
        return 0; /* body incomplete */
    }
    *a_body = a_ctx->http + a_ctx->headers_size;
    *a_body_size = a_ctx->content_len;
    return 1;
}

static void s_http_reset(dap_carrier_dap_ctx_t *a_ctx)
{
    DAP_DEL_Z(a_ctx->http);
    a_ctx->http_size = 0;
    a_ctx->headers_size = 0;
    a_ctx->content_len = 0;
    a_ctx->have_headers = false;
    a_ctx->http_status = 0;
}

void dap_carrier_dap_fail(dap_carrier_dap_ctx_t *a_ctx, const char *a_why)
{
    if (!a_ctx || a_ctx->phase == DAP_CARRIER_PHASE_CLOSED)
        return;
    log_it(L_ERROR, "%s: %s (phase %s)", a_ctx->log_tag, a_why, dap_carrier_phase_str(a_ctx->phase));
    a_ctx->phase = DAP_CARRIER_PHASE_CLOSED;
    if (a_ctx->hooks.signal_stop)
        a_ctx->hooks.signal_stop(a_ctx->hooks.user);
    if (a_ctx->es)
        a_ctx->es->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

/* ------------------------------------------------------------------ */
/* esocket callback wrappers                                           */
/* ------------------------------------------------------------------ */

void dap_carrier_dap_es_error(dap_events_socket_t *a_es, int a_err)
{
    dap_carrier_dap_ctx_t *l_ctx = (dap_carrier_dap_ctx_t *)a_es->callbacks.arg;
    log_it(L_WARNING, "%s: data socket error %d (phase %s)",
           l_ctx ? l_ctx->log_tag : "?", a_err,
           l_ctx ? dap_carrier_phase_str(l_ctx->phase) : "?");
    if (l_ctx && l_ctx->base_cbs.error_callback)
        l_ctx->base_cbs.error_callback(a_es, a_err);
    dap_carrier_dap_fail(l_ctx, "data socket error");
}

void dap_carrier_dap_es_delete(dap_events_socket_t *a_es, void *a_arg)
{
    dap_carrier_dap_ctx_t *l_ctx = a_arg;
    debug_if(true, L_DEBUG, "%s: data esocket delete (ctx=%p)",
             l_ctx ? l_ctx->log_tag : "?", l_ctx);
    if (l_ctx) {
        if (l_ctx->base_cbs.delete_callback)
            l_ctx->base_cbs.delete_callback(a_es, l_ctx->base_cbs.arg);
        if (l_ctx->hooks.stop)
            l_ctx->hooks.stop(l_ctx->hooks.user);
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
        if (l_ctx->hooks.destroy)
            l_ctx->hooks.destroy(l_ctx->hooks.user);
    }
}

void dap_carrier_dap_ctrl_read_cb(dap_events_socket_t *a_es, void *a_arg)
{
    dap_carrier_dap_ctx_t *l_ctx = a_arg;
    if (!l_ctx || !a_es->buf_in_size)
        return;
    for (size_t i = 0; i < a_es->buf_in_size; i++) {
        char l_ev = (char)a_es->buf_in[i];
        switch (l_ev) {
        case 'R':
            debug_if(true, L_DEBUG, "%s: bridge ready", l_ctx->log_tag);
            break;
        case 'E': {
            int l_err = 0;
            if (i + 1 < a_es->buf_in_size)
                l_err = -(int)(signed char)a_es->buf_in[++i];
            log_it(L_ERROR, "%s: bridge error %d", l_ctx->log_tag, l_err);
            a_es->buf_in_size = 0;
            dap_carrier_dap_fail(l_ctx, "bridge error");
            return;
        }
        case 'C':
            a_es->buf_in_size = 0;
            dap_carrier_dap_fail(l_ctx, "bridge closed");
            return;
        default:
            break;
        }
    }
    a_es->buf_in_size = 0;
}

void dap_carrier_dap_ctrl_delete(dap_events_socket_t *a_es, void *a_arg)
{
    dap_carrier_dap_ctx_t *l_ctx = a_arg;
    debug_if(true, L_DEBUG, "%s: ctrl esocket delete", l_ctx ? l_ctx->log_tag : "?");
    if (l_ctx)
        l_ctx->ctrl_es = NULL;
    /* no chained callbacks: ctrl socket is adapter-internal */
}

/* ------------------------------------------------------------------ */
/* read callback: single lifecycle phase machine (on buf_in)            */
/* ------------------------------------------------------------------ */

void dap_carrier_dap_read_cb(dap_events_socket_t *a_es, void *a_arg)
{
    dap_carrier_dap_ctx_t *l_ctx = a_arg;
    if (!l_ctx || !a_es->buf_in_size)
        return;
    debug_if(true, L_DEBUG, "%s: read event %zu bytes (phase %s, http_buf %zu)",
             l_ctx->log_tag, a_es->buf_in_size, dap_carrier_phase_str(l_ctx->phase), l_ctx->http_size);

    switch (l_ctx->phase) {
    case DAP_CARRIER_PHASE_ENC_WAIT:
    case DAP_CARRIER_PHASE_CTL_WAIT: {
        if (s_http_append(l_ctx, a_es->buf_in, a_es->buf_in_size)) {
            a_es->buf_in_size = 0;
            dap_carrier_dap_fail(l_ctx, "HTTP accumulate failure");
            return;
        }
        a_es->buf_in_size = 0;

        const uint8_t *l_body = NULL;
        size_t l_body_size = 0;
        int l_rc = s_http_try_complete(l_ctx, false, &l_body, &l_body_size);
        if (l_rc == 0)
            return;             /* need more bytes */
        if (l_rc < 0) {
            dap_carrier_dap_fail(l_ctx, "bad HTTP response");
            return;
        }

        if (l_ctx->phase == DAP_CARRIER_PHASE_ENC_WAIT) {
            log_it(L_NOTICE, "%s: enc_init response received (%zu bytes)", l_ctx->log_tag, l_body_size);
            dap_net_trans_handshake_cb_t l_cb = l_ctx->handshake_cb;
            l_ctx->handshake_cb = NULL;
            l_ctx->phase = DAP_CARRIER_PHASE_CTL_IDLE;
            s_http_reset(l_ctx);
            if (l_cb) {
                /* callback copies synchronously (same contract as TLS) */
                l_cb(l_ctx->stream, l_body, l_body_size, 0);
            }
        } else if (l_ctx->phase == DAP_CARRIER_PHASE_CTL_WAIT) {
            /* stream_ctl body is encrypted with the symmetric session key */
            dap_net_trans_ctx_t *l_tc = l_ctx->stream ? l_ctx->stream->trans_ctx : NULL;
            if (!l_tc || !l_tc->session_key) {
                dap_carrier_dap_fail(l_ctx, "no session key for stream_ctl reply");
                return;
            }
            size_t l_calc = dap_enc_decode_out_size(l_tc->session_key, l_body_size,
                                                    DAP_ENC_DATA_TYPE_RAW);
            size_t l_buf_size = dap_max(l_calc, l_body_size) + 32;
            char *l_plain = DAP_NEW_Z_SIZE(char, l_buf_size);
            if (!l_plain) {
                dap_carrier_dap_fail(l_ctx, "stream_ctl reply OOM");
                return;
            }
            size_t l_plain_size = dap_enc_decode(l_tc->session_key, l_body, l_body_size,
                                                 l_plain, l_buf_size, DAP_ENC_DATA_TYPE_RAW);
            if (!l_plain_size || l_plain_size > l_buf_size) {
                DAP_DELETE(l_plain);
                dap_carrier_dap_fail(l_ctx, "stream_ctl reply decrypt failed");
                return;
            }
            l_plain[l_plain_size] = '\0';
            log_it(L_NOTICE, "%s: stream_ctl response received (%zu bytes)", l_ctx->log_tag, l_plain_size);
            dap_net_trans_session_cb_t l_cb = l_ctx->session_cb;
            l_ctx->session_cb = NULL;
            l_ctx->phase = DAP_CARRIER_PHASE_STREAM_IDLE;
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

    case DAP_CARRIER_PHASE_STREAM_200_WAIT: {
        if (s_http_append(l_ctx, a_es->buf_in, a_es->buf_in_size)) {
            a_es->buf_in_size = 0;
            dap_carrier_dap_fail(l_ctx, "HTTP accumulate failure");
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
            dap_carrier_dap_fail(l_ctx, "stream GET was not accepted");
            return;
        }

        log_it(L_NOTICE, "%s: stream takeover (HTTP %d)", l_ctx->log_tag, l_ctx->http_status);
        dap_net_trans_ready_cb_t l_cb = l_ctx->ready_cb;
        l_ctx->ready_cb = NULL;
        l_ctx->phase = DAP_CARRIER_PHASE_STREAMING;

        /* flush writes buffered before takeover (channel pings, keepalive) */
        if (l_ctx->pending_size) {
            debug_if(true, L_DEBUG, "%s: flushing %zu pre-takeover bytes", l_ctx->log_tag, l_ctx->pending_size);
            dap_events_socket_write_unsafe(l_ctx->es, l_ctx->pending, l_ctx->pending_size);
            DAP_DEL_Z(l_ctx->pending);
            l_ctx->pending_size = 0;
        }
        if (l_cb)
            l_cb(l_ctx->stream, 0);
        /* Stream bytes that arrived together with the headers. Feed BEFORE
         * s_http_reset(): l_raw points into the accumulation buffer. */
        if (l_raw_size) {
            debug_if(true, L_DEBUG, "%s: %zu stream bytes with takeover headers", l_ctx->log_tag, l_raw_size);
            if (s_tls_stream_feed(l_ctx->stream, &l_ctx->tail, &l_ctx->tail_size,
                                  l_raw, l_raw_size) != 0) {
                s_http_reset(l_ctx);
                dap_carrier_dap_fail(l_ctx, "stream feed failure");
                return;
            }
        }
        s_http_reset(l_ctx);
        break;
    }

    case DAP_CARRIER_PHASE_STREAMING:
    case DAP_CARRIER_PHASE_CTL_IDLE:
    case DAP_CARRIER_PHASE_STREAM_IDLE: {
        /* In IDLE phases the node may still push stream packets (e.g. on an
         * already-established session) — feed them, same as STREAMING. */
        if (l_ctx->phase == DAP_CARRIER_PHASE_STREAMING || l_ctx->phase == DAP_CARRIER_PHASE_STREAM_IDLE) {
            const uint8_t *l_data = a_es->buf_in;
            size_t l_size = a_es->buf_in_size;
            a_es->buf_in_size = 0;
            if (s_tls_stream_feed(l_ctx->stream, &l_ctx->tail, &l_ctx->tail_size,
                                  l_data, l_size) != 0) {
                dap_carrier_dap_fail(l_ctx, "stream feed failure");
                return;
            }
        } else {
            /* CTL_IDLE with unexpected bytes: drop (server push before ctl) */
            log_it(L_WARNING, "%s: dropping %zu unexpected bytes in %s",
                   l_ctx->log_tag, a_es->buf_in_size, dap_carrier_phase_str(l_ctx->phase));
            a_es->buf_in_size = 0;
        }
        break;
    }

    case DAP_CARRIER_PHASE_CONNECTING:
    default:
        log_it(L_WARNING, "%s: %zu bytes in phase %s — dropped",
               l_ctx->log_tag, a_es->buf_in_size, dap_carrier_phase_str(l_ctx->phase));
        a_es->buf_in_size = 0;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* request builders (wire format = node's HTTP-DAP listener)            */
/* ------------------------------------------------------------------ */

static int s_send_enc_init(dap_carrier_dap_ctx_t *a_ctx, dap_net_handshake_params_t *a_params)
{
    if (!a_params->alice_pub_key || !a_params->alice_pub_key_size) {
        log_it(L_ERROR, "%s: empty alice public key", a_ctx->log_tag);
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
            a_ctx->client->link_info.uplink_addr : a_ctx->default_host,
        l_body_size, l_body);
    DAP_DELETE(l_body);
    if (!l_req) {
        log_it(L_ERROR, "%s: enc_init request build failed", a_ctx->log_tag);
        return -1;
    }
    size_t l_req_size = dap_strlen(l_req);
    ssize_t l_sent = dap_events_socket_write_unsafe(a_ctx->es, l_req, l_req_size);
    DAP_DELETE(l_req);
    if (l_sent != (ssize_t)l_req_size) {
        log_it(L_ERROR, "%s: enc_init send failed (%zd/%zu)", a_ctx->log_tag, l_sent, l_req_size);
        return -1;
    }
    debug_if(true, L_DEBUG, "%s: enc_init sent (%zu bytes)", a_ctx->log_tag, l_req_size);
    return 0;
}

static int s_send_stream_ctl(dap_carrier_dap_ctx_t *a_ctx, dap_net_session_params_t *a_params)
{
    dap_net_trans_ctx_t *l_tc = a_ctx->stream ? a_ctx->stream->trans_ctx : NULL;
    if (!l_tc || !l_tc->session_key) {
        log_it(L_ERROR, "%s: stream_ctl without session key", a_ctx->log_tag);
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
            a_ctx->client->link_info.uplink_addr : a_ctx->default_host,
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
        log_it(L_ERROR, "%s: stream_ctl send failed", a_ctx->log_tag);
        return -1;
    }
    debug_if(true, L_DEBUG, "%s: stream_ctl sent (%zu + %zu bytes)",
             a_ctx->log_tag, l_req_size, l_body_enc_size - 1);
    return 0;
}

static int s_send_stream_get(dap_carrier_dap_ctx_t *a_ctx, uint32_t a_session_id)
{
    char *l_req = dap_strdup_printf(
        "GET /%s/globaldb?session_id=%u HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        DAP_UPLINK_PATH_STREAM, a_session_id,
        a_ctx->client && a_ctx->client->link_info.uplink_addr[0] ?
            a_ctx->client->link_info.uplink_addr : a_ctx->default_host);
    if (!l_req)
        return -1;
    size_t l_req_size = dap_strlen(l_req);
    ssize_t l_sent = dap_events_socket_write_unsafe(a_ctx->es, l_req, l_req_size);
    DAP_DELETE(l_req);
    if (l_sent != (ssize_t)l_req_size) {
        log_it(L_ERROR, "%s: stream GET send failed", a_ctx->log_tag);
        return -1;
    }
    log_it(L_INFO, "%s: stream GET queued (session_id=%u), waiting for 200", a_ctx->log_tag, a_session_id);
    return 0;
}

/* ------------------------------------------------------------------ */
/* shared transport ops                                                */
/* ------------------------------------------------------------------ */

static dap_carrier_dap_ctx_t *s_ctx_from_stream(dap_stream_t *a_stream)
{
    return a_stream && a_stream->trans_ctx ? a_stream->trans_ctx->transport_priv : NULL;
}

int dap_carrier_dap_handshake_init(dap_stream_t *a_stream, dap_net_handshake_params_t *a_params,
                                   dap_net_trans_handshake_cb_t a_callback)
{
    dap_carrier_dap_ctx_t *l_ctx = s_ctx_from_stream(a_stream);
    if (!l_ctx || !a_params || !a_callback) {
        log_it(L_ERROR, "carrier_dap: handshake_init invalid args");
        return -1;
    }
    if (l_ctx->phase != DAP_CARRIER_PHASE_CONNECTING) {
        log_it(L_ERROR, "%s: handshake_init in phase %s", l_ctx->log_tag, dap_carrier_phase_str(l_ctx->phase));
        return -1;
    }
    l_ctx->handshake_cb = a_callback;
    if (s_send_enc_init(l_ctx, a_params)) {
        l_ctx->handshake_cb = NULL;
        DAP_DEL_Z(l_ctx->alice);
        a_stream->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return -1;
    }
    l_ctx->phase = DAP_CARRIER_PHASE_ENC_WAIT;
    dap_events_socket_set_readable_unsafe(l_ctx->es, true);
    return 0;
}

int dap_carrier_dap_session_create(dap_stream_t *a_stream, dap_net_session_params_t *a_params,
                                   dap_net_trans_session_cb_t a_callback)
{
    dap_carrier_dap_ctx_t *l_ctx = s_ctx_from_stream(a_stream);
    if (!l_ctx || !a_params || !a_callback || l_ctx->phase != DAP_CARRIER_PHASE_CTL_IDLE) {
        log_it(L_ERROR, "%s: session_create invalid state (phase %s)",
               l_ctx ? l_ctx->log_tag : "?", l_ctx ? dap_carrier_phase_str(l_ctx->phase) : "no ctx");
        return -1;
    }
    l_ctx->session_cb = a_callback;
    if (s_send_stream_ctl(l_ctx, a_params)) {
        l_ctx->session_cb = NULL;
        a_stream->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return -1;
    }
    l_ctx->phase = DAP_CARRIER_PHASE_CTL_WAIT;
    dap_events_socket_set_readable_unsafe(l_ctx->es, true);
    return 0;
}

int dap_carrier_dap_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                  dap_net_trans_ready_cb_t a_callback)
{
    dap_carrier_dap_ctx_t *l_ctx = s_ctx_from_stream(a_stream);
    if (!l_ctx || !a_callback || l_ctx->phase != DAP_CARRIER_PHASE_STREAM_IDLE) {
        log_it(L_ERROR, "%s: session_start invalid state (phase %s)",
               l_ctx ? l_ctx->log_tag : "?", l_ctx ? dap_carrier_phase_str(l_ctx->phase) : "no ctx");
        return -1;
    }
    if (!a_session_id) {
        log_it(L_ERROR, "%s: session_start invalid session id", l_ctx->log_tag);
        return -1;
    }
    l_ctx->ready_cb = a_callback;
    if (s_send_stream_get(l_ctx, a_session_id)) {
        l_ctx->ready_cb = NULL;
        a_stream->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return -1;
    }
    l_ctx->phase = DAP_CARRIER_PHASE_STREAM_200_WAIT;
    dap_events_socket_set_readable_unsafe(l_ctx->es, true);
    return 0;
}

/* Egress backpressure: dap_events_socket_write_unsafe() returns 0 when buf_out
 * is at its cap. Shed the packet (inner TCP retransmits) instead of returning
 * an error, which the stream layer would treat as a fatal transport failure. */
static void s_shed_log(const char *a_tag, size_t a_size)
{
    static int64_t s_last_ms;
    static uint64_t s_count;
    s_count++;
    int64_t l_now = dap_get_monotime_ms();
    if (l_now - s_last_ms >= 5000) {
        s_last_ms = l_now;
        log_it(L_WARNING, "%s: egress queue full, shedding packet (%zu bytes, %llu shed total)",
               a_tag, a_size, (unsigned long long)s_count);
    }
}

ssize_t dap_carrier_dap_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    dap_carrier_dap_ctx_t *l_ctx = s_ctx_from_stream(a_stream);
    if (!l_ctx || !a_data || !a_size)
        return -1;
    if (l_ctx->phase != DAP_CARRIER_PHASE_STREAMING) {
        /* Pre-takeover writes (channel setup / keepalive pings): buffer and
         * flush into the tunnel at takeover — the HTTP transport queues them
         * in esocket buf_out the same way. Refusing them here made the server
         * side idle-close the session mid-negotiation. */
        if (l_ctx->pending_size + a_size > 256u * 1024u) {
            log_it(L_WARNING, "%s: pre-takeover pending queue overflow (%zu bytes shed)",
                   l_ctx->log_tag, a_size);
            return 0;
        }
        if (s_pending_append(l_ctx, a_data, a_size) != 0)
            return 0;
        debug_if(true, L_DEBUG, "%s: queued %zu bytes in phase %s (pending %zu)",
                 l_ctx->log_tag, a_size, dap_carrier_phase_str(l_ctx->phase), l_ctx->pending_size);
        return (ssize_t)a_size;
    }
    size_t l_sent = dap_events_socket_write_unsafe(l_ctx->es, a_data, a_size);
    if (l_sent == a_size)
        return (ssize_t)a_size;
    if (l_sent == 0) {
        s_shed_log(l_ctx->log_tag, a_size);
        return 0;                   /* retryable backpressure, session stays up */
    }
    log_it(L_ERROR, "%s: partial write %zu/%zu", l_ctx->log_tag, l_sent, a_size);
    return -1;
}

void dap_carrier_dap_close(dap_stream_t *a_stream)
{
    dap_carrier_dap_ctx_t *l_ctx = s_ctx_from_stream(a_stream);
    if (!l_ctx)
        return;
    debug_if(true, L_DEBUG, "%s: close (phase %s)", l_ctx->log_tag, dap_carrier_phase_str(l_ctx->phase));
    l_ctx->phase = DAP_CARRIER_PHASE_CLOSED;
    l_ctx->handshake_cb = NULL;
    l_ctx->session_cb = NULL;
    l_ctx->ready_cb = NULL;
    if (l_ctx->hooks.signal_stop) {
        l_ctx->hooks.signal_stop(l_ctx->hooks.user);
        /* Do NOT join here: close may run on the FSM thread while the bridge
         * thread may be writing into the pipes; the delete wrapper on the
         * worker thread performs the final stop. */
    }
}