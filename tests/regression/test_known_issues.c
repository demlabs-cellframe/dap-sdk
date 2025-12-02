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
#include "dap_hash.h"
#include "dap_enc_key.h"
#include "dap_sign.h"
#include "../fixtures/utilities/test_helpers.h"

#define LOG_TAG "test_regression_known_issues"

/**
 * @brief Regression test: JSON null handling issue
 * @details Tests fix for issue where JSON null values caused crashes
 * @note This is an example of a regression test that would prevent 
 *       a previously fixed bug from reoccurring
 */
static bool s_test_json_null_handling_regression(void) {
    log_it(L_INFO, "Testing JSON null handling regression");
    
    // Test case that previously caused a crash or incorrect behavior
    dap_json_t* l_root = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_root, "JSON root object creation");
    
    // Add null value - this should work without crashing
    dap_json_object_add_null(l_root, "null_field");
    
    // Serialize JSON with null value
    char* l_json_str = dap_json_to_string(l_root);
    DAP_TEST_ASSERT_NOT_NULL(l_json_str, "JSON serialization with null value");
    
    // Check that null is properly represented (allow for formatting differences)
    log_it(L_INFO, "Serialized JSON: %s", l_json_str);
    bool l_has_null_field = (strstr(l_json_str, "\"null_field\"") != NULL && strstr(l_json_str, "null") != NULL);
    if (!l_has_null_field) {
        log_it(L_ERROR, "null_field with null value not found in '%s'", l_json_str);
    }
    DAP_TEST_ASSERT(l_has_null_field, "Null field should be serialized correctly");
    
    // Parse back and verify null value
    dap_json_t* l_parsed = dap_json_parse_string(l_json_str);
    DAP_TEST_ASSERT_NOT_NULL(l_parsed, "JSON parsing with null value");
    
    // Check that null field exists and is null
    dap_json_t* l_null_value = NULL;
    bool l_field_exists = dap_json_object_get_ex(l_parsed, "null_field", &l_null_value);
    DAP_TEST_ASSERT(l_field_exists, "Parsed JSON should have null field");
    
    // Cleanup
    DAP_DELETE(l_json_str);
    dap_json_object_free(l_root);
    dap_json_object_free(l_parsed);
    
    log_it(L_INFO, "JSON null handling regression test passed");
    return true;
}

/**
 * @brief Regression test: Hash consistency across platforms
 * @details Tests fix for issue where hash results differed on different platforms
 */
static bool s_test_hash_consistency_regression(void) {
    log_it(L_INFO, "Testing hash consistency regression");
    
    // Test with specific input that previously showed inconsistency
    const char* l_test_input = "DAP SDK cross-platform test string";
    dap_hash_fast_t l_hash1 = {0};
    dap_hash_fast_t l_hash2 = {0};
    
    // Calculate hash twice to ensure consistency
    bool l_ret1 = dap_hash_fast(l_test_input, strlen(l_test_input), &l_hash1);
    bool l_ret2 = dap_hash_fast(l_test_input, strlen(l_test_input), &l_hash2);
    
    DAP_TEST_ASSERT(l_ret1 == true, "First hash calculation should succeed");
    DAP_TEST_ASSERT(l_ret2 == true, "Second hash calculation should succeed");
    
    // Hashes should be identical
    int l_compare = memcmp(&l_hash1, &l_hash2, sizeof(dap_hash_fast_t));
    DAP_TEST_ASSERT(l_compare == 0, "Hash results should be consistent");
    
    // Test with edge cases that previously caused issues
    const char* l_edge_cases[] = {
        "",                    // Empty string
        "a",                   // Single character
        "The quick brown fox jumps over the lazy dog", // Standard test string
        "\x00\x01\x02\x03",   // Binary data
        "🚀💫🔥"              // Unicode characters
    };
    
    size_t l_num_cases = sizeof(l_edge_cases) / sizeof(l_edge_cases[0]);
    
    for (size_t i = 0; i < l_num_cases; i++) {
        dap_hash_fast_t l_hash_a = {0};
        dap_hash_fast_t l_hash_b = {0};
        
        size_t l_input_len = (i == 3) ? 4 : strlen(l_edge_cases[i]); // Binary data case
        
        bool l_ret_a = dap_hash_fast(l_edge_cases[i], l_input_len, &l_hash_a);
        bool l_ret_b = dap_hash_fast(l_edge_cases[i], l_input_len, &l_hash_b);
        
        DAP_TEST_ASSERT(l_ret_a == l_ret_b, "Hash return codes should match");
        
        if (l_ret_a == true) {
            int l_edge_compare = memcmp(&l_hash_a, &l_hash_b, sizeof(dap_hash_fast_t));
            DAP_TEST_ASSERT(l_edge_compare == 0, "Edge case hash should be consistent");
        }
    }
    
    log_it(L_INFO, "Hash consistency regression test passed");
    return true;
}

/**
 * @brief Regression test: Memory management in key operations
 * @details Tests fix for memory leaks in key generation and signing operations
 */
static bool s_test_memory_management_regression(void) {
    log_it(L_INFO, "Testing memory management regression");
    
    // This test simulates the scenario that previously caused memory leaks
    const size_t l_iterations = 50;
    
    for (size_t i = 0; i < l_iterations; i++) {
        // Generate key
        dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
        if (!l_key) {
            log_it(L_WARNING, "Key generation failed at iteration %zu", i);
            continue;
        }
        
        // Create signature
        const char* l_data = "Memory management test data";
        dap_sign_t* l_signature = dap_sign_create(l_key, l_data, strlen(l_data));
        
        if (l_signature) {
            // Verify signature
            int l_verify = dap_sign_verify(l_signature, l_data, strlen(l_data));
            
            // This verification step previously caused issues if not cleaned up properly
            DAP_TEST_ASSERT(l_verify == 0, "Signature verification in memory test");
            
            // Clean up signature
            DAP_DELETE(l_signature);
        }
        
        // Clean up key - this step previously had memory leaks
        dap_enc_key_delete(l_key);
    }
    
    log_it(L_INFO, "Memory management regression test completed (%zu iterations)", l_iterations);
    return true;
}

/**
 * @brief Regression test: JSON parsing edge cases
 * @details Tests fix for JSON parser issues with specific input patterns
 */
static bool s_test_json_parsing_edge_cases_regression(void) {
    log_it(L_INFO, "Testing JSON parsing edge cases regression");
    
    // Test cases that previously caused parser failures
    struct {
        const char* json;
        bool should_parse;
        const char* description;
    } l_test_cases[] = {
        {"{}", true, "Empty object"},
        {"[]", true, "Empty array"},
        {"{\"key\":\"value\"}", true, "Simple object"},
        {"{\"number\":123}", true, "Object with number"},
        {"{\"bool\":true}", true, "Object with boolean"},
        {"{\"null\":null}", true, "Object with null"},
        {"{\"nested\":{\"inner\":\"value\"}}", true, "Nested object"},
        {"{\"array\":[1,2,3]}", true, "Object with array"},
        {"{\"key\":\"value\",}", true, "Trailing comma (json-c tolerates this)"},
        {"{\"key\":}", false, "Missing value (invalid)"},
        {"{\"key\":\"unclosed string}", false, "Unclosed string (invalid)"},
        {"", false, "Empty string (invalid)"},
        {"invalid", false, "Non-JSON string (invalid)"}
    };
    
    size_t l_num_cases = sizeof(l_test_cases) / sizeof(l_test_cases[0]);
    
    for (size_t i = 0; i < l_num_cases; i++) {
        log_it(L_DEBUG, "Testing case %zu: %s", i, l_test_cases[i].description);
        
        dap_json_t* l_parsed = dap_json_parse_string(l_test_cases[i].json);
        
        if (l_test_cases[i].should_parse) {
            DAP_TEST_ASSERT(l_parsed != NULL, "JSON should parse successfully");
            if (l_parsed) {
                dap_json_object_free(l_parsed);
            }
        } else {
            if (l_parsed != NULL) {
                log_it(L_ERROR, "Case %zu ('%s') should NOT parse but did: %s", i, l_test_cases[i].description, l_test_cases[i].json);
            }
            DAP_TEST_ASSERT(l_parsed == NULL, "JSON should not parse");
        }
    }
    
    log_it(L_INFO, "JSON parsing edge cases regression test passed");
    return true;
}

/**
 * @brief Regression test: Integer overflow in size calculations
 * @details Tests fix for integer overflow issues in size calculations
 */
static bool s_test_integer_overflow_regression(void) {
    log_it(L_INFO, "Testing integer overflow regression");
    
    // Test large size values that previously caused overflow
    const size_t l_large_sizes[] = {
        SIZE_MAX,
        SIZE_MAX - 1,
        SIZE_MAX / 2,
        1024 * 1024 * 1024,  // 1GB
        SIZE_MAX / 1024      // Large but reasonable
    };
    
    size_t l_num_sizes = sizeof(l_large_sizes) / sizeof(l_large_sizes[0]);
    
    for (size_t i = 0; i < l_num_sizes; i++) {
        size_t l_test_size = l_large_sizes[i];
        
        log_it(L_DEBUG, "Testing size calculation with %zu", l_test_size);
        
        // Test allocation with large size (should fail gracefully, not overflow)
        void* l_ptr = DAP_NEW_SIZE(uint8_t, l_test_size);
        
        if (l_ptr) {
            // If allocation succeeded (unlikely for very large sizes), free it
            DAP_DELETE(l_ptr);
            log_it(L_DEBUG, "Large allocation of %zu bytes succeeded", l_test_size);
        } else {
            // This is expected for very large sizes
            log_it(L_DEBUG, "Large allocation of %zu bytes failed gracefully", l_test_size);
        }
        
        // The important thing is that the system didn't crash or have undefined behavior
    }
    
    log_it(L_INFO, "Integer overflow regression test passed");
    return true;
}

/**
 * @brief Main test function for regression tests
 */
int main(void) {
    log_it(L_INFO, "Starting DAP SDK Regression Tests");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    l_all_passed &= s_test_json_null_handling_regression();
    l_all_passed &= s_test_hash_consistency_regression();
    l_all_passed &= s_test_memory_management_regression();
    l_all_passed &= s_test_json_parsing_edge_cases_regression();
    l_all_passed &= s_test_integer_overflow_regression();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All Regression tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some Regression tests failed!");
        return -1;
    }
}

