/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * This file is part of DAP SDK the open source project
 *
 *    DAP is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "dap_arena.h"
#include "dap_test.h"
#include "dap_test_helpers.h"

#define LOG_TAG "test_dap_arena"

/* ========================================================================== */
/*                         TESTS                                              */
/* ========================================================================== */

/**
 * @brief Test basic arena creation and deallocation
 */
static void test_arena_new_free(void)
{
    dap_print_module_name("Arena new/free");
    
    dap_arena_t *arena = dap_arena_new(4096);
    dap_assert(arena != NULL, "Arena creation");
    
    dap_arena_stats_t stats;
    dap_arena_get_stats(arena, &stats);
    dap_assert(stats.page_count == 1, "Initial page count");
    dap_assert(stats.total_used == 0, "Initial used bytes");
    
    dap_arena_free(arena);
}

/**
 * @brief Test basic allocation
 */
static void test_arena_alloc_basic(void)
{
    dap_print_module_name("Basic allocation");
    
    dap_arena_t *arena = dap_arena_new(4096);
    dap_assert(arena != NULL, "Arena creation");
    
    // Allocate small block
    void *ptr1 = dap_arena_alloc(arena, 64);
    dap_assert(ptr1 != NULL, "First allocation");
    
    // Allocate another block
    void *ptr2 = dap_arena_alloc(arena, 128);
    dap_assert(ptr2 != NULL, "Second allocation");
    dap_assert(ptr2 > ptr1, "Pointers in order");
    
    // Check stats
    dap_arena_stats_t stats;
    dap_arena_get_stats(arena, &stats);
    dap_assert(stats.allocation_count == 2, "Allocation count");
    dap_assert(stats.total_used > 0, "Used bytes");
    
    dap_arena_free(arena);
}

/**
 * @brief Test zero-initialized allocation
 */
static void test_arena_alloc_zero(void)
{
    dap_print_module_name("Zero allocation");
    
    dap_arena_t *arena = dap_arena_new(4096);
    dap_assert(arena != NULL, "Arena creation");
    
    uint8_t *ptr = (uint8_t *)dap_arena_alloc_zero(arena, 256);
    dap_assert(ptr != NULL, "Zero allocation");
    
    // Check all bytes are zero
    bool all_zero = true;
    for (size_t i = 0; i < 256; i++) {
        if (ptr[i] != 0) {
            all_zero = false;
            break;
        }
    }
    dap_assert(all_zero, "Memory is zeroed");
    
    dap_arena_free(arena);
}

/**
 * @brief Test aligned allocation
 */
static void test_arena_alloc_aligned(void)
{
    dap_print_module_name("Aligned allocation");
    
    dap_arena_t *arena = dap_arena_new(4096);
    dap_assert(arena != NULL, "Arena creation");
    
    // Allocate 64-byte aligned block
    void *ptr = dap_arena_alloc_aligned(arena, 128, 64);
    dap_assert(ptr != NULL, "Aligned allocation");
    
    uintptr_t addr = (uintptr_t)ptr;
    dap_assert((addr % 64) == 0, "64-byte alignment");
    
    dap_arena_free(arena);
}

/**
 * @brief Test string duplication
 */
static void test_arena_strdup(void)
{
    dap_print_module_name("String duplication");
    
    dap_arena_t *arena = dap_arena_new(4096);
    dap_assert(arena != NULL, "Arena creation");
    
    const char *original = "Hello, Arena!";
    char *copy = dap_arena_strdup(arena, original);
    dap_assert(copy != NULL, "String duplication");
    dap_assert(strcmp(copy, original) == 0, "String content");
    
    // Test strndup
    char *partial = dap_arena_strndup(arena, original, 5);
    dap_assert(partial != NULL, "Partial string duplication");
    dap_assert(strcmp(partial, "Hello") == 0, "Partial string content");
    dap_assert(partial[5] == '\0', "NULL terminator");
    
    dap_arena_free(arena);
}

/**
 * @brief Test arena reset (bulk deallocation)
 */
static void test_arena_reset(void)
{
    dap_print_module_name("Arena reset");
    
    dap_arena_t *arena = dap_arena_new(4096);
    dap_assert(arena != NULL, "Arena creation");
    
    // Make several allocations
    for (int i = 0; i < 10; i++) {
        void *ptr = dap_arena_alloc(arena, 256);
        dap_assert(ptr != NULL, "Allocation in loop");
    }
    
    dap_arena_stats_t stats_before;
    dap_arena_get_stats(arena, &stats_before);
    dap_assert(stats_before.allocation_count == 10, "Allocation count before reset");
    dap_assert(stats_before.total_used > 0, "Used bytes before reset");
    
    // Reset arena
    dap_arena_reset(arena);
    
    dap_arena_stats_t stats_after;
    dap_arena_get_stats(arena, &stats_after);
    dap_assert(stats_after.allocation_count == 0, "Allocation count after reset");
    dap_assert(stats_after.total_used == 0, "Used bytes after reset");
    dap_assert(stats_after.total_allocated == stats_before.total_allocated,
               "Total allocated unchanged");
    
    // Should be able to allocate again
    void *ptr = dap_arena_alloc(arena, 128);
    dap_assert(ptr != NULL, "Allocation after reset");
    
    dap_arena_free(arena);
}

/**
 * @brief Test page growth
 */
static void test_arena_page_growth(void)
{
    dap_print_module_name("Page growth");
    
    dap_arena_t *arena = dap_arena_new(4096); // Standard page
    dap_assert(arena != NULL, "Arena creation");
    
    dap_arena_stats_t stats;
    dap_arena_get_stats(arena, &stats);
    dap_assert(stats.page_count == 1, "Initial page count");
    
    // Allocate more than one page (need >4096 bytes total)
    for (int i = 0; i < 10; i++) {
        void *ptr = dap_arena_alloc(arena, 512);
        dap_assert(ptr != NULL, "Allocation in loop");
    }
    
    dap_arena_get_stats(arena, &stats);
    dap_assert(stats.page_count > 1, "Multiple pages created");
    
    dap_arena_free(arena);
}

/**
 * @brief Test large allocation (larger than page size)
 */
static void test_arena_large_alloc(void)
{
    dap_print_module_name("Large allocation");
    
    dap_arena_t *arena = dap_arena_new(4096);
    dap_assert(arena != NULL, "Arena creation");
    
    // Allocate 16KB (larger than initial page)
    void *ptr = dap_arena_alloc(arena, 16 * 1024);
    dap_assert(ptr != NULL, "Large allocation");
    
    dap_arena_stats_t stats;
    dap_arena_get_stats(arena, &stats);
    dap_assert(stats.page_count >= 2, "New page created");
    
    dap_arena_free(arena);
}

/**
 * @brief Test many small allocations (stress test)
 */
static void test_arena_many_small_allocs(void)
{
    dap_print_module_name("Many small allocations");
    
    dap_arena_t *arena = dap_arena_new(4096);
    dap_assert(arena != NULL, "Arena creation");
    
    // Allocate 1000 small blocks
    for (int i = 0; i < 1000; i++) {
        void *ptr = dap_arena_alloc(arena, 32);
        dap_assert(ptr != NULL, "Small allocation");
    }
    
    dap_arena_stats_t stats;
    dap_arena_get_stats(arena, &stats);
    dap_assert(stats.allocation_count == 1000, "Allocation count");
    
    dap_arena_free(arena);
}

/**
 * @brief Test thread-local arena creation
 */
static void test_arena_thread_local(void)
{
    dap_print_module_name("Thread-local arena");
    
    dap_arena_t *arena = dap_arena_new_thread_local(4096);
    dap_assert(arena != NULL, "Thread-local arena creation");
    
    void *ptr = dap_arena_alloc(arena, 256);
    dap_assert(ptr != NULL, "Allocation from thread-local arena");
    
    dap_arena_free(arena);
}

/**
 * @brief Test NULL handling
 */
static void test_arena_null_handling(void)
{
    dap_print_module_name("NULL handling");
    
    // NULL arena
    void *ptr = dap_arena_alloc(NULL, 128);
    dap_assert(ptr == NULL, "NULL arena returns NULL");
    
    // Zero size
    dap_arena_t *arena = dap_arena_new(4096);
    ptr = dap_arena_alloc(arena, 0);
    dap_assert(ptr == NULL, "Zero size returns NULL");
    
    // NULL string
    char *str = dap_arena_strdup(arena, NULL);
    dap_assert(str == NULL, "NULL string returns NULL");
    
    dap_arena_free(arena);
    dap_arena_free(NULL); // Should not crash
}

/* ========================================================================== */
/*                         MAIN                                               */
/* ========================================================================== */

int main(void)
{
    TEST_SUITE_START("DAP Arena Allocator Tests");
    
    TEST_RUN(test_arena_new_free);
    TEST_RUN(test_arena_alloc_basic);
    TEST_RUN(test_arena_alloc_zero);
    TEST_RUN(test_arena_alloc_aligned);
    TEST_RUN(test_arena_strdup);
    TEST_RUN(test_arena_reset);
    TEST_RUN(test_arena_page_growth);
    TEST_RUN(test_arena_large_alloc);
    TEST_RUN(test_arena_many_small_allocs);
    TEST_RUN(test_arena_thread_local);
    TEST_RUN(test_arena_null_handling);
    
    TEST_SUITE_END();
    
    return 0;
}

