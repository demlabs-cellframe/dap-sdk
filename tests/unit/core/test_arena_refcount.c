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
    log_it(L_INFO, "\n=== Test: Basic Refcount ===");
    
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
    
    log_it(L_INFO, "Allocated 3 blocks from arena");
    log_it(L_INFO, "  Block 1: %p (page: %p)", l_result1.ptr, l_result1.page_handle);
    log_it(L_INFO, "  Block 2: %p (page: %p)", l_result2.ptr, l_result2.page_handle);
    log_it(L_INFO, "  Block 3: %p (page: %p)", l_result3.ptr, l_result3.page_handle);
    
    // All should be from same page (4KB is enough)
    dap_assert(l_result1.page_handle == l_result2.page_handle, "Same page for alloc 1&2");
    dap_assert(l_result2.page_handle == l_result3.page_handle, "Same page for alloc 2&3");
    
    // Check initial refcount (should be 4: arena + 3 allocations)
    int l_refcount = dap_arena_page_get_refcount(l_result1.page_handle);
    log_it(L_INFO, "Initial refcount: %d (expected 4: arena + 3 allocs)", l_refcount);
    dap_assert(l_refcount == 4, "Initial refcount correct");
    
    // Increment refcount (simulate borrowed reference)
    dap_arena_page_ref(l_result1.page_handle);
    l_refcount = dap_arena_page_get_refcount(l_result1.page_handle);
    log_it(L_INFO, "After ref: %d (expected 5)", l_refcount);
    dap_assert(l_refcount == 5, "Refcount after ref");
    
    // Decrement refcount
    dap_arena_page_unref(l_result1.page_handle);
    l_refcount = dap_arena_page_get_refcount(l_result1.page_handle);
    log_it(L_INFO, "After unref: %d (expected 4)", l_refcount);
    dap_assert(l_refcount == 4, "Refcount after unref");
    
    // Cleanup
    dap_arena_free(l_arena);
    
    log_it(L_INFO, "✓ Basic refcount test passed");
}

/**
 * @brief Test statistics with refcount
 */
static void s_test_stats_refcount(void)
{
    log_it(L_INFO, "\n=== Test: Statistics ===");
    
    dap_arena_t *l_arena = dap_arena_new_opt((dap_arena_opt_t){
        .initial_size = 4096,
        .use_refcount = true
    });
    
    dap_assert(l_arena != NULL, "Arena created");
    
    dap_arena_stats_t l_stats;
    dap_arena_get_stats(l_arena, &l_stats);
    
    log_it(L_INFO, "Initial stats:");
    log_it(L_INFO, "  Pages: %zu", l_stats.page_count);
    log_it(L_INFO, "  Total allocated: %zu bytes", l_stats.total_allocated);
    log_it(L_INFO, "  Total used: %zu bytes", l_stats.total_used);
    log_it(L_INFO, "  Active refcount: %zu", l_stats.active_refcount);
    log_it(L_INFO, "  Allocations: %zu", l_stats.allocation_count);
    
    dap_assert(l_stats.page_count == 1, "Initial page count");
    dap_assert(l_stats.active_refcount == 1, "Initial refcount (arena only)");
    
    // Allocate with refcounting
    dap_arena_alloc_ex_t l_result;
    dap_arena_alloc_ex(l_arena, 128, &l_result);
    
    dap_arena_get_stats(l_arena, &l_stats);
    log_it(L_INFO, "\nAfter 1 allocation:");
    log_it(L_INFO, "  Active refcount: %zu (expected 2)", l_stats.active_refcount);
    dap_assert(l_stats.active_refcount == 2, "Refcount after alloc");
    
    dap_arena_free(l_arena);
    
    log_it(L_INFO, "✓ Stats test passed");
}

/**
 * @brief Test comparison: refcounted vs non-refcounted
 */
static void s_test_comparison(void)
{
    log_it(L_INFO, "\n=== Test: Refcounted vs Standard ===");
    
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
    log_it(L_INFO, "✓ Standard arena: simple alloc works");
    
    // Refcounted arena: extended alloc
    dap_arena_alloc_ex_t l_result_ref;
    dap_assert(dap_arena_alloc_ex(l_arena_ref, 128, &l_result_ref), "Refcounted alloc works");
    log_it(L_INFO, "✓ Refcounted arena: extended alloc works");
    
    // Standard arena: can still use simple alloc
    void *l_ptr_std2 = dap_arena_alloc(l_arena_std, 256);
    dap_assert(l_ptr_std2 != NULL, "Standard alloc still works");
    
    // Refcounted arena: can also use simple alloc (for backward compatibility)
    void *l_ptr_ref_simple = dap_arena_alloc(l_arena_ref, 256);
    dap_assert(l_ptr_ref_simple != NULL, "Refcounted arena: simple alloc works too");
    log_it(L_INFO, "✓ Refcounted arena: backward compatible with simple alloc");
    
    dap_arena_free(l_arena_std);
    dap_arena_free(l_arena_ref);
    
    log_it(L_INFO, "✓ Comparison test passed");
}

/**
 * @brief Test page growth with options
 */
static void s_test_growth_options(void)
{
    log_it(L_INFO, "\n=== Test: Page Growth Options ===");
    
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
    log_it(L_INFO, "After filling first page (4KB):");
    log_it(L_INFO, "  Pages: %zu (expected 1)", l_stats.page_count);
    log_it(L_INFO, "  Used: %zu bytes", l_stats.total_used);
    dap_assert(l_stats.page_count == 1, "Still on first page");
    
    // This should trigger new page (with growth factor 1.5 = 6KB)
    dap_arena_alloc(l_arena, 1024);  // Exceeds first page
    
    dap_arena_get_stats(l_arena, &l_stats);
    log_it(L_INFO, "After allocation requiring new page:");
    log_it(L_INFO, "  Pages: %zu (expected 2)", l_stats.page_count);
    log_it(L_INFO, "  Total allocated: %zu bytes", l_stats.total_allocated);
    log_it(L_INFO, "  Total used: %zu bytes", l_stats.total_used);
    
    dap_assert(l_stats.page_count >= 2, "Multiple pages created");
    
    dap_arena_free(l_arena);
    
    log_it(L_INFO, "✓ Growth options test passed");
}

/**
 * @brief Regression: refcounted reset must preserve reusable page chain
 */
static void s_test_reset_chain_refcount(void)
{
    log_it(L_INFO, "\n=== Test: Reset Preserves Chain (Refcounted) ===");
    
    dap_arena_t *l_arena = dap_arena_new_opt((dap_arena_opt_t){
        .initial_size = 512,
        .allow_small_pages = true,
        .page_growth_factor = 1.0,
        .use_refcount = true
    });
    dap_assert(l_arena != NULL, "Arena created");
    
    for (int i = 0; i < 4; i++) {
        void *l_ptr = dap_arena_alloc(l_arena, 400);
        dap_assert(l_ptr != NULL, "Initial multi-page alloc");
    }
    
    dap_arena_stats_t l_stats_before, l_stats_after;
    dap_arena_get_stats(l_arena, &l_stats_before);
    dap_assert(l_stats_before.page_count == 4, "Four pages created");
    
    dap_arena_reset(l_arena);
    dap_assert(dap_arena_alloc(l_arena, 400) != NULL, "Alloc after reset #1");
    dap_assert(dap_arena_alloc(l_arena, 400) != NULL, "Alloc after reset #2");
    
    dap_arena_get_stats(l_arena, &l_stats_after);
    dap_assert(l_stats_after.page_count == l_stats_before.page_count,
               "Page chain preserved after reset+regrow");
    dap_assert(l_stats_after.total_allocated == l_stats_before.total_allocated,
               "No extra allocation when reusing preserved chain");
    
    dap_arena_free(l_arena);
    
    log_it(L_INFO, "✓ Refcounted reset-chain regression test passed");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    dap_log_level_set(L_INFO);
    
    log_it(L_INFO, "\n═══════════════════════════════════════════════════════");
    log_it(L_INFO, "  DAP Arena Refcount Test Suite");
    log_it(L_INFO, "  Hybrid approach with dap_arena_opt_t");
    log_it(L_INFO, "═══════════════════════════════════════════════════════");
    
    s_test_basic_refcount();
    s_test_stats_refcount();
    s_test_comparison();
    s_test_growth_options();
    s_test_reset_chain_refcount();
    
    log_it(L_INFO, "\n═══════════════════════════════════════════════════════");
    log_it(L_INFO, "  ✅ ALL TESTS PASSED");
    log_it(L_INFO, "═══════════════════════════════════════════════════════\n");
    
    return 0;
}
