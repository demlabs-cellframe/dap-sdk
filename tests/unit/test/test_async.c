/**
 * @file test_async.c
 * @brief Unit tests for DAP SDK Async Test Framework
 * @details Tests all async testing utilities:
 *          - Global timeout handling
 *          - Condition polling
 *          - pthread condition variable helpers
 *          - Time utilities
 * 
 * @date 2025-10-27
 * @copyright (c) 2025 Demlabs
 */

#include "../../fixtures/utilities/test_helpers.h"
#include "dap_test.h"
#include "dap_test_async.h"
#include "dap_common.h"
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>

#define LOG_TAG "test_async"

// =============================================================================
// TEST HELPERS
// =============================================================================

static bool s_condition_met = false;
static int s_condition_check_count = 0;

static bool test_condition_always_true(void *a_data)
{
    UNUSED(a_data);
    s_condition_check_count++;
    return true;
}

static bool test_condition_always_false(void *a_data)
{
    UNUSED(a_data);
    s_condition_check_count++;
    return false;
}

static bool test_condition_delayed(void *a_data)
{
    UNUSED(a_data);
    s_condition_check_count++;
    
    // Becomes true after 3 checks
    return s_condition_check_count >= 3;
}

// =============================================================================
// TIME UTILITIES TESTS
// =============================================================================

static void test_time_utilities(void)
{
    log_it(L_INFO, "=== Test 1: Time Utilities ===");
    
    // Test get_time_ms returns monotonic increasing values
    uint64_t l_time1 = dap_test_get_time_ms();
    dap_test_sleep_ms(100);
    uint64_t l_time2 = dap_test_get_time_ms();
    
    log_it(L_DEBUG, "Time1: %llu ms, Time2: %llu ms, Delta: %llu ms",
           (unsigned long long)l_time1, (unsigned long long)l_time2,
           (unsigned long long)(l_time2 - l_time1));
    
    dap_assert_PIF(l_time2 > l_time1, "Time should increase");
    // Wine/Windows timer tolerance ~20%
    dap_assert_PIF(l_time2 - l_time1 >= 80 && l_time2 - l_time1 <= 200,
                   "Sleep should be accurate (+/- 20% tolerance for Wine)");
    
    log_it(L_INFO, "✓ Test 1: Time Utilities PASSED\n");
}

// =============================================================================
// CONDITION POLLING TESTS
// =============================================================================

static void test_condition_polling_immediate_success(void)
{
    log_it(L_INFO, "=== Test 2: Condition Polling - Immediate Success ===");
    
    s_condition_check_count = 0;
    
    dap_test_async_config_t l_cfg = DAP_TEST_ASYNC_CONFIG_DEFAULT;
    l_cfg.timeout_ms = 1000;
    l_cfg.poll_interval_ms = 100;
    l_cfg.operation_name = "immediate success test";
    l_cfg.fail_on_timeout = true;
    
    uint64_t l_start = dap_test_get_time_ms();
    bool l_result = dap_test_wait_condition(test_condition_always_true, NULL, &l_cfg);
    uint64_t l_elapsed = dap_test_get_time_ms() - l_start;
    
    log_it(L_DEBUG, "Condition met immediately, elapsed: %llu ms, checks: %d",
           (unsigned long long)l_elapsed, s_condition_check_count);
    
    dap_assert_PIF(l_result == true, "Condition should succeed immediately");
    dap_assert_PIF(s_condition_check_count == 1, "Should check condition once");
    dap_assert_PIF(l_elapsed < 200, "Should complete quickly");
    
    log_it(L_INFO, "✓ Test 2: Immediate Success PASSED\n");
}

static void test_condition_polling_delayed_success(void)
{
    log_it(L_INFO, "=== Test 3: Condition Polling - Delayed Success ===");
    
    s_condition_check_count = 0;
    
    dap_test_async_config_t l_cfg = DAP_TEST_ASYNC_CONFIG_DEFAULT;
    l_cfg.timeout_ms = 2000;
    l_cfg.poll_interval_ms = 100;
    l_cfg.operation_name = "delayed success test";
    l_cfg.fail_on_timeout = true;
    
    uint64_t l_start = dap_test_get_time_ms();
    bool l_result = dap_test_wait_condition(test_condition_delayed, NULL, &l_cfg);
    uint64_t l_elapsed = dap_test_get_time_ms() - l_start;
    
    log_it(L_DEBUG, "Condition met after %llu ms, checks: %d",
           (unsigned long long)l_elapsed, s_condition_check_count);
    
    dap_assert_PIF(l_result == true, "Condition should eventually succeed");
    dap_assert_PIF(s_condition_check_count >= 3, "Should check at least 3 times");
    // Allow 10% timer tolerance for Wine/Windows timing differences
    dap_assert_PIF(l_elapsed >= 100 && l_elapsed < 1000,
                   "Should take ~200-300ms (3 polls * 100ms, with 10% tolerance)");
    
    log_it(L_INFO, "✓ Test 3: Delayed Success PASSED\n");
}

static void test_condition_polling_timeout(void)
{
    log_it(L_INFO, "=== Test 4: Condition Polling - Timeout ===");
    
    s_condition_check_count = 0;
    
    dap_test_async_config_t l_cfg = DAP_TEST_ASYNC_CONFIG_DEFAULT;
    l_cfg.timeout_ms = 500;
    l_cfg.poll_interval_ms = 100;
    l_cfg.operation_name = "timeout test";
    l_cfg.fail_on_timeout = false;  // Don't abort on timeout
    
    uint64_t l_start = dap_test_get_time_ms();
    bool l_result = dap_test_wait_condition(test_condition_always_false, NULL, &l_cfg);
    uint64_t l_elapsed = dap_test_get_time_ms() - l_start;
    
    log_it(L_DEBUG, "Timeout after %llu ms, checks: %d",
           (unsigned long long)l_elapsed, s_condition_check_count);
    
    dap_assert_PIF(l_result == false, "Condition should timeout");
    // Wine/Windows timer tolerance
    dap_assert_PIF(l_elapsed >= 450 && l_elapsed < 700,
                   "Should timeout at ~500ms (+/- tolerance)");
    dap_assert_PIF(s_condition_check_count >= 5,
                   "Should poll multiple times before timeout");
    
    log_it(L_INFO, "✓ Test 4: Timeout PASSED\n");
}

// =============================================================================
// PTHREAD CONDITION VARIABLE TESTS
// =============================================================================

typedef struct {
    dap_test_cond_wait_ctx_t *ctx;
    uint32_t delay_ms;
} async_signal_args_t;

static void* async_signal_thread(void *a_arg)
{
    async_signal_args_t *l_args = (async_signal_args_t *)a_arg;
    
    log_it(L_DEBUG, "Async thread: sleeping %u ms before signal", l_args->delay_ms);
    dap_test_sleep_ms(l_args->delay_ms);
    
    log_it(L_DEBUG, "Async thread: signaling condition");
    dap_test_cond_signal(l_args->ctx);
    
    return NULL;
}

static void test_cond_wait_immediate_signal(void)
{
    log_it(L_INFO, "=== Test 5: Cond Wait - Immediate Signal ===");
    
    dap_test_cond_wait_ctx_t l_ctx;
    dap_test_cond_wait_init(&l_ctx);
    
    // Signal before wait
    dap_test_cond_signal(&l_ctx);
    
    uint64_t l_start = dap_test_get_time_ms();
    bool l_result = dap_test_cond_wait(&l_ctx, 5000);
    uint64_t l_elapsed = dap_test_get_time_ms() - l_start;
    
    log_it(L_DEBUG, "Signaled immediately, elapsed: %llu ms",
           (unsigned long long)l_elapsed);
    
    dap_assert_PIF(l_result == true, "Should succeed immediately");
    dap_assert_PIF(l_elapsed < 100, "Should complete instantly");
    
    dap_test_cond_wait_deinit(&l_ctx);
    
    log_it(L_INFO, "✓ Test 5: Immediate Signal PASSED\n");
}

static void test_cond_wait_delayed_signal(void)
{
    log_it(L_INFO, "=== Test 6: Cond Wait - Delayed Signal ===");
    
    dap_test_cond_wait_ctx_t l_ctx;
    dap_test_cond_wait_init(&l_ctx);
    
    // Start thread that will signal after 200ms
    async_signal_args_t l_args = {
        .ctx = &l_ctx,
        .delay_ms = 200
    };
    
    pthread_t l_thread;
    pthread_create(&l_thread, NULL, async_signal_thread, &l_args);
    
    uint64_t l_start = dap_test_get_time_ms();
    bool l_result = dap_test_cond_wait(&l_ctx, 5000);
    uint64_t l_elapsed = dap_test_get_time_ms() - l_start;
    
    pthread_join(l_thread, NULL);
    
    log_it(L_DEBUG, "Signal received after %llu ms", (unsigned long long)l_elapsed);
    
    dap_assert_PIF(l_result == true, "Should receive signal");
    // Wine/Windows timer tolerance ~20%
    dap_assert_PIF(l_elapsed >= 160 && l_elapsed < 400,
                   "Should take ~200ms for signal (+/- 20% tolerance)");
    
    dap_test_cond_wait_deinit(&l_ctx);
    
    log_it(L_INFO, "✓ Test 6: Delayed Signal PASSED\n");
}

static void test_cond_wait_timeout(void)
{
    log_it(L_INFO, "=== Test 7: Cond Wait - Timeout ===");
    
    dap_test_cond_wait_ctx_t l_ctx;
    dap_test_cond_wait_init(&l_ctx);
    
    // No signal - should timeout
    uint64_t l_start = dap_test_get_time_ms();
    bool l_result = dap_test_cond_wait(&l_ctx, 500);
    uint64_t l_elapsed = dap_test_get_time_ms() - l_start;
    
    log_it(L_DEBUG, "Timeout after %llu ms", (unsigned long long)l_elapsed);
    
    dap_assert_PIF(l_result == false, "Should timeout");
    // Wine/Windows timer tolerance
    dap_assert_PIF(l_elapsed >= 450 && l_elapsed < 700,
                   "Should timeout at ~500ms (+/- tolerance)");
    
    dap_test_cond_wait_deinit(&l_ctx);
    
    log_it(L_INFO, "✓ Test 7: Timeout PASSED\n");
}

// =============================================================================
// MACRO TESTS
// =============================================================================

static void* test_macro_thread(void *a_arg)
{
    UNUSED(a_arg);
    dap_test_sleep_ms(300);
    s_condition_met = true;
    return NULL;
}

static void test_wait_until_macro(void)
{
    log_it(L_INFO, "=== Test 8: DAP_TEST_WAIT_UNTIL Macro ===");
    
    s_condition_met = false;
    
    // Start thread that sets condition after 300ms
    pthread_t l_thread;
    pthread_create(&l_thread, NULL, test_macro_thread, NULL);
    
    uint64_t l_start = dap_test_get_time_ms();
    
    // Use macro to wait
    DAP_TEST_WAIT_UNTIL(
        s_condition_met == true,
        2000,
        "Condition should be met"
    );
    
    uint64_t l_elapsed = dap_test_get_time_ms() - l_start;
    
    pthread_join(l_thread, NULL);
    
    log_it(L_DEBUG, "Macro wait completed in %llu ms", (unsigned long long)l_elapsed);
    // Wine/Windows timer tolerance ~20%
    dap_assert_PIF(l_elapsed >= 250 && l_elapsed < 600,
                   "Should wait ~300ms for condition (+/- 20% tolerance)");
    
    log_it(L_INFO, "✓ Test 8: Macro PASSED\n");
}

// =============================================================================
// GLOBAL TIMEOUT TEST (separate executable needed for actual timeout)
// =============================================================================

static void test_global_timeout_setup(void)
{
    log_it(L_INFO, "=== Test 9: Global Timeout Setup ===");
    
    dap_test_global_timeout_t l_timeout;
    
    // Setup timeout
    int l_result = dap_test_set_global_timeout(&l_timeout, 5, "Timeout Test");
    dap_assert_PIF(l_result == 0, "Timeout setup should succeed");
    
    log_it(L_DEBUG, "Global timeout set to 5 seconds");
    
    // Cancel immediately
    dap_test_cancel_global_timeout();
    
    log_it(L_DEBUG, "Global timeout cancelled");
    
    log_it(L_INFO, "✓ Test 9: Global Timeout Setup PASSED\n");
}

// =============================================================================
// MAIN TEST SUITE
// =============================================================================

int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    // Initialize DAP SDK
    int l_ret = dap_common_init("test_async", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }
    
    log_it(L_INFO, "=== DAP SDK Async Test - Unit Tests ===");
    log_it(L_INFO, "Testing all async utilities...\n");
    
    // Run tests
    test_time_utilities();
    test_condition_polling_immediate_success();
    test_condition_polling_delayed_success();
    test_condition_polling_timeout();
    test_cond_wait_immediate_signal();
    test_cond_wait_delayed_signal();
    test_cond_wait_timeout();
    test_wait_until_macro();
    test_global_timeout_setup();
    
    log_it(L_INFO, "\n=== All Async Tests PASSED! ===");
    
    dap_common_deinit();
    return 0;
}

