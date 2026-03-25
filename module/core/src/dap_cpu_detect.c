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
#include <stdio.h>
#include <pthread.h>

#if DAP_CPU_DETECT_X86
    #include <cpuid.h>
#elif DAP_CPU_DETECT_ARM
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
static pthread_once_t s_detect_once = PTHREAD_ONCE_INIT;

#if DAP_CPU_DETECT_X86

/**
 * @brief Detect x86/x64 CPU features using CPUID
 */
static void s_detect_x86_features(dap_cpu_features_t *a_features)
{
    unsigned int eax, ebx, ecx, edx;
    
    a_features->is_64bit = sizeof(void*) == 8;
    a_features->cache_line_size = 64;
    
    // CPUID leaf 0: max leaf + vendor string
    if (!__get_cpuid(0, &eax, &ebx, &ecx, &edx)) {
        return;
    }

    // Vendor: EBX-EDX-ECX form a 12-byte ASCII string
    // "GenuineIntel", "AuthenticAMD", "HygonGenuine",
    // "CentaurHauls" / "  Shanghai  " (VIA/Zhaoxin)
    if (ebx == 0x756E6547 && edx == 0x49656E69 && ecx == 0x6C65746E)
        a_features->vendor = DAP_CPU_VENDOR_INTEL;
    else if (ebx == 0x68747541 && edx == 0x69746E65 && ecx == 0x444D4163)
        a_features->vendor = DAP_CPU_VENDOR_AMD;
    else if (ebx == 0x6F677948 && edx == 0x6E65476E && ecx == 0x656E6975)
        a_features->vendor = DAP_CPU_VENDOR_HYGON;
    else if (ebx == 0x746E6543 && edx == 0x48727561 && ecx == 0x736C7561)
        a_features->vendor = DAP_CPU_VENDOR_VIA;
    else if (ebx == 0x68532020 && edx == 0x68676E61 && ecx == 0x20206961)
        a_features->vendor = DAP_CPU_VENDOR_VIA;

    // Brand string (leaves 0x80000002..04)
    unsigned int brand[12] = {0};
    if (__get_cpuid(0x80000000, &eax, &ebx, &ecx, &edx) && eax >= 0x80000004) {
        __get_cpuid(0x80000002, &brand[0], &brand[1], &brand[2], &brand[3]);
        __get_cpuid(0x80000003, &brand[4], &brand[5], &brand[6], &brand[7]);
        __get_cpuid(0x80000004, &brand[8], &brand[9], &brand[10], &brand[11]);
        snprintf(s_cpu_name, sizeof(s_cpu_name), "%s", (char*)brand);
        char *p = s_cpu_name;
        while (*p == ' ') p++;
        if (p != s_cpu_name) {
            memmove(s_cpu_name, p, strlen(p) + 1);
        }
    }
    
    // CPUID leaf 1: family/model/stepping + feature flags
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        uint32_t l_stepping   = eax & 0xF;
        uint32_t l_model      = (eax >> 4) & 0xF;
        uint32_t l_family     = (eax >> 8) & 0xF;
        uint32_t l_ext_model  = (eax >> 16) & 0xF;
        uint32_t l_ext_family = (eax >> 20) & 0xFF;
        if (l_family == 0x6 || l_family == 0xF)
            l_model += l_ext_model << 4;
        if (l_family == 0xF)
            l_family += l_ext_family;
        a_features->x86_family   = l_family;
        a_features->x86_model    = l_model;
        a_features->x86_stepping = l_stepping;

        a_features->has_sse2 = (edx & (1 << 26)) != 0;
        a_features->has_popcnt = (ecx & (1 << 23)) != 0;
        a_features->has_sse4_1 = (ecx & (1 << 19)) != 0;
        a_features->has_sse4_2 = (ecx & (1 << 20)) != 0;
        a_features->has_avx = (ecx & (1 << 28)) != 0;
        a_features->has_aes_ni = (ecx & (1 << 25)) != 0;
        a_features->has_pclmulqdq = (ecx & (1 << 1)) != 0;
    }
    
    // CPUID.7:EBX - Extended features
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        a_features->has_avx2 = (ebx & (1 << 5)) != 0;
        a_features->has_bmi = (ebx & (1 << 3)) != 0;
        a_features->has_bmi2 = (ebx & (1 << 8)) != 0;
        a_features->has_avx512f = (ebx & (1 << 16)) != 0;
        a_features->has_avx512dq = (ebx & (1 << 17)) != 0;
        a_features->has_avx512bw = (ebx & (1 << 30)) != 0;
        a_features->has_avx512vl = (ebx & (1U << 31)) != 0;
        a_features->has_sha_ni = (ebx & (1 << 29)) != 0;
        a_features->has_avx512_ifma = (ebx & (1 << 21)) != 0;
        a_features->has_avx512_vbmi2 = (ecx & (1 << 6)) != 0;
    }
}

#elif DAP_CPU_DETECT_ARM

/**
 * @brief Detect ARM CPU features
 */
static void s_detect_arm_vendor(dap_cpu_features_t *a_features)
{
#if defined(__APPLE__) && defined(__aarch64__)
    a_features->vendor = DAP_CPU_VENDOR_APPLE;
#elif defined(__linux__) && defined(__aarch64__)
    // MIDR_EL1 is readable from /proc/cpuinfo or /sys on Linux
    FILE *f = fopen("/sys/devices/system/cpu/cpu0/regs/identification/midr_el1", "r");
    if (f) {
        unsigned long long midr = 0;
        if (fscanf(f, "%llx", &midr) == 1) {
            uint8_t l_impl = (uint8_t)(midr >> 24);
            switch (l_impl) {
                case 0x41: a_features->vendor = DAP_CPU_VENDOR_ARM;       break;
                case 0x42: /* Broadcom — fall through to unknown */       break;
                case 0x48: a_features->vendor = DAP_CPU_VENDOR_HUAWEI;    break;
                case 0x51: a_features->vendor = DAP_CPU_VENDOR_QUALCOMM;  break;
                case 0x53: a_features->vendor = DAP_CPU_VENDOR_SAMSUNG;   break;
                case 0x61: a_features->vendor = DAP_CPU_VENDOR_APPLE;     break;
                case 0xC0: a_features->vendor = DAP_CPU_VENDOR_AMPERE;    break;
            }
        }
        fclose(f);
    }
#endif
}

static void s_detect_arm_features(dap_cpu_features_t *a_features)
{
    a_features->is_64bit = sizeof(void*) == 8;
    a_features->cache_line_size = 64;

#if defined(__aarch64__)
    a_features->has_neon = true;
    snprintf(s_cpu_name, sizeof(s_cpu_name), "ARM64 (AArch64)");
#elif defined(__ARM_NEON)
    a_features->has_neon = true;
    snprintf(s_cpu_name, sizeof(s_cpu_name), "ARM32 with NEON");
#else
    snprintf(s_cpu_name, sizeof(s_cpu_name), "ARM32");
#endif

#if defined(__APPLE__) && defined(__aarch64__)
    a_features->has_arm_ce = true;
#elif defined(__ARM_FEATURE_CRYPTO)
    a_features->has_arm_ce = true;
#endif

#if defined(__linux__)
    unsigned long hwcap = getauxval(AT_HWCAP);
    unsigned long hwcap2 = getauxval(AT_HWCAP2);

    #ifdef HWCAP_AES
        a_features->has_arm_ce = (hwcap & HWCAP_AES) != 0;
    #endif

    #ifdef HWCAP_SVE
        a_features->has_sve = (hwcap & HWCAP_SVE) != 0;
    #endif

    #ifdef HWCAP2_SVE2
        a_features->has_sve2 = (hwcap2 & HWCAP2_SVE2) != 0;
    #endif
#endif

    s_detect_arm_vendor(a_features);
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
static void s_detect_features_impl(void)
{
    memset(&s_cached_features, 0, sizeof(s_cached_features));

#if DAP_CPU_DETECT_X86
    s_detect_x86_features(&s_cached_features);
#elif DAP_CPU_DETECT_ARM
    s_detect_arm_features(&s_cached_features);
#else
    s_detect_generic_features(&s_cached_features);
#endif

    s_features_detected = true;

    static const char *s_vendor_names[] = {
        "Unknown", "Intel", "AMD", "ARM", "Apple", "Qualcomm",
        "Samsung", "Ampere", "HiSilicon", "Baikal", "VIA/Zhaoxin", "Hygon"
    };
    unsigned l_vi = (unsigned)s_cached_features.vendor;
    const char *l_vendor = l_vi < sizeof(s_vendor_names)/sizeof(s_vendor_names[0])
                           ? s_vendor_names[l_vi] : "Other";
    log_it(L_INFO, "CPU detected: %s", s_cpu_name);
    log_it(L_DEBUG, "  64-bit: %s", s_cached_features.is_64bit ? "yes" : "no");

#if DAP_CPU_DETECT_X86
    log_it(L_DEBUG, "  Vendor: %s, Family: 0x%X, Model: 0x%X, Stepping: %u",
           l_vendor,
           s_cached_features.x86_family,
           s_cached_features.x86_model,
           s_cached_features.x86_stepping);
    log_it(L_DEBUG, "  SSE2: %s, SSE4.1: %s, SSE4.2: %s",
           s_cached_features.has_sse2 ? "yes" : "no",
           s_cached_features.has_sse4_1 ? "yes" : "no",
           s_cached_features.has_sse4_2 ? "yes" : "no");
    log_it(L_DEBUG, "  AVX: %s, AVX2: %s",
           s_cached_features.has_avx ? "yes" : "no",
           s_cached_features.has_avx2 ? "yes" : "no");
    log_it(L_DEBUG, "  AVX-512F: %s, AVX-512DQ: %s, AVX-512BW: %s, AVX-512VL: %s, AVX-512IFMA: %s, AVX-512VBMI2: %s",
           s_cached_features.has_avx512f ? "yes" : "no",
           s_cached_features.has_avx512dq ? "yes" : "no",
           s_cached_features.has_avx512bw ? "yes" : "no",
           s_cached_features.has_avx512vl ? "yes" : "no",
           s_cached_features.has_avx512_ifma ? "yes" : "no",
           s_cached_features.has_avx512_vbmi2 ? "yes" : "no");
    log_it(L_DEBUG, "  BMI: %s, BMI2: %s, POPCNT: %s",
           s_cached_features.has_bmi ? "yes" : "no",
           s_cached_features.has_bmi2 ? "yes" : "no",
           s_cached_features.has_popcnt ? "yes" : "no");
    log_it(L_DEBUG, "  AES-NI: %s, SHA-NI: %s, PCLMULQDQ: %s",
           s_cached_features.has_aes_ni ? "yes" : "no",
           s_cached_features.has_sha_ni ? "yes" : "no",
           s_cached_features.has_pclmulqdq ? "yes" : "no");
#elif DAP_CPU_DETECT_ARM
    log_it(L_DEBUG, "  Vendor: %s", l_vendor);
    log_it(L_DEBUG, "  NEON: %s, SVE: %s, SVE2: %s, ARM-CE: %s",
           s_cached_features.has_neon ? "yes" : "no",
           s_cached_features.has_sve ? "yes" : "no",
           s_cached_features.has_sve2 ? "yes" : "no",
           s_cached_features.has_arm_ce ? "yes" : "no");
#endif
}

dap_cpu_features_t dap_cpu_detect_features(void)
{
    pthread_once(&s_detect_once, s_detect_features_impl);
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

    static const char *s_arch_name =
#if DAP_CPU_DETECT_X86
        "x86";
#elif DAP_CPU_DETECT_ARM
        "ARM";
#else
        "Unknown";
#endif

    log_it(L_INFO, "=== CPU Features ===");
    log_it(L_INFO, "CPU: %s", s_cpu_name);
    log_it(L_INFO, "Architecture: %s %s", s_arch_name,
           f.is_64bit ? "64-bit" : "32-bit");
    log_it(L_INFO, "Cache line size: %u bytes", f.cache_line_size);

#if DAP_CPU_DETECT_X86
    log_it(L_INFO, "SIMD: SSE2=%d SSE4.1=%d SSE4.2=%d AVX=%d AVX2=%d",
           f.has_sse2, f.has_sse4_1, f.has_sse4_2, f.has_avx, f.has_avx2);
    log_it(L_INFO, "AVX-512: F=%d DQ=%d BW=%d VL=%d IFMA=%d",
           f.has_avx512f, f.has_avx512dq, f.has_avx512bw, f.has_avx512vl, f.has_avx512_ifma);
    log_it(L_INFO, "Other: BMI=%d BMI2=%d POPCNT=%d AES-NI=%d SHA-NI=%d PCLMULQDQ=%d",
           f.has_bmi, f.has_bmi2, f.has_popcnt, f.has_aes_ni, f.has_sha_ni, f.has_pclmulqdq);
#elif DAP_CPU_DETECT_ARM
    log_it(L_INFO, "SIMD: NEON=%d SVE=%d SVE2=%d ARM-CE=%d",
           f.has_neon, f.has_sve, f.has_sve2, f.has_arm_ce);
#endif
}

