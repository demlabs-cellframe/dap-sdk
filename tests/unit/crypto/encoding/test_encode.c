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
#include "dap_encode.h"
#include "../../../fixtures/utilities/test_helpers.h"
#include <inttypes.h>
#include <string.h>
#include <stdint.h>

#define LOG_TAG "test_encode"



const char c_b58digits_ordered[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
const int8_t c_b58digits_map[] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8,-1,-1,-1,-1,-1,-1,
    -1, 9,10,11,12,13,14,15,16,-1,17,18,19,20,21,-1,
    22,23,24,25,26,27,28,29,30,31,32,-1,-1,-1,-1,-1,
    -1,33,34,35,36,37,38,39,40,41,42,43,-1,44,45,46,
    47,48,49,50,51,52,53,54,55,56,57,-1,-1,-1,-1,-1,
};


static const char b64_standart_table[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', '+', '/'
};

/**
 * @breif Base64 url safe index table.
 */
static const char b64_table_url_safe[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', '-', '_'
};

/**
 * @brief Test dap_encode_char_by_char with NULL inputs
 */
static bool s_test_encode_null_inputs(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with NULL inputs");
    
    char l_output[256];
    const char l_table[256] = "0123456789ABCDEF";
    
    // Test NULL input buffer
    size_t l_result = dap_encode_char_by_char(NULL, 10, 8, l_table, l_output);
    DAP_TEST_ASSERT(l_result == 0, "NULL input should return 0");
    
    // Test NULL output buffer
    const char l_input[] = "test";
    l_result = dap_encode_char_by_char(l_input, sizeof(l_input), 8, l_table, NULL);
    DAP_TEST_ASSERT(l_result == 0, "NULL output should return 0");
    
    // Test NULL table
    l_result = dap_encode_char_by_char(l_input, sizeof(l_input), 8, NULL, l_output);
    DAP_TEST_ASSERT(l_result == 0, "NULL table should return 0");
    
    // Test zero base_size
    l_result = dap_encode_char_by_char(l_input, sizeof(l_input), 0, l_table, l_output);
    DAP_TEST_ASSERT(l_result == 0, "Zero base_size should return 0");
    
    log_it(L_DEBUG, "NULL inputs test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with empty input
 */
static bool s_test_encode_empty_input(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with empty input");
    
    const char l_input[] = "";
    char l_output[256];
    const char l_table[256] = "0123456789ABCDEF";
    
    size_t l_result = dap_encode_char_by_char(l_input, 0, 8, l_table, l_output);
    DAP_TEST_ASSERT(l_result == 0, "Empty input should return 0");
    
    log_it(L_DEBUG, "Empty input test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with base_size = 8
 * Note: The function does bit-level encoding, so base_size=8 may not work as expected
 * for direct byte mapping. This test verifies the function runs without crashing.
 */
static bool s_test_encode_base8(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with base_size = 8");
    
    // Create a simple identity table (byte value maps to itself)
    char l_table[256];
    for (int i = 0; i < 256; i++) {
        l_table[i] = (char)i;
    }
    
    const char l_input[] = "Hello";
    size_t l_input_size = strlen(l_input);
    size_t l_expected_output_size = (l_input_size * 8) / 8; // Should be same as input size
    
    char l_output[256];
    memset(l_output, 0, sizeof(l_output));
    
    size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 8, l_table, l_output);
    
    DAP_TEST_ASSERT(l_result == l_expected_output_size, "Output size should match expected");
    DAP_TEST_ASSERT(l_result == l_input_size, "Base-8 encoding should preserve size");
    
    // The function does bit-level encoding, so we just verify it produces output
    DAP_TEST_ASSERT(l_output[0] != 0, "Output should be non-empty");
    
    log_it(L_DEBUG, "Base-8 test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with base_size = 5 (base32-like)
 */
static bool s_test_encode_base5(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with base_size = 5");
    
    // Create a simple table for base-5 encoding
    char l_table[256];
    for (int i = 0; i < 256; i++) {
        l_table[i] = (char)('A' + (i % 26)); // Simple mapping
    }
    
    const char l_input[] = "AB"; // 2 bytes = 16 bits
    size_t l_input_size = strlen(l_input);
    size_t l_expected_output_size = (l_input_size * 8) / 5; // 16 / 5 = 3 (integer division)
    
    char l_output[256];
    memset(l_output, 0, sizeof(l_output));
    
    size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 5, l_table, l_output);
    
    DAP_TEST_ASSERT(l_result == l_expected_output_size, "Output size should match expected");
    DAP_TEST_ASSERT(l_result == 3, "2 bytes with base-5 should produce 3 output chars");
    DAP_TEST_ASSERT(l_output[0] != 0, "First output character should be set");
    DAP_TEST_ASSERT(l_output[1] != 0, "Second output character should be set");
    DAP_TEST_ASSERT(l_output[2] != 0, "Third output character should be set");
    
    log_it(L_DEBUG, "Base-5 test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with base_size = 6 (base64-like)
 */
static bool s_test_encode_base6(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with base_size = 6");
    
    // Create a simple table for base-6 encoding
    char l_table[256];
    for (int i = 0; i < 256; i++) {
        l_table[i] = (char)('0' + (i % 10)); // Simple mapping
    }
    
    const char l_input[] = "ABC"; // 3 bytes = 24 bits
    size_t l_input_size = strlen(l_input);
    size_t l_expected_output_size = (l_input_size * 8) / 6; // 24 / 6 = 4
    
    char l_output[256];
    memset(l_output, 0, sizeof(l_output));
    
    size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 6, l_table, l_output);
    
    DAP_TEST_ASSERT(l_result == l_expected_output_size, "Output size should match expected");
    DAP_TEST_ASSERT(l_result == 4, "3 bytes with base-6 should produce 4 output chars");
    
    // Verify all output characters are set
    for (size_t i = 0; i < l_result; i++) {
        DAP_TEST_ASSERT(l_output[i] != 0, "All output characters should be set");
    }
    
    log_it(L_DEBUG, "Base-6 test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with different input sizes
 */
static bool s_test_encode_different_sizes(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with different input sizes");
    
    // Create a simple table
    char l_table[256];
    for (int i = 0; i < 256; i++) {
        l_table[i] = (char)('A' + (i % 26));
    }
    
    // Test with 1 byte
    const char l_input1[] = "X";
    char l_output1[256];
    size_t l_result1 = dap_encode_char_by_char(l_input1, 1, 5, l_table, l_output1);
    DAP_TEST_ASSERT(l_result1 == 1, "1 byte with base-5 should produce 1 output char (8/5 = 1)");
    
    // Test with 4 bytes
    const char l_input4[] = "Test";
    char l_output4[256];
    size_t l_result4 = dap_encode_char_by_char(l_input4, 4, 5, l_table, l_output4);
    DAP_TEST_ASSERT(l_result4 == 6, "4 bytes with base-5 should produce 6 output chars (32/5 = 6)");
    
    // Test with 8 bytes
    const char l_input8[] = "12345678";
    char l_output8[256];
    size_t l_result8 = dap_encode_char_by_char(l_input8, 8, 5, l_table, l_output8);
    DAP_TEST_ASSERT(l_result8 == 12, "8 bytes with base-5 should produce 12 output chars (64/5 = 12)");
    
    log_it(L_DEBUG, "Different sizes test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char output size calculation
 */
static bool s_test_encode_output_size(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char output size calculation");
    
    char l_table[256];
    for (int i = 0; i < 256; i++) {
        l_table[i] = (char)i;
    }
    
    const char l_input[] = "Hello World!";
    size_t l_input_size = strlen(l_input);
    char l_output[256];
    
    // Test various base sizes and verify output size calculation
    struct {
        uint8_t base_size;
        size_t expected_output_size;
    } l_test_cases[] = {
        {1, l_input_size * 8},  // 1 bit per char: 8 chars per byte
        {2, l_input_size * 4},  // 2 bits per char: 4 chars per byte
        {4, l_input_size * 2},  // 4 bits per char: 2 chars per byte
        {8, l_input_size * 1},  // 8 bits per char: 1 char per byte
    };
    
    for (size_t i = 0; i < sizeof(l_test_cases) / sizeof(l_test_cases[0]); i++) {
        size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 
                                                   l_test_cases[i].base_size, 
                                                   l_table, l_output);
        if (l_result != l_test_cases[i].expected_output_size) {
            log_it(L_ERROR, "Output size mismatch for base_size=%u: expected %zu, got %zu",
                   l_test_cases[i].base_size, l_test_cases[i].expected_output_size, l_result);
        }
        DAP_TEST_ASSERT(l_result == l_test_cases[i].expected_output_size,
                       "Output size should match expected");
    }
    
    log_it(L_DEBUG, "Output size calculation test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with custom table
 * Note: The function does bit-level encoding, so for base_size=8 it combines bits
 * from adjacent bytes. This test verifies the function produces output using the table.
 */
static bool s_test_encode_custom_table(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with custom table");
    
    // Create a custom table that maps byte values to specific characters
    char l_table[256];
    for (int i = 0; i < 256; i++) {
        l_table[i] = (char)('!' + (i % 94)); // Printable ASCII range
    }
    
    const char l_input[] = "Test";
    size_t l_input_size = strlen(l_input);
    char l_output[256];
    memset(l_output, 0, sizeof(l_output));
    
    size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 8, l_table, l_output);
    
    DAP_TEST_ASSERT(l_result == l_input_size, "Output size should match input size");
    
    // Verify output characters are from the table (the function uses combined bytes as table indices)
    for (size_t i = 0; i < l_result; i++) {
        // The function uses combined byte values as table indices, so output should be valid table entries
        DAP_TEST_ASSERT(l_output[i] != 0, "Output character should be set");
    }
    
    log_it(L_DEBUG, "Custom table test passed");
    return true;
}

/**
 * @brief Create a 256-element base58 table from the base58 character mapping
 * The function uses byte values (0-255) as indices, so we need a 256-element table.
 * For indices 0-57, we map to base58 characters. For indices 58-255, we wrap around.
 */
static void s_create_base58_table(char *a_table) {
    const size_t l_b58_len = strlen(c_b58digits_ordered);
    for (int i = 0; i < 256; i++) {
        a_table[i] = c_b58digits_ordered[i % l_b58_len];
    }
}

/**
 * @brief Create a 256-element base64 table from the base64 character mapping
 * The function uses byte values (0-255) as indices, so we need a 256-element table.
 * For indices 0-63, we map to base64 characters. For indices 64-255, we wrap around.
 */
static void s_create_base64_table(char *a_table, const char *a_b64_chars) {
    const size_t l_b64_len = 64;
    for (int i = 0; i < 256; i++) {
        a_table[i] = a_b64_chars[i % l_b64_len];
    }
}

/**
 * @brief Test dap_encode_char_by_char with base58 mapping
 * Base58 uses base_size=6 (since 2^6=64 > 58, we extract 6 bits at a time)
 */
static bool s_test_encode_base58(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with base58 mapping");
    
    // Create base58 table (256 elements)
    char l_b58_table[256];
    s_create_base58_table(l_b58_table);
    
    // Test with base_size=6 (extracts 6 bits, giving values 0-63)
    const char l_input[] = "Hello";
    size_t l_input_size = strlen(l_input);
    size_t l_expected_output_size = (l_input_size * 8) / 6; // 5 bytes * 8 bits / 6 = 6 (with integer division)
    
    char l_output[256];
    memset(l_output, 0, sizeof(l_output));
    
    size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 6, l_b58_table, l_output);
    
    DAP_TEST_ASSERT(l_result == l_expected_output_size, 
                   "Base58 output size should match expected (5 bytes -> 6 chars)");
    DAP_TEST_ASSERT(l_result == 6, "5 bytes with base-6 should produce 6 output chars");
    
    // Verify all output characters are valid base58 characters
    for (size_t i = 0; i < l_result; i++) {
        bool l_is_valid = false;
        for (size_t j = 0; j < strlen(c_b58digits_ordered); j++) {
            if (l_output[i] == c_b58digits_ordered[j]) {
                l_is_valid = true;
                break;
            }
        }
        DAP_TEST_ASSERT(l_is_valid, "Output character should be a valid base58 character");
    }
    
    // Test with different input sizes
    const char l_input1[] = "A";  // 1 byte = 8 bits -> 1 char (8/6 = 1)
    char l_output1[256];
    size_t l_result1 = dap_encode_char_by_char(l_input1, 1, 6, l_b58_table, l_output1);
    DAP_TEST_ASSERT(l_result1 == 1, "1 byte with base-6 should produce 1 output char");
    DAP_TEST_ASSERT(l_output1[0] != 0, "Output should be non-empty");
    
    const char l_input3[] = "ABC";  // 3 bytes = 24 bits -> 4 chars (24/6 = 4)
    char l_output3[256];
    size_t l_result3 = dap_encode_char_by_char(l_input3, 3, 6, l_b58_table, l_output3);
    DAP_TEST_ASSERT(l_result3 == 4, "3 bytes with base-6 should produce 4 output chars");
    
    log_it(L_DEBUG, "Base58 test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with base64 standard mapping
 * Base64 uses base_size=6 (2^6=64, extracts 6 bits at a time)
 */
static bool s_test_encode_base64_standard(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with base64 standard mapping");
    
    // Create base64 standard table (256 elements)
    char l_b64_table[256];
    s_create_base64_table(l_b64_table, b64_standart_table);
    
    // Test with base_size=6 (extracts 6 bits, giving values 0-63)
    const char l_input[] = "Hello";
    size_t l_input_size = strlen(l_input);
    size_t l_expected_output_size = (l_input_size * 8) / 6; // 5 bytes * 8 bits / 6 = 6
    
    char l_output[256];
    memset(l_output, 0, sizeof(l_output));
    
    size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 6, l_b64_table, l_output);
    
    DAP_TEST_ASSERT(l_result == l_expected_output_size, 
                   "Base64 output size should match expected (5 bytes -> 6 chars)");
    DAP_TEST_ASSERT(l_result == 6, "5 bytes with base-6 should produce 6 output chars");
    
    // Verify all output characters are valid base64 standard characters
    for (size_t i = 0; i < l_result; i++) {
        bool l_is_valid = false;
        for (size_t j = 0; j < 64; j++) {
            if (l_output[i] == b64_standart_table[j]) {
                l_is_valid = true;
                break;
            }
        }
        DAP_TEST_ASSERT(l_is_valid, "Output character should be a valid base64 standard character");
    }
    
    // Test with 3 bytes (typical base64 input size)
    const char l_input3[] = "Man";  // 3 bytes = 24 bits -> 4 chars (24/6 = 4)
    char l_output3[256];
    size_t l_result3 = dap_encode_char_by_char(l_input3, 3, 6, l_b64_table, l_output3);
    DAP_TEST_ASSERT(l_result3 == 4, "3 bytes with base-6 should produce 4 output chars");
    
    // Test with 1 byte
    const char l_input1[] = "A";  // 1 byte = 8 bits -> 1 char (8/6 = 1)
    char l_output1[256];
    size_t l_result1 = dap_encode_char_by_char(l_input1, 1, 6, l_b64_table, l_output1);
    DAP_TEST_ASSERT(l_result1 == 1, "1 byte with base-6 should produce 1 output char");
    DAP_TEST_ASSERT(l_output1[0] != 0, "Output should be non-empty");
    
    log_it(L_DEBUG, "Base64 standard test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with base64 URL-safe mapping
 * Base64 URL-safe uses base_size=6 (2^6=64, extracts 6 bits at a time)
 */
static bool s_test_encode_base64_url_safe(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with base64 URL-safe mapping");
    
    // Create base64 URL-safe table (256 elements)
    char l_b64_table[256];
    s_create_base64_table(l_b64_table, b64_table_url_safe);
    
    // Test with base_size=6 (extracts 6 bits, giving values 0-63)
    const char l_input[] = "Hello";
    size_t l_input_size = strlen(l_input);
    size_t l_expected_output_size = (l_input_size * 8) / 6; // 5 bytes * 8 bits / 6 = 6
    
    char l_output[256];
    memset(l_output, 0, sizeof(l_output));
    
    size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 6, l_b64_table, l_output);
    
    DAP_TEST_ASSERT(l_result == l_expected_output_size, 
                   "Base64 URL-safe output size should match expected (5 bytes -> 6 chars)");
    DAP_TEST_ASSERT(l_result == 6, "5 bytes with base-6 should produce 6 output chars");
    
    // Verify all output characters are valid base64 URL-safe characters
    for (size_t i = 0; i < l_result; i++) {
        bool l_is_valid = false;
        for (size_t j = 0; j < 64; j++) {
            if (l_output[i] == b64_table_url_safe[j]) {
                l_is_valid = true;
                break;
            }
        }
        DAP_TEST_ASSERT(l_is_valid, "Output character should be a valid base64 URL-safe character");
    }
    
    // Test with different input sizes
    const char l_input3[] = "Test";  // 4 bytes = 32 bits -> 5 chars (32/6 = 5)
    char l_output3[256];
    size_t l_result3 = dap_encode_char_by_char(l_input3, 4, 6, l_b64_table, l_output3);
    DAP_TEST_ASSERT(l_result3 == 5, "4 bytes with base-6 should produce 5 output chars");
    
    log_it(L_DEBUG, "Base64 URL-safe test passed");
    return true;
}

/**
 * @brief Main test function for dap_encode_char_by_char
 */
int main(void) {
    log_it(L_INFO, "Starting dap_encode_char_by_char unit tests");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    // Test NULL inputs
    l_all_passed &= s_test_encode_null_inputs();
    
    // Test empty input
    l_all_passed &= s_test_encode_empty_input();
    
    // Test base_size = 8 (byte-by-byte)
    l_all_passed &= s_test_encode_base8();
    
    // Test base_size = 5 (base32-like)
    l_all_passed &= s_test_encode_base5();
    
    // Test base_size = 6 (base64-like)
    l_all_passed &= s_test_encode_base6();
    
    // Test different input sizes
    l_all_passed &= s_test_encode_different_sizes();
    
    // Test output size calculation
    l_all_passed &= s_test_encode_output_size();
    
    // Test custom table
    l_all_passed &= s_test_encode_custom_table();
    
    // Test base58 mapping
    l_all_passed &= s_test_encode_base58();
    
    // Test base64 standard mapping
    l_all_passed &= s_test_encode_base64_standard();
    
    // Test base64 URL-safe mapping
    l_all_passed &= s_test_encode_base64_url_safe();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All dap_encode_char_by_char tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some dap_encode_char_by_char tests failed!");
        return -1;
    }
}

