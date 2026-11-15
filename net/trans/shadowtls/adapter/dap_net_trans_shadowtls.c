/*
 * @file dap_net_trans_shadowtls.c
 * @brief ShadowTLS v3 client transport adapter — SDK-aware glue (trust +
 *        carrier bridge + carrier_dap). Mirrors the REALITY/Trojan/AnyTLS/
 *        XHTTP isolation pattern.
 *
 * The carrier bridge runs the blocking shadowtls_session (OpenSSL relayed
 * handshake + HMAC data framing) on a thread; carrier_dap drives the node's
 * HTTP-DAP wire format through the pipe. The ShadowTLS front forwards the
 * decrypted stream to its --server backend (deployed to the node listener).
 *
 * Client role only. No hardcoded secrets: password provisioned via IPC/config.
 */
#include <string.h>
#include <stdio.h>

#include "dap_common.h"
#include "dap_config.h"
#include "dap_events_socket.h"
#include "dap_net_trans.h"
#include "dap_net_trans_ctx.h"
#include "dap_client.h"
#include "dap_stream.h"

#include "shadowtls/shadowtls.h"
#include "carrier/carrier.h"
#include "dap_carrier_dap.h"

#define LOG_TAG "dap_net_trans_shadowtls"

#define SHADOWTLS_DEFAULT_SNI         "www.samsung.com"
#define SHADOWTLS_DEFAULT_PORT        443
#define SHADOWTLS_CONNECT_TIMEOUT_SEC 20

typedef struct shadowtls_adapter_ctx {
    dap_carrier_dap_ctx_t base;     /* MUST be first */
    carrier_bridge_t *bridge;
    shadowtls_config_t scfg;
    char host[CARRIER_HOST_MAX];
    uint16_t port;
    int timeout_sec;
    char iface[32];
} shadowtls_adapter_ctx_t;

static void s_hook_signal_stop(void *a_user)
{
    shadowtls_adapter_ctx_t *l_ctx = a_user;
    if (l_ctx->bridge) carrier_bridge_signal_stop(l_ctx->bridge);
}
static void s_hook_stop(void *a_user)
{
    shadowtls_adapter_ctx_t *l_ctx = a_user;
    if (l_ctx->bridge) { carrier_bridge_stop(l_ctx->bridge); l_ctx->bridge = NULL; }
}
static void s_hook_destroy(void *a_user) { DAP_DELETE(a_user); }

static carrier_io_ops_t *s_io_connect(void *a_user, const char *a_host, uint16_t a_port,
                                      int a_timeout, const char *a_iface)
{
    (void)a_user;
    return carrier_io_socket_connect(a_host, a_port, a_timeout, a_iface);
}
/* carrier_io_ops_t and shadowtls_io_ops_t are layout-identical but NOT
 * code-identical: carrier write returns 0 on success (writes all) and uses
 * CARRIER_ERR_AGAIN=-4/CLOSED=-3, while the shadowtls core expects a >0 byte
 * count and SHADOWTLS_ERR_AGAIN=-5/CLOSED=-4. Adapt at the DI boundary. */
typedef struct shadowtls_io_shim {
    carrier_io_ops_t *c;
    shadowtls_io_ops_t ops;
} shadowtls_io_shim_t;

static int s_shim_read(void *a_u, uint8_t *a_b, size_t a_l, size_t *a_o)
{
    shadowtls_io_shim_t *l_sh = a_u;
    int l_rc = l_sh->c->read(l_sh->c->user, a_b, a_l, a_o);
    if (l_rc == CARRIER_OK) return SHADOWTLS_ERR_NONE;
    if (l_rc == CARRIER_ERR_AGAIN) return SHADOWTLS_ERR_AGAIN;
    if (l_rc == CARRIER_ERR_CLOSED) return SHADOWTLS_ERR_CLOSED;
    return SHADOWTLS_ERR_IO;
}
static int s_shim_write(void *a_u, const uint8_t *a_b, size_t a_l)
{
    shadowtls_io_shim_t *l_sh = a_u;
    int l_rc = l_sh->c->write(l_sh->c->user, a_b, a_l);
    if (l_rc == CARRIER_OK) return (int)a_l;
    if (l_rc == CARRIER_ERR_AGAIN) return SHADOWTLS_ERR_AGAIN;
    return SHADOWTLS_ERR_IO;
}
static void s_shim_close(void *a_u)
{
    shadowtls_io_shim_t *l_sh = a_u;
    if (l_sh->c && l_sh->c->close) l_sh->c->close(l_sh->c->user);
    DAP_DELETE(l_sh);
}
static int s_shim_fd(void *a_u)
{
    shadowtls_io_shim_t *l_sh = a_u;
    return l_sh->c->fd ? l_sh->c->fd(l_sh->c->user) : -1;
}
static void s_shim_shutdown(void *a_u)
{
    shadowtls_io_shim_t *l_sh = a_u;
    if (l_sh->c->shutdown) l_sh->c->shutdown(l_sh->c->user);
}
static void *s_sess_connect(void *a_user, carrier_io_ops_t *a_io)
{
    shadowtls_io_shim_t *l_sh = DAP_NEW_Z(shadowtls_io_shim_t);
    if (!l_sh) return NULL;
    l_sh->c = a_io;
    l_sh->ops.user = l_sh;
    l_sh->ops.read = s_shim_read;
    l_sh->ops.write = s_shim_write;
    l_sh->ops.close = s_shim_close;
    l_sh->ops.fd = s_shim_fd;
    l_sh->ops.shutdown = s_shim_shutdown;
    shadowtls_session_t *l_s = shadowtls_session_connect((shadowtls_config_t *)a_user, &l_sh->ops);
    if (!l_s) { DAP_DELETE(l_sh); return NULL; }
    return l_s;
}
static int s_sess_read(void *a_user, void *a_s, uint8_t *a_b, size_t a_l, size_t *a_o)
{
    (void)a_user; return shadowtls_session_read((shadowtls_session_t *)a_s, a_b, a_l, a_o);
}
static int s_sess_write(void *a_user, void *a_s, const uint8_t *a_b, size_t a_l)
{
    (void)a_user; return shadowtls_session_write((shadowtls_session_t *)a_s, a_b, a_l);
}
static bool s_sess_pending(void *a_user, void *a_s)
{
    (void)a_user; return shadowtls_session_has_pending((shadowtls_session_t *)a_s);
}
static void s_sess_close(void *a_user, void *a_s)
{
    (void)a_user; shadowtls_session_close((shadowtls_session_t *)a_s);
}

static int s_shadowtls_stage_prepare(dap_net_trans_t *a_trans,
                                     const dap_net_stage_prepare_params_t *a_params,
                                     dap_net_stage_prepare_result_t *a_result)
{
    if (!a_params || !a_params->host || !a_result) return -1;
    memset(a_result, 0, sizeof(*a_result));
    a_result->error_code = -1;

    dap_client_t *l_client = (dap_client_t *)a_params->client_ctx;
    shadowtls_adapter_ctx_t *l_ctx = DAP_NEW_Z(shadowtls_adapter_ctx_t);
    if (!l_ctx) { log_it(L_CRITICAL, "SHADOWTLS: ctx OOM"); return -1; }

    dap_carrier_dap_ctx_t *l_base = &l_ctx->base;
    l_base->client = l_client;
    l_base->phase = DAP_CARRIER_PHASE_CONNECTING;
    l_base->base_cbs = *a_params->callbacks;
    l_base->log_tag = "SHADOWTLS";
    l_base->default_host = "127.0.0.1";
    l_base->hooks.user = l_ctx;
    l_base->hooks.signal_stop = s_hook_signal_stop;
    l_base->hooks.stop = s_hook_stop;
    l_base->hooks.destroy = s_hook_destroy;

    l_ctx->port = a_params->port ? a_params->port : SHADOWTLS_DEFAULT_PORT;
    l_ctx->timeout_sec = SHADOWTLS_CONNECT_TIMEOUT_SEC;
    dap_strncpy(l_ctx->host, a_params->host, sizeof(l_ctx->host) - 1);

    const char *l_pw = l_client && l_client->shadowtls_password[0] ? l_client->shadowtls_password : NULL;
    const char *l_sni = l_client && l_client->tls13_server_name[0] ? l_client->tls13_server_name : NULL;

    char l_section[512];
    snprintf(l_section, sizeof(l_section), "shadowtls:%s:%u", a_params->host, l_ctx->port);
    if (g_config) {
        if (!l_pw) l_pw = dap_config_get_item_str(g_config, l_section, "password");
        if (!l_sni) l_sni = dap_config_get_item_str(g_config, l_section, "server_name");
        const char *l_iface = dap_config_get_item_str(g_config, l_section, "iface");
        if (l_iface && *l_iface) dap_strncpy(l_ctx->iface, l_iface, sizeof(l_ctx->iface) - 1);
    }

    if (!l_pw || !*l_pw) {
        log_it(L_ERROR, "SHADOWTLS: endpoint %s has no password (config [%s])", a_params->host, l_section);
        DAP_DELETE(l_ctx); return -1;
    }
    dap_strncpy(l_ctx->scfg.password, l_pw, sizeof(l_ctx->scfg.password) - 1);
    dap_strncpy(l_ctx->scfg.sni, (l_sni && *l_sni) ? l_sni : SHADOWTLS_DEFAULT_SNI,
                sizeof(l_ctx->scfg.sni) - 1);

    carrier_bridge_cfg_t l_bcfg; memset(&l_bcfg, 0, sizeof(l_bcfg));
    dap_strncpy(l_bcfg.host, l_ctx->host, sizeof(l_bcfg.host) - 1);
    l_bcfg.port = l_ctx->port;
    l_bcfg.timeout_sec = l_ctx->timeout_sec;
    dap_strncpy(l_bcfg.iface, l_ctx->iface, sizeof(l_bcfg.iface) - 1);
    l_bcfg.io_connect = s_io_connect;
    l_bcfg.io_user = NULL;
    l_bcfg.session.user = &l_ctx->scfg;
    l_bcfg.session.connect = s_sess_connect;
    l_bcfg.session.read = s_sess_read;
    l_bcfg.session.write = s_sess_write;
    l_bcfg.session.has_pending = s_sess_pending;
    l_bcfg.session.close = s_sess_close;
    l_bcfg.err_io = SHADOWTLS_ERR_IO;
    l_bcfg.err_closed = SHADOWTLS_ERR_CLOSED;
    l_bcfg.err_again = SHADOWTLS_ERR_AGAIN;

    l_ctx->bridge = carrier_bridge_start(&l_bcfg);
    if (!l_ctx->bridge) {
        log_it(L_ERROR, "SHADOWTLS: bridge start failed for %s:%u", l_ctx->host, l_ctx->port);
        DAP_DELETE(l_ctx); return -1;
    }
    int l_data_fd = carrier_bridge_data_fd(l_ctx->bridge);
    int l_ctrl_fd = carrier_bridge_ctrl_fd(l_ctx->bridge);
    if (l_data_fd < 0 || l_ctrl_fd < 0) {
        carrier_bridge_stop(l_ctx->bridge); l_ctx->bridge = NULL;
        DAP_DELETE(l_ctx); return -1;
    }

    dap_events_socket_callbacks_t l_cbs = {
        .read_callback = dap_carrier_dap_read_cb, .write_callback = NULL,
        .error_callback = dap_carrier_dap_es_error, .delete_callback = dap_carrier_dap_es_delete,
        .connected_callback = NULL, .arg = l_base,
    };
    dap_events_socket_t *l_es = dap_events_socket_wrap_no_add((SOCKET)l_data_fd, &l_cbs);
    if (!l_es) { carrier_bridge_stop(l_ctx->bridge); l_ctx->bridge = NULL; DAP_DELETE(l_ctx); return -1; }
    l_es->_inheritor = a_params->client_ctx;
    l_es->flags |= DAP_SOCK_READY_TO_WRITE;
    l_base->es = l_es;
    if (a_params->worker) dap_worker_add_events_socket(a_params->worker, l_es);

    dap_events_socket_callbacks_t l_ctrl_cbs = {
        .read_callback = dap_carrier_dap_ctrl_read_cb, .write_callback = NULL,
        .error_callback = NULL, .delete_callback = dap_carrier_dap_ctrl_delete,
        .connected_callback = NULL, .arg = l_base,
    };
    l_base->ctrl_es = dap_events_socket_wrap_no_add((SOCKET)l_ctrl_fd, &l_ctrl_cbs);
    if (!l_base->ctrl_es) {
        memset(&l_es->callbacks, 0, sizeof(l_es->callbacks));
        if (l_es->worker) dap_events_socket_remove_and_delete_unsafe(l_es, true);
        else dap_events_socket_delete_unsafe(l_es, true);
        l_base->es = NULL;
        carrier_bridge_stop(l_ctx->bridge); l_ctx->bridge = NULL; DAP_DELETE(l_ctx); return -1;
    }
    if (a_params->worker) dap_worker_add_events_socket(a_params->worker, l_base->ctrl_es);

    dap_net_trans_ctx_t *l_staging = DAP_NEW_Z(dap_net_trans_ctx_t);
    if (!l_staging) {
        memset(&l_es->callbacks, 0, sizeof(l_es->callbacks));
        if (l_es->worker) dap_events_socket_remove_and_delete_unsafe(l_es, true);
        else dap_events_socket_delete_unsafe(l_es, true);
        l_base->es = NULL;
        memset(&l_base->ctrl_es->callbacks, 0, sizeof(l_base->ctrl_es->callbacks));
        if (l_base->ctrl_es->worker) dap_events_socket_remove_and_delete_unsafe(l_base->ctrl_es, true);
        else dap_events_socket_delete_unsafe(l_base->ctrl_es, true);
        l_base->ctrl_es = NULL;
        carrier_bridge_stop(l_ctx->bridge); l_ctx->bridge = NULL; DAP_DELETE(l_ctx); return -1;
    }
    l_staging->transport_priv = l_base;
    l_staging->stream = NULL;

    dap_stream_t *l_stream = dap_stream_new_es_client(l_es,
                                (dap_stream_node_addr_t *)a_params->node_addr, a_params->authorized);
    if (!l_stream) {
        DAP_DELETE(l_staging);
        memset(&l_es->callbacks, 0, sizeof(l_es->callbacks));
        if (l_es->worker) dap_events_socket_remove_and_delete_unsafe(l_es, true);
        else dap_events_socket_delete_unsafe(l_es, true);
        l_base->es = NULL;
        memset(&l_base->ctrl_es->callbacks, 0, sizeof(l_base->ctrl_es->callbacks));
        if (l_base->ctrl_es->worker) dap_events_socket_remove_and_delete_unsafe(l_base->ctrl_es, true);
        else dap_events_socket_delete_unsafe(l_base->ctrl_es, true);
        l_base->ctrl_es = NULL;
        carrier_bridge_stop(l_ctx->bridge); l_ctx->bridge = NULL; DAP_DELETE(l_ctx); return -1;
    }
    l_stream->trans = a_trans;
    l_base->stream = l_stream;
    l_staging->stream = l_stream;
    l_staging->esocket = l_es;
    l_staging->trans = a_trans;
    l_stream->trans_ctx = l_staging;

    a_result->esocket = l_es;
    a_result->stream = l_stream;
    a_result->error_code = 0;
    log_it(L_INFO, "SHADOWTLS: bridge to %s:%u started (sni=%s), stream created",
           l_ctx->host, l_ctx->port, l_ctx->scfg.sni);
    return 0;
}

static uint32_t s_shadowtls_caps(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return DAP_NET_TRANS_CAP_RELIABLE | DAP_NET_TRANS_CAP_ORDERED |
           DAP_NET_TRANS_CAP_OBFUSCATION | DAP_NET_TRANS_CAP_BIDIRECTIONAL |
           DAP_NET_TRANS_CAP_HIGH_THROUGHPUT | DAP_NET_TRANS_CAP_MIMICRY;
}

static const dap_net_trans_ops_t s_shadowtls_ops = {
    .init = NULL, .deinit = NULL, .connect = NULL, .listen = NULL, .accept = NULL,
    .handshake_init = dap_carrier_dap_handshake_init, .handshake_process = NULL,
    .session_create = dap_carrier_dap_session_create, .session_start = dap_carrier_dap_session_start,
    .read = NULL, .write = dap_carrier_dap_write, .close = dap_carrier_dap_close,
    .get_capabilities = s_shadowtls_caps, .register_server_handlers = NULL,
    .stage_prepare = s_shadowtls_stage_prepare, .get_client_context = NULL, .get_max_packet_size = NULL,
};

int dap_net_trans_shadowtls_register(void)
{
    if (dap_net_trans_find(DAP_NET_TRANS_SHADOWTLS)) return 0;
    int l_rc = dap_net_trans_register("shadowtls", DAP_NET_TRANS_SHADOWTLS,
                                      &s_shadowtls_ops, DAP_NET_TRANS_SOCKET_TCP, NULL);
    if (l_rc) { log_it(L_ERROR, "SHADOWTLS: registration failed (%d)", l_rc); return l_rc; }
    log_it(L_NOTICE, "SHADOWTLS client transport registered (type 0x%02X)", DAP_NET_TRANS_SHADOWTLS);
    return 0;
}

int dap_net_trans_shadowtls_unregister(void)
{
    if (!dap_net_trans_find(DAP_NET_TRANS_SHADOWTLS)) return 0;
    int l_rc = dap_net_trans_unregister(DAP_NET_TRANS_SHADOWTLS);
    if (l_rc == 0) log_it(L_NOTICE, "SHADOWTLS client transport unregistered");
    return l_rc;
}