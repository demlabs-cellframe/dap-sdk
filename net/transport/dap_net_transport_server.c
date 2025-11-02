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
#include "dap_net_transport_server.h"

// Include headers for handler registration
#include "dap_stream.h"
#include "dap_stream_ctl.h"
#include "dap_stream_transport.h"
#include "dap_enc_http.h"
#include "uthash.h"

#define LOG_TAG "dap_net_transport_server"

/**
 * @brief Registry entry for transport server operations
 */
typedef struct dap_net_transport_server_ops_entry {
    dap_stream_transport_type_t transport_type;
    const dap_net_transport_server_ops_t *ops;
    UT_hash_handle hh;
} dap_net_transport_server_ops_entry_t;

// Global registry for transport server operations
static dap_net_transport_server_ops_entry_t *s_ops_registry = NULL;

/**
 * @brief Register transport server operations for a transport type
 */
int dap_net_transport_server_register_ops(dap_stream_transport_type_t a_transport_type,
                                            const dap_net_transport_server_ops_t *a_ops)
{
    if (!a_ops || !a_ops->new || !a_ops->start || !a_ops->stop || !a_ops->delete) {
        log_it(L_ERROR, "Invalid operations structure for transport type %d", a_transport_type);
        return -1;
    }
    
    // Check if already registered
    dap_net_transport_server_ops_entry_t *l_entry = NULL;
    HASH_FIND_INT(s_ops_registry, &a_transport_type, l_entry);
    if (l_entry) {
        log_it(L_WARNING, "Transport server operations for type %d already registered, replacing", a_transport_type);
        l_entry->ops = a_ops;
        return 0;
    }
    
    // Create new entry
    l_entry = DAP_NEW_Z(dap_net_transport_server_ops_entry_t);
    if (!l_entry) {
        log_it(L_CRITICAL, "Failed to allocate memory for transport server operations entry");
        return -1;
    }
    
    l_entry->transport_type = a_transport_type;
    l_entry->ops = a_ops;
    
    HASH_ADD_INT(s_ops_registry, transport_type, l_entry);
    
    log_it(L_INFO, "Registered transport server operations for type %d (registry size: %u)", 
           a_transport_type, HASH_COUNT(s_ops_registry));
    log_it(L_DEBUG, "Registry after registration: ops->new=%p, ops->start=%p, ops->stop=%p, ops->delete=%p",
           (void*)a_ops->new, (void*)a_ops->start, (void*)a_ops->stop, (void*)a_ops->delete);
    return 0;
}

/**
 * @brief Unregister transport server operations for a transport type
 */
void dap_net_transport_server_unregister_ops(dap_stream_transport_type_t a_transport_type)
{
    dap_net_transport_server_ops_entry_t *l_entry = NULL;
    HASH_FIND_INT(s_ops_registry, &a_transport_type, l_entry);
    if (l_entry) {
        HASH_DEL(s_ops_registry, l_entry);
        DAP_DELETE(l_entry);
        log_it(L_DEBUG, "Unregistered transport server operations for type %d", a_transport_type);
    }
}

/**
 * @brief Get transport server operations for a transport type
 */
const dap_net_transport_server_ops_t *dap_net_transport_server_get_ops(dap_stream_transport_type_t a_transport_type)
{
    dap_net_transport_server_ops_entry_t *l_entry = NULL;
    HASH_FIND_INT(s_ops_registry, &a_transport_type, l_entry);
    if (l_entry) {
        log_it(L_DEBUG, "Found transport server operations for type %d", a_transport_type);
        return l_entry->ops;
    }
    log_it(L_ERROR, "Transport server operations NOT FOUND for type %d (registry size: %u)", 
           a_transport_type, HASH_COUNT(s_ops_registry));
    return NULL;
}

/**
 * @brief Create new transport server instance
 */
dap_net_transport_server_t *dap_net_transport_server_new(dap_stream_transport_type_t a_transport_type,
                                                          const char *a_server_name)
{
    if (!a_server_name) {
        log_it(L_ERROR, "Server name is NULL");
        return NULL;
    }

    // Get operations for this transport type
    const dap_net_transport_server_ops_t *l_ops = dap_net_transport_server_get_ops(a_transport_type);
    if (!l_ops) {
        log_it(L_ERROR, "Transport server operations not registered for type %d", a_transport_type);
        return NULL;
    }

    dap_net_transport_server_t *l_server = DAP_NEW_Z(dap_net_transport_server_t);
    if (!l_server) {
        log_it(L_CRITICAL, "Cannot allocate memory for transport server");
        return NULL;
    }

    l_server->transport_type = a_transport_type;
    dap_strncpy(l_server->server_name, a_server_name, sizeof(l_server->server_name) - 1);

    // Create transport-specific server instance using registered callback
    l_server->transport_specific = l_ops->new(a_server_name);
    if (!l_server->transport_specific) {
        log_it(L_ERROR, "Failed to create transport-specific server for type %d", a_transport_type);
        DAP_DELETE(l_server);
        return NULL;
    }

    log_it(L_INFO, "Created transport server: %s (type: %d)", a_server_name, a_transport_type);
    return l_server;
}

/**
 * @brief Start transport server on specified addresses and ports
 */
int dap_net_transport_server_start(dap_net_transport_server_t *a_server,
                                   const char *a_cfg_section,
                                   const char **a_addrs,
                                   uint16_t *a_ports,
                                   size_t a_count)
{
    if (!a_server || !a_ports || a_count == 0) {
        log_it(L_ERROR, "Invalid parameters for transport server start");
        return -1;
    }

    // Get operations for this transport type
    const dap_net_transport_server_ops_t *l_ops = dap_net_transport_server_get_ops(a_server->transport_type);
    if (!l_ops) {
        log_it(L_ERROR, "Transport server operations not registered for type %d", a_server->transport_type);
        return -1;
    }

    // Start transport-specific server using registered callback
    return l_ops->start(a_server->transport_specific, a_cfg_section, a_addrs, a_ports, a_count);
}

/**
 * @brief Stop transport server
 */
void dap_net_transport_server_stop(dap_net_transport_server_t *a_server)
{
    if (!a_server) {
        return;
    }

    // Get operations for this transport type
    const dap_net_transport_server_ops_t *l_ops = dap_net_transport_server_get_ops(a_server->transport_type);
    if (!l_ops) {
        log_it(L_WARNING, "Transport server operations not registered for type %d", a_server->transport_type);
        return;
    }

    // Stop transport-specific server using registered callback
    l_ops->stop(a_server->transport_specific);
}

/**
 * @brief Delete transport server instance
 */
void dap_net_transport_server_delete(dap_net_transport_server_t *a_server)
{
    if (!a_server) {
        return;
    }

    // Stop server first
    dap_net_transport_server_stop(a_server);

    // Get operations for this transport type
    const dap_net_transport_server_ops_t *l_ops = dap_net_transport_server_get_ops(a_server->transport_type);
    if (l_ops) {
        // Delete transport-specific server using registered callback
        l_ops->delete(a_server->transport_specific);
    } else {
        log_it(L_WARNING, "Transport server operations not registered for type %d, cannot delete", a_server->transport_type);
    }

    log_it(L_INFO, "Deleted transport server: %s", a_server->server_name);
    DAP_DELETE(a_server);
}

/**
 * @brief Get transport-specific server instance
 */
void *dap_net_transport_server_get_specific(dap_net_transport_server_t *a_server)
{
    if (!a_server) {
        return NULL;
    }
    return a_server->transport_specific;
}

/**
 * @brief Register all standard DAP protocol handlers on transport server
 */
int dap_net_transport_server_register_handlers(dap_net_transport_server_context_t *a_context)
{
    if (!a_context || !a_context->http_server) {
        log_it(L_ERROR, "Invalid transport server context");
        return -1;
    }

    log_it(L_DEBUG, "Registering DAP protocol handlers for transport type %d", a_context->transport_type);

    // Register enc_init handler (encryption handshake)
    // The client uses "enc_init/gd4y5yh78w42aaagh" path for enc_init requests
    // HTTP server parses URL and looks for processor by dirname first, then extracts basename
    // z_dirname() returns "/enc_init" for path "/enc_init/gd4y5yh78w42aaagh" (without trailing slash)
    // So we register processor for "/enc_init" directory path (without trailing slash)
    enc_http_add_proc(a_context->http_server, "/enc_init");
    log_it(L_DEBUG, "Registered enc_init handler (path: /enc_init)");

    // Register stream handler (DAP stream protocol)
    dap_stream_add_proc_http(a_context->http_server, "stream");
    log_it(L_DEBUG, "Registered stream handler");

    // Register stream_ctl handler (stream session control)
    dap_stream_ctl_add_proc(a_context->http_server, "stream_ctl");
    log_it(L_DEBUG, "Registered stream_ctl handler");

    // Register transport-specific handlers via transport's callback
    // Each transport registers its own handlers (e.g., WebSocket upgrade handlers)
    dap_stream_transport_t *l_stream_transport = dap_stream_transport_find(a_context->transport_type);
    if (l_stream_transport && l_stream_transport->ops && l_stream_transport->ops->register_server_handlers) {
        int l_ret = l_stream_transport->ops->register_server_handlers(l_stream_transport, a_context);
        if (l_ret != 0) {
            log_it(L_WARNING, "Transport '%s' failed to register server handlers: %d", 
                   l_stream_transport->name, l_ret);
            // Non-fatal, continue
        } else {
            log_it(L_DEBUG, "Registered transport-specific handlers for '%s'", l_stream_transport->name);
        }
    } else {
        log_it(L_DEBUG, "Transport type %d doesn't require server handler registration", 
               a_context->transport_type);
    }

    log_it(L_INFO, "Registered all DAP protocol handlers for transport type %d", a_context->transport_type);
    return 0;
}

/**
 * @brief Register custom encrypted request handler
 */
int dap_net_transport_server_register_enc_custom(dap_net_transport_server_context_t *a_context, const char *a_url_path)
{
    if (!a_context || !a_context->http_server || !a_url_path) {
        log_it(L_ERROR, "Invalid parameters for register_enc_custom");
        return -1;
    }

    // Register custom path through enc_http system
    enc_http_add_proc(a_context->http_server, a_url_path);
    log_it(L_INFO, "Registered custom encrypted request handler: %s", a_url_path);
    return 0;
}

/**
 * @brief Create transport server context from HTTP server
 */
dap_net_transport_server_context_t *dap_net_transport_server_context_from_http(dap_http_server_t *a_http_server,
                                                                                  dap_stream_transport_type_t a_transport_type,
                                                                                  void *a_transport_specific)
{
    if (!a_http_server) {
        log_it(L_ERROR, "HTTP server is NULL");
        return NULL;
    }

    dap_net_transport_server_context_t *l_context = DAP_NEW_Z(dap_net_transport_server_context_t);
    if (!l_context) {
        log_it(L_CRITICAL, "Failed to allocate transport server context");
        return NULL;
    }

    l_context->transport_type = a_transport_type;
    l_context->http_server = a_http_server;
    l_context->server = a_http_server->server;
    l_context->transport_specific = a_transport_specific;

    log_it(L_DEBUG, "Created transport server context for type %d", a_transport_type);
    return l_context;
}

/**
 * @brief Delete transport server context
 */
void dap_net_transport_server_context_delete(dap_net_transport_server_context_t *a_context)
{
    if (!a_context) {
        return;
    }

    log_it(L_DEBUG, "Deleting transport server context for type %d", a_context->transport_type);
    DAP_DELETE(a_context);
}

