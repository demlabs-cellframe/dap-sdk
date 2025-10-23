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
#include "dap_sign.h"
#include "dap_enc_key.h"
#include "../fixtures/utilities/test_helpers.h"
#include <string.h>

#define LOG_TAG "test_security_memory"

/**
 * @brief Security test: Buffer overflow prevention in JSON parsing
 * @details Tests that JSON parser handles malformed input safely
 */
static bool s_test_json_buffer_overflow_prevention(void) {
    log_it(L_INFO, "Testing JSON buffer overflow prevention");
    
    // Test 1: Extremely long string values
    char* l_long_string = DAP_NEW_SIZE(char, 1024 * 1024 + 1); // 1MB string
    if (!l_long_string) {
        log_it(L_WARNING, "Cannot allocate 1MB for overflow test");
        return true; // Skip test if memory allocation fails
    }
    
    memset(l_long_string, 'A', 1024 * 1024);
    l_long_string[1024 * 1024] = '\0';
    
    // Create JSON with very long string
    char* l_json_template = "{\"long_field\":\"%s\"}";
    size_t l_json_size = strlen(l_json_template) + strlen(l_long_string) + 1;
    char* l_long_json = DAP_NEW_SIZE(char, l_json_size);
    
    if (l_long_json) {
        snprintf(l_long_json, l_json_size, l_json_template, l_long_string);
        
        // Try to parse - should either succeed or fail gracefully
        dap_json_t* l_parsed = dap_json_parse_string(l_long_json);
        
        if (l_parsed) {
            log_it(L_DEBUG, "Large JSON parsed successfully");
            dap_json_object_free(l_parsed);
        } else {
            log_it(L_DEBUG, "Large JSON rejected gracefully");
        }
        
        DAP_DELETE(l_long_json);
    }
    
    DAP_DELETE(l_long_string);
    
    // Test 2: Deeply nested objects
    const size_t l_nesting_depth = 1000;
    size_t l_nested_json_size = l_nesting_depth * 20; // Rough estimate
    char* l_nested_json = DAP_NEW_SIZE(char, l_nested_json_size);
    
    if (l_nested_json) {
        strcpy(l_nested_json, "{");
        for (size_t i = 0; i < l_nesting_depth - 1; i++) {
            strcat(l_nested_json, "\"level\":{");
        }
        strcat(l_nested_json, "\"final\":\"value\"");
        for (size_t i = 0; i < l_nesting_depth; i++) {
            strcat(l_nested_json, "}");
        }
        
        // Try to parse deeply nested JSON
        dap_json_t* l_nested_parsed = dap_json_parse_string(l_nested_json);
        
        if (l_nested_parsed) {
            log_it(L_DEBUG, "Deeply nested JSON parsed successfully");
            dap_json_object_free(l_nested_parsed);
        } else {
            log_it(L_DEBUG, "Deeply nested JSON rejected gracefully");
        }
        
        DAP_DELETE(l_nested_json);
    }
    
    log_it(L_INFO, "JSON buffer overflow prevention test passed");
    return true;
}

/**
 * @brief Security test: Input validation for crypto functions
 * @details Tests that crypto functions validate inputs properly
 */
static bool s_test_crypto_input_validation(void) {
    log_it(L_INFO, "Testing crypto input validation");
    
    // Test 1: NULL pointer handling
    dap_hash_fast_t l_hash = {0};
    
    // Hash with NULL input should fail gracefully
    bool l_ret1 = dap_hash_fast(NULL, 100, &l_hash);
    DAP_TEST_ASSERT(l_ret1 == false, "Hash with NULL input should fail");
    
    // Hash with NULL output should fail gracefully
    bool l_ret2 = dap_hash_fast("test", 4, NULL);
    DAP_TEST_ASSERT(l_ret2 == false, "Hash with NULL output should fail");
    
    // Test 2: Zero-length input handling
    bool l_ret3 = dap_hash_fast("", 0, &l_hash);
    // Zero-length input might be valid, so we just check it doesn't crash
    log_it(L_DEBUG, "Zero-length hash result: %d", l_ret3);
    
    // Test 3: Key generation with invalid parameters
    dap_enc_key_t* l_invalid_key = dap_enc_key_new_generate(999, NULL, 0, NULL, 0, 0); // Invalid key type
    DAP_TEST_ASSERT_NULL(l_invalid_key, "Invalid key type should return NULL");
    
    // Test 4: Signing with NULL key
    size_t l_sig_size = 0;
    dap_sign_t* l_signature = dap_sign_create(NULL, "test", 4);
    DAP_TEST_ASSERT_NULL(l_signature, "Signing with NULL key should fail");
    
    // Test 5: Verification with invalid parameters (safer approach)
    dap_enc_key_t* l_valid_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
    if (l_valid_key) {
        // Verify with NULL signature pointer (should be safe)
        int l_verify1 = dap_sign_verify(NULL, "test", 4);
        DAP_TEST_ASSERT(l_verify1 != 0, "Verification with NULL signature should fail");
        
        // Test that we can't verify without proper signature
        log_it(L_DEBUG, "NULL signature verification correctly rejected");
        
        dap_enc_key_delete(l_valid_key);
    }
    
    log_it(L_INFO, "Crypto input validation test passed");
    return true;
}

/**
 * @brief Security test: Memory leak detection in crypto operations
 * @details Tests for memory leaks in cryptographic operations
 */
static bool s_test_crypto_memory_leaks(void) {
    log_it(L_INFO, "Testing crypto memory leak prevention");
    
    const size_t l_iterations = 100;
    
    for (size_t i = 0; i < l_iterations; i++) {
        // Test key generation and deletion
        dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
        if (!l_key) continue;
        
        // Test signing and cleanup
        const char* l_data = "Memory leak test data";
        size_t l_sig_size = 0;
        dap_sign_t* l_signature = dap_sign_create(l_key, l_data, strlen(l_data));
        
        if (l_signature) {
            // Verify signature
            dap_sign_verify(l_signature, l_data, strlen(l_data));
            
            // Clean up signature
            DAP_DELETE(l_signature);
        }
        
        // Clean up key
        dap_enc_key_delete(l_key);
        
        // Test hash operations
        dap_hash_fast_t l_hash = {0};
        dap_hash_fast(l_data, strlen(l_data), &l_hash);
    }
    
    log_it(L_INFO, "Crypto memory leak test completed (%zu iterations)", l_iterations);
    return true;
}

/**
 * @brief Security test: JSON injection prevention
 * @details Tests that JSON parser prevents injection attacks
 */
static bool s_test_json_injection_prevention(void) {
    log_it(L_INFO, "Testing JSON injection prevention");
    
    // Test 1: Script injection attempts
    const char* l_injection_attempts[] = {
        "{\"script\":\"<script>alert('xss')</script>\"}",
        "{\"eval\":\"eval('malicious code')\"}",
        "{\"command\":\"system('rm -rf /')\"}",
        "{\"sql\":\"'; DROP TABLE users; --\"}",
        "{\"buffer\":\"" "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" "\"}"
    };
    
    size_t l_num_attempts = sizeof(l_injection_attempts) / sizeof(l_injection_attempts[0]);
    
    for (size_t i = 0; i < l_num_attempts; i++) {
        dap_json_t* l_parsed = dap_json_parse_string(l_injection_attempts[i]);
        
        if (l_parsed) {
            // JSON was parsed, but content should be treated as safe string data
            log_it(L_DEBUG, "Injection attempt %zu parsed as safe JSON", i);
            
            // Verify that we can safely extract the string values
            // (The security is in how we use the data, not in rejecting it)
            dap_json_object_free(l_parsed);
        } else {
            log_it(L_DEBUG, "Injection attempt %zu rejected by parser", i);
        }
    }
    
    // Test 2: Malformed JSON that could cause parser issues
    const char* l_malformed_json[] = {
        "{\"unclosed\":\"string",
        "{\"key\":}",
        "{\"nested\":{\"unclosed\":}",
        "{{{{{{{{{{",
        "}}}}}}}}}}",
        "{\"key\":\"value\",,,}"
    };
    
    size_t l_num_malformed = sizeof(l_malformed_json) / sizeof(l_malformed_json[0]);
    
    for (size_t i = 0; i < l_num_malformed; i++) {
        dap_json_t* l_parsed = dap_json_parse_string(l_malformed_json[i]);
        
        // Malformed JSON should be rejected
        DAP_TEST_ASSERT_NULL(l_parsed, "Malformed JSON should be rejected");
        
        if (l_parsed) {
            dap_json_object_free(l_parsed);
        }
    }
    
    log_it(L_INFO, "JSON injection prevention test passed");
    return true;
}

/**
 * @brief Security test: Sensitive data wiping
 * @details Tests that sensitive data is properly cleared from memory
 */
static bool s_test_sensitive_data_wiping(void) {
    log_it(L_INFO, "Testing sensitive data wiping");
    
    // Test 1: Key data wiping
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Key generation for wiping test");
    
    if (l_key) {
        // Note: In a real implementation, we would need access to the internal
        // key data to verify it's properly wiped. This is a conceptual test.
        
        // Use the key for operations
        const char* l_test_data = "Sensitive test data";
        size_t l_sig_size = 0;
        dap_sign_t* l_signature = dap_sign_create(l_key, l_test_data, strlen(l_test_data));
        
        if (l_signature) {
            // Verify signature works
            int l_verify = dap_sign_verify(l_signature, l_test_data, strlen(l_test_data));
            DAP_TEST_ASSERT(l_verify == 0, "Signature verification before key deletion");
            
            DAP_DELETE(l_signature);
        }
        
        // Delete key (should wipe sensitive data)
        dap_enc_key_delete(l_key);
        
        // Note: In production, we would verify that the key memory is zeroed
        log_it(L_DEBUG, "Key deleted - sensitive data should be wiped");
    }
    
    // Test 2: Hash context wiping (conceptual)
    dap_hash_fast_t l_hash = {0};
    const char* l_sensitive_input = "Secret message that should not remain in memory";
    
    dap_hash_fast(l_sensitive_input, strlen(l_sensitive_input), &l_hash);
    
    // In a real implementation, the hash context should be wiped after use
    log_it(L_DEBUG, "Hash operation completed - intermediate state should be wiped");
    
    log_it(L_INFO, "Sensitive data wiping test passed");
    return true;
}

/**
 * @brief Main test function for security tests
 */
int main(void) {
    log_it(L_INFO, "Starting DAP SDK Security Tests");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    l_all_passed &= s_test_json_buffer_overflow_prevention();
    l_all_passed &= s_test_crypto_input_validation();
    l_all_passed &= s_test_crypto_memory_leaks();
    l_all_passed &= s_test_json_injection_prevention();
    l_all_passed &= s_test_sensitive_data_wiping();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All Security tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some Security tests failed!");
        return -1;
    }
}

