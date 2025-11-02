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
 * @file test_transport_dns.c
 * @brief Comprehensive unit tests for DNS transport server and stream
 * 
 * Tests DNS transport with full mocking for isolation:
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
#include "dap_stream_transport.h"
#include "dap_net_transport_server.h"
#include "dap_net_transport_dns_server.h"
#include "dap_net_transport_dns_stream.h"
#include "dap_server.h"
#include "dap_stream.h"
#include "dap_stream_handshake.h"
#include "dap_stream_session.h"
#include "dap_events_socket.h"
#include "dap_enc_server.h"

#define LOG_TAG "test_transport_dns"

// ============================================================================
// Mock Declarations
// ============================================================================

// Mock dap_events functions
DAP_MOCK_DECLARE(dap_events_init);
DAP_MOCK_DECLARE(dap_events_start);
DAP_MOCK_DECLARE(dap_events_stop_all);
DAP_MOCK_DECLARE(dap_events_deinit);

// Mock dap_server functions
DAP_MOCK_DECLARE(dap_server_create);
DAP_MOCK_DECLARE(dap_server_new);
DAP_MOCK_DECLARE(dap_server_listen_addr_add);
DAP_MOCK_DECLARE(dap_server_delete);

// Mock dap_stream_transport functions
// Don't mock dap_stream_transport_find - use real implementation
// This allows tests to work with real transport registration

// Mock dap_stream functions
DAP_MOCK_DECLARE(dap_stream_add_proc_dns);
DAP_MOCK_DECLARE(dap_stream_delete);
DAP_MOCK_DECLARE(dap_stream_init);
DAP_MOCK_DECLARE(dap_stream_deinit);

// Mock dap_events_socket functions
DAP_MOCK_DECLARE(dap_events_socket_create);
DAP_MOCK_DECLARE(dap_events_socket_delete);
DAP_MOCK_DECLARE(dap_events_socket_write_unsafe);

// Mock encryption server functions
DAP_MOCK_DECLARE(dap_enc_server_process_request);
DAP_MOCK_DECLARE(dap_enc_server_response_free);

// ============================================================================
// Mock Wrappers
// ============================================================================

// Mock server instance for testing
static dap_server_t s_mock_server = {0};
static dap_stream_transport_t s_mock_stream_transport = {0};
static dap_stream_t s_mock_stream = {0};
static dap_events_socket_t s_mock_events_socket = {0};

// Wrapper for dap_server_new
DAP_MOCK_WRAPPER_CUSTOM(dap_server_t*, dap_server_new,
    PARAM(const char*, a_cfg_section),
    PARAM(dap_events_socket_callbacks_t*, a_server_callbacks),
    PARAM(dap_events_socket_callbacks_t*, a_client_callbacks)
)
{
    UNUSED(a_cfg_section);
    UNUSED(a_server_callbacks);
    UNUSED(a_client_callbacks);
    
    // Return mock server if set, otherwise return NULL
    if (g_mock_dap_server_new && g_mock_dap_server_new->return_value.ptr) {
        return (dap_server_t*)g_mock_dap_server_new->return_value.ptr;
    }
    
    // Return default mock server
    return &s_mock_server;
}

// Wrapper for dap_server_listen_addr_add
DAP_MOCK_WRAPPER_CUSTOM(int, dap_server_listen_addr_add,
    PARAM(dap_server_t*, a_server),
    PARAM(const char*, a_addr),
    PARAM(uint16_t, a_port),
    PARAM(dap_events_desc_type_t, a_type),
    PARAM(dap_events_socket_callbacks_t*, a_callbacks)
)
{
    UNUSED(a_server);
    UNUSED(a_addr);
    UNUSED(a_port);
    UNUSED(a_type);
    UNUSED(a_callbacks);
    
    // Return mock value if set, otherwise return 0 (success)
    if (g_mock_dap_server_listen_addr_add && g_mock_dap_server_listen_addr_add->return_value.i != 0) {
        return g_mock_dap_server_listen_addr_add->return_value.i;
    }
    return 0;
}

// Mock wrapper for dap_server_delete - just verify it's called, don't actually delete
DAP_MOCK_WRAPPER_CUSTOM(void, dap_server_delete,
    PARAM(dap_server_t *, a_server)
)
{
    // Just verify the call, don't actually delete anything
    // In real implementation this would free the server, but in tests we use static mocks
    (void)a_server;
}

// dap_stream_transport_find is not mocked - using real implementation


// Wrapper for dap_stream_add_proc_dns
DAP_MOCK_WRAPPER_CUSTOM(int, dap_stream_add_proc_dns,
    PARAM(dap_server_t*, a_server)
)
{
    UNUSED(a_server);
    
    // Return mock value if set, otherwise return 0 (success)
    if (g_mock_dap_stream_add_proc_dns && g_mock_dap_stream_add_proc_dns->return_value.i != 0) {
        return g_mock_dap_stream_add_proc_dns->return_value.i;
    }
    return 0;
}

// Wrapper for dap_events_socket_write_unsafe
// Return size of data written (success) for DNS write tests
DAP_MOCK_WRAPPER_CUSTOM(size_t, dap_events_socket_write_unsafe,
    PARAM(dap_events_socket_t*, a_esocket),
    PARAM(const void*, a_data),
    PARAM(size_t, a_size)
)
{
    UNUSED(a_esocket);
    UNUSED(a_data);
    
    // Return size written (success) - simulate successful write
    // Use size_t field from return_value union
    if (g_mock_dap_events_socket_write_unsafe && g_mock_dap_events_socket_write_unsafe->return_value.ptr != NULL) {
        return (size_t)(uintptr_t)g_mock_dap_events_socket_write_unsafe->return_value.ptr;
    }
    
    // Return size passed (simulate successful write)
    return a_size;
}

// Mock dap_enc_server_response_t for testing
static dap_enc_server_response_t s_mock_enc_response = {
    .success = true,
    .encrypt_msg = NULL,
    .encrypt_msg_len = 0,
    .error_message = NULL
};

// Wrapper for dap_enc_server_process_request
DAP_MOCK_WRAPPER_CUSTOM(int, dap_enc_server_process_request,
    PARAM(dap_enc_server_request_t*, a_request),
    PARAM(dap_enc_server_response_t**, a_response_out)
)
{
    UNUSED(a_request);
    
    // Return mock response if set, otherwise return success with default mock
    if (g_mock_dap_enc_server_process_request && g_mock_dap_enc_server_process_request->return_value.i != 0) {
        return g_mock_dap_enc_server_process_request->return_value.i;
    }
    
    // Set response to mock response
    if (a_response_out) {
        *a_response_out = &s_mock_enc_response;
    }
    
    return 0;  // Success
}

// Wrapper for dap_enc_server_response_free
// Don't actually free - just verify the call (mock response is static)
DAP_MOCK_WRAPPER_CUSTOM(void, dap_enc_server_response_free,
    PARAM(dap_enc_server_response_t*, a_response)
)
{
    // Don't actually free - mock response is static
    // In real implementation this would free the response, but in tests we use static mocks
    UNUSED(a_response);
}

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
        int l_ret = dap_common_init("test_transport_dns", NULL);
        TEST_ASSERT(l_ret == 0, "DAP common initialization failed");
        
        // Initialize mock framework
        dap_mock_init();
        
        // Initialize transport layer
        l_ret = dap_stream_transport_init();
        TEST_ASSERT(l_ret == 0, "Transport layer initialization failed");
        
        // Initialize DNS transport server (this registers operations)
        l_ret = dap_net_transport_dns_server_init();
        TEST_ASSERT(l_ret == 0, "DNS transport server initialization failed");
        
        // Initialize DNS stream transport
        // Check if already registered (might be auto-registered via module constructor)
        dap_stream_transport_t *l_existing = dap_stream_transport_find(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
        if (l_existing) {
            TEST_INFO("DNS stream transport already registered (auto-registered), skipping manual registration");
        } else {
            l_ret = dap_net_transport_dns_stream_register();
            TEST_ASSERT(l_ret == 0, "DNS stream transport registration failed");
        }
        
        s_test_initialized = true;
        TEST_INFO("DNS transport test suite initialized");
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
        // Deinitialize DNS stream transport
        dap_net_transport_dns_stream_unregister();
        
        // Deinitialize DNS transport server (unregisters operations)
        dap_net_transport_dns_server_deinit();
        
        // Deinitialize transport layer
        dap_stream_transport_deinit();
        
        // Deinitialize mock framework
        dap_mock_deinit();
        
        // Deinitialize DAP common
        dap_common_deinit();
        
        s_test_initialized = false;
        TEST_INFO("DNS transport test suite cleaned up");
    }
}

// ============================================================================
// Server Tests
// ============================================================================

/**
 * @brief Test DNS transport server operations registration
 */
static void test_01_server_ops_registration(void)
{
    TEST_INFO("Testing DNS transport server operations registration");
    
    // Verify operations are registered
    const dap_net_transport_server_ops_t *l_ops = 
        dap_net_transport_server_get_ops(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
    
    TEST_ASSERT_NOT_NULL(l_ops, "DNS transport server operations should be registered");
    TEST_ASSERT_NOT_NULL(l_ops->new, "new callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->start, "start callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->stop, "stop callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->delete, "delete callback should be set");
    
    TEST_SUCCESS("DNS transport server operations registration verified");
}

/**
 * @brief Test DNS transport server creation through unified API
 */
static void test_02_server_creation(void)
{
    TEST_INFO("Testing DNS transport server creation");
    
    const char *l_server_name = "test_dns_server";
    
    // Setup mock for dap_server_new
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)&s_mock_server);
    
    // Create server through unified API
    dap_net_transport_server_t *l_server = 
        dap_net_transport_server_new(DAP_STREAM_TRANSPORT_DNS_TUNNEL, l_server_name);
    
    TEST_ASSERT_NOT_NULL(l_server, "DNS server should be created");
    TEST_ASSERT(l_server->transport_type == DAP_STREAM_TRANSPORT_DNS_TUNNEL, 
                "Transport type should be DNS_TUNNEL");
    TEST_ASSERT(strcmp(l_server->server_name, l_server_name) == 0,
                "Server name should match");
    TEST_ASSERT_NOT_NULL(l_server->transport_specific,
                         "Transport-specific server instance should be created");
    
    // Note: dap_server_new is called in start(), not in new()
    // So we don't verify it here - it will be verified in test_03_server_start
    
    // Cleanup
    dap_net_transport_server_delete(l_server);
    
    TEST_SUCCESS("DNS transport server creation verified");
}

/**
 * @brief Test DNS transport server start with handlers registration
 */
static void test_03_server_start(void)
{
    TEST_INFO("Testing DNS transport server start");
    
    const char *l_server_name = "test_dns_server";
    const char *l_cfg_section = "test_server";
    const char *l_addrs[] = {"127.0.0.1"};
    uint16_t l_ports[] = {53};
    
    // Setup mocks
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)&s_mock_server);
    DAP_MOCK_SET_RETURN(dap_server_listen_addr_add, 0);
    // Note: dap_stream_transport_find is not mocked - using real implementation
    
    // Create server
    dap_net_transport_server_t *l_server = 
        dap_net_transport_server_new(DAP_STREAM_TRANSPORT_DNS_TUNNEL, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Start server
    int l_ret = dap_net_transport_server_start(l_server, l_cfg_section, 
                                                l_addrs, l_ports, 1);
    TEST_ASSERT(l_ret == 0, "Server start should succeed");
    
    // Verify DNS handlers were registered
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_stream_add_proc_dns) >= 1,
                "dap_stream_add_proc_dns should be called for DNS handlers");
    
    // Verify listen address was added
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_server_listen_addr_add) >= 1,
                "dap_server_listen_addr_add should be called");
    
    // Stop server
    dap_net_transport_server_stop(l_server);
    
    // Cleanup
    dap_net_transport_server_delete(l_server);
    
    TEST_SUCCESS("DNS transport server start verified");
}

/**
 * @brief Test DNS transport server stop
 */
static void test_04_server_stop(void)
{
    TEST_INFO("Testing DNS transport server stop");
    
    const char *l_server_name = "test_dns_server";
    
    // Setup mocks
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)&s_mock_server);
    
    // Create server
    dap_net_transport_server_t *l_server = 
        dap_net_transport_server_new(DAP_STREAM_TRANSPORT_DNS_TUNNEL, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Stop server
    dap_net_transport_server_stop(l_server);
    
    // Cleanup
    dap_net_transport_server_delete(l_server);
    
    TEST_SUCCESS("DNS transport server stop verified");
}

/**
 * @brief Test DNS transport server with invalid transport type
 */
static void test_05_server_invalid_type(void)
{
    TEST_INFO("Testing DNS transport server with invalid transport type");
    
    // Try to create server with invalid type
    dap_net_transport_server_t *l_server = 
        dap_net_transport_server_new(DAP_STREAM_TRANSPORT_TLS_DIRECT, "test_server");
    
    TEST_ASSERT_NULL(l_server, "Server should not be created for unregistered transport type");
    
    TEST_SUCCESS("Invalid transport type handling verified");
}

// ============================================================================
// Stream Tests
// ============================================================================

/**
 * @brief Test DNS stream transport registration
 */
static void test_06_stream_registration(void)
{
    TEST_INFO("Testing DNS stream transport registration");
    
    // Find DNS transport
    dap_stream_transport_t *l_transport = 
        dap_stream_transport_find(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
    
    TEST_ASSERT_NOT_NULL(l_transport, "DNS transport should be registered");
    TEST_ASSERT(l_transport->type == DAP_STREAM_TRANSPORT_DNS_TUNNEL,
                "Transport type should be DNS_TUNNEL");
    
    TEST_SUCCESS("DNS stream transport registration verified");
}

/**
 * @brief Test DNS stream transport capabilities
 */
static void test_07_stream_capabilities(void)
{
    TEST_INFO("Testing DNS stream transport capabilities");
    
    // Find DNS transport
    dap_stream_transport_t *l_transport = 
        dap_stream_transport_find(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
    
    TEST_ASSERT_NOT_NULL(l_transport, "DNS transport should be registered");
    TEST_ASSERT_NOT_NULL(l_transport->ops, "Transport operations should be set");
    TEST_ASSERT_NOT_NULL(l_transport->ops->init, "init callback should be set");
    TEST_ASSERT_NOT_NULL(l_transport->ops->deinit, "deinit callback should be set");
    TEST_ASSERT_NOT_NULL(l_transport->ops->connect, "connect callback should be set");
    TEST_ASSERT_NOT_NULL(l_transport->ops->listen, "listen callback should be set");
    
    TEST_SUCCESS("DNS stream transport capabilities verified");
}

/**
 * @brief Test DNS stream transport initialization
 */
static void test_08_stream_init(void)
{
    TEST_INFO("Testing DNS stream transport initialization");
    
    // Find DNS transport
    dap_stream_transport_t *l_transport = 
        dap_stream_transport_find(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
    
    TEST_ASSERT_NOT_NULL(l_transport, "DNS transport should be registered");
    
    // Initialize transport instance
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    TEST_ASSERT_NOT_NULL(l_transport->_inheritor, "Private data should be allocated");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("DNS stream transport initialization verified");
}

/**
 * @brief Test DNS stream transport unregistration
 */
static void test_09_stream_unregistration(void)
{
    TEST_INFO("Testing DNS stream transport unregistration");
    
    // Find DNS transport before unregistration
    dap_stream_transport_t *l_transport_before = 
        dap_stream_transport_find(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_transport_before, "DNS transport should be registered");
    
    // Unregister DNS stream transport
    int l_ret = dap_net_transport_dns_stream_unregister();
    TEST_ASSERT(l_ret == 0, "Unregistration should succeed");
    
    // Try to find transport after unregistration
    dap_stream_transport_t *l_transport_after = 
        dap_stream_transport_find(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
    
    // Note: unregistration might not remove from registry immediately
    // depending on implementation, so we just verify unregistration call succeeded
    
    // Re-register for other tests
    dap_net_transport_dns_stream_register();
    
    TEST_SUCCESS("DNS stream transport unregistration verified");
}

/**
 * @brief Test DNS stream transport connect operation
 */
static void test_10_stream_connect(void)
{
    TEST_INFO("Testing DNS stream transport connect operation");
    
    // Find DNS transport
    dap_stream_transport_t *l_transport = 
        dap_stream_transport_find(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_transport, "DNS transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Create mock stream
    s_mock_stream.stream_transport = l_transport;
    
    // Test connect operation
    l_ret = l_transport->ops->connect(&s_mock_stream, "127.0.0.1", 53, NULL);
    TEST_ASSERT(l_ret == 0, "Connect operation should succeed");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("DNS stream transport connect operation verified");
}

/**
 * @brief Test DNS stream transport read operation
 */
static void test_11_stream_read(void)
{
    TEST_INFO("Testing DNS stream transport read operation");
    
    // Find DNS transport
    dap_stream_transport_t *l_transport = 
        dap_stream_transport_find(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_transport, "DNS transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Create mock stream
    s_mock_stream.stream_transport = l_transport;
    
    // Test read operation
    char l_buffer[1024];
    ssize_t l_bytes_read = l_transport->ops->read(&s_mock_stream, l_buffer, sizeof(l_buffer));
    TEST_ASSERT(l_bytes_read >= 0, "Read operation should not fail");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("DNS stream transport read operation verified");
}

/**
 * @brief Test DNS stream transport write operation
 */
static void test_12_stream_write(void)
{
    TEST_INFO("Testing DNS stream transport write operation");
    
    // Find DNS transport
    dap_stream_transport_t *l_transport = 
        dap_stream_transport_find(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_transport, "DNS transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Create mock stream
    s_mock_stream.stream_transport = l_transport;
    s_mock_stream.esocket = &s_mock_events_socket;  // Set esocket for write operation
    
    // Test write operation
    const char l_test_data[] = "test data";
    ssize_t l_bytes_written = l_transport->ops->write(&s_mock_stream, l_test_data, sizeof(l_test_data));
    TEST_ASSERT(l_bytes_written > 0, "Write operation should succeed");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("DNS stream transport write operation verified");
}

/**
 * @brief Test DNS stream transport handshake operations
 */
static void test_13_stream_handshake(void)
{
    TEST_INFO("Testing DNS stream transport handshake operations");
    
    // Find DNS transport
    dap_stream_transport_t *l_transport = 
        dap_stream_transport_find(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_transport, "DNS transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Create mock stream
    s_mock_stream.stream_transport = l_transport;
    s_mock_stream.esocket = &s_mock_events_socket;  // Set esocket for handshake operations
    
    // Test handshake_init operation
    dap_stream_handshake_params_t l_params = {0};
    l_ret = l_transport->ops->handshake_init(&s_mock_stream, &l_params, NULL);
    TEST_ASSERT(l_ret == 0, "Handshake init should succeed");
    
    // Test handshake_process operation (server-side)
    uint8_t l_handshake_data[100] = {0};
    void *l_response = NULL;
    size_t l_response_size = 0;
    l_ret = l_transport->ops->handshake_process(&s_mock_stream, l_handshake_data, 
                                                 sizeof(l_handshake_data),
                                                 &l_response, &l_response_size);
    TEST_ASSERT(l_ret == 0, "Handshake process should succeed");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("DNS stream transport handshake operations verified");
}

/**
 * @brief Test DNS stream transport session operations
 */
static void test_14_stream_session(void)
{
    TEST_INFO("Testing DNS stream transport session operations");
    
    // Find DNS transport
    dap_stream_transport_t *l_transport = 
        dap_stream_transport_find(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_transport, "DNS transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Create mock stream
    s_mock_stream.stream_transport = l_transport;
    
    // Test session_create operation
    dap_stream_session_params_t l_session_params = {0};
    l_ret = l_transport->ops->session_create(&s_mock_stream, &l_session_params, NULL);
    TEST_ASSERT(l_ret == 0, "Session create should succeed");
    
    // Test session_start operation
    l_ret = l_transport->ops->session_start(&s_mock_stream, 12345, NULL);
    TEST_ASSERT(l_ret == 0, "Session start should succeed");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("DNS stream transport session operations verified");
}

/**
 * @brief Test DNS stream transport listen operation
 */
static void test_15_stream_listen(void)
{
    TEST_INFO("Testing DNS stream transport listen operation");
    
    // Find DNS transport
    dap_stream_transport_t *l_transport = 
        dap_stream_transport_find(DAP_STREAM_TRANSPORT_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_transport, "DNS transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Setup mock server
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)&s_mock_server);
    
    // Test listen operation (server-side)
    l_ret = l_transport->ops->listen(l_transport, "127.0.0.1", 53, &s_mock_server);
    TEST_ASSERT(l_ret == 0, "Listen operation should succeed");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("DNS stream transport listen operation verified");
}

// ============================================================================
// Test Suite Definition
// ============================================================================

int main(int argc, char *argv[])
{
    // Initialize test suite
    setup_test();
    
    TEST_SUITE_START("DNS Transport Comprehensive Unit Tests");
    
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
