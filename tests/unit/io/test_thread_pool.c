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
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>
#include <pthread.h>
#include "dap_test.h"
#include "dap_thread_pool.h"
#include "dap_common.h"

// ===== Shared test state =====

static atomic_int s_task_counter = 0;
static atomic_int s_callback_counter = 0;

/**
 * @brief Simple task: adds value to counter, returns value * 2
 */
static void *s_simple_task(void *a_arg)
{
    int l_value = *(int *)a_arg;
    atomic_fetch_add(&s_task_counter, l_value);
    usleep(10000);  // 10ms work
    return (void *)(intptr_t)(l_value * 2);
}

/**
 * @brief Task completion callback: verifies result, increments callback counter
 */
static void s_task_callback(dap_thread_pool_t *a_pool,
                            dap_thread_t a_worker_thread,
                            void *a_result,
                            void *a_arg)
{
    UNUSED(a_pool);
    UNUSED(a_worker_thread);

    int l_result = (intptr_t)a_result;
    int l_expected = *(int *)a_arg * 2;

    dap_assert(l_result == l_expected, "Callback result should match expected");
    atomic_fetch_add(&s_callback_counter, 1);
}

// ===== Test: create / delete =====

static void test_thread_pool_create_delete(void)
{
    dap_test_msg("Test: Thread pool create/delete");

    dap_thread_pool_t *l_pool = dap_thread_pool_create(2, 10);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Thread pool create/delete works");
}

// ===== Test: auto CPU count =====

static void test_thread_pool_auto_cpus(void)
{
    dap_test_msg("Test: Thread pool auto CPU detection");

    dap_thread_pool_t *l_pool = dap_thread_pool_create(0, 10);
    dap_assert(l_pool != NULL, "Thread pool creation with auto CPUs should succeed");

    uint32_t l_count = dap_thread_pool_get_thread_count(l_pool);
    dap_assert(l_count > 0, "Auto-detected thread count should be > 0");
    dap_test_msg("  auto-detected %u threads", l_count);

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Thread pool auto CPU detection works");
}

// ===== Test: get_thread_count =====

static void test_thread_pool_get_thread_count(void)
{
    dap_test_msg("Test: Thread pool get_thread_count");

    // Explicit count
    dap_thread_pool_t *l_pool = dap_thread_pool_create(3, 0);
    dap_assert(l_pool != NULL, "Pool creation should succeed");
    dap_assert(dap_thread_pool_get_thread_count(l_pool) == 3,
               "get_thread_count should return 3");
    dap_thread_pool_delete(l_pool);

    // Another value
    l_pool = dap_thread_pool_create(7, 0);
    dap_assert(l_pool != NULL, "Pool creation should succeed");
    dap_assert(dap_thread_pool_get_thread_count(l_pool) == 7,
               "get_thread_count should return 7");
    dap_thread_pool_delete(l_pool);

    // NULL pool
    dap_assert(dap_thread_pool_get_thread_count(NULL) == 0,
               "get_thread_count(NULL) should return 0");

    dap_pass_msg("Thread pool get_thread_count works");
}

// ===== Test: submit tasks (round-robin) =====

static void test_thread_pool_submit_tasks(void)
{
    dap_test_msg("Test: Thread pool submit tasks (round-robin)");

    atomic_store(&s_task_counter, 0);
    atomic_store(&s_callback_counter, 0);

    dap_thread_pool_t *l_pool = dap_thread_pool_create(2, 100);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");

    #define NUM_SUBMIT_TASKS 10
    int l_args[NUM_SUBMIT_TASKS];

    for (int i = 0; i < NUM_SUBMIT_TASKS; i++) {
        l_args[i] = i + 1;
        int l_ret = dap_thread_pool_submit(l_pool, s_simple_task, &l_args[i],
                                           s_task_callback, &l_args[i]);
        dap_assert(l_ret == 0, "Task submission should succeed");
    }

    // Wait for tasks
    int l_timeout = 0;
    while (atomic_load(&s_callback_counter) < NUM_SUBMIT_TASKS && l_timeout < 50) {
        usleep(100000);
        l_timeout++;
    }

    dap_assert(atomic_load(&s_callback_counter) == NUM_SUBMIT_TASKS,
               "All callbacks should be invoked");

    // Sum 1+2+...+10 = 55
    int l_expected = (NUM_SUBMIT_TASKS * (NUM_SUBMIT_TASKS + 1)) / 2;
    dap_assert(atomic_load(&s_task_counter) == l_expected,
               "All tasks should execute correctly");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Thread pool task submission (round-robin) works");
}

// ===== Test: submit_to (sticky binding) =====

// Per-thread tracking for sticky binding test
#define STICKY_THREADS 4
#define STICKY_TASKS_PER_THREAD 20

static pthread_t s_sticky_thread_ids[STICKY_THREADS][STICKY_TASKS_PER_THREAD];
static atomic_int s_sticky_counts[STICKY_THREADS];

typedef struct {
    uint32_t thread_idx;
    uint32_t task_seq;
} sticky_task_arg_t;

static void *s_sticky_task(void *a_arg)
{
    sticky_task_arg_t *l_a = (sticky_task_arg_t *)a_arg;
    uint32_t l_idx = l_a->thread_idx;
    uint32_t l_seq = atomic_fetch_add(&s_sticky_counts[l_idx], 1);

    // Record which pthread executed this task
    if (l_seq < STICKY_TASKS_PER_THREAD)
        s_sticky_thread_ids[l_idx][l_seq] = pthread_self();

    usleep(1000);  // 1ms work to ensure ordering visibility
    return NULL;
}

static void test_thread_pool_submit_to(void)
{
    dap_test_msg("Test: Thread pool submit_to (sticky binding)");

    dap_thread_pool_t *l_pool = dap_thread_pool_create(STICKY_THREADS, 0);
    dap_assert(l_pool != NULL, "Pool creation should succeed");
    dap_assert(dap_thread_pool_get_thread_count(l_pool) == STICKY_THREADS,
               "Pool should have exact thread count");

    // Reset tracking
    memset(s_sticky_thread_ids, 0, sizeof(s_sticky_thread_ids));
    for (int i = 0; i < STICKY_THREADS; i++)
        atomic_store(&s_sticky_counts[i], 0);

    // Allocate args (need stable addresses)
    sticky_task_arg_t l_args[STICKY_THREADS][STICKY_TASKS_PER_THREAD];

    for (uint32_t t = 0; t < STICKY_THREADS; t++) {
        for (uint32_t s = 0; s < STICKY_TASKS_PER_THREAD; s++) {
            l_args[t][s].thread_idx = t;
            l_args[t][s].task_seq = s;
            int l_ret = dap_thread_pool_submit_to(l_pool, t,
                                                   s_sticky_task, &l_args[t][s],
                                                   NULL, NULL);
            dap_assert(l_ret == 0, "submit_to should succeed");
        }
    }

    // Wait for all tasks
    int l_timeout = 0;
    while (l_timeout < 100) {
        bool l_all_done = true;
        for (int i = 0; i < STICKY_THREADS; i++) {
            if (atomic_load(&s_sticky_counts[i]) < STICKY_TASKS_PER_THREAD) {
                l_all_done = false;
                break;
            }
        }
        if (l_all_done) break;
        usleep(50000);
        l_timeout++;
    }

    // Verify: all tasks for each thread_idx ran on the same pthread
    for (uint32_t t = 0; t < STICKY_THREADS; t++) {
        int l_count = atomic_load(&s_sticky_counts[t]);
        dap_assert(l_count == STICKY_TASKS_PER_THREAD, "All tasks for thread should complete");

        pthread_t l_expected_tid = s_sticky_thread_ids[t][0];
        dap_assert(l_expected_tid != 0, "Thread ID should be recorded");

        for (int s = 1; s < STICKY_TASKS_PER_THREAD; s++) {
            dap_assert(pthread_equal(s_sticky_thread_ids[t][s], l_expected_tid),
                       "All tasks for same index must run on same pthread");
        }
        dap_test_msg("  thread_idx=%u: all %d tasks ran on same pthread", t, l_count);
    }

    // Verify: different thread_idx -> different pthreads
    for (uint32_t t1 = 0; t1 < STICKY_THREADS; t1++) {
        for (uint32_t t2 = t1 + 1; t2 < STICKY_THREADS; t2++) {
            dap_assert(!pthread_equal(s_sticky_thread_ids[t1][0],
                                      s_sticky_thread_ids[t2][0]),
                       "Different thread indices must use different pthreads");
        }
    }

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Thread pool submit_to (sticky binding) works");
}

// ===== Test: submit_to with sequential ordering guarantee =====

#define ORDER_TASKS 50
static int s_order_sequence[ORDER_TASKS];
static atomic_int s_order_count;

typedef struct {
    int sequence_number;
} order_task_arg_t;

static void *s_order_task(void *a_arg)
{
    order_task_arg_t *l_a = (order_task_arg_t *)a_arg;
    int l_pos = atomic_fetch_add(&s_order_count, 1);
    if (l_pos < ORDER_TASKS)
        s_order_sequence[l_pos] = l_a->sequence_number;
    return NULL;
}

static void test_thread_pool_submit_to_ordering(void)
{
    dap_test_msg("Test: submit_to preserves FIFO ordering on same thread");

    dap_thread_pool_t *l_pool = dap_thread_pool_create(4, 0);
    dap_assert(l_pool != NULL, "Pool creation should succeed");

    memset(s_order_sequence, -1, sizeof(s_order_sequence));
    atomic_store(&s_order_count, 0);

    order_task_arg_t l_args[ORDER_TASKS];
    for (int i = 0; i < ORDER_TASKS; i++) {
        l_args[i].sequence_number = i;
        // All tasks go to thread 0 -> must execute in order
        int l_ret = dap_thread_pool_submit_to(l_pool, 0, s_order_task, &l_args[i], NULL, NULL);
        dap_assert(l_ret == 0, "submit_to should succeed");
    }

    // Wait for completion
    int l_timeout = 0;
    while (atomic_load(&s_order_count) < ORDER_TASKS && l_timeout < 100) {
        usleep(50000);
        l_timeout++;
    }

    dap_assert(atomic_load(&s_order_count) == ORDER_TASKS, "All order tasks should complete");

    // Verify FIFO: tasks must appear in submission order
    for (int i = 0; i < ORDER_TASKS; i++) {
        if (s_order_sequence[i] != i) {
            dap_test_msg("  FAIL: position %d has sequence %d (expected %d)",
                         i, s_order_sequence[i], i);
            dap_assert(false, "FIFO ordering must be preserved for submit_to");
        }
    }

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("submit_to preserves FIFO ordering");
}

// ===== Test: submit_to invalid index =====

static void *s_noop_task(void *a_arg)
{
    UNUSED(a_arg);
    return NULL;
}

static void test_thread_pool_submit_to_invalid_index(void)
{
    dap_test_msg("Test: submit_to with invalid index");

    dap_thread_pool_t *l_pool = dap_thread_pool_create(2, 0);
    dap_assert(l_pool != NULL, "Pool creation should succeed");

    // Index equal to thread count (out of bounds)
    int l_ret = dap_thread_pool_submit_to(l_pool, 2, s_noop_task, NULL, NULL, NULL);
    dap_assert(l_ret == -1, "submit_to with index==count should return -1");

    // Index way out of bounds
    l_ret = dap_thread_pool_submit_to(l_pool, 999, s_noop_task, NULL, NULL, NULL);
    dap_assert(l_ret == -1, "submit_to with index=999 should return -1");

    // NULL pool
    l_ret = dap_thread_pool_submit_to(NULL, 0, s_noop_task, NULL, NULL, NULL);
    dap_assert(l_ret == -1, "submit_to with NULL pool should return -1");

    // NULL func
    l_ret = dap_thread_pool_submit_to(l_pool, 0, NULL, NULL, NULL, NULL);
    dap_assert(l_ret == -1, "submit_to with NULL func should return -1");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("submit_to with invalid index handled correctly");
}

// ===== Test: submit error handling =====

static void test_thread_pool_submit_errors(void)
{
    dap_test_msg("Test: submit error handling");

    // NULL pool
    int l_ret = dap_thread_pool_submit(NULL, s_noop_task, NULL, NULL, NULL);
    dap_assert(l_ret == -1, "submit with NULL pool should return -1");

    // NULL func
    dap_thread_pool_t *l_pool = dap_thread_pool_create(2, 0);
    l_ret = dap_thread_pool_submit(l_pool, NULL, NULL, NULL, NULL);
    dap_assert(l_ret == -1, "submit with NULL func should return -1");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("submit error handling works");
}

// ===== Test: pending count =====

static void test_thread_pool_pending_count(void)
{
    dap_test_msg("Test: Thread pool pending count");

    atomic_store(&s_task_counter, 0);

    dap_thread_pool_t *l_pool = dap_thread_pool_create(1, 100);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");

    static int l_pending_args[5];
    for (int i = 0; i < 5; i++) {
        l_pending_args[i] = i;
        dap_thread_pool_submit(l_pool, s_simple_task, &l_pending_args[i], NULL, NULL);
    }

    uint32_t l_pending = dap_thread_pool_get_pending_count(l_pool);
    dap_test_msg("  pending tasks: %u", l_pending);

    // Wait for completion with retry
    for(int _w = 0; _w < 20; _w++) {
        usleep(100000);
        l_pending = dap_thread_pool_get_pending_count(l_pool);
        if(l_pending == 0) break;
    }
    dap_assert(l_pending == 0, "All tasks should complete");

    // NULL pool
    dap_assert(dap_thread_pool_get_pending_count(NULL) == 0,
               "get_pending_count(NULL) should return 0");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Thread pool pending count works");
}

// ===== Test: queue overflow =====

static void test_thread_pool_queue_overflow(void)
{
    dap_test_msg("Test: Thread pool queue overflow");

    // Create pool with small queue (2 per-thread)
    dap_thread_pool_t *l_pool = dap_thread_pool_create(1, 2);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");

    static int l_overflow_arg = 1;
    int l_success = 0;
    int l_failed = 0;

    for (int i = 0; i < 10; i++) {
        int l_ret = dap_thread_pool_submit(l_pool, s_simple_task, &l_overflow_arg, NULL, NULL);
        if (l_ret == 0) {
            l_success++;
        } else if (l_ret == -3) {
            l_failed++;
        }
    }

    dap_test_msg("  successful: %d, rejected (queue full): %d", l_success, l_failed);
    dap_assert(l_failed > 0, "Some tasks should be rejected when queue is full");

    // Also test submit_to overflow
    int l_ret = dap_thread_pool_submit_to(l_pool, 0, s_simple_task, &l_overflow_arg, NULL, NULL);
    // May or may not fail depending on timing
    dap_test_msg("  submit_to after overflow: %d", l_ret);

    usleep(200000);
    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Thread pool queue overflow handling works");
}

// ===== Test: graceful shutdown =====

static void test_thread_pool_shutdown(void)
{
    dap_test_msg("Test: Thread pool graceful shutdown");

    atomic_store(&s_task_counter, 0);

    dap_thread_pool_t *l_pool = dap_thread_pool_create(2, 100);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");

    static int l_shutdown_args[5];
    for (int i = 0; i < 5; i++) {
        l_shutdown_args[i] = i + 1;
        dap_thread_pool_submit(l_pool, s_simple_task, &l_shutdown_args[i], NULL, NULL);
    }

    int l_ret = dap_thread_pool_shutdown(l_pool, 5000);
    dap_assert(l_ret == 0, "Shutdown should succeed");

    dap_assert(atomic_load(&s_task_counter) == 15, "All tasks should complete before shutdown");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Thread pool graceful shutdown works");
}

// ===== Test: shutdown rejects new tasks =====

static void test_thread_pool_shutdown_rejects(void)
{
    dap_test_msg("Test: Shutdown rejects new tasks");

    dap_thread_pool_t *l_pool = dap_thread_pool_create(2, 0);
    dap_assert(l_pool != NULL, "Pool creation should succeed");

    // Shutdown
    int l_ret = dap_thread_pool_shutdown(l_pool, 5000);
    dap_assert(l_ret == 0, "Shutdown should succeed");

    // Try to submit after shutdown
    l_ret = dap_thread_pool_submit(l_pool, s_noop_task, NULL, NULL, NULL);
    dap_assert(l_ret == -2, "submit after shutdown should return -2");

    l_ret = dap_thread_pool_submit_to(l_pool, 0, s_noop_task, NULL, NULL, NULL);
    dap_assert(l_ret == -2, "submit_to after shutdown should return -2");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Shutdown correctly rejects new tasks");
}

// ===== Test: double shutdown is safe =====

static void test_thread_pool_double_shutdown(void)
{
    dap_test_msg("Test: Double shutdown is safe (no UB)");

    dap_thread_pool_t *l_pool = dap_thread_pool_create(2, 0);
    dap_assert(l_pool != NULL, "Pool creation should succeed");

    int l_ret = dap_thread_pool_shutdown(l_pool, 5000);
    dap_assert(l_ret == 0, "First shutdown should succeed");

    // Second shutdown — must not crash (double pthread_join guard)
    l_ret = dap_thread_pool_shutdown(l_pool, 5000);
    dap_assert(l_ret == 0, "Second shutdown should succeed (no-op)");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Double shutdown is safe");
}

// ===== Test: mixed submit and submit_to =====

static atomic_int s_mixed_rr_count;
static atomic_int s_mixed_sticky_count;

static void *s_mixed_rr_task(void *a_arg)
{
    UNUSED(a_arg);
    atomic_fetch_add(&s_mixed_rr_count, 1);
    usleep(5000);
    return NULL;
}

static void *s_mixed_sticky_task(void *a_arg)
{
    UNUSED(a_arg);
    atomic_fetch_add(&s_mixed_sticky_count, 1);
    usleep(5000);
    return NULL;
}

static void test_thread_pool_mixed_submit(void)
{
    dap_test_msg("Test: Mixed submit and submit_to");

    atomic_store(&s_mixed_rr_count, 0);
    atomic_store(&s_mixed_sticky_count, 0);

    dap_thread_pool_t *l_pool = dap_thread_pool_create(4, 0);
    dap_assert(l_pool != NULL, "Pool creation should succeed");

    // Interleave round-robin and sticky tasks
    for (int i = 0; i < 20; i++) {
        if (i % 2 == 0) {
            dap_thread_pool_submit(l_pool, s_mixed_rr_task, NULL, NULL, NULL);
        } else {
            dap_thread_pool_submit_to(l_pool, i % 4, s_mixed_sticky_task, NULL, NULL, NULL);
        }
    }

    // Wait
    int l_timeout = 0;
    while ((atomic_load(&s_mixed_rr_count) + atomic_load(&s_mixed_sticky_count)) < 20
            && l_timeout < 100) {
        usleep(50000);
        l_timeout++;
    }

    dap_assert(atomic_load(&s_mixed_rr_count) == 10, "10 round-robin tasks should complete");
    dap_assert(atomic_load(&s_mixed_sticky_count) == 10, "10 sticky tasks should complete");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Mixed submit and submit_to works");
}

// ===== Test: stress with many tasks =====

static void test_thread_pool_stress(void)
{
    dap_test_msg("Test: Thread pool stress (100 tasks)");

    atomic_store(&s_task_counter, 0);
    atomic_store(&s_callback_counter, 0);

    dap_thread_pool_t *l_pool = dap_thread_pool_create(4, 200);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");

    #define STRESS_TASKS 100
    static int l_stress_arg = 1;

    for (int i = 0; i < STRESS_TASKS; i++) {
        int l_ret = dap_thread_pool_submit(l_pool, s_simple_task, &l_stress_arg,
                                           s_task_callback, &l_stress_arg);
        dap_assert(l_ret == 0, "Task submission should succeed");
    }

    int l_timeout = 0;
    while (atomic_load(&s_callback_counter) < STRESS_TASKS && l_timeout < 100) {
        usleep(100000);
        l_timeout++;
    }

    dap_assert(atomic_load(&s_callback_counter) == STRESS_TASKS,
               "All callbacks should be invoked");
    dap_assert(atomic_load(&s_task_counter) == STRESS_TASKS,
               "All tasks should execute");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Thread pool stress test passed");
}

// ===== Test: stress submit_to =====

static atomic_int s_stress_sticky_total;

static void *s_stress_sticky_task(void *a_arg)
{
    UNUSED(a_arg);
    atomic_fetch_add(&s_stress_sticky_total, 1);
    return NULL;
}

static void test_thread_pool_stress_submit_to(void)
{
    dap_test_msg("Test: Stress submit_to (1000 tasks across 4 threads)");

    atomic_store(&s_stress_sticky_total, 0);

    #define STRESS_STICKY_THREADS 4
    #define STRESS_STICKY_PER_THREAD 250
    #define STRESS_STICKY_TOTAL (STRESS_STICKY_THREADS * STRESS_STICKY_PER_THREAD)

    dap_thread_pool_t *l_pool = dap_thread_pool_create(STRESS_STICKY_THREADS, 0);
    dap_assert(l_pool != NULL, "Pool creation should succeed");

    for (uint32_t t = 0; t < STRESS_STICKY_THREADS; t++) {
        for (int s = 0; s < STRESS_STICKY_PER_THREAD; s++) {
            int l_ret = dap_thread_pool_submit_to(l_pool, t,
                                                   s_stress_sticky_task, NULL, NULL, NULL);
            dap_assert(l_ret == 0, "submit_to should succeed");
        }
    }

    int l_timeout = 0;
    while (atomic_load(&s_stress_sticky_total) < STRESS_STICKY_TOTAL && l_timeout < 100) {
        usleep(50000);
        l_timeout++;
    }

    dap_assert(atomic_load(&s_stress_sticky_total) == STRESS_STICKY_TOTAL,
               "All 1000 sticky tasks should complete");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Stress submit_to passed");
}

// ===== Test: callback receives correct pool and thread =====

static dap_thread_pool_t *s_cb_pool_ref = NULL;
static atomic_int s_cb_pool_ok;
static atomic_int s_cb_thread_ok;

static void *s_cb_verify_task(void *a_arg)
{
    UNUSED(a_arg);
    return (void *)(intptr_t)42;
}

static void s_cb_verify_callback(dap_thread_pool_t *a_pool,
                                  dap_thread_t a_worker_thread,
                                  void *a_result,
                                  void *a_arg)
{
    UNUSED(a_arg);

    if (a_pool == s_cb_pool_ref)
        atomic_fetch_add(&s_cb_pool_ok, 1);

    // Worker thread should be valid (non-zero on Linux)
    if (a_worker_thread != 0)
        atomic_fetch_add(&s_cb_thread_ok, 1);

    dap_assert((intptr_t)a_result == 42, "Callback result should be 42");
}

static void test_thread_pool_callback_args(void)
{
    dap_test_msg("Test: Callback receives correct pool and thread");

    atomic_store(&s_cb_pool_ok, 0);
    atomic_store(&s_cb_thread_ok, 0);

    dap_thread_pool_t *l_pool = dap_thread_pool_create(2, 0);
    dap_assert(l_pool != NULL, "Pool creation should succeed");
    s_cb_pool_ref = l_pool;

    for (int i = 0; i < 5; i++) {
        int l_ret = dap_thread_pool_submit(l_pool, s_cb_verify_task, NULL,
                                           s_cb_verify_callback, NULL);
        dap_assert(l_ret == 0, "submit should succeed");
    }

    usleep(200000);

    dap_assert(atomic_load(&s_cb_pool_ok) == 5, "All callbacks should get correct pool pointer");
    dap_assert(atomic_load(&s_cb_thread_ok) == 5, "All callbacks should get valid thread handle");

    dap_thread_pool_delete(l_pool);
    s_cb_pool_ref = NULL;

    dap_pass_msg("Callback receives correct pool and thread");
}

// ===== Main =====

int main(void)
{
    dap_test_msg("=== DAP Thread Pool Unit Tests ===");

    test_thread_pool_create_delete();
    test_thread_pool_auto_cpus();
    test_thread_pool_get_thread_count();
    test_thread_pool_submit_tasks();
    test_thread_pool_submit_to();
    test_thread_pool_submit_to_ordering();
    test_thread_pool_submit_to_invalid_index();
    test_thread_pool_submit_errors();
    test_thread_pool_pending_count();
    test_thread_pool_queue_overflow();
    test_thread_pool_shutdown();
    test_thread_pool_shutdown_rejects();
    test_thread_pool_double_shutdown();
    test_thread_pool_mixed_submit();
    test_thread_pool_stress();
    test_thread_pool_stress_submit_to();
    test_thread_pool_callback_args();

    dap_test_msg("=== All Thread Pool Tests Passed ===");
    return 0;
}
