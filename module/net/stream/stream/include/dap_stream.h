/*
 Copyright (c) 2017-2018 (c) Project "DeM Labs Inc" https://github.com/demlabsinc
  All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <stdbool.h>
#include <pthread.h>
#include "dap_http_server.h"
#include "dap_events_socket.h"
#include "dap_config.h"
#include "dap_stream_session.h"
#include "dap_timerfd.h"
#include "dap_sign.h"
#include "dap_cert.h"
#include "dap_pkey.h"
#include "dap_strfuncs.h"
#include "dap_enc_ks.h"
#include "dap_net_trans.h"
#include "dap_net_trans_ctx.h"
#include "dap_ht.h"
#include "dap_hash_compat.h"

#define STREAM_KEEPALIVE_TIMEOUT    3   // How  often send keeplive messages (seconds)

typedef struct dap_stream_ch dap_stream_ch_t;
typedef struct dap_stream_worker dap_stream_worker_t;

typedef struct dap_stream {
    dap_cluster_node_addr_t node;
    bool authorized;
    bool primary;
    int id;
    dap_stream_session_t *session;
    dap_stream_worker_t *stream_worker;
    dap_events_socket_t *esocket;

    dap_timerfd_t *keepalive_timer;
    bool is_active;

    char *service_key;
    bool is_client_to_uplink;

    uint8_t *buf_fragments, *pkt_cache;
    size_t buf_fragments_size_total;// Full size of all fragments
    size_t buf_fragments_size_filled;// Received size

    dap_stream_ch_t **channel;
    size_t channel_count;

    size_t seq_id;
    size_t stream_size;
    size_t client_last_seq_id_packet;

    dap_ht_handle_t hh;
    struct dap_stream *prev, *next;
    
    /**
     * @brief Transport layer abstraction
     * 
     * This field provides access to the pluggable transport layer
     * interface that supports HTTP, UDP, WebSocket, and other transports.
     * 
     * For HTTP transport, use dap_stream_transport_http_get_client()
     * to get the underlying http_client if needed.
     * 
     * @see dap_stream_trans.h
     * @see dap_stream_trans_http.h
     */
    struct dap_net_trans *trans;
    dap_net_trans_ctx_t *trans_ctx;
    
    /**
     * @brief Datagram flow (for UDP, SCTP, etc)
     * 
     * For datagram-based transports, points to dap_io_flow_datagram_t.
     * Used for both CLIENT and SERVER to get remote address via callback.
     * NULL for stream-oriented transports (TCP, HTTP).
     * 
     * @see dap_io_flow_datagram.h
     */
    void *flow;
    
    /**
     * @brief Server-side session backlink
     * 
     * On server, points to the protocol-specific session structure
     * (e.g., stream_udp_session_t for UDP). This allows trans->ops->write
     * to find the session and call the appropriate send callback.
     * 
     * NULL on client side.
     */
    void *_server_session;
} dap_stream_t;

typedef struct dap_stream_info {
    dap_cluster_node_addr_t node_addr;
    char *remote_addr_str;
    uint16_t remote_port;
    char *channels;
    size_t total_packets_sent;
    bool is_uplink;
} dap_stream_info_t;

#define DAP_STREAM(a) ((dap_stream_t *) (a)->_inheritor )

int dap_stream_init(dap_config_t * g_config);

bool dap_stream_get_dump_packet_headers();

void dap_stream_deinit();

void dap_stream_add_proc_http(dap_http_server_t * sh, const char * url);

void dap_stream_add_proc_udp(dap_server_t *a_udp_server);

void dap_stream_add_proc_dns(dap_server_t *a_dns_server);

dap_stream_t* dap_stream_new_es_client(dap_events_socket_t * a_es, dap_cluster_node_addr_t *a_addr, bool a_authorized);
size_t dap_stream_data_proc_read(dap_stream_t * a_stream);
size_t dap_stream_data_proc_read_ext(dap_stream_t * a_stream, const void *a_data, size_t a_data_size);
size_t dap_stream_data_proc_write(dap_stream_t * a_stream);

/**
 * @brief Send raw data to stream (datagram-aware)
 * 
 * Sends raw data to the stream, handling datagram (UDP, SCTP, etc) destination resolution automatically.
 * For datagram streams, uses flow callback to get destination address.
 * For stream-oriented (TCP), uses direct esocket write.
 * 
 * @param a_stream Stream instance
 * @param a_data Data to send
 * @param a_size Data size
 * @return Number of bytes sent, or 0 on error
 */
ssize_t dap_stream_send_unsafe(dap_stream_t *a_stream, const void *a_data, size_t a_size);

/**
 * @brief Write data via transport layer (wrapper for trans->ops->write)
 * 
 * Generic wrapper to call transport-specific write callback.
 * For UDP this goes through Flow Control for reliable delivery.
 * FAIL-FAST: No fallback, returns error if trans->ops->write is not set.
 * 
 * @param a_stream Stream instance
 * @param a_data Data to write
 * @param a_size Data size
 * @return Number of bytes written, or 0 on error
 */
ssize_t dap_stream_trans_write_unsafe(dap_stream_t *a_stream, const void *a_data, size_t a_size);

void dap_stream_delete_unsafe(dap_stream_t * a_stream);
void dap_stream_proc_pkt_in(dap_stream_t * sid);

dap_enc_key_type_t dap_stream_get_preferred_encryption_type();
dap_stream_t *dap_stream_get_from_es(dap_events_socket_t *a_es);

// autorization stream block
int dap_stream_add_addr(dap_cluster_node_addr_t a_addr, void *a_id);
int dap_stream_add_to_list(dap_stream_t *a_stream);
int dap_stream_delete_addr(dap_cluster_node_addr_t a_addr, bool a_full);
int dap_stream_delete_prep_addr(uint64_t a_num_id, void *a_pointer_id);
int dap_stream_add_stream_info(dap_stream_t *a_stream, uint64_t a_id);

dap_events_socket_uuid_t dap_stream_find_by_addr(dap_cluster_node_addr_t *a_addr, dap_worker_t **a_worker);
dap_list_t *dap_stream_find_all_by_addr(dap_cluster_node_addr_t *a_addr);
dap_stream_info_t *dap_stream_get_all_links_info(size_t *a_count);
dap_stream_info_t *dap_stream_get_links_info_by_addrs(dap_cluster_node_addr_t *a_addrs,
                                                       size_t a_addrs_count, size_t *a_count);
void dap_stream_delete_links_info(dap_stream_info_t *a_info, size_t a_count);

static inline size_t dap_stream_get_links_count(void) {
    size_t l_count = 0;
    dap_stream_info_t *l_info = dap_stream_get_all_links_info(&l_count);
    if (l_info)
        dap_stream_delete_links_info(l_info, l_count);
    return l_count;
}

typedef void (*dap_stream_member_callback_t)(dap_cluster_node_addr_t *a_addr);
void dap_stream_set_member_callbacks(dap_stream_member_callback_t a_add,
                                     dap_stream_member_callback_t a_del);

