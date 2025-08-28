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
 * @brief Main test function
 */
int main(void) {
    log_it(L_INFO, "Starting DAP JSON Unit Tests");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    l_all_passed &= s_test_json_object_creation();
    l_all_passed &= s_test_json_array_creation();
    l_all_passed &= s_test_json_string_operations();
    l_all_passed &= s_test_json_parsing();
    l_all_passed &= s_test_json_serialization();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All DAP JSON tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some DAP JSON tests failed!");
        return -1;
    }
}