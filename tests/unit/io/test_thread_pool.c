/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdatomic.h>
#include "dap_test.h"
#include "dap_thread_pool.h"
#include "dap_common.h"

// Test state
static atomic_int s_task_counter = 0;
static atomic_int s_callback_counter = 0;

/**
 * @brief Simple task function
 */
static void* s_simple_task(void *a_arg)
{
    int l_value = *(int*)a_arg;
    atomic_fetch_add(&s_task_counter, l_value);
    usleep(10000);  // 10ms work
    return (void*)(intptr_t)(l_value * 2);
}

/**
 * @brief Task completion callback
 */
static void s_task_callback(dap_thread_pool_t *a_pool,
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
 * @brief Test thread pool creation and deletion
 */
static void test_thread_pool_create_delete(void)
{
    dap_test_msg("Test: Thread pool create/delete");
    
    // Create pool with 2 threads
    dap_thread_pool_t *l_pool = dap_thread_pool_create(2, 10);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");
    
    // Delete pool
    dap_thread_pool_delete(l_pool);
    
    dap_pass_msg("Thread pool create/delete works");
}

/**
 * @brief Test auto CPU count detection
 */
static void test_thread_pool_auto_cpus(void)
{
    dap_test_msg("Test: Thread pool auto CPU detection");
    
    // Create pool with auto CPU count (0)
    dap_thread_pool_t *l_pool = dap_thread_pool_create(0, 10);
    dap_assert(l_pool != NULL, "Thread pool creation with auto CPUs should succeed");
    
    dap_thread_pool_delete(l_pool);
    
    dap_pass_msg("Thread pool auto CPU detection works");
}

/**
 * @brief Test task submission and execution
 */
static void test_thread_pool_submit_tasks(void)
{
    dap_test_msg("Test: Thread pool submit tasks");
    
    atomic_store(&s_task_counter, 0);
    atomic_store(&s_callback_counter, 0);
    
    dap_thread_pool_t *l_pool = dap_thread_pool_create(2, 100);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");
    
    // Submit 10 tasks
    #define NUM_TASKS 10
    int l_args[NUM_TASKS];
    
    for (int i = 0; i < NUM_TASKS; i++) {
        l_args[i] = i + 1;
        int l_ret = dap_thread_pool_submit(l_pool, s_simple_task, &l_args[i],
                                          s_task_callback, &l_args[i]);
        dap_assert(l_ret == 0, "Task submission should succeed");
    }
    
    // Wait for tasks to complete
    int l_timeout = 0;
    while (atomic_load(&s_callback_counter) < NUM_TASKS && l_timeout < 50) {
        usleep(100000);  // 100ms
        l_timeout++;
    }
    
    dap_assert(atomic_load(&s_callback_counter) == NUM_TASKS,
               "All callbacks should be invoked");
    
    // Sum should be 1+2+3+...+10 = 55
    int l_expected = (NUM_TASKS * (NUM_TASKS + 1)) / 2;
    dap_assert(atomic_load(&s_task_counter) == l_expected,
               "All tasks should execute correctly");
    
    dap_thread_pool_delete(l_pool);
    
    dap_pass_msg("Thread pool task submission works");
}

/**
 * @brief Test pending count
 */
static void test_thread_pool_pending_count(void)
{
    dap_test_msg("Test: Thread pool pending count");
    
    atomic_store(&s_task_counter, 0);
    
    dap_thread_pool_t *l_pool = dap_thread_pool_create(1, 100);  // Single worker
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");
    
    // Submit multiple tasks quickly
    for (int i = 0; i < 5; i++) {
        dap_thread_pool_submit(l_pool, s_simple_task, &(int){i}, NULL, NULL);
    }
    
    // Check pending count (should be > 0 initially)
    uint32_t l_pending = dap_thread_pool_get_pending_count(l_pool);
    dap_test_msg("Pending tasks: %u", l_pending);
    
    // Wait for completion
    usleep(200000);  // 200ms
    
    l_pending = dap_thread_pool_get_pending_count(l_pool);
    dap_assert(l_pending == 0, "All tasks should complete");
    
    dap_thread_pool_delete(l_pool);
    
    dap_pass_msg("Thread pool pending count works");
}

/**
 * @brief Test queue overflow handling
 */
static void test_thread_pool_queue_overflow(void)
{
    dap_test_msg("Test: Thread pool queue overflow");
    
    // Create pool with small queue (2 slots)
    dap_thread_pool_t *l_pool = dap_thread_pool_create(1, 2);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");
    
    // Submit tasks until queue is full
    int l_success = 0;
    int l_failed = 0;
    
    for (int i = 0; i < 10; i++) {
        int l_ret = dap_thread_pool_submit(l_pool, s_simple_task, &(int){1}, NULL, NULL);
        if (l_ret == 0) {
            l_success++;
        } else if (l_ret == -3) {  // Queue full
            l_failed++;
        }
    }
    
    dap_test_msg("Successful submissions: %d, Failed (queue full): %d", l_success, l_failed);
    dap_assert(l_failed > 0, "Some tasks should be rejected when queue is full");
    
    // Wait for tasks to complete
    usleep(200000);  // 200ms
    
    dap_thread_pool_delete(l_pool);
    
    dap_pass_msg("Thread pool queue overflow handling works");
}

/**
 * @brief Test graceful shutdown
 */
static void test_thread_pool_shutdown(void)
{
    dap_test_msg("Test: Thread pool graceful shutdown");
    
    atomic_store(&s_task_counter, 0);
    
    dap_thread_pool_t *l_pool = dap_thread_pool_create(2, 100);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");
    
    // Submit tasks
    for (int i = 0; i < 5; i++) {
        dap_thread_pool_submit(l_pool, s_simple_task, &(int){i + 1}, NULL, NULL);
    }
    
    // Shutdown with timeout
    int l_ret = dap_thread_pool_shutdown(l_pool, 5000);  // 5 second timeout
    dap_assert(l_ret == 0, "Shutdown should succeed");
    
    // All tasks should have completed
    dap_assert(atomic_load(&s_task_counter) == 15, "All tasks should complete before shutdown");
    
    dap_thread_pool_delete(l_pool);
    
    dap_pass_msg("Thread pool graceful shutdown works");
}

/**
 * @brief Test stress with many tasks
 */
static void test_thread_pool_stress(void)
{
    dap_test_msg("Test: Thread pool stress (100 tasks)");
    
    atomic_store(&s_task_counter, 0);
    atomic_store(&s_callback_counter, 0);
    
    dap_thread_pool_t *l_pool = dap_thread_pool_create(4, 200);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");
    
    // Submit 100 tasks
    #define STRESS_TASKS 100
    
    for (int i = 0; i < STRESS_TASKS; i++) {
        int l_ret = dap_thread_pool_submit(l_pool, s_simple_task, &(int){1},
                                          s_task_callback, &(int){1});
        dap_assert(l_ret == 0, "Task submission should succeed");
    }
    
    // Wait for completion with timeout
    int l_timeout = 0;
    while (atomic_load(&s_callback_counter) < STRESS_TASKS && l_timeout < 100) {
        usleep(100000);  // 100ms
        l_timeout++;
    }
    
    dap_assert(atomic_load(&s_callback_counter) == STRESS_TASKS,
               "All callbacks should be invoked");
    dap_assert(atomic_load(&s_task_counter) == STRESS_TASKS,
               "All tasks should execute");
    
    dap_thread_pool_delete(l_pool);
    
    dap_pass_msg("Thread pool stress test passed");
}

/**
 * @brief Main test suite
 */
int main(void)
{
    dap_test_msg("=== DAP Thread Pool Unit Tests ===");
    
    test_thread_pool_create_delete();
    test_thread_pool_auto_cpus();
    test_thread_pool_submit_tasks();
    test_thread_pool_pending_count();
    test_thread_pool_queue_overflow();
    test_thread_pool_shutdown();
    test_thread_pool_stress();
    
    dap_test_msg("=== All Thread Pool Tests Passed ===");
    return 0;
}
