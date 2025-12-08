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
#include <stdlib.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_net_trans_server.h"

// Include headers for handler registration
#include "dap_stream.h"
#include "dap_stream_ctl.h"
#include "dap_net_trans.h"
#include "dap_enc_http.h"
#include "uthash.h"

#define LOG_TAG "dap_net_trans_server"

/**
 * @brief Registry entry for trans server operations
 */
typedef struct dap_net_trans_server_ops_entry {
    dap_net_trans_type_t trans_type;
    const dap_net_trans_server_ops_t *ops;
    UT_hash_handle hh;
} dap_net_trans_server_ops_entry_t;

// Global registry for trans server operations
static dap_net_trans_server_ops_entry_t *s_ops_registry = NULL;

/**
 * @brief Register trans server operations for a trans type
 */
int dap_net_trans_server_register_ops(dap_net_trans_type_t a_trans_type,
                                            const dap_net_trans_server_ops_t *a_ops)
{
    if (!a_ops || !a_ops->new || !a_ops->start || !a_ops->stop || !a_ops->delete) {
        log_it(L_ERROR, "Invalid operations structure for trans type %d", a_trans_type);
        return -1;
    }
    
    // Check if already registered
    dap_net_trans_server_ops_entry_t *l_entry = NULL;
    HASH_FIND_INT(s_ops_registry, &a_trans_type, l_entry);
    if (l_entry) {
        log_it(L_WARNING, "Trans server operations for type %d already registered, replacing", a_trans_type);
        l_entry->ops = a_ops;
        return 0;
    }
    
    // Create new entry
    l_entry = DAP_NEW_Z(dap_net_trans_server_ops_entry_t);
    if (!l_entry) {
        log_it(L_CRITICAL, "Failed to allocate memory for trans server operations entry");
        return -1;
    }
    
    l_entry->trans_type = a_trans_type;
    l_entry->ops = a_ops;
    
    HASH_ADD_INT(s_ops_registry, trans_type, l_entry);
    
    log_it(L_INFO, "Registered trans server operations for type %d (registry size: %u)", 
           a_trans_type, HASH_COUNT(s_ops_registry));
    log_it(L_DEBUG, "Registry after registration: ops->new=%p, ops->start=%p, ops->stop=%p, ops->delete=%p",
           (void*)a_ops->new, (void*)a_ops->start, (void*)a_ops->stop, (void*)a_ops->delete);
    return 0;
}

/**
 * @brief Unregister trans server operations for a trans type
 */
void dap_net_trans_server_unregister_ops(dap_net_trans_type_t a_trans_type)
{
    dap_net_trans_server_ops_entry_t *l_entry = NULL;
    HASH_FIND_INT(s_ops_registry, &a_trans_type, l_entry);
    if (l_entry) {
        HASH_DEL(s_ops_registry, l_entry);
        DAP_DELETE(l_entry);
        log_it(L_DEBUG, "Unregistered trans server operations for type %d", a_trans_type);
    }
}

/**
 * @brief Get trans server operations for a trans type
 */
const dap_net_trans_server_ops_t *dap_net_trans_server_get_ops(dap_net_trans_type_t a_trans_type)
{
    dap_net_trans_server_ops_entry_t *l_entry = NULL;
    HASH_FIND_INT(s_ops_registry, &a_trans_type, l_entry);
    if (l_entry) {
        log_it(L_DEBUG, "Found trans server operations for type %d", a_trans_type);
        return l_entry->ops;
    }
    log_it(L_ERROR, "Trans server operations NOT FOUND for type %d (registry size: %u)", 
           a_trans_type, HASH_COUNT(s_ops_registry));
    return NULL;
}

/**
 * @brief Create new trans server instance
 */
dap_net_trans_server_t *dap_net_trans_server_new(dap_net_trans_type_t a_trans_type,
                                                          const char *a_server_name)
{
    if (!a_server_name) {
        log_it(L_ERROR, "Server name is NULL");
        return NULL;
    }

    // Get operations for this trans type
    const dap_net_trans_server_ops_t *l_ops = dap_net_trans_server_get_ops(a_trans_type);
    if (!l_ops) {
        log_it(L_ERROR, "Trans server operations not registered for type %d", a_trans_type);
        return NULL;
    }

    dap_net_trans_server_t *l_server = DAP_NEW_Z(dap_net_trans_server_t);
    if (!l_server) {
        log_it(L_CRITICAL, "Cannot allocate memory for trans server");
        return NULL;
    }

    l_server->trans_type = a_trans_type;
    dap_strncpy(l_server->server_name, a_server_name, sizeof(l_server->server_name) - 1);

    // Create trans-specific server instance using registered callback
    l_server->trans_specific = l_ops->new(a_server_name);
    if (!l_server->trans_specific) {
        log_it(L_ERROR, "Failed to create trans-specific server for type %d", a_trans_type);
        DAP_DELETE(l_server);
        return NULL;
    }

    log_it(L_INFO, "Created trans server: %s (type: %d)", a_server_name, a_trans_type);
    return l_server;
}

/**
 * @brief Start trans server on specified addresses and ports
 */
int dap_net_trans_server_start(dap_net_trans_server_t *a_server,
                                   const char *a_cfg_section,
                                   const char **a_addrs,
                                   uint16_t *a_ports,
                                   size_t a_count)
{
    if (!a_server || !a_ports || a_count == 0) {
        log_it(L_ERROR, "Invalid parameters for trans server start");
        return -1;
    }

    // Get operations for this trans type
    const dap_net_trans_server_ops_t *l_ops = dap_net_trans_server_get_ops(a_server->trans_type);
    if (!l_ops) {
        log_it(L_ERROR, "Trans server operations not registered for type %d", a_server->trans_type);
        return -1;
    }

    // Start trans-specific server using registered callback
    return l_ops->start(a_server->trans_specific, a_cfg_section, a_addrs, a_ports, a_count);
}

/**
 * @brief Stop trans server
 */
void dap_net_trans_server_stop(dap_net_trans_server_t *a_server)
{
    if (!a_server) {
        return;
    }

    // Get operations for this trans type
    const dap_net_trans_server_ops_t *l_ops = dap_net_trans_server_get_ops(a_server->trans_type);
    if (!l_ops) {
        log_it(L_WARNING, "Trans server operations not registered for type %d", a_server->trans_type);
        return;
    }

    // Stop trans-specific server using registered callback
    l_ops->stop(a_server->trans_specific);
}

/**
 * @brief Delete trans server instance
 */
void dap_net_trans_server_delete(dap_net_trans_server_t *a_server)
{
    if (!a_server) {
        return;
    }

    // Stop server first
    dap_net_trans_server_stop(a_server);

    // Get operations for this trans type
    const dap_net_trans_server_ops_t *l_ops = dap_net_trans_server_get_ops(a_server->trans_type);
    if (l_ops) {
        // Delete trans-specific server using registered callback
        l_ops->delete(a_server->trans_specific);
    } else {
        log_it(L_WARNING, "Trans server operations not registered for type %d, cannot delete", a_server->trans_type);
    }

    log_it(L_INFO, "Deleted trans server: %s", a_server->server_name);
    DAP_DELETE(a_server);
}

/**
 * @brief Get trans-specific server instance
 */
void *dap_net_trans_server_get_specific(dap_net_trans_server_t *a_server)
{
    if (!a_server) {
        return NULL;
    }
    return a_server->trans_specific;
}

/**
 * @brief Register all standard DAP protocol handlers on trans server
 */
int dap_net_trans_server_register_handlers(dap_net_trans_server_ctx_t *a_ctx)
{
    if (!a_ctx || !a_ctx->http_server) {
        log_it(L_ERROR, "Invalid trans server ctx");
        return -1;
    }

    log_it(L_DEBUG, "Registering DAP protocol handlers for trans type %d", a_ctx->trans_type);

    // Register enc_init handler (encryption handshake)
    // The client uses "enc_init/gd4y5yh78w42aaagh" path for enc_init requests
    // HTTP server parses URL and looks for processor by dirname first, then extracts basename
    // z_dirname() returns "/enc_init" for path "/enc_init/gd4y5yh78w42aaagh" (without trailing slash)
    // So we register processor for "/enc_init" directory path (without trailing slash)
    enc_http_add_proc(a_ctx->http_server, "/enc_init");
    log_it(L_DEBUG, "Registered enc_init handler (path: /enc_init)");

    // Register stream handler (DAP stream protocol)
    dap_stream_add_proc_http(a_ctx->http_server, "/stream");
    log_it(L_DEBUG, "Registered stream handler");

    // Register stream_ctl handler (stream session control)
    // The client uses "stream_ctl/..." path for stream_ctl requests
    // HTTP server parses URL and looks for processor by dirname first, then extracts basename
    // z_dirname() returns "/stream_ctl" for path "/stream_ctl/..." (without trailing slash)
    // So we register processor for "/stream_ctl" directory path (without trailing slash)
    dap_stream_ctl_add_proc(a_ctx->http_server, "/stream_ctl");
    log_it(L_DEBUG, "Registered stream_ctl handler");

    // Register trans-specific handlers via trans's callback
    // Each trans registers its own handlers (e.g., WebSocket upgrade handlers)
    dap_net_trans_t *l_stream_trans = dap_net_trans_find(a_ctx->trans_type);
    if (l_stream_trans && l_stream_trans->ops && l_stream_trans->ops->register_server_handlers) {
        int l_ret = l_stream_trans->ops->register_server_handlers(l_stream_trans, a_ctx);
        if (l_ret != 0) {
            log_it(L_WARNING, "Trans '%s' failed to register server handlers: %d", 
                   l_stream_trans->name, l_ret);
            // Non-fatal, continue
        } else {
            log_it(L_DEBUG, "Registered trans-specific handlers for '%s'", l_stream_trans->name);
        }
    } else {
        log_it(L_DEBUG, "Trans type %d doesn't require server handler registration", 
               a_ctx->trans_type);
    }

    log_it(L_INFO, "Registered all DAP protocol handlers for trans type %d", a_ctx->trans_type);
    return 0;
}

/**
 * @brief Register custom encrypted request handler
 */
int dap_net_trans_server_register_enc_custom(dap_net_trans_server_ctx_t *a_ctx, const char *a_url_path)
{
    if (!a_ctx || !a_ctx->http_server || !a_url_path) {
        log_it(L_ERROR, "Invalid parameters for register_enc_custom");
        return -1;
    }

    // Register custom path through enc_http system
    enc_http_add_proc(a_ctx->http_server, a_url_path);
    log_it(L_INFO, "Registered custom encrypted request handler: %s", a_url_path);
    return 0;
}

/**
 * @brief Create trans server ctx from HTTP server
 */
dap_net_trans_server_ctx_t *dap_net_trans_server_ctx_from_http(dap_http_server_t *a_http_server,
                                                                                  dap_net_trans_type_t a_trans_type,
                                                                                  void *a_trans_specific)
{
    if (!a_http_server) {
        log_it(L_ERROR, "HTTP server is NULL");
        return NULL;
    }

    dap_net_trans_server_ctx_t *l_ctx = DAP_NEW_Z(dap_net_trans_server_ctx_t);
    if (!l_ctx) {
        log_it(L_CRITICAL, "Failed to allocate trans server ctx");
        return NULL;
    }

    l_ctx->trans_type = a_trans_type;
    l_ctx->http_server = a_http_server;
    l_ctx->server = a_http_server->server;
    l_ctx->trans_specific = a_trans_specific;

    log_it(L_DEBUG, "Created trans server ctx for type %d", a_trans_type);
    return l_ctx;
}

/**
 * @brief Delete trans server ctx
 */
void dap_net_trans_server_ctx_delete(dap_net_trans_server_ctx_t *a_ctx)
{
    if (!a_ctx) {
        return;
    }

    log_it(L_DEBUG, "Deleting trans server ctx for type %d", a_ctx->trans_type);
    DAP_DELETE(a_ctx);
}

