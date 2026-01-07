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

#include "dap_cpu_detect.h"
#include "dap_common.h"
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #include <cpuid.h>
#elif defined(__aarch64__) || defined(__arm__)
    #if defined(__linux__)
        #include <sys/auxv.h>
        #include <asm/hwcap.h>
    #endif
#endif

#define LOG_TAG "dap_cpu_detect"

// Cached features (initialized once)
static dap_cpu_features_t s_cached_features = {0};
static bool s_features_detected = false;
static char s_cpu_name[64] = "Unknown CPU";

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

/**
 * @brief Detect x86/x64 CPU features using CPUID
 */
static void s_detect_x86_features(dap_cpu_features_t *a_features)
{
    unsigned int eax, ebx, ecx, edx;
    
    a_features->is_x86 = true;
    a_features->is_64bit = sizeof(void*) == 8;
    a_features->cache_line_size = 64;  // Common for modern x86
    
    // Check CPUID support
    if (!__get_cpuid(0, &eax, &ebx, &ecx, &edx)) {
        return;
    }
    
    // Get CPU brand string
    unsigned int brand[12] = {0};
    if (__get_cpuid(0x80000000, &eax, &ebx, &ecx, &edx) && eax >= 0x80000004) {
        __get_cpuid(0x80000002, &brand[0], &brand[1], &brand[2], &brand[3]);
        __get_cpuid(0x80000003, &brand[4], &brand[5], &brand[6], &brand[7]);
        __get_cpuid(0x80000004, &brand[8], &brand[9], &brand[10], &brand[11]);
        snprintf(s_cpu_name, sizeof(s_cpu_name), "%s", (char*)brand);
        // Trim leading spaces
        char *p = s_cpu_name;
        while (*p == ' ') p++;
        if (p != s_cpu_name) {
            memmove(s_cpu_name, p, strlen(p) + 1);
        }
    }
    
    // CPUID.1:EDX - Feature flags
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        a_features->has_sse2 = (edx & (1 << 26)) != 0;
        a_features->has_popcnt = (ecx & (1 << 23)) != 0;
        a_features->has_sse4_1 = (ecx & (1 << 19)) != 0;
        a_features->has_sse4_2 = (ecx & (1 << 20)) != 0;
        a_features->has_avx = (ecx & (1 << 28)) != 0;
    }
    
    // CPUID.7:EBX - Extended features
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        a_features->has_avx2 = (ebx & (1 << 5)) != 0;
        a_features->has_bmi = (ebx & (1 << 3)) != 0;
        a_features->has_bmi2 = (ebx & (1 << 8)) != 0;
        a_features->has_avx512f = (ebx & (1 << 16)) != 0;
        a_features->has_avx512dq = (ebx & (1 << 17)) != 0;
        a_features->has_avx512bw = (ebx & (1 << 30)) != 0;
        a_features->has_avx512vl = (ebx & (1 << 31)) != 0;
    }
}

#elif defined(__aarch64__) || defined(__arm__)

/**
 * @brief Detect ARM CPU features
 */
static void s_detect_arm_features(dap_cpu_features_t *a_features)
{
    a_features->is_arm = true;
    a_features->is_64bit = sizeof(void*) == 8;
    a_features->cache_line_size = 64;  // Common for modern ARM
    
#if defined(__aarch64__)
    // NEON is baseline for ARM64
    a_features->has_neon = true;
    snprintf(s_cpu_name, sizeof(s_cpu_name), "ARM64 (AArch64)");
#elif defined(__ARM_NEON)
    a_features->has_neon = true;
    snprintf(s_cpu_name, sizeof(s_cpu_name), "ARM32 with NEON");
#else
    snprintf(s_cpu_name, sizeof(s_cpu_name), "ARM32");
#endif
    
#if defined(__linux__)
    // Use getauxval to detect SVE on Linux
    unsigned long hwcap = getauxval(AT_HWCAP);
    unsigned long hwcap2 = getauxval(AT_HWCAP2);
    
    #ifdef HWCAP_SVE
        a_features->has_sve = (hwcap & HWCAP_SVE) != 0;
    #endif
    
    #ifdef HWCAP2_SVE2
        a_features->has_sve2 = (hwcap2 & HWCAP2_SVE2) != 0;
    #endif
#endif
}

#else

/**
 * @brief Fallback for unknown architectures
 */
static void s_detect_generic_features(dap_cpu_features_t *a_features)
{
    a_features->is_64bit = sizeof(void*) == 8;
    a_features->cache_line_size = 64;
    snprintf(s_cpu_name, sizeof(s_cpu_name), "Generic CPU");
}

#endif

/**
 * @brief Detect CPU features (implementation)
 */
dap_cpu_features_t dap_cpu_detect_features(void)
{
    if (!s_features_detected) {
        memset(&s_cached_features, 0, sizeof(s_cached_features));
        
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        s_detect_x86_features(&s_cached_features);
#elif defined(__aarch64__) || defined(__arm__)
        s_detect_arm_features(&s_cached_features);
#else
        s_detect_generic_features(&s_cached_features);
#endif
        
        s_features_detected = true;
        
        log_it(L_INFO, "CPU detected: %s", s_cpu_name);
        log_it(L_DEBUG, "  64-bit: %s", s_cached_features.is_64bit ? "yes" : "no");
        
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        log_it(L_DEBUG, "  SSE2: %s, SSE4.1: %s, SSE4.2: %s",
               s_cached_features.has_sse2 ? "yes" : "no",
               s_cached_features.has_sse4_1 ? "yes" : "no",
               s_cached_features.has_sse4_2 ? "yes" : "no");
        log_it(L_DEBUG, "  AVX: %s, AVX2: %s",
               s_cached_features.has_avx ? "yes" : "no",
               s_cached_features.has_avx2 ? "yes" : "no");
        log_it(L_DEBUG, "  AVX-512F: %s, AVX-512DQ: %s, AVX-512BW: %s, AVX-512VL: %s",
               s_cached_features.has_avx512f ? "yes" : "no",
               s_cached_features.has_avx512dq ? "yes" : "no",
               s_cached_features.has_avx512bw ? "yes" : "no",
               s_cached_features.has_avx512vl ? "yes" : "no");
        log_it(L_DEBUG, "  BMI: %s, BMI2: %s, POPCNT: %s",
               s_cached_features.has_bmi ? "yes" : "no",
               s_cached_features.has_bmi2 ? "yes" : "no",
               s_cached_features.has_popcnt ? "yes" : "no");
#elif defined(__aarch64__) || defined(__arm__)
        log_it(L_DEBUG, "  NEON: %s, SVE: %s, SVE2: %s",
               s_cached_features.has_neon ? "yes" : "no",
               s_cached_features.has_sve ? "yes" : "no",
               s_cached_features.has_sve2 ? "yes" : "no");
#endif
    }
    
    return s_cached_features;
}

const char* dap_cpu_get_name(void)
{
    if (!s_features_detected) {
        dap_cpu_detect_features();
    }
    return s_cpu_name;
}

void dap_cpu_print_features(void)
{
    dap_cpu_features_t f = dap_cpu_detect_features();
    
    log_it(L_INFO, "=== CPU Features ===");
    log_it(L_INFO, "CPU: %s", s_cpu_name);
    log_it(L_INFO, "Architecture: %s %s",
           f.is_x86 ? "x86" : (f.is_arm ? "ARM" : "Unknown"),
           f.is_64bit ? "64-bit" : "32-bit");
    log_it(L_INFO, "Cache line size: %u bytes", f.cache_line_size);
    
    if (f.is_x86) {
        log_it(L_INFO, "SIMD: SSE2=%d SSE4.1=%d SSE4.2=%d AVX=%d AVX2=%d",
               f.has_sse2, f.has_sse4_1, f.has_sse4_2, f.has_avx, f.has_avx2);
        log_it(L_INFO, "AVX-512: F=%d DQ=%d BW=%d VL=%d",
               f.has_avx512f, f.has_avx512dq, f.has_avx512bw, f.has_avx512vl);
        log_it(L_INFO, "Other: BMI=%d BMI2=%d POPCNT=%d",
               f.has_bmi, f.has_bmi2, f.has_popcnt);
    } else if (f.is_arm) {
        log_it(L_INFO, "SIMD: NEON=%d SVE=%d SVE2=%d",
               f.has_neon, f.has_sve, f.has_sve2);
    }
}

