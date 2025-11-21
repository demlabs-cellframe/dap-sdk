/**
 * @file test_mock_framework.c
 * @brief Unit tests for DAP SDK Mock Framework V4
 * @details Comprehensive tests for all mock framework features:
 *          - Mock declaration with structured config
 *          - Enable/disable mocks
 *          - Return value configuration (union-based)
 *          - Call counting and recording
 *          - Delay execution (fixed, range, variance)
 *          - Custom callbacks
 *          - Thread safety
 * 
 * @date 2025-10-27
 * @copyright (c) 2025 Demlabs
 */

#include "dap_test.h"
#include "dap_test_async.h"
#include "dap_mock.h"
#include "dap_common.h"
#include <pthread.h>
#include <string.h>
#include <stdint.h>

#define LOG_TAG "test_mock"

// =============================================================================
// MOCK DECLARATIONS FOR TESTING
// =============================================================================

// Test 1: Simple mock with default config
DAP_MOCK_DECLARE(simple_function);

// Test 2: Mock with custom return value
DAP_MOCK_DECLARE(function_with_return, {
    .return_value.i = 42
});

// Test 3: Mock with pointer return
DAP_MOCK_DECLARE(function_returns_ptr, {
    .return_value.ptr = (void*)0xDEADBEEF
});

// Test 4: Mock for delay tests (delay will be set at runtime)
DAP_MOCK_DECLARE(function_with_delay, {
    .return_value.i = 100
});

// Test 5: Mock with callback
DAP_MOCK_DECLARE(function_with_callback, {
    .return_value.i = 0
}, {
    // Callback: multiply first two args
    if (a_arg_count >= 2) {
        int a = (int)(intptr_t)a_args[0];
        int b = (int)(intptr_t)a_args[1];
        return (void*)(intptr_t)(a * b);
    }
    return (void*)(intptr_t)0;
});

// Test 6: Mock initially disabled (configured at runtime)
DAP_MOCK_DECLARE(disabled_function);

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

static void reset_all_test_mocks(void)
{
    DAP_MOCK_RESET(simple_function);
    DAP_MOCK_RESET(function_with_return);
    DAP_MOCK_RESET(function_returns_ptr);
    DAP_MOCK_RESET(function_with_delay);
    DAP_MOCK_RESET(function_with_callback);
    DAP_MOCK_RESET(disabled_function);
}

// =============================================================================
// BASIC MOCK TESTS
// =============================================================================

static void test_mock_declaration_defaults(void)
{
    log_it(L_INFO, "=== Test 1: Mock Declaration Defaults ===");
    
    reset_all_test_mocks();
    
    // Simple function should be enabled by default
    dap_assert_PIF(g_mock_simple_function->enabled == true,
                   "Mock should be enabled by default");
    
    // Default return value should be 0
    int l_ret = (int)(intptr_t)g_mock_simple_function->return_value.ptr;
    log_it(L_DEBUG, "Default return value: %d", l_ret);
    dap_assert_PIF(l_ret == 0, "Default return should be 0");
    
    // Call count should be 0
    int l_count = DAP_MOCK_GET_CALL_COUNT(simple_function);
    dap_assert_PIF(l_count == 0, "Initial call count should be 0");
    
    log_it(L_INFO, "✓ Test 1: Declaration Defaults PASSED\n");
}

static void test_mock_custom_return_values(void)
{
    log_it(L_INFO, "=== Test 2: Custom Return Values ===");
    
    reset_all_test_mocks();
    
    // Test int return value
    int l_int_ret = g_mock_function_with_return->return_value.i;
    log_it(L_DEBUG, "Custom int return: %d (expected: 42)", l_int_ret);
    dap_assert_PIF(l_int_ret == 42, "Should use custom int return value");
    
    // Test pointer return value
    void *l_ptr_ret = g_mock_function_returns_ptr->return_value.ptr;
    log_it(L_DEBUG, "Custom ptr return: %p (expected: 0xDEADBEEF)", l_ptr_ret);
    dap_assert_PIF(l_ptr_ret == (void*)0xDEADBEEF,
                   "Should use custom pointer return value");
    
    log_it(L_INFO, "✓ Test 2: Custom Return Values PASSED\n");
}

static void test_mock_enable_disable(void)
{
    log_it(L_INFO, "=== Test 3: Enable/Disable ===");
    
    reset_all_test_mocks();
    
    // Initially enabled mock
    dap_assert_PIF(g_mock_simple_function->enabled == true,
                   "Should be enabled by default");
    
    // Disable
    DAP_MOCK_DISABLE(simple_function);
    dap_assert_PIF(g_mock_simple_function->enabled == false,
                   "Should be disabled after DAP_MOCK_DISABLE");
    
    // Enable again
    DAP_MOCK_ENABLE(simple_function);
    dap_assert_PIF(g_mock_simple_function->enabled == true,
                   "Should be enabled after DAP_MOCK_ENABLE");
    
    // Test disabling from enabled state
    DAP_MOCK_DISABLE(disabled_function);
    g_mock_disabled_function->return_value.i = 99;
    
    dap_assert_PIF(g_mock_disabled_function->enabled == false,
                   "disabled_function should be disabled");
    dap_assert_PIF(g_mock_disabled_function->return_value.i == 99,
                   "Should keep custom return value even when disabled");
    
    log_it(L_INFO, "✓ Test 3: Enable/Disable PASSED\n");
}

// =============================================================================
// CALL COUNTING TESTS
// =============================================================================

static void test_mock_call_counting(void)
{
    log_it(L_INFO, "=== Test 4: Call Counting ===");
    
    reset_all_test_mocks();
    
    // Initial count
    int l_count = DAP_MOCK_GET_CALL_COUNT(simple_function);
    dap_assert_PIF(l_count == 0, "Initial count should be 0");
    
    // Simulate calls
    void *l_args1[2] = {(void*)1, (void*)2};
    dap_mock_record_call(g_mock_simple_function, l_args1, 2, (void*)10);
    
    void *l_args2[1] = {(void*)5};
    dap_mock_record_call(g_mock_simple_function, l_args2, 1, (void*)20);
    
    dap_mock_record_call(g_mock_simple_function, NULL, 0, (void*)30);
    
    l_count = DAP_MOCK_GET_CALL_COUNT(simple_function);
    log_it(L_DEBUG, "After 3 calls: count=%d", l_count);
    dap_assert_PIF(l_count == 3, "Should count 3 calls");
    
    // Check last call
    dap_mock_call_record_t *l_last = dap_mock_get_last_call(g_mock_simple_function);
    dap_assert_PIF(l_last != NULL, "Should have last call record");
    dap_assert_PIF(l_last->return_value == (void*)30,
                   "Last call return should be 30");
    
    // Reset and verify
    DAP_MOCK_RESET(simple_function);
    l_count = DAP_MOCK_GET_CALL_COUNT(simple_function);
    dap_assert_PIF(l_count == 0, "Count should be 0 after reset");
    
    log_it(L_INFO, "✓ Test 4: Call Counting PASSED\n");
}

static void test_mock_call_arguments(void)
{
    log_it(L_INFO, "=== Test 5: Call Arguments ===");
    
    reset_all_test_mocks();
    
    // Record call with specific arguments
    void *l_args[3] = {(void*)10, (void*)20, (void*)30};
    dap_mock_record_call(g_mock_simple_function, l_args, 3, (void*)100);
    
    // Retrieve arguments
    void **l_retrieved = dap_mock_get_call_args(g_mock_simple_function, 0);
    dap_assert_PIF(l_retrieved != NULL, "Should retrieve call args");
    
    dap_assert_PIF(l_retrieved[0] == (void*)10, "Arg 0 should be 10");
    dap_assert_PIF(l_retrieved[1] == (void*)20, "Arg 1 should be 20");
    dap_assert_PIF(l_retrieved[2] == (void*)30, "Arg 2 should be 30");
    
    log_it(L_DEBUG, "Retrieved args: %d, %d, %d",
           (int)(intptr_t)l_retrieved[0],
           (int)(intptr_t)l_retrieved[1],
           (int)(intptr_t)l_retrieved[2]);
    
    log_it(L_INFO, "✓ Test 5: Call Arguments PASSED\n");
}

// =============================================================================
// DELAY TESTS
// =============================================================================

static void test_mock_delay_fixed(void)
{
    log_it(L_INFO, "=== Test 6: Fixed Delay ===");
    
    reset_all_test_mocks();
    
    // Set 100ms fixed delay
    DAP_MOCK_SET_DELAY_FIXED(function_with_delay, 100000);
    
    uint64_t l_start = dap_test_get_time_ms();
    dap_mock_execute_delay(g_mock_function_with_delay);
    uint64_t l_elapsed = dap_test_get_time_ms() - l_start;
    
    log_it(L_DEBUG, "Fixed delay elapsed: %llu ms (expected: ~100ms)",
           (unsigned long long)l_elapsed);
    
    dap_assert_PIF(l_elapsed >= 90 && l_elapsed <= 150,
                   "Delay should be ~100ms (+/- tolerance)");
    
    log_it(L_INFO, "✓ Test 6: Fixed Delay PASSED\n");
}

static void test_mock_delay_range(void)
{
    log_it(L_INFO, "=== Test 7: Range Delay ===");
    
    reset_all_test_mocks();
    
    // Set range delay 50-150ms
    DAP_MOCK_SET_DELAY_RANGE(simple_function, 50000, 150000);
    
    // Test multiple times to verify randomness
    int l_in_range_count = 0;
    int l_total_tests = 5;
    
    for (int i = 0; i < l_total_tests; i++) {
        uint64_t l_start = dap_test_get_time_ms();
        dap_mock_execute_delay(g_mock_simple_function);
        uint64_t l_elapsed = dap_test_get_time_ms() - l_start;
        
        log_it(L_DEBUG, "Range delay #%d: %llu ms (range: 50-150ms)",
               i+1, (unsigned long long)l_elapsed);
        
        // Allow wider tolerance for timing variance
        if (l_elapsed >= 30 && l_elapsed <= 250) {
            l_in_range_count++;
        }
    }
    
    log_it(L_DEBUG, "In-range delays: %d/%d", l_in_range_count, l_total_tests);
    
    dap_assert_PIF(l_in_range_count >= 3,
                   "At least 3/5 delays should be in acceptable range");
    
    log_it(L_INFO, "✓ Test 7: Range Delay PASSED\n");
}

static void test_mock_delay_variance(void)
{
    log_it(L_INFO, "=== Test 8: Variance Delay ===");
    
    reset_all_test_mocks();
    
    // Set variance delay: 100ms ± 20ms (range: 80-120ms)
    DAP_MOCK_SET_DELAY_VARIANCE(simple_function, 100000, 20000);
    
    int l_in_range_count = 0;
    int l_total_tests = 5;
    
    for (int i = 0; i < l_total_tests; i++) {
        uint64_t l_start = dap_test_get_time_ms();
        dap_mock_execute_delay(g_mock_simple_function);
        uint64_t l_elapsed = dap_test_get_time_ms() - l_start;
        
        log_it(L_DEBUG, "Variance delay #%d: %llu ms (expected: 80-120ms)",
               i+1, (unsigned long long)l_elapsed);
        
        // Allow wider tolerance: 60-160ms
        if (l_elapsed >= 60 && l_elapsed <= 160) {
            l_in_range_count++;
        }
    }
    
    log_it(L_DEBUG, "In-range delays: %d/%d", l_in_range_count, l_total_tests);
    
    dap_assert_PIF(l_in_range_count >= 3,
                   "At least 3/5 delays should be within variance range");
    
    log_it(L_INFO, "✓ Test 8: Variance Delay PASSED\n");
}

// =============================================================================
// CALLBACK TESTS
// =============================================================================

static void test_mock_custom_callback(void)
{
    log_it(L_INFO, "=== Test 9: Custom Callback ===");
    
    reset_all_test_mocks();
    
    // function_with_callback multiplies first two args
    void *l_args[2] = {(void*)5, (void*)7};
    
    void *l_result = dap_mock_execute_callback(g_mock_function_with_callback,
                                                l_args, 2);
    int l_result_int = (int)(intptr_t)l_result;
    
    log_it(L_DEBUG, "Callback result: %d (expected: 5*7=35)", l_result_int);
    dap_assert_PIF(l_result_int == 35, "Callback should multiply args");
    
    // Test with different args
    l_args[0] = (void*)3;
    l_args[1] = (void*)4;
    l_result = dap_mock_execute_callback(g_mock_function_with_callback,
                                         l_args, 2);
    l_result_int = (int)(intptr_t)l_result;
    
    log_it(L_DEBUG, "Callback result: %d (expected: 3*4=12)", l_result_int);
    dap_assert_PIF(l_result_int == 12, "Callback should work with new args");
    
    log_it(L_INFO, "✓ Test 9: Custom Callback PASSED\n");
}

// Runtime callback helper
static void* runtime_callback_impl(void **a_args, int a_arg_count, void *a_user_data)
{
    UNUSED(a_user_data);
    if (a_arg_count >= 1) {
        int val = (int)(intptr_t)a_args[0];
        return (void*)(intptr_t)(val * 10);  // Multiply by 10
    }
    return (void*)0;
}

static void test_mock_runtime_callback(void)
{
    log_it(L_INFO, "=== Test 10: Runtime Callback Assignment ===");
    
    reset_all_test_mocks();
    
    // Set callback at runtime
    DAP_MOCK_SET_CALLBACK(simple_function, runtime_callback_impl, NULL);
    
    void *l_args[1] = {(void*)7};
    void *l_result = dap_mock_execute_callback(g_mock_simple_function, l_args, 1);
    int l_result_int = (int)(intptr_t)l_result;
    
    log_it(L_DEBUG, "Runtime callback result: %d (expected: 7*10=70)", l_result_int);
    dap_assert_PIF(l_result_int == 70, "Runtime callback should work");
    
    // Clear callback
    DAP_MOCK_CLEAR_CALLBACK(simple_function);
    
    log_it(L_INFO, "✓ Test 10: Runtime Callback PASSED\n");
}

// =============================================================================
// THREAD SAFETY TESTS
// =============================================================================

static void* concurrent_mock_thread(void *a_arg)
{
    UNUSED(a_arg);
    
    // Reduced to fit within DAP_MOCK_MAX_CALLS (100) limit
    for (int i = 0; i < 15; i++) {
        void *l_args[1] = {(void*)(intptr_t)i};
        dap_mock_record_call(g_mock_simple_function, l_args, 1, (void*)(intptr_t)(i*2));
        
        int l_count = DAP_MOCK_GET_CALL_COUNT(simple_function);
        UNUSED(l_count);  // Just exercise the API
    }
    
    return NULL;
}

static void test_mock_thread_safety(void)
{
    log_it(L_INFO, "=== Test 11: Thread Safety ===");
    
    reset_all_test_mocks();
    
    const int THREAD_COUNT = 5;
    const int CALLS_PER_THREAD = 15;
    pthread_t l_threads[THREAD_COUNT];
    
    log_it(L_DEBUG, "Starting %d threads, %d calls each (total: %d)...",
           THREAD_COUNT, CALLS_PER_THREAD, THREAD_COUNT * CALLS_PER_THREAD);
    
    // Create threads
    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_create(&l_threads[i], NULL, concurrent_mock_thread, NULL);
    }
    
    // Wait for completion
    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(l_threads[i], NULL);
    }
    
    int l_final_count = DAP_MOCK_GET_CALL_COUNT(simple_function);
    int l_expected = THREAD_COUNT * CALLS_PER_THREAD;
    
    log_it(L_DEBUG, "Final call count: %d (expected: %d)",
           l_final_count, l_expected);
    
    dap_assert_PIF(l_final_count == l_expected,
                   "All calls should be counted atomically");
    
    log_it(L_INFO, "✓ Test 11: Thread Safety PASSED\n");
}

// =============================================================================
// RETURN VALUE MODIFICATION TESTS
// =============================================================================

static void test_mock_dynamic_return_values(void)
{
    log_it(L_INFO, "=== Test 12: Dynamic Return Values ===");
    
    reset_all_test_mocks();
    
    // Set different return values at runtime
    DAP_MOCK_SET_RETURN(simple_function, (void*)100);
    dap_assert_PIF(g_mock_simple_function->return_value.ptr == (void*)100,
                   "Should set int return value");
    
    DAP_MOCK_SET_RETURN(simple_function, (void*)0xCAFEBABE);
    dap_assert_PIF(g_mock_simple_function->return_value.ptr == (void*)0xCAFEBABE,
                   "Should update return value");
    
    // Test with union fields
    g_mock_simple_function->return_value.i = 42;
    dap_assert_PIF(g_mock_simple_function->return_value.i == 42,
                   "Should set via union.i");
    
    g_mock_simple_function->return_value.l = 0xDEADBEEF;
    dap_assert_PIF(g_mock_simple_function->return_value.l == (long)0xDEADBEEF,
                   "Should set via union.l");
    
    log_it(L_INFO, "✓ Test 12: Dynamic Return Values PASSED\n");
}

// =============================================================================
// MAIN TEST SUITE
// =============================================================================

int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    // Initialize DAP SDK
    int l_ret = dap_common_init("test_mock", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }
    
    // Initialize mock framework
    dap_mock_init();
    
    log_it(L_INFO, "=== DAP SDK Mock Framework - Unit Tests ===");
    log_it(L_INFO, "Testing all mock features...\n");
    
    // Run tests
    test_mock_declaration_defaults();
    test_mock_custom_return_values();
    test_mock_enable_disable();
    test_mock_call_counting();
    test_mock_call_arguments();
    test_mock_delay_fixed();
    test_mock_delay_range();
    test_mock_delay_variance();
    test_mock_custom_callback();
    test_mock_runtime_callback();
    test_mock_thread_safety();
    test_mock_dynamic_return_values();
    
    log_it(L_INFO, "\n=== All Mock Framework Tests PASSED! ===");
    log_it(L_INFO, "Total: 12 tests");
    
    dap_mock_deinit();
    dap_common_deinit();
    return 0;
}
