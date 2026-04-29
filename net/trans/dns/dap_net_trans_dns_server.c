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
#include <errno.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_time.h"
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
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define LOG_TAG "dap_net_trans_dns_server"
#define DNS_STOP_READ_DRAIN_RETRIES 500
#define DNS_STOP_READ_DRAIN_SLEEP_US 1000
#define DNS_SERVER_DEFER_STOP      0x01u
#define DNS_SERVER_DEFER_DELETE    0x02u
#define DNS_SERVER_DEFER_QUEUED    0x04u
#define DNS_SERVER_DEFER_RUNNING   0x08u

#ifdef DAP_OS_WINDOWS
#define DAP_DNS_RECV_FLAGS 0
#else
#define DAP_DNS_RECV_FLAGS MSG_DONTWAIT
#endif

static bool s_debug_more = false;
static void s_dns_listener_read_cb(dap_events_socket_t *a_es, void *a_arg);
static ssize_t s_dns_server_trans_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static bool s_dns_server_get_remote_addr_cb(dap_io_flow_datagram_t *a_flow,
                                            struct sockaddr_storage *a_addr_out,
                                            socklen_t *a_addr_len_out);
static void s_dns_process_datagram(dap_events_socket_t *a_es, dap_net_trans_dns_server_t *a_dns_server,
                                   void *a_data, size_t a_size,
                                   struct sockaddr_storage *a_addr, socklen_t a_addr_len);
static bool s_dns_server_stop_internal(dap_net_trans_dns_server_t *a_dns_server);
static bool s_dns_server_wait_reads_drain(dap_net_trans_dns_server_t *a_dns_server, size_t a_retries,
                                          bool a_allow_current_datagram);
static void s_dns_server_deferred_cb(void *a_arg);
static void s_dns_server_delete_terminal(dap_net_trans_dns_server_t *a_dns_server);
static bool s_dns_server_remove_deleted_session(dap_net_trans_dns_server_t *a_dns_server,
                                                dns_server_client_session_t *a_session);
static void s_dns_server_dispatch_unqueued_deferred(dap_net_trans_dns_server_t *a_dns_server,
                                                   bool a_run_inline_on_failure);

static _Thread_local dap_net_trans_dns_server_t *s_tls_processing_server = NULL;

#ifdef DAP_OS_WINDOWS
static int s_dns_sessions_lock_init(dap_net_trans_dns_sessions_lock_t *a_lock)
{
    if (!a_lock)
        return EINVAL;

    SRWLOCK *l_lock = DAP_NEW_Z(SRWLOCK);
    if (!l_lock)
        return ENOMEM;
    InitializeSRWLock(l_lock);
    *a_lock = l_lock;
    return 0;
}

static int s_dns_sessions_lock_destroy(dap_net_trans_dns_sessions_lock_t *a_lock)
{
    if (!a_lock || !*a_lock)
        return EINVAL;

    DAP_DELETE(*a_lock);
    *a_lock = NULL;
    return 0;
}

static int s_dns_sessions_rdlock(dap_net_trans_dns_sessions_lock_t *a_lock)
{
    if (!a_lock || !*a_lock)
        return EINVAL;

    AcquireSRWLockShared((SRWLOCK *)*a_lock);
    return 0;
}

static int s_dns_sessions_wrlock(dap_net_trans_dns_sessions_lock_t *a_lock)
{
    if (!a_lock || !*a_lock)
        return EINVAL;

    AcquireSRWLockExclusive((SRWLOCK *)*a_lock);
    return 0;
}

static void s_dns_sessions_rdunlock(dap_net_trans_dns_sessions_lock_t *a_lock)
{
    if (a_lock && *a_lock)
        ReleaseSRWLockShared((SRWLOCK *)*a_lock);
}

static void s_dns_sessions_wrunlock(dap_net_trans_dns_sessions_lock_t *a_lock)
{
    if (a_lock && *a_lock)
        ReleaseSRWLockExclusive((SRWLOCK *)*a_lock);
}
#else
static int s_dns_sessions_lock_init(dap_net_trans_dns_sessions_lock_t *a_lock)
{
    return pthread_rwlock_init(a_lock, NULL);
}

static int s_dns_sessions_lock_destroy(dap_net_trans_dns_sessions_lock_t *a_lock)
{
    return pthread_rwlock_destroy(a_lock);
}

static int s_dns_sessions_rdlock(dap_net_trans_dns_sessions_lock_t *a_lock)
{
    return pthread_rwlock_rdlock(a_lock);
}

static int s_dns_sessions_wrlock(dap_net_trans_dns_sessions_lock_t *a_lock)
{
    return pthread_rwlock_wrlock(a_lock);
}

static void s_dns_sessions_rdunlock(dap_net_trans_dns_sessions_lock_t *a_lock)
{
    pthread_rwlock_unlock(a_lock);
}

static void s_dns_sessions_wrunlock(dap_net_trans_dns_sessions_lock_t *a_lock)
{
    pthread_rwlock_unlock(a_lock);
}
#endif

#ifdef _WIN32
static SOCKET s_dns_esocket_socket(dap_events_socket_t *a_es)
{
    return a_es ? a_es->socket : INVALID_SOCKET;
}

static bool s_dns_esocket_is_valid(dap_events_socket_t *a_es)
{
    return a_es && a_es->socket != INVALID_SOCKET;
}
#else
static int s_dns_esocket_socket(dap_events_socket_t *a_es)
{
    return a_es ? a_es->fd : -1;
}

static bool s_dns_esocket_is_valid(dap_events_socket_t *a_es)
{
    return a_es && a_es->fd >= 0;
}
#endif

static bool s_dns_server_is_stopping(const dap_net_trans_dns_server_t *a_dns_server)
{
    return a_dns_server && atomic_load(&a_dns_server->stopping);
}

static bool s_dns_server_is_current_datagram(const dap_net_trans_dns_server_t *a_dns_server)
{
    return a_dns_server && s_tls_processing_server == a_dns_server;
}

static void s_dns_server_stream_detach_shared_listener(dap_stream_t *a_stream)
{
    if (!a_stream)
        return;

    a_stream->esocket = NULL;
    a_stream->esocket_uuid = 0;
    a_stream->esocket_worker = NULL;
    a_stream->trans = NULL;
}

static dap_io_flow_datagram_t *s_dns_server_stream_take_flow(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->flow)
        return NULL;

    dap_io_flow_datagram_t *l_flow = a_stream->flow;
    a_stream->flow = NULL;
    return l_flow;
}

static void s_dns_server_stream_release_shared_listener_ownership(dap_stream_t *a_stream)
{
    if (!a_stream)
        return;

    a_stream->esocket_uuid = 0;
    a_stream->esocket_worker = NULL;
    a_stream->trans = NULL;
}

static void s_dns_server_session_forget_deleted_stream(dns_server_client_session_t *a_session)
{
    if (!a_session)
        return;

    if (a_session->handshake_key)
        dap_enc_key_delete(a_session->handshake_key);
    a_session->stream = NULL;
    a_session->trans_ctx = NULL;
    a_session->stream_session = NULL;
    DAP_DELETE(a_session);
}

static void s_dns_server_session_delete(dns_server_client_session_t *a_session)
{
    if (!a_session)
        return;

    if (a_session->handshake_key) {
        dap_enc_key_delete(a_session->handshake_key);
        a_session->handshake_key = NULL;
    }

    dap_stream_t *l_stream = a_session->stream;
    dap_net_trans_ctx_t *l_trans_ctx = a_session->trans_ctx;
    dap_stream_session_t *l_stream_session = a_session->stream_session;
    uint32_t l_stream_session_id = l_stream_session ? l_stream_session->id : 0;
    bool l_stream_owns_trans_ctx = l_stream && l_stream->trans_ctx == l_trans_ctx;
    bool l_stream_owns_session = l_stream && l_stream->session == l_stream_session;

    a_session->stream = NULL;
    a_session->trans_ctx = NULL;
    a_session->stream_session = NULL;

    if (l_stream) {
        s_dns_server_stream_detach_shared_listener(l_stream);
        dap_io_flow_datagram_t *l_flow = s_dns_server_stream_take_flow(l_stream);
        if (l_flow)
            dap_io_flow_datagram_delete(l_flow);
        l_stream->_server_session = NULL;
        dap_stream_delete_unsafe(l_stream);
    }

    if (l_stream_session_id && !l_stream_owns_session)
        dap_stream_session_close_mt(l_stream_session_id);
    if (l_trans_ctx && !l_stream_owns_trans_ctx) {
        l_trans_ctx->stream = NULL;
        DAP_DELETE(l_trans_ctx);
    }
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

static void s_dns_server_sessions_detach_unsafe(dap_net_trans_dns_server_t *a_dns_server)
{
    if (!a_dns_server)
        return;

    dns_server_client_session_t *l_session = NULL, *l_tmp = NULL;
    HASH_ITER(hh, a_dns_server->sessions, l_session, l_tmp) {
        if (l_session->stream)
            s_dns_server_stream_detach_shared_listener(l_session->stream);
    }
}

static bool s_dns_server_remove_deleted_session(dap_net_trans_dns_server_t *a_dns_server,
                                                dns_server_client_session_t *a_session)
{
    if (!a_dns_server || !a_session)
        return false;

    int l_lock_ret = s_dns_sessions_wrlock(&a_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_ERROR, "DNS server: failed to lock sessions for delete-request cleanup: %d (%s)",
               l_lock_ret, dap_strerror(l_lock_ret));
        return false;
    }

    dns_server_client_session_t *l_found = NULL;
    HASH_FIND(hh, a_dns_server->sessions, &a_session->remote_addr,
              (unsigned)a_session->remote_addr_len, l_found);
    if (l_found == a_session)
        HASH_DEL(a_dns_server->sessions, a_session);
    s_dns_sessions_wrunlock(&a_dns_server->sessions_lock);

    if (l_found != a_session)
        return false;

    s_dns_server_session_forget_deleted_stream(a_session);
    return true;
}

static bool s_dns_server_deferred_callback_active(unsigned int a_state)
{
    return (a_state & (DNS_SERVER_DEFER_QUEUED | DNS_SERVER_DEFER_RUNNING)) != 0;
}

static bool s_dns_server_enqueue_deferred(dap_net_trans_dns_server_t *a_dns_server)
{
    if (!a_dns_server)
        return false;

    unsigned int l_state = atomic_load(&a_dns_server->deferred_state);
    while (!s_dns_server_deferred_callback_active(l_state)) {
        if (atomic_compare_exchange_weak(&a_dns_server->deferred_state, &l_state,
                                         l_state | DNS_SERVER_DEFER_QUEUED)) {
            dap_worker_t *l_worker = dap_worker_get_current();
            if (!l_worker) {
                atomic_fetch_and(&a_dns_server->deferred_state, ~DNS_SERVER_DEFER_QUEUED);
                return false;
            }

            int l_ret = dap_worker_exec_callback_on(l_worker, s_dns_server_deferred_cb, a_dns_server);
            if (l_ret != 0) {
                atomic_fetch_and(&a_dns_server->deferred_state, ~DNS_SERVER_DEFER_QUEUED);
                log_it(L_ERROR, "DNS server '%s': failed to enqueue deferred cleanup callback: %d",
                       a_dns_server->server_name, l_ret);
                return false;
            }
            return true;
        }
    }

    return true;
}

static bool s_dns_server_schedule_deferred(dap_net_trans_dns_server_t *a_dns_server, unsigned int a_pending_bits)
{
    if (!a_dns_server)
        return false;

    atomic_fetch_or(&a_dns_server->deferred_state, a_pending_bits);
    return s_dns_server_enqueue_deferred(a_dns_server);
}

static bool s_dns_server_schedule_deferred_delete(dap_net_trans_dns_server_t *a_dns_server)
{
    return s_dns_server_schedule_deferred(a_dns_server, DNS_SERVER_DEFER_DELETE);
}

static bool s_dns_server_schedule_deferred_stop(dap_net_trans_dns_server_t *a_dns_server)
{
    if (!a_dns_server || (atomic_load(&a_dns_server->deferred_state) & DNS_SERVER_DEFER_DELETE))
        return false;
    return s_dns_server_schedule_deferred(a_dns_server, DNS_SERVER_DEFER_STOP);
}

static bool s_dns_server_has_deferred_work(unsigned int a_state)
{
    return (a_state & (DNS_SERVER_DEFER_STOP | DNS_SERVER_DEFER_DELETE)) != 0;
}

static void s_dns_server_dispatch_unqueued_deferred(dap_net_trans_dns_server_t *a_dns_server,
                                                   bool a_run_inline_on_failure)
{
    if (!a_dns_server)
        return;

    unsigned int l_state = atomic_load(&a_dns_server->deferred_state);
    if (!s_dns_server_has_deferred_work(l_state) ||
            s_dns_server_deferred_callback_active(l_state))
        return;

    if (s_dns_server_enqueue_deferred(a_dns_server))
        return;

    if (!a_run_inline_on_failure)
        return;

    l_state = atomic_load(&a_dns_server->deferred_state);
    if (s_dns_server_has_deferred_work(l_state) &&
            !s_dns_server_deferred_callback_active(l_state)) {
        log_it(L_WARNING, "DNS server '%s': running deferred cleanup inline after enqueue failure",
               a_dns_server->server_name);
        s_dns_server_deferred_cb(a_dns_server);
    }
}

static bool s_dns_server_wait_reads_drain(dap_net_trans_dns_server_t *a_dns_server, size_t a_retries,
                                          bool a_allow_current_datagram)
{
    if (!a_dns_server)
        return true;

    unsigned int l_target = a_allow_current_datagram ? 1 : 0;
    for (size_t i = 0; i < a_retries; i++) {
        if (atomic_load(&a_dns_server->datagram_reads_inflight) <= l_target)
            return true;
        dap_usleep(DNS_STOP_READ_DRAIN_SLEEP_US);
    }
    return atomic_load(&a_dns_server->datagram_reads_inflight) <= l_target;
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

    int l_lock_ret = s_dns_sessions_lock_init(&l_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_CRITICAL, "Cannot initialize DNS server sessions lock: %d (%s)",
               l_lock_ret, dap_strerror(l_lock_ret));
        DAP_DELETE(l_dns_server);
        return NULL;
    }
    atomic_store(&l_dns_server->stopping, false);
    atomic_store(&l_dns_server->deferred_state, 0);
    atomic_store(&l_dns_server->datagram_reads_inflight, 0);

    dap_strncpy(l_dns_server->server_name, a_server_name, sizeof(l_dns_server->server_name) - 1);
    l_dns_server->sessions = NULL;
    
    l_dns_server->trans = DAP_NEW_Z(dap_net_trans_t);
    if (!l_dns_server->trans) {
        log_it(L_CRITICAL, "Cannot allocate DNS server trans");
        s_dns_sessions_lock_destroy(&l_dns_server->sessions_lock);
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
    if (!s_dns_server_wait_reads_drain(a_dns_server, DNS_STOP_READ_DRAIN_RETRIES, false)) {
        unsigned int l_reads_left = atomic_load(&a_dns_server->datagram_reads_inflight);
        log_it(L_ERROR, "DNS server start aborted: %u datagram read(s) still in progress",
               l_reads_left);
        return -3;
    }

    int l_lock_ret = s_dns_sessions_wrlock(&a_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_ERROR, "Failed to lock DNS sessions for start: %d (%s)",
               l_lock_ret, dap_strerror(l_lock_ret));
        return -4;
    }
    if (a_dns_server->sessions) {
        log_it(L_WARNING, "DNS server '%s': removing stale sessions before start", a_dns_server->server_name);
        s_dns_server_sessions_cleanup_unsafe(a_dns_server);
    }
    s_dns_sessions_wrunlock(&a_dns_server->sessions_lock);

    atomic_store(&a_dns_server->stopping, false);
    atomic_store(&a_dns_server->deferred_state, 0);

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

    atomic_store(&a_dns_server->stopping, true);
    if (s_dns_server_is_current_datagram(a_dns_server)) {
        if (!s_dns_server_schedule_deferred_stop(a_dns_server))
            log_it(L_ERROR, "DNS server '%s': failed to schedule deferred stop",
                   a_dns_server->server_name);
        log_it(L_WARNING, "DNS server '%s' stop requested from current datagram callback; cleanup deferred until callback exits",
               a_dns_server->server_name);
        return false;
    }

    if (a_dns_server->server) {
        dap_server_delete_sync(a_dns_server->server);
        a_dns_server->server = NULL;
    }

    if (!s_dns_server_wait_reads_drain(a_dns_server, DNS_STOP_READ_DRAIN_RETRIES, false)) {
        unsigned int l_reads_left = atomic_load(&a_dns_server->datagram_reads_inflight);
        log_it(L_ERROR, "DNS server stop deferred: %u datagram read(s) still in progress",
               l_reads_left);
        return false;
    }

    int l_lock_ret = s_dns_sessions_wrlock(&a_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_ERROR, "Failed to lock DNS sessions for cleanup: %d (%s)",
               l_lock_ret, dap_strerror(l_lock_ret));
        return false;
    }
    s_dns_server_sessions_detach_unsafe(a_dns_server);
    s_dns_server_sessions_cleanup_unsafe(a_dns_server);
    s_dns_sessions_wrunlock(&a_dns_server->sessions_lock);

    log_it(L_INFO, "DNS server '%s' stopped", a_dns_server->server_name);
    return true;
}

void dap_net_trans_dns_server_stop(dap_net_trans_dns_server_t *a_dns_server)
{
    if (!s_dns_server_stop_internal(a_dns_server) && a_dns_server) {
        log_it(L_WARNING, "DNS server '%s' stop did not fully complete", a_dns_server->server_name);
    }
}

static void s_dns_server_deferred_cb(void *a_arg)
{
    dap_net_trans_dns_server_t *l_dns_server = (dap_net_trans_dns_server_t *)a_arg;
    if (!l_dns_server)
        return;

    atomic_fetch_or(&l_dns_server->deferred_state, DNS_SERVER_DEFER_RUNNING);
    atomic_fetch_and(&l_dns_server->deferred_state, ~DNS_SERVER_DEFER_QUEUED);

    for (;;) {
        unsigned int l_state = atomic_load(&l_dns_server->deferred_state);
        if (l_state & DNS_SERVER_DEFER_DELETE) {
            atomic_fetch_and(&l_dns_server->deferred_state,
                             ~(DNS_SERVER_DEFER_STOP | DNS_SERVER_DEFER_DELETE | DNS_SERVER_DEFER_QUEUED));
            s_dns_server_delete_terminal(l_dns_server);
            return;
        }

        if (l_state & DNS_SERVER_DEFER_STOP) {
            atomic_fetch_and(&l_dns_server->deferred_state, ~DNS_SERVER_DEFER_STOP);
            dap_net_trans_dns_server_stop(l_dns_server);
            continue;
        }

        unsigned int l_expected = l_state;
        unsigned int l_desired = l_state & ~DNS_SERVER_DEFER_RUNNING;
        if (atomic_compare_exchange_weak(&l_dns_server->deferred_state, &l_expected, l_desired))
            return;
    }
}

void dap_net_trans_dns_server_delete(dap_net_trans_dns_server_t *a_dns_server)
{
    if (!a_dns_server)
        return;

    atomic_store(&a_dns_server->stopping, true);
    if (s_dns_server_is_current_datagram(a_dns_server)) {
        if (!s_dns_server_schedule_deferred_delete(a_dns_server))
            log_it(L_ERROR, "DNS server '%s': failed to schedule deferred delete",
                   a_dns_server->server_name);
        log_it(L_WARNING, "DNS server '%s' delete deferred until current datagram callback exits",
               a_dns_server->server_name);
        return;
    }

    unsigned int l_deferred_state = atomic_load(&a_dns_server->deferred_state);
    if (s_dns_server_deferred_callback_active(l_deferred_state)) {
        if (!s_dns_server_schedule_deferred_delete(a_dns_server))
            log_it(L_ERROR, "DNS server '%s': failed to promote deferred cleanup to delete",
                   a_dns_server->server_name);
        else
            log_it(L_WARNING, "DNS server '%s' delete deferred behind active cleanup callback",
                   a_dns_server->server_name);
        return;
    }

    atomic_store(&a_dns_server->deferred_state, 0);
    s_dns_server_delete_terminal(a_dns_server);
}

static void s_dns_server_delete_terminal(dap_net_trans_dns_server_t *a_dns_server)
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
    while (!s_dns_server_wait_reads_drain(a_dns_server, DNS_STOP_READ_DRAIN_RETRIES, false)) {
        unsigned int l_reads_left = atomic_load(&a_dns_server->datagram_reads_inflight);
        log_it(L_WARNING, "DNS server '%s' delete wait: %u datagram read(s) still in progress",
               a_dns_server->server_name, l_reads_left);
    }

    int l_lock_ret = s_dns_sessions_wrlock(&a_dns_server->sessions_lock);
    bool l_have_sessions_lock = (l_lock_ret == 0 || l_lock_ret == EDEADLK);
    if (!l_have_sessions_lock) {
        log_it(L_ERROR, "Delete fallback lock failed for DNS server '%s': %d (%s). Cleaning sessions without lock",
               a_dns_server->server_name, l_lock_ret, dap_strerror(l_lock_ret));
    }
    s_dns_server_sessions_detach_unsafe(a_dns_server);
    s_dns_server_sessions_cleanup_unsafe(a_dns_server);
    if (l_lock_ret == 0)
        s_dns_sessions_wrunlock(&a_dns_server->sessions_lock);

    // Best-effort destroy. Even if this fails, delete object to avoid orphan/leak.
    l_lock_ret = s_dns_sessions_lock_destroy(&a_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_WARNING, "Deleting DNS server '%s' with non-destroyed sessions lock: %d (%s)",
               a_dns_server->server_name, l_lock_ret, dap_strerror(l_lock_ret));
    }

    DAP_DEL_Z(a_dns_server->trans);
    log_it(L_INFO, "Deleted DNS server: %s", a_dns_server->server_name);
    DAP_DELETE(a_dns_server);
}

#ifdef DAP_SDK_TESTS
bool dap_net_trans_dns_server_test_schedule_deferred_stop(dap_net_trans_dns_server_t *a_dns_server)
{
    return s_dns_server_schedule_deferred_stop(a_dns_server);
}

bool dap_net_trans_dns_server_test_schedule_deferred_delete(dap_net_trans_dns_server_t *a_dns_server)
{
    return s_dns_server_schedule_deferred_delete(a_dns_server);
}

void dap_net_trans_dns_server_test_run_deferred(dap_net_trans_dns_server_t *a_dns_server)
{
    s_dns_server_deferred_cb(a_dns_server);
}

bool dap_net_trans_dns_server_test_deferred_has_delete(dap_net_trans_dns_server_t *a_dns_server)
{
    return a_dns_server &&
           (atomic_load(&a_dns_server->deferred_state) & DNS_SERVER_DEFER_DELETE) != 0;
}

bool dap_net_trans_dns_server_test_deferred_has_stop(dap_net_trans_dns_server_t *a_dns_server)
{
    return a_dns_server &&
           (atomic_load(&a_dns_server->deferred_state) & DNS_SERVER_DEFER_STOP) != 0;
}

bool dap_net_trans_dns_server_test_deferred_has_callback(dap_net_trans_dns_server_t *a_dns_server)
{
    return a_dns_server &&
           s_dns_server_deferred_callback_active(atomic_load(&a_dns_server->deferred_state));
}

bool dap_net_trans_dns_server_test_remove_deleted_session(dap_net_trans_dns_server_t *a_dns_server,
                                                         dns_server_client_session_t *a_session)
{
    return s_dns_server_remove_deleted_session(a_dns_server, a_session);
}

void dap_net_trans_dns_server_test_process_datagram(dap_events_socket_t *a_es,
                                                   dap_net_trans_dns_server_t *a_dns_server,
                                                   void *a_data, size_t a_size,
                                                   struct sockaddr_storage *a_addr,
                                                   socklen_t a_addr_len)
{
    s_dns_process_datagram(a_es, a_dns_server, a_data, a_size, a_addr, a_addr_len);
}
#endif

/**
 * @brief DNS server listener read callback
 *
 * Receives raw UDP packets, routes by remote address, processes KEM handshake.
 * For new clients: do KEM encapsulation, send bob_ciphertext back.
 */
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
        if (s_dns_server_is_stopping(l_dns_server))
            break;
        socklen_t l_addr_len = sizeof(l_addr);
        ssize_t l_read = recvfrom(s_dns_esocket_socket(a_es), l_buf, sizeof(l_buf), DAP_DNS_RECV_FLAGS,
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
        if (s_dns_server_is_stopping(l_dns_server))
            break;
        s_dns_process_datagram(a_es, l_dns_server, l_buf, (size_t)l_read, &l_addr, l_addr_len);
    }

    s_dns_server_dispatch_unqueued_deferred(l_dns_server, true);
}

static void s_dns_process_datagram(dap_events_socket_t *a_es, dap_net_trans_dns_server_t *a_dns_server,
                                   void *a_data, size_t a_size,
                                   struct sockaddr_storage *a_addr, socklen_t a_addr_len)
{
    if (!a_es || !a_dns_server || !a_data || a_size == 0 || !a_addr || a_addr_len == 0)
        return;

    atomic_fetch_add(&a_dns_server->datagram_reads_inflight, 1);
    dap_net_trans_dns_server_t *l_prev_processing_server = s_tls_processing_server;
    s_tls_processing_server = a_dns_server;

    bool l_have_sessions_lock = false;
    dap_enc_key_t *l_bob_key = NULL;
    dap_enc_key_t *l_handshake_key = NULL;

    int l_lock_ret = s_dns_sessions_rdlock(&a_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_ERROR, "DNS server: failed to lock sessions for read: %d (%s)",
               l_lock_ret, dap_strerror(l_lock_ret));
        goto cleanup;
    }
    l_have_sessions_lock = true;
    if (s_dns_server_is_stopping(a_dns_server)) {
        goto cleanup;
    }

    dns_server_client_session_t *l_session = NULL;
    HASH_FIND(hh, a_dns_server->sessions, a_addr, (unsigned)a_addr_len, l_session);

    dap_stream_t *l_existing_stream = l_session ? l_session->stream : NULL;
    if (l_session) {
        s_dns_sessions_rdunlock(&a_dns_server->sessions_lock);
        l_have_sessions_lock = false;
        if (l_existing_stream) {
            bool l_delete_requested = false;
            dap_io_flow_datagram_t *l_flow = l_existing_stream->flow;
            s_dns_server_stream_release_shared_listener_ownership(l_existing_stream);
            dap_stream_data_proc_read_ext_checked(l_existing_stream, a_data, a_size, &l_delete_requested);
            if (l_delete_requested) {
                if (l_flow)
                    dap_io_flow_datagram_delete(l_flow);
                s_dns_server_remove_deleted_session(a_dns_server, l_session);
                goto cleanup;
            }
        }
        goto cleanup;
    }
    s_dns_sessions_rdunlock(&a_dns_server->sessions_lock);
    l_have_sessions_lock = false;

    log_it(L_INFO, "DNS server: new client handshake, size=%zu", a_size);

    if (dap_qos_is_probe(a_data, a_size)) {
        debug_if(s_debug_more, L_DEBUG, "DNS server: QoS probe detected (%zu bytes)", a_size);
        void  *l_echo = NULL;
        size_t l_echo_size = 0;
        if (dap_qos_build_echo(a_data, a_size, &l_echo, &l_echo_size) == 0) {
            if (!s_dns_server_is_stopping(a_dns_server))
                dap_events_socket_sendto_unsafe(a_es, l_echo, l_echo_size, a_addr, a_addr_len);
            DAP_DELETE(l_echo);
        }
        goto cleanup;
    }

    if (s_dns_server_is_stopping(a_dns_server))
        goto cleanup;

    /* KEM encapsulation: generate bob key, derive shared secret */
    l_bob_key = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    if (!l_bob_key) {
        log_it(L_ERROR, "DNS server: failed to generate Bob KEM key");
        goto cleanup;
    }

    void *l_bob_pub = NULL;
    size_t l_shared_key_size = 0;

    if (!l_bob_key->gen_bob_shared_key) {
        log_it(L_ERROR, "DNS server: key type doesn't support KEM");
        goto cleanup;
    }

    l_shared_key_size = l_bob_key->gen_bob_shared_key(
        l_bob_key, a_data, a_size, &l_bob_pub);

    if (!l_bob_pub || l_shared_key_size == 0 || !l_bob_key->shared_key) {
        log_it(L_ERROR, "DNS server: KEM encapsulation failed");
        goto cleanup;
    }

    log_it(L_INFO, "DNS server: KEM done, ciphertext=%zu bytes", l_shared_key_size);

    l_handshake_key = dap_enc_kdf_create_cipher_key(
        l_bob_key,
        DAP_ENC_KEY_TYPE_SALSA2012,
        "dns_handshake", 13,
        0, 32);

    if (!l_handshake_key) {
        log_it(L_ERROR, "DNS server: failed to derive handshake key");
        goto cleanup;
    }

    if (s_dns_server_is_stopping(a_dns_server))
        goto cleanup;

    /* Send bob_ciphertext back to client */
    size_t l_sent = dap_events_socket_sendto_unsafe(
        a_es, l_bob_pub, l_shared_key_size,
        a_addr, a_addr_len);

    if (l_sent != l_shared_key_size) {
        log_it(L_ERROR, "DNS server: failed to send handshake response: %zu of %zu",
               l_sent, l_shared_key_size);
        goto cleanup;
    }

    log_it(L_INFO, "DNS server: sent handshake response (%zu bytes)", l_sent);

    /* Create session for this client */
    l_session = DAP_NEW_Z(dns_server_client_session_t);
    if (!l_session) {
        goto cleanup;
    }

    memcpy(&l_session->remote_addr, a_addr, a_addr_len);
    l_session->remote_addr_len = a_addr_len;
    l_session->handshake_key = l_handshake_key;
    l_handshake_key = NULL;
    l_session->server = a_dns_server;

    /* Create server-side stream for bidirectional data exchange */
    dap_stream_t *l_stream = DAP_NEW_Z(dap_stream_t);
    if (!l_stream) {
        log_it(L_ERROR, "DNS server: failed to allocate stream");
        s_dns_server_session_delete(l_session);
        goto cleanup;
    }

    dap_net_trans_ctx_t *l_trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
    if (!l_trans_ctx) {
        log_it(L_ERROR, "DNS server: failed to allocate trans_ctx");
        DAP_DELETE(l_stream);
        s_dns_server_session_delete(l_session);
        goto cleanup;
    }
    l_trans_ctx->trans = a_dns_server->trans;
    l_trans_ctx->stream = l_stream;
    l_stream->esocket = a_es;
    l_stream->esocket_uuid = 0;
    l_stream->esocket_worker = NULL;
    l_stream->trans = a_dns_server->trans;
    l_stream->trans_ctx = l_trans_ctx;
    l_stream->_server_session = l_session;
    l_stream->stream_worker = DAP_STREAM_WORKER(a_es->worker);
    l_stream->flow = dap_io_flow_datagram_new(s_dns_server_get_remote_addr_cb, l_session);
    if (!l_stream->flow) {
        log_it(L_ERROR, "DNS server: failed to create datagram flow");
        DAP_DELETE(l_trans_ctx);
        DAP_DELETE(l_stream);
        s_dns_server_session_delete(l_session);
        goto cleanup;
    }

    dap_stream_session_t *l_stream_session = dap_stream_session_new(0, false);
    if (!l_stream_session) {
        log_it(L_ERROR, "DNS server: failed to create stream session");
        dap_io_flow_datagram_delete(l_stream->flow);
        l_stream->flow = NULL;
        DAP_DELETE(l_trans_ctx);
        DAP_DELETE(l_stream);
        s_dns_server_session_delete(l_session);
        goto cleanup;
    }
    dap_stream_session_open(l_stream_session);
    l_stream_session->key = dap_enc_key_dup(l_session->handshake_key);
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

    l_lock_ret = s_dns_sessions_wrlock(&a_dns_server->sessions_lock);
    if (l_lock_ret != 0) {
        log_it(L_ERROR, "DNS server: failed to lock sessions for add: %d (%s)",
               l_lock_ret, dap_strerror(l_lock_ret));
        s_dns_server_session_delete(l_session);
        goto cleanup;
    }
    l_have_sessions_lock = true;

    if (s_dns_server_is_stopping(a_dns_server)) {
        s_dns_sessions_wrunlock(&a_dns_server->sessions_lock);
        l_have_sessions_lock = false;
        s_dns_server_session_delete(l_session);
        goto cleanup;
    }

    dns_server_client_session_t *l_existing_session = NULL;
    HASH_FIND(hh, a_dns_server->sessions, a_addr, (unsigned)a_addr_len, l_existing_session);
    if (l_existing_session) {
        s_dns_sessions_wrunlock(&a_dns_server->sessions_lock);
        l_have_sessions_lock = false;
        s_dns_server_session_delete(l_session);
        goto cleanup;
    }

    HASH_ADD(hh, a_dns_server->sessions, remote_addr, (unsigned)a_addr_len, l_session);
    s_dns_sessions_wrunlock(&a_dns_server->sessions_lock);
    l_have_sessions_lock = false;

    log_it(L_INFO, "DNS server: created stream %p with %zu channels for new client",
           l_stream, l_stream->channel_count);

cleanup:
    if (l_have_sessions_lock)
        s_dns_sessions_wrunlock(&a_dns_server->sessions_lock);
    if (l_handshake_key)
        dap_enc_key_delete(l_handshake_key);
    if (l_bob_key)
        dap_enc_key_delete(l_bob_key);
    s_tls_processing_server = l_prev_processing_server;
    atomic_fetch_sub(&a_dns_server->datagram_reads_inflight, 1);
    s_dns_server_dispatch_unqueued_deferred(a_dns_server, false);
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
    if (!s_dns_esocket_is_valid(l_es))
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
    int l_ret = dap_worker_exec_callback_on(l_target, s_dns_sendto_callback, l_args);
    if (l_ret != 0) {
        log_it(L_ERROR, "DNS server write: failed to enqueue async send callback: %d", l_ret);
        DAP_DELETE(l_args->data);
        DAP_DELETE(l_args);
        return -1;
    }
    return (ssize_t)a_size;
}
