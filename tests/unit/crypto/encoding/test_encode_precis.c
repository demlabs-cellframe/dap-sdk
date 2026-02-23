/*
 * Authors:
 * Based on PRECIS Framework methodology for Unicode testing
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2025
 * All rights reserved.
 *
 * This file is part of DAP SDK the open source project
 *
 *    DAP SDK is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP SDK is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dap_common.h"
#include "dap_encode.h"
#include "../../../fixtures/utilities/test_helpers.h"
#include <inttypes.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define LOG_TAG "test_encode_precis"

/**
 * @brief Create a 256-element encoding table
 * The function uses byte values (0-255) as indices, so we need a 256-element table.
 */
static void s_create_encoding_table(char *a_table, const char *a_chars, size_t a_chars_len) {
    for (int i = 0; i < 256; i++) {
        a_table[i] = a_chars[i % a_chars_len];
    }
}

/**
 * @brief Test dap_encode_char_by_char with ASCII7 characters
 * Following PRECIS methodology: test systematic Unicode categories
 */
static bool s_test_encode_ascii7(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with ASCII7 characters (PRECIS methodology)");
    
    // ASCII7 range: 0x00-0x7F
    char l_table[256];
    s_create_encoding_table(l_table, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/", 64);
    
    // Test with various ASCII7 characters
    const char *l_test_cases[] = {
        "Hello",           // Basic ASCII letters
        "12345",           // ASCII digits
        "!@#$%",           // ASCII punctuation
        " \t\n\r",         // ASCII whitespace
        "\x00\x01\x02",    // ASCII control characters
        "A",               // Single ASCII character
        "",                // Empty string (edge case)
    };
    
    for (size_t i = 0; i < sizeof(l_test_cases) / sizeof(l_test_cases[0]); i++) {
        const char *l_input = l_test_cases[i];
        size_t l_input_size = strlen(l_input);
        
        if (l_input_size == 0) {
            // Skip empty string test here (handled separately)
            continue;
        }
        
        size_t l_expected_output_size = (l_input_size * 8) / 6; // base_size = 6
        char l_output[256];
        memset(l_output, 0, sizeof(l_output));
        
        size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 6, l_table, l_output);
        
        char l_msg[256];
        snprintf(l_msg, sizeof(l_msg), "ASCII7 test case %zu: output size mismatch", i);
        DAP_TEST_ASSERT(l_result == l_expected_output_size, l_msg);
        snprintf(l_msg, sizeof(l_msg), "ASCII7 test case %zu: should produce output", i);
        DAP_TEST_ASSERT(l_result > 0, l_msg);
    }
    
    log_it(L_DEBUG, "ASCII7 test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with Unicode letters
 * Following PRECIS methodology: test letter/digit category
 */
static bool s_test_encode_unicode_letters(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with Unicode letters (PRECIS methodology)");
    
    char l_table[256];
    s_create_encoding_table(l_table, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/", 64);
    
    // Test with various Unicode letter categories
    // Using UTF-8 encoded strings
    const char *l_test_cases[] = {
        "Hello",                    // ASCII letters
        "Привет",                   // Cyrillic letters (UTF-8)
        "こんにちは",               // Hiragana (UTF-8)
        "你好",                     // Han characters (UTF-8)
        "Γεια",                     // Greek letters (UTF-8)
        "שלום",                     // Hebrew letters (UTF-8)
        "Café",                     // Latin with diacritics (UTF-8)
        "München",                  // German umlaut (UTF-8)
        "北京",                     // Chinese characters (UTF-8)
    };
    
    for (size_t i = 0; i < sizeof(l_test_cases) / sizeof(l_test_cases[0]); i++) {
        const char *l_input = l_test_cases[i];
        size_t l_input_size = strlen(l_input);
        
        if (l_input_size == 0) {
            continue;
        }
        
        size_t l_expected_output_size = (l_input_size * 8) / 6; // base_size = 6
        char l_output[256];
        memset(l_output, 0, sizeof(l_output));
        
        size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 6, l_table, l_output);
        
        char l_msg[256];
        snprintf(l_msg, sizeof(l_msg), "Unicode letters test case %zu: output size mismatch", i);
        DAP_TEST_ASSERT(l_result == l_expected_output_size, l_msg);
        snprintf(l_msg, sizeof(l_msg), "Unicode letters test case %zu: should produce output", i);
        DAP_TEST_ASSERT(l_result > 0, l_msg);
        
        // Verify output characters are from the table
        for (size_t j = 0; j < l_result; j++) {
            bool l_is_valid = false;
            for (int k = 0; k < 64; k++) {
                if (l_output[j] == l_table[k]) {
                    l_is_valid = true;
                    break;
                }
            }
            snprintf(l_msg, sizeof(l_msg), "Unicode letters test case %zu: output char %zu should be from table", i, j);
            DAP_TEST_ASSERT(l_is_valid, l_msg);
        }
    }
    
    log_it(L_DEBUG, "Unicode letters test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with Unicode digits
 * Following PRECIS methodology: test decimal number category
 */
static bool s_test_encode_unicode_digits(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with Unicode digits (PRECIS methodology)");
    
    char l_table[256];
    s_create_encoding_table(l_table, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/", 64);
    
    // Test with various digit representations
    const char *l_test_cases[] = {
        "12345",           // ASCII digits
        "٠١٢٣٤٥",         // Arabic-Indic digits (UTF-8)
        "一二三四五",       // Chinese numerals (UTF-8)
        "ⅠⅡⅢⅣⅤ",         // Roman numerals (UTF-8)
    };
    
    for (size_t i = 0; i < sizeof(l_test_cases) / sizeof(l_test_cases[0]); i++) {
        const char *l_input = l_test_cases[i];
        size_t l_input_size = strlen(l_input);
        
        if (l_input_size == 0) {
            continue;
        }
        
        size_t l_expected_output_size = (l_input_size * 8) / 6; // base_size = 6
        char l_output[256];
        memset(l_output, 0, sizeof(l_output));
        
        size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 6, l_table, l_output);
        
        char l_msg[256];
        snprintf(l_msg, sizeof(l_msg), "Unicode digits test case %zu: output size mismatch", i);
        DAP_TEST_ASSERT(l_result == l_expected_output_size, l_msg);
        snprintf(l_msg, sizeof(l_msg), "Unicode digits test case %zu: should produce output", i);
        DAP_TEST_ASSERT(l_result > 0, l_msg);
    }
    
    log_it(L_DEBUG, "Unicode digits test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with Unicode symbols
 * Following PRECIS methodology: test symbol category
 */
static bool s_test_encode_unicode_symbols(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with Unicode symbols (PRECIS methodology)");
    
    char l_table[256];
    s_create_encoding_table(l_table, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/", 64);
    
    // Test with various symbol categories
    const char *l_test_cases[] = {
        "!@#$%",           // ASCII symbols
        "€£¥",             // Currency symbols (UTF-8)
        "±×÷",             // Math symbols (UTF-8)
        "©®™",             // Other symbols (UTF-8)
    };
    
    for (size_t i = 0; i < sizeof(l_test_cases) / sizeof(l_test_cases[0]); i++) {
        const char *l_input = l_test_cases[i];
        size_t l_input_size = strlen(l_input);
        
        if (l_input_size == 0) {
            continue;
        }
        
        size_t l_expected_output_size = (l_input_size * 8) / 6; // base_size = 6
        char l_output[256];
        memset(l_output, 0, sizeof(l_output));
        
        size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 6, l_table, l_output);
        
        char l_msg[256];
        snprintf(l_msg, sizeof(l_msg), "Unicode symbols test case %zu: output size mismatch", i);
        DAP_TEST_ASSERT(l_result == l_expected_output_size, l_msg);
        snprintf(l_msg, sizeof(l_msg), "Unicode symbols test case %zu: should produce output", i);
        DAP_TEST_ASSERT(l_result > 0, l_msg);
    }
    
    log_it(L_DEBUG, "Unicode symbols test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with Unicode punctuation
 * Following PRECIS methodology: test punctuation category
 */
static bool s_test_encode_unicode_punctuation(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with Unicode punctuation (PRECIS methodology)");
    
    char l_table[256];
    s_create_encoding_table(l_table, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/", 64);
    
    // Test with various punctuation categories
    const char *l_test_cases[] = {
        ".,;:!?",          // ASCII punctuation
        //"«»„"",            // Quotation marks (UTF-8)
        "—–",              // Dashes (UTF-8)
        "…",               // Ellipsis (UTF-8)
    };
    
    for (size_t i = 0; i < sizeof(l_test_cases) / sizeof(l_test_cases[0]); i++) {
        const char *l_input = l_test_cases[i];
        size_t l_input_size = strlen(l_input);
        
        if (l_input_size == 0) {
            continue;
        }
        
        size_t l_expected_output_size = (l_input_size * 8) / 6; // base_size = 6
        char l_output[256];
        memset(l_output, 0, sizeof(l_output));
        
        size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 6, l_table, l_output);
        
        char l_msg[256];
        snprintf(l_msg, sizeof(l_msg), "Unicode punctuation test case %zu: output size mismatch", i);
        DAP_TEST_ASSERT(l_result == l_expected_output_size, l_msg);
        snprintf(l_msg, sizeof(l_msg), "Unicode punctuation test case %zu: should produce output", i);
        DAP_TEST_ASSERT(l_result > 0, l_msg);
    }
    
    log_it(L_DEBUG, "Unicode punctuation test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with Unicode spaces
 * Following PRECIS methodology: test space separator category
 */
static bool s_test_encode_unicode_spaces(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with Unicode spaces (PRECIS methodology)");
    
    char l_table[256];
    s_create_encoding_table(l_table, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/", 64);
    
    // Test with various space characters
    const char *l_test_cases[] = {
        " ",               // ASCII space
        "  ",              // Multiple ASCII spaces
        "\t\n\r",          // ASCII whitespace
    };
    
    for (size_t i = 0; i < sizeof(l_test_cases) / sizeof(l_test_cases[0]); i++) {
        const char *l_input = l_test_cases[i];
        size_t l_input_size = strlen(l_input);
        
        if (l_input_size == 0) {
            continue;
        }
        
        size_t l_expected_output_size = (l_input_size * 8) / 6; // base_size = 6
        char l_output[256];
        memset(l_output, 0, sizeof(l_output));
        
        size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 6, l_table, l_output);
        
        char l_msg[256];
        snprintf(l_msg, sizeof(l_msg), "Unicode spaces test case %zu: output size mismatch", i);
        DAP_TEST_ASSERT(l_result == l_expected_output_size, l_msg);
        snprintf(l_msg, sizeof(l_msg), "Unicode spaces test case %zu: should produce output", i);
        DAP_TEST_ASSERT(l_result > 0, l_msg);
    }
    
    log_it(L_DEBUG, "Unicode spaces test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with multi-byte UTF-8 sequences
 * Following PRECIS methodology: test proper UTF-8 handling
 */
static bool s_test_encode_multibyte_utf8(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with multi-byte UTF-8 sequences (PRECIS methodology)");
    
    char l_table[256];
    s_create_encoding_table(l_table, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/", 64);
    
    // Test with various multi-byte UTF-8 sequences
    // These test cases include 2-byte, 3-byte, and 4-byte UTF-8 characters
    const char *l_test_cases[] = {
        "αβγ",             // Greek letters (2-byte UTF-8)
        "中文",             // Chinese characters (3-byte UTF-8)
        "🚀🎉",             // Emoji (4-byte UTF-8)
        "Café",            // Mixed ASCII and 2-byte UTF-8
        "Hello世界",        // Mixed ASCII and 3-byte UTF-8
    };
    
    for (size_t i = 0; i < sizeof(l_test_cases) / sizeof(l_test_cases[0]); i++) {
        const char *l_input = l_test_cases[i];
        size_t l_input_size = strlen(l_input);
        
        if (l_input_size == 0) {
            continue;
        }
        
        size_t l_expected_output_size = (l_input_size * 8) / 6; // base_size = 6
        char l_output[256];
        memset(l_output, 0, sizeof(l_output));
        
        size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 6, l_table, l_output);
        
        char l_msg[256];
        snprintf(l_msg, sizeof(l_msg), "Multi-byte UTF-8 test case %zu: output size mismatch", i);
        DAP_TEST_ASSERT(l_result == l_expected_output_size, l_msg);
        snprintf(l_msg, sizeof(l_msg), "Multi-byte UTF-8 test case %zu: should produce output", i);
        DAP_TEST_ASSERT(l_result > 0, l_msg);
    }
    
    log_it(L_DEBUG, "Multi-byte UTF-8 test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with different base sizes
 * Following PRECIS methodology: test systematic parameter variations
 */
static bool s_test_encode_different_base_sizes(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with different base sizes (PRECIS methodology)");
    
    char l_table[256];
    s_create_encoding_table(l_table, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/", 64);
    
    // Test with Unicode string and various base sizes
    const char *l_input = "Hello世界";  // Mixed ASCII and Unicode
    size_t l_input_size = strlen(l_input);
    
    struct {
        uint8_t base_size;
        size_t expected_output_size;
    } l_test_cases[] = {
        {1, l_input_size * 8},   // 1 bit per char
        {2, l_input_size * 4},   // 2 bits per char
        {4, l_input_size * 2},   // 4 bits per char
        {5, (l_input_size * 8) / 5},  // 5 bits per char
        {6, (l_input_size * 8) / 6},  // 6 bits per char (base64-like)
        {8, l_input_size * 1},   // 8 bits per char
    };
    
    for (size_t i = 0; i < sizeof(l_test_cases) / sizeof(l_test_cases[0]); i++) {
        char l_output[256];
        memset(l_output, 0, sizeof(l_output));
        
        size_t l_result = dap_encode_char_by_char(l_input, l_input_size,
                                                   l_test_cases[i].base_size,
                                                   l_table, l_output);
        
        char l_msg[256];
        snprintf(l_msg, sizeof(l_msg), "Base size %u test: output size mismatch (expected %zu, got %zu)",
                 l_test_cases[i].base_size,
                 l_test_cases[i].expected_output_size,
                 l_result);
        DAP_TEST_ASSERT(l_result == l_test_cases[i].expected_output_size, l_msg);
        snprintf(l_msg, sizeof(l_msg), "Base size %u test: should produce output",
                 l_test_cases[i].base_size);
        DAP_TEST_ASSERT(l_result > 0, l_msg);
    }
    
    log_it(L_DEBUG, "Different base sizes test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with edge cases
 * Following PRECIS methodology: test boundary conditions and error cases
 */
static bool s_test_encode_edge_cases(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with edge cases (PRECIS methodology)");
    
    char l_table[256];
    s_create_encoding_table(l_table, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/", 64);
    
    // Test NULL inputs (error handling)
    char l_output[256];
    size_t l_result = dap_encode_char_by_char(NULL, 10, 6, l_table, l_output);
    DAP_TEST_ASSERT(l_result == 0, "NULL input should return 0");
    
    const char l_input[] = "test";
    l_result = dap_encode_char_by_char(l_input, sizeof(l_input), 6, NULL, l_output);
    DAP_TEST_ASSERT(l_result == 0, "NULL table should return 0");
    
    l_result = dap_encode_char_by_char(l_input, sizeof(l_input), 6, l_table, NULL);
    DAP_TEST_ASSERT(l_result == 0, "NULL output should return 0");
    
    l_result = dap_encode_char_by_char(l_input, sizeof(l_input), 0, l_table, l_output);
    DAP_TEST_ASSERT(l_result == 0, "Zero base_size should return 0");
    
    // Test empty input
    const char l_empty[] = "";
    l_result = dap_encode_char_by_char(l_empty, 0, 6, l_table, l_output);
    DAP_TEST_ASSERT(l_result == 0, "Empty input should return 0");
    
    // Test single byte
    const char l_single[] = "A";
    l_result = dap_encode_char_by_char(l_single, 1, 6, l_table, l_output);
    DAP_TEST_ASSERT(l_result == 1, "Single byte should produce 1 output char (8/6 = 1)");
    
    // Test with maximum byte values
    const char l_max_bytes[] = "\xFF\xFF\xFF";
    l_result = dap_encode_char_by_char(l_max_bytes, 3, 6, l_table, l_output);
    DAP_TEST_ASSERT(l_result == 4, "3 bytes with base-6 should produce 4 output chars");
    
    log_it(L_DEBUG, "Edge cases test passed");
    return true;
}

/**
 * @brief Test dap_encode_char_by_char with various Unicode scripts
 * Following PRECIS methodology: test different script categories
 */
static bool s_test_encode_unicode_scripts(void) {
    log_it(L_DEBUG, "Testing dap_encode_char_by_char with various Unicode scripts (PRECIS methodology)");
    
    char l_table[256];
    s_create_encoding_table(l_table, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/", 64);
    
    // Test with various Unicode scripts
    const char *l_test_cases[] = {
        "Hello",           // Latin script
        "Привет",          // Cyrillic script
        "Γεια",            // Greek script
        "שלום",            // Hebrew script
        "こんにちは",      // Hiragana script
        "カタカナ",        // Katakana script
        "中文",            // Han script
        "العربية",        // Arabic script
        "ไทย",             // Thai script
        "हिन्दी",          // Devanagari script
    };
    
    for (size_t i = 0; i < sizeof(l_test_cases) / sizeof(l_test_cases[0]); i++) {
        const char *l_input = l_test_cases[i];
        size_t l_input_size = strlen(l_input);
        
        if (l_input_size == 0) {
            continue;
        }
        
        size_t l_expected_output_size = (l_input_size * 8) / 6; // base_size = 6
        char l_output[256];
        memset(l_output, 0, sizeof(l_output));
        
        size_t l_result = dap_encode_char_by_char(l_input, l_input_size, 6, l_table, l_output);
        
        char l_msg[256];
        snprintf(l_msg, sizeof(l_msg), "Unicode script test case %zu: output size mismatch", i);
        DAP_TEST_ASSERT(l_result == l_expected_output_size, l_msg);
        snprintf(l_msg, sizeof(l_msg), "Unicode script test case %zu: should produce output", i);
        DAP_TEST_ASSERT(l_result > 0, l_msg);
    }
    
    log_it(L_DEBUG, "Unicode scripts test passed");
    return true;
}

/**
 * @brief Main test function for dap_encode_char_by_char using PRECIS methodology
 */
int main(void) {
    log_it(L_INFO, "Starting dap_encode_char_by_char unit tests (PRECIS methodology)");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    // Test ASCII7 characters (PRECIS category)
    l_all_passed &= s_test_encode_ascii7();
    
    // Test Unicode letters (PRECIS category)
    l_all_passed &= s_test_encode_unicode_letters();
    
    // Test Unicode digits (PRECIS category)
    l_all_passed &= s_test_encode_unicode_digits();
    
    // Test Unicode symbols (PRECIS category)
    l_all_passed &= s_test_encode_unicode_symbols();
    
    // Test Unicode punctuation (PRECIS category)
    l_all_passed &= s_test_encode_unicode_punctuation();
    
    // Test Unicode spaces (PRECIS category)
    l_all_passed &= s_test_encode_unicode_spaces();
    
    // Test multi-byte UTF-8 sequences
    l_all_passed &= s_test_encode_multibyte_utf8();
    
    // Test different base sizes
    l_all_passed &= s_test_encode_different_base_sizes();
    
    // Test edge cases
    l_all_passed &= s_test_encode_edge_cases();
    
    // Test various Unicode scripts
    l_all_passed &= s_test_encode_unicode_scripts();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All dap_encode_char_by_char tests (PRECIS methodology) passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some dap_encode_char_by_char tests (PRECIS methodology) failed!");
        return -1;
    }
}
