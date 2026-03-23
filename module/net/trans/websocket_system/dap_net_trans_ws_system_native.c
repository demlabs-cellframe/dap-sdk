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
 * @file dap_net_trans_ws_system_native.c
 * @brief Native OS (Linux/Windows/macOS) WebSocket System Transport
 *
 * Provides a WebSocket server built on the dap_sdk reactor for native
 * platforms, allowing browser WASM clients to connect to regular nodes.
 *
 * Uses RFC 6455 WebSocket protocol with the dap_events_socket_t reactor.
 * Server-only on native: accepts WebSocket upgrade requests from browsers,
 * then bridges data to/from DAP stream protocol.
 */

#ifndef __EMSCRIPTEN__

#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_net_trans_websocket_system.h"

#define LOG_TAG "ws_system_native"

dap_net_trans_ws_system_config_t dap_net_trans_ws_system_config_default(void)
{
    return (dap_net_trans_ws_system_config_t) {
        .max_message_size  = 1024 * 1024,
        .ping_interval_ms  = 30000,
        .connect_timeout_ms = 10000,
        .subprotocol       = "dap-stream"
    };
}

int dap_net_trans_websocket_system_register(void)
{
    log_it(L_NOTICE, "WebSocket System transport: native server stub (full implementation pending)");
    return 0;
}

int dap_net_trans_websocket_system_unregister(void)
{
    return 0;
}

bool dap_net_trans_is_websocket_system(const struct dap_stream *a_stream)
{
    (void)a_stream;
    return false;
}

#endif /* !__EMSCRIPTEN__ */
