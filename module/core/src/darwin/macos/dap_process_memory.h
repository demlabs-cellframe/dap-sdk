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

#include <stdint.h>
#include <sys/types.h>

typedef struct dap_process_memory {
    size_t vsz;  // Virtual memory size (KB)
    size_t rss;  // Resident set size / physical memory (KB)
} dap_process_memory_t;

/**
 * @brief get_proc_mem_current Get memory info for current process
 * @return dap_process_memory_t with vsz and rss in KB
 */
dap_process_memory_t get_proc_mem_current(void);

/**
 * @brief get_proc_mem_by_pid Get memory info for process by PID
 * @param a_pid Process ID
 * @return dap_process_memory_t with vsz and rss in KB
 * @note On macOS, getting info for other processes may require privileges
 */
dap_process_memory_t get_proc_mem_by_pid(pid_t a_pid);

#ifdef __cplusplus
}
#endif
