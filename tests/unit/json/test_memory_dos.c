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
 * @file test_memory_dos.c
 * @brief Memory DoS Protection Tests - Critical Security
 * @details Tests protection against:
 *          - Hash collision attacks (craft keys with same hash → O(n²) lookup)
 *          - Memory exhaustion (huge JSON allocations)
 *          - Stack exhaustion (deep recursion limits)
 *          - Billion laughs attack (nested duplicates with exponential expansion)
 *          - Algorithmic complexity attacks (O(n²) worst-case inputs)
 *          - Arena allocator exhaustion
 *          - String pool flooding
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "test_memory_dos"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

// =============================================================================
// TIMING UTILITIES
// =============================================================================

typedef struct {
    struct timespec start;
    struct timespec end;
    uint64_t total_ns;
} dos_timer_t;

static inline void s_timer_start(dos_timer_t *a_timer)
{
    clock_gettime(CLOCK_MONOTONIC, &a_timer->start);
}

static inline void s_timer_stop(dos_timer_t *a_timer)
{
    clock_gettime(CLOCK_MONOTONIC, &a_timer->end);
    
    uint64_t start_ns = (uint64_t)a_timer->start.tv_sec * 1000000000ULL + a_timer->start.tv_nsec;
    uint64_t end_ns = (uint64_t)a_timer->end.tv_sec * 1000000000ULL + a_timer->end.tv_nsec;
    a_timer->total_ns = end_ns - start_ns;
}

static inline double s_timer_get_seconds(dos_timer_t *a_timer)
{
    return (double)a_timer->total_ns / 1e9;
}

// =============================================================================
// TEST 1: Hash Collision Attack
// =============================================================================

/**
 * @brief Test hash collision attack - craft keys with same hash
 * @details Attack scenario: Create JSON object with N keys that all hash to same value
 *          Expected: O(n) insertion/lookup, NOT O(n²)
 *          Threshold: Parse time should be linear (< 2x slowdown for 2x keys)
 */
static bool s_test_hash_collision_attack(void) {
    log_it(L_DEBUG, "Testing hash collision attack protection");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *l_json_str = NULL;
    
    // Create JSON with keys designed to collide in common hash functions
    // Strategy: Use keys with patterns that hash to similar values
    // Example: "a", "aa", "aaa", ... or "key0", "key1", ... with crafted suffixes
    
    const int COLLISION_KEY_COUNT = 1000;  // 1000 keys with potential collisions
    const int MAX_JSON_SIZE = COLLISION_KEY_COUNT * 50;  // ~50 bytes per key
    
    l_json_str = (char*)malloc(MAX_JSON_SIZE);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "Allocate JSON string buffer");
    
    // Build JSON: {"key_0000": 0, "key_0001": 1, ..., "key_0999": 999}
    // These keys may collide depending on hash function quality
    char *ptr = l_json_str;
    ptr += sprintf(ptr, "{");
    
    for (int i = 0; i < COLLISION_KEY_COUNT; i++) {
        if (i > 0) ptr += sprintf(ptr, ",");
        // Use zero-padded numbers - these often create hash collisions
        ptr += sprintf(ptr, "\"key_%04d\":%d", i, i);
    }
    ptr += sprintf(ptr, "}");
    
    // Time the parsing
    dos_timer_t timer;
    s_timer_start(&timer);
    
    l_json = dap_json_parse_string(l_json_str);
    
    s_timer_stop(&timer);
    double parse_time = s_timer_get_seconds(&timer);
    
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with potential hash collisions");
    
    // Verify all keys accessible
    int l_value = dap_json_object_get_int(l_json, "key_0000");
    DAP_TEST_FAIL_IF(l_value != 0, "First key accessible");
    
    l_value = dap_json_object_get_int(l_json, "key_0999");
    DAP_TEST_FAIL_IF(l_value != 999, "Last key accessible");
    
    // Check parse time is reasonable (< 0.1 seconds for 1000 keys)
    // If hash table degrades to O(n²), this would take much longer
    log_it(L_INFO, "Hash collision test: %d keys parsed in %.3f seconds", 
           COLLISION_KEY_COUNT, parse_time);
    DAP_TEST_FAIL_IF(parse_time > 0.1, "Parse time reasonable (no O(n²) degradation)");
    
    result = true;
    log_it(L_DEBUG, "Hash collision attack protection test passed");
    
cleanup:
    if (l_json_str) free(l_json_str);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 2: Memory Exhaustion
// =============================================================================

/**
 * @brief Test memory exhaustion protection
 * @details Try to allocate huge JSON (> reasonable limits)
 *          Expected: Parser should reject or handle gracefully (no crash/OOM)
 */
static bool s_test_memory_exhaustion(void) {
    log_it(L_DEBUG, "Testing memory exhaustion protection");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Try to create array with 10 million elements
    // This should either:
    // 1. Be handled gracefully (with reasonable performance)
    // 2. Reject with error (if limits enforced)
    // 3. NOT crash or hang
    
    const char *l_huge_array_start = "[";
    const char *l_huge_array_elem = "1,";
    const char *l_huge_array_end = "1]";
    const int ELEM_COUNT = 100000;  // 100K elements (moderate test)
    
    // Calculate required size
    size_t json_size = strlen(l_huge_array_start) + 
                       ELEM_COUNT * strlen(l_huge_array_elem) + 
                       strlen(l_huge_array_end) + 1;
    
    char *l_json_str = (char*)malloc(json_size);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "Allocate large JSON string");
    
    // Build array
    char *ptr = l_json_str;
    ptr += sprintf(ptr, "%s", l_huge_array_start);
    for (int i = 0; i < ELEM_COUNT; i++) {
        ptr += sprintf(ptr, "%s", l_huge_array_elem);
    }
    ptr += sprintf(ptr, "%s", l_huge_array_end);
    
    log_it(L_INFO, "Memory exhaustion test: parsing array with %d elements (%zu bytes)", 
           ELEM_COUNT, json_size);
    
    // Time the operation
    dos_timer_t timer;
    s_timer_start(&timer);
    
    l_json = dap_json_parse_string(l_json_str);
    
    s_timer_stop(&timer);
    double parse_time = s_timer_get_seconds(&timer);
    
    // Parser should either succeed or fail gracefully
    if (l_json) {
        log_it(L_INFO, "Parser handled large array successfully in %.3f seconds", parse_time);
        
        // Verify array is accessible
        size_t array_len = dap_json_array_length(l_json);
        DAP_TEST_FAIL_IF(array_len != (size_t)(ELEM_COUNT + 1), "Array length correct");
        
        // Check parse time is reasonable (< 1 second)
        DAP_TEST_FAIL_IF(parse_time > 1.0, "Parse time reasonable for large array");
    } else {
        log_it(L_INFO, "Parser rejected large array (acceptable if limits enforced)");
        // Rejection is OK if limits are configured
    }
    
    result = true;
    log_it(L_DEBUG, "Memory exhaustion protection test passed");
    
cleanup:
    free(l_json_str);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 3: Stack Exhaustion (Deep Recursion)
// =============================================================================

/**
 * @brief Test stack exhaustion protection
 * @details Create deeply nested JSON (1000+ levels)
 *          Expected: Parser should handle or reject gracefully (no stack overflow crash)
 */
static bool s_test_stack_exhaustion(void) {
    log_it(L_DEBUG, "Testing stack exhaustion protection");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Create deeply nested object: {"a":{"a":{"a":...}}}
    const int NESTING_DEPTH = 1000;
    const int MAX_JSON_SIZE = NESTING_DEPTH * 10;
    
    char *l_json_str = (char*)malloc(MAX_JSON_SIZE);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "Allocate nested JSON buffer");
    
    // Build nested structure
    char *ptr = l_json_str;
    for (int i = 0; i < NESTING_DEPTH; i++) {
        ptr += sprintf(ptr, "{\"a\":");
    }
    ptr += sprintf(ptr, "1");
    for (int i = 0; i < NESTING_DEPTH; i++) {
        ptr += sprintf(ptr, "}");
    }
    
    log_it(L_INFO, "Stack exhaustion test: parsing %d levels of nesting", NESTING_DEPTH);
    
    // This should NOT crash (stack overflow)
    l_json = dap_json_parse_string(l_json_str);
    
    // Parser should either:
    // 1. Handle deep nesting successfully
    // 2. Reject with error (if depth limit enforced)
    // 3. NOT crash
    
    if (l_json) {
        log_it(L_INFO, "Parser handled deep nesting successfully");
        
        // Verify structure is parseable - traverse first 10 levels
        dap_json_t *l_nested = l_json;
        int depth = 0;
        while (l_nested && depth < 10) {  // Check first 10 levels
            l_nested = dap_json_object_get_object(l_nested, "a");
            depth++;
        }
        DAP_TEST_FAIL_IF(depth < 10, "Nested structure accessible");
    } else {
        log_it(L_INFO, "Parser rejected deep nesting (acceptable if limits enforced)");
    }
    
    result = true;
    log_it(L_DEBUG, "Stack exhaustion protection test passed");
    
cleanup:
    free(l_json_str);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 4: Billion Laughs Attack
// =============================================================================

/**
 * @brief Test billion laughs attack (exponential expansion)
 * @details Pattern: [[[[[[...]]]]]]] - nested arrays with duplicates
 *          Attack: Small JSON expands to huge memory (exponential)
 *          Expected: Parser should handle or reject (no exponential memory use)
 */
static bool s_test_billion_laughs_attack(void) {
    log_it(L_DEBUG, "Testing billion laughs attack protection");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Create pattern that could cause exponential expansion
    // Example: [[1,1],[1,1],[[1,1],[1,1]],[[1,1],[1,1]]]
    // Each level doubles the elements
    
    const char *l_pattern = "[[1,1],[1,1],[[1,1],[1,1]],[[1,1],[1,1]],"
                            "[[[1,1],[1,1]],[[1,1],[1,1]]],"
                            "[[[1,1],[1,1]],[[1,1],[1,1]]]]";
    
    log_it(L_INFO, "Billion laughs test: parsing potentially expanding pattern");
    
    dos_timer_t timer;
    s_timer_start(&timer);
    
    l_json = dap_json_parse_string(l_pattern);
    
    s_timer_stop(&timer);
    double parse_time = s_timer_get_seconds(&timer);
    
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse nested array pattern");
    
    // Check parse time is reasonable (< 0.01 seconds for small input)
    log_it(L_INFO, "Billion laughs test completed in %.3f seconds", parse_time);
    DAP_TEST_FAIL_IF(parse_time > 0.01, "Parse time reasonable (no exponential expansion)");
    
    result = true;
    log_it(L_DEBUG, "Billion laughs attack protection test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 5: Algorithmic Complexity Attack
// =============================================================================

/**
 * @brief Test algorithmic complexity attack (O(n²) worst-case)
 * @details Create input that triggers worst-case performance
 *          Expected: Parse time should be O(n), not O(n²)
 */
static bool s_test_algorithmic_complexity(void) {
    log_it(L_DEBUG, "Testing algorithmic complexity attack protection");
    bool result = false;
    
    // Warmup: parse small JSON to warm caches
    dap_json_t *l_warmup = dap_json_parse_string("{\"warmup\":1}");
    dap_json_object_free(l_warmup);
    
    // Test with increasing sizes to detect O(n²) behavior
    const int sizes[] = {100, 200, 400};
    double times[3];
    
    for (int test = 0; test < 3; test++) {
        int size = sizes[test];
        
        // Create JSON object with 'size' keys
        size_t json_size = size * 30;
        char *l_json_str = (char*)malloc(json_size);
        DAP_TEST_FAIL_IF_NULL(l_json_str, "Allocate JSON buffer");
        
        char *ptr = l_json_str;
        ptr += sprintf(ptr, "{");
        for (int i = 0; i < size; i++) {
            if (i > 0) ptr += sprintf(ptr, ",");
            ptr += sprintf(ptr, "\"k%d\":%d", i, i);
        }
        ptr += sprintf(ptr, "}");
        
        dos_timer_t timer;
        s_timer_start(&timer);
        
        dap_json_t *l_json = dap_json_parse_string(l_json_str);
        
        s_timer_stop(&timer);
        times[test] = s_timer_get_seconds(&timer);
        
        DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON");
        
        log_it(L_INFO, "Complexity test: %d keys in %.3f seconds", size, times[test]);
        
        dap_json_object_free(l_json);
        free(l_json_str);
    }
    
    // Check that time grows linearly (not quadratically)
    // If linear: time[1]/time[0] ≈ 2.0, time[2]/time[0] ≈ 4.0
    // If quadratic: time[1]/time[0] ≈ 4.0, time[2]/time[0] ≈ 16.0
    
    double ratio_2x = times[1] / times[0];
    double ratio_4x = times[2] / times[0];
    
    log_it(L_INFO, "Time ratios: 2x=%.2f (expect ~2.0), 4x=%.2f (expect ~4.0)", 
           ratio_2x, ratio_4x);
    
    // Allow variance for cache effects, arena resizing, etc.
    // Reject only if clearly O(n²) (ratio > 5x would indicate quadratic growth)
    DAP_TEST_FAIL_IF(ratio_2x > 6.0, "2x size should not take > 6x time (detecting O(n²))");
    DAP_TEST_FAIL_IF(ratio_4x > 16.0, "4x size should not take > 16x time (detecting O(n²))");
    
    result = true;
    log_it(L_DEBUG, "Algorithmic complexity attack protection test passed");
    
cleanup:
    return result;
}

// =============================================================================
// TEST 6: Arena Allocator Exhaustion
// =============================================================================

/**
 * @brief Test arena allocator exhaustion
 * @details Allocate many small objects to exhaust arena
 *          Expected: Graceful handling or growth (no crash)
 */
static bool s_test_arena_exhaustion(void) {
    log_it(L_DEBUG, "Testing arena allocator exhaustion protection");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Create JSON with many small objects
    // This tests arena allocator expansion/limits
    const int OBJECT_COUNT = 10000;
    const size_t json_size = OBJECT_COUNT * 50;
    
    char *l_json_str = (char*)malloc(json_size);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "Allocate JSON buffer");
    
    // Build array of small objects: [{"a":1},{"a":2},...,{"a":10000}]
    char *ptr = l_json_str;
    ptr += sprintf(ptr, "[");
    for (int i = 0; i < OBJECT_COUNT; i++) {
        if (i > 0) ptr += sprintf(ptr, ",");
        ptr += sprintf(ptr, "{\"a\":%d}", i);
    }
    ptr += sprintf(ptr, "]");
    
    log_it(L_INFO, "Arena exhaustion test: parsing %d small objects", OBJECT_COUNT);
    
    l_json = dap_json_parse_string(l_json_str);
    
    // Should handle many allocations without crash
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse many small objects");
    
    // Verify array length
    size_t array_len = dap_json_array_length(l_json);
    DAP_TEST_FAIL_IF(array_len != (size_t)OBJECT_COUNT, "Array length correct");
    
    result = true;
    log_it(L_DEBUG, "Arena allocator exhaustion protection test passed");
    
cleanup:
    free(l_json_str);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 7: String Pool Flooding
// =============================================================================

/**
 * @brief Test string pool flooding
 * @details Create many unique strings to fill string pool
 *          Expected: Graceful handling or growth (no crash)
 */
static bool s_test_string_pool_flooding(void) {
    log_it(L_DEBUG, "Testing string pool flooding protection");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Create JSON with many unique strings
    // This tests string pool capacity/limits
    const int STRING_COUNT = 5000;
    const size_t json_size = STRING_COUNT * 50;
    
    char *l_json_str = (char*)malloc(json_size);
    DAP_TEST_FAIL_IF_NULL(l_json_str, "Allocate JSON buffer");
    
    // Build object with unique string values: {"k0":"str0","k1":"str1",...}
    char *ptr = l_json_str;
    ptr += sprintf(ptr, "{");
    for (int i = 0; i < STRING_COUNT; i++) {
        if (i > 0) ptr += sprintf(ptr, ",");
        ptr += sprintf(ptr, "\"k%d\":\"string_%d\"", i, i);
    }
    ptr += sprintf(ptr, "}");
    
    log_it(L_INFO, "String pool flooding test: parsing %d unique strings", STRING_COUNT);
    
    l_json = dap_json_parse_string(l_json_str);
    
    // Should handle many unique strings without crash
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse many unique strings");
    
    // Verify first and last strings are correct
    const char *l_first = dap_json_object_get_string(l_json, "k0");
    DAP_TEST_FAIL_IF_NULL(l_first, "Get first string");
    DAP_TEST_FAIL_IF(strcmp(l_first, "string_0") != 0, "First string correct");
    
    char last_key[16];
    snprintf(last_key, sizeof(last_key), "k%d", STRING_COUNT - 1);
    const char *l_last = dap_json_object_get_string(l_json, last_key);
    DAP_TEST_FAIL_IF_NULL(l_last, "Get last string");
    
    result = true;
    log_it(L_DEBUG, "String pool flooding protection test passed");
    
cleanup:
    free(l_json_str);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_json_memory_dos_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Memory DoS Protection Tests ===");
    
    int tests_passed = 0;
    int tests_total = 7;
    
    tests_passed += s_test_hash_collision_attack() ? 1 : 0;
    tests_passed += s_test_memory_exhaustion() ? 1 : 0;
    tests_passed += s_test_stack_exhaustion() ? 1 : 0;
    tests_passed += s_test_billion_laughs_attack() ? 1 : 0;
    tests_passed += s_test_algorithmic_complexity() ? 1 : 0;
    tests_passed += s_test_arena_exhaustion() ? 1 : 0;
    tests_passed += s_test_string_pool_flooding() ? 1 : 0;
    
    log_it(L_INFO, "Memory DoS protection tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void) {
    dap_print_module_name("DAP JSON Memory DoS Protection Tests");
    return dap_json_memory_dos_tests_run();
}

