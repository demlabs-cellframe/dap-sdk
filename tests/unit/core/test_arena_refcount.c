/*
 * Test for dap_arena refcounted mode
 * 
 * Demonstrates hybrid approach with dap_arena_opt_t
 */

#include <stdio.h>
#include <assert.h>
#include "dap_arena.h"
#include "dap_common.h"
#include "dap_test.h"

#define LOG_TAG "test_arena_refcount"

/**
 * @brief Test basic refcounted arena functionality
 */
static void s_test_basic_refcount(void)
{
    printf("\n=== Test: Basic Refcount ===\n");
    
    // Create refcounted arena
    dap_arena_t *l_arena = dap_arena_new_opt((dap_arena_opt_t){
        .initial_size = 4096,
        .use_refcount = true
    });
    
    dap_assert(l_arena != NULL, "Arena created");
    
    // Allocate some memory
    dap_arena_alloc_ex_t l_result1, l_result2, l_result3;
    
    dap_assert(dap_arena_alloc_ex(l_arena, 128, &l_result1), "Alloc 1 success");
    dap_assert(dap_arena_alloc_ex(l_arena, 256, &l_result2), "Alloc 2 success");
    dap_assert(dap_arena_alloc_ex(l_arena, 512, &l_result3), "Alloc 3 success");
    
    printf("Allocated 3 blocks from arena\n");
    printf("  Block 1: %p (page: %p)\n", l_result1.ptr, l_result1.page_handle);
    printf("  Block 2: %p (page: %p)\n", l_result2.ptr, l_result2.page_handle);
    printf("  Block 3: %p (page: %p)\n", l_result3.ptr, l_result3.page_handle);
    
    // All should be from same page (4KB is enough)
    dap_assert(l_result1.page_handle == l_result2.page_handle, "Same page for alloc 1&2");
    dap_assert(l_result2.page_handle == l_result3.page_handle, "Same page for alloc 2&3");
    
    // Check initial refcount (should be 4: arena + 3 allocations)
    int l_refcount = dap_arena_page_get_refcount(l_result1.page_handle);
    printf("Initial refcount: %d (expected 4: arena + 3 allocs)\n", l_refcount);
    dap_assert(l_refcount == 4, "Initial refcount correct");
    
    // Increment refcount (simulate borrowed reference)
    dap_arena_page_ref(l_result1.page_handle);
    l_refcount = dap_arena_page_get_refcount(l_result1.page_handle);
    printf("After ref: %d (expected 5)\n", l_refcount);
    dap_assert(l_refcount == 5, "Refcount after ref");
    
    // Decrement refcount
    dap_arena_page_unref(l_result1.page_handle);
    l_refcount = dap_arena_page_get_refcount(l_result1.page_handle);
    printf("After unref: %d (expected 4)\n", l_refcount);
    dap_assert(l_refcount == 4, "Refcount after unref");
    
    // Cleanup
    dap_arena_free(l_arena);
    
    printf("✓ Basic refcount test passed\n");
}

/**
 * @brief Test statistics with refcount
 */
static void s_test_stats_refcount(void)
{
    printf("\n=== Test: Statistics ===\n");
    
    dap_arena_t *l_arena = dap_arena_new_opt((dap_arena_opt_t){
        .initial_size = 4096,
        .use_refcount = true
    });
    
    dap_assert(l_arena != NULL, "Arena created");
    
    dap_arena_stats_t l_stats;
    dap_arena_get_stats(l_arena, &l_stats);
    
    printf("Initial stats:\n");
    printf("  Pages: %zu\n", l_stats.page_count);
    printf("  Total allocated: %zu bytes\n", l_stats.total_allocated);
    printf("  Total used: %zu bytes\n", l_stats.total_used);
    printf("  Active refcount: %zu\n", l_stats.active_refcount);
    printf("  Allocations: %zu\n", l_stats.allocation_count);
    
    dap_assert(l_stats.page_count == 1, "Initial page count");
    dap_assert(l_stats.active_refcount == 1, "Initial refcount (arena only)");
    
    // Allocate with refcounting
    dap_arena_alloc_ex_t l_result;
    dap_arena_alloc_ex(l_arena, 128, &l_result);
    
    dap_arena_get_stats(l_arena, &l_stats);
    printf("\nAfter 1 allocation:\n");
    printf("  Active refcount: %zu (expected 2)\n", l_stats.active_refcount);
    dap_assert(l_stats.active_refcount == 2, "Refcount after alloc");
    
    dap_arena_free(l_arena);
    
    printf("✓ Stats test passed\n");
}

/**
 * @brief Test comparison: refcounted vs non-refcounted
 */
static void s_test_comparison(void)
{
    printf("\n=== Test: Refcounted vs Standard ===\n");
    
    // Standard arena (no refcount)
    dap_arena_t *l_arena_std = dap_arena_new_opt((dap_arena_opt_t){
        .initial_size = 4096,
        .use_refcount = false
    });
    
    // Refcounted arena
    dap_arena_t *l_arena_ref = dap_arena_new_opt((dap_arena_opt_t){
        .initial_size = 4096,
        .use_refcount = true
    });
    
    dap_assert(l_arena_std != NULL && l_arena_ref != NULL, "Both arenas created");
    
    // Standard arena: simple alloc
    void *l_ptr_std = dap_arena_alloc(l_arena_std, 128);
    dap_assert(l_ptr_std != NULL, "Standard alloc works");
    printf("✓ Standard arena: simple alloc works\n");
    
    // Refcounted arena: extended alloc
    dap_arena_alloc_ex_t l_result_ref;
    dap_assert(dap_arena_alloc_ex(l_arena_ref, 128, &l_result_ref), "Refcounted alloc works");
    printf("✓ Refcounted arena: extended alloc works\n");
    
    // Standard arena: can still use simple alloc
    void *l_ptr_std2 = dap_arena_alloc(l_arena_std, 256);
    dap_assert(l_ptr_std2 != NULL, "Standard alloc still works");
    
    // Refcounted arena: can also use simple alloc (for backward compatibility)
    void *l_ptr_ref_simple = dap_arena_alloc(l_arena_ref, 256);
    dap_assert(l_ptr_ref_simple != NULL, "Refcounted arena: simple alloc works too");
    printf("✓ Refcounted arena: backward compatible with simple alloc\n");
    
    dap_arena_free(l_arena_std);
    dap_arena_free(l_arena_ref);
    
    printf("✓ Comparison test passed\n");
}

/**
 * @brief Test page growth with options
 */
static void s_test_growth_options(void)
{
    printf("\n=== Test: Page Growth Options ===\n");
    
    // Note: minimum page size is 4KB, so we test with larger allocations
    dap_arena_t *l_arena = dap_arena_new_opt((dap_arena_opt_t){
        .initial_size = 4096,          // Start with minimum (4KB)
        .page_growth_factor = 1.5,     // Grow by 1.5x
        .max_page_size = 16384,        // Cap at 16KB
        .use_refcount = true
    });
    
    dap_assert(l_arena != NULL, "Arena created");
    
    dap_arena_stats_t l_stats;
    
    // Fill first page (4KB = 4096 bytes)
    // Allocate 2KB chunks to fill it
    dap_arena_alloc(l_arena, 2048);  // 2KB
    dap_arena_alloc(l_arena, 2048);  // 4KB total (page full)
    
    dap_arena_get_stats(l_arena, &l_stats);
    printf("After filling first page (4KB):\n");
    printf("  Pages: %zu (expected 1)\n", l_stats.page_count);
    printf("  Used: %zu bytes\n", l_stats.total_used);
    dap_assert(l_stats.page_count == 1, "Still on first page");
    
    // This should trigger new page (with growth factor 1.5 = 6KB)
    dap_arena_alloc(l_arena, 1024);  // Exceeds first page
    
    dap_arena_get_stats(l_arena, &l_stats);
    printf("After allocation requiring new page:\n");
    printf("  Pages: %zu (expected 2)\n", l_stats.page_count);
    printf("  Total allocated: %zu bytes\n", l_stats.total_allocated);
    printf("  Total used: %zu bytes\n", l_stats.total_used);
    
    dap_assert(l_stats.page_count >= 2, "Multiple pages created");
    
    dap_arena_free(l_arena);
    
    printf("✓ Growth options test passed\n");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    dap_log_level_set(L_DEBUG);
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  DAP Arena Refcount Test Suite\n");
    printf("  Hybrid approach with dap_arena_opt_t\n");
    printf("═══════════════════════════════════════════════════════\n");
    
    s_test_basic_refcount();
    s_test_stats_refcount();
    s_test_comparison();
    s_test_growth_options();
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  ✅ ALL TESTS PASSED\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("\n");
    
    return 0;
}
