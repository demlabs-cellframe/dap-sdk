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

/**
 * @file dap_net_transport_server.h
 * @brief Unified API for creating and managing transport servers
 * @details Provides unified interface for creating servers for different transport types
 *          (HTTP, WebSocket, UDP, DNS, etc.) with consistent API.
 * 
 * @date 2025-11-02
 * @copyright (c) 2025 Cellframe Network
 */

#pragma once

#include "dap_net_transport.h"
#include "dap_http_server.h"
#include "dap_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transport server operations callbacks
 * 
 * Each transport server implementation registers these callbacks
 * to provide unified interface for server management.
 */
typedef struct dap_net_transport_server_ops {
    /**
     * @brief Create new transport server instance
     * @param a_server_name Server name for identification
     * @return Transport-specific server instance or NULL on error
     */
    void* (*new)(const char *a_server_name);
    
    /**
     * @brief Start transport server
     * @param a_server Transport-specific server instance
     * @param a_cfg_section Configuration section name
     * @param a_addrs Array of addresses (can be NULL)
     * @param a_ports Array of ports
     * @param a_count Number of addresses/ports
     * @return 0 on success, negative error code on failure
     */
    int (*start)(void *a_server, const char *a_cfg_section, 
                 const char **a_addrs, uint16_t *a_ports, size_t a_count);
    
    /**
     * @brief Stop transport server
     * @param a_server Transport-specific server instance
     */
    void (*stop)(void *a_server);
    
    /**
     * @brief Delete transport server instance
     * @param a_server Transport-specific server instance
     */
    void (*delete)(void *a_server);
} dap_net_transport_server_ops_t;

/**
 * @brief Register transport server operations for a transport type
 * 
 * Each transport server implementation should call this during initialization
 * to register its operations callbacks.
 * 
 * @param a_transport_type Transport type
 * @param a_ops Operations callbacks structure (must remain valid)
 * @return 0 on success, negative error code on failure
 */
int dap_net_transport_server_register_ops(dap_net_transport_type_t a_transport_type,
                                            const dap_net_transport_server_ops_t *a_ops);

/**
 * @brief Unregister transport server operations for a transport type
 * 
 * @param a_transport_type Transport type
 */
void dap_net_transport_server_unregister_ops(dap_net_transport_type_t a_transport_type);

/**
 * @brief Get transport server operations for a transport type
 * 
 * @param a_transport_type Transport type
 * @return Operations structure or NULL if not registered
 */
const dap_net_transport_server_ops_t *dap_net_transport_server_get_ops(dap_net_transport_type_t a_transport_type);

/**
 * @brief Transport server context for handler registration
 * 
 * This structure encapsulates transport-specific server instance
 * and provides unified interface for handler registration.
 * Used internally by dap_net_transport_server functions.
 */
typedef struct dap_net_transport_server_context {
    dap_net_transport_type_t transport_type;  ///< Transport type
    dap_http_server_t *http_server;              ///< HTTP server (for HTTP/WebSocket transports)
    dap_server_t *server;                        ///< Underlying server instance
    void *transport_specific;                    ///< Transport-specific data (e.g., websocket server instance)
} dap_net_transport_server_context_t;

/**
 * @brief Unified transport server structure
 * 
 * This structure provides a unified interface for all transport server types.
 * The actual server implementation is stored in transport_specific field.
 */
typedef struct dap_net_transport_server {
    dap_net_transport_type_t transport_type;  ///< Transport type
    void *transport_specific;                     ///< Transport-specific server instance
    char server_name[256];                        ///< Server name for identification
} dap_net_transport_server_t;

/**
 * @brief Create new transport server instance
 * 
 * Creates a server instance for the specified transport type.
 * The actual server implementation is automatically selected based on transport_type.
 * 
 * @param a_transport_type Transport type (HTTP, WebSocket, UDP, DNS, etc.)
 * @param a_server_name Server name for identification
 * @return Pointer to dap_net_transport_server_t instance or NULL on error
 */
dap_net_transport_server_t *dap_net_transport_server_new(dap_net_transport_type_t a_transport_type,
                                                          const char *a_server_name);

/**
 * @brief Start transport server on specified addresses and ports
 * 
 * Starts the server on all specified address:port pairs.
 * For HTTP/WebSocket transports, this also initializes enc_http and registers
 * all DAP protocol handlers automatically.
 * 
 * @param a_server Transport server instance
 * @param a_cfg_section Configuration section name for dap_server
 * @param a_addrs Array of addresses (can be NULL for INADDR_ANY)
 * @param a_ports Array of ports
 * @param a_count Number of addresses/ports in arrays
 * @return 0 if success, negative error code otherwise
 */
int dap_net_transport_server_start(dap_net_transport_server_t *a_server,
                                   const char *a_cfg_section,
                                   const char **a_addrs,
                                   uint16_t *a_ports,
                                   size_t a_count);

/**
 * @brief Stop transport server and cleanup resources
 * 
 * @param a_server Transport server instance
 */
void dap_net_transport_server_stop(dap_net_transport_server_t *a_server);

/**
 * @brief Delete transport server instance
 * 
 * Frees dap_net_transport_server_t structure. Call dap_net_transport_server_stop()
 * first to cleanup server resources.
 * 
 * @param a_server Transport server instance
 */
void dap_net_transport_server_delete(dap_net_transport_server_t *a_server);

/**
 * @brief Get transport-specific server instance
 * 
 * Helper function to get transport-specific server instance from unified structure.
 * 
 * @param a_server Unified transport server instance
 * @return Transport-specific server instance or NULL
 */
void *dap_net_transport_server_get_specific(dap_net_transport_server_t *a_server);

/**
 * @brief Register all standard DAP protocol handlers on transport server
 * 
 * This function automatically registers all required handlers:
 * - enc_init: Encryption handshake endpoint
 * - stream: DAP stream protocol endpoint
 * - stream_ctl: Stream session control endpoint
 * 
 * It also registers transport-specific handlers (e.g., WebSocket upgrade for stream).
 * 
 * @param a_context Transport server context (created from HTTP server or transport server)
 * @return 0 on success, negative on error
 */
int dap_net_transport_server_register_handlers(dap_net_transport_server_context_t *a_context);

/**
 * @brief Register custom encrypted request handler
 * 
 * Registers a custom path for encrypted requests. The path will be processed
 * through enc_http system, allowing encrypted URL paths and request bodies.
 * 
 * @param a_context Transport server context
 * @param a_url_path URL path for encrypted requests (e.g., "custom_api", "license")
 * @return 0 on success, negative on error
 */
int dap_net_transport_server_register_enc_custom(dap_net_transport_server_context_t *a_context, const char *a_url_path);

/**
 * @brief Create transport server context from HTTP server
 * 
 * Helper function to create transport context from HTTP server.
 * Used for HTTP and WebSocket transports.
 * 
 * @param a_http_server HTTP server instance
 * @param a_transport_type Transport type (from dap_stream_transport_type_t)
 * @param a_transport_specific Transport-specific data (optional)
 * @return Transport server context or NULL on error
 */
dap_net_transport_server_context_t *dap_net_transport_server_context_from_http(dap_http_server_t *a_http_server,
                                                                                  dap_net_transport_type_t a_transport_type,
                                                                                  void *a_transport_specific);

/**
 * @brief Delete transport server context
 * 
 * @param a_context Transport server context (does not free underlying server)
 */
void dap_net_transport_server_context_delete(dap_net_transport_server_context_t *a_context);

#ifdef __cplusplus
}
#endif

