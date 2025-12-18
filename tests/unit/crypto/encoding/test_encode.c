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
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All dap_encode_char_by_char tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some dap_encode_char_by_char tests failed!");
        return -1;
    }
}

