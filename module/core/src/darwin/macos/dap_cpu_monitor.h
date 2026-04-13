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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CPU_COUNT 64

#include <stdlib.h>

typedef struct dap_cpu {
    unsigned ncpu;      // CPU core number
    float load;         // Load percentage
    size_t total_time;  // Total CPU time
    size_t idle_time;   // Idle CPU time
} dap_cpu_t;

typedef struct dap_cpu_stats {
    unsigned cpu_cores_count;
    dap_cpu_t cpu_summary;          // Average statistics for all CPUs
    dap_cpu_t cpus[MAX_CPU_COUNT];  // Per-CPU statistics
} dap_cpu_stats_t;

/**
 * @brief dap_cpu_monitor_init Initialize CPU monitoring
 * @return 0 on success, negative on error
 */
int dap_cpu_monitor_init(void);

/**
 * @brief dap_cpu_monitor_deinit Deinitialize CPU monitoring
 */
void dap_cpu_monitor_deinit(void);

/**
 * @brief dap_cpu_get_stats Get current CPU statistics
 * @return dap_cpu_stats_t structure with CPU information
 */
dap_cpu_stats_t dap_cpu_get_stats(void);

#ifdef __cplusplus
}
#endif
