/*
 * Regression: pre-init proc-thread API calls must not crash.
 */

#include <stdbool.h>
#include <stdio.h>

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

int main(void)
{
    dap_proc_thread_deinit();

    if (dap_proc_thread_get_auto() != NULL) {
        fprintf(stderr, "dap_proc_thread_get_auto() must return NULL before init\n");
        return 1;
    }

    if (dap_proc_thread_get_avg_queue_size() != 0) {
        fprintf(stderr, "dap_proc_thread_get_avg_queue_size() must return 0 before init\n");
        return 2;
    }

    if (dap_proc_thread_callback_add(NULL, s_dummy_queue_callback, NULL) == 0) {
        fprintf(stderr, "dap_proc_thread_callback_add(NULL, ...) must fail before init\n");
        return 3;
    }

    if (dap_proc_thread_timer_add_pri(NULL, s_dummy_timer_callback, NULL, 100, false,
                                      DAP_QUEUE_MSG_PRIORITY_NORMAL) == 0) {
        fprintf(stderr, "dap_proc_thread_timer_add_pri(NULL, ...) must fail before init\n");
        return 4;
    }

    return 0;
}
