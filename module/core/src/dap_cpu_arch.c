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
/*                  CPU-SPECIFIC TUNING RULES TABLE                           */
/* ========================================================================== */

/**
 * @brief Rule for overriding default arch selection on specific CPUs
 *
 * Matched top-to-bottom; first match wins. If algo_class == DEFAULT,
 * the rule applies to every class that has no more-specific match.
 */
typedef struct {
    dap_cpu_vendor_t vendor;
    uint32_t family_min, family_max;
    uint32_t model_min,  model_max;
    dap_algo_class_t algo_class;
    dap_cpu_arch_t preferred_arch;
} dap_cpu_tune_rule_t;

/*
 * Tuning table: first matching rule wins.
 *
 * AMD Zen4 (Raphael/Phoenix): family 0x19, model 0x60-0x7F / 0x70-0x7F
 *   AVX-512 on Zen4 uses double-pumping (256-bit units), causing frequency
 *   throttling on sustained SIMD workloads like NTT butterfly.  AVX2 is
 *   faster in practice.
 *
 * AMD Zen5 (Granite Ridge / Strix Point): family 0x1A
 *   512-bit data paths present but still observed to throttle under
 *   sustained load; conservatively cap NTT at AVX2.
 *
 * Intel Xeon / Core (all families): no rules needed — AVX-512 runs
 *   at full width without throttling on server parts (Skylake-X+),
 *   and desktop Alder/Raptor Lake simply lacks AVX-512, so
 *   dap_cpu_arch_get_best() already falls back to AVX2.
 */
static const dap_cpu_tune_rule_t s_tune_rules[] = {
    /* AMD Zen4: family 0x19, model range 0x60..0x7F — NTT prefers AVX2 */
    { DAP_CPU_VENDOR_AMD, 0x19, 0x19, 0x60, 0x7F,
      DAP_ALGO_CLASS_NTT, DAP_CPU_ARCH_AVX2 },

    /* AMD Zen5: family 0x1A, all models — NTT prefers AVX2 */
    { DAP_CPU_VENDOR_AMD, 0x1A, 0x1A, 0x00, 0xFF,
      DAP_ALGO_CLASS_NTT, DAP_CPU_ARCH_AVX2 },
};

#define DAP_TUNE_RULES_COUNT  (sizeof(s_tune_rules) / sizeof(s_tune_rules[0]))

/* ========================================================================== */
/*              PER-ALGORITHM-CLASS BEST ARCHITECTURE SELECTION                */
/* ========================================================================== */

dap_cpu_arch_t dap_cpu_arch_get_best_for(dap_algo_class_t a_class)
{
    if (s_manual_arch != DAP_CPU_ARCH_AUTO)
        return s_manual_arch;

    dap_cpu_arch_t l_best = dap_cpu_arch_get_best();

    if (a_class == DAP_ALGO_CLASS_DEFAULT)
        return l_best;

    dap_cpu_features_t l_feat = dap_cpu_detect_features();

    for (size_t i = 0; i < DAP_TUNE_RULES_COUNT; i++) {
        const dap_cpu_tune_rule_t *r = &s_tune_rules[i];
        if (r->vendor != DAP_CPU_VENDOR_UNKNOWN && r->vendor != l_feat.vendor)
            continue;
        if (l_feat.x86_family < r->family_min || l_feat.x86_family > r->family_max)
            continue;
        if (l_feat.x86_model < r->model_min || l_feat.x86_model > r->model_max)
            continue;
        if (r->algo_class != DAP_ALGO_CLASS_DEFAULT && r->algo_class != a_class)
            continue;

        if (r->preferred_arch < l_best)
            return r->preferred_arch;
        break;
    }

    return l_best;
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

