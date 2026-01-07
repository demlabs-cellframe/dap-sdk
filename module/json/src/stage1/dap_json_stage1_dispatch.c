/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
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
 * @file dap_json_stage1_dispatch.c
 * @brief Global CPU features cache for Stage 1 dispatch
 */

#include "internal/dap_json_stage1.h"
#include "dap_common.h"

#define LOG_TAG "dap_json_stage1_dispatch"

// Global CPU features cache (initialized once at startup or on first use)
dap_cpu_features_t g_dap_json_cpu_features = {0};
bool g_dap_json_cpu_features_initialized = false;

/**
 * @brief Initialize CPU features detection for Stage 1 dispatch
 */
void dap_json_stage1_init_dispatch(void)
{
    if (g_dap_json_cpu_features_initialized) {
        log_it(L_WARNING, "Stage 1 dispatch already initialized, skipping");
        return;
    }
    
    g_dap_json_cpu_features = dap_cpu_detect_features();
    g_dap_json_cpu_features_initialized = true;
    
    log_it(L_INFO, "Stage 1 dispatch initialized");
    
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    if (g_dap_json_cpu_features.has_avx512f && 
        g_dap_json_cpu_features.has_avx512dq && 
        g_dap_json_cpu_features.has_avx512bw) {
        log_it(L_INFO, "  Selected implementation: AVX-512 (64 bytes/iteration)");
    } else if (g_dap_json_cpu_features.has_avx2) {
        log_it(L_INFO, "  Selected implementation: AVX2 (32 bytes/iteration)");
    } else if (g_dap_json_cpu_features.has_sse2) {
        log_it(L_INFO, "  Selected implementation: SSE2 (16 bytes/iteration)");
    } else {
        log_it(L_INFO, "  Selected implementation: Reference C (portable)");
    }
#elif defined(__ARM_NEON) || defined(__aarch64__)
    if (g_dap_json_cpu_features.has_neon) {
        log_it(L_INFO, "  Selected implementation: NEON (16 bytes/iteration)");
    } else {
        log_it(L_INFO, "  Selected implementation: Reference C (portable)");
    }
#else
    log_it(L_INFO, "  Selected implementation: Reference C (portable)");
#endif
}

