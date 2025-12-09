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
#include "dap_enc_base58.h"
#include "../../../fixtures/utilities/test_helpers.h"
#include "../../../fixtures/json_samples.h"
#include <inttypes.h>
#include <string.h>
#include <strings.h>

#define LOG_TAG "test_base58"

// Test cases based on base58_encode_decode.json structure, adapted for base58
// Format: [hex_string, base58_string]
typedef struct {
    const char* hex_input;
    const char* base58_expected;
} base58_test_case_t;


// Test cases - based on base58_encode_decode.json from Bitcoin Core tests
static const base58_test_case_t s_base58_test_cases[] = {
    {"", ""},
    {"61", "2g"},
    {"626262", "a3gV"},
    {"636363", "aPEr"},
    {"73696d706c792061206c6f6e6720737472696e67", "2cFupjhnEsSn59qHXstmK2ffpLv2"},
    {"00eb15231dfceb60925886b67d065299925915aeb172c06647", "1NS17iag9jJgTHD1VXjvLCEnZuQ3rJDE9L"},
    {"516b6fcd0f", "ABnLTmg"},
    {"bf4f89001e670274dd", "3SEo3LWLoPntC"},
    {"572e4794", "3EFU7m"},
    {"ecac89cad93923c02321", "EJDM8drfXA6uyA"},
    {"10c8511e", "Rt5zm"},
    {"00000000000000000000", "1111111111"},
    {"00000000000000000000000000000000000000000000000000000000000000000000000000000000", "1111111111111111111111111111111111111111"},
    {"00000000000000000000000000000000000000000000000000000000000000000000000000000001", "1111111111111111111111111111111111111112"},
};


/**
 * @brief Parse hex string to binary data
 * @param hex_str Hex string to parse
 * @param out Output buffer
 * @param out_size Output buffer size
 * @return Number of bytes parsed, or 0 on error
 */
static size_t s_parse_hex(const char* hex_str, uint8_t* out, size_t out_size) {
    if (!hex_str || !out) {
        return 0;
    }
    
    size_t hex_len = strlen(hex_str);
    if (hex_len == 0) {
        return 0;
    }
    
    // Hex string must have even length
    if (hex_len % 2 != 0) {
        return 0;
    }
    f
    size_t bytes_needed = hex_len / 2;
    if (bytes_needed > out_size) {
        return 0;
    }
    
    size_t hex_result = dap_hex2bin(out, hex_str, hex_len);
    // dap_hex2bin returns hex string length, but we need bytes decoded
    return bytes_needed;
}



/**
 * @brief Test Base58 basic functionality
 */
static bool s_test_base58_basic(void) {
    log_it(L_DEBUG, "Testing Base58 basic functionality");
    
    const char* l_input = CRYPTO_SAMPLE_HASH_INPUT;
    size_t l_input_size = strlen(l_input); 
    
    // Calculate encode size
    size_t l_encode_size = DAP_ENC_BASE58_ENCODE_SIZE(l_input_size);
    char l_encoded[l_encode_size];
    
    // Encode
    size_t l_encoded_result = dap_enc_base58_encode(l_input, l_input_size, l_encoded);
    DAP_TEST_ASSERT(l_encoded_result > 0, "Base58 encoding should succeed");
    DAP_TEST_ASSERT(l_encoded_result <= l_encode_size, "Encoded size should not exceed calculated size");
    
    // Decode
    size_t l_decode_size = DAP_ENC_BASE58_DECODE_SIZE(l_encoded_result);
    uint8_t l_decoded[l_decode_size];
    size_t l_decoded_result = dap_enc_base58_decode(l_encoded, l_decoded);
    
    DAP_TEST_ASSERT(l_decoded_result > 0, "Base58 decoding should succeed");
    DAP_TEST_ASSERT(l_decoded_result == l_input_size, "Decoded size should match original input size");
    
    // Verify decoded data matches original
    int l_cmp = memcmp(l_input, l_decoded, l_input_size);
    DAP_TEST_ASSERT(l_cmp == 0, "Decoded data should match original input");
    
    log_it(L_DEBUG, "Base58 basic test passed");
    return true;
}

/**
 * @brief Test Base58 encoding
 */
static bool s_test_base58_encode(void) {
    log_it(L_DEBUG, "Testing Base58 encoding");
    
    size_t l_num_cases = sizeof(s_base58_test_cases) / sizeof(s_base58_test_cases[0]);
    
    for (size_t i = 0; i < l_num_cases; i++) {
        const base58_test_case_t* l_test = &s_base58_test_cases[i];
        
        // Handle empty string case
        if (strlen(l_test->hex_input) == 0) {
            size_t l_encode_size = DAP_ENC_BASE58_ENCODE_SIZE(0);
            char l_encoded[l_encode_size];
            size_t l_encoded_result = dap_enc_base58_encode(NULL, 0, l_encoded);
            
            if (strlen(l_test->base58_expected) == 0) {
                DAP_TEST_ASSERT(l_encoded_result == 0, "Empty input should produce empty encoding");
            } else {
                DAP_TEST_ASSERT(l_encoded_result > 0, "Encoding should succeed");
                DAP_TEST_ASSERT(strcmp(l_encoded, l_test->base58_expected) == 0, 
                               "Encoded result should match expected");
            }
            continue;
        }
        
        // Parse hex input
        uint8_t l_input_buf[256];
        size_t l_input_size = s_parse_hex(l_test->hex_input, l_input_buf, sizeof(l_input_buf));
        DAP_TEST_ASSERT(l_input_size > 0, "Hex parsing should succeed");
        
        // Calculate encode size and encode
        size_t l_encode_size = DAP_ENC_BASE58_ENCODE_SIZE(l_input_size);
        char l_encoded[l_encode_size];
        size_t l_encoded_result = dap_enc_base58_encode(l_input_buf, l_input_size, l_encoded);
        
        DAP_TEST_ASSERT(l_encoded_result > 0, "Base58 encoding should succeed");
        if (strcmp(l_encoded, l_test->base58_expected) != 0) {
            log_it(L_ERROR, "Base58 encoding mismatch for hex '%s': expected '%s', got '%s'", 
                   l_test->hex_input, l_test->base58_expected, l_encoded);
        }
        DAP_TEST_ASSERT(strcmp(l_encoded, l_test->base58_expected) == 0, 
                       "Encoded result should match expected base58 string");
    }
    
    log_it(L_DEBUG, "Base58 encoding test passed");
    return true;
}

/**
 * @brief Test Base58 decoding
 */
static bool s_test_base58_decode(void) {
    log_it(L_DEBUG, "Testing Base58 decoding");
    
    size_t l_num_cases = sizeof(s_base58_test_cases) / sizeof(s_base58_test_cases[0]);
    
    for (size_t i = 0; i < l_num_cases; i++) {
        const base58_test_case_t* l_test = &s_base58_test_cases[i];
        
        // Handle empty string case
        if (strlen(l_test->base58_expected) == 0) {
            uint8_t l_decoded[256];
            size_t l_decoded_result = dap_enc_base58_decode("", l_decoded);
            
            if (strlen(l_test->hex_input) == 0) {
                DAP_TEST_ASSERT(l_decoded_result == 0, "Empty base58 should decode to empty");
            }
            // Note: If hex_input is not empty but base58_expected is empty, 
            // this shouldn't happen in valid test data
            continue;
        }
        
        // Decode base58 string
        size_t l_decode_size = DAP_ENC_BASE58_DECODE_SIZE(strlen(l_test->base58_expected));
        uint8_t l_decoded[l_decode_size];
        size_t l_decoded_result = dap_enc_base58_decode(l_test->base58_expected, l_decoded);
        
        // Parse expected hex input for comparison
        if (strlen(l_test->hex_input) == 0) {
            DAP_TEST_ASSERT(l_decoded_result == 0, "Decoded result should be empty for empty hex input");
        } else {
            DAP_TEST_ASSERT(l_decoded_result > 0, "Base58 decoding should succeed");
            uint8_t l_expected_buf[256];
            size_t l_expected_size = s_parse_hex(l_test->hex_input, l_expected_buf, sizeof(l_expected_buf));
            DAP_TEST_ASSERT(l_expected_size > 0, "Hex parsing should succeed");
            DAP_TEST_ASSERT(l_decoded_result == l_expected_size, 
                           "Decoded size should match expected hex input size");
            
            // Convert decoded result to hex for comparison
            char l_decoded_hex[512];
            dap_bin2hex(l_decoded_hex, l_decoded, l_decoded_result);
            
            // Compare with expected hex (case-insensitive)
            int l_cmp = strcasecmp(l_decoded_hex, l_test->hex_input);
            if (l_cmp != 0) {
                log_it(L_ERROR, "Base58 decoding mismatch for base58 '%s': expected hex '%s', got '%s'", 
                       l_test->base58_expected, l_test->hex_input, l_decoded_hex);
            }
            DAP_TEST_ASSERT(l_cmp == 0, "Decoded hex should match expected hex input");
        }
    }
    
    log_it(L_DEBUG, "Base58 decoding test passed");
    return true;
}

/**
 * @brief Test Base58 with empty input
 */
static bool s_test_base58_empty(void) {
    log_it(L_DEBUG, "Testing Base58 with empty input");
    
    // Test with empty string
    const char* l_empty = "";
    size_t l_empty_size = 0;
    
    size_t l_encode_size = DAP_ENC_BASE58_ENCODE_SIZE(l_empty_size);
    char l_encoded[l_encode_size];
    
    size_t l_encoded_result = dap_enc_base58_encode(l_empty, l_empty_size, l_encoded);
    
    if (l_encoded_result > 0) {
        size_t l_decode_size = DAP_ENC_BASE58_DECODE_SIZE(l_encoded_result);
        uint8_t l_decoded[l_decode_size];
        size_t l_decoded_result = dap_enc_base58_decode(l_encoded, l_decoded);
        
        DAP_TEST_ASSERT(l_decoded_result == l_empty_size, "Decoded empty string should have size 0");
    }
    
    log_it(L_DEBUG, "Base58 empty input test passed");
    return true;
}


/**
 * @brief Main test function for Base58
 */
int main(void) {
    log_it(L_INFO, "Starting Base58 unit tests");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    l_all_passed &= s_test_base58_basic();
    l_all_passed &= s_test_base58_encode();
    l_all_passed &= s_test_base58_decode();
    l_all_passed &= s_test_base58_empty();
 
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All Base58 tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some Base58 tests failed!");
        return -1;
    }
}
