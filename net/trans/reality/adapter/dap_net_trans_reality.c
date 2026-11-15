/*
 * @file dap_net_trans_reality.c
 * @brief REALITY (VLESS) client transport adapter — the ONLY file in the
 *        reality module that knows about dap-sdk types.
 *
 * Architecture (per adapter/DESIGN.md, plan phase 0):
 *   - reality_bridge (port/, over the shared carrier) runs the blocking
 *     REALITY+VLESS session on a dedicated thread and exposes it as a byte
 *     pipe + ctrl pipe.
 *   - The shared HTTP-DAP phase machine (net/trans/carrier_dap) drives the
 *     node's wire format (POST /enc_init, POST /stream_ctl, GET /stream +
 *     takeover) through the pipe. This adapter only resolves REALITY trust,
 *     starts the bridge, wraps the fds and creates the stream; the ops vtable
 *     and esocket callbacks point at the shared carrier_dap functions.
 *
 * Client role only: listen/accept are NULL (server = Xray front).
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

#include "reality/reality.h"
#include "reality_bridge.h"
#include "dap_carrier_dap.h"

#define LOG_TAG "dap_net_trans_reality"

/* Reality endpoint defaults (overridable via [reality:<host>:<port>] config) */
#define REALITY_DEFAULT_SNI         "www.samsung.com"
#define REALITY_DEFAULT_DEST_HOST   "127.0.0.1"
#define REALITY_DEFAULT_DEST_PORT   8079    /* node's HTTP-DAP listener behind Xray */
#define REALITY_DEFAULT_PORT        443
#define REALITY_CONNECT_TIMEOUT_SEC 15

typedef struct reality_adapter_ctx {
    dap_carrier_dap_ctx_t base;     /* MUST be first: shared ops cast to it */
    reality_bridge_t *bridge;
    reality_bridge_cfg_t cfg;
} reality_adapter_ctx_t;

/* ---- carrier_dap hooks (bridge lifecycle + ctx destruction) ---- */

static void s_reality_hook_signal_stop(void *a_user)
{
    reality_adapter_ctx_t *l_ctx = a_user;
    if (l_ctx->bridge)
        reality_bridge_signal_stop(l_ctx->bridge);
}

static void s_reality_hook_stop(void *a_user)
{
    reality_adapter_ctx_t *l_ctx = a_user;
    if (l_ctx->bridge) {
        reality_bridge_stop(l_ctx->bridge);
        l_ctx->bridge = NULL;
    }
}

static void s_reality_hook_destroy(void *a_user)
{
    DAP_DELETE(a_user);
}

/* ------------------------------------------------------------------ */
/* stage_prepare: trust resolution + bridge + esockets + stream         */
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
    dap_carrier_dap_ctx_t *l_base = &l_ctx->base;
    l_base->client = l_client;
    l_base->phase = DAP_CARRIER_PHASE_CONNECTING;
    l_base->base_cbs = *a_params->callbacks;
    l_base->log_tag = "REALITY";
    l_base->default_host = REALITY_DEFAULT_DEST_HOST;
    l_base->hooks.user = l_ctx;
    l_base->hooks.signal_stop = s_reality_hook_signal_stop;
    l_base->hooks.stop = s_reality_hook_stop;
    l_base->hooks.destroy = s_reality_hook_destroy;

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
        .read_callback   = dap_carrier_dap_read_cb,
        .write_callback  = NULL,           /* reactor flushes buf_out itself */
        .error_callback  = dap_carrier_dap_es_error,
        .delete_callback = dap_carrier_dap_es_delete,
        .connected_callback = NULL,
        .arg             = l_base,
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
    l_base->es = l_es;
    if (a_params->worker)
        dap_worker_add_events_socket(a_params->worker, l_es);

    dap_events_socket_callbacks_t l_ctrl_cbs = {
        .read_callback   = dap_carrier_dap_ctrl_read_cb,
        .write_callback  = NULL,
        .error_callback  = NULL,
        .delete_callback = dap_carrier_dap_ctrl_delete,
        .connected_callback = NULL,
        .arg             = l_base,
    };
    l_base->ctrl_es = dap_events_socket_wrap_no_add((SOCKET)l_ctrl_fd, &l_ctrl_cbs);
    if (!l_base->ctrl_es) {
        log_it(L_ERROR, "REALITY: ctrl esocket wrap failed");
        memset(&l_es->callbacks, 0, sizeof(l_es->callbacks));
        if (l_es->worker)
            dap_events_socket_remove_and_delete_unsafe(l_es, true);
        else
            dap_events_socket_delete_unsafe(l_es, true);
        l_base->es = NULL;
        reality_bridge_stop(l_ctx->bridge);
        l_ctx->bridge = NULL;
        DAP_DELETE(l_ctx);
        return -1;
    }
    if (a_params->worker)
        dap_worker_add_events_socket(a_params->worker, l_base->ctrl_es);

    /* Staging trans_ctx (FSM adopts transport_priv into its own ctx). */
    dap_net_trans_ctx_t *l_staging = DAP_NEW_Z(dap_net_trans_ctx_t);
    if (!l_staging) {
        log_it(L_CRITICAL, "REALITY: staging trans_ctx OOM");
        memset(&l_es->callbacks, 0, sizeof(l_es->callbacks));
        if (l_es->worker)
            dap_events_socket_remove_and_delete_unsafe(l_es, true);
        else
            dap_events_socket_delete_unsafe(l_es, true);
        l_base->es = NULL;
        memset(&l_base->ctrl_es->callbacks, 0, sizeof(l_base->ctrl_es->callbacks));
        if (l_base->ctrl_es->worker)
            dap_events_socket_remove_and_delete_unsafe(l_base->ctrl_es, true);
        else
            dap_events_socket_delete_unsafe(l_base->ctrl_es, true);
        l_base->ctrl_es = NULL;
        reality_bridge_stop(l_ctx->bridge);
        l_ctx->bridge = NULL;
        DAP_DELETE(l_ctx);
        return -1;
    }
    l_staging->transport_priv = l_base;
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
        l_base->es = NULL;
        memset(&l_base->ctrl_es->callbacks, 0, sizeof(l_base->ctrl_es->callbacks));
        if (l_base->ctrl_es->worker)
            dap_events_socket_remove_and_delete_unsafe(l_base->ctrl_es, true);
        else
            dap_events_socket_delete_unsafe(l_base->ctrl_es, true);
        l_base->ctrl_es = NULL;
        reality_bridge_stop(l_ctx->bridge);
        l_ctx->bridge = NULL;
        DAP_DELETE(l_ctx);
        return -1;
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
    .handshake_init          = dap_carrier_dap_handshake_init,
    .handshake_process       = NULL,
    .session_create          = dap_carrier_dap_session_create,
    .session_start           = dap_carrier_dap_session_start,
    .read                    = NULL,
    .write                   = dap_carrier_dap_write,
    .close                   = dap_carrier_dap_close,
    .get_capabilities        = s_reality_caps,
    .register_server_handlers = NULL,
    .stage_prepare           = s_reality_stage_prepare,
    .get_client_context      = NULL,
    .get_max_packet_size     = NULL,   /* streaming transport */
};

int dap_net_trans_reality_register(void)
{
    /* The registry is the ONLY source of truth here. dap_net_trans_deinit()
     * wipes it during stack teardown, so a module-level "already registered"
     * flag made the second stack init skip registration silently — the
     * transport then vanished from the fallback list and every later connect
     * fell back to HTTP. */
    if (dap_net_trans_find(DAP_NET_TRANS_REALITY))
        return 0;
    int l_rc = dap_net_trans_register("reality", DAP_NET_TRANS_REALITY,
                                      &s_reality_ops, DAP_NET_TRANS_SOCKET_TCP, NULL);
    if (l_rc) {
        log_it(L_ERROR, "REALITY: transport registration failed (%d)", l_rc);
        return l_rc;
    }
    log_it(L_NOTICE, "REALITY (VLESS) client transport registered (type 0x%02X)",
           DAP_NET_TRANS_REALITY);
    return 0;
}

int dap_net_trans_reality_unregister(void)
{
    if (!dap_net_trans_find(DAP_NET_TRANS_REALITY))
        return 0;
    int l_rc = dap_net_trans_unregister(DAP_NET_TRANS_REALITY);
    if (l_rc == 0)
        log_it(L_NOTICE, "REALITY (VLESS) client transport unregistered");
    return l_rc;
}