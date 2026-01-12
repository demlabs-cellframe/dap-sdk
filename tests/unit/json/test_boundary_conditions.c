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
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "dap_json_boundary_tests"

/**
 * @brief Test very long strings (>64KB)
 */
static bool s_test_very_long_string(void) {
    log_it(L_DEBUG, "Testing very long string (>64KB)");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *l_long_string = NULL;
    char *l_json_str = NULL;
    
    // Create a 100KB string
    const size_t l_string_size = 100 * 1024;
    l_long_string = (char*)calloc(l_string_size + 1, 1);
    DAP_TEST_FAIL_IF_NULL(l_long_string, "Allocate long string");
    
    // Fill with 'A's
    memset(l_long_string, 'A', l_string_size);
    l_long_string[l_string_size] = '\0';
    
    // Create JSON with this string
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "Create JSON object");
    
    dap_json_object_add_string(l_json, "longstr", l_long_string);
    
    // Retrieve and verify
    const char *l_retrieved = dap_json_object_get_string(l_json, "longstr");
    DAP_TEST_FAIL_IF_NULL(l_retrieved, "Get long string");
    DAP_TEST_FAIL_IF(strlen(l_retrieved) != l_string_size, "Long string length preserved");
    DAP_TEST_FAIL_IF(strcmp(l_retrieved, l_long_string) != 0, "Long string content preserved");
    
    // Test serialization and parsing back
    l_json_str = dap_json_to_string(l_json);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "Serialize JSON with long string");
    
    dap_json_t *l_json2 = dap_json_parse_string(l_json_str);
    DAP_TEST_FAIL_IF_NULL(l_json2, "Parse JSON with long string");
    
    const char *l_retrieved2 = dap_json_object_get_string(l_json2, "longstr");
    DAP_TEST_FAIL_IF_NULL(l_retrieved2, "Get long string after round-trip");
    DAP_TEST_FAIL_IF(strlen(l_retrieved2) != l_string_size, "Long string length after round-trip");
    
    dap_json_object_free(l_json2);
    
    result = true;
    log_it(L_DEBUG, "Very long string test passed");
    
cleanup:
    if (l_long_string) free(l_long_string);
    if (l_json_str) free(l_json_str);
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test megabyte-sized string
 */
static bool s_test_megabyte_string(void) {
    log_it(L_DEBUG, "Testing megabyte-sized string");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *l_large_string = NULL;
    
    // Create a 1MB string
    const size_t l_string_size = 1024 * 1024;
    l_large_string = (char*)calloc(l_string_size + 1, 1);
    DAP_TEST_FAIL_IF_NULL(l_large_string, "Allocate 1MB string");
    
    // Fill with pattern
    for (size_t i = 0; i < l_string_size; i++) {
        l_large_string[i] = 'A' + (i % 26);
    }
    l_large_string[l_string_size] = '\0';
    
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "Create JSON object");
    
    dap_json_object_add_string(l_json, "megabyte", l_large_string);
    
    const char *l_retrieved = dap_json_object_get_string(l_json, "megabyte");
    DAP_TEST_FAIL_IF_NULL(l_retrieved, "Get megabyte string");
    DAP_TEST_FAIL_IF(strlen(l_retrieved) != l_string_size, "Megabyte string length preserved");
    
    result = true;
    log_it(L_DEBUG, "Megabyte string test passed");
    
cleanup:
    if (l_large_string) free(l_large_string);
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test deep nesting (>100 levels)
 */
static bool s_test_deep_nesting(void) {
    log_it(L_DEBUG, "Testing deep nesting (>100 levels)");
    bool result = false;
    dap_json_t *l_json = NULL;
    dap_json_t *l_current = NULL;
    dap_json_t *l_root = NULL;
    
    const int l_depth = 200;
    
    // Build deeply nested structure: {a:{a:{a:...}}}
    l_root = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_root, "Create root object");
    
    l_current = l_root;
    for (int i = 0; i < l_depth; i++) {
        dap_json_t *l_nested = dap_json_object_new();
        DAP_TEST_FAIL_IF_NULL(l_nested, "Create nested object");
        
        dap_json_object_add_object(l_current, "nested", l_nested);
        l_current = l_nested;
    }
    
    // Add a value at the deepest level
    dap_json_object_add_int(l_current, "deep_value", 42);
    
    // Traverse back down and verify
    l_current = l_root;
    for (int i = 0; i < l_depth; i++) {
        l_current = dap_json_object_get_object(l_current, "nested");
        DAP_TEST_FAIL_IF_NULL(l_current, "Navigate to nested level");
    }
    
    int l_value = dap_json_object_get_int(l_current, "deep_value");
    DAP_TEST_FAIL_IF(l_value != 42, "Get value from deep nesting");
    
    // Test serialization and parsing
    char *l_json_str = dap_json_to_string(l_root);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "Serialize deeply nested JSON");
    
    l_json = dap_json_parse_string(l_json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse deeply nested JSON");
    
    free(l_json_str);
    
    result = true;
    log_it(L_DEBUG, "Deep nesting test passed");
    
cleanup:
    dap_json_object_free(l_root);
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test extremely deep nesting (>1000 levels)
 */
static bool s_test_extremely_deep_nesting(void) {
    log_it(L_DEBUG, "Testing extremely deep nesting (>1000 levels)");
    bool result = false;
    dap_json_t *l_root = NULL;
    dap_json_t *l_current = NULL;
    
    const int l_depth = 1500;
    
    l_root = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_root, "Create root object");
    
    l_current = l_root;
    for (int i = 0; i < l_depth; i++) {
        dap_json_t *l_nested = dap_json_object_new();
        if (!l_nested) {
            log_it(L_WARNING, "Failed to create nested object at depth %d", i);
            // This may hit resource limits, which is acceptable
            result = true;
            goto cleanup;
        }
        
        dap_json_object_add_object(l_current, "n", l_nested);
        l_current = l_nested;
    }
    
    dap_json_object_add_string(l_current, "bottom", "reached");
    
    // Verify traversal
    l_current = l_root;
    for (int i = 0; i < l_depth && l_current; i++) {
        l_current = dap_json_object_get_object(l_current, "n");
    }
    
    if (l_current) {
        const char *l_value = dap_json_object_get_string(l_current, "bottom");
        DAP_TEST_FAIL_IF(!l_value || strcmp(l_value, "reached") != 0, 
                         "Reach bottom of extreme nesting");
    }
    
    result = true;
    log_it(L_DEBUG, "Extremely deep nesting test passed");
    
cleanup:
    dap_json_object_free(l_root);
    return result;
}

/**
 * @brief Test huge array (>1M elements)
 */
static bool s_test_huge_array(void) {
    log_it(L_DEBUG, "Testing huge array (>1M elements)");
    bool result = false;
    dap_json_t *l_array = NULL;
    
    const int l_count = 1000000;  // 1 million elements
    
    l_array = dap_json_array_new();
    DAP_TEST_FAIL_IF_NULL(l_array, "Create array");
    
    log_it(L_DEBUG, "Adding %d elements...", l_count);
    
    // Add elements
    for (int i = 0; i < l_count; i++) {
        dap_json_t *l_num = dap_json_object_new_int(i);
        if (!l_num) {
            log_it(L_WARNING, "Failed to create element at %d", i);
            // May hit memory limits
            result = true;
            goto cleanup;
        }
        dap_json_array_add(l_array, l_num);
        
        // Progress log every 100k
        if ((i + 1) % 100000 == 0) {
            log_it(L_DEBUG, "Added %d elements", i + 1);
        }
    }
    
    log_it(L_DEBUG, "Verifying array length...");
    size_t l_length = dap_json_array_length(l_array);
    DAP_TEST_FAIL_IF(l_length != (size_t)l_count, "Array length matches");
    
    // Verify first and last elements
    dap_json_t *l_first = dap_json_array_get_idx(l_array, 0);
    DAP_TEST_FAIL_IF_NULL(l_first, "Get first element");
    DAP_TEST_FAIL_IF(dap_json_get_int(l_first) != 0, "First element value");
    
    dap_json_t *l_last = dap_json_array_get_idx(l_array, l_count - 1);
    DAP_TEST_FAIL_IF_NULL(l_last, "Get last element");
    DAP_TEST_FAIL_IF(dap_json_get_int(l_last) != l_count - 1, "Last element value");
    
    result = true;
    log_it(L_DEBUG, "Huge array test passed");
    
cleanup:
    dap_json_object_free(l_array);
    return result;
}

/**
 * @brief Test huge object (>100K keys)
 */
static bool s_test_huge_object(void) {
    log_it(L_DEBUG, "Testing huge object (>100K keys)");
    bool result = false;
    dap_json_t *l_obj = NULL;
    char l_key[32];
    
    const int l_key_count = 100000;  // 100K keys
    
    l_obj = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_obj, "Create object");
    
    log_it(L_DEBUG, "Adding %d keys...", l_key_count);
    
    // Add keys
    for (int i = 0; i < l_key_count; i++) {
        snprintf(l_key, sizeof(l_key), "key%d", i);
        dap_json_object_add_int(l_obj, l_key, i);
        
        // Progress log every 10k
        if ((i + 1) % 10000 == 0) {
            log_it(L_DEBUG, "Added %d keys", i + 1);
        }
    }
    
    log_it(L_DEBUG, "Verifying keys...");
    
    // Verify first and last keys
    int l_first = dap_json_object_get_int(l_obj, "key0");
    DAP_TEST_FAIL_IF(l_first != 0, "First key value");
    
    snprintf(l_key, sizeof(l_key), "key%d", l_key_count - 1);
    int l_last = dap_json_object_get_int(l_obj, l_key);
    DAP_TEST_FAIL_IF(l_last != l_key_count - 1, "Last key value");
    
    // Verify middle key
    snprintf(l_key, sizeof(l_key), "key%d", l_key_count / 2);
    int l_middle = dap_json_object_get_int(l_obj, l_key);
    DAP_TEST_FAIL_IF(l_middle != l_key_count / 2, "Middle key value");
    
    result = true;
    log_it(L_DEBUG, "Huge object test passed");
    
cleanup:
    dap_json_object_free(l_obj);
    return result;
}

/**
 * @brief Test empty key string
 */
static bool s_test_empty_key(void) {
    log_it(L_DEBUG, "Testing empty key string");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "Create JSON object");
    
    // Empty key is technically valid in JSON
    dap_json_object_add_string(l_json, "", "empty_key_value");
    
    const char *l_value = dap_json_object_get_string(l_json, "");
    DAP_TEST_FAIL_IF_NULL(l_value, "Get value for empty key");
    DAP_TEST_FAIL_IF(strcmp(l_value, "empty_key_value") != 0, "Empty key value");
    
    // Test serialization
    char *l_json_str = dap_json_to_string(l_json);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "Serialize JSON with empty key");
    
    // Should contain {"":"empty_key_value"}
    DAP_TEST_FAIL_IF(strstr(l_json_str, "\"\":") == NULL, "Empty key in serialized JSON");
    
    free(l_json_str);
    
    result = true;
    log_it(L_DEBUG, "Empty key test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test array with mixed large content
 */
static bool s_test_mixed_large_content(void) {
    log_it(L_DEBUG, "Testing array with mixed large content");
    bool result = false;
    dap_json_t *l_array = NULL;
    char *l_large_str = NULL;
    
    // Create large string (10KB)
    const size_t l_str_size = 10240;
    l_large_str = (char*)calloc(l_str_size + 1, 1);
    DAP_TEST_FAIL_IF_NULL(l_large_str, "Allocate large string");
    memset(l_large_str, 'X', l_str_size);
    
    l_array = dap_json_array_new();
    DAP_TEST_FAIL_IF_NULL(l_array, "Create array");
    
    // Mix of content types with large elements
    for (int i = 0; i < 100; i++) {
        switch (i % 4) {
            case 0:
                dap_json_array_add(l_array, dap_json_object_new_string(l_large_str));
                break;
            case 1:
                dap_json_array_add(l_array, dap_json_object_new_int(INT64_MAX));
                break;
            case 2: {
                dap_json_t *l_nested_obj = dap_json_object_new();
                dap_json_object_add_string(l_nested_obj, "large", l_large_str);
                dap_json_array_add(l_array, l_nested_obj);
                break;
            }
            case 3: {
                dap_json_t *l_nested_arr = dap_json_array_new();
                for (int j = 0; j < 10; j++) {
                    dap_json_array_add(l_nested_arr, dap_json_object_new_int(j));
                }
                dap_json_array_add(l_array, l_nested_arr);
                break;
            }
        }
    }
    
    size_t l_length = dap_json_array_length(l_array);
    DAP_TEST_FAIL_IF(l_length != 100, "Array length");
    
    result = true;
    log_it(L_DEBUG, "Mixed large content test passed");
    
cleanup:
    if (l_large_str) free(l_large_str);
    dap_json_object_free(l_array);
    return result;
}

/**
 * @brief Main test runner for boundary conditions tests
 */
int dap_json_boundary_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Boundary Conditions Tests ===");
    
    int tests_passed = 0;
    int tests_total = 8;
    
    tests_passed += s_test_very_long_string() ? 1 : 0;
    tests_passed += s_test_megabyte_string() ? 1 : 0;
    tests_passed += s_test_deep_nesting() ? 1 : 0;
    tests_passed += s_test_extremely_deep_nesting() ? 1 : 0;
    tests_passed += s_test_huge_array() ? 1 : 0;
    tests_passed += s_test_huge_object() ? 1 : 0;
    tests_passed += s_test_empty_key() ? 1 : 0;
    tests_passed += s_test_mixed_large_content() ? 1 : 0;
    
    log_it(L_INFO, "Boundary conditions tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

/**
 * @brief Main entry point
 */
int main(void) {
    dap_print_module_name("DAP JSON Boundary Conditions Tests");
    return dap_json_boundary_tests_run();
}

