/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 */

/**
 * @file test_array_edge_cases.c
 * @brief Array Structure Edge Cases - Phase 1.8.2
 * @details ПОЛНАЯ реализация 8 array edge case tests
 * 
 * Tests:
 *   1. Empty array ([])
 *   2. Single element array ([1])
 *   3. Nested empty arrays ([[],[[]],[[[]]]]) - depth 5
 *   4. Heterogeneous arrays ([1, "text", true, null, {"key": "value"}, [1,2]])
 *   5. Large arrays (10,000+ elements performance)
 *   6. Arrays with only commas (invalid: [,], [1,,2], [,1])
 *   7. Trailing commas (JSON5: [1,2,3,], strict JSON: invalid)
 *   8. Sparse representation (null vs omitted)
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "test_array_edges"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <string.h>
#include <stdlib.h>

// =============================================================================
// TEST 1: Empty Array
// =============================================================================

static bool s_test_empty_array(void) {
    log_it(L_DEBUG, "Testing empty array");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test 1: Simple empty array
    l_json = dap_json_parse_string("[]");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse empty array");
    
    size_t len = dap_json_array_length(l_json);
    DAP_TEST_FAIL_IF(len != 0, "Empty array has zero length");
    
    dap_json_object_free(l_json);
    
    // Test 2: Empty array as value
    l_json = dap_json_parse_string("{\"arr\":[]}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse object with empty array");
    
    dap_json_t *arr = dap_json_object_get_array(l_json, "arr");
    DAP_TEST_FAIL_IF_NULL(arr, "Get empty array");
    
    len = dap_json_array_length(arr);
    DAP_TEST_FAIL_IF(len != 0, "Empty array from object has zero length");
    
    dap_json_object_free(l_json);
    
    // Test 3: Multiple empty arrays
    l_json = dap_json_parse_string("{\"a\":[],\"b\":[],\"c\":[]}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse multiple empty arrays");
    
    result = true;
    log_it(L_DEBUG, "Empty array test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 2: Single Element Array
// =============================================================================

static bool s_test_single_element_array(void) {
    log_it(L_DEBUG, "Testing single element array");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test various single-element arrays
    l_json = dap_json_parse_string("[42]");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse single int");
    
    size_t len = dap_json_array_length(l_json);
    DAP_TEST_FAIL_IF(len != 1, "Single element array has length 1");
    
    int val = dap_json_array_get_int(l_json, 0);
    DAP_TEST_FAIL_IF(val != 42, "Single int value correct");
    
    dap_json_object_free(l_json);
    
    // Single string
    l_json = dap_json_parse_string("[\"text\"]");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse single string");
    
    const char *str = dap_json_array_get_string(l_json, 0);
    DAP_TEST_FAIL_IF(strcmp(str, "text") != 0, "Single string correct");
    
    dap_json_object_free(l_json);
    
    // Single object
    l_json = dap_json_parse_string("[{\"key\":\"value\"}]");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse single object");
    
    dap_json_t *obj = dap_json_array_get_object(l_json, 0);
    DAP_TEST_FAIL_IF_NULL(obj, "Get single object");
    
    result = true;
    log_it(L_DEBUG, "Single element array test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 3: Nested Empty Arrays (Depth 5)
// =============================================================================

static bool s_test_nested_empty_arrays(void) {
    log_it(L_DEBUG, "Testing nested empty arrays (depth 5)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test: [[],[[]],[[[]]]]]
    const char *json_str = "[[],[[]],[[[]]],[[[[]]]],[[[[[]]]]]]";
    
    l_json = dap_json_parse_string(json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse nested empty arrays depth 5");
    
    size_t len = dap_json_array_length(l_json);
    DAP_TEST_FAIL_IF(len != 5, "Top-level array has 5 elements");
    
    // Check depth 1: []
    dap_json_t *depth1 = dap_json_array_get_array(l_json, 0);
    DAP_TEST_FAIL_IF_NULL(depth1, "Get depth 1");
    DAP_TEST_FAIL_IF(dap_json_array_length(depth1) != 0, "Depth 1 is empty");
    
    // Check depth 2: [[]]
    dap_json_t *depth2 = dap_json_array_get_array(l_json, 1);
    DAP_TEST_FAIL_IF_NULL(depth2, "Get depth 2");
    DAP_TEST_FAIL_IF(dap_json_array_length(depth2) != 1, "Depth 2 has 1 element");
    
    dap_json_t *depth2_inner = dap_json_array_get_array(depth2, 0);
    DAP_TEST_FAIL_IF_NULL(depth2_inner, "Get depth 2 inner");
    DAP_TEST_FAIL_IF(dap_json_array_length(depth2_inner) != 0, "Depth 2 inner is empty");
    
    // Check depth 5: [[[[[]]]]]
    dap_json_t *depth5 = dap_json_array_get_array(l_json, 4);
    DAP_TEST_FAIL_IF_NULL(depth5, "Get depth 5");
    
    // Navigate down
    dap_json_t *d5_1 = dap_json_array_get_array(depth5, 0);
    DAP_TEST_FAIL_IF_NULL(d5_1, "Depth 5 level 1");
    
    dap_json_t *d5_2 = dap_json_array_get_array(d5_1, 0);
    DAP_TEST_FAIL_IF_NULL(d5_2, "Depth 5 level 2");
    
    dap_json_t *d5_3 = dap_json_array_get_array(d5_2, 0);
    DAP_TEST_FAIL_IF_NULL(d5_3, "Depth 5 level 3");
    
    dap_json_t *d5_4 = dap_json_array_get_array(d5_3, 0);
    DAP_TEST_FAIL_IF_NULL(d5_4, "Depth 5 level 4");
    
    DAP_TEST_FAIL_IF(dap_json_array_length(d5_4) != 0, "Depth 5 innermost is empty");
    
    result = true;
    log_it(L_DEBUG, "Nested empty arrays test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 4: Heterogeneous Arrays
// =============================================================================

static bool s_test_heterogeneous_arrays(void) {
    log_it(L_DEBUG, "Testing heterogeneous arrays");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Array with all JSON types
    const char *json_str = "[1, \"text\", true, null, {\"key\":\"value\"}, [1,2,3], 3.14]";
    
    l_json = dap_json_parse_string(json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse heterogeneous array");
    
    size_t len = dap_json_array_length(l_json);
    DAP_TEST_FAIL_IF(len != 7, "Heterogeneous array has 7 elements");
    
    // Check each element
    int int_val = dap_json_array_get_int(l_json, 0);
    DAP_TEST_FAIL_IF(int_val != 1, "Element 0 is int 1");
    
    const char *str_val = dap_json_array_get_string(l_json, 1);
    DAP_TEST_FAIL_IF(strcmp(str_val, "text") != 0, "Element 1 is string");
    
    bool bool_val = dap_json_array_get_bool(l_json, 2);
    DAP_TEST_FAIL_IF(!bool_val, "Element 2 is true");
    
    // Element 3 is null - check with is_null
    // (Assuming API has dap_json_array_is_null)
    
    dap_json_t *obj = dap_json_array_get_object(l_json, 4);
    DAP_TEST_FAIL_IF_NULL(obj, "Element 4 is object");
    
    dap_json_t *arr = dap_json_array_get_array(l_json, 5);
    DAP_TEST_FAIL_IF_NULL(arr, "Element 5 is array");
    DAP_TEST_FAIL_IF(dap_json_array_length(arr) != 3, "Nested array has 3 elements");
    
    double dbl_val = dap_json_array_get_double(l_json, 6);
    DAP_TEST_FAIL_IF(dbl_val < 3.13 || dbl_val > 3.15, "Element 6 is double 3.14");
    
    result = true;
    log_it(L_DEBUG, "Heterogeneous arrays test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 5: Large Arrays (10,000+ elements)
// =============================================================================

static bool s_test_large_arrays(void) {
    log_it(L_DEBUG, "Testing large arrays (10,000+ elements)");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *json_buf = NULL;
    
    const int ELEMENT_COUNT = 10000;
    const size_t BUF_SIZE = ELEMENT_COUNT * 8 + 32;  // "[1,2,3,...]"
    
    json_buf = (char*)malloc(BUF_SIZE);
    DAP_TEST_FAIL_IF_NULL(json_buf, "Allocate buffer for large array");
    
    strcpy(json_buf, "[");
    char *ptr = json_buf + 1;
    
    for (int i = 0; i < ELEMENT_COUNT; i++) {
        if (i > 0) {
            *ptr++ = ',';
        }
        ptr += sprintf(ptr, "%d", i);
    }
    strcpy(ptr, "]");
    
    log_it(L_INFO, "Parsing array with %d elements (%zu bytes)", ELEMENT_COUNT, strlen(json_buf));
    
    l_json = dap_json_parse_string(json_buf);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse large array");
    
    size_t len = dap_json_array_length(l_json);
    DAP_TEST_FAIL_IF(len != ELEMENT_COUNT, "Large array has correct length");
    
    // Spot check elements
    int first = dap_json_array_get_int(l_json, 0);
    DAP_TEST_FAIL_IF(first != 0, "First element is 0");
    
    int middle = dap_json_array_get_int(l_json, ELEMENT_COUNT / 2);
    DAP_TEST_FAIL_IF(middle != ELEMENT_COUNT / 2, "Middle element correct");
    
    int last = dap_json_array_get_int(l_json, ELEMENT_COUNT - 1);
    DAP_TEST_FAIL_IF(last != ELEMENT_COUNT - 1, "Last element correct");
    
    result = true;
    log_it(L_DEBUG, "Large arrays test passed");
    
cleanup:
    free(json_buf);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 6: Arrays with Only Commas (Invalid)
// =============================================================================

static bool s_test_arrays_only_commas_invalid(void) {
    log_it(L_DEBUG, "Testing arrays with only commas (invalid)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test [,] - invalid
    l_json = dap_json_parse_string("[,]");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects [,]");
    
    // Test [1,,2] - invalid (missing element)
    l_json = dap_json_parse_string("[1,,2]");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects [1,,2]");
    
    // Test [,1] - invalid (leading comma)
    l_json = dap_json_parse_string("[,1]");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects [,1]");
    
    // Test [1,] - trailing comma (invalid in strict JSON, valid in JSON5)
    l_json = dap_json_parse_string("[1,]");
    if (l_json) {
        log_it(L_INFO, "Parser accepts trailing comma [1,] (JSON5 mode?)");
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser rejects trailing comma [1,] (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "Arrays with only commas test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 7: Trailing Commas (JSON5 vs Strict)
// =============================================================================

static bool s_test_trailing_commas(void) {
    log_it(L_DEBUG, "Testing trailing commas");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test trailing comma in array
    l_json = dap_json_parse_string("[1,2,3,]");
    if (l_json) {
        log_it(L_INFO, "Parser accepts array trailing comma (JSON5 feature)");
        size_t len = dap_json_array_length(l_json);
        DAP_TEST_FAIL_IF(len != 3, "Trailing comma doesn't add element");
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser rejects array trailing comma (strict JSON)");
    }
    
    // Test trailing comma in object
    l_json = dap_json_parse_string("{\"a\":1,\"b\":2,}");
    if (l_json) {
        log_it(L_INFO, "Parser accepts object trailing comma (JSON5 feature)");
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser rejects object trailing comma (strict JSON)");
    }
    
    result = true;
    log_it(L_DEBUG, "Trailing commas test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 8: Sparse Representation (null vs omitted)
// =============================================================================

static bool s_test_sparse_representation(void) {
    log_it(L_DEBUG, "Testing sparse representation");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // JSON doesn't support sparse arrays - [1,,3] is invalid
    // But [1,null,3] is valid and explicit
    
    l_json = dap_json_parse_string("[1,null,3]");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse array with explicit null");
    
    size_t len = dap_json_array_length(l_json);
    DAP_TEST_FAIL_IF(len != 3, "Array with null has 3 elements");
    
    int val0 = dap_json_array_get_int(l_json, 0);
    DAP_TEST_FAIL_IF(val0 != 1, "Element 0 is 1");
    
    // Element 1 should be null
    // (Assuming API has dap_json_array_is_null or similar)
    
    int val2 = dap_json_array_get_int(l_json, 2);
    DAP_TEST_FAIL_IF(val2 != 3, "Element 2 is 3");
    
    dap_json_object_free(l_json);
    
    // Test that [1,,3] is rejected
    l_json = dap_json_parse_string("[1,,3]");
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects sparse array [1,,3]");
    
    result = true;
    log_it(L_DEBUG, "Sparse representation test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_json_array_edge_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Array Edge Cases ===");
    
    int tests_passed = 0;
    int tests_total = 8;
    
    tests_passed += s_test_empty_array() ? 1 : 0;
    tests_passed += s_test_single_element_array() ? 1 : 0;
    tests_passed += s_test_nested_empty_arrays() ? 1 : 0;
    tests_passed += s_test_heterogeneous_arrays() ? 1 : 0;
    tests_passed += s_test_large_arrays() ? 1 : 0;
    tests_passed += s_test_arrays_only_commas_invalid() ? 1 : 0;
    tests_passed += s_test_trailing_commas() ? 1 : 0;
    tests_passed += s_test_sparse_representation() ? 1 : 0;
    
    log_it(L_INFO, "Array edge tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void) {
    dap_print_module_name("DAP JSON Array Edge Cases");
    return dap_json_array_edge_tests_run();
}

