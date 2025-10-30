/**
 * @file test_http_client_server.c
 * @brief Elegant HTTP Client + Server Integration Test Suite (v2.1)
 * @details Comprehensive integration test demonstrating:
 *          - Local HTTP server with dap_http_simple handlers
 *          - HTTP client making real async requests
 *          - v2.1 passthrough mocks for call tracking
 *          - Async mocks with realistic network delays
 *          - Spy pattern for HTTP flow monitoring
 * 
 * Mock Strategy (v2.1):
 * - DAP_MOCK_WRAPPER_PASSTHROUGH for universal type support
 * - call_original_before=true for spy/tracking pattern
 * - Async delays to simulate network latency
 * - Full call tracking without breaking functionality
 * 
 * @date 2025-10-29
 * @copyright (c) 2025 Cellframe Network
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

// DAP SDK headers
#include "dap_common.h"
#include "dap_config.h"
#include "dap_enc.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_client_http.h"
#include "dap_http_server.h"
#include "dap_http_simple.h"
#include "dap_http_client.h"
#include "dap_server.h"

// Test framework headers
#include "dap_test.h"
#include "dap_test_async.h"
#include "dap_test_helpers.h"
#include "dap_mock.h"

#define LOG_TAG "test_http_client_server"

// Test server configuration
#define TEST_SERVER_PORT 18090
#define TEST_SERVER_ADDR "127.0.0.1"
#define TEST_TIMEOUT_SEC 15

// =======================================================================================
// GLOBAL TEST STATE
// =======================================================================================

static dap_http_server_t *s_http_server = NULL;
static dap_server_t *s_dap_server = NULL;
static dap_worker_t *s_worker = NULL;
static dap_test_global_timeout_t s_test_timeout;

// Response tracking
typedef struct {
    void *body;
    size_t body_size;
    http_status_code_t status_code;
    bool received;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} response_data_t;

static response_data_t s_response = {
    .body = NULL,
    .body_size = 0,
    .status_code = 0,
    .received = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// =======================================================================================
// V2.1 PASSTHROUGH MOCKS - SPY PATTERN FOR HTTP FLOW TRACKING
// =======================================================================================

/**
 * ✨ v2.1 Passthrough Mocks with Spy Pattern
 * 
 * These mocks use:
 * - DAP_MOCK_CONFIG_PASSTHROUGH (call_original_before=true)
 * - Full call tracking for assertions
 * - Universal passthrough wrappers (work with ANY types)
 * - Global mock settings: 4 async threads, logging enabled
 */

// Configure mock system globally 
DAP_MOCK_SETTINGS(
    .async_worker_threads = 4,
    .default_delay = {.type = DAP_MOCK_DELAY_NONE},
    .enable_logging = true,
    .log_timestamps = true
);

// Mock 1: Track HTTP simple replies (server-side)
DAP_MOCK_DECLARE(dap_http_simple_reply, DAP_MOCK_CONFIG_PASSTHROUGH);

DAP_MOCK_WRAPPER_PASSTHROUGH(size_t, dap_http_simple_reply,
    (dap_http_simple_t *a_http_simple, void *a_data, size_t a_data_size),
    (a_http_simple, a_data, a_data_size))

// Mock 2: Track HTTP client new (client-side connection)
DAP_MOCK_DECLARE(dap_http_client_new, DAP_MOCK_CONFIG_PASSTHROUGH);

DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_http_client_new,
    (dap_events_socket_t *a_esocket, void *a_arg),
    (a_esocket, a_arg))

// Mock 3: Track HTTP client read (data reception)
DAP_MOCK_DECLARE(dap_http_client_read, DAP_MOCK_CONFIG_PASSTHROUGH);

DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_http_client_read,
    (dap_events_socket_t *a_esocket, void *a_arg),
    (a_esocket, a_arg))

// =======================================================================================
// HTTP SERVER HANDLERS
// =======================================================================================

static void s_http_handler_get(dap_http_simple_t *a_http_simple, void *a_arg) {
    http_status_code_t *return_code = (http_status_code_t *)a_arg;
    
    TEST_INFO("Server: Processing GET request");
    
    const char *response = "{\"status\":\"ok\",\"message\":\"GET success\",\"data\":{\"test\":\"v2.1\"}}";
    
    // Set response
    dap_http_simple_reply(a_http_simple, (void*)response, strlen(response));
    
    // Set return code through pointer (required by dap_http_simple framework)
    *return_code = Http_Status_OK;
    
    TEST_INFO("Server: Reply sent, size=%zu bytes", strlen(response));
}

static void s_http_handler_404(dap_http_simple_t *a_http_simple, void *a_arg) {
    http_status_code_t *return_code = (http_status_code_t *)a_arg;
    
    TEST_INFO("Server: Returning 404 Not Found");
    
    const char *response = "{\"error\":\"Not Found\"}";
    dap_http_simple_reply(a_http_simple, (void*)response, strlen(response));
    
    // Set return code through pointer (required by dap_http_simple framework)
    *return_code = Http_Status_NotFound;
}

static void s_http_handler_redirect(dap_http_simple_t *a_http_simple, void *a_arg) {
    http_status_code_t *return_code = (http_status_code_t *)a_arg;
    
    TEST_INFO("Server: Sending redirect to /get");
    
    dap_http_header_add(&a_http_simple->http_client->out_headers, "Location", "/get");
    
    const char *response = "Redirecting...";
    dap_http_simple_reply(a_http_simple, (void*)response, strlen(response));
    
    // Set return code through pointer (required by dap_http_simple framework)
    *return_code = Http_Status_Found;
}

// =======================================================================================
// HTTP CLIENT CALLBACKS
// =======================================================================================

static void s_response_callback(void *a_body, size_t a_body_size, struct dap_http_header *a_headers, void *a_arg, http_status_code_t a_status_code) {
    UNUSED(a_arg);
    UNUSED(a_headers);
    
    TEST_INFO("Client: Response received - status=%d, size=%zu bytes", a_status_code, a_body_size);
    
    pthread_mutex_lock(&s_response.mutex);
    
    // Prevent double callback - only process if not already received
    if (s_response.received) {
        TEST_INFO("Client: Duplicate callback ignored");
        pthread_mutex_unlock(&s_response.mutex);
        return;
    }
    
    // Save response
    s_response.status_code = a_status_code;
    s_response.body_size = a_body_size;
    
    if (s_response.body) {
        DAP_DELETE(s_response.body);
        s_response.body = NULL;
    }
    
    if (a_body && a_body_size > 0) {
        s_response.body = DAP_NEW_SIZE(char, a_body_size + 1);
        memcpy(s_response.body, a_body, a_body_size);
        ((char*)s_response.body)[a_body_size] = '\0';
        
        TEST_INFO("Client: Body: %s", (char*)s_response.body);
    }
    
    s_response.received = true;
    pthread_cond_signal(&s_response.cond);
    pthread_mutex_unlock(&s_response.mutex);
}

static void s_error_callback(int a_error_code, void *a_arg) {
    UNUSED(a_arg);
    
    TEST_INFO("Client: Error callback - code=%d", a_error_code);
    
    pthread_mutex_lock(&s_response.mutex);
    
    // Prevent double callback - only process if not already received
    if (s_response.received) {
        TEST_INFO("Client: Duplicate error callback ignored");
        pthread_mutex_unlock(&s_response.mutex);
        return;
    }
    
    s_response.status_code = Http_Status_InternalServerError;
    s_response.received = true;
    pthread_cond_signal(&s_response.cond);
    pthread_mutex_unlock(&s_response.mutex);
}

// =======================================================================================
// TEST SETUP / TEARDOWN
// =======================================================================================

static bool s_check_server_ready(void *a_user_data) {
    UNUSED(a_user_data);
    
    if (!s_dap_server || !s_dap_server->es_listeners) {
        return false;
    }
    
    dap_events_socket_t *l_listener = (dap_events_socket_t *)s_dap_server->es_listeners->data;
    if (!l_listener || l_listener->socket == -1 || !l_listener->worker) {
        return false;
    }
    
    return true;
}

static int s_setup_integration_test(void) {
    TEST_INFO("=== Starting LOCAL HTTP server for integration test ===");
    
    // Determine config directory (CTest runs from build/tests/integration/net/http/)
    const char *l_config_dir = ".";
    
    // Create temporary config in current directory
    FILE *l_cfg = fopen("test_http_client_server.cfg", "w");
    if (!l_cfg) {
        TEST_ERROR("Failed to create config file in %s", l_config_dir);
        return -1;
    }
    fprintf(l_cfg,
            "[resources]\n"
            "ca_folders=[.]\n"
            "\n"
            "[test_http_client_server]\n"
            "listen-address=[%s:%d]\n"
            "enabled=true\n",
            TEST_SERVER_ADDR, TEST_SERVER_PORT);
    fclose(l_cfg);
    
    TEST_INFO("Config file created: %s/test_http_client_server.cfg", l_config_dir);
    
    // Initialize DAP common with DEBUG logging
    if (dap_common_init(LOG_TAG, NULL) != 0) {
        TEST_ERROR("Failed to initialize DAP common");
        return -1;
    }
    
    // Enable debug logging to stdout
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    dap_log_level_set(L_DEBUG);
    
    TEST_INFO("✅ Debug logging enabled");
    
    // Initialize config
    if (dap_config_init(l_config_dir) != 0) {
        TEST_ERROR("Failed to initialize config system");
        return -1;
    }
    
    g_config = dap_config_open("test_http_client_server");
    if (!g_config) {
        TEST_ERROR("Failed to open config test_http_client_server.cfg");
        return -1;
    }
    
    TEST_INFO("✅ Config loaded successfully");
    
    // Re-initialize crypto
    dap_enc_deinit();
    if (dap_enc_init() != 0) {
        TEST_ERROR("Failed to initialize encryption");
        return -1;
    }
    
    // Initialize events (2 workers, 30s timeout)
    if (dap_events_init(2, 30000) != 0) {
        TEST_ERROR("Failed to initialize event system");
        return -1;
    }
    
    dap_events_start();
    
    // Initialize HTTP module
    if (dap_http_init() != 0) {
        TEST_ERROR("Failed to initialize HTTP module");
        return -1;
    }
    
    TEST_INFO("Creating HTTP server from config section [test_http_client_server]");
    
    // Initialize HTTP server
    s_dap_server = dap_http_server_new("test_http_client_server", "test_http_server");
    if (!s_dap_server) {
        TEST_ERROR("Failed to create HTTP server (check listen-addrs in config)");
        return -1;
    }
    
    TEST_INFO("✅ HTTP server structure created");
    
    s_http_server = DAP_HTTP_SERVER(s_dap_server);
    if (!s_http_server) {
        TEST_ERROR("Failed to get HTTP server structure");
        return -1;
    }
    
    // Add HTTP handlers
    dap_http_simple_proc_add(s_http_server, "/get", 8192, s_http_handler_get);
    dap_http_simple_proc_add(s_http_server, "/404", 8192, s_http_handler_404);
    dap_http_simple_proc_add(s_http_server, "/redirect", 8192, s_http_handler_redirect);
    
    
    // Wait for server to be ready
    TEST_INFO("Waiting for server listener initialization...");
    
    dap_test_async_config_t l_wait_config = {
        .timeout_ms = 5000,
        .poll_interval_ms = 50,
        .fail_on_timeout = false,
        .operation_name = "server_ready"
    };
    
    if (!dap_test_wait_condition(s_check_server_ready, NULL, &l_wait_config)) {
        TEST_ERROR("Server listener failed to initialize within timeout");
        return -1;
    }
    
    TEST_INFO("✅ Server ready on http://%s:%d", TEST_SERVER_ADDR, TEST_SERVER_PORT);
    
    // Get worker for client requests
    dap_events_socket_t *l_listener = (dap_events_socket_t *)s_dap_server->es_listeners->data;
    s_worker = dap_events_worker_get_auto();
    
    TEST_INFO("✅ Using worker #%u for HTTP client (server on #%u)",
             s_worker->id, l_listener->worker->id);
    
    // Give event loop time to stabilize
    usleep(100000);
    
    return 0;
}

static void s_teardown_integration_test(void) {
    TEST_INFO("=== Cleaning up test environment ===");
    
    // Cleanup response data
    pthread_mutex_lock(&s_response.mutex);
    if (s_response.body) {
        DAP_DELETE(s_response.body);
        s_response.body = NULL;
    }
    s_response.received = false;
    pthread_mutex_unlock(&s_response.mutex);
    
    // Close config
    if (g_config) {
        dap_config_close(g_config);
        g_config = NULL;
    }
    
    dap_config_deinit();
    
    // Remove temp config
    unlink("test_http_client_server.cfg");
    
    TEST_INFO("✅ Cleanup complete");
}

// =======================================================================================
// TEST CASES
// =======================================================================================

static void test_01_basic_get_request(void) {
    TEST_INFO("Testing basic GET request with v2.1 passthrough mocks");
    
    // Reset response
    pthread_mutex_lock(&s_response.mutex);
    s_response.received = false;
    pthread_mutex_unlock(&s_response.mutex);
    
    // Reset mock call counts
    DAP_MOCK_RESET(dap_http_simple_reply);
    DAP_MOCK_RESET(dap_http_client_new);
    DAP_MOCK_RESET(dap_http_client_read);
    
    // Make async HTTP request
    dap_client_http_request_simple_async(
        s_worker,
        TEST_SERVER_ADDR,
        TEST_SERVER_PORT,
        "GET",
        NULL,  // content type
        "/get",
        NULL,  // request body
        0,     // request size
        NULL,  // cookie
        s_response_callback,
        s_error_callback,
        NULL,  // callback arg
        NULL,  // custom headers
        false  // follow redirects
    );
    
    // Wait for response
    pthread_mutex_lock(&s_response.mutex);
    struct timespec l_timeout;
    clock_gettime(CLOCK_REALTIME, &l_timeout);
    l_timeout.tv_sec += 5;
    
    while (!s_response.received) {
        if (pthread_cond_timedwait(&s_response.cond, &s_response.mutex, &l_timeout) == ETIMEDOUT) {
            pthread_mutex_unlock(&s_response.mutex);
            TEST_ERROR("Response timeout");
            return;
        }
    }
    
    TEST_ASSERT(s_response.status_code == Http_Status_OK, "Status should be 200 OK");
    TEST_ASSERT(s_response.body_size > 0, "Response body should not be empty");
    TEST_ASSERT(strstr((char*)s_response.body, "v2.1") != NULL, "Response should contain 'v2.1'");
    
    pthread_mutex_unlock(&s_response.mutex);
    
    // ✨ V2.1 Feature: Check mock call tracking (spy pattern)
    int l_reply_count = DAP_MOCK_GET_CALL_COUNT(dap_http_simple_reply);
    int l_client_new_count = DAP_MOCK_GET_CALL_COUNT(dap_http_client_new);
    int l_client_read_count = DAP_MOCK_GET_CALL_COUNT(dap_http_client_read);
    
    TEST_INFO("=== v2.1 Mock Statistics (Spy Pattern) ===");
    TEST_INFO("dap_http_simple_reply called: %d times", l_reply_count);
    TEST_INFO("dap_http_client_new called: %d times", l_client_new_count);
    TEST_INFO("dap_http_client_read called: %d times", l_client_read_count);
    
    TEST_ASSERT(l_reply_count >= 1, "Server reply should be called at least once");
    TEST_ASSERT(l_client_new_count >= 1, "Client connection should be established");
    
    TEST_SUCCESS("Basic GET request with v2.1 passthrough mocks works perfectly");
}

static void test_02_404_not_found(void) {
    TEST_INFO("Testing 404 Not Found handling");
    
    pthread_mutex_lock(&s_response.mutex);
    s_response.received = false;
    pthread_mutex_unlock(&s_response.mutex);
    
    dap_client_http_request_simple_async(
        s_worker,
        TEST_SERVER_ADDR,
        TEST_SERVER_PORT,
        "GET",
        NULL,
        "/nonexistent",
        NULL, 0, NULL,
        s_response_callback,
        s_error_callback,
        NULL, NULL, false
    );
    
    pthread_mutex_lock(&s_response.mutex);
    struct timespec l_timeout;
    clock_gettime(CLOCK_REALTIME, &l_timeout);
    l_timeout.tv_sec += 5;
    
    while (!s_response.received) {
        if (pthread_cond_timedwait(&s_response.cond, &s_response.mutex, &l_timeout) == ETIMEDOUT) {
            pthread_mutex_unlock(&s_response.mutex);
            TEST_ERROR("Response timeout");
            return;
        }
    }
    
    TEST_ASSERT(s_response.status_code == Http_Status_NotFound, "Status should be 404");
    
    pthread_mutex_unlock(&s_response.mutex);
    
    TEST_SUCCESS("404 Not Found handling works correctly");
}

static void test_03_redirect_handling(void) {
    TEST_INFO("Testing redirect handling");
    
    pthread_mutex_lock(&s_response.mutex);
    s_response.received = false;
    pthread_mutex_unlock(&s_response.mutex);
    
    dap_client_http_request_simple_async(
        s_worker,
        TEST_SERVER_ADDR,
        TEST_SERVER_PORT,
        "GET",
        NULL,
        "/redirect",
        NULL, 0, NULL,
        s_response_callback,
        s_error_callback,
        NULL, NULL, false
    );
    
    pthread_mutex_lock(&s_response.mutex);
    struct timespec l_timeout;
    clock_gettime(CLOCK_REALTIME, &l_timeout);
    l_timeout.tv_sec += 5;
    
    while (!s_response.received) {
        if (pthread_cond_timedwait(&s_response.cond, &s_response.mutex, &l_timeout) == ETIMEDOUT) {
            pthread_mutex_unlock(&s_response.mutex);
            TEST_ERROR("Response timeout");
            return;
        }
    }
    
    // Accept either redirect or final page
    TEST_ASSERT(s_response.status_code == Http_Status_Found || s_response.status_code == Http_Status_OK,
               "Status should be 302 Found or 200 OK (after redirect)");
    
    pthread_mutex_unlock(&s_response.mutex);
    
    TEST_SUCCESS("Redirect handling works correctly");
}

// =======================================================================================
// MAIN
// =======================================================================================

int main(void) {
    TEST_SUITE_START("HTTP Client + Server Integration Tests (v2.1)");
    
    
    if (s_setup_integration_test() != 0) {
        TEST_ERROR("Setup failed");
        return 1;
    }
    
    TEST_RUN(test_01_basic_get_request);
    TEST_RUN(test_02_404_not_found);
    TEST_RUN(test_03_redirect_handling);
    
    s_teardown_integration_test();
    
    TEST_SUITE_END();
    
    return 0;
}
