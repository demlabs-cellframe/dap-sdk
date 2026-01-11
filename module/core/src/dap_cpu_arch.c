/*
 * Authors:
 * Dmitry Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with DAP SDK.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file dap_cpu_arch.c
 * @brief CPU Architecture Definitions Implementation
 * @date 2025-01-08
 */

#include "dap_cpu_arch.h"
#include "dap_cpu_detect.h"
#include "dap_common.h"

#define LOG_TAG "dap_cpu_arch"

/* ========================================================================== */
/*                      GLOBAL ARCHITECTURE STATE                             */
/* ========================================================================== */

/**
 * @brief Global manual architecture override
 * @details DAP_CPU_ARCH_AUTO means no override (use auto-detection)
 */
static dap_cpu_arch_t s_manual_arch = DAP_CPU_ARCH_AUTO;

/* ========================================================================== */
/*                        ARCHITECTURE NAME MAPPING                           */
/* ========================================================================== */

const char* dap_cpu_arch_get_name(dap_cpu_arch_t a_arch)
{
    switch (a_arch) {
        case DAP_CPU_ARCH_AUTO:      return "Auto";
        case DAP_CPU_ARCH_REFERENCE: return "Reference C";
        case DAP_CPU_ARCH_SSE2:      return "SSE2";
        case DAP_CPU_ARCH_AVX2:      return "AVX2";
        case DAP_CPU_ARCH_AVX512:    return "AVX-512";
        case DAP_CPU_ARCH_NEON:      return "NEON";
        case DAP_CPU_ARCH_SVE:       return "SVE";
        case DAP_CPU_ARCH_SVE2:      return "SVE2";
        case DAP_CPU_ARCH_RISC_V:    return "RISC-V";
        default:                     return "Unknown";
    }
}

/* ========================================================================== */
/*                     ARCHITECTURE AVAILABILITY CHECK                        */
/* ========================================================================== */

bool dap_cpu_arch_is_available(dap_cpu_arch_t a_arch)
{
    dap_cpu_features_t features = dap_cpu_detect_features();
    
    switch (a_arch) {
        case DAP_CPU_ARCH_AUTO:
        case DAP_CPU_ARCH_REFERENCE:
            return true;  // Always available
            
        case DAP_CPU_ARCH_SSE2:
            return features.has_sse2;
            
        case DAP_CPU_ARCH_AVX2:
            return features.has_avx2;
            
        case DAP_CPU_ARCH_AVX512:
            return features.has_avx512f && features.has_avx512bw;
            
        case DAP_CPU_ARCH_NEON:
            return features.has_neon;
            
        case DAP_CPU_ARCH_SVE:
            return features.has_sve;
            
        case DAP_CPU_ARCH_SVE2:
            return features.has_sve2;
            
        case DAP_CPU_ARCH_RISC_V:
            return false;  // Not yet implemented
            
        default:
            return false;
    }
}

/* ========================================================================== */
/*                       BEST ARCHITECTURE SELECTION                          */
/* ========================================================================== */

dap_cpu_arch_t dap_cpu_arch_get_best(void)
{
    dap_cpu_features_t features = dap_cpu_detect_features();
    
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    /* x86/x64 priority: AVX-512 > AVX2 > SSE2 > Reference */
    if (features.has_avx512f && features.has_avx512bw) {
        return DAP_CPU_ARCH_AVX512;
    }
    if (features.has_avx2) {
        return DAP_CPU_ARCH_AVX2;
    }
    if (features.has_sse2) {
        return DAP_CPU_ARCH_SSE2;
    }
#elif defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
    /* ARM priority: SVE2 > SVE > NEON > Reference */
    if (features.has_sve2) {
        return DAP_CPU_ARCH_SVE2;
    }
    if (features.has_sve) {
        return DAP_CPU_ARCH_SVE;
    }
    if (features.has_neon) {
        return DAP_CPU_ARCH_NEON;
    }
#endif
    
    /* Fallback to reference implementation */
    return DAP_CPU_ARCH_REFERENCE;
}

/* ========================================================================== */
/*                   GLOBAL ARCHITECTURE STATE MANAGEMENT                     */
/* ========================================================================== */

int dap_cpu_arch_set(dap_cpu_arch_t a_arch)
{
    // AUTO is always valid - resets to auto-detection
    if (a_arch == DAP_CPU_ARCH_AUTO) {
        s_manual_arch = DAP_CPU_ARCH_AUTO;
        log_it(L_INFO, "CPU architecture set to AUTO (will auto-detect)");
        return 0;
    }
    
    // Reference C is always available
    if (a_arch == DAP_CPU_ARCH_REFERENCE) {
        s_manual_arch = DAP_CPU_ARCH_REFERENCE;
        log_it(L_INFO, "CPU architecture manually set to: %s", dap_cpu_arch_get_name(a_arch));
        return 0;
    }
    
    // Check architecture availability using CPU detection
    if (dap_cpu_arch_is_available(a_arch)) {
        s_manual_arch = a_arch;
        log_it(L_INFO, "CPU architecture manually set to: %s", dap_cpu_arch_get_name(a_arch));
        return 0;
    } else {
        log_it(L_WARNING, "Architecture %s not available on this CPU", dap_cpu_arch_get_name(a_arch));
        return -1;
    }
}

dap_cpu_arch_t dap_cpu_arch_get(void)
{
    // If manual override is set and not AUTO, return it
    if (s_manual_arch != DAP_CPU_ARCH_AUTO) {
        return s_manual_arch;
    }
    
    // Otherwise return best available architecture
    return dap_cpu_arch_get_best();
}

