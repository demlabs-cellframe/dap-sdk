/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform) the open source project
 *
 *    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>

#include "dap_arena.h"
#include "dap_common.h"

#define LOG_TAG "dap_arena"

static bool s_debug_more = false;
// Default page size: 64KB (good balance between allocation count and memory overhead)
#define DAP_ARENA_DEFAULT_PAGE_SIZE (64 * 1024)

// Default page growth factor
#define DAP_ARENA_DEFAULT_GROWTH_FACTOR 2.0

// Minimum page size: 4KB
#define DAP_ARENA_MIN_PAGE_SIZE (4 * 1024)

// Alignment for all allocations (8 bytes for 64-bit pointers)
#define DAP_ARENA_ALIGNMENT 8

// Round up to alignment boundary
#define ALIGN_UP(n, align) (((n) + (align) - 1) & ~((align) - 1))

/**
 * @brief Arena memory page (refcounted version)
 * 
 * Each page has a header followed by data region.
 * Pages are linked in a list.
 * 
 * When use_refcount=true, pages track references and can be
 * freed independently when refcount reaches 0.
 */
typedef struct dap_arena_page {
    struct dap_arena_page *next;  // Next page in list
    size_t size;                  // Total size of this page (including header)
    size_t used;                  // Bytes used in this page
    
    // ⭐ Refcounting support (only used if arena->use_refcount=true)
    atomic_int refcount;          // Atomic reference count (thread-safe)
    bool is_refcounted;           // Flag to check if this page uses refcount
    
    uint8_t data[];               // Flexible array member for data
} dap_arena_page_t;

/**
 * @brief Arena allocator structure
 */
struct dap_arena {
    dap_arena_page_t *first_page;   // First page in list
    dap_arena_page_t *current_page; // Current page for allocation
    
    size_t page_size;               // Default size for new pages
    double page_growth_factor;      // Multiplier for subsequent pages
    size_t max_page_size;           // Maximum page size (0 = unlimited)
    
    size_t total_allocated;         // Total bytes allocated from system
    size_t allocation_count;        // Number of allocations
    
    bool is_thread_local;           // Thread-local flag
    bool use_refcount;              // Enable reference counting
};

/* ========================================================================== */
/*                         PAGE MANAGEMENT                                    */
/* ========================================================================== */

/**
 * @brief Create new arena page
 */
static dap_arena_page_t *s_arena_page_new(size_t a_size, bool a_use_refcount)
{
    // Ensure minimum size
    if (a_size < DAP_ARENA_MIN_PAGE_SIZE) {
        a_size = DAP_ARENA_MIN_PAGE_SIZE;
    }
    
    // Allocate page
    size_t l_total_size = sizeof(dap_arena_page_t) + a_size;
    dap_arena_page_t *l_page = (dap_arena_page_t *)DAP_NEW_Z_SIZE(uint8_t, l_total_size);
    
    if (!l_page) {
        log_it(L_ERROR, "Failed to allocate arena page (%zu bytes)", l_total_size);
        return NULL;
    }
    
    l_page->next = NULL;
    l_page->size = a_size;
    l_page->used = 0;
    l_page->is_refcounted = a_use_refcount;
    
    // Initialize refcount (start at 1 if refcounting enabled)
    if (a_use_refcount) {
        atomic_init(&l_page->refcount, 1);
        debug_if(s_debug_more, L_DEBUG, "Created refcounted page %p (size: %zu, refcount: 1)", l_page, a_size);
    }
    
    return l_page;
}

/**
 * @brief Free all pages in arena
 */
static void s_arena_free_pages(dap_arena_page_t *a_page)
{
    while (a_page) {
        dap_arena_page_t *l_next = a_page->next;
        
        // Warn if freeing refcounted page with active references
        if (a_page->is_refcounted) {
            int l_refcount = atomic_load(&a_page->refcount);
            if (l_refcount > 0) {
                log_it(L_WARNING, "Freeing arena page %p with active refcount: %d", 
                       a_page, l_refcount);
            }
        }
        
        DAP_DELETE(a_page);
        a_page = l_next;
    }
}

/* ========================================================================== */
/*                         PUBLIC API - CREATION                              */
/* ========================================================================== */

/**
 * @brief Create new arena allocator with options
 */
dap_arena_t *dap_arena_new_opt(dap_arena_opt_t a_opt)
{
    // Apply defaults
    size_t l_initial_size = a_opt.initial_size > 0 ? a_opt.initial_size : DAP_ARENA_DEFAULT_PAGE_SIZE;
    double l_growth_factor = a_opt.page_growth_factor > 0.0 ? a_opt.page_growth_factor : DAP_ARENA_DEFAULT_GROWTH_FACTOR;
    
    // Allocate arena structure
    dap_arena_t *l_arena = DAP_NEW_Z(dap_arena_t);
    if (!l_arena) {
        log_it(L_ERROR, "Failed to allocate arena structure");
        return NULL;
    }
    
    // Create first page
    dap_arena_page_t *l_page = s_arena_page_new(l_initial_size, a_opt.use_refcount);
    if (!l_page) {
        DAP_DELETE(l_arena);
        return NULL;
    }
    
    l_arena->first_page = l_page;
    l_arena->current_page = l_page;
    l_arena->page_size = l_initial_size;
    l_arena->page_growth_factor = l_growth_factor;
    l_arena->max_page_size = a_opt.max_page_size;
    l_arena->total_allocated = sizeof(dap_arena_page_t) + l_initial_size;
    l_arena->allocation_count = 0;
    l_arena->is_thread_local = a_opt.thread_local;
    l_arena->use_refcount = a_opt.use_refcount;
    
    debug_if(s_debug_more, L_DEBUG, "Arena created (page size: %zu, refcount: %s, thread_local: %s)", 
           l_initial_size, 
           a_opt.use_refcount ? "yes" : "no",
           a_opt.thread_local ? "yes" : "no");
    
    return l_arena;
}

/**
 * @brief Create new arena allocator (simple version)
 */
dap_arena_t *dap_arena_new(size_t a_initial_size)
{
    return dap_arena_new_opt((dap_arena_opt_t){
        .initial_size = a_initial_size
    });
}

/**
 * @brief Create thread-local arena
 */
dap_arena_t *dap_arena_new_thread_local(size_t a_initial_size)
{
    return dap_arena_new_opt((dap_arena_opt_t){
        .initial_size = a_initial_size,
        .thread_local = true
    });
}

/* ========================================================================== */
/*                         PUBLIC API - ALLOCATION                            */
/* ========================================================================== */

/**
 * @brief Allocate memory from arena (internal helper)
 * 
 * @return Pointer to allocated memory and page handle (for refcounted arenas)
 */
static inline void *s_arena_alloc_internal(dap_arena_t *a_arena, size_t a_size, dap_arena_page_t **a_out_page)
{
    if (!a_arena) {
        log_it(L_ERROR, "NULL arena pointer");
        return NULL;
    }
    
    if (a_size == 0) {
        return NULL;
    }
    
    // Align size
    size_t l_aligned_size = ALIGN_UP(a_size, DAP_ARENA_ALIGNMENT);
    
    // Check if current page has enough space
    dap_arena_page_t *l_page = a_arena->current_page;
    
    if (l_page->used + l_aligned_size > l_page->size) {
        // Need new page
        // Apply growth factor to page size
        size_t l_new_page_size = (size_t)(a_arena->page_size * a_arena->page_growth_factor);
        
        // Respect max_page_size if set
        if (a_arena->max_page_size > 0 && l_new_page_size > a_arena->max_page_size) {
            l_new_page_size = a_arena->max_page_size;
        }
        
        // If allocation is larger than default page size, create larger page
        if (l_aligned_size > l_new_page_size) {
            l_new_page_size = l_aligned_size * 2; // Double for future allocations
        }
        
        dap_arena_page_t *l_new_page = s_arena_page_new(l_new_page_size, a_arena->use_refcount);
        if (!l_new_page) {
            return NULL;
        }
        
        // Link new page to end of list
        l_page->next = l_new_page;
        a_arena->current_page = l_new_page;
        a_arena->page_size = l_new_page_size; // Update for next growth
        a_arena->total_allocated += sizeof(dap_arena_page_t) + l_new_page_size;
        
        l_page = l_new_page;
        
        debug_if(s_debug_more, L_DEBUG, "Arena: new page allocated (%zu bytes, total pages: %zu)", 
               l_new_page_size, a_arena->total_allocated / (sizeof(dap_arena_page_t) + a_arena->page_size));
    }
    
    // Allocate from current page (bump pointer)
    void *l_ptr = l_page->data + l_page->used;
    l_page->used += l_aligned_size;
    a_arena->allocation_count++;
    
    // Return page handle if requested (for refcounted arenas)
    if (a_out_page) {
        *a_out_page = l_page;
    }
    
    return l_ptr;
}

/**
 * @brief Allocate memory from arena
 */
void *dap_arena_alloc(dap_arena_t *a_arena, size_t a_size)
{
    return s_arena_alloc_internal(a_arena, a_size, NULL);
}

/**
 * @brief Allocate memory from refcounted arena (extended version)
 */
bool dap_arena_alloc_ex(dap_arena_t *a_arena, size_t a_size, dap_arena_alloc_ex_t *a_result)
{
    if (!a_result) {
        log_it(L_ERROR, "NULL result pointer");
        return false;
    }
    
    if (!a_arena) {
        log_it(L_ERROR, "NULL arena pointer");
        return false;
    }
    
    if (!a_arena->use_refcount) {
        log_it(L_ERROR, "dap_arena_alloc_ex() called on non-refcounted arena");
        return false;
    }
    
    dap_arena_page_t *l_page = NULL;
    void *l_ptr = s_arena_alloc_internal(a_arena, a_size, &l_page);
    
    if (!l_ptr) {
        return false;
    }
    
    // Increment page refcount (allocation holds a reference)
    atomic_fetch_add(&l_page->refcount, 1);
    
    a_result->ptr = l_ptr;
    a_result->page_handle = (void*)l_page;
    
    debug_if(s_debug_more, L_DEBUG, "Arena alloc_ex: %p from page %p (refcount: %d)", 
           l_ptr, l_page, atomic_load(&l_page->refcount));
    
    return true;
}

/**
 * @brief Allocate zero-initialized memory
 */
void *dap_arena_alloc_zero(dap_arena_t *a_arena, size_t a_size)
{
    void *l_ptr = dap_arena_alloc(a_arena, a_size);
    if (l_ptr) {
        memset(l_ptr, 0, a_size);
    }
    return l_ptr;
}

/**
 * @brief Allocate aligned memory
 */
void *dap_arena_alloc_aligned(dap_arena_t *a_arena, size_t a_size, size_t a_alignment)
{
    if (!a_arena) {
        log_it(L_ERROR, "NULL arena pointer");
        return NULL;
    }
    
    // Check alignment is power of 2
    if (a_alignment == 0 || (a_alignment & (a_alignment - 1)) != 0) {
        log_it(L_ERROR, "Alignment must be power of 2 (got %zu)", a_alignment);
        return NULL;
    }
    
    // Allocate extra space for alignment
    size_t l_padding = a_alignment - 1;
    size_t l_total_size = a_size + l_padding;
    
    void *l_ptr = dap_arena_alloc(a_arena, l_total_size);
    if (!l_ptr) {
        return NULL;
    }
    
    // Align pointer
    uintptr_t l_addr = (uintptr_t)l_ptr;
    uintptr_t l_aligned_addr = ALIGN_UP(l_addr, a_alignment);
    
    return (void *)l_aligned_addr;
}

/**
 * @brief Duplicate string in arena
 */
char *dap_arena_strdup(dap_arena_t *a_arena, const char *a_str)
{
    if (!a_str) {
        return NULL;
    }
    
    size_t l_len = strlen(a_str);
    return dap_arena_strndup(a_arena, a_str, l_len);
}

/**
 * @brief Duplicate string with length
 */
char *dap_arena_strndup(dap_arena_t *a_arena, const char *a_str, size_t a_len)
{
    if (!a_str) {
        return NULL;
    }
    
    char *l_copy = (char *)dap_arena_alloc(a_arena, a_len + 1);
    if (!l_copy) {
        return NULL;
    }
    
    memcpy(l_copy, a_str, a_len);
    l_copy[a_len] = '\0';
    
    return l_copy;
}

/**
 * @brief Reset arena (bulk deallocation)
 */
void dap_arena_reset(dap_arena_t *a_arena)
{
    if (!a_arena) {
        return;
    }
    
    // For refcounted arenas, only reset pages with refcount=0
    if (a_arena->use_refcount) {
        size_t l_skipped = 0;
        for (dap_arena_page_t *l_page = a_arena->first_page; l_page; l_page = l_page->next) {
            int l_refcount = atomic_load(&l_page->refcount);
            if (l_refcount == 0 || l_refcount == 1) {
                // refcount=1 means only arena holds it, safe to reset
                l_page->used = 0;
                atomic_store(&l_page->refcount, 1);
            } else {
                // Active references exist, skip this page
                l_skipped++;
                debug_if(s_debug_more, L_DEBUG, "Arena reset: skipping page %p (refcount: %d)", l_page, l_refcount);
            }
        }
        
        if (l_skipped > 0) {
            log_it(L_WARNING, "Arena reset: %zu pages skipped due to active references", l_skipped);
        }
    } else {
        // Standard arena: reset all pages
        for (dap_arena_page_t *l_page = a_arena->first_page; l_page; l_page = l_page->next) {
            l_page->used = 0;
        }
    }
    
    // Reset to first page
    a_arena->current_page = a_arena->first_page;
    a_arena->allocation_count = 0;
    
    debug_if(s_debug_more, L_DEBUG, "Arena reset (%zu total allocated remains available for reuse)",
           a_arena->total_allocated);
}

/* ========================================================================== */
/*                         REFCOUNT API                                       */
/* ========================================================================== */

/**
 * @brief Increment page reference count
 */
void dap_arena_page_ref(void *a_page_handle)
{
    if (!a_page_handle) {
        log_it(L_ERROR, "NULL page handle");
        return;
    }
    
    dap_arena_page_t *l_page = (dap_arena_page_t*)a_page_handle;
    
    if (!l_page->is_refcounted) {
        log_it(L_ERROR, "Attempted to ref non-refcounted page %p", l_page);
        return;
    }
    
    int l_old_refcount = atomic_fetch_add(&l_page->refcount, 1);
    
    debug_if(s_debug_more, L_DEBUG, "Arena page_ref: %p (refcount: %d -> %d)", 
           l_page, l_old_refcount, l_old_refcount + 1);
}

/**
 * @brief Decrement page reference count
 */
void dap_arena_page_unref(void *a_page_handle)
{
    if (!a_page_handle) {
        log_it(L_ERROR, "NULL page handle");
        return;
    }
    
    dap_arena_page_t *l_page = (dap_arena_page_t*)a_page_handle;
    
    if (!l_page->is_refcounted) {
        log_it(L_ERROR, "Attempted to unref non-refcounted page %p", l_page);
        return;
    }
    
    int l_old_refcount = atomic_fetch_sub(&l_page->refcount, 1);
    
    debug_if(s_debug_more, L_DEBUG, "Arena page_unref: %p (refcount: %d -> %d)", 
           l_page, l_old_refcount, l_old_refcount - 1);
    
    if (l_old_refcount <= 0) {
        log_it(L_ERROR, "Arena page_unref: refcount underflow on page %p (was %d)", 
               l_page, l_old_refcount);
    }
    
    // Note: We don't free the page immediately when refcount reaches 0.
    // Pages are reused by the arena. Only dap_arena_free() releases memory.
}

/**
 * @brief Get page reference count
 */
int dap_arena_page_get_refcount(void *a_page_handle)
{
    if (!a_page_handle) {
        return -1;
    }
    
    dap_arena_page_t *l_page = (dap_arena_page_t*)a_page_handle;
    
    if (!l_page->is_refcounted) {
        return -1;
    }
    
    return atomic_load(&l_page->refcount);
}

/**
 * @brief Get arena statistics
 */
void dap_arena_get_stats(const dap_arena_t *a_arena, dap_arena_stats_t *a_stats)
{
    if (!a_arena || !a_stats) {
        return;
    }
    
    memset(a_stats, 0, sizeof(dap_arena_stats_t));
    
    a_stats->total_allocated = a_arena->total_allocated;
    a_stats->allocation_count = a_arena->allocation_count;
    
    // Calculate used bytes, page count, and total refcount
    size_t l_page_count = 0;
    size_t l_total_used = 0;
    size_t l_total_refcount = 0;
    
    for (const dap_arena_page_t *l_page = a_arena->first_page; l_page; l_page = l_page->next) {
        l_page_count++;
        l_total_used += l_page->used;
        
        if (l_page->is_refcounted) {
            l_total_refcount += (size_t)atomic_load(&l_page->refcount);
        }
        
        if (l_page == a_arena->current_page) {
            a_stats->current_page = l_page_count - 1;
        }
    }
    
    a_stats->total_used = l_total_used;
    a_stats->page_count = l_page_count;
    a_stats->active_refcount = l_total_refcount;
}

/**
 * @brief Free arena
 */
void dap_arena_free(dap_arena_t *a_arena)
{
    if (!a_arena) {
        return;
    }
    
    s_arena_free_pages(a_arena->first_page);
    DAP_DELETE(a_arena);
    
    debug_if(s_debug_more, L_DEBUG, "Arena freed");
}

