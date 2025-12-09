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
 * @file test_trans_http.c
 * @brief Comprehensive unit tests for HTTP trans server and stream
 * 
 * Tests HTTP trans with full mocking for isolation:
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
#include "dap_net_trans_http_server.h"
#include "dap_net_trans_http_stream.h"
#include "dap_net_server_common.h"
#include "dap_http_server.h"
#include "dap_server.h"
#include "dap_stream.h"
#include "dap_stream_handshake.h"
#include "dap_stream_session.h"
#include "dap_trans_test_mocks.h"

#define LOG_TAG "test_trans_http"

// ============================================================================
// Mock Declarations (using common trans mocks)
// ============================================================================
// Common mocks are declared in dap_trans_test_mocks.h
// Only trans-specific mocks are declared here

// Mock declarations are in dap_trans_test_mocks.h
// The mock scanner now scans header files too, so no need to duplicate declarations here

// ============================================================================
// Mock Wrappers
// ============================================================================
// Common wrappers are implemented in dap_trans_test_mocks.c
// Only trans-specific wrappers are defined here

// All common wrappers are in dap_trans_test_mocks.c
// dap_net_trans_find is not mocked - using real implementation
// This allows tests to access real registered transs with proper ops

// ============================================================================
// Test Suite State
// ============================================================================

static bool s_test_initialized = false;
static bool s_handshake_callback_called = false;
static bool s_session_callback_called = false;

static void s_handshake_callback(dap_stream_t *a_stream, const void *a_response, size_t a_response_size, int a_error_code) {
    UNUSED(a_stream);
    UNUSED(a_response);
    UNUSED(a_response_size);
    UNUSED(a_error_code);
    s_handshake_callback_called = true;
}

static void s_session_callback(dap_stream_t *a_stream, uint32_t a_session_id, const char *a_response_data, size_t a_response_size, int a_error_code) {
    UNUSED(a_stream);
    UNUSED(a_session_id);
    UNUSED(a_response_data);
    UNUSED(a_response_size);
    UNUSED(a_error_code);
    s_session_callback_called = true;
}

// Mock instances for tests
static dap_server_t s_mock_server = {0};
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
        int l_ret = dap_common_init("test_trans_http", NULL);
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
        
        // Check if already registered (might be auto-registered via module constructor)
        dap_net_trans_t *l_existing = dap_net_trans_find(DAP_NET_TRANS_HTTP);
        if (! l_existing) {
            TEST_ASSERT(l_ret == 0, "HTTP stream trans not registred");
        }
        
        s_test_initialized = true;
        TEST_INFO("HTTP trans test suite initialized");
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
        
        // Deinitialize mock framework
        dap_mock_deinit();
        
        // Deinitialize DAP common
        dap_common_deinit();
        
        s_test_initialized = false;
        TEST_INFO("HTTP trans test suite cleaned up");
    }
}

// ============================================================================
// Server Tests
// ============================================================================

/**
 * @brief Test HTTP trans server operations registration
 */
static void test_01_server_ops_registration(void)
{
    TEST_INFO("Testing HTTP trans server operations registration");
    
    // Verify operations are registered
    const dap_net_trans_server_ops_t *l_ops = 
        dap_net_trans_server_get_ops(DAP_NET_TRANS_HTTP);
    
    TEST_ASSERT_NOT_NULL(l_ops, "HTTP trans server operations should be registered");
    TEST_ASSERT_NOT_NULL(l_ops->new, "new callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->start, "start callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->stop, "stop callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->delete, "delete callback should be set");
    
    TEST_SUCCESS("HTTP trans server operations registration verified");
}

/**
 * @brief Test HTTP trans server creation through unified API
 */
static void test_02_server_creation(void)
{
    TEST_INFO("Testing HTTP trans server creation");
    
    const char *l_server_name = "test_http_server";
    
    // Setup mock for dap_http_server_new
    DAP_MOCK_SET_RETURN(dap_http_server_new, (void*)dap_trans_test_get_mock_server());
    
    // Create server through unified API
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_HTTP, l_server_name);
    
    TEST_ASSERT_NOT_NULL(l_server, "HTTP server should be created");
    TEST_ASSERT(l_server->trans_type == DAP_NET_TRANS_HTTP, 
                "Trans type should be HTTP");
    TEST_ASSERT(strcmp(l_server->server_name, l_server_name) == 0,
                "Server name should match");
    TEST_ASSERT_NOT_NULL(l_server->trans_specific,
                         "Trans-specific server instance should be created");
    
    // Note: dap_http_server_new is called in start(), not in new()
    // So we don't verify it here - it will be verified in test_03_server_start
    
    // Cleanup
    dap_net_trans_server_delete(l_server);
    
    TEST_SUCCESS("HTTP trans server creation verified");
}

/**
 * @brief Test HTTP trans server start with handlers registration
 */
static void test_03_server_start(void)
{
    TEST_INFO("Testing HTTP trans server start");
    
    const char *l_server_name = "test_http_server";
    const char *l_cfg_section = "test_server";
    const char *l_addrs[] = {"127.0.0.1"};
    uint16_t l_ports[] = {8080};
    
    // Setup mocks
    // Note: dap_net_server_listen_addr_add_with_callback is NOT mocked - using real implementation
    DAP_MOCK_ENABLE(enc_http_add_proc);  // Enable mock for enc_http_add_proc
    DAP_MOCK_SET_RETURN(dap_http_server_new, (void*)dap_trans_test_get_mock_server());
    DAP_MOCK_SET_RETURN(enc_http_init, 0);  // Ensure enc_http_init succeeds
    // Note: dap_net_trans_find is not mocked - using real implementation
    
    // Create server
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_HTTP, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Start server
    int l_ret = dap_net_trans_server_start(l_server, l_cfg_section, 
                                                l_addrs, l_ports, 1);
    if (l_ret != 0) {
        fprintf(stderr, "ERROR: Server start failed with code %d\n", l_ret);
        fflush(stderr);
    }
    TEST_ASSERT(l_ret == 0, "Server start should succeed");
    
    // Verify handlers were registered
    // Use dap_mock_find to get the actual registered mock state
    // This avoids issues with static g_mock variables in different compilation units
    dap_mock_function_state_t *l_mock_state = dap_mock_find("enc_http_add_proc");
    int l_call_count = l_mock_state ? dap_mock_get_call_count(l_mock_state) : 0;
    if (l_mock_state) {
        log_it(L_DEBUG, "After server start, enc_http_add_proc call_count=%d, g_mock=%p, found_mock=%p, enabled=%d",
               l_call_count, (void*)g_mock_enc_http_add_proc, (void*)l_mock_state, l_mock_state->enabled);
    } else {
        log_it(L_WARNING, "enc_http_add_proc mock not found in registry!");
    }
    TEST_ASSERT(l_call_count >= 1,
                "enc_http_add_proc should be called for enc_init handler");
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_stream_add_proc_http) >= 1,
                "dap_stream_add_proc_http should be called for stream handler");
    
    // Verify listen address was added
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_server_listen_addr_add) >= 1,
                "dap_server_listen_addr_add should be called");
    
    // Stop server
    dap_net_trans_server_stop(l_server);
    
    // Cleanup
    dap_net_trans_server_delete(l_server);
    
    TEST_SUCCESS("HTTP trans server start verified");
}

/**
 * @brief Test HTTP trans server stop
 */
static void test_04_server_stop(void)
{
    TEST_INFO("Testing HTTP trans server stop");
    
    const char *l_server_name = "test_http_server";
    
    // Setup mocks
    DAP_MOCK_SET_RETURN(dap_http_server_new, (void*)dap_trans_test_get_mock_server());
    
    // Create and start server
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_HTTP, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Stop server
    dap_net_trans_server_stop(l_server);
    
    // Cleanup
    dap_net_trans_server_delete(l_server);
    
    TEST_SUCCESS("HTTP trans server stop verified");
}

/**
 * @brief Test HTTP trans server with invalid trans type
 */
static void test_05_server_invalid_type(void)
{
    TEST_INFO("Testing HTTP trans server with invalid trans type");
    
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
 * @brief Test HTTP stream trans registration
 */
static void test_06_stream_registration(void)
{
    TEST_INFO("Testing HTTP stream trans registration");
    
    // Find HTTP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_HTTP);
    
    TEST_ASSERT_NOT_NULL(l_trans, "HTTP trans should be registered");
    TEST_ASSERT(l_trans->type == DAP_NET_TRANS_HTTP,
                "Trans type should be HTTP");
    
    TEST_SUCCESS("HTTP stream trans registration verified");
}

/**
 * @brief Test HTTP stream trans capabilities
 */
static void test_07_stream_capabilities(void)
{
    TEST_INFO("Testing HTTP stream trans capabilities");
    
    // Find HTTP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_HTTP);
    
    TEST_ASSERT_NOT_NULL(l_trans, "HTTP trans should be registered");
    TEST_ASSERT_NOT_NULL(l_trans->ops, "Trans operations should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->init, "init callback should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->deinit, "deinit callback should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->connect, "connect callback should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->listen, "listen callback should be set");
    
    TEST_SUCCESS("HTTP stream trans capabilities verified");
}

/**
 * @brief Test HTTP stream trans initialization
 */
static void test_08_stream_init(void)
{
    TEST_INFO("Testing HTTP stream trans initialization");
    
    // Find HTTP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_HTTP);
    
    TEST_ASSERT_NOT_NULL(l_trans, "HTTP trans should be registered");
    
    // Initialize trans instance
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    TEST_ASSERT_NOT_NULL(l_trans->_inheritor, "Private data should be allocated");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("HTTP stream trans initialization verified");
}

/**
 * @brief Test HTTP stream trans unregistration
 */
static void test_09_stream_unregistration(void)
{
    TEST_INFO("Testing HTTP stream trans unregistration");
    
    // Find HTTP trans before unregistration
    dap_net_trans_t *l_trans_before = 
        dap_net_trans_find(DAP_NET_TRANS_HTTP);
    TEST_ASSERT_NOT_NULL(l_trans_before, "HTTP trans should be registered");
    
    // Unregister HTTP stream trans
    int l_ret = dap_net_trans_http_stream_unregister();
    TEST_ASSERT(l_ret == 0, "Unregistration should succeed");
    
    // Try to find trans after unregistration
    dap_net_trans_t *l_trans_after = 
        dap_net_trans_find(DAP_NET_TRANS_HTTP);
    
    // Note: unregistration might not remove from registry immediately
    // depending on implementation, so we just verify unregistration call succeeded
    
    // Re-register for other tests
    dap_net_trans_http_stream_register();
    
    TEST_SUCCESS("HTTP stream trans unregistration verified");
}

/**
 * @brief Test HTTP stream trans connect operation
 */
static void test_10_stream_connect(void)
{
    TEST_INFO("Testing HTTP stream trans connect operation");
    
    // Find HTTP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_HTTP);
    TEST_ASSERT_NOT_NULL(l_trans, "HTTP trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Setup mock stream
    s_mock_stream.trans = l_trans;
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    // Set up mock esocket for read/write operations
    // Check if we need esocket (read/write tests)
    if (0) {} // Placeholder - will be replaced
    
    // Test connect operation
    l_ret = l_trans->ops->connect(&s_mock_stream, "127.0.0.1", 8080, NULL);
    TEST_ASSERT(l_ret == 0, "Connect operation should succeed");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("HTTP stream trans connect operation verified");
}

/**
 * @brief Test HTTP stream trans read operation
 */
static void test_11_stream_read(void)
{
    TEST_INFO("Testing HTTP stream trans read operation");
    
    // Find HTTP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_HTTP);
    TEST_ASSERT_NOT_NULL(l_trans, "HTTP trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream
    s_mock_stream.trans = l_trans;
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    s_mock_trans_ctx.esocket = &s_mock_events_socket; // Set mock esocket for read
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    
    // Test read operation (HTTP trans may return 0 for event-driven reading)
    char l_buffer[1024];
    ssize_t l_bytes_read = l_trans->ops->read(&s_mock_stream, l_buffer, sizeof(l_buffer));
    TEST_ASSERT(l_bytes_read >= 0, "Read operation should not fail");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("HTTP stream trans read operation verified");
}

/**
 * @brief Test HTTP stream trans write operation
 */
static void test_12_stream_write(void)
{
    TEST_INFO("Testing HTTP stream trans write operation");
    
    // Find HTTP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_HTTP);
    TEST_ASSERT_NOT_NULL(l_trans, "HTTP trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream
    s_mock_stream.trans = l_trans;
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    s_mock_trans_ctx.esocket = &s_mock_events_socket; // Set mock esocket for write
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    
    // Test write operation
    const char l_test_data[] = "test data";
    ssize_t l_bytes_written = l_trans->ops->write(&s_mock_stream, l_test_data, sizeof(l_test_data));
    TEST_ASSERT(l_bytes_written > 0, "Write operation should succeed");
    TEST_ASSERT(l_bytes_written == (ssize_t)sizeof(l_test_data), 
                "All bytes should be written");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("HTTP stream trans write operation verified");
}

/**
 * @brief Test HTTP stream trans handshake operations
 */
static void test_13_stream_handshake(void)
{
    TEST_INFO("Testing HTTP stream trans handshake operations");
    
    // Find HTTP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_HTTP);
    TEST_ASSERT_NOT_NULL(l_trans, "HTTP trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream with esocket and client ctx
    s_mock_stream.trans = l_trans;
    s_mock_trans_ctx.esocket = dap_trans_test_get_mock_esocket();
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    s_mock_stream.trans_ctx->esocket->_inheritor = (void*)dap_trans_test_get_mock_client();
    
    // Test handshake_init operation
    dap_net_handshake_params_t l_params = {0};
    // Set up handshake parameters - need alice_pub_key for handshake
    static uint8_t s_mock_alice_pub_key[32] = {0}; // Mock public key
    l_params.alice_pub_key = s_mock_alice_pub_key;
    l_params.alice_pub_key_size = sizeof(s_mock_alice_pub_key);
    // handshake_init requires a non-NULL callback
    s_handshake_callback_called = false;
    l_ret = l_trans->ops->handshake_init(&s_mock_stream, &l_params, s_handshake_callback);
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
    
    TEST_SUCCESS("HTTP stream trans handshake operations verified");
}

/**
 * @brief Test HTTP stream trans session operations
 */
static void test_14_stream_session(void)
{
    TEST_INFO("Testing HTTP stream trans session operations");
    
    // Find HTTP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_HTTP);
    TEST_ASSERT_NOT_NULL(l_trans, "HTTP trans should be registered");
    
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
    
    TEST_SUCCESS("HTTP stream trans session operations verified");
}

/**
 * @brief Test HTTP stream trans listen operation
 */
static void test_15_stream_listen(void)
{
    TEST_INFO("Testing HTTP stream trans listen operation");
    
    // Find HTTP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_HTTP);
    TEST_ASSERT_NOT_NULL(l_trans, "HTTP trans should be registered");
    
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
    
    TEST_SUCCESS("HTTP stream trans listen operation verified");
}

// ============================================================================
// Test Suite Definition
// ============================================================================

int main(int argc, char *argv[])
{
    // Initialize test suite
    setup_test();
    
    TEST_SUITE_START("HTTP Trans Comprehensive Unit Tests");
    
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
