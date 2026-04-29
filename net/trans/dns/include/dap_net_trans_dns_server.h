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

#pragma once

#include <stdbool.h>
#include <stdatomic.h>

#include "dap_common.h"
#include "dap_server.h"
#include "dap_net_trans.h"
#include "dap_net_trans_ctx.h"
#include "dap_enc_key.h"
#include "dap_stream.h"
#include "dap_stream_session.h"
#include "uthash.h"

typedef struct dap_net_trans_dns_server dap_net_trans_dns_server_t;

#ifdef DAP_OS_WINDOWS
typedef void *dap_net_trans_dns_sessions_lock_t;
#else
#include <pthread.h>
typedef pthread_rwlock_t dap_net_trans_dns_sessions_lock_t;
#endif

/**
 * @brief Per-client session for DNS server address-based routing
 *
 * DNS is connectionless — server distinguishes clients by remote address.
 * Each unique IP:port pair gets a session with its own encryption key
 * and a server-side stream for bidirectional data exchange.
 */
typedef struct dns_server_client_session {
    struct sockaddr_storage remote_addr;
    socklen_t remote_addr_len;
    dap_enc_key_t *handshake_key;
    dap_stream_t *stream;
    dap_net_trans_ctx_t *trans_ctx;
    dap_stream_session_t *stream_session;
    dap_net_trans_dns_server_t *server;
    UT_hash_handle hh;
} dns_server_client_session_t;

/**
 * @brief DNS tunnel server structure
 */
struct dap_net_trans_dns_server {
    dap_server_t *server;
    char server_name[256];
    dap_net_trans_t *trans;
    dns_server_client_session_t *sessions;
    dap_net_trans_dns_sessions_lock_t sessions_lock;
    atomic_bool stopping;
    atomic_uint deferred_state;
    atomic_uint datagram_reads_inflight;
};

#define DAP_NET_TRANS_DNS_SERVER(a) ((dap_net_trans_dns_server_t *) (a)->_inheritor)

int dap_net_trans_dns_server_init(void);
void dap_net_trans_dns_server_deinit(void);
dap_net_trans_dns_server_t *dap_net_trans_dns_server_new(const char *a_server_name);
int dap_net_trans_dns_server_start(dap_net_trans_dns_server_t *a_dns_server,
                                       const char *a_cfg_section,
                                       const char **a_addrs, 
                                       uint16_t *a_ports, 
                                       size_t a_count);
void dap_net_trans_dns_server_stop(dap_net_trans_dns_server_t *a_dns_server);
void dap_net_trans_dns_server_delete(dap_net_trans_dns_server_t *a_dns_server);

#ifdef DAP_SDK_TESTS
bool dap_net_trans_dns_server_test_schedule_deferred_stop(dap_net_trans_dns_server_t *a_dns_server);
bool dap_net_trans_dns_server_test_schedule_deferred_delete(dap_net_trans_dns_server_t *a_dns_server);
void dap_net_trans_dns_server_test_run_deferred(dap_net_trans_dns_server_t *a_dns_server);
bool dap_net_trans_dns_server_test_deferred_has_delete(dap_net_trans_dns_server_t *a_dns_server);
bool dap_net_trans_dns_server_test_deferred_has_stop(dap_net_trans_dns_server_t *a_dns_server);
bool dap_net_trans_dns_server_test_deferred_has_callback(dap_net_trans_dns_server_t *a_dns_server);
bool dap_net_trans_dns_server_test_remove_deleted_session(dap_net_trans_dns_server_t *a_dns_server,
                                                         dns_server_client_session_t *a_session);
void dap_net_trans_dns_server_test_process_datagram(dap_events_socket_t *a_es,
                                                   dap_net_trans_dns_server_t *a_dns_server,
                                                   void *a_data, size_t a_size,
                                                   struct sockaddr_storage *a_addr,
                                                   socklen_t a_addr_len);
#endif
