/*
 * Regression: dap_events_deinit() must fully cleanup even without explicit dap_events_wait().
 */

#include <stdio.h>

#if !defined(DAP_OS_WINDOWS)
#include <signal.h>
#include <unistd.h>
#endif

#include "dap_common.h"
#include "dap_events.h"

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
    alarm(8);
#endif

    int l_common_rc = dap_common_init("test_events_deinit_without_wait", NULL);
    if (l_common_rc != 0) {
        fprintf(stderr, "dap_common_init failed: %d\n", l_common_rc);
        return 1;
    }

    for (int i = 0; i < 2; i++) {
        int l_init_rc = dap_events_init(1, 1000);
        if (l_init_rc != 0) {
            fprintf(stderr, "dap_events_init (no-start cycle) failed: %d\n", l_init_rc);
            dap_common_deinit();
            return 2;
        }
        dap_events_deinit();
    }

    int l_init_rc = dap_events_init(1, 1000);
    if (l_init_rc != 0) {
        fprintf(stderr, "dap_events_init failed: %d\n", l_init_rc);
        dap_common_deinit();
        return 3;
    }

    int l_start_rc = dap_events_start();
    if (l_start_rc != 0) {
        fprintf(stderr, "dap_events_start failed: %d\n", l_start_rc);
        dap_events_deinit();
        dap_common_deinit();
        return 4;
    }

    dap_events_deinit();

    l_init_rc = dap_events_init(1, 1000);
    if (l_init_rc != 0) {
        fprintf(stderr, "second dap_events_init failed: %d\n", l_init_rc);
        dap_common_deinit();
        return 5;
    }

    l_start_rc = dap_events_start();
    if (l_start_rc != 0) {
        fprintf(stderr, "second dap_events_start failed: %d\n", l_start_rc);
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
