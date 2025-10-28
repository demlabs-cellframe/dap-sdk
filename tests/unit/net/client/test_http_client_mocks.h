/**
 * @file test_http_client_mocks.h
 * @brief HTTP Client Mock Framework for Unit Testing
 * @details Provides mocking for dap_client_http functions using GNU ld linker wrapping
 * 
 * This allows testing HTTP client behavior without actual network calls.
 * 
 * @date 2025-10-27
 * @copyright (c) 2025 Cellframe Network
 */

#ifndef TEST_HTTP_CLIENT_MOCKS_H
#define TEST_HTTP_CLIENT_MOCKS_H

#include "dap_mock.h"
#include "dap_mock_linker_wrapper.h"
#include "dap_client_http.h"
#include <string.h>

/**
 * Mock State Tracking with Random Delays
 * 
 * All HTTP client mocks simulate network latency with random delays:
 * 100ms ± 50ms (50-150ms range)
 */

// Config with 100±50ms delay
#define HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY ((dap_mock_config_t){.enabled=true, .delay={.type=DAP_MOCK_DELAY_VARIANCE, .variance={.center_us=100000, .variance_us=50000}}})
#define HTTP_CLIENT_MOCK_CONFIG_NO_DELAY ((dap_mock_config_t){.enabled=true, .delay={.type=DAP_MOCK_DELAY_NONE}})

// Mock for dap_client_http_request_full with 100±50ms delay
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request_full, HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY)

// Mock for dap_client_http_request with 100±50ms delay
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request, HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY)

// Mock for dap_client_http_request_custom with 100±50ms delay
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request_custom, HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY)

// Mock for dap_client_http_request_async with 100±50ms delay
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request_async, HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY)

// Mock for dap_client_http_request_simple_async with 100±50ms delay
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_request_simple_async, HTTP_CLIENT_MOCK_CONFIG_WITH_DELAY)

// Mock for dap_client_http_close_unsafe (no delay for cleanup)
DAP_MOCK_DECLARE_CUSTOM(dap_client_http_close_unsafe, HTTP_CLIENT_MOCK_CONFIG_NO_DELAY)

/**
 * Mock Response Configuration
 */
typedef struct dap_http_client_mock_response {
    http_status_code_t status_code;
    void *body;
    size_t body_size;
    struct dap_http_header *headers;
    int error_code;  // 0 = success, > 0 = error
    uint32_t delay_ms;  // Simulate network delay
    bool should_fail;   // If true, trigger error callback
} dap_http_client_mock_response_t;

/**
 * Global mock response configuration
 */
extern dap_http_client_mock_response_t g_mock_http_response;

/**
 * Initialize HTTP client mocks
 * Note: DAP_MOCK_DECLARE already auto-registers mocks via constructor,
 * so we only need to initialize mock response configuration and enable mocks
 */
static inline void dap_http_client_mocks_init(void) {
    dap_mock_init();
    
    // Initialize default response
    memset(&g_mock_http_response, 0, sizeof(g_mock_http_response));
    g_mock_http_response.status_code = Http_Status_OK;
    
}

/**
 * Cleanup HTTP client mocks
 */
static inline void dap_http_client_mocks_deinit(void) {
    // Free any allocated mock response body
    if (g_mock_http_response.body) {
        free(g_mock_http_response.body);
        g_mock_http_response.body = NULL;
    }
    
    dap_mock_deinit();
}

/**
 * Configure mock response
 */
static inline void dap_http_client_mock_set_response(
    http_status_code_t a_status_code,
    const void *a_body,
    size_t a_body_size,
    struct dap_http_header *a_headers
) {
    g_mock_http_response.status_code = a_status_code;
    g_mock_http_response.should_fail = false;
    g_mock_http_response.error_code = 0;
    
    // Copy body if provided
    if (a_body && a_body_size > 0) {
        if (g_mock_http_response.body) {
            free(g_mock_http_response.body);
        }
        g_mock_http_response.body = malloc(a_body_size);
        memcpy(g_mock_http_response.body, a_body, a_body_size);
        g_mock_http_response.body_size = a_body_size;
    } else {
        g_mock_http_response.body = NULL;
        g_mock_http_response.body_size = 0;
    }
    
    g_mock_http_response.headers = a_headers;
}

/**
 * Configure mock error
 */
static inline void dap_http_client_mock_set_error(int a_error_code) {
    g_mock_http_response.should_fail = true;
    g_mock_http_response.error_code = a_error_code;
}

/**
 * Enable/disable mocking for specific function
 */
static inline void dap_http_client_mock_enable(const char *a_func_name, bool a_enabled) {
    if (strcmp(a_func_name, "dap_client_http_request_full") == 0) {
        dap_mock_set_enabled(g_mock_dap_client_http_request_full, a_enabled);
    } else if (strcmp(a_func_name, "dap_client_http_request") == 0) {
        dap_mock_set_enabled(g_mock_dap_client_http_request, a_enabled);
    } else if (strcmp(a_func_name, "dap_client_http_request_custom") == 0) {
        dap_mock_set_enabled(g_mock_dap_client_http_request_custom, a_enabled);
    } else if (strcmp(a_func_name, "dap_client_http_request_async") == 0) {
        dap_mock_set_enabled(g_mock_dap_client_http_request_async, a_enabled);
    } else if (strcmp(a_func_name, "dap_client_http_request_simple_async") == 0) {
        dap_mock_set_enabled(g_mock_dap_client_http_request_simple_async, a_enabled);
    } else if (strcmp(a_func_name, "dap_client_http_close_unsafe") == 0) {
        dap_mock_set_enabled(g_mock_dap_client_http_close_unsafe, a_enabled);
    }
}

#endif // TEST_HTTP_CLIENT_MOCKS_H

