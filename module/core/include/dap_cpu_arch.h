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
 * @file dap_cpu_arch.h
 * @brief CPU Architecture Definitions for DAP SDK
 * @details Universal CPU architecture enumeration for SIMD dispatch, 
 *          optimization selection, and runtime feature detection.
 *          Used across all SDK modules (JSON, Crypto, Network, etc.)
 * @date 2025-01-08
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ========================================================================== */
/*                     PLATFORM DETECTION MACROS                              */
/* ========================================================================== */

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#  define DAP_PLATFORM_X86 1
#else
#  define DAP_PLATFORM_X86 0
#endif

#if defined(__x86_64__) || defined(_M_X64)
#  define DAP_PLATFORM_X86_64 1
#else
#  define DAP_PLATFORM_X86_64 0
#endif

#if defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64)
#  define DAP_PLATFORM_ARM 1
#else
#  define DAP_PLATFORM_ARM 0
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#  define DAP_PLATFORM_ARM64 1
#else
#  define DAP_PLATFORM_ARM64 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                     ALGORITHM CLASS (REGISTRATION-BASED)                   */
/* ========================================================================== */

/**
 * @brief Opaque algorithm class handle for per-workload SIMD tuning.
 *
 * Modules (math, crypto, etc.) register their own classes via
 * dap_algo_class_register() and then attach CPU-specific tuning
 * rules via dap_cpu_tune_add().  Core knows nothing about the
 * semantics — it only stores the handles and matches rules.
 */
typedef uint32_t dap_algo_class_t;

#define DAP_ALGO_CLASS_DEFAULT  ((dap_algo_class_t)0)

/* ========================================================================== */
/*                     CPU ARCHITECTURE ENUMERATION                           */
/* ========================================================================== */

/**
 * @brief CPU SIMD Architecture Types
 * @details Unified architecture enumeration for runtime dispatch and selection
 * 
 * Usage:
 *   - Auto-detection: Use DAP_CPU_ARCH_AUTO for automatic best selection
 *   - Manual override: Set specific architecture for testing/benchmarking
 *   - Fallback: DAP_CPU_ARCH_REFERENCE is pure C, always available
 * 
 * Architecture availability is platform-dependent:
 *   - x86/x64: REFERENCE, SSE2, AVX2, AVX512
 *   - ARM32/64: REFERENCE, NEON, SVE (future)
 *   - Others: REFERENCE only
 */
typedef enum {
    /* ====== Universal (all platforms) ====== */
    DAP_CPU_ARCH_AUTO       = 0,  ///< Auto-detect best available (default)
    DAP_CPU_ARCH_REFERENCE  = 1,  ///< Pure C reference (no SIMD, portable)
    
    /* ====== x86/x64 Architectures ====== */
    DAP_CPU_ARCH_SSE2       = 2,  ///< SSE2 (16 bytes/iteration, x86/x64)
    DAP_CPU_ARCH_AVX2       = 3,  ///< AVX2 (32 bytes/iteration, x86/x64)
    DAP_CPU_ARCH_AVX512     = 4,  ///< AVX-512 (64 bytes/iteration, x86/x64)
    
    /* ====== ARM Architectures ====== */
    DAP_CPU_ARCH_NEON       = 5,  ///< ARM NEON (16 bytes/iteration, ARM32/64)
    DAP_CPU_ARCH_SVE        = 6,  ///< ARM SVE (128-2048 bits variable, ARM64 v8.2+)
    DAP_CPU_ARCH_SVE2       = 7,  ///< ARM SVE2 (enhanced SVE, ARM64 v9.0+)
    
    /* ====== Future Extensions ====== */
    DAP_CPU_ARCH_RISC_V     = 8,  ///< RISC-V Vector Extension (future)
    
    DAP_CPU_ARCH_MAX              ///< Sentinel value, not a valid architecture
} dap_cpu_arch_t;

/**
 * @brief Get human-readable name for CPU architecture
 * @param a_arch Architecture enum value
 * @return String name (e.g. "SSE2", "NEON", "Reference C")
 * 
 * @code
 *   dap_cpu_arch_t arch = DAP_CPU_ARCH_AVX2;
 *   printf("Using: %s\n", dap_cpu_arch_get_name(arch));
 *   // Output: "Using: AVX2"
 * @endcode
 */
const char* dap_cpu_arch_get_name(dap_cpu_arch_t a_arch);

/**
 * @brief Check if architecture is available on current CPU
 * @param a_arch Architecture to check
 * @return true if architecture is supported by current CPU, false otherwise
 * 
 * @note Requires prior call to dap_cpu_detect() for accurate results
 * 
 * @code
 *   if (dap_cpu_arch_is_available(DAP_CPU_ARCH_AVX512)) {
 *       // Use AVX-512 optimized path
 *   }
 * @endcode
 */
bool dap_cpu_arch_is_available(dap_cpu_arch_t a_arch);

/**
 * @brief Get best available architecture for current CPU
 * @return Highest-performance architecture supported by current CPU
 * 
 * @note Requires prior call to dap_cpu_detect() for accurate results
 * @note Returns DAP_CPU_ARCH_REFERENCE if no SIMD detected
 * 
 * Priority order (highest to lowest):
 *   x86/x64: AVX-512 > AVX2 > SSE2 > Reference
 *   ARM:     SVE2 > SVE > NEON > Reference
 */
dap_cpu_arch_t dap_cpu_arch_get_best(void);

/**
 * @brief Get best architecture for a specific algorithm class
 * @param a_class Algorithm class handle (from dap_algo_class_register)
 * @return Optimal architecture considering CPU-specific tuning rules
 *
 * @details Consults registered tuning rules that map
 *          (vendor, family, model, algo_class) -> preferred arch.
 *
 * @note Manual override via dap_cpu_arch_set() takes highest priority.
 * @note DAP_ALGO_CLASS_DEFAULT returns the same as dap_cpu_arch_get_best().
 */
dap_cpu_arch_t dap_cpu_arch_get_best_for(dap_algo_class_t a_class);

/* ========================================================================== */
/*                  ALGORITHM CLASS REGISTRATION                              */
/* ========================================================================== */

/**
 * @brief Register a new algorithm class.
 * @param a_name Human-readable name (e.g. "NTT", "HASH", "ECC")
 * @return Unique non-zero handle, or 0 on capacity overflow.
 *
 * Call once per class during module init.  The returned handle is
 * then passed to dap_cpu_tune_add() and DAP_DISPATCH_ARCH_SELECT_FOR().
 */
dap_algo_class_t dap_algo_class_register(const char *a_name);

/**
 * @brief Get human-readable name of a registered algorithm class.
 */
const char *dap_algo_class_get_name(dap_algo_class_t a_class);

/* ========================================================================== */
/*                  CPU TUNING RULE REGISTRATION                              */
/* ========================================================================== */

/* Forward-declare vendor enum used in rules (defined in dap_cpu_detect.h) */
#include "dap_cpu_detect.h"

/**
 * @brief CPU-specific tuning rule.
 *
 * Modules populate these and pass to dap_cpu_tune_add().
 * When dap_cpu_arch_get_best_for(class) is called, rules are
 * matched top-to-bottom; first match wins.
 *
 * vendor == DAP_CPU_VENDOR_UNKNOWN means "match any vendor".
 * algo_class == DAP_ALGO_CLASS_DEFAULT means "match any class".
 */
typedef struct {
    dap_cpu_vendor_t vendor;
    uint32_t family_min, family_max;
    uint32_t model_min,  model_max;
    dap_algo_class_t algo_class;
    dap_cpu_arch_t   preferred_arch;
} dap_cpu_tune_rule_t;

/**
 * @brief Register a single tuning rule.
 * @return 0 on success, -1 if the rule table is full.
 */
int dap_cpu_tune_add(const dap_cpu_tune_rule_t *a_rule);

/**
 * @brief Register multiple tuning rules at once.
 * @return 0 on success, -1 if the rule table overflows.
 */
int dap_cpu_tune_add_rules(const dap_cpu_tune_rule_t *a_rules, size_t a_count);

/* ========================================================================== */
/*                   GLOBAL ARCHITECTURE STATE MANAGEMENT                     */
/* ========================================================================== */

/**
 * @brief Set manual architecture override for DAP SDK
 * @details Overrides automatic CPU detection. Returns -1 if requested
 *          architecture is not available on current CPU.
 * 
 * @param a_arch Desired architecture (DAP_CPU_ARCH_* constant)
 * @return 0 on success, -1 if not available
 * 
 * @note Setting DAP_CPU_ARCH_AUTO resets to automatic detection
 * @note This affects ALL DAP SDK modules (JSON, Crypto, Network, etc.)
 * @note Thread-safe: uses atomic operations
 * 
 * @code
 *   // Force SSE2 for testing
 *   if (dap_cpu_arch_set(DAP_CPU_ARCH_SSE2) != 0) {
 *       printf("SSE2 not available\n");
 *   }
 *   
 *   // Reset to auto-detection
 *   dap_cpu_arch_set(DAP_CPU_ARCH_AUTO);
 * @endcode
 */
int dap_cpu_arch_set(dap_cpu_arch_t a_arch);

/**
 * @brief Get currently selected architecture
 * @return Current architecture (respects manual override if set)
 * 
 * @note If manual override is set (via dap_cpu_arch_set), returns that
 * @note Otherwise returns best available architecture for current CPU
 * @note Thread-safe: uses atomic operations
 * 
 * @code
 *   dap_cpu_arch_t arch = dap_cpu_arch_get();
 *   printf("Using: %s\n", dap_cpu_arch_get_name(arch));
 * @endcode
 */
dap_cpu_arch_t dap_cpu_arch_get(void);

#ifdef __cplusplus
}
#endif

