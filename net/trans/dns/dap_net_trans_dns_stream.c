/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025-2026
 * All rights reserved.
 */

#include <string.h>
#include <time.h>
#ifndef DAP_OS_WINDOWS
#include <arpa/inet.h>
#endif
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_config.h"
#include "dap_net_trans_dns_stream.h"
#include "dap_net_trans_dns_server.h"
#include "dap_dns_tunnel_wire.h"
#include "dap_net_trans.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_timerfd.h"
#include "dap_net.h"

#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include "dap_stream_handshake.h"
#include "dap_stream.h"
#include "dap_server.h"
#include "dap_enc_server.h"
#include "dap_enc_kdf.h"
#include "dap_net_trans_qos.h"
#include "dap_client.h"
#include "dap_client_fsm.h"
#include "dap_client_trans_ctx.h"
#include "dap_net_trans_ctx.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_stream_trans_dns"

#define DNS_MAX_FRAGS 32U
#define DNS_MAX_OUT 64U
#define DNS_MAX_MSG (16U * 1024U)
#define DNS_RETRY_MS 400U
#define DNS_POLL_MS 300U
#define DNS_MAX_RETRIES 8U

static bool s_debug_more = false;

static int s_dns_init(dap_net_trans_t *a_trans, dap_config_t *a_config);
static void s_dns_deinit(dap_net_trans_t *a_trans);
static int s_dns_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port,
        dap_net_trans_connect_cb_t a_callback);
static int s_dns_listen(dap_net_trans_t *a_trans, const char *a_addr, uint16_t a_port,
        dap_server_t *a_server);
static int s_dns_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out);
static int s_dns_handshake_init(dap_stream_t *a_stream,
        dap_net_handshake_params_t *a_params,
        dap_net_trans_handshake_cb_t a_callback);
static int s_dns_handshake_process(dap_stream_t *a_stream,
        const void *a_data, size_t a_data_size,
        void **a_response, size_t *a_response_size);
static int s_dns_session_create(dap_stream_t *a_stream,
        dap_net_session_params_t *a_params,
        dap_net_trans_session_cb_t a_callback);
static int s_dns_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
        dap_net_trans_ready_cb_t a_callback);
static ssize_t s_dns_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size);
static ssize_t s_dns_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static void s_dns_close(dap_stream_t *a_stream);
static uint32_t s_dns_get_capabilities(dap_net_trans_t *a_trans);
static size_t s_dns_get_max_packet_size(dap_net_trans_t *a_trans);
static int s_dns_stage_prepare(dap_net_trans_t *a_trans,
        const dap_net_stage_prepare_params_t *a_params,
        dap_net_stage_prepare_result_t *a_result);

static const dap_net_trans_ops_t s_dns_ops = {
    .init = s_dns_init,
    .deinit = s_dns_deinit,
    .connect = s_dns_connect,
    .listen = s_dns_listen,
    .accept = s_dns_accept,
    .handshake_init = s_dns_handshake_init,
    .handshake_process = s_dns_handshake_process,
    .session_create = s_dns_session_create,
    .session_start = s_dns_session_start,
    .read = s_dns_read,
    .write = s_dns_write,
    .close = s_dns_close,
    .get_capabilities = s_dns_get_capabilities,
    .register_server_handlers = NULL,
    .stage_prepare = s_dns_stage_prepare,
    .get_max_packet_size = s_dns_get_max_packet_size
};

typedef struct dns_out_frag {
    uint8_t type;
    uint32_t message_id;
    uint16_t index;
    uint16_t count;
    uint16_t payload_len;
    uint8_t retries;
    bool acked;
    uint8_t *payload;
} dns_out_frag_t;

typedef struct dns_reasm {
    uint32_t message_id;
    uint8_t type;
    uint16_t fragment_count;
    uint16_t received;
    uint16_t lens[DNS_MAX_FRAGS];
    uint8_t *frags[DNS_MAX_FRAGS];
} dns_reasm_t;

typedef struct dns_client_priv {
    uint64_t client_nonce;
    uint64_t session_cookie;
    uint32_t stream_id;
    uint32_t next_message_id;
    uint16_t next_txid;
    size_t query_budget;
    char suffix[256];
    dap_net_trans_handshake_cb_t handshake_cb;
    bool handshake_done;
    bool polling;
    bool closed;
    bool inflight;
    uint16_t inflight_txid;
    uint8_t retries;
    uint32_t last_ack_msg;
    uint16_t last_ack_frag;
    size_t out_count;
    size_t out_pos;
    dns_out_frag_t out[DNS_MAX_OUT];
    dns_reasm_t reasm;
    dap_timerfd_t *retry_timer;
    dap_timerfd_t *poll_timer;
    uint8_t last_query[DAP_DNS_TUNNEL_EDNS_UDP_SIZE];
    size_t last_query_size;
} dns_client_priv_t;

static dap_stream_trans_dns_private_t *s_get_private(dap_net_trans_t *a_trans);
static dns_client_priv_t *s_get_or_create_client_ctx(dap_stream_t *a_stream);
static void s_dns_client_read_cb(dap_events_socket_t *a_es, void *a_arg);
static void s_client_free_priv(dns_client_priv_t *a_ctx);
static int s_client_send_next(dap_stream_t *a_stream, dns_client_priv_t *a_ctx);
static bool s_client_retry_cb(void *a_arg);
static bool s_client_poll_cb(void *a_arg);

int dap_net_trans_dns_stream_register(void)
{
    int l_ret = dap_net_trans_dns_server_init();
    if(l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize DNS server module: %d", l_ret);
        return l_ret;
    }
    int l_ret_trans = dap_net_trans_register("DNS_TUNNEL",
            DAP_NET_TRANS_DNS_TUNNEL,
            &s_dns_ops,
            DAP_NET_TRANS_SOCKET_UDP,
            NULL);
    if(l_ret_trans != 0) {
        log_it(L_ERROR, "Failed to register DNS tunnel trans: %d", l_ret_trans);
        dap_net_trans_dns_server_deinit();
        return l_ret_trans;
    }
    log_it(L_NOTICE, "DNS tunnel trans registered successfully");
    return 0;
}

int dap_net_trans_dns_stream_unregister(void)
{
    int l_ret = dap_net_trans_unregister(DAP_NET_TRANS_DNS_TUNNEL);
    if(l_ret != 0) {
        log_it(L_ERROR, "Failed to unregister DNS tunnel trans: %d", l_ret);
        return l_ret;
    }
    dap_net_trans_dns_server_deinit();
    log_it(L_NOTICE, "DNS tunnel trans unregistered successfully");
    return 0;
}

dap_stream_trans_dns_config_t dap_stream_trans_dns_config_default(void)
{
    dap_stream_trans_dns_config_t l_config = {
        .max_record_size = DAP_STREAM_DNS_DEFAULT_MAX_RECORD_SIZE,
        .max_query_size = DAP_STREAM_DNS_DEFAULT_MAX_QUERY_SIZE,
        .query_timeout_ms = DAP_STREAM_DNS_DEFAULT_TIMEOUT_MS,
        .use_base32 = true,
        .enable_compression = false,
        .domain_suffix = NULL
    };
    return l_config;
}

int dap_stream_trans_dns_set_config(dap_net_trans_t *a_trans,
        const dap_stream_trans_dns_config_t *a_config)
{
    if(!a_trans || !a_config)
        return -1;
    dap_stream_trans_dns_private_t *l_priv = s_get_private(a_trans);
    if(!l_priv)
        return -2;
    DAP_DELETE(l_priv->config.domain_suffix);
    l_priv->config = *a_config;
    if(a_config->domain_suffix)
        l_priv->config.domain_suffix = dap_strdup(a_config->domain_suffix);
    return 0;
}

int dap_stream_trans_dns_get_config(dap_net_trans_t *a_trans,
        dap_stream_trans_dns_config_t *a_config)
{
    if(!a_trans || !a_config)
        return -1;
    dap_stream_trans_dns_private_t *l_priv = s_get_private(a_trans);
    if(!l_priv)
        return -2;
    *a_config = l_priv->config;
    if(l_priv->config.domain_suffix)
        a_config->domain_suffix = dap_strdup(l_priv->config.domain_suffix);
    return 0;
}

bool dap_stream_trans_is_dns(const dap_stream_t *a_stream)
{
    return a_stream && a_stream->trans &&
            a_stream->trans->type == DAP_NET_TRANS_DNS_TUNNEL;
}

dap_stream_trans_dns_private_t *dap_stream_trans_dns_get_private(dap_stream_t *a_stream)
{
    if(!a_stream || !a_stream->trans ||
            a_stream->trans->type != DAP_NET_TRANS_DNS_TUNNEL)
        return NULL;
    return (dap_stream_trans_dns_private_t *)a_stream->trans->_inheritor;
}

static int s_dns_init(dap_net_trans_t *a_trans, dap_config_t *a_config)
{
    if(!a_trans)
        return -1;
    dap_stream_trans_dns_private_t *l_priv = DAP_NEW_Z(dap_stream_trans_dns_private_t);
    if(!l_priv)
        return -2;
    l_priv->config = dap_stream_trans_dns_config_default();
    if(a_config) {
        s_debug_more = dap_config_get_item_bool_default(a_config, "dns",
                "debug_more", false);
        const char *l_suffix = dap_config_get_item_str(a_config, "dns",
                "domain_suffix");
        if(l_suffix)
            l_priv->config.domain_suffix = dap_strdup(l_suffix);
    }
    if(!l_priv->config.domain_suffix)
        l_priv->config.domain_suffix = dap_strdup(DAP_DNS_TUNNEL_DEFAULT_SUFFIX);
    a_trans->_inheritor = l_priv;
    a_trans->has_session_control = false;
    log_it(L_INFO, "DNS tunnel trans initialized (suffix=%s)",
            l_priv->config.domain_suffix);
    return 0;
}

static void s_dns_deinit(dap_net_trans_t *a_trans)
{
    if(!a_trans)
        return;
    dap_stream_trans_dns_private_t *l_priv = s_get_private(a_trans);
    if(l_priv) {
        DAP_DELETE(l_priv->config.domain_suffix);
        DAP_DELETE(l_priv);
        a_trans->_inheritor = NULL;
    }
}

static int s_dns_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port,
        dap_net_trans_connect_cb_t a_callback)
{
    if(!a_stream || !a_host || !a_stream->trans)
        return -1;
    dap_stream_trans_dns_private_t *l_priv = s_get_private(a_stream->trans);
    if(!l_priv)
        return -1;
    struct sockaddr_in *l_addr_in = (struct sockaddr_in *)&l_priv->remote_addr;
    l_addr_in->sin_family = AF_INET;
    l_addr_in->sin_port = htons(a_port);
    if(inet_pton(AF_INET, a_host, &l_addr_in->sin_addr) != 1) {
        log_it(L_ERROR, "Invalid IPv4 address: %s", a_host);
        return -1;
    }
    l_priv->remote_addr_len = sizeof(struct sockaddr_in);
    l_priv->esocket = a_stream->esocket;
    if(l_priv->esocket) {
        memcpy(&l_priv->esocket->addr_storage, &l_priv->remote_addr,
                l_priv->remote_addr_len);
        l_priv->esocket->addr_size = l_priv->remote_addr_len;
    }
    log_it(L_INFO, "DNS tunnel trans connecting to %s:%u", a_host, a_port);
    if(a_callback)
        a_callback(a_stream, 0);
    return 0;
}

static int s_dns_listen(dap_net_trans_t *a_trans, const char *a_addr, uint16_t a_port,
        dap_server_t *a_server)
{
    UNUSED(a_trans);
    UNUSED(a_server);
    log_it(L_INFO, "DNS tunnel trans listening on %s:%u",
            a_addr ? a_addr : "0.0.0.0", a_port);
    return 0;
}

static int s_dns_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out)
{
    if(!a_listener || !a_stream_out)
        return -1;
    return 0;
}

static void s_reasm_reset(dns_reasm_t *a_reasm)
{
    if(!a_reasm)
        return;
    for(size_t i = 0; i < DNS_MAX_FRAGS; ++i)
        DAP_DEL_Z(a_reasm->frags[i]);
    memset(a_reasm, 0, sizeof(*a_reasm));
}

static void s_client_free_priv(dns_client_priv_t *a_ctx)
{
    if(!a_ctx)
        return;
    if(a_ctx->retry_timer) {
        dap_timerfd_delete_unsafe(a_ctx->retry_timer);
        a_ctx->retry_timer = NULL;
    }
    if(a_ctx->poll_timer) {
        dap_timerfd_delete_unsafe(a_ctx->poll_timer);
        a_ctx->poll_timer = NULL;
    }
    for(size_t i = 0; i < a_ctx->out_count; ++i)
        DAP_DELETE(a_ctx->out[i].payload);
    s_reasm_reset(&a_ctx->reasm);
    DAP_DELETE(a_ctx);
}

static int s_enqueue_message(dns_client_priv_t *a_ctx, uint8_t a_type,
        const uint8_t *a_data, size_t a_size)
{
    if(!a_ctx || a_size > DNS_MAX_MSG)
        return -1;
    size_t l_budget = a_ctx->query_budget ? a_ctx->query_budget : 32;
    size_t l_count = a_size ? (a_size + l_budget - 1) / l_budget : 1;
    if(!l_count || l_count > DNS_MAX_FRAGS ||
            a_ctx->out_count + l_count > DNS_MAX_OUT)
        return -1;
    uint32_t l_message_id = ++a_ctx->next_message_id;
    size_t l_offset = 0;
    for(size_t i = 0; i < l_count; ++i) {
        size_t l_chunk = a_size - l_offset;
        if(l_chunk > l_budget)
            l_chunk = l_budget;
        dns_out_frag_t *l_frag = &a_ctx->out[a_ctx->out_count++];
        memset(l_frag, 0, sizeof(*l_frag));
        l_frag->type = a_type;
        l_frag->message_id = l_message_id;
        l_frag->index = (uint16_t)i;
        l_frag->count = (uint16_t)l_count;
        l_frag->payload_len = (uint16_t)l_chunk;
        if(l_chunk) {
            l_frag->payload = DAP_DUP_SIZE(a_data + l_offset, l_chunk);
            if(!l_frag->payload)
                return -1;
        }
        l_offset += l_chunk;
    }
    return 0;
}

static dns_out_frag_t *s_next_unacked(dns_client_priv_t *a_ctx)
{
    for(size_t i = a_ctx->out_pos; i < a_ctx->out_count; ++i) {
        if(!a_ctx->out[i].acked)
            return &a_ctx->out[i];
        a_ctx->out_pos = i + 1;
    }
    return NULL;
}

static int s_client_send_frame(dap_stream_t *a_stream, dns_client_priv_t *a_ctx,
        const dap_dns_tunnel_frame_t *a_frame)
{
    dap_events_socket_t *l_es = a_stream->esocket;
    if(!l_es)
        return -1;
    size_t l_size = sizeof(a_ctx->last_query);
    int l_rc = dap_dns_tunnel_wire_build_query(a_ctx->next_txid, a_ctx->suffix,
            a_frame, a_ctx->last_query, &l_size);
    if(l_rc != DAP_DNS_TUNNEL_WIRE_OK)
        return -1;
    size_t l_sent = dap_events_socket_sendto_unsafe(l_es, a_ctx->last_query,
            l_size, &l_es->addr_storage, l_es->addr_size);
    if(l_sent != l_size)
        return -1;
    a_ctx->last_query_size = l_size;
    a_ctx->inflight_txid = a_ctx->next_txid++;
    a_ctx->inflight = true;
    a_ctx->retries = 0;
    if(l_es->worker && !a_ctx->retry_timer)
        a_ctx->retry_timer = dap_timerfd_start_on_worker(l_es->worker,
                DNS_RETRY_MS, s_client_retry_cb, a_stream);
    return 0;
}

static int s_client_send_next(dap_stream_t *a_stream, dns_client_priv_t *a_ctx)
{
    if(!a_stream || !a_ctx || a_ctx->closed || a_ctx->inflight)
        return 0;
    dns_out_frag_t *l_frag = s_next_unacked(a_ctx);
    dap_dns_tunnel_frame_t l_frame = {
        .type = l_frag ? l_frag->type : DAP_DNS_TUNNEL_MSG_POLL,
        .client_nonce = a_ctx->client_nonce,
        .session_cookie = a_ctx->session_cookie,
        .message_id = l_frag ? l_frag->message_id : a_ctx->next_message_id,
        .fragment_index = l_frag ? l_frag->index : 0,
        .fragment_count = l_frag ? l_frag->count : 1,
        .ack_message_id = a_ctx->last_ack_msg,
        .ack_fragment_index = a_ctx->last_ack_frag,
        .payload_length = l_frag ? l_frag->payload_len : 0,
        .payload = l_frag ? l_frag->payload : NULL
    };
    if(!l_frag && !a_ctx->polling && a_ctx->handshake_done)
        return 0;
    if(!l_frag && !a_ctx->polling && !a_ctx->handshake_cb)
        return 0;
    return s_client_send_frame(a_stream, a_ctx, &l_frame);
}

static bool s_client_retry_cb(void *a_arg)
{
    dap_stream_t *l_stream = (dap_stream_t *)a_arg;
    if(!l_stream || !l_stream->trans_ctx)
        return false;
    dns_client_priv_t *l_ctx = (dns_client_priv_t *)l_stream->trans_ctx->transport_priv;
    if(!l_ctx || l_ctx->closed) {
        if(l_ctx)
            l_ctx->retry_timer = NULL;
        return false;
    }
    if(!l_ctx->inflight || !l_stream->esocket)
        return true;
    if(l_ctx->retries++ >= DNS_MAX_RETRIES) {
        log_it(L_ERROR, "DNS client: query retry budget exhausted");
        l_ctx->inflight = false;
        if(l_ctx->handshake_cb && !l_ctx->handshake_done) {
            dap_net_trans_handshake_cb_t l_cb = l_ctx->handshake_cb;
            l_ctx->handshake_cb = NULL;
            l_cb(l_stream, NULL, 0, -1);
        }
        return true;
    }
    dap_events_socket_sendto_unsafe(l_stream->esocket, l_ctx->last_query,
            l_ctx->last_query_size, &l_stream->esocket->addr_storage,
            l_stream->esocket->addr_size);
    return true;
}

static bool s_client_poll_cb(void *a_arg)
{
    dap_stream_t *l_stream = (dap_stream_t *)a_arg;
    if(!l_stream || !l_stream->trans_ctx)
        return false;
    dns_client_priv_t *l_ctx = (dns_client_priv_t *)l_stream->trans_ctx->transport_priv;
    if(!l_ctx || l_ctx->closed) {
        if(l_ctx)
            l_ctx->poll_timer = NULL;
        return false;
    }
    s_client_send_next(l_stream, l_ctx);
    return true;
}

static int s_reasm_add(dns_reasm_t *a_reasm, const dap_dns_tunnel_frame_t *a_frame,
        uint8_t **a_out, size_t *a_out_size)
{
    if(!a_frame->fragment_count || a_frame->fragment_count > DNS_MAX_FRAGS)
        return -1;
    if(a_reasm->fragment_count && a_reasm->message_id != a_frame->message_id)
        s_reasm_reset(a_reasm);
    if(!a_reasm->fragment_count) {
        a_reasm->message_id = a_frame->message_id;
        a_reasm->type = a_frame->type;
        a_reasm->fragment_count = a_frame->fragment_count;
    } else if(a_reasm->fragment_count != a_frame->fragment_count ||
            a_reasm->type != a_frame->type) {
        return -1;
    }
    if(a_reasm->frags[a_frame->fragment_index])
        return 0;
    if(a_frame->payload_length) {
        a_reasm->frags[a_frame->fragment_index] =
                DAP_DUP_SIZE(a_frame->payload, a_frame->payload_length);
        if(!a_reasm->frags[a_frame->fragment_index])
            return -1;
    }
    a_reasm->lens[a_frame->fragment_index] = a_frame->payload_length;
    a_reasm->received++;
    if(a_reasm->received != a_reasm->fragment_count)
        return 0;
    size_t l_total = 0;
    for(uint16_t i = 0; i < a_reasm->fragment_count; ++i)
        l_total += a_reasm->lens[i];
    uint8_t *l_buf = l_total ? DAP_NEW_SIZE(uint8_t, l_total) : DAP_NEW_Z(uint8_t);
    if(!l_buf)
        return -1;
    size_t l_off = 0;
    for(uint16_t i = 0; i < a_reasm->fragment_count; ++i) {
        if(a_reasm->lens[i] && a_reasm->frags[i]) {
            memcpy(l_buf + l_off, a_reasm->frags[i], a_reasm->lens[i]);
            l_off += a_reasm->lens[i];
        }
    }
    s_reasm_reset(a_reasm);
    *a_out = l_buf;
    *a_out_size = l_total;
    return 1;
}

static int s_finish_handshake(dap_stream_t *a_stream, dns_client_priv_t *a_ctx,
        const uint8_t *a_payload, size_t a_size)
{
    if(a_size >= sizeof(dap_qos_echo_pkt_t)) {
        const dap_qos_echo_pkt_t *l_echo = (const dap_qos_echo_pkt_t *)a_payload;
        if(l_echo->magic == DAP_QOS_ECHO_MAGIC) {
            a_ctx->handshake_done = true;
            if(a_ctx->handshake_cb)
                a_ctx->handshake_cb(a_stream, a_payload, a_size, 0);
            a_ctx->handshake_cb = NULL;
            return 0;
        }
    }
    if(a_size < 4) {
        if(a_ctx->handshake_cb)
            a_ctx->handshake_cb(a_stream, a_payload, a_size, 0);
        a_ctx->handshake_cb = NULL;
        a_ctx->handshake_done = true;
        return 0;
    }
    uint32_t l_stream_id = ((uint32_t)a_payload[0] << 24) |
            ((uint32_t)a_payload[1] << 16) |
            ((uint32_t)a_payload[2] << 8) | a_payload[3];
    const uint8_t *l_kem = a_payload + 4;
    size_t l_kem_size = a_size - 4;
    dap_client_t *l_client = (dap_client_t *)a_stream->esocket->_inheritor;
    dap_client_fsm_t *l_fsm = l_client ? DAP_CLIENT_FSM(l_client) : NULL;
    dap_net_trans_ctx_t *l_tc = l_fsm ? l_fsm->trans_ctx : NULL;
    if(!l_tc || !l_tc->session_key_open ||
            !l_tc->session_key_open->gen_alice_shared_key) {
        if(a_ctx->handshake_cb)
            a_ctx->handshake_cb(a_stream, NULL, 0, -1);
        a_ctx->handshake_cb = NULL;
        return -1;
    }
    size_t l_shared = l_tc->session_key_open->gen_alice_shared_key(
            l_tc->session_key_open, l_tc->session_key_open->priv_key_data,
            l_kem_size, (void *)l_kem);
    if(!l_shared || !l_tc->session_key_open->shared_key) {
        if(a_ctx->handshake_cb)
            a_ctx->handshake_cb(a_stream, NULL, 0, -1);
        a_ctx->handshake_cb = NULL;
        return -1;
    }
    dap_enc_key_t *l_key = dap_enc_kdf_create_cipher_key(
            l_tc->session_key_open, DAP_ENC_KEY_TYPE_SALSA2012,
            "dns_handshake", 13, 0, 32);
    if(!l_key) {
        if(a_ctx->handshake_cb)
            a_ctx->handshake_cb(a_stream, NULL, 0, -1);
        a_ctx->handshake_cb = NULL;
        return -1;
    }
    if(l_tc->stream_key)
        dap_enc_key_delete(l_tc->stream_key);
    l_tc->stream_key = dap_enc_key_dup(l_key);
    dap_enc_key_delete(l_key);
    a_ctx->stream_id = l_stream_id;
    a_ctx->handshake_done = true;
    log_it(L_INFO, "DNS client: handshake complete, session=%u cookie=0x%"DAP_UINT64_FORMAT_x,
            l_stream_id, a_ctx->session_cookie);
    if(a_ctx->handshake_cb)
        a_ctx->handshake_cb(a_stream, NULL, 0, 0);
    a_ctx->handshake_cb = NULL;
    return 0;
}

static int s_dns_handshake_init(dap_stream_t *a_stream,
        dap_net_handshake_params_t *a_params,
        dap_net_trans_handshake_cb_t a_callback)
{
    if(!a_stream || !a_params || !a_stream->esocket)
        return -1;
    if(!a_params->alice_pub_key_size)
        return -1;
    dns_client_priv_t *l_ctx = s_get_or_create_client_ctx(a_stream);
    if(!l_ctx)
        return -1;
    l_ctx->handshake_cb = a_callback;
    a_stream->esocket->callbacks.read_callback = s_dns_client_read_cb;
    if(s_enqueue_message(l_ctx, DAP_DNS_TUNNEL_MSG_HANDSHAKE,
            a_params->alice_pub_key, a_params->alice_pub_key_size) != 0)
        return -1;
    log_it(L_INFO, "DNS handshake init: enc_type=%d key_size=%zu fragments queued",
            a_params->enc_type, a_params->alice_pub_key_size);
    return s_client_send_next(a_stream, l_ctx);
}

static int s_dns_handshake_process(dap_stream_t *a_stream,
        const void *a_data, size_t a_data_size,
        void **a_response, size_t *a_response_size)
{
    UNUSED(a_stream);
    UNUSED(a_data);
    UNUSED(a_data_size);
    if(a_response)
        *a_response = NULL;
    if(a_response_size)
        *a_response_size = 0;
    return 0;
}

static int s_dns_session_create(dap_stream_t *a_stream,
        dap_net_session_params_t *a_params,
        dap_net_trans_session_cb_t a_callback)
{
    UNUSED(a_params);
    if(!a_stream || !a_stream->trans_ctx)
        return -1;
    dns_client_priv_t *l_ctx = (dns_client_priv_t *)a_stream->trans_ctx->transport_priv;
    if(!l_ctx || !l_ctx->handshake_done || !l_ctx->stream_id)
        return -1;
    if(a_callback)
        a_callback(a_stream, l_ctx->stream_id, NULL, 0, 0);
    return 0;
}

static int s_dns_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
        dap_net_trans_ready_cb_t a_callback)
{
    if(!a_stream || !a_stream->trans_ctx || !a_stream->esocket)
        return -1;
    dns_client_priv_t *l_ctx = (dns_client_priv_t *)a_stream->trans_ctx->transport_priv;
    if(!l_ctx)
        return -1;
    UNUSED(a_session_id);
    l_ctx->polling = true;
    if(a_stream->esocket->worker && !l_ctx->poll_timer)
        l_ctx->poll_timer = dap_timerfd_start_on_worker(a_stream->esocket->worker,
                DNS_POLL_MS, s_client_poll_cb, a_stream);
    s_client_send_next(a_stream, l_ctx);
    if(a_callback)
        a_callback(a_stream, 0);
    return 0;
}

static ssize_t s_dns_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    UNUSED(a_buffer);
    UNUSED(a_size);
    if(!a_stream || !a_stream->esocket)
        return 0;
    return 0;
}

static ssize_t s_dns_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if(!a_stream || !a_data || !a_size || !a_stream->trans_ctx)
        return -1;
    dns_client_priv_t *l_ctx = (dns_client_priv_t *)a_stream->trans_ctx->transport_priv;
    if(!l_ctx || !l_ctx->handshake_done)
        return -1;
    if(s_enqueue_message(l_ctx, DAP_DNS_TUNNEL_MSG_DATA, a_data, a_size) != 0)
        return -1;
    if(s_client_send_next(a_stream, l_ctx) != 0)
        return -1;
    return (ssize_t)a_size;
}

static void s_dns_close(dap_stream_t *a_stream)
{
    if(!a_stream)
        return;
    if(a_stream->trans_ctx && a_stream->trans_ctx->transport_priv) {
        dns_client_priv_t *l_ctx = (dns_client_priv_t *)a_stream->trans_ctx->transport_priv;
        l_ctx->closed = true;
        s_client_free_priv(l_ctx);
        a_stream->trans_ctx->transport_priv = NULL;
    }
    if(a_stream->trans) {
        dap_stream_trans_dns_private_t *l_priv = s_get_private(a_stream->trans);
        if(l_priv) {
            l_priv->esocket = NULL;
            memset(&l_priv->remote_addr, 0, sizeof(l_priv->remote_addr));
            l_priv->remote_addr_len = 0;
        }
    }
}

static int s_dns_stage_prepare(dap_net_trans_t *a_trans,
        const dap_net_stage_prepare_params_t *a_params,
        dap_net_stage_prepare_result_t *a_result)
{
    if(!a_trans || !a_params || !a_result || !a_params->worker)
        return -1;
    a_result->esocket = NULL;
    a_result->stream = NULL;
    a_result->error_code = 0;
    dap_events_socket_t *l_es = dap_events_socket_create_platform(PF_INET,
            SOCK_DGRAM, IPPROTO_UDP, a_params->callbacks);
    if(!l_es) {
        a_result->error_code = -1;
        return -1;
    }
    l_es->type = DESCRIPTOR_TYPE_SOCKET_UDP;
    l_es->_inheritor = a_params->client_ctx;
    int l_buf_size = 4 * 1024 * 1024;
    setsockopt(l_es->fd, SOL_SOCKET, SO_RCVBUF, (const char *)&l_buf_size,
            sizeof(l_buf_size));
    setsockopt(l_es->fd, SOL_SOCKET, SO_SNDBUF, (const char *)&l_buf_size,
            sizeof(l_buf_size));
    if(dap_events_socket_resolve_and_set_addr(l_es, a_params->host,
            a_params->port) < 0) {
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
#ifdef DAP_OS_WINDOWS
    {
        struct sockaddr_in l_bind_addr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = 0
        };
        if(bind(l_es->socket, (struct sockaddr *)&l_bind_addr,
                sizeof(l_bind_addr)) < 0) {
            dap_events_socket_delete_unsafe(l_es, true);
            a_result->error_code = -1;
            return -1;
        }
        l_es->addr_size = sizeof(struct sockaddr_in);
    }
#endif
    dap_worker_add_events_socket(a_params->worker, l_es);
    dap_stream_t *l_stream = dap_stream_new_es_client(l_es,
            (dap_stream_node_addr_t *)a_params->node_addr, a_params->authorized);
    if(!l_stream) {
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    l_stream->trans = a_trans;
    a_result->esocket = l_es;
    a_result->stream = l_stream;
    return 0;
}

static uint32_t s_dns_get_capabilities(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return DAP_NET_TRANS_CAP_OBFUSCATION |
            DAP_NET_TRANS_CAP_LOW_LATENCY |
            DAP_NET_TRANS_CAP_BIDIRECTIONAL;
}

static size_t s_dns_get_max_packet_size(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return 4096;
}

static dns_client_priv_t *s_get_or_create_client_ctx(dap_stream_t *a_stream)
{
    if(!a_stream || !a_stream->trans_ctx)
        return NULL;
    if(a_stream->trans_ctx->transport_priv)
        return (dns_client_priv_t *)a_stream->trans_ctx->transport_priv;
    dns_client_priv_t *l_ctx = DAP_NEW_Z(dns_client_priv_t);
    if(!l_ctx)
        return NULL;
    l_ctx->client_nonce = m_dap_random_u64();
    l_ctx->next_txid = (uint16_t)m_dap_random_u16();
    dap_stream_trans_dns_private_t *l_priv = s_get_private(a_stream->trans);
    const char *l_suffix = l_priv && l_priv->config.domain_suffix ?
            l_priv->config.domain_suffix : DAP_DNS_TUNNEL_DEFAULT_SUFFIX;
    dap_strncpy(l_ctx->suffix, l_suffix, sizeof(l_ctx->suffix) - 1);
    size_t l_response = 0;
    if(dap_dns_tunnel_wire_payload_budget(l_ctx->suffix, &l_ctx->query_budget,
            &l_response) != DAP_DNS_TUNNEL_WIRE_OK)
        l_ctx->query_budget = 32;
    a_stream->trans_ctx->transport_priv = l_ctx;
    return l_ctx;
}

static void s_dns_client_read_cb(dap_events_socket_t *a_es, void *a_arg)
{
    UNUSED(a_arg);
    if(!a_es || a_es->buf_in_size == 0)
        return;
    dap_client_t *l_client = (dap_client_t *)a_es->_inheritor;
    if(!l_client) {
        a_es->buf_in_size = 0;
        return;
    }
    dap_client_trans_ctx_touch(l_client);
    a_es->last_time_active = time(NULL);
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(l_client);
    dap_net_trans_ctx_t *l_tc = l_fsm ? l_fsm->trans_ctx : NULL;
    if(!l_fsm || !l_tc || !l_tc->stream || !l_tc->transport_priv) {
        a_es->buf_in_size = 0;
        return;
    }
    dns_client_priv_t *l_ctx = (dns_client_priv_t *)l_tc->transport_priv;
    dap_stream_t *l_stream = l_tc->stream;
    uint8_t l_payload[DAP_DNS_TUNNEL_EDNS_UDP_SIZE];
    size_t l_payload_size = sizeof(l_payload);
    uint16_t l_txid = 0;
    dap_dns_tunnel_frame_t l_frame;
    int l_rc = dap_dns_tunnel_wire_parse_response(a_es->buf_in, a_es->buf_in_size,
            l_ctx->suffix, &l_txid, &l_frame, l_payload, &l_payload_size);
    a_es->buf_in_size = 0;
    if(l_rc != DAP_DNS_TUNNEL_WIRE_OK) {
        debug_if(s_debug_more, L_DEBUG, "DNS client: dropping malformed response (%d)", l_rc);
        return;
    }
    if(l_ctx->inflight && l_txid != l_ctx->inflight_txid)
        return;
    l_ctx->inflight = false;
    if(l_frame.session_cookie)
        l_ctx->session_cookie = l_frame.session_cookie;
    for(size_t i = 0; i < l_ctx->out_count; ++i) {
        if(l_ctx->out[i].message_id == l_frame.ack_message_id &&
                l_ctx->out[i].index == l_frame.ack_fragment_index)
            l_ctx->out[i].acked = true;
    }
    if(l_frame.type == DAP_DNS_TUNNEL_MSG_HANDSHAKE ||
            l_frame.type == DAP_DNS_TUNNEL_MSG_DATA) {
        l_ctx->last_ack_msg = l_frame.message_id;
        l_ctx->last_ack_frag = l_frame.fragment_index;
        uint8_t *l_msg = NULL;
        size_t l_msg_size = 0;
        int l_ready = s_reasm_add(&l_ctx->reasm, &l_frame, &l_msg, &l_msg_size);
        if(l_ready > 0) {
            if(l_frame.type == DAP_DNS_TUNNEL_MSG_HANDSHAKE && !l_ctx->handshake_done)
                s_finish_handshake(l_stream, l_ctx, l_msg, l_msg_size);
            else if(l_frame.type == DAP_DNS_TUNNEL_MSG_DATA && l_msg_size)
                dap_stream_data_proc_read_ext(l_stream, l_msg, l_msg_size);
            DAP_DELETE(l_msg);
        }
    }
    s_client_send_next(l_stream, l_ctx);
}

static dap_stream_trans_dns_private_t *s_get_private(dap_net_trans_t *a_trans)
{
    if(!a_trans)
        return NULL;
    return (dap_stream_trans_dns_private_t *)a_trans->_inheritor;
}
