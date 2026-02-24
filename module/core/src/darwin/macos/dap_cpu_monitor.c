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
    int l_mib[2] = { CTL_HW, HW_NCPU };
    int l_num_cpus = 0;
    size_t l_len = sizeof(l_num_cpus);
    
    if (sysctl(l_mib, 2, &l_num_cpus, &l_len, NULL, 0) < 0) {
        log_it(L_ERROR, "Failed to get CPU count via sysctl");
        return -1;
    }
    
    s_cpu_stats.cpu_cores_count = (unsigned)l_num_cpus;
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
 * @param a_idle_time Current idle ticks
 * @param a_prev_idle_time Previous idle ticks
 * @param a_total_time Current total ticks
 * @param a_prev_total_time Previous total ticks
 * @return Load percentage [0..100]
 */
static float s_calculate_load(uint64_t a_idle_time, uint64_t a_prev_idle_time,
                              uint64_t a_total_time, uint64_t a_prev_total_time)
{
    uint64_t l_total_diff = a_total_time - a_prev_total_time;
    uint64_t l_idle_diff = a_idle_time - a_prev_idle_time;
    
    if (l_total_diff == 0) {
        return 0.0f;
    }
    
    return (1.0f - (float)l_idle_diff / (float)l_total_diff) * 100.0f;
}

/**
 * @brief dap_cpu_get_stats Get CPU statistics
 * @return dap_cpu_stats_t structure with CPU stats
 */
dap_cpu_stats_t dap_cpu_get_stats(void)
{
    natural_t l_num_cpus = 0;
    processor_info_array_t l_cpu_info = NULL;
    mach_msg_type_number_t l_num_cpu_info = 0;
    
    kern_return_t l_kr = host_processor_info(mach_host_self(),
                                             PROCESSOR_CPU_LOAD_INFO,
                                             &l_num_cpus,
                                             &l_cpu_info,
                                             &l_num_cpu_info);
    
    if (l_kr != KERN_SUCCESS) {
        log_it(L_ERROR, "host_processor_info failed: %s", mach_error_string(l_kr));
        return (dap_cpu_stats_t){0};
    }
    
    uint64_t l_total_user = 0, l_total_system = 0, l_total_idle = 0, l_total_nice = 0;
    
    // Process each CPU
    for (unsigned i = 0; i < l_num_cpus && i < s_cpu_stats.cpu_cores_count; i++) {
        processor_cpu_load_info_t l_cpu_load = 
            (processor_cpu_load_info_t)(l_cpu_info + i * CPU_STATE_MAX);
        
        uint64_t l_user   = l_cpu_load->cpu_ticks[CPU_STATE_USER];
        uint64_t l_system = l_cpu_load->cpu_ticks[CPU_STATE_SYSTEM];
        uint64_t l_idle   = l_cpu_load->cpu_ticks[CPU_STATE_IDLE];
        uint64_t l_nice   = l_cpu_load->cpu_ticks[CPU_STATE_NICE];
        
        uint64_t l_total = l_user + l_system + l_idle + l_nice;
        
        // Store for summary
        l_total_user += l_user;
        l_total_system += l_system;
        l_total_idle += l_idle;
        l_total_nice += l_nice;
        
        // Calculate per-CPU load
        s_cpu_stats.cpus[i].ncpu = i;
        s_cpu_stats.cpus[i].total_time = l_total;
        s_cpu_stats.cpus[i].idle_time = l_idle;
        s_cpu_stats.cpus[i].load = s_calculate_load(l_idle, s_prev_idle_ticks[i],
                                                     l_total, s_prev_total_ticks[i]);
        
        // Save for next iteration
        s_prev_total_ticks[i] = l_total;
        s_prev_idle_ticks[i] = l_idle;
    }
    
    // Calculate summary stats
    uint64_t l_summary_total = l_total_user + l_total_system + l_total_idle + l_total_nice;
    s_cpu_stats.cpu_summary.total_time = l_summary_total;
    s_cpu_stats.cpu_summary.idle_time = l_total_idle;
    s_cpu_stats.cpu_summary.load = s_calculate_load(l_total_idle, s_prev_summary_idle,
                                                     l_summary_total, s_prev_summary_total);
    
    // Save summary for next iteration
    s_prev_summary_total = l_summary_total;
    s_prev_summary_idle = l_total_idle;
    
    // Copy old stats
    memcpy(&s_cpu_summary_old, &s_cpu_stats.cpu_summary, sizeof(dap_cpu_t));
    memcpy(s_cpu_old_stats, s_cpu_stats.cpus, sizeof(dap_cpu_t) * s_cpu_stats.cpu_cores_count);
    
    // Free the cpu_info array allocated by host_processor_info
    vm_deallocate(mach_task_self(), (vm_address_t)l_cpu_info, 
                  l_num_cpu_info * sizeof(integer_t));
    
    return s_cpu_stats;
}
