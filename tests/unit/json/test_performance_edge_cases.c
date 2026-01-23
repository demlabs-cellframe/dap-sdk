/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2026
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

/**
 * @file test_performance_edge_cases.c
 * @brief Performance Edge Cases Tests - Phase 1.8.4
 * @details Full implementation of 10 performance edge case tests
 * 
 * Performance edge cases that stress parser performance:
 *   1. Heavily escaped strings (every character escaped)
 *   2. Very long strings (100KB+)
 *   3. Pathological nesting (100+ levels)
 *   4. Wide objects (10000+ keys)
 *   5. Repeated patterns (triggers poor caching)
 *   6. Alternating types (defeats SIMD prediction)
 *   7. Minimal whitespace (dense packing)
 *   8. Maximal whitespace (sparse layout)
 *   9. Interleaved string/number patterns
 *   10. High token density (many small values)
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "dap_json_perf_edge"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

// =============================================================================
// HELPER: Parse and measure performance
// =============================================================================

/**
 * @brief Parse JSON and measure performance metrics
 * @param a_test_name Test name for logging
 * @param a_json_str JSON string to parse
 * @return true if parsing succeeded, false otherwise
 */
static bool s_parse_and_measure(const char *a_test_name, const char *a_json_str) {
    log_it(L_DEBUG, "Testing %s", a_test_name);
    
    clock_t l_start = clock();
    dap_json_t *l_json = dap_json_parse_string(a_json_str);
    clock_t l_end = clock();
    
    double l_elapsed = (double)(l_end - l_start) / CLOCKS_PER_SEC * 1000.0;
    
    if (!l_json) {
        log_it(L_ERROR, "%s: parsing FAILED", a_test_name);
        return false;
    }
    
    size_t l_json_len = strlen(a_json_str);
    double l_mbytes = l_json_len / (1024.0 * 1024.0);
    double l_mbps = l_mbytes / (l_elapsed / 1000.0);
    
    log_it(L_INFO, "%s: %.2f ms, %.2f MB/s (%zu bytes)", 
           a_test_name, l_elapsed, l_mbps, l_json_len);
    
    dap_json_object_free(l_json);
    return true;
}

// =============================================================================
// TEST 1: Heavily Escaped Strings (Every Character Escaped)
// =============================================================================

static bool s_test_heavily_escaped_string(void) {
    char *json_str = NULL;
    bool result = false;
    
    // Build string with every character escaped
    const size_t str_len = 1000;
    const size_t buf_size = str_len * 2 + 100;
    
    json_str = DAP_NEW_Z_SIZE(char, buf_size);
    if (!json_str) {
        log_it(L_ERROR, "Failed to allocate memory");
        return false;
    }
    
    strcpy(json_str, "{\"escaped\":\"");
    size_t offset = strlen(json_str);
    
    const char *escape_sequences[] = {"\\t", "\\n", "\\r", "\\/", "\\b", "\\f", "\\\""};
    const size_t num_escapes = sizeof(escape_sequences) / sizeof(escape_sequences[0]);
    
    for (size_t i = 0; i < str_len; i++) {
        const char *esc = escape_sequences[i % num_escapes];
        strcpy(json_str + offset, esc);
        offset += strlen(esc);
    }
    
    strcpy(json_str + offset, "\"}");
    
    result = s_parse_and_measure("Heavily Escaped String", json_str);
    
    DAP_DEL_Z(json_str);
    return result;
}

// =============================================================================
// TEST 2: Very Long String (100KB+)
// =============================================================================

static bool s_test_very_long_string(void) {
    char *json_str = NULL;
    bool result = false;
    
    const size_t string_len = 100 * 1024;  // 100KB
    const size_t total_size = string_len + 100;
    
    json_str = DAP_NEW_Z_SIZE(char, total_size);
    if (!json_str) {
        log_it(L_ERROR, "Failed to allocate memory");
        return false;
    }
    
    strcpy(json_str, "{\"longstr\":\"");
    size_t offset = strlen(json_str);
    memset(json_str + offset, 'A', string_len);
    offset += string_len;
    strcpy(json_str + offset, "\"}");
    
    result = s_parse_and_measure("Very Long String (100KB)", json_str);
    
    DAP_DEL_Z(json_str);
    return result;
}

// =============================================================================
// TEST 3: Pathological Nesting (150 levels)
// =============================================================================

/**
 * @brief Test deeply nested JSON structures (stress test)
 * @details Tests parser with 150 levels of nesting
 * @return true if parsing succeeded, false otherwise
 */
static bool s_test_pathological_nesting(void) {
    char *l_json_str = NULL;
    bool l_result = false;
    
    const size_t l_depth = 150;
    // Calculate exact buffer size: "{\"a\":" per level (6 bytes) + "42" (2 bytes) + "}" per level (1 byte) + null terminator
    const size_t l_buf_size = (l_depth * 7) + 10;  // 6 for open + 1 for close per level + margin
    
    l_json_str = DAP_NEW_Z_SIZE(char, l_buf_size);
    if (!l_json_str) {
        log_it(L_ERROR, "Failed to allocate memory");
        return false;
    }
    
    // Build opening braces character by character (avoids strcpy null-terminator issues)
    size_t l_offset = 0;
    for (size_t i = 0; i < l_depth; i++) {
        l_json_str[l_offset++] = '{';
        l_json_str[l_offset++] = '"';
        l_json_str[l_offset++] = 'a';
        l_json_str[l_offset++] = '"';
        l_json_str[l_offset++] = ':';
    }
    
    // Add value
    l_json_str[l_offset++] = '4';
    l_json_str[l_offset++] = '2';
    
    // Add closing braces
    for (size_t i = 0; i < l_depth; i++) {
        l_json_str[l_offset++] = '}';
    }
    l_json_str[l_offset] = '\0';
    
    log_it(L_DEBUG, "Generated JSON length: %zu bytes (expected: %zu)", strlen(l_json_str), l_offset);
    
    l_result = s_parse_and_measure("Pathological Nesting (150 levels)", l_json_str);
    
    DAP_DEL_Z(l_json_str);
    return l_result;
}

// =============================================================================
// TEST 4: Wide Object (10000+ Keys)
// =============================================================================

static bool s_test_wide_object(void) {
    char *json_str = NULL;
    bool result = false;
    
    const size_t num_keys = 10000;
    const size_t buf_size = num_keys * 30;
    
    json_str = DAP_NEW_Z_SIZE(char, buf_size);
    if (!json_str) {
        log_it(L_ERROR, "Failed to allocate memory");
        return false;
    }
    
    strcpy(json_str, "{");
    size_t offset = 1;
    
    for (size_t i = 0; i < num_keys; i++) {
        offset += snprintf(json_str + offset, buf_size - offset, 
                          "\"key%zu\":%zu%s", i, i, (i < num_keys - 1) ? "," : "");
    }
    strcpy(json_str + offset, "}");
    
    result = s_parse_and_measure("Wide Object (10000 keys)", json_str);
    
    DAP_DEL_Z(json_str);
    return result;
}

// =============================================================================
// TEST 5: Repeated Patterns (Defeats Caching)
// =============================================================================

static bool s_test_repeated_patterns(void) {
    char *json_str = NULL;
    bool result = false;
    
    const size_t pattern_count = 1000;
    const size_t buf_size = pattern_count * 100;
    
    json_str = DAP_NEW_Z_SIZE(char, buf_size);
    if (!json_str) {
        log_it(L_ERROR, "Failed to allocate memory");
        return false;
    }
    
    strcpy(json_str, "[");
    size_t offset = 1;
    
    for (size_t i = 0; i < pattern_count; i++) {
        const char *patterns[] = {"{\"a\":1}", "[1,2,3]", "42", "\"str\""};
        const char *pattern = patterns[i % 4];
        offset += snprintf(json_str + offset, buf_size - offset, 
                          "%s%s", pattern, (i < pattern_count - 1) ? "," : "");
    }
    strcpy(json_str + offset, "]");
    
    result = s_parse_and_measure("Repeated Patterns (1000 elements)", json_str);
    
    DAP_DEL_Z(json_str);
    return result;
}

// =============================================================================
// TEST 6: Alternating Types (Defeats SIMD Prediction)
// =============================================================================

static bool s_test_alternating_types(void) {
    const char *json_str = 
        "{\"k0\":\"v0\",\"k1\":1,\"k2\":\"v2\",\"k3\":3,"
        "\"k4\":\"v4\",\"k5\":5,\"k6\":\"v6\",\"k7\":7,"
        "\"k8\":\"v8\",\"k9\":9,\"k10\":\"v10\",\"k11\":11,"
        "\"k12\":\"v12\",\"k13\":13,\"k14\":\"v14\",\"k15\":15}";
    
    return s_parse_and_measure("Alternating Types", json_str);
}

// =============================================================================
// TEST 7: Minimal Whitespace (Dense Packing)
// =============================================================================

static bool s_test_minimal_whitespace(void) {
    const char *json_str = "{\"a\":1,\"b\":[2,3,4],\"c\":{\"d\":5}}";
    return s_parse_and_measure("Minimal Whitespace", json_str);
}

// =============================================================================
// TEST 8: Maximal Whitespace (Sparse Layout)
// =============================================================================

static bool s_test_maximal_whitespace(void) {
    const char *json_str = 
        "  {  \n\t \"a\"  :  1  ,  \r\n  \"b\"  :  [  2  ,  3  ,  4  ]  ,  \n"
        "  \"c\"  :  {  \"d\"  :  5  }  }  ";
    
    return s_parse_and_measure("Maximal Whitespace", json_str);
}

// =============================================================================
// TEST 9: Interleaved String/Number Patterns
// =============================================================================

static bool s_test_interleaved_string_number(void) {
    char *json_str = NULL;
    bool result = false;
    
    const size_t count = 1000;
    const size_t buf_size = count * 50;
    
    json_str = DAP_NEW_Z_SIZE(char, buf_size);
    if (!json_str) {
        log_it(L_ERROR, "Failed to allocate memory");
        return false;
    }
    
    strcpy(json_str, "[");
    size_t offset = 1;
    
    for (size_t i = 0; i < count; i++) {
        if (i % 2 == 0) {
            offset += snprintf(json_str + offset, buf_size - offset, 
                              "\"%zu\"%s", i, (i < count - 1) ? "," : "");
        } else {
            offset += snprintf(json_str + offset, buf_size - offset, 
                              "%zu%s", i, (i < count - 1) ? "," : "");
        }
    }
    strcpy(json_str + offset, "]");
    
    result = s_parse_and_measure("Interleaved String/Number (1000 elements)", json_str);
    
    DAP_DEL_Z(json_str);
    return result;
}

// =============================================================================
// TEST 10: High Token Density (Many Small Values)
// =============================================================================

static bool s_test_high_token_density(void) {
    char *json_str = NULL;
    bool result = false;
    
    const size_t count = 5000;
    const size_t buf_size = count * 10;
    
    json_str = DAP_NEW_Z_SIZE(char, buf_size);
    if (!json_str) {
        log_it(L_ERROR, "Failed to allocate memory");
        return false;
    }
    
    strcpy(json_str, "[");
    size_t offset = 1;
    
    for (size_t i = 0; i < count; i++) {
        offset += snprintf(json_str + offset, buf_size - offset, 
                          "%d%s", (int)(i % 10), (i < count - 1) ? "," : "");
    }
    strcpy(json_str + offset, "]");
    
    result = s_parse_and_measure("High Token Density (5000 tokens)", json_str);
    
    DAP_DEL_Z(json_str);
    return result;
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_json_perf_edge_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Performance Edge Cases Tests ===");
    log_it(L_INFO, "Testing parser performance on pathological inputs");
    
    int tests_passed = 0;
    int tests_total = 10;
    
    tests_passed += s_test_heavily_escaped_string() ? 1 : 0;
    tests_passed += s_test_very_long_string() ? 1 : 0;
    tests_passed += s_test_pathological_nesting() ? 1 : 0;
    tests_passed += s_test_wide_object() ? 1 : 0;
    tests_passed += s_test_repeated_patterns() ? 1 : 0;
    tests_passed += s_test_alternating_types() ? 1 : 0;
    tests_passed += s_test_minimal_whitespace() ? 1 : 0;
    tests_passed += s_test_maximal_whitespace() ? 1 : 0;
    tests_passed += s_test_interleaved_string_number() ? 1 : 0;
    tests_passed += s_test_high_token_density() ? 1 : 0;
    
    log_it(L_INFO, "Performance edge case tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void) {
    dap_print_module_name("DAP JSON Performance Edge Cases");
    return dap_json_perf_edge_tests_run();
}
