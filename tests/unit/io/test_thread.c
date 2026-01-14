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
#include "dap_thread.h"
#include "dap_common.h"

// Test thread state
static atomic_int s_test_counter = 0;
static atomic_bool s_test_flag = false;

/**
 * @brief Simple thread function for testing
 */
static void* s_test_thread_func(void *a_arg)
{
    int l_value = *(int*)a_arg;
    
    // Increment counter
    atomic_fetch_add(&s_test_counter, l_value);
    
    // Set flag
    atomic_store(&s_test_flag, true);
    
    // Sleep a bit to test join
    usleep(100000);  // 100ms
    
    return (void*)(intptr_t)42;
}

/**
 * @brief Thread function that sets its own name
 */
static void* s_test_thread_setname_func(void *a_arg)
{
    UNUSED(a_arg);
    
    // Try to set name from within thread
    dap_thread_t l_self = pthread_self();
    int l_ret = dap_thread_set_name(l_self, "self_named");
    
    return (void*)(intptr_t)l_ret;
}

/**
 * @brief Test thread creation and join
 */
static void test_thread_create_join(void)
{
    dap_test_msg("Test: Thread create and join");
    
    // Reset state
    atomic_store(&s_test_counter, 0);
    atomic_store(&s_test_flag, false);
    
    int l_arg = 10;
    dap_thread_t l_thread = dap_thread_create(s_test_thread_func, &l_arg);
    
    dap_assert(l_thread != 0, "Thread creation should succeed");
    
    // Wait for thread
    void *l_retval = NULL;
    int l_ret = dap_thread_join(l_thread, &l_retval);
    
    dap_assert(l_ret == 0, "Thread join should succeed");
    dap_assert((intptr_t)l_retval == 42, "Thread should return 42");
    dap_assert(atomic_load(&s_test_counter) == 10, "Counter should be incremented");
    dap_assert(atomic_load(&s_test_flag) == true, "Flag should be set");
    
    dap_pass_msg("Thread create/join works correctly");
}

/**
 * @brief Test thread ID retrieval
 */
static void test_thread_get_id(void)
{
    dap_test_msg("Test: Thread get ID");
    
    dap_thread_id_t l_main_id = dap_thread_get_id();
    dap_assert(l_main_id != 0, "Main thread should have valid ID");
    
    dap_pass_msg("Thread ID retrieval works");
}

/**
 * @brief Test thread naming
 */
static void test_thread_set_name(void)
{
    dap_test_msg("Test: Thread set name");
    
    dap_thread_t l_thread = dap_thread_create(s_test_thread_setname_func, NULL);
    dap_assert(l_thread != 0, "Thread creation should succeed");
    
    // Set name from parent thread
    int l_ret = dap_thread_set_name(l_thread, "test_thread");
    // Note: On macOS this might fail, but that's OK
    
    void *l_retval = NULL;
    dap_thread_join(l_thread, &l_retval);
    
    dap_pass_msg("Thread naming completed (platform-specific behavior)");
}

/**
 * @brief Test thread detach
 */
static void test_thread_detach(void)
{
    dap_test_msg("Test: Thread detach");
    
    // Reset state
    atomic_store(&s_test_counter, 0);
    
    int l_arg = 5;
    dap_thread_t l_thread = dap_thread_create(s_test_thread_func, &l_arg);
    dap_assert(l_thread != 0, "Thread creation should succeed");
    
    // Detach thread
    int l_ret = dap_thread_detach(l_thread);
    dap_assert(l_ret == 0, "Thread detach should succeed");
    
    // Give thread time to complete
    usleep(200000);  // 200ms
    
    dap_assert(atomic_load(&s_test_counter) == 5, "Detached thread should execute");
    
    dap_pass_msg("Thread detach works correctly");
}

/**
 * @brief Test CPU affinity
 */
static void test_thread_affinity(void)
{
    dap_test_msg("Test: Thread CPU affinity");
    
    dap_thread_t l_thread = dap_thread_create(s_test_thread_func, &(int){1});
    dap_assert(l_thread != 0, "Thread creation should succeed");
    
    // Try to set affinity to CPU 0
    int l_ret = dap_thread_set_affinity(l_thread, 0);
    // Note: This might fail on some platforms, which is OK
    if (l_ret == 0) {
        dap_test_msg("CPU affinity set successfully");
    } else if (l_ret == -3) {
        dap_test_msg("CPU affinity not supported on this platform");
    }
    
    dap_thread_join(l_thread, NULL);
    
    dap_pass_msg("Thread CPU affinity test completed");
}

/**
 * @brief Test multiple threads
 */
static void test_thread_multiple(void)
{
    dap_test_msg("Test: Multiple threads");
    
    // Reset state
    atomic_store(&s_test_counter, 0);
    
    #define NUM_THREADS 10
    dap_thread_t l_threads[NUM_THREADS];
    int l_args[NUM_THREADS];
    
    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        l_args[i] = i + 1;
        l_threads[i] = dap_thread_create(s_test_thread_func, &l_args[i]);
        dap_assert(l_threads[i] != 0, "Thread creation should succeed");
    }
    
    // Join all threads
    for (int i = 0; i < NUM_THREADS; i++) {
        int l_ret = dap_thread_join(l_threads[i], NULL);
        dap_assert(l_ret == 0, "Thread join should succeed");
    }
    
    // Sum should be 1+2+3+...+10 = 55
    int l_expected = (NUM_THREADS * (NUM_THREADS + 1)) / 2;
    dap_assert(atomic_load(&s_test_counter) == l_expected,
               "Counter should be sum of 1..10");
    
    dap_pass_msg("Multiple threads work correctly");
}

/**
 * @brief Main test suite
 */
int main(void)
{
    dap_test_msg("=== DAP Thread Unit Tests ===");
    
    test_thread_create_join();
    test_thread_get_id();
    test_thread_set_name();
    test_thread_detach();
    test_thread_affinity();
    test_thread_multiple();
    
    dap_test_msg("=== All Thread Tests Passed ===");
    return 0;
}
