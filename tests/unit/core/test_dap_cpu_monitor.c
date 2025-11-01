/**
 * @file test_dap_cpu_monitor.c
 * @brief Unit tests for CPU monitor (Unix-specific)
 * @date 2025
 */

#include <dap_test.h>
#include <dap_common.h>
#include <dap_cpu_monitor.h>
#include <unistd.h>

static void init_test_case(void)
{
    dap_assert(dap_cpu_monitor_init() == 0, "Cpu module init");
    usleep(1000); // wait for new cpu parameters
}

static void deinit_test_case(void)
{
    dap_cpu_monitor_deinit();
}

static void test_cpu_get_stats(void)
{
    dap_cpu_stats_t stat = dap_cpu_get_stats();
    dap_assert(stat.cpu_cores_count > 0, "Check cpu count");
    dap_assert(stat.cpu_summary.total_time > 0, "Check cpu summary total_time");
    dap_assert(stat.cpu_summary.idle_time > 0, "Check cpu summary idle_time");
    dap_assert(stat.cpu_cores_count > 0, "Check cpu count");
    for (unsigned i = 0; i < stat.cpu_cores_count; i++) {
        dap_assert_PIF(stat.cpus[i].ncpu == i, "Check ncpu and index in array");
        dap_assert_PIF(stat.cpus[i].idle_time > 0, "Check cpu idle_time");
        dap_assert_PIF(stat.cpus[i].total_time > 0, "Check cpu total_time");
    }
}

static void test_cpu_get_stats_multiple(void)
{
    dap_cpu_stats_t stat1 = dap_cpu_get_stats();
    usleep(10000); // 10ms
    dap_cpu_stats_t stat2 = dap_cpu_get_stats();
    
    dap_assert(stat1.cpu_cores_count > 0, "First stat: cpu count > 0");
    dap_assert(stat2.cpu_cores_count > 0, "Second stat: cpu count > 0");
    dap_assert(stat1.cpu_cores_count == stat2.cpu_cores_count, "CPU cores count should remain constant");
}

int main(void)
{
    dap_log_level_set(L_CRITICAL);
    dap_print_module_name("dap_cpu_monitor");

    init_test_case();
    test_cpu_get_stats();
    test_cpu_get_stats_multiple();
    deinit_test_case();

    return 0;
}
