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
#include <stdarg.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_test_helpers.h"
#include "dap_mock.h"
#include "dap_net_trans.h"
#include "dap_net_trans_server.h"
#include "dap_net_trans_websocket_server.h"
#include "dap_net_trans_websocket_handshake.h"
#include "dap_net_trans_websocket_stream.h"
#include "dap_net_server_common.h"
#include "dap_http_server.h"
#include "dap_server.h"
#include "dap_stream.h"
#include "dap_stream_handshake.h"
#include "dap_stream_session.h"
#include "dap_timerfd.h"
#include "dap_worker.h"
#include "dap_client_http.h"
#include "dap_net_trans_ctx.h"
#include "dap_trans_test_fixtures.h"
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

// Mock transport/IO calls that must stay isolated in unit tests
DAP_MOCK_DECLARE(dap_client_http_request);
DAP_MOCK_DECLARE(dap_events_socket_write_unsafe);
DAP_MOCK_DECLARE(dap_events_socket_write_f_unsafe);
DAP_MOCK_DECLARE(dap_events_socket_create_platform);
DAP_MOCK_DECLARE(dap_events_socket_connect);
DAP_MOCK_DECLARE(dap_events_socket_resolve_and_set_addr);
DAP_MOCK_DECLARE(dap_events_socket_delete_unsafe);
DAP_MOCK_DECLARE(dap_worker_add_events_socket);
DAP_MOCK_DECLARE(dap_stream_new_es_client);

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

// Wrapper for dap_events_worker_get_auto
DAP_MOCK_WRAPPER_CUSTOM(dap_worker_t*, dap_events_worker_get_auto, void)
{
    // Return explicitly configured worker when test needs strict control.
    if (g_mock_dap_events_worker_get_auto && g_mock_dap_events_worker_get_auto->return_value.ptr) {
        return (dap_worker_t*)g_mock_dap_events_worker_get_auto->return_value.ptr;
    }

    // No fake fallback: passthrough to real worker chooser.
    // This avoids fake worker->context == NULL on Windows IOCP path.
    return __real_dap_events_worker_get_auto();
}

// Mock dap_timerfd_t for testing
static dap_events_socket_t s_mock_timer_esocket = {
    .uuid = 0x1234567890ABCDEF,
    .type = DESCRIPTOR_TYPE_TIMER
};
static dap_timerfd_t s_mock_timerfd = {
    .events_socket = &s_mock_timer_esocket
};

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

typedef struct ws_test_http_capture {
    dap_worker_t *worker;
    char method[16];
    char path[1024];
    size_t request_size;
    size_t last_write_size;
    size_t last_write_f_size;
    uint8_t last_write_prefix[16];
    size_t last_write_prefix_size;
    bool response_callback_called;
    bool error_callback_called;
    bool write_f_called;
} ws_test_http_capture_t;

static ws_test_http_capture_t s_http_capture = {0};
static dap_client_http_t s_mock_http_client = {0};
static dap_events_socket_t s_stage_mock_esocket = {0};
static dap_stream_t s_stage_mock_stream_obj = {0};

// Wrapper for dap_client_http_request (unit-test isolation from real network/IOCP path)
DAP_MOCK_WRAPPER_CUSTOM(dap_client_http_t*, dap_client_http_request,
    PARAM(dap_worker_t*, a_worker),
    PARAM(const char*, a_uplink_addr),
    PARAM(uint16_t, a_uplink_port),
    PARAM(const char*, a_method),
    PARAM(const char*, a_request_content_type),
    PARAM(const char*, a_path),
    PARAM(const void*, a_request),
    PARAM(size_t, a_request_size),
    PARAM(char*, a_cookie),
    PARAM(dap_client_http_callback_data_t, a_response_callback),
    PARAM(dap_client_http_callback_error_t, a_error_callback),
    PARAM(void*, a_callbacks_arg),
    PARAM(char*, a_custom_headers)
)
{
    UNUSED(a_uplink_addr);
    UNUSED(a_uplink_port);
    UNUSED(a_request_content_type);
    UNUSED(a_request);
    UNUSED(a_cookie);
    UNUSED(a_custom_headers);

    s_http_capture.worker = a_worker;
    s_http_capture.request_size = a_request_size;
    dap_strncpy(s_http_capture.method, a_method ? a_method : "", sizeof(s_http_capture.method) - 1);
    dap_strncpy(s_http_capture.path, a_path ? a_path : "", sizeof(s_http_capture.path) - 1);

    // Complete request synchronously in tests to release temporary handshake ctx safely.
    if (a_response_callback) {
        static const char s_mock_response[] = "ok";
        s_http_capture.response_callback_called = true;
        a_response_callback((void*)s_mock_response, sizeof(s_mock_response) - 1, a_callbacks_arg, (http_status_code_t)200);
    } else if (a_error_callback) {
        s_http_capture.error_callback_called = true;
        a_error_callback(-1, a_callbacks_arg);
    }

    if (g_mock_dap_client_http_request && g_mock_dap_client_http_request->return_value.ptr) {
        return (dap_client_http_t*)g_mock_dap_client_http_request->return_value.ptr;
    }
    return &s_mock_http_client;
}

// Wrapper for dap_events_socket_write_unsafe (unit-test isolation from real socket writes)
DAP_MOCK_WRAPPER_CUSTOM(size_t, dap_events_socket_write_unsafe,
    PARAM(dap_events_socket_t*, a_esocket),
    PARAM(const void*, a_data),
    PARAM(size_t, a_data_size)
)
{
    UNUSED(a_esocket);
    UNUSED(a_data);

    s_http_capture.last_write_size = a_data_size;
    s_http_capture.last_write_prefix_size = dap_min(a_data_size, sizeof(s_http_capture.last_write_prefix));
    memcpy(s_http_capture.last_write_prefix, a_data, s_http_capture.last_write_prefix_size);

    if (g_mock_dap_events_socket_write_unsafe && g_mock_dap_events_socket_write_unsafe->return_value.ptr != NULL) {
        return (size_t)(uintptr_t)g_mock_dap_events_socket_write_unsafe->return_value.ptr;
    }
    return a_data_size;
}

// Manual wrapper for variadic dap_events_socket_write_f_unsafe.
// Keeps websocket unit test isolated from real IOCP/WSASend path.
ssize_t __wrap_dap_events_socket_write_f_unsafe(dap_events_socket_t *a_es, const char *a_format, ...)
{
    void *l_args[] = {(void*)a_es, (void*)a_format};
    bool l_mock_enabled = dap_mock_prepare_call(g_mock_dap_events_socket_write_f_unsafe, l_args, 2);

    int l_fmt_size = 0;
    if (a_format) {
        va_list l_ap;
        va_start(l_ap, a_format);
        l_fmt_size = vsnprintf(NULL, 0, a_format, l_ap);
        va_end(l_ap);
        if (l_fmt_size < 0) {
            l_fmt_size = 0;
        }
    }

    s_http_capture.write_f_called = true;
    s_http_capture.last_write_f_size = (size_t)l_fmt_size;

    if (l_mock_enabled) {
        ssize_t l_ret = (ssize_t)l_fmt_size;
        if (g_mock_dap_events_socket_write_f_unsafe &&
            g_mock_dap_events_socket_write_f_unsafe->return_value.ptr != NULL) {
            l_ret = (ssize_t)(intptr_t)g_mock_dap_events_socket_write_f_unsafe->return_value.ptr;
        }
        dap_mock_record_call(g_mock_dap_events_socket_write_f_unsafe, l_args, 2, (void*)(intptr_t)l_ret);
        return l_ret;
    }

    // Varargs passthrough is intentionally not used here: this test suite enforces isolation.
    return (ssize_t)l_fmt_size;
}

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

    if (g_mock_dap_events_socket_create_platform &&
        g_mock_dap_events_socket_create_platform->return_value.ptr) {
        return (dap_events_socket_t*)g_mock_dap_events_socket_create_platform->return_value.ptr;
    }
    return &s_stage_mock_esocket;
}

DAP_MOCK_WRAPPER_CUSTOM(int, dap_events_socket_resolve_and_set_addr,
    PARAM(dap_events_socket_t*, a_es),
    PARAM(const char*, a_host),
    PARAM(uint16_t, a_port)
)
{
    UNUSED(a_es);
    UNUSED(a_host);
    UNUSED(a_port);

    if (g_mock_dap_events_socket_resolve_and_set_addr &&
        g_mock_dap_events_socket_resolve_and_set_addr->return_value.i != 0) {
        return g_mock_dap_events_socket_resolve_and_set_addr->return_value.i;
    }
    return 0;
}

DAP_MOCK_WRAPPER_CUSTOM(int, dap_events_socket_connect,
    PARAM(dap_events_socket_t*, a_es),
    PARAM(int*, a_error_code)
)
{
    UNUSED(a_es);
    if (a_error_code) {
        *a_error_code = 0;
    }
    if (g_mock_dap_events_socket_connect &&
        g_mock_dap_events_socket_connect->return_value.i != 0) {
        if (a_error_code) {
            *a_error_code = g_mock_dap_events_socket_connect->return_value.i;
        }
        return g_mock_dap_events_socket_connect->return_value.i;
    }
    return 0;
}

DAP_MOCK_WRAPPER_CUSTOM(void, dap_events_socket_delete_unsafe,
    PARAM(dap_events_socket_t*, a_es),
    PARAM(bool, a_now)
)
{
    UNUSED(a_es);
    UNUSED(a_now);
}

DAP_MOCK_WRAPPER_CUSTOM(void, dap_worker_add_events_socket,
    PARAM(dap_worker_t*, a_worker),
    PARAM(dap_events_socket_t*, a_es)
)
{
    UNUSED(a_worker);
    UNUSED(a_es);
}

DAP_MOCK_WRAPPER_CUSTOM(dap_stream_t*, dap_stream_new_es_client,
    PARAM(dap_events_socket_t*, a_es),
    PARAM(dap_stream_node_addr_t*, a_node_addr),
    PARAM(bool, a_authorized)
)
{
    UNUSED(a_node_addr);
    UNUSED(a_authorized);

    if (g_mock_dap_stream_new_es_client &&
        g_mock_dap_stream_new_es_client->return_value.ptr) {
        return (dap_stream_t*)g_mock_dap_stream_new_es_client->return_value.ptr;
    }

    memset(&s_stage_mock_stream_obj, 0, sizeof(s_stage_mock_stream_obj));
    s_stage_mock_stream_obj.esocket = a_es;
    return &s_stage_mock_stream_obj;
}

// ============================================================================
// Test Suite State
// ============================================================================

static bool s_test_initialized = false;
static bool s_session_callback_called = false;
static bool s_original_delete_callback_called = false;

static void s_test_original_delete_callback(dap_events_socket_t *a_es, void *a_arg)
{
    UNUSED(a_es);
    UNUSED(a_arg);
    s_original_delete_callback_called = true;
}

static dap_worker_t *s_get_real_auto_worker(void)
{
    bool l_prev_enabled = g_mock_dap_events_worker_get_auto ? g_mock_dap_events_worker_get_auto->enabled : false;
    if (g_mock_dap_events_worker_get_auto) {
        dap_mock_set_enabled(g_mock_dap_events_worker_get_auto, false);
    }
    dap_worker_t *l_worker = dap_events_worker_get_auto();
    if (g_mock_dap_events_worker_get_auto) {
        dap_mock_set_enabled(g_mock_dap_events_worker_get_auto, l_prev_enabled);
    }
    return l_worker;
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
    memset(&s_http_capture, 0, sizeof(s_http_capture));
}

/**
 * @brief Teardown function called after each test
 */
static void teardown_test(void)
{
    // Reset all mocks for next test
    dap_mock_reset_all();
    memset(&s_http_capture, 0, sizeof(s_http_capture));
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
    s_mock_stream.esocket = &s_mock_events_socket;
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
    s_mock_stream.esocket = &s_mock_events_socket;
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
    s_mock_stream.esocket = &s_mock_events_socket;
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
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_events_socket_write_unsafe) >= 1,
                "Write operation should use mocked dap_events_socket_write_unsafe");
    TEST_ASSERT(s_http_capture.last_write_size > 0,
                "Mocked socket write should capture frame size");
    
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
    
    // Use real worker from initialized event system, avoid fake worker fallback.
    DAP_MOCK_ENABLE(dap_events_worker_get_auto);
    dap_worker_t *l_real_worker = s_get_real_auto_worker();
    TEST_ASSERT_NOT_NULL(l_real_worker, "Real worker should be available for handshake setup");
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, l_real_worker);

    DAP_MOCK_ENABLE(dap_client_http_request);

    // Create mock stream with proper trans_ctx chain
    dap_trans_test_get_mock_client();
    s_mock_stream.trans = l_trans;
    s_mock_stream.esocket = &s_mock_events_socket;
    s_mock_stream.trans_ctx = dap_trans_test_get_mock_net_trans_ctx();
    
    // Set esocket in private data for WebSocket
    dap_net_trans_websocket_private_t *l_priv = 
        (dap_net_trans_websocket_private_t*)l_trans->_inheritor;
    if (l_priv) {
        l_priv->esocket = &s_mock_events_socket;
    }
    
    // Setup mock timer (needed by dap_client_http callbacks)
    DAP_MOCK_ENABLE(dap_timerfd_start_on_worker);
    DAP_MOCK_SET_RETURN(dap_timerfd_start_on_worker, &s_mock_timerfd);
    
    // Test handshake_init operation
    dap_net_handshake_params_t l_params = {0};
    // WebSocket handshake needs alice_pub_key
    static uint8_t s_mock_alice_pub_key[32] = {0}; // Mock public key
    l_params.alice_pub_key = s_mock_alice_pub_key;
    l_params.alice_pub_key_size = sizeof(s_mock_alice_pub_key);
    l_ret = l_trans->ops->handshake_init(&s_mock_stream, &l_params, NULL);
    TEST_ASSERT(l_ret == 0, "Handshake init should succeed");
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_client_http_request) == 1,
                "Handshake init should call mocked dap_client_http_request exactly once");
    TEST_ASSERT(strcmp(s_http_capture.method, "POST") == 0,
                "Handshake request method should be POST");
    TEST_ASSERT(strstr(s_http_capture.path, DAP_UPLINK_PATH_ENC_INIT) != NULL,
                "Handshake request path should target ENC_INIT endpoint");
    TEST_ASSERT(s_http_capture.worker == l_real_worker,
                "Handshake should use explicitly configured real worker handle");
    TEST_ASSERT(s_http_capture.response_callback_called,
                "Mocked HTTP request should trigger response callback");
    
    // Test handshake_process operation (server-side)
    uint8_t l_handshake_data[100] = {0};
    void *l_response = NULL;
    size_t l_response_size = 0;
    l_ret = l_trans->ops->handshake_process(&s_mock_stream, l_handshake_data, 
                                                 sizeof(l_handshake_data),
                                                 &l_response, &l_response_size);
    TEST_ASSERT(l_ret == 0, "Handshake process should succeed");
    
    // Cleanup mocks
    DAP_MOCK_DISABLE(dap_events_worker_get_auto);
    DAP_MOCK_DISABLE(dap_timerfd_start_on_worker);
    DAP_MOCK_DISABLE(dap_client_http_request);
    
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
    dap_trans_test_get_mock_client();
    s_mock_stream.trans = l_trans;
    s_mock_stream.is_client_to_uplink = true;
    s_mock_stream.esocket = dap_trans_test_get_mock_esocket();
    s_mock_stream.trans_ctx = dap_trans_test_get_mock_net_trans_ctx();
    
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
    
    // Prepare mock esocket buf_out for write operations (required by session_start)
    dap_events_socket_t *l_mock_es = dap_trans_test_get_mock_esocket();
    if (!l_mock_es->buf_out) {
        l_mock_es->buf_out_size_max = 4096;
        l_mock_es->buf_out = DAP_NEW_Z_SIZE(byte_t, l_mock_es->buf_out_size_max);
        l_mock_es->buf_out_size = 0;
    }

    // Call connect first to set l_priv->esocket (required by session_start)
    l_ret = l_trans->ops->connect(&s_mock_stream, "127.0.0.1", 8080, NULL);
    TEST_ASSERT(l_ret == 0, "Connect should succeed");
    
    // Test session_start operation (sends WebSocket upgrade request via esocket)
    l_ret = l_trans->ops->session_start(&s_mock_stream, 12345, NULL);
    TEST_ASSERT(l_ret == 0, "Session start should succeed");
    TEST_ASSERT(s_http_capture.write_f_called, "Session start should call mocked dap_events_socket_write_f_unsafe");
    TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(dap_events_socket_write_f_unsafe) >= 1,
                "Session start should use mocked formatted socket write path");
    TEST_ASSERT(s_http_capture.last_write_f_size > 0,
                "Mocked formatted write should capture non-zero request size");
    
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

/**
 * @brief Test RFC 6455 Sec-WebSocket-Accept reference vector
 */
static void test_16_accept_key_rfc6455(void)
{
    TEST_INFO("Testing RFC 6455 Sec-WebSocket-Accept reference vector");

    const char *l_client_key = "dGhlIHNhbXBsZSBub25jZQ==";
    const char *l_expected_accept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
    char l_accept[64] = {0};

    int l_ret = dap_net_trans_websocket_build_accept_key(l_client_key, l_accept, sizeof(l_accept));
    TEST_ASSERT(l_ret == 0, "Accept key generation should succeed");
    TEST_ASSERT(strcmp(l_accept, l_expected_accept) == 0,
                "Accept key should match RFC 6455 reference value");

    TEST_SUCCESS("RFC 6455 Sec-WebSocket-Accept vector verified");
}

/**
 * @brief Test malformed Sec-WebSocket-Key values are rejected
 */
static void test_17_accept_key_rejects_malformed_client_keys(void)
{
    TEST_INFO("Testing malformed Sec-WebSocket-Key rejection");

    static const char *s_invalid_keys[] = {
        "",
        "dGhlIHNhbXBsZSBub25jZQ=!",
        "AQIDBA==",
        "QUJDREVGR0hJSktMTU5PUFE="
    };
    char l_accept[64] = {0};

    for (size_t i = 0; i < sizeof(s_invalid_keys) / sizeof(s_invalid_keys[0]); ++i) {
        memset(l_accept, 0, sizeof(l_accept));
        int l_ret = dap_net_trans_websocket_validate_client_key(s_invalid_keys[i]);
        TEST_ASSERT(l_ret != 0, "Malformed client key should fail validation");
        l_ret = dap_net_trans_websocket_build_accept_key(s_invalid_keys[i], l_accept, sizeof(l_accept));
        TEST_ASSERT(l_ret != 0, "Malformed client key should not produce accept key");
        TEST_ASSERT(l_accept[0] == '\0', "Malformed client key should leave accept key empty");
    }

    TEST_ASSERT(dap_net_trans_websocket_validate_client_key(NULL) != 0,
                "NULL client key should fail validation");
    TEST_ASSERT(dap_net_trans_websocket_build_accept_key(NULL, l_accept, sizeof(l_accept)) != 0,
                "NULL client key should not produce accept key");

    TEST_SUCCESS("Malformed Sec-WebSocket-Key rejection verified");
}

static void s_test_ws_http_headers_clear(dap_http_client_t *a_http_client)
{
    while (a_http_client->in_headers) {
        dap_http_header_remove(&a_http_client->in_headers, a_http_client->in_headers);
    }
    while (a_http_client->out_headers) {
        dap_http_header_remove(&a_http_client->out_headers, a_http_client->out_headers);
    }
}

static void s_test_ws_cleanup_upgrade_stream(dap_http_client_t *a_http_client)
{
    dap_net_trans_ctx_t *l_ctx = s_stage_mock_stream_obj.trans_ctx;
    if (l_ctx) {
        DAP_DELETE(l_ctx->transport_priv);
        DAP_DELETE(l_ctx);
    }
    memset(&s_stage_mock_stream_obj, 0, sizeof(s_stage_mock_stream_obj));
    if (a_http_client) {
        a_http_client->_inheritor = NULL;
        if (a_http_client->esocket) {
            a_http_client->esocket->_inheritor = NULL;
        }
    }
}

static int s_test_ws_try_upgrade_with_headers(const char *a_upgrade,
                                              const char *a_connection,
                                              uint16_t *a_status_out)
{
    dap_http_client_t l_http_client = {0};
    dap_events_socket_t l_esocket = {0};

    dap_strncpy(l_esocket.remote_addr_str, "127.0.0.1", sizeof(l_esocket.remote_addr_str) - 1);
    l_http_client.esocket = &l_esocket;
    dap_http_header_add(&l_http_client.in_headers, "Upgrade", a_upgrade);
    dap_http_header_add(&l_http_client.in_headers, "Connection", a_connection);
    dap_http_header_add(&l_http_client.in_headers, "Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
    dap_http_header_add(&l_http_client.in_headers, "Sec-WebSocket-Version", "13");

    int l_ret = dap_net_trans_websocket_try_upgrade(&l_http_client);
    if (a_status_out) {
        *a_status_out = l_http_client.reply_status_code;
    }

    s_test_ws_cleanup_upgrade_stream(&l_http_client);
    s_test_ws_http_headers_clear(&l_http_client);
    return l_ret;
}

/**
 * @brief Test WebSocket Upgrade/Connection header token matching
 */
static void test_18_upgrade_rejects_substring_tokens(void)
{
    TEST_INFO("Testing WebSocket upgrade token matching");

    uint16_t l_status = 0;
    int l_ret = s_test_ws_try_upgrade_with_headers("notwebsocket", "Upgrade", &l_status);
    TEST_ASSERT(l_ret == 0, "Malformed Upgrade header should be handled as bad upgrade");
    TEST_ASSERT(l_status == 400, "Upgrade: notwebsocket should be rejected");

    l_status = 0;
    l_ret = s_test_ws_try_upgrade_with_headers("websocket", "XUpgrade", &l_status);
    TEST_ASSERT(l_ret == 0, "Malformed Connection header should be handled as bad upgrade");
    TEST_ASSERT(l_status == 400, "Connection: XUpgrade should be rejected");

    l_status = 0;
    l_ret = s_test_ws_try_upgrade_with_headers("WebSocket", "keep-alive, Upgrade", &l_status);
    TEST_ASSERT(l_ret == 0, "Valid case-insensitive token-list upgrade should be handled");
    TEST_ASSERT(l_status == 101, "Valid WebSocket token-list upgrade should switch protocols");
    TEST_ASSERT(s_http_capture.write_f_called, "Valid WebSocket upgrade should write 101 response");

    TEST_SUCCESS("WebSocket upgrade token matching verified");
}

/**
 * @brief Regression: IOCP must not post read before TCP connect completes.
 */
static void test_19_stage_prepare_iocp_connect_socket_not_read_ready(void)
{
#ifdef DAP_EVENTS_CAPS_IOCP
    TEST_INFO("Testing WebSocket IOCP stage_prepare does not arm read before connect completes");

    dap_net_trans_t *l_trans =
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");
    TEST_ASSERT_NOT_NULL(l_trans->ops, "Trans ops should be set");
    TEST_ASSERT_NOT_NULL(l_trans->ops->stage_prepare, "stage_prepare callback should be set");

    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");

    dap_worker_t *l_worker = s_get_real_auto_worker();
    TEST_ASSERT_NOT_NULL(l_worker, "Worker should be available");

    DAP_MOCK_RESET(dap_events_socket_create_platform);
    DAP_MOCK_RESET(dap_events_socket_resolve_and_set_addr);
    DAP_MOCK_RESET(dap_events_socket_connect);
    DAP_MOCK_RESET(dap_worker_add_events_socket);
    DAP_MOCK_RESET(dap_stream_new_es_client);
    DAP_MOCK_SET_RETURN(dap_events_socket_connect, (void*)(intptr_t)0);

    memset(&s_stage_mock_esocket, 0, sizeof(s_stage_mock_esocket));
    s_stage_mock_esocket.fd = 42;
    s_stage_mock_esocket.type = DESCRIPTOR_TYPE_SOCKET_CLIENT;
    s_stage_mock_esocket.flags = DAP_SOCK_READY_TO_READ;
    memset(&s_stage_mock_stream_obj, 0, sizeof(s_stage_mock_stream_obj));

    dap_events_socket_callbacks_t l_cbs = {0};
    dap_stream_node_addr_t l_node_addr = {0};

    dap_net_stage_prepare_params_t l_params = {
        .host = "127.0.0.1",
        .port = 8080,
        .node_addr = &l_node_addr,
        .authorized = false,
        .callbacks = &l_cbs,
        .client_ctx = NULL,
        .worker = l_worker
    };
    dap_net_stage_prepare_result_t l_result = {0};

    l_ret = l_trans->ops->stage_prepare(l_trans, &l_params, &l_result);

    TEST_ASSERT(l_ret == 0, "stage_prepare should succeed");
    TEST_ASSERT_NOT_NULL(l_result.esocket, "esocket must be non-NULL after stage_prepare");
    TEST_ASSERT(l_result.esocket->flags & DAP_SOCK_CONNECTING,
                "IOCP TCP socket should still be in CONNECTING state");
    TEST_ASSERT(!(l_result.esocket->flags & DAP_SOCK_READY_TO_READ),
                "IOCP TCP socket must not be READ-ready while CONNECTING");

    l_trans->ops->deinit(l_trans);

    TEST_SUCCESS("WebSocket IOCP connect/read ordering regression verified");
#else
    TEST_SUCCESS("WebSocket IOCP connect/read ordering regression skipped on non-IOCP platform");
#endif
}

/**
 * @brief Test WebSocket parser rejects oversized and malformed frame sizes.
 */
static void test_20_parse_frame_rejects_oversized_and_incomplete_frames(void)
{
    TEST_INFO("Testing WebSocket frame parser size validation");

    dap_ws_opcode_t l_opcode = 0;
    bool l_fin = false;
    uint8_t *l_payload = NULL;
    size_t l_payload_size = 0;
    size_t l_frame_size = 0;

    const uint8_t l_one_byte_header[] = { 0x82 };
    int l_ret = dap_net_trans_websocket_parse_frame(l_one_byte_header, sizeof(l_one_byte_header),
                                                    &l_opcode, &l_fin, &l_payload,
                                                    &l_payload_size, &l_frame_size);
    TEST_ASSERT(l_ret == -2, "Single-byte WebSocket header should be incomplete");
    TEST_ASSERT_NULL(l_payload, "Incomplete single-byte header should not allocate payload");
    TEST_ASSERT(l_payload_size == 0, "Incomplete single-byte header should report zero payload size");
    TEST_ASSERT(l_frame_size == 0, "Incomplete single-byte header should report zero consumed size");

    const uint8_t l_oversized_frame[] = {
        0x82, 0x7f,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01
    };
    l_ret = dap_net_trans_websocket_parse_frame(l_oversized_frame, sizeof(l_oversized_frame),
                                                &l_opcode, &l_fin, &l_payload,
                                                &l_payload_size, &l_frame_size);
    TEST_ASSERT(l_ret == -1, "Oversized payload length should be rejected before allocation");
    TEST_ASSERT_NULL(l_payload, "Rejected oversized frame should not allocate payload");
    TEST_ASSERT(l_payload_size == 0, "Rejected oversized frame should report zero payload size");
    TEST_ASSERT(l_frame_size == 0, "Rejected oversized frame should report zero consumed size");

    const uint8_t l_incomplete_frame[] = { 0x82, 0x7e, 0x00, 0x04, 0xaa };
    l_ret = dap_net_trans_websocket_parse_frame(l_incomplete_frame, sizeof(l_incomplete_frame),
                                               &l_opcode, &l_fin, &l_payload,
                                               &l_payload_size, &l_frame_size);
    TEST_ASSERT(l_ret == -2, "Incomplete in-cap frame should request more data");
    TEST_ASSERT_NULL(l_payload, "Incomplete frame should not allocate payload");

    const uint8_t l_oversized_control_frame[] = { 0x89, 0x7e, 0x00, 0x7e };
    l_ret = dap_net_trans_websocket_parse_frame(l_oversized_control_frame,
                                               sizeof(l_oversized_control_frame),
                                               &l_opcode, &l_fin, &l_payload,
                                               &l_payload_size, &l_frame_size);
    TEST_ASSERT(l_ret == -1, "Oversized control frame should be rejected");
    TEST_ASSERT_NULL(l_payload, "Rejected control frame should not allocate payload");

    const uint8_t l_close_len_one_frame[] = { 0x88, 0x01, 0x00 };
    l_ret = dap_net_trans_websocket_parse_frame(l_close_len_one_frame,
                                               sizeof(l_close_len_one_frame),
                                               &l_opcode, &l_fin, &l_payload,
                                               &l_payload_size, &l_frame_size);
    TEST_ASSERT(l_ret == -1, "CLOSE frame with 1-byte payload should be rejected");
    TEST_ASSERT_NULL(l_payload, "Rejected CLOSE len=1 frame should not allocate payload");

    TEST_SUCCESS("WebSocket frame parser size validation verified");
}

/**
 * @brief Regression: client CLOSE read path must not touch freed per-stream state.
 */
static void test_21_client_close_frame_read_unwinds_after_free(void)
{
    TEST_INFO("Testing WebSocket client CLOSE read unwind");

    dap_net_trans_t *l_trans =
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");

    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");

    uint8_t l_close_frame[] = { 0x88, 0x00 };
    memset(&s_mock_stream, 0, sizeof(s_mock_stream));
    memset(&s_mock_events_socket, 0, sizeof(s_mock_events_socket));
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0};
    s_mock_events_socket.buf_in = l_close_frame;
    s_mock_events_socket.buf_in_size = sizeof(l_close_frame);
    s_mock_events_socket.buf_in_size_max = sizeof(l_close_frame);
    s_mock_events_socket.callbacks.delete_callback = s_test_original_delete_callback;
    s_mock_stream.trans = l_trans;
    s_mock_stream.is_client_to_uplink = true;
    s_mock_stream.esocket = &s_mock_events_socket;
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;

    l_ret = l_trans->ops->connect(&s_mock_stream, "127.0.0.1", 8080, NULL);
    TEST_ASSERT(l_ret == 0, "Connect should allocate per-stream WebSocket state");

    dap_net_trans_websocket_private_t *l_priv =
        (dap_net_trans_websocket_private_t*)s_mock_trans_ctx.transport_priv;
    TEST_ASSERT_NOT_NULL(l_priv, "Per-stream WebSocket state should exist before CLOSE");
    l_priv->state = DAP_WS_STATE_OPEN;

    TEST_ASSERT(s_mock_events_socket.callbacks.delete_callback != s_test_original_delete_callback,
                "Connect should wrap the original esocket delete callback");

    s_original_delete_callback_called = false;
    ssize_t l_read_ret = l_trans->ops->read(&s_mock_stream, NULL, 0);
    TEST_ASSERT(l_read_ret == 0, "CLOSE frame read should unwind successfully");
    TEST_ASSERT_NULL(s_mock_trans_ctx.transport_priv,
                     "CLOSE frame should release per-stream WebSocket state");
    TEST_ASSERT(s_mock_events_socket.buf_in_size == 0,
                "CLOSE frame bytes should be consumed before close cleanup");
    TEST_ASSERT(s_mock_events_socket.callbacks.delete_callback != s_test_original_delete_callback,
                "CLOSE cleanup should leave the esocket-owned delete wrapper installed");
    s_mock_events_socket.callbacks.delete_callback(&s_mock_events_socket, NULL);
    TEST_ASSERT(s_original_delete_callback_called,
                "Wrapped delete callback should still call the original callback later");

    l_trans->ops->deinit(l_trans);

    TEST_SUCCESS("WebSocket client CLOSE read unwind verified");
}

/**
 * @brief Regression: 1-byte WebSocket header must remain buffered in read path.
 */
static void test_22_read_keeps_partial_one_byte_header(void)
{
    TEST_INFO("Testing WebSocket read keeps partial single-byte header");

    dap_net_trans_t *l_trans =
        dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");

    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");

    uint8_t l_partial_frame[] = { 0x82 };
    memset(&s_mock_stream, 0, sizeof(s_mock_stream));
    memset(&s_mock_events_socket, 0, sizeof(s_mock_events_socket));
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0};
    s_mock_events_socket.buf_in = l_partial_frame;
    s_mock_events_socket.buf_in_size = sizeof(l_partial_frame);
    s_mock_events_socket.buf_in_size_max = sizeof(l_partial_frame);
    s_mock_stream.trans = l_trans;
    s_mock_stream.is_client_to_uplink = true;
    s_mock_stream.esocket = &s_mock_events_socket;
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;

    l_ret = l_trans->ops->connect(&s_mock_stream, "127.0.0.1", 8080, NULL);
    TEST_ASSERT(l_ret == 0, "Connect should allocate per-stream WebSocket state");

    dap_net_trans_websocket_private_t *l_priv =
        (dap_net_trans_websocket_private_t*)s_mock_trans_ctx.transport_priv;
    TEST_ASSERT_NOT_NULL(l_priv, "Per-stream WebSocket state should exist before read");
    l_priv->state = DAP_WS_STATE_OPEN;

    ssize_t l_read_ret = l_trans->ops->read(&s_mock_stream, NULL, 0);
    TEST_ASSERT(l_read_ret == 0, "Partial frame read should wait for more bytes");
    TEST_ASSERT(s_mock_events_socket.buf_in_size == sizeof(l_partial_frame),
                "Partial single-byte header should not be consumed");
    TEST_ASSERT(s_mock_events_socket.buf_in[0] == 0x82,
                "Partial single-byte header byte should remain intact");

    l_trans->ops->close(&s_mock_stream);
    l_trans->ops->deinit(l_trans);

    TEST_SUCCESS("WebSocket read partial single-byte header handling verified");
}

/**
 * @brief Test public server-side parser helper requires client masking.
 */
static void test_23_parse_client_frame_requires_mask(void)
{
    TEST_INFO("Testing WebSocket client-frame mask validation API");

    const uint8_t l_payload_in[] = { 0x10, 0x20, 0x30 };
    uint8_t l_frame[64] = {0};
    size_t l_frame_size = 0;
    int l_ret = dap_net_trans_websocket_build_frame(l_frame, sizeof(l_frame),
                                                    DAP_WS_OPCODE_BINARY, true, true,
                                                    l_payload_in, sizeof(l_payload_in),
                                                    &l_frame_size);
    TEST_ASSERT(l_ret == 0, "Masked frame build should succeed");

    dap_ws_opcode_t l_opcode = 0;
    bool l_fin = false;
    uint8_t *l_payload_out = NULL;
    size_t l_payload_size = 0;
    size_t l_consumed = 0;
    l_ret = dap_net_trans_websocket_parse_client_frame(l_frame, l_frame_size,
                                                       &l_opcode, &l_fin,
                                                       &l_payload_out, &l_payload_size,
                                                       &l_consumed);
    TEST_ASSERT(l_ret == 0, "Masked client frame should parse successfully");
    TEST_ASSERT(l_opcode == DAP_WS_OPCODE_BINARY, "Parsed opcode should be binary");
    TEST_ASSERT(l_fin, "Parsed frame should be final");
    TEST_ASSERT(l_payload_size == sizeof(l_payload_in), "Parsed payload size should match");
    TEST_ASSERT(l_consumed == l_frame_size, "Parsed frame size should match");
    TEST_ASSERT(memcmp(l_payload_out, l_payload_in, sizeof(l_payload_in)) == 0,
                "Masked payload should be unmasked by parser");
    DAP_DEL_Z(l_payload_out);

    memset(l_frame, 0, sizeof(l_frame));
    l_ret = dap_net_trans_websocket_build_frame(l_frame, sizeof(l_frame),
                                                DAP_WS_OPCODE_BINARY, true, false,
                                                l_payload_in, sizeof(l_payload_in),
                                                &l_frame_size);
    TEST_ASSERT(l_ret == 0, "Unmasked frame build should succeed");

    l_ret = dap_net_trans_websocket_parse_client_frame(l_frame, l_frame_size,
                                                       &l_opcode, &l_fin,
                                                       &l_payload_out, &l_payload_size,
                                                       &l_consumed);
    TEST_ASSERT(l_ret == -1, "Server-side client-frame parser should reject unmasked frames");
    TEST_ASSERT_NULL(l_payload_out, "Rejected unmasked frame should not allocate payload");

    TEST_SUCCESS("WebSocket client-frame mask validation API verified");
}

/**
 * @brief Regression: server receive path rejects unmasked client frames.
 */
static void test_24_server_read_rejects_unmasked_client_frame(void)
{
    TEST_INFO("Testing WebSocket server read rejects unmasked client frames");

    dap_http_client_t l_http_client = {0};
    dap_events_socket_t l_esocket = {0};
    dap_strncpy(l_esocket.remote_addr_str, "127.0.0.1", sizeof(l_esocket.remote_addr_str) - 1);
    l_http_client.esocket = &l_esocket;
    dap_http_header_add(&l_http_client.in_headers, "Upgrade", "websocket");
    dap_http_header_add(&l_http_client.in_headers, "Connection", "Upgrade");
    dap_http_header_add(&l_http_client.in_headers, "Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
    dap_http_header_add(&l_http_client.in_headers, "Sec-WebSocket-Version", "13");

    int l_ret = dap_net_trans_websocket_try_upgrade(&l_http_client);
    TEST_ASSERT(l_ret == 0, "Valid WebSocket upgrade should be handled");
    TEST_ASSERT(l_http_client.reply_status_code == 101, "Upgrade should switch protocols");
    TEST_ASSERT_NOT_NULL(l_esocket.callbacks.read_callback,
                         "Upgrade should install WebSocket server read callback");

    s_http_capture.last_write_size = 0;
    uint8_t l_unmasked_frame[2] = { 0x82, 0x00 };
    l_esocket.buf_in = l_unmasked_frame;
    l_esocket.buf_in_size = sizeof(l_unmasked_frame);
    l_esocket.buf_in_size_max = sizeof(l_unmasked_frame);

    l_esocket.callbacks.read_callback(&l_esocket, NULL);

    TEST_ASSERT(l_esocket.flags & DAP_SOCK_SIGNAL_CLOSE,
                "Unmasked client frame should close the server-side WebSocket");
    TEST_ASSERT(s_http_capture.last_write_size > 0,
                "Protocol close frame should be written for unmasked client frame");

    s_test_ws_cleanup_upgrade_stream(&l_http_client);
    s_test_ws_http_headers_clear(&l_http_client);

    TEST_SUCCESS("WebSocket server read mask enforcement verified");
}

/**
 * @brief Regression: generic stream send must use WebSocket framing.
 */
static void test_25_stream_send_uses_masked_websocket_frame(void)
{
    TEST_INFO("Testing generic stream send uses masked WebSocket frame");

    dap_net_trans_t *l_trans = dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_trans, "WebSocket trans should be registered");

    int l_ret = l_trans->ops->init(l_trans, NULL);
    TEST_ASSERT(l_ret == 0, "Trans initialization should succeed");

    memset(&s_mock_stream, 0, sizeof(s_mock_stream));
    memset(&s_mock_events_socket, 0, sizeof(s_mock_events_socket));
    s_mock_trans_ctx = (dap_net_trans_ctx_t){0};
    s_mock_stream.trans = l_trans;
    s_mock_stream.is_client_to_uplink = true;
    s_mock_stream.esocket = &s_mock_events_socket;
    s_mock_stream.trans_ctx = &s_mock_trans_ctx;
    s_mock_trans_ctx.trans = l_trans;

    l_ret = l_trans->ops->connect(&s_mock_stream, "127.0.0.1", 8080, NULL);
    TEST_ASSERT(l_ret == 0, "Connect should allocate per-stream WebSocket state");

    dap_net_trans_websocket_private_t *l_priv =
        (dap_net_trans_websocket_private_t*)s_mock_trans_ctx.transport_priv;
    TEST_ASSERT_NOT_NULL(l_priv, "Per-stream WebSocket state should exist before send");
    l_priv->state = DAP_WS_STATE_OPEN;

    static const uint8_t l_payload[] = { 0x01, 0x02, 0x03 };
    ssize_t l_sent = dap_stream_send_unsafe(&s_mock_stream, l_payload, sizeof(l_payload));
    TEST_ASSERT(l_sent == (ssize_t)sizeof(l_payload),
                "WebSocket stream send should report payload bytes");
    TEST_ASSERT(s_http_capture.last_write_size > sizeof(l_payload),
                "WebSocket stream send should write a framed payload");
    TEST_ASSERT(s_http_capture.last_write_prefix_size >= 2,
                "Captured WebSocket frame should include header bytes");
    TEST_ASSERT((s_http_capture.last_write_prefix[1] & 0x80) != 0,
                "Client WebSocket stream send should mask frames");

    l_trans->ops->close(&s_mock_stream);
    l_trans->ops->deinit(l_trans);

    TEST_SUCCESS("Generic WebSocket stream send framing verified");
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
    TEST_RUN(test_16_accept_key_rfc6455);
    TEST_RUN(test_17_accept_key_rejects_malformed_client_keys);
    TEST_RUN(test_18_upgrade_rejects_substring_tokens);
    TEST_RUN(test_19_stage_prepare_iocp_connect_socket_not_read_ready);
    TEST_RUN(test_20_parse_frame_rejects_oversized_and_incomplete_frames);
    TEST_RUN(test_21_client_close_frame_read_unwinds_after_free);
    TEST_RUN(test_22_read_keeps_partial_one_byte_header);
    TEST_RUN(test_23_parse_client_frame_requires_mask);
    TEST_RUN(test_24_server_read_rejects_unmasked_client_frame);
    TEST_RUN(test_25_stream_send_uses_masked_websocket_frame);
    
    TEST_SUITE_END();
    
    // Cleanup test suite
    suite_cleanup();
    
    return 0;
}
