/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
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
 * @file dap_json_stage1.c
 * @brief Stage 1 dispatch and common functionality
 * 
 * This file contains:
 * - CPU feature detection and caching
 * - SIMD architecture selection (auto and manual)
 * - Global state for Stage 1 dispatch
 */

#include "internal/dap_json_stage1.h"
#include "dap_common.h"
#include "dap_json.h"
#include "dap_cpu_arch.h"

#define LOG_TAG "dap_json_stage1"

/* ========================================================================== */
/*                          GLOBAL STATE                                      */
/* ========================================================================== */

// Global CPU features cache (initialized once at startup or on first use)
dap_cpu_features_t g_dap_json_cpu_features = {0};
bool g_dap_json_cpu_features_initialized = false;

/* ========================================================================== */
/*                     INITIALIZATION                                         */
/* ========================================================================== */

/**
 * @brief Initialize CPU features detection for Stage 1
 */
void dap_json_stage1_init_cpu(void)
{
    if (g_dap_json_cpu_features_initialized) {
        log_it(L_WARNING, "Stage 1 already initialized, skipping");
        return;
    }
    
    g_dap_json_cpu_features = dap_cpu_detect_features();
    g_dap_json_cpu_features_initialized = true;
    
    dap_cpu_arch_t arch = dap_cpu_arch_get();
    log_it(L_INFO, "Stage 1 initialized (arch: %s)", dap_cpu_arch_get_name(arch));
}

/* ========================================================================== */
/*                  ARCHITECTURE SELECTION (WRAPPERS)                         */
/* ========================================================================== */

/**
 * @brief Get currently selected SIMD architecture (wrapper)
 * @details Calls dap_cpu_arch_get() from core module
 * @note This is called by dap_json_stage1_run() dispatch
 */
dap_cpu_arch_t dap_json_stage1_get_arch(void)
{
    return dap_cpu_arch_get();
}

/**
 * @brief Set SIMD architecture manually (wrapper)
 * @param a_arch Architecture to use
 * @return 0 on success, -1 if not available
 * @note This is called by dap_json_set_arch() at the top level
 */
int dap_json_stage1_set_arch(dap_cpu_arch_t a_arch)
{
    return dap_cpu_arch_set(a_arch);
}

