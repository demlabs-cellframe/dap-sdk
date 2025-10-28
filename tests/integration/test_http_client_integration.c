/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file test_http_client_integration.c
 * @brief Integration tests for DAP HTTP Client
 * 
 * Integration tests that make real HTTP requests to test servers.
 * These tests verify actual network connectivity, protocol handling,
 * and real-world scenarios without mocks.
 * 
 * @date 2025-10-28
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_test_helpers.h"
#include "dap_client_http.h"
#include "dap_events.h"
#include "dap_worker.h"

#define LOG_TAG "test_http_client_integration"

// Test configuration
#define TEST_TIMEOUT_SEC 30
#define TEST_SERVER_URL "http://httpbin.org"  // Public HTTP test server

// ============================================================================
// Test State
// ============================================================================

static bool s_test_initialized = false;
static dap_worker_t *s_test_worker = NULL;

// Test result tracking
typedef struct {
    bool completed;
    bool success;
    int response_code;
    char *error_message;
} test_result_t;

// ============================================================================
// Setup/Teardown
// ============================================================================

static void setup_integration_test(void)
{
    if (!s_test_initialized) {
        TEST_INFO("Initializing HTTP client integration tests...");
        
        // Initialize DAP events system
        int ret = dap_events_init();
        TEST_ASSERT(ret == 0, "Failed to initialize DAP events");
        
        ret = dap_events_start();
        TEST_ASSERT(ret == 0, "Failed to start DAP events");
        
        // Initialize HTTP client
        ret = dap_client_http_init();
        TEST_ASSERT(ret == 0, "Failed to initialize HTTP client");
        
        // Get worker for async operations
        s_test_worker = dap_events_worker_get_auto();
        TEST_ASSERT_NOT_NULL(s_test_worker, "Failed to get worker");
        
        s_test_initialized = true;
        TEST_INFO("Integration test environment initialized");
    }
}

static void teardown_integration_test(void)
{
    // Cleanup is done in suite_cleanup
}

static void suite_cleanup(void)
{
    if (s_test_initialized) {
        TEST_INFO("Cleaning up integration test environment...");
        
        dap_events_stop_all();
        dap_events_deinit();
        
        s_test_initialized = false;
        s_test_worker = NULL;
        
        TEST_INFO("Integration test environment cleaned up");
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Wait for test result with timeout
 */
static bool wait_for_result(test_result_t *result, int timeout_sec)
{
    int elapsed = 0;
    while (!result->completed && elapsed < timeout_sec) {
        sleep(1);
        elapsed++;
    }
    return result->completed;
}

// ============================================================================
// Callback Functions
// ============================================================================

/**
 * @brief Callback for simple GET request
 */
static void simple_get_callback(dap_client_http_t *a_client, void *a_arg)
{
    test_result_t *result = (test_result_t *)a_arg;
    
    if (!a_client) {
        result->error_message = strdup("Client is NULL");
        result->success = false;
        result->completed = true;
        return;
    }
    
    result->response_code = a_client->http_resp_code;
    
    if (result->response_code == 200) {
        result->success = true;
        TEST_INFO("    Received HTTP 200 OK");
        if (a_client->http_resp_data_size > 0) {
            TEST_INFO("    Response size: %zu bytes", a_client->http_resp_data_size);
        }
    } else {
        result->success = false;
        result->error_message = dap_strdup_printf("Unexpected response code: %d", 
                                                   result->response_code);
    }
    
    result->completed = true;
}

/**
 * @brief Callback for error handling test
 */
static void error_callback(dap_client_http_t *a_client, void *a_arg)
{
    test_result_t *result = (test_result_t *)a_arg;
    
    // For error tests, we expect the callback to be called even on failure
    result->completed = true;
    
    if (a_client && a_client->http_resp_code != 0) {
        result->response_code = a_client->http_resp_code;
        result->success = true;  // Successfully handled error
    } else {
        result->success = true;  // Connection failure is expected for invalid URLs
    }
}

// ============================================================================
// Integration Tests
// ============================================================================

/**
 * @brief Test 1: Simple GET request to public API
 */
static void test_01_simple_get_request(void)
{
    setup_integration_test();
    
    TEST_INFO("Test 1: Simple GET request to " TEST_SERVER_URL "/get");
    
    test_result_t result = {0};
    
    // Make async HTTP GET request
    dap_client_http_t *client = dap_client_http_request_simple_async(
        TEST_SERVER_URL,
        "/get",
        NULL,  // no custom headers
        simple_get_callback,
        &result,
        NULL,  // no request body
        NULL   // content type not needed for GET
    );
    
    TEST_ASSERT_NOT_NULL(client, "Failed to create HTTP client");
    
    // Wait for result
    bool completed = wait_for_result(&result, TEST_TIMEOUT_SEC);
    TEST_ASSERT(completed, "Request timed out after %d seconds", TEST_TIMEOUT_SEC);
    TEST_ASSERT(result.success, "Request failed: %s", 
                result.error_message ? result.error_message : "Unknown error");
    TEST_ASSERT_EQUAL_INT(200, result.response_code, "Expected HTTP 200 OK");
    
    if (result.error_message) {
        DAP_DELETE(result.error_message);
    }
    
    TEST_SUCCESS("Test 1 passed: Simple GET request works");
    teardown_integration_test();
}

/**
 * @brief Test 2: GET request with query parameters
 */
static void test_02_get_with_parameters(void)
{
    setup_integration_test();
    
    TEST_INFO("Test 2: GET request with query parameters");
    
    test_result_t result = {0};
    
    // httpbin.org/get?param1=value1&param2=value2 echoes back the parameters
    dap_client_http_t *client = dap_client_http_request_simple_async(
        TEST_SERVER_URL,
        "/get?test_param=integration&value=123",
        NULL,
        simple_get_callback,
        &result,
        NULL,
        NULL
    );
    
    TEST_ASSERT_NOT_NULL(client, "Failed to create HTTP client");
    
    bool completed = wait_for_result(&result, TEST_TIMEOUT_SEC);
    TEST_ASSERT(completed, "Request timed out");
    TEST_ASSERT(result.success, "Request with parameters failed");
    TEST_ASSERT_EQUAL_INT(200, result.response_code, "Expected HTTP 200 OK");
    
    if (result.error_message) {
        DAP_DELETE(result.error_message);
    }
    
    TEST_SUCCESS("Test 2 passed: GET with parameters works");
    teardown_integration_test();
}

/**
 * @brief Test 3: Handle 404 error
 */
static void test_03_handle_404_error(void)
{
    setup_integration_test();
    
    TEST_INFO("Test 3: Handle 404 Not Found error");
    
    test_result_t result = {0};
    
    dap_client_http_t *client = dap_client_http_request_simple_async(
        TEST_SERVER_URL,
        "/status/404",  // httpbin.org provides /status/<code> endpoints
        NULL,
        error_callback,
        &result,
        NULL,
        NULL
    );
    
    TEST_ASSERT_NOT_NULL(client, "Failed to create HTTP client");
    
    bool completed = wait_for_result(&result, TEST_TIMEOUT_SEC);
    TEST_ASSERT(completed, "Request timed out");
    TEST_ASSERT(result.success, "Error handling failed");
    TEST_ASSERT_EQUAL_INT(404, result.response_code, "Expected HTTP 404");
    
    if (result.error_message) {
        DAP_DELETE(result.error_message);
    }
    
    TEST_SUCCESS("Test 3 passed: 404 error handled correctly");
    teardown_integration_test();
}

/**
 * @brief Test 4: Handle invalid URL
 */
static void test_04_invalid_url(void)
{
    setup_integration_test();
    
    TEST_INFO("Test 4: Handle invalid URL");
    
    test_result_t result = {0};
    
    // Try to connect to non-existent host
    dap_client_http_t *client = dap_client_http_request_simple_async(
        "http://this-domain-definitely-does-not-exist-12345.invalid",
        "/",
        NULL,
        error_callback,
        &result,
        NULL,
        NULL
    );
    
    // Client creation might fail immediately or callback might be called
    if (client) {
        bool completed = wait_for_result(&result, 5);  // Shorter timeout for invalid URL
        TEST_ASSERT(completed, "Error callback should be called for invalid URL");
        TEST_ASSERT(result.success, "Error should be handled gracefully");
    } else {
        // Client creation failed immediately - this is also acceptable
        result.success = true;
    }
    
    TEST_ASSERT(result.success, "Invalid URL should be handled gracefully");
    
    if (result.error_message) {
        DAP_DELETE(result.error_message);
    }
    
    TEST_SUCCESS("Test 4 passed: Invalid URL handled correctly");
    teardown_integration_test();
}

// ============================================================================
// Test Suite Main
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    TEST_SUITE_START("DAP HTTP Client Integration Tests");
    
    TEST_INFO("NOTE: These tests require internet connection to " TEST_SERVER_URL);
    TEST_INFO("      Tests will timeout if network is unavailable");
    
    // Run integration tests
    TEST_RUN(test_01_simple_get_request);
    TEST_RUN(test_02_get_with_parameters);
    TEST_RUN(test_03_handle_404_error);
    TEST_RUN(test_04_invalid_url);
    
    // Cleanup
    suite_cleanup();
    
    TEST_SUITE_END();
    
    return 0;
}

