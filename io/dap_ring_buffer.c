/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * This file is part of DAP SDK the open source project
 *
 *    DAP SDK is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP SDK is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dap_ring_buffer.h"
#include "dap_common.h"
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "dap_ring_buffer"

/**
 * @brief Round up to next power of 2
 */
static inline size_t s_next_power_of_2(size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
#if SIZE_MAX > 0xFFFFFFFF
    n |= n >> 32;
#endif
    return n + 1;
}

/**
 * @brief Create a new ring buffer
 */
dap_ring_buffer_t *dap_ring_buffer_create(size_t a_capacity) {
    if (a_capacity == 0) {
        log_it(L_ERROR, "Ring buffer capacity cannot be 0");
        return NULL;
    }
    
    // Round up to power of 2
    size_t l_capacity = s_next_power_of_2(a_capacity);
    
    // Allocate ring buffer with flexible array member
    size_t l_size = sizeof(dap_ring_buffer_t) + l_capacity * sizeof(void*);
    dap_ring_buffer_t *l_rb = (dap_ring_buffer_t*)aligned_alloc(DAP_RING_BUFFER_CACHE_LINE, l_size);
    
    if (!l_rb) {
        log_it(L_CRITICAL, "Failed to allocate ring buffer of size %zu", l_size);
        return NULL;
    }
    
    memset(l_rb, 0, l_size);
    
    l_rb->capacity = l_capacity;
    l_rb->mask = l_capacity - 1;
    
    // Initialize atomics
    atomic_store_explicit(&l_rb->write_pos, 0, memory_order_relaxed);
    atomic_store_explicit(&l_rb->read_pos, 0, memory_order_relaxed);
    atomic_store_explicit(&l_rb->total_pushes, 0, memory_order_relaxed);
    atomic_store_explicit(&l_rb->total_pops, 0, memory_order_relaxed);
    atomic_store_explicit(&l_rb->total_full, 0, memory_order_relaxed);
    atomic_store_explicit(&l_rb->total_empty, 0, memory_order_relaxed);
    
    log_it(L_DEBUG, "Created ring buffer: capacity=%zu (requested=%zu)", l_capacity, a_capacity);
    
    return l_rb;
}

/**
 * @brief Delete ring buffer
 */
void dap_ring_buffer_delete(dap_ring_buffer_t *a_rb) {
    if (!a_rb) {
        return;
    }
    
    // Get final stats
    uint64_t l_pushes = atomic_load_explicit(&a_rb->total_pushes, memory_order_relaxed);
    uint64_t l_pops = atomic_load_explicit(&a_rb->total_pops, memory_order_relaxed);
    uint64_t l_full = atomic_load_explicit(&a_rb->total_full, memory_order_relaxed);
    uint64_t l_empty = atomic_load_explicit(&a_rb->total_empty, memory_order_relaxed);
    
    log_it(L_DEBUG, "Deleting ring buffer: capacity=%zu, total_pushes=%"PRIu64", total_pops=%"PRIu64", "
                    "total_full=%"PRIu64", total_empty=%"PRIu64,
           a_rb->capacity, l_pushes, l_pops, l_full, l_empty);
    
    free(a_rb);
}

/**
 * @brief Push pointer into ring buffer (producer side)
 */
bool dap_ring_buffer_push(dap_ring_buffer_t *a_rb, void *a_ptr) {
    if (!a_rb || !a_ptr) {
        return false;
    }
    
    // Load current positions
    size_t l_write = atomic_load_explicit(&a_rb->write_pos, memory_order_relaxed);
    size_t l_read = atomic_load_explicit(&a_rb->read_pos, memory_order_acquire);
    
    // Check if full (next write position would equal read position)
    size_t l_next_write = (l_write + 1) & a_rb->mask;
    if (l_next_write == (l_read & a_rb->mask)) {
        atomic_fetch_add_explicit(&a_rb->total_full, 1, memory_order_relaxed);
        return false;  // Buffer is full
    }
    
    // Write data at current write position
    a_rb->data[l_write & a_rb->mask] = a_ptr;
    
    // Advance write position with release semantics
    // This ensures all previous writes are visible before updating write_pos
    atomic_store_explicit(&a_rb->write_pos, l_next_write, memory_order_release);
    
    atomic_fetch_add_explicit(&a_rb->total_pushes, 1, memory_order_relaxed);
    
    return true;
}

/**
 * @brief Pop pointer from ring buffer (consumer side)
 */
void *dap_ring_buffer_pop(dap_ring_buffer_t *a_rb) {
    if (!a_rb) {
        return NULL;
    }
    
    // Load current positions
    size_t l_read = atomic_load_explicit(&a_rb->read_pos, memory_order_relaxed);
    size_t l_write = atomic_load_explicit(&a_rb->write_pos, memory_order_acquire);
    
    // Check if empty
    if (l_read == l_write) {
        atomic_fetch_add_explicit(&a_rb->total_empty, 1, memory_order_relaxed);
        return NULL;  // Buffer is empty
    }
    
    // Read data at current read position
    void *l_ptr = a_rb->data[l_read & a_rb->mask];
    
    // Clear the slot (optional, for debugging)
    a_rb->data[l_read & a_rb->mask] = NULL;
    
    // Advance read position with release semantics
    // This ensures the data read is complete before updating read_pos
    size_t l_next_read = (l_read + 1) & a_rb->mask;
    atomic_store_explicit(&a_rb->read_pos, l_next_read, memory_order_release);
    
    atomic_fetch_add_explicit(&a_rb->total_pops, 1, memory_order_relaxed);
    
    return l_ptr;
}

/**
 * @brief Get statistics from ring buffer
 */
void dap_ring_buffer_get_stats(const dap_ring_buffer_t *a_rb,
                                 uint64_t *a_total_pushes,
                                 uint64_t *a_total_pops,
                                 uint64_t *a_total_full,
                                 uint64_t *a_total_empty) {
    if (!a_rb) {
        return;
    }
    
    if (a_total_pushes) {
        *a_total_pushes = atomic_load_explicit(&a_rb->total_pushes, memory_order_relaxed);
    }
    if (a_total_pops) {
        *a_total_pops = atomic_load_explicit(&a_rb->total_pops, memory_order_relaxed);
    }
    if (a_total_full) {
        *a_total_full = atomic_load_explicit(&a_rb->total_full, memory_order_relaxed);
    }
    if (a_total_empty) {
        *a_total_empty = atomic_load_explicit(&a_rb->total_empty, memory_order_relaxed);
    }
}

/**
 * @brief Reset statistics
 */
void dap_ring_buffer_reset_stats(dap_ring_buffer_t *a_rb) {
    if (!a_rb) {
        return;
    }
    
    atomic_store_explicit(&a_rb->total_pushes, 0, memory_order_relaxed);
    atomic_store_explicit(&a_rb->total_pops, 0, memory_order_relaxed);
    atomic_store_explicit(&a_rb->total_full, 0, memory_order_relaxed);
    atomic_store_explicit(&a_rb->total_empty, 0, memory_order_relaxed);
}
