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

#pragma once

#include "dap_ring_buffer.h"
#include "dap_events_socket.h"

/**
 * @brief Ring buffer-based queue for inter-context communication
 * 
 * Universal queue for workers and proc_threads.
 * Uses lock-free ring buffer + cross-platform event socket for notifications.
 * 
 * Benefits over pipes:
 * - No system call overhead for push/pop
 * - Better cache locality
 * - Lock-free operation
 * - Lower latency
 * - Cross-platform (eventfd/kqueue/IOCP)
 */

typedef struct dap_context_queue {
    dap_ring_buffer_t *ring_buffer;     ///< Lock-free ring buffer for data
    dap_events_socket_t *event_socket;  ///< Event socket for reactor integration
    void (*callback)(void *);           ///< Callback function for processing popped items
    dap_context_t *context;             ///< Associated context (worker or proc_thread)
} dap_context_queue_t;

/**
 * @brief Create context queue
 * @param a_context Associated context (worker->context or proc_thread->context)
 * @param a_capacity Ring buffer capacity (0 = default)
 * @param a_callback Callback function for processing items
 * @return Queue instance or NULL on error
 */
dap_context_queue_t *dap_context_queue_create(dap_context_t *a_context, size_t a_capacity, void (*a_callback)(void *));

/**
 * @brief Delete context queue
 * @param a_queue Queue to delete
 */
void dap_context_queue_delete(dap_context_queue_t *a_queue);

/**
 * @brief Push item to queue (thread-safe, lock-free)
 * @param a_queue Queue
 * @param a_item Item to push
 * @return true if successful, false if full
 */
bool dap_context_queue_push(dap_context_queue_t *a_queue, void *a_item);

/**
 * @brief Process one item from queue (called by reactor)
 * @param a_queue Queue
 * @return 0 on success, -1 on error
 */
int dap_context_queue_process(dap_context_queue_t *a_queue);

/**
 * @brief Get queue statistics
 * @param a_queue Queue
 * @param a_size Current size
 * @param a_total_pushes Total pushes
 * @param a_total_pops Total pops
 * @param a_total_full Total times full
 */
void dap_context_queue_get_stats(dap_context_queue_t *a_queue,
                                  size_t *a_size,
                                  uint64_t *a_total_pushes,
                                  uint64_t *a_total_pops,
                                  uint64_t *a_total_full);
