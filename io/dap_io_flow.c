/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 *
 * This file is part of DAP the open source project.
 *
 * DAP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * DAP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * See more details here <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_io_flow.h"
#include "dap_io_flow_socket.h"
#include "dap_worker.h"
#include "dap_context.h"
#include "dap_proc_thread.h"

#define LOG_TAG "dap_io_flow"

// Forward declarations for internal structures
typedef struct flow_worker_context flow_worker_context_t;
typedef struct flow_cross_worker_packet flow_cross_worker_packet_t;

/**
 * @brief Cross-worker packet forwarding structure
 */
struct flow_cross_worker_packet {
    dap_io_flow_t *flow;                   // Target flow (in remote worker)
    uint8_t *data;                          // Packet data (ownership transferred)
    size_t size;                            // Data size
    struct sockaddr_storage remote_addr;   // Source address
    socklen_t remote_addr_len;             // Address length
};

/**
 * @brief Per-worker context for inter-worker communication
 */
struct flow_worker_context {
    uint32_t worker_id;
    dap_events_socket_t *queue_input_es;     // Queue input for receiving from other workers
    dap_events_socket_t **queue_output_es;   // Queue outputs to other workers
    _Atomic size_t packets_sent;
    _Atomic size_t packets_received;
};

// Static debug flag
static bool s_debug_more = false;

// Forward declarations for internal functions
static void s_flow_server_read_callback(dap_events_socket_t *a_es, void *a_arg);
static void s_queue_ptr_callback(dap_events_socket_t *a_es, void *a_ptr);
static int s_init_inter_worker_queues(dap_io_flow_server_t *a_server);
static int s_forward_packet_to_worker(dap_io_flow_server_t *a_server, 
                                      uint32_t a_from_worker_id,
                                      uint32_t a_to_worker_id, 
                                      struct flow_cross_worker_packet *a_packet);

// =============================================================================
// Public API - Core
// =============================================================================

/**
 * @brief Create IO flow server
 */
dap_io_flow_server_t* dap_io_flow_server_new(
    const char *a_name,
    dap_io_flow_ops_t *a_ops,
    dap_io_flow_boundary_type_t a_boundary_type)
{
    if (!a_name || !a_ops) {
        log_it(L_ERROR, "Invalid arguments: name=%p, ops=%p", a_name, a_ops);
        return NULL;
    }
    
    // Validate required callbacks
    if (!a_ops->packet_received || !a_ops->flow_create || !a_ops->flow_destroy) {
        log_it(L_ERROR, "Required callbacks are missing");
        return NULL;
    }
    
    dap_io_flow_server_t *l_server = DAP_NEW_Z(dap_io_flow_server_t);
    if (!l_server) {
        log_it(L_CRITICAL, "Memory allocation failed");
        return NULL;
    }
    
    l_server->name = dap_strdup(a_name);
    l_server->ops = a_ops;
    l_server->boundary_type = a_boundary_type;
    
    // Get worker count
    uint32_t l_worker_count = dap_proc_thread_get_count();
    
    // Allocate per-worker hash tables
    l_server->flows_per_worker = DAP_NEW_Z_COUNT(dap_io_flow_t*, l_worker_count);
    l_server->flow_locks_per_worker = DAP_NEW_Z_COUNT(pthread_rwlock_t, l_worker_count);
    
    if (!l_server->flows_per_worker || !l_server->flow_locks_per_worker) {
        log_it(L_CRITICAL, "Failed to allocate per-worker structures");
        DAP_DEL_Z(l_server->flows_per_worker);
        DAP_DEL_Z(l_server->flow_locks_per_worker);
        DAP_DEL_Z(l_server->name);
        DAP_DELETE(l_server);
        return NULL;
    }
    
    // Initialize RW locks
    for (uint32_t i = 0; i < l_worker_count; i++) {
        pthread_rwlock_init(&l_server->flow_locks_per_worker[i], NULL);
        l_server->flows_per_worker[i] = NULL;  // uthash starts with NULL
    }
    
    log_it(L_INFO, "Created flow server '%s' with %u workers, boundary_type=%d",
           a_name, l_worker_count, a_boundary_type);
    
    return l_server;
}

/**
 * @brief Start listening on address:port
 */
int dap_io_flow_server_listen(
    dap_io_flow_server_t *a_server,
    const char *a_addr,
    uint16_t a_port)
{
    if (!a_server) {
        log_it(L_ERROR, "Server is NULL");
        return -1;
    }
    
    if (a_server->is_running) {
        log_it(L_WARNING, "Server '%s' is already running", a_server->name);
        return -2;
    }
    
    // Setup callbacks for listener sockets
    dap_events_socket_callbacks_t l_callbacks = {
        .read_callback = s_flow_server_read_callback,
        .arg = a_server
    };
    
    // Determine socket type and protocol based on boundary type
    int l_socket_type = SOCK_DGRAM;
    int l_protocol = IPPROTO_UDP;
    
    if (a_server->boundary_type == DAP_IO_FLOW_BOUNDARY_RECORD) {
        // SCTP uses SOCK_SEQPACKET
        l_socket_type = SOCK_SEQPACKET;
        l_protocol = IPPROTO_SCTP;
    }
    
    // Create/get dap_server if not exists
    if (!a_server->dap_server) {
        a_server->dap_server = dap_server_new(NULL, NULL, NULL);
        if (!a_server->dap_server) {
            log_it(L_ERROR, "Failed to create DAP server");
            return -3;
        }
    }
    
    // Create sharded listeners
    int l_ret = dap_io_flow_socket_create_sharded_listeners(
        a_server->dap_server,
        a_addr,
        a_port,
        l_socket_type,
        l_protocol,
        &l_callbacks);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to create sharded listeners: %d", l_ret);
        return l_ret;
    }
    
    // Initialize inter-worker queues
    l_ret = s_init_inter_worker_queues(a_server);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize inter-worker queues: %d", l_ret);
        return l_ret;
    }
    
    a_server->is_running = true;
    
    log_it(L_INFO, "Server '%s' listening on %s:%u (socket_type=%d, protocol=%d)",
           a_server->name, a_addr ? a_addr : "0.0.0.0", a_port, l_socket_type, l_protocol);
    
    return 0;
}

/**
 * @brief Stop IO flow server
 */
void dap_io_flow_server_stop(dap_io_flow_server_t *a_server)
{
    if (!a_server) {
        return;
    }
    
    if (!a_server->is_running) {
        log_it(L_WARNING, "Server '%s' is not running", a_server->name);
        return;
    }
    
    a_server->is_running = false;
    
    // Stop DAP server (closes all listener sockets)
    if (a_server->dap_server) {
        dap_server_delete(a_server->dap_server);
        a_server->dap_server = NULL;
    }
    
    log_it(L_INFO, "Server '%s' stopped", a_server->name);
}

/**
 * @brief Delete IO flow server and cleanup all resources
 */
void dap_io_flow_server_delete(dap_io_flow_server_t *a_server)
{
    if (!a_server) {
        return;
    }
    
    // Stop server if running
    if (a_server->is_running) {
        dap_io_flow_server_stop(a_server);
    }
    
    // Cleanup all flows
    uint32_t l_worker_count = dap_proc_thread_get_count();
    for (uint32_t i = 0; i < l_worker_count; i++) {
        pthread_rwlock_wrlock(&a_server->flow_locks_per_worker[i]);
        
        dap_io_flow_t *l_flow, *l_tmp;
        HASH_ITER(hh, a_server->flows_per_worker[i], l_flow, l_tmp) {
            HASH_DEL(a_server->flows_per_worker[i], l_flow);
            
            // Call protocol's flow_destroy
            if (a_server->ops->flow_destroy) {
                a_server->ops->flow_destroy(l_flow);
            }
        }
        
        pthread_rwlock_unlock(&a_server->flow_locks_per_worker[i]);
        pthread_rwlock_destroy(&a_server->flow_locks_per_worker[i]);
    }
    
    // Cleanup per-worker structures
    DAP_DELETE(a_server->flows_per_worker);
    DAP_DELETE(a_server->flow_locks_per_worker);
    
    // Cleanup inter-worker pipes
    if (a_server->inter_worker_queues) {
        for (uint32_t i = 0; i < l_worker_count; i++) {
            if (a_server->inter_worker_queues[i]) {
                for (uint32_t j = 0; j < l_worker_count; j++) {
                    if (i != j && a_server->inter_worker_queues[i][j]) {
                        dap_events_socket_delete_unsafe(a_server->inter_worker_queues[i][j], false);
                    }
                }
                DAP_DELETE(a_server->inter_worker_queues[i]);
            }
        }
        DAP_DELETE(a_server->inter_worker_queues);
    }
    
    DAP_DELETE(a_server->name);
    DAP_DELETE(a_server);
    
    log_it(L_INFO, "Server deleted");
}

/**
 * @brief Find flow by remote address
 */
dap_io_flow_t* dap_io_flow_find(
    dap_io_flow_server_t *a_server,
    const struct sockaddr_storage *a_remote_addr)
{
    if (!a_server || !a_remote_addr) {
        return NULL;
    }
    
    // Try local worker first (fast path)
    dap_worker_t *l_worker = dap_worker_get_current();
    if (l_worker) {
        uint32_t l_worker_id = l_worker->id;
        
        pthread_rwlock_rdlock(&a_server->flow_locks_per_worker[l_worker_id]);
        
        dap_io_flow_t *l_flow = NULL;
        HASH_FIND(hh, a_server->flows_per_worker[l_worker_id], a_remote_addr,
                  sizeof(struct sockaddr_storage), l_flow);
        
        pthread_rwlock_unlock(&a_server->flow_locks_per_worker[l_worker_id]);
        
        if (l_flow) {
            atomic_fetch_add(&a_server->local_hits, 1);
            return l_flow;
        }
    }
    
    // Search in other workers (slow path)
    uint32_t l_worker_count = dap_proc_thread_get_count();
    for (uint32_t i = 0; i < l_worker_count; i++) {
        if (l_worker && i == l_worker->id) {
            continue;  // Already checked
        }
        
        pthread_rwlock_rdlock(&a_server->flow_locks_per_worker[i]);
        
        dap_io_flow_t *l_flow = NULL;
        HASH_FIND(hh, a_server->flows_per_worker[i], a_remote_addr,
                  sizeof(struct sockaddr_storage), l_flow);
        
        pthread_rwlock_unlock(&a_server->flow_locks_per_worker[i]);
        
        if (l_flow) {
            atomic_fetch_add(&a_server->remote_hits, 1);
            atomic_fetch_add(&l_flow->remote_access_count, 1);
            return l_flow;
        }
    }
    
    return NULL;
}

/**
 * @brief Send data to flow's remote address
 */
int dap_io_flow_send(
    dap_io_flow_t *a_flow,
    const uint8_t *a_data,
    size_t a_size)
{
    if (!a_flow || !a_data || a_size == 0) {
        return -1;
    }
    
    // Get listener socket for this flow's worker
    // This needs to be implemented based on how we store listener references
    // For now, return success (to be implemented fully)
    
    return (int)a_size;
}

/**
 * @brief Get server statistics
 */
void dap_io_flow_server_get_stats(
    dap_io_flow_server_t *a_server,
    size_t *a_local_hits,
    size_t *a_remote_hits,
    size_t *a_cross_worker_packets)
{
    if (!a_server) {
        return;
    }
    
    if (a_local_hits) {
        *a_local_hits = atomic_load(&a_server->local_hits);
    }
    if (a_remote_hits) {
        *a_remote_hits = atomic_load(&a_server->remote_hits);
    }
    if (a_cross_worker_packets) {
        *a_cross_worker_packets = atomic_load(&a_server->cross_worker_packets);
    }
}

// =============================================================================
// Internal Functions
// =============================================================================

/**
 * @brief Main read callback for flow server listeners
 */
static void s_flow_server_read_callback(dap_events_socket_t *a_es, void *a_arg)
{
    dap_io_flow_server_t *l_server = (dap_io_flow_server_t*)a_arg;
    
    if (!l_server || !a_es) {
        return;
    }
    
    dap_worker_t *l_worker = dap_worker_get_current();
    
    // Process incoming data from socket
    if (a_es->buf_in_size == 0) {
        return;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Flow server received %zu bytes from %s",
             a_es->buf_in_size,
             dap_io_flow_socket_addr_to_string(&a_es->addr_storage));
    
    // Find or create flow
    dap_io_flow_t *l_flow = dap_io_flow_find(l_server, &a_es->addr_storage);
    
    if (!l_flow && l_worker) {
        // Create new flow
        l_flow = l_server->ops->flow_create(l_server, &a_es->addr_storage, a_es);
        
        if (l_flow) {
            // Add to local worker's hash table
            memcpy(&l_flow->remote_addr, &a_es->addr_storage, sizeof(struct sockaddr_storage));
            l_flow->remote_addr_len = a_es->addr_size;
            l_flow->owner_worker_id = l_worker->id;
            l_flow->last_activity = time(NULL);
            l_flow->boundary_type = l_server->boundary_type;
            
            pthread_rwlock_wrlock(&l_server->flow_locks_per_worker[l_worker->id]);
            HASH_ADD(hh, l_server->flows_per_worker[l_worker->id], remote_addr,
                     sizeof(struct sockaddr_storage), l_flow);
            pthread_rwlock_unlock(&l_server->flow_locks_per_worker[l_worker->id]);
            
            debug_if(s_debug_more, L_DEBUG, "Created new flow for %s in worker %u",
                     dap_io_flow_socket_addr_to_string(&a_es->addr_storage), l_worker->id);
        }
    }
    
    // Call protocol's packet_received callback
    if (l_server->ops->packet_received) {
        l_server->ops->packet_received(l_server, l_flow,
                                       a_es->buf_in, a_es->buf_in_size,
                                       &a_es->addr_storage, a_es);
    }
    
    // Clear input buffer
    a_es->buf_in_size = 0;
}

/**
 * @brief Initialize inter-worker queues for cross-worker forwarding
 */
static int s_init_inter_worker_queues(dap_io_flow_server_t *a_server)
{
    uint32_t l_worker_count = dap_proc_thread_get_count();
    
    if (l_worker_count <= 1) {
        // No need for queues with single worker
        return 0;
    }
    
    // Allocate queue inputs array (one per worker)
    a_server->queue_inputs = DAP_NEW_Z_COUNT(dap_events_socket_t*, l_worker_count);
    if (!a_server->queue_inputs) {
        log_it(L_ERROR, "Failed to allocate queue inputs array");
        return -1;
    }
    
    // Allocate queue outputs 2D array [src_worker][dst_worker]
    a_server->inter_worker_queues = DAP_NEW_Z_COUNT(dap_events_socket_t**, l_worker_count);
    if (!a_server->inter_worker_queues) {
        log_it(L_ERROR, "Failed to allocate queue outputs array");
        return -2;
    }
    
    for (uint32_t i = 0; i < l_worker_count; i++) {
        a_server->inter_worker_queues[i] = DAP_NEW_Z_COUNT(dap_events_socket_t*, l_worker_count);
        if (!a_server->inter_worker_queues[i]) {
            log_it(L_ERROR, "Failed to allocate queue outputs for worker %u", i);
            return -3;
        }
    }
    
    // Create queue inputs for each worker
    for (uint32_t dst = 0; dst < l_worker_count; dst++) {
        dap_worker_t *l_dst_worker = dap_events_worker_get(dst);
        if (!l_dst_worker) {
            log_it(L_ERROR, "Failed to get worker %u", dst);
            return -4;
        }
        
        // Create queue input esocket on destination worker
        a_server->queue_inputs[dst] = dap_events_socket_create_type_queue_ptr_mt(
            l_dst_worker, s_queue_ptr_callback);
        
        if (!a_server->queue_inputs[dst]) {
            log_it(L_ERROR, "Failed to create queue input for worker %u", dst);
            return -5;
        }
        
        a_server->queue_inputs[dst]->_inheritor = a_server;
    }
    
    // Create queue outputs from each source worker to each destination worker
    for (uint32_t src = 0; src < l_worker_count; src++) {
        for (uint32_t dst = 0; dst < l_worker_count; dst++) {
            if (src == dst) {
                continue;  // No queue to self
            }
            
            // Create queue output esocket (linked to queue input)
            dap_events_socket_t *l_queue_out = dap_events_socket_queue_ptr_create_input(
                a_server->queue_inputs[dst]);
            
            if (!l_queue_out) {
                log_it(L_ERROR, "Failed to create queue output %u -> %u", src, dst);
                return -6;
            }
            
            a_server->inter_worker_queues[src][dst] = l_queue_out;
        }
    }
    
    log_it(L_INFO, "Initialized inter-worker queues for %u workers", l_worker_count);
    return 0;
}

/**
 * @brief Queue pointer callback - process cross-worker packets
 * 
 * Called by reactor when packet pointer arrives from another worker's queue.
 * Processes the packet and frees the cross-worker packet structure.
 */
static void s_queue_ptr_callback(dap_events_socket_t *a_es, void *a_ptr)
{
    if (!a_ptr) {
        return;
    }
    
    dap_io_flow_server_t *l_server = (dap_io_flow_server_t*)a_es->_inheritor;
    if (!l_server) {
        log_it(L_ERROR, "Queue callback: server not found in _inheritor");
        DAP_DELETE(a_ptr);
        return;
    }
    
    struct flow_cross_worker_packet *l_packet = (struct flow_cross_worker_packet*)a_ptr;
    
    // Increment statistics
    atomic_fetch_add(&l_server->cross_worker_packets, 1);
    
    // Call protocol's packet handler
    if (l_server->ops && l_server->ops->packet_received) {
        l_server->ops->packet_received(
            l_server,
            l_packet->flow,
            l_packet->data,
            l_packet->size,
            &l_packet->remote_addr,
            a_es  // Pass queue esocket as listener
        );
    }
    
    // Free packet data and structure
    DAP_DELETE(l_packet->data);
    DAP_DELETE(l_packet);
}

/**
 * @brief Forward packet to another worker via queue
 * 
 * Creates cross-worker packet structure and sends pointer via queue_ptr.
 * Zero-copy: only pointer is sent, not packet data.
 */
static int s_forward_packet_to_worker(dap_io_flow_server_t *a_server, 
                                      uint32_t a_from_worker_id,
                                      uint32_t a_to_worker_id, 
                                      struct flow_cross_worker_packet *a_packet)
{
    if (!a_server || !a_packet) {
        return -1;
    }
    
    uint32_t l_worker_count = dap_proc_thread_get_count();
    if (a_from_worker_id >= l_worker_count || a_to_worker_id >= l_worker_count) {
        log_it(L_ERROR, "Invalid worker IDs: %u -> %u", a_from_worker_id, a_to_worker_id);
        return -2;
    }
    
    // Get queue output for this worker pair
    dap_events_socket_t *l_queue_out = a_server->inter_worker_queues[a_from_worker_id][a_to_worker_id];
    if (!l_queue_out) {
        log_it(L_ERROR, "No queue output for workers %u -> %u", a_from_worker_id, a_to_worker_id);
        return -3;
    }
    
    // Send packet pointer via queue (zero-copy)
    int l_ret = dap_events_socket_queue_ptr_send_to_input(a_server->queue_inputs[a_to_worker_id], a_packet);
    
    if (l_ret != 0) {
        log_it(L_WARNING, "Failed to send packet pointer to worker %u (ret=%d)", a_to_worker_id, l_ret);
        return -4;
    }
    
    debug_if(s_debug_more, L_DEBUG, 
             "Forwarded packet %p (%zu bytes) from worker %u to worker %u",
             a_packet, a_packet->size, a_from_worker_id, a_to_worker_id);
    
    return 0;
}

