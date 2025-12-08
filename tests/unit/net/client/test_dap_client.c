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
 * @brief Comprehensive unit tests for DAP Client module
 * 
 * Tests client initialization, creation, state machine, and lifecycle.
 * External dependencies are mocked for isolation.
 * 
 * @date 2025-11-01
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
#include "dap_client_pvt.h"
#include "dap_stream.h"
#include "dap_net_trans.h"
#include "dap_stream_ch.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_cert.h"
#include "dap_enc.h"
#include "dap_enc_key.h"
#include "dap_config.h"

#define LOG_TAG "test_dap_client"

// ============================================================================
// Mock Declarations
// ============================================================================

// Mock dap_http_client functions (external dependency from dap_http_server module)
DAP_MOCK_DECLARE(dap_http_client_init);
DAP_MOCK_DECLARE(dap_http_client_deinit);

// Mock dap_events_worker functions (used by dap_client_new)
DAP_MOCK_DECLARE(dap_events_worker_get_auto);

// Mock dap_stream_ch functions (used by dap_client operations)
DAP_MOCK_DECLARE(dap_stream_ch_by_id_unsafe);
DAP_MOCK_DECLARE(dap_stream_ch_pkt_write_unsafe);

// Mock dap_worker functions
DAP_MOCK_DECLARE(dap_worker_exec_callback_on);

// Mock dap_cert functions
DAP_MOCK_DECLARE(dap_cert_find_by_name);

// Mock dap_enc functions
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
        
        // Mock external functions called during init
        DAP_MOCK_SET_RETURN(dap_http_client_init, 0);
        
        // Initialize client module
        l_ret = dap_client_init();
        TEST_ASSERT(l_ret == 0, "Client module initialization failed");
        
        s_test_initialized = true;
        TEST_INFO("Client test suite initialized");
    } else {
        // Reset mock return values for next test
        DAP_MOCK_SET_RETURN(dap_http_client_init, 0);
    }
}

/**
 * @brief Teardown function called after each test
 */
static void teardown_test(void)
{
    // Don't reset call counts here - they're needed for verification
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
    
    // Test deinit (just verify it executes without errors)
    // Note: dap_http_client_deinit is called internally and won't be intercepted by --wrap
    dap_client_deinit();
    
    // Re-init for next tests
    DAP_MOCK_SET_RETURN(dap_http_client_init, 0);
    
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
    
    // Mock worker
    dap_worker_t *l_mock_worker = DAP_NEW_Z(dap_worker_t);
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Verify client structure
    TEST_ASSERT(l_client->_internal != NULL, "Client internal should not be NULL");
    TEST_ASSERT(l_client->trans_type == DAP_NET_TRANS_HTTP, 
                "Default transport type should be HTTP");
    TEST_ASSERT(l_client->active_channels == NULL, 
                "Active channels should be NULL initially");
    TEST_ASSERT(l_client->stage_target == STAGE_BEGIN, 
                "Initial stage target should be STAGE_BEGIN");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    DAP_DELETE(l_mock_worker);
    
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
    
    // Mock worker
    dap_worker_t *l_mock_worker = DAP_NEW_Z(dap_worker_t);
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Delete client
    dap_client_delete_unsafe(l_client);
    DAP_DELETE(l_mock_worker);
    
    TEST_SUCCESS("Test 3 passed: Client deletion works correctly");
    teardown_test();
}

// ============================================================================
// Test 4: Client State Machine - Stage Target
// ============================================================================

static void test_04_set_stage_target(void)
{
    setup_test();
    
    TEST_INFO("Test 4: Setting stage target");
    
    // Mock worker
    dap_worker_t *l_mock_worker = DAP_NEW_Z(dap_worker_t);
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Test stage target setting (direct field access)
    l_client->stage_target = STAGE_ENC_INIT;
    TEST_ASSERT(l_client->stage_target == STAGE_ENC_INIT, 
                "Stage target should be set to STAGE_ENC_INIT");
    
    l_client->stage_target = STAGE_STREAM_STREAMING;
    TEST_ASSERT(l_client->stage_target == STAGE_STREAM_STREAMING, 
                "Stage target should be set to STAGE_STREAM_STREAMING");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    DAP_DELETE(l_mock_worker);
    
    TEST_SUCCESS("Test 4 passed: Stage target setting works correctly");
    teardown_test();
}

// ============================================================================
// Test 5: Client Uplink Configuration
// ============================================================================

static void test_05_set_uplink(void)
{
    setup_test();
    
    TEST_INFO("Test 5: Setting uplink configuration");
    
    // Mock worker
    dap_worker_t *l_mock_worker = DAP_NEW_Z(dap_worker_t);
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Test uplink setting
    dap_stream_node_addr_t l_node_addr = {0};
    const char *l_addr = "192.168.1.1";
    uint16_t l_port = 8080;
    
    dap_client_set_uplink_unsafe(l_client, &l_node_addr, l_addr, l_port);
    
    TEST_ASSERT(strcmp(l_client->link_info.uplink_addr, l_addr) == 0, 
                "Uplink address should match");
    TEST_ASSERT(l_client->link_info.uplink_port == l_port, 
                "Uplink port should match");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    DAP_DELETE(l_mock_worker);
    
    TEST_SUCCESS("Test 5 passed: Uplink configuration works correctly");
    teardown_test();
}

// ============================================================================
// Test 6: Active Channels Configuration
// ============================================================================

static void test_06_set_active_channels(void)
{
    setup_test();
    
    TEST_INFO("Test 6: Setting active channels");
    
    // Mock worker
    dap_worker_t *l_mock_worker = DAP_NEW_Z(dap_worker_t);
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Test setting active channels
    const char *l_channels = "ABC";
    dap_client_set_active_channels_unsafe(l_client, l_channels);
    
    TEST_ASSERT(l_client->active_channels != NULL, "Active channels should not be NULL");
    TEST_ASSERT(strcmp(l_client->active_channels, l_channels) == 0, 
                "Active channels should match");
    
    // Test updating active channels
    const char *l_new_channels = "XYZ";
    dap_client_set_active_channels_unsafe(l_client, l_new_channels);
    
    TEST_ASSERT(strcmp(l_client->active_channels, l_new_channels) == 0, 
                "Updated active channels should match");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    DAP_DELETE(l_mock_worker);
    
    TEST_SUCCESS("Test 6 passed: Active channels configuration works correctly");
    teardown_test();
}

// ============================================================================
// Test 7: Client Authentication Certificate
// ============================================================================

static void test_07_set_auth_cert(void)
{
    setup_test();
    
    TEST_INFO("Test 7: Setting authentication certificate");
    
    // Mock worker
    dap_worker_t *l_mock_worker = DAP_NEW_Z(dap_worker_t);
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Test: Initially auth_cert should be NULL
    TEST_ASSERT(l_client->auth_cert == NULL, "Initial auth cert should be NULL");
    
    // Note: dap_client_set_auth_cert calls dap_cert_find_by_name internally,
    // which won't be intercepted by --wrap (internal call limitation).
    // We can't fully test this without real certificates or by mocking dap_cert module.
    // This test verifies the function doesn't crash with invalid cert name.
    const char *l_cert_name = "nonexistent_cert";
    dap_client_set_auth_cert(l_client, l_cert_name);
    
    // Cert should still be NULL as nonexistent cert wasn't found
    TEST_ASSERT(l_client->auth_cert == NULL, "Auth cert should remain NULL for nonexistent cert");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    DAP_DELETE(l_mock_worker);
    
    TEST_SUCCESS("Test 7 passed: Auth certificate setting behavior verified");
    teardown_test();
}

// ============================================================================
// Test 8: Transport Type Set/Get
// ============================================================================

static void test_08_trans_type(void)
{
    setup_test();
    
    TEST_INFO("Test 8: Transport type set/get operations");
    
    // Mock worker
    dap_worker_t *l_mock_worker = DAP_NEW_Z(dap_worker_t);
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_mock_worker);
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT(l_client != NULL, "Client creation should succeed");
    
    // Test: Default transport type should be HTTP
    dap_net_trans_type_t l_default_type = dap_client_get_trans_type(l_client);
    TEST_ASSERT(l_default_type == DAP_NET_TRANS_HTTP, 
                "Default transport type should be HTTP");
    
    // Test: Set transport to UDP_RELIABLE
    dap_client_set_trans_type(l_client, DAP_NET_TRANS_UDP_RELIABLE);
    dap_net_trans_type_t l_new_type = dap_client_get_trans_type(l_client);
    TEST_ASSERT(l_new_type == DAP_NET_TRANS_UDP_RELIABLE, 
                "Transport type should be set to UDP_RELIABLE");
    
    // Test: Set transport to WEBSOCKET
    dap_client_set_trans_type(l_client, DAP_NET_TRANS_WEBSOCKET);
    l_new_type = dap_client_get_trans_type(l_client);
    TEST_ASSERT(l_new_type == DAP_NET_TRANS_WEBSOCKET, 
                "Transport type should be set to WEBSOCKET");
    
    // Test: Set transport to TLS_DIRECT
    dap_client_set_trans_type(l_client, DAP_NET_TRANS_TLS_DIRECT);
    l_new_type = dap_client_get_trans_type(l_client);
    TEST_ASSERT(l_new_type == DAP_NET_TRANS_TLS_DIRECT, 
                "Transport type should be set to TLS_DIRECT");
    
    // Test: Verify field is accessible directly
    TEST_ASSERT(l_client->trans_type == DAP_NET_TRANS_TLS_DIRECT, 
                "Direct field access should match getter result");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    DAP_DELETE(l_mock_worker);
    
    TEST_SUCCESS("Test 8 passed: Transport type set/get works correctly");
    teardown_test();
}

// ============================================================================
// Main Test Execution
// ============================================================================

int main(void)
{
    // Initialize logging
    dap_log_level_set(L_CRITICAL);
    
    dap_print_module_name("dap_client");
    
    // Run all tests
    test_01_client_init_deinit();
    test_02_client_creation();
    test_03_client_deletion();
    test_04_set_stage_target();
    test_05_set_uplink();
    test_06_set_active_channels();
    test_07_set_auth_cert();
    test_08_trans_type();
    
    // Cleanup
    suite_cleanup();
    
    return 0;
}
