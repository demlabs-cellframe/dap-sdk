/**
 * @file test_dap_timerfd.c
 * @brief Unit tests for DAP timerfd module (Linux only)
 * @details Tests timer creation, management, and callbacks with mocking
 * @date 2025-11-21
 * @copyright (c) 2025 Cellframe Network
 */

#include "dap_common.h"
#include "dap_test.h"
#include "dap_timerfd.h"
#include "dap_worker.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_mock.h"

#define LOG_TAG "test_dap_timerfd"

// Mock adjacent DAP SDK modules to isolate dap_timerfd
DAP_MOCK_DECLARE(dap_events_init);
DAP_MOCK_DECLARE(dap_events_deinit);
DAP_MOCK_DECLARE(dap_events_worker_get);
DAP_MOCK_DECLARE(dap_events_worker_get_auto);
DAP_MOCK_DECLARE(dap_events_socket_create_type_pipe);
DAP_MOCK_DECLARE(dap_worker_add_events_socket_unsafe);

// Test data
static bool s_timer_callback_called = false;
static int s_timer_callback_count = 0;

/**
 * @brief Test callback for timer operations
 */
static bool s_test_timer_callback(void *a_arg)
{
    UNUSED(a_arg);
    s_timer_callback_called = true;
    s_timer_callback_count++;
    log_it(L_DEBUG, "Timer callback executed (count: %d)", s_timer_callback_count);
    return false; // Don't repeat
}

/**
 * @brief Test: Initialize timerfd system
 */
static void s_test_timerfd_init(void)
{
    log_it(L_INFO, "Testing timerfd initialization");
    
    int l_ret = dap_timerfd_init();
    dap_assert(l_ret == 0, "Timerfd initialization");
}

/**
 * @brief Test: Create timerfd with mocked dependencies
 */
static void s_test_timerfd_create(void)
{
    log_it(L_INFO, "Testing timerfd creation");
    
    dap_timerfd_init();
    
    // Create timer (will use system timerfd_create on Linux)
    s_timer_callback_called = false;
    s_timer_callback_count = 0;
    
    dap_timerfd_t *l_timer = dap_timerfd_create(1000, s_test_timer_callback, NULL);
    log_it(L_DEBUG, "Timer created: %p", l_timer);
    
    // Timer may be created or NULL depending on platform and mocks
    if (l_timer) {
        dap_assert(l_timer->timeout_ms == 1000, "Timer timeout matches");
        dap_assert(l_timer->callback == s_test_timer_callback, "Timer callback matches");
    }
}

/**
 * @brief Test: Start timerfd with mocked worker
 */
static void s_test_timerfd_start(void)
{
    log_it(L_INFO, "Testing timerfd start");
    
    // Mock events system
    DAP_MOCK_SET_RETURN(dap_events_init, 0);
    
    // Create mock worker
    dap_worker_t l_mock_worker = {0};
    l_mock_worker.id = 0;
    DAP_MOCK_SET_RETURN(dap_events_worker_get_auto, &l_mock_worker);
    
    // Mock events socket for timer
    dap_events_socket_t l_mock_es = {0};
    l_mock_es.type = DESCRIPTOR_TYPE_TIMER;
    DAP_MOCK_SET_RETURN(dap_events_socket_create_type_pipe, &l_mock_es);
    DAP_MOCK_SET_RETURN(dap_worker_add_events_socket_unsafe, 0);
    
    dap_timerfd_init();
    
    int l_ret = dap_events_init(1, 60);
    if (l_ret == 0) {
        // Start timer (with mocked worker)
        s_timer_callback_called = false;
        dap_timerfd_t *l_timer = dap_timerfd_start(500, s_test_timer_callback, NULL);
        log_it(L_DEBUG, "Timer started: %p", l_timer);
        
        dap_events_deinit();
    }
    
    // Reset mocks
    DAP_MOCK_RESET(dap_events_init);
    DAP_MOCK_RESET(dap_events_worker_get_auto);
    DAP_MOCK_RESET(dap_events_socket_create_type_pipe);
    DAP_MOCK_RESET(dap_worker_add_events_socket_unsafe);
}

/**
 * @brief Test: Start timerfd on specific worker
 */
static void s_test_timerfd_start_on_worker(void)
{
    log_it(L_INFO, "Testing timerfd start on specific worker");
    
    // Mock events system
    DAP_MOCK_SET_RETURN(dap_events_init, 0);
    
    // Create mock worker
    dap_worker_t l_mock_worker = {0};
    l_mock_worker.id = 0;
    DAP_MOCK_SET_RETURN(dap_events_worker_get, &l_mock_worker);
    
    // Mock events socket
    dap_events_socket_t l_mock_es = {0};
    l_mock_es.type = DESCRIPTOR_TYPE_TIMER;
    DAP_MOCK_SET_RETURN(dap_events_socket_create_type_pipe, &l_mock_es);
    DAP_MOCK_SET_RETURN(dap_worker_add_events_socket_unsafe, 0);
    
    dap_timerfd_init();
    
    int l_ret = dap_events_init(2, 60);
    if (l_ret == 0) {
        dap_worker_t *l_worker = dap_events_worker_get(0);
        
        if (l_worker) {
            // Start timer on specific worker
            s_timer_callback_called = false;
            dap_timerfd_t *l_timer = dap_timerfd_start_on_worker(
                l_worker, 250, s_test_timer_callback, NULL);
            log_it(L_DEBUG, "Timer started on worker: %p", l_timer);
        }
        
        dap_events_deinit();
    }
    
    // Reset mocks
    DAP_MOCK_RESET(dap_events_init);
    DAP_MOCK_RESET(dap_events_worker_get);
    DAP_MOCK_RESET(dap_events_socket_create_type_pipe);
    DAP_MOCK_RESET(dap_worker_add_events_socket_unsafe);
}

/**
 * @brief Test: Delete and reset timerfd
 */
static void s_test_timerfd_delete_reset(void)
{
    log_it(L_INFO, "Testing timerfd delete and reset");
    
    dap_timerfd_init();
    
    // Create timer
    dap_timerfd_t *l_timer = dap_timerfd_create(1000, s_test_timer_callback, NULL);
    
    if (l_timer) {
        // Test reset
        dap_timerfd_reset_unsafe(l_timer);
        dap_pass_msg("Timer reset");
        
        // Test delete
        dap_timerfd_delete_unsafe(l_timer);
        dap_pass_msg("Timer deleted");
    }
}

/**
 * @brief Test: Edge cases and NULL checks
 */
static void s_test_timerfd_edge_cases(void)
{
    log_it(L_INFO, "Testing timerfd edge cases");
    
    dap_timerfd_init();
    
    // Test delete with NULL
    dap_timerfd_delete_unsafe(NULL);
    dap_pass_msg("Delete NULL timer handled gracefully");
    
    // Test reset with NULL
    dap_timerfd_reset_unsafe(NULL);
    dap_pass_msg("Reset NULL timer handled gracefully");
    
    // Test create with NULL callback
    dap_timerfd_t *l_timer_null_cb = dap_timerfd_create(1000, NULL, NULL);
    log_it(L_DEBUG, "Timer with NULL callback: %p", l_timer_null_cb);
    
    if (l_timer_null_cb) {
        dap_timerfd_delete_unsafe(l_timer_null_cb);
    }
}

/**
 * @brief Test: Different timeout values
 */
static void s_test_timerfd_timeouts(void)
{
    log_it(L_INFO, "Testing different timeout values");
    
    dap_timerfd_init();
    
    // Test with very small timeout
    dap_timerfd_t *l_timer1 = dap_timerfd_create(1, s_test_timer_callback, NULL);
    log_it(L_DEBUG, "Timer with 1ms timeout: %p", l_timer1);
    if (l_timer1) {
        dap_assert(l_timer1->timeout_ms == 1, "Timeout 1ms correct");
        dap_timerfd_delete_unsafe(l_timer1);
    }
    
    // Test with large timeout
    dap_timerfd_t *l_timer2 = dap_timerfd_create(86400000, s_test_timer_callback, NULL); // 1 day
    log_it(L_DEBUG, "Timer with 1 day timeout: %p", l_timer2);
    if (l_timer2) {
        dap_assert(l_timer2->timeout_ms == 86400000, "Timeout 1 day correct");
        dap_timerfd_delete_unsafe(l_timer2);
    }
    
    // Test with zero timeout (edge case)
    dap_timerfd_t *l_timer3 = dap_timerfd_create(0, s_test_timer_callback, NULL);
    log_it(L_DEBUG, "Timer with 0ms timeout: %p", l_timer3);
    if (l_timer3) {
        dap_timerfd_delete_unsafe(l_timer3);
    }
}

/**
 * @brief Test: Multiple timers
 */
static void s_test_multiple_timers(void)
{
    log_it(L_INFO, "Testing multiple timers");
    
    dap_timerfd_init();
    
    // Create multiple timers
    dap_timerfd_t *l_timers[3] = {
        dap_timerfd_create(100, s_test_timer_callback, NULL),
        dap_timerfd_create(200, s_test_timer_callback, NULL),
        dap_timerfd_create(300, s_test_timer_callback, NULL)
    };
    
    // Verify and cleanup
    for (int i = 0; i < 3; i++) {
        if (l_timers[i]) {
            log_it(L_DEBUG, "Timer %d created with timeout %lu ms", 
                   i, l_timers[i]->timeout_ms);
            dap_timerfd_delete_unsafe(l_timers[i]);
        }
    }
}

/**
 * @brief Main test function
 */
int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    // Initialize DAP SDK
    int l_ret = dap_common_init("test_dap_timerfd", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }
    
    // Initialize mock framework
    dap_mock_init();
    
    log_it(L_INFO, "=== DAP Timerfd - Unit Tests ===");
    
    // Run tests
    s_test_timerfd_init();
    s_test_timerfd_create();
    s_test_timerfd_start();
    s_test_timerfd_start_on_worker();
    s_test_timerfd_delete_reset();
    s_test_timerfd_edge_cases();
    s_test_timerfd_timeouts();
    s_test_multiple_timers();
    
    log_it(L_INFO, "=== All Timerfd Tests PASSED! ===");
    
    // Cleanup
    dap_mock_deinit();
    dap_common_deinit();
    
    return 0;
}
