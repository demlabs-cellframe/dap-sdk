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

#include "dap_worker_queue.h"
#include "dap_common.h"
#include "dap_events_socket.h"
#include "dap_context.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define LOG_TAG "dap_worker_queue"

// Default ring buffer capacity (power of 2)
#define DAP_WORKER_QUEUE_DEFAULT_CAPACITY 16384

/**
 * @brief Callback from reactor when eventfd is readable (items available in queue)
 */
static void s_eventfd_read_callback(dap_events_socket_t *a_es, void *a_arg) {
    dap_worker_queue_t *l_queue = (dap_worker_queue_t *)a_arg;
    
    if (!l_queue) {
        log_it(L_ERROR, "Eventfd callback: NULL queue pointer");
        return;
    }
    
    // Read eventfd to clear notification (must read 8 bytes)
    uint64_t l_value = 0;
    ssize_t l_ret = read(a_es->fd, &l_value, sizeof(l_value));
    if (l_ret != sizeof(l_value)) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_it(L_WARNING, "Failed to read eventfd: %s", strerror(errno));
        }
        return;
    }
    
    // Process all available items in queue
    size_t l_processed = dap_worker_queue_process(l_queue);
    
    if (l_processed > 0) {
        debug_if(g_debug_reactor, L_DEBUG, "Worker queue processed %zu items (eventfd value=%"PRIu64")",
                 l_processed, l_value);
    }
}

/**
 * @brief Create worker queue
 */
dap_worker_queue_t *dap_worker_queue_create(dap_worker_t *a_worker, size_t a_capacity, void (*a_callback)(void *)) {
    if (!a_worker || !a_callback) {
        log_it(L_ERROR, "Worker queue create: NULL worker or callback");
        return NULL;
    }
    
    dap_worker_queue_t *l_queue = DAP_NEW_Z(dap_worker_queue_t);
    if (!l_queue) {
        log_it(L_CRITICAL, "Failed to allocate worker queue");
        return NULL;
    }
    
    // Create ring buffer
    size_t l_capacity = a_capacity > 0 ? a_capacity : DAP_WORKER_QUEUE_DEFAULT_CAPACITY;
    l_queue->ring_buffer = dap_ring_buffer_create(l_capacity);
    if (!l_queue->ring_buffer) {
        log_it(L_ERROR, "Failed to create ring buffer for worker queue");
        DAP_DELETE(l_queue);
        return NULL;
    }
    
    l_queue->callback = a_callback;
    l_queue->worker = a_worker;
    
    // Create eventfd for notifications (EFD_NONBLOCK | EFD_SEMAPHORE)
    // EFD_SEMAPHORE: each read decrements counter by 1 (better for multiple items)
    int l_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC | EFD_SEMAPHORE);
    if (l_eventfd < 0) {
        log_it(L_ERROR, "Failed to create eventfd: %s", strerror(errno));
        dap_ring_buffer_delete(l_queue->ring_buffer);
        DAP_DELETE(l_queue);
        return NULL;
    }
    
    // Wrap eventfd in dap_events_socket for reactor integration
    l_queue->event_socket = dap_events_socket_wrap_no_add(l_eventfd, s_eventfd_read_callback, l_queue);
    if (!l_queue->event_socket) {
        log_it(L_ERROR, "Failed to wrap eventfd in events_socket");
        close(l_eventfd);
        dap_ring_buffer_delete(l_queue->ring_buffer);
        DAP_DELETE(l_queue);
        return NULL;
    }
    
    l_queue->event_socket->type = DESCRIPTOR_TYPE_EVENT;
    l_queue->event_socket->flags |= DAP_SOCK_READY_TO_READ;
    
    // Add event socket to worker's reactor
    if (dap_worker_add_events_socket(a_worker, l_queue->event_socket) != 0) {
        log_it(L_ERROR, "Failed to add eventfd to worker reactor");
        dap_events_socket_delete_unsafe(l_queue->event_socket, false);
        dap_ring_buffer_delete(l_queue->ring_buffer);
        DAP_DELETE(l_queue);
        return NULL;
    }
    
    log_it(L_INFO, "Created worker queue: worker=%u, capacity=%zu, eventfd=%d",
           a_worker->id, l_capacity, l_eventfd);
    
    return l_queue;
}

/**
 * @brief Delete worker queue
 */
void dap_worker_queue_delete(dap_worker_queue_t *a_queue) {
    if (!a_queue) {
        return;
    }
    
    // Get stats before deletion
    uint64_t l_pushes, l_pops, l_full, l_empty;
    dap_ring_buffer_get_stats(a_queue->ring_buffer, &l_pushes, &l_pops, &l_full, &l_empty);
    
    log_it(L_INFO, "Deleting worker queue: pushes=%"PRIu64", pops=%"PRIu64", full=%"PRIu64", empty=%"PRIu64,
           l_pushes, l_pops, l_full, l_empty);
    
    if (l_full > 0) {
        log_it(L_WARNING, "Worker queue was full %"PRIu64" times - consider increasing capacity", l_full);
    }
    
    // Remove event socket from reactor and delete
    if (a_queue->event_socket) {
        dap_events_socket_remove_and_delete_unsafe(a_queue->event_socket, false);
        a_queue->event_socket = NULL;
    }
    
    // Delete ring buffer
    if (a_queue->ring_buffer) {
        dap_ring_buffer_delete(a_queue->ring_buffer);
        a_queue->ring_buffer = NULL;
    }
    
    DAP_DELETE(a_queue);
}

/**
 * @brief Push item to queue (thread-safe, lock-free)
 */
bool dap_worker_queue_push(dap_worker_queue_t *a_queue, void *a_item) {
    if (!a_queue || !a_item) {
        return false;
    }
    
    // Push to ring buffer (lock-free)
    if (!dap_ring_buffer_push(a_queue->ring_buffer, a_item)) {
        // Ring buffer full
        log_it(L_WARNING, "Worker queue full, dropping item (worker=%u)",
               a_queue->worker ? a_queue->worker->id : 0);
        return false;
    }
    
    // Signal eventfd to wake up reactor (increment counter by 1)
    uint64_t l_value = 1;
    ssize_t l_ret = write(a_queue->event_socket->fd, &l_value, sizeof(l_value));
    if (l_ret != sizeof(l_value)) {
        // Write failed, but item is already in ring buffer
        // Reactor will eventually process it on next wakeup
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_it(L_WARNING, "Failed to signal eventfd: %s", strerror(errno));
        }
    }
    
    return true;
}

/**
 * @brief Process all available items in queue (called by reactor)
 */
size_t dap_worker_queue_process(dap_worker_queue_t *a_queue) {
    if (!a_queue || !a_queue->callback) {
        return 0;
    }
    
    size_t l_processed = 0;
    void *l_item;
    
    // Process all available items (lock-free pop)
    while ((l_item = dap_ring_buffer_pop(a_queue->ring_buffer)) != NULL) {
        // Call user callback
        a_queue->callback(l_item);
        l_processed++;
        
        // Limit batch size to prevent starvation of other sockets
        if (l_processed >= 1024) {
            // Re-signal eventfd if more items remain
            if (!dap_ring_buffer_is_empty(a_queue->ring_buffer)) {
                uint64_t l_value = 1;
                write(a_queue->event_socket->fd, &l_value, sizeof(l_value));
            }
            break;
        }
    }
    
    return l_processed;
}

/**
 * @brief Get queue statistics
 */
void dap_worker_queue_get_stats(const dap_worker_queue_t *a_queue,
                                  size_t *a_size,
                                  uint64_t *a_total_pushes,
                                  uint64_t *a_total_pops,
                                  uint64_t *a_total_full) {
    if (!a_queue) {
        return;
    }
    
    if (a_size) {
        *a_size = dap_ring_buffer_size(a_queue->ring_buffer);
    }
    
    uint64_t l_empty;
    dap_ring_buffer_get_stats(a_queue->ring_buffer, 
                               a_total_pushes, a_total_pops, a_total_full, &l_empty);
}
