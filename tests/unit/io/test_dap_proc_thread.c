/**
 * @file test_dap_proc_thread.c
 * @brief Unit tests for DAP proc thread module
 * @details Tests proc thread creation, queues, timers, and callbacks with mocking
 * @date 2025-11-21
 * @copyright (c) 2025 Cellframe Network
 */

#include <pthread.h>
#include <unistd.h>
#include "dap_common.h"
#include "dap_test.h"
#include "dap_proc_thread.h"
#include "dap_context.h"
#include "dap_events.h"
#include "dap_mock.h"

#define LOG_TAG "test_dap_proc_thread"

/* confcall W59-R7.1: dap_proc_thread_init() requires a live worker per CPU
 * ID (s_context_callback_started() does dap_events_worker_get(cpu_id) and
 * ERRORs "Cannot get worker for CPU ID" — every posted callback then fails
 * with -3, "thread stopped").  In every production path (dap_events.c
 * dap_events_start(), native branch) proc threads are brought up AFTER the
 * event-socket workers, with the SAME thread count, never standalone.  The
 * short DAP_MOCK(dap_context_init/...) form below is also never actually
 * linker-wrapped by this project's mock generator (it only recognises
 * DAP_MOCK_DECLARE/_CUSTOM — see module/test/mocks/lib/awk/
 * scan_mock_declarations.awk), so "mocking out" dap_context_new here has
 * never done anything; drive the real reactor instead. */
static int s_events_up(uint32_t a_thread_count)
{
    int l_ret = dap_events_init(a_thread_count, 60);
    if (l_ret != 0)
        return l_ret;
    return dap_events_start();
}

static void s_events_down(void)
{
    dap_events_stop_all();
    dap_events_wait();
    dap_events_deinit();   /* also runs dap_proc_thread_deinit() internally */
}

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
    
    uint32_t l_thread_count = 2;
    int l_ret = s_events_up(l_thread_count);
    dap_assert(l_ret == 0, "Proc thread initialization");
    
    // Check thread count
    uint32_t l_count = dap_proc_thread_get_count();
    log_it(L_DEBUG, "Thread count: %u", l_count);
    dap_assert(l_count == l_thread_count, "Thread count matches request");
    
    s_events_down();
    dap_pass_msg("Proc thread deinitialization");
}

/**
 * @brief Test: Get proc thread by index
 */
static void s_test_proc_thread_get(void)
{
    log_it(L_INFO, "Testing proc thread retrieval");
    
    uint32_t l_thread_count = 3;
    int l_ret = s_events_up(l_thread_count);
    dap_assert(l_ret == 0, "Proc thread initialization");
    
    // Test getting thread by index
    dap_proc_thread_t *l_thread0 = dap_proc_thread_get(0);
    dap_assert(l_thread0 != NULL, "Get thread 0");
    
    dap_proc_thread_t *l_thread1 = dap_proc_thread_get(1);
    dap_assert(l_thread1 != NULL, "Get thread 1");
    
    // Test invalid index
    dap_proc_thread_t *l_thread_invalid = dap_proc_thread_get(999);
    dap_assert(l_thread_invalid == NULL, "Invalid index returns NULL");
    
    s_events_down();
}

/**
 * @brief Test: Get auto thread (load balancing)
 */
static void s_test_proc_thread_get_auto(void)
{
    log_it(L_INFO, "Testing auto thread selection");
    
    uint32_t l_thread_count = 2;
    int l_ret = s_events_up(l_thread_count);
    dap_assert(l_ret == 0, "Proc thread initialization");
    
    // Test auto selection
    dap_proc_thread_t *l_thread_auto = dap_proc_thread_get_auto();
    dap_assert(l_thread_auto != NULL, "Get auto thread");
    
    s_events_down();
}

/**
 * @brief Test: Add callback to queue
 */
static void s_test_proc_thread_callback_add(void)
{
    log_it(L_INFO, "Testing callback addition to queue");
    
    uint32_t l_thread_count = 1;
    int l_ret = s_events_up(l_thread_count);
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

        // Give the proc thread a chance to actually run the queued callbacks
        // before the reactor is torn down underneath it.
        for (int i = 0; i < 200 && s_callback_count < 3; i++)
            usleep(10000);
        dap_assert(s_callback_count >= 1, "At least one queued callback actually ran");
    }
    
    s_events_down();
}

/**
 * @brief Test: Add timer with callback
 */
static void s_test_proc_thread_timer_add(void)
{
    log_it(L_INFO, "Testing timer addition");
    
    uint32_t l_thread_count = 1;
    int l_ret = s_events_up(l_thread_count);
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

        // Let the oneshot timer actually fire before tearing the reactor down.
        for (int i = 0; i < 200 && !s_callback_executed; i++)
            usleep(10000);
        dap_assert(s_callback_executed, "Oneshot timer callback actually fired");
    }
    
    s_events_down();
}

/**
 * @brief Test: Queue size statistics
 */
static void s_test_proc_thread_queue_size(void)
{
    log_it(L_INFO, "Testing queue size statistics");
    
    uint32_t l_thread_count = 2;
    int l_ret = s_events_up(l_thread_count);
    dap_assert(l_ret == 0, "Proc thread initialization");
    
    // Get average queue size
    size_t l_avg_size = dap_proc_thread_get_avg_queue_size();
    log_it(L_DEBUG, "Average queue size: %zu", l_avg_size);
    // size_t is always >= 0, skip assert
    
    s_events_down();
}

/**
 * @brief Test: Edge cases and NULL checks
 */
static void s_test_proc_thread_edge_cases(void)
{
    log_it(L_INFO, "Testing proc thread edge cases");
    
    uint32_t l_thread_count = 1;
    int l_ret = s_events_up(l_thread_count);
    dap_assert(l_ret == 0, "Proc thread initialization");
    
    // Test adding a NULL callback function - always invalid regardless of thread.
    dap_proc_thread_t *l_thread = dap_proc_thread_get(0);
    dap_assert(l_thread != NULL, "Get thread 0");
    if (l_thread) {
        int l_add_ret = dap_proc_thread_callback_add(l_thread, NULL, NULL);
        log_it(L_DEBUG, "NULL callback add returned: %d", l_add_ret);
        dap_assert(l_add_ret != 0, "NULL callback fails gracefully");
    }

    /* confcall W59-R7.1: a_thread == NULL is a *documented* "pick any thread"
     * request (dap_proc_thread_callback_add_pri_owned() falls back to
     * dap_proc_thread_get_auto()), not an error case - with a live reactor
     * this legitimately succeeds.  The original assertion here expected
     * failure and only passed by accident because the reactor was never
     * actually running (see s_events_up/s_events_down above), so l_thread
     * itself was always NULL too and every add failed for the wrong reason. */
    s_callback_count = 0;
    int l_add_ret = dap_proc_thread_callback_add(NULL, s_test_queue_callback, NULL);
    dap_assert(l_add_ret == 0, "NULL thread auto-selects a live thread");
    for (int i = 0; i < 200 && s_callback_count < 1; i++)
        usleep(10000);
    dap_assert(s_callback_count == 1, "Auto-selected callback actually ran");

    // Now tear the whole reactor down (dap_proc_thread_deinit() only runs
    // as part of dap_events_deinit(), NOT dap_events_stop_all()/_wait() -
    // those only signal the worker contexts) and confirm a post-shutdown
    // add is rejected instead of silently accepted or crashing.
    s_events_down();
    l_add_ret = dap_proc_thread_callback_add(NULL, s_test_queue_callback, NULL);
    dap_assert(l_add_ret != 0, "Callback add after shutdown fails gracefully");
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
