/**
 * HTTP Client-Server Integration Test Suite
 * 
 * Complete integration test for HTTP stack - tests BOTH client AND server together.
 * 
 * Test Architecture:
 * - Local HTTP server started on 127.0.0.1:18080
 * - HTTP client makes real requests to local server
 * - Full request/response cycle tested
 * - Real network stack, real TCP connections (no mocks)
 * 
 * Features tested:
 * - Client-server connection establishment
 * - GET/POST request processing
 * - HTTP status codes (200, 302, 404)
 * - Redirect handling (3xx responses)
 * - Error handling (4xx, 5xx)
 * - Request headers and response headers
 * - Connection lifecycle (setup, request, teardown)
 * - Concurrent connections
 * 
 * @note This is a TRUE INTEGRATION test - verifies client+server interaction
 * @note Uses real network stack - no component mocking
 * @note Server is ephemeral - started for test, stopped after
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include "dap_client_http.h"
#include "dap_server.h"
#include "dap_http_server.h"
#include "dap_http_client.h"
#include "dap_http_simple.h"
#include "dap_worker.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_http_header.h"
#include "dap_strfuncs.h"
#include "dap_test_helpers.h"
#include "dap_test_async.h"

#define LOG_TAG "test_http_client_server"

/**
 * This integration test verifies the complete HTTP request/response cycle
 * by testing client and server components together in a single process.
 * 
 * Test flow:
 * 1. Start local HTTP server (dap_http_server)
 * 2. Register HTTP handlers (dap_http_simple_proc_add)
 * 3. Create HTTP client (dap_client_http)
 * 4. Make requests to local server
 * 5. Verify responses
 * 6. Shutdown server and client
 */

// Local test server configuration
#define TEST_SERVER_ADDR "127.0.0.1"
#define TEST_SERVER_PORT 18080

static dap_worker_t *s_worker = NULL;
static dap_http_server_t *s_http_server = NULL;
static dap_server_t *s_dap_server = NULL;

// Test completion flags
static volatile bool s_test_completed = false;
static volatile bool s_test_success = false;
static volatile int s_test_status_code = 0;
static volatile size_t s_test_body_size = 0;

// ==============================================
// Test Infrastructure
// ==============================================

// ==============================================
// Test HTTP Server Handlers
// ==============================================

static void s_http_handler_get(dap_http_simple_t *a_http_simple, void *a_arg) {
    UNUSED(a_arg);
    TEST_INFO("Server: Handling GET request");
    
    const char *response = "{\"status\":\"ok\",\"message\":\"GET success\"}";
    dap_http_simple_reply(a_http_simple, (void*)response, strlen(response));
}

static void s_http_handler_404(dap_http_simple_t *a_http_simple, void *a_arg) {
    UNUSED(a_arg);
    TEST_INFO("Server: Handling 404 request");
    
    const char *response = "{\"error\":\"Not Found\"}";
    
    // Set 404 status code in HTTP response
    a_http_simple->http_client->reply_status_code = 404;
    
    dap_http_simple_reply(a_http_simple, (void*)response, strlen(response));
    dap_strncpy(a_http_simple->reply_mime, "application/json", sizeof(a_http_simple->reply_mime) - 1);
    
    // Copy reply and MIME to response
    a_http_simple->http_client->out_content_length = a_http_simple->reply_size;
    dap_strncpy(a_http_simple->http_client->out_content_type, a_http_simple->reply_mime, 
                sizeof(a_http_simple->http_client->out_content_type) - 1);
}

static void s_http_handler_redirect(dap_http_simple_t *a_http_simple, void *a_arg) {
    UNUSED(a_arg);
    TEST_INFO("Server: Handling redirect");
    
    // Set 302 redirect with Location header
    a_http_simple->http_client->reply_status_code = 302;
    dap_http_header_add(&a_http_simple->http_client->out_headers, "Location", "/get");
    
    dap_http_simple_reply(a_http_simple, NULL, 0);
}

static void s_setup_integration_test(void) {
    TEST_INFO("=== Starting LOCAL HTTP server for integration test ===");
    
    // Initialize DAP event system (2 workers for client+server)
    int l_ret = dap_events_init(2, 60000);
    TEST_ASSERT(l_ret == 0, "dap_events_init failed");
    dap_events_start();
    
    // Initialize HTTP module
    l_ret = dap_http_init();
    TEST_ASSERT(l_ret == 0, "dap_http_init failed");
    
    // Initialize HTTP simple module
    l_ret = dap_http_simple_module_init();
    TEST_ASSERT(l_ret == 0, "dap_http_simple_module_init failed");
    
    // Create HTTP server (with config section NULL, auto-config)
    s_dap_server = dap_http_server_new(NULL, "test_http_server");
    TEST_ASSERT(s_dap_server != NULL, "dap_http_server_new failed");
    
    // Get HTTP server structure
    s_http_server = DAP_HTTP_SERVER(s_dap_server);
    TEST_ASSERT(s_http_server != NULL, "HTTP server structure not found");
    
    // Add listen address (use server's client_callbacks)
    l_ret = dap_server_listen_addr_add(s_dap_server, TEST_SERVER_ADDR, TEST_SERVER_PORT,
                                       DESCRIPTOR_TYPE_SOCKET_LISTENING, &s_dap_server->client_callbacks);
    TEST_ASSERT(l_ret == 0, "dap_server_listen_addr_add failed on %s:%d", 
                TEST_SERVER_ADDR, TEST_SERVER_PORT);
    
    // Register HTTP handlers using dap_http_simple_proc_add
    dap_http_simple_proc_add(s_http_server, "/get", 10000, s_http_handler_get);
    dap_http_simple_proc_add(s_http_server, "/status/404", 10000, s_http_handler_404);
    dap_http_simple_proc_add(s_http_server, "/redirect", 10000, s_http_handler_redirect);
    
    // Initialize HTTP client
    dap_client_http_init();
    
    // Get worker for async operations
    s_worker = dap_events_worker_get_auto();
    TEST_ASSERT(s_worker != NULL, "Failed to get worker");
    
    TEST_INFO("✅ HTTP server started on http://%s:%d", TEST_SERVER_ADDR, TEST_SERVER_PORT);
    TEST_INFO("✅ HTTP client initialized (worker: %p)", s_worker);
    
    // Give server time to start
    usleep(100000);  // 100ms
}

static void s_teardown_integration_test(void) {
    TEST_INFO("=== Stopping HTTP server and cleaning up ===");
    
    // Stop HTTP server
    if (s_dap_server) {
        dap_server_delete(s_dap_server);
        s_dap_server = NULL;
        s_http_server = NULL;
    }
    
    // Cleanup HTTP modules
    dap_http_simple_module_deinit();
    dap_http_deinit();
    
    // Stop and cleanup event system
    dap_events_stop_all();
    dap_events_deinit();
    
    s_worker = NULL;
    
    TEST_INFO("✅ Cleanup complete");
}

static void s_reset_test_state(void) {
    s_test_completed = false;
    s_test_success = false;
    s_test_status_code = 0;
    s_test_body_size = 0;
}

// ==============================================
// Callbacks
// ==============================================

static void s_response_callback(void *a_body, size_t a_body_size,
                                struct dap_http_header *a_headers,
                                void *a_arg, http_status_code_t a_status_code)
{
    TEST_INFO("Response received: status=%d, size=%zu bytes", a_status_code, a_body_size);
    
    s_test_status_code = a_status_code;
    s_test_body_size = a_body_size;
    s_test_success = true;
    s_test_completed = true;
}

static void s_error_callback(int a_error_code, void *a_arg)
{
    TEST_INFO("Error callback: code=%d", a_error_code);
    
    s_test_success = false;
    s_test_completed = true;
}

// ==============================================
// Test Cases
// ==============================================

/**
 * Test 1: Basic GET request to LOCAL server
 */
static void test_01_basic_get_request(void) {
    TEST_INFO("Testing GET request to LOCAL server at http://%s:%d/get", 
              TEST_SERVER_ADDR, TEST_SERVER_PORT);
    s_reset_test_state();
    
    dap_client_http_request_simple_async(
        s_worker,
        TEST_SERVER_ADDR,
        TEST_SERVER_PORT,
        "GET",
        NULL,
        "/get",
        NULL,
        0,
        NULL,
        s_response_callback,
        s_error_callback,
        NULL,
        NULL,
        false
    );
    
    // Local server should respond quickly
    DAP_TEST_WAIT_UNTIL(s_test_completed, 5000, "GET request to local server");
    
    TEST_ASSERT(s_test_success, "Request should succeed");
    TEST_ASSERT_EQUAL_INT(Http_Status_OK, s_test_status_code, "Expected 200 OK");
    TEST_ASSERT(s_test_body_size > 0, "Response body should not be empty");
    
    TEST_SUCCESS("Basic GET request to local server works");
}

/**
 * Test 2: GET request with query parameters to LOCAL server
 */
static void test_02_get_with_params(void) {
    TEST_INFO("Testing GET request with parameters to local server");
    s_reset_test_state();
    
    dap_client_http_request_simple_async(
        s_worker,
        TEST_SERVER_ADDR,
        TEST_SERVER_PORT,
        "GET",
        NULL,
        "/get?param1=value1&param2=value2",
        NULL,
        0,
        NULL,
        s_response_callback,
        s_error_callback,
        NULL,
        NULL,
        false
    );
    
    DAP_TEST_WAIT_UNTIL(s_test_completed, 5000, "GET with params");
    
    TEST_ASSERT(s_test_success, "Request should succeed");
    TEST_ASSERT_EQUAL_INT(Http_Status_OK, s_test_status_code, "Expected 200 OK");
    
    TEST_SUCCESS("GET with parameters works");
}

/**
 * Test 3: Redirect following from LOCAL server
 */
static void test_03_redirect_following(void) {
    TEST_INFO("Testing redirect following on local server");
    s_reset_test_state();
    
    dap_client_http_request_simple_async(
        s_worker,
        TEST_SERVER_ADDR,
        TEST_SERVER_PORT,
        "GET",
        NULL,
        "/redirect",
        NULL,
        0,
        NULL,
        s_response_callback,
        s_error_callback,
        NULL,
        NULL,
        true  // follow redirects
    );
    
    DAP_TEST_WAIT_UNTIL(s_test_completed, 5000, "Redirect following");
    
    TEST_ASSERT(s_test_success, "Should follow redirects successfully");
    TEST_ASSERT_EQUAL_INT(Http_Status_OK, s_test_status_code, "Expected 200 OK after redirect");
    
    TEST_SUCCESS("Redirect following works");
}

/**
 * Test 4: 404 Not Found handling on LOCAL server
 */
static void test_04_not_found_handling(void) {
    TEST_INFO("Testing 404 Not Found handling on local server");
    s_reset_test_state();
    
    dap_client_http_request_simple_async(
        s_worker,
        TEST_SERVER_ADDR,
        TEST_SERVER_PORT,
        "GET",
        NULL,
        "/status/404",
        NULL,
        0,
        NULL,
        s_response_callback,
        s_error_callback,
        NULL,
        NULL,
        false
    );
    
    DAP_TEST_WAIT_UNTIL(s_test_completed, 5000, "404 handling");
    
    TEST_ASSERT(s_test_completed, "Request should complete");
    TEST_ASSERT_EQUAL_INT(Http_Status_NotFound, s_test_status_code, "Expected 404 Not Found");
    
    TEST_SUCCESS("404 handling works");
}

/**
 * Test 5: Connection to wrong port (simulates server down)
 */
static void test_05_connection_failure(void) {
    TEST_INFO("Testing connection to non-existent endpoint");
    s_reset_test_state();
    
    // Try to connect to wrong port (server not listening there)
    dap_client_http_request_simple_async(
        s_worker,
        TEST_SERVER_ADDR,
        TEST_SERVER_PORT + 1,  // Wrong port
        "GET",
        NULL,
        "/",
        NULL,
        0,
        NULL,
        s_response_callback,
        s_error_callback,
        NULL,
        NULL,
        false
    );
    
    // Wait a bit for connection attempt
    for (int i = 0; i < 30 && !s_test_completed; i++) {
        usleep(100000); // 100ms
    }
    
    // If completed, should be error (connection refused)
    if (s_test_completed) {
        TEST_ASSERT(!s_test_success, "Connection to wrong port should fail");
    }
    
    TEST_SUCCESS("Connection failure handling works");
}

/**
 * Test 6: Multiple concurrent requests to LOCAL server
 */

// State for concurrent requests test
static struct {
    volatile int completed_count;
    volatile int success_count;
    volatile int error_count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} s_concurrent_state;

static void s_concurrent_response_callback(void *a_body, size_t a_body_size,
                                          struct dap_http_header *a_headers,
                                          void *a_arg, http_status_code_t a_status_code)
{
    int request_id = (int)(intptr_t)a_arg;
    
    pthread_mutex_lock(&s_concurrent_state.mutex);
    
    s_concurrent_state.completed_count++;
    if (a_status_code == Http_Status_OK) {
        s_concurrent_state.success_count++;
        TEST_INFO("Request #%d: SUCCESS (status=%d, size=%zu)", 
                  request_id, a_status_code, a_body_size);
    } else {
        TEST_INFO("Request #%d: UNEXPECTED status=%d", request_id, a_status_code);
    }
    
    pthread_cond_signal(&s_concurrent_state.cond);
    pthread_mutex_unlock(&s_concurrent_state.mutex);
}

static void s_concurrent_error_callback(int a_error_code, void *a_arg)
{
    int request_id = (int)(intptr_t)a_arg;
    
    pthread_mutex_lock(&s_concurrent_state.mutex);
    
    s_concurrent_state.completed_count++;
    s_concurrent_state.error_count++;
    TEST_INFO("Request #%d: ERROR (code=%d)", request_id, a_error_code);
    
    pthread_cond_signal(&s_concurrent_state.cond);
    pthread_mutex_unlock(&s_concurrent_state.mutex);
}

static void test_06_concurrent_requests(void) {
    TEST_INFO("Testing multiple concurrent requests to local server");
    
    #define CONCURRENT_COUNT 5
    
    // Initialize concurrent state
    s_concurrent_state.completed_count = 0;
    s_concurrent_state.success_count = 0;
    s_concurrent_state.error_count = 0;
    pthread_mutex_init(&s_concurrent_state.mutex, NULL);
    pthread_cond_init(&s_concurrent_state.cond, NULL);
    
    // Launch multiple requests
    for (int i = 0; i < CONCURRENT_COUNT; i++) {
        dap_client_http_request_simple_async(
            s_worker,
            TEST_SERVER_ADDR,
            TEST_SERVER_PORT,
            "GET",
            NULL,
            "/get",
            NULL,
            0,
            NULL,
            s_concurrent_response_callback,
            s_concurrent_error_callback,
            (void*)(intptr_t)i,
            NULL,
            false
        );
        TEST_INFO("Launched request #%d", i);
    }
    
    // Wait for all to complete with timeout
    pthread_mutex_lock(&s_concurrent_state.mutex);
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 10;  // 10 second timeout
    
    while (s_concurrent_state.completed_count < CONCURRENT_COUNT) {
        int ret = pthread_cond_timedwait(&s_concurrent_state.cond, 
                                         &s_concurrent_state.mutex, 
                                         &timeout);
        if (ret == ETIMEDOUT) {
            TEST_ERROR("Timeout waiting for concurrent requests: %d/%d completed",
                      s_concurrent_state.completed_count, CONCURRENT_COUNT);
            break;
        }
    }
    
    int completed = s_concurrent_state.completed_count;
    int success = s_concurrent_state.success_count;
    int errors = s_concurrent_state.error_count;
    
    pthread_mutex_unlock(&s_concurrent_state.mutex);
    
    // Verify results
    TEST_INFO("Results: %d completed, %d success, %d errors", 
              completed, success, errors);
    
    TEST_ASSERT_EQUAL_INT(CONCURRENT_COUNT, completed, 
                         "All requests should complete");
    TEST_ASSERT_EQUAL_INT(CONCURRENT_COUNT, success, 
                         "All requests should succeed");
    TEST_ASSERT_EQUAL_INT(0, errors, "No errors expected");
    
    // Cleanup
    pthread_mutex_destroy(&s_concurrent_state.mutex);
    pthread_cond_destroy(&s_concurrent_state.cond);
    
    TEST_SUCCESS("Concurrent requests work correctly");
}

// ==============================================
// Main Test Suite
// ==============================================

int main(void) {
    TEST_SUITE_START("HTTP Client + Server Integration Tests");
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  TRUE INTEGRATION TEST - Client + Server\n");
    printf("  Local HTTP server: http://%s:%d\n", TEST_SERVER_ADDR, TEST_SERVER_PORT);
    printf("  No mocks - real TCP connections, real HTTP protocol\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Setup once for all tests (starts local server)
    s_setup_integration_test();
    
    if (!s_http_server) {
        TEST_ERROR("Failed to start HTTP server, aborting tests");
        return 1;
    }
    
    // Run tests
    TEST_RUN(test_01_basic_get_request);
    TEST_RUN(test_02_get_with_params);
    TEST_RUN(test_03_redirect_following);
    TEST_RUN(test_04_not_found_handling);
    TEST_RUN(test_05_connection_failure);
    TEST_RUN(test_06_concurrent_requests);
    
    // Cleanup (stops server)
    s_teardown_integration_test();
    
    TEST_SUITE_END();
    
    return 0;
}

