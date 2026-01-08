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
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <stdint.h>
#include <limits.h>
#include <float.h>
#include <math.h>

#define LOG_TAG "dap_json_numeric_tests"

/**
 * @brief Test INT64_MIN boundary
 */
static bool s_test_int64_min(void) {
    log_it(L_DEBUG, "Testing INT64_MIN");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "Create JSON object");
    
    // INT64_MIN = -9223372036854775808
    dap_json_object_add_int64(l_json, "min", INT64_MIN);
    
    int64_t l_value = dap_json_object_get_int64(l_json, "min");
    DAP_TEST_FAIL_IF(l_value != INT64_MIN, "INT64_MIN round-trip");
    
    // Test parsing from string
    const char *l_min_json = "{\"value\":-9223372036854775808}";
    dap_json_t *l_json2 = dap_json_parse_string(l_min_json);
    DAP_TEST_FAIL_IF_NULL(l_json2, "Parse INT64_MIN from JSON");
    
    int64_t l_parsed = dap_json_object_get_int64(l_json2, "value");
    DAP_TEST_FAIL_IF(l_parsed != INT64_MIN, "Parse INT64_MIN correctly");
    
    dap_json_object_free(l_json2);
    
    result = true;
    log_it(L_DEBUG, "INT64_MIN test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test INT64_MAX boundary
 */
static bool s_test_int64_max(void) {
    log_it(L_DEBUG, "Testing INT64_MAX");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "Create JSON object");
    
    // INT64_MAX = 9223372036854775807
    dap_json_object_add_int64(l_json, "max", INT64_MAX);
    
    int64_t l_value = dap_json_object_get_int64(l_json, "max");
    DAP_TEST_FAIL_IF(l_value != INT64_MAX, "INT64_MAX round-trip");
    
    // Test parsing from string
    const char *l_max_json = "{\"value\":9223372036854775807}";
    dap_json_t *l_json2 = dap_json_parse_string(l_max_json);
    DAP_TEST_FAIL_IF_NULL(l_json2, "Parse INT64_MAX from JSON");
    
    int64_t l_parsed = dap_json_object_get_int64(l_json2, "value");
    DAP_TEST_FAIL_IF(l_parsed != INT64_MAX, "Parse INT64_MAX correctly");
    
    dap_json_object_free(l_json2);
    
    result = true;
    log_it(L_DEBUG, "INT64_MAX test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test UINT64_MAX boundary
 */
static bool s_test_uint64_max(void) {
    log_it(L_DEBUG, "Testing UINT64_MAX");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "Create JSON object");
    
    // UINT64_MAX = 18446744073709551615
    dap_json_object_add_uint64(l_json, "max", UINT64_MAX);
    
    uint64_t l_value = dap_json_object_get_uint64(l_json, "max");
    DAP_TEST_FAIL_IF(l_value != UINT64_MAX, "UINT64_MAX round-trip");
    
    // Test parsing from string
    const char *l_max_json = "{\"value\":18446744073709551615}";
    dap_json_t *l_json2 = dap_json_parse_string(l_max_json);
    DAP_TEST_FAIL_IF_NULL(l_json2, "Parse UINT64_MAX from JSON");
    
    uint64_t l_parsed = dap_json_object_get_uint64(l_json2, "value");
    DAP_TEST_FAIL_IF(l_parsed != UINT64_MAX, "Parse UINT64_MAX correctly");
    
    dap_json_object_free(l_json2);
    
    result = true;
    log_it(L_DEBUG, "UINT64_MAX test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test uint256_t boundaries
 */
static bool s_test_uint256_boundaries(void) {
    log_it(L_DEBUG, "Testing uint256_t boundaries");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "Create JSON object");
    
    // uint256_t max value (all bits set)
    uint256_t l_max_256 = uint256_max;
    dap_json_object_add_uint256(l_json, "max256", l_max_256);
    
    uint256_t l_retrieved;
    int l_ret = dap_json_object_get_uint256(l_json, "max256", &l_retrieved);
    DAP_TEST_FAIL_IF(l_ret != 0, "Get uint256 value");
    DAP_TEST_FAIL_IF(!EQUAL_256(l_retrieved, l_max_256), "uint256_t MAX round-trip");
    
    // uint256_t zero
    uint256_t l_zero = uint256_0;
    dap_json_object_add_uint256(l_json, "zero256", l_zero);
    
    uint256_t l_zero_retrieved;
    l_ret = dap_json_object_get_uint256(l_json, "zero256", &l_zero_retrieved);
    DAP_TEST_FAIL_IF(l_ret != 0, "Get uint256 zero");
    DAP_TEST_FAIL_IF(!IS_ZERO_256(l_zero_retrieved), "uint256_t zero round-trip");
    
    result = true;
    log_it(L_DEBUG, "uint256_t boundaries test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test floating point infinity (+Inf, -Inf)
 */
static bool s_test_float_infinity(void) {
    log_it(L_DEBUG, "Testing floating point infinity");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "Create JSON object");
    
    // Positive infinity
    double l_pos_inf = INFINITY;
    dap_json_object_add_double(l_json, "pos_inf", l_pos_inf);
    
    double l_retrieved_pos = dap_json_object_get_double(l_json, "pos_inf");
    DAP_TEST_FAIL_IF(!isinf(l_retrieved_pos) || l_retrieved_pos < 0, 
                     "Positive infinity round-trip");
    
    // Negative infinity
    double l_neg_inf = -INFINITY;
    dap_json_object_add_double(l_json, "neg_inf", l_neg_inf);
    
    double l_retrieved_neg = dap_json_object_get_double(l_json, "neg_inf");
    DAP_TEST_FAIL_IF(!isinf(l_retrieved_neg) || l_retrieved_neg > 0, 
                     "Negative infinity round-trip");
    
    result = true;
    log_it(L_DEBUG, "Floating point infinity test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test NaN (Not a Number)
 */
static bool s_test_float_nan(void) {
    log_it(L_DEBUG, "Testing NaN");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "Create JSON object");
    
    // NaN
    double l_nan = NAN;
    dap_json_object_add_double(l_json, "nan", l_nan);
    
    double l_retrieved = dap_json_object_get_double(l_json, "nan");
    DAP_TEST_FAIL_IF(!isnan(l_retrieved), "NaN round-trip");
    
    result = true;
    log_it(L_DEBUG, "NaN test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test denormalized floats (very small numbers)
 */
static bool s_test_float_denormalized(void) {
    log_it(L_DEBUG, "Testing denormalized floats");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "Create JSON object");
    
    // Smallest positive denormalized double
    double l_denorm = DBL_MIN / 2.0;
    dap_json_object_add_double(l_json, "denorm", l_denorm);
    
    double l_retrieved = dap_json_object_get_double(l_json, "denorm");
    // May lose precision, but should be very close
    DAP_TEST_FAIL_IF(fabs(l_retrieved - l_denorm) > l_denorm * 0.1, 
                     "Denormalized float approximate round-trip");
    
    result = true;
    log_it(L_DEBUG, "Denormalized floats test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test very large exponents
 */
static bool s_test_large_exponents(void) {
    log_it(L_DEBUG, "Testing large exponents");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test parsing 1e308 (near DBL_MAX)
    const char *l_large_exp = "{\"large\":1e308}";
    l_json = dap_json_parse_string(l_large_exp);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse large exponent");
    
    double l_value = dap_json_object_get_double(l_json, "large");
    DAP_TEST_FAIL_IF(l_value < 1e307, "Large exponent value");
    
    dap_json_object_free(l_json);
    
    // Test parsing 1e-308 (near DBL_MIN)
    const char *l_small_exp = "{\"small\":1e-308}";
    l_json = dap_json_parse_string(l_small_exp);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse small exponent");
    
    double l_small = dap_json_object_get_double(l_json, "small");
    DAP_TEST_FAIL_IF(l_small > 1e-307, "Small exponent value");
    DAP_TEST_FAIL_IF(l_small == 0.0, "Small exponent not zero");
    
    result = true;
    log_it(L_DEBUG, "Large exponents test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test leading zeros (invalid in JSON)
 */
static bool s_test_leading_zeros_invalid(void) {
    log_it(L_DEBUG, "Testing leading zeros (invalid)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Leading zeros are invalid in JSON (except "0" itself)
    const char *l_invalid_json = "{\"bad\":00123}";
    
    l_json = dap_json_parse_string(l_invalid_json);
    // Parser should reject this
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects leading zeros");
    
    // Single zero is valid
    const char *l_valid_json = "{\"ok\":0}";
    l_json = dap_json_parse_string(l_valid_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Single zero is valid");
    
    result = true;
    log_it(L_DEBUG, "Leading zeros test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test numeric overflow
 */
static bool s_test_numeric_overflow(void) {
    log_it(L_DEBUG, "Testing numeric overflow");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Number too large for int64_t: 9223372036854775808 (INT64_MAX + 1)
    const char *l_overflow_json = "{\"overflow\":9223372036854775808}";
    
    l_json = dap_json_parse_string(l_overflow_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse overflow number");
    
    // Should be treated as uint64_t or double
    // Verify it's not INT64_MIN (wraparound)
    int64_t l_as_int64 = dap_json_object_get_int64(l_json, "overflow");
    DAP_TEST_FAIL_IF(l_as_int64 == INT64_MIN, "Overflow doesn't wrap to INT64_MIN");
    
    // Should work as uint64_t
    uint64_t l_as_uint64 = dap_json_object_get_uint64(l_json, "overflow");
    DAP_TEST_FAIL_IF(l_as_uint64 != (uint64_t)INT64_MAX + 1, "Overflow as uint64");
    
    result = true;
    log_it(L_DEBUG, "Numeric overflow test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test numeric underflow
 */
static bool s_test_numeric_underflow(void) {
    log_it(L_DEBUG, "Testing numeric underflow");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Number too small: -9223372036854775809 (INT64_MIN - 1)
    const char *l_underflow_json = "{\"underflow\":-9223372036854775809}";
    
    l_json = dap_json_parse_string(l_underflow_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse underflow number");
    
    // Should be treated as double or handled gracefully
    int64_t l_as_int64 = dap_json_object_get_int64(l_json, "underflow");
    DAP_TEST_FAIL_IF(l_as_int64 == INT64_MAX, "Underflow doesn't wrap to INT64_MAX");
    
    result = true;
    log_it(L_DEBUG, "Numeric underflow test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test precision of large integers
 */
static bool s_test_large_integer_precision(void) {
    log_it(L_DEBUG, "Testing large integer precision");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Large integer that fits in int64_t but may lose precision in double
    int64_t l_large = 9007199254740993LL;  // 2^53 + 1 (beyond double precision)
    
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "Create JSON object");
    
    dap_json_object_add_int64(l_json, "large", l_large);
    
    int64_t l_retrieved = dap_json_object_get_int64(l_json, "large");
    DAP_TEST_FAIL_IF(l_retrieved != l_large, "Large integer precision preserved");
    
    result = true;
    log_it(L_DEBUG, "Large integer precision test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test zero variants (+0, -0, 0.0)
 */
static bool s_test_zero_variants(void) {
    log_it(L_DEBUG, "Testing zero variants");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *l_zeros_json = "{\"pos\":+0,\"neg\":-0,\"float\":0.0}";
    
    l_json = dap_json_parse_string(l_zeros_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse zero variants");
    
    // All should be zero
    int l_pos = dap_json_object_get_int(l_json, "pos");
    int l_neg = dap_json_object_get_int(l_json, "neg");
    double l_float = dap_json_object_get_double(l_json, "float");
    
    DAP_TEST_FAIL_IF(l_pos != 0, "Positive zero");
    DAP_TEST_FAIL_IF(l_neg != 0, "Negative zero");
    DAP_TEST_FAIL_IF(l_float != 0.0, "Float zero");
    
    result = true;
    log_it(L_DEBUG, "Zero variants test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Main test runner for numeric edge cases
 */
int dap_json_numeric_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Numeric Edge Cases Tests ===");
    
    int tests_passed = 0;
    int tests_total = 13;
    
    tests_passed += s_test_int64_min() ? 1 : 0;
    tests_passed += s_test_int64_max() ? 1 : 0;
    tests_passed += s_test_uint64_max() ? 1 : 0;
    tests_passed += s_test_uint256_boundaries() ? 1 : 0;
    tests_passed += s_test_float_infinity() ? 1 : 0;
    tests_passed += s_test_float_nan() ? 1 : 0;
    tests_passed += s_test_float_denormalized() ? 1 : 0;
    tests_passed += s_test_large_exponents() ? 1 : 0;
    tests_passed += s_test_leading_zeros_invalid() ? 1 : 0;
    tests_passed += s_test_numeric_overflow() ? 1 : 0;
    tests_passed += s_test_numeric_underflow() ? 1 : 0;
    tests_passed += s_test_large_integer_precision() ? 1 : 0;
    tests_passed += s_test_zero_variants() ? 1 : 0;
    
    log_it(L_INFO, "Numeric edge cases tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

/**
 * @brief Main entry point
 */
int main(void) {
    dap_print_module_name("DAP JSON Numeric Edge Cases Tests");
    return dap_json_numeric_tests_run();
}

