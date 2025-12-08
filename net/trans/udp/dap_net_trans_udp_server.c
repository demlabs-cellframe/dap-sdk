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
#include "dap_net_trans_udp_server.h"
#include "dap_net_trans_udp_stream.h"
#include "dap_stream.h"
#include "dap_net_trans_server.h"
#include "dap_events_socket.h"

#define LOG_TAG "dap_net_trans_udp_server"

// Trans server operations callbacks
static void* s_udp_server_new(const char *a_server_name)
{
    return (void*)dap_net_trans_udp_server_new(a_server_name);
}

static int s_udp_server_start(void *a_server, const char *a_cfg_section, 
                               const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    dap_net_trans_udp_server_t *l_udp = (dap_net_trans_udp_server_t *)a_server;
    return dap_net_trans_udp_server_start(l_udp, a_cfg_section, a_addrs, a_ports, a_count);
}

static void s_udp_server_stop(void *a_server)
{
    dap_net_trans_udp_server_t *l_udp = (dap_net_trans_udp_server_t *)a_server;
    dap_net_trans_udp_server_stop(l_udp);
}

static void s_udp_server_delete(void *a_server)
{
    dap_net_trans_udp_server_t *l_udp = (dap_net_trans_udp_server_t *)a_server;
    dap_net_trans_udp_server_delete(l_udp);
}

static const dap_net_trans_server_ops_t s_udp_server_ops = {
    .new = s_udp_server_new,
    .start = s_udp_server_start,
    .stop = s_udp_server_stop,
    .delete = s_udp_server_delete
};

/**
 * @brief Initialize UDP server module
 */
int dap_net_trans_udp_server_init(void)
{
    // Register trans server operations for all UDP variants
    int l_ret = dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_BASIC, &s_udp_server_ops);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register UDP_BASIC trans server operations");
        return l_ret;
    }
    
    // Register for other UDP variants too
    dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_RELIABLE, &s_udp_server_ops);
    dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_QUIC_LIKE, &s_udp_server_ops);
    
    log_it(L_NOTICE, "Initialized UDP server module");
    return 0;
}

/**
 * @brief Deinitialize UDP server module
 */
void dap_net_trans_udp_server_deinit(void)
{
    // Unregister trans server operations
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_UDP_BASIC);
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_UDP_RELIABLE);
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_UDP_QUIC_LIKE);
    
    log_it(L_INFO, "UDP server module deinitialized");
}

/**
 * @brief Create new UDP server instance
 */
dap_net_trans_udp_server_t *dap_net_trans_udp_server_new(const char *a_server_name)
{
    if (!a_server_name) {
        log_it(L_ERROR, "Server name is NULL");
        return NULL;
    }

    dap_net_trans_udp_server_t *l_udp_server = DAP_NEW_Z(dap_net_trans_udp_server_t);
    if (!l_udp_server) {
        log_it(L_CRITICAL, "Cannot allocate memory for UDP server");
        return NULL;
    }

    dap_strncpy(l_udp_server->server_name, a_server_name, sizeof(l_udp_server->server_name) - 1);
    
    // Get UDP trans instance
    l_udp_server->trans = dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    if (!l_udp_server->trans) {
        log_it(L_ERROR, "UDP trans not registered");
        DAP_DELETE(l_udp_server);
        return NULL;
    }

    log_it(L_INFO, "Created UDP server: %s", a_server_name);
    return l_udp_server;
}

/**
 * @brief Start UDP server on specified addresses and ports
 */
int dap_net_trans_udp_server_start(dap_net_trans_udp_server_t *a_udp_server,
                                       const char *a_cfg_section,
                                       const char **a_addrs,
                                       uint16_t *a_ports,
                                       size_t a_count)
{
    if (!a_udp_server || !a_ports || a_count == 0) {
        log_it(L_ERROR, "Invalid parameters for UDP server start");
        return -1;
    }

    if (a_udp_server->server) {
        log_it(L_WARNING, "UDP server already started");
        return -2;
    }

    // Create underlying dap_server_t
    // UDP callbacks will be set by dap_stream_add_proc_udp()
    dap_events_socket_callbacks_t l_udp_callbacks = {
        .new_callback = NULL,      // Will be set by dap_stream_add_proc_udp
        .delete_callback = NULL,   // Will be set by dap_stream_add_proc_udp
        .read_callback = NULL,     // Will be set by dap_stream_add_proc_udp
        .write_callback = NULL,    // Will be set by dap_stream_add_proc_udp
        .error_callback = NULL
    };

    a_udp_server->server = dap_server_new(a_cfg_section, NULL, &l_udp_callbacks);
    if (!a_udp_server->server) {
        log_it(L_ERROR, "Failed to create dap_server for UDP");
        return -3;
    }

    // Set UDP server as inheritor
    a_udp_server->server->_inheritor = a_udp_server;

    // Register UDP stream handlers
    // This sets up all necessary callbacks for UDP processing
    dap_stream_add_proc_udp(a_udp_server->server);

    log_it(L_DEBUG, "Registered UDP stream handlers");

    // Start listening on all specified address:port pairs
    for (size_t i = 0; i < a_count; i++) {
        const char *l_addr = (a_addrs && a_addrs[i]) ? a_addrs[i] : "0.0.0.0";
        uint16_t l_port = a_ports[i];

        int l_ret = dap_server_listen_addr_add(a_udp_server->server, l_addr, l_port,
                                                DESCRIPTOR_TYPE_SOCKET_UDP,
                                                &a_udp_server->server->client_callbacks);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to start UDP server on %s:%u", l_addr, l_port);
            dap_net_trans_udp_server_stop(a_udp_server);
            return -4;
        }

        log_it(L_NOTICE, "UDP server '%s' listening on %s:%u",
               a_udp_server->server_name, l_addr, l_port);
    }

    return 0;
}

/**
 * @brief Stop UDP server
 */
void dap_net_trans_udp_server_stop(dap_net_trans_udp_server_t *a_udp_server)
{
    if (!a_udp_server) {
        return;
    }

    if (a_udp_server->server) {
        dap_server_delete(a_udp_server->server);
        a_udp_server->server = NULL;
    }

    log_it(L_INFO, "UDP server '%s' stopped", a_udp_server->server_name);
}

/**
 * @brief Delete UDP server instance
 */
void dap_net_trans_udp_server_delete(dap_net_trans_udp_server_t *a_udp_server)
{
    if (!a_udp_server) {
        return;
    }

    // Ensure server is stopped before deletion
    dap_net_trans_udp_server_stop(a_udp_server);

    log_it(L_INFO, "Deleted UDP server: %s", a_udp_server->server_name);
    DAP_DELETE(a_udp_server);
}

