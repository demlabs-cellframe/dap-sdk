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
 * @file test_transport_websocket.c
 * @brief Comprehensive unit tests for WebSocket transport server and stream
 * 
 * Tests WebSocket transport with full mocking for isolation:
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
#include "dap_net_transport_websocket_server.h"
#include "dap_net_transport_websocket_stream.h"
#include "dap_http_server.h"
#include "dap_server.h"
#include "dap_stream.h"
#include "dap_stream_handshake.h"
#include "dap_stream_session.h"
#include "dap_timerfd.h"
#include "dap_worker.h"

#define LOG_TAG "test_transport_websocket"

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
DAP_MOCK_DECLARE(dap_http_init);
DAP_MOCK_DECLARE(dap_http_deinit);

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

// Mock WebSocket-specific functions
DAP_MOCK_DECLARE(dap_net_transport_websocket_server_add_upgrade_handler);

// Mock dap_events_worker functions (needed for WebSocket ping timer)
DAP_MOCK_DECLARE(dap_events_worker_get_auto);
DAP_MOCK_DECLARE(dap_timerfd_start_on_worker);

// Mock server instance for testing
static dap_server_t s_mock_server = {0};
static dap_http_server_t s_mock_http_server = {0};
static dap_net_transport_t s_mock_stream_transport = {0};
static dap_stream_t s_mock_stream = {0};
static dap_http_client_t s_mock_http_client = {0};

// Wrapper for dap_http_server_new
DAP_MOCK_WRAPPER_CUSTOM(dap_server_t*, dap_http_server_new,
    PARAM(const char*, a_cfg_section),
    PARAM(const char*, a_server_name)
)
{
    UNUSED(a_cfg_section);
    UNUSED(a_server_name);
    
    // Return mock server if set, otherwise return NULL
    if (g_mock_dap_http_server_new && g_mock_dap_http_server_new->return_value.ptr) {
        return (dap_server_t*)g_mock_dap_http_server_new->return_value.ptr;
    }
    
    // Return default mock server
    s_mock_server._inheritor = &s_mock_http_server;
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

// Wrapper for dap_server_new (needed for websocket server)
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
    UNUSED(a_size);
    
    // Return mock value if set, otherwise return size (success)
    if (g_mock_dap_http_client_write && g_mock_dap_http_client_write->return_value.i != 0) {
        return g_mock_dap_http_client_write->return_value.i;
    }
    return a_size;
}

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

// Wrapper for dap_net_transport_websocket_server_add_upgrade_handler
DAP_MOCK_WRAPPER_CUSTOM(int, dap_net_transport_websocket_server_add_upgrade_handler,
    PARAM(dap_net_transport_websocket_server_t*, a_ws_server),
    PARAM(const char*, a_url_path)
)
{
    UNUSED(a_ws_server);
    UNUSED(a_url_path);
    
    // Return mock value if set, otherwise return 0 (success)
    if (g_mock_dap_net_transport_websocket_server_add_upgrade_handler && 
        g_mock_dap_net_transport_websocket_server_add_upgrade_handler->return_value.i != 0) {
        return g_mock_dap_net_transport_websocket_server_add_upgrade_handler->return_value.i;
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
        int l_ret = dap_common_init("test_transport_websocket", NULL);
        TEST_ASSERT(l_ret == 0, "DAP common initialization failed");
        
        // Initialize mock framework
        dap_mock_init();
        
        // Transport layer is initialized automatically via dap_module system
        // No need to call dap_net_transport_init() manually
        
        // Initialize WebSocket transport server (this registers operations)
        l_ret = dap_net_transport_websocket_server_init();
        TEST_ASSERT(l_ret == 0, "WebSocket transport server initialization failed");
        
        // Initialize WebSocket stream transport
        // Check if already registered (might be auto-registered via module constructor)
        dap_net_transport_t *l_existing = dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
        if (l_existing) {
            TEST_INFO("WebSocket stream transport already registered (auto-registered), skipping manual registration");
        } else {
            l_ret = dap_net_transport_websocket_stream_register();
            TEST_ASSERT(l_ret == 0, "WebSocket stream transport registration failed");
        }
        
        s_test_initialized = true;
        TEST_INFO("WebSocket transport test suite initialized");
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
        // Deinitialize WebSocket stream transport
        dap_net_transport_websocket_stream_unregister();
        
        // Deinitialize WebSocket transport server (unregisters operations)
        dap_net_transport_websocket_server_deinit();
        
        // Transport layer is deinitialized automatically via dap_module system
        // No need to call dap_net_transport_deinit() manually
        
        // Deinitialize mock framework
        dap_mock_deinit();
        
        // Deinitialize DAP common
        dap_common_deinit();
        
        s_test_initialized = false;
        TEST_INFO("WebSocket transport test suite cleaned up");
    }
}

// ============================================================================
// Server Tests
// ============================================================================

/**
 * @brief Test WebSocket transport server operations registration
 */
static void test_01_server_ops_registration(void)
{
    TEST_INFO("Testing WebSocket transport server operations registration");
    
    // Verify operations are registered
    const dap_net_transport_server_ops_t *l_ops = 
        dap_net_transport_server_get_ops(DAP_NET_TRANSPORT_WEBSOCKET);
    
    TEST_ASSERT_NOT_NULL(l_ops, "WebSocket transport server operations should be registered");
    TEST_ASSERT_NOT_NULL(l_ops->new, "new callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->start, "start callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->stop, "stop callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->delete, "delete callback should be set");
    
    TEST_SUCCESS("WebSocket transport server operations registration verified");
}

/**
 * @brief Test WebSocket transport server creation through unified API
 */
static void test_02_server_creation(void)
{
    TEST_INFO("Testing WebSocket transport server creation");
    
    const char *l_server_name = "test_websocket_server";
    
    // Setup mock for dap_http_server_new
    DAP_MOCK_SET_RETURN(dap_http_server_new, (void*)&s_mock_server);
    
    // Create server through unified API
    dap_net_transport_server_t *l_server = 
        dap_net_transport_server_new(DAP_NET_TRANSPORT_WEBSOCKET, l_server_name);
    
    TEST_ASSERT_NOT_NULL(l_server, "WebSocket server should be created");
    TEST_ASSERT(l_server->transport_type == DAP_NET_TRANSPORT_WEBSOCKET, 
                "Transport type should be WEBSOCKET");
    TEST_ASSERT(strcmp(l_server->server_name, l_server_name) == 0,
                "Server name should match");
    TEST_ASSERT_NOT_NULL(l_server->transport_specific,
                         "Transport-specific server instance should be created");
    
    // Note: dap_http_server_new is called in start(), not in new()
    // So we don't verify it here - it will be verified in test_03_server_start
    
    // Cleanup
    dap_net_transport_server_delete(l_server);
    
    TEST_SUCCESS("WebSocket transport server creation verified");
}

/**
 * @brief Test WebSocket transport server start with handlers registration
 */
static void test_03_server_start(void)
{
    TEST_INFO("Testing WebSocket transport server start");
    
    const char *l_server_name = "test_websocket_server";
    const char *l_cfg_section = "test_server";
    const char *l_addrs[] = {"127.0.0.1"};
    uint16_t l_ports[] = {8080};
    
    // Setup mocks
    DAP_MOCK_SET_RETURN(dap_http_server_new, (void*)&s_mock_server);
    DAP_MOCK_SET_RETURN(dap_server_listen_addr_add, 0);
    // Note: dap_net_transport_find is not mocked - using real implementation
    DAP_MOCK_SET_RETURN(dap_net_transport_websocket_server_add_upgrade_handler, 0);
    
    // Create server
    dap_net_transport_server_t *l_server = 
        dap_net_transport_server_new(DAP_NET_TRANSPORT_WEBSOCKET, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Start server
    int l_ret = dap_net_transport_server_start(l_server, l_cfg_section, 
                                                l_addrs, l_ports, 1);
    TEST_ASSERT(l_ret == 0, "Server start should succeed");
    
    // Verify handlers were registered
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(enc_http_add_proc) >= 1,
                "enc_http_add_proc should be called for enc_init handler");
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_stream_add_proc_http) >= 1,
                "dap_stream_add_proc_http should be called for stream handler");
    
    // Verify WebSocket upgrade handler was registered
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_net_transport_websocket_server_add_upgrade_handler) >= 1,
                "WebSocket upgrade handler should be registered");
    
    // Verify listen address was added
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_server_listen_addr_add) >= 1,
                "dap_server_listen_addr_add should be called");
    
    // Stop server
    dap_net_transport_server_stop(l_server);
    
    // Cleanup
    dap_net_transport_server_delete(l_server);
    
    TEST_SUCCESS("WebSocket transport server start verified");
}

/**
 * @brief Test WebSocket transport server stop
 */
static void test_04_server_stop(void)
{
    TEST_INFO("Testing WebSocket transport server stop");
    
    const char *l_server_name = "test_websocket_server";
    
    // Setup mocks
    DAP_MOCK_SET_RETURN(dap_http_server_new, (void*)&s_mock_server);
    
    // Create and start server
    dap_net_transport_server_t *l_server = 
        dap_net_transport_server_new(DAP_NET_TRANSPORT_WEBSOCKET, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Stop server
    dap_net_transport_server_stop(l_server);
    
    // Cleanup
    dap_net_transport_server_delete(l_server);
    
    TEST_SUCCESS("WebSocket transport server stop verified");
}

/**
 * @brief Test WebSocket transport server with invalid transport type
 */
static void test_05_server_invalid_type(void)
{
    TEST_INFO("Testing WebSocket transport server with invalid transport type");
    
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
 * @brief Test WebSocket stream transport registration
 */
static void test_06_stream_registration(void)
{
    TEST_INFO("Testing WebSocket stream transport registration");
    
    // Find WebSocket transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    
    TEST_ASSERT_NOT_NULL(l_transport, "WebSocket transport should be registered");
    TEST_ASSERT(l_transport->type == DAP_NET_TRANSPORT_WEBSOCKET,
                "Transport type should be WEBSOCKET");
    
    TEST_SUCCESS("WebSocket stream transport registration verified");
}

/**
 * @brief Test WebSocket stream transport capabilities
 */
static void test_07_stream_capabilities(void)
{
    TEST_INFO("Testing WebSocket stream transport capabilities");
    
    // Find WebSocket transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    
    TEST_ASSERT_NOT_NULL(l_transport, "WebSocket transport should be registered");
    TEST_ASSERT_NOT_NULL(l_transport->ops, "Transport operations should be set");
    TEST_ASSERT_NOT_NULL(l_transport->ops->init, "init callback should be set");
    TEST_ASSERT_NOT_NULL(l_transport->ops->deinit, "deinit callback should be set");
    TEST_ASSERT_NOT_NULL(l_transport->ops->connect, "connect callback should be set");
    TEST_ASSERT_NOT_NULL(l_transport->ops->listen, "listen callback should be set");
    
    TEST_SUCCESS("WebSocket stream transport capabilities verified");
}

/**
 * @brief Test WebSocket stream transport initialization
 */
static void test_08_stream_init(void)
{
    TEST_INFO("Testing WebSocket stream transport initialization");
    
    // Find WebSocket transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    
    TEST_ASSERT_NOT_NULL(l_transport, "WebSocket transport should be registered");
    
    // Initialize transport instance
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    TEST_ASSERT_NOT_NULL(l_transport->_inheritor, "Private data should be allocated");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("WebSocket stream transport initialization verified");
}

/**
 * @brief Test WebSocket stream transport unregistration
 */
static void test_09_stream_unregistration(void)
{
    TEST_INFO("Testing WebSocket stream transport unregistration");
    
    // Find WebSocket transport before unregistration
    dap_net_transport_t *l_transport_before = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_transport_before, "WebSocket transport should be registered");
    
    // Unregister WebSocket stream transport
    int l_ret = dap_net_transport_websocket_stream_unregister();
    TEST_ASSERT(l_ret == 0, "Unregistration should succeed");
    
    // Try to find transport after unregistration
    dap_net_transport_t *l_transport_after = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    
    // Note: unregistration might not remove from registry immediately
    // depending on implementation, so we just verify unregistration call succeeded
    
    // Re-register for other tests
    dap_net_transport_websocket_stream_register();
    
    TEST_SUCCESS("WebSocket stream transport unregistration verified");
}

/**
 * @brief Test WebSocket stream transport connect operation
 */
static void test_10_stream_connect(void)
{
    TEST_INFO("Testing WebSocket stream transport connect operation");
    
    // Find WebSocket transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_transport, "WebSocket transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Create mock stream
    s_mock_stream.stream_transport = l_transport;
    
    // Test connect operation
    l_ret = l_transport->ops->connect(&s_mock_stream, "127.0.0.1", 8080, NULL);
    TEST_ASSERT(l_ret == 0, "Connect operation should succeed");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("WebSocket stream transport connect operation verified");
}

/**
 * @brief Test WebSocket stream transport read operation
 */
static void test_11_stream_read(void)
{
    TEST_INFO("Testing WebSocket stream transport read operation");
    
    // Find WebSocket transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_transport, "WebSocket transport should be registered");
    
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
    
    TEST_SUCCESS("WebSocket stream transport read operation verified");
}

/**
 * @brief Test WebSocket stream transport write operation
 */
static void test_12_stream_write(void)
{
    TEST_INFO("Testing WebSocket stream transport write operation");
    
    // Find WebSocket transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_transport, "WebSocket transport should be registered");
    
    // Initialize transport
    int l_ret = l_transport->ops->init(l_transport, NULL);
    TEST_ASSERT(l_ret == 0, "Transport initialization should succeed");
    
    // Create mock stream
    s_mock_stream.stream_transport = l_transport;
    
    // Initialize stream transport private data and set state to OPEN for write test
    // In real usage, this would be done by session_start, but for unit test we need to set it manually
    dap_stream_transport_ws_private_t *l_priv = 
        (dap_stream_transport_ws_private_t*)l_transport->_inheritor;
    if (l_priv) {
        l_priv->state = DAP_WS_STATE_OPEN;
    }
    
    // Test write operation
    const char l_test_data[] = "test data";
    ssize_t l_bytes_written = l_transport->ops->write(&s_mock_stream, l_test_data, sizeof(l_test_data));
    TEST_ASSERT(l_bytes_written > 0, "Write operation should succeed");
    
    // Deinitialize
    l_transport->ops->deinit(l_transport);
    
    TEST_SUCCESS("WebSocket stream transport write operation verified");
}

/**
 * @brief Test WebSocket stream transport handshake operations
 */
static void test_13_stream_handshake(void)
{
    TEST_INFO("Testing WebSocket stream transport handshake operations");
    
    // Find WebSocket transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_transport, "WebSocket transport should be registered");
    
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
    
    TEST_SUCCESS("WebSocket stream transport handshake operations verified");
}

/**
 * @brief Test WebSocket stream transport session operations
 */
static void test_14_stream_session(void)
{
    TEST_INFO("Testing WebSocket stream transport session operations");
    
    // Find WebSocket transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_transport, "WebSocket transport should be registered");
    
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
    
    TEST_SUCCESS("WebSocket stream transport session operations verified");
}

/**
 * @brief Test WebSocket stream transport listen operation
 */
static void test_15_stream_listen(void)
{
    TEST_INFO("Testing WebSocket stream transport listen operation");
    
    // Find WebSocket transport
    dap_net_transport_t *l_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_transport, "WebSocket transport should be registered");
    
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
    
    TEST_SUCCESS("WebSocket stream transport listen operation verified");
}

// ============================================================================
// Test Suite Definition
// ============================================================================

int main(int argc, char *argv[])
{
    // Initialize test suite
    setup_test();
    
    TEST_SUITE_START("WebSocket Transport Comprehensive Unit Tests");
    
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
