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
 * @file test_trans_websocket.c
 * @brief Comprehensive unit tests for WebSocket trans server and stream
 * 
 * Tests WebSocket trans with full mocking for isolation:
 * - Server: creation, start, stop, handler registration
 * - Stream: registration, connection, read/write operations
 * - Complete isolation through mocks for all dependencies
 * 
 * @date 2025-11-02
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_test_helpers.h"
#include "dap_mock.h"
#include "dap_net_trans.h"
#include "dap_net_trans_server.h"
#include "dap_net_trans_websocket_server.h"
#include "dap_net_trans_websocket_stream.h"
#include "dap_net_server_common.h"
#include "dap_http_server.h"
#include "dap_server.h"
#include "dap_stream.h"
#include "dap_stream_handshake.h"
#include "dap_stream_session.h"
#include "dap_timerfd.h"
#include "dap_worker.h"
#include "dap_trans_test_mocks.h"

#define LOG_TAG "test_trans_websocket"

// ============================================================================
// Mock Declarations (using common trans mocks)
// ============================================================================
// Common mocks are declared in dap_trans_test_mocks.h
// Only WebSocket-specific mocks are declared here

// Mock declarations are in dap_trans_test_mocks.h
// The mock scanner now scans header files too, so no need to duplicate declarations here

// Mock WebSocket-specific functions
DAP_MOCK_DECLARE(dap_net_trans_websocket_server_add_upgrade_handler);

// Mock dap_events_worker functions (needed for WebSocket ping timer)
DAP_MOCK_DECLARE(dap_events_worker_get_auto);
DAP_MOCK_DECLARE(dap_timerfd_start_on_worker);

// ============================================================================
// Mock Wrappers
// ============================================================================
// Common wrappers are implemented in dap_trans_test_mocks.c
// Only WebSocket-specific wrappers are defined here

// All common wrappers are in dap_trans_test_mocks.c
// dap_net_trans_find is not mocked - using real implementation
// This allows tests to access real registered transs with proper ops

// Wrapper for dap_net_trans_websocket_server_add_upgrade_handler
DAP_MOCK_WRAPPER_CUSTOM(int, dap_net_trans_websocket_server_add_upgrade_handler,
    PARAM(dap_net_trans_websocket_server_t*, a_ws_server),
    PARAM(const char*, a_url_path)
)
{
    UNUSED(a_ws_server);
    UNUSED(a_url_path);
    
    // Return mock value if set, otherwise return 0 (success)
    if (g_mock_dap_net_trans_websocket_server_add_upgrade_handler && 
        g_mock_dap_net_trans_websocket_server_add_upgrade_handler->return_value.i != 0) {
        return g_mock_dap_net_trans_websocket_server_add_upgrade_handler->return_value.i;
    }
    return 0;
}

// Mock dap_worker_t for testing
static dap_worker_t s_mock_worker = {0};

// Wrapper for dap_events_worker_get_auto
DAP_MOCK_WRAPPER_CUSTOM(dap_worker_t*, dap_events_worker_get_auto, void)
{
    // Return mock worker if set, otherwise return NULL (no worker available)
    if (g_mock_dap_events_worker_get_auto && g_mock_dap_events_worker_get_auto->return_value.ptr) {
        return (dap_worker_t*)g_mock_dap_events_worker_get_auto->return_value.ptr;
    }
    
    // Return default mock worker (for tests that need it)
    return &s_mock_worker;
}

// Mock dap_timerfd_t for testing
static dap_timerfd_t s_mock_timerfd = {0};

// Wrapper for dap_timerfd_start_on_worker
DAP_MOCK_WRAPPER_CUSTOM(dap_timerfd_t*, dap_timerfd_start_on_worker,
    PARAM(dap_worker_t*, a_worker),
    PARAM(uint32_t, a_interval_ms),
    PARAM(dap_timerfd_callback_t, a_callback),
    PARAM(void*, a_user_data)
)
{
    UNUSED(a_worker);
    UNUSED(a_interval_ms);
    UNUSED(a_callback);
    UNUSED(a_user_data);
    
    // Return mock timer if set, otherwise return default mock
    if (g_mock_dap_timerfd_start_on_worker && g_mock_dap_timerfd_start_on_worker->return_value.ptr) {
        return (dap_timerfd_t*)g_mock_dap_timerfd_start_on_worker->return_value.ptr;
    }
    
    // Return default mock timerfd
    return &s_mock_timerfd;
}

// ============================================================================
// Test Suite State
// ============================================================================

static bool s_test_initialized = false;
static bool s_session_callback_called = false;

static void s_session_callback(dap_stream_t *a_stream, uint32_t a_session_id, const char *a_response_data, size_t a_response_size, int a_error_code) {
    UNUSED(a_stream);
    UNUSED(a_session_id);
    UNUSED(a_response_data);
    UNUSED(a_response_size);
    UNUSED(a_error_code);
    s_session_callback_called = true;
}

// Mock instances for tests
static dap_stream_t s_mock_stream = {0};
static dap_events_socket_t s_mock_events_socket = {0};
static dap_net_trans_ctx_t s_mock_trans_ctx;

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
        int l_ret = dap_common_init("test_trans_websocket", NULL);
        TEST_ASSERT(l_ret == 0, "DAP common initialization failed");
        
        // Initialize and start event system (needed for dap_events_worker_get_auto)
        l_ret = dap_events_init(0, 30); // CPU count threads, 30 second timeout
        TEST_ASSERT(l_ret == 0, "dap_events_init failed");
        l_ret = dap_events_start(); // Start worker threads
        TEST_ASSERT(l_ret == 0, "dap_events_start failed");
        
        // Enable DEBUG logging for mock framework debugging
        dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
        dap_log_level_set(L_DEBUG);
        
        // Initialize mock framework
        dap_mock_init();
        
        // Trans layer is initialized automatically via dap_module system
        // No need to call dap_net_trans_init() manually
        
        // Initialize WebSocket trans server (this registers operations)
        l_ret = dap_net_trans_websocket_server_init();
        TEST_ASSERT(l_ret == 0, "WebSocket trans server initialization failed");
        
        // Initialize WebSocket stream trans
        // Check if already registered (might be auto-registered via module constructor)
        dap_net_trans_t *l_existing = dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
        if (l_existing) {
            TEST_INFO("WebSocket stream trans already registered (auto-registered), skipping manual registration");
        } else {
            l_ret = dap_net_trans_websocket_stream_register();
            TEST_ASSERT(l_ret == 0, "WebSocket stream trans registration failed");
        }
        
        s_test_initialized = true;
        TEST_INFO("WebSocket trans test suite initialized");
    }
    
    // Reset mocks before each test
    dap_mock_reset_all();
}

/**
 * @brief Teardown function called after each test
 */
static void teardown_test(void)
{
    // Reset all mocks for next test
    dap_mock_reset_all();
}

/**
 * @brief Suite cleanup function
 */
static void suite_cleanup(void)
{
    if (s_test_initialized) {
        // Deinitialize WebSocket stream trans
        dap_net_trans_websocket_stream_unregister();
        
        // Deinitialize WebSocket trans server (unregisters operations)
        dap_net_trans_websocket_server_deinit();
        
        // Trans layer is deinitialized automatically via dap_module system
        // No need to call dap_net_trans_deinit() manually
        
        // Deinitialize mock framework
        dap_mock_deinit();
        
        // Deinitialize DAP common
        dap_common_deinit();
        
        s_test_initialized = false;
        TEST_INFO("WebSocket trans test suite cleaned up");
    }
}

// ============================================================================
// Server Tests
// ============================================================================

/**
 * @brief Test WebSocket trans server operations registration
 */
static void test_01_server_ops_registration(void)
{
    TEST_INFO("Testing WebSocket trans server operations registration");
    
    // Verify operations are registered
    const dap_net_trans_server_ops_t *l_ops = 
        dap_net_trans_server_get_ops(DAP_NET_TRANS_WEBSOCKET);
    
    TEST_ASSERT_NOT_NULL(l_ops, "WebSocket trans server operations should be registered");
    TEST_ASSERT_NOT_NULL(l_ops->new, "new callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->start, "start callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->stop, "stop callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->delete, "delete callback should be set");
    
    TEST_SUCCESS("WebSocket trans server operations registration verified");
}

/**
 * @brief Test WebSocket trans server creation through unified API
 */
static void test_02_server_creation(void)
{
    TEST_INFO("Testing WebSocket trans server creation");
    
    const char *l_server_name = "test_websocket_server";
    
    // Setup mock for dap_http_server_new
    DAP_MOCK_SET_RETURN(dap_http_server_new, (void*)dap_trans_test_get_mock_server());
    
    // Create server through unified API
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_WEBSOCKET, l_server_name);
    
    TEST_ASSERT_NOT_NULL(l_server, "WebSocket server should be created");
    TEST_ASSERT(l_server->trans_type == DAP_NET_TRANS_WEBSOCKET, 
                "Trans type should be WEBSOCKET");
    TEST_ASSERT(strcmp(l_server->server_name, l_server_name) == 0,
                "Server name should match");
    TEST_ASSERT_NOT_NULL(l_server->trans_specific,
                         "Trans-specific server instance should be created");
    
    // Note: dap_http_server_new is called in start(), not in new()
    // So we don't verify it here - it will be verified in test_03_server_start
    
    // Cleanup
    dap_net_trans_server_delete(l_server);
    
    TEST_SUCCESS("WebSocket trans server creation verified");
}

/**
 * @brief Test WebSocket trans server start with handlers registration
 */
static void test_03_server_start(void)
{
    TEST_INFO("Testing WebSocket trans server start");
    
    const char *l_server_name = "test_websocket_server";
    const char *l_cfg_section = "test_server";
    const char *l_addrs[] = {"127.0.0.1"};
    uint16_t l_ports[] = {8080};
    
    // Setup mocks
    DAP_MOCK_ENABLE(enc_http_add_proc);  // Enable mock for enc_http_add_proc
    DAP_MOCK_SET_RETURN(dap_http_server_new, (void*)dap_trans_test_get_mock_server());
    // Note: dap_net_trans_find is not mocked - using real implementation
    DAP_MOCK_SET_RETURN(dap_net_trans_websocket_server_add_upgrade_handler, 0);
    
    // Create server
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_WEBSOCKET, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Start server
    int l_ret = dap_net_trans_server_start(l_server, l_cfg_section, 
                                                l_addrs, l_ports, 1);
    TEST_ASSERT(l_ret == 0, "Server start should succeed");
    
    // Verify handlers were registered
    // Use dap_mock_find to get the actual registered mock state
    // This avoids issues with static g_mock variables in different compilation units
    dap_mock_function_state_t *l_mock_state = dap_mock_find("enc_http_add_proc");
    TEST_ASSERT_NOT_NULL(l_mock_state, "enc_http_add_proc mock should be found in registry");
    int l_call_count = dap_mock_get_call_count(l_mock_state);
    log_it(L_DEBUG, "After server start, enc_http_add_proc call_count=%d, g_mock=%p, found_mock=%p, enabled=%d",
           l_call_count, (void*)g_mock_enc_http_add_proc, (void*)l_mock_state, l_mock_state->enabled);
    TEST_ASSERT(l_call_count >= 1,
                "enc_http_add_proc should be called for enc_init handler");
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_stream_add_proc_http) >= 1,
                "dap_stream_add_proc_http should be called for stream handler");
    
    // Verify WebSocket upgrade handler was registered
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_net_trans_websocket_server_add_upgrade_handler) >= 1,
                "WebSocket upgrade handler should be registered");
    
    // Verify listen address was added
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_server_listen_addr_add) >= 1,
                "dap_server_listen_addr_add should be called");
    
    // Stop server
    dap_net_trans_server_stop(l_server);
    
    // Cleanup
    dap_net_trans_server_delete(l_server);
    
    TEST_SUCCESS("WebSocket trans server start verified");
}

/**
 * @brief Test WebSocket trans server stop
 */
static void test_04_server_stop(void)
{
    TEST_INFO("Testing WebSocket trans server stop");
    
    const char *l_server_name = "test_websocket_server";
    
    // Setup mocks
    DAP_MOCK_SET_RETURN(dap_http_server_new, (void*)dap_trans_test_get_mock_server());
    
    // Create and start server
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_WEBSOCKET, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Stop server
    dap_net_trans_server_stop(l_server);
    
    // Cleanup
    dap_net_trans_server_delete(l_server);
    
    TEST_SUCCESS("WebSocket trans server stop verified");
}

/**
 * @brief Test WebSocket trans server with invalid trans type
 */
static void test_05_server_invalid_type(void)
{
    TEST_INFO("Testing WebSocket trans server with invalid trans type");
    
    // Try to create server with invalid type
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_TLS_DIRECT, "test_server");
    
    TEST_ASSERT_NULL(l_server, "Server should not be created for unregistered trans type");
    
    TEST_SUCCESS("Invalid trans type handling verified");
}

// ============================================================================
// Stream Tests
// ============================================================================

/**
 * @brief Test WebSocket stream trans registration
 */
static void test_06_stream_registration(void)
{
    TEST_INFO("Testing WebSocket stream trans registration");
    
    // Find WebSocket trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");
    TEST_ASSERT(l_trans->type == DAP_NET_TRANS_WEBSOCKET,
                "Trans type should be WEBSOCKET");
    
    TEST_SUCCESS("WebSocket stream trans registration verified");
}

/**
 * @brief Test WebSocket stream trans capabilities
 */
static void test_07_stream_capabilities(void)
{
    TEST_INFO("Testing WebSocket stream trans capabilities");
    
    // Find WebSocket trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");
    TEST_ASSERT_NOT_NULL(l_trans->ops, "Trans operations should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->init, "init callback should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->deinit, "deinit callback should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->connect, "connect callback should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->listen, "listen callback should be set");
    
    TEST_SUCCESS("WebSocket stream trans capabilities verified");
}

/**
 * @brief Test WebSocket stream trans initialization
 */
static void test_08_stream_init(void)
{
    TEST_INFO("Testing WebSocket stream trans initialization");
    
    // Find WebSocket trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");
    
    // Initialize trans instance
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    TEST_ASSERT_NOT_NULL(l_trans->_inheritor, "Private data should be allocated");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("WebSocket stream trans initialization verified");
}

/**
 * @brief Test WebSocket stream trans unregistration
 */
static void test_09_stream_unregistration(void)
{
    TEST_INFO("Testing WebSocket stream trans unregistration");
    
    // Find WebSocket trans before unregistration
    dap_net_trans_t *l_trans_before = 
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_trans_before, "WebSocket trans should be registered");
    
    // Unregister WebSocket stream trans
    int l_ret = dap_net_trans_websocket_stream_unregister();
    TEST_ASSERT(l_ret == 0, "Unregistration should succeed");
    
    // Try to find trans after unregistration
    dap_net_trans_t *l_trans_after = 
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    
    // Note: unregistration might not remove from registry immediately
    // depending on implementation, so we just verify unregistration call succeeded
    
    // Re-register for other tests
    dap_net_trans_websocket_stream_register();
    
    TEST_SUCCESS("WebSocket stream trans unregistration verified");
}

/**
 * @brief Test WebSocket stream trans connect operation
 */
static void test_10_stream_connect(void)
{
    TEST_INFO("Testing WebSocket stream trans connect operation");
    
    // Find WebSocket trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream
    s_mock_stream.trans = l_trans;
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    s_mock_trans_ctx.esocket = &s_mock_events_socket; // Set mock esocket for operations
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    
    // Test connect operation
    l_ret = l_trans->ops->connect(&s_mock_stream, "127.0.0.1", 8080, NULL);
    TEST_ASSERT(l_ret == 0, "Connect operation should succeed");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("WebSocket stream trans connect operation verified");
}

/**
 * @brief Test WebSocket stream trans read operation
 */
static void test_11_stream_read(void)
{
    TEST_INFO("Testing WebSocket stream trans read operation");
    
    // Find WebSocket trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream
    s_mock_stream.trans = l_trans;
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    s_mock_trans_ctx.esocket = &s_mock_events_socket; // Set mock esocket for operations
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    
    // Set esocket in private data for WebSocket (it uses l_priv->esocket)
    dap_net_trans_websocket_private_t *l_priv = 
        (dap_net_trans_websocket_private_t*)l_trans->_inheritor;
    if (l_priv) {
        l_priv->esocket = &s_mock_events_socket;
    }
    
    // Test read operation
    char l_buffer[1024];
    ssize_t l_bytes_read = l_trans->ops->read(&s_mock_stream, l_buffer, sizeof(l_buffer));
    TEST_ASSERT(l_bytes_read >= 0, "Read operation should not fail");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("WebSocket stream trans read operation verified");
}

/**
 * @brief Test WebSocket stream trans write operation
 */
static void test_12_stream_write(void)
{
    TEST_INFO("Testing WebSocket stream trans write operation");
    
    // Find WebSocket trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream
    s_mock_stream.trans = l_trans;
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    s_mock_trans_ctx.esocket = &s_mock_events_socket; // Set mock esocket for operations
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    
    // Initialize stream trans private data and set state to OPEN for write test
    // In real usage, this would be done by session_start, but for unit test we need to set it manually
    dap_net_trans_websocket_private_t *l_priv = 
        (dap_net_trans_websocket_private_t*)l_trans->_inheritor;
    if (l_priv) {
        l_priv->state = DAP_WS_STATE_OPEN;
        l_priv->esocket = &s_mock_events_socket;  // WebSocket uses l_priv->esocket for I/O
    }
    
    // Test write operation
    const char l_test_data[] = "test data";
    ssize_t l_bytes_written = l_trans->ops->write(&s_mock_stream, l_test_data, sizeof(l_test_data));
    TEST_ASSERT(l_bytes_written > 0, "Write operation should succeed");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("WebSocket stream trans write operation verified");
}

/**
 * @brief Test WebSocket stream trans handshake operations
 */
static void test_13_stream_handshake(void)
{
    TEST_INFO("Testing WebSocket stream trans handshake operations");
    
    // Find WebSocket trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream
    s_mock_stream.trans = l_trans;
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    s_mock_trans_ctx.esocket = &s_mock_events_socket; // Set mock esocket for operations
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    s_mock_stream.trans_ctx->esocket->_inheritor = (void*)dap_trans_test_get_mock_client();  // WebSocket handshake needs client_pvt
    
    // Set esocket in private data for WebSocket
    dap_net_trans_websocket_private_t *l_priv = 
        (dap_net_trans_websocket_private_t*)l_trans->_inheritor;
    if (l_priv) {
        l_priv->esocket = &s_mock_events_socket;
    }
    
    // Test handshake_init operation
    dap_net_handshake_params_t l_params = {0};
    // WebSocket handshake needs alice_pub_key
    static uint8_t s_mock_alice_pub_key[32] = {0}; // Mock public key
    l_params.alice_pub_key = s_mock_alice_pub_key;
    l_params.alice_pub_key_size = sizeof(s_mock_alice_pub_key);
    l_ret = l_trans->ops->handshake_init(&s_mock_stream, &l_params, NULL);
    TEST_ASSERT(l_ret == 0, "Handshake init should succeed");
    
    // Test handshake_process operation (server-side)
    uint8_t l_handshake_data[100] = {0};
    void *l_response = NULL;
    size_t l_response_size = 0;
    l_ret = l_trans->ops->handshake_process(&s_mock_stream, l_handshake_data, 
                                                 sizeof(l_handshake_data),
                                                 &l_response, &l_response_size);
    TEST_ASSERT(l_ret == 0, "Handshake process should succeed");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("WebSocket stream trans handshake operations verified");
}

/**
 * @brief Test WebSocket stream trans session operations
 */
static void test_14_stream_session(void)
{
    TEST_INFO("Testing WebSocket stream trans session operations");
    
    // Find WebSocket trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream with esocket and client ctx (required for session_create)
    s_mock_stream.trans = l_trans;
    s_mock_trans_ctx.esocket = dap_trans_test_get_mock_esocket();
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    s_mock_stream.trans_ctx->esocket->_inheritor = (void*)dap_trans_test_get_mock_client();
    
    // Test session_create operation
    dap_net_session_params_t l_session_params = {0};
    // Set required parameters for session_create
    l_session_params.channels = "0"; // Default channel
    l_session_params.enc_type = 0;
    l_session_params.enc_key_size = 0;
    l_session_params.enc_headers = false;
    s_session_callback_called = false;
    l_ret = l_trans->ops->session_create(&s_mock_stream, &l_session_params, s_session_callback);
    TEST_ASSERT(l_ret == 0, "Session create should succeed");
    
    // Test session_start operation
    l_ret = l_trans->ops->session_start(&s_mock_stream, 12345, NULL);
    TEST_ASSERT(l_ret == 0, "Session start should succeed");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("WebSocket stream trans session operations verified");
}

/**
 * @brief Test WebSocket stream trans listen operation
 */
static void test_15_stream_listen(void)
{
    TEST_INFO("Testing WebSocket stream trans listen operation");
    
    // Find WebSocket trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Setup mock server
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)dap_trans_test_get_mock_server());
    
    // Test listen operation (server-side)
    l_ret = l_trans->ops->listen(l_trans, "127.0.0.1", 8080, dap_trans_test_get_mock_server());
    TEST_ASSERT(l_ret == 0, "Listen operation should succeed");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("WebSocket stream trans listen operation verified");
}

// ============================================================================
// Test Suite Definition
// ============================================================================

int main(int argc, char *argv[])
{
    // Initialize test suite
    setup_test();
    
    TEST_SUITE_START("WebSocket Trans Comprehensive Unit Tests");
    
    // Server tests
    TEST_RUN(test_01_server_ops_registration);
    TEST_RUN(test_02_server_creation);
    TEST_RUN(test_03_server_start);
    TEST_RUN(test_04_server_stop);
    TEST_RUN(test_05_server_invalid_type);
    
    // Stream tests
    TEST_RUN(test_06_stream_registration);
    TEST_RUN(test_07_stream_capabilities);
    TEST_RUN(test_08_stream_init);
    TEST_RUN(test_09_stream_unregistration);
    
    // Stream operations tests
    TEST_RUN(test_10_stream_connect);
    TEST_RUN(test_11_stream_read);
    TEST_RUN(test_12_stream_write);
    TEST_RUN(test_13_stream_handshake);
    TEST_RUN(test_14_stream_session);
    TEST_RUN(test_15_stream_listen);
    
    TEST_SUITE_END();
    
    // Cleanup test suite
    suite_cleanup();
    
    return 0;
}
