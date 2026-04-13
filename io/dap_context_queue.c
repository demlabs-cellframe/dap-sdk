/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net>
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

#include "dap_context_queue.h"
#include "dap_common.h"
#include "dap_events_socket.h"
#include "dap_context.h"
#include "dap_events.h"
#include <sys/time.h>

#define LOG_TAG "dap_context_queue"

static bool s_debug_more = false;
// Default ring buffer capacity (power of 2)
// INCREASED for high-throughput scenarios with cross-worker packet forwarding
#define DAP_CONTEXT_QUEUE_DEFAULT_CAPACITY 65536  // Was 16384, now 64K

/**
 * @brief Callback from reactor when event is signaled (items available in queue)
 */
static void s_event_read_callback(dap_events_socket_t *a_es, uint64_t a_value) {
    dap_context_queue_t *l_queue = (dap_context_queue_t *)a_es->_inheritor;
    
    if (!l_queue) {
        log_it(L_ERROR, "Event callback: NULL queue pointer in _inheritor");
        return;
    }
    
    size_t l_processed = dap_context_queue_process(l_queue);
    
    if (l_processed > 0) {
        debug_if(s_debug_more, L_DEBUG, "Context queue fd=%d: processed %zu items (eventfd_value=%"PRIu64")",
                 a_es->fd, l_processed, a_value);
    } else if (a_value > 0) {
        log_it(L_WARNING, "Context queue fd=%d: EMPTY wakeup (eventfd_value=%"PRIu64", rb_size=%zu)",
               a_es->fd, a_value,
               l_queue->ring_buffer ? dap_ring_buffer_size(l_queue->ring_buffer) : 0);
    }
}

/**
 * @brief Create context queue
 */
dap_context_queue_t *dap_context_queue_create(dap_context_t *a_context, size_t a_capacity, void (*a_callback)(void *)) {
    if (!a_context || !a_callback) {
        log_it(L_ERROR, "Context queue create: NULL context or callback");
        return NULL;
    }
    
    dap_context_queue_t *l_queue = DAP_NEW_Z(dap_context_queue_t);
    if (!l_queue) {
        log_it(L_CRITICAL, "Failed to allocate context queue");
        return NULL;
    }
    
    // Create ring buffer
    size_t l_capacity = a_capacity > 0 ? a_capacity : DAP_CONTEXT_QUEUE_DEFAULT_CAPACITY;
    l_queue->ring_buffer = dap_ring_buffer_create(l_capacity);
    if (!l_queue->ring_buffer) {
        log_it(L_ERROR, "Failed to create ring buffer for context queue");
        DAP_DELETE(l_queue);
        return NULL;
    }
    
    l_queue->callback = a_callback;
    l_queue->context = a_context;
    
    // Create cross-platform event socket for notifications
    // This will use eventfd on Linux, kqueue on BSD/macOS, IOCP on Windows
    l_queue->event_socket = dap_context_create_event(a_context, s_event_read_callback);
    if (!l_queue->event_socket) {
        log_it(L_ERROR, "Failed to create event socket");
        dap_ring_buffer_delete(l_queue->ring_buffer);
        DAP_DELETE(l_queue);
        return NULL;
    }
    
    // Store queue pointer in event socket's _inheritor for callback
    l_queue->event_socket->_inheritor = l_queue;
    
    // Add event socket to context's reactor (already done in dap_context_create_event for worker context)
    // Event socket is already added to context during creation
    
    debug_if(s_debug_more, L_DEBUG, "Created context queue: context=%p, capacity=%zu, event_fd=%"DAP_FORMAT_SOCKET,
             (void *)a_context, l_capacity, l_queue->event_socket->fd);
    
    return l_queue;
}

/**
 * @brief Delete context queue
 */
void dap_context_queue_delete(dap_context_queue_t *a_queue) {
    if (!a_queue) {
        return;
    }
    
    // Get stats before deletion (only if ring_buffer is valid)
    if (a_queue->ring_buffer) {
        uint64_t l_pushes, l_pops, l_full, l_empty;
        dap_ring_buffer_get_stats(a_queue->ring_buffer, &l_pushes, &l_pops, &l_full, &l_empty);
        
        debug_if(s_debug_more, L_DEBUG, "Deleting context queue %p: pushes=%"PRIu64", pops=%"PRIu64", full=%"PRIu64", empty=%"PRIu64,
                 (void *)a_queue, l_pushes, l_pops, l_full, l_empty);
        
        if (l_full > 0) {
            log_it(L_WARNING, "Context queue was full %"PRIu64" times - consider increasing capacity", l_full);
        }
        
        // Delete ring buffer
        dap_ring_buffer_delete(a_queue->ring_buffer);
        a_queue->ring_buffer = NULL;
    }
    
    // Remove event socket from reactor and delete
    // IMPORTANT: preserve_inheritor = true because _inheritor points to this queue itself!
    // We will free the queue at the end of this function with DAP_DELETE(a_queue)
    if (a_queue->event_socket) {
        dap_events_socket_remove_and_delete_unsafe(a_queue->event_socket, true);
        a_queue->event_socket = NULL;
    }
    
    DAP_DELETE(a_queue);
}

/**
 * @brief Push item to queue (thread-safe, lock-free)
 */
bool dap_context_queue_push(dap_context_queue_t *a_queue, void *a_item) {
    if (!a_queue || !a_item) {
        return false;
    }
    
    // Push to ring buffer (lock-free)
    if (!dap_ring_buffer_push(a_queue->ring_buffer, a_item)) {
        // Ring buffer full
        log_it(L_WARNING, "Context queue full, dropping item (context=%p)", a_queue->context);
        return false;
    }
    
    // Signal event socket to wake up reactor (cross-platform)
    int l_ret = dap_events_socket_event_signal(a_queue->event_socket, 1);
    if (l_ret != 0) {
        log_it(L_WARNING, "Failed to signal event socket fd=%d context=%p: error %d",
               a_queue->event_socket ? a_queue->event_socket->fd : -1, a_queue->context, l_ret);
    } else {
        debug_if(s_debug_more, L_DEBUG, "Queue push OK: signaled eventfd=%d context=%p",
               a_queue->event_socket ? a_queue->event_socket->fd : -1, a_queue->context);
    }
    
    return true;
}

#define DAP_CONTEXT_QUEUE_BATCH_SIZE 5

/**
 * @brief Process a batch of items from queue (called by reactor)
 *
 * Processes up to DAP_CONTEXT_QUEUE_BATCH_SIZE items per call to avoid
 * starving other reactor events under heavy load. Re-signals the eventfd
 * if items remain, so the reactor picks them up on the next iteration.
 *
 * @return Number of items processed, or -1 on error
 */
int dap_context_queue_process(dap_context_queue_t *a_queue) {
    if (!a_queue || !a_queue->callback) {
        return -1;
    }
    
    int l_count = 0;
    void *l_item;
    while (l_count < DAP_CONTEXT_QUEUE_BATCH_SIZE
           && (l_item = dap_ring_buffer_pop(a_queue->ring_buffer)) != NULL) {
        a_queue->callback(l_item);
        l_count++;
    }
    
    bool l_has_more = !dap_ring_buffer_is_empty(a_queue->ring_buffer);
    if (l_has_more) {
        dap_events_socket_event_signal(a_queue->event_socket, 1);
    }
    
    if (l_count == 0) {
        debug_if(g_debug_reactor, L_DEBUG, "Context queue: empty wakeup (spurious signal), fd=%"DAP_FORMAT_SOCKET,
                 a_queue->event_socket ? a_queue->event_socket->fd : -1);
    }
    
    return l_count;
}

/**
 * @brief Get queue statistics
 */
void dap_context_queue_get_stats(dap_context_queue_t *a_queue,
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
