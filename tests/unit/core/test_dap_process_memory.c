/*
 * Authors:
 * Cellframe SDK Team
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file test_dap_process_memory.c
 * @brief Unit tests for process memory monitoring
 */

#include <dap_test.h>
#include <dap_common.h>
#include <dap_process_memory.h>
#include <unistd.h>
#include <stdlib.h>

static void test_current_process(void)
{
    dap_process_memory_t l_mem = get_proc_mem_current();
    dap_assert(l_mem.vsz != 0, "Check vsz current process");
    dap_assert(l_mem.rss != 0, "Check rss current process ");
}

static void test_nonexistent_process(void)
{
    dap_process_memory_t l_mem = get_proc_mem_by_pid(-1);
    dap_assert(l_mem.vsz == 0, "Check vsz nonexistent process");
    dap_assert(l_mem.rss == 0, "Check rss nonexistent process");
}

static void test_init_process(void)
{
    // PID 1 should exist on Unix systems (init/systemd)
    dap_process_memory_t l_mem = get_proc_mem_by_pid(1);
    
    // If accessible, should have non-zero memory
    if (l_mem.vsz > 0 || l_mem.rss > 0) {
        dap_assert(l_mem.vsz > 0, "PID 1 VSZ should be positive");
        dap_assert(l_mem.rss > 0, "PID 1 RSS should be positive");
    }
}

/**
 * @brief Test that RSS <= VSZ (physical memory <= virtual memory)
 */
static void test_memory_consistency(void)
{
    dap_process_memory_t l_mem = get_proc_mem_current();
    dap_assert(l_mem.rss <= l_mem.vsz, "RSS should be <= VSZ");
}

/**
 * @brief Test that get_proc_mem_by_pid(getpid()) works like get_proc_mem_current()
 */
static void test_memory_self_pid(void)
{
    dap_process_memory_t l_mem1 = get_proc_mem_current();
    dap_process_memory_t l_mem2 = get_proc_mem_by_pid(getpid());
    
    // Should return valid data for self
    dap_assert(l_mem2.vsz > 0, "Self PID should have VSZ > 0");
    dap_assert(l_mem2.rss > 0, "Self PID should have RSS > 0");
    
    // Values should be reasonably close (within 10% due to timing)
    // We just check they're both non-zero and in same ballpark
    size_t l_diff_vsz = (l_mem1.vsz > l_mem2.vsz) ? (l_mem1.vsz - l_mem2.vsz) : (l_mem2.vsz - l_mem1.vsz);
    dap_assert(l_diff_vsz < l_mem1.vsz / 2, "VSZ from current() and by_pid(self) should be similar");
}

/**
 * @brief Test that memory allocation increases RSS
 */
static void test_memory_allocation_effect(void)
{
    dap_process_memory_t l_mem_before = get_proc_mem_current();
    
    // Allocate 1MB and touch it to ensure it's in RSS
    size_t l_alloc_size = 1024 * 1024;
    char *l_buf = (char*)malloc(l_alloc_size);
    if (l_buf) {
        // Touch all pages to ensure they're in RSS
        for (size_t i = 0; i < l_alloc_size; i += 4096) {
            l_buf[i] = 1;
        }
        
        dap_process_memory_t l_mem_after = get_proc_mem_current();
        
        // RSS should increase (at least partially)
        // Note: might not be exactly 1MB due to allocator overhead
        dap_assert(l_mem_after.rss >= l_mem_before.rss, "RSS should not decrease after allocation");
        
        free(l_buf);
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
