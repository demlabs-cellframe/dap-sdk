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
 * @file test_stage1_simd_correctness.c
 * @brief SIMD correctness tests - compare SIMD implementations vs Reference
 * @details Ensures that all SIMD implementations produce identical results to reference C code
 */

#include "dap_common.h"
#include "dap_test.h"
#include "dap_json.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

// Note: Architecture-specific SIMD headers are now included via dap_json_stage1.h
// They are generated into the build directory and exposed as PUBLIC includes

#include <string.h>
#include <stdio.h>

#define LOG_TAG "test_simd_correctness"


// Test cases - various JSON inputs
static const char* s_test_inputs[] = {
    // Simple
    "{}",
    "[]",
    "null",
    "true",
    "false",
    "42",
    "-123",
    "3.14",
    "\"hello\"",
    
    // Objects
    "{\"key\":\"value\"}",
    "{\"a\":1,\"b\":2,\"c\":3}",
    "{\"nested\":{\"inner\":true}}",
    
    // Arrays
    "[1,2,3,4,5]",
    "[true,false,null]",
    "[\"a\",\"b\",\"c\"]",
    "[[1,2],[3,4]]",
    
    // Mixed
    "{\"arr\":[1,2,3],\"str\":\"test\",\"num\":42,\"bool\":true,\"null\":null}",
    "[{\"id\":1},{\"id\":2},{\"id\":3}]",
    
    // Strings with escapes
    "\"hello\\nworld\"",
    "\"tab\\there\"",
    "\"quote\\\"inside\"",
    "\"backslash\\\\\"",
    "\"unicode\\u0041\\u0042\"",
    
    // Numbers
    "0",
    "-0",
    "123456789",
    "-987654321",
    "3.14159265359",
    "1.23e10",
    "1.23e-10",
    "-2.5e+5",
    
    // Whitespace variations
    " { } ",
    "\n[\n]\n",
    "\t{\t\"key\"\t:\t\"value\"\t}\t",
    "  {  \"a\"  :  1  ,  \"b\"  :  2  }  ",
    
    // Large structures
    "[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20]",
    "{\"k1\":1,\"k2\":2,\"k3\":3,\"k4\":4,\"k5\":5,\"k6\":6,\"k7\":7,\"k8\":8}",
    
    NULL
};

/**
 * @brief Compare two Stage 1 results
 */
static bool s_compare_stage1_results(
    const dap_json_stage1_t *a_ref,
    const dap_json_stage1_t *a_simd,
    const char *a_simd_name,
    const char *a_input
)
{
    // Compare token count
    if (a_ref->indices_count != a_simd->indices_count) {
        log_it(L_ERROR, "[%s] Token count mismatch: ref=%zu, simd=%zu (input: %s)",
               a_simd_name, a_ref->indices_count, a_simd->indices_count, a_input);
        return false;
    }
    
    // Compare each token
    for (size_t i = 0; i < a_ref->indices_count; i++) {
        const dap_json_struct_index_t *l_ref_tok = &a_ref->indices[i];
        const dap_json_struct_index_t *l_simd_tok = &a_simd->indices[i];
        
        if (l_ref_tok->position != l_simd_tok->position ||
            l_ref_tok->length != l_simd_tok->length ||
            l_ref_tok->type != l_simd_tok->type ||
            l_ref_tok->character != l_simd_tok->character) {
            
            log_it(L_ERROR, "[%s] Token %zu mismatch:", a_simd_name, i);
            log_it(L_ERROR, "  REF:  pos=%u len=%u type=%d char=%u",
                   l_ref_tok->position, l_ref_tok->length, l_ref_tok->type, l_ref_tok->character);
            log_it(L_ERROR, "  SIMD: pos=%u len=%u type=%d char=%u",
                   l_simd_tok->position, l_simd_tok->length, l_simd_tok->type, l_simd_tok->character);
            log_it(L_ERROR, "  Input: %s", a_input);
            return false;
        }
    }
    
    log_it(L_DEBUG, "[%s] PASS: %zu tokens match reference (input: %s)",
           a_simd_name, a_ref->indices_count, a_input);
    return true;
}

/**
 * @brief Test SIMD implementation vs Reference
 */
static bool s_test_simd_impl(
    int (*a_simd_run)(dap_json_stage1_t*),
    const char *a_simd_name
)
{
    log_it(L_INFO, "Testing %s correctness...", a_simd_name);
    
    size_t l_total = 0;
    size_t l_passed = 0;
    size_t l_failed = 0;
    
    for (const char **p = s_test_inputs; *p != NULL; p++) {
        const char *l_input = *p;
        size_t l_len = strlen(l_input);
        l_total++; // Count ALL tests, even those that error
        
        // Run reference
        dap_json_stage1_t *l_ref = dap_json_stage1_create((const uint8_t*)l_input, l_len);
        if (!l_ref) {
            log_it(L_ERROR, "[%s] Test #%zu FAILED: Reference init failed for: %s", a_simd_name, l_total, l_input);
            l_failed++;
            continue;
        }
        
        int l_ref_err = dap_json_stage1_run_ref(l_ref);
        if (l_ref_err != STAGE1_SUCCESS) {
            log_it(L_ERROR, "[%s] Test #%zu FAILED: Reference run failed for: %s", a_simd_name, l_total, l_input);
            dap_json_stage1_free(l_ref);
            l_failed++;
            continue;
        }
        
        // Run SIMD
        dap_json_stage1_t *l_simd = dap_json_stage1_create((const uint8_t*)l_input, l_len);
        if (!l_simd) {
            log_it(L_ERROR, "[%s] Test #%zu FAILED: SIMD init failed for: %s", a_simd_name, l_total, l_input);
            dap_json_stage1_free(l_ref);
            l_failed++;
            continue;
        }
        
        int l_simd_err = a_simd_run(l_simd);
        if (l_simd_err != STAGE1_SUCCESS) {
            log_it(L_ERROR, "[%s] Test #%zu FAILED: SIMD run failed for: %s (error=%d)", a_simd_name, l_total, l_input, l_simd_err);
            dap_json_stage1_free(l_ref);
            dap_json_stage1_free(l_simd);
            l_failed++;
            continue;
        }
        
        // Compare
        if (s_compare_stage1_results(l_ref, l_simd, a_simd_name, l_input)) {
            l_passed++;
        } else {
            l_failed++;
        }
        
        // Cleanup
        dap_json_stage1_free(l_ref);
        dap_json_stage1_free(l_simd);
    }
    
    log_it(L_INFO, "[%s] Results: %zu/%zu tests passed (%.1f%%), %zu failed",
           a_simd_name, l_passed, l_total, (100.0 * l_passed) / l_total, l_failed);
    
    return l_passed == l_total;
}

/**
 * @brief Test AVX2 correctness
 */
static bool s_test_avx2_correctness(void)
{
    log_it(L_DEBUG, "Testing AVX2 correctness");
    
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    // Check if AVX2 is available at RUNTIME (not compile time)
    if (dap_cpu_arch_is_available(DAP_CPU_ARCH_AVX2)) {
        log_it(L_INFO, "Testing AVX2 correctness...");
        bool l_result = s_test_simd_impl(dap_json_stage1_run_avx2, "AVX2");
        dap_assert(l_result, "AVX2 correctness test");
        return l_result;
    } else {
        log_it(L_INFO, "AVX2 not available on this CPU, skipping");
        return true;  // Skip is not a failure
    }
#else
    log_it(L_INFO, "AVX2 not supported on this architecture");
    return true;  // Skip on non-x86
#endif
}

/**
 * @brief Test SSE2 correctness
 */
static bool s_test_sse2_correctness(void)
{
    log_it(L_DEBUG, "Testing SSE2 correctness");
    
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    if (dap_cpu_arch_is_available(DAP_CPU_ARCH_SSE2)) {
        log_it(L_INFO, "Testing SSE2 correctness...");
        bool l_result = s_test_simd_impl(dap_json_stage1_run_sse2, "SSE2");
        dap_assert(l_result, "SSE2 correctness test");
        return l_result;
    } else {
        log_it(L_INFO, "SSE2 not available on this CPU, skipping");
        return true;
    }
#else
    log_it(L_INFO, "SSE2 not supported on this architecture");
    return true;  // Skip on non-x86
#endif
}

/**
 * @brief Test NEON correctness
 */
static bool s_test_neon_correctness(void)
{
    log_it(L_DEBUG, "Testing NEON correctness");
    
#if defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
    if (dap_cpu_arch_is_available(DAP_CPU_ARCH_NEON)) {
        log_it(L_INFO, "Testing NEON correctness...");
        bool l_result = s_test_simd_impl(dap_json_stage1_run_neon, "NEON");
        dap_assert(l_result, "NEON correctness test");
        return l_result;
    } else {
        log_it(L_INFO, "NEON not available on this CPU, skipping");
        return true;
    }
#else
    log_it(L_INFO, "NEON not available on this architecture (x86/x64), skipping");
    return true;
#endif
}

/**
 * @brief Test AVX-512 correctness
 */
static bool s_test_avx512_correctness(void)
{
    log_it(L_DEBUG, "Testing AVX-512 correctness");
    
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    if (dap_cpu_arch_is_available(DAP_CPU_ARCH_AVX512)) {
        log_it(L_INFO, "Testing AVX-512 correctness...");
        bool l_result = s_test_simd_impl(dap_json_stage1_run_avx512, "AVX-512");
        dap_assert(l_result, "AVX-512 correctness test");
        return l_result;
    } else {
        log_it(L_INFO, "AVX-512 not available on this CPU, skipping");
        return true;
    }
#else
    log_it(L_INFO, "AVX-512 not supported on this architecture");
    return true;  // Skip on non-x86
#endif
}

int main(void)
{
    dap_json_init(); // Initialize dispatch
    
    dap_print_module_name("SIMD Correctness Tests (vs Reference)");
    
    // Test x86/x64 SIMD implementations
    #if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    s_test_avx2_correctness();
    s_test_sse2_correctness();
    s_test_avx512_correctness();
    #endif
    
    // Test ARM SIMD implementations
    #if defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
    s_test_neon_correctness();
    #endif
    
    log_it(L_INFO, "=== All SIMD Correctness Tests Passed ===");
    
    return 0;
}

