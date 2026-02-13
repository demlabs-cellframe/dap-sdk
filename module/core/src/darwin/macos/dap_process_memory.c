/*
 * Authors:
 * Cellframe SDK Team
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2024
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

#include "dap_process_memory.h"
#include "dap_common.h"

#include <mach/mach.h>
#include <mach/task_info.h>
#include <mach/mach_init.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define LOG_TAG "dap_process_mem"

/**
 * @brief Get memory info for a task (internal helper)
 * @param a_task Mach task port
 * @return dap_process_memory_t with memory statistics
 */
static dap_process_memory_t s_get_task_memory(task_t a_task)
{
    dap_process_memory_t l_proc_mem = {0, 0};
    
    if (a_task == MACH_PORT_NULL) {
        return l_proc_mem;
    }
    
    // Use MACH_TASK_BASIC_INFO for modern macOS
    mach_task_basic_info_data_t l_info;
    mach_msg_type_number_t l_info_count = MACH_TASK_BASIC_INFO_COUNT;
    
    kern_return_t kr = task_info(a_task, MACH_TASK_BASIC_INFO, 
                                  (task_info_t)&l_info, &l_info_count);
    
    if (kr != KERN_SUCCESS) {
        log_it(L_WARNING, "task_info failed: %s", mach_error_string(kr));
        return l_proc_mem;
    }
    
    // Convert bytes to kilobytes (to match Linux /proc/self/status format)
    l_proc_mem.vsz = l_info.virtual_size / 1024;   // Virtual memory in KB
    l_proc_mem.rss = l_info.resident_size / 1024;  // Physical memory (RSS) in KB
    
    return l_proc_mem;
}

/**
 * @brief get_proc_mem_current Get memory info for current process
 * @return dap_process_memory_t with vsz and rss in KB
 */
dap_process_memory_t get_proc_mem_current(void)
{
    return s_get_task_memory(mach_task_self());
}

/**
 * @brief get_proc_mem_by_pid Get memory info for process by PID
 * @param a_pid Process ID
 * @return dap_process_memory_t with vsz and rss in KB
 * @note On macOS, getting info for other processes requires privileges
 */
dap_process_memory_t get_proc_mem_by_pid(pid_t a_pid)
{
    dap_process_memory_t l_proc_mem = {0, 0};
    
    // For current process, use direct method
    if (a_pid == getpid()) {
        return get_proc_mem_current();
    }
    
    // For other processes, try task_for_pid (requires root or entitlements)
    task_t l_task = MACH_PORT_NULL;
    kern_return_t kr = task_for_pid(mach_task_self(), a_pid, &l_task);
    
    if (kr != KERN_SUCCESS) {
        // task_for_pid requires special privileges on macOS
        // Fall back to using sysctl for basic info
        int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, a_pid };
        struct kinfo_proc l_kinfo;
        size_t l_len = sizeof(l_kinfo);
        
        if (sysctl(mib, 4, &l_kinfo, &l_len, NULL, 0) == 0 && l_len > 0) {
            // Check if process exists
            if (l_kinfo.kp_proc.p_pid == a_pid) {
                // On modern macOS, detailed memory info for other processes
                // requires task_for_pid which needs special entitlements.
                // We can only confirm the process exists but cannot get memory stats.
                log_it(L_DEBUG, "Process PID %d exists but memory info requires privileges", a_pid);
            }
        } else {
            log_it(L_DEBUG, "Process with PID %d not found or no permission", a_pid);
        }
        
        return l_proc_mem;
    }
    
    l_proc_mem = s_get_task_memory(l_task);
    
    // Deallocate the task port
    mach_port_deallocate(mach_task_self(), l_task);
    
    return l_proc_mem;
}
