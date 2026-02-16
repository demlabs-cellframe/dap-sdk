/*
 * Regression: stale proc-thread handle after deinit must fail fast.
 */

#include <stdbool.h>
#include <stdio.h>

#if !defined(DAP_OS_WINDOWS)
#include <signal.h>
#include <unistd.h>
#endif

#include "dap_common.h"
#include "dap_events.h"
#include "dap_proc_thread.h"

static bool s_dummy_queue_callback(void *a_arg)
{
    (void)a_arg;
    return false;
}

static void s_dummy_timer_callback(void *a_arg)
{
    (void)a_arg;
}

#if !defined(DAP_OS_WINDOWS)
static void s_alarm_handler(int a_sig)
{
    (void)a_sig;
    _exit(124);
}
#endif

int main(void)
{
#if !defined(DAP_OS_WINDOWS)
    signal(SIGALRM, s_alarm_handler);
    alarm(5);
#endif

    int l_common_rc = dap_common_init("test_proc_thread_stale_handle_after_deinit", NULL);
    if (l_common_rc != 0) {
        fprintf(stderr, "dap_common_init failed: %d\n", l_common_rc);
        return 1;
    }

    int l_events_init_rc = dap_events_init(1, 1000);
    if (l_events_init_rc != 0) {
        fprintf(stderr, "dap_events_init failed: %d\n", l_events_init_rc);
        dap_common_deinit();
        return 2;
    }

    int l_events_start_rc = dap_events_start();
    if (l_events_start_rc != 0) {
        fprintf(stderr, "dap_events_start failed: %d\n", l_events_start_rc);
        dap_events_deinit();
        dap_common_deinit();
        return 3;
    }

    dap_proc_thread_t *l_thread = dap_proc_thread_get(0);
    if (!l_thread) {
        fprintf(stderr, "dap_proc_thread_get(0) returned NULL\n");
        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
        dap_common_deinit();
        return 4;
    }

    dap_proc_thread_deinit();

    int l_cb_rc = dap_proc_thread_callback_add(l_thread, s_dummy_queue_callback, NULL);
    if (l_cb_rc == 0) {
        fprintf(stderr, "callback_add with stale handle must fail\n");
        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
        dap_common_deinit();
        return 5;
    }

    int l_timer_rc = dap_proc_thread_timer_add_pri(l_thread, s_dummy_timer_callback, NULL, 100, true,
                                                    DAP_QUEUE_MSG_PRIORITY_NORMAL);
    if (l_timer_rc == 0) {
        fprintf(stderr, "timer_add_pri with stale handle must fail\n");
        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
        dap_common_deinit();
        return 6;
    }

    dap_events_stop_all();
    dap_events_wait();
    dap_events_deinit();
    dap_common_deinit();

#if !defined(DAP_OS_WINDOWS)
    alarm(0);
#endif

    return 0;
}
