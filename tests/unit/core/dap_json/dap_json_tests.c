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
#include "../../../fixtures/json_samples.h"
#include "../../../fixtures/utilities/test_helpers.h"

#define LOG_TAG "dap_json_unit_tests"

/**
 * @brief Test JSON object creation and destruction
 */
static bool s_test_json_object_creation(void) {
    log_it(L_DEBUG, "Testing JSON object creation");
    
    dap_json_t *l_json = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_json, "JSON object creation");
    
    dap_json_object_free(l_json);
    log_it(L_DEBUG, "JSON object creation test passed");
    return true;
}

/**
 * @brief Test JSON array creation and destruction
 */
static bool s_test_json_array_creation(void) {
    log_it(L_DEBUG, "Testing JSON array creation");
    
    dap_json_t *l_array = dap_json_array_new();
    DAP_TEST_ASSERT_NOT_NULL(l_array, "JSON array creation");
    
    dap_json_object_free(l_array);
    log_it(L_DEBUG, "JSON array creation test passed");
    return true;
}

/**
 * @brief Test JSON string operations
 */
static bool s_test_json_string_operations(void) {
    log_it(L_DEBUG, "Testing JSON string operations");
    
    dap_json_t *l_root = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_root, "JSON root object");
    
    // Add string value
    dap_json_object_add_string(l_root, "test_key", "test_value");
    
    // Get string value
    const char* l_value = dap_json_object_get_string(l_root, "test_key");
    DAP_TEST_ASSERT_NOT_NULL(l_value, "Retrieved string value");
    DAP_TEST_ASSERT_STRING_EQUAL("test_value", l_value, "String value comparison");
    
    dap_json_object_free(l_root);
    log_it(L_DEBUG, "JSON string operations test passed");
    return true;
}

/**
 * @brief Test JSON parsing of sample data
 */
static bool s_test_json_parsing(void) {
    log_it(L_DEBUG, "Testing JSON parsing");
    
    // Parse simple JSON sample
    dap_json_t *l_parsed = dap_json_parse_string(JSON_SAMPLE_SIMPLE);
    DAP_TEST_ASSERT_NOT_NULL(l_parsed, "JSON parsing of simple sample");
    
    // Verify parsed content
    const char* l_name = dap_json_object_get_string(l_parsed, "name");
    DAP_TEST_ASSERT_STRING_EQUAL("test", l_name, "Parsed name field");
    
    int64_t l_value = dap_json_object_get_int64(l_parsed, "value");
    DAP_TEST_ASSERT_EQUAL(123, l_value, "Parsed value field");
    
    dap_json_object_free(l_parsed);
    log_it(L_DEBUG, "JSON parsing test passed");
    return true;
}

/**
 * @brief Test JSON serialization
 */
static bool s_test_json_serialization(void) {
    log_it(L_DEBUG, "Testing JSON serialization");
    
    dap_json_t *l_root = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_root, "JSON root for serialization");
    
    dap_json_object_add_string(l_root, "name", "test");
    dap_json_object_add_int64(l_root, "value", 123);
    
    char* l_json_str = dap_json_to_string(l_root);
    DAP_TEST_ASSERT_NOT_NULL(l_json_str, "JSON serialization");
    
    // Check that output contains expected elements
    DAP_TEST_ASSERT(strstr(l_json_str, "name") != NULL, "Serialized JSON contains name");
    DAP_TEST_ASSERT(strstr(l_json_str, "test") != NULL, "Serialized JSON contains test");
    DAP_TEST_ASSERT(strstr(l_json_str, "value") != NULL, "Serialized JSON contains value");
    
    log_it(L_DEBUG, "Serialized JSON: %s", l_json_str);
    
    DAP_DELETE(l_json_str);
    dap_json_object_free(l_root);
    log_it(L_DEBUG, "JSON serialization test passed");
    return true;
}

/**
 * @brief Test wrapper invalidation after add_object (Phase 3.1 verification)
 * This test verifies that after adding a child object to parent, the child wrapper
 * is invalidated (pvt = NULL) to prevent double-free
 */
static bool s_test_wrapper_invalidation_add_object(void) {
    log_it(L_DEBUG, "Testing wrapper invalidation after add_object");
    
    dap_json_t *l_parent = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_parent, "Parent object creation");
    
    dap_json_t *l_child = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_child, "Child object creation");
    
    // Add some data to child
    dap_json_object_add_string(l_child, "name", "child");
    
    // Add child to parent - this should invalidate l_child wrapper
    int ret = dap_json_object_add_object(l_parent, "child_key", l_child);
    DAP_TEST_ASSERT_EQUAL(0, ret, "Adding child to parent");
    
    // After add, l_child wrapper should be invalidated (pvt = NULL)
    // Calling dap_json_object_free on invalidated wrapper should be safe
    dap_json_object_free(l_child);  // Should only free wrapper, not underlying object
    
    // Parent should still contain valid child data via nested get
    dap_json_t *l_retrieved_child = dap_json_object_get_object(l_parent, "child_key");
    DAP_TEST_ASSERT_NOT_NULL(l_retrieved_child, "Child accessible via parent");
    
    const char *l_child_name = dap_json_object_get_string(l_retrieved_child, "name");
    DAP_TEST_ASSERT_STRING_EQUAL("child", l_child_name, "Child data correct");
    
    // Free retrieved child wrapper (decrements refcount)
    dap_json_object_free(l_retrieved_child);
    
    // Free parent (which frees the underlying child object)
    dap_json_object_free(l_parent);
    
    log_it(L_DEBUG, "Wrapper invalidation after add_object test passed");
    return true;
}

/**
 * @brief Test wrapper invalidation after add_array (Phase 3.1 verification)
 */
static bool s_test_wrapper_invalidation_add_array(void) {
    log_it(L_DEBUG, "Testing wrapper invalidation after add_array");
    
    dap_json_t *l_parent = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_parent, "Parent object creation");
    
    dap_json_t *l_array = dap_json_array_new();
    DAP_TEST_ASSERT_NOT_NULL(l_array, "Array creation");
    
    // Add some strings to array via string objects
    dap_json_t *l_item1 = dap_json_object_new_string("item1");
    dap_json_t *l_item2 = dap_json_object_new_string("item2");
    dap_json_array_add(l_array, l_item1);
    dap_json_array_add(l_array, l_item2);
    
    // Add array to parent - this should invalidate l_array wrapper
    int ret = dap_json_object_add_array(l_parent, "array_key", l_array);
    DAP_TEST_ASSERT_EQUAL(0, ret, "Adding array to parent");
    
    // After add, l_array wrapper should be invalidated
    dap_json_object_free(l_array);  // Should only free wrapper
    
    // Parent should still contain valid array data
    dap_json_t *l_retrieved_array = dap_json_object_get_array(l_parent, "array_key");
    DAP_TEST_ASSERT_NOT_NULL(l_retrieved_array, "Array accessible via parent");
    
    size_t array_len = dap_json_array_length(l_retrieved_array);
    DAP_TEST_ASSERT_EQUAL(2, array_len, "Array length correct");
    
    // Free retrieved array wrapper
    dap_json_object_free(l_retrieved_array);
    
    // Free parent
    dap_json_object_free(l_parent);
    
    log_it(L_DEBUG, "Wrapper invalidation after add_array test passed");
    return true;
}

/**
 * @brief Test wrapper invalidation after array_add (Phase 3.1 verification)
 */
static bool s_test_wrapper_invalidation_array_add(void) {
    log_it(L_DEBUG, "Testing wrapper invalidation after array_add");
    
    dap_json_t *l_array = dap_json_array_new();
    DAP_TEST_ASSERT_NOT_NULL(l_array, "Array creation");
    
    dap_json_t *l_item = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_item, "Item object creation");
    
    dap_json_object_add_string(l_item, "name", "item");
    
    // Add item to array - this should invalidate l_item wrapper
    int ret = dap_json_array_add(l_array, l_item);
    DAP_TEST_ASSERT_EQUAL(0, ret, "Adding item to array");
    
    // After add, l_item wrapper should be invalidated
    dap_json_object_free(l_item);  // Should only free wrapper
    
    // Array should still contain valid item
    size_t array_len = dap_json_array_length(l_array);
    DAP_TEST_ASSERT_EQUAL(1, array_len, "Array contains item");
    
    // Free array
    dap_json_object_free(l_array);
    
    log_it(L_DEBUG, "Wrapper invalidation after array_add test passed");
    return true;
}

/**
 * @brief Test reference counting for get_object (Phase 3.1 verification)
 * After get_object, user MUST call dap_json_object_free on returned wrapper
 */
static bool s_test_refcount_get_object(void) {
    log_it(L_DEBUG, "Testing reference counting for get_object");
    
    dap_json_t *l_parent = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_parent, "Parent object creation");
    
    // Create child object with data
    dap_json_t *l_child_obj = dap_json_object_new();
    dap_json_object_add_string(l_child_obj, "name", "test_child");
    
    // Add child to parent
    dap_json_object_add_object(l_parent, "child", l_child_obj);
    
    // Get child object - this increments refcount, user must free
    dap_json_t *l_retrieved = dap_json_object_get_object(l_parent, "child");
    DAP_TEST_ASSERT_NOT_NULL(l_retrieved, "Retrieved child object");
    
    // Verify child data
    const char *l_name = dap_json_object_get_string(l_retrieved, "name");
    DAP_TEST_ASSERT_STRING_EQUAL("test_child", l_name, "Child data correct");
    
    // Must free retrieved wrapper (decrements refcount)
    dap_json_object_free(l_retrieved);
    
    // Parent should still be valid, verify via another get
    dap_json_t *l_retrieved2 = dap_json_object_get_object(l_parent, "child");
    DAP_TEST_ASSERT_NOT_NULL(l_retrieved2, "Parent still valid");
    const char *l_name2 = dap_json_object_get_string(l_retrieved2, "name");
    DAP_TEST_ASSERT_STRING_EQUAL("test_child", l_name2, "Child data still correct");
    dap_json_object_free(l_retrieved2);
    
    // Free parent
    dap_json_object_free(l_parent);
    
    log_it(L_DEBUG, "Reference counting for get_object test passed");
    return true;
}

/**
 * @brief Test reference counting for array_get_idx (Phase 3.1 verification)
 */
static bool s_test_refcount_array_get_idx(void) {
    log_it(L_DEBUG, "Testing reference counting for array_get_idx");
    
    dap_json_t *l_array = dap_json_array_new();
    DAP_TEST_ASSERT_NOT_NULL(l_array, "Array creation");
    
    // Add string items via objects
    dap_json_t *l_s1 = dap_json_object_new_string("item1");
    dap_json_t *l_s2 = dap_json_object_new_string("item2");
    dap_json_t *l_s3 = dap_json_object_new_string("item3");
    dap_json_array_add(l_array, l_s1);
    dap_json_array_add(l_array, l_s2);
    dap_json_array_add(l_array, l_s3);
    
    // Get array length
    size_t len = dap_json_array_length(l_array);
    DAP_TEST_ASSERT_EQUAL(3, len, "Array length");
    
    // Get items - each get increments refcount, must free
    dap_json_t *l_item1 = dap_json_array_get_idx(l_array, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_item1, "First item retrieved");
    DAP_TEST_ASSERT(dap_json_is_string(l_item1), "First item is string");
    dap_json_object_free(l_item1);  // Must free wrapper
    
    dap_json_t *l_item2 = dap_json_array_get_idx(l_array, 1);
    DAP_TEST_ASSERT_NOT_NULL(l_item2, "Second item retrieved");
    DAP_TEST_ASSERT(dap_json_is_string(l_item2), "Second item is string");
    dap_json_object_free(l_item2);  // Must free wrapper
    
    // Free array (which frees all items)
    dap_json_object_free(l_array);
    
    log_it(L_DEBUG, "Reference counting for array_get_idx test passed");
    return true;
}

/**
 * @brief Test numeric types operations (Phase 3.4)
 */
static bool s_test_numeric_types(void) {
    log_it(L_DEBUG, "Testing numeric types");
    
    dap_json_t *l_obj = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_obj, "Object creation");
    
    // Test int operations
    dap_json_object_add_int(l_obj, "int_val", 42);
    int l_int = dap_json_object_get_int(l_obj, "int_val");
    DAP_TEST_ASSERT_EQUAL(42, l_int, "Int value");
    
    // Test int64 operations
    dap_json_object_add_int64(l_obj, "int64_val", 9223372036854775807LL);
    int64_t l_int64 = dap_json_object_get_int64(l_obj, "int64_val");
    DAP_TEST_ASSERT_EQUAL(9223372036854775807LL, l_int64, "Int64 value");
    
    // Test double operations
    dap_json_object_add_double(l_obj, "double_val", 3.14159);
    double l_double = dap_json_object_get_double(l_obj, "double_val");
    DAP_TEST_ASSERT(l_double > 3.14 && l_double < 3.15, "Double value");
    
    // Test bool operations
    dap_json_object_add_bool(l_obj, "bool_true", true);
    dap_json_object_add_bool(l_obj, "bool_false", false);
    bool l_bool_t = dap_json_object_get_bool(l_obj, "bool_true");
    bool l_bool_f = dap_json_object_get_bool(l_obj, "bool_false");
    DAP_TEST_ASSERT(l_bool_t == true, "Bool true value");
    DAP_TEST_ASSERT(l_bool_f == false, "Bool false value");
    
    dap_json_object_free(l_obj);
    log_it(L_DEBUG, "Numeric types test passed");
    return true;
}

/**
 * @brief Test array operations (Phase 3.4)
 */
static bool s_test_array_operations(void) {
    log_it(L_DEBUG, "Testing array operations");
    
    dap_json_t *l_array = dap_json_array_new();
    DAP_TEST_ASSERT_NOT_NULL(l_array, "Array creation");
    
    // Test array length with empty array
    size_t l_len = dap_json_array_length(l_array);
    DAP_TEST_ASSERT_EQUAL(0, l_len, "Empty array length");
    
    // Add multiple items
    dap_json_t *l_item1 = dap_json_object_new_int(10);
    dap_json_t *l_item2 = dap_json_object_new_int(20);
    dap_json_t *l_item3 = dap_json_object_new_int(30);
    
    dap_json_array_add(l_array, l_item1);
    dap_json_array_add(l_array, l_item2);
    dap_json_array_add(l_array, l_item3);
    
    // Check length
    l_len = dap_json_array_length(l_array);
    DAP_TEST_ASSERT_EQUAL(3, l_len, "Array length after adds");
    
    // Get and verify items
    dap_json_t *l_retrieved = dap_json_array_get_idx(l_array, 1);
    DAP_TEST_ASSERT_NOT_NULL(l_retrieved, "Get array item");
    DAP_TEST_ASSERT(dap_json_is_int(l_retrieved), "Item is int");
    dap_json_object_free(l_retrieved);
    
    dap_json_object_free(l_array);
    log_it(L_DEBUG, "Array operations test passed");
    return true;
}

/**
 * @brief Test type checking functions (Phase 3.4)
 */
static bool s_test_type_checking(void) {
    log_it(L_DEBUG, "Testing type checking");
    
    // Test is_object
    dap_json_t *l_obj = dap_json_object_new();
    DAP_TEST_ASSERT(dap_json_is_object(l_obj), "is_object check");
    dap_json_object_free(l_obj);
    
    // Test is_array
    dap_json_t *l_array = dap_json_array_new();
    DAP_TEST_ASSERT(dap_json_is_array(l_array), "is_array check");
    dap_json_object_free(l_array);
    
    // Test is_string
    dap_json_t *l_str = dap_json_object_new_string("test");
    DAP_TEST_ASSERT(dap_json_is_string(l_str), "is_string check");
    dap_json_object_free(l_str);
    
    // Test is_int
    dap_json_t *l_int = dap_json_object_new_int(42);
    DAP_TEST_ASSERT(dap_json_is_int(l_int), "is_int check");
    dap_json_object_free(l_int);
    
    // Test is_bool
    dap_json_t *l_bool = dap_json_object_new_bool(true);
    DAP_TEST_ASSERT(dap_json_is_bool(l_bool), "is_bool check");
    dap_json_object_free(l_bool);
    
    // Test is_double
    dap_json_t *l_double = dap_json_object_new_double(3.14);
    DAP_TEST_ASSERT(dap_json_is_double(l_double), "is_double check");
    dap_json_object_free(l_double);
    
    log_it(L_DEBUG, "Type checking test passed");
    return true;
}

/**
 * @brief Test object key operations (Phase 3.4)
 */
static bool s_test_object_key_operations(void) {
    log_it(L_DEBUG, "Testing object key operations");
    
    dap_json_t *l_obj = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_obj, "Object creation");
    
    // Add some keys
    dap_json_object_add_string(l_obj, "key1", "value1");
    dap_json_object_add_int(l_obj, "key2", 42);
    
    // Test has_key (Phase 3.3 convenience function)
    DAP_TEST_ASSERT(dap_json_object_has_key(l_obj, "key1"), "has_key existing");
    DAP_TEST_ASSERT(dap_json_object_has_key(l_obj, "key2"), "has_key existing 2");
    DAP_TEST_ASSERT(!dap_json_object_has_key(l_obj, "nonexistent"), "has_key nonexistent");
    
    // Test delete key
    int ret = dap_json_object_del(l_obj, "key1");
    DAP_TEST_ASSERT_EQUAL(0, ret, "Delete key");
    DAP_TEST_ASSERT(!dap_json_object_has_key(l_obj, "key1"), "Key deleted");
    
    dap_json_object_free(l_obj);
    log_it(L_DEBUG, "Object key operations test passed");
    return true;
}

/**
 * @brief Test error conditions and NULL handling (Phase 3.4)
 */
static bool s_test_error_conditions(void) {
    log_it(L_DEBUG, "Testing error conditions");
    
    // Test NULL object operations
    const char *l_str = dap_json_object_get_string(NULL, "key");
    DAP_TEST_ASSERT(l_str == NULL, "Get from NULL object");
    
    // Test NULL key operations
    dap_json_t *l_obj = dap_json_object_new();
    int ret = dap_json_object_add_string(l_obj, NULL, "value");
    DAP_TEST_ASSERT_EQUAL(-1, ret, "Add with NULL key");
    
    // Test has_key with NULL
    bool has = dap_json_object_has_key(NULL, "key");
    DAP_TEST_ASSERT(!has, "has_key on NULL object");
    
    has = dap_json_object_has_key(l_obj, NULL);
    DAP_TEST_ASSERT(!has, "has_key with NULL key");
    
    dap_json_object_free(l_obj);
    
    // Test free on NULL (should not crash)
    dap_json_object_free(NULL);
    
    log_it(L_DEBUG, "Error conditions test passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(void) {
    log_it(L_INFO, "Starting DAP JSON Unit Tests");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    // Basic functionality tests
    l_all_passed &= s_test_json_object_creation();
    l_all_passed &= s_test_json_array_creation();
    l_all_passed &= s_test_json_string_operations();
    l_all_passed &= s_test_json_parsing();
    l_all_passed &= s_test_json_serialization();
    
    // wrapper invalidation & reference counting
    log_it(L_INFO, "Running wrapper invalidation & reference counting tests...");
    l_all_passed &= s_test_wrapper_invalidation_add_object();
    l_all_passed &= s_test_wrapper_invalidation_add_array();
    l_all_passed &= s_test_wrapper_invalidation_array_add();
    l_all_passed &= s_test_refcount_get_object();
    l_all_passed &= s_test_refcount_array_get_idx();
    
    // Phase 3.4 comprehensive tests: core functionality
    log_it(L_INFO, "Running Phase 3.4 comprehensive tests...");
    l_all_passed &= s_test_numeric_types();
    l_all_passed &= s_test_array_operations();
    l_all_passed &= s_test_type_checking();
    l_all_passed &= s_test_object_key_operations();
    l_all_passed &= s_test_error_conditions();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All DAP JSON tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some DAP JSON tests failed!");
        return -1;
    }
}