/**
 * @file test_proc_thread_timer_cancel.c
 * @brief confcall W59-R7.3: regression coverage for the proc-thread
 * cancellable-timer contract introduced/hardened across W53-W54.
 *
 * @details Six consecutive dap-sdk waves (W53 b8b737067 -> W58 b10664bac)
 * touched dap_proc_thread's timer wrapper (struct timer_arg) because it
 * kept being the site of UAF/leak bugs: a oneshot timer wrapper that was
 * never freed on any path (W53), a cancelled timer whose in-flight
 * callback could still run after the owner freed callback_arg (W54-F1,
 * fixed by cancel_then), and refcount handling across the two hops
 * (timer esocket tick -> proc-thread dispatch). None of that had a
 * dedicated unit test - it was only ever exercised indirectly through
 * dap_link_manager's s_update_states timer and caught by hand during
 * manual review. This file locks down the exact documented contract:
 *
 *   1. A repeating timer's callback fires periodically until cancelled.
 *   2. After dap_proc_thread_timer_cancel(), no NEW callback invocation
 *      starts (the wrapper's `cancelled` flag is checked on both hops).
 *   3. dap_proc_thread_timer_cancel_then() posts the finalizer to the
 *      SAME proc thread as the timer callbacks - the single-consumer FIFO
 *      guarantees the finalizer runs strictly after any hop that was
 *      already queued, so it is safe to free callback_arg from the
 *      finalizer even if a callback was in flight at cancel time.
 *   4. A oneshot timer never hands out a handle (a_handle is set to NULL)
 *      - per the W54-F1 comment, its wrapper self-frees on its own tick,
 *      so any handle to it would dangle.
 */

#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include "dap_common.h"
#include "dap_test.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_proc_thread.h"

#define LOG_TAG "test_proc_thread_timer_cancel"

static void s_events_up(uint32_t a_thread_count)
{
    dap_assert(dap_events_init(a_thread_count, 60) == 0, "Events initialization");
    dap_assert(dap_events_start() == 0, "Events started");
}

static void s_events_down(void)
{
    dap_events_stop_all();
    dap_events_wait();
    dap_events_deinit();   /* also runs dap_proc_thread_deinit() internally */
}

/**
 * @brief Test: repeating timer fires multiple times, then cancel() stops
 * any further invocation - the classic dap_link_manager s_update_states
 * shape (N1, MR-1).
 */
static _Atomic int s_repeat_count;

static void s_repeat_callback(void *a_arg)
{
    UNUSED(a_arg);
    atomic_fetch_add_explicit(&s_repeat_count, 1, memory_order_relaxed);
}

static void s_test_timer_repeat_then_cancel(void)
{
    log_it(L_INFO, "Testing repeating timer fire + cancel");

    s_events_up(1);

    atomic_store_explicit(&s_repeat_count, 0, memory_order_relaxed);
    dap_proc_thread_t *l_thread = dap_proc_thread_get(0);
    dap_assert(l_thread != NULL, "Get proc thread 0");

    dap_proc_thread_timer_t l_handle = NULL;
    int l_ret = dap_proc_thread_timer_add_pri_ex(l_thread, s_repeat_callback, NULL,
                                                 20 /*ms*/, false /*repeating*/,
                                                 DAP_QUEUE_MSG_PRIORITY_NORMAL, &l_handle);
    dap_assert(l_ret == 0, "Repeating timer added");
    dap_assert(l_handle != NULL, "Repeating timer handle is non-NULL");

    // Let it fire several times.
    for (int i = 0; i < 300 && atomic_load_explicit(&s_repeat_count, memory_order_relaxed) < 3; i++)
        usleep(10000);
    int l_fired_before_cancel = atomic_load_explicit(&s_repeat_count, memory_order_relaxed);
    dap_assert(l_fired_before_cancel >= 3, "Repeating timer fired at least 3 times before cancel");

    dap_proc_thread_timer_cancel(l_handle);

    // Give any in-flight callback time to finish, then snapshot the count
    // and make sure it does NOT keep growing afterwards.
    usleep(100000);
    int l_count_after_cancel = atomic_load_explicit(&s_repeat_count, memory_order_relaxed);
    usleep(200000);
    int l_count_later = atomic_load_explicit(&s_repeat_count, memory_order_relaxed);
    dap_assert(l_count_later == l_count_after_cancel,
               "No new callback invocation after cancel()");

    s_events_down();
}

/**
 * @brief Test: cancel_then's finalizer runs strictly after cancellation -
 * verifying it is safe to release callback_arg from the finalizer (the
 * W54-F2 contract this API exists for).
 */
typedef struct {
    _Atomic bool cancelled_before_finalizer_saw_it;
    _Atomic int  timer_fire_count;
} cancel_then_ctx_t;

static void s_cancel_then_timer_callback(void *a_arg)
{
    cancel_then_ctx_t *l_ctx = a_arg;
    atomic_fetch_add_explicit(&l_ctx->timer_fire_count, 1, memory_order_relaxed);
    /* Simulate a slow callback so a cancel_then() call racing this hop
     * actually has a chance to observe "still in flight" - the FIFO
     * ordering guarantee is what makes this safe regardless. */
    usleep(5000);
}

static bool s_cancel_then_finalizer(void *a_arg)
{
    cancel_then_ctx_t *l_ctx = a_arg;
    /* By the time the finalizer runs, no further timer callback should be
     * able to start (cancelled was set before this was posted) - freeing
     * l_ctx here would be safe in a real caller. */
    atomic_store_explicit(&l_ctx->cancelled_before_finalizer_saw_it, true, memory_order_relaxed);
    return false; /* don't repeat */
}

static void s_test_timer_cancel_then_fifo_order(void)
{
    log_it(L_INFO, "Testing cancel_then() finalizer FIFO ordering");

    s_events_up(1);

    dap_proc_thread_t *l_thread = dap_proc_thread_get(0);
    dap_assert(l_thread != NULL, "Get proc thread 0");

    cancel_then_ctx_t l_ctx = {0};
    dap_proc_thread_timer_t l_handle = NULL;
    int l_ret = dap_proc_thread_timer_add_pri_ex(l_thread, s_cancel_then_timer_callback, &l_ctx,
                                                 15 /*ms*/, false /*repeating*/,
                                                 DAP_QUEUE_MSG_PRIORITY_NORMAL, &l_handle);
    dap_assert(l_ret == 0, "Repeating timer added for cancel_then test");
    dap_assert(l_handle != NULL, "cancel_then timer handle is non-NULL");

    // Let a couple of ticks land so there's a realistic chance of a
    // cancel racing an in-flight callback.
    for (int i = 0; i < 300 && atomic_load_explicit(&l_ctx.timer_fire_count, memory_order_relaxed) < 2; i++)
        usleep(5000);

    int l_cancel_ret = dap_proc_thread_timer_cancel_then(l_handle, s_cancel_then_finalizer, &l_ctx);
    dap_assert(l_cancel_ret == 0, "cancel_then() posted the finalizer");

    // Wait for the finalizer to actually run.
    for (int i = 0; i < 300 && !atomic_load_explicit(&l_ctx.cancelled_before_finalizer_saw_it, memory_order_relaxed); i++)
        usleep(10000);
    dap_assert(atomic_load_explicit(&l_ctx.cancelled_before_finalizer_saw_it, memory_order_relaxed),
               "Finalizer ran after cancel_then()");

    // No further ticks should be observed once the finalizer has run -
    // the FIFO guarantee means every callback already queued at cancel
    // time ran BEFORE the finalizer, and nothing new gets queued after.
    int l_fires_at_finalizer = atomic_load_explicit(&l_ctx.timer_fire_count, memory_order_relaxed);
    usleep(100000);
    int l_fires_later = atomic_load_explicit(&l_ctx.timer_fire_count, memory_order_relaxed);
    dap_assert(l_fires_later == l_fires_at_finalizer,
               "No timer callback fired after the cancel_then finalizer ran");

    s_events_down();
}

/**
 * @brief Test: a oneshot timer never hands out a live handle - per
 * W54-F1, its wrapper self-frees on its own tick and a handle to it
 * would dangle.
 */
static void s_oneshot_callback(void *a_arg)
{
    UNUSED(a_arg);
}

static void s_test_timer_oneshot_no_handle(void)
{
    log_it(L_INFO, "Testing oneshot timer never hands out a handle");

    s_events_up(1);

    dap_proc_thread_t *l_thread = dap_proc_thread_get(0);
    dap_assert(l_thread != NULL, "Get proc thread 0");

    dap_proc_thread_timer_t l_handle = (dap_proc_thread_timer_t)(void *)0x1; /* poison, must be cleared */
    int l_ret = dap_proc_thread_timer_add_pri_ex(l_thread, s_oneshot_callback, NULL,
                                                 10 /*ms*/, true /*oneshot*/,
                                                 DAP_QUEUE_MSG_PRIORITY_NORMAL, &l_handle);
    dap_assert(l_ret == 0, "Oneshot timer added");
    dap_assert(l_handle == NULL, "Oneshot timer handle is NULL (self-frees, no live handle)");

    // Let it fire and self-free before tearing the reactor down.
    usleep(50000);

    s_events_down();
}

int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);

    int l_ret = dap_common_init("test_proc_thread_timer_cancel", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }

    log_it(L_INFO, "=== Proc Thread Timer Cancel/Cancel-Then Regression (W59-R7.3) ===");

    s_test_timer_repeat_then_cancel();
    s_test_timer_cancel_then_fifo_order();
    s_test_timer_oneshot_no_handle();

    log_it(L_INFO, "=== Proc Thread Timer Cancel/Cancel-Then Tests PASSED! ===");

    dap_common_deinit();
    return 0;
}
