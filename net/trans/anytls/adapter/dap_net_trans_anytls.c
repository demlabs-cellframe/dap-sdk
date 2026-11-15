/*
 * @file dap_net_trans_anytls.c
 * @brief AnyTLS client transport adapter — SDK-aware glue (trust + carrier
 *        bridge + carrier_dap). Mirrors the REALITY/Trojan isolation pattern.
 *
 * The carrier bridge runs the blocking anytls_session (TLS + auth + session
 * frames, single stream) on a thread; carrier_dap drives the node's HTTP-DAP
 * wire format through the pipe. The AnyTLS front forwards the stream to the
 * node listener (dest_host:dest_port, default 127.0.0.1:8079).
 *
 * Client role only. No hardcoded secrets: password/pin provisioned via IPC
 * or config; the adapter hashes the password (SHA256) and the default padding
 * scheme (MD5).
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

#include <openssl/evp.h>
#include <openssl/md5.h>

#include "dap_tls.h"
#include "anytls/anytls.h"
#include "carrier/carrier.h"
#include "dap_carrier_dap.h"

#define LOG_TAG "dap_net_trans_anytls"

#define ANYTLS_DEFAULT_SNI         "www.samsung.com"
#define ANYTLS_DEFAULT_DEST_HOST   "127.0.0.1"
#define ANYTLS_DEFAULT_DEST_PORT   8079
#define ANYTLS_DEFAULT_PORT        443
#define ANYTLS_CONNECT_TIMEOUT_SEC 15

const anytls_tls_ops_t *anytls_tls_dap(void);

typedef struct anytls_adapter_ctx {
    dap_carrier_dap_ctx_t base;     /* MUST be first */
    carrier_bridge_t *bridge;
    anytls_config_t acfg;
    char host[CARRIER_HOST_MAX];
    uint16_t port;
    int timeout_sec;
    char iface[32];
} anytls_adapter_ctx_t;

static void s_hook_signal_stop(void *a_user)
{
    anytls_adapter_ctx_t *l_ctx = a_user;
    if (l_ctx->bridge) carrier_bridge_signal_stop(l_ctx->bridge);
}
static void s_hook_stop(void *a_user)
{
    anytls_adapter_ctx_t *l_ctx = a_user;
    if (l_ctx->bridge) { carrier_bridge_stop(l_ctx->bridge); l_ctx->bridge = NULL; }
}
static void s_hook_destroy(void *a_user) { DAP_DELETE(a_user); }

static carrier_io_ops_t *s_io_connect(void *a_user, const char *a_host, uint16_t a_port,
                                      int a_timeout, const char *a_iface)
{
    (void)a_user;
    return carrier_io_socket_connect(a_host, a_port, a_timeout, a_iface);
}
/* anytls_io_ops_t and carrier_io_ops_t are layout-identical. */
static void *s_sess_connect(void *a_user, carrier_io_ops_t *a_io)
{
    return anytls_session_connect((anytls_config_t *)a_user,
                                  (anytls_io_ops_t *)a_io, anytls_tls_dap());
}
static int s_sess_read(void *a_user, void *a_s, uint8_t *a_b, size_t a_l, size_t *a_o)
{
    (void)a_user; return anytls_session_read((anytls_session_t *)a_s, a_b, a_l, a_o);
}
static int s_sess_write(void *a_user, void *a_s, const uint8_t *a_b, size_t a_l)
{
    (void)a_user; return anytls_session_write((anytls_session_t *)a_s, a_b, a_l);
}
static bool s_sess_pending(void *a_user, void *a_s)
{
    (void)a_user; return anytls_session_has_pending((anytls_session_t *)a_s);
}
static void s_sess_close(void *a_user, void *a_s)
{
    (void)a_user; anytls_session_close((anytls_session_t *)a_s);
}

static int s_hex_decode(const char *a_hex, uint8_t *a_out, size_t a_out_len)
{
    if (strlen(a_hex) != a_out_len * 2) return -1;
    for (size_t i = 0; i < a_out_len; ++i) {
        unsigned int l_b;
        if (sscanf(a_hex + 2 * i, "%2x", &l_b) != 1) return -1;
        a_out[i] = (uint8_t)l_b;
    }
    return 0;
}

static int s_hello_profile_from_index(uint8_t a_idx)
{
    switch (a_idx) {
        case 1: return DAP_TLS_HELLO_FIREFOX;
        case 2: return DAP_TLS_HELLO_EDGE;
        case 3: return DAP_TLS_HELLO_SAFARI;
        default: return DAP_TLS_HELLO_CHROME;
    }
}

static int s_anytls_stage_prepare(dap_net_trans_t *a_trans,
                                  const dap_net_stage_prepare_params_t *a_params,
                                  dap_net_stage_prepare_result_t *a_result)
{
    if (!a_params || !a_params->host || !a_result) return -1;
    memset(a_result, 0, sizeof(*a_result));
    a_result->error_code = -1;

    dap_client_t *l_client = (dap_client_t *)a_params->client_ctx;
    anytls_adapter_ctx_t *l_ctx = DAP_NEW_Z(anytls_adapter_ctx_t);
    if (!l_ctx) { log_it(L_CRITICAL, "ANYTLS: ctx OOM"); return -1; }

    dap_carrier_dap_ctx_t *l_base = &l_ctx->base;
    l_base->client = l_client;
    l_base->phase = DAP_CARRIER_PHASE_CONNECTING;
    l_base->base_cbs = *a_params->callbacks;
    l_base->log_tag = "ANYTLS";
    l_base->default_host = ANYTLS_DEFAULT_DEST_HOST;
    l_base->hooks.user = l_ctx;
    l_base->hooks.signal_stop = s_hook_signal_stop;
    l_base->hooks.stop = s_hook_stop;
    l_base->hooks.destroy = s_hook_destroy;

    l_ctx->port = a_params->port ? a_params->port : ANYTLS_DEFAULT_PORT;
    l_ctx->timeout_sec = ANYTLS_CONNECT_TIMEOUT_SEC;
    dap_strncpy(l_ctx->host, a_params->host, sizeof(l_ctx->host) - 1);
    dap_strncpy(l_ctx->acfg.dest_host, ANYTLS_DEFAULT_DEST_HOST, sizeof(l_ctx->acfg.dest_host) - 1);
    l_ctx->acfg.dest_port = ANYTLS_DEFAULT_DEST_PORT;

    const char *l_pw = l_client && l_client->anytls_password[0] ? l_client->anytls_password : NULL;
    const char *l_pin = l_client && l_client->tls13_spki_sha256[0] ? l_client->tls13_spki_sha256 : NULL;
    const char *l_sni = l_client && l_client->tls13_server_name[0] ? l_client->tls13_server_name : NULL;

    char l_section[512];
    snprintf(l_section, sizeof(l_section), "anytls:%s:%u", a_params->host, l_ctx->port);
    if (g_config) {
        if (!l_pw) l_pw = dap_config_get_item_str(g_config, l_section, "password");
        if (!l_pin) l_pin = dap_config_get_item_str(g_config, l_section, "spki_sha256");
        if (!l_sni) l_sni = dap_config_get_item_str(g_config, l_section, "server_name");
        const char *l_dest = dap_config_get_item_str(g_config, l_section, "dest_host");
        if (l_dest && *l_dest)
            dap_strncpy(l_ctx->acfg.dest_host, l_dest, sizeof(l_ctx->acfg.dest_host) - 1);
        uint16_t l_dest_port = dap_config_get_item_uint16_default(g_config, l_section, "dest_port", 0);
        if (l_dest_port) l_ctx->acfg.dest_port = l_dest_port;
        const char *l_iface = dap_config_get_item_str(g_config, l_section, "iface");
        if (l_iface && *l_iface) dap_strncpy(l_ctx->iface, l_iface, sizeof(l_ctx->iface) - 1);
    }

    if (!l_pw || !*l_pw) {
        log_it(L_ERROR, "ANYTLS: endpoint %s has no password (config [%s])", a_params->host, l_section);
        DAP_DELETE(l_ctx); return -1;
    }
    unsigned int l_dlen = 0;
    if (EVP_Digest(l_pw, strlen(l_pw), l_ctx->acfg.password_hash, &l_dlen, EVP_sha256(), NULL) != 1 || l_dlen != 32) {
        log_it(L_ERROR, "ANYTLS: password hash failed");
        DAP_DELETE(l_ctx); return -1;
    }
    if (!l_pin || s_hex_decode(l_pin, l_ctx->acfg.spki_sha256, 32) != 0) {
        log_it(L_ERROR, "ANYTLS: endpoint %s has no valid SPKI pin (config [%s])", a_params->host, l_section);
        DAP_DELETE(l_ctx); return -1;
    }
    l_ctx->acfg.have_pin = true;
    dap_strncpy(l_ctx->acfg.sni, (l_sni && *l_sni) ? l_sni : ANYTLS_DEFAULT_SNI, sizeof(l_ctx->acfg.sni) - 1);
    l_ctx->acfg.hello_profile = s_hello_profile_from_index(l_client ? l_client->tls_fp_profile_index : 0);

    /* MD5 of the default padding scheme for cmdSettings padding-md5. */
    uint8_t l_md5[16]; unsigned int l_md5_len = 0;
    if (EVP_Digest(ANYTLS_DEFAULT_PADDING_SCHEME, strlen(ANYTLS_DEFAULT_PADDING_SCHEME),
                   l_md5, &l_md5_len, EVP_md5(), NULL) == 1 && l_md5_len == 16) {
        for (int i = 0; i < 16; ++i) snprintf(l_ctx->acfg.padding_md5 + 2 * i, 3, "%02x", l_md5[i]);
    }

    carrier_bridge_cfg_t l_bcfg; memset(&l_bcfg, 0, sizeof(l_bcfg));
    dap_strncpy(l_bcfg.host, l_ctx->host, sizeof(l_bcfg.host) - 1);
    l_bcfg.port = l_ctx->port;
    l_bcfg.timeout_sec = l_ctx->timeout_sec;
    dap_strncpy(l_bcfg.iface, l_ctx->iface, sizeof(l_bcfg.iface) - 1);
    l_bcfg.io_connect = s_io_connect;
    l_bcfg.io_user = NULL;
    l_bcfg.session.user = &l_ctx->acfg;
    l_bcfg.session.connect = s_sess_connect;
    l_bcfg.session.read = s_sess_read;
    l_bcfg.session.write = s_sess_write;
    l_bcfg.session.has_pending = s_sess_pending;
    l_bcfg.session.close = s_sess_close;
    l_bcfg.err_io = ANYTLS_ERR_IO;
    l_bcfg.err_closed = ANYTLS_ERR_CLOSED;
    l_bcfg.err_again = ANYTLS_ERR_AGAIN;

    l_ctx->bridge = carrier_bridge_start(&l_bcfg);
    if (!l_ctx->bridge) {
        log_it(L_ERROR, "ANYTLS: bridge start failed for %s:%u", l_ctx->host, l_ctx->port);
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
    log_it(L_INFO, "ANYTLS: bridge to %s:%u started (sni=%s, dest=%s:%u), stream created",
           l_ctx->host, l_ctx->port, l_ctx->acfg.sni, l_ctx->acfg.dest_host, l_ctx->acfg.dest_port);
    return 0;
}

static uint32_t s_anytls_caps(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return DAP_NET_TRANS_CAP_RELIABLE | DAP_NET_TRANS_CAP_ORDERED |
           DAP_NET_TRANS_CAP_OBFUSCATION | DAP_NET_TRANS_CAP_BIDIRECTIONAL |
           DAP_NET_TRANS_CAP_HIGH_THROUGHPUT | DAP_NET_TRANS_CAP_MIMICRY |
           DAP_NET_TRANS_CAP_PADDING | DAP_NET_TRANS_CAP_MULTIPLEXING;
}

static const dap_net_trans_ops_t s_anytls_ops = {
    .init = NULL, .deinit = NULL, .connect = NULL, .listen = NULL, .accept = NULL,
    .handshake_init = dap_carrier_dap_handshake_init, .handshake_process = NULL,
    .session_create = dap_carrier_dap_session_create, .session_start = dap_carrier_dap_session_start,
    .read = NULL, .write = dap_carrier_dap_write, .close = dap_carrier_dap_close,
    .get_capabilities = s_anytls_caps, .register_server_handlers = NULL,
    .stage_prepare = s_anytls_stage_prepare, .get_client_context = NULL, .get_max_packet_size = NULL,
};

int dap_net_trans_anytls_register(void)
{
    if (dap_net_trans_find(DAP_NET_TRANS_ANYTLS)) return 0;
    int l_rc = dap_net_trans_register("anytls", DAP_NET_TRANS_ANYTLS,
                                      &s_anytls_ops, DAP_NET_TRANS_SOCKET_TCP, NULL);
    if (l_rc) { log_it(L_ERROR, "ANYTLS: registration failed (%d)", l_rc); return l_rc; }
    log_it(L_NOTICE, "ANYTLS client transport registered (type 0x%02X)", DAP_NET_TRANS_ANYTLS);
    return 0;
}

int dap_net_trans_anytls_unregister(void)
{
    if (!dap_net_trans_find(DAP_NET_TRANS_ANYTLS)) return 0;
    int l_rc = dap_net_trans_unregister(DAP_NET_TRANS_ANYTLS);
    if (l_rc == 0) log_it(L_NOTICE, "ANYTLS client transport unregistered");
    return l_rc;
}