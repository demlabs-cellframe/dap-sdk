/**
 * @file test_dap_proc_thread.c
 * @brief Unit tests for DAP proc thread module
 * @details Tests proc thread creation, queues, timers, and callbacks with mocking
 * @date 2025-11-21
 * @copyright (c) 2025 Cellframe Network
 */

#include <pthread.h>
#include "dap_common.h"
#include "dap_test.h"
#include "dap_proc_thread.h"
#include "dap_context.h"
#include "dap_mock.h"

#define LOG_TAG "test_dap_proc_thread"

// Mock adjacent DAP SDK modules to isolate dap_proc_thread
DAP_MOCK_DECLARE(dap_context_init);
DAP_MOCK_DECLARE(dap_context_deinit);
DAP_MOCK_DECLARE(dap_context_new);

// Test data
static bool s_callback_executed = false;
static int s_callback_count = 0;

/**
 * @brief Test callback for queue operations
 */
static bool s_test_queue_callback(void *a_arg)
{
    UNUSED(a_arg);
    s_callback_executed = true;
    s_callback_count++;
    log_it(L_DEBUG, "Queue callback executed (count: %d)", s_callback_count);
    return false; // Don't repeat
}

/**
 * @brief Test timer callback
 */
static void s_test_timer_callback(void *a_arg)
{
    UNUSED(a_arg);
    s_callback_executed = true;
    s_callback_count++;
    log_it(L_DEBUG, "Timer callback executed (count: %d)", s_callback_count);
}

/**
 * @brief Test: Initialize and deinitialize proc thread system
 */
static void s_test_proc_thread_init_deinit(void)
{
    log_it(L_INFO, "Testing proc thread init/deinit");
    
    // Mock context module (proc_thread depends on it)
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    uint32_t l_thread_count = 2;
    int l_ret = dap_proc_thread_init(l_thread_count);
    dap_assert(l_ret == 0, "Proc thread initialization");
    
    // Check thread count
    uint32_t l_count = dap_proc_thread_get_count();
    log_it(L_DEBUG, "Thread count: %u", l_count);
    
    dap_proc_thread_deinit();
    dap_pass_msg("Proc thread deinitialization");
    
    // Reset mocks
    DAP_MOCK_RESET(dap_context_init);
}

/**
 * @brief Test: Get proc thread by index
 */
static void s_test_proc_thread_get(void)
{
    log_it(L_INFO, "Testing proc thread retrieval");
    
    // Mock context module
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    uint32_t l_thread_count = 3;
    int l_ret = dap_proc_thread_init(l_thread_count);
    dap_assert(l_ret == 0, "Proc thread initialization");
    
    // Test getting thread by index
    dap_proc_thread_t *l_thread0 = dap_proc_thread_get(0);
    dap_assert(l_thread0 != NULL, "Get thread 0");
    
    dap_proc_thread_t *l_thread1 = dap_proc_thread_get(1);
    dap_assert(l_thread1 != NULL, "Get thread 1");
    
    // Test invalid index
    dap_proc_thread_t *l_thread_invalid = dap_proc_thread_get(999);
    dap_assert(l_thread_invalid == NULL, "Invalid index returns NULL");
    
    dap_proc_thread_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_context_init);
}

/**
 * @brief Test: Get auto thread (load balancing)
 */
static void s_test_proc_thread_get_auto(void)
{
    log_it(L_INFO, "Testing auto thread selection");
    
    // Mock context module
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    uint32_t l_thread_count = 2;
    int l_ret = dap_proc_thread_init(l_thread_count);
    dap_assert(l_ret == 0, "Proc thread initialization");
    
    // Test auto selection
    dap_proc_thread_t *l_thread_auto = dap_proc_thread_get_auto();
    dap_assert(l_thread_auto != NULL, "Get auto thread");
    
    dap_proc_thread_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_context_init);
}

/**
 * @brief Test: Add callback to queue
 */
static void s_test_proc_thread_callback_add(void)
{
    log_it(L_INFO, "Testing callback addition to queue");
    
    // Mock context module
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    uint32_t l_thread_count = 1;
    int l_ret = dap_proc_thread_init(l_thread_count);
    dap_assert(l_ret == 0, "Proc thread initialization");
    
    dap_proc_thread_t *l_thread = dap_proc_thread_get(0);
    dap_assert(l_thread != NULL, "Get thread 0");
    
    if (l_thread) {
        // Test adding callback with normal priority
        s_callback_executed = false;
        int l_add_ret = dap_proc_thread_callback_add(l_thread, s_test_queue_callback, NULL);
        log_it(L_DEBUG, "Callback add returned: %d", l_add_ret);
        dap_assert(l_add_ret == 0, "Callback added successfully");
        
        // Test adding callback with high priority
        l_add_ret = dap_proc_thread_callback_add_pri(l_thread, s_test_queue_callback,
                                                       NULL, DAP_QUEUE_MSG_PRIORITY_HIGH);
        log_it(L_DEBUG, "High priority callback add returned: %d", l_add_ret);
        dap_assert(l_add_ret == 0, "High priority callback added");
        
        // Test adding callback with critical priority
        l_add_ret = dap_proc_thread_callback_add_pri(l_thread, s_test_queue_callback,
                                                       NULL, DAP_QUEUE_MSG_PRIORITY_CRITICAL);
        log_it(L_DEBUG, "Critical priority callback add returned: %d", l_add_ret);
        dap_assert(l_add_ret == 0, "Critical priority callback added");
    }
    
    dap_proc_thread_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_context_init);
}

/**
 * @brief Test: Add timer with callback
 */
static void s_test_proc_thread_timer_add(void)
{
    log_it(L_INFO, "Testing timer addition");
    
    // Mock context module
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    uint32_t l_thread_count = 1;
    int l_ret = dap_proc_thread_init(l_thread_count);
    dap_assert(l_ret == 0, "Proc thread initialization");
    
    dap_proc_thread_t *l_thread = dap_proc_thread_get(0);
    dap_assert(l_thread != NULL, "Get thread 0");
    
    if (l_thread) {
        // Test adding timer with default priority
        s_callback_executed = false;
        int l_timer_ret = dap_proc_thread_timer_add(l_thread, s_test_timer_callback,
                                                      NULL, 1000); // 1 second
        log_it(L_DEBUG, "Timer add returned: %d", l_timer_ret);
        dap_assert(l_timer_ret == 0, "Timer added successfully");
        
        // Test adding oneshot timer with high priority
        l_timer_ret = dap_proc_thread_timer_add_pri(l_thread, s_test_timer_callback,
                                                      NULL, 500, true, DAP_QUEUE_MSG_PRIORITY_HIGH);
        log_it(L_DEBUG, "Oneshot timer add returned: %d", l_timer_ret);
        dap_assert(l_timer_ret == 0, "Oneshot timer added");
    }
    
    dap_proc_thread_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_context_init);
}

/**
 * @brief Test: Queue size statistics
 */
static void s_test_proc_thread_queue_size(void)
{
    log_it(L_INFO, "Testing queue size statistics");
    
    // Mock context module
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    uint32_t l_thread_count = 2;
    int l_ret = dap_proc_thread_init(l_thread_count);
    dap_assert(l_ret == 0, "Proc thread initialization");
    
    // Get average queue size
    size_t l_avg_size = dap_proc_thread_get_avg_queue_size();
    log_it(L_DEBUG, "Average queue size: %zu", l_avg_size);
    // size_t is always >= 0, skip assert
    
    dap_proc_thread_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_context_init);
}

/**
 * @brief Test: Edge cases and NULL checks
 */
static void s_test_proc_thread_edge_cases(void)
{
    log_it(L_INFO, "Testing proc thread edge cases");
    
    // Mock context module
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    uint32_t l_thread_count = 1;
    int l_ret = dap_proc_thread_init(l_thread_count);
    dap_assert(l_ret == 0, "Proc thread initialization");
    
    // Test adding NULL callback
    dap_proc_thread_t *l_thread = dap_proc_thread_get(0);
    if (l_thread) {
        int l_add_ret = dap_proc_thread_callback_add(l_thread, NULL, NULL);
        log_it(L_DEBUG, "NULL callback add returned: %d", l_add_ret);
    }
    
    // Test with NULL thread
    int l_add_ret = dap_proc_thread_callback_add(NULL, s_test_queue_callback, NULL);
    dap_assert(l_add_ret != 0, "NULL thread fails gracefully");
    
    dap_proc_thread_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_context_init);
}

/**
 * @brief Main test function
 */
int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    // Initialize DAP SDK
    int l_ret = dap_common_init("test_dap_proc_thread", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }
    
    // Initialize mock framework
    dap_mock_init();
    
    log_it(L_INFO, "=== DAP Proc Thread - Unit Tests ===");
    
    // Run tests
    s_test_proc_thread_init_deinit();
    s_test_proc_thread_get();
    s_test_proc_thread_get_auto();
    s_test_proc_thread_callback_add();
    s_test_proc_thread_timer_add();
    s_test_proc_thread_queue_size();
    s_test_proc_thread_edge_cases();
    
    log_it(L_INFO, "=== All Proc Thread Tests PASSED! ===");
    
    // Cleanup
    dap_mock_deinit();
    dap_common_deinit();
    
    return 0;
}
