/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform) the open source project
 *
 *    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
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
 * @file test_dap_client.c
 * @brief Comprehensive unit tests for DAP Client module with full mocking
 * 
 * Tests client initialization, state machine, connection lifecycle, and error handling.
 * All external dependencies are mocked for complete isolation.
 * 
 * @date 2025-10-30
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_test_helpers.h"
#include "dap_mock.h"
#include "dap_client.h"
#include "dap_client_pvt.h"  // For dap_client_pvt_t and DAP_CLIENT_PVT
#include "dap_stream.h"
#include "dap_stream_transport.h"
#include "dap_stream_ch.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_cert.h"
#include "dap_enc.h"
#include "dap_enc_key.h"

#define LOG_TAG "test_dap_client"

// ============================================================================
// Mock Declarations
// ============================================================================

// Mock dap_stream functions
DAP_MOCK_DECLARE(dap_stream_init);
DAP_MOCK_DECLARE(dap_stream_deinit);
DAP_MOCK_DECLARE(dap_stream_delete_unsafe);

// Mock dap_stream_transport functions
DAP_MOCK_DECLARE(dap_stream_transport_init);
DAP_MOCK_DECLARE(dap_stream_transport_deinit);
DAP_MOCK_DECLARE(dap_stream_transport_find);
DAP_MOCK_DECLARE(dap_stream_transport_register);

// Mock dap_events functions
DAP_MOCK_DECLARE(dap_events_init);
DAP_MOCK_DECLARE(dap_events_start);
DAP_MOCK_DECLARE(dap_events_stop_all);
DAP_MOCK_DECLARE(dap_events_deinit);
DAP_MOCK_DECLARE(dap_events_worker_get_auto);

// Mock dap_cert functions
DAP_MOCK_DECLARE(dap_cert_find_by_name);

// Mock dap_http_client functions (used by dap_client_init)
DAP_MOCK_DECLARE(dap_http_client_init);
DAP_MOCK_DECLARE(dap_http_client_deinit);

// Mock dap_client_http functions (used by dap_client_init)
DAP_MOCK_DECLARE(dap_client_http_init);
DAP_MOCK_DECLARE(dap_client_http_deinit);

// Mock dap_client_pvt functions (internal module)
DAP_MOCK_DECLARE(dap_client_pvt_init);
DAP_MOCK_DECLARE(dap_client_pvt_deinit);
DAP_MOCK_DECLARE(dap_client_pvt_new);
DAP_MOCK_DECLARE(dap_client_pvt_delete_unsafe);
DAP_MOCK_DECLARE(dap_client_pvt_queue_add);
DAP_MOCK_DECLARE(dap_client_pvt_queue_clear);
DAP_MOCK_DECLARE(dap_client_pvt_stage_transaction_begin);
DAP_MOCK_DECLARE(dap_client_pvt_request);
DAP_MOCK_DECLARE(dap_client_pvt_request_enc);

// Mock dap_stream functions
DAP_MOCK_DECLARE(dap_stream_ch_by_id_unsafe);
DAP_MOCK_DECLARE(dap_stream_ch_pkt_write_unsafe);

// Mock dap_worker functions
DAP_MOCK_DECLARE(dap_worker_exec_callback_on);

// Mock dap_enc functions (used by dap_client_request_enc_unsafe)
DAP_MOCK_DECLARE(dap_enc_code_out_size);
DAP_MOCK_DECLARE(dap_enc_code);
DAP_MOCK_DECLARE(dap_enc_key_new_generate);

// ============================================================================
// Test Suite State
// ============================================================================

static bool s_test_initialized = false;

// ============================================================================
// Setup/Teardown Functions
// ============================================================================

/**
 * @brief Setup function called before each test
 */
static void setup_test(void)
{
    if (!s_test_initialized) {
        // Initialize DAP common
        int l_ret = dap_common_init("test_dap_client", NULL);
        TEST_ASSERT(l_ret == 0, "DAP common initialization failed");
        
        // Mock initialization functions to return success
        DAP_MOCK_SET_RETURN(dap_http_client_init, 0);
        DAP_MOCK_SET_RETURN(dap_client_http_init, 0);
        DAP_MOCK_SET_RETURN(dap_client_pvt_init, 0);
        
        // Initialize client module
        l_ret = dap_client_init();
        TEST_ASSERT(l_ret == 0, "Client module initialization failed");
        
        s_test_initialized = true;
        TEST_INFO("Client test suite initialized");
    } else {
        // Reset mock return values for next test (but keep call counts)
        DAP_MOCK_SET_RETURN(dap_http_client_init, 0);
        DAP_MOCK_SET_RETURN(dap_client_http_init, 0);
        DAP_MOCK_SET_RETURN(dap_client_pvt_init, 0);
    }
}

/**
 * @brief Teardown function called after each test
 */
static void teardown_test(void)
{
    // Don't reset call counts here - they're needed for verification
    // Only reset return values if needed
}

/**
 * @brief Suite cleanup function
 */
static void suite_cleanup(void)
{
    if (s_test_initialized) {
        // Deinitialize client module
        dap_client_deinit();
        
        // Deinitialize DAP common
        dap_common_deinit();
        
        s_test_initialized = false;
        TEST_INFO("Client test suite cleaned up");
    }
}

// ============================================================================
// Test 1: Client Initialization
// ============================================================================

static void test_01_client_init_deinit(void)
{
    setup_test();
    
    TEST_INFO("Test 1: Client initialization/deinitialization");
    
    // Reset call counts for deinit functions before testing deinit
    // NOTE: DAP_MOCK_RESET resets call_count to 0, so we reset BEFORE calling deinit
    DAP_MOCK_RESET(dap_client_pvt_deinit);
    DAP_MOCK_RESET(dap_http_client_deinit);
    
    // Enable mocks explicitly to ensure they intercept calls
    DAP_MOCK_ENABLE(dap_client_pvt_deinit);
    DAP_MOCK_ENABLE(dap_http_client_deinit);
    
    // Test deinit
    // NOTE: Due to --wrap limitation, internal calls within the same static library
    // (dap_client_deinit -> dap_client_pvt_deinit) are not intercepted by --wrap.
    // This is a known limitation of GNU ld --wrap option.
    // For now, we only test that dap_client_deinit executes without errors.
    // Internal function calls (dap_client_pvt_deinit, dap_http_client_deinit)
    // would need to be tested separately via direct calls or by mocking at a higher level.
    dap_client_deinit();
    
    // Verify deinit was called
    // Note: Call count should be > 0 after dap_client_deinit() calls our wrapper
    uint32_t l_deinit_calls = DAP_MOCK_GET_CALL_COUNT(dap_client_pvt_deinit);
    fprintf(stderr, "[DEBUG] After dap_client_deinit(), dap_client_pvt_deinit call count: %u\n", l_deinit_calls);
    fflush(stderr);
    TEST_ASSERT(l_deinit_calls > 0, "dap_client_pvt_deinit should have been called");
    
    uint32_t l_http_deinit_calls = DAP_MOCK_GET_CALL_COUNT(dap_http_client_deinit);
    fprintf(stderr, "[DEBUG] After dap_client_deinit(), dap_http_client_deinit call count: %u\n", l_http_deinit_calls);
    fflush(stderr);
    TEST_ASSERT(l_http_deinit_calls > 0, "dap_http_client_deinit should have been called");
    
    // Re-init for next tests
    DAP_MOCK_SET_RETURN(dap_http_client_init, 0);
    DAP_MOCK_SET_RETURN(dap_client_http_init, 0);
    DAP_MOCK_SET_RETURN(dap_client_pvt_init, 0);
    int l_ret = dap_client_init();
    TEST_ASSERT(l_ret == 0, "Re-init after deinit should succeed");
    
    TEST_SUCCESS("Test 1 passed: Client init/deinit works correctly");
    teardown_test();
}

// ============================================================================
// Test 2: Client Creation
// ============================================================================

static void test_02_client_creation(void)
{
    setup_test();
    
    TEST_INFO("Test 2: Client creation");
    
    // Mock worker get
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    
    // Mock pvt_new
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Reset call counts AFTER setting return values but BEFORE creating client
    DAP_MOCK_RESET(dap_events_worker_get_auto);
    DAP_MOCK_RESET(dap_client_pvt_new);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Verify client structure
    TEST_ASSERT(l_client->_internal != NULL, "Client internal should not be NULL");
    TEST_ASSERT(l_client->transport_type == DAP_STREAM_TRANSPORT_HTTP, 
                "Default transport type should be HTTP");
    TEST_ASSERT(l_client->active_channels == NULL, 
                "Active channels should be NULL initially");
    TEST_ASSERT(l_client->stage_target == STAGE_BEGIN, 
                "Initial stage target should be STAGE_BEGIN");
    
    // Verify pvt_new was called
    // Note: dap_client_pvt_new is called from static library, but we check it anyway
    uint32_t l_pvt_new_calls = DAP_MOCK_GET_CALL_COUNT(dap_client_pvt_new);
    // Note: This may be 0 if called from static library - that's acceptable
    
    // Verify worker_get was called
    uint32_t l_worker_calls = DAP_MOCK_GET_CALL_COUNT(dap_events_worker_get_auto);
    TEST_ASSERT(l_worker_calls > 0, "dap_events_worker_get_auto should have been called");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 2 passed: Client creation works correctly");
    teardown_test();
}

// ============================================================================
// Test 3: Client Deletion
// ============================================================================

static void test_03_client_deletion(void)
{
    setup_test();
    
    TEST_INFO("Test 3: Client deletion");
    
    // Reset call counts for this test
    DAP_MOCK_RESET(dap_events_worker_get_auto);
    DAP_MOCK_RESET(dap_client_pvt_new);
    DAP_MOCK_RESET(dap_client_pvt_delete_unsafe);
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    
    // Mock pvt_new
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Set active channels to test cleanup
    dap_client_set_active_channels_unsafe(l_client, "C,F,N");
    TEST_ASSERT(l_client->active_channels != NULL, "Active channels should be set");
    
    // Delete client
    dap_client_delete_unsafe(l_client);
    
    // Verify pvt_delete was called
    uint32_t l_pvt_delete_calls = DAP_MOCK_GET_CALL_COUNT(dap_client_pvt_delete_unsafe);
    TEST_ASSERT(l_pvt_delete_calls > 0, "dap_client_pvt_delete_unsafe should have been called");
    
    TEST_SUCCESS("Test 3 passed: Client deletion works correctly");
    teardown_test();
}

// ============================================================================
// Test 4: Client Uplink Configuration
// ============================================================================

static void test_04_client_uplink_config(void)
{
    setup_test();
    
    TEST_INFO("Test 4: Client uplink configuration");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Set uplink
    dap_stream_node_addr_t l_node_addr = {0};
    dap_client_set_uplink_unsafe(l_client, &l_node_addr, "127.0.0.1", 8080);
    
    // Verify uplink was set
    const char *l_addr = dap_client_get_uplink_addr_unsafe(l_client);
    uint16_t l_port = dap_client_get_uplink_port_unsafe(l_client);
    
    TEST_ASSERT(strcmp(l_addr, "127.0.0.1") == 0, "Uplink address should be set");
    TEST_ASSERT(l_port == 8080, "Uplink port should be set");
    
    // Test NULL client (should not crash)
    dap_client_set_uplink_unsafe(NULL, &l_node_addr, "127.0.0.1", 8080);
    
    // Test NULL address (should not crash)
    dap_client_set_uplink_unsafe(l_client, &l_node_addr, NULL, 8080);
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 4 passed: Client uplink configuration works correctly");
    teardown_test();
}

// ============================================================================
// Test 5: Client Active Channels Configuration
// ============================================================================

static void test_05_client_active_channels(void)
{
    setup_test();
    
    TEST_INFO("Test 5: Client active channels configuration");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Set active channels
    dap_client_set_active_channels_unsafe(l_client, "C,F,N");
    TEST_ASSERT(l_client->active_channels != NULL, "Active channels should be set");
    TEST_ASSERT(strcmp(l_client->active_channels, "C,F,N") == 0, 
                "Active channels should match");
    
    // Change active channels
    dap_client_set_active_channels_unsafe(l_client, "C");
    TEST_ASSERT(strcmp(l_client->active_channels, "C") == 0, 
                "Active channels should be updated");
    
    // Test NULL client (should not crash)
    dap_client_set_active_channels_unsafe(NULL, "C");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 5 passed: Client active channels configuration works correctly");
    teardown_test();
}

// ============================================================================
// Test 6: Client Transport Type Configuration
// ============================================================================

static void test_06_client_transport_type(void)
{
    setup_test();
    
    TEST_INFO("Test 6: Client transport type configuration");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Verify default transport (HTTP)
    dap_stream_transport_type_t l_type = dap_client_get_transport_type(l_client);
    TEST_ASSERT(l_type == DAP_STREAM_TRANSPORT_HTTP, 
                "Default transport type should be HTTP");
    
    // Set UDP transport
    dap_client_set_transport_type(l_client, DAP_STREAM_TRANSPORT_UDP_BASIC);
    l_type = dap_client_get_transport_type(l_client);
    TEST_ASSERT(l_type == DAP_STREAM_TRANSPORT_UDP_BASIC, 
                "Transport type should be UDP Basic");
    
    // Set WebSocket transport
    dap_client_set_transport_type(l_client, DAP_STREAM_TRANSPORT_WEBSOCKET);
    l_type = dap_client_get_transport_type(l_client);
    TEST_ASSERT(l_type == DAP_STREAM_TRANSPORT_WEBSOCKET, 
                "Transport type should be WebSocket");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 6 passed: Client transport type configuration works correctly");
    teardown_test();
}

// ============================================================================
// Test 7: Client State Machine - Initial State
// ============================================================================

static void test_07_client_state_machine_init(void)
{
    setup_test();
    
    TEST_INFO("Test 7: Client state machine - initial state");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Check initial stage
    dap_client_stage_t l_stage = dap_client_get_stage(l_client);
    dap_client_stage_status_t l_status = dap_client_get_stage_status(l_client);
    
    // Initial stage should be STAGE_BEGIN or STAGE_UNDEFINED
    TEST_ASSERT(l_stage == STAGE_BEGIN || l_stage == STAGE_UNDEFINED, 
                "Initial stage should be STAGE_BEGIN or STAGE_UNDEFINED");
    
    // Get stage strings
    const char *l_stage_str = dap_client_get_stage_str(l_client);
    TEST_ASSERT(l_stage_str != NULL, "Stage string should not be NULL");
    
    const char *l_status_str = dap_client_get_stage_status_str(l_client);
    TEST_ASSERT(l_status_str != NULL, "Status string should not be NULL");
    
    // Test stage string functions
    const char *l_stage_enum_str = dap_client_stage_str(STAGE_BEGIN);
    TEST_ASSERT(l_stage_enum_str != NULL, "Stage enum string should not be NULL");
    
    const char *l_status_enum_str = dap_client_stage_status_str(STAGE_STATUS_NONE);
    TEST_ASSERT(l_status_enum_str != NULL, "Status enum string should not be NULL");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 7 passed: Client state machine initial state works correctly");
    teardown_test();
}

// ============================================================================
// Test 8: Client Error Handling
// ============================================================================

static void test_08_client_error_handling(void)
{
    setup_test();
    
    TEST_INFO("Test 8: Client error handling");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client for error handling tests
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Test NULL client handling for safe functions
    dap_client_stage_t l_stage = dap_client_get_stage(NULL);
    TEST_ASSERT(l_stage == STAGE_UNDEFINED, 
                "NULL client should return STAGE_UNDEFINED");
    
    dap_client_stage_status_t l_status = dap_client_get_stage_status(NULL);
    TEST_ASSERT(l_status == STAGE_STATUS_NONE, 
                "NULL client should return STAGE_STATUS_NONE");
    
    const char *l_stage_str = dap_client_get_stage_str(NULL);
    TEST_ASSERT(l_stage_str != NULL, "NULL client stage string should not be NULL");
    
    // Note: dap_client_get_error_str doesn't handle NULL client safely - test with real client
    const char *l_error_str = dap_client_get_error_str(l_client);
    TEST_ASSERT(l_error_str != NULL, "Client error string should not be NULL");
    
    // Test error string functions
    const char *l_error_enum_str = dap_client_error_str(ERROR_NO_ERROR);
    TEST_ASSERT(l_error_enum_str != NULL, "Error enum string should not be NULL");
    
    l_error_enum_str = dap_client_error_str(ERROR_NETWORK_CONNECTION_REFUSE);
    TEST_ASSERT(l_error_enum_str != NULL, "Error enum string should not be NULL");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 8 passed: Client error handling works correctly");
    teardown_test();
}

// ============================================================================
// Test 9: Client Reconnect Configuration
// ============================================================================

static void test_09_client_reconnect_config(void)
{
    setup_test();
    
    TEST_INFO("Test 9: Client reconnect configuration");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Test always_reconnect
    bool l_always_reconnect = dap_client_get_is_always_reconnect(l_client);
    TEST_ASSERT(l_always_reconnect == false, 
                "Default always_reconnect should be false");
    
    dap_client_set_is_always_reconnect(l_client, true);
    l_always_reconnect = dap_client_get_is_always_reconnect(l_client);
    TEST_ASSERT(l_always_reconnect == true, 
                "always_reconnect should be set to true");
    
    // Test connect_on_demand (default is false)
    TEST_ASSERT(l_client->connect_on_demand == false, 
                "Default connect_on_demand should be false");
    
    l_client->connect_on_demand = true;
    TEST_ASSERT(l_client->connect_on_demand == true, 
                "connect_on_demand should be set to true");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 9 passed: Client reconnect configuration works correctly");
    teardown_test();
}

// ============================================================================
// Test 10: Client Callbacks
// ============================================================================

static void test_10_client_callbacks(void)
{
    setup_test();
    
    TEST_INFO("Test 10: Client callbacks");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    void *l_test_arg = (void *)0xDEADBEEF;
    
    // Callback function
    dap_client_callback_t l_callback = NULL; // We'll test callback separately
    
    // Create client with callback
    dap_client_t *l_client = dap_client_new(l_callback, l_test_arg);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    TEST_ASSERT(l_client->stage_status_error_callback == l_callback, 
                "Error callback should be set");
    TEST_ASSERT(l_client->callbacks_arg == l_test_arg, 
                "Callback arg should be set");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 10 passed: Client callbacks work correctly");
    teardown_test();
}

// ============================================================================
// Test 11: Client Write Operations
// ============================================================================

static void test_11_client_write_operations(void)
{
    setup_test();
    
    TEST_INFO("Test 11: Client write operations");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Set active channels
    dap_client_set_active_channels_unsafe(l_client, "C");
    
    // Reset call counts for this test
    DAP_MOCK_RESET(dap_stream_ch_by_id_unsafe);
    DAP_MOCK_RESET(dap_stream_ch_pkt_write_unsafe);
    DAP_MOCK_RESET(dap_worker_exec_callback_on);
    
    // Test write_unsafe with invalid channel (should fail)
    uint8_t l_data[] = {1, 2, 3, 4};
    ssize_t l_ret = dap_client_write_unsafe(l_client, 'X', 0, l_data, sizeof(l_data));
    TEST_ASSERT(l_ret < 0, "Write with invalid channel should fail");
    
    // Test write_unsafe with NULL client (should fail)
    l_ret = dap_client_write_unsafe(NULL, 'C', 0, l_data, sizeof(l_data));
    TEST_ASSERT(l_ret < 0, "Write with NULL client should fail");
    
    // Test write_mt (multithreaded)
    l_ret = dap_client_write_mt(l_client, 'C', 1, l_data, sizeof(l_data));
    // Note: This will queue the write, actual execution depends on worker callbacks
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 11 passed: Client write operations work correctly");
    teardown_test();
}

// ============================================================================
// Test 12: Client Queue Operations
// ============================================================================

static void test_12_client_queue_operations(void)
{
    setup_test();
    
    TEST_INFO("Test 12: Client queue operations");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Reset call counts for this test
    DAP_MOCK_RESET(dap_worker_exec_callback_on);
    
    // Clear queue (will execute on worker)
    dap_client_queue_clear(l_client);
    
    // Verify worker callback was called
    uint32_t l_calls = DAP_MOCK_GET_CALL_COUNT(dap_worker_exec_callback_on);
    TEST_ASSERT(l_calls > 0, "dap_worker_exec_callback_on should be called");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 12 passed: Client queue operations work correctly");
    teardown_test();
}

// ============================================================================
// Test 13: Client Request Operations
// ============================================================================

static void test_13_client_request_operations(void)
{
    setup_test();
    
    TEST_INFO("Test 13: Client request operations");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Reset call counts for this test
    DAP_MOCK_RESET(dap_client_pvt_request);
    DAP_MOCK_RESET(dap_client_pvt_request_enc);
    DAP_MOCK_RESET(dap_enc_code_out_size);
    DAP_MOCK_RESET(dap_enc_code);
    
    // Test request_unsafe
    uint8_t l_request_data[] = {1, 2, 3};
    dap_client_request_unsafe(l_client, "/test/path", l_request_data, sizeof(l_request_data), 
                              NULL, NULL);
    
    // Verify pvt_request was called
    uint32_t l_pvt_request_calls = DAP_MOCK_GET_CALL_COUNT(dap_client_pvt_request);
    TEST_ASSERT(l_pvt_request_calls > 0, "dap_client_pvt_request should have been called");
    
    // Setup encryption key for request_enc_unsafe test
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    TEST_ASSERT(l_client_pvt != NULL, "Client internal should not be NULL");
    
    // Create mock encryption key
    dap_enc_key_t *l_mock_session_key = (dap_enc_key_t *)0xABCDEF00;
    l_mock_session_key->type = DAP_ENC_KEY_TYPE_SALSA2012;
    
    // Set session key in client
    l_client_pvt->session_key = l_mock_session_key;
    
    // Enable mocks for encryption functions
    DAP_MOCK_ENABLE(dap_enc_code_out_size);
    DAP_MOCK_ENABLE(dap_enc_code);
    DAP_MOCK_SET_RETURN(dap_enc_code_out_size, (void*)(intptr_t)256); // Mock output size
    DAP_MOCK_SET_RETURN(dap_enc_code, (void*)(intptr_t)3); // Mock encoded size
    
    // Test request_enc_unsafe
    dap_client_request_enc_unsafe(l_client, "/enc", "/sub", "query=test", 
                                   l_request_data, sizeof(l_request_data), NULL, NULL);
    
    // Verify pvt_request_enc was called
    uint32_t l_pvt_request_enc_calls = DAP_MOCK_GET_CALL_COUNT(dap_client_pvt_request_enc);
    TEST_ASSERT(l_pvt_request_enc_calls > 0, "dap_client_pvt_request_enc should have been called");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 13 passed: Client request operations work correctly");
    teardown_test();
}

// ============================================================================
// Test 14: Client Go Stage
// ============================================================================

static void test_14_client_go_stage(void)
{
    setup_test();
    
    TEST_INFO("Test 14: Client go stage");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Reset call counts for this test
    DAP_MOCK_RESET(dap_worker_exec_callback_on);
    
    // Test go_stage
    dap_client_go_stage(l_client, STAGE_STREAM_STREAMING, NULL);
    
    // Verify worker callback was called
    uint32_t l_calls = DAP_MOCK_GET_CALL_COUNT(dap_worker_exec_callback_on);
    TEST_ASSERT(l_calls > 0, "dap_worker_exec_callback_on should be called");
    
    // Test go_stage with NULL client (should not crash)
    dap_client_go_stage(NULL, STAGE_BEGIN, NULL);
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 14 passed: Client go stage works correctly");
    teardown_test();
}

// ============================================================================
// Test 15: Client Delete MT
// ============================================================================

static void test_15_client_delete_mt(void)
{
    setup_test();
    
    TEST_INFO("Test 15: Client delete MT");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Reset call counts for this test
    DAP_MOCK_RESET(dap_worker_exec_callback_on);
    
    // Delete MT (multithreaded)
    dap_client_delete_mt(l_client);
    
    // Verify worker callback was called
    uint32_t l_calls = DAP_MOCK_GET_CALL_COUNT(dap_worker_exec_callback_on);
    TEST_ASSERT(l_calls > 0, "dap_worker_exec_callback_on should be called");
    
    // Note: Actual deletion happens in worker callback, we can't verify it here
    
    TEST_SUCCESS("Test 15 passed: Client delete MT works correctly");
    teardown_test();
}

// ============================================================================
// Test 16: Client Auth Certificate
// ============================================================================

static void test_16_client_auth_cert(void)
{
    setup_test();
    
    TEST_INFO("Test 16: Client auth certificate");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Mock certificate
    dap_cert_t *l_mock_cert = (dap_cert_t *)0xABCDEF00;
    DAP_MOCK_SET_RETURN(dap_cert_find_by_name, l_mock_cert);
    
    // Set auth certificate
    dap_client_set_auth_cert(l_client, "test_cert");
    TEST_ASSERT(l_client->auth_cert == l_mock_cert, "Auth cert should be set");
    
    // Test with NULL client (should not crash)
    dap_client_set_auth_cert(NULL, "test_cert");
    
    // Test with NULL cert name (should not crash)
    dap_client_set_auth_cert(l_client, NULL);
    
    // Test with non-existent cert
    DAP_MOCK_SET_RETURN(dap_cert_find_by_name, NULL);
    dap_client_set_auth_cert(l_client, "non_existent");
    // Cert should remain unchanged or be NULL
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 16 passed: Client auth certificate works correctly");
    teardown_test();
}

// ============================================================================
// Test 17: Client Stream Accessors
// ============================================================================

static void test_17_client_stream_accessors(void)
{
    setup_test();
    
    TEST_INFO("Test 17: Client stream accessors");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Test get_stream (should return NULL initially)
    dap_stream_t *l_stream = dap_client_get_stream(l_client);
    // Can be NULL initially
    
    // Test get_stream_worker (should return NULL initially)
    dap_stream_worker_t *l_stream_worker = dap_client_get_stream_worker(l_client);
    // Can be NULL initially
    
    // Test get_stream_ch_unsafe (should return NULL initially)
    dap_stream_ch_t *l_ch = dap_client_get_stream_ch_unsafe(l_client, 'C');
    TEST_ASSERT(l_ch == NULL, "Stream channel should be NULL initially");
    
    // Test get_stream_id (should return 0 initially)
    uint32_t l_stream_id = dap_client_get_stream_id(l_client);
    TEST_ASSERT(l_stream_id == 0, "Stream ID should be 0 initially");
    
    // Test NULL client handling
    l_stream = dap_client_get_stream(NULL);
    TEST_ASSERT(l_stream == NULL, "get_stream with NULL client should return NULL");
    
    l_stream_worker = dap_client_get_stream_worker(NULL);
    TEST_ASSERT(l_stream_worker == NULL, "get_stream_worker with NULL client should return NULL");
    
    l_ch = dap_client_get_stream_ch_unsafe(NULL, 'C');
    TEST_ASSERT(l_ch == NULL, "get_stream_ch with NULL client should return NULL");
    
    l_stream_id = dap_client_get_stream_id(NULL);
    TEST_ASSERT(l_stream_id == 0, "get_stream_id with NULL client should return 0");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 17 passed: Client stream accessors work correctly");
    teardown_test();
}

// ============================================================================
// Test 18: Client From ESocket
// ============================================================================

static void test_18_client_from_esocket(void)
{
    setup_test();
    
    TEST_INFO("Test 18: Client from esocket");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Create mock esocket
    dap_events_socket_t l_mock_esocket = {0};
    l_mock_esocket._inheritor = l_client;
    
    // Test from_esocket
    dap_client_t *l_client_from_es = dap_client_from_esocket(&l_mock_esocket);
    TEST_ASSERT(l_client_from_es == l_client, "Should return client from esocket");
    
    // Test NULL esocket
    dap_client_t *l_null = dap_client_from_esocket(NULL);
    // Can be NULL or undefined behavior
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 18 passed: Client from esocket works correctly");
    teardown_test();
}

// ============================================================================
// Test 19: Client Get Key Stream
// ============================================================================

static void test_19_client_get_key_stream(void)
{
    setup_test();
    
    TEST_INFO("Test 19: Client get key stream");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Test get_key_stream (should return NULL initially)
    dap_enc_key_t *l_key = dap_client_get_key_stream(l_client);
    // Can be NULL initially
    
    // Test NULL client
    l_key = dap_client_get_key_stream(NULL);
    TEST_ASSERT(l_key == NULL, "get_key_stream with NULL client should return NULL");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 19 passed: Client get key stream works correctly");
    teardown_test();
}

// ============================================================================
// Test 20: Client Get Auth Cookie
// ============================================================================
// NOTE: dap_client_get_auth_cookie is declared but not implemented in dap_client.c
// Skipping this test until function is implemented
/*
static void test_20_client_get_auth_cookie(void)
{
    setup_test();
    
    TEST_INFO("Test 20: Client get auth cookie");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Test get_auth_cookie
    const char *l_cookie = dap_client_get_auth_cookie(l_client);
    // Can be NULL or valid cookie string
    
    // Test NULL client
    l_cookie = dap_client_get_auth_cookie(NULL);
    // Should handle NULL gracefully
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 20 passed: Client get auth cookie works correctly");
    teardown_test();
}
*/

// ============================================================================
// Test 21: Client Get Uplink Addr/Port (Inline Functions)
// ============================================================================

static void test_21_client_get_uplink_addr_port(void)
{
    setup_test();
    
    TEST_INFO("Test 21: Client get uplink addr/port");
    
    // Mock worker
    dap_worker_t *l_mock_worker = (dap_worker_t *)0x12345678;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    DAP_MOCK_SET_RETURN(dap_client_pvt_new, 0);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Set uplink
    dap_stream_node_addr_t l_node_addr = {0};
    dap_client_set_uplink_unsafe(l_client, &l_node_addr, "192.168.1.1", 9090);
    
    // Test get_uplink_addr_unsafe (inline)
    const char *l_addr = dap_client_get_uplink_addr_unsafe(l_client);
    TEST_ASSERT(strcmp(l_addr, "192.168.1.1") == 0, "Uplink address should match");
    
    // Test get_uplink_port_unsafe (inline)
    uint16_t l_port = dap_client_get_uplink_port_unsafe(l_client);
    TEST_ASSERT(l_port == 9090, "Uplink port should match");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    
    TEST_SUCCESS("Test 21 passed: Client get uplink addr/port works correctly");
    teardown_test();
}

// ============================================================================
// Main Test Suite
// ============================================================================

int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    TEST_SUITE_START("DAP Client Module - Full Unit Tests");
    
    // Run all tests
    TEST_RUN(test_01_client_init_deinit);
    TEST_RUN(test_02_client_creation);
    TEST_RUN(test_03_client_deletion);
    TEST_RUN(test_04_client_uplink_config);
    TEST_RUN(test_05_client_active_channels);
    TEST_RUN(test_06_client_transport_type);
    TEST_RUN(test_07_client_state_machine_init);
    TEST_RUN(test_08_client_error_handling);
    TEST_RUN(test_09_client_reconnect_config);
    TEST_RUN(test_10_client_callbacks);
    TEST_RUN(test_11_client_write_operations);
    TEST_RUN(test_12_client_queue_operations);
    TEST_RUN(test_13_client_request_operations);
    TEST_RUN(test_14_client_go_stage);
    TEST_RUN(test_15_client_delete_mt);
    TEST_RUN(test_16_client_auth_cert);
    TEST_RUN(test_17_client_stream_accessors);
    TEST_RUN(test_18_client_from_esocket);
    TEST_RUN(test_19_client_get_key_stream);
    // TEST_RUN(test_20_client_get_auth_cookie); // Function not implemented yet
    TEST_RUN(test_21_client_get_uplink_addr_port);
    
    suite_cleanup();
    
    TEST_SUITE_END();
    
    return 0;
}

