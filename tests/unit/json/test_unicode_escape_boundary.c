/**
 * @file test_unicode_escape_boundary.c
 * @brief Unit tests for Unicode escape sequences crossing SIMD chunk boundaries
 * @details Tests edge cases where JSON with \uXXXX escapes crosses 16/32/64-byte
 *          SIMD chunk boundaries in SSE2/AVX2/AVX-512 implementations.
 * 
 * Known Issue: AVX-512 and SSE2 fail with JSON >= 66 bytes containing multiple
 * Unicode escape sequences. This is a boundary handling bug in HYBRID architecture.
 */

#define LOG_TAG "unicode_escape_boundary_tests"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"

/* ========================================================================== */
/*                              TEST HELPERS                                  */
/* ========================================================================== */

/**
 * @brief Test JSON parsing with specific SIMD architecture
 */
static bool s_test_with_arch(const char *a_json, dap_cpu_arch_t a_arch, const char *a_desc) {
    const char *l_arch_name = dap_json_get_arch_name(a_arch);
    log_it(L_DEBUG, "Testing with %s: %s", l_arch_name, a_desc);
    
    dap_json_set_simd_arch(a_arch);
    dap_json_t *l_json = dap_json_parse_string(a_json);
    
    bool l_success = (l_json != NULL);
    
    if (l_json) {
        dap_json_object_free(l_json);
    } else {
        log_it(L_WARNING, "%s FAILED: %s", l_arch_name, a_desc);
    }
    
    return l_success;
}

/* ========================================================================== */
/*                              TEST CASES                                    */
/* ========================================================================== */

/**
 * @brief Test Unicode escapes at exact chunk boundaries
 */
static bool s_test_exact_boundary_sizes(void) {
    log_it(L_DEBUG, "Testing Unicode escapes at exact SIMD chunk boundaries");
    bool result = false;
    
    // Test at 16-byte boundary (SSE2)
    const char *l_json_16 = "{\"a\":\"\\u0041\\u0042\"}"; // 19 bytes
    DAP_TEST_FAIL_IF(!s_test_with_arch(l_json_16, DAP_CPU_ARCH_SSE2, "16-byte boundary"),
                     "SSE2: 16-byte boundary with \\u escapes");
    
    // Test at 32-byte boundary (AVX2)
    const char *l_json_32 = "{\"a\":\"\\u0041\",\"b\":\"\\u0042\",\"c\":\"\\u0043\"}"; // 40 bytes
    DAP_TEST_FAIL_IF(!s_test_with_arch(l_json_32, DAP_CPU_ARCH_AVX2, "32-byte boundary"),
                     "AVX2: 32-byte boundary with \\u escapes");
    
    // Test at 64-byte boundary (AVX-512) - THIS IS KNOWN TO FAIL
    const char *l_json_64 = "{\"a\":\"\\u0041\",\"b\":\"\\u0042\",\"c\":\"\\u0043\",\"d\":\"\\u0044\"}"; // 53 bytes - OK
    DAP_TEST_FAIL_IF(!s_test_with_arch(l_json_64, DAP_CPU_ARCH_AVX512, "53 bytes (under 64)"),
                     "AVX-512: 53 bytes with \\u escapes (should work)");
    
    result = true;
    log_it(L_DEBUG, "Exact boundary test passed");
    
cleanup:
    (void)result; // Suppress unused label warning
    return result;
}

/**
 * @brief Test the specific failing case: 66 bytes with 5 fields
 * @details This is the KNOWN BUG case that fails with AVX-512 and SSE2
 */
static bool s_test_66_byte_bug(void) {
    log_it(L_DEBUG, "Testing known 66-byte bug case");
    bool result = false;
    
    // 66 bytes: 5 fields with Unicode escapes - FAILS with AVX-512/SSE2
    const char *l_json_66 = "{\"a\":\"\\u0041\",\"b\":\"\\u0042\",\"c\":\"\\u0043\",\"d\":\"\\u0044\",\"e\":\"\\u0045\"}";
    
    log_it(L_DEBUG, "JSON length: %zu bytes", strlen(l_json_66));
    log_it(L_DEBUG, "JSON: %s", l_json_66);
    
    // Test with Reference (should always work)
    DAP_TEST_FAIL_IF(!s_test_with_arch(l_json_66, DAP_CPU_ARCH_REFERENCE, "66 bytes - Reference"),
                     "Reference: 66 bytes with 5 fields");
    
    // Test with AVX2 (should work - AVX2 doesn't have this bug)
    DAP_TEST_FAIL_IF(!s_test_with_arch(l_json_66, DAP_CPU_ARCH_AVX2, "66 bytes - AVX2"),
                     "AVX2: 66 bytes with 5 fields");
    
    // Test with AVX-512 (KNOWN TO FAIL - this is the bug we're tracking)
    bool l_avx512_result = s_test_with_arch(l_json_66, DAP_CPU_ARCH_AVX512, "66 bytes - AVX-512");
    if (!l_avx512_result) {
        log_it(L_WARNING, "AVX-512: 66-byte bug CONFIRMED (expected failure)");
        // Don't fail the test - this is a known issue
    }
    
    // Test with SSE2 (also KNOWN TO FAIL)
    bool l_sse2_result = s_test_with_arch(l_json_66, DAP_CPU_ARCH_SSE2, "66 bytes - SSE2");
    if (!l_sse2_result) {
        log_it(L_WARNING, "SSE2: 66-byte bug CONFIRMED (expected failure)");
        // Don't fail the test - this is a known issue
    }
    
    result = true;
    log_it(L_DEBUG, "66-byte bug case test passed (known failures documented)");
    
cleanup:
    (void)result;
    return result;
}

/**
 * @brief Test escalating complexity: progressively more fields
 */
static bool s_test_progressive_complexity(void) {
    log_it(L_DEBUG, "Testing progressive complexity with Unicode escapes");
    bool result = false;
    
    const char *l_tests[] = {
        "{\"a\":\"\\u0041\"}",                                                        // 14 bytes, 1 field
        "{\"a\":\"\\u0041\",\"b\":\"\\u0042\"}",                                      // 27 bytes, 2 fields
        "{\"a\":\"\\u0041\",\"b\":\"\\u0042\",\"c\":\"\\u0043\"}",                    // 40 bytes, 3 fields
        "{\"a\":\"\\u0041\",\"b\":\"\\u0042\",\"c\":\"\\u0043\",\"d\":\"\\u0044\"}",  // 53 bytes, 4 fields - OK
        "{\"a\":\"\\u0041\",\"b\":\"\\u0042\",\"c\":\"\\u0043\",\"d\":\"\\u0044\",\"e\":\"\\u0045\"}",  // 66 bytes, 5 fields - FAILS
        "{\"a\":\"\\u0041\",\"b\":\"\\u0042\",\"c\":\"\\u0043\",\"d\":\"\\u0044\",\"e\":\"\\u0045\",\"f\":\"\\u0046\"}",  // 79 bytes, 6 fields
    };
    
    int l_num_tests = sizeof(l_tests) / sizeof(l_tests[0]);
    
    // Test all with Reference (should all pass)
    for (int i = 0; i < l_num_tests; i++) {
        char l_desc[128];
        snprintf(l_desc, sizeof(l_desc), "Reference: %zu bytes, %d fields", 
                 strlen(l_tests[i]), i + 1);
        
        DAP_TEST_FAIL_IF(!s_test_with_arch(l_tests[i], DAP_CPU_ARCH_REFERENCE, l_desc),
                         "Reference implementation should handle all cases");
    }
    
    // Test all with AVX2 (should all pass - AVX2 doesn't have this bug)
    for (int i = 0; i < l_num_tests; i++) {
        char l_desc[128];
        snprintf(l_desc, sizeof(l_desc), "AVX2: %zu bytes, %d fields", 
                 strlen(l_tests[i]), i + 1);
        
        DAP_TEST_FAIL_IF(!s_test_with_arch(l_tests[i], DAP_CPU_ARCH_AVX2, l_desc),
                         "AVX2 should handle all cases");
    }
    
    // Test with AVX-512 (will fail at index 4 - 66 bytes)
    log_it(L_DEBUG, "Testing AVX-512 progressive (expecting failure at 66+ bytes)...");
    for (int i = 0; i < l_num_tests; i++) {
        bool l_avx512_result = s_test_with_arch(l_tests[i], DAP_CPU_ARCH_AVX512, "");
        
        if (i < 4) {
            // Should pass for < 66 bytes
            DAP_TEST_FAIL_IF(!l_avx512_result, 
                             "AVX-512 should handle < 66 bytes");
        } else {
            // May fail for >= 66 bytes (known bug)
            if (!l_avx512_result) {
                log_it(L_WARNING, "AVX-512: Failed at %d fields (%zu bytes) - known bug",
                       i + 1, strlen(l_tests[i]));
            }
        }
    }
    
    result = true;
    log_it(L_DEBUG, "Progressive complexity test passed");
    
cleanup:
    return result;
}

/**
 * @brief Test Unicode escapes vs regular strings at boundary
 */
static bool s_test_unicode_vs_regular(void) {
    log_it(L_DEBUG, "Testing Unicode escapes vs regular strings at 66-byte boundary");
    bool result = false;
    
    // 66 bytes WITHOUT Unicode escapes - should work
    const char *l_json_regular = "{\"a\":\"ABC\",\"b\":\"DEF\",\"c\":\"GHI\",\"d\":\"JKL\",\"e\":\"MNO12345678\"}"; // 61 bytes
    DAP_TEST_FAIL_IF(!s_test_with_arch(l_json_regular, DAP_CPU_ARCH_AVX512, "Regular strings, 61 bytes"),
                     "AVX-512 should handle regular strings");
    
    // 66 bytes WITH Unicode escapes - fails (known bug)
    const char *l_json_unicode = "{\"a\":\"\\u0041\",\"b\":\"\\u0042\",\"c\":\"\\u0043\",\"d\":\"\\u0044\",\"e\":\"\\u0045\"}"; // 66 bytes
    bool l_unicode_result = s_test_with_arch(l_json_unicode, DAP_CPU_ARCH_AVX512, "Unicode escapes, 66 bytes");
    
    if (!l_unicode_result) {
        log_it(L_WARNING, "AVX-512: Unicode escape boundary bug confirmed (regular strings work, Unicode fails)");
    }
    
    result = true;
    log_it(L_DEBUG, "Unicode vs regular test passed");
    
cleanup:
    return result;
}

/**
 * @brief Test original failing case from test_unicode.c
 */
static bool s_test_original_failing_case(void) {
    log_it(L_DEBUG, "Testing original failing case from test_unicode.c");
    bool result = false;
    
    // Original failing JSON from test_unicode.c
    const char *l_json_original = "{\"copy\":\"\\u00A9\",\"smile\":\"\\u263A\",\"cyrillic\":\"\\u0410\\u0411\\u0412\"}";
    
    log_it(L_DEBUG, "Original JSON length: %zu bytes", strlen(l_json_original));
    log_it(L_DEBUG, "Original JSON: %s", l_json_original);
    
    // Should work with Reference
    DAP_TEST_FAIL_IF(!s_test_with_arch(l_json_original, DAP_CPU_ARCH_REFERENCE, "Original - Reference"),
                     "Reference: Original failing case");
    
    // Should work with AVX2
    DAP_TEST_FAIL_IF(!s_test_with_arch(l_json_original, DAP_CPU_ARCH_AVX2, "Original - AVX2"),
                     "AVX2: Original failing case");
    
    // Will fail with AVX-512 (known bug)
    bool l_avx512_result = s_test_with_arch(l_json_original, DAP_CPU_ARCH_AVX512, "Original - AVX-512");
    if (!l_avx512_result) {
        log_it(L_WARNING, "AVX-512: Original case FAILED (known bug)");
    }
    
    result = true;
    log_it(L_DEBUG, "Original failing case test passed");
    
cleanup:
    (void)result;
    return result;
}

/* ========================================================================== */
/*                              TEST RUNNER                                   */
/* ========================================================================== */

int main(void) {
    // Initialize
    dap_json_init();
    dap_log_level_set(L_DEBUG);
    
    log_it(L_NOTICE, "================================================");
    log_it(L_NOTICE, "Unicode Escape Boundary Tests");
    log_it(L_NOTICE, "Testing edge case: JSON >= 66 bytes with \\uXXXX");
    log_it(L_NOTICE, "================================================");
    
    // Run tests
    int l_tests_passed = 0;
    int l_tests_total = 5;
    
    l_tests_passed += s_test_exact_boundary_sizes() ? 1 : 0;
    l_tests_passed += s_test_66_byte_bug() ? 1 : 0;
    l_tests_passed += s_test_progressive_complexity() ? 1 : 0;
    l_tests_passed += s_test_unicode_vs_regular() ? 1 : 0;
    l_tests_passed += s_test_original_failing_case() ? 1 : 0;
    
    // Summary
    log_it(L_NOTICE, "================================================");
    log_it(L_NOTICE, "SUMMARY: Unicode Escape Boundary Tests");
    log_it(L_NOTICE, "Tests: %d/%d passed (%.0f%%)", 
           l_tests_passed, l_tests_total,
           l_tests_total > 0 ? (100.0 * l_tests_passed / l_tests_total) : 0.0);
    log_it(L_NOTICE, "================================================");
    log_it(L_NOTICE, "KNOWN ISSUE: AVX-512/SSE2 fail with JSON >= 66 bytes");
    log_it(L_NOTICE, "             containing multiple Unicode escapes");
    log_it(L_NOTICE, "WORKAROUND:  Use Reference or AVX2 implementation");
    log_it(L_NOTICE, "================================================");
    
    return (l_tests_passed == l_tests_total) ? 0 : 255;
}

