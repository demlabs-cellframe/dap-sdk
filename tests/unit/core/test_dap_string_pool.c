/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2026
 * All rights reserved.

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_string_pool.h"
#include "dap_test.h"
#include "dap_test_helpers.h"

#define LOG_TAG "test_dap_string_pool"

/* ========================================================================== */
/*                         TESTS                                              */
/* ========================================================================== */

/**
 * @brief Test basic pool creation
 */
static void test_string_pool_new_free(void)
{
    dap_print_module_name("String pool new/free");
    
    dap_string_pool_t *l_pool = dap_string_pool_new(NULL, 128);
    dap_assert(l_pool != NULL, "Pool creation");
    
    dap_string_pool_stats_t l_stats;
    dap_string_pool_get_stats(l_pool, &l_stats);
    dap_assert(l_stats.string_count == 0, "Initial count");
    
    dap_string_pool_free(l_pool);
}

/**
 * @brief Test basic string interning
 */
static void test_string_pool_intern_basic(void)
{
    dap_print_module_name("Basic string interning");
    
    dap_string_pool_t *l_pool = dap_string_pool_new(NULL, 128);
    dap_assert(l_pool != NULL, "Pool creation");
    
    const char *l_s1 = dap_string_pool_intern(l_pool, "test");
    dap_assert(l_s1 != NULL, "First intern");
    dap_assert(strcmp(l_s1, "test") == 0, "String content");
    
    const char *l_s2 = dap_string_pool_intern(l_pool, "test");
    dap_assert(l_s2 != NULL, "Second intern");
    dap_assert(l_s1 == l_s2, "Pointer equality (deduplication)");
    
    dap_string_pool_stats_t l_stats;
    dap_string_pool_get_stats(l_pool, &l_stats);
    dap_assert(l_stats.string_count == 1, "Only one unique string");
    dap_assert(l_stats.lookup_count == 2, "Two lookups");
    dap_assert(l_stats.hit_count == 1, "One cache hit");
    
    dap_string_pool_free(l_pool);
}

/**
 * @brief Test interning with known length
 */
static void test_string_pool_intern_n(void)
{
    dap_print_module_name("String interning with length");
    
    dap_string_pool_t *l_pool = dap_string_pool_new(NULL, 128);
    dap_assert(l_pool != NULL, "Pool creation");
    
    const char *l_s1 = dap_string_pool_intern_n(l_pool, "hello_world", 5); // Only "hello"
    dap_assert(l_s1 != NULL, "Partial intern");
    dap_assert(strcmp(l_s1, "hello") == 0, "String content");
    dap_assert(strlen(l_s1) == 5, "String length");
    
    const char *l_s2 = dap_string_pool_intern(l_pool, "hello");
    dap_assert(l_s1 == l_s2, "Same as full intern");
    
    dap_string_pool_free(l_pool);
}

/**
 * @brief Test multiple different strings
 */
static void test_string_pool_multiple_strings(void)
{
    dap_print_module_name("Multiple different strings");
    
    dap_string_pool_t *l_pool = dap_string_pool_new(NULL, 128);
    dap_assert(l_pool != NULL, "Pool creation");
    
    const char *l_strings[] = {"name", "value", "id", "type", "data"};
    const char *l_interned[5];
    
    for (int i = 0; i < 5; i++) {
        l_interned[i] = dap_string_pool_intern(l_pool, l_strings[i]);
        dap_assert(l_interned[i] != NULL, "Intern string");
        dap_assert(strcmp(l_interned[i], l_strings[i]) == 0, "Content match");
    }
    
    // All should be different pointers
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            dap_assert(l_interned[i] != l_interned[j], "Different pointers");
        }
    }
    
    dap_string_pool_stats_t l_stats;
    dap_string_pool_get_stats(l_pool, &l_stats);
    dap_assert(l_stats.string_count == 5, "Five unique strings");
    
    dap_string_pool_free(l_pool);
}

/**
 * @brief Test contains functionality
 */
static void test_string_pool_contains(void)
{
    dap_print_module_name("String pool contains");
    
    dap_string_pool_t *l_pool = dap_string_pool_new(NULL, 128);
    dap_assert(l_pool != NULL, "Pool creation");
    
    dap_string_pool_intern(l_pool, "exists");
    
    const char *l_found = dap_string_pool_contains(l_pool, "exists");
    dap_assert(l_found != NULL, "String exists");
    
    const char *l_not_found = dap_string_pool_contains(l_pool, "not_exists");
    dap_assert(l_not_found == NULL, "String doesn't exist");
    
    dap_string_pool_free(l_pool);
}

/**
 * @brief Test pool clear
 */
static void test_string_pool_clear(void)
{
    dap_print_module_name("String pool clear");
    
    dap_string_pool_t *l_pool = dap_string_pool_new(NULL, 128);
    dap_assert(l_pool != NULL, "Pool creation");
    
    for (int i = 0; i < 10; i++) {
        char l_buf[32];
        snprintf(l_buf, sizeof(l_buf), "string_%d", i);
        dap_string_pool_intern(l_pool, l_buf);
    }
    
    dap_string_pool_stats_t l_stats_before;
    dap_string_pool_get_stats(l_pool, &l_stats_before);
    dap_assert(l_stats_before.string_count == 10, "Ten strings");
    
    dap_string_pool_clear(l_pool);
    
    dap_string_pool_stats_t l_stats_after;
    dap_string_pool_get_stats(l_pool, &l_stats_after);
    dap_assert(l_stats_after.string_count == 0, "No strings after clear");
    
    // Should be able to intern again
    const char *l_s = dap_string_pool_intern(l_pool, "new_string");
    dap_assert(l_s != NULL, "Intern after clear");
    
    dap_string_pool_free(l_pool);
}

/**
 * @brief Test hash collisions
 */
static void test_string_pool_collisions(void)
{
    dap_print_module_name("Hash collisions handling");
    
    // Small capacity to force collisions
    dap_string_pool_t *l_pool = dap_string_pool_new(NULL, 16);
    dap_assert(l_pool != NULL, "Pool creation");
    
    // Intern many strings (will cause collisions)
    for (int i = 0; i < 100; i++) {
        char l_buf[32];
        snprintf(l_buf, sizeof(l_buf), "key_%d", i);
        const char *l_s = dap_string_pool_intern(l_pool, l_buf);
        dap_assert(l_s != NULL, "Intern with collisions");
    }
    
    // Verify all strings are unique and retrievable
    for (int i = 0; i < 100; i++) {
        char l_buf[32];
        snprintf(l_buf, sizeof(l_buf), "key_%d", i);
        const char *l_s = dap_string_pool_contains(l_pool, l_buf);
        dap_assert(l_s != NULL, "String still exists");
        dap_assert(strcmp(l_s, l_buf) == 0, "Content matches");
    }
    
    dap_string_pool_stats_t l_stats;
    dap_string_pool_get_stats(l_pool, &l_stats);
    dap_assert(l_stats.string_count == 100, "All strings stored");
    dap_assert(l_stats.collision_count > 0, "Collisions occurred");
    
    dap_string_pool_free(l_pool);
}

/**
 * @brief Test memory efficiency
 */
static void test_string_pool_memory_efficiency(void)
{
    dap_print_module_name("Memory efficiency");
    
    dap_string_pool_t *l_pool = dap_string_pool_new(NULL, 128);
    dap_assert(l_pool != NULL, "Pool creation");
    
    // Intern same string many times
    const char *l_base = "repeated_string";
    for (int i = 0; i < 1000; i++) {
        dap_string_pool_intern(l_pool, l_base);
    }
    
    dap_string_pool_stats_t l_stats;
    dap_string_pool_get_stats(l_pool, &l_stats);
    
    // Only one copy should exist
    dap_assert(l_stats.string_count == 1, "One unique string");
    
    // Total allocated should be minimal
    size_t l_expected_max = 1024 * 10; // Should be way less than 10KB
    dap_assert(l_stats.total_allocated < l_expected_max, "Memory efficient");
    
    dap_string_pool_free(l_pool);
}

/**
 * @brief Test NULL handling
 */
static void test_string_pool_null_handling(void)
{
    dap_print_module_name("NULL handling");
    
    // NULL pool
    const char *l_s = dap_string_pool_intern(NULL, "test");
    dap_assert(l_s == NULL, "NULL pool returns NULL");
    
    // NULL string
    dap_string_pool_t *l_pool = dap_string_pool_new(NULL, 128);
    l_s = dap_string_pool_intern(l_pool, NULL);
    dap_assert(l_s == NULL, "NULL string returns NULL");
    
    dap_string_pool_free(l_pool);
    dap_string_pool_free(NULL); // Should not crash
}

/**
 * @brief Test thread-safe pool
 */
static void test_string_pool_thread_safe(void)
{
    dap_print_module_name("Thread-safe pool");
    
    dap_string_pool_t *l_pool = dap_string_pool_new_thread_safe(128);
    dap_assert(l_pool != NULL, "Thread-safe pool creation");
    
    const char *l_s = dap_string_pool_intern(l_pool, "thread_safe_test");
    dap_assert(l_s != NULL, "Intern in thread-safe pool");
    
    dap_string_pool_free(l_pool);
}

/* ========================================================================== */
/*                         MAIN                                               */
/* ========================================================================== */

int main(void)
{
    TEST_SUITE_START("DAP String Pool Tests");
    
    TEST_RUN(test_string_pool_new_free);
    TEST_RUN(test_string_pool_intern_basic);
    TEST_RUN(test_string_pool_intern_n);
    TEST_RUN(test_string_pool_multiple_strings);
    TEST_RUN(test_string_pool_contains);
    TEST_RUN(test_string_pool_clear);
    TEST_RUN(test_string_pool_collisions);
    TEST_RUN(test_string_pool_memory_efficiency);
    TEST_RUN(test_string_pool_null_handling);
    TEST_RUN(test_string_pool_thread_safe);
    
    TEST_SUITE_END();
    
    return 0;
}
