/*
 * TLS Mimicry Server Transport
 *
 * TLS handshake → direct handler calls (enc_init, stream_ctl, stream).
 * No HTTP server needed — parses URL+body from raw HTTP POST.
 *
 * esocket->_inheritor always points to tls_conn_ctx_t for the entire
 * connection lifetime.  After stream_ctl, the dap_stream_t is stored
 * inside tls_conn_ctx_t.stream and reached from there — no _inheritor
 * type swap, no magic sentinels.
 */

#include <string.h>
#include <errno.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_server.h"
#include "dap_net_trans.h"
#include "dap_net_trans_server.h"
#include "dap_tls_mimicry.h"
#include "dap_net_server_common.h"
#include "dap_trans_request.h"
#include "dap_enc_handler.h"
#include "dap_stream_ctl_handler.h"
#include "dap_stream.h"
#include "dap_stream_pkt.h"

#define LOG_TAG "dap_net_trans_tls_server"

#define TLS_CT_CHANGE_CIPHER_SPEC  0x14
#define TLS_CT_APPLICATION_DATA    0x17

/* ------------------------------------------------------------------ */
/*  TLS context — stored in esocket->_inheritor for the entire conn    */
/* ------------------------------------------------------------------ */

typedef struct tls_conn_ctx {
    dap_tls_mimicry_t *mimicry;
    bool handshake_done;
    bool client_finished_consumed;
    size_t prev_buf_in_size;  /* guard: buf_in_size at function exit */
    bool stream_mode;         /* after stream_ctl: route DAP packets to stream */
    dap_stream_t *stream;     /* set by dap_stream_new_es_server; NULL until then */
} tls_conn_ctx_t;

static bool s_debug_more = false;

static inline tls_conn_ctx_t *s_tls_ctx(dap_events_socket_t *a_es)
{
    return a_es ? (tls_conn_ctx_t *)a_es->_inheritor : NULL;
}

/* ------------------------------------------------------------------ */
/*  HTTP POST parser — extracts URL path, query, body from raw HTTP    */
/* ------------------------------------------------------------------ */

static int s_parse_http_post(char *a_raw, size_t a_raw_len,
                             char **a_url_path, char **a_query_string,
                             char **a_body, size_t *a_body_len)
{
    *a_url_path = NULL;
    *a_query_string = NULL;
    *a_body = NULL;
    *a_body_len = 0;

    /* Find end of request line: POST <url> HTTP/1.x\r\n */
    char *l_eol = memchr(a_raw, '\n', a_raw_len);
    if (!l_eol) return -1;

    /* Skip "POST " */
    char *l_p = a_raw + 5;

    /* Extract URL path (up to '?' or ' ') */
    char *l_path_start = l_p;
    while (*l_p && *l_p != '?' && *l_p != ' ' && *l_p != '\r' && *l_p != '\n')
        l_p++;
    size_t l_path_len = (size_t)(l_p - l_path_start);
    *a_url_path = DAP_NEW_SIZE(char, l_path_len + 1);
    memcpy(*a_url_path, l_path_start, l_path_len);
    (*a_url_path)[l_path_len] = '\0';

    /* Extract query string (after '?' up to ' ') */
    if (*l_p == '?') {
        l_p++;
        char *l_qstart = l_p;
        while (*l_p && *l_p != ' ' && *l_p != '\r' && *l_p != '\n')
            l_p++;
        size_t l_qlen = (size_t)(l_p - l_qstart);
        *a_query_string = DAP_NEW_SIZE(char, l_qlen + 1);
        memcpy(*a_query_string, l_qstart, l_qlen);
        (*a_query_string)[l_qlen] = '\0';
    }

    /* Find Content-Length and end of headers */
    size_t l_content_length = 0;
    char *l_body_start = NULL;
    char *l_line = l_eol + 1;

    while (l_line < a_raw + a_raw_len) {
        if (*l_line == '\r' && *(l_line + 1) == '\n') {
            l_body_start = l_line + 2;
            break;
        }
        char *l_line_end = memchr(l_line, '\n', (size_t)(a_raw + a_raw_len - l_line));
        if (!l_line_end) break;

        if (strncasecmp(l_line, "Content-Length:", 15) == 0) {
            l_content_length = (size_t)atoi(l_line + 15);
        }
        l_line = l_line_end + 1;
    }

    if (l_body_start && l_content_length > 0) {
        *a_body = DAP_NEW_SIZE(char, l_content_length + 1);
        memcpy(*a_body, l_body_start, l_content_length);
        (*a_body)[l_content_length] = '\0';
        *a_body_len = l_content_length;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  URL router — dispatches to appropriate handler                     */
/* ------------------------------------------------------------------ */

static int s_route_request(const char *a_url_path, const char *a_query,
                           const void *a_body, size_t a_body_len,
                           char **a_response, size_t *a_response_len)
{
    *a_response = NULL;
    *a_response_len = 0;

    /* Create transport-independent request */
    dap_trans_request_t l_req = {0};
    l_req.query_string = a_query;
    l_req.url_path = a_url_path;
    l_req.url_path_len = a_url_path ? strlen(a_url_path) : 0;
    l_req.body = a_body;
    l_req.body_len = a_body_len;
    l_req.status_code = 404;

    int l_rc = -1;

    /* Route by URL path prefix (skip leading '/') */
    const char *l_path = a_url_path;
    if (l_path && *l_path == '/') l_path++;

    if (l_path && strncmp(l_path, "enc_init", 8) == 0) {
        /* enc_init: encryption handshake */
        l_rc = dap_enc_handler_process(&l_req);
    } else if (l_path && strncmp(l_path, "stream_ctl", 10) == 0) {
        /* stream_ctl: session creation — extract params after '/' */
        const char *l_params = strchr(l_path, '/');
        if (l_params) l_params++; else l_params = l_path + 10;
        l_req.url_path = (char *)l_params;
        l_req.url_path_len = l_params ? strlen(l_params) : 0;
        l_req.body = a_body;
        l_req.body_len = a_body_len;
        l_rc = dap_stream_ctl_handler_process(&l_req);
    } else {
        log_it(L_WARNING, "TLS server: unknown URL path '%s'", a_url_path ? a_url_path : "(null)");
        l_req.status_code = 404;
    }

    if (l_req.reply && l_req.reply_size > 0) {
        /* TLS transport sends raw handler output — no HTTP framing needed.
         * The client's stream layer parses the DAP protocol directly. */
        *a_response = (char *)l_req.reply;
        *a_response_len = l_req.reply_size;
        l_req.reply = NULL; /* ownership transferred */
    }

    return l_rc;
}

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

static void s_tls_read(dap_events_socket_t *a_es, void *a_arg);
static bool s_tls_write(dap_events_socket_t *a_es, void *a_arg);
static void s_tls_delete(dap_events_socket_t *a_es, void *a_arg);
static void s_tls_error(dap_events_socket_t *a_es, int a_error);

/* ------------------------------------------------------------------ */
/*  Read: TLS handshake → route request → TLS wrap response            */
/* ------------------------------------------------------------------ */

static void s_tls_read(dap_events_socket_t *a_es, void *a_arg)
{
    tls_conn_ctx_t *t = s_tls_ctx(a_es);
    if (!t) return;

    /* Guard: if buf_in hasn't changed since we last exited, skip.
     * Prevents tight loop when unwrap returns rc=1 (need more data)
     * and event loop re-enters with same buffer. */
    if (a_es->buf_in_size > 0 && a_es->buf_in_size == t->prev_buf_in_size)
        return;

    log_it(L_NOTICE, "TLS server: s_tls_read fd=%d, buf_in_size=%zu, t=%p",
           a_es ? a_es->socket : -1, a_es ? a_es->buf_in_size : 0, (void*)t);

    /* === TLS handshake phase === */
    if (!t->handshake_done) {
        if (a_es->buf_in_size == 0) return;

        if (!t->mimicry) {
            t->mimicry = dap_tls_mimicry_new(true);
            log_it(L_NOTICE, "TLS server: mimicry_new=%p", (void*)t->mimicry);
            if (!t->mimicry) {
                dap_events_socket_remove_and_delete_unsafe(a_es, true);
                return;
            }
        }

        void *l_resp = NULL; size_t l_resp_sz = 0;
        int l_rc = dap_tls_mimicry_process_client_hello(
            t->mimicry, a_es->buf_in, a_es->buf_in_size, &l_resp, &l_resp_sz);
        a_es->buf_in_size = 0;
        log_it(L_NOTICE, "TLS server: process_client_hello rc=%d, resp_sz=%zu", l_rc, l_resp_sz);

        if (l_rc < 0) {
            DAP_DELETE(l_resp);
            dap_events_socket_remove_and_delete_unsafe(a_es, true);
            return;
        }

        if (l_resp && l_resp_sz > 0) {
            dap_events_socket_write_unsafe(a_es, l_resp, l_resp_sz);
            DAP_DELETE(l_resp);
        }

        t->handshake_done = true;
        debug_if(s_debug_more, L_DEBUG, "TLS handshake done");
        return;
    }

    /* === Post-handshake: skip CCS+Finished, unwrap TLS, route request === */
    if (!t->mimicry || a_es->buf_in_size == 0) return;

    /* Skip CCS+Finished from client */
    if (!t->client_finished_consumed) {
        uint8_t *d = a_es->buf_in;
        size_t pos = 0, sz = a_es->buf_in_size;

        while (pos + 5 <= sz && d[pos] == TLS_CT_CHANGE_CIPHER_SPEC) {
            uint16_t len = ((uint16_t)d[pos+3] << 8) | d[pos+4];
            if (pos + 5 + len > sz) break;
            pos += 5 + len;
        }
        if (pos + 5 <= sz && d[pos] == TLS_CT_APPLICATION_DATA) {
            uint16_t len = ((uint16_t)d[pos+3] << 8) | d[pos+4];
            if (pos + 5 + len <= sz) pos += 5 + len;
        }
        t->client_finished_consumed = true;
        log_it(L_NOTICE, "TLS server: CCS skip pos=%zu, sz=%zu, first_byte=0x%02X", pos, sz, sz > 0 ? d[0] : 0);

        if (pos > 0) {
            if (pos < sz) {
                memmove(a_es->buf_in, a_es->buf_in + pos, sz - pos);
                a_es->buf_in_size -= pos;
            } else {
                a_es->buf_in_size = 0;
                return;
            }
        }
    }

    if (a_es->buf_in_size == 0) return;

    /* Unwrap TLS APPLICATION_DATA records */
    void *l_raw = NULL; size_t l_raw_sz = 0, l_consumed = 0;
    int l_rc = dap_tls_mimicry_unwrap(t->mimicry,
        a_es->buf_in, a_es->buf_in_size, &l_raw, &l_raw_sz, &l_consumed);
    log_it(L_NOTICE, "TLS server: unwrap rc=%d, consumed=%zu, raw_sz=%zu, buf_in_size=%zu",
           l_rc, l_consumed, l_raw_sz, a_es->buf_in_size);

    if (l_consumed > 0) {
        if (l_consumed < a_es->buf_in_size) {
            memmove(a_es->buf_in, a_es->buf_in + l_consumed,
                    a_es->buf_in_size - l_consumed);
            a_es->buf_in_size -= l_consumed;
        } else {
            a_es->buf_in_size = 0;
        }
    }

    if (l_rc != 0 || !l_raw || l_raw_sz == 0) {
        DAP_DELETE(l_raw);
        t->prev_buf_in_size = a_es->buf_in_size;  /* update guard at exit */
        return;
    }

    /* After stream_ctl, data is DAP stream packets (not HTTP POST).
     * Detect deterministically by the DAP stream packet signature
     * ({0xa0,0x95,0x96,0xa9,...} — see c_dap_stream_sig), not by absence of "POST".
     * HTTP POST starts with ASCII 'P' (0x50), which can never match the binary
     * signature's first byte 0xa0, so the two cases are unambiguous. */
    if (!t->stream_mode && l_raw_sz >= STREAM_PKT_SIG_SIZE
            && memcmp(l_raw, c_dap_stream_sig, STREAM_PKT_SIG_SIZE) == 0) {
        t->stream_mode = true;
        log_it(L_NOTICE, "TLS server: switching to stream mode (DAP stream signature detected, %zu bytes)", l_raw_sz);
    }

    if (t->stream_mode) {
        /* DAP stream packets — deliver to the stream created at stream_ctl */
        if (t->stream) {
            dap_stream_data_proc_read_ext(t->stream, l_raw, l_raw_sz);
        } else {
            log_it(L_WARNING, "TLS server: stream mode but no stream (dropping %zu bytes)", l_raw_sz);
        }
        DAP_DELETE(l_raw);
        t->prev_buf_in_size = a_es->buf_in_size;
        return;
    }

    /* Parse HTTP POST: extract URL, query, body */
    char *l_url_path = NULL, *l_query = NULL, *l_body = NULL;
    size_t l_body_len = 0;
    s_parse_http_post((char *)l_raw, l_raw_sz,
                      &l_url_path, &l_query, &l_body, &l_body_len);
    DAP_DELETE(l_raw);

    log_it(L_NOTICE, "TLS server: HTTP POST %s (body=%zu bytes, raw_sz=%zu)",
           l_url_path ? l_url_path : "?", l_body_len, l_raw_sz);

    /* Route request to handler */
    char *l_response = NULL;
    size_t l_response_len = 0;
    s_route_request(l_url_path, l_query, l_body, l_body_len,
                    &l_response, &l_response_len);

    /* After stream_ctl, switch to stream mode for subsequent data AND
     * create the dap_stream_t object that TLS direct mode lacks.
     * In HTTP path, GET /stream?session_id=<id> → s_stream_new() does this.
     * In TLS direct mode, stream_ctl is the last HTTP-style request, so
     * we must create the stream here using the session_id from the response. */
    if (l_url_path && strncmp(l_url_path, "/stream_ctl", 11) == 0) {
        /* Parse session_id from response: "<session_id> <key_str> ..." */
        uint32_t l_session_id = 0;
        if (l_response && l_response_len > 0)
            sscanf(l_response, "%u", &l_session_id);

        dap_stream_session_t *l_session = dap_stream_session_id_mt(l_session_id);
        if (l_session) {
            dap_stream_t *l_stream = dap_stream_new_es_server(a_es, l_session);
            if (l_stream) {
                t->stream = l_stream;
                log_it(L_NOTICE, "TLS server: stream created (session=%u, stream=%p)",
                       l_session_id, (void*)l_stream);
            } else {
                log_it(L_ERROR, "TLS server: failed to create stream for session %u", l_session_id);
            }
        } else {
            log_it(L_ERROR, "TLS server: stream_ctl session %u not found", l_session_id);
        }
        t->stream_mode = true;
        log_it(L_NOTICE, "TLS server: stream_ctl processed, stream_mode=true");
    }

    log_it(L_NOTICE, "TLS server: route result: response_len=%zu", l_response_len);

    DAP_DELETE(l_url_path);
    DAP_DELETE(l_query);
    DAP_DELETE(l_body);

    /* Wrap response in TLS records and send */
    if (l_response && l_response_len > 0 && t->mimicry) {
        void *l_wrapped = NULL; size_t l_wrapped_sz = 0;
        int l_wrap_rc = dap_tls_mimicry_wrap(t->mimicry, l_response, l_response_len,
                                  &l_wrapped, &l_wrapped_sz);
        log_it(L_NOTICE, "TLS server: wrap rc=%d, wrapped_sz=%zu", l_wrap_rc, l_wrapped_sz);
        if (l_wrap_rc == 0 && l_wrapped && l_wrapped_sz > 0) {
            dap_events_socket_write_unsafe(a_es, l_wrapped, l_wrapped_sz);
            DAP_DELETE(l_wrapped);
            log_it(L_NOTICE, "TLS server: response sent (%zu bytes)", l_wrapped_sz);
        }
        DAP_DELETE(l_response);
    } else {
        log_it(L_NOTICE, "TLS server: no response to send (resp=%p, resp_len=%zu, mimicry=%p)",
               (void*)l_response, l_response_len, t ? (void*)t->mimicry : NULL);
        DAP_DELETE(l_response);
    }
    t->prev_buf_in_size = a_es->buf_in_size;  /* update guard at exit */
}

/* ------------------------------------------------------------------ */
/*  Write: passthrough (data already in buf_out from s_tls_read)       */
/* ------------------------------------------------------------------ */

static bool s_tls_write(dap_events_socket_t *a_es, void *a_arg)
{
    return a_es->buf_out_size > 0;
}

/* ------------------------------------------------------------------ */
/*  Delete / Error                                                      */
/* ------------------------------------------------------------------ */

static void s_tls_delete(dap_events_socket_t *a_es, void *a_arg)
{
    tls_conn_ctx_t *t = s_tls_ctx(a_es);
    int l_fd = a_es ? a_es->socket : -1;

    log_it(L_NOTICE, "TLS server: delete fd=%d, t=%p, stream=%p, mimicry=%p",
           l_fd, (void*)t, t ? (void*)t->stream : NULL,
           t ? (void*)t->mimicry : NULL);

    if (t) {
        /* Stream + trans_ctx are freed by the dap_stream layer
         * (dap_stream_delete_unsafe) — we must not touch them here. */
        if (t->mimicry) dap_tls_mimicry_free(t->mimicry);
        DAP_DELETE(t);
        a_es->_inheritor = NULL;
    }
}

static void s_tls_error(dap_events_socket_t *a_es, int a_error)
{
    log_it(L_NOTICE, "TLS server: error fd=%d, err=%d", a_es ? a_es->socket : -1, a_error);
}

/* ------------------------------------------------------------------ */
/*  New connection                                                      */
/* ------------------------------------------------------------------ */

static void s_tls_client_new(dap_events_socket_t *a_es, void *a_arg)
{
    log_it(L_NOTICE, "TLS server: new client fd=%d, read_cb=%p", a_es ? a_es->socket : -1,
           a_es ? (void*)a_es->callbacks.read_callback : NULL);

    tls_conn_ctx_t *t = DAP_NEW_Z(tls_conn_ctx_t);
    if (!t) return;

    t->handshake_done = false;
    t->client_finished_consumed = false;
    t->mimicry = NULL;
    t->stream = NULL;

    a_es->_inheritor = t;
    a_es->callbacks.read_callback = s_tls_read;
    a_es->callbacks.write_callback = s_tls_write;
    a_es->callbacks.delete_callback = s_tls_delete;
    a_es->callbacks.error_callback = s_tls_error;
}

/* ------------------------------------------------------------------ */
/*  Server ops                                                          */
/* ------------------------------------------------------------------ */

typedef struct { char name[64]; dap_server_t *server; } tls_server_ctx_t;

static dap_events_socket_callbacks_t s_tls_client_cbs = {
    .accept_callback = dap_net_server_accept_callback,
    .new_callback    = s_tls_client_new,
};
static dap_events_socket_callbacks_t s_tls_listener_cbs = {
    .accept_callback = dap_net_server_accept_callback,
};

static void *s_tls_server_new(const char *a_name)
{
    tls_server_ctx_t *c = DAP_NEW_Z(tls_server_ctx_t);
    if (c) dap_strncpy(c->name, a_name, sizeof(c->name) - 1);
    return c;
}

static int s_tls_server_start(void *a_server, const char *a_cfg,
                              const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    tls_server_ctx_t *c = (tls_server_ctx_t *)a_server;
    if (!c || !a_ports || !a_count) return -1;

    const char *l_addr = (a_addrs && a_addrs[0]) ? a_addrs[0] : "0.0.0.0";
    uint16_t l_port = a_ports[0];

    /* Minimal server — no HTTP server, no URL routing.
     * TLS server handles everything directly. */
    dap_server_t *l_server = dap_server_new(NULL, NULL, &s_tls_client_cbs);
    if (!l_server) return -1;

    l_server->client_callbacks = s_tls_client_cbs;

    int l_rc = dap_server_listen_addr_add(l_server, l_addr, l_port,
                                           DESCRIPTOR_TYPE_SOCKET_LISTENING,
                                           &s_tls_listener_cbs);
    if (l_rc != 0) {
        dap_server_delete(l_server);
        return -1;
    }

    c->server = l_server;
    log_it(L_NOTICE, "TLS mimicry server listening on %s:%u", l_addr, l_port);
    return 0;
}

static void s_tls_server_stop(void *a_server)
{
    tls_server_ctx_t *c = (tls_server_ctx_t *)a_server;
    if (c && c->server) { dap_server_delete(c->server); c->server = NULL; }
}

static void s_tls_server_delete(void *a_server)
{
    tls_server_ctx_t *c = (tls_server_ctx_t *)a_server;
    if (c) { if (c->server) dap_server_delete(c->server); DAP_DELETE(c); }
}

static const dap_net_trans_server_ops_t s_tls_ops = {
    .new = s_tls_server_new, .start = s_tls_server_start,
    .stop = s_tls_server_stop, .delete = s_tls_server_delete,
};

int dap_net_trans_tls_server_init(void)
{
    int r = dap_net_trans_server_register_ops(DAP_NET_TRANS_TLS_DIRECT, &s_tls_ops);
    if (r == 0) log_it(L_NOTICE, "TLS mimicry server module initialized");
    return r;
}

void dap_net_trans_tls_server_deinit(void)
{
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_TLS_DIRECT);
}
