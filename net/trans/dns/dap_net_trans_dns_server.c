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
#include <unistd.h>
#ifndef DAP_OS_WINDOWS
#include <errno.h>
#endif
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
#include "dap_worker.h"
#include "dap_enc_key.h"
#include "dap_net_trans_qos.h"
#include "dap_enc_kdf.h"
#include "dap_io_flow_datagram.h"
#include "dap_context.h"

#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define LOG_TAG "dap_net_trans_dns_server"
#define DNS_STOP_READ_DRAIN_RETRIES 500
#define DNS_STOP_READ_DRAIN_SLEEP_US 1000

static bool s_debug_more = false;
static void s_dns_listener_read_cb(dap_events_socket_t *a_es, void *a_arg);
static ssize_t s_dns_server_trans_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static bool s_dns_server_get_remote_addr_cb(dap_io_flow_datagram_t *a_flow,
                                            struct sockaddr_storage *a_addr_out,
                                            socklen_t *a_addr_len_out);
static bool s_dns_server_stop_internal(dap_net_trans_dns_server_t *a_dns_server);
static bool s_dns_server_wait_reads_drain(dap_net_trans_dns_server_t *a_dns_server, size_t a_retries);

static bool s_dns_server_is_stopping(const dap_net_trans_dns_server_t *a_dns_server)
{
    return a_dns_server && atomic_load(&a_dns_server->stopping);
}

static void s_dns_server_session_delete(dns_server_client_session_t *a_session)
{
    if (!a_session)
        return;

    if (a_session->handshake_key)
        dap_enc_key_delete(a_session->handshake_key);
    if (a_session->stream) {
        if (a_session->stream->flow) {
            dap_io_flow_datagram_delete(a_session->stream->flow);
            a_session->stream->flow = NULL;
        }
        a_session->stream->trans_ctx = NULL;
        DAP_DEL_Z(a_session->stream->buf_fragments);
        DAP_DEL_Z(a_session->stream->buf_fragments_map);
        DAP_DEL_Z(a_session->stream->pkt_cache);
        DAP_DEL_Z(a_session->stream->channel);
    }
    DAP_DEL_Z(a_session->trans_ctx);
    DAP_DEL_Z(a_session->stream);
    DAP_DELETE(a_session);
}

static void s_dns_server_sessions_cleanup_unsafe(dap_net_trans_dns_server_t *a_dns_server)
{
    if (!a_dns_server)
        return;

    dns_server_client_session_t *l_session = NULL, *l_tmp = NULL;
    HASH_ITER(hh, a_dns_server->sessions, l_session, l_tmp) {
        HASH_DEL(a_dns_server->sessions, l_session);
        s_dns_server_session_delete(l_session);
    }
}

static bool s_dns_server_wait_reads_drain(dap_net_trans_dns_server_t *a_dns_server, size_t a_retries)
{
    if (!a_dns_server)
        return true;

    for (size_t i = 0; i < a_retries; i++) {
        if (atomic_load(&a_dns_server->datagram_reads_inflight) == 0)
            return true;
        usleep(DNS_STOP_READ_DRAIN_SLEEP_US);
    }
    return atomic_load(&a_dns_server->datagram_reads_inflight) == 0;
}

static size_t s_dns_server_get_max_packet_size(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return 1200;
}

static const dap_net_trans_ops_t s_dns_server_trans_ops = {
    .write = s_dns_server_trans_write,
    .get_max_packet_size = s_dns_server_get_max_packet_size
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

    int l_lock_ret = pthread_rwlock_init(&l_dns_server->sessions_lock, NULL);
    if (l_lock_ret != 0) {
        log_it(L_CRITICAL, "Cannot initialize DNS server sessions lock: %d (%s)",
               l_lock_ret, dap_strerror(l_lock_ret));
        DAP_DELETE(l_dns_server);
        return NULL;
    }
    atomic_store(&l_dns_server->stopping, false);
    atomic_store(&l_dns_server->datagram_reads_inflight, 0);

    dap_strncpy(l_dns_server->server_name, a_server_name, sizeof(l_dns_server->server_name) - 1);
    l_dns_server->sessions = NULL;
    
    l_dns_server->trans = DAP_NEW_Z(dap_net_trans_t);
    if (!l_dns_server->trans) {
        log_it(L_CRITICAL, "Cannot allocate DNS server trans");
        pthread_rwlock_destroy(&l_dns_server->sessions_lock);
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

    // If previous stop timed out, finish pending reads and purge stale sessions
    // before opening listener sockets again.
    if (!s_dns_server_wait_reads_drain(a_dns_server, DNS_STOP_READ_DRAIN_RETRIES)) {
        unsigned int l_reads_left = atomic_load(&a_dns_server->datagram_reads_inflight);
        log_it(L_ERROR, "DNS server start aborted: %u datagram read(s) still in progress",
               l_reads_left);
        return -3;
    }

    int l_lock_ret = pthread_rwlock_wrlock(&a_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_ERROR, "Failed to lock DNS sessions for start: %d (%s)",
               l_lock_ret, dap_strerror(l_lock_ret));
        return -4;
    }
    if (a_dns_server->sessions) {
        log_it(L_WARNING, "DNS server '%s': removing stale sessions before start", a_dns_server->server_name);
        s_dns_server_sessions_cleanup_unsafe(a_dns_server);
    }
    pthread_rwlock_unlock(&a_dns_server->sessions_lock);

    atomic_store(&a_dns_server->stopping, false);

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
        return -5;
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
            return -6;
        }

        log_it(L_NOTICE, "DNS server '%s' listening on %s:%u",
               a_dns_server->server_name, l_addr, l_port);
    }

    return 0;
}

static bool s_dns_server_stop_internal(dap_net_trans_dns_server_t *a_dns_server)
{
    if (!a_dns_server)
        return true;

    dns_server_client_session_t *l_session, *l_tmp;
    int l_lock_ret = pthread_rwlock_wrlock(&a_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_ERROR, "Failed to lock DNS sessions for stop: %d (%s)",
               l_lock_ret, dap_strerror(l_lock_ret));
        return false;
    }

    atomic_store(&a_dns_server->stopping, true);
    HASH_ITER(hh, a_dns_server->sessions, l_session, l_tmp) {
        if (l_session->stream) {
            l_session->stream->esocket = NULL;
            l_session->stream->esocket_uuid = 0;
            l_session->stream->esocket_worker = NULL;
            l_session->stream->trans_ctx = NULL;
        }
    }
    pthread_rwlock_unlock(&a_dns_server->sessions_lock);

    if (a_dns_server->server) {
        dap_server_delete_sync(a_dns_server->server);
        a_dns_server->server = NULL;
    }

    if (!s_dns_server_wait_reads_drain(a_dns_server, DNS_STOP_READ_DRAIN_RETRIES)) {
        unsigned int l_reads_left = atomic_load(&a_dns_server->datagram_reads_inflight);
        log_it(L_ERROR, "DNS server stop deferred: %u datagram read(s) still in progress",
               l_reads_left);
        return false;
    }

    l_lock_ret = pthread_rwlock_wrlock(&a_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_ERROR, "Failed to lock DNS sessions for cleanup: %d (%s)",
               l_lock_ret, dap_strerror(l_lock_ret));
        return false;
    }
    s_dns_server_sessions_cleanup_unsafe(a_dns_server);
    pthread_rwlock_unlock(&a_dns_server->sessions_lock);

    log_it(L_INFO, "DNS server '%s' stopped", a_dns_server->server_name);
    return true;
}

void dap_net_trans_dns_server_stop(dap_net_trans_dns_server_t *a_dns_server)
{
    if (!s_dns_server_stop_internal(a_dns_server) && a_dns_server) {
        log_it(L_WARNING, "DNS server '%s' stop did not fully complete", a_dns_server->server_name);
    }
}

void dap_net_trans_dns_server_delete(dap_net_trans_dns_server_t *a_dns_server)
{
    if (!a_dns_server)
        return;

    bool l_stopped = s_dns_server_stop_internal(a_dns_server);
    if (!l_stopped) {
        log_it(L_WARNING, "DNS server '%s': stop incomplete during delete, forcing cleanup",
               a_dns_server->server_name);
    }

    // Delete is a terminal path: guarantee no new reads and close listener if stop failed early.
    atomic_store(&a_dns_server->stopping, true);
    if (a_dns_server->server) {
        dap_server_delete_sync(a_dns_server->server);
        a_dns_server->server = NULL;
    }

    // Wait in bounded rounds until all in-flight datagram reads complete.
    while (!s_dns_server_wait_reads_drain(a_dns_server, DNS_STOP_READ_DRAIN_RETRIES)) {
        unsigned int l_reads_left = atomic_load(&a_dns_server->datagram_reads_inflight);
        log_it(L_WARNING, "DNS server '%s' delete wait: %u datagram read(s) still in progress",
               a_dns_server->server_name, l_reads_left);
    }

    int l_lock_ret = pthread_rwlock_wrlock(&a_dns_server->sessions_lock);
    bool l_have_sessions_lock = (l_lock_ret == 0 || l_lock_ret == EDEADLK);
    if (!l_have_sessions_lock) {
        log_it(L_ERROR, "Delete fallback lock failed for DNS server '%s': %d (%s). Cleaning sessions without lock",
               a_dns_server->server_name, l_lock_ret, dap_strerror(l_lock_ret));
    }
    s_dns_server_sessions_cleanup_unsafe(a_dns_server);
    if (l_lock_ret == 0)
        pthread_rwlock_unlock(&a_dns_server->sessions_lock);

    // Best-effort destroy. Even if this fails, delete object to avoid orphan/leak.
    l_lock_ret = pthread_rwlock_destroy(&a_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_WARNING, "Deleting DNS server '%s' with non-destroyed sessions lock: %d (%s)",
               a_dns_server->server_name, l_lock_ret, dap_strerror(l_lock_ret));
    }

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
    if (s_dns_server_is_stopping(l_dns_server)) {
        a_es->buf_in_size = 0;
        return;
    }
    
    struct sockaddr_storage l_remote_addr;
    socklen_t l_remote_addr_len = a_es->addr_size;
    memcpy(&l_remote_addr, &a_es->addr_storage, l_remote_addr_len);

    s_dns_process_datagram(a_es, l_dns_server, a_es->buf_in, a_es->buf_in_size,
                           &l_remote_addr, l_remote_addr_len);
    a_es->buf_in_size = 0;

    /* EPOLLET listener: drain socket until EAGAIN/EWOULDBLOCK to avoid dropped wakeups. */
    byte_t l_buf[65536];
    struct sockaddr_storage l_addr;
    for (;;) {
        socklen_t l_addr_len = sizeof(l_addr);
        ssize_t l_read = recvfrom(a_es->fd, l_buf, sizeof(l_buf), MSG_DONTWAIT,
                                  (struct sockaddr *)&l_addr, &l_addr_len);
        if (l_read <= 0) {
#ifdef DAP_OS_WINDOWS
            int l_err = WSAGetLastError();
            if (l_read < 0 && l_err == WSAEINTR)
                continue;
            if (l_read < 0 && l_err == WSAEWOULDBLOCK)
                break;
#else
            if (l_read < 0 && errno == EINTR)
                continue;
            if (l_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
#endif
            break;
        }
        s_dns_process_datagram(a_es, l_dns_server, l_buf, (size_t)l_read, &l_addr, l_addr_len);
    }
}

static void s_dns_process_datagram(dap_events_socket_t *a_es, dap_net_trans_dns_server_t *a_dns_server,
                                   void *a_data, size_t a_size,
                                   struct sockaddr_storage *a_addr, socklen_t a_addr_len)
{
    int l_lock_ret = pthread_rwlock_rdlock(&a_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_ERROR, "DNS server: failed to lock sessions for read: %d (%s)",
               l_lock_ret, dap_strerror(l_lock_ret));
        return;
    }
    if (s_dns_server_is_stopping(a_dns_server)) {
        pthread_rwlock_unlock(&a_dns_server->sessions_lock);
        return;
    }

    dns_server_client_session_t *l_session = NULL;
    HASH_FIND(hh, a_dns_server->sessions, a_addr, (unsigned)a_addr_len, l_session);

    dap_stream_t *l_existing_stream = l_session ? l_session->stream : NULL;
    if (l_existing_stream) {
        atomic_fetch_add(&a_dns_server->datagram_reads_inflight, 1);
    }

    if (l_session) {
        pthread_rwlock_unlock(&a_dns_server->sessions_lock);
        if (l_existing_stream) {
            dap_stream_data_proc_read_ext(l_existing_stream, a_data, a_size);
            atomic_fetch_sub(&a_dns_server->datagram_reads_inflight, 1);
        }
        return;
    }
    pthread_rwlock_unlock(&a_dns_server->sessions_lock);

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
    l_session->server = a_dns_server;

    /* Create server-side stream for bidirectional data exchange */
    dap_stream_t *l_stream = DAP_NEW_Z(dap_stream_t);
    if (!l_stream) {
        log_it(L_ERROR, "DNS server: failed to allocate stream");
        dap_enc_key_delete(l_bob_key);
        dap_enc_key_delete(l_session->handshake_key);
        l_session->handshake_key = NULL;
        DAP_DELETE(l_session);
        return;
    }

    dap_net_trans_ctx_t *l_trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
    if (!l_trans_ctx) {
        log_it(L_ERROR, "DNS server: failed to allocate trans_ctx");
        dap_enc_key_delete(l_bob_key);
        dap_enc_key_delete(l_session->handshake_key);
        l_session->handshake_key = NULL;
        DAP_DELETE(l_stream);
        DAP_DELETE(l_session);
        return;
    }
    l_trans_ctx->trans = a_dns_server->trans;
    l_trans_ctx->stream = l_stream;
    l_stream->esocket = a_es;
    l_stream->esocket_uuid = a_es->uuid;
    l_stream->esocket_worker = a_es->worker;
    l_stream->trans = a_dns_server->trans;
    l_stream->trans_ctx = l_trans_ctx;
    l_stream->_server_session = l_session;
    l_stream->stream_worker = DAP_STREAM_WORKER(a_es->worker);
    l_stream->flow = dap_io_flow_datagram_new(s_dns_server_get_remote_addr_cb, l_session);
    if (!l_stream->flow) {
        log_it(L_ERROR, "DNS server: failed to create datagram flow");
        dap_enc_key_delete(l_bob_key);
        dap_enc_key_delete(l_session->handshake_key);
        l_session->handshake_key = NULL;
        DAP_DELETE(l_trans_ctx);
        DAP_DELETE(l_stream);
        DAP_DELETE(l_session);
        return;
    }

    dap_stream_session_t *l_stream_session = dap_stream_session_new(0, false);
    if (!l_stream_session) {
        log_it(L_ERROR, "DNS server: failed to create stream session");
        dap_enc_key_delete(l_bob_key);
        dap_enc_key_delete(l_session->handshake_key);
        l_session->handshake_key = NULL;
        dap_io_flow_datagram_delete(l_stream->flow);
        l_stream->flow = NULL;
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

    l_lock_ret = pthread_rwlock_wrlock(&a_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_ERROR, "DNS server: failed to lock sessions for add: %d (%s)",
               l_lock_ret, dap_strerror(l_lock_ret));
        dap_enc_key_delete(l_bob_key);
        s_dns_server_session_delete(l_session);
        return;
    }

    if (s_dns_server_is_stopping(a_dns_server)) {
        pthread_rwlock_unlock(&a_dns_server->sessions_lock);
        dap_enc_key_delete(l_bob_key);
        s_dns_server_session_delete(l_session);
        return;
    }

    dns_server_client_session_t *l_existing_session = NULL;
    HASH_FIND(hh, a_dns_server->sessions, a_addr, (unsigned)a_addr_len, l_existing_session);
    if (l_existing_session) {
        pthread_rwlock_unlock(&a_dns_server->sessions_lock);
        dap_enc_key_delete(l_bob_key);
        s_dns_server_session_delete(l_session);
        return;
    }

    HASH_ADD(hh, a_dns_server->sessions, remote_addr, (unsigned)a_addr_len, l_session);
    pthread_rwlock_unlock(&a_dns_server->sessions_lock);

    log_it(L_INFO, "DNS server: created stream %p with %zu channels for new client",
           l_stream, l_stream->channel_count);

    dap_enc_key_delete(l_bob_key);
}

static bool s_dns_server_get_remote_addr_cb(dap_io_flow_datagram_t *a_flow,
                                             struct sockaddr_storage *a_addr_out,
                                             socklen_t *a_addr_len_out)
{
    if (!a_flow || !a_addr_out || !a_addr_len_out) {
        return false;
    }

    dns_server_client_session_t *l_session = (dns_server_client_session_t *)a_flow->protocol_data;
    if (!l_session || l_session->remote_addr_len == 0) {
        return false;
    }

    memcpy(a_addr_out, &l_session->remote_addr, sizeof(struct sockaddr_storage));
    *a_addr_len_out = l_session->remote_addr_len;
    return true;
}

typedef struct dns_sendto_args {
    dap_worker_t *worker;
    dap_events_socket_uuid_t esocket_uuid;
    void *data;
    size_t size;
    struct sockaddr_storage addr;
    socklen_t addr_len;
} dns_sendto_args_t;

static void s_dns_sendto_callback(void *a_arg)
{
    dns_sendto_args_t *l_args = (dns_sendto_args_t *)a_arg;
    if (!l_args)
        return;

    dap_events_socket_t *l_es = l_args->worker && l_args->esocket_uuid
        ? dap_context_find(l_args->worker->context, l_args->esocket_uuid)
        : NULL;
    if (!l_es || l_es->fd < 0)
        goto cleanup;

    size_t l_sent = dap_events_socket_sendto_unsafe(l_es,
        l_args->data, l_args->size,
        &l_args->addr, l_args->addr_len);
    if (l_sent != l_args->size) {
        log_it(L_WARNING, "DNS async write incomplete: %zu of %zu bytes", l_sent, l_args->size);
    }

cleanup:
    DAP_DELETE(l_args->data);
    DAP_DELETE(l_args);
}

/**
 * @brief Server-side write: send data back to DNS client
 * @note Worker-aware: if called from a different worker thread,
 *       marshals the sendto onto the esocket's owner worker via callback.
 */
static ssize_t s_dns_server_trans_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if(!a_stream || !a_data || a_size == 0)
        return -1;

    dns_server_client_session_t *l_session =
        (dns_server_client_session_t *)a_stream->_server_session;
    if(!l_session || !l_session->server || !a_stream->esocket || !a_stream->esocket->worker) {
        log_it(L_WARNING, "DNS server write: no session or esocket (likely during teardown)");
        return 0;
    }
    if (s_dns_server_is_stopping(l_session->server))
        return 0;

    dap_events_socket_t *l_es = a_stream->esocket;
    dap_worker_t *l_current = dap_worker_get_current();
    dap_worker_t *l_target = l_es->worker;

    if(l_current == l_target) {
        size_t l_sent = dap_events_socket_sendto_unsafe(l_es,
            a_data, a_size,
            &l_session->remote_addr, l_session->remote_addr_len);
        if(l_sent != a_size)
            log_it(L_WARNING, "DNS server write incomplete: %zu of %zu bytes", l_sent, a_size);
        return (ssize_t)l_sent;
    }

    dns_sendto_args_t *l_args = DAP_NEW_Z(dns_sendto_args_t);
    if(!l_args)
        return -1;
    l_args->worker = l_target;
    l_args->esocket_uuid = l_es->uuid;
    l_args->data = DAP_NEW_SIZE(byte_t, a_size);
    if(!l_args->data) {
        DAP_DELETE(l_args);
        return -1;
    }
    memcpy(l_args->data, a_data, a_size);
    l_args->size = a_size;
    memcpy(&l_args->addr, &l_session->remote_addr, l_session->remote_addr_len);
    l_args->addr_len = l_session->remote_addr_len;
    dap_worker_exec_callback_on(l_target, s_dns_sendto_callback, l_args);
    return (ssize_t)a_size;
}
