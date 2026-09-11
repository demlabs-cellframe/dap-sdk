/**
 * @file test_dap_timerfd.c
 * @brief Unit tests for DAP timerfd module (Linux only)
 * @details Tests timer creation, management, and callbacks with mocking
 * @date 2025-11-21
 * @copyright (c) 2025 Cellframe Network
 */

#include <unistd.h>
#include "dap_common.h"
#include "dap_test.h"
#include "dap_timerfd.h"
#include "dap_worker.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_mock.h"

#define LOG_TAG "test_dap_timerfd"

/* confcall W59-R7.1: DAP_MOCK(func) on a function with no matching
 * DAP_MOCK_WRAPPER_CUSTOM body in this file is never actually
 * linker-wrapped by this project's mock generator (module/test/mocks/lib/
 * awk/scan_mock_declarations.awk only recognises DAP_MOCK_DECLARE/_CUSTOM),
 * so every "mocked" call below always ran the real implementation.  The
 * original test therefore stubbed dap_events_init() to "succeed" without
 * calling it, so dap_events_worker_get_auto() dereferenced an uninitialized
 * s_workers[] and segfaulted (dap_events.c:598, worker_get_index_min).
 * Drive the real dap_events_init()+dap_events_start() pairing instead. */
DAP_MOCK(dap_events_init);
DAP_MOCK(dap_events_deinit);
DAP_MOCK(dap_events_worker_get);
DAP_MOCK(dap_events_worker_get_auto);
DAP_MOCK(dap_events_socket_create_type_pipe);
DAP_MOCK(dap_worker_add_events_socket_unsafe);

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
 * @brief Test: Create timerfd (standalone, not attached to any reactor)
 *
 * dap_timerfd_create() itself only wraps a bare (unattached) events socket
 * via dap_events_socket_wrap_no_add() - it doesn't touch the reactor at
 * all, so this is safe to call with no live dap_events instance.
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
    dap_assert(l_timer != NULL, "Timer created");
    
    if (l_timer) {
        dap_assert(l_timer->timeout_ms == 1000, "Timer timeout matches");
        dap_assert(l_timer->callback == s_test_timer_callback, "Timer callback matches");
        dap_timerfd_delete_unsafe(l_timer);
    }
}

/**
 * @brief Test: Start timerfd on the auto-selected worker of a real reactor
 *
 * dap_timerfd_start() calls dap_events_worker_get_auto(), which requires a
 * live, started reactor (dap_events_init() alone only allocates the
 * s_workers[] array of NULL pointers - dap_events_start() is what actually
 * spawns worker threads and populates it).
 */
static void s_test_timerfd_start(void)
{
    log_it(L_INFO, "Testing timerfd start");
    
    dap_assert(dap_events_init(1, 60) == 0, "Events initialization");
    dap_assert(dap_events_start() == 0, "Events started");

    dap_timerfd_init();

    s_timer_callback_called = false;
    s_timer_callback_count = 0;
    dap_timerfd_t *l_timer = dap_timerfd_start(500, s_test_timer_callback, NULL);
    log_it(L_DEBUG, "Timer started: %p", l_timer);
    dap_assert(l_timer != NULL, "Timer started on auto-selected worker");

    if (l_timer) {
        // Let the 500ms timer actually fire before tearing the reactor down.
        for (int i = 0; i < 200 && !s_timer_callback_called; i++)
            usleep(10000);
        dap_assert(s_timer_callback_called, "Timer callback actually fired");
    }

    dap_events_stop_all();
    dap_events_wait();
    dap_events_deinit();
}

/**
 * @brief Test: Start timerfd on specific worker
 */
static void s_test_timerfd_start_on_worker(void)
{
    log_it(L_INFO, "Testing timerfd start on specific worker");
    
    dap_assert(dap_events_init(2, 60) == 0, "Events initialization");
    dap_assert(dap_events_start() == 0, "Events started");

    dap_timerfd_init();

    dap_worker_t *l_worker = dap_events_worker_get(0);
    dap_assert(l_worker != NULL, "Get worker 0");
    
    if (l_worker) {
        // Start timer on specific worker
        s_timer_callback_called = false;
        s_timer_callback_count = 0;
        dap_timerfd_t *l_timer = dap_timerfd_start_on_worker(
            l_worker, 250, s_test_timer_callback, NULL);
        log_it(L_DEBUG, "Timer started on worker: %p", l_timer);
        dap_assert(l_timer != NULL, "Timer started on explicit worker");
        dap_assert(l_timer == NULL || l_timer->worker == l_worker, "Timer bound to requested worker");

        if (l_timer) {
            for (int i = 0; i < 200 && !s_timer_callback_called; i++)
                usleep(10000);
            dap_assert(s_timer_callback_called, "Timer callback actually fired on explicit worker");
        }
    }
    
    dap_events_stop_all();
    dap_events_wait();
    dap_events_deinit();
}

/**
 * @brief Test: Delete and reset timerfd
 *
 * confcall W59-R7.1: dap_timerfd_reset_unsafe() (Linux path) only touches
 * the kernel timerfd via timerfd_settime() plus
 * dap_events_socket_set_readable_unsafe(), which is safe to call on a
 * never-attached (context == NULL) esocket - so this, unlike start(),
 * doesn't need a live reactor.
 */
static void s_test_timerfd_delete_reset(void)
{
    log_it(L_INFO, "Testing timerfd delete and reset");
    
    dap_timerfd_init();
    
    // Create timer
    dap_timerfd_t *l_timer = dap_timerfd_create(1000, s_test_timer_callback, NULL);
    dap_assert(l_timer != NULL, "Timer created for reset/delete test");
    
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
    
    // Test create with NULL callback - dap_timerfd_reset_unsafe() itself
    // doesn't touch a_timerfd->callback, only assert()s on a_timerfd being
    // non-NULL, so testing "reset with NULL" is redundant with
    // "delete with NULL" above and was never exercising anything different.
    dap_timerfd_t *l_timer_null_cb = dap_timerfd_create(1000, NULL, NULL);
    log_it(L_DEBUG, "Timer with NULL callback: %p", l_timer_null_cb);
    dap_assert(l_timer_null_cb != NULL, "Timer with NULL callback still created");
    
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
    dap_assert(l_timer1 != NULL, "1ms timer created");
    if (l_timer1) {
        dap_assert(l_timer1->timeout_ms == 1, "Timeout 1ms correct");
        dap_timerfd_delete_unsafe(l_timer1);
    }
    
    // Test with large timeout
    dap_timerfd_t *l_timer2 = dap_timerfd_create(86400000, s_test_timer_callback, NULL); // 1 day
    log_it(L_DEBUG, "Timer with 1 day timeout: %p", l_timer2);
    dap_assert(l_timer2 != NULL, "1 day timer created");
    if (l_timer2) {
        dap_assert(l_timer2->timeout_ms == 86400000, "Timeout 1 day correct");
        dap_timerfd_delete_unsafe(l_timer2);
    }
    
    // Test with zero timeout (edge case)
    dap_timerfd_t *l_timer3 = dap_timerfd_create(0, s_test_timer_callback, NULL);
    log_it(L_DEBUG, "Timer with 0ms timeout: %p", l_timer3);
    dap_assert(l_timer3 != NULL, "0ms timer created");
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
        dap_assert(l_timers[i] != NULL, "Timer created");
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
