#define _GNU_SOURCE

/*
 * Regression: concurrent callback_add and proc-thread deinit must not deadlock.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>

#if !defined(DAP_OS_WINDOWS)
#include <signal.h>
#include <time.h>
#include <unistd.h>
#endif

#include "dap_common.h"
#include "dap_events.h"
#include "dap_proc_thread.h"

static atomic_bool s_stop = ATOMIC_VAR_INIT(false);

static bool s_dummy_queue_callback(void *a_arg)
{
    (void)a_arg;
    return false;
}

struct producer_arg {
    dap_proc_thread_t *thread;
    uint64_t attempts;
};

static void *s_producer_thread(void *a_arg)
{
    struct producer_arg *l_arg = (struct producer_arg *)a_arg;
    while (!atomic_load_explicit(&s_stop, memory_order_relaxed)) {
        (void)dap_proc_thread_callback_add(l_arg->thread, s_dummy_queue_callback, NULL);
        l_arg->attempts++;
    }
    return NULL;
}

#if !defined(DAP_OS_WINDOWS)
static void s_alarm_handler(int a_sig)
{
    (void)a_sig;
    _exit(124);
}
#endif

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
#if !defined(DAP_OS_WINDOWS)
    signal(SIGALRM, s_alarm_handler);
    alarm(20);
#endif

    int l_common_rc = dap_common_init("test_proc_thread_callback_deinit_race", NULL);
    if (l_common_rc != 0) {
        fprintf(stderr, "dap_common_init failed: %d\n", l_common_rc);
        return 1;
    }

    for (int i = 0; i < 120; i++) {
        int l_events_init_rc = dap_events_init(1, 1000);
        if (l_events_init_rc != 0) {
            fprintf(stderr, "iter=%d dap_events_init failed: %d\n", i, l_events_init_rc);
            dap_common_deinit();
            return 2;
        }

        int l_events_start_rc = dap_events_start();
        if (l_events_start_rc != 0) {
            fprintf(stderr, "iter=%d dap_events_start failed: %d\n", i, l_events_start_rc);
            dap_events_deinit();
            dap_common_deinit();
            return 3;
        }

        dap_proc_thread_t *l_thread = dap_proc_thread_get(0);
        if (!l_thread) {
            fprintf(stderr, "iter=%d dap_proc_thread_get(0) returned NULL\n", i);
            dap_events_stop_all();
            dap_events_wait();
            dap_events_deinit();
            dap_common_deinit();
            return 4;
        }

        atomic_store_explicit(&s_stop, false, memory_order_relaxed);
        struct producer_arg l_arg = { .thread = l_thread, .attempts = 0 };
        pthread_t l_producer = 0;
        if (pthread_create(&l_producer, NULL, s_producer_thread, &l_arg) != 0) {
            fprintf(stderr, "iter=%d pthread_create failed\n", i);
            dap_events_stop_all();
            dap_events_wait();
            dap_events_deinit();
            dap_common_deinit();
            return 5;
        }

#if !defined(DAP_OS_WINDOWS)
        struct timespec l_sleep = { .tv_sec = 0, .tv_nsec = 2 * 1000 * 1000 };
        nanosleep(&l_sleep, NULL);
#endif

        dap_proc_thread_deinit();
        atomic_store_explicit(&s_stop, true, memory_order_relaxed);

        int l_join_rc = s_join_with_timeout(l_producer, 2);
        if (l_join_rc != 0) {
            fprintf(stderr, "iter=%d producer join timeout/error: %d\n", i, l_join_rc);
            dap_events_stop_all();
            dap_events_wait();
            dap_events_deinit();
            dap_common_deinit();
            return 6;
        }

        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
    }

    dap_common_deinit();

#if !defined(DAP_OS_WINDOWS)
    alarm(0);
#endif

    return 0;
}
