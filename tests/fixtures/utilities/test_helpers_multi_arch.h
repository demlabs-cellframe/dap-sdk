/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 */

/**
 * @file test_helpers_multi_arch.h
 * @brief Multi-Architecture Test Helpers
 * @details Helper functions for running JSON tests across ALL SIMD implementations
 * 
 * CRITICAL: JSON parser bugs can be architecture-specific (SSE2 vs AVX2 vs AVX-512)
 *           All correctness tests MUST run for each SIMD variant!
 * 
 * Usage:
 *   MULTI_ARCH_TEST_BEGIN("My Test");
 *   
 *   // Test code here - will run for each architecture
 *   dap_json_t *json = dap_json_parse_string(test_json);
 *   // ... assertions ...
 *   dap_json_object_free(json);
 *   
 *   MULTI_ARCH_TEST_END();
 * 
 * @date 2026-01-12
 */

#ifndef DAP_JSON_TEST_HELPERS_MULTI_ARCH_H
#define DAP_JSON_TEST_HELPERS_MULTI_ARCH_H

#include "dap_common.h"
#include "dap_json.h"
#include "dap_cpu_arch.h"

/**
 * @brief Run test function for all available architectures
 * @param test_name Test name for logging
 * @param test_func Test function to run (returns bool: true = pass)
 * @return Number of architectures that passed
 */
static inline int run_multi_arch_test(
    const char *test_name,
    bool (*test_func)(void)
)
{
    log_it(L_INFO, "=== Multi-Arch Test: %s ===", test_name);
    
    int passed = 0;
    int total = 0;
    
    // Get available architectures
    dap_cpu_features_t features = dap_cpu_arch_get_features();
    
    // Test 1: Reference C implementation (always available)
    {
        total++;
        dap_cpu_arch_set(DAP_CPU_ARCH_REFERENCE);
        log_it(L_DEBUG, "  [%d/%d] Testing: Reference C", total, total);
        
        if (test_func()) {
            passed++;
            log_it(L_INFO, "    ✅ Reference C: PASS");
        } else {
            log_it(L_ERROR, "    ❌ Reference C: FAIL");
        }
    }
    
    // Test 2: SSE2 (if available)
    if (features.has_sse2) {
        total++;
        dap_cpu_arch_set(DAP_CPU_ARCH_SSE2);
        log_it(L_DEBUG, "  [%d/%d] Testing: SSE2", total, total);
        
        if (test_func()) {
            passed++;
            log_it(L_INFO, "    ✅ SSE2: PASS");
        } else {
            log_it(L_ERROR, "    ❌ SSE2: FAIL");
        }
    }
    
    // Test 3: AVX2 (if available)
    if (features.has_avx2) {
        total++;
        dap_cpu_arch_set(DAP_CPU_ARCH_AVX2);
        log_it(L_DEBUG, "  [%d/%d] Testing: AVX2", total, total);
        
        if (test_func()) {
            passed++;
            log_it(L_INFO, "    ✅ AVX2: PASS");
        } else {
            log_it(L_ERROR, "    ❌ AVX2: FAIL");
        }
    }
    
    // Test 4: AVX-512 (if available)
    if (features.has_avx512) {
        total++;
        dap_cpu_arch_set(DAP_CPU_ARCH_AVX512);
        log_it(L_DEBUG, "  [%d/%d] Testing: AVX-512", total, total);
        
        if (test_func()) {
            passed++;
            log_it(L_INFO, "    ✅ AVX-512: PASS");
        } else {
            log_it(L_ERROR, "    ❌ AVX-512: FAIL");
        }
    }
    
    // Test 5: ARM NEON (if available)
    if (features.has_neon) {
        total++;
        dap_cpu_arch_set(DAP_CPU_ARCH_NEON);
        log_it(L_DEBUG, "  [%d/%d] Testing: ARM NEON", total, total);
        
        if (test_func()) {
            passed++;
            log_it(L_INFO, "    ✅ ARM NEON: PASS");
        } else {
            log_it(L_ERROR, "    ❌ ARM NEON: FAIL");
        }
    }
    
    // Test 6: ARM SVE (if available)
    if (features.has_sve) {
        total++;
        dap_cpu_arch_set(DAP_CPU_ARCH_SVE);
        log_it(L_DEBUG, "  [%d/%d] Testing: ARM SVE", total, total);
        
        if (test_func()) {
            passed++;
            log_it(L_INFO, "    ✅ ARM SVE: PASS");
        } else {
            log_it(L_ERROR, "    ❌ ARM SVE: FAIL");
        }
    }
    
    // Test 7: ARM SVE2 (if available)
    if (features.has_sve2) {
        total++;
        dap_cpu_arch_set(DAP_CPU_ARCH_SVE2);
        log_it(L_DEBUG, "  [%d/%d] Testing: ARM SVE2", total, total);
        
        if (test_func()) {
            passed++;
            log_it(L_INFO, "    ✅ ARM SVE2: PASS");
        } else {
            log_it(L_ERROR, "    ❌ ARM SVE2: FAIL");
        }
    }
    
    // Reset to auto-detect
    dap_cpu_arch_set(DAP_CPU_ARCH_AUTO);
    
    log_it(L_INFO, "=== Multi-Arch Summary: %d/%d architectures passed ===", passed, total);
    
    return (passed == total) ? 0 : -1;
}

/**
 * @brief Macro for multi-arch test block
 */
#define MULTI_ARCH_TEST(test_name, test_body) \
    static bool _test_func_##test_name(void) { \
        test_body \
    } \
    run_multi_arch_test(#test_name, _test_func_##test_name)

#endif // DAP_JSON_TEST_HELPERS_MULTI_ARCH_H
