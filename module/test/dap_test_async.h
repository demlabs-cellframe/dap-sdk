/**
 * @file dap_test_async.h
 * @brief DAP SDK Asynchronous Test Utilities
 * @details Universal utilities for testing asynchronous operations:
 *          - Waiting for conditions with timeout
 *          - State polling with intervals
 *          - Whole test timeout handling
 *          - Thread-safe checks
 * 
 * @date 2025-10-27
 * @copyright (c) 2025 Demlabs
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include "dap_common.h"
#include "dap_test.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// TIMEOUT CONFIGURATION
// =============================================================================

/**
 * @brief Timeout configuration for asynchronous operation
 */
typedef struct dap_test_async_config {
    uint32_t timeout_ms;           ///< Maximum wait time (ms)
    uint32_t poll_interval_ms;     ///< Condition polling interval (ms), 0 = no polling
    bool fail_on_timeout;          ///< true = abort() on timeout, false = return false
    const char *operation_name;    ///< Operation name for logging
} dap_test_async_config_t;

/**
 * @brief Default configuration (5 sec timeout, 100ms poll, abort on timeout)
 */
#define DAP_TEST_ASYNC_CONFIG_DEFAULT { \
    .timeout_ms = 5000, \
    .poll_interval_ms = 100, \
    .fail_on_timeout = true, \
    .operation_name = "async operation" \
}

// =============================================================================
// CONDITION POLLING
// =============================================================================

/**
 * @brief Callback for checking condition
 * @param a_user_data User data
 * @return true if condition is met, false to continue waiting
 */
typedef bool (*dap_test_condition_cb_t)(void *a_user_data);

/**
 * @brief Wait for condition with timeout and polling
 * @details Periodically calls callback until condition is met or timeout
 * 
 * @param a_condition Callback to check condition
 * @param a_user_data Data passed to callback
 * @param a_config Timeout configuration
 * @return true if condition met, false on timeout
 * 
 * @code
 * // Example: wait for state machine to reach CONNECTED
 * bool check_connected(void *a_data) {
 *     vpn_sm_t *l_sm = (vpn_sm_t *)a_data;
 *     return vpn_sm_get_state(l_sm) == VPN_STATE_CONNECTED;
 * }
 * 
 * dap_test_async_config_t l_cfg = DAP_TEST_ASYNC_CONFIG_DEFAULT;
 * l_cfg.timeout_ms = 10000;
 * l_cfg.operation_name = "VPN connection";
 * 
 * bool l_result = dap_test_wait_condition(check_connected, l_sm, &l_cfg);
 * dap_assert(l_result, "VPN should connect within 10 sec");
 * @endcode
 */
bool dap_test_wait_condition(
    dap_test_condition_cb_t a_condition,
    void *a_user_data,
    const dap_test_async_config_t *a_config
);

// =============================================================================
// PTHREAD CONDITION VARIABLE HELPERS
// =============================================================================

/**
 * @brief Context for waiting on pthread condition variable
 */
typedef struct dap_test_cond_wait_ctx {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool condition_met;
    void *user_data;
} dap_test_cond_wait_ctx_t;

/**
 * @brief Initialize condition variable context
 * @param a_ctx Context to initialize
 */
void dap_test_cond_wait_init(dap_test_cond_wait_ctx_t *a_ctx);

/**
 * @brief Deinitialize context
 * @param a_ctx Context to cleanup
 */
void dap_test_cond_wait_deinit(dap_test_cond_wait_ctx_t *a_ctx);

/**
 * @brief Signal that condition is met
 * @param a_ctx Context
 */
void dap_test_cond_signal(dap_test_cond_wait_ctx_t *a_ctx);

/**
 * @brief Wait on condition variable with timeout
 * @param a_ctx Context
 * @param a_timeout_ms Timeout in milliseconds
 * @return true if condition met, false on timeout
 * 
 * @code
 * // Example: async callback signals completion
 * dap_test_cond_wait_ctx_t l_ctx;
 * dap_test_cond_wait_init(&l_ctx);
 * 
 * // Start async operation
 * start_async_operation(&l_ctx, my_completion_callback);
 * 
 * // Wait with 5 sec timeout
 * bool l_success = dap_test_cond_wait(&l_ctx, 5000);
 * dap_assert(l_success, "Async operation should complete");
 * 
 * dap_test_cond_wait_deinit(&l_ctx);
 * @endcode
 */
bool dap_test_cond_wait(dap_test_cond_wait_ctx_t *a_ctx, uint32_t a_timeout_ms);

// =============================================================================
// WHOLE TEST TIMEOUT (ALARM-BASED)
// =============================================================================

/**
 * @brief Global timeout context for entire test suite
 */
typedef struct dap_test_global_timeout {
    sigjmp_buf jump_buf;
    volatile sig_atomic_t timeout_triggered;
    uint32_t timeout_sec;
    const char *test_name;
} dap_test_global_timeout_t;

/**
 * @brief Set global timeout for entire test suite
 * @details Uses alarm() to limit test execution time.
 *          On timeout, siglongjmp is called to exit the test.
 * 
 * @param a_timeout Timeout context
 * @param a_timeout_sec Timeout in seconds
 * @param a_test_name Test name for logging
 * @return 0 on first call, 1 if timeout occurred (after longjmp)
 * 
 * @code
 * int main(int argc, char **argv) {
 *     dap_test_global_timeout_t l_timeout;
 *     
 *     // Set 30 sec timeout for entire test suite
 *     if (dap_test_set_global_timeout(&l_timeout, 30, "VPN State Machine Tests")) {
 *         // Timeout triggered
 *         log_it(L_CRITICAL, "Test suite timeout!");
 *         return 1;
 *     }
 *     
 *     // Run tests
 *     run_all_tests();
 *     
 *     // Cancel timeout
 *     dap_test_cancel_global_timeout();
 *     return 0;
 * }
 * @endcode
 */
int dap_test_set_global_timeout(
    dap_test_global_timeout_t *a_timeout,
    uint32_t a_timeout_sec,
    const char *a_test_name
);

/**
 * @brief Cancel global timeout
 */
void dap_test_cancel_global_timeout(void);

// =============================================================================
// SIMPLE DELAY HELPERS
// =============================================================================

/**
 * @brief Sleep for specified milliseconds (cross-platform)
 * @param a_delay_ms Delay in ms
 */
static inline void dap_test_sleep_ms(uint32_t a_delay_ms) {
    usleep(a_delay_ms * 1000);
}

/**
 * @brief Get current time in milliseconds (monotonic)
 * @return Time in ms
 */
static inline uint64_t dap_test_get_time_ms(void) {
    struct timespec l_ts;
    clock_gettime(CLOCK_MONOTONIC, &l_ts);
    return (uint64_t)l_ts.tv_sec * 1000 + (uint64_t)l_ts.tv_nsec / 1000000;
}

// =============================================================================
// CONVENIENCE MACROS
// =============================================================================

/**
 * @brief Macro for waiting condition with default parameters
 * @param condition Condition (expression returning bool)
 * @param timeout_ms Timeout in ms
 * @param msg Message to print on failure
 */
#define DAP_TEST_WAIT_UNTIL(condition, timeout_ms, msg) do { \
    uint64_t l_start = dap_test_get_time_ms(); \
    bool l_success = false; \
    while (dap_test_get_time_ms() - l_start < (timeout_ms)) { \
        if (condition) { \
            l_success = true; \
            break; \
        } \
        dap_test_sleep_ms(100); \
    } \
    dap_assert_PIF(l_success, msg); \
} while(0)

#ifdef __cplusplus
}
#endif

