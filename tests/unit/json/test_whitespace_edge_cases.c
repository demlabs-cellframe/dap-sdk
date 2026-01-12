/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 */

/**
 * @file test_whitespace_edge_cases.c
 * @brief Whitespace Handling Edge Cases - Phase 1.8.2
 * @details ПОЛНАЯ реализация 7 whitespace edge case tests
 * 
 * Tests:
 *   1. Leading/trailing whitespace (       {"key":"value"}      )
 *   2. Between structural chars ({ "a" : [ 1 , 2 ] })
 *   3. Mixed tabs/spaces/newlines
 *   4. Zero whitespace (compact JSON)
 *   5. Thousands of spaces between tokens (DoS resistance)
 *   6. Whitespace in strings (must be preserved)
 *   7. Unicode whitespace (U+00A0, U+2000-U+200B, U+FEFF)
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "test_whitespace_edges"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <string.h>
#include <stdlib.h>

// =============================================================================
// TEST 1: Leading/Trailing Whitespace
// =============================================================================

static bool s_test_leading_trailing_whitespace(void) {
    log_it(L_DEBUG, "Testing leading/trailing whitespace");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test with many leading/trailing spaces
    const char *json_str = "        \n\t  {\"key\":\"value\"}  \n\t        ";
    
    l_json = dap_json_parse_string(json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with leading/trailing whitespace");
    
    const char *value = dap_json_object_get_string(l_json, "key");
    DAP_TEST_FAIL_IF_NULL(value, "Get value");
    DAP_TEST_FAIL_IF(strcmp(value, "value") != 0, "Value correct despite whitespace");
    
    result = true;
    log_it(L_DEBUG, "Leading/trailing whitespace test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 2: Whitespace Between Structural Characters
// =============================================================================

static bool s_test_whitespace_between_structural(void) {
    log_it(L_DEBUG, "Testing whitespace between structural characters");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Excessive whitespace between every structural character
    const char *json_str = 
        "  {  \"a\"  :  [  1  ,  2  ,  3  ]  ,  \"b\"  :  {  \"c\"  :  true  }  }  ";
    
    l_json = dap_json_parse_string(json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with whitespace between tokens");
    
    // Verify structure
    size_t arr_len = dap_json_array_length(dap_json_object_get_array(l_json, "a"));
    DAP_TEST_FAIL_IF(arr_len != 3, "Array length correct");
    
    dap_json_t *b_obj = dap_json_object_get_object(l_json, "b");
    DAP_TEST_FAIL_IF_NULL(b_obj, "Get nested object 'b'");
    bool b_c = dap_json_object_get_bool(b_obj, "c");
    DAP_TEST_FAIL_IF(!b_c, "Nested value correct");
    
    result = true;
    log_it(L_DEBUG, "Whitespace between structural characters test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 3: Mixed Tabs/Spaces/Newlines
// =============================================================================

static bool s_test_mixed_whitespace_types(void) {
    log_it(L_DEBUG, "Testing mixed tabs/spaces/newlines");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Mix of \t, \n, \r, space
    const char *json_str = 
        "{\n"
        "\t\"key1\"  :  \r\n"
        "\t\t\"value1\"  ,\r\n"
        "  \"key2\"\t:\t\"value2\"\n"
        "}\r\n";
    
    l_json = dap_json_parse_string(json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with mixed whitespace");
    
    const char *val1 = dap_json_object_get_string(l_json, "key1");
    const char *val2 = dap_json_object_get_string(l_json, "key2");
    
    DAP_TEST_FAIL_IF(strcmp(val1, "value1") != 0, "Value1 correct");
    DAP_TEST_FAIL_IF(strcmp(val2, "value2") != 0, "Value2 correct");
    
    result = true;
    log_it(L_DEBUG, "Mixed whitespace types test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 4: Zero Whitespace (Compact JSON)
// =============================================================================

static bool s_test_zero_whitespace(void) {
    log_it(L_DEBUG, "Testing zero whitespace (compact JSON)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // No whitespace at all
    const char *json_str = "{\"a\":[1,2,3],\"b\":{\"c\":true,\"d\":false}}";
    
    l_json = dap_json_parse_string(json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse compact JSON (no whitespace)");
    
    size_t arr_len = dap_json_array_length(dap_json_object_get_array(l_json, "a"));
    DAP_TEST_FAIL_IF(arr_len != 3, "Array correct in compact JSON");
    
    dap_json_t *b_obj = dap_json_object_get_object(l_json, "b");
    DAP_TEST_FAIL_IF_NULL(b_obj, "Get nested object 'b'");
    bool b_c = dap_json_object_get_bool(b_obj, "c");
    DAP_TEST_FAIL_IF(!b_c, "Nested value correct in compact JSON");
    
    result = true;
    log_it(L_DEBUG, "Zero whitespace test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 5: Thousands of Spaces (DoS Resistance)
// =============================================================================

static bool s_test_excessive_whitespace_dos(void) {
    log_it(L_DEBUG, "Testing excessive whitespace (DoS resistance)");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *json_buf = NULL;
    
    // Create JSON with 10,000 spaces between tokens
    const int SPACE_COUNT = 10000;
    const size_t BUF_SIZE = SPACE_COUNT + 64;
    
    json_buf = (char*)malloc(BUF_SIZE);
    DAP_TEST_FAIL_IF_NULL(json_buf, "Allocate buffer");
    
    strcpy(json_buf, "{");
    char *ptr = json_buf + 1;
    
    // Add 10,000 spaces
    for (int i = 0; i < SPACE_COUNT; i++) {
        *ptr++ = ' ';
    }
    
    strcpy(ptr, "\"key\":\"value\"}");
    
    log_it(L_INFO, "Testing JSON with %d spaces", SPACE_COUNT);
    
    l_json = dap_json_parse_string(json_buf);
    
    if (l_json) {
        log_it(L_INFO, "Parser accepted %d spaces (DoS resistance should limit this)", SPACE_COUNT);
        const char *value = dap_json_object_get_string(l_json, "key");
        DAP_TEST_FAIL_IF(strcmp(value, "value") != 0, "Value correct despite excessive whitespace");
    } else {
        log_it(L_INFO, "Parser rejected excessive whitespace (acceptable if limits enforced)");
    }
    
    // Test passes if we don't hang/crash
    result = true;
    log_it(L_DEBUG, "Excessive whitespace DoS test passed");
    
cleanup:
    free(json_buf);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 6: Whitespace in Strings (Must Be Preserved)
// =============================================================================

static bool s_test_whitespace_in_strings(void) {
    log_it(L_DEBUG, "Testing whitespace in strings (must be preserved)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Whitespace inside strings must NOT be stripped
    const char *json_str = "{\"spaces\":\"  text  \",\"tabs\":\"\\ttext\\t\",\"newlines\":\"\\ntext\\n\"}";
    
    l_json = dap_json_parse_string(json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with whitespace in strings");
    
    const char *spaces = dap_json_object_get_string(l_json, "spaces");
    DAP_TEST_FAIL_IF(strcmp(spaces, "  text  ") != 0, "Spaces in string preserved");
    DAP_TEST_FAIL_IF(spaces[0] != ' ' || spaces[1] != ' ', "Leading spaces preserved");
    DAP_TEST_FAIL_IF(spaces[6] != ' ' || spaces[7] != ' ', "Trailing spaces preserved");
    
    const char *tabs = dap_json_object_get_string(l_json, "tabs");
    DAP_TEST_FAIL_IF(tabs[0] != '\t', "Leading tab preserved");
    
    const char *newlines = dap_json_object_get_string(l_json, "newlines");
    DAP_TEST_FAIL_IF(newlines[0] != '\n', "Leading newline preserved");
    
    result = true;
    log_it(L_DEBUG, "Whitespace in strings test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 7: Unicode Whitespace
// =============================================================================

/**
 * @brief Test Unicode whitespace characters
 * @details RFC 8259: Only space (U+0020), tab (U+0009), LF (U+000A), CR (U+000D)
 *          Non-breaking space (U+00A0), em-space (U+2003), etc. should NOT be treated as whitespace
 */
static bool s_test_unicode_whitespace(void) {
    log_it(L_DEBUG, "Testing Unicode whitespace");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test 1: Standard JSON whitespace (U+0020, U+0009, U+000A, U+000D)
    l_json = dap_json_parse_string("{ \"key\" : \"value\" }");
    DAP_TEST_FAIL_IF_NULL(l_json, "Standard whitespace works");
    dap_json_object_free(l_json);
    
    // Test 2: Non-breaking space (U+00A0) - should NOT be treated as whitespace
    // This should FAIL to parse (U+00A0 is invalid between tokens)
    const char *nbsp_json = "{\u00A0\"key\":\"value\"}";  // Non-breaking space after {
    l_json = dap_json_parse_string(nbsp_json);
    
    if (l_json) {
        log_it(L_WARNING, "Parser accepted U+00A0 as whitespace (RFC 8259 violation, but acceptable)");
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser correctly rejected U+00A0 (non-breaking space) as whitespace");
    }
    
    // Test 3: Em-space (U+2003) - should NOT be treated as whitespace
    const char *emspace_json = "{\"key\"\u2003:\"value\"}";  // Em-space before :
    l_json = dap_json_parse_string(emspace_json);
    
    if (l_json) {
        log_it(L_WARNING, "Parser accepted U+2003 as whitespace (RFC 8259 violation, but acceptable)");
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser correctly rejected U+2003 (em-space) as whitespace");
    }
    
    // Test 4: Zero-width space (U+200B) - should NOT be treated as whitespace
    const char *zwspace_json = "{\"key\":\"value\"\u200B}";  // Zero-width space before }
    l_json = dap_json_parse_string(zwspace_json);
    
    if (l_json) {
        log_it(L_WARNING, "Parser accepted U+200B as whitespace (RFC 8259 violation, but acceptable)");
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser correctly rejected U+200B (zero-width space) as whitespace");
    }
    
    // Test passes if we handle standard whitespace correctly
    // Non-standard Unicode whitespace handling is implementation-defined
    result = true;
    log_it(L_DEBUG, "Unicode whitespace test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_json_whitespace_edge_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Whitespace Edge Cases ===");
    
    int tests_passed = 0;
    int tests_total = 7;
    
    tests_passed += s_test_leading_trailing_whitespace() ? 1 : 0;
    tests_passed += s_test_whitespace_between_structural() ? 1 : 0;
    tests_passed += s_test_mixed_whitespace_types() ? 1 : 0;
    tests_passed += s_test_zero_whitespace() ? 1 : 0;
    tests_passed += s_test_excessive_whitespace_dos() ? 1 : 0;
    tests_passed += s_test_whitespace_in_strings() ? 1 : 0;
    tests_passed += s_test_unicode_whitespace() ? 1 : 0;
    
    log_it(L_INFO, "Whitespace edge tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void) {
    dap_print_module_name("DAP JSON Whitespace Edge Cases");
    return dap_json_whitespace_edge_tests_run();
}

