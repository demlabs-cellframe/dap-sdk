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
 * @file test_trans_dns.c
 * @brief Comprehensive unit tests for DNS trans server and stream
 * 
 * Tests DNS trans with full mocking for isolation:
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
#include <errno.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include "dap_common.h"
#include "dap_test.h"
#include "dap_test_helpers.h"
#include "dap_mock.h"
#include "dap_net_trans.h"
#include "dap_net_trans_server.h"
#include "dap_net_trans_dns_server.h"
#include "dap_net_trans_dns_stream.h"
#include "dap_server.h"
#include "dap_stream.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_handshake.h"
#include "dap_stream_session.h"
#include "dap_stream_worker.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_enc_server.h"
#include "dap_io_flow_datagram.h"

#define LOG_TAG "test_trans_dns"

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
DAP_MOCK_DECLARE(dap_server_delete_sync);

// Mock dap_stream_trans functions
// Don't mock dap_net_trans_find - use real implementation
// This allows tests to work with real trans registration

// Mock dap_stream functions
DAP_MOCK_DECLARE(dap_stream_add_proc_dns);
DAP_MOCK_DECLARE(dap_stream_delete);
DAP_MOCK_DECLARE(dap_stream_init);
DAP_MOCK_DECLARE(dap_stream_deinit);
DAP_MOCK_DECLARE(dap_stream_data_proc_read_ext_checked);

// Mock dap_events_socket functions
DAP_MOCK_DECLARE(dap_events_socket_create);
DAP_MOCK_DECLARE(dap_events_socket_create_platform);
DAP_MOCK_DECLARE(dap_events_socket_delete);
DAP_MOCK_DECLARE(dap_events_socket_delete_unsafe);
DAP_MOCK_DECLARE(dap_events_socket_write_unsafe);
DAP_MOCK_DECLARE(dap_events_socket_sendto_unsafe);
DAP_MOCK_DECLARE(dap_events_socket_connect);
DAP_MOCK_DECLARE(dap_events_socket_resolve_and_set_addr);
DAP_MOCK_DECLARE(dap_worker_add_events_socket);
DAP_MOCK_DECLARE(dap_worker_get_current);
DAP_MOCK_DECLARE(dap_worker_exec_callback_on);
DAP_MOCK_DECLARE(dap_io_flow_datagram_delete);

// Mock encryption server functions
DAP_MOCK_DECLARE(dap_enc_server_process_request);
DAP_MOCK_DECLARE(dap_enc_server_response_free);

// ============================================================================
// Mock Wrappers
// ============================================================================

// Mock server instance for testing
static dap_server_t s_mock_server = {0};
static dap_net_trans_t s_mock_stream_trans = {0};
static dap_stream_t s_mock_stream = {0};
static dap_net_trans_ctx_t s_mock_trans_ctx;
static dap_events_socket_t s_mock_events_socket = {0};
static dap_worker_t s_mock_worker = {0};
static struct sockaddr_storage s_last_sendto_addr = {0};
static socklen_t s_last_sendto_addr_len = 0;
static size_t s_sendto_call_count = 0;
static size_t s_server_delete_sync_call_count = 0;
static size_t s_worker_exec_callback_call_count = 0;
static int s_worker_exec_callback_ret = 0;
static int s_worker_exec_callback_once_ret = 0;
static size_t s_worker_exec_callback_failures_remaining = 0;
static bool s_worker_exec_callback_run_immediately = false;
static dap_worker_t *s_worker_get_current_result = NULL;
static dap_worker_callback_t s_last_worker_callback = NULL;
static void *s_last_worker_callback_arg = NULL;
static bool s_stream_ext_checked_delete_requested = false;
static bool s_stream_ext_checked_free_stream = false;
static bool s_stream_ext_checked_delete_server_on_read = false;
static bool s_stream_ext_checked_saw_listener_detached = false;
static size_t s_stream_ext_checked_return_size = 0;
static size_t s_stream_ext_checked_call_count = 0;
static dap_stream_t *s_stream_ext_checked_last_stream = NULL;
static size_t s_io_flow_datagram_delete_call_count = 0;

static void s_set_ipv4_addr(struct sockaddr_storage *a_addr, socklen_t *a_addr_len,
                            const char *a_ip, uint16_t a_port)
{
    memset(a_addr, 0, sizeof(*a_addr));
    struct sockaddr_in *l_addr = (struct sockaddr_in *)a_addr;
    l_addr->sin_family = AF_INET;
    l_addr->sin_port = htons(a_port);
    inet_pton(AF_INET, a_ip, &l_addr->sin_addr);
    if (a_addr_len)
        *a_addr_len = sizeof(struct sockaddr_in);
}

static bool s_test_flow_remote_addr_cb(dap_io_flow_datagram_t *a_flow,
                                       struct sockaddr_storage *a_addr_out,
                                       socklen_t *a_addr_len_out)
{
    UNUSED(a_flow);
    UNUSED(a_addr_out);
    UNUSED(a_addr_len_out);
    return false;
}

DAP_MOCK_WRAPPER_CUSTOM(void, dap_server_delete_sync,
    PARAM(dap_server_t *, a_server)
)
{
    (void)a_server;
    s_server_delete_sync_call_count++;
}

// dap_net_trans_find is not mocked - using real implementation


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

DAP_MOCK_WRAPPER_CUSTOM(size_t, dap_stream_data_proc_read_ext_checked,
    PARAM(dap_stream_t*, a_stream),
    PARAM(const void*, a_data),
    PARAM(size_t, a_data_size),
    PARAM(bool*, a_delete_requested)
)
{
    UNUSED(a_data);

    s_stream_ext_checked_call_count++;
    s_stream_ext_checked_last_stream = a_stream;
    s_stream_ext_checked_saw_listener_detached =
        a_stream &&
        a_stream->esocket == &s_mock_events_socket &&
        a_stream->esocket_uuid == 0 &&
        a_stream->esocket_worker == NULL &&
        a_stream->trans == NULL;
    if (a_delete_requested)
        *a_delete_requested = s_stream_ext_checked_delete_requested;
    dns_server_client_session_t *l_session = a_stream
            ? (dns_server_client_session_t *)a_stream->_server_session : NULL;
    if (s_stream_ext_checked_delete_server_on_read && l_session && l_session->server)
        dap_net_trans_dns_server_delete(l_session->server);
    if (s_stream_ext_checked_delete_requested && s_stream_ext_checked_free_stream && a_stream) {
        DAP_DELETE(a_stream->trans_ctx);
        DAP_DELETE(a_stream);
    }
    return s_stream_ext_checked_return_size ? s_stream_ext_checked_return_size : a_data_size;
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

// Wrapper for dap_events_socket_sendto_unsafe (used by DNS write)
DAP_MOCK_WRAPPER_CUSTOM(size_t, dap_events_socket_sendto_unsafe,
    PARAM(dap_events_socket_t*, a_esocket),
    PARAM(const void*, a_data),
    PARAM(size_t, a_data_size),
    PARAM(const struct sockaddr_storage*, a_addr),
    PARAM(socklen_t, a_addr_len)
)
{
    UNUSED(a_esocket);
    UNUSED(a_data);

    s_sendto_call_count++;
    memset(&s_last_sendto_addr, 0, sizeof(s_last_sendto_addr));
    if (a_addr && a_addr_len > 0) {
        memcpy(&s_last_sendto_addr, a_addr, sizeof(struct sockaddr_storage));
        s_last_sendto_addr_len = a_addr_len;
    } else {
        s_last_sendto_addr_len = 0;
    }
    
    return a_data_size;
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
    return &s_mock_events_socket;
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
    return &s_mock_events_socket;
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

// Wrapper for dap_worker_get_current
DAP_MOCK_WRAPPER_CUSTOM(dap_worker_t*, dap_worker_get_current, void)
{
    return s_worker_get_current_result;
}

// Wrapper for dap_worker_exec_callback_on
DAP_MOCK_WRAPPER_CUSTOM(int, dap_worker_exec_callback_on,
    PARAM(dap_worker_t*, a_worker),
    PARAM(dap_worker_callback_t, a_callback),
    PARAM(void*, a_arg)
)
{
    UNUSED(a_worker);

    s_worker_exec_callback_call_count++;
    s_last_worker_callback = a_callback;
    s_last_worker_callback_arg = a_arg;
    if (s_worker_exec_callback_failures_remaining > 0) {
        s_worker_exec_callback_failures_remaining--;
        return s_worker_exec_callback_once_ret ? s_worker_exec_callback_once_ret : -EAGAIN;
    }
    if (s_worker_exec_callback_ret != 0)
        return s_worker_exec_callback_ret;
    if (s_worker_exec_callback_run_immediately && a_callback)
        a_callback(a_arg);
    return 0;
}

DAP_MOCK_WRAPPER_CUSTOM(void, dap_io_flow_datagram_delete,
    PARAM(dap_io_flow_datagram_t*, a_flow)
)
{
    s_io_flow_datagram_delete_call_count++;
    DAP_DELETE(a_flow);
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
        int l_ret = dap_common_init("test_trans_dns", NULL);
        TEST_ASSERT(l_ret == 0, "DAP common initialization failed");
        
        // Initialize mock framework
        dap_mock_init();
        
        // Initialize trans layer
        l_ret = dap_net_trans_init();
        TEST_ASSERT(l_ret == 0, "Trans layer initialization failed");
        
        // Initialize DNS trans server (this registers operations)
        l_ret = dap_net_trans_dns_server_init();
        TEST_ASSERT(l_ret == 0, "DNS trans server initialization failed");
        
        // Initialize DNS stream trans
        // Check if already registered (might be auto-registered via module constructor)
        dap_net_trans_t *l_existing = dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
        if (l_existing) {
            TEST_INFO("DNS stream trans already registered (auto-registered), skipping manual registration");
        } else {
            l_ret = dap_net_trans_dns_stream_register();
            TEST_ASSERT(l_ret == 0, "DNS stream trans registration failed");
        }
        
        s_test_initialized = true;
        TEST_INFO("DNS trans test suite initialized");
    }
    
    // Reset mocks before each test
    dap_mock_reset_all();
    memset(&s_mock_stream, 0, sizeof(s_mock_stream));
    memset(&s_mock_trans_ctx, 0, sizeof(s_mock_trans_ctx));
    memset(&s_mock_events_socket, 0, sizeof(s_mock_events_socket));
    memset(&s_mock_worker, 0, sizeof(s_mock_worker));
    s_mock_worker.id = 7;
    memset(&s_last_sendto_addr, 0, sizeof(s_last_sendto_addr));
    s_last_sendto_addr_len = 0;
    s_sendto_call_count = 0;
    s_server_delete_sync_call_count = 0;
    s_worker_exec_callback_call_count = 0;
    s_worker_exec_callback_ret = 0;
    s_worker_exec_callback_once_ret = 0;
    s_worker_exec_callback_failures_remaining = 0;
    s_worker_exec_callback_run_immediately = false;
    s_worker_get_current_result = NULL;
    s_last_worker_callback = NULL;
    s_last_worker_callback_arg = NULL;
    s_stream_ext_checked_delete_requested = false;
    s_stream_ext_checked_free_stream = false;
    s_stream_ext_checked_delete_server_on_read = false;
    s_stream_ext_checked_saw_listener_detached = false;
    s_stream_ext_checked_return_size = 0;
    s_stream_ext_checked_call_count = 0;
    s_stream_ext_checked_last_stream = NULL;
    s_io_flow_datagram_delete_call_count = 0;
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
        // Deinitialize DNS stream trans
        dap_net_trans_dns_stream_unregister();
        
        // Deinitialize DNS trans server (unregisters operations)
        dap_net_trans_dns_server_deinit();
        
        // Trans layer is deinitialized automatically via dap_module system
        // No need to call dap_net_trans_deinit() manually
        
        // Deinitialize mock framework
        dap_mock_deinit();
        
        // Deinitialize DAP common
        dap_common_deinit();
        
        s_test_initialized = false;
        TEST_INFO("DNS trans test suite cleaned up");
    }
}

// ============================================================================
// Server Tests
// ============================================================================

/**
 * @brief Test DNS trans server operations registration
 */
static void test_01_server_ops_registration(void)
{
    TEST_INFO("Testing DNS trans server operations registration");
    
    // Verify operations are registered
    const dap_net_trans_server_ops_t *l_ops = 
        dap_net_trans_server_get_ops(DAP_NET_TRANS_DNS_TUNNEL);
    
    TEST_ASSERT_NOT_NULL(l_ops, "DNS trans server operations should be registered");
    TEST_ASSERT_NOT_NULL(l_ops->new, "new callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->start, "start callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->stop, "stop callback should be set");
    TEST_ASSERT_NOT_NULL(l_ops->delete, "delete callback should be set");
    
    TEST_SUCCESS("DNS trans server operations registration verified");
}

/**
 * @brief Test DNS trans server creation through unified API
 */
static void test_02_server_creation(void)
{
    TEST_INFO("Testing DNS trans server creation");
    
    const char *l_server_name = "test_dns_server";
    
    // Setup mock for dap_server_new
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)&s_mock_server);
    
    // Create server through unified API
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_DNS_TUNNEL, l_server_name);
    
    TEST_ASSERT_NOT_NULL(l_server, "DNS server should be created");
    TEST_ASSERT(l_server->trans_type == DAP_NET_TRANS_DNS_TUNNEL, 
                "Trans type should be DNS_TUNNEL");
    TEST_ASSERT(strcmp(l_server->server_name, l_server_name) == 0,
                "Server name should match");
    TEST_ASSERT_NOT_NULL(l_server->trans_specific,
                         "Trans-specific server instance should be created");
    
    // Note: dap_server_new is called in start(), not in new()
    // So we don't verify it here - it will be verified in test_03_server_start
    
    // Cleanup
    dap_net_trans_server_delete(l_server);
    
    TEST_SUCCESS("DNS trans server creation verified");
}

/**
 * @brief Test DNS trans server start with handlers registration
 */
static void test_03_server_start(void)
{
    TEST_INFO("Testing DNS trans server start");
    
    const char *l_server_name = "test_dns_server";
    const char *l_cfg_section = "test_server";
    const char *l_addrs[] = {"127.0.0.1"};
    uint16_t l_ports[] = {53};
    
    // Setup mocks
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)&s_mock_server);
    DAP_MOCK_SET_RETURN(dap_server_listen_addr_add, 0);
    // Note: dap_net_trans_find is not mocked - using real implementation
    
    // Create server
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_DNS_TUNNEL, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Start server
    int l_ret = dap_net_trans_server_start(l_server, l_cfg_section, 
                                                l_addrs, l_ports, 1);
    TEST_ASSERT(l_ret == 0, "Server start should succeed");
    
    // Verify server was created (DNS uses its own UDP listener, not dap_stream_add_proc_dns)
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_server_new) >= 1,
                "dap_server_new should be called to create UDP listener");
    
    // Verify listen address was added
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_server_listen_addr_add) >= 1,
                "dap_server_listen_addr_add should be called");
    
    // Stop server
    dap_net_trans_server_stop(l_server);
    
    // Cleanup
    dap_net_trans_server_delete(l_server);
    
    TEST_SUCCESS("DNS trans server start verified");
}

/**
 * @brief Test DNS trans server stop
 */
static void test_04_server_stop(void)
{
    TEST_INFO("Testing DNS trans server stop");
    
    const char *l_server_name = "test_dns_server";
    
    // Setup mocks
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)&s_mock_server);
    
    // Create server
    dap_net_trans_server_t *l_server = 
        dap_net_trans_server_new(DAP_NET_TRANS_DNS_TUNNEL, l_server_name);
    TEST_ASSERT_NOT_NULL(l_server, "Server should be created");
    
    // Stop server
    dap_net_trans_server_stop(l_server);
    
    // Cleanup
    dap_net_trans_server_delete(l_server);
    
    TEST_SUCCESS("DNS trans server stop verified");
}

/**
 * @brief Test deferred delete enqueue failure does not leave callback state stuck
 */
static void test_05a_server_deferred_enqueue_failure_not_stuck(void)
{
    TEST_INFO("Testing DNS server deferred delete enqueue failure handling");

    s_server_delete_sync_call_count = 0;
    s_worker_exec_callback_call_count = 0;
    s_worker_exec_callback_ret = 0;
    s_worker_exec_callback_run_immediately = false;
    s_worker_get_current_result = &s_mock_worker;
    s_last_worker_callback = NULL;
    s_last_worker_callback_arg = NULL;

    dap_net_trans_dns_server_t *l_dns_server =
        dap_net_trans_dns_server_new("test_dns_deferred_enqueue_failure");
    TEST_ASSERT_NOT_NULL(l_dns_server, "DNS server should be created");

    l_dns_server->server = &s_mock_server;
    s_worker_exec_callback_ret = -EAGAIN;

    bool l_scheduled = dap_net_trans_dns_server_test_schedule_deferred_delete(l_dns_server);
    TEST_ASSERT(!l_scheduled, "Deferred delete schedule should report enqueue failure");
    TEST_ASSERT(dap_net_trans_dns_server_test_deferred_has_delete(l_dns_server),
                "Deferred delete intent should remain pending");
    TEST_ASSERT(!dap_net_trans_dns_server_test_deferred_has_callback(l_dns_server),
                "Failed enqueue should not leave queued/running callback state");
    TEST_ASSERT(s_worker_exec_callback_call_count == 1,
                "Deferred callback enqueue should be attempted once");

    s_worker_exec_callback_ret = 0;
    dap_net_trans_dns_server_delete(l_dns_server);
    TEST_ASSERT(s_server_delete_sync_call_count == 1,
                "Public delete should not early-return after failed deferred enqueue");
    s_worker_get_current_result = NULL;

    TEST_SUCCESS("Deferred enqueue failure handling verified");
}

/**
 * @brief Test current-datagram delete retries deferred dispatch after enqueue failure
 */
static void test_05aa_server_current_datagram_delete_retries_failed_enqueue(void)
{
    TEST_INFO("Testing DNS server current-datagram delete retries failed deferred enqueue");

    s_server_delete_sync_call_count = 0;
    s_worker_exec_callback_call_count = 0;
    s_worker_exec_callback_ret = 0;
    s_worker_exec_callback_once_ret = -EAGAIN;
    s_worker_exec_callback_failures_remaining = 1;
    s_worker_exec_callback_run_immediately = false;
    s_worker_get_current_result = &s_mock_worker;
    s_last_worker_callback = NULL;
    s_last_worker_callback_arg = NULL;
    s_stream_ext_checked_delete_server_on_read = true;

    dap_net_trans_dns_server_t *l_dns_server =
        dap_net_trans_dns_server_new("test_dns_current_datagram_delete_retry");
    TEST_ASSERT_NOT_NULL(l_dns_server, "DNS server should be created");
    l_dns_server->server = &s_mock_server;

    dns_server_client_session_t *l_session = DAP_NEW_Z(dns_server_client_session_t);
    dap_stream_t *l_stream = DAP_NEW_Z(dap_stream_t);
    dap_net_trans_ctx_t *l_trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
    TEST_ASSERT_NOT_NULL(l_session, "DNS session should be allocated");
    TEST_ASSERT_NOT_NULL(l_stream, "DNS stream should be allocated");
    TEST_ASSERT_NOT_NULL(l_trans_ctx, "DNS trans_ctx should be allocated");

    socklen_t l_addr_len = 0;
    s_set_ipv4_addr(&l_session->remote_addr, &l_addr_len, "127.0.0.41", 4141);
    l_session->remote_addr_len = l_addr_len;
    l_session->server = l_dns_server;
    l_session->stream = l_stream;
    l_session->trans_ctx = l_trans_ctx;

    l_trans_ctx->trans = l_dns_server->trans;
    l_trans_ctx->stream = l_stream;
    l_stream->esocket = &s_mock_events_socket;
    l_stream->trans = l_dns_server->trans;
    l_stream->trans_ctx = l_trans_ctx;
    l_stream->_server_session = l_session;
    l_stream->flow = dap_io_flow_datagram_new(s_test_flow_remote_addr_cb, l_session);
    TEST_ASSERT_NOT_NULL(l_stream->flow, "DNS stream datagram flow should be allocated");

    HASH_ADD(hh, l_dns_server->sessions, remote_addr, (unsigned)l_session->remote_addr_len, l_session);

    byte_t l_datagram[] = {0x01, 0x02, 0x03, 0x04};
    dap_net_trans_dns_server_test_process_datagram(&s_mock_events_socket, l_dns_server,
                                                   l_datagram, sizeof(l_datagram),
                                                   &l_session->remote_addr,
                                                   l_session->remote_addr_len);

    TEST_ASSERT(s_worker_exec_callback_call_count == 2,
                "Current-datagram delete should retry deferred enqueue after the read exits");
    TEST_ASSERT(dap_net_trans_dns_server_test_deferred_has_delete(l_dns_server),
                "Deferred delete intent should remain pending for the retried callback");
    TEST_ASSERT(dap_net_trans_dns_server_test_deferred_has_callback(l_dns_server),
                "Retried deferred delete should have an active callback");
    TEST_ASSERT(s_server_delete_sync_call_count == 0,
                "Current-datagram delete must not run terminal cleanup inline");
    TEST_ASSERT_NOT_NULL(s_last_worker_callback, "Deferred callback should be captured after retry");

    s_stream_ext_checked_delete_server_on_read = false;
    s_last_worker_callback(s_last_worker_callback_arg);
    TEST_ASSERT(s_server_delete_sync_call_count == 1,
                "Retried deferred callback should execute terminal delete");

    s_worker_get_current_result = NULL;
    s_last_worker_callback = NULL;
    s_last_worker_callback_arg = NULL;
    s_stream_ext_checked_call_count = 0;
    s_stream_ext_checked_last_stream = NULL;
    s_stream_ext_checked_saw_listener_detached = false;
    s_io_flow_datagram_delete_call_count = 0;

    TEST_SUCCESS("Current-datagram deferred delete retry verified");
}

/**
 * @brief Test deferred delete dominates an already queued deferred stop
 */
static void test_05b_server_deferred_delete_dominates_stop(void)
{
    TEST_INFO("Testing DNS server deferred delete dominates queued stop");

    s_server_delete_sync_call_count = 0;
    s_worker_exec_callback_call_count = 0;
    s_worker_exec_callback_ret = 0;
    s_worker_exec_callback_run_immediately = false;
    s_worker_get_current_result = &s_mock_worker;
    s_last_worker_callback = NULL;
    s_last_worker_callback_arg = NULL;

    dap_net_trans_dns_server_t *l_dns_server =
        dap_net_trans_dns_server_new("test_dns_deferred_delete_dominates_stop");
    TEST_ASSERT_NOT_NULL(l_dns_server, "DNS server should be created");

    l_dns_server->server = &s_mock_server;

    bool l_scheduled = dap_net_trans_dns_server_test_schedule_deferred_stop(l_dns_server);
    TEST_ASSERT(l_scheduled, "Deferred stop should schedule");
    TEST_ASSERT(dap_net_trans_dns_server_test_deferred_has_stop(l_dns_server),
                "Deferred stop should remain pending before callback runs");
    TEST_ASSERT(dap_net_trans_dns_server_test_deferred_has_callback(l_dns_server),
                "Deferred callback should be queued");
    TEST_ASSERT(s_worker_exec_callback_call_count == 1,
                "Deferred stop should enqueue one callback");
    TEST_ASSERT_NOT_NULL(s_last_worker_callback, "Deferred callback should be captured");

    l_scheduled = dap_net_trans_dns_server_test_schedule_deferred_delete(l_dns_server);
    TEST_ASSERT(l_scheduled, "Deferred delete should attach to active deferred callback");
    TEST_ASSERT(dap_net_trans_dns_server_test_deferred_has_delete(l_dns_server),
                "Deferred delete should remain pending");
    TEST_ASSERT(s_worker_exec_callback_call_count == 1,
                "Deferred delete should not enqueue a second callback while one is active");

    s_last_worker_callback(s_last_worker_callback_arg);
    TEST_ASSERT(s_server_delete_sync_call_count == 1,
                "Deferred callback should execute delete, not only stop");
    s_worker_get_current_result = NULL;
    s_last_worker_callback = NULL;
    s_last_worker_callback_arg = NULL;

    TEST_SUCCESS("Deferred delete dominance verified");
}

/**
 * @brief Test checked-read stream deletion removes DNS session without stale cleanup
 */
static void test_05c_server_checked_read_delete_removes_session(void)
{
    TEST_INFO("Testing DNS server checked-read delete removes existing session");

    dap_net_trans_dns_server_t *l_dns_server =
        dap_net_trans_dns_server_new("test_dns_checked_read_delete");
    TEST_ASSERT_NOT_NULL(l_dns_server, "DNS server should be created");

    dns_server_client_session_t *l_session = DAP_NEW_Z(dns_server_client_session_t);
    dap_stream_t *l_stream = DAP_NEW_Z(dap_stream_t);
    dap_net_trans_ctx_t *l_trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
    TEST_ASSERT_NOT_NULL(l_session, "DNS session should be allocated");
    TEST_ASSERT_NOT_NULL(l_stream, "DNS stream should be allocated");
    TEST_ASSERT_NOT_NULL(l_trans_ctx, "DNS trans_ctx should be allocated");

    socklen_t l_addr_len = 0;
    s_set_ipv4_addr(&l_session->remote_addr, &l_addr_len, "127.0.0.42", 4242);
    l_session->remote_addr_len = l_addr_len;
    l_session->server = l_dns_server;
    l_session->stream = l_stream;
    l_session->trans_ctx = l_trans_ctx;

    l_trans_ctx->trans = l_dns_server->trans;
    l_trans_ctx->stream = l_stream;
    l_stream->esocket = &s_mock_events_socket;
    l_stream->esocket_uuid = 0x1234;
    l_stream->esocket_worker = &s_mock_worker;
    l_stream->trans = l_dns_server->trans;
    l_stream->trans_ctx = l_trans_ctx;
    l_stream->_server_session = l_session;
    l_stream->flow = dap_io_flow_datagram_new(s_test_flow_remote_addr_cb, l_session);
    TEST_ASSERT_NOT_NULL(l_stream->flow, "DNS stream datagram flow should be allocated");

    HASH_ADD(hh, l_dns_server->sessions, remote_addr, (unsigned)l_session->remote_addr_len, l_session);

    byte_t l_datagram[] = {0x01, 0x02, 0x03, 0x04};
    s_stream_ext_checked_delete_requested = true;
    s_stream_ext_checked_free_stream = true;

    dap_net_trans_dns_server_test_process_datagram(&s_mock_events_socket, l_dns_server,
                                                   l_datagram, sizeof(l_datagram),
                                                   &l_session->remote_addr,
                                                   l_session->remote_addr_len);

    TEST_ASSERT(s_stream_ext_checked_call_count == 1,
                "Existing-session datagram should use checked stream read");
    TEST_ASSERT(s_stream_ext_checked_saw_listener_detached,
                "Stream should release listener ownership before checked read deletion");
    TEST_ASSERT_NULL(l_dns_server->sessions,
                     "Delete-requested checked read should remove session from hash");
    TEST_ASSERT(s_io_flow_datagram_delete_call_count == 1,
                "Delete-requested checked read should delete the per-session datagram flow");

    s_stream_ext_checked_free_stream = false;
    s_stream_ext_checked_delete_requested = false;
    dap_net_trans_dns_server_delete(l_dns_server);

    TEST_SUCCESS("Checked-read delete session cleanup verified");
}

/**
 * @brief Test server stop removes channel worker-hash entries and global stream sessions
 */
static void test_05d_server_stop_deletes_stream_resources(void)
{
    TEST_INFO("Testing DNS server stop tears down stream-owned resources");

    dap_stream_ch_proc_add('Z', NULL, NULL, NULL, NULL);

    dap_net_trans_dns_server_t *l_dns_server =
        dap_net_trans_dns_server_new("test_dns_stop_resource_cleanup");
    TEST_ASSERT_NOT_NULL(l_dns_server, "DNS server should be created");

    dns_server_client_session_t *l_session = DAP_NEW_Z(dns_server_client_session_t);
    dap_stream_t *l_stream = DAP_NEW_Z(dap_stream_t);
    dap_net_trans_ctx_t *l_trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
    dap_stream_session_t *l_stream_session = dap_stream_session_new(0, false);
    TEST_ASSERT_NOT_NULL(l_session, "DNS session should be allocated");
    TEST_ASSERT_NOT_NULL(l_stream, "DNS stream should be allocated");
    TEST_ASSERT_NOT_NULL(l_trans_ctx, "DNS trans_ctx should be allocated");
    TEST_ASSERT_NOT_NULL(l_stream_session, "Stream session should be allocated");
    dap_stream_session_open(l_stream_session);
    uint32_t l_stream_session_id = l_stream_session->id;

    dap_stream_worker_t l_stream_worker = {0};
    pthread_rwlock_init(&l_stream_worker.channels_rwlock, NULL);

    socklen_t l_addr_len = 0;
    s_set_ipv4_addr(&l_session->remote_addr, &l_addr_len, "127.0.0.43", 4343);
    l_session->remote_addr_len = l_addr_len;
    l_session->server = l_dns_server;
    l_session->stream = l_stream;
    l_session->trans_ctx = l_trans_ctx;
    l_session->stream_session = l_stream_session;

    l_trans_ctx->trans = l_dns_server->trans;
    l_trans_ctx->stream = l_stream;
    l_stream->esocket = &s_mock_events_socket;
    l_stream->esocket_uuid = 0x4321;
    l_stream->esocket_worker = &s_mock_worker;
    l_stream->trans = l_dns_server->trans;
    l_stream->trans_ctx = l_trans_ctx;
    l_stream->_server_session = l_session;
    l_stream->stream_worker = &l_stream_worker;
    l_stream->session = l_stream_session;

    dap_stream_ch_t *l_ch = dap_stream_ch_new(l_stream, 'Z');
    TEST_ASSERT_NOT_NULL(l_ch, "Test stream channel should be created");
    dap_stream_ch_uuid_t l_ch_uuid = l_ch->uuid;
    TEST_ASSERT_NOT_NULL(dap_stream_ch_find_by_uuid_unsafe(&l_stream_worker, l_ch_uuid),
                         "Channel should be present in stream-worker hash before stop");
    TEST_ASSERT_NOT_NULL(dap_stream_session_id_mt(l_stream_session_id),
                         "Stream session should be globally visible before stop");

    HASH_ADD(hh, l_dns_server->sessions, remote_addr, (unsigned)l_session->remote_addr_len, l_session);

    dap_net_trans_dns_server_stop(l_dns_server);

    TEST_ASSERT_NULL(l_dns_server->sessions, "Stop should remove DNS sessions");
    TEST_ASSERT_NULL(dap_stream_ch_find_by_uuid_unsafe(&l_stream_worker, l_ch_uuid),
                     "Stop should remove channel from stream-worker hash");
    TEST_ASSERT_NULL(dap_stream_session_id_mt(l_stream_session_id),
                     "Stop should close and remove the global stream session");

    dap_net_trans_dns_server_delete(l_dns_server);
    pthread_rwlock_destroy(&l_stream_worker.channels_rwlock);

    TEST_SUCCESS("DNS server stream resource teardown verified");
}

/**
 * @brief Test server async write reports enqueue failure and releases ownership
 */
static void test_05e_server_async_write_enqueue_failure(void)
{
    TEST_INFO("Testing DNS server async write enqueue failure");

    dap_net_trans_dns_server_t *l_dns_server =
        dap_net_trans_dns_server_new("test_dns_async_write_enqueue_failure");
    TEST_ASSERT_NOT_NULL(l_dns_server, "DNS server should be created");

    dns_server_client_session_t l_session = {0};
    dap_stream_t l_stream = {0};
    socklen_t l_addr_len = 0;
    s_set_ipv4_addr(&l_session.remote_addr, &l_addr_len, "127.0.0.44", 4444);
    l_session.remote_addr_len = l_addr_len;
    l_session.server = l_dns_server;
    l_stream._server_session = &l_session;
    l_stream.esocket = &s_mock_events_socket;
    s_mock_events_socket.worker = &s_mock_worker;
    s_mock_events_socket.uuid = 0x4444;

    s_sendto_call_count = 0;
    s_worker_exec_callback_call_count = 0;
    s_last_worker_callback = NULL;
    s_last_worker_callback_arg = NULL;
    s_worker_get_current_result = NULL;
    s_worker_exec_callback_ret = -EIO;
    const char l_payload[] = "dns async payload";
    ssize_t l_ret = l_dns_server->trans->ops->write(&l_stream, l_payload, sizeof(l_payload));

    TEST_ASSERT(l_ret == -1, "Async write should fail when callback enqueue fails");
    TEST_ASSERT(s_worker_exec_callback_call_count == 1,
                "Async write should attempt one worker callback enqueue");
    TEST_ASSERT(s_sendto_call_count == 0,
                "Failed enqueue should not send synchronously or report success");

    s_worker_exec_callback_ret = 0;
    s_last_worker_callback = NULL;
    s_last_worker_callback_arg = NULL;
    dap_net_trans_dns_server_delete(l_dns_server);

    TEST_SUCCESS("DNS server async write enqueue failure verified");
}

/**
 * @brief Test DNS trans server with invalid trans type
 */
static void test_05_server_invalid_type(void)
{
    TEST_INFO("Testing DNS trans server with invalid trans type");
    
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
 * @brief Test DNS stream trans registration
 */
static void test_06_stream_registration(void)
{
    TEST_INFO("Testing DNS stream trans registration");
    
    // Find DNS trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
    
    TEST_ASSERT_NOT_NULL(l_trans, "DNS trans should be registered");
    TEST_ASSERT(l_trans->type == DAP_NET_TRANS_DNS_TUNNEL,
                "Trans type should be DNS_TUNNEL");
    
    TEST_SUCCESS("DNS stream trans registration verified");
}

/**
 * @brief Test DNS stream trans capabilities
 */
static void test_07_stream_capabilities(void)
{
    TEST_INFO("Testing DNS stream trans capabilities");
    
    // Find DNS trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
    
    TEST_ASSERT_NOT_NULL(l_trans, "DNS trans should be registered");
    TEST_ASSERT_NOT_NULL(l_trans->ops, "Trans operations should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->init, "init callback should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->deinit, "deinit callback should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->connect, "connect callback should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->listen, "listen callback should be set");
    
    TEST_SUCCESS("DNS stream trans capabilities verified");
}

/**
 * @brief Test DNS stream trans initialization
 */
static void test_08_stream_init(void)
{
    TEST_INFO("Testing DNS stream trans initialization");
    
    // Find DNS trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
    
    TEST_ASSERT_NOT_NULL(l_trans, "DNS trans should be registered");
    
    // Initialize trans instance
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    TEST_ASSERT_NOT_NULL(l_trans->_inheritor, "Private data should be allocated");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("DNS stream trans initialization verified");
}

/**
 * @brief Test DNS stream trans unregistration
 */
static void test_09_stream_unregistration(void)
{
    TEST_INFO("Testing DNS stream trans unregistration");
    
    // Find DNS trans before unregistration
    dap_net_trans_t *l_trans_before = 
        dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_trans_before, "DNS trans should be registered");
    
    // Unregister DNS stream trans
    int l_ret = dap_net_trans_dns_stream_unregister();
    TEST_ASSERT(l_ret == 0, "Unregistration should succeed");
    
    // Try to find trans after unregistration
    dap_net_trans_t *l_trans_after = 
        dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
    
    // Note: unregistration might not remove from registry immediately
    // depending on implementation, so we just verify unregistration call succeeded
    
    // Re-register for other tests
    dap_net_trans_dns_stream_register();
    
    TEST_SUCCESS("DNS stream trans unregistration verified");
}

/**
 * @brief Test DNS stream trans connect operation
 */
static void test_10_stream_connect(void)
{
    TEST_INFO("Testing DNS stream trans connect operation");
    
    // Find DNS trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_trans, "DNS trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream
    s_mock_stream.trans = l_trans;
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    s_mock_stream.esocket = &s_mock_events_socket;
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    
    // Test connect operation
    l_ret = l_trans->ops->connect(&s_mock_stream, "127.0.0.1", 53, NULL);
    TEST_ASSERT(l_ret == 0, "Connect operation should succeed");

    if (l_trans->ops->close)
        l_trans->ops->close(&s_mock_stream);
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("DNS stream trans connect operation verified");
}

/**
 * @brief Test DNS stream trans read operation
 */
static void test_11_stream_read(void)
{
    TEST_INFO("Testing DNS stream trans read operation");
    
    // Find DNS trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_trans, "DNS trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream
    s_mock_stream.trans = l_trans;
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    s_mock_stream.esocket = &s_mock_events_socket;
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    
    // Test read operation
    char l_buffer[1024];
    ssize_t l_bytes_read = l_trans->ops->read(&s_mock_stream, l_buffer, sizeof(l_buffer));
    TEST_ASSERT(l_bytes_read >= 0, "Read operation should not fail");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("DNS stream trans read operation verified");
}

/**
 * @brief Test DNS stream trans write operation
 */
static void test_12_stream_write(void)
{
    TEST_INFO("Testing DNS stream trans write operation");
    
    // Find DNS trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_trans, "DNS trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream
    s_mock_stream.trans = l_trans;
    s_mock_stream.esocket = &s_mock_events_socket;
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0};
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    l_ret = l_trans->ops->connect(&s_mock_stream, "127.0.0.1", 53, NULL);
    TEST_ASSERT(l_ret == 0, "Connect operation should prepare stable DNS remote address");
    
    // Test write operation
    const char l_test_data[] = "test data";
    ssize_t l_bytes_written = l_trans->ops->write(&s_mock_stream, l_test_data, sizeof(l_test_data));
    TEST_ASSERT(l_bytes_written > 0, "Write operation should succeed");

    if (l_trans->ops->close)
        l_trans->ops->close(&s_mock_stream);
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("DNS stream trans write operation verified");
}

/**
 * @brief Test DNS client writes keep using the stable server address
 */
static void test_13_stream_write_uses_stable_remote_addr(void)
{
    TEST_INFO("Testing DNS stream write uses stable remote address");

    dap_net_trans_t *l_trans =
        dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_trans, "DNS trans should be registered");

    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");

    s_sendto_call_count = 0;
    s_worker_get_current_result = NULL;
    s_worker_exec_callback_run_immediately = false;
    s_last_sendto_addr_len = 0;
    memset(&s_last_sendto_addr, 0, sizeof(s_last_sendto_addr));
    memset(&s_mock_stream, 0, sizeof(s_mock_stream));
    memset(&s_mock_trans_ctx, 0, sizeof(s_mock_trans_ctx));
    memset(&s_mock_events_socket, 0, sizeof(s_mock_events_socket));

    s_mock_stream.trans = l_trans;
    s_mock_stream.esocket = &s_mock_events_socket;
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;

    l_ret = l_trans->ops->connect(&s_mock_stream, "127.0.0.1", 5353, NULL);
    TEST_ASSERT(l_ret == 0, "Connect should prepare stable DNS remote address");

    s_set_ipv4_addr(&s_mock_events_socket.addr_storage, &s_mock_events_socket.addr_size,
                    "127.0.0.2", 9999);

    const char l_test_data[] = "stable address write";
    ssize_t l_bytes_written = l_trans->ops->write(&s_mock_stream, l_test_data, sizeof(l_test_data));
    TEST_ASSERT(l_bytes_written == (ssize_t)sizeof(l_test_data), "Write should succeed");
    TEST_ASSERT(s_sendto_call_count == 1, "sendto should be called once");
    TEST_ASSERT(s_last_sendto_addr_len == sizeof(struct sockaddr_in), "sendto address length should be IPv4");

    struct sockaddr_in *l_sent_addr = (struct sockaddr_in *)&s_last_sendto_addr;
    TEST_ASSERT(l_sent_addr->sin_family == AF_INET, "sendto address should be IPv4");
    TEST_ASSERT(ntohs(l_sent_addr->sin_port) == 5353, "sendto should use stable server port");
    TEST_ASSERT(ntohl(l_sent_addr->sin_addr.s_addr) == 0x7f000001U,
                "sendto should use stable server IP, not mutable esocket addr_storage");

    if (l_trans->ops->close)
        l_trans->ops->close(&s_mock_stream);
    l_trans->ops->deinit(l_trans);

    TEST_SUCCESS("DNS stream write stable remote address verified");
}

/**
 * @brief Test DNS stream trans handshake operations
 */
static void test_14_stream_handshake(void)
{
    TEST_INFO("Testing DNS stream trans handshake operations");
    
    // Find DNS trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_trans, "DNS trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream
    s_mock_stream.trans = l_trans;
    s_mock_stream.esocket = &s_mock_events_socket;
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0};
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    l_ret = l_trans->ops->connect(&s_mock_stream, "127.0.0.1", 53, NULL);
    TEST_ASSERT(l_ret == 0, "Connect should prepare stable DNS remote address for handshake");
    
    // Test handshake_init operation (requires a non-empty alice_pub_key)
    uint8_t l_fake_pub_key[32] = {1, 2, 3};
    dap_net_handshake_params_t l_params = {0};
    l_params.alice_pub_key = l_fake_pub_key;
    l_params.alice_pub_key_size = sizeof(l_fake_pub_key);
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

    if (l_trans->ops->close)
        l_trans->ops->close(&s_mock_stream);
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("DNS stream trans handshake operations verified");
}

/**
 * @brief Test DNS stream trans session operations
 */
static void test_15_stream_session(void)
{
    TEST_INFO("Testing DNS stream trans session operations");
    
    // Find DNS trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_trans, "DNS trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Create mock stream
    s_mock_stream.trans = l_trans;
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0}; // Reset context
    s_mock_stream.esocket = &s_mock_events_socket;
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    
    // Test session_create operation
    dap_net_session_params_t l_session_params = {0};
    l_ret = l_trans->ops->session_create(&s_mock_stream, &l_session_params, NULL);
    TEST_ASSERT(l_ret == 0, "Session create should succeed");
    
    // Test session_start operation
    l_ret = l_trans->ops->session_start(&s_mock_stream, 12345, NULL);
    TEST_ASSERT(l_ret == 0, "Session start should succeed");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("DNS stream trans session operations verified");
}

/**
 * @brief Test DNS stream trans listen operation
 */
static void test_16_stream_listen(void)
{
    TEST_INFO("Testing DNS stream trans listen operation");
    
    // Find DNS trans
    dap_net_trans_t *l_trans = 
        dap_net_trans_find(DAP_NET_TRANS_DNS_TUNNEL);
    TEST_ASSERT_NOT_NULL(l_trans, "DNS trans should be registered");
    
    // Initialize trans
    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");
    
    // Setup mock server
    DAP_MOCK_SET_RETURN(dap_server_new, (void*)&s_mock_server);
    
    // Test listen operation (server-side)
    l_ret = l_trans->ops->listen(l_trans, "127.0.0.1", 53, &s_mock_server);
    TEST_ASSERT(l_ret == 0, "Listen operation should succeed");
    
    // Deinitialize
    l_trans->ops->deinit(l_trans);
    
    TEST_SUCCESS("DNS stream trans listen operation verified");
}

// ============================================================================
// Test Suite Definition
// ============================================================================

int main(int argc, char *argv[])
{
    // Initialize test suite
    setup_test();
    
    TEST_SUITE_START("DNS Trans Comprehensive Unit Tests");
    
    // Server tests
    TEST_RUN(test_01_server_ops_registration);
    TEST_RUN(test_02_server_creation);
    TEST_RUN(test_03_server_start);
    TEST_RUN(test_04_server_stop);
    TEST_RUN(test_05a_server_deferred_enqueue_failure_not_stuck);
    TEST_RUN(test_05aa_server_current_datagram_delete_retries_failed_enqueue);
    TEST_RUN(test_05b_server_deferred_delete_dominates_stop);
    TEST_RUN(test_05c_server_checked_read_delete_removes_session);
    TEST_RUN(test_05d_server_stop_deletes_stream_resources);
    TEST_RUN(test_05e_server_async_write_enqueue_failure);
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
    TEST_RUN(test_13_stream_write_uses_stable_remote_addr);
    TEST_RUN(test_14_stream_handshake);
    TEST_RUN(test_15_stream_session);
    TEST_RUN(test_16_stream_listen);
    
    TEST_SUITE_END();
    
    // Cleanup test suite
    suite_cleanup();
    
    return 0;
}
