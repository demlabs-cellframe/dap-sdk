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

#include <pthread.h>
#include "dap_server.h"
#include "dap_net_trans.h"
#include "dap_net_trans_ctx.h"
#include "dap_enc_key.h"
#include "dap_stream.h"
#include "dap_stream_session.h"
#include "uthash.h"
#include <time.h>

/**
 * @brief Per-client session for DNS server cookie/nonce routing
 */
typedef struct dns_server_client_session {
    uint64_t cookie;
    uint64_t nonce;
    struct sockaddr_storage remote_addr;
    socklen_t remote_addr_len;
    dap_enc_key_t *handshake_key;
    dap_stream_t *stream;
    dap_net_trans_ctx_t *trans_ctx;
    dap_stream_session_t *stream_session;
    bool handshake_complete;
    time_t last_active;
    uint32_t next_down_id;
    uint32_t last_up_msg;
    uint16_t last_up_frag;
    size_t down_count;
    size_t down_pos;
    void *down_queue;
    void *reasm;
    UT_hash_handle hh;
} dns_server_client_session_t;

/**
 * @brief DNS tunnel server structure
 */
typedef struct dap_net_trans_dns_server {
    dap_server_t *server;
    char server_name[256];
    dap_net_trans_t *trans;
    dns_server_client_session_t *sessions;
    /* Protects the sessions hash table.
     * The table is shared across all sharded listener sockets (one per worker).
     * Without this lock concurrent HASH_ADD/FIND from different workers corrupt it. */
    pthread_mutex_t sessions_lock;
} dap_net_trans_dns_server_t;

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
