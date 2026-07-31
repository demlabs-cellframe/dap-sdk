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
#include "dap_worker.h"
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

    /* Drain ALL items per eventfd wakeup — matches master's pipe2 behavior
     * where a single read() drains all available pointers.
     *
     * No re-signal: dap_context_queue_push already calls eventfd_write on
     * every push.  If the producer pushes more items after this drain,
     * eventfd_write re-arms the level-triggered wakeup naturally.
     *
     * Busy-loop protection: when send() in the EPOLLOUT handler returns
     * EAGAIN, level-triggered epoll does NOT re-notify EPOLLOUT (kernel
     * buffer is full).  The worker sleeps in epoll_wait until either:
     *   a) the peer reads → kernel buffer drains → EPOLLOUT fires → drain
     *   b) a new item is pushed → eventfd_write → EPOLLIN fires → drain
     * This is exactly how master's level-triggered epoll provides
     * backpressure without explicit flags or counters. */
    int l_processed = dap_context_queue_process(l_queue);

    if (l_processed > 0) {
        debug_if(s_debug_more, L_DEBUG, "Context queue fd=%d: processed %d items (eventfd_value=%"PRIu64")",
                 a_es->fd, l_processed, a_value);
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
        
        /* Drain remaining items to avoid dangling pointers in freed ring buffer memory */
        size_t l_drained = 0;
        void *l_item;
        while ((l_item = dap_ring_buffer_pop(a_queue->ring_buffer)) != NULL) {
            if (a_queue->callback)
                a_queue->callback(l_item);
            l_drained++;
        }
        if (l_drained > 0) {
            log_it(L_DEBUG, "Drained %zu remaining items from context queue %p on deletion", l_drained, (void *)a_queue);
        }
        
        // Delete ring buffer
        dap_ring_buffer_delete(a_queue->ring_buffer);
        a_queue->ring_buffer = NULL;
    }
    
    // Remove event socket from reactor and delete
    // IMPORTANT: preserve_inheritor = true because _inheritor points to this queue itself!
    // We will free the queue at the end of this function with DAP_DELETE(a_queue)
    if (a_queue->event_socket) {
        dap_worker_t *l_owner = a_queue->event_socket->worker;
        if (l_owner && dap_worker_get_current() == l_owner) {
            /* Called from the owning worker thread — safe to delete directly */
            dap_events_socket_remove_and_delete_unsafe(a_queue->event_socket, true);
        } else if (l_owner) {
            /* Called from a different thread — schedule deletion on the owner worker.
             * Clear _inheritor first: the _mt path deletes with preserve_inheritor=false,
             * but _inheritor points to this queue which we are about to free. */
            a_queue->event_socket->_inheritor = NULL;
            dap_events_socket_remove_and_delete_mt(l_owner, a_queue->event_socket->uuid);
        } else {
            /* No worker assigned — orphaned esocket, just delete the socket */
            dap_events_socket_delete_unsafe(a_queue->event_socket, true);
        }
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

bool dap_context_queue_push_quiet(dap_context_queue_t *a_queue, void *a_item) {
    if (!a_queue || !a_item)
        return false;
    return dap_ring_buffer_push(a_queue->ring_buffer, a_item);
}

void dap_context_queue_signal(dap_context_queue_t *a_queue) {
    if (a_queue && a_queue->event_socket)
        dap_events_socket_event_signal(a_queue->event_socket, 1);
}

/**
 * @brief Process items from queue (called by reactor)
 *
 * Drains up to DAP_CONTEXT_QUEUE_BATCH_SIZE items per eventfd wakeup.
 * This matches master's pipe2 behavior where read(fd, buf, PIPE_BUF)
 * reads up to 4096 bytes = 512 pointers in a single syscall.
 *
 * No re-signal: dap_context_queue_push calls eventfd_write on every
 * push, so new items naturally re-arm the level-triggered wakeup.
 * If items remain after a batch with no new pushes, they will be
 * processed on the next push's eventfd_write signal.
 *
 * @return Number of items processed, or -1 on error
 */
/* Maximum items per wakeup — matches master's PIPE_BUF / sizeof(void*) */
#define DAP_CONTEXT_QUEUE_BATCH_SIZE 512

int dap_context_queue_process(dap_context_queue_t *a_queue) {
    if (!a_queue || !a_queue->callback) {
        return -1;
    }

    int l_count = 0;
    void *l_item;
    while (l_count < DAP_CONTEXT_QUEUE_BATCH_SIZE &&
           (l_item = dap_ring_buffer_pop(a_queue->ring_buffer)) != NULL) {
        a_queue->callback(l_item);
        l_count++;
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
