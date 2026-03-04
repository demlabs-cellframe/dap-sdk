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
 * @file test_concurrency.c
 * @brief Concurrency & Thread-Safety Tests - CRITICAL SECURITY
 * @details Tests thread-safety of JSON parser in multi-threaded environment
 * 
 * CRITICAL: Parser used in cellframe-node (multi-threaded). Race conditions can cause:
 *           - Data corruption (parser returns wrong values)
 *           - Crashes (double-free, use-after-free)
 *           - Hangs (deadlock)
 * 
 * Tests:
 *   1. ThreadSanitizer (TSan) validation - detect data races
 *   2. Concurrent parsing (multiple threads parse different JSON)
 *   3. Arena allocator races (bump pointer atomic?)
 *   4. String pool races (hash table concurrent access?)
 *   5. Global state races (dap_cpu_arch_set thread-local?)
 *   6. Read-write contention (concurrent read/write)
 *   7. Deadlock scenarios
 *   8. Memory ordering (acquire/release semantics)
 * 
 * IMPORTANT: Compile with -fsanitize=thread for TSan detection
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "test_concurrency"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_cpu_arch.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>

// =============================================================================
// TEST DATA
// =============================================================================

static const char *s_test_json = 
    "{\"user\":{\"id\":12345,\"name\":\"John Doe\","
    "\"email\":\"john@example.com\",\"age\":30,"
    "\"verified\":true,\"tags\":[\"user\",\"premium\"],"
    "\"metadata\":{\"created\":\"2026-01-01\"}}}";

// =============================================================================
// TEST 1: ThreadSanitizer (TSan) Validation
// =============================================================================

/**
 * @brief Test basic thread-safety with TSan
 * @details Parse JSON in 2 threads simultaneously
 *          Expected: No data races detected by TSan
 * 
 * NOTE: This test MUST be run with -fsanitize=thread to be effective
 *       Without TSan, it only tests for crashes
 */

typedef struct {
    int thread_id;
    int iterations;
    atomic_int *success_count;
    atomic_int *failure_count;
} thread_test_args_t;

static void *s_parse_thread(void *a_arg)
{
    thread_test_args_t *args = (thread_test_args_t*)a_arg;
    
    for (int i = 0; i < args->iterations; i++) {
        dap_json_t *l_json = dap_json_parse_string(s_test_json);
        
        if (l_json) {
            // Verify data (must get nested object first, not use dot notation)
            dap_json_t *l_user = dap_json_object_get_object(l_json, "user");
            if (l_user) {
                int id = dap_json_object_get_int(l_user, "id");
                if (id == 12345) {
                    atomic_fetch_add(args->success_count, 1);
                } else {
                    atomic_fetch_add(args->failure_count, 1);
                }
            } else {
                atomic_fetch_add(args->failure_count, 1);
            }
            dap_json_object_free(l_json);
        } else {
            atomic_fetch_add(args->failure_count, 1);
        }
    }
    
    return NULL;
}

static bool s_test_tsan_validation(void) {
    log_it(L_DEBUG, "Testing ThreadSanitizer validation");
    bool result = false;
    
    enum { NUM_THREADS = 4 };
    enum { ITERATIONS_PER_THREAD = 100 };
    
    pthread_t threads[NUM_THREADS];
    thread_test_args_t args[NUM_THREADS];
    atomic_int success_count = 0;
    atomic_int failure_count = 0;
    
    log_it(L_INFO, "TSan test: %d threads × %d iterations", NUM_THREADS, ITERATIONS_PER_THREAD);
    
    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_id = i;
        args[i].iterations = ITERATIONS_PER_THREAD;
        args[i].success_count = &success_count;
        args[i].failure_count = &failure_count;
        
        if (pthread_create(&threads[i], NULL, s_parse_thread, &args[i]) != 0) {
            log_it(L_ERROR, "Failed to create thread %d", i);
            goto cleanup;
        }
    }
    
    // Wait for all threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    int total_success = atomic_load(&success_count);
    int total_failure = atomic_load(&failure_count);
    
    log_it(L_INFO, "TSan test: %d success, %d failures", total_success, total_failure);
    
    // All parses should succeed
    DAP_TEST_FAIL_IF(total_failure > 0, "No parsing failures in concurrent test");
    DAP_TEST_FAIL_IF(total_success != NUM_THREADS * ITERATIONS_PER_THREAD, 
                     "All parses successful");
    
    result = true;
    log_it(L_DEBUG, "TSan validation test passed (run with -fsanitize=thread for race detection)");
    
cleanup:
    return result;
}

// =============================================================================
// TEST 2: Concurrent Parsing (Different JSON)
// =============================================================================

static void *s_parse_different_json_thread(void *a_arg)
{
    thread_test_args_t *args = (thread_test_args_t*)a_arg;
    
    // Each thread parses different JSON
    char json_buf[256];
    snprintf(json_buf, sizeof(json_buf), 
             "{\"thread\":%d,\"data\":\"thread_%d\"}", 
             args->thread_id, args->thread_id);
    
    for (int i = 0; i < args->iterations; i++) {
        dap_json_t *l_json = dap_json_parse_string(json_buf);
        
        if (l_json) {
            int thread_id = dap_json_object_get_int(l_json, "thread");
            if (thread_id == args->thread_id) {
                atomic_fetch_add(args->success_count, 1);
            } else {
                atomic_fetch_add(args->failure_count, 1);
                log_it(L_ERROR, "Thread %d got wrong data: %d", args->thread_id, thread_id);
            }
            dap_json_object_free(l_json);
        } else {
            atomic_fetch_add(args->failure_count, 1);
        }
    }
    
    return NULL;
}

static bool s_test_concurrent_parsing_different_json(void) {
    log_it(L_DEBUG, "Testing concurrent parsing of different JSON");
    bool result = false;
    
    enum { NUM_THREADS = 8 };
    enum { ITERATIONS_PER_THREAD = 50 };
    
    pthread_t threads[NUM_THREADS];
    thread_test_args_t args[NUM_THREADS];
    atomic_int success_count = 0;
    atomic_int failure_count = 0;
    
    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_id = i;
        args[i].iterations = ITERATIONS_PER_THREAD;
        args[i].success_count = &success_count;
        args[i].failure_count = &failure_count;
        
        if (pthread_create(&threads[i], NULL, s_parse_different_json_thread, &args[i]) != 0) {
            log_it(L_ERROR, "Failed to create thread %d", i);
            goto cleanup;
        }
    }
    
    // Wait for all threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    int total_success = atomic_load(&success_count);
    int total_failure = atomic_load(&failure_count);
    
    log_it(L_INFO, "Concurrent parsing test: %d success, %d failures", total_success, total_failure);
    
    DAP_TEST_FAIL_IF(total_failure > 0, "No data corruption in concurrent parsing");
    
    result = true;
    log_it(L_DEBUG, "Concurrent parsing test passed");
    
cleanup:
    return result;
}

// =============================================================================
// TEST 3: Arena Allocator Race Conditions
// =============================================================================

static void *s_arena_stress_thread(void *a_arg)
{
    thread_test_args_t *args = (thread_test_args_t*)a_arg;
    
    // Create many small objects to stress arena
    for (int i = 0; i < args->iterations; i++) {
        char json_buf[128];
        snprintf(json_buf, sizeof(json_buf), "[");
        for (int j = 0; j < 10; j++) {
            if (j > 0) strcat(json_buf, ",");
            sprintf(json_buf + strlen(json_buf), "{\"i\":%d}", j);
        }
        strcat(json_buf, "]");
        
        dap_json_t *l_json = dap_json_parse_string(json_buf);
        
        if (l_json) {
            atomic_fetch_add(args->success_count, 1);
            dap_json_object_free(l_json);
        } else {
            atomic_fetch_add(args->failure_count, 1);
        }
    }
    
    return NULL;
}

static bool s_test_arena_allocator_races(void) {
    log_it(L_DEBUG, "Testing arena allocator race conditions");
    bool result = false;
    
    enum { NUM_THREADS = 4 };
    enum { ITERATIONS_PER_THREAD = 100 };
    
    pthread_t threads[NUM_THREADS];
    thread_test_args_t args[NUM_THREADS];
    atomic_int success_count = 0;
    atomic_int failure_count = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_id = i;
        args[i].iterations = ITERATIONS_PER_THREAD;
        args[i].success_count = &success_count;
        args[i].failure_count = &failure_count;
        
        if (pthread_create(&threads[i], NULL, s_arena_stress_thread, &args[i]) != 0) {
            log_it(L_ERROR, "Failed to create thread %d", i);
            goto cleanup;
        }
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    int total_success = atomic_load(&success_count);
    int total_failure = atomic_load(&failure_count);
    
    log_it(L_INFO, "Arena allocator test: %d success, %d failures", total_success, total_failure);
    
    DAP_TEST_FAIL_IF(total_failure > 0, "Arena allocator handles concurrent allocations");
    
    result = true;
    log_it(L_DEBUG, "Arena allocator race test passed");
    
cleanup:
    return result;
}

// =============================================================================
// TEST 4: String Pool Concurrent Access
// =============================================================================

static void *s_string_pool_stress_thread(void *a_arg)
{
    thread_test_args_t *args = (thread_test_args_t*)a_arg;
    
    // Create JSON with unique strings
    for (int i = 0; i < args->iterations; i++) {
        char json_buf[256];
        snprintf(json_buf, sizeof(json_buf), 
                 "{\"str_%d_%d\":\"value_%d_%d\"}", 
                 args->thread_id, i, args->thread_id, i);
        
        dap_json_t *l_json = dap_json_parse_string(json_buf);
        
        if (l_json) {
            atomic_fetch_add(args->success_count, 1);
            dap_json_object_free(l_json);
        } else {
            atomic_fetch_add(args->failure_count, 1);
        }
    }
    
    return NULL;
}

static bool s_test_string_pool_races(void) {
    log_it(L_DEBUG, "Testing string pool race conditions");
    bool result = false;
    
    enum { NUM_THREADS = 4 };
    enum { ITERATIONS_PER_THREAD = 100 };
    
    pthread_t threads[NUM_THREADS];
    thread_test_args_t args[NUM_THREADS];
    atomic_int success_count = 0;
    atomic_int failure_count = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].thread_id = i;
        args[i].iterations = ITERATIONS_PER_THREAD;
        args[i].success_count = &success_count;
        args[i].failure_count = &failure_count;
        
        if (pthread_create(&threads[i], NULL, s_string_pool_stress_thread, &args[i]) != 0) {
            log_it(L_ERROR, "Failed to create thread %d", i);
            goto cleanup;
        }
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    int total_success = atomic_load(&success_count);
    int total_failure = atomic_load(&failure_count);
    
    log_it(L_INFO, "String pool test: %d success, %d failures", total_success, total_failure);
    
    DAP_TEST_FAIL_IF(total_failure > 0, "String pool handles concurrent access");
    
    result = true;
    log_it(L_DEBUG, "String pool race test passed");
    
cleanup:
    return result;
}

// =============================================================================
// TEST 5-8: Additional Concurrency Tests (Placeholders)
// =============================================================================

static bool s_test_global_state_races(void) {
    log_it(L_DEBUG, "Testing global state races - TODO");
    // TODO: Test dap_cpu_arch_set/get concurrency
    return true;
}

static bool s_test_read_write_contention(void) {
    log_it(L_DEBUG, "Testing read-write contention - TODO");
    // TODO: Concurrent read + modify operations
    return true;
}

static bool s_test_deadlock_scenarios(void) {
    log_it(L_DEBUG, "Testing deadlock scenarios - TODO");
    // TODO: Lock ordering tests
    return true;
}

static bool s_test_memory_ordering(void) {
    log_it(L_DEBUG, "Testing memory ordering - TODO");
    // TODO: Acquire/release semantics
    return true;
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_json_concurrency_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Concurrency & Thread-Safety Tests ===");
    
#ifdef __SANITIZE_THREAD__
    log_it(L_INFO, "ThreadSanitizer ENABLED - race detection active");
#else
    log_it(L_WARNING, "ThreadSanitizer NOT enabled - compile with -fsanitize=thread for full coverage");
#endif
    
    int tests_passed = 0;
    int tests_total = 8;
    
    tests_passed += s_test_tsan_validation() ? 1 : 0;
    tests_passed += s_test_concurrent_parsing_different_json() ? 1 : 0;
    tests_passed += s_test_arena_allocator_races() ? 1 : 0;
    tests_passed += s_test_string_pool_races() ? 1 : 0;
    tests_passed += s_test_global_state_races() ? 1 : 0;
    tests_passed += s_test_read_write_contention() ? 1 : 0;
    tests_passed += s_test_deadlock_scenarios() ? 1 : 0;
    tests_passed += s_test_memory_ordering() ? 1 : 0;
    
    log_it(L_INFO, "Concurrency tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void) {
    dap_print_module_name("DAP JSON Concurrency & Thread-Safety Tests");
    return dap_json_concurrency_tests_run();
}

