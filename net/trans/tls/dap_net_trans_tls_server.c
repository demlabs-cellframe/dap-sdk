/*
 * TLS Mimicry Server Transport
 *
 * Accepts TCP connections, performs TLS 1.3 mimicry handshake (server side),
 * then hands the connection to the HTTP server for DAP stream processing.
 * TLS wrapping/unwrapping is transparent to the HTTP layer.
 */

#include <string.h>
#include <errno.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_server.h"
#include "dap_http_client.h"
#include "dap_http_server.h"
#include "dap_net_trans.h"
#include "dap_net_trans_server.h"
#include "dap_tls_mimicry.h"

#define LOG_TAG "dap_net_trans_tls_server"

/* ------------------------------------------------------------------ */
/*  Per-connection TLS context (stored in http_client->_inheritor)     */
/* ------------------------------------------------------------------ */

typedef struct tls_conn_ctx {
    dap_tls_mimicry_t *mimicry;
    bool handshake_done;
    /* Original HTTP callbacks saved before wrapping */
    void (*orig_read_callback)(dap_events_socket_t *, void *);
    bool (*orig_write_callback)(dap_events_socket_t *, void *);
    void (*orig_delete_callback)(dap_events_socket_t *, void *);
    void (*orig_error_callback)(dap_events_socket_t *, int);
} tls_conn_ctx_t;

static bool s_debug_more = false;

/* Forward declarations */
static void s_tls_wrapped_read(dap_events_socket_t *a_es, void *a_arg);
static bool s_tls_wrapped_write(dap_events_socket_t *a_es, void *a_arg);

/* ------------------------------------------------------------------ */
/*  Wrapped read: TLS unwrap → HTTP client read                        */
/* ------------------------------------------------------------------ */

static void s_tls_wrapped_read(dap_events_socket_t *a_es, void *a_arg)
{
    dap_http_client_t *l_http = DAP_HTTP_CLIENT(a_es);
    tls_conn_ctx_t *l_tls = l_http ? (tls_conn_ctx_t *)l_http->_inheritor : NULL;
    if (!l_tls) {
        /* No TLS context — shouldn't happen after handshake, but pass through */
        if (l_http && l_tls && l_tls->orig_read_callback)
            l_tls->orig_read_callback(a_es, a_arg);
        return;
    }

    if (!l_tls->handshake_done) {
        /* TLS handshake phase */
        if (!l_tls->mimicry) {
            l_tls->mimicry = dap_tls_mimicry_new(true);
            if (!l_tls->mimicry) {
                log_it(L_ERROR, "Failed to create TLS mimicry server context");
                dap_events_socket_remove_and_delete_unsafe(a_es, true);
                return;
            }
        }

        void *l_response = NULL;
        size_t l_response_size = 0;
        int l_rc = dap_tls_mimicry_process_client_hello(
            l_tls->mimicry,
            a_es->buf_in, a_es->buf_in_size,
            &l_response, &l_response_size);
        a_es->buf_in_size = 0;

        if (l_rc < 0) {
            log_it(L_WARNING, "TLS mimicry: invalid ClientHello (size=%zu)", a_es->buf_in_size);
            DAP_DELETE(l_response);
            dap_events_socket_remove_and_delete_unsafe(a_es, true);
            return;
        }

        if (l_response && l_response_size > 0) {
            dap_events_socket_write_unsafe(a_es, l_response, l_response_size);
            DAP_DELETE(l_response);
        }

        l_tls->handshake_done = true;
        debug_if(s_debug_more, L_DEBUG, "TLS handshake completed (server side)");
        return;
    }

    /* After handshake: unwrap TLS records, then call original HTTP read */
    if (l_tls->mimicry && a_es->buf_in_size > 0) {
        void *l_unwrapped = NULL;
        size_t l_unwrapped_size = 0;
        size_t l_consumed = 0;
        int l_rc = dap_tls_mimicry_unwrap(l_tls->mimicry,
                                           a_es->buf_in, a_es->buf_in_size,
                                           &l_unwrapped, &l_unwrapped_size,
                                           &l_consumed);
        /* Remove consumed bytes from buf_in */
        if (l_consumed > 0 && l_consumed < a_es->buf_in_size) {
            memmove(a_es->buf_in, a_es->buf_in + l_consumed,
                    a_es->buf_in_size - l_consumed);
            a_es->buf_in_size -= l_consumed;
        } else {
            a_es->buf_in_size = 0;
        }

        if (l_rc == 0 && l_unwrapped && l_unwrapped_size > 0) {
            /* Place unwrapped data into buf_in for the HTTP callback */
            if (l_unwrapped_size <= a_es->buf_in_size_max) {
                memcpy(a_es->buf_in, l_unwrapped, l_unwrapped_size);
                a_es->buf_in_size = l_unwrapped_size;
            }
            DAP_DELETE(l_unwrapped);
        } else if (l_rc == 1) {
            /* Incomplete TLS record — wait for more data */
            DAP_DELETE(l_unwrapped);
            return;
        } else {
            DAP_DELETE(l_unwrapped);
            return;
        }
    }

    /* Call original HTTP read callback */
    if (l_tls->orig_read_callback)
        l_tls->orig_read_callback(a_es, a_arg);
}

/* ------------------------------------------------------------------ */
/*  Wrapped write: HTTP write → TLS wrap → send                        */
/* ------------------------------------------------------------------ */

static bool s_tls_wrapped_write(dap_events_socket_t *a_es, void *a_arg)
{
    dap_http_client_t *l_http = DAP_HTTP_CLIENT(a_es);
    tls_conn_ctx_t *l_tls = l_http ? (tls_conn_ctx_t *)l_http->_inheritor : NULL;
    if (!l_tls || !l_tls->handshake_done) {
        if (l_tls && l_tls->orig_write_callback)
            return l_tls->orig_write_callback(a_es, a_arg);
        return false;
    }

    /* Call original HTTP write callback to populate buf_out */
    if (l_tls->orig_write_callback)
        l_tls->orig_write_callback(a_es, a_arg);

    /* Wrap buf_out in TLS records */
    if (l_tls->mimicry && a_es->buf_out_size > 0) {
        void *l_wrapped = NULL;
        size_t l_wrapped_size = 0;
        int l_rc = dap_tls_mimicry_wrap(l_tls->mimicry,
                                         a_es->buf_out, a_es->buf_out_size,
                                         &l_wrapped, &l_wrapped_size);
        if (l_rc == 0 && l_wrapped && l_wrapped_size > 0) {
            size_t l_copy = l_wrapped_size <= a_es->buf_out_size_max
                          ? l_wrapped_size : a_es->buf_out_size_max;
            memcpy(a_es->buf_out, l_wrapped, l_copy);
            a_es->buf_out_size = l_copy;
            DAP_DELETE(l_wrapped);
        }
    }

    return a_es->buf_out_size == 0;
}

/* ------------------------------------------------------------------ */
/*  TLS-aware delete callback                                          */
/* ------------------------------------------------------------------ */

static void s_tls_wrapped_delete(dap_events_socket_t *a_es, void *a_arg)
{
    dap_http_client_t *l_http = DAP_HTTP_CLIENT(a_es);
    tls_conn_ctx_t *l_tls = l_http ? (tls_conn_ctx_t *)l_http->_inheritor : NULL;

    if (l_tls) {
        if (l_tls->mimicry)
            dap_tls_mimicry_free(l_tls->mimicry);
        if (l_tls->orig_delete_callback)
            l_tls->orig_delete_callback(a_es, a_arg);
        DAP_DELETE(l_tls);
        if (l_http) l_http->_inheritor = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  TLS-aware error callback                                           */
/* ------------------------------------------------------------------ */

static void s_tls_wrapped_error(dap_events_socket_t *a_es, int a_error)
{
    dap_http_client_t *l_http = DAP_HTTP_CLIENT(a_es);
    tls_conn_ctx_t *l_tls = l_http ? (tls_conn_ctx_t *)l_http->_inheritor : NULL;
    if (l_tls && l_tls->orig_error_callback)
        l_tls->orig_error_callback(a_es, a_error);
    else
        log_it(L_WARNING, "TLS server: client error %d", a_error);
}

/* ------------------------------------------------------------------ */
/*  New connection: perform TLS handshake, then hand to HTTP server     */
/* ------------------------------------------------------------------ */

static void s_tls_client_new(dap_events_socket_t *a_es, void *a_arg)
{
    /* Create TLS context */
    tls_conn_ctx_t *l_tls = DAP_NEW_Z(tls_conn_ctx_t);
    if (!l_tls) {
        log_it(L_CRITICAL, "Cannot allocate TLS connection context");
        return;
    }

    /* Initialize HTTP client on this esocket */
    dap_http_client_new(a_es, a_arg);

    /* Store TLS context in HTTP client's _inheritor */
    dap_http_client_t *l_http = DAP_HTTP_CLIENT(a_es);
    if (!l_http) {
        log_it(L_ERROR, "Failed to create HTTP client on TLS connection");
        DAP_DELETE(l_tls);
        return;
    }
    l_http->_inheritor = l_tls;

    /* Save original HTTP callbacks */
    l_tls->orig_read_callback = a_es->callbacks.read_callback;
    l_tls->orig_write_callback = a_es->callbacks.write_callback;
    l_tls->orig_delete_callback = a_es->callbacks.delete_callback;
    l_tls->orig_error_callback = a_es->callbacks.error_callback;

    /* Replace with TLS-wrapped callbacks */
    a_es->callbacks.read_callback = s_tls_wrapped_read;
    a_es->callbacks.write_callback = s_tls_wrapped_write;
    a_es->callbacks.delete_callback = s_tls_wrapped_delete;
    a_es->callbacks.error_callback = s_tls_wrapped_error;

    debug_if(s_debug_more, L_DEBUG, "TLS server: new client, HTTP client initialized");
}

/* ------------------------------------------------------------------ */
/*  Server operations                                                  */
/* ------------------------------------------------------------------ */

typedef struct tls_server_ctx {
    char server_name[64];
    dap_server_t *server;
    dap_net_trans_t *trans;
} tls_server_ctx_t;

static dap_events_socket_callbacks_t s_tls_client_callbacks = {
    .new_callback = s_tls_client_new,
};

static void *s_tls_server_new(const char *a_server_name)
{
    tls_server_ctx_t *l_ctx = DAP_NEW_Z(tls_server_ctx_t);
    if (!l_ctx) return NULL;
    dap_strncpy(l_ctx->server_name, a_server_name, sizeof(l_ctx->server_name) - 1);
    l_ctx->trans = dap_net_trans_find(DAP_NET_TRANS_TLS_DIRECT);
    log_it(L_INFO, "TLS mimicry server created: %s", a_server_name);
    return l_ctx;
}

static int s_tls_server_start(void *a_server, const char *a_cfg_section,
                              const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    tls_server_ctx_t *l_ctx = (tls_server_ctx_t *)a_server;
    if (!l_ctx || !a_ports || a_count == 0) return -1;

    const char *l_addr = (a_addrs && a_count > 0) ? a_addrs[0] : "0.0.0.0";
    uint16_t l_port = a_ports[0];

    /* Create HTTP server — TLS wraps HTTP, so we need the HTTP processing pipeline.
     * This creates dap_server_t with dap_http_server_t as _inheritor, which is
     * required for URL routing (enc_init, stream, stream_ctl, /remain_limits_vpn). */
    dap_server_t *l_server = dap_http_server_new(NULL, l_ctx->server_name);
    if (!l_server) {
        log_it(L_ERROR, "Failed to create TLS+HTTP server on %s:%u", l_addr, l_port);
        return -1;
    }

    /* Register DAP protocol handlers on the HTTP server */
    dap_http_server_t *l_http = DAP_HTTP_SERVER(l_server);
    if (l_http) {
        dap_net_trans_server_ctx_t *l_ctx_reg = dap_net_trans_server_ctx_from_http(
            l_http, DAP_NET_TRANS_TLS_DIRECT, NULL);
        if (l_ctx_reg) {
            dap_net_trans_server_register_handlers(l_ctx_reg);
            dap_net_trans_server_ctx_delete(l_ctx_reg);
        }
    }

    int l_rc = dap_server_listen_addr_add(l_server, l_addr, l_port,
                                          DESCRIPTOR_TYPE_SOCKET_LISTENING,
                                          &s_tls_client_callbacks);
    if (l_rc != 0) {
        log_it(L_ERROR, "Failed to add TLS listen address %s:%u (rc=%d)", l_addr, l_port, l_rc);
        dap_server_delete(l_server);
        return -1;
    }

    l_ctx->server = l_server;

    /* Register HTTP server in global registry for VPN plugin access */
    dap_net_trans_server_set_http_server(DAP_NET_TRANS_TLS_DIRECT, l_http);

    log_it(L_NOTICE, "TLS mimicry server listening on %s:%u", l_addr, l_port);
    return 0;
}

static void s_tls_server_stop(void *a_server)
{
    tls_server_ctx_t *l_ctx = (tls_server_ctx_t *)a_server;
    if (l_ctx && l_ctx->server) {
        dap_server_delete(l_ctx->server);
        l_ctx->server = NULL;
    }
}

static void s_tls_server_delete(void *a_server)
{
    tls_server_ctx_t *l_ctx = (tls_server_ctx_t *)a_server;
    if (l_ctx) {
        if (l_ctx->server)
            dap_server_delete(l_ctx->server);
        DAP_DELETE(l_ctx);
    }
}

static const dap_net_trans_server_ops_t s_tls_server_ops = {
    .new    = s_tls_server_new,
    .start  = s_tls_server_start,
    .stop   = s_tls_server_stop,
    .delete = s_tls_server_delete,
};

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

int dap_net_trans_tls_server_init(void)
{
    int l_ret = dap_net_trans_server_register_ops(DAP_NET_TRANS_TLS_DIRECT, &s_tls_server_ops);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register TLS server operations: %d", l_ret);
        return l_ret;
    }
    log_it(L_NOTICE, "TLS mimicry server module initialized");
    return 0;
}

void dap_net_trans_tls_server_deinit(void)
{
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_TLS_DIRECT);
    log_it(L_INFO, "TLS mimicry server module deinitialized");
}
