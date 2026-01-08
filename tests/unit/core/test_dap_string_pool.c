/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
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
    
    dap_string_pool_t *pool = dap_string_pool_new(128);
    dap_assert(pool != NULL, "Pool creation");
    
    dap_string_pool_stats_t stats;
    dap_string_pool_get_stats(pool, &stats);
    dap_assert(stats.string_count == 0, "Initial count");
    
    dap_string_pool_free(pool);
}

/**
 * @brief Test basic string interning
 */
static void test_string_pool_intern_basic(void)
{
    dap_print_module_name("Basic string interning");
    
    dap_string_pool_t *pool = dap_string_pool_new(128);
    dap_assert(pool != NULL, "Pool creation");
    
    const char *s1 = dap_string_pool_intern(pool, "test");
    dap_assert(s1 != NULL, "First intern");
    dap_assert(strcmp(s1, "test") == 0, "String content");
    
    const char *s2 = dap_string_pool_intern(pool, "test");
    dap_assert(s2 != NULL, "Second intern");
    dap_assert(s1 == s2, "Pointer equality (deduplication)");
    
    dap_string_pool_stats_t stats;
    dap_string_pool_get_stats(pool, &stats);
    dap_assert(stats.string_count == 1, "Only one unique string");
    dap_assert(stats.lookup_count == 2, "Two lookups");
    dap_assert(stats.hit_count == 1, "One cache hit");
    
    dap_string_pool_free(pool);
}

/**
 * @brief Test interning with known length
 */
static void test_string_pool_intern_n(void)
{
    dap_print_module_name("String interning with length");
    
    dap_string_pool_t *pool = dap_string_pool_new(128);
    dap_assert(pool != NULL, "Pool creation");
    
    const char *s1 = dap_string_pool_intern_n(pool, "hello_world", 5); // Only "hello"
    dap_assert(s1 != NULL, "Partial intern");
    dap_assert(strcmp(s1, "hello") == 0, "String content");
    dap_assert(strlen(s1) == 5, "String length");
    
    const char *s2 = dap_string_pool_intern(pool, "hello");
    dap_assert(s1 == s2, "Same as full intern");
    
    dap_string_pool_free(pool);
}

/**
 * @brief Test multiple different strings
 */
static void test_string_pool_multiple_strings(void)
{
    dap_print_module_name("Multiple different strings");
    
    dap_string_pool_t *pool = dap_string_pool_new(128);
    dap_assert(pool != NULL, "Pool creation");
    
    const char *strings[] = {"name", "value", "id", "type", "data"};
    const char *interned[5];
    
    for (int i = 0; i < 5; i++) {
        interned[i] = dap_string_pool_intern(pool, strings[i]);
        dap_assert(interned[i] != NULL, "Intern string");
        dap_assert(strcmp(interned[i], strings[i]) == 0, "Content match");
    }
    
    // All should be different pointers
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            dap_assert(interned[i] != interned[j], "Different pointers");
        }
    }
    
    dap_string_pool_stats_t stats;
    dap_string_pool_get_stats(pool, &stats);
    dap_assert(stats.string_count == 5, "Five unique strings");
    
    dap_string_pool_free(pool);
}

/**
 * @brief Test contains functionality
 */
static void test_string_pool_contains(void)
{
    dap_print_module_name("String pool contains");
    
    dap_string_pool_t *pool = dap_string_pool_new(128);
    dap_assert(pool != NULL, "Pool creation");
    
    dap_string_pool_intern(pool, "exists");
    
    const char *found = dap_string_pool_contains(pool, "exists");
    dap_assert(found != NULL, "String exists");
    
    const char *not_found = dap_string_pool_contains(pool, "not_exists");
    dap_assert(not_found == NULL, "String doesn't exist");
    
    dap_string_pool_free(pool);
}

/**
 * @brief Test pool clear
 */
static void test_string_pool_clear(void)
{
    dap_print_module_name("String pool clear");
    
    dap_string_pool_t *pool = dap_string_pool_new(128);
    dap_assert(pool != NULL, "Pool creation");
    
    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "string_%d", i);
        dap_string_pool_intern(pool, buf);
    }
    
    dap_string_pool_stats_t stats_before;
    dap_string_pool_get_stats(pool, &stats_before);
    dap_assert(stats_before.string_count == 10, "Ten strings");
    
    dap_string_pool_clear(pool);
    
    dap_string_pool_stats_t stats_after;
    dap_string_pool_get_stats(pool, &stats_after);
    dap_assert(stats_after.string_count == 0, "No strings after clear");
    
    // Should be able to intern again
    const char *s = dap_string_pool_intern(pool, "new_string");
    dap_assert(s != NULL, "Intern after clear");
    
    dap_string_pool_free(pool);
}

/**
 * @brief Test hash collisions
 */
static void test_string_pool_collisions(void)
{
    dap_print_module_name("Hash collisions handling");
    
    // Small capacity to force collisions
    dap_string_pool_t *pool = dap_string_pool_new(16);
    dap_assert(pool != NULL, "Pool creation");
    
    // Intern many strings (will cause collisions)
    for (int i = 0; i < 100; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key_%d", i);
        const char *s = dap_string_pool_intern(pool, buf);
        dap_assert(s != NULL, "Intern with collisions");
    }
    
    // Verify all strings are unique and retrievable
    for (int i = 0; i < 100; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "key_%d", i);
        const char *s = dap_string_pool_contains(pool, buf);
        dap_assert(s != NULL, "String still exists");
        dap_assert(strcmp(s, buf) == 0, "Content matches");
    }
    
    dap_string_pool_stats_t stats;
    dap_string_pool_get_stats(pool, &stats);
    dap_assert(stats.string_count == 100, "All strings stored");
    dap_assert(stats.collision_count > 0, "Collisions occurred");
    
    dap_string_pool_free(pool);
}

/**
 * @brief Test memory efficiency
 */
static void test_string_pool_memory_efficiency(void)
{
    dap_print_module_name("Memory efficiency");
    
    dap_string_pool_t *pool = dap_string_pool_new(128);
    dap_assert(pool != NULL, "Pool creation");
    
    // Intern same string many times
    const char *base = "repeated_string";
    for (int i = 0; i < 1000; i++) {
        dap_string_pool_intern(pool, base);
    }
    
    dap_string_pool_stats_t stats;
    dap_string_pool_get_stats(pool, &stats);
    
    // Only one copy should exist
    dap_assert(stats.string_count == 1, "One unique string");
    
    // Total allocated should be minimal
    size_t expected_max = 1024 * 10; // Should be way less than 10KB
    dap_assert(stats.total_allocated < expected_max, "Memory efficient");
    
    dap_string_pool_free(pool);
}

/**
 * @brief Test NULL handling
 */
static void test_string_pool_null_handling(void)
{
    dap_print_module_name("NULL handling");
    
    // NULL pool
    const char *s = dap_string_pool_intern(NULL, "test");
    dap_assert(s == NULL, "NULL pool returns NULL");
    
    // NULL string
    dap_string_pool_t *pool = dap_string_pool_new(128);
    s = dap_string_pool_intern(pool, NULL);
    dap_assert(s == NULL, "NULL string returns NULL");
    
    dap_string_pool_free(pool);
    dap_string_pool_free(NULL); // Should not crash
}

/**
 * @brief Test thread-safe pool
 */
static void test_string_pool_thread_safe(void)
{
    dap_print_module_name("Thread-safe pool");
    
    dap_string_pool_t *pool = dap_string_pool_new_thread_safe(128);
    dap_assert(pool != NULL, "Thread-safe pool creation");
    
    const char *s = dap_string_pool_intern(pool, "thread_safe_test");
    dap_assert(s != NULL, "Intern in thread-safe pool");
    
    dap_string_pool_free(pool);
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

