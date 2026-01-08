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
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"

#define LOG_TAG "dap_json_invalid_tests"

/**
 * @brief Test trailing commas in objects
 */
static bool s_test_trailing_comma_object(void) {
    log_it(L_DEBUG, "Testing trailing comma in object");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Trailing comma after last property (invalid in JSON, valid in JSON5)
    const char *l_invalid = "{\"a\":1,\"b\":2,}";
    
    l_json = dap_json_parse_string(l_invalid);
    // Standard JSON parser should reject this
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects trailing comma in object");
    
    result = true;
    log_it(L_DEBUG, "Trailing comma in object test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test trailing commas in arrays
 */
static bool s_test_trailing_comma_array(void) {
    log_it(L_DEBUG, "Testing trailing comma in array");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Trailing comma after last element
    const char *l_invalid = "[1,2,3,]";
    
    l_json = dap_json_parse_string(l_invalid);
    // Standard JSON parser should reject this
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects trailing comma in array");
    
    result = true;
    log_it(L_DEBUG, "Trailing comma in array test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test missing commas
 */
static bool s_test_missing_comma(void) {
    log_it(L_DEBUG, "Testing missing comma");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Missing comma between properties
    const char *l_invalid = "{\"a\":1 \"b\":2}";
    
    l_json = dap_json_parse_string(l_invalid);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects missing comma");
    
    result = true;
    log_it(L_DEBUG, "Missing comma test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test unclosed string
 */
static bool s_test_unclosed_string(void) {
    log_it(L_DEBUG, "Testing unclosed string");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // String not closed
    const char *l_invalid = "{\"unclosed\":\"value}";
    
    l_json = dap_json_parse_string(l_invalid);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects unclosed string");
    
    result = true;
    log_it(L_DEBUG, "Unclosed string test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test unclosed object
 */
static bool s_test_unclosed_object(void) {
    log_it(L_DEBUG, "Testing unclosed object");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Object not closed
    const char *l_invalid = "{\"key\":\"value\"";
    
    l_json = dap_json_parse_string(l_invalid);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects unclosed object");
    
    result = true;
    log_it(L_DEBUG, "Unclosed object test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test unclosed array
 */
static bool s_test_unclosed_array(void) {
    log_it(L_DEBUG, "Testing unclosed array");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Array not closed
    const char *l_invalid = "[1,2,3";
    
    l_json = dap_json_parse_string(l_invalid);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects unclosed array");
    
    result = true;
    log_it(L_DEBUG, "Unclosed array test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test comments (invalid in standard JSON)
 */
static bool s_test_comments_invalid(void) {
    log_it(L_DEBUG, "Testing comments (invalid in JSON)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // C++ style comment (invalid in JSON, valid in JSONC/JSON5)
    const char *l_invalid1 = "{\"a\":1, // comment\n\"b\":2}";
    l_json = dap_json_parse_string(l_invalid1);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects // comments");
    
    // C style comment (invalid in JSON, valid in JSONC/JSON5)
    const char *l_invalid2 = "{\"a\":1, /* comment */ \"b\":2}";
    l_json = dap_json_parse_string(l_invalid2);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects /* */ comments");
    
    result = true;
    log_it(L_DEBUG, "Comments invalid test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test single quotes (invalid in JSON)
 */
static bool s_test_single_quotes_invalid(void) {
    log_it(L_DEBUG, "Testing single quotes (invalid)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Single quotes for strings (invalid in JSON, valid in JSON5)
    const char *l_invalid = "{'key':'value'}";
    
    l_json = dap_json_parse_string(l_invalid);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects single quotes");
    
    result = true;
    log_it(L_DEBUG, "Single quotes invalid test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test unquoted keys (invalid in JSON)
 */
static bool s_test_unquoted_keys_invalid(void) {
    log_it(L_DEBUG, "Testing unquoted keys (invalid)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Unquoted keys (invalid in JSON, valid in JSON5)
    const char *l_invalid = "{key:\"value\"}";
    
    l_json = dap_json_parse_string(l_invalid);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects unquoted keys");
    
    result = true;
    log_it(L_DEBUG, "Unquoted keys invalid test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test missing colon
 */
static bool s_test_missing_colon(void) {
    log_it(L_DEBUG, "Testing missing colon");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Missing colon between key and value
    const char *l_invalid = "{\"key\" \"value\"}";
    
    l_json = dap_json_parse_string(l_invalid);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects missing colon");
    
    result = true;
    log_it(L_DEBUG, "Missing colon test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test extra closing bracket
 */
static bool s_test_extra_closing_bracket(void) {
    log_it(L_DEBUG, "Testing extra closing bracket");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Extra closing bracket
    const char *l_invalid = "{\"key\":\"value\"}}";
    
    l_json = dap_json_parse_string(l_invalid);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects extra closing bracket");
    
    result = true;
    log_it(L_DEBUG, "Extra closing bracket test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test mismatched brackets
 */
static bool s_test_mismatched_brackets(void) {
    log_it(L_DEBUG, "Testing mismatched brackets");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Object opened, array closed
    const char *l_invalid1 = "{\"key\":\"value\"]";
    l_json = dap_json_parse_string(l_invalid1);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects { closed with ]");
    
    // Array opened, object closed
    const char *l_invalid2 = "[1,2,3}";
    l_json = dap_json_parse_string(l_invalid2);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects [ closed with }");
    
    result = true;
    log_it(L_DEBUG, "Mismatched brackets test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test bare values (not in object/array)
 */
static bool s_test_bare_values(void) {
    log_it(L_DEBUG, "Testing bare values");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Bare string (not in object/array) - technically valid in some parsers
    const char *l_bare_string = "\"just a string\"";
    l_json = dap_json_parse_string(l_bare_string);
    // This may or may not be accepted depending on parser strictness
    // Just ensure it doesn't crash
    dap_json_object_free(l_json);
    
    // Bare number
    const char *l_bare_number = "42";
    l_json = dap_json_parse_string(l_bare_number);
    dap_json_object_free(l_json);
    
    result = true;
    log_it(L_DEBUG, "Bare values test passed");
    
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test duplicate keys in object
 */
static bool s_test_duplicate_keys(void) {
    log_it(L_DEBUG, "Testing duplicate keys");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Duplicate keys (technically valid JSON, last one wins)
    const char *l_json_str = "{\"key\":\"first\",\"key\":\"second\"}";
    
    l_json = dap_json_parse_string(l_json_str);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with duplicate keys");
    
    // Should get the last value
    const char *l_value = dap_json_object_get_string(l_json, "key");
    DAP_TEST_FAIL_IF_NULL(l_value, "Get value for duplicate key");
    DAP_TEST_FAIL_IF(strcmp(l_value, "second") != 0, "Last value wins for duplicate keys");
    
    result = true;
    log_it(L_DEBUG, "Duplicate keys test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test empty input
 */
static bool s_test_empty_input(void) {
    log_it(L_DEBUG, "Testing empty input");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Empty string
    const char *l_empty = "";
    l_json = dap_json_parse_string(l_empty);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects empty string");
    
    // Whitespace only
    const char *l_whitespace = "   \n\t  ";
    l_json = dap_json_parse_string(l_whitespace);
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects whitespace-only input");
    
    result = true;
    log_it(L_DEBUG, "Empty input test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Main test runner for invalid JSON tests
 */
int dap_json_invalid_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Invalid Format Tests ===");
    
    int tests_passed = 0;
    int tests_total = 15;
    
    tests_passed += s_test_trailing_comma_object() ? 1 : 0;
    tests_passed += s_test_trailing_comma_array() ? 1 : 0;
    tests_passed += s_test_missing_comma() ? 1 : 0;
    tests_passed += s_test_unclosed_string() ? 1 : 0;
    tests_passed += s_test_unclosed_object() ? 1 : 0;
    tests_passed += s_test_unclosed_array() ? 1 : 0;
    tests_passed += s_test_comments_invalid() ? 1 : 0;
    tests_passed += s_test_single_quotes_invalid() ? 1 : 0;
    tests_passed += s_test_unquoted_keys_invalid() ? 1 : 0;
    tests_passed += s_test_missing_colon() ? 1 : 0;
    tests_passed += s_test_extra_closing_bracket() ? 1 : 0;
    tests_passed += s_test_mismatched_brackets() ? 1 : 0;
    tests_passed += s_test_bare_values() ? 1 : 0;
    tests_passed += s_test_duplicate_keys() ? 1 : 0;
    tests_passed += s_test_empty_input() ? 1 : 0;
    
    log_it(L_INFO, "Invalid JSON tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

/**
 * @brief Main entry point
 */
int main(void) {
    dap_print_module_name("DAP JSON Invalid Format Tests");
    dap_json_init(); // Initialize Stage 1 dispatch
    return dap_json_invalid_tests_run();
}

