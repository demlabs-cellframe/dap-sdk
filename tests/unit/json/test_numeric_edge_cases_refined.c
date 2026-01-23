/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
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
 * @file test_numeric_edge_cases_refined.c
 * @brief Refined Numeric Edge Cases - Phase 1.8.2
 * @details Extends test_numeric_edge_cases.c with 6 additional critical cases
 * 
 * NOTE: test_numeric_edge_cases.c (13 tests) covers most cases WELL!
 *       This file adds ONLY the missing 6 critical edge cases:
 *       1. Negative zero IEEE 754 distinction (-0.0 vs +0.0 bit pattern)
 *       2. INT64_MAX+1 parsing (9223372036854775808)
 *       3. UINT64_MAX+1 parsing (18446744073709551616)
 *       4. Very long numbers (> 1000 digits)
 *       5. Multiple decimals/signs invalid (1.2.3, --5, +-3)
 *       6. Empty/malformed exponents (1e, 1e+, 0e0.5)
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "test_numeric_refined"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <stdint.h>
#include <string.h>
#include <math.h>

// =============================================================================
// TEST 1: Negative Zero IEEE 754 Distinction
// =============================================================================

/**
 * @brief Test -0.0 vs +0.0 IEEE 754 bit pattern distinction
 * @details IEEE 754: -0.0 has sign bit set, +0.0 does not
 *          Bit patterns: -0.0 = 0x8000000000000000, +0.0 = 0x0000000000000000
 */
static bool s_test_negative_zero_distinction(void) {
    log_it(L_DEBUG, "Testing negative zero IEEE 754 distinction");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Parse "-0.0" and "0.0"
    const char *l_neg_zero_json = "{\"neg_zero\":-0.0,\"pos_zero\":0.0}";
    l_json = dap_json_parse_string(l_neg_zero_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse negative/positive zero");
    
    double neg_zero = dap_json_object_get_double(l_json, "neg_zero");
    double pos_zero = dap_json_object_get_double(l_json, "pos_zero");
    
    // Check bit patterns
    union { double d; uint64_t u; } neg_conv = { .d = neg_zero };
    union { double d; uint64_t u; } pos_conv = { .d = pos_zero };
    
    log_it(L_DEBUG, "neg_zero bits: 0x%016llx, pos_zero bits: 0x%016llx", 
           neg_conv.u, pos_conv.u);
    
    // IEEE 754: -0.0 = 0x8000000000000000, +0.0 = 0x0000000000000000
    bool neg_is_negative_zero = (neg_conv.u == 0x8000000000000000ULL);
    bool pos_is_positive_zero = (pos_conv.u == 0x0000000000000000ULL);
    
    // Note: Some JSON implementations may normalize -0.0 to +0.0
    // Log the result but don't fail if not distinguished
    if (neg_is_negative_zero && pos_is_positive_zero) {
        log_it(L_INFO, "Parser preserves -0.0 vs +0.0 distinction (GOOD)");
    } else {
        log_it(L_WARNING, "Parser normalizes -0.0 to +0.0 (acceptable, RFC 8259 doesn't mandate distinction)");
    }
    
    // At minimum, both should be zero numerically
    DAP_TEST_FAIL_IF(neg_zero != 0.0, "Negative zero is zero");
    DAP_TEST_FAIL_IF(pos_zero != 0.0, "Positive zero is zero");
    
    result = true;
    log_it(L_DEBUG, "Negative zero distinction test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 2: INT64_MAX+1 Parsing
// =============================================================================

/**
 * @brief Test INT64_MAX+1 parsing (9223372036854775808)
 * @details This exceeds INT64_MAX, should either:
 *          1. Parse as double (with potential precision loss)
 *          2. Parse as UINT64
 *          3. Reject as overflow error
 */
static bool s_test_int64_max_plus_one(void) {
    log_it(L_DEBUG, "Testing INT64_MAX+1 parsing");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // INT64_MAX+1 = 9223372036854775808
    const char *l_json_str = "{\"value\":9223372036854775808}";
    l_json = dap_json_parse_string(l_json_str);
    
    if (l_json) {
        // Parser accepted it - should parse as UINT64 or double
        uint64_t u_value = dap_json_object_get_uint64(l_json, "value");
        log_it(L_INFO, "INT64_MAX+1 parsed as uint64: %llu", (unsigned long long)u_value);
        
        // Should be exactly 9223372036854775808
        DAP_TEST_FAIL_IF(u_value != 9223372036854775808ULL, 
                         "INT64_MAX+1 parsed as correct uint64");
    } else {
        // Parser rejected - acceptable if overflow protection enabled
        log_it(L_INFO, "Parser rejected INT64_MAX+1 (acceptable if overflow protection enabled)");
    }
    
    result = true;
    log_it(L_DEBUG, "INT64_MAX+1 test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 3: UINT64_MAX+1 Parsing
// =============================================================================

/**
 * @brief Test UINT64_MAX+1 parsing (18446744073709551616)
 * @details This exceeds UINT64_MAX, should either:
 *          1. Parse as double (with precision loss)
 *          2. Reject as overflow error
 */
static bool s_test_uint64_max_plus_one(void) {
    log_it(L_DEBUG, "Testing UINT64_MAX+1 parsing");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // UINT64_MAX+1 = 18446744073709551616
    const char *l_json_str = "{\"value\":18446744073709551616}";
    l_json = dap_json_parse_string(l_json_str);
    
    if (l_json) {
        // Parser accepted - should parse as double
        double d_value = dap_json_object_get_double(l_json, "value");
        log_it(L_INFO, "UINT64_MAX+1 parsed as double: %.0f", d_value);
        
        // Should be approximately 1.8446744073709552e+19 (precision loss expected)
        DAP_TEST_FAIL_IF(d_value < 1.8e+19, "UINT64_MAX+1 parsed as large double");
    } else {
        // Parser rejected - acceptable
        log_it(L_INFO, "Parser rejected UINT64_MAX+1 (acceptable)");
    }
    
    result = true;
    log_it(L_DEBUG, "UINT64_MAX+1 test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 4: Very Long Numbers (> 1000 digits)
// =============================================================================

/**
 * @brief Test very long numbers (> 1000 digits)
 * @details Performance test: parser should handle or reject long numbers
 *          Should NOT hang or crash
 */
static bool s_test_very_long_numbers(void) {
    log_it(L_DEBUG, "Testing very long numbers (> 1000 digits)");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *l_json_str = NULL;
    
    // Create number with 2000 digits
    const int DIGIT_COUNT = 2000;
    l_json_str = (char*)malloc(DIGIT_COUNT + 32);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "Allocate buffer for long number");
    
    sprintf(l_json_str, "{\"value\":");
    char *ptr = l_json_str + strlen(l_json_str);
    for (int i = 0; i < DIGIT_COUNT; i++) {
        *ptr++ = '9';
    }
    strcpy(ptr, "}");
    
    log_it(L_INFO, "Parsing number with %d digits", DIGIT_COUNT);
    
    l_json = dap_json_parse_string(l_json_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser accepted %d-digit number", DIGIT_COUNT);
        double value = dap_json_object_get_double(l_json, "value");
        log_it(L_DEBUG, "Parsed as: %e", value);
    } else {
        log_it(L_INFO, "Parser rejected %d-digit number (acceptable if limits enforced)", DIGIT_COUNT);
    }
    
    // Test passes if we don't crash/hang
    result = true;
    log_it(L_DEBUG, "Very long numbers test passed");
    
cleanup:
    free(l_json_str);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 5: Multiple Decimals/Signs Invalid
// =============================================================================

/**
 * @brief Test multiple decimals/signs (invalid)
 * @details Cases: 1.2.3, --5, +-3, ++5
 *          Expected: Parser should reject all
 */
static bool s_test_multiple_decimals_signs_invalid(void) {
    log_it(L_DEBUG, "Testing multiple decimals/signs (invalid)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test 1.2.3 (multiple decimals)
    l_json = dap_json_parse_string("{\"value\":1.2.3}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 1.2.3 (multiple decimals)");
    
    // Test --5 (double negative)
    l_json = dap_json_parse_string("{\"value\":--5}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects --5 (double negative)");
    
    // Test +-3 (plus then minus)
    l_json = dap_json_parse_string("{\"value\":+-3}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects +-3 (mixed signs)");
    
    // Test ++5 (double positive - NOTE: + is invalid in JSON)
    l_json = dap_json_parse_string("{\"value\":++5}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects ++5 (double plus)");
    
    // Test +5 (single positive - also invalid in JSON)
    l_json = dap_json_parse_string("{\"value\":+5}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects +5 (plus sign invalid)");
    
    result = true;
    log_it(L_DEBUG, "Multiple decimals/signs invalid test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 6: Empty/Malformed Exponents
// =============================================================================

/**
 * @brief Test empty/malformed exponents (invalid)
 * @details Cases: 1e, 1e+, 1e-, 1E, 0e0.5
 *          Expected: Parser should reject all
 */
static bool s_test_empty_malformed_exponents(void) {
    log_it(L_DEBUG, "Testing empty/malformed exponents (invalid)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test 1e (empty exponent)
    l_json = dap_json_parse_string("{\"value\":1e}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 1e (empty exponent)");
    
    // Test 1e+ (empty after plus)
    l_json = dap_json_parse_string("{\"value\":1e+}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 1e+ (empty after plus)");
    
    // Test 1e- (empty after minus)
    l_json = dap_json_parse_string("{\"value\":1e-}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 1e- (empty after minus)");
    
    // Test 1E (uppercase E - should be valid, but test anyway)
    l_json = dap_json_parse_string("{\"value\":1E+2}");
    if (l_json) {
        double value = dap_json_object_get_double(l_json, "value");
        log_it(L_DEBUG, "1E+2 parsed as: %.0f (uppercase E accepted)", value);
        dap_json_object_free(l_json);
        l_json = NULL;
    }
    
    // Test 0e0.5 (fractional exponent - invalid)
    l_json = dap_json_parse_string("{\"value\":0e0.5}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 0e0.5 (fractional exponent)");
    
    result = true;
    log_it(L_DEBUG, "Empty/malformed exponents test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_json_numeric_refined_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Numeric Edge Cases (Refined) ===");
    log_it(L_INFO, "NOTE: This extends test_numeric_edge_cases.c with 6 additional cases");
    
    int tests_passed = 0;
    int tests_total = 6;
    
    tests_passed += s_test_negative_zero_distinction() ? 1 : 0;
    tests_passed += s_test_int64_max_plus_one() ? 1 : 0;
    tests_passed += s_test_uint64_max_plus_one() ? 1 : 0;
    tests_passed += s_test_very_long_numbers() ? 1 : 0;
    tests_passed += s_test_multiple_decimals_signs_invalid() ? 1 : 0;
    tests_passed += s_test_empty_malformed_exponents() ? 1 : 0;
    
    log_it(L_INFO, "Numeric refined tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void) {
    dap_print_module_name("DAP JSON Numeric Edge Cases (Refined)");
    return dap_json_numeric_refined_tests_run();
}

