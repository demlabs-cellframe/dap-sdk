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
            return true;

#if DAP_CPU_DETECT_X86
        case DAP_CPU_ARCH_SSE2:   return features.has_sse2;
        case DAP_CPU_ARCH_AVX2:   return features.has_avx2;
        case DAP_CPU_ARCH_AVX512: return features.has_avx512f && features.has_avx512bw;
#endif

#if DAP_CPU_DETECT_ARM
        case DAP_CPU_ARCH_NEON:   return features.has_neon;
        case DAP_CPU_ARCH_SVE:    return features.has_sve;
        case DAP_CPU_ARCH_SVE2:   return features.has_sve2;
#endif

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
    
#if DAP_CPU_DETECT_X86
    if (features.has_avx512f && features.has_avx512bw)
        return DAP_CPU_ARCH_AVX512;
    if (features.has_avx2)
        return DAP_CPU_ARCH_AVX2;
    if (features.has_sse2)
        return DAP_CPU_ARCH_SSE2;
#elif DAP_CPU_DETECT_ARM
    if (features.has_sve2)
        return DAP_CPU_ARCH_SVE2;
    if (features.has_sve)
        return DAP_CPU_ARCH_SVE;
    if (features.has_neon)
        return DAP_CPU_ARCH_NEON;
#endif
    
    /* Fallback to reference implementation */
    return DAP_CPU_ARCH_REFERENCE;
}

/* ========================================================================== */
/*                    ALGORITHM CLASS REGISTRY                                */
/* ========================================================================== */

#define DAP_ALGO_CLASS_MAX_REGISTERED 32

static const char *s_algo_class_names[DAP_ALGO_CLASS_MAX_REGISTERED] = { [0] = "default" };
static uint32_t    s_algo_class_next_id = 1;

dap_algo_class_t dap_algo_class_register(const char *a_name)
{
    uint32_t l_id = s_algo_class_next_id;
    if (l_id >= DAP_ALGO_CLASS_MAX_REGISTERED) {
        log_it(L_ERROR, "Algorithm class registry full (max %d)", DAP_ALGO_CLASS_MAX_REGISTERED);
        return DAP_ALGO_CLASS_DEFAULT;
    }
    s_algo_class_names[l_id] = a_name;
    s_algo_class_next_id = l_id + 1;
    log_it(L_DEBUG, "Registered algo class %u: \"%s\"", l_id, a_name);
    return (dap_algo_class_t)l_id;
}

const char *dap_algo_class_get_name(dap_algo_class_t a_class)
{
    if (a_class < s_algo_class_next_id)
        return s_algo_class_names[a_class];
    return "unknown";
}

/* ========================================================================== */
/*                  CPU TUNING RULES (DYNAMIC REGISTRY)                       */
/* ========================================================================== */

#define DAP_TUNE_RULES_MAX 64

static dap_cpu_tune_rule_t s_tune_rules[DAP_TUNE_RULES_MAX];
static size_t              s_tune_rules_count = 0;

int dap_cpu_tune_add(const dap_cpu_tune_rule_t *a_rule)
{
    if (s_tune_rules_count >= DAP_TUNE_RULES_MAX) {
        log_it(L_ERROR, "CPU tune rules table full (max %d)", DAP_TUNE_RULES_MAX);
        return -1;
    }
    s_tune_rules[s_tune_rules_count++] = *a_rule;
    return 0;
}

int dap_cpu_tune_add_rules(const dap_cpu_tune_rule_t *a_rules, size_t a_count)
{
    for (size_t i = 0; i < a_count; i++) {
        if (dap_cpu_tune_add(&a_rules[i]) != 0)
            return -1;
    }
    return 0;
}

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

    for (size_t i = 0; i < s_tune_rules_count; i++) {
        const dap_cpu_tune_rule_t *r = &s_tune_rules[i];
        if (r->vendor != DAP_CPU_VENDOR_UNKNOWN && r->vendor != l_feat.vendor)
            continue;
#if DAP_CPU_DETECT_X86
        if (l_feat.x86_family < r->family_min || l_feat.x86_family > r->family_max)
            continue;
        if (l_feat.x86_model < r->model_min || l_feat.x86_model > r->model_max)
            continue;
#endif
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

