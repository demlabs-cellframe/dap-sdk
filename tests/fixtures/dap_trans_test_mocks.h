/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 *
 * This file is part of DAP the open source project
 *
 *    DAP is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file dap_trans_test_mocks.h
 * @brief Common mocks for trans unit tests
 * 
 * Provides standard mock declarations and wrappers for trans tests.
 * Used by HTTP, WebSocket, UDP, and DNS trans unit tests.
 * 
 * @date 2025-11-07
 */

#pragma once

#include "dap_mock.h"
#include "dap_mock_linker_wrapper.h"
#include "dap_common.h"
#include "dap_server.h"
#include "dap_http_server.h"
#include "dap_net_trans.h"
#include "dap_stream.h"
#include "dap_net_server_common.h"
#include "dap_events_socket.h"
#include "dap_client.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Common Mock Declarations for Trans Tests
// ============================================================================

// Mock dap_events functions
// dap_events_init and dap_events_start need passthrough to real function (event system initialization required)
DAP_MOCK_DECLARE(dap_events_init, DAP_MOCK_CONFIG_PASSTHROUGH);
DAP_MOCK_DECLARE(dap_events_start, DAP_MOCK_CONFIG_PASSTHROUGH);
DAP_MOCK_DECLARE(dap_events_stop_all);
DAP_MOCK_DECLARE(dap_events_deinit);

// Mock dap_server functions
DAP_MOCK_DECLARE(dap_server_create);
DAP_MOCK_DECLARE(dap_server_new);
DAP_MOCK_DECLARE(dap_server_listen_addr_add);
DAP_MOCK_DECLARE(dap_server_delete);

// Mock dap_net_server_common functions
// Note: dap_net_server_listen_addr_add_with_callback is NOT mocked - using real implementation

// Mock dap_http_server functions
DAP_MOCK_DECLARE(dap_http_server_new);

// Mock enc_http functions
DAP_MOCK_DECLARE(enc_http_init);
DAP_MOCK_DECLARE(enc_http_deinit);
DAP_MOCK_DECLARE(enc_http_add_proc);

// Mock dap_stream functions
DAP_MOCK_DECLARE(dap_stream_add_proc_http);
DAP_MOCK_DECLARE(dap_stream_ctl_add_proc);
DAP_MOCK_DECLARE(dap_stream_delete);
DAP_MOCK_DECLARE(dap_stream_init);
DAP_MOCK_DECLARE(dap_stream_deinit);

// Mock dap_http_client functions
DAP_MOCK_DECLARE(dap_http_client_new);
DAP_MOCK_DECLARE(dap_http_client_delete);
DAP_MOCK_DECLARE(dap_http_client_connect);
DAP_MOCK_DECLARE(dap_http_client_write);

// Mock dap_http functions
DAP_MOCK_DECLARE(dap_http_init);
DAP_MOCK_DECLARE(dap_http_deinit);

// Mock dap_enc functions (needed for session_create encryption)
DAP_MOCK_DECLARE(dap_enc_code_out_size);
DAP_MOCK_DECLARE(dap_enc_code);

// ============================================================================
// Common Mock Server Instances
// ============================================================================

/**
 * @brief Get shared mock server instance
 * @return Pointer to static mock server
 */
dap_server_t* dap_trans_test_get_mock_server(void);

/**
 * @brief Get shared mock HTTP server instance
 * @return Pointer to static mock HTTP server
 */
dap_http_server_t* dap_trans_test_get_mock_http_server(void);

/**
 * @brief Get shared mock HTTP client instance
 * @return Pointer to static mock HTTP client
 */
dap_http_client_t* dap_trans_test_get_mock_http_client(void);
dap_events_socket_t* dap_trans_test_get_mock_esocket(void);
dap_client_t* dap_trans_test_get_mock_client(void);

#ifdef __cplusplus
}
#endif

