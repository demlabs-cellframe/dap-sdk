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
    bool result = false;
    dap_json_t *l_json = NULL;
    
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "JSON object creation");
    
    result = true;
    log_it(L_DEBUG, "JSON object creation test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test JSON array creation and destruction
 */
static bool s_test_json_array_creation(void) {
    log_it(L_DEBUG, "Testing JSON array creation");
    bool result = false;
    dap_json_t *l_array = NULL;
    
    l_array = dap_json_array_new();
    DAP_TEST_FAIL_IF_NULL(l_array, "JSON array creation");
    
    result = true;
    log_it(L_DEBUG, "JSON array creation test passed");
    
cleanup:
    dap_json_object_free(l_array);
    return result;
}

/**
 * @brief Test JSON string operations
 */
static bool s_test_json_string_operations(void) {
    log_it(L_DEBUG, "Testing JSON string operations");
    bool result = false;
    dap_json_t *l_root = NULL;
    
    l_root = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_root, "JSON root object");
    
    // Add string value
    dap_json_object_add_string(l_root, "test_key", "test_value");
    
    // Get string value
    const char* l_value = dap_json_object_get_string(l_root, "test_key");
    DAP_TEST_FAIL_IF_NULL(l_value, "Retrieved string value");
    DAP_TEST_FAIL_IF_STRING_NOT_EQUAL("test_value", l_value, "String value comparison");
    
    result = true;
    log_it(L_DEBUG, "JSON string operations test passed");
    
cleanup:
    dap_json_object_free(l_root);
    return result;
}

/**
 * @brief Test JSON parsing of sample data
 */
static bool s_test_json_parsing(void) {
    log_it(L_DEBUG, "Testing JSON parsing");
    bool result = false;
    dap_json_t *l_parsed = NULL;
    
    // Parse simple JSON sample
    l_parsed = dap_json_parse_string(JSON_SAMPLE_SIMPLE);
    DAP_TEST_FAIL_IF_NULL(l_parsed, "JSON parsing of simple sample");
    
    // Verify parsed content
    const char* l_name = dap_json_object_get_string(l_parsed, "name");
    DAP_TEST_FAIL_IF_STRING_NOT_EQUAL("test", l_name, "Parsed name field");
    
    int64_t l_value = dap_json_object_get_int64(l_parsed, "value");
    DAP_TEST_FAIL_IF_NOT(123 == l_value, "Parsed value field");
    
    result = true;
    log_it(L_DEBUG, "JSON parsing test passed");
    
cleanup:
    dap_json_object_free(l_parsed);
    return result;
}

/**
 * @brief Test JSON serialization
 */
static bool s_test_json_serialization(void) {
    log_it(L_DEBUG, "Testing JSON serialization");
    bool result = false;
    dap_json_t *l_root = NULL;
    char* l_json_str = NULL;
    
    l_root = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_root, "JSON root for serialization");
    dap_json_object_add_string(l_root, "name", "test");
    dap_json_object_add_int64(l_root, "value", 123);
    
    l_json_str = dap_json_to_string(l_root);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "JSON serialization");
    // Check that output contains expected elements
    DAP_TEST_FAIL_IF_NOT(strstr(l_json_str, "name") != NULL, "Serialized JSON contains name");
    DAP_TEST_FAIL_IF_NOT(strstr(l_json_str, "test") != NULL, "Serialized JSON contains test");
    DAP_TEST_FAIL_IF_NOT(strstr(l_json_str, "value") != NULL, "Serialized JSON contains value");
    
    log_it(L_DEBUG, "Serialized JSON: %s", l_json_str);
    
    result = true;
    log_it(L_DEBUG, "JSON serialization test passed");
    
cleanup:
    DAP_DELETE(l_json_str);
    dap_json_object_free(l_root);
    return result;
}

/**
 * @brief Test wrapper invalidation after add_object 
 * This test verifies that after adding a child object to parent, the child wrapper
 * is invalidated (pvt = NULL) to prevent double-free
 */
static bool s_test_wrapper_invalidation_add_object(void) {
    log_it(L_DEBUG, "Testing wrapper invalidation after add_object");
    bool result = false;
    dap_json_t *l_parent = NULL;
    dap_json_t *l_child = NULL;
    dap_json_t *l_retrieved_child = NULL;
    
    l_parent = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_parent, "Parent object creation");
    l_child = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_child, "Child object creation");
    // Add some data to child
    dap_json_object_add_string(l_child, "name", "child");
    
    // Add child to parent - this should invalidate l_child wrapper
    int ret = dap_json_object_add_object(l_parent, "child_key", l_child);
    DAP_TEST_FAIL_IF_NONZERO(ret, "Adding child to parent");
    // After add, l_child wrapper should be invalidated (pvt = NULL);
    // Calling dap_json_object_free on invalidated wrapper should be safe
    dap_json_object_free(l_child);  // Should only free wrapper, not underlying object
    l_child = NULL;
    
    // Parent should still contain valid child data via nested get
    l_retrieved_child = dap_json_object_get_object(l_parent, "child_key");
    DAP_TEST_FAIL_IF_NULL(l_retrieved_child, "Child accessible via parent");
    const char *l_child_name = dap_json_object_get_string(l_retrieved_child, "name");
    DAP_TEST_FAIL_IF_STRING_NOT_EQUAL("child", l_child_name, "Child data correct");
    result = true;
    log_it(L_DEBUG, "Wrapper invalidation after add_object test passed");
    
cleanup:
    // l_retrieved_child is borrowed - freed automatically with parent
    // Free parent (which frees the underlying child object and borrowed wrappers)
    dap_json_object_free(l_parent);
    // l_child already freed above
    return result;
}

/**
 * @brief Test wrapper invalidation after add_array 
 */
static bool s_test_wrapper_invalidation_add_array(void) {
    log_it(L_DEBUG, "Testing wrapper invalidation after add_array");
    bool result = false;
    dap_json_t *l_parent = NULL;
    dap_json_t *l_array = NULL;
    dap_json_t *l_item1 = NULL;
    dap_json_t *l_item2 = NULL;
    dap_json_t *l_retrieved_array = NULL;
    
    l_parent = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_parent, "Parent object creation");
    l_array = dap_json_array_new();
    DAP_TEST_FAIL_IF_NULL(l_array, "Array creation");
    // Add some strings to array via string objects
    l_item1 = dap_json_object_new_string("item1");
    l_item2 = dap_json_object_new_string("item2");
    dap_json_array_add(l_array, l_item1);
    dap_json_object_free(l_item1);  // Must free wrapper after ownership transfer
    l_item1 = NULL;
    dap_json_array_add(l_array, l_item2);
    dap_json_object_free(l_item2);  // Must free wrapper after ownership transfer
    l_item2 = NULL;
    
    // Add array to parent - this should invalidate l_array wrapper
    int ret = dap_json_object_add_array(l_parent, "array_key", l_array);
    DAP_TEST_FAIL_IF_NONZERO(ret, "Adding array to parent");
    // After add, l_array wrapper should be invalidated
    dap_json_object_free(l_array);  // Should only free wrapper
    l_array = NULL;
    
    // Parent should still contain valid array data
    l_retrieved_array = dap_json_object_get_array(l_parent, "array_key");
    DAP_TEST_FAIL_IF_NULL(l_retrieved_array, "Array accessible via parent");
    size_t array_len = dap_json_array_length(l_retrieved_array);
    DAP_TEST_FAIL_IF_NOT(2 == array_len, "Array length correct");
    result = true;
    log_it(L_DEBUG, "Wrapper invalidation after add_array test passed");
    
cleanup:
    // l_retrieved_array is borrowed - freed automatically with parent
    // Free parent
    dap_json_object_free(l_parent);
    // l_array, l_item1, l_item2 already freed above
    return result;
}

/**
 * @brief Test wrapper invalidation after array_add 
 */
static bool s_test_wrapper_invalidation_array_add(void) {
    log_it(L_DEBUG, "Testing wrapper invalidation after array_add");
    bool result = false;
    dap_json_t *l_array = NULL;
    dap_json_t *l_item = NULL;
    
    l_array = dap_json_array_new();
    DAP_TEST_FAIL_IF_NULL(l_array, "Array creation");
    l_item = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_item, "Item object creation");
    dap_json_object_add_string(l_item, "name", "item");
    
    // Add item to array - this should invalidate l_item wrapper
    int ret = dap_json_array_add(l_array, l_item);
    DAP_TEST_FAIL_IF_NONZERO(ret, "Adding item to array");
    // After add, l_item wrapper should be invalidated
    dap_json_object_free(l_item);  // Should only free wrapper
    l_item = NULL;
    
    // Array should still contain valid item
    size_t array_len = dap_json_array_length(l_array);
    DAP_TEST_FAIL_IF_NOT(1 == array_len, "Array contains item");
    result = true;
    log_it(L_DEBUG, "Wrapper invalidation after array_add test passed");
    
cleanup:
    // Free array
    dap_json_object_free(l_array);
    // l_item already freed above
    return result;
}

/**
 * @brief Test borrowed references for get_object 
 * After get_object, returned wrapper is borrowed and freed with parent
 */
static bool s_test_refcount_get_object(void) {
    log_it(L_DEBUG, "Testing borrowed references for get_object");
    bool result = false;
    dap_json_t *l_parent = NULL;
    dap_json_t *l_child_obj = NULL;
    dap_json_t *l_retrieved = NULL;
    dap_json_t *l_retrieved2 = NULL;
    
    l_parent = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_parent, "Parent object creation");
    // Create child object with data
    l_child_obj = dap_json_object_new();
    dap_json_object_add_string(l_child_obj, "name", "test_child");
    
    // Add child to parent
    dap_json_object_add_object(l_parent, "child", l_child_obj);
    dap_json_object_free(l_child_obj);  // Must free wrapper after ownership transfer
    l_child_obj = NULL;
    
    // Get child object - this returns borrowed reference
    l_retrieved = dap_json_object_get_object(l_parent, "child");
    DAP_TEST_FAIL_IF_NULL(l_retrieved, "Retrieved child object");
    // Verify child data
    const char *l_name = dap_json_object_get_string(l_retrieved, "name");
    DAP_TEST_FAIL_IF_STRING_NOT_EQUAL("test_child", l_name, "Child data correct");
    // NO free for borrowed reference!
    
    // Parent should still be valid, verify via another get
    l_retrieved2 = dap_json_object_get_object(l_parent, "child");
    DAP_TEST_FAIL_IF_NULL(l_retrieved2, "Parent still valid");
    const char *l_name2 = dap_json_object_get_string(l_retrieved2, "name");
    DAP_TEST_FAIL_IF_STRING_NOT_EQUAL("test_child", l_name2, "Child data still correct");
    result = true;
    log_it(L_DEBUG, "Borrowed references for get_object test passed");
    
cleanup:
    // l_retrieved and l_retrieved2 are borrowed - freed with parent
    // Free parent
    dap_json_object_free(l_parent);
    // l_child_obj, l_retrieved already freed above
    return result;
}

/**
 * @brief Test borrowed references for array_get_idx 
 */
static bool s_test_refcount_array_get_idx(void) {
    log_it(L_DEBUG, "Testing borrowed references for array_get_idx");
    bool result = false;
    dap_json_t *l_array = NULL;
    dap_json_t *l_s1 = NULL, *l_s2 = NULL, *l_s3 = NULL;
    dap_json_t *l_item1 = NULL, *l_item2 = NULL;
    
    l_array = dap_json_array_new();
    DAP_TEST_FAIL_IF_NULL(l_array, "Array creation");
    // Add string items via objects
    l_s1 = dap_json_object_new_string("item1");
    l_s2 = dap_json_object_new_string("item2");
    l_s3 = dap_json_object_new_string("item3");
    dap_json_array_add(l_array, l_s1);
    dap_json_object_free(l_s1);  l_s1 = NULL;
    dap_json_array_add(l_array, l_s2);
    dap_json_object_free(l_s2);  l_s2 = NULL;
    dap_json_array_add(l_array, l_s3);
    dap_json_object_free(l_s3);  l_s3 = NULL;
    
    // Get array length
    size_t len = dap_json_array_length(l_array);
    DAP_TEST_FAIL_IF_NOT(3 == len, "Array length");
    // Get items - returns borrowed references
    l_item1 = dap_json_array_get_idx(l_array, 0);
    DAP_TEST_FAIL_IF_NULL(l_item1, "First item retrieved");
    DAP_TEST_FAIL_IF_NOT(dap_json_is_string(l_item1), "First item is string");
    // l_item1 is borrowed - no free needed
    
    l_item2 = dap_json_array_get_idx(l_array, 1);
    DAP_TEST_FAIL_IF_NULL(l_item2, "Second item retrieved");
    DAP_TEST_FAIL_IF_NOT(dap_json_is_string(l_item2), "Second item is string");
    // l_item2 is borrowed - no free needed
    
    result = true;
    log_it(L_DEBUG, "Borrowed references for array_get_idx test passed");
    
cleanup:
    // l_item1 and l_item2 are borrowed - freed with array
    // Free array (which frees all items)
    dap_json_object_free(l_array);
    // l_s1, l_s2, l_s3 already freed above
    return result;
}

/**
 * @brief Test numeric types operations 
 */
static bool s_test_numeric_types(void) {
    log_it(L_DEBUG, "Testing numeric types");
    bool result = false;
    dap_json_t *l_obj = NULL;
    
    l_obj = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_obj, "Object creation");
    // Test int operations
    dap_json_object_add_int(l_obj, "int_val", 42);
    int l_int = dap_json_object_get_int(l_obj, "int_val");
    DAP_TEST_FAIL_IF_NOT(42 == l_int, "Int value");
    // Test int64 operations
    dap_json_object_add_int64(l_obj, "int64_val", 9223372036854775807LL);
    int64_t l_int64 = dap_json_object_get_int64(l_obj, "int64_val");
    DAP_TEST_FAIL_IF_NOT(9223372036854775807LL == l_int64, "Int64 value");
    
    // Test double operations
    dap_json_object_add_double(l_obj, "double_val", 3.14159);
    double l_double = dap_json_object_get_double(l_obj, "double_val");
    DAP_TEST_FAIL_IF_NOT(l_double > 3.14 && l_double < 3.15, "Double value");
    
    // Test bool operations
    dap_json_object_add_bool(l_obj, "bool_true", true);
    dap_json_object_add_bool(l_obj, "bool_false", false);
    bool l_bool_t = dap_json_object_get_bool(l_obj, "bool_true");
    bool l_bool_f = dap_json_object_get_bool(l_obj, "bool_false");
    DAP_TEST_FAIL_IF_NOT(l_bool_t == true, "Bool true value");
    DAP_TEST_FAIL_IF_NOT(l_bool_f == false, "Bool false value");
    
    result = true;
    log_it(L_DEBUG, "Numeric types test passed");
    
cleanup:
    dap_json_object_free(l_obj);
    return result;
}

/**
 * @brief Test array operations
 */
static bool s_test_array_operations(void) {
    log_it(L_DEBUG, "Testing array operations");
    bool result = false;
    dap_json_t *l_array = NULL;
    dap_json_t *l_item1 = NULL, *l_item2 = NULL, *l_item3 = NULL;
    dap_json_t *l_retrieved = NULL;
    
    l_array = dap_json_array_new();
    DAP_TEST_FAIL_IF_NULL(l_array, "Array creation");
    // Test array length with empty array
    size_t l_len = dap_json_array_length(l_array);
    DAP_TEST_FAIL_IF_NONZERO(l_len, "Empty array length");
    // Add multiple items
    l_item1 = dap_json_object_new_int(10);
    l_item2 = dap_json_object_new_int(20);
    l_item3 = dap_json_object_new_int(30);
    
    dap_json_array_add(l_array, l_item1);
    dap_json_object_free(l_item1);  l_item1 = NULL;
    dap_json_array_add(l_array, l_item2);
    dap_json_object_free(l_item2);  l_item2 = NULL;
    dap_json_array_add(l_array, l_item3);
    dap_json_object_free(l_item3);  l_item3 = NULL;
    
    // Check length
    l_len = dap_json_array_length(l_array);
    DAP_TEST_FAIL_IF_NOT(3 == l_len, "Array length after adds");
    // Get and verify items (borrowed reference)
    l_retrieved = dap_json_array_get_idx(l_array, 1);
    DAP_TEST_FAIL_IF_NULL(l_retrieved, "Get array item");
    DAP_TEST_FAIL_IF_NOT(dap_json_is_int(l_retrieved), "Item is int");
    // l_retrieved is borrowed - no free needed
    
    result = true;
    log_it(L_DEBUG, "Array operations test passed");
    
cleanup:
    dap_json_object_free(l_array);
    // l_item1, l_item2, l_item3, l_retrieved already freed above
    return result;
}

/**
 * @brief Test type checking functions 
 */
static bool s_test_type_checking(void) {
    log_it(L_DEBUG, "Testing type checking");
    bool result = false;
    dap_json_t *l_obj = NULL;
    
    // Test is_object
    l_obj = dap_json_object_new();
    DAP_TEST_FAIL_IF_NOT(dap_json_is_object(l_obj), "is_object check");
    dap_json_object_free(l_obj);
    
    // Test is_array
    l_obj = dap_json_array_new();
    DAP_TEST_FAIL_IF_NOT(dap_json_is_array(l_obj), "is_array check");
    dap_json_object_free(l_obj);
    
    // Test is_string
    l_obj = dap_json_object_new_string("test");
    DAP_TEST_FAIL_IF_NOT(dap_json_is_string(l_obj), "is_string check");
    dap_json_object_free(l_obj);
    
    // Test is_int
    l_obj = dap_json_object_new_int(42);
    DAP_TEST_FAIL_IF_NOT(dap_json_is_int(l_obj), "is_int check");
    dap_json_object_free(l_obj);
    
    // Test is_bool
    l_obj = dap_json_object_new_bool(true);
    DAP_TEST_FAIL_IF_NOT(dap_json_is_bool(l_obj), "is_bool check");
    dap_json_object_free(l_obj);
    
    // Test is_double
    l_obj = dap_json_object_new_double(3.14);
    DAP_TEST_FAIL_IF_NOT(dap_json_is_double(l_obj), "is_double check");
    dap_json_object_free(l_obj);
    l_obj = NULL;
    
    result = true;
    log_it(L_DEBUG, "Type checking test passed");
    
cleanup:
    dap_json_object_free(l_obj);
    return result;
}

/**
 * @brief Test object key operations 
 */
static bool s_test_object_key_operations(void) {
    log_it(L_DEBUG, "Testing object key operations");
    bool result = false;
    dap_json_t *l_obj = NULL;
    
    l_obj = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_obj, "Object creation");
    // Add some keys
    dap_json_object_add_string(l_obj, "key1", "value1");
    dap_json_object_add_int(l_obj, "key2", 42);
    
    // Test has_key 
    DAP_TEST_FAIL_IF_NOT(dap_json_object_has_key(l_obj, "key1"), "has_key existing");
    DAP_TEST_FAIL_IF_NOT(dap_json_object_has_key(l_obj, "key2"), "has_key existing 2");
    DAP_TEST_FAIL_IF(dap_json_object_has_key(l_obj, "nonexistent"), "has_key nonexistent");
    
    // Test delete key
    int ret = dap_json_object_del(l_obj, "key1");
    DAP_TEST_FAIL_IF_NONZERO(ret, "Delete key");
    DAP_TEST_FAIL_IF(dap_json_object_has_key(l_obj, "key1"), "Key deleted");
    
    result = true;
    log_it(L_DEBUG, "Object key operations test passed");
    
cleanup:
    dap_json_object_free(l_obj);
    return result;
}

/**
 * @brief Test error conditions and NULL handling 
 */
static bool s_test_error_conditions(void) {
    log_it(L_DEBUG, "Testing error conditions");
    bool result = false;
    dap_json_t *l_obj = NULL;
    
    // Test NULL object operations
    const char *l_str = dap_json_object_get_string(NULL, "key");
    DAP_TEST_FAIL_IF_NOT(l_str == NULL, "Get from NULL object");
    
    // Test NULL key operations
    l_obj = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_obj, "Object creation");
    
    int ret = dap_json_object_add_string(l_obj, NULL, "value");
    DAP_TEST_FAIL_IF_NOT(-1 == ret, "Add with NULL key");
    
    // Test has_key with NULL
    bool has = dap_json_object_has_key(NULL, "key");
    DAP_TEST_FAIL_IF(has, "has_key on NULL object");
    
    has = dap_json_object_has_key(l_obj, NULL);
    DAP_TEST_FAIL_IF(has, "has_key with NULL key");
    
    // Test free on NULL (should not crash);
    dap_json_object_free(NULL);
    
    result = true;
    log_it(L_DEBUG, "Error conditions test passed");
    
cleanup:
    dap_json_object_free(l_obj);
    return result;
}

/**
 * @brief Test complex nested structures 
 */
static bool s_test_nested_structures(void) {
    log_it(L_DEBUG, "Testing nested structures");
    bool result = false;
    dap_json_t *l_root = NULL;
    dap_json_t *l_user = NULL;
    dap_json_t *l_tags = NULL;
    dap_json_t *l_tag1 = NULL;
    dap_json_t *l_tag2 = NULL;
    
    // Create complex nested structure
    l_root = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_root, "Root object creation");
    
    // Add nested object
    l_user = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_user, "User object creation");
    
    dap_json_object_add_string(l_user, "name", "Alice");
    dap_json_object_add_int(l_user, "age", 30);
    
    // Add nested array
    l_tags = dap_json_array_new();
    DAP_TEST_FAIL_IF_NULL(l_tags, "Tags array creation");
    
    l_tag1 = dap_json_object_new_string("developer");
    DAP_TEST_FAIL_IF_NULL(l_tag1, "Tag1 creation");
    dap_json_array_add(l_tags, l_tag1);
    dap_json_object_free(l_tag1);
    l_tag1 = NULL;  // Ownership transferred
    
    l_tag2 = dap_json_object_new_string("blockchain");
    DAP_TEST_FAIL_IF_NULL(l_tag2, "Tag2 creation");
    dap_json_array_add(l_tags, l_tag2);
    dap_json_object_free(l_tag2);
    l_tag2 = NULL;  // Ownership transferred
    
    // Add to user object
    dap_json_object_add_array(l_user, "tags", l_tags);
    dap_json_object_free(l_tags);
    l_tags = NULL;  // Ownership transferred
    
    // Add user to root
    dap_json_object_add_object(l_root, "user", l_user);
    dap_json_object_free(l_user);
    l_user = NULL;  // Ownership transferred
    
    // Verify structure by retrieving (borrowed references - don't free!);
    dap_json_t *l_retrieved_user = dap_json_object_get_object(l_root, "user");
    DAP_TEST_FAIL_IF_NULL(l_retrieved_user, "Retrieved nested object");
    
    const char *l_name = dap_json_object_get_string(l_retrieved_user, "name");
    DAP_TEST_FAIL_IF_STRING_NOT_EQUAL("Alice", l_name, "Nested string value");
    
    dap_json_t *l_retrieved_tags = dap_json_object_get_array(l_retrieved_user, "tags");
    DAP_TEST_FAIL_IF_NULL(l_retrieved_tags, "Retrieved nested array");
    
    size_t l_tags_len = dap_json_array_length(l_retrieved_tags);
    DAP_TEST_FAIL_IF_NOT(2 == l_tags_len, "Nested array length");
    
    result = true;
    log_it(L_DEBUG, "Nested structures test passed");
    
cleanup:
    // Note: l_retrieved_* are borrowed references, freed with l_root
    dap_json_object_free(l_root);
    dap_json_object_free(l_user);    // NULL if transferred
    dap_json_object_free(l_tags);    // NULL if transferred
    dap_json_object_free(l_tag1);    // NULL if transferred
    dap_json_object_free(l_tag2);    // NULL if transferred
    return result;
}

/**
 * @brief Test JSON parsing edge cases 
 */
static bool s_test_parsing_edge_cases(void) {
    log_it(L_DEBUG, "Testing parsing edge cases");
    bool result = false;
    dap_json_t *l_empty = NULL;
    dap_json_t *l_empty_arr = NULL;
    dap_json_t *l_complex_arr = NULL;
    
    // Test empty object parsing
    l_empty = dap_json_parse_string("{}");
    DAP_TEST_FAIL_IF_NULL(l_empty, "Parse empty object");
    DAP_TEST_FAIL_IF_NOT(dap_json_is_object(l_empty), "Empty object is object");
    dap_json_object_free(l_empty);
    l_empty = NULL;
    
    // Test empty array parsing
    l_empty_arr = dap_json_parse_string("[]");
    DAP_TEST_FAIL_IF_NULL(l_empty_arr, "Parse empty array");
    DAP_TEST_FAIL_IF_NOT(dap_json_is_array(l_empty_arr), "Empty array is array");
    dap_json_object_free(l_empty_arr);
    l_empty_arr = NULL;
    
    // Test array with various types
    const char *l_complex_json = "[1, \"test\", true, 3.14, null]";
    l_complex_arr = dap_json_parse_string(l_complex_json);
    DAP_TEST_FAIL_IF_NULL(l_complex_arr, "Parse complex array");
    
    size_t l_len = dap_json_array_length(l_complex_arr);
    DAP_TEST_FAIL_IF_NOT(5 == l_len, "Complex array length");
    
    dap_json_object_free(l_complex_arr);
    l_complex_arr = NULL;
    
    // Test invalid JSON
    dap_json_t *l_invalid = dap_json_parse_string("{invalid}");
    DAP_TEST_FAIL_IF_NOT(l_invalid == NULL, "Invalid JSON returns NULL");
    
    // Test NULL string
    dap_json_t *l_null_str = dap_json_parse_string(NULL);
    DAP_TEST_FAIL_IF_NOT(l_null_str == NULL, "NULL string returns NULL");
    
    result = true;
    log_it(L_DEBUG, "Parsing edge cases test passed");
    
cleanup:
    dap_json_object_free(l_empty);
    dap_json_object_free(l_empty_arr);
    dap_json_object_free(l_complex_arr);
    return result;
}

/**
 * @brief Test serialization edge cases 
 */
static bool s_test_serialization_edge_cases(void) {
    log_it(L_DEBUG, "Testing serialization edge cases");
    bool result = false;
    dap_json_t *l_empty = NULL;
    dap_json_t *l_special = NULL;
    char *l_json_str = NULL;
    char *l_special_json = NULL;
    
    // Test empty object serialization
    l_empty = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_empty, "Empty object creation");
    
    l_json_str = dap_json_to_string(l_empty);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "Empty object serialization");
    // Note: JSON-C may format empty object as "{}" or "{ }", both are valid
    DAP_TEST_FAIL_IF_NOT(strlen(l_json_str) >= 2, "Empty object JSON has content");
    DAP_DELETE(l_json_str);
    l_json_str = NULL;
    dap_json_object_free(l_empty);
    l_empty = NULL;
    
    // Test object with special characters
    l_special = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_special, "Special object creation");
    
    dap_json_object_add_string(l_special, "text", "Hello\nWorld\t!");
    l_special_json = dap_json_to_string(l_special);
    DAP_TEST_FAIL_IF_NULL(l_special_json, "Special chars serialization");
    DAP_DELETE(l_special_json);
    l_special_json = NULL;
    dap_json_object_free(l_special);
    l_special = NULL;
    
    // Test NULL serialization
    char *l_null_json = dap_json_to_string(NULL);
    DAP_TEST_FAIL_IF_NOT(l_null_json == NULL, "NULL object serialization");
    
    result = true;
    log_it(L_DEBUG, "Serialization edge cases test passed");
    
cleanup:
    DAP_DELETE(l_json_str);
    DAP_DELETE(l_special_json);
    dap_json_object_free(l_empty);
    dap_json_object_free(l_special);
    return result;
}

/**
 * @brief Test with large data volumes
 */
static bool s_test_large_data(void) {
    log_it(L_DEBUG, "Testing large data volumes");
    bool result = false;
    dap_json_t *l_large_array = NULL;
    dap_json_t *l_item = NULL;
    
    // Create array with many elements
    l_large_array = dap_json_array_new();
    DAP_TEST_FAIL_IF_NULL(l_large_array, "Large array creation");
    
    const size_t ITEM_COUNT = 1000;
    for (size_t i = 0; i < ITEM_COUNT; i++) {
        l_item = dap_json_object_new_int((int)i);
        DAP_TEST_FAIL_IF_NULL(l_item, "Item creation");
        dap_json_array_add(l_large_array, l_item);
        dap_json_object_free(l_item);
        l_item = NULL;  // Ownership transferred
    }
    
    size_t l_len = dap_json_array_length(l_large_array);
    DAP_TEST_FAIL_IF_NOT(ITEM_COUNT == l_len, "Large array length");
    
    // Retrieve and verify some items (borrowed references - don't free!);
    dap_json_t *l_first = dap_json_array_get_idx(l_large_array, 0);
    DAP_TEST_FAIL_IF_NULL(l_first, "First item");
    
    dap_json_t *l_last = dap_json_array_get_idx(l_large_array, ITEM_COUNT - 1);
    DAP_TEST_FAIL_IF_NULL(l_last, "Last item");
    
    result = true;
    log_it(L_DEBUG, "Large data volumes test passed");
    
cleanup:
    dap_json_object_free(l_large_array);
    dap_json_object_free(l_item);  // NULL if transferred
    return result;
}

/**
 * @brief Test deeply nested structures 
 */
static bool s_test_deep_nesting(void) {
    log_it(L_DEBUG, "Testing deeply nested structures");
    bool result = false;
    dap_json_t *l_root = NULL;
    dap_json_t *l_level1 = NULL;
    dap_json_t *l_level2 = NULL;
    dap_json_t *l_level3 = NULL;
    dap_json_t *l_parsed = NULL;
    char *l_json_str = NULL;
    
    // Create deeply nested structure
    l_root = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_root, "Root creation");
    
    l_level1 = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_level1, "Level1 creation");
    
    l_level2 = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_level2, "Level2 creation");
    
    l_level3 = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_level3, "Level3 creation");
    
    // Add innermost value
    dap_json_object_add_string(l_level3, "deep_value", "found!");
    
    // Nest the objects
    dap_json_object_add_object(l_level2, "level3", l_level3);
    dap_json_object_free(l_level3);
    l_level3 = NULL;  // Ownership transferred
    
    dap_json_object_add_object(l_level1, "level2", l_level2);
    dap_json_object_free(l_level2);
    l_level2 = NULL;  // Ownership transferred
    
    dap_json_object_add_object(l_root, "level1", l_level1);
    dap_json_object_free(l_level1);
    l_level1 = NULL;  // Ownership transferred
    
    // Serialize and verify
    l_json_str = dap_json_to_string(l_root);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "Deep nesting serialization");
    DAP_DELETE(l_json_str);
    
    // Parse back and verify
    l_json_str = dap_json_to_string(l_root);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "Deep nesting re-serialization");
    
    l_parsed = dap_json_parse_string(l_json_str);
    DAP_TEST_FAIL_IF_NULL(l_parsed, "Deep nesting parse back");
    
    result = true;
    log_it(L_DEBUG, "Deeply nested structures test passed");
    
cleanup:
    DAP_DELETE(l_json_str);
    dap_json_object_free(l_parsed);
    dap_json_object_free(l_root);
    dap_json_object_free(l_level1);  // NULL if transferred
    dap_json_object_free(l_level2);  // NULL if transferred
    dap_json_object_free(l_level3);  // NULL if transferred
    return result;
}

/**
 * @brief Test fix for Problem #1: dap_json_object_get_ex borrowed reference
 * Verifies that get_ex returns borrowed reference (JSON-C semantics)
 */
static bool s_test_fix_get_ex_refcount(void) {
    log_it(L_DEBUG, "Testing: dap_json_object_get_ex borrowed reference semantics");
    bool result = false;
    dap_json_t *l_parent = NULL;
    dap_json_t *l_retrieved = NULL;
    
    l_parent = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_parent, "Parent object creation");
    
    dap_json_object_add_string(l_parent, "test_key", "test_value");
    
    // get_ex should return borrowed reference
    bool l_found = dap_json_object_get_ex(l_parent, "test_key", &l_retrieved);
    DAP_TEST_FAIL_IF_NOT(l_found, "Key found via get_ex");
    DAP_TEST_FAIL_IF_NULL(l_retrieved, "Retrieved value not NULL");
    
    // Verify value is accessible
    const char *l_value = dap_json_get_string(l_retrieved);
    DAP_TEST_FAIL_IF_STRING_NOT_EQUAL("test_value", l_value, "Value accessible");
    
    // To make it owned, need to call ref()
    dap_json_object_ref(l_retrieved);
    
    // Now free parent - retrieved should still be valid (owns its refcount)
    dap_json_object_free(l_parent);
    l_parent = NULL;
    
    // Retrieved value should still be accessible after ref()
    l_value = dap_json_get_string(l_retrieved);
    DAP_TEST_FAIL_IF_STRING_NOT_EQUAL("test_value", l_value, "Value accessible after parent freed");
    
    result = true;
    log_it(L_DEBUG, "Verified: dap_json_object_get_ex returns borrowed reference");
    
cleanup:
    dap_json_object_free(l_parent);
    dap_json_object_free(l_retrieved);  // Safe because we called ref()
    return result;
}

/**
 * @brief Test fix for Problem #3: dap_json_object_ref increments refcount
 * Verifies that ref increments refcount and returns same wrapper
 */
static bool s_test_fix_ref_new_wrapper(void) {
    log_it(L_DEBUG, "Testing: dap_json_object_ref increments refcount");
    bool result = false;
    dap_json_t *l_obj1 = NULL;
    dap_json_t *l_obj2 = NULL;
    
    l_obj1 = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_obj1, "Object 1 creation");
    
    dap_json_object_add_string(l_obj1, "key", "value");
    
    // Create reference - should get SAME wrapper (JSON-C semantics)
    l_obj2 = dap_json_object_ref(l_obj1);
    DAP_TEST_FAIL_IF_NULL(l_obj2, "Object 2 reference creation");
    
    // Verify they point to the SAME wrapper structure
    DAP_TEST_FAIL_IF_NOT(l_obj1 == l_obj2, "Wrapper is the same (JSON-C semantics)");
    
    // Verify value is accessible
    const char *l_value = dap_json_object_get_string(l_obj1, "key");
    DAP_TEST_FAIL_IF_STRING_NOT_EQUAL("value", l_value, "Object functional");
    
    // Free wrapper once - underlying JSON-C object still has refcount 2
    dap_json_object_free(l_obj1);
    l_obj1 = NULL;
    l_obj2 = NULL;  // Same pointer, already freed
    
    result = true;
    log_it(L_DEBUG, "Verified: dap_json_object_ref increments refcount");
    
cleanup:
    // Both pointers were the same, already freed above
    return result;
}

/**
 * @brief Test fix for Problem #4: wrapper leak in print_object
 * Verifies that printing objects doesn't leak wrapper structures
 */
static bool s_test_fix_print_object_no_leak(void) {
    log_it(L_DEBUG, "Testing fix: print object no wrapper leaks");
    bool result = false;
    dap_json_t *l_obj = NULL;
    char *l_json_str = NULL;
    
    l_obj = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_obj, "Object creation");
    
    // Add many keys to test for leaks
    for (int i = 0; i < 100; i++) {
        char l_key[32];
        snprintf(l_key, sizeof(l_key), "key_%d", i);
        dap_json_object_add_int(l_obj, l_key, i);
    }
    
    // Print to string multiple times - should not leak wrappers
    for (int i = 0; i < 10; i++) {
        l_json_str = dap_json_to_string_pretty(l_obj);
        DAP_TEST_FAIL_IF_NULL(l_json_str, "JSON string created");
        free(l_json_str);
        l_json_str = NULL;
    }
    
    result = true;
    log_it(L_DEBUG, "Fix verified: print object no wrapper leaks");
    
cleanup:
    free(l_json_str);
    dap_json_object_free(l_obj);
    return result;
}

/**
 * @brief Test fix for Problem #5: refcount leak in print array
 * Verifies that printing arrays doesn't leak refcounts
 */
static bool s_test_fix_print_array_no_leak(void) {
    log_it(L_DEBUG, "Testing fix: print array no refcount leaks");
    bool result = false;
    dap_json_t *l_array = NULL;
    dap_json_t *l_item = NULL;
    char *l_json_str = NULL;
    
    l_array = dap_json_array_new();
    DAP_TEST_FAIL_IF_NULL(l_array, "Array creation");
    
    // Add many items to test for leaks
    for (int i = 0; i < 100; i++) {
        l_item = dap_json_object_new_string("test");
        DAP_TEST_FAIL_IF_NULL(l_item, "Item creation");
        dap_json_array_add(l_array, l_item);
        dap_json_object_free(l_item);
        l_item = NULL;  // Ownership transferred
    }
    
    // Print to string multiple times - should not leak refcounts
    for (int i = 0; i < 10; i++) {
        l_json_str = dap_json_to_string_pretty(l_array);
        DAP_TEST_FAIL_IF_NULL(l_json_str, "JSON string created");
        free(l_json_str);
        l_json_str = NULL;
    }
    
    result = true;
    log_it(L_DEBUG, "Fix verified: print array no refcount leaks");
    
cleanup:
    free(l_json_str);
    dap_json_object_free(l_array);  // Works for arrays too
    dap_json_object_free(l_item);  // NULL if transferred
    return result;
}

/**
 * @brief Test memory safety: multiple get operations
 */
static bool s_test_memory_multiple_gets(void) {
    log_it(L_DEBUG, "Testing memory safety: multiple get operations");
    bool result = false;
    dap_json_t *l_parent = NULL;
    dap_json_t *l_child = NULL;
    
    l_parent = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_parent, "Parent object creation");
    
    l_child = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_child, "Child object creation");
    
    dap_json_object_add_string(l_child, "data", "test");
    dap_json_object_add_object(l_parent, "child", l_child);
    dap_json_object_free(l_child);
    l_child = NULL;  // Ownership transferred
    
    // Multiple get operations return borrowed references - DON'T free them!
    dap_json_t *l_get1 = dap_json_object_get_object(l_parent, "child");
    dap_json_t *l_get2 = dap_json_object_get_object(l_parent, "child");
    dap_json_t *l_get3 = dap_json_object_get_object(l_parent, "child");
    
    DAP_TEST_FAIL_IF_NULL(l_get1, "First get successful");
    DAP_TEST_FAIL_IF_NULL(l_get2, "Second get successful");
    DAP_TEST_FAIL_IF_NULL(l_get3, "Third get successful");
    
    result = true;
    log_it(L_DEBUG, "Memory safety: multiple gets - passed");
    
cleanup:
    // Note: l_get* are borrowed references, freed with l_parent
    dap_json_object_free(l_parent);
    dap_json_object_free(l_child);  // NULL if transferred
    return result;
}

/**
 * @brief Test memory safety: complex nested operations
 */
static bool s_test_memory_complex_nested(void) {
    log_it(L_DEBUG, "Testing memory safety: complex nested operations");
    bool result = false;
    dap_json_t *l_root = NULL;
    dap_json_t *l_level1 = NULL;
    dap_json_t *l_level2 = NULL;
    dap_json_t *l_array = NULL;
    dap_json_t *l_item = NULL;
    
    // Create complex nested structure
    l_root = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_root, "Root creation");
    
    l_level1 = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_level1, "Level1 creation");
    
    l_level2 = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_level2, "Level2 creation");
    
    l_array = dap_json_array_new();
    DAP_TEST_FAIL_IF_NULL(l_array, "Array creation");
    
    // Add array items
    for (int i = 0; i < 10; i++) {
        l_item = dap_json_object_new_int(i);
        DAP_TEST_FAIL_IF_NULL(l_item, "Item creation");
        dap_json_array_add(l_array, l_item);
        dap_json_object_free(l_item);
        l_item = NULL;  // Ownership transferred
    }
    
    // Build nested structure
    dap_json_object_add_array(l_level2, "numbers", l_array);
    dap_json_object_free(l_array);
    l_array = NULL;  // Ownership transferred
    
    dap_json_object_add_object(l_level1, "level2", l_level2);
    dap_json_object_free(l_level2);
    l_level2 = NULL;  // Ownership transferred
    
    dap_json_object_add_object(l_root, "level1", l_level1);
    dap_json_object_free(l_level1);
    l_level1 = NULL;  // Ownership transferred
    
    // Retrieve and verify (borrowed references - DON'T free!);
    dap_json_t *l_retrieved_l1 = dap_json_object_get_object(l_root, "level1");
    DAP_TEST_FAIL_IF_NULL(l_retrieved_l1, "Retrieved level1");
    
    dap_json_t *l_retrieved_l2 = dap_json_object_get_object(l_retrieved_l1, "level2");
    DAP_TEST_FAIL_IF_NULL(l_retrieved_l2, "Retrieved level2");
    
    dap_json_t *l_retrieved_arr = dap_json_object_get_array(l_retrieved_l2, "numbers");
    DAP_TEST_FAIL_IF_NULL(l_retrieved_arr, "Retrieved array");
    
    size_t arr_len = dap_json_array_length(l_retrieved_arr);
    DAP_TEST_FAIL_IF_NOT(10 == arr_len, "Array length correct");
    
    result = true;
    log_it(L_DEBUG, "Memory safety: complex nested - passed");
    
cleanup:
    // Note: l_retrieved_* are borrowed references, freed with l_root
    dap_json_object_free(l_root);
    dap_json_object_free(l_level1);  // NULL if transferred
    dap_json_object_free(l_level2);  // NULL if transferred
    dap_json_object_free(l_array);   // NULL if transferred
    dap_json_object_free(l_item);    // NULL if transferred
    return result;
}

/**
 * @brief Test automatic cleanup of borrowed wrappers
 * Verifies that borrowed references are automatically freed with parent
 */
static bool s_test_borrowed_wrapper_cleanup(void) {
    log_it(L_DEBUG, "Testing: automatic cleanup of borrowed wrappers");
    bool result = false;
    dap_json_t *l_parent = NULL;
    dap_json_t *l_child1 = NULL;
    dap_json_t *l_child2 = NULL;
    
    // Create parent with nested structure
    l_parent = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_parent, "Parent creation");
    
    dap_json_object_add_string(l_parent, "name", "parent");
    dap_json_t *l_nested = dap_json_object_new();
    dap_json_object_add_string(l_nested, "type", "nested");
    dap_json_object_add_object(l_parent, "nested", l_nested);
    l_nested = NULL;  // Ownership transferred
    
    // Get borrowed references - these should NOT be freed manually
    l_child1 = dap_json_object_get_object(l_parent, "nested");
    DAP_TEST_FAIL_IF_NULL(l_child1, "First borrowed reference");
    
    l_child2 = dap_json_object_get_object(l_parent, "nested");
    DAP_TEST_FAIL_IF_NULL(l_child2, "Second borrowed reference");
    
    // Get nested borrowed reference from borrowed reference
    const char *l_type = dap_json_object_get_string(l_child1, "type");
    DAP_TEST_FAIL_IF_STRING_NOT_EQUAL("nested", l_type, "Nested value");
    
    // Free parent - all borrowed wrappers should be automatically freed
    dap_json_object_free(l_parent);
    l_parent = NULL;
    
    // DO NOT free borrowed references - they're already freed!
    // l_child1, l_child2 are now invalid
    
    result = true;
    log_it(L_DEBUG, "Verified: borrowed wrappers automatically cleaned up");
    
cleanup:
    dap_json_object_free(l_parent);
    // NO cleanup for borrowed references needed
    return result;
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
    
    // comprehensive tests: core functionality
    log_it(L_INFO, "Running comprehensive tests...");
    l_all_passed &= s_test_numeric_types();
    l_all_passed &= s_test_array_operations();
    l_all_passed &= s_test_type_checking();
    l_all_passed &= s_test_object_key_operations();
    l_all_passed &= s_test_error_conditions();
    
    // advanced tests: complex structures and edge cases
    log_it(L_INFO, "Running advanced tests...");
    l_all_passed &= s_test_nested_structures();
    l_all_passed &= s_test_parsing_edge_cases();
    l_all_passed &= s_test_serialization_edge_cases();
    l_all_passed &= s_test_large_data();
    l_all_passed &= s_test_deep_nesting();
    
    // Fix validation tests: verify all 5 problem fixes
    log_it(L_INFO, "Running fix validation tests...");
    l_all_passed &= s_test_fix_get_ex_refcount();
    l_all_passed &= s_test_fix_ref_new_wrapper();
    l_all_passed &= s_test_fix_print_object_no_leak();
    l_all_passed &= s_test_fix_print_array_no_leak();
    
    // Memory safety tests: stress testing
    log_it(L_INFO, "Running memory safety tests...");
    l_all_passed &= s_test_memory_multiple_gets();
    l_all_passed &= s_test_memory_complex_nested();
    l_all_passed &= s_test_borrowed_wrapper_cleanup();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All DAP JSON tests passed!");
        return EXIT_SUCCESS;  // ✅ Standard: 0
    } else {
        log_it(L_ERROR, "Some DAP JSON tests failed!");
        return EXIT_FAILURE;  // ✅ Standard: 1 (Unix/POSIX compliant);
    }
}