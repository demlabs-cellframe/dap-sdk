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
#include "dap_time.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define LOG_TAG "dap_json_benchmark"

// Helper to get current time in microseconds
static uint64_t get_time_usec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/**
 * @brief Benchmark parsing small JSON (< 1KB)
 */
static bool s_benchmark_parse_small(void) {
    log_it(L_DEBUG, "Benchmarking small JSON parsing");
    bool result = false;
    
    const char *l_small_json = "{\"name\":\"John\",\"age\":30,\"city\":\"New York\","
                                "\"email\":\"john@example.com\",\"active\":true}";
    
    const int l_iterations = 100000;
    uint64_t l_start = get_time_usec();
    
    for (int i = 0; i < l_iterations; i++) {
        dap_json_t *l_json = dap_json_parse_string(l_small_json);
        if (!l_json) {
            log_it(L_ERROR, "Parse failed at iteration %d", i);
            goto cleanup;
        }
        dap_json_object_free(l_json);
    }
    
    uint64_t l_end = get_time_usec();
    uint64_t l_elapsed = l_end - l_start;
    double l_ops_per_sec = (double)l_iterations / ((double)l_elapsed / 1000000.0);
    double l_usec_per_op = (double)l_elapsed / (double)l_iterations;
    
    log_it(L_INFO, "Small JSON parsing: %.2f ops/sec, %.2f µs/op", 
           l_ops_per_sec, l_usec_per_op);
    
    result = true;
    
cleanup:
    return result;
}

/**
 * @brief Benchmark parsing medium JSON (~10KB)
 */
static bool s_benchmark_parse_medium(void) {
    log_it(L_DEBUG, "Benchmarking medium JSON parsing");
    bool result = false;
    char *l_medium_json = NULL;
    
    // Build medium JSON (~10KB) with 100 objects
    dap_json_t *l_root = dap_json_object_new();
    dap_json_t *l_array = dap_json_array_new();
    
    for (int i = 0; i < 100; i++) {
        dap_json_t *l_item = dap_json_object_new();
        dap_json_object_add_int(l_item, "id", i);
        dap_json_object_add_string(l_item, "name", "Item");
        dap_json_object_add_double(l_item, "value", 123.456);
        dap_json_object_add_bool(l_item, "active", i % 2 == 0);
        dap_json_array_add(l_array, l_item);
    }
    
    dap_json_object_add_array(l_root, "items", l_array);
    l_medium_json = dap_json_to_string(l_root);  // Creates new string (must be freed)
    dap_json_object_free(l_root);
    
    if (!l_medium_json) {
        log_it(L_ERROR, "Failed to create medium JSON");
        goto cleanup;
    }
    
    log_it(L_DEBUG, "Medium JSON size: %zu bytes", strlen(l_medium_json));
    
    const int l_iterations = 10000;
    uint64_t l_start = get_time_usec();
    
    for (int i = 0; i < l_iterations; i++) {
        dap_json_t *l_json = dap_json_parse_string(l_medium_json);
        if (!l_json) {
            log_it(L_ERROR, "Parse failed at iteration %d", i);
            goto cleanup;
        }
        dap_json_object_free(l_json);
    }
    
    uint64_t l_end = get_time_usec();
    uint64_t l_elapsed = l_end - l_start;
    double l_ops_per_sec = (double)l_iterations / ((double)l_elapsed / 1000000.0);
    double l_usec_per_op = (double)l_elapsed / (double)l_iterations;
    double l_mb_per_sec = ((double)strlen(l_medium_json) * l_iterations) / 
                          ((double)l_elapsed / 1000000.0) / (1024.0 * 1024.0);
    
    log_it(L_INFO, "Medium JSON parsing: %.2f ops/sec, %.2f µs/op, %.2f MB/s", 
           l_ops_per_sec, l_usec_per_op, l_mb_per_sec);
    
    result = true;
    
cleanup:
    if (l_medium_json) DAP_DELETE(l_medium_json);
    return result;
}

/**
 * @brief Benchmark serialization
 */
static bool s_benchmark_serialization(void) {
    log_it(L_DEBUG, "Benchmarking JSON serialization");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Create JSON object with various types
    l_json = dap_json_object_new();
    dap_json_object_add_string(l_json, "name", "Benchmark");
    dap_json_object_add_int(l_json, "count", 12345);
    dap_json_object_add_double(l_json, "pi", 3.14159265359);
    dap_json_object_add_bool(l_json, "flag", true);
    
    // Add array
    dap_json_t *l_array = dap_json_array_new();
    for (int i = 0; i < 50; i++) {
        dap_json_array_add(l_array, dap_json_object_new_int(i));
    }
    dap_json_object_add_array(l_json, "numbers", l_array);
    
    const int l_iterations = 50000;
    uint64_t l_start = get_time_usec();
    
    for (int i = 0; i < l_iterations; i++) {
        char *l_str = dap_json_to_string(l_json);
        if (!l_str) {
            log_it(L_ERROR, "Serialization failed at iteration %d", i);
            goto cleanup;
        }
        DAP_DELETE(l_str);
    }
    
    uint64_t l_end = get_time_usec();
    uint64_t l_elapsed = l_end - l_start;
    double l_ops_per_sec = (double)l_iterations / ((double)l_elapsed / 1000000.0);
    double l_usec_per_op = (double)l_elapsed / (double)l_iterations;
    
    log_it(L_INFO, "JSON serialization: %.2f ops/sec, %.2f µs/op", 
           l_ops_per_sec, l_usec_per_op);
    
    result = true;
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Benchmark object creation and manipulation
 */
static bool s_benchmark_object_manipulation(void) {
    log_it(L_DEBUG, "Benchmarking object creation and manipulation");
    bool result = false;
    
    const int l_iterations = 100000;
    uint64_t l_start = get_time_usec();
    
    for (int i = 0; i < l_iterations; i++) {
        dap_json_t *l_json = dap_json_object_new();
        if (!l_json) {
            log_it(L_ERROR, "Object creation failed at iteration %d", i);
            goto cleanup;
        }
        
        dap_json_object_add_string(l_json, "key1", "value1");
        dap_json_object_add_int(l_json, "key2", i);
        dap_json_object_add_double(l_json, "key3", 3.14);
        
        // Retrieve values
        const char *l_str = dap_json_object_get_string(l_json, "key1");
        int l_int = dap_json_object_get_int(l_json, "key2");
        double l_dbl = dap_json_object_get_double(l_json, "key3");
        
        // Suppress unused warnings
        (void)l_str; (void)l_int; (void)l_dbl;
        
        dap_json_object_free(l_json);
    }
    
    uint64_t l_end = get_time_usec();
    uint64_t l_elapsed = l_end - l_start;
    double l_ops_per_sec = (double)l_iterations / ((double)l_elapsed / 1000000.0);
    double l_usec_per_op = (double)l_elapsed / (double)l_iterations;
    
    log_it(L_INFO, "Object manipulation: %.2f ops/sec, %.2f µs/op", 
           l_ops_per_sec, l_usec_per_op);
    
    result = true;
    
cleanup:
    return result;
}

/**
 * @brief Benchmark memory usage baseline
 */
static bool s_benchmark_memory_usage(void) {
    log_it(L_DEBUG, "Benchmarking memory usage");
    bool result = false;
    dap_json_t **l_objects = NULL;
    
    const int l_count = 10000;
    l_objects = (dap_json_t**)calloc(l_count, sizeof(dap_json_t*));
    if (!l_objects) {
        log_it(L_ERROR, "Failed to allocate objects array");
        goto cleanup;
    }
    
    // Create many objects
    for (int i = 0; i < l_count; i++) {
        l_objects[i] = dap_json_object_new();
        if (!l_objects[i]) {
            log_it(L_ERROR, "Failed to create object %d", i);
            goto cleanup;
        }
        
        dap_json_object_add_string(l_objects[i], "data", "test data for memory benchmark");
        dap_json_object_add_int(l_objects[i], "index", i);
        dap_json_object_add_double(l_objects[i], "value", i * 1.5);
    }
    
    log_it(L_INFO, "Created %d JSON objects successfully", l_count);
    
    // Clean up
    for (int i = 0; i < l_count; i++) {
        if (l_objects[i]) {
            dap_json_object_free(l_objects[i]);
            l_objects[i] = NULL;  // Prevent double-free in cleanup
        }
    }
    
    log_it(L_INFO, "Memory benchmark: %d objects created and freed successfully", l_count);
    
    result = true;
    
cleanup:
    if (l_objects) {
        for (int i = 0; i < l_count; i++) {
            if (l_objects[i]) {
                dap_json_object_free(l_objects[i]);
            }
        }
        free(l_objects);
    }
    return result;
}

/**
 * @brief Main test runner for performance baseline benchmarks
 */
int dap_json_benchmark_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Performance Baseline Benchmarks ===");
    log_it(L_INFO, "NOTE: These establish baseline with json-c for Phase 2 comparisons");
    
    int tests_passed = 0;
    int tests_total = 5;
    
    tests_passed += s_benchmark_parse_small() ? 1 : 0;
    tests_passed += s_benchmark_parse_medium() ? 1 : 0;
    tests_passed += s_benchmark_serialization() ? 1 : 0;
    tests_passed += s_benchmark_object_manipulation() ? 1 : 0;
    tests_passed += s_benchmark_memory_usage() ? 1 : 0;
    
    log_it(L_INFO, "Performance benchmarks: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

/**
 * @brief Main entry point
 */
int main(void) {
    dap_print_module_name("DAP JSON Performance Baseline Benchmarks");
    return dap_json_benchmark_tests_run();
}
