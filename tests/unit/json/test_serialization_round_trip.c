/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 */

/**
 * @file test_serialization_round_trip.c
 * @brief Serialization Round-Trip Tests - Phase 1.8.2
 * @details ПОЛНАЯ реализация 8 serialization round-trip tests
 * 
 * Tests:
 *   1. Parse → serialize → parse → compare (simple object)
 *   2. Whitespace normalization (input vs output formatting)
 *   3. Key ordering preservation (if supported)
 *   4. Special characters preservation (Unicode, escapes)
 *   5. Large data round-trip (10K elements array/object)
 *   6. Deeply nested round-trip (depth 20)
 *   7. Mixed types round-trip (heterogeneous data)
 *   8. Precision preservation (IEEE 754 double round-trip)
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "test_serialization"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

// =============================================================================
// TEST 1: Simple Object Round-Trip
// =============================================================================

static bool s_test_simple_object_round_trip(void) {
    log_it(L_DEBUG, "Testing simple object round-trip");
    bool result = false;
    dap_json_t *l_json1 = NULL;
    dap_json_t *l_json2 = NULL;
    char *serialized = NULL;
    
    const char *original = "{\"name\":\"John\",\"age\":30,\"verified\":true}";
    
    // Parse original
    l_json1 = dap_json_parse_string(original);
    DAP_TEST_FAIL_IF_NULL(l_json1, "Parse original");
    
    // Serialize
    serialized = dap_json_to_string(l_json1);
    DAP_TEST_FAIL_IF_NULL(serialized, "Serialize");
    
    log_it(L_DEBUG, "Original:   %s", original);
    log_it(L_DEBUG, "Serialized: %s", serialized);
    
    // Parse serialized
    l_json2 = dap_json_parse_string(serialized);
    DAP_TEST_FAIL_IF_NULL(l_json2, "Parse serialized");
    
    // Compare values
    const char *name1 = dap_json_object_get_string(l_json1, "name");
    const char *name2 = dap_json_object_get_string(l_json2, "name");
    DAP_TEST_FAIL_IF_NULL(name1, "Get name from original");
    DAP_TEST_FAIL_IF_NULL(name2, "Get name from serialized");
    if (strcmp(name1, name2) != 0) {
        log_it(L_ERROR, "Name mismatch: '%s' vs '%s' (len %zu vs %zu)", 
               name1, name2, strlen(name1), strlen(name2));
    }
    DAP_TEST_FAIL_IF(strcmp(name1, name2) != 0, "Name preserved");
    
    int age1 = dap_json_object_get_int(l_json1, "age");
    int age2 = dap_json_object_get_int(l_json2, "age");
    DAP_TEST_FAIL_IF(age1 != age2, "Age preserved");
    
    bool verified1 = dap_json_object_get_bool(l_json1, "verified");
    bool verified2 = dap_json_object_get_bool(l_json2, "verified");
    DAP_TEST_FAIL_IF(verified1 != verified2, "Boolean preserved");
    
    result = true;
    log_it(L_DEBUG, "Simple object round-trip test passed");
    
cleanup:
    DAP_DELETE(serialized);
    dap_json_object_free(l_json1);
    dap_json_object_free(l_json2);
    return result;
}

// =============================================================================
// TEST 2: Whitespace Normalization
// =============================================================================

static bool s_test_whitespace_normalization(void) {
    log_it(L_DEBUG, "Testing whitespace normalization");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *serialized = NULL;
    
    // Input with excessive whitespace
    const char *input = "  \n\t  {  \"key\"  :  \"value\"  ,  \"arr\"  :  [  1  ,  2  ]  }  \n\t  ";
    
    l_json = dap_json_parse_string(input);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse with excessive whitespace");
    
    // Serialize (should normalize whitespace)
    serialized = dap_json_to_string(l_json);
    DAP_TEST_FAIL_IF_NULL(serialized, "Serialize");
    
    log_it(L_DEBUG, "Normalized: %s", serialized);
    
    // Check that serialized version is compact (or consistently formatted)
    // Most implementations produce compact JSON by default
    DAP_TEST_FAIL_IF(strlen(serialized) > strlen(input), 
                     "Serialized is more compact than input");
    
    // Re-parse and verify
    dap_json_object_free(l_json);
    l_json = dap_json_parse_string(serialized);
    DAP_TEST_FAIL_IF_NULL(l_json, "Re-parse normalized");
    
    const char *key_val = dap_json_object_get_string(l_json, "key");
    DAP_TEST_FAIL_IF(strcmp(key_val, "value") != 0, "Value preserved after normalization");
    
    result = true;
    log_it(L_DEBUG, "Whitespace normalization test passed");
    
cleanup:
    DAP_DELETE(serialized);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 3: Key Ordering Preservation
// =============================================================================

static bool s_test_key_ordering_preservation(void) {
    log_it(L_DEBUG, "Testing key ordering preservation");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *serialized = NULL;
    
    const char *input = "{\"z\":1,\"a\":2,\"m\":3}";
    
    l_json = dap_json_parse_string(input);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse with specific key order");
    
    serialized = dap_json_to_string(l_json);
    DAP_TEST_FAIL_IF_NULL(serialized, "Serialize");
    
    log_it(L_DEBUG, "Input:      %s", input);
    log_it(L_DEBUG, "Serialized: %s", serialized);
    
    // RFC 8259: key ordering is implementation-defined
    // Log whether ordering is preserved, but don't fail if it's not
    if (strstr(serialized, "\"z\"") < strstr(serialized, "\"a\"") &&
        strstr(serialized, "\"a\"") < strstr(serialized, "\"m\"")) {
        log_it(L_INFO, "Key ordering preserved (insertion order)");
    } else if (strstr(serialized, "\"a\"") < strstr(serialized, "\"m\"") &&
               strstr(serialized, "\"m\"") < strstr(serialized, "\"z\"")) {
        log_it(L_INFO, "Keys sorted alphabetically");
    } else {
        log_it(L_INFO, "Key ordering: implementation-defined");
    }
    
    // Verify values are preserved regardless of order
    dap_json_object_free(l_json);
    l_json = dap_json_parse_string(serialized);
    
    int z_val = dap_json_object_get_int(l_json, "z");
    int a_val = dap_json_object_get_int(l_json, "a");
    int m_val = dap_json_object_get_int(l_json, "m");
    
    DAP_TEST_FAIL_IF(z_val != 1, "z=1 preserved");
    DAP_TEST_FAIL_IF(a_val != 2, "a=2 preserved");
    DAP_TEST_FAIL_IF(m_val != 3, "m=3 preserved");
    
    result = true;
    log_it(L_DEBUG, "Key ordering preservation test passed");
    
cleanup:
    DAP_DELETE(serialized);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 4: Special Characters Preservation
// =============================================================================

static bool s_test_special_characters_preservation(void) {
    log_it(L_DEBUG, "Testing special characters preservation");
    bool result = false;
    dap_json_t *l_json1 = NULL;
    dap_json_t *l_json2 = NULL;
    char *serialized = NULL;
    
    // Unicode, escapes, special chars
    const char *input = "{\"unicode\":\"Привет, 世界, 😀\",\"escapes\":\"\\n\\t\\\"\\\\\",\"special\":\"@#$%\"}";
    
    l_json1 = dap_json_parse_string(input);
    DAP_TEST_FAIL_IF_NULL(l_json1, "Parse special characters");
    
    serialized = dap_json_to_string(l_json1);
    DAP_TEST_FAIL_IF_NULL(serialized, "Serialize special characters");
    
    log_it(L_DEBUG, "Original:   %s", input);
    log_it(L_DEBUG, "Serialized: %s", serialized);
    
    // Re-parse
    l_json2 = dap_json_parse_string(serialized);
    DAP_TEST_FAIL_IF_NULL(l_json2, "Re-parse special characters");
    
    // Compare
    const char *unicode1 = dap_json_object_get_string(l_json1, "unicode");
    const char *unicode2 = dap_json_object_get_string(l_json2, "unicode");
    DAP_TEST_FAIL_IF_NULL(unicode1, "Get unicode from original");
    DAP_TEST_FAIL_IF_NULL(unicode2, "Get unicode from serialized");
    DAP_TEST_FAIL_IF(strcmp(unicode1, unicode2) != 0, "Unicode preserved");
    
    const char *escapes1 = dap_json_object_get_string(l_json1, "escapes");
    const char *escapes2 = dap_json_object_get_string(l_json2, "escapes");
    DAP_TEST_FAIL_IF(strcmp(escapes1, escapes2) != 0, "Escapes preserved");
    
    const char *special1 = dap_json_object_get_string(l_json1, "special");
    const char *special2 = dap_json_object_get_string(l_json2, "special");
    DAP_TEST_FAIL_IF(strcmp(special1, special2) != 0, "Special chars preserved");
    
    result = true;
    log_it(L_DEBUG, "Special characters preservation test passed");
    
cleanup:
    DAP_DELETE(serialized);
    dap_json_object_free(l_json1);
    dap_json_object_free(l_json2);
    return result;
}

// =============================================================================
// TEST 5: Large Data Round-Trip
// =============================================================================

static bool s_test_large_data_round_trip(void) {
    log_it(L_DEBUG, "Testing large data round-trip (10K elements)");
    bool result = false;
    dap_json_t *l_json1 = NULL;
    dap_json_t *l_json2 = NULL;
    char *json_buf = NULL;
    char *serialized = NULL;
    
    const int ELEMENT_COUNT = 10000;
    const size_t BUF_SIZE = ELEMENT_COUNT * 16 + 32;
    
    json_buf = (char*)malloc(BUF_SIZE);
    DAP_TEST_FAIL_IF_NULL(json_buf, "Allocate buffer");
    
    // Create large array
    strcpy(json_buf, "[");
    char *ptr = json_buf + 1;
    for (int i = 0; i < ELEMENT_COUNT; i++) {
        if (i > 0) *ptr++ = ',';
        ptr += sprintf(ptr, "%d", i);
    }
    strcpy(ptr, "]");
    
    log_it(L_INFO, "Parsing %d elements (%zu bytes)", ELEMENT_COUNT, strlen(json_buf));
    
    // Parse
    l_json1 = dap_json_parse_string(json_buf);
    DAP_TEST_FAIL_IF_NULL(l_json1, "Parse large array");
    
    // Serialize
    serialized = dap_json_to_string(l_json1);
    DAP_TEST_FAIL_IF_NULL(serialized, "Serialize large array");
    
    log_it(L_INFO, "Serialized: %zu bytes", strlen(serialized));
    
    // Re-parse
    l_json2 = dap_json_parse_string(serialized);
    DAP_TEST_FAIL_IF_NULL(l_json2, "Re-parse large array");
    
    // Verify
    size_t len1 = dap_json_array_length(l_json1);
    size_t len2 = dap_json_array_length(l_json2);
    DAP_TEST_FAIL_IF(len1 != len2, "Array lengths match");
    DAP_TEST_FAIL_IF(len1 != (size_t)ELEMENT_COUNT, "Array length correct");
    
    // Spot check
    int val_first = dap_json_array_get_int(l_json2, 0);
    int val_mid = dap_json_array_get_int(l_json2, ELEMENT_COUNT / 2);
    int val_last = dap_json_array_get_int(l_json2, ELEMENT_COUNT - 1);
    
    DAP_TEST_FAIL_IF(val_first != 0, "First element preserved");
    DAP_TEST_FAIL_IF(val_mid != (int)(ELEMENT_COUNT / 2), "Middle element preserved");
    DAP_TEST_FAIL_IF(val_last != (int)(ELEMENT_COUNT - 1), "Last element preserved");
    
    result = true;
    log_it(L_DEBUG, "Large data round-trip test passed");
    
cleanup:
    free(json_buf);
    DAP_DELETE(serialized);
    dap_json_object_free(l_json1);
    dap_json_object_free(l_json2);
    return result;
}

// =============================================================================
// TEST 6: Deeply Nested Round-Trip
// =============================================================================

static bool s_test_deeply_nested_round_trip(void) {
    log_it(L_DEBUG, "Testing deeply nested round-trip (depth 20)");
    bool result = false;
    dap_json_t *l_json1 = NULL;
    dap_json_t *l_json2 = NULL;
    char *json_buf = NULL;
    char *serialized = NULL;
    
    const int DEPTH = 20;
    const size_t BUF_SIZE = DEPTH * 16 + 32;
    
    json_buf = (char*)malloc(BUF_SIZE);
    DAP_TEST_FAIL_IF_NULL(json_buf, "Allocate buffer");
    
    // Create deeply nested: {"a":{"a":{"a":...{"value":42}}}}
    char *ptr = json_buf;
    for (int i = 0; i < DEPTH; i++) {
        ptr += sprintf(ptr, "{\"a\":");
    }
    ptr += sprintf(ptr, "42");
    for (int i = 0; i < DEPTH; i++) {
        *ptr++ = '}';
    }
    *ptr = '\0';
    
    log_it(L_INFO, "Nesting depth: %d", DEPTH);
    
    // Parse
    l_json1 = dap_json_parse_string(json_buf);
    DAP_TEST_FAIL_IF_NULL(l_json1, "Parse deeply nested");
    
    // Serialize
    serialized = dap_json_to_string(l_json1);
    DAP_TEST_FAIL_IF_NULL(serialized, "Serialize deeply nested");
    
    // Re-parse
    l_json2 = dap_json_parse_string(serialized);
    DAP_TEST_FAIL_IF_NULL(l_json2, "Re-parse deeply nested");
    
    // Navigate to deepest value
    dap_json_t *current = l_json2;
    for (int i = 0; i < DEPTH - 1; i++) {
        current = dap_json_object_get_object(current, "a");
        DAP_TEST_FAIL_IF_NULL(current, "Navigate depth level");
    }
    
    int final_val = dap_json_object_get_int(current, "a");
    DAP_TEST_FAIL_IF(final_val != 42, "Deepest value preserved");
    
    result = true;
    log_it(L_DEBUG, "Deeply nested round-trip test passed");
    
cleanup:
    free(json_buf);
    DAP_DELETE(serialized);
    dap_json_object_free(l_json1);
    dap_json_object_free(l_json2);
    return result;
}

// =============================================================================
// TEST 7: Mixed Types Round-Trip
// =============================================================================

static bool s_test_mixed_types_round_trip(void) {
    log_it(L_DEBUG, "Testing mixed types round-trip");
    bool result = false;
    dap_json_t *l_json1 = NULL;
    dap_json_t *l_json2 = NULL;
    char *serialized = NULL;
    
    const char *input = 
        "{\"int\":42,\"double\":3.14,\"string\":\"text\",\"bool\":true,"
        "\"null\":null,\"array\":[1,2,3],\"object\":{\"nested\":\"value\"}}";
    
    l_json1 = dap_json_parse_string(input);
    DAP_TEST_FAIL_IF_NULL(l_json1, "Parse mixed types");
    
    serialized = dap_json_to_string(l_json1);
    DAP_TEST_FAIL_IF_NULL(serialized, "Serialize mixed types");
    
    l_json2 = dap_json_parse_string(serialized);
    DAP_TEST_FAIL_IF_NULL(l_json2, "Re-parse mixed types");
    
    // Verify each type
    int int_val = dap_json_object_get_int(l_json2, "int");
    DAP_TEST_FAIL_IF(int_val != 42, "Int preserved");
    
    double dbl_val = dap_json_object_get_double(l_json2, "double");
    DAP_TEST_FAIL_IF(fabs(dbl_val - 3.14) > 0.01, "Double preserved");
    
    const char *str_val = dap_json_object_get_string(l_json2, "string");
    DAP_TEST_FAIL_IF_NULL(str_val, "Get string from serialized");
    DAP_TEST_FAIL_IF(strcmp(str_val, "text") != 0, "String preserved");
    
    bool bool_val = dap_json_object_get_bool(l_json2, "bool");
    DAP_TEST_FAIL_IF(!bool_val, "Bool preserved");
    
    dap_json_t *arr = dap_json_object_get_array(l_json2, "array");
    DAP_TEST_FAIL_IF_NULL(arr, "Array preserved");
    DAP_TEST_FAIL_IF(dap_json_array_length(arr) != 3UL, "Array length preserved");
    
    dap_json_t *obj = dap_json_object_get_object(l_json2, "object");
    DAP_TEST_FAIL_IF_NULL(obj, "Object preserved");
    
    const char *nested_val = dap_json_object_get_string(obj, "nested");
    DAP_TEST_FAIL_IF(strcmp(nested_val, "value") != 0, "Nested value preserved");
    
    result = true;
    log_it(L_DEBUG, "Mixed types round-trip test passed");
    
cleanup:
    DAP_DELETE(serialized);
    dap_json_object_free(l_json1);
    dap_json_object_free(l_json2);
    return result;
}

// =============================================================================
// TEST 8: IEEE 754 Double Precision Preservation
// =============================================================================

static bool s_test_ieee754_precision_preservation(void) {
    log_it(L_DEBUG, "Testing IEEE 754 double precision preservation");
    bool result = false;
    dap_json_t *l_json1 = NULL;
    dap_json_t *l_json2 = NULL;
    char *serialized = NULL;
    
    // Test various IEEE 754 edge cases
    const char *input = 
        "{\"pi\":3.141592653589793,"
        "\"e\":2.718281828459045,"
        "\"small\":1.0e-308,"
        "\"large\":1.0e+308,"
        "\"neg\":-123.456}";
    
    l_json1 = dap_json_parse_string(input);
    DAP_TEST_FAIL_IF_NULL(l_json1, "Parse IEEE 754 doubles");
    
    serialized = dap_json_to_string(l_json1);
    DAP_TEST_FAIL_IF_NULL(serialized, "Serialize IEEE 754 doubles");
    
    log_it(L_DEBUG, "Serialized: %s", serialized);
    
    l_json2 = dap_json_parse_string(serialized);
    DAP_TEST_FAIL_IF_NULL(l_json2, "Re-parse IEEE 754 doubles");
    
    // Compare with tolerance
    double pi1 = dap_json_object_get_double(l_json1, "pi");
    double pi2 = dap_json_object_get_double(l_json2, "pi");
    DAP_TEST_FAIL_IF(fabs(pi1 - pi2) > 1e-15, "Pi preserved with high precision");
    
    double e1 = dap_json_object_get_double(l_json1, "e");
    double e2 = dap_json_object_get_double(l_json2, "e");
    DAP_TEST_FAIL_IF(fabs(e1 - e2) > 1e-15, "e preserved with high precision");
    
    double small1 = dap_json_object_get_double(l_json1, "small");
    double small2 = dap_json_object_get_double(l_json2, "small");
    DAP_TEST_FAIL_IF(fabs(small1 - small2) / small1 > 1e-10, "Small double preserved");
    
    double large1 = dap_json_object_get_double(l_json1, "large");
    double large2 = dap_json_object_get_double(l_json2, "large");
    DAP_TEST_FAIL_IF(fabs(large1 - large2) / large1 > 1e-10, "Large double preserved");
    
    result = true;
    log_it(L_DEBUG, "IEEE 754 precision preservation test passed");
    
cleanup:
    DAP_DELETE(serialized);
    dap_json_object_free(l_json1);
    dap_json_object_free(l_json2);
    return result;
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_json_serialization_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Serialization Round-Trip Tests ===");
    
    int tests_passed = 0;
    int tests_total = 8;
    
    tests_passed += s_test_simple_object_round_trip() ? 1 : 0;
    tests_passed += s_test_whitespace_normalization() ? 1 : 0;
    tests_passed += s_test_key_ordering_preservation() ? 1 : 0;
    tests_passed += s_test_special_characters_preservation() ? 1 : 0;
    tests_passed += s_test_large_data_round_trip() ? 1 : 0;
    tests_passed += s_test_deeply_nested_round_trip() ? 1 : 0;
    tests_passed += s_test_mixed_types_round_trip() ? 1 : 0;
    tests_passed += s_test_ieee754_precision_preservation() ? 1 : 0;
    
    log_it(L_INFO, "Serialization round-trip tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void) {
    dap_print_module_name("DAP JSON Serialization Round-Trip Tests");
    return dap_json_serialization_tests_run();
}

