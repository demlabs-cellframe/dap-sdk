/*
 * Integration test for thread pool
 *
 * Tests:
 * - Thread pool creation with auto CPU count
 * - CPU affinity binding
 * - Task submission (round-robin) and execution
 * - Sticky binding (submit_to) with thread affinity
 * - Completion callbacks
 * - Graceful shutdown
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include "dap_test.h"
#include "dap_common.h"
#include "dap_thread_pool.h"

#define LOG_TAG "test_thread_pool_integration"

// Test state
static atomic_int s_task_counter = 0;
static atomic_int s_callback_counter = 0;

/**
 * @brief Simple task function
 */
static void *s_test_task(void *a_arg)
{
    int l_id = *(int *)a_arg;
    dap_test_msg("Task %d executing in thread pool", l_id);

    usleep(50000);  // 50ms

    atomic_fetch_add(&s_task_counter, 1);

    return (void *)(intptr_t)(l_id * 2);
}

/**
 * @brief Task completion callback
 */
static void s_test_callback(dap_thread_pool_t *a_pool,
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

/**
 * @brief Test thread pool lifecycle
 */
static void test_thread_pool_lifecycle(void)
{
    dap_test_msg("Test: Thread pool lifecycle");

    dap_thread_pool_t *l_pool = dap_thread_pool_create(0, 100);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");

    uint32_t l_count = dap_thread_pool_get_thread_count(l_pool);
    dap_test_msg("Thread pool created with %u workers (auto CPU count with affinity)", l_count);
    dap_assert(l_count > 0, "Thread count should be > 0");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Thread pool lifecycle works");
}

/**
 * @brief Test task submission and execution (round-robin)
 */
static void test_thread_pool_tasks(void)
{
    dap_test_msg("Test: Thread pool task execution (round-robin)");

    atomic_store(&s_task_counter, 0);
    atomic_store(&s_callback_counter, 0);

    dap_thread_pool_t *l_pool = dap_thread_pool_create(0, 100);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");

    #define NUM_TASKS 20
    int l_args[NUM_TASKS];

    for (int i = 0; i < NUM_TASKS; i++) {
        l_args[i] = i;
        int l_ret = dap_thread_pool_submit(l_pool, s_test_task, &l_args[i],
                                           s_test_callback, &l_args[i]);
        dap_assert(l_ret == 0, "Task submission should succeed");
    }

    dap_test_msg("Submitted %d tasks, waiting for completion...", NUM_TASKS);

    int l_timeout = 0;
    while (atomic_load(&s_callback_counter) < NUM_TASKS && l_timeout < 50) {
        usleep(100000);
        l_timeout++;
    }

    int l_tasks_completed = atomic_load(&s_task_counter);
    int l_callbacks_invoked = atomic_load(&s_callback_counter);

    dap_test_msg("Tasks completed: %d/%d, Callbacks invoked: %d/%d",
                 l_tasks_completed, NUM_TASKS, l_callbacks_invoked, NUM_TASKS);

    dap_assert(l_tasks_completed == NUM_TASKS, "All tasks should complete");
    dap_assert(l_callbacks_invoked == NUM_TASKS, "All callbacks should be invoked");

    int l_ret = dap_thread_pool_shutdown(l_pool, 5000);
    dap_assert(l_ret == 0, "Shutdown should succeed");

    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Thread pool task execution works correctly");
}

/**
 * @brief Test sticky binding (submit_to) - simulates FSM-like usage
 */

#define STICKY_NUM_FSMS 8
#define STICKY_TASKS_PER_FSM 20

static pthread_t s_sticky_tids[STICKY_NUM_FSMS][STICKY_TASKS_PER_FSM];
static atomic_int s_sticky_counts[STICKY_NUM_FSMS];

typedef struct {
    uint32_t fsm_id;
    uint32_t seq;
    uint32_t thread_idx;
} fsm_task_arg_t;

static void *s_fsm_task(void *a_arg)
{
    fsm_task_arg_t *l_a = (fsm_task_arg_t *)a_arg;
    uint32_t l_fsm = l_a->fsm_id;
    int l_pos = atomic_fetch_add(&s_sticky_counts[l_fsm], 1);

    if (l_pos < STICKY_TASKS_PER_FSM)
        s_sticky_tids[l_fsm][l_pos] = pthread_self();

    usleep(2000);  // 2ms work
    return NULL;
}

static void test_thread_pool_sticky_binding(void)
{
    dap_test_msg("Test: Sticky binding (FSM-like usage pattern)");

    dap_thread_pool_t *l_pool = dap_thread_pool_create(0, 0);
    dap_assert(l_pool != NULL, "Pool creation should succeed");

    uint32_t l_nthreads = dap_thread_pool_get_thread_count(l_pool);
    dap_test_msg("  pool has %u threads, simulating %d FSMs", l_nthreads, STICKY_NUM_FSMS);

    // Reset
    memset(s_sticky_tids, 0, sizeof(s_sticky_tids));
    for (int i = 0; i < STICKY_NUM_FSMS; i++)
        atomic_store(&s_sticky_counts[i], 0);

    // Simulate FSM sticky binding: fsm_id -> thread_idx = fsm_id % nthreads
    fsm_task_arg_t l_args[STICKY_NUM_FSMS][STICKY_TASKS_PER_FSM];

    for (uint32_t fsm = 0; fsm < STICKY_NUM_FSMS; fsm++) {
        uint32_t l_thread_idx = fsm % l_nthreads;
        for (int s = 0; s < STICKY_TASKS_PER_FSM; s++) {
            l_args[fsm][s].fsm_id = fsm;
            l_args[fsm][s].seq = s;
            l_args[fsm][s].thread_idx = l_thread_idx;
            int l_ret = dap_thread_pool_submit_to(l_pool, l_thread_idx,
                                                   s_fsm_task, &l_args[fsm][s],
                                                   NULL, NULL);
            dap_assert(l_ret == 0, "submit_to should succeed");
        }
    }

    // Wait for all
    int l_timeout = 0;
    while (l_timeout < 200) {
        bool l_done = true;
        for (int i = 0; i < STICKY_NUM_FSMS; i++) {
            if (atomic_load(&s_sticky_counts[i]) < STICKY_TASKS_PER_FSM) {
                l_done = false;
                break;
            }
        }
        if (l_done) break;
        usleep(50000);
        l_timeout++;
    }

    // Verify sticky binding: all tasks for the same FSM ran on same pthread
    for (uint32_t fsm = 0; fsm < STICKY_NUM_FSMS; fsm++) {
        int l_count = atomic_load(&s_sticky_counts[fsm]);
        dap_assert(l_count == STICKY_TASKS_PER_FSM, "All tasks for FSM should complete");

        pthread_t l_first_tid = s_sticky_tids[fsm][0];
        for (int s = 1; s < STICKY_TASKS_PER_FSM; s++) {
            dap_assert(pthread_equal(s_sticky_tids[fsm][s], l_first_tid),
                       "All tasks for same FSM must execute on same pthread");
        }

        // FSMs with same thread_idx should share the same pthread
        for (uint32_t fsm2 = fsm + 1; fsm2 < STICKY_NUM_FSMS; fsm2++) {
            uint32_t l_idx1 = fsm % l_nthreads;
            uint32_t l_idx2 = fsm2 % l_nthreads;
            if (l_idx1 == l_idx2) {
                dap_assert(pthread_equal(s_sticky_tids[fsm][0], s_sticky_tids[fsm2][0]),
                           "FSMs mapped to same thread_idx should share pthread");
            } else {
                dap_assert(!pthread_equal(s_sticky_tids[fsm][0], s_sticky_tids[fsm2][0]),
                           "FSMs mapped to different thread_idx should use different pthreads");
            }
        }
    }

    dap_test_msg("  all %d FSMs verified sticky binding", STICKY_NUM_FSMS);

    dap_thread_pool_shutdown(l_pool, 10000);
    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Sticky binding (FSM-like pattern) works correctly");
}

/**
 * @brief Test stress with many concurrent tasks
 */
static void test_thread_pool_stress(void)
{
    dap_test_msg("Test: Thread pool stress (100 concurrent tasks)");

    atomic_store(&s_task_counter, 0);
    atomic_store(&s_callback_counter, 0);

    dap_thread_pool_t *l_pool = dap_thread_pool_create(0, 200);
    dap_assert(l_pool != NULL, "Thread pool creation should succeed");

    #define STRESS_TASKS 100
    static int s_stress_args[STRESS_TASKS];

    for (int i = 0; i < STRESS_TASKS; i++) {
        s_stress_args[i] = i;
        int l_ret = dap_thread_pool_submit(l_pool, s_test_task, &s_stress_args[i],
                                           s_test_callback, &s_stress_args[i]);
        dap_assert(l_ret == 0, "Task submission should succeed");
    }

    dap_test_msg("Submitted %d tasks, waiting for completion...", STRESS_TASKS);

    int l_timeout = 0;
    while (atomic_load(&s_callback_counter) < STRESS_TASKS && l_timeout < 100) {
        usleep(100000);
        l_timeout++;

        if (l_timeout % 10 == 0) {
            dap_test_msg("Progress: %d/%d tasks completed",
                         atomic_load(&s_task_counter), STRESS_TASKS);
        }
    }

    int l_tasks_completed = atomic_load(&s_task_counter);
    int l_callbacks_invoked = atomic_load(&s_callback_counter);

    dap_test_msg("Final: Tasks=%d/%d, Callbacks=%d/%d",
                 l_tasks_completed, STRESS_TASKS, l_callbacks_invoked, STRESS_TASKS);

    dap_assert(l_tasks_completed == STRESS_TASKS, "All stress tasks should complete");
    dap_assert(l_callbacks_invoked == STRESS_TASKS, "All stress callbacks should be invoked");

    dap_thread_pool_shutdown(l_pool, 10000);
    dap_thread_pool_delete(l_pool);

    dap_pass_msg("Thread pool stress test passed");
}

/**
 * @brief Main test suite
 */
int main(void)
{
    dap_test_msg("=== Thread Pool Integration Tests ===");

    test_thread_pool_lifecycle();
    test_thread_pool_tasks();
    test_thread_pool_sticky_binding();
    test_thread_pool_stress();

    dap_test_msg("=== All Thread Pool Integration Tests Passed ===");
    return 0;
}
