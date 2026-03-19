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
#include "dap_net_trans_http_server.h"
#include "dap_net_trans_http_stream.h"
#include "dap_stream.h"
#include "dap_stream_ctl.h"
#include "dap_enc_http.h"
#include "dap_net_trans_server.h"
#include "dap_events_socket.h"
#include "dap_http_simple.h"
#include "dap_net_server_common.h"

#define LOG_TAG "dap_net_trans_http_server"

// Trans server operations callbacks
static void* s_http_server_new(const char *a_server_name)
{
    return (void*)dap_net_trans_http_server_new(a_server_name);
}

static int s_http_server_start(void *a_server, const char *a_cfg_section, 
                                const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    dap_net_trans_http_server_t *l_http = (dap_net_trans_http_server_t *)a_server;
    return dap_net_trans_http_server_start(l_http, a_cfg_section, a_addrs, a_ports, a_count);
}

static void s_http_server_stop(void *a_server)
{
    dap_net_trans_http_server_t *l_http = (dap_net_trans_http_server_t *)a_server;
    dap_net_trans_http_server_stop(l_http);
}

static void s_http_server_delete(void *a_server)
{
    dap_net_trans_http_server_t *l_http = (dap_net_trans_http_server_t *)a_server;
    dap_net_trans_http_server_delete(l_http);
}

static const dap_net_trans_server_ops_t s_http_server_ops = {
    .new = s_http_server_new,
    .start = s_http_server_start,
    .stop = s_http_server_stop,
    .delete = s_http_server_delete
};

/**
 * @brief Initialize HTTP server module
 */
int dap_net_trans_http_server_init(void)
{
    log_it(L_DEBUG, "dap_net_trans_http_server_init: Starting HTTP server initialization");
    
    int l_ret = dap_http_init();
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize HTTP module: %d", l_ret);
        return l_ret;
    }
    
    log_it(L_DEBUG, "dap_net_trans_http_server_init: HTTP module initialized, initializing encryption");
    
    l_ret = enc_http_init();
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize HTTP encryption module: %d", l_ret);
        dap_http_deinit();
        return l_ret;
    }
    
    log_it(L_DEBUG, "dap_net_trans_http_server_init: Encryption module initialized, registering server operations");
    
    // Register trans server operations
    l_ret = dap_net_trans_server_register_ops(DAP_NET_TRANS_HTTP, &s_http_server_ops);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register HTTP trans server operations: %d", l_ret);
        enc_http_deinit();
        dap_http_deinit();
        return l_ret;
    }
    
    log_it(L_NOTICE, "Initialized HTTP server module");
    return 0;
}

/**
 * @brief Deinitialize HTTP server module
 */
void dap_net_trans_http_server_deinit(void)
{
    // Unregister trans server operations
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_HTTP);
    
    enc_http_deinit();
    dap_http_deinit();
    log_it(L_INFO, "HTTP server module deinitialized");
}

/**
 * @brief Create new HTTP server instance
 */
dap_net_trans_http_server_t *dap_net_trans_http_server_new(const char *a_server_name)
{
    if (!a_server_name) {
        log_it(L_ERROR, "Server name is NULL");
        return NULL;
    }

    dap_net_trans_http_server_t *l_http_server = DAP_NEW_Z(dap_net_trans_http_server_t);
    if (!l_http_server) {
        log_it(L_CRITICAL, "Cannot allocate memory for HTTP server");
        return NULL;
    }

    dap_strncpy(l_http_server->server_name, a_server_name, sizeof(l_http_server->server_name) - 1);
    
    // Get HTTP trans instance
    l_http_server->trans = dap_net_trans_find(DAP_NET_TRANS_HTTP);
    if (!l_http_server->trans) {
        log_it(L_ERROR, "HTTP trans not registered");
        DAP_DELETE(l_http_server);
        return NULL;
    }

    log_it(L_INFO, "Created HTTP server: %s", a_server_name);
    return l_http_server;
}

/**
 * @brief Start HTTP server on specified addresses and ports
 */
int dap_net_trans_http_server_start(dap_net_trans_http_server_t *a_http_server,
                                        const char *a_cfg_section,
                                        const char **a_addrs,
                                        uint16_t *a_ports,
                                        size_t a_count)
{
    if (!a_http_server || !a_ports || a_count == 0) {
        log_it(L_ERROR, "Invalid parameters for HTTP server start");
        return -1;
    }

    if (a_http_server->server) {
        log_it(L_WARNING, "HTTP server already started");
        return -2;
    }

    // Initialize HTTP encryption adapter (required for enc_init handler)
    // This is idempotent, safe to call multiple times
    static bool s_enc_http_initialized = false;
    if (!s_enc_http_initialized) {
        int l_ret = enc_http_init();
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to initialize HTTP encryption adapter");
            return -3;
        }
        s_enc_http_initialized = true;
    }

    // Create HTTP server instance (returns dap_server_t *)
    dap_server_t *l_server = dap_http_server_new(NULL, a_http_server->server_name);
    if (!l_server) {
        log_it(L_ERROR, "Failed to create HTTP server");
        return -4;
    }

    // Get HTTP server structure from dap_server_t using macro
    dap_http_server_t *l_http_server = DAP_HTTP_SERVER(l_server);
    if (!l_http_server) {
        log_it(L_ERROR, "Failed to get HTTP server structure");
        dap_server_delete(l_server);
        return -5;
    }

    // Store references
    a_http_server->server = l_server;
    a_http_server->http_server = l_http_server;

    // DO NOT overwrite _inheritor - it must remain as dap_http_server_t
    // for DAP_HTTP_SERVER macro to work correctly
    // a_http_server->server->_inheritor = a_http_server;  // WRONG - overwrites dap_http_server_t!

    // Create trans ctx for handler registration
    dap_net_trans_server_ctx_t *l_ctx = dap_net_trans_server_ctx_from_http(
        l_http_server, DAP_NET_TRANS_HTTP, NULL);
    if (!l_ctx) {
        log_it(L_ERROR, "Failed to create trans ctx");
        dap_server_delete(l_server);
        a_http_server->server = NULL;
        a_http_server->http_server = NULL;
        return -6;
    }

    // Register all DAP protocol handlers (enc_init, stream, stream_ctl)
    int l_ret = dap_net_trans_server_register_handlers(l_ctx);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register DAP protocol handlers");
        dap_net_trans_server_ctx_delete(l_ctx);
        dap_server_delete(l_server);
        a_http_server->server = NULL;
        a_http_server->http_server = NULL;
        return -7;
    }

    // Delete trans ctx (handlers are already registered)
    dap_net_trans_server_ctx_delete(l_ctx);

    log_it(L_DEBUG, "Registered all DAP protocol handlers for HTTP server");

    // Start listening on all specified address:port pairs using common accept callback
    for (size_t i = 0; i < a_count; i++) {
        const char *l_addr = (a_addrs && a_addrs[i]) ? a_addrs[i] : "0.0.0.0";
        uint16_t l_port = a_ports[i];

        l_ret = dap_net_server_listen_addr_add_with_callback(a_http_server->server, l_addr, l_port,
                                                               DESCRIPTOR_TYPE_SOCKET_LISTENING,
                                                               NULL,  // No pre_worker_added callback needed
                                                               NULL);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to start HTTP server on %s:%u", l_addr, l_port);
            dap_net_trans_http_server_stop(a_http_server);
            return -8;
        }

        log_it(L_NOTICE, "HTTP server '%s' listening on %s:%u",
               a_http_server->server_name, l_addr, l_port);
    }

    return 0;
}

/**
 * @brief Stop HTTP server
 */
void dap_net_trans_http_server_stop(dap_net_trans_http_server_t *a_http_server)
{
    if (!a_http_server) {
        return;
    }

    if (a_http_server->server) {
        // Set delete callback for proper cleanup
        a_http_server->server->delete_callback = dap_http_delete;
        dap_server_delete(a_http_server->server);
        a_http_server->server = NULL;
        a_http_server->http_server = NULL;
    }

    log_it(L_INFO, "HTTP server '%s' stopped", a_http_server->server_name);
}

/**
 * @brief Delete HTTP server instance
 */
void dap_net_trans_http_server_delete(dap_net_trans_http_server_t *a_http_server)
{
    if (!a_http_server) {
        return;
    }

    // Ensure server is stopped before deletion
    dap_net_trans_http_server_stop(a_http_server);

    log_it(L_INFO, "Deleted HTTP server: %s", a_http_server->server_name);
    DAP_DELETE(a_http_server);
}

