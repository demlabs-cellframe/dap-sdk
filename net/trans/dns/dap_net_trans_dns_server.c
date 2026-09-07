/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025-2026
 * All rights reserved.
 */

#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_config.h"
#include "dap_net_trans.h"
#include "dap_net_trans_dns_server.h"
#include "dap_net_trans_dns_stream.h"
#include "dap_dns_tunnel_wire.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_session.h"
#include "dap_stream_worker.h"
#include "dap_net_trans_server.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_timerfd.h"
#include "dap_context.h"
#include "dap_enc_key.h"
#include "dap_net_trans_qos.h"
#include "dap_enc_kdf.h"
#include "rand/dap_rand.h"

#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define LOG_TAG "dap_net_trans_dns_server"

#define DNS_MAX_FRAGS 32U
#define DNS_MAX_DOWN 64U
#define DNS_MAX_MSG (16U * 1024U)
#define DNS_MAX_SESSIONS 256U
#define DNS_IDLE_TTL_SEC 120
#define DNS_IDLE_TICK_MS 10000U

static bool s_debug_more = false;
static char s_domain_suffix[256] = DAP_DNS_TUNNEL_DEFAULT_SUFFIX;
static size_t s_query_budget = 32;
static size_t s_response_budget = 256;

static void s_dns_listener_read_cb(dap_events_socket_t *a_es, void *a_arg);
static ssize_t s_dns_server_trans_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static bool s_dns_idle_cb(void *a_arg);

typedef struct dns_reasm {
    uint32_t message_id;
    uint8_t type;
    uint16_t fragment_count;
    uint16_t received;
    uint16_t lens[DNS_MAX_FRAGS];
    uint8_t *frags[DNS_MAX_FRAGS];
} dns_reasm_t;

typedef struct dns_down_frag {
    uint8_t type;
    uint32_t message_id;
    uint16_t index;
    uint16_t count;
    uint16_t payload_len;
    bool acked;
    uint8_t *payload;
} dns_down_frag_t;

static dns_down_frag_t *s_down(dns_server_client_session_t *a_session)
{
    return (dns_down_frag_t *)a_session->down_queue;
}

static dns_reasm_t *s_reasm(dns_server_client_session_t *a_session)
{
    return (dns_reasm_t *)a_session->reasm;
}

static size_t s_dns_server_get_max_packet_size(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return 4096;
}

static const dap_net_trans_ops_t s_dns_server_trans_ops = {
    .write = s_dns_server_trans_write,
    .get_max_packet_size = s_dns_server_get_max_packet_size
};

static void *s_dns_server_new(const char *a_server_name)
{
    return (void *)dap_net_trans_dns_server_new(a_server_name);
}

static int s_dns_server_start(void *a_server, const char *a_cfg_section,
        const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    return dap_net_trans_dns_server_start((dap_net_trans_dns_server_t *)a_server,
            a_cfg_section, a_addrs, a_ports, a_count);
}

static void s_dns_server_stop(void *a_server)
{
    dap_net_trans_dns_server_stop((dap_net_trans_dns_server_t *)a_server);
}

static void s_dns_server_delete(void *a_server)
{
    dap_net_trans_dns_server_delete((dap_net_trans_dns_server_t *)a_server);
}

static const dap_net_trans_server_ops_t s_dns_server_ops = {
    .new = s_dns_server_new,
    .start = s_dns_server_start,
    .stop = s_dns_server_stop,
    .delete = s_dns_server_delete
};

int dap_net_trans_dns_server_init(void)
{
    int l_ret = dap_net_trans_server_register_ops(DAP_NET_TRANS_DNS_TUNNEL,
            &s_dns_server_ops);
    if(l_ret != 0) {
        log_it(L_ERROR, "Failed to register DNS trans server operations");
        return l_ret;
    }
    log_it(L_NOTICE, "Initialized DNS server module");
    return 0;
}

void dap_net_trans_dns_server_deinit(void)
{
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_DNS_TUNNEL);
}

dap_net_trans_dns_server_t *dap_net_trans_dns_server_new(const char *a_server_name)
{
    if(!a_server_name)
        return NULL;
    dap_net_trans_dns_server_t *l_dns_server = DAP_NEW_Z(dap_net_trans_dns_server_t);
    if(!l_dns_server)
        return NULL;
    dap_strncpy(l_dns_server->server_name, a_server_name,
            sizeof(l_dns_server->server_name) - 1);
    pthread_mutex_init(&l_dns_server->sessions_lock, NULL);
    l_dns_server->trans = DAP_NEW_Z(dap_net_trans_t);
    if(!l_dns_server->trans) {
        DAP_DELETE(l_dns_server);
        return NULL;
    }
    l_dns_server->trans->type = DAP_NET_TRANS_DNS_TUNNEL;
    l_dns_server->trans->ops = &s_dns_server_trans_ops;
    l_dns_server->trans->socket_type = DAP_NET_TRANS_SOCKET_UDP;
    return l_dns_server;
}

int dap_net_trans_dns_server_start(dap_net_trans_dns_server_t *a_dns_server,
        const char *a_cfg_section,
        const char **a_addrs,
        uint16_t *a_ports,
        size_t a_count)
{
    if(!a_dns_server || !a_ports || a_count == 0)
        return -1;
    if(a_dns_server->server)
        return -2;
    UNUSED(a_cfg_section);
    if(g_config) {
        s_debug_more = dap_config_get_item_bool_default(g_config, "dns",
                "debug_more", false);
        const char *l_suffix = dap_config_get_item_str(g_config, "dns",
                "domain_suffix");
        if(l_suffix)
            dap_strncpy(s_domain_suffix, l_suffix, sizeof(s_domain_suffix) - 1);
    }
    dap_dns_tunnel_wire_payload_budget(s_domain_suffix, &s_query_budget,
            &s_response_budget);
    dap_events_socket_callbacks_t l_dns_callbacks = {
        .read_callback = s_dns_listener_read_cb
    };
    a_dns_server->server = dap_server_new(NULL, NULL, &l_dns_callbacks);
    if(!a_dns_server->server)
        return -3;
    a_dns_server->server->_inheritor = a_dns_server;
    for(size_t i = 0; i < a_count; i++) {
        const char *l_addr = (a_addrs && a_addrs[i]) ? a_addrs[i] : "0.0.0.0";
        if(dap_server_listen_addr_add(a_dns_server->server, l_addr, a_ports[i],
                DESCRIPTOR_TYPE_SOCKET_UDP,
                &a_dns_server->server->client_callbacks) != 0) {
            dap_net_trans_dns_server_stop(a_dns_server);
            return -4;
        }
        log_it(L_NOTICE, "DNS server '%s' listening on %s:%u suffix=%s",
                a_dns_server->server_name, l_addr, a_ports[i], s_domain_suffix);
    }
    dap_timerfd_start(DNS_IDLE_TICK_MS, s_dns_idle_cb, a_dns_server);
    return 0;
}

static void s_reasm_reset(dns_reasm_t *a_reasm)
{
    for(size_t i = 0; i < DNS_MAX_FRAGS; ++i)
        DAP_DEL_Z(a_reasm->frags[i]);
    memset(a_reasm, 0, sizeof(*a_reasm));
}

static void s_session_free(dns_server_client_session_t *a_session)
{
    if(!a_session)
        return;
    if(a_session->handshake_key)
        dap_enc_key_delete(a_session->handshake_key);
    dns_down_frag_t *l_down = s_down(a_session);
    if(l_down) {
        for(size_t i = 0; i < a_session->down_count; ++i)
            DAP_DELETE(l_down[i].payload);
        DAP_DELETE(l_down);
    }
    if(a_session->reasm) {
        s_reasm_reset(s_reasm(a_session));
        DAP_DELETE(a_session->reasm);
    }
    if(a_session->stream) {
        a_session->stream->esocket = NULL;
        a_session->stream->trans_ctx = NULL;
        DAP_DEL_Z(a_session->stream->buf_fragments);
        DAP_DEL_Z(a_session->stream->pkt_cache);
        DAP_DEL_Z(a_session->stream->channel);
        DAP_DELETE(a_session->stream);
    }
    DAP_DEL_Z(a_session->trans_ctx);
    DAP_DELETE(a_session);
}

void dap_net_trans_dns_server_stop(dap_net_trans_dns_server_t *a_dns_server)
{
    if(!a_dns_server)
        return;
    dns_server_client_session_t *l_session, *l_tmp;
    pthread_mutex_lock(&a_dns_server->sessions_lock);
    HASH_ITER(hh, a_dns_server->sessions, l_session, l_tmp) {
        HASH_DEL(a_dns_server->sessions, l_session);
        pthread_mutex_unlock(&a_dns_server->sessions_lock);
        s_session_free(l_session);
        pthread_mutex_lock(&a_dns_server->sessions_lock);
    }
    pthread_mutex_unlock(&a_dns_server->sessions_lock);
    if(a_dns_server->server) {
        dap_server_delete_sync(a_dns_server->server);
        a_dns_server->server = NULL;
    }
}

void dap_net_trans_dns_server_delete(dap_net_trans_dns_server_t *a_dns_server)
{
    if(!a_dns_server)
        return;
    dap_net_trans_dns_server_stop(a_dns_server);
    DAP_DEL_Z(a_dns_server->trans);
    pthread_mutex_destroy(&a_dns_server->sessions_lock);
    DAP_DELETE(a_dns_server);
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

static int s_enqueue_down(dns_server_client_session_t *a_session, uint8_t a_type,
        const uint8_t *a_data, size_t a_size)
{
    size_t l_budget = s_response_budget ? s_response_budget : 64;
    size_t l_count = a_size ? (a_size + l_budget - 1) / l_budget : 1;
    if(l_count > DNS_MAX_FRAGS || a_session->down_count + l_count > DNS_MAX_DOWN)
        return -1;
    uint32_t l_id = ++a_session->next_down_id;
    size_t l_off = 0;
    dns_down_frag_t *l_down = s_down(a_session);
    if(!l_down)
        return -1;
    for(size_t i = 0; i < l_count; ++i) {
        size_t l_chunk = a_size - l_off;
        if(l_chunk > l_budget)
            l_chunk = l_budget;
        dns_down_frag_t *l_frag = &l_down[a_session->down_count++];
        memset(l_frag, 0, sizeof(*l_frag));
        l_frag->type = a_type;
        l_frag->message_id = l_id;
        l_frag->index = (uint16_t)i;
        l_frag->count = (uint16_t)l_count;
        l_frag->payload_len = (uint16_t)l_chunk;
        if(l_chunk) {
            l_frag->payload = DAP_DUP_SIZE(a_data + l_off, l_chunk);
            if(!l_frag->payload)
                return -1;
        }
        l_off += l_chunk;
    }
    return 0;
}

static dns_down_frag_t *s_next_down(dns_server_client_session_t *a_session)
{
    dns_down_frag_t *l_down = s_down(a_session);
    if(!l_down)
        return NULL;
    for(size_t i = a_session->down_pos; i < a_session->down_count; ++i) {
        if(!l_down[i].acked)
            return &l_down[i];
        a_session->down_pos = i + 1;
    }
    return NULL;
}

static bool s_addr_equal(const struct sockaddr_storage *a_left, socklen_t a_left_len,
        const struct sockaddr_storage *a_right, socklen_t a_right_len)
{
    return a_left_len == a_right_len && !memcmp(a_left, a_right, a_left_len);
}

static dns_server_client_session_t *s_session_find(dap_net_trans_dns_server_t *a_server,
        uint64_t a_cookie, uint64_t a_nonce,
        struct sockaddr_storage *a_addr, socklen_t a_addr_len)
{
    dns_server_client_session_t *l_session = NULL;
    if(a_cookie) {
        HASH_FIND(hh, a_server->sessions, &a_cookie, sizeof(a_cookie), l_session);
        if(l_session && !s_addr_equal(&l_session->remote_addr, l_session->remote_addr_len,
                a_addr, a_addr_len))
            return NULL;
        return l_session;
    }
    dns_server_client_session_t *l_cur, *l_tmp;
    HASH_ITER(hh, a_server->sessions, l_cur, l_tmp) {
        if(l_cur->nonce == a_nonce &&
                s_addr_equal(&l_cur->remote_addr, l_cur->remote_addr_len,
                        a_addr, a_addr_len))
            return l_cur;
    }
    return NULL;
}

static int s_complete_handshake(dap_events_socket_t *a_es,
        dap_net_trans_dns_server_t *a_server,
        dns_server_client_session_t *a_session,
        const uint8_t *a_data, size_t a_size)
{
    if(dap_qos_is_probe(a_data, a_size)) {
        void *l_echo = NULL;
        size_t l_echo_size = 0;
        if(dap_qos_build_echo(a_data, a_size, &l_echo, &l_echo_size) == 0) {
            s_enqueue_down(a_session, DAP_DNS_TUNNEL_MSG_HANDSHAKE, l_echo, l_echo_size);
            DAP_DELETE(l_echo);
        }
        return 0;
    }
    dap_enc_key_t *l_bob_key = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    if(!l_bob_key || !l_bob_key->gen_bob_shared_key)
        return -1;
    void *l_bob_pub = NULL;
    size_t l_shared = l_bob_key->gen_bob_shared_key(l_bob_key, a_data, a_size, &l_bob_pub);
    if(!l_bob_pub || !l_shared || !l_bob_key->shared_key) {
        dap_enc_key_delete(l_bob_key);
        return -1;
    }
    dap_enc_key_t *l_hs_key = dap_enc_kdf_create_cipher_key(l_bob_key,
            DAP_ENC_KEY_TYPE_SALSA2012, "dns_handshake", 13, 0, 32);
    if(!l_hs_key) {
        dap_enc_key_delete(l_bob_key);
        return -1;
    }
    a_session->handshake_key = l_hs_key;
    a_session->cookie = m_dap_random_u64();
    if(!a_session->cookie)
        a_session->cookie = 1;
    dap_stream_t *l_stream = DAP_NEW_Z(dap_stream_t);
    dap_net_trans_ctx_t *l_trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
    if(!l_stream || !l_trans_ctx) {
        dap_enc_key_delete(l_bob_key);
        return -1;
    }
    l_trans_ctx->trans = a_server->trans;
    l_trans_ctx->stream = l_stream;
    l_stream->esocket = a_es;
    l_stream->esocket_uuid = a_es->uuid;
    l_stream->esocket_worker = a_es->worker;
    l_stream->trans = a_server->trans;
    l_stream->trans_ctx = l_trans_ctx;
    l_stream->_server_session = a_session;
    l_stream->stream_worker = DAP_STREAM_WORKER(a_es->worker);
    dap_stream_session_t *l_stream_session = dap_stream_session_new(0, false);
    if(!l_stream_session) {
        dap_enc_key_delete(l_bob_key);
        DAP_DELETE(l_trans_ctx);
        DAP_DELETE(l_stream);
        return -1;
    }
    dap_stream_session_open(l_stream_session);
    l_stream_session->key = dap_enc_key_dup(l_hs_key);
    dap_strncpy(l_stream_session->active_channels, "RS",
            sizeof(l_stream_session->active_channels) - 1);
    l_stream->session = l_stream_session;
    const char *l_channels = "RS";
    for(size_t i = 0; i < strlen(l_channels); i++) {
        dap_stream_ch_t *l_ch = dap_stream_ch_new(l_stream, (uint8_t)l_channels[i]);
        if(l_ch)
            l_ch->ready_to_read = true;
    }
    a_session->stream = l_stream;
    a_session->trans_ctx = l_trans_ctx;
    a_session->stream_session = l_stream_session;
    a_session->handshake_complete = true;
    uint8_t *l_payload = DAP_NEW_SIZE(uint8_t, 4 + l_shared);
    if(!l_payload) {
        dap_enc_key_delete(l_bob_key);
        return -1;
    }
    uint32_t l_id = l_stream_session->id;
    l_payload[0] = (uint8_t)(l_id >> 24);
    l_payload[1] = (uint8_t)(l_id >> 16);
    l_payload[2] = (uint8_t)(l_id >> 8);
    l_payload[3] = (uint8_t)l_id;
    memcpy(l_payload + 4, l_bob_pub, l_shared);
    s_enqueue_down(a_session, DAP_DNS_TUNNEL_MSG_HANDSHAKE, l_payload, 4 + l_shared);
    DAP_DELETE(l_payload);
    dap_enc_key_delete(l_bob_key);
    log_it(L_INFO, "DNS server: handshake complete session=%u cookie=0x%"DAP_UINT64_FORMAT_x,
            l_id, a_session->cookie);
    return 0;
}

static void s_dns_process_datagram(dap_events_socket_t *a_es,
        dap_net_trans_dns_server_t *a_dns_server,
        void *a_data, size_t a_size,
        struct sockaddr_storage *a_addr, socklen_t a_addr_len)
{
    uint8_t l_payload[DAP_DNS_TUNNEL_EDNS_UDP_SIZE];
    size_t l_payload_size = sizeof(l_payload);
    uint16_t l_txid = 0;
    dap_dns_tunnel_frame_t l_frame;
    if(dap_dns_tunnel_wire_parse_query(a_data, a_size, s_domain_suffix,
            &l_txid, &l_frame, l_payload, &l_payload_size) != DAP_DNS_TUNNEL_WIRE_OK)
        return;

    pthread_mutex_lock(&a_dns_server->sessions_lock);
    dns_server_client_session_t *l_session = s_session_find(a_dns_server,
            l_frame.session_cookie, l_frame.client_nonce, a_addr, a_addr_len);
    if(!l_session) {
        unsigned l_count = HASH_COUNT(a_dns_server->sessions);
        if(l_count >= DNS_MAX_SESSIONS) {
            pthread_mutex_unlock(&a_dns_server->sessions_lock);
            return;
        }
        l_session = DAP_NEW_Z(dns_server_client_session_t);
        if(!l_session) {
            pthread_mutex_unlock(&a_dns_server->sessions_lock);
            return;
        }
        l_session->down_queue = DAP_NEW_Z_SIZE(dns_down_frag_t, DNS_MAX_DOWN);
        l_session->reasm = DAP_NEW_Z(dns_reasm_t);
        if(!l_session->down_queue || !l_session->reasm) {
            pthread_mutex_unlock(&a_dns_server->sessions_lock);
            s_session_free(l_session);
            return;
        }
        l_session->nonce = l_frame.client_nonce;
        memcpy(&l_session->remote_addr, a_addr, a_addr_len);
        l_session->remote_addr_len = a_addr_len;
        l_session->cookie = l_frame.client_nonce ? l_frame.client_nonce : 1;
        HASH_ADD(hh, a_dns_server->sessions, cookie, sizeof(l_session->cookie), l_session);
    }
    l_session->last_active = time(NULL);
    pthread_mutex_unlock(&a_dns_server->sessions_lock);

    dns_down_frag_t *l_down_list = s_down(l_session);
    for(size_t i = 0; i < l_session->down_count; ++i) {
        if(l_down_list[i].message_id == l_frame.ack_message_id &&
                l_down_list[i].index == l_frame.ack_fragment_index)
            l_down_list[i].acked = true;
    }
    if(l_frame.type == DAP_DNS_TUNNEL_MSG_HANDSHAKE ||
            l_frame.type == DAP_DNS_TUNNEL_MSG_DATA) {
        l_session->last_up_msg = l_frame.message_id;
        l_session->last_up_frag = l_frame.fragment_index;
        uint8_t *l_msg = NULL;
        size_t l_msg_size = 0;
        int l_ready = s_reasm_add(s_reasm(l_session), &l_frame, &l_msg, &l_msg_size);
        if(l_ready > 0) {
            if(l_frame.type == DAP_DNS_TUNNEL_MSG_HANDSHAKE &&
                    !l_session->handshake_complete) {
                uint64_t l_old_cookie = l_session->cookie;
                s_complete_handshake(a_es, a_dns_server, l_session, l_msg, l_msg_size);
                pthread_mutex_lock(&a_dns_server->sessions_lock);
                HASH_DEL(a_dns_server->sessions, l_session);
                HASH_ADD(hh, a_dns_server->sessions, cookie,
                        sizeof(l_session->cookie), l_session);
                pthread_mutex_unlock(&a_dns_server->sessions_lock);
                UNUSED(l_old_cookie);
            } else if(l_frame.type == DAP_DNS_TUNNEL_MSG_DATA && l_session->stream &&
                    l_msg_size) {
                dap_stream_data_proc_read_ext(l_session->stream, l_msg, l_msg_size);
            }
            DAP_DELETE(l_msg);
        }
    }

    dns_down_frag_t *l_down = s_next_down(l_session);
    dap_dns_tunnel_frame_t l_reply = {
        .type = l_down ? l_down->type : DAP_DNS_TUNNEL_MSG_POLL,
        .client_nonce = l_session->nonce,
        .session_cookie = l_session->handshake_complete ? l_session->cookie : 0,
        .message_id = l_down ? l_down->message_id : 0,
        .fragment_index = l_down ? l_down->index : 0,
        .fragment_count = l_down ? l_down->count : 1,
        .ack_message_id = l_session->last_up_msg,
        .ack_fragment_index = l_session->last_up_frag,
        .payload_length = l_down ? l_down->payload_len : 0,
        .payload = l_down ? l_down->payload : NULL
    };
    uint8_t l_response[DAP_DNS_TUNNEL_EDNS_UDP_SIZE];
    size_t l_response_size = sizeof(l_response);
    if(dap_dns_tunnel_wire_build_response(a_data, a_size, s_domain_suffix,
            &l_reply, l_response, &l_response_size) == DAP_DNS_TUNNEL_WIRE_OK)
        dap_events_socket_sendto_unsafe(a_es, l_response, l_response_size,
                a_addr, a_addr_len);
}

static void s_dns_listener_read_cb(dap_events_socket_t *a_es, void *a_arg)
{
    UNUSED(a_arg);
    if(!a_es || a_es->buf_in_size == 0)
        return;
    dap_server_t *l_server = a_es->server;
    if(!l_server || !l_server->_inheritor) {
        a_es->buf_in_size = 0;
        return;
    }
    dap_net_trans_dns_server_t *l_dns_server = DAP_NET_TRANS_DNS_SERVER(l_server);
    struct sockaddr_storage l_remote_addr;
    socklen_t l_remote_addr_len = a_es->addr_size;
    memcpy(&l_remote_addr, &a_es->addr_storage, l_remote_addr_len);
    s_dns_process_datagram(a_es, l_dns_server, a_es->buf_in, a_es->buf_in_size,
            &l_remote_addr, l_remote_addr_len);
    a_es->buf_in_size = 0;

    typedef struct {
        byte_t data[2048];
        size_t size;
        struct sockaddr_storage addr;
        socklen_t addr_len;
    } s_dns_drain_pkt_t;
    s_dns_drain_pkt_t *l_queue = NULL;
    size_t l_q_size = 0;
    size_t l_q_cap = 0;
    for(;;) {
        if(l_q_size >= l_q_cap) {
            size_t l_new_cap = l_q_cap ? l_q_cap * 2 : 128;
            s_dns_drain_pkt_t *l_tmp = DAP_REALLOC(l_queue,
                    l_new_cap * sizeof(s_dns_drain_pkt_t));
            if(!l_tmp)
                break;
            l_queue = l_tmp;
            l_q_cap = l_new_cap;
        }
        s_dns_drain_pkt_t *l_slot = &l_queue[l_q_size];
        l_slot->addr_len = sizeof(l_slot->addr);
        ssize_t l_n = recvfrom(a_es->fd, l_slot->data, sizeof(l_slot->data),
                MSG_DONTWAIT, (struct sockaddr *)&l_slot->addr, &l_slot->addr_len);
        if(l_n <= 0)
            break;
        l_slot->size = (size_t)l_n;
        l_q_size++;
    }
    for(size_t i = 0; i < l_q_size; i++) {
        s_dns_drain_pkt_t *l_slot = &l_queue[i];
        s_dns_process_datagram(a_es, l_dns_server, l_slot->data, l_slot->size,
                &l_slot->addr, l_slot->addr_len);
    }
    DAP_DELETE(l_queue);
}

static bool s_dns_idle_cb(void *a_arg)
{
    dap_net_trans_dns_server_t *l_server = (dap_net_trans_dns_server_t *)a_arg;
    if(!l_server || !l_server->server)
        return false;
    time_t l_now = time(NULL);
    pthread_mutex_lock(&l_server->sessions_lock);
    dns_server_client_session_t *l_session, *l_tmp;
    HASH_ITER(hh, l_server->sessions, l_session, l_tmp) {
        if(l_now - l_session->last_active < DNS_IDLE_TTL_SEC)
            continue;
        HASH_DEL(l_server->sessions, l_session);
        pthread_mutex_unlock(&l_server->sessions_lock);
        s_session_free(l_session);
        pthread_mutex_lock(&l_server->sessions_lock);
    }
    pthread_mutex_unlock(&l_server->sessions_lock);
    return true;
}

static ssize_t s_dns_server_trans_write(dap_stream_t *a_stream, const void *a_data,
        size_t a_size)
{
    if(!a_stream || !a_data || a_size == 0)
        return -1;
    dns_server_client_session_t *l_session =
            (dns_server_client_session_t *)a_stream->_server_session;
    if(!l_session)
        return 0;
    if(s_enqueue_down(l_session, DAP_DNS_TUNNEL_MSG_DATA, a_data, a_size) != 0)
        return -1;
    return (ssize_t)a_size;
}
