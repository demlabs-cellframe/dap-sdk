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
#include "dap_http_server.h"
#include "dap_net_transport.h"

/**
 * @brief WebSocket server structure
 * 
 * WebSocket server is built on top of HTTP server to handle
 * HTTP Upgrade requests and switch to WebSocket protocol.
 * 
 * This structure is stored in dap_server_t->_inheritor field,
 * similar to dap_http_server_t pattern.
 */
typedef struct dap_net_transport_websocket_server {
    dap_server_t *server;               ///< Back pointer to parent dap_server instance
    dap_http_server_t *http_server;     ///< HTTP server for upgrade handling
    char server_name[256];              ///< Server name for identification
    dap_net_transport_t *transport;  ///< WebSocket transport instance
} dap_net_transport_websocket_server_t;

#define DAP_NET_TRANSPORT_WEBSOCKET_SERVER(a) ((dap_net_transport_websocket_server_t *) (a)->_inheritor)

/**
 * @brief Initialize WebSocket server module
 * @return 0 if success, negative error code otherwise
 */
int dap_net_transport_websocket_server_init(void);

/**
 * @brief Deinitialize WebSocket server module
 */
void dap_net_transport_websocket_server_deinit(void);

/**
 * @brief Create new WebSocket server instance
 * 
 * Allocates dap_net_transport_websocket_server_t structure. Call dap_net_transport_websocket_server_start()
 * to create internal dap_server_t and start listening.
 * 
 * @param a_server_name Server name for identification
 * @return Pointer to dap_net_transport_websocket_server_t instance or NULL on error
 */
dap_net_transport_websocket_server_t *dap_net_transport_websocket_server_new(const char *a_server_name);

/**
 * @brief Start WebSocket server on specified addresses and ports
 * 
 * Creates internal dap_server_t and HTTP server, then starts listening
 * on all specified address:port pairs.
 * 
 * @param a_ws_server WebSocket server instance
 * @param a_cfg_section Configuration section name for dap_server
 * @param a_addrs Array of addresses (can be NULL for INADDR_ANY)
 * @param a_ports Array of ports
 * @param a_count Number of addresses/ports in arrays
 * @return 0 if success, negative error code otherwise
 */
int dap_net_transport_websocket_server_start(dap_net_transport_websocket_server_t *a_ws_server,
                                const char *a_cfg_section,
                                const char **a_addrs, 
                                uint16_t *a_ports, 
                                size_t a_count);

/**
 * @brief Stop WebSocket server and cleanup resources
 * 
 * @param a_ws_server WebSocket server instance
 */
void dap_net_transport_websocket_server_stop(dap_net_transport_websocket_server_t *a_ws_server);

/**
 * @brief Delete WebSocket server instance
 * 
 * Frees dap_net_transport_websocket_server_t structure. Call dap_net_transport_websocket_server_stop()
 * first to cleanup server resources.
 * 
 * @param a_ws_server WebSocket server instance
 */
void dap_net_transport_websocket_server_delete(dap_net_transport_websocket_server_t *a_ws_server);

/**
 * @brief Register WebSocket upgrade handler on a specific URL path
 * 
 * This function registers HTTP callbacks that handle WebSocket Upgrade requests
 * on the specified URL path. After successful upgrade (101 Switching Protocols),
 * the connection switches to WebSocket protocol.
 * 
 * @param a_ws_server WebSocket server instance
 * @param a_url_path URL path for WebSocket upgrade (e.g., "/stream" or "/ws")
 * @return 0 on success, negative error code on failure
 */
int dap_net_transport_websocket_server_add_upgrade_handler(dap_net_transport_websocket_server_t *a_ws_server, const char *a_url_path);

