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
#include "dap_json.h"
#include "dap_cpu_arch.h"

#define LOG_TAG "dap_json_stage1_dispatch"

// Global CPU features cache (initialized once at startup or on first use)
dap_cpu_features_t g_dap_json_cpu_features = {0};
bool g_dap_json_cpu_features_initialized = false;

// Manual architecture override (DAP_CPU_ARCH_AUTO = no override)
static dap_cpu_arch_t s_manual_arch = DAP_CPU_ARCH_AUTO;

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

/* ========================================================================== */
/*                  Manual Architecture Selection API                         */
/* ========================================================================== */

/**
 * @brief Get currently selected SIMD architecture
 * @details Returns manual override if set, otherwise returns auto-detected
 */
dap_cpu_arch_t dap_json_get_simd_arch(void)
{
    // If manual override is set and not AUTO, return it
    if (s_manual_arch != DAP_CPU_ARCH_AUTO) {
        return s_manual_arch;
    }
    
    // Otherwise return auto-detected architecture
    if (!g_dap_json_cpu_features_initialized) {
        dap_json_stage1_init_dispatch();
    }
    
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    if (g_dap_json_cpu_features.has_avx512f && 
        g_dap_json_cpu_features.has_avx512dq && 
        g_dap_json_cpu_features.has_avx512bw) {
        return DAP_CPU_ARCH_AVX512;
    } else if (g_dap_json_cpu_features.has_avx2) {
        return DAP_CPU_ARCH_AVX2;
    } else if (g_dap_json_cpu_features.has_sse2) {
        return DAP_CPU_ARCH_SSE2;
    }
#elif defined(__ARM_NEON) || defined(__aarch64__)
    if (g_dap_json_cpu_features.has_neon) {
        return DAP_CPU_ARCH_NEON;
    }
#endif
    
    return DAP_CPU_ARCH_REFERENCE;
}

/**
 * @brief Set SIMD architecture manually (for testing/benchmarking)
 * @param a_arch Architecture to use
 * @return true if architecture is available and set, false otherwise
 */
bool dap_json_set_simd_arch(dap_cpu_arch_t a_arch)
{
    // Initialize CPU detection if not done yet
    if (!g_dap_json_cpu_features_initialized) {
        dap_json_stage1_init_dispatch();
    }
    
    // AUTO is always valid - resets to auto-detection
    if (a_arch == DAP_CPU_ARCH_AUTO) {
        s_manual_arch = DAP_CPU_ARCH_AUTO;
        log_it(L_INFO, "SIMD architecture set to AUTO (will auto-detect)");
        return true;
    }
    
    // Reference C is always available
    if (a_arch == DAP_CPU_ARCH_REFERENCE) {
        s_manual_arch = DAP_CPU_ARCH_REFERENCE;
        log_it(L_INFO, "SIMD architecture manually set to: Reference C");
        return true;
    }
    
    // Check architecture availability using core module
    bool available = dap_cpu_arch_is_available(a_arch);
    const char *arch_name = dap_cpu_arch_get_name(a_arch);
    
    if (available) {
        s_manual_arch = a_arch;
        log_it(L_INFO, "SIMD architecture manually set to: %s", arch_name);
        return true;
    } else {
        log_it(L_WARNING, "Architecture %s not available on this CPU", arch_name);
        return false;
    }
}

/**
 * @brief Get human-readable name of architecture
 * @note Wrapper around dap_cpu_arch_get_name from core module
 */
const char* dap_json_get_arch_name(dap_cpu_arch_t a_arch)
{
    return dap_cpu_arch_get_name(a_arch);
}
