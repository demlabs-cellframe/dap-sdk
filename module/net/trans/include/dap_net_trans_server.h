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
 * @file dap_net_trans_server.h
 * @brief Unified API for creating and managing trans servers
 * @details Provides unified interface for creating servers for different trans types
 *          (HTTP, WebSocket, UDP, DNS, etc.) with consistent API.
 * 
 * @date 2025-11-02
 * @copyright (c) 2025 Cellframe Network
 */

#pragma once

#include "dap_net_trans.h"
#include "dap_http_server.h"
#include "dap_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Trans server operations callbacks
 * 
 * Each trans server implementation registers these callbacks
 * to provide unified interface for server management.
 */
typedef struct dap_net_trans_server_ops {
    /**
     * @brief Create new trans server instance
     * @param a_server_name Server name for identification
     * @return Trans-specific server instance or NULL on error
     */
    void* (*new)(const char *a_server_name);
    
    /**
     * @brief Start trans server
     * @param a_server Trans-specific server instance
     * @param a_cfg_section Configuration section name
     * @param a_addrs Array of addresses (can be NULL)
     * @param a_ports Array of ports
     * @param a_count Number of addresses/ports
     * @return 0 on success, negative error code on failure
     */
    int (*start)(void *a_server, const char *a_cfg_section, 
                 const char **a_addrs, uint16_t *a_ports, size_t a_count);
    
    /**
     * @brief Stop trans server
     * @param a_server Trans-specific server instance
     */
    void (*stop)(void *a_server);
    
    /**
     * @brief Delete trans server instance
     * @param a_server Trans-specific server instance
     */
    void (*delete)(void *a_server);
} dap_net_trans_server_ops_t;

/**
 * @brief Register trans server operations for a trans type
 * 
 * Each trans server implementation should call this during initialization
 * to register its operations callbacks.
 * 
 * @param a_trans_type Trans type
 * @param a_ops Operations callbacks structure (must remain valid)
 * @return 0 on success, negative error code on failure
 */
int dap_net_trans_server_register_ops(dap_net_trans_type_t a_trans_type,
                                            const dap_net_trans_server_ops_t *a_ops);

/**
 * @brief Unregister trans server operations for a trans type
 * 
 * @param a_trans_type Trans type
 */
void dap_net_trans_server_unregister_ops(dap_net_trans_type_t a_trans_type);

/**
 * @brief Get trans server operations for a trans type
 * 
 * @param a_trans_type Trans type
 * @return Operations structure or NULL if not registered
 */
const dap_net_trans_server_ops_t *dap_net_trans_server_get_ops(dap_net_trans_type_t a_trans_type);

/**
 * @brief Trans server ctx for handler registration
 * 
 * This structure encapsulates trans-specific server instance
 * and provides unified interface for handler registration.
 * Used internally by dap_net_trans_server functions.
 */
typedef struct dap_net_trans_server_ctx {
    dap_net_trans_type_t trans_type;  ///< Trans type
    dap_http_server_t *http_server;              ///< HTTP server (for HTTP/WebSocket transs)
    dap_server_t *server;                        ///< Underlying server instance
    void *trans_specific;                    ///< Trans-specific data (e.g., websocket server instance)
} dap_net_trans_server_ctx_t;

/**
 * @brief Unified trans server structure
 * 
 * This structure provides a unified interface for all trans server types.
 * The actual server implementation is stored in trans_specific field.
 */
typedef struct dap_net_trans_server {
    dap_net_trans_type_t trans_type;  ///< Trans type
    void *trans_specific;                     ///< Trans-specific server instance
    char server_name[256];                        ///< Server name for identification
} dap_net_trans_server_t;

/**
 * @brief Create new trans server instance
 * 
 * Creates a server instance for the specified trans type.
 * The actual server implementation is automatically selected based on trans_type.
 * 
 * @param a_trans_type Trans type (HTTP, WebSocket, UDP, DNS, etc.)
 * @param a_server_name Server name for identification
 * @return Pointer to dap_net_trans_server_t instance or NULL on error
 */
dap_net_trans_server_t *dap_net_trans_server_new(dap_net_trans_type_t a_trans_type,
                                                          const char *a_server_name);

/**
 * @brief Start trans server on specified addresses and ports
 * 
 * Starts the server on all specified address:port pairs.
 * For HTTP/WebSocket transs, this also initializes enc_http and registers
 * all DAP protocol handlers automatically.
 * 
 * @param a_server Trans server instance
 * @param a_cfg_section Configuration section name for dap_server
 * @param a_addrs Array of addresses (can be NULL for INADDR_ANY)
 * @param a_ports Array of ports
 * @param a_count Number of addresses/ports in arrays
 * @return 0 if success, negative error code otherwise
 */
int dap_net_trans_server_start(dap_net_trans_server_t *a_server,
                                   const char *a_cfg_section,
                                   const char **a_addrs,
                                   uint16_t *a_ports,
                                   size_t a_count);

/**
 * @brief Stop trans server and cleanup resources
 * 
 * @param a_server Trans server instance
 */
void dap_net_trans_server_stop(dap_net_trans_server_t *a_server);

/**
 * @brief Delete trans server instance
 * 
 * Frees dap_net_trans_server_t structure. Call dap_net_trans_server_stop()
 * first to cleanup server resources.
 * 
 * @param a_server Trans server instance
 */
void dap_net_trans_server_delete(dap_net_trans_server_t *a_server);

/**
 * @brief Get trans-specific server instance
 * 
 * Helper function to get trans-specific server instance from unified structure.
 * 
 * @param a_server Unified trans server instance
 * @return Trans-specific server instance or NULL
 */
void *dap_net_trans_server_get_specific(dap_net_trans_server_t *a_server);

/**
 * @brief Register all standard DAP protocol handlers on trans server
 * 
 * This function automatically registers all required handlers:
 * - enc_init: Encryption handshake endpoint
 * - stream: DAP stream protocol endpoint
 * - stream_ctl: Stream session control endpoint
 * 
 * It also registers trans-specific handlers (e.g., WebSocket upgrade for stream).
 * 
 * @param a_ctx Trans server ctx (created from HTTP server or trans server)
 * @return 0 on success, negative on error
 */
int dap_net_trans_server_register_handlers(dap_net_trans_server_ctx_t *a_ctx);

/**
 * @brief Register custom encrypted request handler
 * 
 * Registers a custom path for encrypted requests. The path will be processed
 * through enc_http system, allowing encrypted URL paths and request bodies.
 * 
 * @param a_ctx Trans server ctx
 * @param a_url_path URL path for encrypted requests (e.g., "custom_api", "license")
 * @return 0 on success, negative on error
 */
int dap_net_trans_server_register_enc_custom(dap_net_trans_server_ctx_t *a_ctx, const char *a_url_path);

/**
 * @brief Create trans server ctx from HTTP server
 * 
 * Helper function to create trans ctx from HTTP server.
 * Used for HTTP and WebSocket transs.
 * 
 * @param a_http_server HTTP server instance
 * @param a_trans_type Trans type (from dap_stream_trans_type_t)
 * @param a_trans_specific Trans-specific data (optional)
 * @return Trans server ctx or NULL on error
 */
dap_net_trans_server_ctx_t *dap_net_trans_server_ctx_from_http(dap_http_server_t *a_http_server,
                                                                                  dap_net_trans_type_t a_trans_type,
                                                                                  void *a_trans_specific);

/**
 * @brief Delete trans server ctx
 * 
 * @param a_ctx Trans server ctx (does not free underlying server)
 */
void dap_net_trans_server_ctx_delete(dap_net_trans_server_ctx_t *a_ctx);

#ifdef __cplusplus
}
#endif

