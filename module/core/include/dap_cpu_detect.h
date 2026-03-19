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
    DAP_CPU_VENDOR_INTEL,
    DAP_CPU_VENDOR_AMD,
} dap_cpu_vendor_t;

/**
 * @brief CPU feature flags and identity
 */
typedef struct dap_cpu_features {
    // x86/x64 features
    bool has_sse2;      /**< SSE2 (baseline for x86-64) */
    bool has_sse4_1;    /**< SSE4.1 */
    bool has_sse4_2;    /**< SSE4.2 */
    bool has_avx;       /**< AVX */
    bool has_avx2;      /**< AVX2 */
    bool has_avx512f;   /**< AVX-512 Foundation */
    bool has_avx512dq;  /**< AVX-512 DQ */
    bool has_avx512bw;  /**< AVX-512 Byte/Word */
    bool has_avx512vl;  /**< AVX-512 Vector Length */
    bool has_bmi;       /**< BMI (Bit Manipulation Instructions) */
    bool has_bmi2;      /**< BMI2 */
    bool has_popcnt;    /**< POPCNT */
    bool has_aes_ni;    /**< AES-NI (hardware AES acceleration) */
    bool has_sha_ni;    /**< SHA-NI (hardware SHA acceleration) */
    bool has_pclmulqdq; /**< PCLMULQDQ (carry-less multiply for GCM/GHASH) */
    bool has_avx512_ifma; /**< AVX-512 IFMA (52-bit integer FMA for bignum) */
    
    // ARM features
    bool has_neon;      /**< ARM NEON (baseline for ARM64) */
    bool has_sve;       /**< ARM SVE (Scalable Vector Extension) */
    bool has_sve2;      /**< ARM SVE2 */
    bool has_arm_ce;    /**< ARM Cryptographic Extensions (AES/SHA) */
    
    // Architecture info
    bool is_x86;        /**< x86/x64 architecture */
    bool is_arm;        /**< ARM architecture */
    bool is_64bit;      /**< 64-bit architecture */

    // CPU identity (x86: from CPUID; ARM: from MIDR_EL1 / /proc/cpuinfo)
    dap_cpu_vendor_t vendor;    /**< CPU vendor (Intel, AMD, ...) */
    uint32_t x86_family;        /**< x86 extended family */
    uint32_t x86_model;         /**< x86 extended model */
    uint32_t x86_stepping;      /**< x86 stepping */
    
    // Cache info
    uint32_t cache_line_size;  /**< L1 cache line size in bytes */
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

