/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file dap_net_trans_websocket_system.h
 * @brief System WebSocket Transport for WASM/Browser environments
 *
 * Unlike DAP_NET_TRANS_WEBSOCKET which implements the WebSocket protocol
 * manually on top of raw TCP, this transport uses the browser's native
 * WebSocket API. This is the only way to do real network I/O from WASM.
 *
 * Architecture (WASM):
 * ```
 * DAP Stream
 *     |
 * Trans Abstraction Layer
 *     |
 * WebSocket System Trans  <- This module
 *     |
 * EM_JS -> JavaScript WebSocket API
 *     |
 * Browser Network Stack
 * ```
 *
 * On native platforms (Linux/Windows/macOS), this module provides a
 * WebSocket server implementation built on top of the dap_sdk reactor,
 * allowing browser clients to connect to regular nodes.
 *
 * @see dap_net_trans.h
 * @see RFC 6455 - The WebSocket Protocol
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dap_net_trans;
struct dap_stream;

typedef enum dap_ws_system_state {
    DAP_WS_SYSTEM_STATE_CONNECTING  = 0,
    DAP_WS_SYSTEM_STATE_OPEN        = 1,
    DAP_WS_SYSTEM_STATE_CLOSING     = 2,
    DAP_WS_SYSTEM_STATE_CLOSED      = 3
} dap_ws_system_state_t;

typedef struct dap_net_trans_ws_system_config {
    uint32_t max_message_size;       ///< Max inbound message size (bytes), 0 = default
    uint32_t ping_interval_ms;       ///< Ping interval (ms), 0 = disabled
    uint32_t connect_timeout_ms;     ///< Connection timeout (ms), 0 = default
    const char *subprotocol;         ///< WebSocket subprotocol (e.g., "dap-stream")
} dap_net_trans_ws_system_config_t;

/**
 * @brief Register WebSocket System transport
 * @return 0 on success, negative on failure
 */
int dap_net_trans_websocket_system_register(void);

/**
 * @brief Unregister WebSocket System transport
 * @return 0 on success, negative on failure
 */
int dap_net_trans_websocket_system_unregister(void);

/**
 * @brief Get default configuration
 * @return Default config struct
 */
dap_net_trans_ws_system_config_t dap_net_trans_ws_system_config_default(void);

/**
 * @brief Check if a stream uses this transport
 */
bool dap_net_trans_is_websocket_system(const struct dap_stream *a_stream);

#ifdef __cplusplus
}
#endif
