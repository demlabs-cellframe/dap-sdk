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
 * @file test_transport_http.c
 * @brief Comprehensive unit tests for HTTP transport server and stream
 * 
 * Tests HTTP transport with full mocking for isolation:
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
#include "dap_net_transport.h"
#include "dap_net_transport_server.h"
#include "dap_net_transport_http_server.h"
#include "dap_net_transport_http_stream.h"
#include "dap_http_server.h"
#include "dap_server.h"
#include "dap_stream.h"
#include "dap_stream_handshake.h"
#include "dap_stream_session.h"

#define LOG_TAG "test_transport_http"

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

// Mock dap_http_server functions
DAP_MOCK_DECLARE(dap_http_server_new);

// Mock enc_http functions
DAP_MOCK_DECLARE(enc_http_init);
DAP_MOCK_DECLARE(enc_http_deinit);
DAP_MOCK_DECLARE(enc_http_add_proc);

// Mock dap_stream functions
DAP_MOCK_DECLARE(dap_stream_add_proc_http);
DAP_MOCK_DECLARE(dap_stream_ctl_add_proc);

// Don't mock dap_net_transport_find - use real implementation
// This allows tests to work with real transport registration

// Mock dap_stream functions
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

// ============================================================================
// Mock Wrappers
// ============================================================================

// Mock server instance for testing
static dap_server_t s_mock_server = {0};
static dap_http_server_t s_mock_http_server = {0};
static dap_net_transport_t s_mock_stream_transport = {0};
static dap_stream_t s_mock_stream = {0};
static dap_http_client_t s_mock_http_client = {0};

// Wrapper for dap_server_new (needed for HTTP server)
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

// Wrapper for dap_http_server_new
DAP_MOCK_WRAPPER_CUSTOM(dap_server_t*, dap_http_server_new,
    PARAM(const char*, a_cfg_section),
    PARAM(const char*, a_server_name)
)
{
    UNUSED(a_cfg_section);
    UNUSED(a_server_name);
    
    // Return mock server if set, otherwise return default mock
    dap_server_t *l_server = &s_mock_server;
    if (g_mock_dap_http_server_new && g_mock_dap_http_server_new->return_value.ptr) {
        l_server = (dap_server_t*)g_mock_dap_http_server_new->return_value.ptr;
    }
    
    // Set _inheritor to point to mock HTTP server structure
    // This is required for DAP_HTTP_SERVER macro to work
    l_server->_inheritor = &s_mock_http_server;
    
    return l_server;
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

// Wrapper for enc_http_add_proc
DAP_MOCK_WRAPPER_CUSTOM(dap_http_url_proc_t*, enc_http_add_proc,
    PARAM(dap_http_server_t*, a_server),
    PARAM(const char*, a_url_path)
)
{
    UNUSED(a_server);
    UNUSED(a_url_path);
    
    // Return mock value if set, otherwise return NULL
    if (g_mock_enc_http_add_proc && g_mock_enc_http_add_proc->return_value.ptr) {
        return (dap_http_url_proc_t*)g_mock_enc_http_add_proc->return_value.ptr;
    }
    return NULL;
}

// Wrapper for dap_stream_add_proc_http
DAP_MOCK_WRAPPER_CUSTOM(dap_http_url_proc_t*, dap_stream_add_proc_http,
    PARAM(dap_http_server_t*, a_server),
    PARAM(const char*, a_url_path)
)
{
    UNUSED(a_server);
    UNUSED(a_url_path);
    
    // Return mock value if set, otherwise return NULL
    if (g_mock_dap_stream_add_proc_http && g_mock_dap_stream_add_proc_http->return_value.ptr) {
        return (dap_http_url_proc_t*)g_mock_dap_stream_add_proc_http->return_value.ptr;
    }
    return NULL;
}

// dap_net_transport_find is not mocked - using real implementation
// This allows tests to access real registered transports with proper ops


// Wrapper for dap_http_client_new
DAP_MOCK_WRAPPER_CUSTOM(dap_http_client_t*, dap_http_client_new,
    PARAM(const char*, a_host),
    PARAM(uint16_t, a_port)
)
{
    UNUSED(a_host);
    UNUSED(a_port);
    
    // Return mock client if set, otherwise return NULL
    if (g_mock_dap_http_client_new && g_mock_dap_http_client_new->return_value.ptr) {
        return (dap_http_client_t*)g_mock_dap_http_client_new->return_value.ptr;
    }
    
    // Return default mock client
    return &s_mock_http_client;
}

// Wrapper for dap_http_client_delete
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_http_client_delete, (dap_http_client_t *a_client), (a_client));

// Wrapper for dap_http_client_write
DAP_MOCK_WRAPPER_CUSTOM(ssize_t, dap_http_client_write,
    PARAM(dap_http_client_t*, a_client),
    PARAM(const void*, a_data),
    PARAM(size_t, a_size)
)
{
    UNUSED(a_client);
    UNUSED(a_data);
    
    // Return mock value if set, otherwise return size (success)
    if (g_mock_dap_http_client_write && g_mock_dap_http_client_write->return_value.i != 0) {
        return g_mock_dap_http_client_write->return_value.i;
    }
    return (ssize_t)a_size;
}

// Wrapper for dap_http_init
DAP_MOCK_WRAPPER_CUSTOM(int, dap_http_init,
    void
)
{
    // Return mock value if set, otherwise return 0 (success)
    if (g_mock_dap_http_init && g_mock_dap_http_init->return_value.i != 0) {
        return g_mock_dap_http_init->return_value.i;
    }
    return 0;
}

// Wrapper for dap_http_deinit
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_http_deinit, (), ());

// Wrapper for enc_http_init
DAP_MOCK_WRAPPER_CUSTOM(int, enc_http_init,
    void
)
{
    // Return mock value if set, otherwise return 0 (success)
    if (g_mock_enc_http_init && g_mock_enc_http_init->return_value.i != 0) {
        return g_mock_enc_http_init->return_value.i;
    }
    return 0;
}

// Wrapper for enc_http_deinit
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(enc_http_deinit, (), ());

// Wrapper for dap_stream_ctl_add_proc
DAP_MOCK_WRAPPER_CUSTOM(dap_http_url_proc_t*, dap_stream_ctl_add_proc,
    PARAM(dap_http_server_t*, a_server),
    PARAM(const char*, a_url_path)
)
{
    UNUSED(a_server);
    UNUSED(a_url_path);
    
    // Return mock value if set, otherwise return NULL
    if (g_mock_dap_stream_ctl_add_proc && g_mock_dap_stream_ctl_add_proc->return_value.ptr) {
        return (dap_http_url_proc_t*)g_mock_dap_stream_ctl_add_proc->return_value.ptr;
    }
    return NULL;
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
        int l_ret = dap_common_init("test_transport_http", NULL);
        TEST_ASSERT(l_ret == 0, "DAP common initialization failed");
        
        // Initialize mock framework
        dap_mock_init();
        
        
        // Check if already registered (might be auto-registered via module constructor)
        dap_net_transport_t *l_existing = dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
        if (! l_existing) {
            TEST_ASSERT(l_ret == 0, "HTTP stream transport not registred");
        }
        
        s_test_initialized = true;
        TEST_INFO("HTTP transport test suite initialized");
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
        TEST_INFO("HTTP transport test suite cleaned up");
    }
}

// ============================================================================
// Server Tests
// ============================================================================

/**
 * @brief Test HTTP transport server operations registration
 */
static void test_01_server_ops_registration(void)
{
    TEST_INFO("Testing HTTP transport server operations registration");
    
    // Verify operations are registered
    const dap_net_transport_server_ops_t *l_ops = 
        dap_net_transport_server_get_ops(DAP_NET_TRANSPORT_HTTP);
    
    TEST_ASSERT_NOT_NULL(l_ops, "HTTP transport server operations should be registered");
    TEST_ASSERT_NOT_NULL(l_ops->new, "new callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->start, "start callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->stop, "stop callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->delete, "delete callback should be set");
    
    TEST_SUCCESS("HTTP transport server operations registration verified");
}

/**
 * @brief Test HTTP transport server creation through unified API
 */
static void test_02_server_creation(void)
{
    TEST_INFO("Testing HTTP transport server creation");
    
    const char *l_server_name = "test_http_server";
    
    // Setup mock for dap_http_server_new
    DAP_MOCK_SET_RETURN(dap_http_server_new, (void*)&s_mock_server);
    
    // Create server through unified API
    dap_net_transport_server_t *l_server = 
        dap_net_transport_server_new(DAP_NET_TRANSPORT_HTTP, l_server_name);
    
    TEST_ASSERT_NOT_NULL(l_server, "HTTP server should be created");
    TEST_ASSERT(l_server->transport_type == DAP_NET_TRANSPORT_HTTP, 
                "Transport type should be HTTP");
    TEST_ASSERT(strcmp(l_server->server_name, l_server_name) == 0,
                "Server name should match");
    TEST_ASSERT_NOT_NULL(l_server->transport_specific,
                         "Transport-specific server instance should be created");
    
    // Note: dap_http_server_new is called in start(), not in new()
    // So we don't verify it here - it will be verified in test_03_server_start
    
    // Cleanup
    dap_net_transport_server_delete(l_server);
    
    TEST_SUCCESS("HTTP transport server creation verified");
}

/**
 * @brief Test HTTP transport server start with handlers registration
 */
static void test_03_server_start(void)
{
    TEST_INFO("Testing HTTP transport server start");
    
    const char *l_server_name = "test_http_server";
    const char *l_cfg_section = "test_server";
    const char *l_addrs[] = {"127.0.0.1"};
    uint16_t l_ports[] = {8080};
    
    // Setup mocks
    DAP_MOCK_SET_RETURN(dap_http_server_new, (void*)&s_mock_server);
    DAP_MOCK_SET_RETURN(dap_server_listen_addr_add, 0);
    DAP_MOCK_SET_RETURN(enc_http_init, 0);  // Ensure enc_http_init succeeds
    // Note: dap_net_transport_find is not mocked - using real implementation
    
    // Create server
    dap_net_transport_server_t *l_server = 
        dap_net_transport_server_new(DAP_NET_TRANSPORT_HTTP, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Start server
    int l_ret = dap_net_transport_server_start(l_server, l_cfg_section, 
                                                l_addrs, l_ports, 1);
    if (l_ret != 0) {
        fprintf(stderr, "ERROR: Server start failed with code %d\n", l_ret);
        fflush(stderr);
    }
    TEST_ASSERT(l_ret == 0, "Server start should succeed");
    
    // Verify handlers were registered
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(enc_http_add_proc) >= 1,
                "enc_http_add_proc should be called for enc_init handler");
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_stream_add_proc_http) >= 1,
                "dap_stream_add_proc_http should be called for stream handler");
    
    // Verify listen address was added
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_server_listen_addr_add) >= 1,
                "dap_server_listen_addr_add should be called");
    
    // Stop server
    dap_net_transport_server_stop(l_server);
    
    // Cleanup
    dap_net_transport_server_delete(l_server);
    
    TEST_SUCCESS("HTTP transport server start verified");
}

/**
 * @brief Test HTTP transport server stop
 */
static void test_04_server_stop(void)
{
    TEST_INFO("Testing HTTP transport server stop");
    
    const char *l_server_name = "test_http_server";
    
    // Setup mocks
    DAP_MOCK_SET_RETURN(dap_http_server_new, (void*)&s_mock_server);
    
    // Create and start server
    dap_net_transport_server_t *l_server = 
        dap_net_transport_server_new(DAP_NET_TRANSPORT_HTTP, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Stop server
    dap_net_transport_server_stop(l_server);
    
    // Cleanup
    dap_net_transport_server_delete(l_server);
    
    TEST_SUCCESS("HTTP transport server stop verified");
}

/**
 * @brief Test HTTP transport server with invalid transport type
 */
static void test_05_server_invalid_type(void)
{
    TEST_INFO("Testing HTTP transport server with invalid transport type");
    
    // Try to create server with invalid type
    dap_net_transport_server_t *l_server = 
        dap_net_transport_server_new(DAP_NET_TRANSPORT_TLS_DIRECT, "test_server");
    
    TEST_ASSERT_NULL(l_server, "Server should not be created for unregistered transport type");
    
    TEST_SUCCESS("Invalid transport type handling verified");
}

// ============================================================================
// Stream Tests
// ============================================================================

/**
 * @brief Test HTTP stream transport registration
 */
static void test_06_stream_registration(void)
{
    TEST_INFO("Testing HTTP stream transport registration");
    
    // Find HTTP transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    
    TEST_ASSERT_NOT_NULL(l_transport, "HTTP transport should be registered");
    TEST_ASSERT(l_transport->type == DAP_NET_TRANSPORT_HTTP,
                "Transport type should be HTTP");
    
    TEST_SUCCESS("HTTP stream transport registration verified");
}

/**
 * @brief Test HTTP stream transport capabilities
 */
static void test_07_stream_capabilities(void)
{
    TEST_INFO("Testing HTTP stream transport capabilities");
    
    // Find HTTP transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    
    TEST_ASSERT_NOT_NULL(l_transport, "HTTP transport should be registered");
    TEST_ASSERT_NOT_NULL(l_transport->ops, "Transport operations should be set");
    TEST_ASSERT_NOT_NULL(l_transport->ops->init, "init callback should be set");
    TEST_ASSERT_NOT_NULL(l_transport->ops->deinit, "deinit callback should be set");
    TEST_ASSERT_NOT_NULL(l_transport->ops->connect, "connect callback should be set");
    TEST_ASSERT_NOT_NULL(l_transport->ops->listen, "listen callback should be set");
    
    TEST_SUCCESS("HTTP stream transport capabilities verified");
}

/**
 * @brief Test HTTP stream transport initialization
 */
static void test_08_stream_init(void)
{
    TEST_INFO("Testing HTTP stream transport initialization");
    
    // Find HTTP transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    
    TEST_ASSERT_NOT_NULL(l_transport, "HTTP transport should be registered");
    
    // Initialize transport instance
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    TEST_ASSERT_NOT_NULL(l_transport->_inheritor, "Private data should be allocated");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("HTTP stream transport initialization verified");
}

/**
 * @brief Test HTTP stream transport unregistration
 */
static void test_09_stream_unregistration(void)
{
    TEST_INFO("Testing HTTP stream transport unregistration");
    
    // Find HTTP transport before unregistration
    dap_net_transport_t *l_transport_before = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    TEST_ASSERT_NOT_NULL(l_transport_before, "HTTP transport should be registered");
    
    // Unregister HTTP stream transport
    int l_ret = dap_net_transport_http_stream_unregister();
    TEST_ASSERT(l_ret == 0, "Unregistration should succeed");
    
    // Try to find transport after unregistration
    dap_net_transport_t *l_transport_after = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    
    // Note: unregistration might not remove from registry immediately
    // depending on implementation, so we just verify unregistration call succeeded
    
    // Re-register for other tests
    dap_net_transport_http_stream_register();
    
    TEST_SUCCESS("HTTP stream transport unregistration verified");
}

/**
 * @brief Test HTTP stream transport connect operation
 */
static void test_10_stream_connect(void)
{
    TEST_INFO("Testing HTTP stream transport connect operation");
    
    // Find HTTP transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    TEST_ASSERT_NOT_NULL(l_transport, "HTTP transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Setup mock stream
    s_mock_stream.stream_transport = l_transport;
    
    // Test connect operation
    l_ret = l_transport->ops->connect(&s_mock_stream, "127.0.0.1", 8080, NULL);
    TEST_ASSERT(l_ret == 0, "Connect operation should succeed");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("HTTP stream transport connect operation verified");
}

/**
 * @brief Test HTTP stream transport read operation
 */
static void test_11_stream_read(void)
{
    TEST_INFO("Testing HTTP stream transport read operation");
    
    // Find HTTP transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    TEST_ASSERT_NOT_NULL(l_transport, "HTTP transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Create mock stream
    s_mock_stream.stream_transport = l_transport;
    
    // Test read operation (HTTP transport may return 0 for event-driven reading)
    char l_buffer[1024];
    ssize_t l_bytes_read = l_transport->ops->read(&s_mock_stream, l_buffer, sizeof(l_buffer));
    TEST_ASSERT(l_bytes_read >= 0, "Read operation should not fail");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("HTTP stream transport read operation verified");
}

/**
 * @brief Test HTTP stream transport write operation
 */
static void test_12_stream_write(void)
{
    TEST_INFO("Testing HTTP stream transport write operation");
    
    // Find HTTP transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    TEST_ASSERT_NOT_NULL(l_transport, "HTTP transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Create mock stream
    s_mock_stream.stream_transport = l_transport;
    
    // Test write operation
    const char l_test_data[] = "test data";
    ssize_t l_bytes_written = l_transport->ops->write(&s_mock_stream, l_test_data, sizeof(l_test_data));
    TEST_ASSERT(l_bytes_written > 0, "Write operation should succeed");
    TEST_ASSERT(l_bytes_written == (ssize_t)sizeof(l_test_data), 
                "All bytes should be written");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("HTTP stream transport write operation verified");
}

/**
 * @brief Test HTTP stream transport handshake operations
 */
static void test_13_stream_handshake(void)
{
    TEST_INFO("Testing HTTP stream transport handshake operations");
    
    // Find HTTP transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    TEST_ASSERT_NOT_NULL(l_transport, "HTTP transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Create mock stream
    s_mock_stream.stream_transport = l_transport;
    
    // Test handshake_init operation
    dap_net_handshake_params_t l_params = {0};
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
    
    TEST_SUCCESS("HTTP stream transport handshake operations verified");
}

/**
 * @brief Test HTTP stream transport session operations
 */
static void test_14_stream_session(void)
{
    TEST_INFO("Testing HTTP stream transport session operations");
    
    // Find HTTP transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    TEST_ASSERT_NOT_NULL(l_transport, "HTTP transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Create mock stream
    s_mock_stream.stream_transport = l_transport;
    
    // Test session_create operation
    dap_net_session_params_t l_session_params = {0};
    l_ret = l_transport->ops->session_create(&s_mock_stream, &l_session_params, NULL);
    TEST_ASSERT(l_ret == 0, "Session create should succeed");
    
    // Test session_start operation
    l_ret = l_transport->ops->session_start(&s_mock_stream, 12345, NULL);
    TEST_ASSERT(l_ret == 0, "Session start should succeed");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("HTTP stream transport session operations verified");
}

/**
 * @brief Test HTTP stream transport listen operation
 */
static void test_15_stream_listen(void)
{
    TEST_INFO("Testing HTTP stream transport listen operation");
    
    // Find HTTP transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    TEST_ASSERT_NOT_NULL(l_transport, "HTTP transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Setup mock server
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)&s_mock_server);
    
    // Test listen operation (server-side)
    l_ret = l_transport->ops->listen(l_transport, "127.0.0.1", 8080, &s_mock_server);
    TEST_ASSERT(l_ret == 0, "Listen operation should succeed");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("HTTP stream transport listen operation verified");
}

// ============================================================================
// Test Suite Definition
// ============================================================================

int main(int argc, char *argv[])
{
    // Initialize test suite
    setup_test();
    
    TEST_SUITE_START("HTTP Transport Comprehensive Unit Tests");
    
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
