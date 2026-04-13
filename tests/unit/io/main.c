#include "dap_common.h"
#include "dap_traffic_track_test.h"

#ifndef DAP_NETWORK_MONITOR_TEST_OFF
#include "linux/dap_network_monitor.h"
#include "dap_network_monitor_test.h"
#endif

int main(int argc, const char * argv[]) {
    //dap_log_level_set(L_CRITICAL);
    //dap_traffic_track_tests_run();
    return 0;
#ifndef DAP_NETWORK_MONITOR_TEST_OFF
    dap_network_monitor_test_run();
#endif
}

