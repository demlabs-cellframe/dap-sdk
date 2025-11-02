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
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_list.h"
#include "dap_net_server_common.h"
#include "dap_events_socket.h"
#include "dap_server.h"
#include "dap_worker.h"

#define LOG_TAG "dap_net_server_common"

/**
 * @brief Structure to store pre_worker_added callback and user data in listener socket
 */
typedef struct {
    dap_net_server_pre_worker_added_callback_t pre_worker_added;
    void *user_data;
} dap_net_server_listener_data_t;

/**
 * @brief Standard accept callback for server listening sockets
 */
void dap_net_server_accept_callback(dap_events_socket_t *a_es_listener, 
                                     SOCKET a_remote_socket, 
                                     struct sockaddr_storage *a_remote_addr)
{
    dap_server_t *l_server = a_es_listener->server;
    if (!l_server) {
        log_it(L_ERROR, "No server in listening socket");
        closesocket(a_remote_socket);
        return;
    }

    if (a_remote_socket < 0) {
#ifdef DAP_OS_WINDOWS
        _set_errno(WSAGetLastError());
#endif
        log_it(L_ERROR, "Server socket %d accept() error %d: %s",
                        a_es_listener->socket, errno, dap_strerror(errno));
        return;
    }

    char l_remote_addr_str[INET6_ADDRSTRLEN] = "", l_port_str[NI_MAXSERV] = "";
    dap_events_desc_type_t l_es_type = DESCRIPTOR_TYPE_SOCKET_CLIENT;

    switch (a_remote_addr->ss_family) {
#ifdef DAP_OS_UNIX
    case AF_UNIX:
        l_es_type = DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT;
        debug_if(l_server->ext_log, L_INFO, "Connection accepted at \"%s\", socket %"DAP_FORMAT_SOCKET,
                                            a_es_listener->remote_addr_str, a_remote_socket);
        break;
#endif
    case AF_INET:
    case AF_INET6:
        if (getnameinfo((struct sockaddr*)a_remote_addr, sizeof(*a_remote_addr),
                       l_remote_addr_str, sizeof(l_remote_addr_str),
                       l_port_str, sizeof(l_port_str), NI_NUMERICHOST | NI_NUMERICSERV)) {
#ifdef DAP_OS_WINDOWS
            _set_errno(WSAGetLastError());
#endif
            log_it(L_ERROR, "getnameinfo() error %d: %s", errno, dap_strerror(errno));
            closesocket(a_remote_socket);
            return;
        }
        // Check whitelist/blacklist
        if ((l_server->whitelist
            ? !dap_str_find(l_server->whitelist, l_remote_addr_str)
            : !!dap_str_find(l_server->blacklist, l_remote_addr_str))) {
            log_it(L_DEBUG, "Connection from %s : %s denied by whitelist/blacklist (whitelist=%p, blacklist=%p)",
                   l_remote_addr_str, l_port_str, (void*)l_server->whitelist, (void*)l_server->blacklist);
            closesocket(a_remote_socket);
            return debug_if(l_server->ext_log, L_INFO, "Connection from %s : %s denied",
                            l_remote_addr_str, l_port_str);
        }
        debug_if(l_server->ext_log, L_INFO, "Connection accepted from %s : %s, socket %"DAP_FORMAT_SOCKET,
                                l_remote_addr_str, l_port_str, a_remote_socket);
        log_it(L_DEBUG, "Connection accepted from %s : %s, socket %"DAP_FORMAT_SOCKET,
                                l_remote_addr_str, l_port_str, a_remote_socket);
        int one = 1;
        if (setsockopt(a_remote_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one)) < 0) {
            log_it(L_WARNING, "Can't disable Nagle alg, error %d: %s", errno, dap_strerror(errno));
        }
        break;
    default:
        closesocket(a_remote_socket);
        return log_it(L_ERROR, "Unsupported protocol family %hu from accept()", a_remote_addr->ss_family);
    }

    // Create new client socket using server's client_callbacks
    dap_events_socket_t *l_es_new = dap_events_socket_wrap_no_add(a_remote_socket, &l_server->client_callbacks);
    if (!l_es_new) {
        log_it(L_ERROR, "Failed to wrap new client socket");
        closesocket(a_remote_socket);
        return;
    }

    l_es_new->server = l_server;
    l_es_new->type = l_es_type;
    l_es_new->addr_storage = *a_remote_addr;
    l_es_new->remote_port = strtol(l_port_str, NULL, 10);
    dap_strncpy(l_es_new->remote_addr_str, l_remote_addr_str, INET6_ADDRSTRLEN);
    
    log_it(L_DEBUG, "Created client socket %"DAP_FORMAT_SOCKET" from %s:%s, new_callback=%p",
           l_es_new->socket, l_remote_addr_str, l_port_str, 
           (void*)l_server->client_callbacks.new_callback);
    
    // Call pre_worker_added callback if provided
    dap_net_server_listener_data_t *l_listener_data = (dap_net_server_listener_data_t*)a_es_listener->_inheritor;
    if (l_listener_data && l_listener_data->pre_worker_added) {
        int l_pre_ret = l_listener_data->pre_worker_added(l_es_new, a_es_listener, l_listener_data->user_data);
        if (l_pre_ret != 0) {
            log_it(L_WARNING, "pre_worker_added callback returned error %d, closing socket", l_pre_ret);
            dap_events_socket_delete_unsafe(l_es_new, false);
            closesocket(a_remote_socket);
            return;
        }
    }
    
    log_it(L_DEBUG, "Adding client socket %"DAP_FORMAT_SOCKET" to worker", l_es_new->socket);
    dap_worker_add_events_socket(dap_events_worker_get_auto(), l_es_new);
    log_it(L_DEBUG, "Client socket %"DAP_FORMAT_SOCKET" added to worker", l_es_new->socket);
}

/**
 * @brief Create server callbacks structure with standard accept callback
 */
dap_events_socket_callbacks_t dap_net_server_listening_callbacks(
    dap_net_server_pre_worker_added_callback_t a_pre_worker_added,
    void *a_user_data)
{
    dap_events_socket_callbacks_t l_callbacks = {
        .accept_callback = dap_net_server_accept_callback,
        .new_callback    = NULL,
        .read_callback   = NULL,
        .write_callback  = NULL,
        .error_callback  = NULL
    };
    return l_callbacks;
}

/**
 * @brief Setup listener socket with pre_worker_added callback
 */
int dap_net_server_listen_addr_add_with_callback(dap_server_t *a_server,
                                                   const char *a_addr,
                                                   uint16_t a_port,
                                                   dap_events_desc_type_t a_type,
                                                   dap_net_server_pre_worker_added_callback_t a_pre_worker_added,
                                                   void *a_user_data)
{
    if (!a_server || !a_addr) {
        log_it(L_ERROR, "Invalid arguments for dap_net_server_listen_addr_add_with_callback");
        return -1;
    }

    // Create callbacks structure
    dap_events_socket_callbacks_t l_callbacks = dap_net_server_listening_callbacks(a_pre_worker_added, a_user_data);
    
    // Call dap_server_listen_addr_add to create listener socket
    int l_ret = dap_server_listen_addr_add(a_server, a_addr, a_port, a_type, &l_callbacks);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to add listener address %s:%u", a_addr, a_port);
        return l_ret;
    }

    log_it(L_DEBUG, "Listener socket added for %s:%u, searching for it in es_listeners", a_addr, a_port);

    // Find the newly created listener socket by address and port
    // The new listener should be at the head of es_listeners list (added via dap_list_prepend)
    dap_events_socket_t *l_listener = NULL;
    if (!a_server->es_listeners) {
        log_it(L_ERROR, "No listeners in server after dap_server_listen_addr_add for %s:%u", a_addr, a_port);
        return -2;
    }
    
    // Search through all listeners to find the one we just created
    dap_list_t *l_iter = a_server->es_listeners;
    while (l_iter) {
        l_listener = (dap_events_socket_t *)l_iter->data;
        if (l_listener) {
            log_it(L_DEBUG, "Found listener socket: addr='%s', port=%u, socket=%"DAP_FORMAT_SOCKET, 
                   l_listener->listener_addr_str, l_listener->listener_port, l_listener->socket);
            if (strcmp(l_listener->listener_addr_str, a_addr) == 0 &&
                l_listener->listener_port == a_port) {
                log_it(L_DEBUG, "Matched listener socket for %s:%u", a_addr, a_port);
                // Verify accept_callback is set
                if (!l_listener->callbacks.accept_callback) {
                    log_it(L_ERROR, "Listener socket for %s:%u has no accept_callback!", a_addr, a_port);
                    return -3;
                }
                log_it(L_DEBUG, "Listener socket for %s:%u has accept_callback=%p", 
                       a_addr, a_port, (void*)l_listener->callbacks.accept_callback);
                
                // Setup listener data if callback provided
                if (a_pre_worker_added) {
                    dap_net_server_listener_data_t *l_listener_data = DAP_NEW_Z(dap_net_server_listener_data_t);
                    if (!l_listener_data) {
                        log_it(L_ERROR, "Failed to allocate listener data");
                        return -4;
                    }
                    l_listener_data->pre_worker_added = a_pre_worker_added;
                    l_listener_data->user_data = a_user_data;
                    l_listener->_inheritor = l_listener_data;
                }
                log_it(L_INFO, "Successfully configured listener socket for %s:%u", a_addr, a_port);
                return 0;
            }
        }
        l_iter = l_iter->next;
    }
    
    log_it(L_ERROR, "Could not find newly created listener socket for %s:%u in server's es_listeners", a_addr, a_port);
    return -5;
}

