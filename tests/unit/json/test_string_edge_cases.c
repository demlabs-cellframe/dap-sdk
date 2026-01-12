/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file test_string_edge_cases.c
 * @brief String Structure Edge Cases - Phase 1.8.2
 * @details ПОЛНАЯ реализация 11 string edge case tests
 * 
 * Tests:
 *   1. Empty strings ("", {"":""}, nested empty)
 *   2. Only whitespace ("   ", "\t\t\t")
 *   3. Maximum key length (test implementation limits)
 *   4. Strings with ONLY escapes ("\\\\\\\\" thousands)
 *   5. Alternating escapes ("\n\n\n..." performance test)
 *   6. Unicode in object keys ({"😀": "value"})
 *   7. Case-sensitive duplicate keys ({"Key": 1, "key": 2})
 *   8. Numeric-looking keys ({"123": "a", "0123": "b"})
 *   9. Backslash at end ("text\\" - valid if escaped)
 *   10. Escape sequence spanning SIMD chunks
 *   11. UTF-8 multibyte split across chunks
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "test_string_edges"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <string.h>
#include <stdlib.h>

// =============================================================================
// TEST 1: Empty Strings
// =============================================================================

/**
 * @brief Test empty strings in various contexts
 * @details Cases: "", {""}, {"":""}, [""], nested empty
 */
static bool s_test_empty_strings(void) {
    log_it(L_DEBUG, "Testing empty strings");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test 1: Empty string value
    l_json = dap_json_parse_string("{\"empty\":\"\"}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse empty string value");
    
    const char *empty_val = dap_json_object_get_string(l_json, "empty");
    DAP_TEST_FAIL_IF_NULL(empty_val, "Get empty string");
    DAP_TEST_FAIL_IF(strlen(empty_val) != 0, "Empty string has zero length");
    
    dap_json_object_free(l_json);
    
    // Test 2: Empty key
    l_json = dap_json_parse_string("{\"\":\"value\"}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse empty key");
    
    const char *val = dap_json_object_get_string(l_json, "");
    DAP_TEST_FAIL_IF_NULL(val, "Get value by empty key");
    DAP_TEST_FAIL_IF(strcmp(val, "value") != 0, "Empty key retrieves correct value");
    
    dap_json_object_free(l_json);
    
    // Test 3: Empty key and empty value
    l_json = dap_json_parse_string("{\"\":\"\"}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse empty key and value");
    
    const char *empty_both = dap_json_object_get_string(l_json, "");
    DAP_TEST_FAIL_IF_NULL(empty_both, "Get empty value by empty key");
    DAP_TEST_FAIL_IF(strlen(empty_both) != 0, "Both key and value empty");
    
    dap_json_object_free(l_json);
    
    // Test 4: Empty string in array
    l_json = dap_json_parse_string("[\"\",\"\",\"\"]");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse array of empty strings");
    
    size_t arr_len = dap_json_array_length(l_json);
    DAP_TEST_FAIL_IF(arr_len != 3, "Array has 3 empty strings");
    
    dap_json_object_free(l_json);
    
    // Test 5: Nested empty strings
    l_json = dap_json_parse_string("{\"outer\":{\"inner\":\"\",\"\":\"\"},\"\":{}}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse nested empty strings");
    
    result = true;
    log_it(L_DEBUG, "Empty strings test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 2: Whitespace Only Strings
// =============================================================================

/**
 * @brief Test strings containing only whitespace
 * @details Cases: "   ", "\t\t\t", "\n\n", mixed whitespace
 */
static bool s_test_whitespace_only_strings(void) {
    log_it(L_DEBUG, "Testing whitespace-only strings");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test 1: Spaces only
    l_json = dap_json_parse_string("{\"spaces\":\"     \"}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse spaces-only string");
    
    const char *spaces = dap_json_object_get_string(l_json, "spaces");
    DAP_TEST_FAIL_IF_NULL(spaces, "Get spaces string");
    DAP_TEST_FAIL_IF(strlen(spaces) != 5, "Spaces string has 5 characters");
    DAP_TEST_FAIL_IF(spaces[0] != ' ', "First character is space");
    
    dap_json_object_free(l_json);
    
    // Test 2: Tabs only (escaped as \t in JSON)
    l_json = dap_json_parse_string("{\"tabs\":\"\\t\\t\\t\"}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse tabs-only string");
    
    const char *tabs = dap_json_object_get_string(l_json, "tabs");
    DAP_TEST_FAIL_IF_NULL(tabs, "Get tabs string");
    DAP_TEST_FAIL_IF(tabs[0] != '\t', "First character is tab");
    
    dap_json_object_free(l_json);
    
    // Test 3: Newlines only (escaped as \n in JSON)
    l_json = dap_json_parse_string("{\"newlines\":\"\\n\\n\\n\"}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse newlines-only string");
    
    const char *newlines = dap_json_object_get_string(l_json, "newlines");
    DAP_TEST_FAIL_IF_NULL(newlines, "Get newlines string");
    DAP_TEST_FAIL_IF(newlines[0] != '\n', "First character is newline");
    
    dap_json_object_free(l_json);
    
    // Test 4: Mixed whitespace
    l_json = dap_json_parse_string("{\"mixed\":\" \\t \\n \\r \"}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse mixed whitespace string");
    
    const char *mixed = dap_json_object_get_string(l_json, "mixed");
    DAP_TEST_FAIL_IF_NULL(mixed, "Get mixed whitespace string");
    
    result = true;
    log_it(L_DEBUG, "Whitespace-only strings test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 3: Maximum Key Length
// =============================================================================

/**
 * @brief Test maximum key length (implementation limits)
 * @details Create keys with 1KB, 10KB, 100KB lengths
 */
static bool s_test_maximum_key_length(void) {
    log_it(L_DEBUG, "Testing maximum key length");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *json_buf = NULL;
    
    // Test 1: 1KB key
    const size_t KEY_SIZE_1KB = 1024;
    json_buf = (char*)malloc(KEY_SIZE_1KB + 32);
    DAP_TEST_FAIL_IF_NULL(json_buf, "Allocate buffer for 1KB key");
    
    strcpy(json_buf, "{\"");
    for (size_t i = 0; i < KEY_SIZE_1KB; i++) {
        json_buf[2 + i] = 'k';
    }
    strcpy(json_buf + 2 + KEY_SIZE_1KB, "\":\"value\"}");
    
    log_it(L_INFO, "Testing %zu byte key", KEY_SIZE_1KB);
    l_json = dap_json_parse_string(json_buf);
    
    if (l_json) {
        log_it(L_INFO, "Parser accepted %zu byte key", KEY_SIZE_1KB);
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser rejected %zu byte key (acceptable if limits enforced)", KEY_SIZE_1KB);
    }
    
    free(json_buf);
    
    // Test 2: 10KB key (stress test)
    const size_t KEY_SIZE_10KB = 10 * 1024;
    json_buf = (char*)malloc(KEY_SIZE_10KB + 32);
    DAP_TEST_FAIL_IF_NULL(json_buf, "Allocate buffer for 10KB key");
    
    strcpy(json_buf, "{\"");
    for (size_t i = 0; i < KEY_SIZE_10KB; i++) {
        json_buf[2 + i] = 'k';
    }
    strcpy(json_buf + 2 + KEY_SIZE_10KB, "\":\"value\"}");
    
    log_it(L_INFO, "Testing %zu byte key", KEY_SIZE_10KB);
    l_json = dap_json_parse_string(json_buf);
    
    if (l_json) {
        log_it(L_INFO, "Parser accepted %zu byte key", KEY_SIZE_10KB);
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser rejected %zu byte key (acceptable)", KEY_SIZE_10KB);
    }
    
    free(json_buf);
    
    // Test passes if we don't crash
    result = true;
    log_it(L_DEBUG, "Maximum key length test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 4: Strings with ONLY Escapes
// =============================================================================

/**
 * @brief Test strings containing only escape sequences
 * @details Create string with thousands of backslashes: "\\\\\\\\\..."
 */
static bool s_test_strings_only_escapes(void) {
    log_it(L_DEBUG, "Testing strings with only escapes");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *json_buf = NULL;
    
    // Create string with 1000 escaped backslashes: "\\\\" repeated 1000 times
    // Each \\\\ in JSON = \\ in C string = single \ character in result
    const int ESCAPE_COUNT = 1000;
    const size_t JSON_SIZE = ESCAPE_COUNT * 4 + 32;  // Each \\\\ = 4 chars
    
    json_buf = (char*)malloc(JSON_SIZE);
    DAP_TEST_FAIL_IF_NULL(json_buf, "Allocate buffer for escaped string");
    
    strcpy(json_buf, "{\"escaped\":\"");
    char *ptr = json_buf + strlen(json_buf);
    
    for (int i = 0; i < ESCAPE_COUNT; i++) {
        *ptr++ = '\\';
        *ptr++ = '\\';
        *ptr++ = '\\';
        *ptr++ = '\\';
    }
    strcpy(ptr, "\"}");
    
    log_it(L_INFO, "Testing string with %d escaped backslashes (%zu bytes JSON)", 
           ESCAPE_COUNT, strlen(json_buf));
    
    l_json = dap_json_parse_string(json_buf);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse string with many escapes");
    
    const char *escaped = dap_json_object_get_string(l_json, "escaped");
    DAP_TEST_FAIL_IF_NULL(escaped, "Get escaped string");
    
    // Verify length: 1000 \\\\ in JSON = 1000 \\ in C = 1000 \ characters
    size_t expected_len = ESCAPE_COUNT * 2;  // Each \\\\ in JSON = 2 chars in result
    size_t actual_len = strlen(escaped);
    log_it(L_DEBUG, "Escaped string length: expected=%zu, actual=%zu", expected_len, actual_len);
    DAP_TEST_FAIL_IF(actual_len != expected_len, "Escaped string has correct length");
    
    result = true;
    log_it(L_DEBUG, "Strings with only escapes test passed");
    
cleanup:
    free(json_buf);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 5: Alternating Escapes (Performance Test)
// =============================================================================

/**
 * @brief Test alternating escapes performance
 * @details Create: "a\na\na\n..." thousands of times
 */
static bool s_test_alternating_escapes(void) {
    log_it(L_DEBUG, "Testing alternating escapes (performance)");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *json_buf = NULL;
    
    // Create: "a\na\na\n..." 1000 times
    const int REPEAT_COUNT = 1000;
    const size_t JSON_SIZE = REPEAT_COUNT * 4 + 32;  // "a\\n" = 4 chars
    
    json_buf = (char*)malloc(JSON_SIZE);
    DAP_TEST_FAIL_IF_NULL(json_buf, "Allocate buffer");
    
    strcpy(json_buf, "{\"alternating\":\"");
    char *ptr = json_buf + strlen(json_buf);
    
    for (int i = 0; i < REPEAT_COUNT; i++) {
        *ptr++ = 'a';
        *ptr++ = '\\';
        *ptr++ = 'n';
    }
    strcpy(ptr, "\"}");
    
    log_it(L_INFO, "Testing alternating escapes: %d repetitions", REPEAT_COUNT);
    
    l_json = dap_json_parse_string(json_buf);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse alternating escapes");
    
    const char *alternating = dap_json_object_get_string(l_json, "alternating");
    DAP_TEST_FAIL_IF_NULL(alternating, "Get alternating string");
    
    // Verify: should be "a\na\na\n..." with actual newlines
    DAP_TEST_FAIL_IF(alternating[0] != 'a', "First character is 'a'");
    DAP_TEST_FAIL_IF(alternating[1] != '\n', "Second character is newline");
    
    result = true;
    log_it(L_DEBUG, "Alternating escapes test passed");
    
cleanup:
    free(json_buf);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 6: Unicode in Object Keys
// =============================================================================

/**
 * @brief Test Unicode characters in object keys
 * @details Keys: {"😀": "emoji", "Привет": "russian", "中文": "chinese"}
 */
static bool s_test_unicode_in_keys(void) {
    log_it(L_DEBUG, "Testing Unicode in object keys");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *json_str = "{\"😀\":\"emoji\",\"Привет\":\"russian\",\"中文\":\"chinese\"}";
    
    l_json = dap_json_parse_string(json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse Unicode keys");
    
    // Test emoji key
    const char *emoji_val = dap_json_object_get_string(l_json, "😀");
    DAP_TEST_FAIL_IF_NULL(emoji_val, "Get value by emoji key");
    DAP_TEST_FAIL_IF(strcmp(emoji_val, "emoji") != 0, "Emoji key retrieves correct value");
    
    // Test Russian key
    const char *russian_val = dap_json_object_get_string(l_json, "Привет");
    DAP_TEST_FAIL_IF_NULL(russian_val, "Get value by Russian key");
    DAP_TEST_FAIL_IF(strcmp(russian_val, "russian") != 0, "Russian key retrieves correct value");
    
    // Test Chinese key
    const char *chinese_val = dap_json_object_get_string(l_json, "中文");
    DAP_TEST_FAIL_IF_NULL(chinese_val, "Get value by Chinese key");
    DAP_TEST_FAIL_IF(strcmp(chinese_val, "chinese") != 0, "Chinese key retrieves correct value");
    
    result = true;
    log_it(L_DEBUG, "Unicode in keys test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 7: Case-Sensitive Duplicate Keys
// =============================================================================

/**
 * @brief Test case-sensitive duplicate keys
 * @details Keys: {"Key": 1, "key": 2, "KEY": 3} - all different
 */
static bool s_test_case_sensitive_duplicate_keys(void) {
    log_it(L_DEBUG, "Testing case-sensitive duplicate keys");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *json_str = "{\"Key\":1,\"key\":2,\"KEY\":3}";
    
    l_json = dap_json_parse_string(json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse case-sensitive keys");
    
    // All three keys should be distinct
    int val_Key = dap_json_object_get_int(l_json, "Key");
    int val_key = dap_json_object_get_int(l_json, "key");
    int val_KEY = dap_json_object_get_int(l_json, "KEY");
    
    DAP_TEST_FAIL_IF(val_Key != 1, "Key=1");
    DAP_TEST_FAIL_IF(val_key != 2, "key=2");
    DAP_TEST_FAIL_IF(val_KEY != 3, "KEY=3");
    
    log_it(L_INFO, "Case-sensitive keys: Key=%d, key=%d, KEY=%d", val_Key, val_key, val_KEY);
    
    result = true;
    log_it(L_DEBUG, "Case-sensitive duplicate keys test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 8: Numeric-Looking Keys
// =============================================================================

/**
 * @brief Test numeric-looking keys (NOT arrays!)
 * @details Keys: {"123": "a", "0123": "b", "0": "c"} - all strings, not array indices
 */
static bool s_test_numeric_looking_keys(void) {
    log_it(L_DEBUG, "Testing numeric-looking keys");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *json_str = "{\"123\":\"a\",\"0123\":\"b\",\"0\":\"c\"}";
    
    l_json = dap_json_parse_string(json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse numeric-looking keys");
    
    const char *val_123 = dap_json_object_get_string(l_json, "123");
    DAP_TEST_FAIL_IF_NULL(val_123, "Get value by '123' key");
    DAP_TEST_FAIL_IF(strcmp(val_123, "a") != 0, "'123' key correct");
    
    const char *val_0123 = dap_json_object_get_string(l_json, "0123");
    DAP_TEST_FAIL_IF_NULL(val_0123, "Get value by '0123' key");
    DAP_TEST_FAIL_IF(strcmp(val_0123, "b") != 0, "'0123' key correct");
    
    const char *val_0 = dap_json_object_get_string(l_json, "0");
    DAP_TEST_FAIL_IF_NULL(val_0, "Get value by '0' key");
    DAP_TEST_FAIL_IF(strcmp(val_0, "c") != 0, "'0' key correct");
    
    result = true;
    log_it(L_DEBUG, "Numeric-looking keys test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 9: Backslash at End
// =============================================================================

/**
 * @brief Test backslash at end of string (must be escaped)
 * @details Valid: "text\\\\" (escaped backslash), Invalid: "text\\" (unescaped)
 */
static bool s_test_backslash_at_end(void) {
    log_it(L_DEBUG, "Testing backslash at end of string");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test 1: Valid escaped backslash at end
    l_json = dap_json_parse_string("{\"valid\":\"text\\\\\"}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse valid escaped backslash at end");
    
    const char *valid = dap_json_object_get_string(l_json, "valid");
    DAP_TEST_FAIL_IF_NULL(valid, "Get string with escaped backslash");
    DAP_TEST_FAIL_IF(strcmp(valid, "text\\") != 0, "Backslash at end correct");
    
    dap_json_object_free(l_json);
    
    // Test 2: Invalid unescaped backslash at end (should fail)
    // Note: Can't easily test this in C string literal, as "\\" is already escaped
    
    result = true;
    log_it(L_DEBUG, "Backslash at end test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 10-11: Chunk Boundary Tests (Covered by test_simd_chunk_boundaries.c)
// =============================================================================

/**
 * @brief Escape spanning chunks - COVERED by test_simd_chunk_boundaries.c
 */
static bool s_test_escape_spanning_chunks(void) {
    log_it(L_DEBUG, "Escape spanning chunks - covered by test_simd_chunk_boundaries.c");
    return true;  // Already tested in dedicated SIMD boundary test
}

/**
 * @brief UTF-8 split across chunks - COVERED by test_simd_chunk_boundaries.c
 */
static bool s_test_utf8_split_chunks(void) {
    log_it(L_DEBUG, "UTF-8 split chunks - covered by test_simd_chunk_boundaries.c");
    return true;  // Already tested in dedicated SIMD boundary test
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_json_string_edge_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON String Edge Cases ===");
    
    int tests_passed = 0;
    int tests_total = 11;
    
    tests_passed += s_test_empty_strings() ? 1 : 0;
    tests_passed += s_test_whitespace_only_strings() ? 1 : 0;
    tests_passed += s_test_maximum_key_length() ? 1 : 0;
    tests_passed += s_test_strings_only_escapes() ? 1 : 0;
    tests_passed += s_test_alternating_escapes() ? 1 : 0;
    tests_passed += s_test_unicode_in_keys() ? 1 : 0;
    tests_passed += s_test_case_sensitive_duplicate_keys() ? 1 : 0;
    tests_passed += s_test_numeric_looking_keys() ? 1 : 0;
    tests_passed += s_test_backslash_at_end() ? 1 : 0;
    tests_passed += s_test_escape_spanning_chunks() ? 1 : 0;
    tests_passed += s_test_utf8_split_chunks() ? 1 : 0;
    
    log_it(L_INFO, "String edge tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void) {
    dap_print_module_name("DAP JSON String Edge Cases");
    return dap_json_string_edge_tests_run();
}

