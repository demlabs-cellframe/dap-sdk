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
 * @file test_dap_http_server.c
 * @brief Comprehensive unit tests for DAP HTTP Server module with full mocking
 * 
 * Tests HTTP server initialization, server creation, URL processor management.
 * All external dependencies are mocked for complete isolation.
 * 
 * @date 2025-10-30
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
#include "dap_config.h"
#include "dap_http_server.h"
#include "dap_server.h"
#include "dap_http_header.h"
#include "dap_http_client.h"

#define LOG_TAG "test_dap_http_server"

// ============================================================================
// Mock Declarations
// ============================================================================

// Mock dap_http_header_server functions
DAP_MOCK_DECLARE(dap_http_header_server_init);
DAP_MOCK_DECLARE(dap_http_header_server_deinit);

// Mock dap_http_client functions
DAP_MOCK_DECLARE(dap_http_client_init);
DAP_MOCK_DECLARE(dap_http_client_deinit);
DAP_MOCK_DECLARE(dap_http_client_new);
DAP_MOCK_DECLARE(dap_http_client_delete);
DAP_MOCK_DECLARE(dap_http_client_read);
DAP_MOCK_DECLARE(dap_http_client_write_callback);
DAP_MOCK_DECLARE(dap_http_client_error);

// Mock dap_server functions
DAP_MOCK_DECLARE(dap_server_new);
DAP_MOCK_DECLARE(dap_server_delete);
DAP_MOCK_DECLARE(dap_server_listen_addr_add);

// Mock dap_config functions
DAP_MOCK_DECLARE(dap_config_get_item_bool_default);

// ============================================================================
// Mock Wrappers for Functions Called from Static Libraries
// ============================================================================

// Mock dap_http_header_server functions
DAP_MOCK_WRAPPER_CUSTOM(int, dap_http_header_server_init, void)
{
    return (int)(intptr_t)g_mock_dap_http_header_server_init->return_value.ptr;
}
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_http_header_server_deinit, (), ());

// Mock dap_http_client functions
DAP_MOCK_WRAPPER_CUSTOM(int, dap_http_client_init, void)
{
    return (int)(intptr_t)g_mock_dap_http_client_init->return_value.ptr;
}
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_http_client_deinit, (), ());
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_http_client_new, (dap_events_socket_t *a_esocket, void *a_arg), (a_esocket, a_arg));
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_http_client_delete, (dap_events_socket_t *a_esocket, void *a_arg), (a_esocket, a_arg));
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_http_client_read, (dap_events_socket_t *a_esocket, void *a_arg), (a_esocket, a_arg));
DAP_MOCK_WRAPPER_PASSTHROUGH(bool, dap_http_client_write_callback, (dap_events_socket_t *a_esocket, void *a_arg), (a_esocket, a_arg));
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_http_client_error, (dap_events_socket_t *a_esocket, int a_arg), (a_esocket, a_arg));

// Mock dap_server functions
DAP_MOCK_WRAPPER_CUSTOM(dap_server_t*, dap_server_new,
    PARAM(const char*, a_cfg_section),
    PARAM(dap_events_socket_callbacks_t*, a_server_callbacks),
    PARAM(dap_events_socket_callbacks_t*, a_client_callbacks)
)
{
    if (!g_mock_dap_server_new || !g_mock_dap_server_new->enabled) {
        return __real_dap_server_new(a_cfg_section, a_server_callbacks, a_client_callbacks);
    }
    // Return mock value if set, otherwise return NULL
    return (dap_server_t*)g_mock_dap_server_new->return_value.ptr;
}
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_server_delete, (dap_server_t *a_server), (a_server));

// Mock dap_server_listen_addr_add
DAP_MOCK_WRAPPER_CUSTOM(int, dap_server_listen_addr_add,
    PARAM(dap_server_t*, a_server),
    PARAM(const char*, a_addr),
    PARAM(uint16_t, a_port),
    PARAM(dap_events_desc_type_t, a_type),
    PARAM(dap_events_socket_callbacks_t*, a_callbacks)
)
{
    printf("DEBUG: Mock dap_server_listen_addr_add called\n");
    // Mock success
    return 0;
}

// Mock dap_config functions
DAP_MOCK_WRAPPER_CUSTOM(bool, dap_config_get_item_bool_default,
    PARAM(dap_config_t*, a_config),
    PARAM(const char*, a_section),
    PARAM(const char*, a_item_name),
    PARAM(bool, a_default)
)
{
    UNUSED(a_config);
    UNUSED(a_section);
    
    // Always enable TCP listening for tests
    if (a_item_name && strcmp(a_item_name, "listen_address_tcp") == 0) {
        return true;
    }

    if (g_mock_dap_config_get_item_bool_default->return_value.ptr) {
        return (bool)(intptr_t)g_mock_dap_config_get_item_bool_default->return_value.ptr;
    }
    return a_default;
}

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
        int l_ret = dap_common_init("test_dap_http_server", NULL);
        TEST_ASSERT(l_ret == 0, "DAP common initialization failed");
        
        // Mock initialization functions to return success
        DAP_MOCK_SET_RETURN(dap_http_header_server_init, 0);
        DAP_MOCK_SET_RETURN(dap_http_client_init, 0);
        DAP_MOCK_SET_RETURN(dap_config_get_item_bool_default, false);
        DAP_MOCK_ENABLE(dap_server_listen_addr_add); // Enable listen mock
        
        // Initialize HTTP module
        l_ret = dap_http_init();
        TEST_ASSERT(l_ret == 0, "HTTP module initialization failed");
        
        s_test_initialized = true;
        TEST_INFO("HTTP server test suite initialized");
    }
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
        // Deinitialize HTTP module
        dap_http_deinit();
        
        // Deinitialize DAP common
        dap_common_deinit();
        
        s_test_initialized = false;
        TEST_INFO("HTTP server test suite cleaned up");
    }
}

// ============================================================================
// Test 1: HTTP Module Initialization
// ============================================================================

static void test_01_http_module_init_deinit(void)
{
    setup_test();
    
    TEST_INFO("Test 1: HTTP module initialization/deinitialization");
    
    // Verify init was called (from setup_test)
    uint32_t l_header_init_calls = DAP_MOCK_GET_CALL_COUNT(dap_http_header_server_init);
    uint32_t l_client_init_calls = DAP_MOCK_GET_CALL_COUNT(dap_http_client_init);
    
    TEST_ASSERT(l_header_init_calls > 0, "dap_http_header_server_init should have been called");
    TEST_ASSERT(l_client_init_calls > 0, "dap_http_client_init should have been called");
    
    // Test deinit
    dap_http_deinit();
    
    // Verify deinit was called
    uint32_t l_header_deinit_calls = DAP_MOCK_GET_CALL_COUNT(dap_http_header_server_deinit);
    uint32_t l_client_deinit_calls = DAP_MOCK_GET_CALL_COUNT(dap_http_client_deinit);
    
    TEST_ASSERT(l_header_deinit_calls > 0, "dap_http_header_server_deinit should have been called");
    TEST_ASSERT(l_client_deinit_calls > 0, "dap_http_client_deinit should have been called");
    
    // Re-init for next tests
    dap_http_init();
    
    TEST_SUCCESS("Test 1 passed: HTTP module init/deinit works correctly");
    teardown_test();
}

// ============================================================================
// Test 2: HTTP Server Creation
// ============================================================================

static void test_02_http_server_creation(void)
{
    setup_test();
    
    TEST_INFO("Test 2: HTTP server creation");
    
    // Mock server creation - create a valid dap_server_t structure
    static dap_server_t s_mock_server = {0};
    dap_server_t *l_mock_server = &s_mock_server;
    DAP_MOCK_SET_RETURN(dap_server_new, l_mock_server);
    DAP_MOCK_ENABLE(dap_server_new);
    
    // Create HTTP server
    dap_server_t *l_server = dap_http_server_new("test_server", "Test HTTP Server");
    TEST_ASSERT(l_server != NULL, "HTTP server creation should succeed");
    TEST_ASSERT(l_server == l_mock_server, "HTTP server should match mocked server");
    
    // Verify HTTP server structure
    dap_http_server_t *l_http_server = DAP_HTTP_SERVER(l_server);
    TEST_ASSERT(l_http_server != NULL, "HTTP server structure should not be NULL");
    TEST_ASSERT(l_http_server->server == l_server, "HTTP server should reference server");
    TEST_ASSERT(strcmp(l_http_server->server_name, "Test HTTP Server") == 0, 
                "Server name should match");
    TEST_ASSERT(l_http_server->url_proc == NULL, "URL processors should be NULL initially");
    
    // Verify server_new was called
    uint32_t l_server_new_calls = DAP_MOCK_GET_CALL_COUNT(dap_server_new);
    TEST_ASSERT(l_server_new_calls > 0, "dap_server_new should have been called");
    
    // Cleanup
    dap_http_delete(l_server, NULL);
    
    TEST_SUCCESS("Test 2 passed: HTTP server creation works correctly");
    teardown_test();
}

// ============================================================================
// Test 3: HTTP Server Deletion
// ============================================================================

static void test_03_http_server_deletion(void)
{
    setup_test();
    
    TEST_INFO("Test 3: HTTP server deletion");
    
    // Mock server creation - create a valid dap_server_t structure
    static dap_server_t s_mock_server = {0};
    dap_server_t *l_mock_server = &s_mock_server;
    DAP_MOCK_SET_RETURN(dap_server_new, l_mock_server);
    
    // Create HTTP server
    dap_server_t *l_server = dap_http_server_new("test_server", "Test Server");
    TEST_ASSERT(l_server != NULL, "HTTP server creation should succeed");
    
    // Delete HTTP server
    dap_http_delete(l_server, NULL);
    
    // HTTP server structure should be cleaned up
    // (We can't verify internal cleanup without accessing freed memory)
    
    TEST_SUCCESS("Test 3 passed: HTTP server deletion works correctly");
    teardown_test();
}

// ============================================================================
// Test 4: HTTP URL Processor Addition
// ============================================================================

static void test_04_http_url_processor_add(void)
{
    setup_test();
    
    TEST_INFO("Test 4: HTTP URL processor addition");
    
    // Mock server creation - create a valid dap_server_t structure
    static dap_server_t s_mock_server = {0};
    dap_server_t *l_mock_server = &s_mock_server;
    DAP_MOCK_SET_RETURN(dap_server_new, l_mock_server);
    
    // Create HTTP server
    dap_server_t *l_server = dap_http_server_new("test_server", "Test Server");
    TEST_ASSERT(l_server != NULL, "HTTP server creation should succeed");
    
    dap_http_server_t *l_http_server = DAP_HTTP_SERVER(l_server);
    TEST_ASSERT(l_http_server != NULL, "HTTP server structure should not be NULL");
    
    // Add URL processor
    // Use NULL as inheritor since dap_http_delete will try to free it
    void *l_inheritor = NULL;
    dap_http_url_proc_t *l_url_proc = dap_http_add_proc(
        l_http_server,
        "/api/test",
        l_inheritor,
        NULL,  // new_callback
        NULL,  // delete_callback
        NULL,  // headers_read_callback
        NULL,  // headers_write_callback
        NULL,  // data_read_callback
        NULL,  // data_write_callback
        NULL   // error_callback
    );
    
    TEST_ASSERT(l_url_proc != NULL, "URL processor should be created");
    TEST_ASSERT(strcmp(l_url_proc->url, "/api/test") == 0, 
                "URL processor path should match");
    TEST_ASSERT(l_url_proc->http == l_http_server, 
                "URL processor should reference HTTP server");
    TEST_ASSERT(l_url_proc->_inheritor == l_inheritor, 
                "URL processor inheritor should match");
    
    // Cleanup
    dap_http_delete(l_server, NULL);
    
    TEST_SUCCESS("Test 4 passed: HTTP URL processor addition works correctly");
    teardown_test();
}

// ============================================================================
// Test 5: Multiple URL Processors
// ============================================================================

static void test_05_multiple_url_processors(void)
{
    setup_test();
    
    TEST_INFO("Test 5: Multiple URL processors");
    
    // Mock server creation - create a valid dap_server_t structure
    static dap_server_t s_mock_server = {0};
    dap_server_t *l_mock_server = &s_mock_server;
    DAP_MOCK_SET_RETURN(dap_server_new, l_mock_server);
    
    // Create HTTP server
    dap_server_t *l_server = dap_http_server_new("test_server", "Test Server");
    dap_http_server_t *l_http_server = DAP_HTTP_SERVER(l_server);
    
    // Add multiple URL processors
    dap_http_url_proc_t *l_proc1 = dap_http_add_proc(
        l_http_server, "/api/v1", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    dap_http_url_proc_t *l_proc2 = dap_http_add_proc(
        l_http_server, "/api/v2", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    dap_http_url_proc_t *l_proc3 = dap_http_add_proc(
        l_http_server, "/static", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    
    TEST_ASSERT(l_proc1 != NULL, "First URL processor should be created");
    TEST_ASSERT(l_proc2 != NULL, "Second URL processor should be created");
    TEST_ASSERT(l_proc3 != NULL, "Third URL processor should be created");
    
    // Verify they are different instances
    TEST_ASSERT(l_proc1 != l_proc2, "Processors should be different instances");
    TEST_ASSERT(l_proc1 != l_proc3, "Processors should be different instances");
    TEST_ASSERT(l_proc2 != l_proc3, "Processors should be different instances");
    
    // Verify URLs
    TEST_ASSERT(strcmp(l_proc1->url, "/api/v1") == 0, "First processor URL should match");
    TEST_ASSERT(strcmp(l_proc2->url, "/api/v2") == 0, "Second processor URL should match");
    TEST_ASSERT(strcmp(l_proc3->url, "/static") == 0, "Third processor URL should match");
    
    // Cleanup
    dap_http_delete(l_server, NULL);
    
    TEST_SUCCESS("Test 5 passed: Multiple URL processors work correctly");
    teardown_test();
}

// ============================================================================
// Test 6: HTTP Module Error Handling
// ============================================================================

static void test_06_http_error_handling(void)
{
    setup_test();
    
    TEST_INFO("Test 6: HTTP module error handling");
    
    // Test init failure (header init fails)
    DAP_MOCK_SET_RETURN(dap_http_header_server_init, -1);
    DAP_MOCK_ENABLE(dap_http_header_server_init);
    
    // Deinit first
    dap_http_deinit();
    
    // Try init (should fail)
    int l_ret = dap_http_init();
    TEST_ASSERT(l_ret == -1, "HTTP init should fail when header init fails");
    
    // Reset mock and re-init
    DAP_MOCK_SET_RETURN(dap_http_header_server_init, 0);
    DAP_MOCK_ENABLE(dap_http_header_server_init);
    dap_http_deinit();
    dap_http_init();
    
    // Test init failure (client init fails)
    DAP_MOCK_SET_RETURN(dap_http_client_init, -1);
    DAP_MOCK_ENABLE(dap_http_client_init);
    dap_http_deinit();
    
    l_ret = dap_http_init();
    TEST_ASSERT(l_ret == -2, "HTTP init should fail when client init fails");
    
    // Reset and re-init for next tests
    DAP_MOCK_SET_RETURN(dap_http_client_init, 0);
    DAP_MOCK_ENABLE(dap_http_client_init);
    dap_http_deinit();
    dap_http_init();
    
    TEST_SUCCESS("Test 6 passed: HTTP error handling works correctly");
    teardown_test();
}

// ============================================================================
// Test 7: HTTP Server Creation Failure
// ============================================================================

static void test_07_http_server_creation_failure(void)
{
    setup_test();
    
    TEST_INFO("Test 7: HTTP server creation failure");
    
    // Mock server creation failure
    DAP_MOCK_SET_RETURN(dap_server_new, NULL);
    
    // Try to create HTTP server (should fail)
    dap_server_t *l_server = dap_http_server_new("test_server", "Test Server");
    TEST_ASSERT(l_server == NULL, "HTTP server creation should fail when server_new returns NULL");
    
    TEST_SUCCESS("Test 7 passed: HTTP server creation failure handling works correctly");
    teardown_test();
}

// ============================================================================
// Test 8: URL Processor with Callbacks
// ============================================================================

static void test_08_url_processor_callbacks(void)
{
    setup_test();
    
    TEST_INFO("Test 8: URL processor with callbacks");
    
    // Mock server creation - create a valid dap_server_t structure
    static dap_server_t s_mock_server = {0};
    dap_server_t *l_mock_server = &s_mock_server;
    DAP_MOCK_SET_RETURN(dap_server_new, l_mock_server);
    
    // Create HTTP server
    dap_server_t *l_server = dap_http_server_new("test_server", "Test Server");
    dap_http_server_t *l_http_server = DAP_HTTP_SERVER(l_server);
    
    // Define test callbacks
    dap_http_client_callback_t l_new_cb = (dap_http_client_callback_t)0x11111111;
    dap_http_client_callback_t l_delete_cb = (dap_http_client_callback_t)0x22222222;
    dap_http_client_callback_t l_headers_read_cb = (dap_http_client_callback_t)0x33333333;
    dap_http_client_callback_write_t l_headers_write_cb = (dap_http_client_callback_write_t)0x44444444;
    dap_http_client_callback_t l_data_read_cb = (dap_http_client_callback_t)0x55555555;
    dap_http_client_callback_write_t l_data_write_cb = (dap_http_client_callback_write_t)0x66666666;
    dap_http_client_callback_error_t l_error_cb = (dap_http_client_callback_error_t)0x77777777;
    
    // Add URL processor with callbacks
    dap_http_url_proc_t *l_url_proc = dap_http_add_proc(
        l_http_server,
        "/test",
        NULL,
        l_new_cb,
        l_delete_cb,
        l_headers_read_cb,
        l_headers_write_cb,
        l_data_read_cb,
        l_data_write_cb,
        l_error_cb
    );
    
    TEST_ASSERT(l_url_proc != NULL, "URL processor should be created");
    TEST_ASSERT(l_url_proc->new_callback == l_new_cb, "New callback should be set");
    TEST_ASSERT(l_url_proc->delete_callback == l_delete_cb, "Delete callback should be set");
    TEST_ASSERT(l_url_proc->headers_read_callback == l_headers_read_cb, 
                "Headers read callback should be set");
    TEST_ASSERT(l_url_proc->headers_write_callback == l_headers_write_cb, 
                "Headers write callback should be set");
    TEST_ASSERT(l_url_proc->data_read_callback == l_data_read_cb, 
                "Data read callback should be set");
    TEST_ASSERT(l_url_proc->data_write_callback == l_data_write_cb, 
                "Data write callback should be set");
    TEST_ASSERT(l_url_proc->error_callback == l_error_cb, "Error callback should be set");
    
    // Cleanup
    dap_http_delete(l_server, NULL);
    
    TEST_SUCCESS("Test 8 passed: URL processor callbacks work correctly");
    teardown_test();
}

// ============================================================================
// Main Test Suite
// ============================================================================

int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    TEST_SUITE_START("DAP HTTP Server Module - Full Unit Tests");
    
    // Run all tests
    TEST_RUN(test_01_http_module_init_deinit);
    TEST_RUN(test_02_http_server_creation);
    TEST_RUN(test_03_http_server_deletion);
    TEST_RUN(test_04_http_url_processor_add);
    TEST_RUN(test_05_multiple_url_processors);
    TEST_RUN(test_06_http_error_handling);
    TEST_RUN(test_07_http_server_creation_failure);
    TEST_RUN(test_08_url_processor_callbacks);
    
    suite_cleanup();
    
    TEST_SUITE_END();
    
    return 0;
}

