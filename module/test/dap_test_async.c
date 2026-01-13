/**
 * @file dap_test_async.c
 * @brief DAP SDK Asynchronous Test Utilities - Implementation
 * @date 2025-01-13
 * @copyright (c) 2025 Demlabs
 */

#include "dap_test_async.h"
#include "dap_test.h"
#include "dap_time.h"
#include <errno.h>
#include <stdio.h>

#define LOG_TAG "dap_test_async"

// =============================================================================
// CONDITION POLLING
// =============================================================================

bool dap_test_wait_condition(
    dap_test_condition_cb_t a_condition,
    void *a_user_data,
    const dap_test_async_config_t *a_config)
{
    if (!a_condition || !a_config) {
        log_it(L_ERROR, "Invalid parameters for dap_test_wait_condition");
        return false;
    }
    
    uint64_t l_start_time = dap_test_get_time_ms();
    uint64_t l_deadline = l_start_time + a_config->timeout_ms;
    uint32_t l_poll_interval = a_config->poll_interval_ms > 0 ? 
                               a_config->poll_interval_ms : 100;
    
    log_it(L_DEBUG, "Waiting for condition '%s' (timeout: %u ms, poll: %u ms)",
           a_config->operation_name, a_config->timeout_ms, l_poll_interval);
    
    while (dap_test_get_time_ms() < l_deadline) {
        // Check condition
        if (a_condition(a_user_data)) {
            uint64_t l_elapsed = dap_test_get_time_ms() - l_start_time;
            log_it(L_DEBUG, "Condition '%s' met after %llu ms",
                   a_config->operation_name, (unsigned long long)l_elapsed);
            return true;
        }
        
        // Sleep before next poll
        dap_test_sleep_ms(l_poll_interval);
    }
    
    // Timeout
    log_it(a_config->fail_on_timeout ? L_ERROR : L_WARNING,
           "Condition '%s' TIMEOUT after %u ms",
           a_config->operation_name, a_config->timeout_ms);
    
    if (a_config->fail_on_timeout) {
        dap_fail("Async operation timeout");
    }
    
    return false;
}

// =============================================================================
// PTHREAD CONDITION VARIABLE HELPERS
// =============================================================================

void dap_test_cond_wait_init(dap_test_cond_wait_ctx_t *a_ctx)
{
    if (!a_ctx) return;
    
    pthread_mutex_init(&a_ctx->mutex, NULL);
    pthread_cond_init(&a_ctx->cond, NULL);
    a_ctx->condition_met = false;
    a_ctx->user_data = NULL;
}

void dap_test_cond_wait_deinit(dap_test_cond_wait_ctx_t *a_ctx)
{
    if (!a_ctx) return;
    
    pthread_mutex_destroy(&a_ctx->mutex);
    pthread_cond_destroy(&a_ctx->cond);
}

void dap_test_cond_signal(dap_test_cond_wait_ctx_t *a_ctx)
{
    if (!a_ctx) return;
    
    pthread_mutex_lock(&a_ctx->mutex);
    a_ctx->condition_met = true;
    pthread_cond_signal(&a_ctx->cond);
    pthread_mutex_unlock(&a_ctx->mutex);
}

bool dap_test_cond_wait(dap_test_cond_wait_ctx_t *a_ctx, uint32_t a_timeout_ms)
{
    if (!a_ctx) return false;
    
    pthread_mutex_lock(&a_ctx->mutex);
    
    if (a_ctx->condition_met) {
        pthread_mutex_unlock(&a_ctx->mutex);
        return true;
    }
    
    // Calculate absolute timeout
    struct timespec l_timeout;
    clock_gettime(CLOCK_REALTIME, &l_timeout);
    
    // Add timeout (handle overflow correctly)
    uint64_t l_nsec = l_timeout.tv_nsec + (uint64_t)(a_timeout_ms % 1000) * 1000000;
    l_timeout.tv_sec += a_timeout_ms / 1000 + l_nsec / 1000000000;
    l_timeout.tv_nsec = l_nsec % 1000000000;
    
    // Wait with timeout
    int l_result = pthread_cond_timedwait(&a_ctx->cond, &a_ctx->mutex, &l_timeout);
    bool l_success = a_ctx->condition_met;
    
    pthread_mutex_unlock(&a_ctx->mutex);
    
    if (l_result == ETIMEDOUT) {
        log_it(L_WARNING, "Condition variable wait timeout after %u ms", a_timeout_ms);
        return false;
    } else if (l_result != 0) {
        log_it(L_ERROR, "pthread_cond_timedwait error: %d", l_result);
        return false;
    }
    
    return l_success;
}
// =============================================================================
// GLOBAL TEST TIMEOUT (CROSS-PLATFORM PTHREAD WATCHDOG)
// =============================================================================

/**
 * @brief Watchdog thread that monitors test execution time
 */
static void *s_timeout_watchdog_thread(void *a_arg)
{
    dap_test_global_timeout_t *l_timeout = (dap_test_global_timeout_t*)a_arg;
    if (!l_timeout) return NULL;
    
    // Sleep for timeout duration
    dap_usleep(l_timeout->timeout_sec * 1000000ULL);
    
    // Check if cancelled
    pthread_mutex_lock(&l_timeout->lock);
    if (!l_timeout->cancelled) {
        l_timeout->timeout_triggered = true;
        
        log_it(L_CRITICAL, "=== TEST TIMEOUT ===");
        log_it(L_CRITICAL, "Test '%s' exceeded %u seconds",
               l_timeout->test_name ? l_timeout->test_name : "unknown",
               l_timeout->timeout_sec);
        log_it(L_CRITICAL, "Aborting test execution...");
        
        // Force exit with error code
        pthread_mutex_unlock(&l_timeout->lock);
        exit(124); // Timeout exit code
    }
    pthread_mutex_unlock(&l_timeout->lock);
    
    return NULL;
}

int dap_test_set_global_timeout(
    dap_test_global_timeout_t *a_timeout,
    uint32_t a_timeout_sec,
    const char *a_test_name)
{
    if (!a_timeout) return -1;
    
    a_timeout->timeout_triggered = false;
    a_timeout->cancelled = false;
    a_timeout->timeout_sec = a_timeout_sec;
    a_timeout->test_name = a_test_name;
    
    // Initialize mutex
    if (pthread_mutex_init(&a_timeout->lock, NULL) != 0) {
        log_it(L_ERROR, "Failed to init timeout mutex");
        return -1;
    }
    
    // Create watchdog thread
    if (pthread_create(&a_timeout->watchdog_thread, NULL, 
                       s_timeout_watchdog_thread, a_timeout) != 0) {
        log_it(L_ERROR, "Failed to create timeout watchdog thread");
        pthread_mutex_destroy(&a_timeout->lock);
        return -1;
    }
    
    log_it(L_INFO, "Global test timeout set: %u seconds for '%s'",
           a_timeout_sec, a_test_name ? a_test_name : "test");
    
    return 0;
}

bool dap_test_check_timeout(dap_test_global_timeout_t *a_timeout)
{
    if (!a_timeout) return false;
    
    pthread_mutex_lock(&a_timeout->lock);
    bool l_triggered = a_timeout->timeout_triggered;
    pthread_mutex_unlock(&a_timeout->lock);
    
    return l_triggered;
}

void dap_test_cancel_global_timeout(dap_test_global_timeout_t *a_timeout)
{
    if (!a_timeout) return;
    
    // Mark as cancelled
    pthread_mutex_lock(&a_timeout->lock);
    a_timeout->cancelled = true;
    pthread_mutex_unlock(&a_timeout->lock);
    
    // Wait for watchdog thread to finish
    pthread_join(a_timeout->watchdog_thread, NULL);
    
    // Cleanup
    pthread_mutex_destroy(&a_timeout->lock);
    
    log_it(L_DEBUG, "Global test timeout cancelled");
}
