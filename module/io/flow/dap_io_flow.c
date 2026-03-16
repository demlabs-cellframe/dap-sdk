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
#include "dap_config.h"
#include "dap_strfuncs.h"
#include "dap_hash.h"
#include "dap_time.h"
#include "dap_io_flow.h"
#include "dap_io_flow_socket.h"
#include "dap_worker.h"
#include "dap_context.h"
#include "dap_context_queue.h"
#include "dap_io_flow_compat.h"
#include "dap_proc_thread.h"
#include "dap_arena.h"

#define LOG_TAG "dap_io_flow"

// Debug mode
static bool s_debug_more = false;

// Thread-local arena for cross-worker packet allocations (REFCOUNTED mode)
// Each worker has its own arena with per-page refcounting
// Pages are freed when all references are released (thread-safe)
static __thread dap_arena_t *tl_cross_worker_arena = NULL;

// Forward declarations for internal structures
typedef struct flow_cross_worker_packet flow_cross_worker_packet_t;

/**
 * @brief Cross-worker packet forwarding structure
 * 
 * Includes page handle for refcounted arena management.
 */
struct flow_cross_worker_packet {
    dap_io_flow_server_t *server;          // Target server (always valid)
    dap_io_flow_t *flow;                   // Target flow (NULL for new flows)
    uint8_t *data;                          // Packet data (arena-allocated)
    size_t size;                            // Data size
    struct sockaddr_storage remote_addr;   // Source address
    socklen_t remote_addr_len;             // Address length
    void *page_handle;                      // Arena page handle for refcounting
};

/**
 * @brief Queue deletion callback arguments
 * 
 * Passed to worker thread for safe queue deletion from reactor context.
 */
typedef struct {
    dap_io_flow_server_t *server;
    dap_context_queue_t *queue;
    uint32_t worker_id;
} queue_delete_args_t;


// Forward declarations for internal functions
static void s_flow_server_read_callback(dap_events_socket_t *a_es, void *a_arg);
static void s_queue_ptr_callback(void *a_ptr);
static int s_init_inter_worker_queues(dap_io_flow_server_t *a_server);
static int s_forward_packet_to_worker(dap_io_flow_server_t *a_server, 
                                      uint32_t a_from_worker_id,
                                      uint32_t a_to_worker_id, 
                                      struct flow_cross_worker_packet *a_packet);
static void s_process_forwarded_packet(dap_io_flow_server_t *server,
                                       dap_io_flow_t *flow,
                                       const uint8_t *data,
                                       size_t size,
                                       const struct sockaddr_storage *remote_addr,
                                       dap_events_socket_t *listener_es);
static void s_process_flow_packet_common(dap_io_flow_server_t *a_server,
                                         const uint8_t *a_data,
                                         size_t a_data_size,
                                         const struct sockaddr_storage *a_remote_addr,
                                         socklen_t a_remote_addr_len,
                                         dap_events_socket_t *a_listener_es);

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
    // Initialize debug mode from config (once)
    static bool s_debug_initialized = false;
    if (!s_debug_initialized && g_config) {
        s_debug_more = dap_config_get_item_bool_default(g_config, "io_flow", "debug_more", false);
        s_debug_initialized = true;
        if (s_debug_more) {
            debug_if(s_debug_more, L_DEBUG, "IO Flow debug mode ENABLED");
        }
    }
    
    debug_if(s_debug_more, L_DEBUG, "dap_io_flow_server_new: entry, name=%s", a_name ? a_name : "NULL");
    
    if (!a_name || !a_ops) {
        log_it(L_ERROR, "Invalid arguments: name=%p, ops=%p", a_name, a_ops);
        return NULL;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Allocating dap_io_flow_server_t");
    
    dap_io_flow_server_t *l_server = DAP_NEW_Z(dap_io_flow_server_t);
    if (!l_server) {
        log_it(L_CRITICAL, "Memory allocation failed");
        return NULL;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Allocated server at %p", (void*)l_server);
    
    l_server->name = dap_strdup(a_name);
    l_server->ops = a_ops;
    l_server->boundary_type = a_boundary_type;
    l_server->_inheritor = NULL;  // Initialize inheritor field
    
    debug_if(s_debug_more, L_DEBUG, "Getting worker count");
    
    // Get worker count
    uint32_t l_worker_count = dap_proc_thread_get_count();
    
    debug_if(s_debug_more, L_DEBUG, "Worker count: %u", l_worker_count);
    
    if (l_worker_count == 0) {
        log_it(L_CRITICAL, "Worker count is 0 - proc threads not initialized");
        DAP_DEL_Z(l_server->name);
        DAP_DELETE(l_server);
        return NULL;
    }
    
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
    
    debug_if(s_debug_more, L_DEBUG, "Initializing RW locks for %u workers", l_worker_count);
    
    // Initialize RW locks
    for (uint32_t i = 0; i < l_worker_count; i++) {
        pthread_rwlock_init(&l_server->flow_locks_per_worker[i], NULL);
        l_server->flows_per_worker[i] = NULL;  // dap_ht starts with NULL
    }
    
    // Initialize cleanup synchronization
    pthread_mutex_init(&l_server->cleanup_mutex, NULL);
    pthread_cond_init(&l_server->cleanup_cond, NULL);
    atomic_init(&l_server->pending_cleanups, 0);
    atomic_init(&l_server->active_callbacks, 0);  // Track callbacks in execution
    atomic_init(&l_server->is_deleting, false);   // Server is valid initially
    
    // Initialize cross-worker packet drain coordination (for natural drain during cleanup)
    pthread_mutex_init(&l_server->cross_worker_mutex, NULL);
    pthread_cond_init(&l_server->cross_worker_cond, NULL);
    // cross_worker_packets initialized by calloc to 0
    
    log_it(L_INFO, "Created flow server '%s' with %u workers, boundary_type=%d",
           a_name, l_worker_count, a_boundary_type);
    
    debug_if(s_debug_more, L_DEBUG, "dap_io_flow_server_new: success, returning %p", (void*)l_server);
    
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
    
    // Create sharded listeners (or single socket if eBPF unavailable)
    // dap_io_flow_socket_create_sharded_listeners detects best LB tier
    int l_ret = dap_io_flow_socket_create_sharded_listeners(
        a_server->dap_server,
        a_addr,
        a_port,
        l_socket_type,
        l_protocol,
        &l_callbacks,
        &a_server->lb_tier);  // Save detected tier
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to create sharded listeners: %d", l_ret);
        return l_ret;
    }
    
    // Initialize inter-worker queues ONLY for Application-level LB (Tier 1)
    // Kernel-level tiers (BPF, BSD LB, etc.) distribute packets directly - no forwarding needed!
    // macOS GCD and Windows RIO also use Application tier internally but with platform optimizations
    bool needs_queues = (a_server->lb_tier == DAP_IO_FLOW_LB_TIER_APPLICATION
#ifdef DAP_IO_FLOW_LB_TIER_DARWIN_GCD
                         || a_server->lb_tier == DAP_IO_FLOW_LB_TIER_DARWIN_GCD
#endif
#ifdef DAP_IO_FLOW_LB_TIER_WIN_RIO
                         || a_server->lb_tier == DAP_IO_FLOW_LB_TIER_WIN_RIO
#endif
                        );
    
    if (needs_queues) {
        l_ret = s_init_inter_worker_queues(a_server);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to initialize inter-worker queues: %d", l_ret);
            return l_ret;
        }
        log_it(L_NOTICE, "Inter-worker queues initialized for application-level distribution");
    } else {
        log_it(L_NOTICE, "Skipping inter-worker queues (kernel-level tier handles distribution)");
    }
    
    a_server->is_running = true;
    
    log_it(L_INFO, "Server '%s' listening on %s:%u (socket_type=%d, protocol=%d)",
           a_server->name, a_addr ? a_addr : "0.0.0.0", a_port, l_socket_type, l_protocol);
    
    return 0;
}

/**
 * @brief Stop IO flow server synchronously
 * 
 * Marks server as stopped - no new packets should be enqueued.
 * Listener sockets remain active until dap_io_flow_server_delete().
 * This allows graceful queue drainage without accepting new connections.
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
    log_it(L_INFO, "Server '%s' marked as stopped (listeners will close on delete)", a_server->name);
}

/**
 * @brief Worker-thread callback to safely delete dap_server
 * 
 * CRITICAL: dap_server contains listener sockets registered in worker reactors!
 * Must be deleted from a worker thread with proper reactor access.
 * 
 * We pick worker 0 as it's guaranteed to exist and handle the deletion there.
 * 
 * @param a_arg dap_io_flow_server_t* with server to delete
 */
/**
 * @brief Delete IO flow server and cleanup all resources
 * 
 * Intelligent cleanup with natural drain:
 * 1. Mark is_deleting=true to stop new packet intake
 * 2. Cleanup flows (protocol data)
 * 3. Free inter_worker_queues reference arrays
 * 4. Wait for natural drain of cross_worker_packets (with pruning)
 * 5. Delete dap_server (listeners)
 * 6. Free structures (queues NOT deleted - belong to workers)
 */
void dap_io_flow_server_delete(dap_io_flow_server_t *a_server)
{
    if (!a_server) {
        return;
    }
    
    // Double-delete protection
    static _Atomic(uintptr_t) s_deleting_server = 0;
    uintptr_t l_server_addr = (uintptr_t)a_server;
    uintptr_t l_expected = 0;
    
    if (!atomic_compare_exchange_strong(&s_deleting_server, &l_expected, l_server_addr)) {
        if (s_deleting_server == l_server_addr) {
            log_it(L_WARNING, "Double-delete detected for server %p - ignoring", a_server);
            return;
        }
    }
    
    // CRITICAL: Mark server as deleting BEFORE stopping
    // This invalidates all queued packets that reference this server
    atomic_store(&a_server->is_deleting, true);
    log_it(L_INFO, "=== Server '%s' deletion START (marked is_deleting=true) ===", 
           a_server->name ? a_server->name : "unknown");
    log_it(L_INFO, "Server '%s' marked for deletion - draining queues", a_server->name);
    
    // Mark server as stopped (if not already)
    if (a_server->is_running) {
        log_it(L_DEBUG, "Marking server as stopped to prevent new flows");
        dap_io_flow_server_stop(a_server);
    }
    
    uint32_t l_worker_count = dap_proc_thread_get_count();
    
    // Step 1: Cleanup all flows (user data)
    debug_if(s_debug_more, L_DEBUG, "Cleaning up flows for %u workers", l_worker_count);
    for (uint32_t i = 0; i < l_worker_count; i++) {
        pthread_rwlock_wrlock(&a_server->flow_locks_per_worker[i]);
        
        dap_io_flow_t *l_flow, *l_tmp;
        dap_ht_foreach(a_server->flows_per_worker[i], l_flow, l_tmp) {
            dap_ht_del(a_server->flows_per_worker[i], l_flow);
            
            // Call protocol's flow_destroy
            if (a_server->ops->flow_destroy) {
                a_server->ops->flow_destroy(l_flow);
            }
        }
        
        pthread_rwlock_unlock(&a_server->flow_locks_per_worker[i]);
    }
    debug_if(s_debug_more, L_DEBUG, "Flows cleanup complete");
    
    // Step 2: Intelligent drain - wait for natural completion of all queued operations
    // Must complete BEFORE deleting queues to avoid use-after-free
    debug_if(s_debug_more, L_INFO, "Waiting for natural drain of cross-worker packets...");
    
    uint64_t l_drain_start = dap_nanotime_now();
    uint64_t l_max_drain_time_ns = 5 * 1000000000ULL;  // 5 seconds max (reduced for faster cleanup)
    uint32_t l_last_count = atomic_load(&a_server->cross_worker_packets);
    uint32_t l_no_progress_cycles = 0;
    const uint32_t MAX_NO_PROGRESS = 20;  // 2 seconds of no progress
    
    while (atomic_load(&a_server->cross_worker_packets) > 0) {
        uint32_t l_current = atomic_load(&a_server->cross_worker_packets);
        
        // Check progress
        if (l_current < l_last_count) {
            l_no_progress_cycles = 0;  // Progress made
            debug_if(s_debug_more, L_DEBUG, "Drain progress: %u -> %u packets", 
                     l_last_count, l_current);
        } else {
            l_no_progress_cycles++;
            if (l_no_progress_cycles >= MAX_NO_PROGRESS) {
                log_it(L_WARNING, "Drain stuck at %u packets for %u cycles - continuing",
                       l_current, l_no_progress_cycles);
                break;
            }
        }
        l_last_count = l_current;
        
        // Timeout check
        uint64_t l_elapsed_ns = dap_nanotime_now() - l_drain_start;
        if (l_elapsed_ns > l_max_drain_time_ns) {
            log_it(L_WARNING, "Drain timeout after %lu ms, %u packets remaining",
                   l_elapsed_ns / 1000000, l_current);
            break;
        }
        
        usleep(100000);  // 100ms between checks
    }
    
    uint64_t l_drain_time_ms = (dap_nanotime_now() - l_drain_start) / 1000000;
    uint32_t l_final_count = atomic_load(&a_server->cross_worker_packets);
    if (l_final_count == 0) {
        log_it(L_INFO, "Natural drain complete in %lu ms", l_drain_time_ms);
    } else {
        log_it(L_WARNING, "Drain finished with %u packets remaining after %lu ms", 
               l_final_count, l_drain_time_ms);
    }
    
    // Step 3: Delete inter_worker_queues references (just pointer arrays, not actual queues)
    debug_if(s_debug_more, L_DEBUG, "Freeing inter-worker queue reference arrays");
    if (a_server->inter_worker_queues) {
        for (uint32_t i = 0; i < l_worker_count; i++) {
            DAP_DELETE(a_server->inter_worker_queues[i]);
        }
        DAP_DELETE(a_server->inter_worker_queues);
        a_server->inter_worker_queues = NULL;
    }
    
    // Step 4: Delete queue_inputs synchronously on each worker thread
    // Each queue was created on a specific worker's context and must be deleted
    // from that worker's thread to avoid race conditions
    debug_if(s_debug_more, L_DEBUG, "Deleting queue_inputs on worker threads");
    if (a_server->queue_inputs) {
        for (uint32_t i = 0; i < l_worker_count; i++) {
            if (a_server->queue_inputs[i]) {
                dap_worker_t *l_worker = dap_events_worker_get(i);
                if (l_worker) {
                    // Use sync callback to delete queue on owning worker
                    dap_worker_exec_callback_on_sync(l_worker, 
                        (dap_worker_callback_t)dap_context_queue_delete, 
                        a_server->queue_inputs[i]);
                }
                a_server->queue_inputs[i] = NULL;
            }
        }
        DAP_DELETE(a_server->queue_inputs);
        a_server->queue_inputs = NULL;
    }
    
    // Step 5: Free structures
    debug_if(s_debug_more, L_DEBUG, "Destroying %u flow locks", l_worker_count);
    for (uint32_t i = 0; i < l_worker_count; i++) {
        pthread_rwlock_destroy(&a_server->flow_locks_per_worker[i]);
    }
    
    DAP_DELETE(a_server->flows_per_worker);
    DAP_DELETE(a_server->flow_locks_per_worker);
    
    pthread_mutex_destroy(&a_server->cleanup_mutex);
    pthread_cond_destroy(&a_server->cleanup_cond);
    
    // Destroy cross-worker packet drain synchronization
    pthread_mutex_destroy(&a_server->cross_worker_mutex);
    pthread_cond_destroy(&a_server->cross_worker_cond);
    
    // Step 6: Stop and delete dap_server (listeners)
    // Use async delete and wait for completion
    debug_if(s_debug_more, L_DEBUG, "Deleting dap_server (listeners)");
    if (a_server->dap_server) {
        dap_server_delete(a_server->dap_server);
        a_server->dap_server = NULL;
        // Give workers time to process delete requests
        usleep(100000);  // 100ms
    }
    
    // Step 7: Final cleanup
    char *l_name_copy = a_server->name ? dap_strdup(a_server->name) : NULL;
    
    log_it(L_DEBUG, "Freeing server name and object");
    DAP_DELETE(a_server->name);
    
    // Mark server as freed (helps detect use-after-free)
    memset(a_server, 0xDE, sizeof(*a_server)); // Fill with 0xDE (dead) pattern
    DAP_DELETE(a_server);
    
    log_it(L_INFO, "Server '%s' deleted", l_name_copy ? l_name_copy : "unknown");
    DAP_DELETE(l_name_copy);
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
        dap_ht_find(a_server->flows_per_worker[l_worker_id], a_remote_addr,
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
        dap_ht_find(a_server->flows_per_worker[i], a_remote_addr,
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
 * @brief Delete all flows for this server
 * 
 * Critical for preventing use-after-free when server resources (e.g., listener sockets)
 * are deleted while flows still hold pointers to them.
 * 
 * Iterates all worker hash tables and calls flow_destroy callback for each flow.
 * 
 * @param a_server Server instance
 * @return Number of flows deleted, or -1 on error
 */
int dap_io_flow_delete_all_flows(dap_io_flow_server_t *a_server)
{
    if (!a_server) {
        log_it(L_ERROR, "dap_io_flow_delete_all_flows: NULL server");
        return -1;
    }
    
    log_it(L_INFO, "=== dap_io_flow_delete_all_flows START for server '%s' ===",
           a_server->name ? a_server->name : "NULL");
    
    if (!a_server->ops || !a_server->ops->flow_destroy) {
        log_it(L_WARNING, "dap_io_flow_delete_all_flows: No flow_destroy callback, skipping");
        return 0;
    }
    
    uint32_t l_worker_count = dap_proc_thread_get_count();
    int l_total_deleted = 0;
    
    debug_if(s_debug_more, L_DEBUG, 
             "dap_io_flow_delete_all_flows: Deleting flows for server '%s' across %u workers",
             a_server->name ? a_server->name : "NULL", l_worker_count);
    
    // Iterate all workers and delete their flows
    for (uint32_t i = 0; i < l_worker_count; i++) {
        pthread_rwlock_wrlock(&a_server->flow_locks_per_worker[i]);
        
        dap_io_flow_t *l_flow = NULL;
        dap_io_flow_t *l_tmp = NULL;
        
        // Use dap_ht_foreach for safe iteration with deletion
        dap_ht_foreach(a_server->flows_per_worker[i], l_flow, l_tmp) {
            // Remove from hash table BEFORE destroying (flow_destroy may free memory)
            dap_ht_del(a_server->flows_per_worker[i], l_flow);
            
            debug_if(s_debug_more, L_DEBUG,
                     "Deleting flow on worker %u: remote=%s",
                     i, dap_io_flow_socket_addr_to_string(&l_flow->remote_addr));
            
            // Call protocol-specific destructor
            a_server->ops->flow_destroy(l_flow);
            
            l_total_deleted++;
        }
        
        pthread_rwlock_unlock(&a_server->flow_locks_per_worker[i]);
    }
    
    log_it(L_INFO, "dap_io_flow_delete_all_flows: Deleted %d flows for server '%s'",
           l_total_deleted, a_server->name ? a_server->name : "NULL");
    
    return l_total_deleted;
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
 * @brief Common logic for processing incoming flow packets
 * 
 * Shared between UDP listener callback and queue callback.
 * Finds or creates flow, calls protocol handler.
 * 
 * @param a_server Flow server
 * @param a_data Packet data
 * @param a_data_size Data size
 * @param a_remote_addr Remote address
 * @param a_listener_es REAL UDP listener socket (not queue!)
 */
/**
 * @brief Get or create thread-local refcounted arena for cross-worker packets
 * 
 * Each worker thread has its own arena with per-page refcounting enabled.
 * Pages are freed when all references are released (thread-safe).
 * Arena is created once per worker and reused for all cross-worker operations.
 * 
 * @return Arena pointer, or NULL on critical allocation failure
 */
static dap_arena_t* s_get_cross_worker_arena(void)
{
    if (!tl_cross_worker_arena) {
        // Create arena with per-page refcounting enabled
        tl_cross_worker_arena = dap_arena_new_opt((dap_arena_opt_t){
            .initial_size = 8192,        // 8KB initial, grows as needed
            .use_refcount = true,        // Enable per-page refcounting
            .thread_local = true         // Thread-local (no locks within worker)
        });
        if (!tl_cross_worker_arena) {
            log_it(L_CRITICAL, "Failed to create refcounted cross-worker arena");
            return NULL;
        }
        log_it(L_DEBUG, "Created refcounted cross-worker arena for worker thread");
    }
    return tl_cross_worker_arena;
}

// =============================================================================
// Cross-Worker Forwarding
// =============================================================================

/**
 * @brief Wrapper for cross-worker forwarded packets
 * 
 * Called from queue callback when packet is forwarded from another worker.
 * Re-processes the packet on the target worker.
 */
static void s_process_forwarded_packet(
    dap_io_flow_server_t *a_server,
    dap_io_flow_t *a_flow,  // May be NULL for new flows
    const uint8_t *a_data,
    size_t a_data_size,
    const struct sockaddr_storage *a_remote_addr,
    dap_events_socket_t *a_listener)
{
    debug_if(s_debug_more, L_DEBUG,
             "Processing forwarded packet: server=%p, flow=%p, size=%zu, listener_fd=%d",
             a_server, a_flow, a_data_size, a_listener ? a_listener->fd : -1);
    
    // Process the packet using common logic
    socklen_t l_addr_len = (a_remote_addr->ss_family == AF_INET) 
        ? sizeof(struct sockaddr_in) 
        : sizeof(struct sockaddr_in6);
    
    s_process_flow_packet_common(a_server, a_data, a_data_size,
                                  a_remote_addr, l_addr_len, a_listener);
}

/**
 * @brief Process incoming packet for flow
 * 
 * Finds or creates flow, handles cross-worker forwarding if needed,
 * and dispatches packet to protocol handler.
 */
static void s_process_flow_packet_common(
    dap_io_flow_server_t *a_server,
    const uint8_t *a_data,
    size_t a_data_size,
    const struct sockaddr_storage *a_remote_addr,
    socklen_t a_remote_addr_len,
    dap_events_socket_t *a_listener_es)
{
    if (!a_server || !a_data || !a_remote_addr || !a_listener_es) {
        return;
    }
    
    dap_worker_t *l_worker = dap_worker_get_current();
    if (!l_worker) {
        log_it(L_WARNING, "s_process_flow_packet_common: no current worker");
        return;
    }
    
    // === FAST PATH for BPF tiers (Tier 2/3): NO forwarding needed! ===
    // Kernel SO_REUSEPORT + BPF already distributed packet to correct worker.
    // Simply create flow locally without any cross-worker logic.
    if (a_server->lb_tier == DAP_IO_FLOW_LB_TIER_EBPF ||
        a_server->lb_tier == DAP_IO_FLOW_LB_TIER_CLASSIC_BPF) {
        
        // Find or create flow on LOCAL worker only
        pthread_rwlock_wrlock(&a_server->flow_locks_per_worker[l_worker->id]);
        
        dap_io_flow_t *l_flow = NULL;
        dap_ht_find(a_server->flows_per_worker[l_worker->id], a_remote_addr,
                  sizeof(struct sockaddr_storage), l_flow);
        
        if (!l_flow) {
            // Create flow locally (kernel already routed packet to correct worker!)
            l_flow = a_server->ops->flow_create(a_server, a_remote_addr, a_listener_es);
            
            if (l_flow) {
                memcpy(&l_flow->remote_addr, a_remote_addr, sizeof(struct sockaddr_storage));
                l_flow->remote_addr_len = a_remote_addr_len;
                l_flow->owner_worker_id = l_worker->id;
                l_flow->server = a_server;
                l_flow->last_activity = time(NULL);
                l_flow->boundary_type = a_server->boundary_type;
                
                dap_ht_add_keyptr(a_server->flows_per_worker[l_worker->id], &l_flow->remote_addr,
                         sizeof(struct sockaddr_storage), l_flow);
                
                debug_if(s_debug_more, L_DEBUG, "BPF tier: created flow locally on worker %u",
                         l_worker->id);
            }
        }
        
        pthread_rwlock_unlock(&a_server->flow_locks_per_worker[l_worker->id]);
        
        if (!l_flow) {
            log_it(L_WARNING, "Failed to create flow - dropping packet");
            return;
        }
        
        // Process packet directly (no forwarding!)
        if (a_server->ops->packet_received) {
            a_server->ops->packet_received(a_server, l_flow, a_data, a_data_size,
                                           a_remote_addr, a_listener_es);
        }
        
        return;  // BPF tier processing complete
    }
    
    // === SLOW PATH for Application-level LB (Tier 1): manual forwarding === 
    
    // CRITICAL FIX: Double-checked locking to prevent TOCTOU race condition
    // WITHOUT BPF, kernel SO_REUSEPORT distributes packets from SAME client to DIFFERENT workers!
    // This causes multiple workers to create duplicate flows for same client.
    // Solution: hold write lock on target_worker during entire find-or-create operation.
    
    // Step 1: Determine target worker (hash-based)
    uint32_t l_target_worker_id = l_worker->id;  // Default: create locally
    
    if (a_server->lb_tier == DAP_IO_FLOW_LB_TIER_APPLICATION) {
        // Application-Level Load Balancing: hash (src_ip, src_port) to determine target worker
        const struct sockaddr *l_sa = (const struct sockaddr *)a_remote_addr;
        uint32_t l_hash = 0;
        
        if (l_sa->sa_family == AF_INET) {
            struct sockaddr_in *l_sin = (struct sockaddr_in *)a_remote_addr;
            uint8_t l_hash_input[6];
            memcpy(l_hash_input, &l_sin->sin_addr.s_addr, 4);
            memcpy(l_hash_input + 4, &l_sin->sin_port, 2);
            l_hash = dap_hash_fnv1a_32(l_hash_input, sizeof(l_hash_input));
        } else if (l_sa->sa_family == AF_INET6) {
            struct sockaddr_in6 *l_sin6 = (struct sockaddr_in6 *)a_remote_addr;
            uint8_t l_hash_input[18];
            memcpy(l_hash_input, &l_sin6->sin6_addr, 16);
            memcpy(l_hash_input + 16, &l_sin6->sin6_port, 2);
            l_hash = dap_hash_fnv1a_32(l_hash_input, sizeof(l_hash_input));
        } else {
            log_it(L_WARNING, "Application LB: unknown address family %d, using local worker",
                   l_sa->sa_family);
            l_hash = l_worker->id;
        }
        
        l_target_worker_id = l_hash % dap_proc_thread_get_count();
        
        debug_if(s_debug_more, L_DEBUG,
                 "Application LB: hash=%u (family=%d), target_worker=%u",
                 l_hash, l_sa->sa_family, l_target_worker_id);
    }
    
    // Step 2: ATOMIC find-or-create with write lock on target worker
    // This prevents race: if 2 packets from same client arrive at different workers,
    // only ONE will create the flow (the one that gets write lock first).
    
    pthread_rwlock_wrlock(&a_server->flow_locks_per_worker[l_target_worker_id]);
    
    // Double-check: maybe someone created flow while we waited for lock
    dap_io_flow_t *l_flow = NULL;
    dap_ht_find(a_server->flows_per_worker[l_target_worker_id], a_remote_addr,
              sizeof(struct sockaddr_storage), l_flow);
    
    if (!l_flow) {
        // Flow still doesn't exist - create it NOW (under lock)
        
        // If we're NOT on target worker, must forward packet for processing
        if (l_target_worker_id != l_worker->id) {
            pthread_rwlock_unlock(&a_server->flow_locks_per_worker[l_target_worker_id]);
            
            debug_if(s_debug_more, L_DEBUG,
                     "Application LB: forwarding new flow packet to worker %u",
                     l_target_worker_id);
            
            // Get refcounted cross-worker arena
            dap_arena_t *l_arena = s_get_cross_worker_arena();
            if (!l_arena) {
                log_it(L_ERROR, "Cross-worker arena not available - dropping packet");
                return;
            }
            
            // Allocate packet structure
            dap_arena_alloc_ex_t l_packet_alloc;
            if (!dap_arena_alloc_ex(l_arena, sizeof(struct flow_cross_worker_packet), &l_packet_alloc)) {
                log_it(L_ERROR, "Arena allocation failed for packet struct - dropping packet");
                return;
            }
            struct flow_cross_worker_packet *l_packet = l_packet_alloc.ptr;
            
            // Allocate data buffer
            dap_arena_alloc_ex_t l_data_alloc;
            if (!dap_arena_alloc_ex(l_arena, a_data_size, &l_data_alloc)) {
                log_it(L_ERROR, "Arena allocation failed for data buffer - dropping packet");
                return;
            }
            uint8_t *l_data_copy = l_data_alloc.ptr;
            
            // Fill packet
            memcpy(l_data_copy, a_data, a_data_size);
            l_packet->server = a_server;
            l_packet->data = l_data_copy;
            l_packet->size = a_data_size;
            l_packet->flow = NULL;  // Will be created on target worker
            memcpy(&l_packet->remote_addr, a_remote_addr, sizeof(struct sockaddr_storage));
            l_packet->remote_addr_len = a_remote_addr_len;
            l_packet->page_handle = l_packet_alloc.page_handle;
            
            // Increment refcount before forwarding
            dap_arena_page_ref(l_packet->page_handle);
            
            debug_if(s_debug_more, L_DEBUG,
                     "Forwarding to target worker %u (packet from arena page_handle=%p)",
                     l_target_worker_id, l_packet->page_handle);
            
            // Forward to target worker
            int l_ret = s_forward_packet_to_worker(a_server, l_worker->id, 
                                                    l_target_worker_id, l_packet);
            
            if (l_ret != 0) {
                log_it(L_WARNING, "Forward failed, releasing page reference");
                dap_arena_page_unref(l_packet->page_handle);
            }
            return;
        }
        
        // We ARE on target worker - create flow locally (still under lock)
        l_flow = a_server->ops->flow_create(a_server, a_remote_addr, a_listener_es);
        
        if (l_flow) {
            // Add to hash table (still under lock - prevents duplicates!)
            memcpy(&l_flow->remote_addr, a_remote_addr, sizeof(struct sockaddr_storage));
            l_flow->remote_addr_len = a_remote_addr_len;
            l_flow->owner_worker_id = l_target_worker_id;
            l_flow->server = a_server;
            l_flow->last_activity = time(NULL);
            l_flow->boundary_type = a_server->boundary_type;
            
            dap_ht_add_keyptr(a_server->flows_per_worker[l_target_worker_id], &l_flow->remote_addr,
                     sizeof(struct sockaddr_storage), l_flow);
            
            debug_if(s_debug_more, L_DEBUG, "Created new flow for %s in worker %u (ATOMIC)",
                     dap_io_flow_socket_addr_to_string(a_remote_addr), l_target_worker_id);
        }
    } else {
        debug_if(s_debug_more, L_DEBUG, "Flow already exists for %s in worker %u (double-check prevented duplicate)",
                 dap_io_flow_socket_addr_to_string(a_remote_addr), l_target_worker_id);
    }
    
    pthread_rwlock_unlock(&a_server->flow_locks_per_worker[l_target_worker_id]);
    
    // If flow creation failed, drop packet
    if (!l_flow) {
        log_it(L_WARNING, "Failed to create flow for %s - dropping packet",
               dap_io_flow_socket_addr_to_string(a_remote_addr));
        return;
    }
    
    // After releasing lock, check if we need to forward packet
    // (flow exists but on different worker than current)
    if (l_flow->owner_worker_id != l_worker->id) {
        // Flow exists on different worker - must forward packet
        debug_if(s_debug_more, L_DEBUG,
                 "Flow on worker %u, current worker %u - forwarding packet size=%zu",
                 l_flow->owner_worker_id, l_worker->id, a_data_size);
        
        // Get refcounted cross-worker arena (fail-fast if not available)
        dap_arena_t *l_arena = s_get_cross_worker_arena();
        if (!l_arena) {
            log_it(L_ERROR, "Cross-worker arena not available - dropping packet");
            return;  // Fail-fast: no arena = no forwarding
        }
        
        // Allocate packet structure (with page handle for refcounting)
        dap_arena_alloc_ex_t l_packet_alloc;
        if (!dap_arena_alloc_ex(l_arena, sizeof(struct flow_cross_worker_packet), &l_packet_alloc)) {
            log_it(L_ERROR, "Arena allocation failed for packet struct - dropping");
            return;  // Fail-fast: allocation failed
        }
        struct flow_cross_worker_packet *l_packet = l_packet_alloc.ptr;
        
        // Allocate data buffer (same page, so same page_handle)
        dap_arena_alloc_ex_t l_data_alloc;
        if (!dap_arena_alloc_ex(l_arena, a_data_size, &l_data_alloc)) {
            log_it(L_ERROR, "Arena allocation failed for data buffer - dropping");
            return;  // Fail-fast: allocation failed
        }
        uint8_t *l_data_copy = l_data_alloc.ptr;
        
        // Fill packet structure
        memcpy(l_data_copy, a_data, a_data_size);
        l_packet->server = a_server;  // Always set server
        l_packet->data = l_data_copy;
        l_packet->size = a_data_size;
        l_packet->flow = l_flow;
        memcpy(&l_packet->remote_addr, a_remote_addr, sizeof(struct sockaddr_storage));
        l_packet->remote_addr_len = a_remote_addr_len;
        l_packet->page_handle = l_packet_alloc.page_handle;  // Store page handle for unref
        
        // Increment refcount before forwarding (thread-safe atomic operation)
        dap_arena_page_ref(l_packet->page_handle);
        
        debug_if(s_debug_more, L_DEBUG,
                 "Allocated cross-worker packet (page_handle=%p, data=%p, size=%zu)",
                 l_packet->page_handle, l_data_copy, a_data_size);
        
        // Forward to correct worker
        int l_ret = s_forward_packet_to_worker(a_server, l_worker->id, 
                                                l_flow->owner_worker_id, l_packet);
        if (l_ret != 0) {
            log_it(L_WARNING, "Failed to forward packet to worker %u - releasing page", 
                   l_flow->owner_worker_id);
            // Forward failed - decrement refcount to free page
            dap_arena_page_unref(l_packet->page_handle);
            return;
        }
        
        // Forwarded successfully - receiver worker will unref when done
        return;  // Packet forwarded, done
    }
    
    // Call protocol's packet_received callback (flow is on current worker OR new flow)
    debug_if(s_debug_more, L_DEBUG, "packet_common: CALLING packet_received (flow=%p, size=%zu, worker=%u)",
           l_flow, a_data_size, l_worker->id);
    
    if (a_server->ops->packet_received) {
        a_server->ops->packet_received(a_server, l_flow,
                                       a_data, a_data_size,
                                       a_remote_addr, a_listener_es);
        
        debug_if(s_debug_more, L_DEBUG, "packet_common: packet_received RETURNED");
    } else {
        log_it(L_ERROR, "packet_common: packet_received is NULL!");
    }
}

/**
 * @brief Read callback for UDP listener sockets
 * 
 * Handles packets arriving directly on UDP listener.
 * Separated from queue callback for architectural clarity.
 */
static void s_listener_read_callback(dap_events_socket_t *a_es, void *a_arg)
{
    dap_io_flow_server_t *l_server = (dap_io_flow_server_t*)a_arg;
    
    if (!l_server || !a_es) {
        return;
    }
    
    // CRITICAL: Drop all incoming packets if server is deleting!
    // This prevents packet flood and callback queue overflow during cleanup.
    if (atomic_load(&l_server->is_deleting)) {
        // Silently drop packet - don't even log (would spam)
        a_es->buf_in_size = 0;  // Clear buffer
        return;
    }
    
    // Process incoming data from socket
    if (a_es->buf_in_size == 0) {
        return;
    }
    
    dap_worker_t *l_worker = dap_worker_get_current();
    
    // Log incoming packet details
    debug_if(s_debug_more, L_DEBUG, 
             "Listener worker=%u received %zu bytes on fd=%d, type=%d, from %s",
             l_worker ? l_worker->id : 999,
             a_es->buf_in_size, a_es->fd, a_es->type,
             dap_io_flow_socket_addr_to_string(&a_es->addr_storage));
    
    // Process via common handler (listener_es = a_es itself)
    s_process_flow_packet_common(l_server, a_es->buf_in, a_es->buf_in_size,
                                  &a_es->addr_storage, a_es->addr_size, a_es);
    
    // Clear input buffer
    a_es->buf_in_size = 0;
}

/**
 * @brief Main read callback for flow server listeners (DEPRECATED - use s_listener_read_callback)
 * 
 * This function is kept for backward compatibility but should not be used for new code.
 * Use s_listener_read_callback instead.
 */
static void s_flow_server_read_callback(dap_events_socket_t *a_es, void *a_arg)
{
    // Just forward to new listener callback
    s_listener_read_callback(a_es, a_arg);
}

/**
 * @brief Initialize inter-worker queues for cross-worker forwarding
 * 
 * Creates lock-free ring buffer based queues for each worker pair.
 * Uses dap_context_queue instead of pipe-based queues.
 */
static int s_init_inter_worker_queues(dap_io_flow_server_t *a_server)
{
    uint32_t l_worker_count = dap_proc_thread_get_count();
    
    if (l_worker_count <= 1) {
        // No need for queues with single worker
        return 0;
    }
    
    // Allocate queue inputs array (one per worker)
    a_server->queue_inputs = DAP_NEW_Z_COUNT(dap_context_queue_t*, l_worker_count);
    if (!a_server->queue_inputs) {
        log_it(L_ERROR, "Failed to allocate queue inputs array");
        return -1;
    }
    
    // Allocate queue outputs 2D array [src_worker][dst_worker]
    a_server->inter_worker_queues = DAP_NEW_Z_COUNT(dap_context_queue_t**, l_worker_count);
    if (!a_server->inter_worker_queues) {
        log_it(L_ERROR, "Failed to allocate queue outputs array");
        return -2;
    }
    
    for (uint32_t i = 0; i < l_worker_count; i++) {
        a_server->inter_worker_queues[i] = DAP_NEW_Z_COUNT(dap_context_queue_t*, l_worker_count);
        if (!a_server->inter_worker_queues[i]) {
            log_it(L_ERROR, "Failed to allocate queue outputs for worker %u", i);
            return -3;
        }
    }
    
    // Create queue inputs for each worker (receiving side)
    for (uint32_t dst = 0; dst < l_worker_count; dst++) {
        dap_worker_t *l_dst_worker = dap_events_worker_get(dst);
        if (!l_dst_worker) {
            log_it(L_ERROR, "Failed to get worker %u", dst);
            return -4;
        }
        
        // Create context queue on destination worker's context  
        a_server->queue_inputs[dst] = dap_context_queue_create(
            l_dst_worker->context, 1048576, s_queue_ptr_callback);  // 1M capacity for cross-worker packet forwarding
        
        if (!a_server->queue_inputs[dst]) {
            log_it(L_ERROR, "Failed to create queue input for worker %u", dst);
            return -5;
        }
        
        debug_if(s_debug_more, L_DEBUG, 
                 "Created queue_input[%u]: queue=%p, worker=%u",
                 dst, a_server->queue_inputs[dst], l_dst_worker->id);
    }
    
    // Setup queue references for cross-worker communication
    // Each worker i can send to any other worker j via inter_worker_queues[i][j]
    for (uint32_t src = 0; src < l_worker_count; src++) {
        for (uint32_t dst = 0; dst < l_worker_count; dst++) {
            if (src == dst) {
                continue;  // No queue to self
            }
            
            // Reference the destination worker's input queue
            // Multiple source workers can push to the same destination queue (thread-safe)
            a_server->inter_worker_queues[src][dst] = a_server->queue_inputs[dst];
            
            debug_if(s_debug_more, L_DEBUG, 
                     "Linked queue_output[%u->%u] to queue_input[%u]",
                     src, dst, dst);
        }
    }
    
    log_it(L_INFO, "Initialized inter-worker queues for %u workers (lock-free ring buffers)", l_worker_count);
    return 0;
}

/**
 * @brief Queue pointer callback - process cross-worker packets
 * 
 * Called by reactor when packet pointer arrives from another worker's queue.
 * Processes the packet and frees the cross-worker packet structure.
 */
static void s_queue_ptr_callback(void *a_ptr)
{
    debug_if(s_debug_more, L_DEBUG, "Queue callback ENTRY: a_ptr=%p", a_ptr);
    
    if (!a_ptr) {
        debug_if(s_debug_more, L_DEBUG, "Queue callback: a_ptr is NULL, returning");
        return;
    }
    
    struct flow_cross_worker_packet *l_packet = (struct flow_cross_worker_packet*)a_ptr;
    
    // Get server from packet (always valid)
    if (!l_packet->server) {
        log_it(L_ERROR, "Queue callback: server not found in packet");
        // NOTE: Do NOT free a_ptr - it's allocated from thread-local arena!
        // Arena memory is automatically reused
        return;
    }
    
    dap_io_flow_server_t *l_server = l_packet->server;
    
    // CRITICAL: Increment statistics FIRST (before any checks that might return)
    // This ensures we decrement on ALL exit paths
    atomic_fetch_add(&l_server->cross_worker_packets, 1);
    
    // CRITICAL: Check if server is being deleted
    if (atomic_load(&l_server->is_deleting)) {
        debug_if(s_debug_more, L_DEBUG, 
                 "Queue callback: server is being deleted - dropping packet");
        // Release arena page for this packet
        if (l_packet->page_handle) {
            dap_arena_page_unref(l_packet->page_handle);
        }
        // CRITICAL: Decrement counter (we incremented it above)
        atomic_fetch_sub(&l_server->cross_worker_packets, 1);
        // Signal waiters (quick lock/unlock/signal pattern)
        pthread_mutex_lock(&l_server->cross_worker_mutex);
        pthread_cond_signal(&l_server->cross_worker_cond);
        pthread_mutex_unlock(&l_server->cross_worker_mutex);
        return;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Queue callback: server=%p, lb_tier=%d", 
             l_server, l_server ? (int)l_server->lb_tier : -1);
    
    // Validate server and its dap_server field
    if (!l_server->dap_server) {
        log_it(L_ERROR, "Queue callback: server->dap_server is NULL (server may have been deleted)");
        if (l_packet->page_handle) {
            dap_arena_page_unref(l_packet->page_handle);
        }
        // CRITICAL: Decrement counter (we incremented it above)
        atomic_fetch_sub(&l_server->cross_worker_packets, 1);
        // Signal waiters (quick lock/unlock/signal pattern)
        pthread_mutex_lock(&l_server->cross_worker_mutex);
        pthread_cond_signal(&l_server->cross_worker_cond);
        pthread_mutex_unlock(&l_server->cross_worker_mutex);
        return;
    }
    
    // CRITICAL: Find REAL UDP listener for current worker!
    dap_worker_t *l_worker = dap_worker_get_current();
    dap_events_socket_t *l_real_listener = NULL;
    
    // Try to find local listener (for Tier 2: eBPF with multiple listeners)
    if (l_worker) {
        dap_list_t *l_listener_item = l_server->dap_server->es_listeners;
        while (l_listener_item) {
            dap_events_socket_t *l_listener = (dap_events_socket_t*)l_listener_item->data;
            if (l_listener && l_listener->worker == l_worker &&
                (l_listener->type == DESCRIPTOR_TYPE_SOCKET_UDP || 
                 l_listener->type == DESCRIPTOR_TYPE_SOCKET_CLIENT)) {
                l_real_listener = l_listener;
                break;
            }
            l_listener_item = l_listener_item->next;
        }
    }
    
    // Fallback for Tier 1 (Application-level): use ANY UDP listener
    // When we have single listener on worker 0, forwarded packets on other workers
    // need to reference that single listener for flow creation
    if (!l_real_listener && l_server->lb_tier == DAP_IO_FLOW_LB_TIER_APPLICATION) {
        dap_list_t *l_listener_item = l_server->dap_server->es_listeners;
        while (l_listener_item) {
            dap_events_socket_t *l_listener = (dap_events_socket_t*)l_listener_item->data;
            if (l_listener && (l_listener->type == DESCRIPTOR_TYPE_SOCKET_UDP || 
                              l_listener->type == DESCRIPTOR_TYPE_SOCKET_CLIENT)) {
                l_real_listener = l_listener;
                debug_if(s_debug_more, L_DEBUG,
                         "Application LB: using listener from worker %u for forwarded packet",
                         l_listener->worker ? l_listener->worker->id : 999);
                break;
            }
            l_listener_item = l_listener_item->next;
        }
    }
    
    if (!l_real_listener) {
        log_it(L_ERROR, "Queue callback: Failed to find UDP listener for worker %u (tier=%d) - server may be stopping",
               l_worker ? l_worker->id : 999, l_server->lb_tier);
        // Release arena page for this packet
        if (l_packet->page_handle) {
            dap_arena_page_unref(l_packet->page_handle);
        }
        // CRITICAL: Decrement counter even on error path (we incremented it above)
        atomic_fetch_sub(&l_server->cross_worker_packets, 1);
        return;
    }
    
    debug_if(s_debug_more, L_DEBUG, 
             "Queue callback: forwarding to flow processing with real listener fd=%d (type=%d)",
             l_real_listener->fd, l_real_listener->type);
    
    // CRITICAL: Call s_process_flow_packet_common instead of packet_received directly!
    // s_process_flow_packet_common will:
    // 1. Find existing flow OR create new flow if l_packet->flow is NULL
    // 2. Add new flow to current worker's hash table
    // 3. Then call packet_received with valid flow
    // This ensures proper flow lifecycle for cross-worker forwarded handshakes
    debug_if(s_debug_more, L_DEBUG,
             "Queue callback: CALLING s_process_flow_packet_common(server=%p, flow=%p, size=%zu, listener_fd=%d)",
             l_server, l_packet->flow, l_packet->size, l_real_listener->fd);
    
    s_process_flow_packet_common(
        l_server,
        l_packet->data,
        l_packet->size,
        &l_packet->remote_addr,
        l_packet->remote_addr_len,
        l_real_listener  // Pass REAL UDP listener, not queue!
    );
    
    debug_if(s_debug_more, L_DEBUG, "Queue callback: s_process_flow_packet_common RETURNED");
    
    // CRITICAL: Decrement page refcount after packet is fully processed!
    // This is thread-safe (atomic operation) and allows arena page to be freed
    // when all references are released.
    if (l_packet->page_handle) {
        debug_if(s_debug_more, L_DEBUG, 
                 "Queue callback: releasing page reference (page_handle=%p)",
                 l_packet->page_handle);
        dap_arena_page_unref(l_packet->page_handle);
    } else {
        log_it(L_WARNING, "Queue callback: packet has NULL page_handle");
    }
    
    // CRITICAL: Decrement cross-worker packet counter after processing!
    // This counter is used in cleanup to wait for all packets to drain.
    atomic_fetch_sub(&l_server->cross_worker_packets, 1);
    // Signal waiters (quick lock/unlock/signal)
    pthread_mutex_lock(&l_server->cross_worker_mutex);
    pthread_cond_signal(&l_server->cross_worker_cond);
    pthread_mutex_unlock(&l_server->cross_worker_mutex);
    
    debug_if(s_debug_more, L_DEBUG, 
             "Queue callback: packet processed, cross_worker_packets now: %u",
             atomic_load(&l_server->cross_worker_packets));
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
    
    // Get queue output for this worker pair (references destination's input queue)
    dap_context_queue_t *l_queue = a_server->inter_worker_queues[a_from_worker_id][a_to_worker_id];
    if (!l_queue) {
        log_it(L_ERROR, "No queue for workers %u -> %u", a_from_worker_id, a_to_worker_id);
        return -3;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "Forwarding: src_worker=%u -> dst_worker=%u, queue=%p",
             a_from_worker_id, a_to_worker_id, l_queue);
    
    // Send packet pointer via lock-free queue (zero-copy)
    if (!dap_context_queue_push(l_queue, a_packet)) {
        log_it(L_WARNING, "Failed to send packet pointer to worker %u (queue full)", a_to_worker_id);
        return -4;
    }
    
    debug_if(s_debug_more, L_DEBUG, 
             "Forwarded packet %p (%zu bytes) from worker %u to worker %u",
             a_packet, a_packet->size, a_from_worker_id, a_to_worker_id);
    
    return 0;
}

// =============================================================================
// Accessor Functions
// =============================================================================

/**
 * @brief Set inheritor (user data) for flow server
 */
void dap_io_flow_server_set_inheritor(dap_io_flow_server_t *a_server, void *a_inheritor)
{
    if (a_server) {
        a_server->_inheritor = a_inheritor;
    }
}

/**
 * @brief Get inheritor (user data) from flow server
 */
void* dap_io_flow_server_get_inheritor(dap_io_flow_server_t *a_server)
{
    return a_server ? a_server->_inheritor : NULL;
}

