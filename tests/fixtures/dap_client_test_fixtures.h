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
 * @file dap_client_test_fixtures.h
 * @brief Client and event system test fixtures for async operations
 * 
 * Provides intelligent waiting functions for testing DAP client initialization,
 * cleanup, and event system state. Uses dap_test_wait_condition for async
 * state verification instead of fixed sleep delays.
 * 
 * @date 2025-10-30
 */

#pragma once

#include "dap_test_async.h"
#include "dap_client.h"
#include "dap_client_pvt.h"
#include "dap_events.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Client State Check Functions
// ============================================================================

/**
 * @brief Check if client is properly initialized
 * @details Verifies that:
 *   - Client structure exists
 *   - Internal structure (client_pvt) exists
 *   - Worker is assigned
 *   - Stage is STAGE_BEGIN
 *   - Stage status is STAGE_STATUS_COMPLETE
 * 
 * @param a_user_data Pointer to dap_client_t to check
 * @return true if client is properly initialized, false otherwise
 * 
 * @example
 * dap_test_async_config_t l_cfg = {
 *     .timeout_ms = 1000,
 *     .poll_interval_ms = 50,
 *     .fail_on_timeout = false,
 *     .operation_name = "client_initialization"
 * };
 * bool l_ready = dap_test_wait_condition(dap_test_client_check_initialized, client, &l_cfg);
 */
bool dap_test_client_check_initialized(void *a_user_data);

/**
 * @brief Check if client is ready for deletion (no active resources)
 * @details Verifies that:
 *   - No active stream
 *   - No active stream event socket
 *   - No active HTTP client
 *   - No reconnect timer
 * 
 * @note This should be called BEFORE dap_client_delete_unsafe
 * 
 * @param a_user_data Pointer to dap_client_t to check
 * @return true if client has no active resources, false otherwise
 * 
 * @example
 * dap_test_async_config_t l_cfg = {
 *     .timeout_ms = 2000,
 *     .poll_interval_ms = 50,
 *     .fail_on_timeout = false,
 *     .operation_name = "client_cleanup"
 * };
 * bool l_ready = dap_test_wait_condition(dap_test_client_check_ready_for_deletion, client, &l_cfg);
 */
bool dap_test_client_check_ready_for_deletion(void *a_user_data);

// ============================================================================
// Event System State Check Functions
// ============================================================================

/**
 * @brief Check if events system is ready to be deinitialized
 * @details After dap_events_stop_all() is called, this checks if
 *          workers have stopped and system is ready for deinit.
 * 
 * @param a_user_data Unused (can be NULL)
 * @return true if events system is ready for deinit
 * 
 * @example
 * dap_events_stop_all();
 * dap_test_async_config_t l_cfg = {
 *     .timeout_ms = 5000,
 *     .poll_interval_ms = 100,
 *     .fail_on_timeout = false,
 *     .operation_name = "events_stop"
 * };
 * bool l_ready = dap_test_wait_condition(dap_test_events_check_ready_for_deinit, NULL, &l_cfg);
 */
bool dap_test_events_check_ready_for_deinit(void *a_user_data);

// ============================================================================
// Convenience Macros
// ============================================================================

/**
 * @brief Wait for client to be initialized with default config
 * @param client Pointer to dap_client_t
 * @param timeout_ms Timeout in milliseconds
 * @return true if initialized, false on timeout
 */
#define DAP_TEST_WAIT_CLIENT_INITIALIZED(client, timeout_ms) \
    dap_test_wait_condition(dap_test_client_check_initialized, (client), \
        &(dap_test_async_config_t){ \
            .timeout_ms = (timeout_ms), \
            .poll_interval_ms = 50, \
            .fail_on_timeout = false, \
            .operation_name = "client_initialization" \
        })

/**
 * @brief Wait for client to be ready for deletion with default config
 * @param client Pointer to dap_client_t
 * @param timeout_ms Timeout in milliseconds
 * @return true if ready, false on timeout
 */
#define DAP_TEST_WAIT_CLIENT_READY_FOR_DELETION(client, timeout_ms) \
    dap_test_wait_condition(dap_test_client_check_ready_for_deletion, (client), \
        &(dap_test_async_config_t){ \
            .timeout_ms = (timeout_ms), \
            .poll_interval_ms = 50, \
            .fail_on_timeout = false, \
            .operation_name = "client_cleanup" \
        })

/**
 * @brief Wait for events system to be ready for deinit with default config
 * @param timeout_ms Timeout in milliseconds
 * @return true if ready, false on timeout
 */
#define DAP_TEST_WAIT_EVENTS_READY_FOR_DEINIT(timeout_ms) \
    dap_test_wait_condition(dap_test_events_check_ready_for_deinit, NULL, \
        &(dap_test_async_config_t){ \
            .timeout_ms = (timeout_ms), \
            .poll_interval_ms = 100, \
            .fail_on_timeout = false, \
            .operation_name = "events_stop" \
        })

#ifdef __cplusplus
}
#endif

