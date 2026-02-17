#define _GNU_SOURCE

/*
 * Regression: concurrent dap_events_wait()/dap_events_stop_all() must not crash or corrupt heap.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>

#if !defined(DAP_OS_WINDOWS)
#include <signal.h>
#include <time.h>
#include <unistd.h>
#endif

#include "dap_common.h"
#include "dap_events.h"

#if defined(DAP_OS_WINDOWS)
int main(void)
{
    return 0;
}
#else

enum {
    TEST_ITERS = 1200
};

struct thread_arg {
    atomic_bool *start;
};

static void s_wait_start(atomic_bool *a_start)
{
    while (!atomic_load_explicit(a_start, memory_order_acquire))
        sched_yield();
}

static void *s_wait_thread(void *a_arg)
{
    struct thread_arg *l_arg = (struct thread_arg *)a_arg;
    s_wait_start(l_arg->start);
    (void)dap_events_wait();
    return NULL;
}

static void *s_stop_thread(void *a_arg)
{
    struct thread_arg *l_arg = (struct thread_arg *)a_arg;
    s_wait_start(l_arg->start);
    dap_events_stop_all();
    return NULL;
}

static void s_alarm_handler(int a_sig)
{
    (void)a_sig;
    _exit(124);
}

static int s_join_with_timeout(pthread_t a_thread, int a_timeout_sec)
{
#if defined(__linux__) && !defined(DAP_OS_WINDOWS)
    struct timespec l_deadline = { 0 };
    if (clock_gettime(CLOCK_REALTIME, &l_deadline) != 0)
        return errno ? errno : -1;
    l_deadline.tv_sec += a_timeout_sec;
    return pthread_timedjoin_np(a_thread, NULL, &l_deadline);
#else
    (void)a_timeout_sec;
    return pthread_join(a_thread, NULL);
#endif
}

int main(void)
{
    signal(SIGALRM, s_alarm_handler);
    alarm(45);

    int l_common_rc = dap_common_init("test_events_wait_stop_concurrent_race", NULL);
    if (l_common_rc != 0) {
        fprintf(stderr, "dap_common_init failed: %d\n", l_common_rc);
        return 1;
    }

    for (int i = 0; i < TEST_ITERS; i++) {
        int l_init_rc = dap_events_init(1, 1000);
        if (l_init_rc != 0) {
            fprintf(stderr, "iter=%d dap_events_init failed: %d\n", i, l_init_rc);
            dap_common_deinit();
            return 2;
        }

        int l_start_rc = dap_events_start();
        if (l_start_rc != 0) {
            fprintf(stderr, "iter=%d dap_events_start failed: %d\n", i, l_start_rc);
            dap_events_deinit();
            dap_common_deinit();
            return 3;
        }

        atomic_bool l_start = ATOMIC_VAR_INIT(false);
        struct thread_arg l_arg = { .start = &l_start };

        pthread_t l_wait1 = 0, l_wait2 = 0, l_stop = 0;
        int l_rc_wait1 = pthread_create(&l_wait1, NULL, s_wait_thread, &l_arg);
        int l_rc_wait2 = l_rc_wait1 ? -1 : pthread_create(&l_wait2, NULL, s_wait_thread, &l_arg);
        int l_rc_stop = (l_rc_wait1 || l_rc_wait2) ? -1 : pthread_create(&l_stop, NULL, s_stop_thread, &l_arg);
        if (l_rc_wait1 || l_rc_wait2 || l_rc_stop) {
            fprintf(stderr, "iter=%d pthread_create failed\n", i);
            atomic_store_explicit(&l_start, true, memory_order_release);
            if (!l_rc_wait1)
                (void)s_join_with_timeout(l_wait1, 1);
            if (!l_rc_wait2)
                (void)s_join_with_timeout(l_wait2, 1);
            if (!l_rc_stop)
                (void)s_join_with_timeout(l_stop, 1);
            dap_events_deinit();
            dap_common_deinit();
            return 4;
        }

        atomic_store_explicit(&l_start, true, memory_order_release);

        int l_j1 = s_join_with_timeout(l_wait1, 3);
        int l_j2 = s_join_with_timeout(l_wait2, 3);
        int l_js = s_join_with_timeout(l_stop, 3);
        if (l_j1 || l_j2 || l_js) {
            fprintf(stderr, "iter=%d join timeout/error: wait1=%d wait2=%d stop=%d\n", i, l_j1, l_j2, l_js);
            dap_events_deinit();
            dap_common_deinit();
            return 5;
        }

        dap_events_deinit();
    }

    dap_common_deinit();
    alarm(0);
    return 0;
}

#endif
