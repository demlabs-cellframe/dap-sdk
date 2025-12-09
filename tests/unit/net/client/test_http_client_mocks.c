/**
 * @file test_http_client_mocks.c
 * @brief Implementation of HTTP Client Mock Framework
 * @details Linker wrapper implementations for HTTP client functions
 * 
 * @date 2025-10-27
 * @copyright (c) 2025 Cellframe Network
 */

#include "test_http_client_mocks.h"
#include "dap_common.h"
#include "dap_time.h"
#include <unistd.h>
#include <pthread.h>

#define LOG_TAG "http_client_mocks"
/**
 * Global mock response configuration
 */
dap_http_client_mock_response_t g_mock_http_response = {0};


/**
 * Simulated async callback trigger
 * This simulates async behavior by invoking callbacks after a delay
 */
static void* mock_async_callback_thread(void *a_arg) {
    typedef struct {
        dap_client_http_callback_full_t response_cb;
        dap_client_http_callback_error_t error_cb;
        void *cb_arg;
    } mock_async_context_t;
    
    mock_async_context_t *l_ctx = (mock_async_context_t *)a_arg;
    
    // Simulate network delay
    if (g_mock_http_response.delay_ms > 0) {
        usleep(g_mock_http_response.delay_ms * 1000);
    }
    
    // Trigger appropriate callback
    if (g_mock_http_response.should_fail && l_ctx->error_cb) {
        l_ctx->error_cb(g_mock_http_response.error_code, l_ctx->cb_arg);
    } else if (l_ctx->response_cb) {
        l_ctx->response_cb(
            g_mock_http_response.body,
            g_mock_http_response.body_size,
            g_mock_http_response.headers,
            l_ctx->cb_arg,
            g_mock_http_response.status_code
        );
    }
    
    free(l_ctx);
    return NULL;
}

/**
 * Linker Wrapper: dap_client_http_request_full
 */
DAP_MOCK_WRAPPER_CUSTOM(dap_client_http_t*, dap_client_http_request_full,
    PARAM(dap_worker_t*, a_worker),
    PARAM(const char*, a_uplink_addr),
    PARAM(uint16_t, a_uplink_port),
    PARAM(const char*, a_method),
    PARAM(const char*, a_request_content_type),
    PARAM(const char*, a_path),
    PARAM(const void*, a_request),
    PARAM(size_t, a_request_size),
    PARAM(char*, a_cookie),
    PARAM(dap_client_http_callback_full_t, a_response_callback),
    PARAM(dap_client_http_callback_error_t, a_error_callback),
    PARAM(void*, a_callbacks_arg),
    PARAM(char*, a_custom_headers),
    PARAM(bool, a_follow_redirects)
)
{
    // Trigger callbacks based on mock configuration
    if (g_mock_http_response.should_fail && a_error_callback) {
        a_error_callback(g_mock_http_response.error_code, a_callbacks_arg);
    } else if (a_response_callback) {
        a_response_callback(
            g_mock_http_response.body,
            g_mock_http_response.body_size,
            g_mock_http_response.headers,
            a_callbacks_arg,
            g_mock_http_response.status_code
        );
    }
    
    // Return mock client object
    return (dap_client_http_t*)g_mock_dap_client_http_request_full->return_value.ptr;
}

/**
 * Linker Wrapper: dap_client_http_request
 */
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
    // Trigger callbacks
    if (g_mock_http_response.should_fail && a_error_callback) {
        a_error_callback(g_mock_http_response.error_code, a_callbacks_arg);
    } else if (a_response_callback) {
        a_response_callback(
            g_mock_http_response.body,
            g_mock_http_response.body_size,
            a_callbacks_arg,
            g_mock_http_response.status_code
        );
    }
    
    return (dap_client_http_t*)g_mock_dap_client_http_request->return_value.ptr;
}

/**
 * Linker Wrapper: dap_client_http_request_async
 */
DAP_MOCK_WRAPPER_CUSTOM(void, dap_client_http_request_async,
    PARAM(dap_worker_t*, a_worker),
    PARAM(const char*, a_uplink_addr),
    PARAM(uint16_t, a_uplink_port),
    PARAM(const char*, a_method),
    PARAM(const char*, a_request_content_type),
    PARAM(const char*, a_path),
    PARAM(const void*, a_request),
    PARAM(size_t, a_request_size),
    PARAM(char*, a_cookie),
    PARAM(dap_client_http_callback_full_t, a_response_callback),
    PARAM(dap_client_http_callback_error_t, a_error_callback),
    PARAM(dap_client_http_callback_started_t, a_started_callback),
    PARAM(dap_client_http_callback_progress_t, a_progress_callback),
    PARAM(void*, a_callbacks_arg),
    PARAM(char*, a_custom_headers),
    PARAM(bool, a_follow_redirects)
)
{
    // Call started callback immediately
    if (a_started_callback) {
        a_started_callback(a_callbacks_arg);
    }
    
    // Simulate async callback in separate thread
    typedef struct {
        dap_client_http_callback_full_t response_cb;
        dap_client_http_callback_error_t error_cb;
        void *cb_arg;
    } mock_async_context_t;
    
    mock_async_context_t *l_ctx = malloc(sizeof(mock_async_context_t));
    l_ctx->response_cb = a_response_callback;
    l_ctx->error_cb = a_error_callback;
    l_ctx->cb_arg = a_callbacks_arg;
    
    pthread_t l_thread;
    pthread_create(&l_thread, NULL, mock_async_callback_thread, l_ctx);
    pthread_detach(l_thread);
}

/**
 * Linker Wrapper: dap_client_http_request_simple_async
 */
DAP_MOCK_WRAPPER_CUSTOM(void, dap_client_http_request_simple_async,
    PARAM(dap_worker_t*, a_worker),
    PARAM(const char*, a_uplink_addr),
    PARAM(uint16_t, a_uplink_port),
    PARAM(const char*, a_method),
    PARAM(const char*, a_request_content_type),
    PARAM(const char*, a_path),
    PARAM(const void*, a_request),
    PARAM(size_t, a_request_size),
    PARAM(char*, a_cookie),
    PARAM(dap_client_http_callback_full_t, a_response_callback),
    PARAM(dap_client_http_callback_error_t, a_error_callback),
    PARAM(void*, a_callbacks_arg),
    PARAM(char*, a_custom_headers),
    PARAM(bool, a_follow_redirects)
)
{
    // Simulate async callback
    typedef struct {
        dap_client_http_callback_full_t response_cb;
        dap_client_http_callback_error_t error_cb;
        void *cb_arg;
    } mock_async_context_t;
    
    mock_async_context_t *l_ctx = DAP_NEW_Z( mock_async_context_t);
    l_ctx->response_cb = a_response_callback;
    l_ctx->error_cb = a_error_callback;
    l_ctx->cb_arg = a_callbacks_arg;
    
    pthread_t l_thread;
    pthread_create(&l_thread, NULL, mock_async_callback_thread, l_ctx);
    pthread_detach(l_thread);
}

/**
 * Linker Wrapper: dap_client_http_close_unsafe
 */
DAP_MOCK_WRAPPER_CUSTOM(void, dap_client_http_close_unsafe,
    PARAM(dap_client_http_t*, a_client_http)
)
{
    // Mock close - just free the fake client object
    if (a_client_http) {
        free(a_client_http);
    }
}
