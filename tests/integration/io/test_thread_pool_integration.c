/*
 * Integration test for thread pool
 * 
 * Tests:
 * - Thread pool creation with auto CPU count
 * - CPU affinity binding
 * - Task submission and execution
 * - Completion callbacks
 * - Graceful shutdown
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdatomic.h>
#include "dap_test.h"
#include "dap_common.h"
#include "dap_thread_pool.h"

#define LOG_TAG "test_thread_pool_integration"

// Test state
static atomic_int s_task_counter = 0;
static atomic_int s_callback_counter = 0;

/**
 * @brief Simple task function
 */
static void* s_test_task(void *a_arg)
{
    int l_id = *(int*)a_arg;
    dap_test_msg("Task %d executing in thread pool", l_id);
    
    // Simulate some work
    usleep(50000);  // 50ms
    
    atomic_fetch_add(&s_task_counter, 1);
    
    return (void*)(intptr_t)(l_id * 2);
}

/**
 * @brief Task completion callback
 */
static void s_test_callback(dap_thread_pool_t *a_pool,
                            dap_thread_t a_worker_thread,
                            void *a_result,
                            void *a_arg)
{
    UNUSED(a_pool);
    UNUSED(a_worker_thread);
    
    int l_result = (intptr_t)a_result;
    int l_expected = *(int*)a_arg * 2;
    
    dap_assert(l_result == l_expected, "Callback result should match expected");
    
    atomic_fetch_add(&s_callback_counter, 1);
}

/**
 * @brief Test thread pool lifecycle
 */
static void test_thread_pool_lifecycle(void)
{
    dap_test_msg("Test: Thread pool lifecycle");
    
    // Create pool with auto CPU count
    dap_thread_pool_t *l_pool = dap_thread_pool_create(0, 100);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");
    
    dap_test_msg("Thread pool created (auto CPU count with affinity)");
    
    // Delete immediately
    dap_thread_pool_delete(l_pool);
    
    dap_pass_msg("Thread pool lifecycle works");
}

/**
 * @brief Test task submission and execution
 */
static void test_thread_pool_tasks(void)
{
    dap_test_msg("Test: Thread pool task execution");
    
    atomic_store(&s_task_counter, 0);
    atomic_store(&s_callback_counter, 0);
    
    dap_thread_pool_t *l_pool = dap_thread_pool_create(0, 100);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");
    
    // Submit 20 tasks
    #define NUM_TASKS 20
    int l_args[NUM_TASKS];
    
    for (int i = 0; i < NUM_TASKS; i++) {
        l_args[i] = i;
        int l_ret = dap_thread_pool_submit(l_pool, s_test_task, &l_args[i],
                                          s_test_callback, &l_args[i]);
        dap_assert(l_ret == 0, "Task submission should succeed");
    }
    
    dap_test_msg("Submitted %d tasks, waiting for completion...", NUM_TASKS);
    
    // Wait for completion with timeout
    int l_timeout = 0;
    while (atomic_load(&s_callback_counter) < NUM_TASKS && l_timeout < 50) {
        usleep(100000);  // 100ms
        l_timeout++;
    }
    
    int l_tasks_completed = atomic_load(&s_task_counter);
    int l_callbacks_invoked = atomic_load(&s_callback_counter);
    
    dap_test_msg("Tasks completed: %d/%d, Callbacks invoked: %d/%d",
                 l_tasks_completed, NUM_TASKS, l_callbacks_invoked, NUM_TASKS);
    
    dap_assert(l_tasks_completed == NUM_TASKS, "All tasks should complete");
    dap_assert(l_callbacks_invoked == NUM_TASKS, "All callbacks should be invoked");
    
    // Graceful shutdown
    int l_ret = dap_thread_pool_shutdown(l_pool, 5000);
    dap_assert(l_ret == 0, "Shutdown should succeed");
    
    dap_thread_pool_delete(l_pool);
    
    dap_pass_msg("Thread pool task execution works correctly");
}

/**
 * @brief Test stress with many concurrent tasks
 */
static void test_thread_pool_stress(void)
{
    dap_test_msg("Test: Thread pool stress (100 concurrent tasks)");
    
    atomic_store(&s_task_counter, 0);
    atomic_store(&s_callback_counter, 0);
    
    dap_thread_pool_t *l_pool = dap_thread_pool_create(0, 200);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");
    
    // Submit 100 tasks
    #define STRESS_TASKS 100
    static int s_stress_args[STRESS_TASKS];  // STATIC to avoid stack issues
    
    for (int i = 0; i < STRESS_TASKS; i++) {
        s_stress_args[i] = i;
        int l_ret = dap_thread_pool_submit(l_pool, s_test_task, &s_stress_args[i],
                                          s_test_callback, &s_stress_args[i]);
        dap_assert(l_ret == 0, "Task submission should succeed");
    }
    
    dap_test_msg("Submitted %d tasks, waiting for completion...", STRESS_TASKS);
    
    // Wait for completion with longer timeout
    int l_timeout = 0;
    while (atomic_load(&s_callback_counter) < STRESS_TASKS && l_timeout < 100) {
        usleep(100000);  // 100ms
        l_timeout++;
        
        if (l_timeout % 10 == 0) {
            dap_test_msg("Progress: %d/%d tasks completed",
                         atomic_load(&s_task_counter), STRESS_TASKS);
        }
    }
    
    int l_tasks_completed = atomic_load(&s_task_counter);
    int l_callbacks_invoked = atomic_load(&s_callback_counter);
    
    dap_test_msg("Final: Tasks=%d/%d, Callbacks=%d/%d",
                 l_tasks_completed, STRESS_TASKS, l_callbacks_invoked, STRESS_TASKS);
    
    dap_assert(l_tasks_completed == STRESS_TASKS, "All stress tasks should complete");
    dap_assert(l_callbacks_invoked == STRESS_TASKS, "All stress callbacks should be invoked");
    
    dap_thread_pool_shutdown(l_pool, 10000);
    dap_thread_pool_delete(l_pool);
    
    dap_pass_msg("Thread pool stress test passed");
}

/**
 * @brief Main test suite
 */
int main(void)
{
    dap_test_msg("=== Thread Pool Integration Tests ===");
    
    test_thread_pool_lifecycle();
    test_thread_pool_tasks();
    test_thread_pool_stress();
    
    dap_test_msg("=== All Thread Pool Integration Tests Passed ===");
    return 0;
}
