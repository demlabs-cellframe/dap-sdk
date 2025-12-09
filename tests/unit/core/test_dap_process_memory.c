/**
 * @file test_dap_process_memory.c
 * @brief Unit tests for process memory monitoring (Unix-specific)
 * @date 2025
 */

#include <dap_test.h>
#include <dap_common.h>
#include <dap_process_memory.h>

static void test_current_process(void)
{
    dap_process_memory_t mem = get_proc_mem_current();
    dap_assert(mem.vsz != 0, "Check vsz current process");
    dap_assert(mem.rss != 0, "Check rss current process ");
}

static void test_nonexistent_process(void)
{
    dap_process_memory_t mem = get_proc_mem_by_pid(-1);
    dap_assert(mem.vsz == 0, "Check vsz nonexistent process");
    dap_assert(mem.rss == 0, "Check rss nonexistent process");
}

static void test_init_process(void)
{
    // PID 1 should exist on Unix systems (init/systemd)
    dap_process_memory_t mem = get_proc_mem_by_pid(1);
    
    // If accessible, should have non-zero memory
    if (mem.vsz > 0 || mem.rss > 0) {
        dap_assert(mem.vsz > 0, "PID 1 VSZ should be positive");
        dap_assert(mem.rss > 0, "PID 1 RSS should be positive");
    }
}

int main(void)
{
    dap_log_level_set(L_CRITICAL);
    dap_print_module_name("dap_process_memory");

    test_current_process();
    test_nonexistent_process();
    test_init_process();

    return 0;
}
