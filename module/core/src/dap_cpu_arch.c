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
    /* ARM priority: SVE > NEON > Reference */
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

