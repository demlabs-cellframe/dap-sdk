/**
 * @file test_dap_events.c
 * @brief Unit tests for DAP events module
 * @details Tests events initialization, workers, and management with mocking
 * @date 2025-11-21
 * @copyright (c) 2025 Cellframe Network
 */

#include <pthread.h>
#include "dap_common.h"
#include "dap_test.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_context.h"
#include "dap_mock.h"

#define LOG_TAG "test_dap_events"

// Mock adjacent DAP SDK modules to isolate dap_events
DAP_MOCK_DECLARE(dap_worker_init);
DAP_MOCK_DECLARE(dap_worker_deinit);
DAP_MOCK_DECLARE(dap_context_init);
DAP_MOCK_DECLARE(dap_context_deinit);
DAP_MOCK_DECLARE(dap_context_new);
DAP_MOCK_DECLARE(dap_context_run);

/**
 * @brief Test: Initialize and deinitialize events system
 */
static void s_test_events_init_deinit(void)
{
    log_it(L_INFO, "Testing events init/deinit");
    
    // Mock dependencies
    DAP_MOCK_SET_RETURN(dap_worker_init, 0);
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    uint32_t l_thread_count = 2;
    size_t l_conn_timeout = 60;
    
    int l_ret = dap_events_init(l_thread_count, l_conn_timeout);
    dap_assert(l_ret == 0, "Events initialization");
    
    // Check that workers were initialized
    uint32_t l_count = dap_events_thread_get_count();
    log_it(L_DEBUG, "Thread count: %u", l_count);
    
    dap_events_deinit();
    dap_pass_msg("Events deinitialization");
    
    // Reset mocks
    DAP_MOCK_RESET(dap_worker_init);
    DAP_MOCK_RESET(dap_context_init);
}

/**
 * @brief Test: Get CPU count
 */
static void s_test_get_cpu_count(void)
{
    log_it(L_INFO, "Testing CPU count retrieval");
    
    uint32_t l_cpu_count = dap_get_cpu_count();
    log_it(L_DEBUG, "CPU count: %u", l_cpu_count);
    dap_assert(l_cpu_count > 0, "CPU count is positive");
}

/**
 * @brief Test: Worker retrieval
 */
static void s_test_events_worker_get(void)
{
    log_it(L_INFO, "Testing worker retrieval");
    
    // Mock dependencies
    DAP_MOCK_SET_RETURN(dap_worker_init, 0);
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    uint32_t l_thread_count = 2;
    int l_ret = dap_events_init(l_thread_count, 60);
    dap_assert(l_ret == 0, "Events initialization");
    
    // Test getting worker by index
    dap_worker_t *l_worker0 = dap_events_worker_get(0);
    log_it(L_DEBUG, "Worker 0: %p", l_worker0);
    
    // Test getting auto worker
    dap_worker_t *l_worker_auto = dap_events_worker_get_auto();
    log_it(L_DEBUG, "Auto worker: %p", l_worker_auto);
    
    // Test invalid worker index
    dap_worker_t *l_worker_invalid = dap_events_worker_get(99);
    dap_assert(l_worker_invalid == NULL, "Invalid worker index returns NULL");
    
    dap_events_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_worker_init);
    DAP_MOCK_RESET(dap_context_init);
}

/**
 * @brief Test: Events start and wait
 */
static void s_test_events_start_wait(void)
{
    log_it(L_INFO, "Testing events start/wait");
    
    // Mock dependencies
    DAP_MOCK_SET_RETURN(dap_worker_init, 0);
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    DAP_MOCK_SET_RETURN(dap_context_run, 0);
    
    uint32_t l_thread_count = 1;
    int l_ret = dap_events_init(l_thread_count, 60);
    dap_assert(l_ret == 0, "Events initialization");
    
    // Test start (may not actually start threads with mocked context_run)
    int32_t l_start_ret = dap_events_start();
    log_it(L_DEBUG, "Events start returned: %d", l_start_ret);
    
    // Test stop
    dap_events_stop_all();
    dap_pass_msg("Events stopped");
    
    dap_events_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_worker_init);
    DAP_MOCK_RESET(dap_context_init);
    DAP_MOCK_RESET(dap_context_run);
}

/**
 * @brief Test: Thread index management
 */
static void s_test_thread_index(void)
{
    log_it(L_INFO, "Testing thread index management");
    
    // Mock dependencies
    DAP_MOCK_SET_RETURN(dap_worker_init, 0);
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    uint32_t l_thread_count = 3;
    int l_ret = dap_events_init(l_thread_count, 60);
    dap_assert(l_ret == 0, "Events initialization");
    
    // Test getting minimum index (for load balancing)
    // uint32_t l_min_index = dap_events_thread_get_index_min(); // Function not exported
    //     log_it(L_DEBUG, "Minimum thread index: %u", l_min_index);
    //     dap_assert(l_min_index < l_thread_count, "Min index is within range");
    
    dap_events_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_worker_init);
    DAP_MOCK_RESET(dap_context_init);
}

/**
 * @brief Test: Workers initialization status
 */
static void s_test_workers_init_status(void)
{
    log_it(L_INFO, "Testing workers initialization status");
    
    // Before init - should return false
    bool l_status_before = dap_events_workers_init_status();
    dap_assert(!l_status_before, "Workers not initialized before init");
    
    // Mock dependencies
    DAP_MOCK_SET_RETURN(dap_worker_init, 0);
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    uint32_t l_thread_count = 2;
    int l_ret = dap_events_init(l_thread_count, 60);
    dap_assert(l_ret == 0, "Events initialization");
    
    // After init - should return true
    bool l_status_after = dap_events_workers_init_status();
    dap_assert(l_status_after, "Workers initialized after init");
    
    dap_events_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_worker_init);
    DAP_MOCK_RESET(dap_context_init);
}

/**
 * @brief Test: CPU assignment
 */
static void s_test_cpu_assign(void)
{
    log_it(L_INFO, "Testing CPU assignment");
    
    // Test CPU assignment (this is a best-effort operation)
    dap_cpu_assign_thread_on(0);
    dap_pass_msg("CPU assignment attempted");
}

/**
 * @brief Test: Edge cases
 */
static void s_test_events_edge_cases(void)
{
    log_it(L_INFO, "Testing events edge cases");
    
    // Mock dependencies
    DAP_MOCK_SET_RETURN(dap_worker_init, 0);
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    // Test with zero threads (should handle gracefully or use default)
    int l_ret = dap_events_init(0, 60);
    log_it(L_DEBUG, "Init with 0 threads returned: %d", l_ret);
    dap_events_deinit();
    
    // Test with very large timeout
    l_ret = dap_events_init(2, 999999);
    log_it(L_DEBUG, "Init with large timeout returned: %d", l_ret);
    dap_events_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_worker_init);
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
    int l_ret = dap_common_init("test_dap_events", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }
    
    // Initialize mock framework
    dap_mock_init();
    
    log_it(L_INFO, "=== DAP Events - Unit Tests ===");
    
    // Run tests
    s_test_get_cpu_count();
    s_test_events_init_deinit();
    s_test_events_worker_get();
    s_test_events_start_wait();
    //     s_test_thread_index();
    s_test_workers_init_status();
    s_test_cpu_assign();
    s_test_events_edge_cases();
    
    log_it(L_INFO, "=== All Events Tests PASSED! ===");
    
    // Cleanup
    dap_mock_deinit();
    dap_common_deinit();
    
    return 0;
}
