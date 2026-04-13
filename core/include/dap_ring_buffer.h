/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2025
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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdalign.h>

/**
 * @brief MPSC (Multiple Producer Single Consumer) ring buffer for inter-worker communication
 * 
 * High-performance ring buffer optimized for passing pointers between worker threads.
 * Multiple producers are synchronized via a lightweight spinlock on the write side;
 * the single consumer (owning worker) pops lock-free.
 * 
 * Features:
 * - MPSC safe: multiple threads can push concurrently
 * - Single consumer pops without locking
 * - Cache-line aligned to prevent false sharing
 * - Power-of-2 capacity for fast modulo operations
 * 
 * Memory ordering:
 * - Producer acquires write_lock, uses release semantics for write_pos
 * - Consumer uses acquire semantics for read_pos
 */

#define DAP_RING_BUFFER_CACHE_LINE 64

typedef struct dap_ring_buffer {
    // Producer-side (write) - isolated on own cache line
    alignas(DAP_RING_BUFFER_CACHE_LINE) atomic_size_t write_pos;
    atomic_flag write_lock;  // Spinlock for MPSC producer synchronization
    
    // Consumer-side (read) - isolated on own cache line
    alignas(DAP_RING_BUFFER_CACHE_LINE) atomic_size_t read_pos;
    
    // Shared metadata - read-only after creation
    alignas(DAP_RING_BUFFER_CACHE_LINE) size_t capacity;      // Must be power of 2
    size_t mask;           // capacity - 1, for fast modulo
    
    // Stats (optional, for debugging)
    atomic_uint_fast64_t total_pushes;
    atomic_uint_fast64_t total_pops;
    atomic_uint_fast64_t total_full;
    atomic_uint_fast64_t total_empty;
    
    // Data array (aligned to cache line)
    alignas(DAP_RING_BUFFER_CACHE_LINE) void *data[];  // Flexible array member
} dap_ring_buffer_t;

/**
 * @brief Create a new ring buffer
 * @param a_capacity Capacity (will be rounded up to next power of 2)
 * @return Ring buffer instance or NULL on error
 */
dap_ring_buffer_t *dap_ring_buffer_create(size_t a_capacity);

/**
 * @brief Delete ring buffer
 * @param a_rb Ring buffer to delete
 */
void dap_ring_buffer_delete(dap_ring_buffer_t *a_rb);

/**
 * @brief Push pointer into ring buffer (producer side)
 * @param a_rb Ring buffer
 * @param a_ptr Pointer to push (must not be NULL)
 * @return true if successful, false if buffer is full
 */
bool dap_ring_buffer_push(dap_ring_buffer_t *a_rb, void *a_ptr);

/**
 * @brief Pop pointer from ring buffer (consumer side)
 * @param a_rb Ring buffer
 * @return Pointer or NULL if buffer is empty
 */
void *dap_ring_buffer_pop(dap_ring_buffer_t *a_rb);

/**
 * @brief Check if ring buffer is empty
 * @param a_rb Ring buffer
 * @return true if empty
 */
static inline bool dap_ring_buffer_is_empty(dap_ring_buffer_t *a_rb) {
    size_t l_read = atomic_load_explicit(&a_rb->read_pos, memory_order_acquire);
    size_t l_write = atomic_load_explicit(&a_rb->write_pos, memory_order_acquire);
    return l_read == l_write;
}

/**
 * @brief Check if ring buffer is full
 * @param a_rb Ring buffer
 * @return true if full
 */
static inline bool dap_ring_buffer_is_full(dap_ring_buffer_t *a_rb) {
    size_t l_write = atomic_load_explicit(&a_rb->write_pos, memory_order_relaxed);
    size_t l_read = atomic_load_explicit(&a_rb->read_pos, memory_order_acquire);
    // Full when next write position would equal read position
    return ((l_write + 1) & a_rb->mask) == (l_read & a_rb->mask);
}

/**
 * @brief Get current size of ring buffer
 * @param a_rb Ring buffer
 * @return Number of elements in buffer
 */
static inline size_t dap_ring_buffer_size(dap_ring_buffer_t *a_rb) {
    size_t l_write = atomic_load_explicit(&a_rb->write_pos, memory_order_acquire);
    size_t l_read = atomic_load_explicit(&a_rb->read_pos, memory_order_acquire);
    return (l_write - l_read) & a_rb->mask;
}

/**
 * @brief Get statistics from ring buffer
 * @param a_rb Ring buffer
 * @param a_total_pushes Output: total push operations
 * @param a_total_pops Output: total pop operations
 * @param a_total_full Output: total times buffer was full
 * @param a_total_empty Output: total times buffer was empty
 */
void dap_ring_buffer_get_stats(dap_ring_buffer_t *a_rb, 
                                 uint64_t *a_total_pushes,
                                 uint64_t *a_total_pops,
                                 uint64_t *a_total_full,
                                 uint64_t *a_total_empty);

/**
 * @brief Reset statistics
 * @param a_rb Ring buffer
 */
void dap_ring_buffer_reset_stats(dap_ring_buffer_t *a_rb);
