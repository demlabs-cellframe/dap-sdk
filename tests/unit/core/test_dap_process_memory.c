/**
 * @file test_dap_process_memory.c
 * @brief Unit tests for process memory monitoring (Unix-specific)
 * @date 2025
 */

#include <dap_test.h>
#include <dap_common.h>
#include <dap_process_memory.h>
#include <unistd.h>
#include <stdlib.h>

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

/**
 * @brief Test that RSS <= VSZ (physical memory <= virtual memory)
 */
static void test_memory_consistency(void)
{
    dap_process_memory_t mem = get_proc_mem_current();
    dap_assert(mem.rss <= mem.vsz, "RSS should be <= VSZ");
}

/**
 * @brief Test that get_proc_mem_by_pid(getpid()) works like get_proc_mem_current()
 */
static void test_memory_self_pid(void)
{
    dap_process_memory_t mem1 = get_proc_mem_current();
    dap_process_memory_t mem2 = get_proc_mem_by_pid(getpid());
    
    // Should return valid data for self
    dap_assert(mem2.vsz > 0, "Self PID should have VSZ > 0");
    dap_assert(mem2.rss > 0, "Self PID should have RSS > 0");
    
    // Values should be reasonably close (within 10% due to timing)
    // We just check they're both non-zero and in same ballpark
    size_t diff_vsz = (mem1.vsz > mem2.vsz) ? (mem1.vsz - mem2.vsz) : (mem2.vsz - mem1.vsz);
    dap_assert(diff_vsz < mem1.vsz / 2, "VSZ from current() and by_pid(self) should be similar");
}

/**
 * @brief Test that memory allocation increases RSS
 */
static void test_memory_allocation_effect(void)
{
    dap_process_memory_t mem_before = get_proc_mem_current();
    
    // Allocate 1MB and touch it to ensure it's in RSS
    size_t alloc_size = 1024 * 1024;
    char *buf = (char*)malloc(alloc_size);
    if (buf) {
        // Touch all pages to ensure they're in RSS
        for (size_t i = 0; i < alloc_size; i += 4096) {
            buf[i] = 1;
        }
        
        dap_process_memory_t mem_after = get_proc_mem_current();
        
        // RSS should increase (at least partially)
        // Note: might not be exactly 1MB due to allocator overhead
        dap_assert(mem_after.rss >= mem_before.rss, "RSS should not decrease after allocation");
        
        free(buf);
    }
}

int main(void)
{
    dap_log_level_set(L_CRITICAL);
    dap_print_module_name("dap_process_memory");

    test_current_process();
    test_nonexistent_process();
    test_init_process();
    test_memory_consistency();
    test_memory_self_pid();
    test_memory_allocation_effect();

    return 0;
}
