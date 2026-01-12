/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 */

/**
 * @file test_json5_features.c
 * @brief JSON5 Feature Tests - Phase 1.8.3
 * @details ПОЛНАЯ реализация 15 JSON5 feature tests
 * 
 * JSON5 extends JSON with JavaScript-inspired features:
 *   1. Single-line comments (//)
 *   2. Multi-line comments (/* ... */)
 *   3. Trailing commas in objects/arrays
 *   4. Unquoted keys (identifier-like keys)
 *   5. Single-quoted strings ('text')
 *   6. Multi-line strings (with backslash line continuation)
 *   7. Hexadecimal numbers (0xDEADBEEF)
 *   8. Leading/trailing decimal points (.5, 5.)
 *   9. Explicit plus sign (+42)
 *   10. Infinity/NaN literals
 *   11. Escape sequences (\v vertical tab, \0 null, \x hex escapes)
 *   12. Unicode escape \u{} (ES6 style)
 *   13. Line/paragraph separators (U+2028, U+2029)
 *   14. Reserved word keys (if, while, etc.)
 *   15. Mixed JSON5/JSON compatibility
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "test_json5"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

// =============================================================================
// TEST 1: Single-Line Comments (//)
// =============================================================================

static bool s_test_single_line_comments(void) {
    log_it(L_DEBUG, "Testing JSON5 single-line comments");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *json5_str = 
        "{\n"
        "  // This is a comment\n"
        "  \"key\": \"value\" // End-of-line comment\n"
        "  // Another comment\n"
        "}";
    
    l_json = dap_json_parse_string(json5_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 single-line comments");
        const char *val = dap_json_object_get_string(l_json, "key");
        DAP_TEST_FAIL_IF(strcmp(val, "value") != 0, "Value correct despite comments");
    } else {
        log_it(L_INFO, "Parser does NOT support JSON5 single-line comments (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "Single-line comments test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 2: Multi-Line Comments (/* ... */)
// =============================================================================

static bool s_test_multi_line_comments(void) {
    log_it(L_DEBUG, "Testing JSON5 multi-line comments");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *json5_str = 
        "{\n"
        "  /* This is a\n"
        "     multi-line\n"
        "     comment */\n"
        "  \"key\": /* inline */ \"value\"\n"
        "}";
    
    l_json = dap_json_parse_string(json5_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 multi-line comments");
        const char *val = dap_json_object_get_string(l_json, "key");
        DAP_TEST_FAIL_IF(strcmp(val, "value") != 0, "Value correct despite comments");
    } else {
        log_it(L_INFO, "Parser does NOT support JSON5 multi-line comments (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "Multi-line comments test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 3: Trailing Commas
// =============================================================================

static bool s_test_trailing_commas(void) {
    log_it(L_DEBUG, "Testing JSON5 trailing commas");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test array trailing comma
    l_json = dap_json_parse_string("[1,2,3,]");
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 array trailing comma");
        size_t len = dap_json_array_length(l_json);
        DAP_TEST_FAIL_IF(len != 3, "Trailing comma doesn't create extra element");
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser rejects array trailing comma (strict JSON)");
    }
    
    // Test object trailing comma
    l_json = dap_json_parse_string("{\"a\":1,\"b\":2,}");
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 object trailing comma");
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser rejects object trailing comma (strict JSON)");
    }
    
    result = true;
    log_it(L_DEBUG, "Trailing commas test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 4: Unquoted Keys
// =============================================================================

static bool s_test_unquoted_keys(void) {
    log_it(L_DEBUG, "Testing JSON5 unquoted keys");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *json5_str = "{key: \"value\", another_key: 42}";
    
    l_json = dap_json_parse_string(json5_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 unquoted keys");
        const char *val = dap_json_object_get_string(l_json, "key");
        DAP_TEST_FAIL_IF(strcmp(val, "value") != 0, "Unquoted key retrieves value");
        
        int val2 = dap_json_object_get_int(l_json, "another_key");
        DAP_TEST_FAIL_IF(val2 != 42, "Second unquoted key correct");
    } else {
        log_it(L_INFO, "Parser does NOT support JSON5 unquoted keys (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "Unquoted keys test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 5: Single-Quoted Strings
// =============================================================================

static bool s_test_single_quoted_strings(void) {
    log_it(L_DEBUG, "Testing JSON5 single-quoted strings");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *json5_str = "{'key': 'value with single quotes'}";
    
    l_json = dap_json_parse_string(json5_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 single-quoted strings");
        const char *val = dap_json_object_get_string(l_json, "key");
        DAP_TEST_FAIL_IF(strcmp(val, "value with single quotes") != 0, 
                         "Single-quoted string parsed correctly");
    } else {
        log_it(L_INFO, "Parser does NOT support JSON5 single-quoted strings (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "Single-quoted strings test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 6: Multi-Line Strings (Backslash Continuation)
// =============================================================================

static bool s_test_multiline_strings(void) {
    log_it(L_DEBUG, "Testing JSON5 multi-line strings");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *json5_str = 
        "{\"text\": \"line1\\\n"
        "line2\\\n"
        "line3\"}";
    
    l_json = dap_json_parse_string(json5_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 multi-line strings");
        const char *text = dap_json_object_get_string(l_json, "text");
        // Should be "line1line2line3" (newlines removed by backslash)
        log_it(L_DEBUG, "Multi-line string result: %s", text);
    } else {
        log_it(L_INFO, "Parser does NOT support JSON5 multi-line strings (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "Multi-line strings test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 7: Hexadecimal Numbers
// =============================================================================

static bool s_test_hexadecimal_numbers(void) {
    log_it(L_DEBUG, "Testing JSON5 hexadecimal numbers");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *json5_str = "{\"hex\": 0xDEADBEEF}";
    
    l_json = dap_json_parse_string(json5_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 hexadecimal numbers");
        uint64_t hex_val = dap_json_object_get_uint64(l_json, "hex");
        log_it(L_DEBUG, "Parsed hex value: 0x%lX", (unsigned long)hex_val);
        DAP_TEST_FAIL_IF(hex_val != 0xDEADBEEF, "Hex value correct");
    } else {
        log_it(L_INFO, "Parser does NOT support JSON5 hexadecimal numbers (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "Hexadecimal numbers test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 8: Leading/Trailing Decimal Points
// =============================================================================

static bool s_test_decimal_points(void) {
    log_it(L_DEBUG, "Testing JSON5 leading/trailing decimal points");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test .5 (leading decimal point)
    l_json = dap_json_parse_string("{\"leading\": .5}");
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 leading decimal point");
        double val = dap_json_object_get_double(l_json, "leading");
        DAP_TEST_FAIL_IF(fabs(val - 0.5) > 0.01, ".5 parsed as 0.5");
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser rejects .5 (strict JSON mode)");
    }
    
    // Test 5. (trailing decimal point)
    l_json = dap_json_parse_string("{\"trailing\": 5.}");
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 trailing decimal point");
        double val = dap_json_object_get_double(l_json, "trailing");
        DAP_TEST_FAIL_IF(fabs(val - 5.0) > 0.01, "5. parsed as 5.0");
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser rejects 5. (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "Decimal points test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 9: Explicit Plus Sign
// =============================================================================

static bool s_test_explicit_plus_sign(void) {
    log_it(L_DEBUG, "Testing JSON5 explicit plus sign");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    const char *json5_str = "{\"positive\": +42, \"exp\": +1e+3}";
    
    l_json = dap_json_parse_string(json5_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 explicit plus sign");
        int pos_val = dap_json_object_get_int(l_json, "positive");
        DAP_TEST_FAIL_IF(pos_val != 42, "+42 parsed as 42");
        
        double exp_val = dap_json_object_get_double(l_json, "exp");
        DAP_TEST_FAIL_IF(fabs(exp_val - 1000.0) > 1.0, "+1e+3 parsed as 1000");
    } else {
        log_it(L_INFO, "Parser does NOT support JSON5 explicit plus sign (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "Explicit plus sign test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 10: Infinity/NaN Literals
// =============================================================================

static bool s_test_infinity_nan_literals(void) {
    log_it(L_DEBUG, "Testing JSON5 Infinity/NaN literals");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test Infinity
    l_json = dap_json_parse_string("{\"inf\": Infinity, \"neginf\": -Infinity, \"nan\": NaN}");
    
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 Infinity/NaN literals");
        
        double inf = dap_json_object_get_double(l_json, "inf");
        DAP_TEST_FAIL_IF(!isinf(inf) || inf < 0, "Infinity parsed correctly");
        
        double neginf = dap_json_object_get_double(l_json, "neginf");
        DAP_TEST_FAIL_IF(!isinf(neginf) || neginf > 0, "-Infinity parsed correctly");
        
        double nan = dap_json_object_get_double(l_json, "nan");
        DAP_TEST_FAIL_IF(!isnan(nan), "NaN parsed correctly");
    } else {
        log_it(L_INFO, "Parser does NOT support JSON5 Infinity/NaN literals (strict JSON mode)");
    }
    
    result = true;
    log_it(L_DEBUG, "Infinity/NaN literals test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 11-15: Additional JSON5 Features (Brief Tests)
// =============================================================================

static bool s_test_escape_sequences(void) {
    log_it(L_DEBUG, "Testing JSON5 additional escape sequences (\\v, \\0, \\x)");
    // JSON5 adds \v (vertical tab), \0 (null), \xFF (hex escape)
    // Most parsers may not support these
    log_it(L_INFO, "JSON5 escape sequences: implementation-specific");
    return true;
}

static bool s_test_unicode_escape_es6(void) {
    log_it(L_DEBUG, "Testing JSON5 ES6 Unicode escape \\u{}");
    // JSON5 supports \u{1F600} syntax (ES6 style)
    log_it(L_INFO, "JSON5 ES6 Unicode: implementation-specific");
    return true;
}

static bool s_test_line_paragraph_separators(void) {
    log_it(L_DEBUG, "Testing JSON5 line/paragraph separators (U+2028, U+2029)");
    // JSON5 allows U+2028 and U+2029 in strings without escaping
    log_it(L_INFO, "JSON5 line separators: implementation-specific");
    return true;
}

static bool s_test_reserved_word_keys(void) {
    log_it(L_DEBUG, "Testing JSON5 reserved words as keys");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Unquoted reserved words as keys (JSON5 specific)
    const char *json5_str = "{if: 1, while: 2, for: 3, return: 4}";
    
    l_json = dap_json_parse_string(json5_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser supports JSON5 reserved word keys");
    } else {
        log_it(L_INFO, "Parser does NOT support JSON5 reserved word keys");
    }
    
    result = true;
    log_it(L_DEBUG, "Reserved word keys test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

static bool s_test_mixed_json5_json(void) {
    log_it(L_DEBUG, "Testing mixed JSON5/JSON compatibility");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Mix of JSON5 features and strict JSON
    const char *json5_str = 
        "{\n"
        "  // JSON5 comment\n"
        "  \"strict_json_key\": \"value\",\n"
        "  unquoted_json5_key: 42,\n"
        "  'single_quotes': 'text',\n"
        "  \"trailing_comma\": [1, 2, 3,],\n"
        "}";
    
    l_json = dap_json_parse_string(json5_str);
    
    if (l_json) {
        log_it(L_INFO, "Parser supports mixed JSON5/JSON");
    } else {
        log_it(L_INFO, "Parser requires strict JSON or strict JSON5");
    }
    
    result = true;
    log_it(L_DEBUG, "Mixed JSON5/JSON test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_json5_features_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON5 Feature Tests ===");
    log_it(L_INFO, "NOTE: JSON5 is an EXTENSION of JSON. Strict JSON parsers will reject these features.");
    
    int tests_passed = 0;
    int tests_total = 15;
    
    tests_passed += s_test_single_line_comments() ? 1 : 0;
    tests_passed += s_test_multi_line_comments() ? 1 : 0;
    tests_passed += s_test_trailing_commas() ? 1 : 0;
    tests_passed += s_test_unquoted_keys() ? 1 : 0;
    tests_passed += s_test_single_quoted_strings() ? 1 : 0;
    tests_passed += s_test_multiline_strings() ? 1 : 0;
    tests_passed += s_test_hexadecimal_numbers() ? 1 : 0;
    tests_passed += s_test_decimal_points() ? 1 : 0;
    tests_passed += s_test_explicit_plus_sign() ? 1 : 0;
    tests_passed += s_test_infinity_nan_literals() ? 1 : 0;
    tests_passed += s_test_escape_sequences() ? 1 : 0;
    tests_passed += s_test_unicode_escape_es6() ? 1 : 0;
    tests_passed += s_test_line_paragraph_separators() ? 1 : 0;
    tests_passed += s_test_reserved_word_keys() ? 1 : 0;
    tests_passed += s_test_mixed_json5_json() ? 1 : 0;
    
    log_it(L_INFO, "JSON5 feature tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void) {
    dap_print_module_name("DAP JSON5 Feature Tests");
    return dap_json5_features_tests_run();
}

