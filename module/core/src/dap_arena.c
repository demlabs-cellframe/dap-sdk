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

#include "dap_arena.h"
#include "dap_common.h"

#define LOG_TAG "dap_arena"

// Debug flag
static bool g_arena_debug = false;

// Default page size: 64KB (good balance between allocation count and memory overhead)
#define DAP_ARENA_DEFAULT_PAGE_SIZE (64 * 1024)

// Minimum page size: 4KB
#define DAP_ARENA_MIN_PAGE_SIZE (4 * 1024)

// Alignment for all allocations (8 bytes for 64-bit pointers)
#define DAP_ARENA_ALIGNMENT 8

// Round up to alignment boundary
#define ALIGN_UP(n, align) (((n) + (align) - 1) & ~((align) - 1))

/**
 * @brief Arena memory page
 * 
 * Each page has a header followed by data region.
 * Pages are linked in a list.
 */
typedef struct dap_arena_page {
    struct dap_arena_page *next;  // Next page in list
    size_t size;                  // Total size of this page (including header)
    size_t used;                  // Bytes used in this page
    uint8_t data[];               // Flexible array member for data
} dap_arena_page_t;

/**
 * @brief Arena allocator structure
 */
struct dap_arena {
    dap_arena_page_t *first_page;   // First page in list
    dap_arena_page_t *current_page; // Current page for allocation
    size_t page_size;               // Default size for new pages
    size_t total_allocated;         // Total bytes allocated from system
    size_t allocation_count;        // Number of allocations
    bool is_thread_local;           // Thread-local flag
};

/* ========================================================================== */
/*                         PAGE MANAGEMENT                                    */
/* ========================================================================== */

/**
 * @brief Create new arena page
 */
static dap_arena_page_t *s_arena_page_new(size_t a_size)
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
    
    return l_page;
}

/**
 * @brief Free all pages in arena
 */
static void s_arena_free_pages(dap_arena_page_t *a_page)
{
    while (a_page) {
        dap_arena_page_t *l_next = a_page->next;
        DAP_DELETE(a_page);
        a_page = l_next;
    }
}

/* ========================================================================== */
/*                         PUBLIC API                                         */
/* ========================================================================== */

/**
 * @brief Create new arena allocator
 */
dap_arena_t *dap_arena_new(size_t a_initial_size)
{
    if (a_initial_size == 0) {
        a_initial_size = DAP_ARENA_DEFAULT_PAGE_SIZE;
    }
    
    // Allocate arena structure
    dap_arena_t *l_arena = DAP_NEW_Z(dap_arena_t);
    if (!l_arena) {
        log_it(L_ERROR, "Failed to allocate arena structure");
        return NULL;
    }
    
    // Create first page
    dap_arena_page_t *l_page = s_arena_page_new(a_initial_size);
    if (!l_page) {
        DAP_DELETE(l_arena);
        return NULL;
    }
    
    l_arena->first_page = l_page;
    l_arena->current_page = l_page;
    l_arena->page_size = a_initial_size;
    l_arena->total_allocated = sizeof(dap_arena_page_t) + a_initial_size;
    l_arena->allocation_count = 0;
    l_arena->is_thread_local = false;
    
    debug_if(g_arena_debug, L_DEBUG, "Arena created (page size: %zu bytes)", a_initial_size);
    
    return l_arena;
}

/**
 * @brief Create thread-local arena
 */
dap_arena_t *dap_arena_new_thread_local(size_t a_initial_size)
{
    dap_arena_t *l_arena = dap_arena_new(a_initial_size);
    if (l_arena) {
        l_arena->is_thread_local = true;
        debug_if(g_arena_debug, L_DEBUG, "Thread-local arena created");
    }
    return l_arena;
}

/**
 * @brief Allocate memory from arena
 */
void *dap_arena_alloc(dap_arena_t *a_arena, size_t a_size)
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
        size_t l_new_page_size = a_arena->page_size;
        
        // If allocation is larger than default page size, create larger page
        if (l_aligned_size > l_new_page_size) {
            l_new_page_size = l_aligned_size * 2; // Double for future allocations
        }
        
        dap_arena_page_t *l_new_page = s_arena_page_new(l_new_page_size);
        if (!l_new_page) {
            return NULL;
        }
        
        // Link new page to end of list
        l_page->next = l_new_page;
        a_arena->current_page = l_new_page;
        a_arena->total_allocated += sizeof(dap_arena_page_t) + l_new_page_size;
        l_page = l_new_page;
        
        debug_if(g_arena_debug, L_DEBUG, "Arena grew: new page %zu bytes (total: %zu bytes)",
               l_new_page_size, a_arena->total_allocated);
    }
    
    // Allocate from current page (bump pointer)
    void *l_ptr = &l_page->data[l_page->used];
    l_page->used += l_aligned_size;
    a_arena->allocation_count++;
    
    return l_ptr;
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
    
    // Reset all pages to unused state
    for (dap_arena_page_t *l_page = a_arena->first_page; l_page; l_page = l_page->next) {
        l_page->used = 0;
    }
    
    // Reset to first page
    a_arena->current_page = a_arena->first_page;
    a_arena->allocation_count = 0;
    
    debug_if(g_arena_debug, L_DEBUG, "Arena reset (%zu total allocated remains available for reuse)",
           a_arena->total_allocated);
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
    
    // Calculate used bytes and page count
    size_t l_page_count = 0;
    size_t l_total_used = 0;
    
    for (const dap_arena_page_t *l_page = a_arena->first_page; l_page; l_page = l_page->next) {
        l_page_count++;
        l_total_used += l_page->used;
        
        if (l_page == a_arena->current_page) {
            a_stats->current_page = l_page_count - 1;
        }
    }
    
    a_stats->total_used = l_total_used;
    a_stats->page_count = l_page_count;
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
    
    debug_if(g_arena_debug, L_DEBUG, "Arena freed");
}

