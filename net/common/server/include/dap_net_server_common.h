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

#ifndef DAP_NET_SERVER_COMMON_H
#define DAP_NET_SERVER_COMMON_H

#include "dap_events_socket.h"
#include "dap_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback type for pre-worker-added hook
 * 
 * Called after creating new client socket but before adding it to worker.
 * Allows custom configuration of the socket before it's added to the event loop.
 * 
 * @param a_es_new Newly created client socket
 * @param a_es_listener Listening socket that accepted the connection
 * @param a_user_data User data passed to dap_net_server_listening_callbacks
 * @return 0 on success, non-zero on error (socket will be closed and not added to worker)
 */
typedef int (*dap_net_server_pre_worker_added_callback_t)(dap_events_socket_t *a_es_new,
                                                           dap_events_socket_t *a_es_listener,
                                                           void *a_user_data);

/**
 * @brief Standard accept callback for server listening sockets
 * 
 * This function implements a platform-independent accept callback that:
 * - Validates the remote socket
 * - Resolves remote address (supports IPv4, IPv6, and Unix sockets)
 * - Checks whitelist/blacklist if configured
 * - Sets TCP_NODELAY for TCP connections
 * - Creates a new client socket using server's client_callbacks
 * - Calls pre_worker_added callback if provided
 * - Adds socket to worker event loop
 * 
 * @param a_es_listener Listening socket that received the connection
 * @param a_remote_socket New client socket from accept()
 * @param a_remote_addr Remote address structure
 */
void dap_net_server_accept_callback(dap_events_socket_t *a_es_listener, 
                                     SOCKET a_remote_socket, 
                                     struct sockaddr_storage *a_remote_addr);

/**
 * @brief Create server callbacks structure with standard accept callback
 * 
 * Helper function to create dap_events_socket_callbacks_t for listening sockets
 * with accept_callback set to dap_net_server_accept_callback.
 * 
 * @param a_pre_worker_added Optional callback called before adding socket to worker
 * @param a_user_data User data passed to pre_worker_added callback
 * @return dap_events_socket_callbacks_t with accept_callback initialized
 */
dap_events_socket_callbacks_t dap_net_server_listening_callbacks(
    dap_net_server_pre_worker_added_callback_t a_pre_worker_added,
    void *a_user_data);

/**
 * @brief Setup listener socket with pre_worker_added callback
 * 
 * Helper function that calls dap_server_listen_addr_add and sets up listener data
 * for pre_worker_added callback support.
 * 
 * @param a_server Server instance
 * @param a_addr Address to listen on
 * @param a_port Port to listen on
 * @param a_type Socket type (DESCRIPTOR_TYPE_SOCKET_LISTENING or DESCRIPTOR_TYPE_SOCKET_UDP)
 * @param a_pre_worker_added Optional callback called before adding socket to worker
 * @param a_user_data User data passed to pre_worker_added callback
 * @return 0 on success, non-zero on error
 */
int dap_net_server_listen_addr_add_with_callback(dap_server_t *a_server,
                                                   const char *a_addr,
                                                   uint16_t a_port,
                                                   dap_events_desc_type_t a_type,
                                                   dap_net_server_pre_worker_added_callback_t a_pre_worker_added,
                                                   void *a_user_data);

#ifdef __cplusplus
}
#endif

#endif // DAP_NET_SERVER_COMMON_H

