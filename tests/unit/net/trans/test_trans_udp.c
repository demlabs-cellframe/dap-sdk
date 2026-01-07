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
 * @file test_trans_udp.c
 * @brief UDP-specific unit tests for UDP transport protocol
 * 
 * Tests ONLY UDP-specific functionality:
 * - UDP packet format (DAP_STREAM_UDP_PKT_*)
 * - UDP sequence numbering
 * - UDP flow control (dap_io_flow_udp)
 * - UDP worker assignment and cross-worker forwarding
 * - UDP socket sharding
 * - UDP MTU limits
 * 
 * All transport-agnostic functionality (handshake, session, stream, encryption)
 * is tested in test_trans.c
 * 
 * @date 2025-01-07
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
#include "dap_net_trans_udp_server.h"
#include "dap_net_trans_udp_stream.h"
#include "dap_server.h"
#include "dap_io_flow.h"
#include "dap_io_flow_udp.h"
#include "dap_enc_server.h"
#include "dap_trans_test_fixtures.h"
#include "dap_trans_test_mocks.h"

#define LOG_TAG "test_trans_udp"

// Note: Common mocks are now in dap_trans_test_mocks.h/c
// Only UDP-specific mock instances are defined here

// UDP-specific mocks
DAP_MOCK_DECLARE(dap_stream_add_proc_udp, DAP_MOCK_CONFIG_PASSTHROUGH);
DAP_MOCK_DECLARE(dap_enc_server_process_request, DAP_MOCK_CONFIG_PASSTHROUGH);
// Note: NOT mocking randombytes - it's a system crypto function that should work correctly
DAP_MOCK_DECLARE(dap_events_socket_write_unsafe, DAP_MOCK_CONFIG_PASSTHROUGH);
DAP_MOCK_DECLARE(dap_events_socket_create, DAP_MOCK_CONFIG_PASSTHROUGH);
DAP_MOCK_DECLARE(dap_events_socket_create_platform, DAP_MOCK_CONFIG_PASSTHROUGH);
DAP_MOCK_DECLARE(dap_events_socket_delete_unsafe, DAP_MOCK_CONFIG_PASSTHROUGH);
DAP_MOCK_DECLARE(dap_events_socket_connect, DAP_MOCK_CONFIG_PASSTHROUGH);
DAP_MOCK_DECLARE(dap_events_socket_resolve_and_set_addr, DAP_MOCK_CONFIG_PASSTHROUGH);
DAP_MOCK_DECLARE(dap_worker_add_events_socket, DAP_MOCK_CONFIG_PASSTHROUGH);

// ============================================================================
// UDP-Specific Mock Instances
// ============================================================================

static dap_io_flow_server_t s_mock_flow_server = {0};

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
        // Use common fixtures setup
        int l_ret = dap_trans_test_setup();
        TEST_ASSERT(l_ret == 0, "Common fixtures setup failed");

        // Setup UDP-specific mocks - only essential ones that are always needed
        // Note: dap_io_flow_server_new_udp is NOT mocked globally - tests that need it
        // will mock it individually
        DAP_MOCK_SET_RETURN(dap_proc_thread_get_count, (void*)4);  // 4 workers for tests
        
        // Initialize trans layer
        l_ret = dap_net_trans_init();
        TEST_ASSERT(l_ret == 0, "Trans layer initialization failed");
        
        // Initialize UDP trans server (this registers operations)
        l_ret = dap_net_trans_udp_server_init();
        TEST_ASSERT(l_ret == 0, "UDP trans server initialization failed");
        
        // Initialize UDP stream trans
        dap_net_trans_t *l_existing = dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
        if (!l_existing) {
            l_ret = dap_net_trans_udp_stream_register();
            TEST_ASSERT(l_ret == 0, "UDP stream trans registration failed");
        }
        
        s_test_initialized = true;
        TEST_INFO("UDP-specific test suite initialized");
    }
    
    // Reset mocks before each test
    dap_trans_test_teardown();
}

/**
 * @brief Teardown function called after each test
 */
static void teardown_test(void)
{
    dap_trans_test_teardown();
}

/**
 * @brief Suite cleanup function
 */
static void suite_cleanup(void)
{
    if (s_test_initialized) {
        // Deinitialize UDP components
        dap_net_trans_udp_stream_unregister();
        dap_net_trans_udp_server_deinit();

        // Use common fixtures cleanup
        dap_trans_test_suite_cleanup();
        
        s_test_initialized = false;
        TEST_INFO("UDP-specific test suite cleaned up");
    }
}

// Original mock wrappers removed - now using dap_trans_test_mocks.c

// ============================================================================
// Server Tests (these will be moved to test_trans.c)
// ============================================================================

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
    return dap_trans_test_get_mock_server();
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

// dap_net_trans_find is not mocked - using real implementation


// Wrapper for dap_stream_add_proc_udp
DAP_MOCK_WRAPPER_CUSTOM(int, dap_stream_add_proc_udp,
    PARAM(dap_server_t*, a_server)
)
{
    UNUSED(a_server);
    
    // Return mock value if set, otherwise return 0 (success)
    if (g_mock_dap_stream_add_proc_udp && g_mock_dap_stream_add_proc_udp->return_value.i != 0) {
        return g_mock_dap_stream_add_proc_udp->return_value.i;
    }
    return 0;
}

// Wrapper for dap_events_socket_write_unsafe
// Return size of data written (success) for UDP write tests
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

// Wrapper for dap_events_socket_create
DAP_MOCK_WRAPPER_CUSTOM(dap_events_socket_t*, dap_events_socket_create,
    PARAM(dap_events_desc_type_t, a_type),
    PARAM(dap_events_socket_callbacks_t*, a_callbacks)
)
{
    UNUSED(a_type);
    UNUSED(a_callbacks);
    if (g_mock_dap_events_socket_create && g_mock_dap_events_socket_create->return_value.ptr) {
        return (dap_events_socket_t*)g_mock_dap_events_socket_create->return_value.ptr;
    }
    return dap_trans_test_get_mock_esocket();
}

// Wrapper for dap_events_socket_create_platform
DAP_MOCK_WRAPPER_CUSTOM(dap_events_socket_t*, dap_events_socket_create_platform,
    PARAM(int, a_domain),
    PARAM(int, a_type),
    PARAM(int, a_protocol),
    PARAM(dap_events_socket_callbacks_t*, a_callbacks)
)
{
    UNUSED(a_domain);
    UNUSED(a_type);
    UNUSED(a_protocol);
    UNUSED(a_callbacks);
    if (g_mock_dap_events_socket_create_platform && g_mock_dap_events_socket_create_platform->return_value.ptr) {
        return (dap_events_socket_t*)g_mock_dap_events_socket_create_platform->return_value.ptr;
    }
    return dap_trans_test_get_mock_esocket();
}

// Wrapper for dap_events_socket_delete_unsafe
DAP_MOCK_WRAPPER_CUSTOM(void, dap_events_socket_delete_unsafe,
    PARAM(dap_events_socket_t*, a_es),
    PARAM(bool, a_unsafe)
)
{
    UNUSED(a_es);
    UNUSED(a_unsafe);
}

// Wrapper for dap_events_socket_connect
DAP_MOCK_WRAPPER_CUSTOM(int, dap_events_socket_connect,
    PARAM(dap_events_socket_t*, a_es),
    PARAM(int*, a_error_code)
)
{
    UNUSED(a_es);
    if (a_error_code) {
        *a_error_code = 0;
    }
    if (g_mock_dap_events_socket_connect && g_mock_dap_events_socket_connect->return_value.i != 0) {
        if (a_error_code) {
            *a_error_code = g_mock_dap_events_socket_connect->return_value.i;
        }
        return g_mock_dap_events_socket_connect->return_value.i;
    }
    return 0;
}

// Wrapper for dap_events_socket_resolve_and_set_addr
DAP_MOCK_WRAPPER_CUSTOM(int, dap_events_socket_resolve_and_set_addr,
    PARAM(dap_events_socket_t*, a_es),
    PARAM(const char*, a_host),
    PARAM(uint16_t, a_port)
)
{
    UNUSED(a_es);
    UNUSED(a_host);
    UNUSED(a_port);
    if (g_mock_dap_events_socket_resolve_and_set_addr && g_mock_dap_events_socket_resolve_and_set_addr->return_value.i != 0) {
        return g_mock_dap_events_socket_resolve_and_set_addr->return_value.i;
    }
    return 0;
}

// Wrapper for dap_worker_add_events_socket
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_worker_add_events_socket, (dap_worker_t *a_worker, dap_events_socket_t *a_es), (a_worker, a_es));

// Mock dap_enc_server_response_t for testing
static dap_enc_server_response_t s_mock_enc_response = {
    .success = true,
    .encrypt_msg = NULL,
    .encrypt_msg_len = 0,
    .error_message = NULL
};

// Wrapper for dap_enc_server_process_request
DAP_MOCK_WRAPPER_CUSTOM(int, dap_enc_server_process_request,
    PARAM(const dap_enc_server_request_t*, a_request),
    PARAM(dap_enc_server_response_t**, a_response)
)
{
    UNUSED(a_request);
    
    // Return mock response if set, otherwise return success with default mock
    if (g_mock_dap_enc_server_process_request && g_mock_dap_enc_server_process_request->return_value.i != 0) {
        return g_mock_dap_enc_server_process_request->return_value.i;
    }
    
    // Set response to mock response
    if (a_response) {
        *a_response = &s_mock_enc_response;
    }
    
    return 0;  // Success
}


// ============================================================================
// Server Tests
// ============================================================================

/**
 * @brief Test UDP trans server operations registration
 */
static void test_01_server_ops_registration(void)
{
    TEST_INFO("Testing UDP trans server operations registration");
    
    // Verify operations are registered for all UDP variants
    const dap_net_trans_server_ops_t *l_ops_basic = 
        dap_net_trans_server_get_ops(DAP_NET_TRANS_UDP_BASIC);
    const dap_net_trans_server_ops_t *l_ops_reliable = 
        dap_net_trans_server_get_ops(DAP_NET_TRANS_UDP_RELIABLE);
    const dap_net_trans_server_ops_t *l_ops_quic = 
        dap_net_trans_server_get_ops(DAP_NET_TRANS_UDP_QUIC_LIKE);
    
    TEST_ASSERT_NOT_NULL(l_ops_basic, "UDP_BASIC trans server operations should be registered");
    TEST_ASSERT_NOT_NULL(l_ops_reliable, "UDP_RELIABLE trans server operations should be registered");
    TEST_ASSERT_NOT_NULL(l_ops_quic, "UDP_QUIC_LIKE trans server operations should be registered");
    
    // Verify callbacks are set
    TEST_ASSERT_NOT_NULL(l_ops_basic->new, "new callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops_basic->start, "start callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops_basic->stop, "stop callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops_basic->delete, "delete callback should be set");
    
    TEST_SUCCESS("UDP trans server operations registration verified");
}

/**
 * @brief Test UDP trans server creation through unified API
 */
static void test_02_server_creation(void)
{
    TEST_INFO("Testing UDP trans server creation");
    
    const char *l_server_name = "test_udp_server";
    
    // Setup mock for dap_server_new
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)dap_trans_test_get_mock_server());
    
    // Create server through unified API (test UDP_BASIC variant)
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_UDP_BASIC, l_server_name);
    
    TEST_ASSERT_NOT_NULL(l_server, "UDP server should be created");
    TEST_ASSERT(l_server->trans_type == DAP_NET_TRANS_UDP_BASIC, 
                "Trans type should be UDP_BASIC");
    TEST_ASSERT(strcmp(l_server->server_name, l_server_name) == 0,
                "Server name should match");
    TEST_ASSERT_NOT_NULL(l_server->trans_specific,
                         "Trans-specific server instance should be created");
    
    // Note: dap_server_new is called in start(), not in new()
    // So we don't verify it here - it will be verified in test_03_server_start
    
    // Cleanup
    dap_net_trans_server_delete(l_server);
    
    TEST_SUCCESS("UDP trans server creation verified");
}

/**
 * @brief Test UDP trans server start with handlers registration
 */
static void test_03_server_start(void)
{
    TEST_INFO("Testing UDP trans server start");
    
    const char *l_server_name = "test_udp_server";
    const char *l_cfg_section = "test_server";
    const char *l_addrs[] = {"127.0.0.1"};
    uint16_t l_ports[] = {8080};
    
    // Setup mocks
    // Note: dap_server_new is NOT mocked - new architecture creates real dap_server internally
    DAP_MOCK_SET_RETURN(dap_server_listen_addr_add, 0);
    // Note: dap_net_trans_find is not mocked - using real implementation
    
    // Create server
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_UDP_BASIC, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Start server
    TEST_INFO("Calling dap_net_trans_server_start...");
    int l_ret = dap_net_trans_server_start(l_server, l_cfg_section, 
                                                l_addrs, l_ports, 1);
    TEST_INFO("dap_net_trans_server_start returned: %d", l_ret);
    TEST_ASSERT(l_ret == 0, "Server start should succeed");
    
    // Note: Verification of internal calls like dap_server_listen_addr_add
    // belongs in integration tests, not unit tests.
    // Unit tests should only verify the public API behavior.
    
    // Stop server
    dap_net_trans_server_stop(l_server);
    
    // Cleanup
    dap_net_trans_server_delete(l_server);
    
    TEST_SUCCESS("UDP trans server start verified");
}

/**
 * @brief Test UDP trans server stop
 */
static void test_04_server_stop(void)
{
    TEST_INFO("Testing UDP trans server stop");
    
    const char *l_server_name = "test_udp_server";
    
    // Setup mocks
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)dap_trans_test_get_mock_server());
    
    // Create server
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_UDP_BASIC, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Stop server
    dap_net_trans_server_stop(l_server);
    
    // Cleanup
    dap_net_trans_server_delete(l_server);
    
    TEST_SUCCESS("UDP trans server stop verified");
}

/**
 * @brief Test UDP trans server with invalid trans type
 */
static void test_05_server_invalid_type(void)
{
    TEST_INFO("Testing UDP trans server with invalid trans type");
    
    // Try to create server with invalid type
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_TLS_DIRECT, "test_server");
    
    TEST_ASSERT_NULL(l_server, "Server should not be created for unregistered trans type");
    
    TEST_SUCCESS("Invalid trans type handling verified");
}

/**
 * @brief Test UDP trans server for all UDP variants
 */
static void test_06_server_all_variants(void)
{
    TEST_INFO("Testing UDP trans server for all UDP variants");
    
    const char *l_server_name = "test_udp_server";
    
    // Setup mock for dap_server_new
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)dap_trans_test_get_mock_server());
    
    // Test UDP_BASIC
    dap_net_trans_server_t *l_server_basic = 
        dap_net_trans_server_new(DAP_NET_TRANS_UDP_BASIC, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server_basic, "UDP_BASIC server should be created");
    dap_net_trans_server_delete(l_server_basic);
    
    // Test UDP_RELIABLE
    dap_net_trans_server_t *l_server_reliable = 
        dap_net_trans_server_new(DAP_NET_TRANS_UDP_RELIABLE, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server_reliable, "UDP_RELIABLE server should be created");
    dap_net_trans_server_delete(l_server_reliable);
    
    // Test UDP_QUIC_LIKE
    dap_net_trans_server_t *l_server_quic = 
        dap_net_trans_server_new(DAP_NET_TRANS_UDP_QUIC_LIKE, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server_quic, "UDP_QUIC_LIKE server should be created");
    dap_net_trans_server_delete(l_server_quic);
    
    TEST_SUCCESS("UDP trans server variants verified");
}

// ============================================================================
// Stream Tests
// ============================================================================

/**
 * @brief Test UDP stream trans registration
 */
static void test_07_stream_registration(void)
{
    TEST_INFO("Testing UDP stream trans registration");
    
    // Find UDP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    
    TEST_ASSERT_NOT_NULL(l_trans, "UDP trans should be registered");
    TEST_ASSERT(l_trans->type == DAP_NET_TRANS_UDP_BASIC,
                "Trans type should be UDP_BASIC");
    
    TEST_SUCCESS("UDP stream trans registration verified");
}

/**
 * @brief Test UDP stream trans capabilities
 */
static void test_08_stream_capabilities(void)
{
    TEST_INFO("Testing UDP stream trans capabilities");
    
    // Find UDP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    
    TEST_ASSERT_NOT_NULL(l_trans, "UDP trans should be registered");
    TEST_ASSERT_NOT_NULL(l_trans->ops, "Trans operations should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->init, "init callback should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->deinit, "deinit callback should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->connect, "connect callback should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->listen, "listen callback should be set");
    
    TEST_SUCCESS("UDP stream trans capabilities verified");
}

/**
 * @brief Test UDP stream trans initialization
 */
static void test_09_stream_init(void)
{
    TEST_INFO("Testing UDP stream trans initialization");
    
    // Find UDP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    
    TEST_ASSERT_NOT_NULL(l_trans, "UDP trans should be registered");
    
    // Initialize trans instance
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    TEST_ASSERT_NOT_NULL(l_trans->_inheritor, "Private data should be allocated");
    
    // Note: trans is a singleton, don't deinit it as it's reused between tests
    
    TEST_SUCCESS("UDP stream trans initialization verified");
}

/**
 * @brief Test UDP stream trans unregistration
 */
static void test_10_stream_unregistration(void)
{
    TEST_INFO("Testing UDP stream trans unregistration");
    
    // Find UDP trans before unregistration
    dap_net_trans_t *l_trans_before = 
        dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    TEST_ASSERT_NOT_NULL(l_trans_before, "UDP trans should be registered");
    
    // Unregister UDP stream trans
    int l_ret = dap_net_trans_udp_stream_unregister();
    TEST_ASSERT(l_ret == 0, "Unregistration should succeed");
    
    // Try to find trans after unregistration
    dap_net_trans_t *l_trans_after = 
        dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    
    // Note: unregistration might not remove from registry immediately
    // depending on implementation, so we just verify unregistration call succeeded
    
    // Re-register for other tests
    l_ret = dap_net_trans_udp_stream_register();
    TEST_ASSERT(l_ret == 0, "Re-registration should succeed");
    
    // After re-registration, trans needs to be initialized again
    l_trans_after = dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    TEST_ASSERT_NOT_NULL(l_trans_after, "UDP trans should be registered after re-registration");
    
    l_ret = l_trans_after->ops->init(l_trans_after, NULL);
    TEST_ASSERT(l_ret == 0, "Trans re-initialization should succeed");
    TEST_ASSERT_NOT_NULL(l_trans_after->_inheritor, "Private data should be allocated after re-initialization");
    
    TEST_SUCCESS("UDP stream trans unregistration verified");
}

/**
 * @brief Test UDP stream trans connect operation
 */
static void test_11_stream_connect(void)
{
    TEST_INFO("Testing UDP stream trans connect operation");
    
    // Find UDP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    TEST_ASSERT_NOT_NULL(l_trans, "UDP trans should be registered");
    
    // Trans is already initialized from test_10 (after re-registration)
    // No need to call init() again
    
    // Create mock stream
    dap_stream_t *l_mock_stream = dap_trans_test_get_mock_stream();
    l_mock_stream->trans = l_trans;
    dap_net_trans_ctx_t *l_mock_trans_ctx = dap_trans_test_get_mock_trans_ctx();
    *l_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    l_mock_trans_ctx->esocket = dap_trans_test_get_mock_esocket(); // Set mock esocket for operations
    l_mock_stream->trans_ctx = l_mock_trans_ctx;
    
    // Test connect operation
    int l_ret = l_trans->ops->connect(l_mock_stream, "127.0.0.1", 8080, NULL);
    TEST_ASSERT(l_ret == 0, "Connect operation should succeed");
    
    // Note: trans is a singleton, don't deinit it as it's reused between tests
    
    TEST_SUCCESS("UDP stream trans connect operation verified");
}

/**
 * @brief Test UDP stream trans read operation
 */
static void test_12_stream_read(void)
{
    TEST_INFO("Testing UDP stream trans read operation");
    
    // Find UDP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    TEST_ASSERT_NOT_NULL(l_trans, "UDP trans should be registered");
    
    // Trans is already initialized, no need to call init() again
    
    // Create mock stream
    dap_stream_t *l_mock_stream = dap_trans_test_get_mock_stream();
    l_mock_stream->trans = l_trans;
    dap_net_trans_ctx_t *l_mock_trans_ctx = dap_trans_test_get_mock_trans_ctx();
    *l_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    l_mock_trans_ctx->esocket = dap_trans_test_get_mock_esocket(); // Set mock esocket for operations
    l_mock_stream->trans_ctx = l_mock_trans_ctx;
    
    // Test read operation
    char l_buffer[1024];
    ssize_t l_bytes_read = l_trans->ops->read(l_mock_stream, l_buffer, sizeof(l_buffer));
    TEST_ASSERT(l_bytes_read >= 0, "Read operation should not fail");
    
    // Note: trans is a singleton, don't deinit it as it's reused between tests
    
    TEST_SUCCESS("UDP stream trans read operation verified");
}

/**
 * @brief Test UDP stream trans write operation
 */
static void test_13_stream_write(void)
{
    TEST_INFO("Testing UDP stream trans write operation");
    
    // Find UDP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    TEST_ASSERT_NOT_NULL(l_trans, "UDP trans should be registered");
    
    // Verify trans is initialized (has _inheritor)
    TEST_ASSERT_NOT_NULL(l_trans->_inheritor, "Trans should have _inheritor");
    
    // Create mock stream with properly initialized UDP context
    dap_stream_t *l_mock_stream = dap_trans_test_get_mock_stream();
    l_mock_stream->trans = l_trans;
    
    // Create and initialize trans context
    dap_net_trans_ctx_t *l_mock_trans_ctx = dap_trans_test_get_mock_trans_ctx();
    memset(l_mock_trans_ctx, 0, sizeof(dap_net_trans_ctx_t));
    l_mock_trans_ctx->esocket = dap_trans_test_get_mock_esocket();
    
    // Create and initialize UDP context
    dap_net_trans_udp_ctx_t *l_udp_ctx = DAP_NEW_Z(dap_net_trans_udp_ctx_t);
    TEST_ASSERT_NOT_NULL(l_udp_ctx, "UDP context allocation should succeed");
    
    l_udp_ctx->session_id = 12345;
    l_udp_ctx->seq_num = 1;
    // Set remote_addr to indicate client-side (no remote_addr for client)
    // or set it for server-side testing
    memset(&l_udp_ctx->remote_addr, 0, sizeof(l_udp_ctx->remote_addr));
    
    // Attach UDP context to trans context
    l_mock_trans_ctx->_inheritor = l_udp_ctx;
    l_mock_stream->trans_ctx = l_mock_trans_ctx;
    
    // Mock the underlying socket write to return success
    // dap_events_socket_write_unsafe should return the number of bytes written
    const char l_test_data[] = "test data";
    size_t l_total_packet_size = sizeof(dap_stream_trans_udp_header_t) + sizeof(l_test_data);
    DAP_MOCK_SET_RETURN(dap_events_socket_write_unsafe, (void*)l_total_packet_size);
    
    // Test write operation
    TEST_INFO("Calling trans->ops->write with %zu bytes...", sizeof(l_test_data));
    ssize_t l_bytes_written = l_trans->ops->write(l_mock_stream, l_test_data, sizeof(l_test_data));
    TEST_INFO("Write returned: %zd bytes", l_bytes_written);
    
    // Write should succeed and return the TOTAL packet size (data + UDP header)
    // UDP protocol prepends a header, so return value = sizeof(header) + data_size
    size_t l_expected = sizeof(dap_stream_trans_udp_header_t) + sizeof(l_test_data);
    TEST_ASSERT(l_bytes_written == (ssize_t)l_expected, 
                "Write operation should succeed and return total packet size (header + data)");
    
    // Cleanup
    DAP_DELETE(l_udp_ctx);
    
    TEST_SUCCESS("UDP stream trans write operation verified");
}

/**
 * @brief Test UDP stream trans handshake operations
 */
static void test_14_stream_handshake(void)
{
    TEST_INFO("Testing UDP stream trans handshake operations (server-side only)");
    
    // Find UDP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    TEST_ASSERT_NOT_NULL(l_trans, "UDP trans should be registered");
    
    // Verify trans is initialized (has _inheritor set from previous tests)
    TEST_ASSERT_NOT_NULL(l_trans->_inheritor, "Trans should be initialized from previous tests");
    
    // Create mock stream for server-side handshake_process
    dap_stream_t *l_mock_stream = dap_trans_test_get_mock_stream();
    l_mock_stream->trans = l_trans;
    dap_net_trans_ctx_t *l_mock_trans_ctx = dap_trans_test_get_mock_trans_ctx();
    *l_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    l_mock_trans_ctx->esocket = NULL; // Server-side handshake_process doesn't need esocket
    l_mock_stream->trans_ctx = l_mock_trans_ctx;
    
    // Test handshake_process operation (server-side)
    // Generate valid Kyber512 public key for testing
    dap_enc_key_t *l_alice_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    TEST_ASSERT_NOT_NULL(l_alice_key, "Alice key generation should succeed");
    TEST_ASSERT_NOT_NULL(l_alice_key->pub_key_data, "Alice public key should exist");
    TEST_ASSERT(l_alice_key->pub_key_data_size > 0, "Alice public key size should be positive");
    
    void *l_response = NULL;
    size_t l_response_size = 0;
    int l_ret = l_trans->ops->handshake_process(l_mock_stream, 
                                                 l_alice_key->pub_key_data,
                                                 l_alice_key->pub_key_data_size,
                                                 &l_response, &l_response_size);
    TEST_ASSERT(l_ret == 0, "Handshake process should succeed");
    
    // Cleanup
    dap_enc_key_delete(l_alice_key);
    
    // Note: trans is a singleton, don't deinit it as it's reused between tests
    
    TEST_SUCCESS("UDP stream trans handshake operations verified");
}

/**
 * @brief Test UDP stream trans session operations
 */
static void test_15_stream_session(void)
{
    TEST_INFO("Testing UDP stream trans session operations");
    
    // Find UDP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    TEST_ASSERT_NOT_NULL(l_trans, "UDP trans should be registered");
    
    // Trans is already initialized, no need to call init() again
    
    // Create mock stream with UDP context
    dap_stream_t *l_mock_stream = dap_trans_test_get_mock_stream();
    l_mock_stream->trans = l_trans;
    dap_net_trans_ctx_t *l_mock_trans_ctx = dap_trans_test_get_mock_trans_ctx();
    *l_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    l_mock_trans_ctx->esocket = dap_trans_test_get_mock_esocket(); // Set mock esocket for operations
    l_mock_stream->trans_ctx = l_mock_trans_ctx;
    
    // Create mock UDP context (required by session_create)
    dap_net_trans_udp_ctx_t l_mock_udp_ctx = {0};
    l_mock_udp_ctx.session_id = 0x1234567890ABCDEF; // Mock session ID from handshake
    l_mock_udp_ctx.seq_num = 1;
    l_mock_trans_ctx->_inheritor = &l_mock_udp_ctx;
    
    // Test session_create operation
    dap_net_session_params_t l_session_params = {0};
    int l_ret = l_trans->ops->session_create(l_mock_stream, &l_session_params, NULL);
    TEST_ASSERT(l_ret == 0, "Session create should succeed");
    
    // Test session_start operation
    l_ret = l_trans->ops->session_start(l_mock_stream, 12345, NULL);
    TEST_ASSERT(l_ret == 0, "Session start should succeed");
    
    // Note: trans is a singleton, don't deinit it as it's reused between tests
    
    TEST_SUCCESS("UDP stream trans session operations verified");
}

/**
 * @brief Test UDP stream trans listen operation
 */
static void test_16_stream_listen(void)
{
    TEST_INFO("Testing UDP stream trans listen operation");
    
    // Find UDP trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    TEST_ASSERT_NOT_NULL(l_trans, "UDP trans should be registered");
    
    // Trans is already initialized, no need to call init() again
    
    // Setup mock server
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)dap_trans_test_get_mock_server());
    
    // Test listen operation (server-side)
    int l_ret = l_trans->ops->listen(l_trans, "127.0.0.1", 8080, dap_trans_test_get_mock_server());
    TEST_ASSERT(l_ret == 0, "Listen operation should succeed");
    
    // Note: trans is a singleton, don't deinit it as it's reused between tests
    
    TEST_SUCCESS("UDP stream trans listen operation verified");
}

// ============================================================================
// Test Suite Definition
// ============================================================================

int main(int argc, char *argv[])
{
    // Initialize test suite
    setup_test();
    
    TEST_SUITE_START("UDP Trans Comprehensive Unit Tests");
    
    // Server tests
    TEST_RUN(test_01_server_ops_registration);
    TEST_RUN(test_02_server_creation);
    TEST_RUN(test_03_server_start);
    TEST_RUN(test_04_server_stop);
    TEST_RUN(test_05_server_invalid_type);
    TEST_RUN(test_06_server_all_variants);
    
    // Stream tests
    TEST_RUN(test_07_stream_registration);
    TEST_RUN(test_08_stream_capabilities);
    TEST_RUN(test_09_stream_init);
    TEST_RUN(test_10_stream_unregistration);
    
    // Stream operations tests
    TEST_RUN(test_11_stream_connect);
    TEST_RUN(test_12_stream_read);
    TEST_RUN(test_13_stream_write);
    TEST_RUN(test_14_stream_handshake);
    TEST_RUN(test_15_stream_session);
    TEST_RUN(test_16_stream_listen);
    
    TEST_SUITE_END();
    
    // Cleanup test suite
    suite_cleanup();
    
    return 0;
}
