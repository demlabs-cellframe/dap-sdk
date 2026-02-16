/*
 * Regression: proc-thread init must fail when worker binding is unavailable.
 */

#include <stdbool.h>
#include <stdio.h>

#include "dap_proc_thread.h"

static bool s_dummy_queue_callback(void *a_arg)
{
    (void)a_arg;
    return false;
}

int main(void)
{
    dap_proc_thread_deinit();

    int l_init_rc = dap_proc_thread_init(1);
    if (l_init_rc == 0) {
        fprintf(stderr, "dap_proc_thread_init(1) must fail without worker binding\n");
        dap_proc_thread_deinit();
        return 1;
    }

    if (dap_proc_thread_get_count() != 0) {
        fprintf(stderr, "dap_proc_thread_get_count() must be 0 after failed init rollback\n");
        dap_proc_thread_deinit();
        return 2;
    }

    if (dap_proc_thread_callback_add(NULL, s_dummy_queue_callback, NULL) == 0) {
        fprintf(stderr, "callback_add must fail after init rollback\n");
        dap_proc_thread_deinit();
        return 3;
    }

    dap_proc_thread_deinit();
    return 0;
}
