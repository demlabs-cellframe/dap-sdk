/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.

This file is part of DAP the open source project.

DAP is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DAP is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

See more details here <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_net_trans.h"
#include "dap_net_trans_dns_server.h"
#include "dap_net_trans_dns_stream.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_session.h"
#include "dap_stream_worker.h"
#include "dap_net_trans_server.h"
#include "dap_events_socket.h"
#include "dap_enc_key.h"
#include "dap_net_trans_qos.h"
#include "dap_enc_kdf.h"

#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define LOG_TAG "dap_net_trans_dns_server"

static bool s_debug_more = false;
static void s_dns_listener_read_cb(dap_events_socket_t *a_es, void *a_arg);
static ssize_t s_dns_server_trans_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);

static const dap_net_trans_ops_t s_dns_server_trans_ops = {
    .write = s_dns_server_trans_write
};

static void* s_dns_server_new(const char *a_server_name)
{
    return (void*)dap_net_trans_dns_server_new(a_server_name);
}

static int s_dns_server_start(void *a_server, const char *a_cfg_section, 
                              const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    dap_net_trans_dns_server_t *l_dns = (dap_net_trans_dns_server_t *)a_server;
    return dap_net_trans_dns_server_start(l_dns, a_cfg_section, a_addrs, a_ports, a_count);
}

static void s_dns_server_stop(void *a_server)
{
    dap_net_trans_dns_server_t *l_dns = (dap_net_trans_dns_server_t *)a_server;
    dap_net_trans_dns_server_stop(l_dns);
}

static void s_dns_server_delete(void *a_server)
{
    dap_net_trans_dns_server_t *l_dns = (dap_net_trans_dns_server_t *)a_server;
    dap_net_trans_dns_server_delete(l_dns);
}

static const dap_net_trans_server_ops_t s_dns_server_ops = {
    .new = s_dns_server_new,
    .start = s_dns_server_start,
    .stop = s_dns_server_stop,
    .delete = s_dns_server_delete
};

int dap_net_trans_dns_server_init(void)
{
    int l_ret = dap_net_trans_server_register_ops(DAP_NET_TRANS_DNS_TUNNEL, &s_dns_server_ops);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register DNS trans server operations");
        return l_ret;
    }
    
    log_it(L_NOTICE, "Initialized DNS server module");
    return 0;
}

void dap_net_trans_dns_server_deinit(void)
{
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_DNS_TUNNEL);
    log_it(L_INFO, "DNS server module deinitialized");
}

dap_net_trans_dns_server_t *dap_net_trans_dns_server_new(const char *a_server_name)
{
    if (!a_server_name) {
        log_it(L_ERROR, "Server name is NULL");
        return NULL;
    }

    dap_net_trans_dns_server_t *l_dns_server = DAP_NEW_Z(dap_net_trans_dns_server_t);
    if (!l_dns_server) {
        log_it(L_CRITICAL, "Cannot allocate memory for DNS server");
        return NULL;
    }

    dap_strncpy(l_dns_server->server_name, a_server_name, sizeof(l_dns_server->server_name) - 1);
    l_dns_server->sessions = NULL;
    
    l_dns_server->trans = DAP_NEW_Z(dap_net_trans_t);
    if (!l_dns_server->trans) {
        log_it(L_CRITICAL, "Cannot allocate DNS server trans");
        DAP_DELETE(l_dns_server);
        return NULL;
    }
    l_dns_server->trans->type = DAP_NET_TRANS_DNS_TUNNEL;
    l_dns_server->trans->ops = &s_dns_server_trans_ops;
    l_dns_server->trans->socket_type = DAP_NET_TRANS_SOCKET_UDP;

    log_it(L_INFO, "Created DNS server: %s", a_server_name);
    return l_dns_server;
}

int dap_net_trans_dns_server_start(dap_net_trans_dns_server_t *a_dns_server,
                                       const char *a_cfg_section,
                                       const char **a_addrs,
                                       uint16_t *a_ports,
                                       size_t a_count)
{
    if (!a_dns_server || !a_ports || a_count == 0) {
        log_it(L_ERROR, "Invalid parameters for DNS server start");
        return -1;
    }

    if (a_dns_server->server) {
        log_it(L_WARNING, "DNS server already started");
        return -2;
    }

    dap_events_socket_callbacks_t l_dns_callbacks = {
        .read_callback = s_dns_listener_read_cb,
        .write_callback = NULL,
        .delete_callback = NULL,
        .new_callback = NULL,
        .error_callback = NULL
    };

    a_dns_server->server = dap_server_new(a_cfg_section, NULL, &l_dns_callbacks);
    if (!a_dns_server->server) {
        log_it(L_ERROR, "Failed to create dap_server for DNS");
        return -3;
    }

    a_dns_server->server->_inheritor = a_dns_server;

    debug_if(s_debug_more, L_DEBUG, "Registered DNS stream handlers");

    for (size_t i = 0; i < a_count; i++) {
        const char *l_addr = (a_addrs && a_addrs[i]) ? a_addrs[i] : "0.0.0.0";
        uint16_t l_port = a_ports[i];

        int l_ret = dap_server_listen_addr_add(a_dns_server->server, l_addr, l_port,
                                                DESCRIPTOR_TYPE_SOCKET_UDP,
                                                &a_dns_server->server->client_callbacks);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to start DNS server on %s:%u", l_addr, l_port);
            dap_net_trans_dns_server_stop(a_dns_server);
            return -4;
        }

        log_it(L_NOTICE, "DNS server '%s' listening on %s:%u",
               a_dns_server->server_name, l_addr, l_port);
    }

    return 0;
}

void dap_net_trans_dns_server_stop(dap_net_trans_dns_server_t *a_dns_server)
{
    if (!a_dns_server)
        return;

    if (a_dns_server->server) {
        dap_server_delete(a_dns_server->server);
        a_dns_server->server = NULL;
    }

    dns_server_client_session_t *l_session, *l_tmp;
    HASH_ITER(hh, a_dns_server->sessions, l_session, l_tmp) {
        HASH_DEL(a_dns_server->sessions, l_session);
        if (l_session->handshake_key)
            dap_enc_key_delete(l_session->handshake_key);
        DAP_DEL_Z(l_session->trans_ctx);
        DAP_DEL_Z(l_session->stream);
        DAP_DELETE(l_session);
    }

    log_it(L_INFO, "DNS server '%s' stopped", a_dns_server->server_name);
}

void dap_net_trans_dns_server_delete(dap_net_trans_dns_server_t *a_dns_server)
{
    if (!a_dns_server)
        return;
    dap_net_trans_dns_server_stop(a_dns_server);
    DAP_DEL_Z(a_dns_server->trans);
    log_it(L_INFO, "Deleted DNS server: %s", a_dns_server->server_name);
    DAP_DELETE(a_dns_server);
}

/**
 * @brief DNS server listener read callback
 *
 * Receives raw UDP packets, routes by remote address, processes KEM handshake.
 * For new clients: do KEM encapsulation, send bob_ciphertext back.
 */
static void s_dns_process_datagram(dap_events_socket_t *a_es, dap_net_trans_dns_server_t *a_dns_server,
                                   void *a_data, size_t a_size,
                                   struct sockaddr_storage *a_addr, socklen_t a_addr_len);

static void s_dns_listener_read_cb(dap_events_socket_t *a_es, void *a_arg)
{
    (void)a_arg;
    
    if (!a_es || a_es->buf_in_size == 0)
        return;

    dap_server_t *l_server = a_es->server;
    if (!l_server || !l_server->_inheritor) {
        log_it(L_ERROR, "DNS listener has no server context");
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

    /* Drain remaining datagrams from kernel buffer to avoid multiple event loop iterations */
    byte_t l_buf[65536];
    struct sockaddr_storage l_addr;
    for (int i = 0; i < 256; i++) {
        socklen_t l_addr_len = sizeof(l_addr);
        ssize_t l_read = recvfrom(a_es->fd, l_buf, sizeof(l_buf), MSG_DONTWAIT,
                                  (struct sockaddr *)&l_addr, &l_addr_len);
        if (l_read <= 0)
            break;
        s_dns_process_datagram(a_es, l_dns_server, l_buf, (size_t)l_read, &l_addr, l_addr_len);
    }
}

static void s_dns_process_datagram(dap_events_socket_t *a_es, dap_net_trans_dns_server_t *a_dns_server,
                                   void *a_data, size_t a_size,
                                   struct sockaddr_storage *a_addr, socklen_t a_addr_len)
{
    dns_server_client_session_t *l_session = NULL;
    HASH_FIND(hh, a_dns_server->sessions, a_addr, a_addr_len, l_session);

    if (l_session) {
        if (l_session->stream) {
            dap_stream_data_proc_read_ext(l_session->stream, a_data, a_size);
        }
        return;
    }

    log_it(L_INFO, "DNS server: new client handshake, size=%zu", a_size);

    if (dap_qos_is_probe(a_data, a_size)) {
        debug_if(s_debug_more, L_DEBUG, "DNS server: QoS probe detected (%zu bytes)", a_size);
        void  *l_echo = NULL;
        size_t l_echo_size = 0;
        if (dap_qos_build_echo(a_data, a_size, &l_echo, &l_echo_size) == 0) {
            dap_events_socket_sendto_unsafe(a_es, l_echo, l_echo_size, a_addr, a_addr_len);
            DAP_DELETE(l_echo);
        }
        return;
    }

    /* KEM encapsulation: generate bob key, derive shared secret */
    dap_enc_key_t *l_bob_key = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    if (!l_bob_key) {
        log_it(L_ERROR, "DNS server: failed to generate Bob KEM key");
        return;
    }

    void *l_bob_pub = NULL;
    size_t l_shared_key_size = 0;

    if (!l_bob_key->gen_bob_shared_key) {
        log_it(L_ERROR, "DNS server: key type doesn't support KEM");
        dap_enc_key_delete(l_bob_key);
        return;
    }

    l_shared_key_size = l_bob_key->gen_bob_shared_key(
        l_bob_key, a_data, a_size, &l_bob_pub);

    if (!l_bob_pub || l_shared_key_size == 0 || !l_bob_key->shared_key) {
        log_it(L_ERROR, "DNS server: KEM encapsulation failed");
        dap_enc_key_delete(l_bob_key);
        return;
    }

    log_it(L_INFO, "DNS server: KEM done, ciphertext=%zu bytes", l_shared_key_size);

    dap_enc_key_t *l_handshake_key = dap_enc_kdf_create_cipher_key(
        l_bob_key,
        DAP_ENC_KEY_TYPE_SALSA2012,
        "dns_handshake", 13,
        0, 32);

    if (!l_handshake_key) {
        log_it(L_ERROR, "DNS server: failed to derive handshake key");
        dap_enc_key_delete(l_bob_key);
        return;
    }

    /* Send bob_ciphertext back to client */
    size_t l_sent = dap_events_socket_sendto_unsafe(
        a_es, l_bob_pub, l_shared_key_size,
        a_addr, a_addr_len);

    if (l_sent != l_shared_key_size) {
        log_it(L_ERROR, "DNS server: failed to send handshake response: %zu of %zu",
               l_sent, l_shared_key_size);
        dap_enc_key_delete(l_bob_key);
        dap_enc_key_delete(l_handshake_key);
        return;
    }

    log_it(L_INFO, "DNS server: sent handshake response (%zu bytes)", l_sent);

    /* Create session for this client */
    l_session = DAP_NEW_Z(dns_server_client_session_t);
    if (!l_session) {
        dap_enc_key_delete(l_bob_key);
        dap_enc_key_delete(l_handshake_key);
        return;
    }

    memcpy(&l_session->remote_addr, a_addr, a_addr_len);
    l_session->remote_addr_len = a_addr_len;
    l_session->handshake_key = l_handshake_key;

    /* Create server-side stream for bidirectional data exchange */
    dap_stream_t *l_stream = DAP_NEW_Z(dap_stream_t);
    if (!l_stream) {
        log_it(L_ERROR, "DNS server: failed to allocate stream");
        dap_enc_key_delete(l_bob_key);
        DAP_DELETE(l_session);
        return;
    }

    dap_net_trans_ctx_t *l_trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
    if (!l_trans_ctx) {
        log_it(L_ERROR, "DNS server: failed to allocate trans_ctx");
        dap_enc_key_delete(l_bob_key);
        DAP_DELETE(l_stream);
        DAP_DELETE(l_session);
        return;
    }
    l_trans_ctx->esocket = a_es;
    l_trans_ctx->esocket_worker = a_es->worker;
    l_trans_ctx->trans = a_dns_server->trans;
    l_trans_ctx->stream = l_stream;
    l_stream->trans = a_dns_server->trans;
    l_stream->trans_ctx = l_trans_ctx;
    l_stream->_server_session = l_session;
    l_stream->stream_worker = DAP_STREAM_WORKER(a_es->worker);

    dap_stream_session_t *l_stream_session = dap_stream_session_new(0, false);
    if (!l_stream_session) {
        log_it(L_ERROR, "DNS server: failed to create stream session");
        dap_enc_key_delete(l_bob_key);
        DAP_DELETE(l_trans_ctx);
        DAP_DELETE(l_stream);
        DAP_DELETE(l_session);
        return;
    }
    dap_stream_session_open(l_stream_session);
    l_stream_session->key = dap_enc_key_dup(l_handshake_key);
    dap_strncpy(l_stream_session->active_channels, "ABC",
                sizeof(l_stream_session->active_channels) - 1);
    l_stream->session = l_stream_session;

    const char *l_channels = "ABC";
    for (size_t i = 0; i < strlen(l_channels); i++) {
        dap_stream_ch_t *l_ch = dap_stream_ch_new(l_stream, (uint8_t)l_channels[i]);
        if (l_ch) {
            l_ch->ready_to_read = true;
        }
    }

    l_session->stream = l_stream;
    l_session->trans_ctx = l_trans_ctx;
    l_session->stream_session = l_stream_session;

    HASH_ADD(hh, a_dns_server->sessions, remote_addr, a_addr_len, l_session);

    log_it(L_INFO, "DNS server: created stream %p with %zu channels for new client",
           l_stream, l_stream->channel_count);

    dap_enc_key_delete(l_bob_key);
}

/**
 * @brief Server-side write: send data back to DNS client via sendto_unsafe
 */
static ssize_t s_dns_server_trans_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0)
        return -1;

    dns_server_client_session_t *l_session =
        (dns_server_client_session_t *)a_stream->_server_session;
    if (!l_session || !a_stream->trans_ctx || !a_stream->trans_ctx->esocket) {
        log_it(L_ERROR, "DNS server write: no session or esocket");
        return -1;
    }

    size_t l_sent = dap_events_socket_sendto_unsafe(
        a_stream->trans_ctx->esocket,
        a_data, a_size,
        &l_session->remote_addr,
        l_session->remote_addr_len);

    if (l_sent != a_size) {
        log_it(L_WARNING, "DNS server write incomplete: %zu of %zu bytes", l_sent, a_size);
    }

    return (ssize_t)l_sent;
}
