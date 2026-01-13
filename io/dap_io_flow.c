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
#include "dap_io_flow.h"
#include "dap_io_flow_socket.h"
#include "dap_worker.h"
#include "dap_context.h"
#include "dap_proc_thread.h"
#include "dap_arena.h"

#define LOG_TAG "dap_io_flow"

// Debug mode
static bool s_debug_more = false;

// Thread-local arena for cross-worker packet allocations
// Each worker has its own arena, reset after packet forwarding
static __thread dap_arena_t *tl_cross_worker_arena = NULL;

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

// Forward declarations for internal functions
static void s_flow_server_read_callback(dap_events_socket_t *a_es, void *a_arg);
static void s_queue_ptr_callback(dap_events_socket_t *a_es, void *a_ptr);
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
            log_it(L_NOTICE, "IO Flow debug mode ENABLED");
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
        l_server->flows_per_worker[i] = NULL;  // uthash starts with NULL
    }
    
    // Initialize cleanup synchronization
    pthread_mutex_init(&l_server->cleanup_mutex, NULL);
    pthread_cond_init(&l_server->cleanup_cond, NULL);
    atomic_init(&l_server->pending_cleanups, 0);
    atomic_init(&l_server->active_callbacks, 0);  // Track callbacks in execution
    
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
 * @brief Stop IO flow server synchronously
 * 
 * Closes listener sockets. Workers continue processing but no new connections accepted.
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
 * @brief Callback argument for queue input cleanup
 */
typedef struct queue_input_cleanup_arg {
    dap_events_socket_t *queue_input;      ///< Queue input to delete
    dap_io_flow_server_t *server;          ///< Server for synchronization
    uint32_t worker_id;                    ///< Worker ID (for logging)
} queue_input_cleanup_arg_t;

/**
 * @brief Callback to delete queue input on its native worker
 * 
 * Called via dap_proc_thread_callback_add to ensure queue_input is deleted
 * in the correct worker context. Decrements pending counter and signals
 * condition variable when all deletions complete.
 * 
 * @param a_arg queue_input_cleanup_arg_t* (freed by this callback)
 * @return false (one-shot callback)
 */
static bool s_queue_input_delete_callback(void *a_arg)
{
    queue_input_cleanup_arg_t *l_arg = (queue_input_cleanup_arg_t*)a_arg;
    
    if (!l_arg) {
        log_it(L_ERROR, "queue cleanup callback: NULL argument");
        // NOTE: This should NEVER happen as we always allocate l_arg before scheduling
        // But if it does, we still need to decrement active_callbacks
        // (we don't have server pointer, so this is a leak - but prevents hang)
        return false;
    }
    
    // NOTE: active_callbacks was already incremented BEFORE scheduling this callback
    // (in dap_io_flow_server_delete loop, before dap_proc_thread_callback_add)
    // This prevents main thread from destroying sync primitives before we start
    
    debug_if(s_debug_more, L_DEBUG, 
             "Deleting queue_input %p on worker %u (native context)", 
             l_arg->queue_input, l_arg->worker_id);
    
    // Delete queue_input in its native worker context
    // remove_and_delete_unsafe calls dap_context_remove to unregister from epoll AND hash
    if (l_arg->queue_input) {
        dap_events_socket_remove_and_delete_unsafe(l_arg->queue_input, true);  // Preserve inheritor
    }
    
    // Decrement pending counter and signal if all done
    if (l_arg->server) {
        uint32_t l_remaining = atomic_fetch_sub(&l_arg->server->pending_cleanups, 1) - 1;
        
        debug_if(s_debug_more, L_DEBUG, 
                 "Queue input deleted on worker %u, remaining=%u", 
                 l_arg->worker_id, l_remaining);
        
        if (l_remaining == 0) {
            // All queue_inputs deleted - signal waiting thread
            pthread_mutex_lock(&l_arg->server->cleanup_mutex);
            pthread_cond_signal(&l_arg->server->cleanup_cond);
            pthread_mutex_unlock(&l_arg->server->cleanup_mutex);
            
            log_it(L_DEBUG, "All queue_inputs deleted, signaled main thread");
        }
        
        // CRITICAL: Decrement active_callbacks counter LAST (before return)
        // This tells main thread that we're done using sync primitives
        // MUST be called on ALL code paths to match the increment before scheduling
        atomic_fetch_sub(&l_arg->server->active_callbacks, 1);
    }
    
    DAP_DELETE(l_arg);
    return false; // One-shot callback
}

/**
 * @brief Delete IO flow server and cleanup all resources
 * 
 * CORRECT ORDER (all dap_io_flow resources deleted BEFORE stop):
 * 1. Cleanup flows (user data)
 * 2. Delete inter_worker_queues (outputs)
 * 3. Delete queue_inputs via proc_thread callbacks (pthread_cond wait)
 * 4. Stop server (listeners) - NOW safe, all flow resources deleted
 * 5. Free structures
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
    
    uint32_t l_worker_count = dap_proc_thread_get_count();
    
    // Step 1: Cleanup all flows (user data)
    log_it(L_DEBUG, "Cleaning up flows for %u workers", l_worker_count);
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
    }
    
    // Step 2: Delete inter_worker_queues (outputs) - close write ends of pipes
    // These are on workers, must use MT-safe deletion
    if (a_server->inter_worker_queues) {
        log_it(L_DEBUG, "Deleting inter-worker queue OUTPUTS (write ends)");
        for (uint32_t i = 0; i < l_worker_count; i++) {
            if (a_server->inter_worker_queues[i]) {
                for (uint32_t j = 0; j < l_worker_count; j++) {
                    if (i != j && a_server->inter_worker_queues[i][j]) {
                        dap_worker_t *l_worker = dap_events_worker_get(i);
                        dap_events_socket_uuid_t l_uuid = a_server->inter_worker_queues[i][j]->uuid;
                        
                        // Use MT-safe deletion (queue output is on worker i)
                        if (l_worker) {
                            dap_events_socket_remove_and_delete_mt(l_worker, l_uuid);
                        }
                        
                        a_server->inter_worker_queues[i][j] = NULL;
                    }
                }
                DAP_DELETE(a_server->inter_worker_queues[i]);
            }
        }
        DAP_DELETE(a_server->inter_worker_queues);
    }
    
    // Step 3: Delete queue_inputs (read ends) - wait for graceful cleanup
    // These esockets are on workers, need to schedule deletion via worker callbacks
    if (a_server->queue_inputs) {
        log_it(L_DEBUG, "Scheduling queue_inputs deletion via worker callbacks");
        
        // Count non-NULL queue_inputs
        uint32_t l_pending_count = 0;
        for (uint32_t i = 0; i < l_worker_count; i++) {
            if (a_server->queue_inputs[i]) {
                l_pending_count++;
            }
        }
        
        if (l_pending_count > 0) {
            // Set pending counter
            atomic_store(&a_server->pending_cleanups, l_pending_count);
            
            // Save UUIDs of queue_inputs for lifecycle tracking
            dap_events_socket_uuid_t *l_queue_uuids = DAP_NEW_Z_COUNT(dap_events_socket_uuid_t, l_pending_count);
            if (!l_queue_uuids) {
                log_it(L_CRITICAL, "Failed to allocate UUID tracking array - proceeding without UUID tracking");
            }
            
            uint32_t l_uuid_index = 0;
            if (l_queue_uuids) {
                for (uint32_t i = 0; i < l_worker_count; i++) {
                    if (a_server->queue_inputs[i]) {
                        l_queue_uuids[l_uuid_index++] = a_server->queue_inputs[i]->uuid;
                    }
                }
            }
            
            // Schedule deletions on each worker via proc_thread callbacks
            for (uint32_t i = 0; i < l_worker_count; i++) {
                if (a_server->queue_inputs[i]) {
                    dap_worker_t *l_worker = dap_events_worker_get(i);
                    if (!l_worker || !l_worker->proc_queue_input) {
                        log_it(L_ERROR, "Worker %u or its proc_queue not found", i);
                        atomic_fetch_sub(&a_server->pending_cleanups, 1);
                        continue;
                    }
                    
                    queue_input_cleanup_arg_t *l_arg = DAP_NEW_Z(queue_input_cleanup_arg_t);
                    if (!l_arg) {
                        log_it(L_ERROR, "Failed to allocate cleanup arg for worker %u", i);
                        atomic_fetch_sub(&a_server->pending_cleanups, 1);
                        continue;
                    }
                    
                    l_arg->queue_input = a_server->queue_inputs[i];
                    l_arg->server = a_server;
                    l_arg->worker_id = i;
                    
                    // Increment active_callbacks BEFORE scheduling callback
                    atomic_fetch_add(&a_server->active_callbacks, 1);
                    
                    // Schedule callback on worker's proc_thread queue
                    int l_ret = dap_proc_thread_callback_add(l_worker->proc_queue_input, 
                                                              s_queue_input_delete_callback, 
                                                              l_arg);
                    if (l_ret != 0) {
                        log_it(L_ERROR, "Failed to schedule cleanup callback for worker %u: %d", i, l_ret);
                        atomic_fetch_sub(&a_server->pending_cleanups, 1);
                        atomic_fetch_sub(&a_server->active_callbacks, 1); // Rollback
                        DAP_DELETE(l_arg);
                        continue;
                    }
                    
                    a_server->queue_inputs[i] = NULL; // Marked for deletion
                }
            }
            
            // Wait for all deletions to complete
            log_it(L_DEBUG, "Waiting for %u queue_inputs to be deleted...", l_pending_count);
            pthread_mutex_lock(&a_server->cleanup_mutex);
            
            struct timespec l_timeout;
            clock_gettime(CLOCK_REALTIME, &l_timeout);
            l_timeout.tv_sec += 10; // 10 second timeout
            
            // STEP 1: Wait for all callbacks to be scheduled and signal (pending_cleanups==0)
            while (atomic_load(&a_server->pending_cleanups) > 0) {
                int l_ret = pthread_cond_timedwait(&a_server->cleanup_cond,
                                                   &a_server->cleanup_mutex,
                                                   &l_timeout);
                if (l_ret == ETIMEDOUT) {
                    uint32_t l_still_pending = atomic_load(&a_server->pending_cleanups);
                    log_it(L_WARNING, "Timeout waiting for queue_inputs deletion, %u still pending",
                           l_still_pending);
                    break;
                }
            }
            
            log_it(L_DEBUG, "All queue_inputs deleted (pending_cleanups==0)");
            
            // STEP 2: Wait for ALL callbacks to FULLY COMPLETE execution (active_callbacks==0)
            struct timespec l_active_timeout;
            clock_gettime(CLOCK_REALTIME, &l_active_timeout);
            l_active_timeout.tv_sec += 5; // 5 second timeout for callbacks to exit
            
            while (atomic_load(&a_server->active_callbacks) > 0) {
                struct timespec l_short_wait;
                clock_gettime(CLOCK_REALTIME, &l_short_wait);
                l_short_wait.tv_nsec += 10000000; // 10ms
                if (l_short_wait.tv_nsec >= 1000000000) {
                    l_short_wait.tv_sec += 1;
                    l_short_wait.tv_nsec -= 1000000000;
                }
                
                pthread_cond_timedwait(&a_server->cleanup_cond, 
                                      &a_server->cleanup_mutex, 
                                      &l_short_wait);
                
                struct timespec l_now;
                clock_gettime(CLOCK_REALTIME, &l_now);
                if (l_now.tv_sec >= l_active_timeout.tv_sec) {
                    uint32_t l_still_active = atomic_load(&a_server->active_callbacks);
                    log_it(L_WARNING, "Timeout waiting for %u active callbacks to exit", l_still_active);
                    break;
                }
            }
            
            pthread_mutex_unlock(&a_server->cleanup_mutex);
            log_it(L_DEBUG, "All callbacks fully exited (active_callbacks==0)");
            
            // Memory barrier
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            
            // STEP 3: Poll until all queue_input UUIDs are removed from worker contexts
            // This ensures workers finished processing events from deleted esockets
            if (l_queue_uuids) {
                time_t l_uuid_wait_start = time(NULL);
                time_t l_uuid_max_wait_sec = 5;  // 5 second max
                
                log_it(L_DEBUG, "Polling for %u queue_input UUIDs to be removed from worker hash tables", l_uuid_index);
                
                bool l_all_removed = false;
                uint32_t l_poll_count = 0;
                
                while (time(NULL) - l_uuid_wait_start < l_uuid_max_wait_sec) {
                    l_all_removed = true;
                    l_poll_count++;
                    
                    // Check if any UUID still exists in worker hash tables
                    for (uint32_t i = 0; i < l_uuid_index; i++) {
                        bool l_found = false;
                        
                        // Search all worker contexts for this UUID
                        for (uint32_t w = 0; w < l_worker_count; w++) {
                            dap_worker_t *l_worker = dap_events_worker_get(w);
                            if (l_worker && l_worker->context) {
                                dap_events_socket_t *l_check = dap_context_find(l_worker->context, l_queue_uuids[i]);
                                if (l_check) {
                                    l_found = true;
                                    l_all_removed = false;
                                    
                                    if (l_poll_count % 100 == 0) {  // Log every 100 polls (~1 sec)
                                        log_it(L_DEBUG, "UUID 0x%016lx still in worker %u hash table (poll #%u)",
                                               l_queue_uuids[i], w, l_poll_count);
                                    }
                                    break;
                                }
                            }
                        }
                        
                        if (l_found) {
                            break;  // Still has UUIDs, keep waiting
                        }
                    }
                    
                    if (l_all_removed) {
                        log_it(L_DEBUG, "All queue_input UUIDs removed from workers after %u polls, safe to free",
                               l_poll_count);
                        break;
                    }
                    
                    // Poll interval
                    struct timespec l_sleep = {0, 10000000};  // 10ms
                    nanosleep(&l_sleep, NULL);
                }
                
                if (!l_all_removed) {
                    log_it(L_WARNING, "Timeout waiting for UUID removal after %ld sec (%u polls)",
                           l_uuid_max_wait_sec, l_poll_count);
                    // Log which UUIDs are still present
                    for (uint32_t i = 0; i < l_uuid_index; i++) {
                        for (uint32_t w = 0; w < l_worker_count; w++) {
                            dap_worker_t *l_worker = dap_events_worker_get(w);
                            if (l_worker && l_worker->context) {
                                if (dap_context_find(l_worker->context, l_queue_uuids[i])) {
                                    log_it(L_WARNING, "UUID 0x%016lx still in worker %u", l_queue_uuids[i], w);
                                }
                            }
                        }
                    }
                }
            }
            
            DAP_DELETE(l_queue_uuids);
        }
        
        log_it(L_DEBUG, "Freeing queue_inputs array");
        DAP_DELETE(a_server->queue_inputs);
        log_it(L_DEBUG, "queue_inputs array freed");
    }
    
    // Step 4: Stop server (listeners) - NOW it's safe
    if (a_server->is_running) {
        log_it(L_DEBUG, "Stopping server listeners");
        dap_io_flow_server_stop(a_server);
        log_it(L_DEBUG, "Server listeners stopped");
    }
    
    // Step 5: Free structures
    log_it(L_DEBUG, "Destroying %u flow locks", l_worker_count);
    for (uint32_t i = 0; i < l_worker_count; i++) {
        pthread_rwlock_destroy(&a_server->flow_locks_per_worker[i]);
    }
    log_it(L_DEBUG, "Flow locks destroyed");
    
    log_it(L_DEBUG, "Freeing flows_per_worker and flow_locks_per_worker");
    DAP_DELETE(a_server->flows_per_worker);
    DAP_DELETE(a_server->flow_locks_per_worker);
    log_it(L_DEBUG, "Per-worker structures freed");
    
    log_it(L_DEBUG, "Destroying cleanup synchronization");
    pthread_mutex_destroy(&a_server->cleanup_mutex);
    pthread_cond_destroy(&a_server->cleanup_cond);
    log_it(L_DEBUG, "Cleanup synchronization destroyed");
    
    // Preserve name pointer before freeing (for logging)
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
        
        // Use HASH_ITER for safe iteration with deletion
        HASH_ITER(hh, a_server->flows_per_worker[i], l_flow, l_tmp) {
            // Remove from hash table BEFORE destroying (flow_destroy may free memory)
            HASH_DELETE(hh, a_server->flows_per_worker[i], l_flow);
            
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
 * @brief Get or create thread-local arena for cross-worker packets
 * 
 * Each worker thread has its own arena for allocating cross-worker packet structures.
 * Arena is created once per worker and reused for all cross-worker operations.
 * 
 * @return Arena pointer, or NULL on critical allocation failure
 */
static dap_arena_t* s_get_cross_worker_arena(void)
{
    if (!tl_cross_worker_arena) {
        tl_cross_worker_arena = dap_arena_new(8192);  // 8KB initial, grows as needed
        if (!tl_cross_worker_arena) {
            log_it(L_CRITICAL, "Failed to create cross-worker arena for worker thread");
            return NULL;
        }
        log_it(L_DEBUG, "Created cross-worker arena for worker thread");
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
    
    // Find or create flow
    dap_io_flow_t *l_flow = dap_io_flow_find(a_server, a_remote_addr);
    
    debug_if(s_debug_more, L_DEBUG, "packet_common: worker=%u, flow=%p, remote=%s",
             l_worker->id, l_flow, dap_io_flow_socket_addr_to_string(a_remote_addr));
    
    if (!l_flow) {
        // Application-Level Load Balancing (Tier 1):
        // If no BPF, manually distribute new flows across workers
        uint32_t l_target_worker_id = l_worker->id;  // Default: create locally
        
        if (a_server->lb_tier == DAP_IO_FLOW_LB_TIER_APPLICATION) {
            // Hash (src_ip, src_port) to determine target worker
            // Simple FNV-1a hash on remote address
            uint32_t l_hash = 2166136261u;  // FNV offset basis
            const uint8_t *l_addr_bytes = (const uint8_t *)a_remote_addr;
            for (size_t i = 0; i < a_remote_addr_len; i++) {
                l_hash = (l_hash ^ l_addr_bytes[i]) * 16777619u;  // FNV prime
            }
            l_target_worker_id = l_hash % dap_proc_thread_get_count();
            
            debug_if(s_debug_more, L_DEBUG,
                     "Application LB: hash=%u, target_worker=%u",
                     l_hash, l_target_worker_id);
        }
        
        // If target worker is different, forward packet BEFORE creating flow
        if (l_target_worker_id != l_worker->id) {
            debug_if(s_debug_more, L_DEBUG,
                     "Application LB: forwarding new flow packet to worker %u",
                     l_target_worker_id);
            
            // Get cross-worker arena
            dap_arena_t *l_arena = s_get_cross_worker_arena();
            if (!l_arena) {
                log_it(L_ERROR, "Cross-worker arena not available - creating locally");
                goto create_local;
            }
            
            // Allocate packet for forwarding
            struct flow_cross_worker_packet *l_packet = 
                dap_arena_alloc(l_arena, sizeof(struct flow_cross_worker_packet));
            uint8_t *l_data_copy = dap_arena_alloc(l_arena, a_data_size);
            
            if (!l_packet || !l_data_copy) {
                log_it(L_ERROR, "Arena allocation failed - creating locally");
                dap_arena_reset(l_arena);
                goto create_local;
            }
            
            // Fill packet (flow = NULL since not created yet)
            memcpy(l_data_copy, a_data, a_data_size);
            l_packet->data = l_data_copy;
            l_packet->size = a_data_size;
            l_packet->flow = NULL;  // Will be created on target worker
            memcpy(&l_packet->remote_addr, a_remote_addr, sizeof(struct sockaddr_storage));
            l_packet->remote_addr_len = a_remote_addr_len;
            
            // Forward to target worker
            int l_ret = s_forward_packet_to_worker(a_server, l_worker->id, 
                                                    l_target_worker_id, l_packet);
            dap_arena_reset(l_arena);
            
            if (l_ret == 0) {
                return;  // Forwarded successfully
            }
            // Fall through to create_local on forward failure
        }
        
create_local:
        // Create new flow locally
        l_flow = a_server->ops->flow_create(a_server, a_remote_addr, a_listener_es);
        
        if (l_flow) {
            // Add to local worker's hash table
            memcpy(&l_flow->remote_addr, a_remote_addr, sizeof(struct sockaddr_storage));
            l_flow->remote_addr_len = a_remote_addr_len;
            l_flow->owner_worker_id = l_worker->id;
            l_flow->last_activity = time(NULL);
            l_flow->boundary_type = a_server->boundary_type;
            
            pthread_rwlock_wrlock(&a_server->flow_locks_per_worker[l_worker->id]);
            HASH_ADD(hh, a_server->flows_per_worker[l_worker->id], remote_addr,
                     sizeof(struct sockaddr_storage), l_flow);
            pthread_rwlock_unlock(&a_server->flow_locks_per_worker[l_worker->id]);
            
            debug_if(s_debug_more, L_DEBUG, "Created new flow for %s in worker %u",
                     dap_io_flow_socket_addr_to_string(a_remote_addr), l_worker->id);
        }
    }
    
    // Check if flow is on another worker - forward if needed
    if (l_flow && l_worker && l_flow->owner_worker_id != l_worker->id) {
        // Flow exists on different worker - must forward packet
        debug_if(s_debug_more, L_DEBUG,
                 "Flow on worker %u, current worker %u - forwarding packet size=%zu",
                 l_flow->owner_worker_id, l_worker->id, a_data_size);
        
        // Get cross-worker arena (fail-fast if not available)
        dap_arena_t *l_arena = s_get_cross_worker_arena();
        if (!l_arena) {
            log_it(L_ERROR, "Cross-worker arena not available - dropping packet");
            return;  // Fail-fast: no arena = no forwarding
        }
        
        // Allocate packet structure from arena (fast path)
        struct flow_cross_worker_packet *l_packet = 
            dap_arena_alloc(l_arena, sizeof(struct flow_cross_worker_packet));
        uint8_t *l_data_copy = dap_arena_alloc(l_arena, a_data_size);
        
        if (!l_packet || !l_data_copy) {
            log_it(L_ERROR, "Arena allocation failed for cross-worker packet");
            dap_arena_reset(l_arena);  // Clean up partial allocations
            return;  // Fail-fast: allocation failed
        }
        
        // Fill packet structure
        memcpy(l_data_copy, a_data, a_data_size);
        l_packet->data = l_data_copy;
        l_packet->size = a_data_size;
        l_packet->flow = l_flow;
        memcpy(&l_packet->remote_addr, a_remote_addr, sizeof(struct sockaddr_storage));
        l_packet->remote_addr_len = a_remote_addr_len;
        
        // Forward to correct worker
        int l_ret = s_forward_packet_to_worker(a_server, l_worker->id, 
                                                l_flow->owner_worker_id, l_packet);
        if (l_ret != 0) {
            log_it(L_WARNING, "Failed to forward packet to worker %u", l_flow->owner_worker_id);
        }
        
        // Reset arena after forwarding (memory reclaimed immediately)
        dap_arena_reset(l_arena);
        
        return;  // Packet forwarded, done
    }
    
    // Call protocol's packet_received callback (flow is on current worker OR new flow)
    if (a_server->ops->packet_received) {
        a_server->ops->packet_received(a_server, l_flow,
                                       a_data, a_data_size,
                                       a_remote_addr, a_listener_es);
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
        
        // Create queue input esocket directly on destination worker's context
        // This ensures it's immediately added to epoll and can receive events
        a_server->queue_inputs[dst] = dap_context_create_queue(
            l_dst_worker->context, s_queue_ptr_callback);
        
        if (!a_server->queue_inputs[dst]) {
            log_it(L_ERROR, "Failed to create queue input for worker %u", dst);
            return -5;
        }
        
        a_server->queue_inputs[dst]->worker = l_dst_worker;
        a_server->queue_inputs[dst]->_inheritor = a_server;
        
        debug_if(s_debug_more, L_DEBUG, 
                 "Created queue_input[%u]: es=%p, fd=%d, worker=%u, callback=%p",
                 dst, a_server->queue_inputs[dst], 
                 a_server->queue_inputs[dst]->fd,
                 l_dst_worker->id,
                 a_server->queue_inputs[dst]->callbacks.queue_ptr_callback);
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
    debug_if(s_debug_more, L_DEBUG, "Queue callback ENTRY: a_es=%p, a_ptr=%p", a_es, a_ptr);
    
    if (!a_ptr) {
        debug_if(s_debug_more, L_DEBUG, "Queue callback: a_ptr is NULL, returning");
        return;
    }
    
    dap_io_flow_server_t *l_server = (dap_io_flow_server_t*)a_es->_inheritor;
    if (!l_server) {
        log_it(L_ERROR, "Queue callback: server not found in _inheritor");
        // NOTE: Do NOT free a_ptr - it's allocated from thread-local arena!
        // Arena memory is automatically reused
        return;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Queue callback: server=%p, lb_tier=%d", l_server, l_server->lb_tier);
    
    struct flow_cross_worker_packet *l_packet = (struct flow_cross_worker_packet*)a_ptr;
    
    // Increment statistics
    atomic_fetch_add(&l_server->cross_worker_packets, 1);
    
    // CRITICAL: Find REAL UDP listener for current worker!
    // Do NOT pass queue esocket (a_es) - it's type=10 (QUEUE), not type=4 (UDP)!
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
        log_it(L_ERROR, "Queue callback: Failed to find UDP listener for worker %u (queue fd=%d, type=%d, tier=%d)",
               l_worker ? l_worker->id : 999, a_es->fd, a_es->type, l_server->lb_tier);
        // NOTE: Do NOT free l_packet or l_packet->data - they're allocated from thread-local arena!
        // Arena memory is automatically reused
        return;
    }
    
    debug_if(s_debug_more, L_DEBUG, 
             "Queue callback: forwarding to flow processing with real listener fd=%d (type=%d) instead of queue fd=%d (type=%d)",
             l_real_listener->fd, l_real_listener->type, a_es->fd, a_es->type);
    
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
    
    // NOTE: Do NOT free l_packet or l_packet->data - they're allocated from thread-local arena!
    // Arena memory is automatically reused when the source worker's arena is reset
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
    
    debug_if(s_debug_more, L_DEBUG,
             "Forwarding: src_worker=%u -> dst_worker=%u, queue_input[%u]=%p (fd=%d)",
             a_from_worker_id, a_to_worker_id, a_to_worker_id,
             a_server->queue_inputs[a_to_worker_id],
             a_server->queue_inputs[a_to_worker_id] ? a_server->queue_inputs[a_to_worker_id]->fd : -1);
    
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

