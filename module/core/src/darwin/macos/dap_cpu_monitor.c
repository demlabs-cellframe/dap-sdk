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

#include "dap_cpu_monitor.h"
#include "dap_common.h"

#include <sys/sysctl.h>
#include <sys/types.h>
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <string.h>
#include <stdlib.h>

#define LOG_TAG "dap_cpu_monitor"

static dap_cpu_stats_t s_cpu_stats = {0};
static dap_cpu_t s_cpu_old_stats[MAX_CPU_COUNT] = {0};
static dap_cpu_t s_cpu_summary_old = {0};

// Previous CPU ticks for delta calculation
static uint64_t s_prev_total_ticks[MAX_CPU_COUNT] = {0};
static uint64_t s_prev_idle_ticks[MAX_CPU_COUNT] = {0};
static uint64_t s_prev_summary_total = 0;
static uint64_t s_prev_summary_idle = 0;

/**
 * @brief dap_cpu_monitor_init Initialize CPU monitoring
 * @return 0 on success
 */
int dap_cpu_monitor_init(void)
{
    // Get number of CPUs using sysctl
    int mib[2] = { CTL_HW, HW_NCPU };
    int num_cpus = 0;
    size_t len = sizeof(num_cpus);
    
    if (sysctl(mib, 2, &num_cpus, &len, NULL, 0) < 0) {
        log_it(L_ERROR, "Failed to get CPU count via sysctl");
        return -1;
    }
    
    s_cpu_stats.cpu_cores_count = (unsigned)num_cpus;
    if (s_cpu_stats.cpu_cores_count > MAX_CPU_COUNT) {
        s_cpu_stats.cpu_cores_count = MAX_CPU_COUNT;
    }
    
    log_it(L_DEBUG, "CPU core count: %u", s_cpu_stats.cpu_cores_count);
    
    // Initialize previous stats
    dap_cpu_get_stats();
    
    return 0;
}

/**
 * @brief dap_cpu_monitor_deinit Deinitialize CPU monitoring
 */
void dap_cpu_monitor_deinit(void)
{
    // Nothing to cleanup
}

/**
 * @brief Calculate CPU load percentage
 */
static float s_calculate_load(uint64_t idle_time, uint64_t prev_idle_time,
                              uint64_t total_time, uint64_t prev_total_time)
{
    uint64_t total_diff = total_time - prev_total_time;
    uint64_t idle_diff = idle_time - prev_idle_time;
    
    if (total_diff == 0) {
        return 0.0f;
    }
    
    return (1.0f - (float)idle_diff / (float)total_diff) * 100.0f;
}

/**
 * @brief dap_cpu_get_stats Get CPU statistics
 * @return dap_cpu_stats_t structure with CPU stats
 */
dap_cpu_stats_t dap_cpu_get_stats(void)
{
    natural_t num_cpus = 0;
    processor_info_array_t cpu_info = NULL;
    mach_msg_type_number_t num_cpu_info = 0;
    
    kern_return_t kr = host_processor_info(mach_host_self(),
                                           PROCESSOR_CPU_LOAD_INFO,
                                           &num_cpus,
                                           &cpu_info,
                                           &num_cpu_info);
    
    if (kr != KERN_SUCCESS) {
        log_it(L_ERROR, "host_processor_info failed: %s", mach_error_string(kr));
        return (dap_cpu_stats_t){0};
    }
    
    uint64_t total_user = 0, total_system = 0, total_idle = 0, total_nice = 0;
    
    // Process each CPU
    for (unsigned i = 0; i < num_cpus && i < s_cpu_stats.cpu_cores_count; i++) {
        processor_cpu_load_info_t cpu_load = 
            (processor_cpu_load_info_t)(cpu_info + i * CPU_STATE_MAX);
        
        uint64_t user   = cpu_load->cpu_ticks[CPU_STATE_USER];
        uint64_t system = cpu_load->cpu_ticks[CPU_STATE_SYSTEM];
        uint64_t idle   = cpu_load->cpu_ticks[CPU_STATE_IDLE];
        uint64_t nice   = cpu_load->cpu_ticks[CPU_STATE_NICE];
        
        uint64_t total = user + system + idle + nice;
        
        // Store for summary
        total_user += user;
        total_system += system;
        total_idle += idle;
        total_nice += nice;
        
        // Calculate per-CPU load
        s_cpu_stats.cpus[i].ncpu = i;
        s_cpu_stats.cpus[i].total_time = total;
        s_cpu_stats.cpus[i].idle_time = idle;
        s_cpu_stats.cpus[i].load = s_calculate_load(idle, s_prev_idle_ticks[i],
                                                     total, s_prev_total_ticks[i]);
        
        // Save for next iteration
        s_prev_total_ticks[i] = total;
        s_prev_idle_ticks[i] = idle;
    }
    
    // Calculate summary stats
    uint64_t summary_total = total_user + total_system + total_idle + total_nice;
    s_cpu_stats.cpu_summary.total_time = summary_total;
    s_cpu_stats.cpu_summary.idle_time = total_idle;
    s_cpu_stats.cpu_summary.load = s_calculate_load(total_idle, s_prev_summary_idle,
                                                     summary_total, s_prev_summary_total);
    
    // Save summary for next iteration
    s_prev_summary_total = summary_total;
    s_prev_summary_idle = total_idle;
    
    // Copy old stats
    memcpy(&s_cpu_summary_old, &s_cpu_stats.cpu_summary, sizeof(dap_cpu_t));
    memcpy(s_cpu_old_stats, s_cpu_stats.cpus, sizeof(dap_cpu_t) * s_cpu_stats.cpu_cores_count);
    
    // Free the cpu_info array allocated by host_processor_info
    vm_deallocate(mach_task_self(), (vm_address_t)cpu_info, 
                  num_cpu_info * sizeof(integer_t));
    
    return s_cpu_stats;
}
