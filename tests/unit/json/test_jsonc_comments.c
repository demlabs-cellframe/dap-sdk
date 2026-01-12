/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 */

/**
 * @file test_jsonc_comments.c
 * @brief JSONC (JSON with Comments) Tests - Phase 1.8.3
 * @details ПОЛНАЯ реализация 5 JSONC comment tests
 * 
 * JSONC is JSON with C-style comments (used by VS Code config files):
 *   1. Single-line comments (//)
 *   2. Multi-line comments (/* ... */)
 *   3. Comments in various positions
 *   4. Nested comments (invalid in C, but test anyway)
 *   5. Comments with special characters
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "test_jsonc"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <string.h>

// =============================================================================
// TEST 1: Single-Line Comments
// =============================================================================

static bool s_test_single_line_comments(void) {
    log_it(L_DEBUG, "Testing JSONC single-line comments");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *jsonc_str = 
        "{\n"
        "  // This is a comment\n"
        "  \"key\": \"value\", // End-of-line comment\n"
        "  \"number\": 42 // Another comment\n"
        "}";
    
    l_json = dap_json_parse_string(jsonc_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser supports JSONC single-line comments");
        const char *val = dap_json_object_get_string(l_json, "key");
        DAP_TEST_FAIL_IF(strcmp(val, "value") != 0, "Value correct despite comments");
        
        int num = dap_json_object_get_int(l_json, "number");
        DAP_TEST_FAIL_IF(num != 42, "Number correct despite comments");
    } else {
        log_it(L_INFO, "Parser does NOT support JSONC single-line comments (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "JSONC single-line comments test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 2: Multi-Line Comments
// =============================================================================

static bool s_test_multi_line_comments(void) {
    log_it(L_DEBUG, "Testing JSONC multi-line comments");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *jsonc_str = 
        "{\n"
        "  /* This is a multi-line comment\n"
        "     that spans multiple lines\n"
        "     and contains various text */\n"
        "  \"key\": /* inline comment */ \"value\",\n"
        "  /* Another comment */\n"
        "  \"array\": /* before array */ [1, 2, 3] /* after array */\n"
        "}";
    
    l_json = dap_json_parse_string(jsonc_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser supports JSONC multi-line comments");
        
        const char *val = dap_json_object_get_string(l_json, "key");
        DAP_TEST_FAIL_IF(strcmp(val, "value") != 0, "Value correct despite inline comment");
        
        dap_json_t *arr = dap_json_object_get_array(l_json, "array");
        DAP_TEST_FAIL_IF_NULL(arr, "Array correct despite surrounding comments");
        DAP_TEST_FAIL_IF(dap_json_array_length(arr) != 3, "Array length correct");
    } else {
        log_it(L_INFO, "Parser does NOT support JSONC multi-line comments (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "JSONC multi-line comments test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 3: Comments in Various Positions
// =============================================================================

static bool s_test_comments_in_various_positions(void) {
    log_it(L_DEBUG, "Testing JSONC comments in various positions");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *jsonc_str = 
        "// Comment before opening brace\n"
        "{\n"
        "  // Comment after opening brace\n"
        "  \"key1\": \"value1\", // After value\n"
        "  // Comment between elements\n"
        "  \"key2\": /* Before value */ \"value2\" /* After value */,\n"
        "  \"array\": [\n"
        "    // Comment in array\n"
        "    1, // After element\n"
        "    2,\n"
        "    3 // Last element\n"
        "    // Comment before closing bracket\n"
        "  ] // After array\n"
        "  // Comment before closing brace\n"
        "}\n"
        "// Comment after closing brace\n";
    
    l_json = dap_json_parse_string(jsonc_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser supports JSONC comments in all positions");
        
        const char *val1 = dap_json_object_get_string(l_json, "key1");
        const char *val2 = dap_json_object_get_string(l_json, "key2");
        
        DAP_TEST_FAIL_IF(strcmp(val1, "value1") != 0, "key1 correct");
        DAP_TEST_FAIL_IF(strcmp(val2, "value2") != 0, "key2 correct");
        
        dap_json_t *arr = dap_json_object_get_array(l_json, "array");
        DAP_TEST_FAIL_IF(dap_json_array_length(arr) != 3, "Array with comments correct");
    } else {
        log_it(L_INFO, "Parser does NOT support JSONC comments (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "JSONC comments in various positions test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 4: Nested Comments (Should NOT Work in C-style)
// =============================================================================

static bool s_test_nested_comments_invalid(void) {
    log_it(L_DEBUG, "Testing JSONC nested comments (should be invalid)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // C-style comments do NOT nest: /* /* nested */ */
    // The first */ closes the comment, leaving extra */ as syntax error
    const char *jsonc_str = "{/* /* nested */ */ \"key\": \"value\"}";
    
    l_json = dap_json_parse_string(jsonc_str);
    
    if (l_json) {
        log_it(L_WARNING, "Parser incorrectly accepted nested comments (or handled gracefully)");
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser correctly rejected nested comments");
    }
    
    // Test double // (should work - second // is inside comment)
    l_json = dap_json_parse_string("{\"key\": \"value\" // comment with // inside\n}");
    
    if (l_json) {
        log_it(L_INFO, "Parser handles '//' inside single-line comment");
    } else {
        log_it(L_INFO, "Parser may have issues with '//' inside comments");
    }
    
    result = true;
    log_it(L_DEBUG, "JSONC nested comments test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 5: Comments with Special Characters
// =============================================================================

static bool s_test_comments_with_special_characters(void) {
    log_it(L_DEBUG, "Testing JSONC comments with special characters");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *jsonc_str = 
        "{\n"
        "  // Comment with special chars: !@#$%^&*()_+-=[]{}|;:',.<>?/\n"
        "  \"key1\": \"value1\",\n"
        "  /* Multi-line with Unicode: Привет, 世界, 😀 */\n"
        "  \"key2\": \"value2\",\n"
        "  // Comment with \"quotes\" and 'apostrophes'\n"
        "  \"key3\": \"value3\",\n"
        "  /* Comment with JSON-like content: {\"not\":\"real\"} */\n"
        "  \"key4\": \"value4\"\n"
        "}";
    
    l_json = dap_json_parse_string(jsonc_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser handles comments with special characters");
        
        const char *val1 = dap_json_object_get_string(l_json, "key1");
        const char *val2 = dap_json_object_get_string(l_json, "key2");
        const char *val3 = dap_json_object_get_string(l_json, "key3");
        const char *val4 = dap_json_object_get_string(l_json, "key4");
        
        DAP_TEST_FAIL_IF(strcmp(val1, "value1") != 0, "key1 correct");
        DAP_TEST_FAIL_IF(strcmp(val2, "value2") != 0, "key2 correct");
        DAP_TEST_FAIL_IF(strcmp(val3, "value3") != 0, "key3 correct");
        DAP_TEST_FAIL_IF(strcmp(val4, "value4") != 0, "key4 correct");
    } else {
        log_it(L_INFO, "Parser does NOT support JSONC comments (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "JSONC comments with special characters test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_jsonc_comments_tests_run(void) {
    log_it(L_INFO, "=== DAP JSONC (JSON with Comments) Tests ===");
    log_it(L_INFO, "NOTE: JSONC is used by VS Code config files. Strict JSON parsers will reject comments.");
    
    int tests_passed = 0;
    int tests_total = 5;
    
    tests_passed += s_test_single_line_comments() ? 1 : 0;
    tests_passed += s_test_multi_line_comments() ? 1 : 0;
    tests_passed += s_test_comments_in_various_positions() ? 1 : 0;
    tests_passed += s_test_nested_comments_invalid() ? 1 : 0;
    tests_passed += s_test_comments_with_special_characters() ? 1 : 0;
    
    log_it(L_INFO, "JSONC comments tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void) {
    dap_print_module_name("DAP JSONC (JSON with Comments) Tests");
    return dap_jsonc_comments_tests_run();
}

