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

#include "dap_server.h"
#include "dap_net_trans.h"

// Forward declarations
typedef struct dap_io_flow_server dap_io_flow_server_t;
typedef struct udp_session_entry udp_session_entry_t;

/**
 * @brief Cross-worker packet forwarding structure
 */
typedef struct udp_cross_worker_packet {
    udp_session_entry_t *session;           ///< Target session (in remote worker)
    uint8_t *data;                           ///< Packet data (ownership transferred)
    size_t size;                             ///< Data size
    struct sockaddr_storage remote_addr;    ///< Source address
    socklen_t remote_addr_len;              ///< Address length
} udp_cross_worker_packet_t;

/**
 * @brief Per-worker context for inter-worker communication
 */
typedef struct udp_worker_context {
    uint32_t worker_id;                     ///< This worker's ID
    
    // Incoming pipe (receive from other workers)
    dap_events_socket_t *pipe_read_es;     ///< Read-end esocket for THIS worker
    
    // NO intermediate buffers! Packet pointers written DIRECTLY to pipe_write_es->buf_out
    // Reactor handles all I/O - zero-copy architecture
    
    // Write ends to other workers' pipes (wrapped as esockets with write callback)
    dap_events_socket_t **pipe_write_es;   ///< Array[worker_count] of write-end esockets
    uint32_t *pipe_write_target_ids;       ///< Array[worker_count] of target worker IDs (for callbacks)
    
    // Statistics
    _Atomic size_t packets_sent;
    _Atomic size_t packets_received;
    _Atomic size_t batches_flushed;
} udp_worker_context_t;

/**
 * @brief UDP server structure
 * 
 * Wraps generic dap_io_flow_server_t with Stream UDP protocol.
 */
typedef struct dap_net_trans_udp_server {
    char server_name[256];               ///< Server name for identification
    dap_net_trans_t *trans;              ///< UDP trans instance
    dap_io_flow_server_t **flow_servers; ///< Array of flow server instances (one per listener)
    size_t flow_servers_count;           ///< Number of flow servers
} dap_net_trans_udp_server_t;

#define DAP_NET_TRANS_UDP_SERVER(a) ((dap_net_trans_udp_server_t *) (a)->_inheritor)

/**
 * @brief Initialize UDP server module
 * @return 0 if success, negative error code otherwise
 */
int dap_net_trans_udp_server_init(void);

/**
 * @brief Deinitialize UDP server module
 */
void dap_net_trans_udp_server_deinit(void);

/**
 * @brief Create new UDP server instance
 * 
 * Allocates dap_net_trans_udp_server_t structure. Call dap_net_trans_udp_server_start()
 * to create internal dap_server_t and start listening.
 * 
 * @param a_server_name Server name for identification
 * @return Pointer to dap_net_trans_udp_server_t instance or NULL on error
 */
dap_net_trans_udp_server_t *dap_net_trans_udp_server_new(const char *a_server_name);

/**
 * @brief Start UDP server on specified address and port
 * 
 * Starts the flow server listening on the given address and port.
 * 
 * @param a_server UDP server instance
 * @param a_addr Address to bind (NULL for INADDR_ANY)
 * @param a_port Port to bind
 * @return 0 if success, negative error code otherwise
 */
int dap_net_trans_udp_server_start(dap_net_trans_udp_server_t *a_server,
                                   const char *a_addr,
                                   uint16_t a_port);

/**
 * @brief Stop UDP server and cleanup resources
 * 
 * @param a_udp_server UDP server instance
 */
void dap_net_trans_udp_server_stop(dap_net_trans_udp_server_t *a_udp_server);

/**
 * @brief Delete UDP server instance
 * 
 * Frees dap_net_trans_udp_server_t structure. Call dap_net_trans_udp_server_stop()
 * first to cleanup server resources.
 * 
 * @param a_udp_server UDP server instance
 */
void dap_net_trans_udp_server_delete(dap_net_trans_udp_server_t *a_udp_server);

