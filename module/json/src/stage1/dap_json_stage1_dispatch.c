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
 * @brief CPU dispatch mechanism for Stage 1 tokenization
 * @details Selects optimal SIMD implementation at runtime based on CPU capabilities
 * 
 * Dispatch order (highest priority first):
 * 1. AVX-512 (x86_64 with AVX-512F, AVX-512BW) - 64 bytes/iteration
 * 2. AVX2 (x86_64 with AVX2) - 32 bytes/iteration
 * 3. SSE2 (x86/x86_64 with SSE2) - 16 bytes/iteration
 * 4. NEON (ARM with NEON) - 16 bytes/iteration
 * 5. Reference (portable C fallback)
 */

#include <stdatomic.h>
#include <stdbool.h>

#include "dap_common.h"
#include "dap_cpu_detect.h"
#include "internal/dap_json_stage1.h"

#define LOG_TAG "dap_json_stage1_dispatch"

/* ========================================================================== */
/*                    FUNCTION POINTER TYPE                                   */
/* ========================================================================== */

/**
 * @brief Type for Stage 1 tokenization function
 * @param[in,out] a_stage1 Stage 1 parser state
 * @return STAGE1_SUCCESS or error code
 */
typedef int (*dap_json_stage1_func_t)(dap_json_stage1_t *a_stage1);

/* ========================================================================== */
/*                    FORWARD DECLARATIONS                                    */
/* ========================================================================== */

// Reference implementation (always available)
extern int dap_json_stage1_run(dap_json_stage1_t *a_stage1);

// x86/x86_64 SIMD implementations
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #if defined(__AVX512F__) && defined(__AVX512BW__)
        extern int dap_json_stage1_run_avx512(dap_json_stage1_t *a_stage1);
    #endif
    
    #if defined(__AVX2__)
        extern int dap_json_stage1_run_avx2(dap_json_stage1_t *a_stage1);
    #endif
    
    #if defined(__SSE2__)
        extern int dap_json_stage1_run_sse2(dap_json_stage1_t *a_stage1);
    #endif
#endif

// ARM SIMD implementations
#if defined(__ARM_NEON) || defined(__aarch64__)
    extern int dap_json_stage1_run_neon(dap_json_stage1_t *a_stage1);
#endif

/* ========================================================================== */
/*                    DISPATCH MECHANISM                                      */
/* ========================================================================== */

// Global dispatch function pointer (atomic for thread-safety)
static _Atomic(dap_json_stage1_func_t) g_stage1_dispatch_func = NULL;

/**
 * @brief Select optimal Stage 1 implementation based on CPU features
 * @return Function pointer to optimal implementation
 */
static dap_json_stage1_func_t s_select_optimal_implementation(void)
{
    log_it(L_INFO, "Selecting optimal Stage 1 tokenization implementation...");
    
    // Detect CPU features
    dap_cpu_features_t l_features = dap_cpu_detect_features();
    
    log_it(L_DEBUG, "CPU features detected: AVX512=%d, AVX2=%d, SSE2=%d, NEON=%d",
           l_features.has_avx512, l_features.has_avx2, l_features.has_sse2, l_features.has_neon);
    
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    // x86/x86_64 dispatch
    
    #if defined(__AVX512F__) && defined(__AVX512BW__)
        if (l_features.has_avx512) {
            log_it(L_INFO, "Selected: AVX-512 implementation (64 bytes/iteration)");
            return dap_json_stage1_run_avx512;
        }
    #endif
    
    #if defined(__AVX2__)
        if (l_features.has_avx2) {
            log_it(L_INFO, "Selected: AVX2 implementation (32 bytes/iteration)");
            return dap_json_stage1_run_avx2;
        }
    #endif
    
    #if defined(__SSE2__)
        if (l_features.has_sse2) {
            log_it(L_INFO, "Selected: SSE2 implementation (16 bytes/iteration)");
            return dap_json_stage1_run_sse2;
        }
    #endif
    
#elif defined(__ARM_NEON) || defined(__aarch64__)
    // ARM dispatch
    
    if (l_features.has_neon) {
        log_it(L_INFO, "Selected: NEON implementation (16 bytes/iteration)");
        return dap_json_stage1_run_neon;
    }
    
#endif
    
    // Fallback to reference implementation
    log_it(L_INFO, "Selected: Reference C implementation (portable fallback)");
    return dap_json_stage1_run;
}

/**
 * @brief Initialize dispatch mechanism (called once on first use)
 * @return Function pointer to optimal implementation
 */
static dap_json_stage1_func_t s_init_dispatch(void)
{
    // Select optimal implementation
    dap_json_stage1_func_t l_func = s_select_optimal_implementation();
    
    // Store atomically
    atomic_store(&g_stage1_dispatch_func, l_func);
    
    return l_func;
}

/**
 * @brief Get Stage 1 dispatch function (thread-safe, lazy initialization)
 * @return Function pointer to optimal Stage 1 implementation
 */
static inline dap_json_stage1_func_t s_get_dispatch_func(void)
{
    dap_json_stage1_func_t l_func = atomic_load(&g_stage1_dispatch_func);
    
    if (!l_func) {
        // First call - initialize
        l_func = s_init_dispatch();
    }
    
    return l_func;
}

/* ========================================================================== */
/*                    PUBLIC API (dispatched entry point)                     */
/* ========================================================================== */

/**
 * @brief Run Stage 1 tokenization (dispatched to optimal implementation)
 * @param[in,out] a_stage1 Stage 1 parser state
 * @return STAGE1_SUCCESS or error code
 */
int dap_json_stage1_run_dispatched(dap_json_stage1_t *a_stage1)
{
    if (!a_stage1) {
        log_it(L_ERROR, "NULL stage1 pointer");
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    // Get dispatch function (lazy init on first call)
    dap_json_stage1_func_t l_func = s_get_dispatch_func();
    
    // Call selected implementation
    return l_func(a_stage1);
}

/**
 * @brief Force re-detection of CPU features and re-dispatch
 * @details Useful for testing or if CPU features change at runtime
 */
void dap_json_stage1_reset_dispatch(void)
{
    log_it(L_INFO, "Resetting Stage 1 dispatch mechanism");
    atomic_store(&g_stage1_dispatch_func, NULL);
}

/**
 * @brief Get current dispatch implementation name (for debugging)
 * @return String description of current implementation
 */
const char* dap_json_stage1_get_dispatch_name(void)
{
    dap_json_stage1_func_t l_func = atomic_load(&g_stage1_dispatch_func);
    
    if (!l_func) {
        return "Not initialized (will auto-detect on first use)";
    }
    
#if defined(__AVX512F__) && defined(__AVX512BW__)
    if (l_func == dap_json_stage1_run_avx512) {
        return "AVX-512 (64 bytes/iteration)";
    }
#endif

#if defined(__AVX2__)
    if (l_func == dap_json_stage1_run_avx2) {
        return "AVX2 (32 bytes/iteration)";
    }
#endif

#if defined(__SSE2__)
    if (l_func == dap_json_stage1_run_sse2) {
        return "SSE2 (16 bytes/iteration)";
    }
#endif

#if defined(__ARM_NEON) || defined(__aarch64__)
    if (l_func == dap_json_stage1_run_neon) {
        return "NEON (16 bytes/iteration)";
    }
#endif
    
    if (l_func == dap_json_stage1_run) {
        return "Reference C (portable)";
    }
    
    return "Unknown";
}

