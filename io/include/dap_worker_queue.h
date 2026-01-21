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

#include "dap_ring_buffer.h"
#include "dap_events_socket.h"

/**
 * @brief Ring buffer-based queue for inter-worker communication
 * 
 * Replacement for pipe-based dap_events_socket queues.
 * Uses lock-free ring buffer + eventfd for notifications.
 * 
 * Benefits over pipes:
 * - No system call overhead for push/pop
 * - Better cache locality
 * - Lock-free operation
 * - Lower latency
 */

typedef struct dap_worker_queue {
    dap_ring_buffer_t *ring_buffer;     ///< Lock-free ring buffer for data
    dap_events_socket_t *event_socket;  ///< Event socket for reactor integration (eventfd)
    void (*callback)(void *);           ///< Callback function for processing popped items
    dap_worker_t *worker;               ///< Associated worker
} dap_worker_queue_t;

/**
 * @brief Create worker queue
 * @param a_worker Associated worker
 * @param a_capacity Ring buffer capacity
 * @param a_callback Callback function for processing items
 * @return Queue instance or NULL on error
 */
dap_worker_queue_t *dap_worker_queue_create(dap_worker_t *a_worker, size_t a_capacity, void (*a_callback)(void *));

/**
 * @brief Delete worker queue
 * @param a_queue Queue to delete
 */
void dap_worker_queue_delete(dap_worker_queue_t *a_queue);

/**
 * @brief Push item to queue (thread-safe, lock-free)
 * @param a_queue Queue
 * @param a_item Item to push
 * @return true if successful, false if full
 */
bool dap_worker_queue_push(dap_worker_queue_t *a_queue, void *a_item);

/**
 * @brief Process all available items in queue (called by reactor)
 * @param a_queue Queue
 * @return Number of items processed
 */
size_t dap_worker_queue_process(dap_worker_queue_t *a_queue);

/**
 * @brief Get queue statistics
 * @param a_queue Queue
 * @param a_size Current size
 * @param a_total_pushes Total pushes
 * @param a_total_pops Total pops
 * @param a_total_full Total times full
 */
void dap_worker_queue_get_stats(const dap_worker_queue_t *a_queue,
                                  size_t *a_size,
                                  uint64_t *a_total_pushes,
                                  uint64_t *a_total_pops,
                                  uint64_t *a_total_full);
