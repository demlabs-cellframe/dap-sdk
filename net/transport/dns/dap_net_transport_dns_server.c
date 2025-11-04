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
#include "dap_net_transport.h"
#include "dap_net_transport_dns_server.h"
#include "dap_net_transport_dns_stream.h"
#include "dap_stream.h"
#include "dap_net_transport_server.h"
#include "dap_events_socket.h"

#define LOG_TAG "dap_net_transport_dns_server"

// Transport server operations callbacks
static void* s_dns_server_new(const char *a_server_name)
{
    return (void*)dap_net_transport_dns_server_new(a_server_name);
}

static int s_dns_server_start(void *a_server, const char *a_cfg_section, 
                              const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    dap_net_transport_dns_server_t *l_dns = (dap_net_transport_dns_server_t *)a_server;
    return dap_net_transport_dns_server_start(l_dns, a_cfg_section, a_addrs, a_ports, a_count);
}

static void s_dns_server_stop(void *a_server)
{
    dap_net_transport_dns_server_t *l_dns = (dap_net_transport_dns_server_t *)a_server;
    dap_net_transport_dns_server_stop(l_dns);
}

static void s_dns_server_delete(void *a_server)
{
    dap_net_transport_dns_server_t *l_dns = (dap_net_transport_dns_server_t *)a_server;
    dap_net_transport_dns_server_delete(l_dns);
}

static const dap_net_transport_server_ops_t s_dns_server_ops = {
    .new = s_dns_server_new,
    .start = s_dns_server_start,
    .stop = s_dns_server_stop,
    .delete = s_dns_server_delete
};

/**
 * @brief Initialize DNS server module
 */
int dap_net_transport_dns_server_init(void)
{
    // Register transport server operations
    int l_ret = dap_net_transport_server_register_ops(DAP_NET_TRANSPORT_DNS_TUNNEL, &s_dns_server_ops);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register DNS transport server operations");
        return l_ret;
    }
    
    log_it(L_NOTICE, "Initialized DNS server module");
    return 0;
}

/**
 * @brief Deinitialize DNS server module
 */
void dap_net_transport_dns_server_deinit(void)
{
    // Unregister transport server operations
    dap_net_transport_server_unregister_ops(DAP_NET_TRANSPORT_DNS_TUNNEL);
    
    log_it(L_INFO, "DNS server module deinitialized");
}

/**
 * @brief Create new DNS server instance
 */
dap_net_transport_dns_server_t *dap_net_transport_dns_server_new(const char *a_server_name)
{
    if (!a_server_name) {
        log_it(L_ERROR, "Server name is NULL");
        return NULL;
    }

    dap_net_transport_dns_server_t *l_dns_server = DAP_NEW_Z(dap_net_transport_dns_server_t);
    if (!l_dns_server) {
        log_it(L_CRITICAL, "Cannot allocate memory for DNS server");
        return NULL;
    }

    dap_strncpy(l_dns_server->server_name, a_server_name, sizeof(l_dns_server->server_name) - 1);
    
    // Get DNS transport instance
    // Note: DNS transport implementation will be created separately
    // For now, we create the server structure but transport will be NULL
    // until DNS transport stream implementation is created
    l_dns_server->transport = dap_net_transport_find(DAP_NET_TRANSPORT_DNS_TUNNEL);
    if (!l_dns_server->transport) {
        log_it(L_WARNING, "DNS transport not registered yet - server will be created but transport operations will be limited");
        // Don't fail - server can be created before transport is registered
    }

    log_it(L_INFO, "Created DNS server: %s", a_server_name);
    return l_dns_server;
}

/**
 * @brief Start DNS server on specified addresses and ports
 * 
 * DNS tunnel server listens on UDP port (typically 53) and processes DNS queries
 * to tunnel DAP stream data through DNS responses.
 */
int dap_net_transport_dns_server_start(dap_net_transport_dns_server_t *a_dns_server,
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

    // Create underlying dap_server_t
    // DNS callbacks will be set by DNS transport implementation
    // Similar to UDP, DNS is connectionless
    dap_events_socket_callbacks_t l_dns_callbacks = {
        .new_callback = NULL,      // Will be set by DNS transport implementation
        .delete_callback = NULL,   // Will be set by DNS transport implementation
        .read_callback = NULL,     // Will be set by DNS transport implementation
        .write_callback = NULL,    // Will be set by DNS transport implementation
        .error_callback = NULL
    };

    a_dns_server->server = dap_server_new(a_cfg_section, NULL, &l_dns_callbacks);
    if (!a_dns_server->server) {
        log_it(L_ERROR, "Failed to create dap_server for DNS");
        return -3;
    }

    // Set DNS server as inheritor
    a_dns_server->server->_inheritor = a_dns_server;

    // Register DNS stream handlers
    // This sets up all necessary callbacks for DNS processing
    // DNS uses the same callbacks as UDP since both are connectionless
    dap_stream_add_proc_dns(a_dns_server->server);

    log_it(L_DEBUG, "Registered DNS stream handlers");

    // Start listening on all specified address:port pairs
    for (size_t i = 0; i < a_count; i++) {
        const char *l_addr = (a_addrs && a_addrs[i]) ? a_addrs[i] : "0.0.0.0";
        uint16_t l_port = a_ports[i];

        int l_ret = dap_server_listen_addr_add(a_dns_server->server, l_addr, l_port,
                                                DESCRIPTOR_TYPE_SOCKET_UDP,
                                                &a_dns_server->server->client_callbacks);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to start DNS server on %s:%u", l_addr, l_port);
            dap_net_transport_dns_server_stop(a_dns_server);
            return -4;
        }

        log_it(L_NOTICE, "DNS server '%s' listening on %s:%u",
               a_dns_server->server_name, l_addr, l_port);
    }

    return 0;
}

/**
 * @brief Stop DNS server
 */
void dap_net_transport_dns_server_stop(dap_net_transport_dns_server_t *a_dns_server)
{
    if (!a_dns_server) {
        return;
    }

    if (a_dns_server->server) {
        dap_server_delete(a_dns_server->server);
        a_dns_server->server = NULL;
    }

    log_it(L_INFO, "DNS server '%s' stopped", a_dns_server->server_name);
}

/**
 * @brief Delete DNS server instance
 */
void dap_net_transport_dns_server_delete(dap_net_transport_dns_server_t *a_dns_server)
{
    if (!a_dns_server) {
        return;
    }

    // Ensure server is stopped before deletion
    dap_net_transport_dns_server_stop(a_dns_server);

    log_it(L_INFO, "Deleted DNS server: %s", a_dns_server->server_name);
    DAP_DELETE(a_dns_server);
}

