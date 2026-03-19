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
 * @file dap_cpu_detect.h
 * @brief Runtime CPU feature detection for SIMD optimizations
 * @details Provides unified API for detecting CPU features across x86/x64 and ARM architectures
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CPU vendor identification
 */
typedef enum {
    DAP_CPU_VENDOR_UNKNOWN = 0,
    DAP_CPU_VENDOR_INTEL,       /**< GenuineIntel */
    DAP_CPU_VENDOR_AMD,         /**< AuthenticAMD */
    DAP_CPU_VENDOR_ARM,         /**< ARM Ltd (from MIDR implementer 0x41) */
    DAP_CPU_VENDOR_APPLE,       /**< Apple Silicon (MIDR implementer 0x61) */
    DAP_CPU_VENDOR_QUALCOMM,    /**< Qualcomm Kryo/Oryon (MIDR implementer 0x51) */
    DAP_CPU_VENDOR_SAMSUNG,     /**< Samsung Exynos (MIDR implementer 0x53) */
    DAP_CPU_VENDOR_AMPERE,      /**< Ampere Computing (MIDR implementer 0xC0) */
    DAP_CPU_VENDOR_HUAWEI,      /**< HiSilicon / Kunpeng (MIDR implementer 0x48) */
    DAP_CPU_VENDOR_BAIKAL,      /**< Baikal Electronics */
    DAP_CPU_VENDOR_VIA,         /**< VIA / Zhaoxin (CentaurHauls / Shanghai) */
    DAP_CPU_VENDOR_HYGON,       /**< Hygon (HygonGenuine, Zen-based Chinese x86) */
} dap_cpu_vendor_t;

/* Platform detection (mirrors dap_arch_dispatch.h, usable in this header) */
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#  define DAP_CPU_DETECT_X86 1
#else
#  define DAP_CPU_DETECT_X86 0
#endif

#if defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64)
#  define DAP_CPU_DETECT_ARM 1
#else
#  define DAP_CPU_DETECT_ARM 0
#endif

/**
 * @brief CPU feature flags and identity
 *
 * Platform-guarded: only fields relevant to the compile target are present.
 * Common fields (vendor, is_64bit, cache_line_size) are always available.
 */
typedef struct dap_cpu_features {

    /* ---- Common ---- */
    dap_cpu_vendor_t vendor;       /**< CPU vendor */
    bool is_64bit;                 /**< 64-bit architecture */
    uint32_t cache_line_size;      /**< L1 cache line size in bytes */

#if DAP_CPU_DETECT_X86
    /* ---- x86/x64 ISA extensions ---- */
    bool has_sse2;                 /**< SSE2 (baseline for x86-64) */
    bool has_sse4_1;               /**< SSE4.1 */
    bool has_sse4_2;               /**< SSE4.2 */
    bool has_avx;                  /**< AVX */
    bool has_avx2;                 /**< AVX2 */
    bool has_avx512f;              /**< AVX-512 Foundation */
    bool has_avx512dq;             /**< AVX-512 DQ */
    bool has_avx512bw;             /**< AVX-512 Byte/Word */
    bool has_avx512vl;             /**< AVX-512 Vector Length */
    bool has_avx512_ifma;          /**< AVX-512 IFMA (52-bit int FMA) */
    bool has_bmi;                  /**< BMI */
    bool has_bmi2;                 /**< BMI2 */
    bool has_popcnt;               /**< POPCNT */
    bool has_aes_ni;               /**< AES-NI */
    bool has_sha_ni;               /**< SHA-NI */
    bool has_pclmulqdq;            /**< PCLMULQDQ (carry-less multiply) */

    /* ---- x86 CPU identity (CPUID leaf 0 + 1) ---- */
    uint32_t x86_family;           /**< Extended family */
    uint32_t x86_model;            /**< Extended model */
    uint32_t x86_stepping;         /**< Stepping */
#endif /* DAP_CPU_DETECT_X86 */

#if DAP_CPU_DETECT_ARM
    /* ---- ARM ISA extensions ---- */
    bool has_neon;                 /**< NEON (baseline for AArch64) */
    bool has_sve;                  /**< SVE (Scalable Vector Extension) */
    bool has_sve2;                 /**< SVE2 */
    bool has_arm_ce;               /**< Cryptographic Extensions (AES/SHA) */
#endif /* DAP_CPU_DETECT_ARM */

} dap_cpu_features_t;

/**
 * @brief Detect CPU features at runtime
 * @return Structure with detected CPU features
 * 
 * This function performs runtime detection of available CPU instruction sets.
 * Results are cached after first call for performance.
 */
dap_cpu_features_t dap_cpu_detect_features(void);

/**
 * @brief Get human-readable CPU name/description
 * @return String with CPU model name (or "Unknown CPU")
 */
const char* dap_cpu_get_name(void);

/**
 * @brief Print detected CPU features to log
 */
void dap_cpu_print_features(void);

#ifdef __cplusplus
}
#endif

