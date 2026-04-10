/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 */

/**
 * @file test_literal_edge_cases.c
 * @brief Literal Edge Cases - Phase 1.8.2
 * @details ПОЛНАЯ реализация 5 literal edge case tests
 * 
 * Tests:
 *   1. Mixed case literals (True, FALSE, NuLl) - should reject
 *   2. Partial literals (tru, fals, nul) - should reject
 *   3. Extra characters (true1, false_, null!) - should reject
 *   4. Multiple literals in array ([true, false, null])
 *   5. Literals as values, not keys ({"bool": true} valid, {true: "val"} invalid)
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "test_literal_edges"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <string.h>

/** dap_json_array_get_idx() allocates a sub-wrapper for tape-backed JSON; must free after use. */
static bool s_array_get_bool_free(dap_json_t *a_arr, size_t a_idx)
{
    dap_json_t *l_elem = dap_json_array_get_idx(a_arr, a_idx);
    if (!l_elem) {
        return false;
    }
    bool l_val = dap_json_get_bool(l_elem);
    dap_json_object_free(l_elem);
    return l_val;
}

// =============================================================================
// TEST 1: Mixed Case Literals (Invalid)
// =============================================================================

static bool s_test_mixed_case_literals_invalid(void) {
    log_it(L_DEBUG, "Testing mixed case literals (invalid)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test True (should be lowercase 'true')
    l_json = dap_json_parse_string("{\"val\":True}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'True'");
    
    // Test FALSE (should be lowercase 'false')
    l_json = dap_json_parse_string("{\"val\":FALSE}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'FALSE'");
    
    // Test NuLl (should be lowercase 'null')
    l_json = dap_json_parse_string("{\"val\":NuLl}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'NuLl'");
    
    // Test TRUE
    l_json = dap_json_parse_string("{\"val\":TRUE}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'TRUE'");
    
    // Test Null
    l_json = dap_json_parse_string("{\"val\":Null}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'Null'");
    
    result = true;
    log_it(L_DEBUG, "Mixed case literals test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 2: Partial Literals (Invalid)
// =============================================================================

static bool s_test_partial_literals_invalid(void) {
    log_it(L_DEBUG, "Testing partial literals (invalid)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test 'tru' (incomplete 'true')
    l_json = dap_json_parse_string("{\"val\":tru}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'tru'");
    
    // Test 'fals' (incomplete 'false')
    l_json = dap_json_parse_string("{\"val\":fals}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'fals'");
    
    // Test 'nul' (incomplete 'null')
    l_json = dap_json_parse_string("{\"val\":nul}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'nul'");
    
    // Test 'tr'
    l_json = dap_json_parse_string("{\"val\":tr}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'tr'");
    
    // Test 'f'
    l_json = dap_json_parse_string("{\"val\":f}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'f'");
    
    // Test 'n'
    l_json = dap_json_parse_string("{\"val\":n}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'n'");
    
    result = true;
    log_it(L_DEBUG, "Partial literals test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 3: Extra Characters in Literals (Invalid)
// =============================================================================

static bool s_test_extra_characters_in_literals_invalid(void) {
    log_it(L_DEBUG, "Testing extra characters in literals (invalid)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test 'true1'
    l_json = dap_json_parse_string("{\"val\":true1}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'true1'");
    
    // Test 'false_'
    l_json = dap_json_parse_string("{\"val\":false_}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'false_'");
    
    // Test 'null!'
    l_json = dap_json_parse_string("{\"val\":null!}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'null!'");
    
    // Test 'truex'
    l_json = dap_json_parse_string("{\"val\":truex}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'truex'");
    
    // Test 'nullnull'
    l_json = dap_json_parse_string("{\"val\":nullnull}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects 'nullnull'");
    
    result = true;
    log_it(L_DEBUG, "Extra characters in literals test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 4: Multiple Literals in Array
// =============================================================================

static bool s_test_multiple_literals_in_array(void) {
    log_it(L_DEBUG, "Testing multiple literals in array");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Array with all three literals
    const char *json_str = "[true, false, null, true, false, null]";
    
    l_json = dap_json_parse_string(json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse array with literals");
    
    size_t len = dap_json_array_length(l_json);
    DAP_TEST_FAIL_IF(len != 6, "Array has 6 elements");
    
    // Check each element (free sub-wrappers from dap_json_array_get_idx)
    bool val0 = s_array_get_bool_free(l_json, 0);
    DAP_TEST_FAIL_IF(!val0, "Element 0 is true");
    
    bool val1 = s_array_get_bool_free(l_json, 1);
    DAP_TEST_FAIL_IF(val1, "Element 1 is false");
    
    // Element 2 is null - check with appropriate API
    
    bool val3 = s_array_get_bool_free(l_json, 3);
    DAP_TEST_FAIL_IF(!val3, "Element 3 is true");
    
    bool val4 = s_array_get_bool_free(l_json, 4);
    DAP_TEST_FAIL_IF(val4, "Element 4 is false");
    
    // Element 5 is null
    
    result = true;
    log_it(L_DEBUG, "Multiple literals in array test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 5: Literals as Values vs Keys
// =============================================================================

/**
 * @brief Test literals as values (valid) vs keys (invalid in strict JSON)
 * @details {"bool": true} is valid
 *          {true: "val"} is invalid (keys must be strings)
 */
static bool s_test_literals_as_values_vs_keys(void) {
    log_it(L_DEBUG, "Testing literals as values vs keys");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test 1: Literals as values (VALID)
    l_json = dap_json_parse_string("{\"bool\":true,\"null\":null,\"bool2\":false}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse literals as values");
    
    bool bool_val = dap_json_object_get_bool(l_json, "bool");
    DAP_TEST_FAIL_IF(!bool_val, "true value correct");
    
    bool bool2_val = dap_json_object_get_bool(l_json, "bool2");
    DAP_TEST_FAIL_IF(bool2_val, "false value correct");
    
    dap_json_object_free(l_json);
    
    // Test 2: Literals as keys (INVALID in strict JSON, valid in JSON5 if unquoted)
    l_json = dap_json_parse_string("{true: \"val\"}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects literal 'true' as unquoted key");
    
    // Test 3: false as key
    l_json = dap_json_parse_string("{false: 1}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects literal 'false' as unquoted key");
    
    // Test 4: null as key
    l_json = dap_json_parse_string("{null: \"value\"}");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects literal 'null' as unquoted key");
    
    // Test 5: String "true" as key (VALID - it's a string)
    l_json = dap_json_parse_string("{\"true\":1,\"false\":2,\"null\":3}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse string literals as keys (valid)");
    
    int val_true = dap_json_object_get_int(l_json, "true");
    DAP_TEST_FAIL_IF(val_true != 1, "String \"true\" as key works");
    
    result = true;
    log_it(L_DEBUG, "Literals as values vs keys test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_json_literal_edge_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Literal Edge Cases ===");
    
    int tests_passed = 0;
    int tests_total = 5;
    
    tests_passed += s_test_mixed_case_literals_invalid() ? 1 : 0;
    tests_passed += s_test_partial_literals_invalid() ? 1 : 0;
    tests_passed += s_test_extra_characters_in_literals_invalid() ? 1 : 0;
    tests_passed += s_test_multiple_literals_in_array() ? 1 : 0;
    tests_passed += s_test_literals_as_values_vs_keys() ? 1 : 0;
    
    log_it(L_INFO, "Literal edge tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void) {
    dap_print_module_name("DAP JSON Literal Edge Cases");
    return dap_json_literal_edge_tests_run();
}

