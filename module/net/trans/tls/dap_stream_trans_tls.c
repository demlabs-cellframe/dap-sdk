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
#include "dap_stream.h"
#include "dap_stream_pkt.h"
#include "dap_tls_mimicry.h"
#include "dap_stream_trans_tls.h"

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

static const dap_net_trans_ops_t s_tls_ops = {
    .init             = s_tls_init,
    .deinit           = s_tls_deinit,
    .stage_prepare    = s_tls_stage_prepare,
    .write            = s_tls_write,
    .read             = s_tls_read,
    .close            = s_tls_close,
    .get_capabilities = s_tls_get_caps,
    .handshake_init   = NULL,
    .session_create   = NULL,
    .session_start    = NULL
};

/* ========================================================================== */
/*  Registration                                                              */
/* ========================================================================== */

int dap_stream_trans_tls_register(void)
{
    s_config = dap_stream_trans_tls_config_default();

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
    log_it(L_DEBUG, "TLS Mimicry transport deinitialized");
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
    l_es->flags |= DAP_SOCK_CONNECTING;

    if (a_params->worker)
        dap_worker_add_events_socket(a_params->worker, l_es);

    int l_err = 0;
    dap_events_socket_connect(l_es, &l_err);
    if (l_err) {
        log_it(L_ERROR, "TLS Mimicry: TCP connect error: %d", l_err);
        a_result->error_code = l_err;
        return -1;
    }

    a_result->esocket = l_es;
    a_result->error_code = 0;

    const char *l_sni = s_config.sni_hostname ? s_config.sni_hostname : a_params->host;
    log_it(L_INFO, "TLS Mimicry: TCP connect to %s:%u initiated (SNI=%s)",
           a_params->host, a_params->port, l_sni);
    return 0;
}

static ssize_t s_tls_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_stream->trans_ctx || !a_stream->trans_ctx->esocket
        || !a_data || a_size == 0)
        return -1;

    void *l_wrapped = NULL;
    size_t l_wrapped_size = 0;
    tls_mimicry_ctx_t *l_ctx = (tls_mimicry_ctx_t *)a_stream->trans_ctx->_inheritor;

    if (l_ctx && l_ctx->mimicry
        && dap_tls_mimicry_get_state(l_ctx->mimicry) == DAP_TLS_MIMICRY_STATE_ESTABLISHED) {
        if (dap_tls_mimicry_wrap(l_ctx->mimicry, a_data, a_size,
                                 &l_wrapped, &l_wrapped_size) != 0) {
            log_it(L_ERROR, "TLS record wrap failed");
            return -1;
        }
        ssize_t l_ret = dap_events_socket_write_unsafe(
            a_stream->trans_ctx->esocket, l_wrapped, l_wrapped_size);
        DAP_DELETE(l_wrapped);
        return l_ret >= 0 ? (ssize_t)a_size : -1;
    }

    return dap_events_socket_write_unsafe(a_stream->trans_ctx->esocket, a_data, a_size);
}

static ssize_t s_tls_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_stream->trans_ctx || !a_stream->trans_ctx->esocket
        || !a_buffer || a_size == 0)
        return -1;

    dap_events_socket_t *l_es = a_stream->trans_ctx->esocket;
    size_t l_avail = l_es->buf_in_size;
    if (l_avail == 0)
        return 0;

    tls_mimicry_ctx_t *l_ctx = (tls_mimicry_ctx_t *)a_stream->trans_ctx->_inheritor;

    if (l_ctx && l_ctx->mimicry
        && dap_tls_mimicry_get_state(l_ctx->mimicry) == DAP_TLS_MIMICRY_STATE_ESTABLISHED) {
        void *l_unwrapped = NULL;
        size_t l_unwrapped_size = 0, l_consumed = 0;
        int l_rc = dap_tls_mimicry_unwrap(l_ctx->mimicry,
                                          l_es->buf_in, l_avail,
                                          &l_unwrapped, &l_unwrapped_size, &l_consumed);
        if (l_rc < 0) {
            log_it(L_ERROR, "TLS record unwrap failed");
            return -1;
        }
        if (l_rc == 1 || l_unwrapped_size == 0)
            return 0;

        size_t l_to_copy = (l_unwrapped_size < a_size) ? l_unwrapped_size : a_size;
        memcpy(a_buffer, l_unwrapped, l_to_copy);
        DAP_DELETE(l_unwrapped);
        return (ssize_t)l_to_copy;
    }

    size_t l_to_read = (l_avail < a_size) ? l_avail : a_size;
    memcpy(a_buffer, l_es->buf_in, l_to_read);
    return (ssize_t)l_to_read;
}

static void s_tls_close(dap_stream_t *a_stream)
{
    if (!a_stream)
        return;
    log_it(L_DEBUG, "TLS Mimicry: closing stream");

    if (a_stream->trans_ctx) {
        tls_mimicry_ctx_t *l_ctx = (tls_mimicry_ctx_t *)a_stream->trans_ctx->_inheritor;
        if (l_ctx) {
            dap_tls_mimicry_free(l_ctx->mimicry);
            DAP_DEL_Z(l_ctx->sni_hostname);
            DAP_DELETE(l_ctx);
            a_stream->trans_ctx->_inheritor = NULL;
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
/*  Utility                                                                   */
/* ========================================================================== */

bool dap_stream_trans_is_tls(const dap_stream_t *a_stream)
{
    return a_stream && a_stream->trans
        && a_stream->trans->type == DAP_NET_TRANS_TLS_DIRECT;
}
