/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.

This file is part of DAP the open source project.

DAP is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DAP is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

See more details here <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_config.h"
#include "dap_net_trans.h"
#include "dap_net_trans_udp_server.h"
#include "dap_net_trans_udp_stream.h"
#include "dap_stream.h"
#include "dap_stream_worker.h"
#include "dap_net_trans_server.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "uthash.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_net_trans_udp_server"

// Forward declaration from dap_net_trans_udp_stream.c
// This function is made non-static to allow server-side stream initialization
dap_net_trans_udp_ctx_t *s_get_or_create_udp_ctx(dap_stream_t *a_stream);

// Forward declarations for inter-worker communication
static void s_pipe_read_callback(dap_events_socket_t *a_es, void *a_arg);
static int s_init_inter_worker_pipes(dap_net_trans_udp_server_t *a_udp_srv);

// Debug flags
static bool s_debug_more = false;  // Extra verbose debugging

// Helper to generate unique UUID for virtual esockets
static inline dap_events_socket_uuid_t dap_events_socket_uuid_generate(void) {
    dap_events_socket_uuid_t l_uuid = 0;
    randombytes((uint8_t*)&l_uuid, sizeof(l_uuid));
    return l_uuid;
}

/**
 * @brief Write callback for virtual UDP esockets
 * 
 * This callback handles write operations for virtual esockets by performing
 * sendto() directly on the physical socket with the client's address.
 * 
 * @param a_es Virtual esocket
 * @param a_arg Pointer to the physical listener socket
 * @return true if data was sent successfully, false otherwise
 */
static bool s_virtual_esocket_write_callback(dap_events_socket_t *a_es, void *a_arg) {
    if (!a_es || !a_es->buf_out_size) {
        return true; // Nothing to write
    }
    
    // Get physical listener socket from arg
    dap_events_socket_t *l_listener = (dap_events_socket_t *)a_arg;
    if (!l_listener) {
        log_it(L_ERROR, "Virtual esocket write: no listener socket");
        return false;
    }
    
    // Send data using sendto with client's address from virtual esocket
    ssize_t l_sent = sendto(l_listener->socket, 
                           (const char *)a_es->buf_out, 
                           a_es->buf_out_size, 
                           0,
                           (struct sockaddr *)&a_es->addr_storage, 
                           a_es->addr_size);
    
    if (l_sent < 0) {
        int l_errno = errno;
        log_it(L_ERROR, "Virtual esocket sendto failed: %s (errno %d)", strerror(l_errno), l_errno);
        return false;
    }
    
    if ((size_t)l_sent < a_es->buf_out_size) {
        log_it(L_WARNING, "Virtual esocket partial send: %zd of %zu bytes", l_sent, a_es->buf_out_size);
        // Shift remaining data
        memmove(a_es->buf_out, a_es->buf_out + l_sent, a_es->buf_out_size - l_sent);
        a_es->buf_out_size -= l_sent;
        return false; // Will retry
    }
    
    debug_if(s_debug_more, L_DEBUG, "Virtual esocket sent %zd bytes via sendto", l_sent);
    a_es->buf_out_size = 0;
    return true;
}

// UDP session mapping structure for server-side demultiplexing
// NEW ARCHITECTURE: No virtual esockets! One physical esocket dispatches to multiple streams.
typedef struct udp_session_entry {
    // Hash key: remote address (IP:port) - uniquely identifies client
    struct sockaddr_storage remote_addr; // Client address (HASH KEY)
    socklen_t remote_addr_len;         // Address length
    
    // Stream associated with this client
    dap_stream_t *stream;              // Associated dap_stream_t instance (NO virtual esocket!)
    uint64_t session_id;               // Session ID from handshake
    
    // Activity tracking
    time_t last_activity;              // Last packet timestamp
    
    // Per-worker locality tracking (for migration decisions)
    uint32_t owner_worker_id;          // Worker that owns this session (where created)
    _Atomic size_t remote_access_count; // Counter for remote worker accesses
    
    // uthash handle (hash by remote_addr)
    UT_hash_handle hh;                 
} udp_session_entry_t;

/**
 * @brief Compare two sockaddr_storage structures for hash table lookup
 * 
 * Compares IP address and port, supporting both IPv4 and IPv6.
 * Used by uthash to find sessions by remote address.
 * 
 * @param a First address
 * @param b Second address
 * @return true if addresses match, false otherwise
 */
static inline bool s_sockaddr_equal(const struct sockaddr_storage *a, const struct sockaddr_storage *b)
{
    if (a->ss_family != b->ss_family)
        return false;
    
    if (a->ss_family == AF_INET) {
        struct sockaddr_in *a4 = (struct sockaddr_in*)a;
        struct sockaddr_in *b4 = (struct sockaddr_in*)b;
        return (a4->sin_port == b4->sin_port) && 
               (a4->sin_addr.s_addr == b4->sin_addr.s_addr);
    } else if (a->ss_family == AF_INET6) {
        struct sockaddr_in6 *a6 = (struct sockaddr_in6*)a;
        struct sockaddr_in6 *b6 = (struct sockaddr_in6*)b;
        return (a6->sin6_port == b6->sin6_port) &&
               (memcmp(&a6->sin6_addr, &b6->sin6_addr, sizeof(struct in6_addr)) == 0);
    }
    
    return false;
}

/**
 * @brief Optimistic session lookup with locality awareness
 * 
 * STRATEGY:
 * 1. Check local worker's hash table first (fast path, no lock contention)
 * 2. If not found, check all other workers' hash tables (slow path)
 * 3. Track remote accesses for migration decisions
 * 
 * @param a_server UDP server instance
 * @param a_remote_addr Client address to lookup
 * @param a_current_worker_id Current worker ID (where packet arrived)
 * @param[out] a_is_local Set to true if session found locally, false if remote
 * @return Session entry or NULL if not found
 */
static udp_session_entry_t* s_find_session_optimistic(
    dap_net_trans_udp_server_t *a_server,
    const struct sockaddr_storage *a_remote_addr,
    uint32_t a_current_worker_id,
    bool *a_is_local)
{
    if (!a_server || a_current_worker_id >= a_server->worker_count) {
        return NULL;
    }
    
    // FAST PATH: Check local worker's hash table
    pthread_rwlock_rdlock(&a_server->worker_locks[a_current_worker_id]);
    udp_session_entry_t *l_session, *l_tmp;
    HASH_ITER(hh, a_server->sessions_per_worker[a_current_worker_id], l_session, l_tmp) {
        if (s_sockaddr_equal(&l_session->remote_addr, a_remote_addr)) {
            pthread_rwlock_unlock(&a_server->worker_locks[a_current_worker_id]);
            *a_is_local = true;
            atomic_fetch_add(&a_server->local_hits, 1);
            return l_session;
        }
    }
    pthread_rwlock_unlock(&a_server->worker_locks[a_current_worker_id]);
    
    // SLOW PATH: Check other workers' hash tables
    for (uint32_t i = 0; i < a_server->worker_count; i++) {
        if (i == a_current_worker_id) {
            continue;  // Already checked
        }
        
        pthread_rwlock_rdlock(&a_server->worker_locks[i]);
        HASH_ITER(hh, a_server->sessions_per_worker[i], l_session, l_tmp) {
            if (s_sockaddr_equal(&l_session->remote_addr, a_remote_addr)) {
                pthread_rwlock_unlock(&a_server->worker_locks[i]);
                *a_is_local = false;
                atomic_fetch_add(&a_server->remote_hits, 1);
                
                // Increment remote access counter (for migration decisions)
                atomic_fetch_add(&l_session->remote_access_count, 1);
                
                debug_if(s_debug_more, L_DEBUG,
                         "Session found in remote worker %u (current=%u), remote_accesses=%zu",
                         i, a_current_worker_id, 
                         atomic_load(&l_session->remote_access_count));
                
                return l_session;
            }
        }
        pthread_rwlock_unlock(&a_server->worker_locks[i]);
    }
    
    return NULL;  // Session not found
}

/**
 * @brief Add packet to inter-worker batch for forwarding
 * 
 * Accumulates packets destined for remote workers in batches.
 * Batches are flushed at the end of reactor cycle.
 * 
 * @param a_udp_srv UDP server instance
 * @param a_from_worker_id Current worker ID (where packet was received)
 * @param a_to_worker_id Target worker ID (where session lives)
 * @param a_session Target session
 * @param a_data Packet data (will be copied)
 * @param a_size Packet size
 * @param a_remote_addr Source address
 * @param a_remote_addr_len Address length
 * @return 0 on success, negative on error
 */
static int s_add_to_batch(
    dap_net_trans_udp_server_t *a_udp_srv,
    uint32_t a_from_worker_id,
    uint32_t a_to_worker_id,
    udp_session_entry_t *a_session,
    const uint8_t *a_data,
    size_t a_size,
    const struct sockaddr_storage *a_remote_addr,
    socklen_t a_remote_addr_len)
{
    if (!a_udp_srv || a_from_worker_id >= a_udp_srv->worker_count || 
        a_to_worker_id >= a_udp_srv->worker_count || !a_session || !a_data) {
        return -1;
    }
    
    udp_worker_context_t *l_ctx = a_udp_srv->worker_contexts[a_from_worker_id];
    if (!l_ctx) {
        log_it(L_ERROR, "Worker context %u is NULL", a_from_worker_id);
        return -2;
    }
    
    // Check if batch array exists for target worker
    if (!l_ctx->batches[a_to_worker_id]) {
        // Allocate initial batch capacity (16 packets)
        l_ctx->batch_capacities[a_to_worker_id] = 16;
        l_ctx->batches[a_to_worker_id] = DAP_NEW_Z_COUNT(udp_cross_worker_packet_t, 
                                                          l_ctx->batch_capacities[a_to_worker_id]);
        if (!l_ctx->batches[a_to_worker_id]) {
            log_it(L_CRITICAL, "Failed to allocate batch for worker %u -> %u", 
                   a_from_worker_id, a_to_worker_id);
            return -3;
        }
        l_ctx->batch_counts[a_to_worker_id] = 0;
    }
    
    // Check if batch is full, expand if needed
    if (l_ctx->batch_counts[a_to_worker_id] >= l_ctx->batch_capacities[a_to_worker_id]) {
        size_t l_new_capacity = l_ctx->batch_capacities[a_to_worker_id] * 2;
        udp_cross_worker_packet_t *l_new_batch = DAP_REALLOC(l_ctx->batches[a_to_worker_id],
                                                               l_new_capacity * sizeof(udp_cross_worker_packet_t));
        if (!l_new_batch) {
            log_it(L_ERROR, "Failed to expand batch for worker %u -> %u", 
                   a_from_worker_id, a_to_worker_id);
            return -4;
        }
        l_ctx->batches[a_to_worker_id] = l_new_batch;
        l_ctx->batch_capacities[a_to_worker_id] = l_new_capacity;
    }
    
    // Add packet to batch
    udp_cross_worker_packet_t *l_pkt = &l_ctx->batches[a_to_worker_id][l_ctx->batch_counts[a_to_worker_id]];
    
    l_pkt->session = a_session;
    l_pkt->size = a_size;
    l_pkt->data = DAP_NEW_SIZE(uint8_t, a_size);
    if (!l_pkt->data) {
        log_it(L_CRITICAL, "Failed to allocate packet data (%zu bytes)", a_size);
        return -5;
    }
    memcpy(l_pkt->data, a_data, a_size);
    
    memcpy(&l_pkt->remote_addr, a_remote_addr, a_remote_addr_len);
    l_pkt->remote_addr_len = a_remote_addr_len;
    
    l_ctx->batch_counts[a_to_worker_id]++;
    
    debug_if(s_debug_more, L_DEBUG, 
             "Added packet to batch: worker %u -> %u (batch size now %zu/%zu)",
             a_from_worker_id, a_to_worker_id, 
             l_ctx->batch_counts[a_to_worker_id],
             l_ctx->batch_capacities[a_to_worker_id]);
    
    return 0;
}

/**
 * @brief Flush batches at end of reactor cycle
 * 
 * Writes accumulated batches to inter-worker pipes.
 * Called by reactor after processing all events.
 * 
 * @param a_udp_srv UDP server instance
 * @param a_worker_id Current worker ID
 */
static void s_flush_batches(dap_net_trans_udp_server_t *a_udp_srv, uint32_t a_worker_id)
{
    if (!a_udp_srv || a_worker_id >= a_udp_srv->worker_count) {
        return;
    }
    
    udp_worker_context_t *l_ctx = a_udp_srv->worker_contexts[a_worker_id];
    if (!l_ctx) {
        return;
    }
    
    // Flush batches to all other workers
    for (uint32_t i = 0; i < a_udp_srv->worker_count; i++) {
        if (i == a_worker_id || l_ctx->batch_counts[i] == 0) {
            continue;  // Skip self or empty batches
        }
        
        size_t l_batch_count = l_ctx->batch_counts[i];
        udp_cross_worker_packet_t *l_batch = l_ctx->batches[i];
        
        // Calculate total size: struct headers + all data
        size_t l_total_size = 0;
        for (size_t j = 0; j < l_batch_count; j++) {
            l_total_size += sizeof(udp_cross_worker_packet_t) + l_batch[j].size;
        }
        
        // Allocate buffer for serialized batch
        uint8_t *l_buffer = DAP_NEW_SIZE(uint8_t, l_total_size);
        if (!l_buffer) {
            log_it(L_ERROR, "Failed to allocate flush buffer (%zu bytes)", l_total_size);
            continue;
        }
        
        // Serialize batch into buffer
        uint8_t *l_ptr = l_buffer;
        for (size_t j = 0; j < l_batch_count; j++) {
            memcpy(l_ptr, &l_batch[j], sizeof(udp_cross_worker_packet_t));
            l_ptr += sizeof(udp_cross_worker_packet_t);
            
            memcpy(l_ptr, l_batch[j].data, l_batch[j].size);
            l_ptr += l_batch[j].size;
            
            // Free packet data (ownership transferred to buffer)
            DAP_DELETE(l_batch[j].data);
        }
        
        // Write to pipe (non-blocking)
        int l_write_fd = l_ctx->pipe_write_fds[i];
        if (l_write_fd >= 0) {
            ssize_t l_written = write(l_write_fd, l_buffer, l_total_size);
            if (l_written < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    log_it(L_ERROR, "Failed to write batch to pipe (worker %u -> %u): %s",
                           a_worker_id, i, strerror(errno));
                }
            } else if ((size_t)l_written < l_total_size) {
                log_it(L_WARNING, "Partial write to pipe (worker %u -> %u): %zd/%zu bytes",
                       a_worker_id, i, l_written, l_total_size);
            } else {
                atomic_fetch_add(&l_ctx->packets_sent, l_batch_count);
                atomic_fetch_add(&l_ctx->batches_flushed, 1);
                
                debug_if(s_debug_more, L_DEBUG, 
                         "Flushed batch: worker %u -> %u (%zu packets, %zu bytes)",
                         a_worker_id, i, l_batch_count, l_total_size);
            }
        }
        
        DAP_DELETE(l_buffer);
        
        // Reset batch
        l_ctx->batch_counts[i] = 0;
    }
}


/**
 * @brief Create virtual UDP esocket for session
 * 
 * Creates a virtual esocket that shares the physical socket FD with the listener,
 * but has its own buffers and remote address storage. This allows multiple UDP
 * sessions to coexist on a single listener socket.
 * 
 * @param a_listener_es Listener socket to share FD with
 * @param a_remote_addr Client address for this virtual socket
 * @param a_remote_addr_len Client address length
 * @return Virtual esocket or NULL on error
 */
static dap_events_socket_t *s_create_virtual_udp_esocket(
    dap_events_socket_t *a_listener_es,
    struct sockaddr_storage *a_remote_addr,
    socklen_t a_remote_addr_len)
{
    if (!a_listener_es || !a_remote_addr) {
        log_it(L_ERROR, "Invalid arguments for virtual esocket creation");
        return NULL;
    }
    
    // Allocate virtual esocket
    dap_events_socket_t *l_virtual_es = DAP_NEW_Z(dap_events_socket_t);
    if (!l_virtual_es) {
        log_it(L_CRITICAL, "Failed to allocate virtual UDP esocket");
        return NULL;
    }
    
    // Share physical socket FD with listener
    l_virtual_es->socket = a_listener_es->socket;
    l_virtual_es->fd = a_listener_es->fd;
    l_virtual_es->type = DESCRIPTOR_TYPE_SOCKET_UDP;
    
    // SHARED BUFFER ARCHITECTURE:
    // Virtual esockets for encrypted stream data read directly from shared buffer
    // Do NOT allocate buf_in - it will be temporarily pointed to shared buffer regions
    // This prevents double-free and enables zero-copy multi-worker architecture
    l_virtual_es->buf_in = NULL;  // Will be set temporarily during packet processing
    l_virtual_es->buf_in_size = 0;
    l_virtual_es->buf_in_size_max = 0;  // Flag: buf_in not owned by this esocket
    
    // Allocate buf_out for responses (owned by virtual esocket)
    l_virtual_es->buf_out_size_max = DAP_EVENTS_SOCKET_BUF_SIZE;
    l_virtual_es->buf_out = DAP_NEW_Z_SIZE(byte_t, l_virtual_es->buf_out_size_max);
    
    if (!l_virtual_es->buf_out) {
        log_it(L_CRITICAL, "Failed to allocate buf_out for virtual esocket");
        DAP_DELETE(l_virtual_es);
        return NULL;
    }
    
    l_virtual_es->buf_out_size = 0;
    
    // Store remote address
    memcpy(&l_virtual_es->addr_storage, a_remote_addr, a_remote_addr_len);
    l_virtual_es->addr_size = a_remote_addr_len;
    
    // Copy context and server references from listener
    l_virtual_es->context = a_listener_es->context;
    l_virtual_es->worker = a_listener_es->worker;
    l_virtual_es->server = a_listener_es->server;
    
    // Set flags (ready to read/write, but don't close physical socket)
    l_virtual_es->flags = DAP_SOCK_READY_TO_READ | DAP_SOCK_READY_TO_WRITE;
    l_virtual_es->no_close = true; // CRITICAL: don't close shared socket
    
    // Initialize timestamps
    l_virtual_es->last_time_active = time(NULL);
    l_virtual_es->time_connection = l_virtual_es->last_time_active;
    
    // Initialize callbacks (will be set by stream)
    memset(&l_virtual_es->callbacks, 0, sizeof(l_virtual_es->callbacks));
    
    // Set custom write callback to handle UDP sendto
    l_virtual_es->callbacks.write_callback = s_virtual_esocket_write_callback;
    l_virtual_es->callbacks.arg = a_listener_es; // Pass listener socket as arg
    
    // Generate unique UUID
    l_virtual_es->uuid = dap_events_socket_uuid_generate();
    
    debug_if(s_debug_more, L_DEBUG, "Created virtual UDP esocket %p (uuid 0x%016" DAP_UINT64_FORMAT_X ") sharing socket %d",
           l_virtual_es, l_virtual_es->uuid, l_virtual_es->socket);
    
    return l_virtual_es;
}

/**
 * @brief Initialize inter-worker pipes for cross-worker packet forwarding
 * 
 * Creates a full mesh of pipes between all workers for batch packet forwarding.
 * Each worker gets one read pipe (for receiving) and N write pipes (for sending to others).
 * 
 * @param a_udp_srv UDP server instance
 * @return 0 on success, negative on error
 */
static int s_init_inter_worker_pipes(dap_net_trans_udp_server_t *a_udp_srv)
{
    if (!a_udp_srv || !a_udp_srv->worker_contexts) {
        log_it(L_ERROR, "Invalid UDP server for pipe initialization");
        return -1;
    }
    
    uint32_t l_count = a_udp_srv->worker_count;
    
    log_it(L_INFO, "Initializing inter-worker pipes: %u workers, %u pipes total", 
           l_count, l_count * l_count);
    
    // Create pipes for each worker using DAP infrastructure
    for (uint32_t i = 0; i < l_count; i++) {
        udp_worker_context_t *l_ctx = a_udp_srv->worker_contexts[i];
        if (!l_ctx) {
            log_it(L_ERROR, "Worker context %u is NULL", i);
            return -2;
        }
        
        dap_worker_t *l_worker = dap_events_worker_get(i);
        if (!l_worker) {
            log_it(L_ERROR, "Failed to get worker %u", i);
            return -3;
        }
        
        // Create pipe esocket for THIS worker (read end will be added to reactor automatically)
        l_ctx->pipe_es = dap_events_socket_create_type_pipe_unsafe(l_worker, s_pipe_read_callback, 0);
        if (!l_ctx->pipe_es) {
            log_it(L_ERROR, "Failed to create pipe esocket for worker %u", i);
            return -4;
        }
        
        // Store UDP server pointer for callback
        l_ctx->pipe_es->_inheritor = a_udp_srv;
        
        // Get read and write fds from pipe esocket
        l_ctx->pipe_read_fd = l_ctx->pipe_es->fd;   // Read end (in reactor)
        int l_write_fd = l_ctx->pipe_es->fd2;       // Write end
        
        // Distribute write end to ALL other workers (for sending TO this worker)
        for (uint32_t j = 0; j < l_count; j++) {
            if (j == i) {
                // Self-pipe not needed
                a_udp_srv->worker_contexts[j]->pipe_write_fds[i] = -1;
            } else {
                // Worker j can write to worker i via this pipe
                a_udp_srv->worker_contexts[j]->pipe_write_fds[i] = l_write_fd;
            }
        }
        
        debug_if(s_debug_more, L_DEBUG, 
                 "Worker %u: pipe read_fd=%d, write_fd=%d distributed to all other workers", 
                 i, l_ctx->pipe_read_fd, l_write_fd);
    }
    
    log_it(L_NOTICE, "Inter-worker pipes initialized: %u workers ready for batch forwarding", l_count);
    return 0;
}

/**
 * @brief Read callback for inter-worker pipe
 * 
 * Receives batched packets from other workers.
 * Reactor already read data into buf_in, now we parse and dispatch.
 * Each pipe read contains ONE batch with multiple packets.
 * 
 * @param a_es Pipe esocket
 * @param a_arg Unused
 */
static void s_pipe_read_callback(dap_events_socket_t *a_es, void *a_arg)
{
    UNUSED(a_arg);
    
    if (!a_es || !a_es->buf_in_size) {
        return;
    }
    
    dap_net_trans_udp_server_t *l_udp_srv = (dap_net_trans_udp_server_t*)a_es->_inheritor;
    if (!l_udp_srv) {
        log_it(L_ERROR, "Pipe read callback: no UDP server in _inheritor");
        a_es->buf_in_size = 0;
        return;
    }
    
    // Get current worker ID
    dap_worker_t *l_worker = a_es->worker;
    uint32_t l_worker_id = l_worker ? l_worker->id : 0;
    udp_worker_context_t *l_ctx = l_udp_srv->worker_contexts[l_worker_id];
    
    // Parse batched packets from buf_in (ONE batch per pipe read)
    // Format: [udp_cross_worker_packet_t][data][udp_cross_worker_packet_t][data]...
    uint8_t *l_ptr = a_es->buf_in;
    size_t l_remaining = a_es->buf_in_size;
    size_t l_packets_processed = 0;
    
    while (l_remaining >= sizeof(udp_cross_worker_packet_t)) {
        udp_cross_worker_packet_t *l_pkt = (udp_cross_worker_packet_t*)l_ptr;
        
        // Validate packet size
        if (l_pkt->size > l_remaining - sizeof(udp_cross_worker_packet_t)) {
            log_it(L_WARNING, "Incomplete packet in pipe buffer (remaining=%zu, expected=%zu), dropping",
                   l_remaining, sizeof(udp_cross_worker_packet_t) + l_pkt->size);
            break;
        }
        
        // Extract packet data (already in buffer after struct)
        uint8_t *l_data = l_ptr + sizeof(udp_cross_worker_packet_t);
        
        // Process packet: dispatch to local session's stream
        // NOTE: We DON'T call trans->ops->read - reactor already did ONE read.
        // We just need to process the UDP packet data directly.
        if (l_pkt->session && l_pkt->session->stream) {
            dap_stream_t *l_stream = l_pkt->session->stream;
            
            // Update remote address (UDP is connectionless)
            if (l_stream->trans_ctx && l_stream->trans_ctx->_inheritor) {
                dap_net_trans_udp_ctx_t *l_udp_ctx = (dap_net_trans_udp_ctx_t*)l_stream->trans_ctx->_inheritor;
                memcpy(&l_udp_ctx->remote_addr, &l_pkt->remote_addr, l_pkt->remote_addr_len);
                l_udp_ctx->remote_addr_len = l_pkt->remote_addr_len;
            }
            
            // Directly process UDP packet data (it's already a complete UDP packet with header)
            // This is the SAME data that was in the original listener's buf_in
            if (l_stream->trans && l_stream->trans->ops && l_stream->trans->ops->read) {
                // Call stream's UDP read handler to process the packet
                ssize_t l_processed = l_stream->trans->ops->read(l_stream, l_data, l_pkt->size);
                if (l_processed > 0) {
                    l_packets_processed++;
                } else {
                    debug_if(s_debug_more, L_WARNING, 
                             "Stream failed to process forwarded packet (%zd)", l_processed);
                }
            } else {
                log_it(L_ERROR, "Stream has no trans read method");
            }
        } else {
            log_it(L_WARNING, "Invalid session or stream in forwarded packet");
        }
        
        // Move to next packet in batch
        l_ptr += sizeof(udp_cross_worker_packet_t) + l_pkt->size;
        l_remaining -= sizeof(udp_cross_worker_packet_t) + l_pkt->size;
    }
    
    if (l_packets_processed > 0) {
        atomic_fetch_add(&l_ctx->packets_received, l_packets_processed);
        
        debug_if(s_debug_more, L_DEBUG, 
                 "Processed %zu packets from inter-worker pipe batch (worker %u)",
                 l_packets_processed, l_worker_id);
    }
    
    // Clear processed data
    a_es->buf_in_size = 0;
}

/**
 * @brief Listener esocket creation callback - initializes shared buffer
 * 
 * Called when physical UDP listener socket is created and added to worker.
 * Sets up shared buffer infrastructure to allow zero-copy reads from multiple 
 * virtual esockets.
 * 
 * @param a_es Listener esocket (physical UDP socket)
 * @param a_arg Unused (always NULL from dap_worker)
 */
/**
 * @brief Listener new callback - called when a new listener esocket is created
 * 
 * WITH SOCKET SHARDING:
 * Each listener operates independently in its own worker thread.
 * No shared buffer needed - each uses its own buf_in.
 * 
 * @param a_es Listener esocket
 * @param a_arg Unused
 */
static void s_listener_new_callback(dap_events_socket_t *a_es, void *a_arg)
{
    UNUSED(a_arg);
    
    if (!a_es || !a_es->server) {
        log_it(L_ERROR, "Invalid esocket or server in listener new callback");
        return;
    }
    
    dap_net_trans_udp_server_t *l_udp_srv = DAP_NET_TRANS_UDP_SERVER(a_es->server);
    if (!l_udp_srv) {
        log_it(L_ERROR, "No UDP server in server->_inheritor");
        return;
    }
    
    // Store first listener for legacy compatibility
    if (!l_udp_srv->listener_es) {
        l_udp_srv->listener_es = a_es;
        log_it(L_INFO, "First listener initialized (fd=%d), socket sharding enabled", a_es->fd);
    } else {
        log_it(L_DEBUG, "Additional listener added (fd=%d) for socket sharding", a_es->fd);
    }
}

/**
 * @brief UDP server read callback - demultiplexes incoming UDP packets
 * 
 * This callback processes incoming UDP datagrams on the server listener socket.
 * It parses the UDP trans header, identifies or creates the corresponding stream,
 * and dispatches the packet for processing.
 * 
 * Packet flow:
 * 1. Parse UDP trans header (dap_stream_trans_udp_header_t)
 * 2. Extract session_id and packet type
 * 3. Lookup or create dap_stream_t for this session
 * 4. Process based on packet type:
 *    - HANDSHAKE: Handle encryption handshake
 *    - SESSION_CREATE: Create new session
 *    - DATA: Forward to dap_stream_data_proc_read
 * 5. Update session activity timestamp
 */
static void s_udp_server_read_callback(dap_events_socket_t *a_es, void *a_arg) {
    (void)a_arg;
    if (!a_es || !a_es->buf_in_size || !a_es->server)
        return;
    
    // Get UDP server instance from listener socket
    dap_net_trans_udp_server_t *l_udp_srv = DAP_NET_TRANS_UDP_SERVER(a_es->server);
    if (!l_udp_srv) {
        log_it(L_ERROR, "No UDP server instance for listener socket");
        a_es->buf_in_size = 0;
        return;
    }
    
    // Get current worker ID for locality-aware session lookup
    dap_worker_t *l_current_worker = a_es->worker;
    uint32_t l_worker_id = l_current_worker ? l_current_worker->id : 0;
    
    // Macro to flush batches before returning
    #define FLUSH_AND_RETURN() do { \
        s_flush_batches(l_udp_srv, l_worker_id); \
        return; \
    } while(0)
    
    // WITH SOCKET SHARDING:
    // Each listener processes its OWN packets using its OWN buf_in.
    // No shared buffer needed - just use a_es->buf_in directly!
    
    uint8_t *l_buf = a_es->buf_in;
    size_t l_buf_size = a_es->buf_in_size;
    
    debug_if(s_debug_more, L_DEBUG, "UDP server received %zu bytes on socket %d (worker %u)", 
           l_buf_size, a_es->socket, l_worker_id);
    
    // Check if we have at least a UDP header
    if (a_es->buf_in_size < sizeof(dap_stream_trans_udp_header_t)) {
        log_it(L_WARNING, "UDP packet too small (%zu bytes), dropping", a_es->buf_in_size);
        a_es->buf_in_size = 0;
        FLUSH_AND_RETURN();
    }
    
    // First, try to find existing session by address (optimistic lookup)
    // If session exists with encryption key AND packet is not a valid control packet,
    // this is encrypted stream data
    udp_session_entry_t *l_session = NULL;
    bool l_is_local = false;
    
    l_session = s_find_session_optimistic(l_udp_srv, &a_es->addr_storage, l_worker_id, &l_is_local);
    
    // ARCHITECTURE: UDP packet encryption
    // - HANDSHAKE: NOT encrypted (no key yet)
    // - SESSION_CREATE: ENCRYPTED with handshake key
    // - DATA: ENCRYPTED with session key
    // So only HANDSHAKE is unencrypted control packet!
    
    dap_stream_trans_udp_header_t *l_header = (dap_stream_trans_udp_header_t*)a_es->buf_in;
    uint8_t l_type = l_header->type;
    bool l_is_handshake = (l_type == DAP_STREAM_UDP_PKT_HANDSHAKE);
    
    // Debug: Check conditions for encrypted data path
    bool l_has_session = (l_session != NULL);
    bool l_has_stream = l_has_session && l_session->stream;
    
    if (l_has_session && l_buf_size > 0) {
        debug_if(s_debug_more, L_DEBUG, 
                 "Dispatcher: session=%p (local=%d), stream=%p, type=0x%02x, is_handshake=%d, size=%zu",
                 l_session, l_is_local, l_has_stream ? l_session->stream : NULL,
                 l_type, l_is_handshake, l_buf_size);
    }
    
    // NEW ARCHITECTURE: Dispatch by encryption status and locality
    // - HANDSHAKE: unencrypted → control path (always local)
    // - SESSION_CREATE, DATA: encrypted → check locality
    //   * Local session: process here
    //   * Remote session: forward via pipe
    // If session exists (after HANDSHAKE), all packets except HANDSHAKE go through encrypted path
    if (l_has_session && l_has_stream && !l_is_handshake) {
        
        debug_if(s_debug_more, L_DEBUG, "Dispatching encrypted UDP packet type=0x%02x (%zu bytes) to stream %p", 
               l_type, l_buf_size, l_session->stream);
        
        // Check if session is LOCAL or REMOTE
        if (!l_is_local) {
            // REMOTE SESSION: Add packet to batch for forwarding to owning worker
            debug_if(s_debug_more, L_INFO, 
                     "Cross-worker packet detected: batching for worker %u", 
                     l_session->owner_worker_id);
            
            // Add to batch (will be flushed at end of reactor cycle)
            if (s_add_to_batch(l_udp_srv, l_worker_id, l_session->owner_worker_id,
                               l_session, a_es->buf_in, l_buf_size,
                               &a_es->addr_storage, a_es->addr_size) != 0) {
                log_it(L_ERROR, "Failed to add packet to batch");
            }
            
            a_es->buf_in_size = 0;
            FLUSH_AND_RETURN();
        }
        
        // LOCAL SESSION: Process here
        // CRITICAL: Keep sessions_lock as READ lock during stream access
        dap_stream_t *l_stream = l_session->stream;
        
        // CRITICAL: Update remote_addr from current packet (UDP is connectionless!)
        // Client may send from different ports, we need to respond to the CURRENT address
        if (l_stream->trans_ctx && l_stream->trans_ctx->_inheritor) {
            dap_net_trans_udp_ctx_t *l_udp_ctx = (dap_net_trans_udp_ctx_t *)l_stream->trans_ctx->_inheritor;
            if (l_udp_ctx && a_es->addr_size > 0) {
                struct sockaddr_in *l_old_addr = (struct sockaddr_in*)&l_udp_ctx->remote_addr;
                uint16_t l_old_port = ntohs(l_old_addr->sin_port);
                
                memcpy(&l_udp_ctx->remote_addr, &a_es->addr_storage, a_es->addr_size);
                l_udp_ctx->remote_addr_len = a_es->addr_size;
                
                struct sockaddr_in *l_new_addr = (struct sockaddr_in*)&l_udp_ctx->remote_addr;
                uint16_t l_new_port = ntohs(l_new_addr->sin_port);
                
                debug_if(s_debug_more, L_DEBUG, "Updated remote_addr for stream %p: %u -> %u", 
                         l_stream, l_old_port, l_new_port);
            }
        }
        
        // Dispatch encrypted data to stream for processing
        // Stream will decrypt and process channel packets internally
        if (l_stream->trans && l_stream->trans->ops && l_stream->trans->ops->read) {
            // Pass FULL UDP packet (with header) to s_udp_read for consistent processing
            // s_udp_read SERVER MODE will parse header and handle decryption
            size_t l_packet_size = l_buf_size;
            
            debug_if(s_debug_more, L_DEBUG, "Calling stream read with encrypted DATA packet (%zu bytes)", l_packet_size);
            
            // Stream read will parse UDP header, decrypt payload, and process stream data
            ssize_t l_read = l_stream->trans->ops->read(l_stream, a_es->buf_in, l_packet_size);
            
            debug_if(s_debug_more, L_DEBUG, "Stream processed %zd bytes of encrypted data", l_read);
        } else {
            log_it(L_ERROR, "Stream has no trans read method for encrypted data");
        }
        
        // Release locks
        
        // Clear listener buffer
        a_es->buf_in_size = 0;
        FLUSH_AND_RETURN();
    }
    
    // No session OR packet is HANDSHAKE - parse as unencrypted control packet (HANDSHAKE only!)
    
    // Parse control packet (l_type and l_header already defined above)
    uint8_t l_version = l_header->version;
    uint16_t l_payload_len = ntohs(l_header->length);
    uint32_t l_seq_num = ntohl(l_header->seq_num);
    uint64_t l_session_id = be64toh(l_header->session_id);
    
    debug_if(s_debug_more, L_DEBUG, "UDP control packet: ver=%u type=%u len=%u seq=%u session=0x%lx", 
           l_version, l_type, l_payload_len, l_seq_num, l_session_id);
    
    // Validate version
    if (l_version != 1) {
        log_it(L_WARNING, "Invalid UDP control packet version %u (expected 1), dropping", l_version);
        a_es->buf_in_size = 0;
        FLUSH_AND_RETURN();
    }
    
    // Check if we have full packet
    size_t l_total_size = sizeof(dap_stream_trans_udp_header_t) + l_payload_len;
    if (a_es->buf_in_size < l_total_size) {
        log_it(L_WARNING, "Incomplete UDP packet (%zu < %zu), dropping", a_es->buf_in_size, l_total_size);
        a_es->buf_in_size = 0;
        FLUSH_AND_RETURN();
    }
    
    // Extract payload pointer
    uint8_t *l_payload = a_es->buf_in + sizeof(dap_stream_trans_udp_header_t);
    
    // Lookup or create session for control packets (HANDSHAKE)
    // NEW ARCHITECTURE: Per-worker hash tables, optimistic lookup already done
    // For HANDSHAKE, need to check again and potentially create new session
    if (l_type == DAP_STREAM_UDP_PKT_HANDSHAKE) {
        // Check if session exists (may have been created between our first lookup and now)
        bool l_dummy;
        l_session = s_find_session_optimistic(l_udp_srv, &a_es->addr_storage, l_worker_id, &l_dummy);
        
        if (!l_session) {
            log_it(L_INFO, "Creating new UDP session 0x%lx for HANDSHAKE from remote addr in worker %u", 
                   l_session_id, l_worker_id);
            
            // Create new session entry (will be added to local worker's hash table)
            l_session = DAP_NEW_Z(udp_session_entry_t);
            if (!l_session) {
                log_it(L_CRITICAL, "Failed to allocate UDP session entry");
                a_es->buf_in_size = 0;
                FLUSH_AND_RETURN();
            }
            
            l_session->session_id = l_session_id;
            l_session->last_activity = time(NULL);
            l_session->owner_worker_id = l_worker_id;  // Session created in this worker
            atomic_init(&l_session->remote_access_count, 0);
            
            // Store client address (from recvfrom in dap_context.c)
        // For UDP listener sockets, remote address is stored in addr_storage during recvfrom
        if (a_es->addr_size > 0) {
            memcpy(&l_session->remote_addr, &a_es->addr_storage, 
                   a_es->addr_size < sizeof(l_session->remote_addr) ? 
                   a_es->addr_size : sizeof(l_session->remote_addr));
            l_session->remote_addr_len = a_es->addr_size;
        }
        
        // NEW ARCHITECTURE: No virtual esocket! Create stream WITHOUT esocket.
        // Stream will use listener's physical esocket for I/O, dispatching by remote_addr.
        l_session->stream = DAP_NEW_Z(dap_stream_t);
        if (!l_session->stream) {
            log_it(L_ERROR, "Failed to create stream for UDP session");
            DAP_DELETE(l_session);
            a_es->buf_in_size = 0;
            FLUSH_AND_RETURN();
        }
        
        // CRITICAL: Set stream_worker from listener's worker for channel creation
        // Without stream_worker, dap_stream_ch_new will fail!
        l_session->stream->stream_worker = DAP_STREAM_WORKER(a_es->worker);
        if (!l_session->stream->stream_worker) {
            log_it(L_ERROR, "Failed to get stream_worker from listener esocket worker");
            DAP_DELETE(l_session->stream);
            DAP_DELETE(l_session);
            a_es->buf_in_size = 0;
            FLUSH_AND_RETURN();
        }
        log_it(L_DEBUG, "Set stream_worker %p for server-side stream %p", 
               l_session->stream->stream_worker, l_session->stream);
        
        // Initialize trans_ctx WITHOUT esocket (dispatcher will handle I/O)
        l_session->stream->trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
        if (!l_session->stream->trans_ctx) {
            log_it(L_ERROR, "Failed to create trans_ctx for UDP session");
            DAP_DELETE(l_session->stream);
            DAP_DELETE(l_session);
            a_es->buf_in_size = 0;
            FLUSH_AND_RETURN();
        }
        
        // trans_ctx points back to stream, but NO esocket! Dispatcher handles I/O.
        l_session->stream->trans_ctx->stream = l_session->stream;
        l_session->stream->trans_ctx->esocket = NULL;  // No virtual esocket!
        l_session->stream->trans_ctx->esocket_uuid = 0;
        l_session->stream->trans_ctx->esocket_worker = a_es->worker;
        
        // Set stream trans to UDP
        if (l_udp_srv->trans) {
            l_session->stream->trans = l_udp_srv->trans;
        }
        
        // CRITICAL: Create UDP per-stream context for server-side stream!
        // This is required for handshake processing and write operations.
        dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_or_create_udp_ctx(l_session->stream);
        if (!l_udp_ctx) {
            log_it(L_ERROR, "Failed to create UDP context for server-side stream");
            DAP_DELETE(l_session->stream->trans_ctx);
            DAP_DELETE(l_session->stream);
            DAP_DELETE(l_session);
            a_es->buf_in_size = 0;
            FLUSH_AND_RETURN();
        }
        
        // Store remote address in UDP context for server-side writes (sendto)
        memcpy(&l_udp_ctx->remote_addr, &a_es->addr_storage, a_es->addr_size);
        l_udp_ctx->remote_addr_len = a_es->addr_size;
        l_udp_ctx->session_id = l_session_id;
        
        // CRITICAL: Store THIS listener esocket (the one that received the packet)
        // With socket sharding, we have N listener sockets. The session must use
        // the SAME listener socket that received its first packet for ALL responses.
        // This ensures session affinity and prevents race conditions on addr_storage.
        l_udp_ctx->listener_esocket = a_es;  // Use a_es (current listener), NOT l_udp_srv->listener_es
        
        log_it(L_DEBUG, "Initialized UDP context for server-side stream %p (session 0x%lx, listener_es=%p fd=%d)", 
               l_session->stream, l_session_id, l_udp_ctx->listener_esocket, 
               l_udp_ctx->listener_esocket ? l_udp_ctx->listener_esocket->fd : -1);
        
        // Add to server's session hash table by remote_addr (already under write lock)
        // NOTE: uthash will use full remote_addr as key via s_find_session_by_addr iteration
        // Add session to THIS worker's hash table (locality-aware)
        pthread_rwlock_wrlock(&l_udp_srv->worker_locks[l_worker_id]);
        HASH_ADD(hh, l_udp_srv->sessions_per_worker[l_worker_id], remote_addr, 
                 sizeof(l_session->remote_addr), l_session);
        pthread_rwlock_unlock(&l_udp_srv->worker_locks[l_worker_id]);
        
        log_it(L_INFO, "Created UDP session 0x%lx in worker %u with stream %p (NO virtual esocket - dispatcher architecture)", 
               l_session_id, l_worker_id, l_session->stream);
        
        // Downgrade from write to read lock (keep lock for stream access safety)
    }  // End of if (!l_session) for HANDSHAKE creation
    }  // End of if (l_type == DAP_STREAM_UDP_PKT_HANDSHAKE)
    
    // NOTE: sessions_lock is held as READ lock at this point for ALL paths
    // This protects session->stream from being deleted by cleanup in another thread
    
    if (!l_session) {
        log_it(L_WARNING, "No session found for UDP packet (session_id=0x%lx, type=%u), dropping", 
               l_session_id, l_type);
        a_es->buf_in_size = 0;
        FLUSH_AND_RETURN();
    }
    
    // Update activity timestamp (under sessions_lock)
    l_session->last_activity = time(NULL);
    
    // Access stream UNDER sessions_lock for thread safety
    dap_stream_t *l_stream = l_session->stream;
    
    if (!l_stream) {
        log_it(L_ERROR, "Session has invalid stream");
        a_es->buf_in_size = 0;
        FLUSH_AND_RETURN();
    }
    
    // NEW ARCHITECTURE: No virtual esocket! Stream reads directly from listener's buf_in.
    // Dispatcher calls stream's read method with data from physical esocket.
    
    switch (l_type) {
        case DAP_STREAM_UDP_PKT_HANDSHAKE:
            debug_if(s_debug_more, L_DEBUG, "Dispatching UDP HANDSHAKE packet to stream %p (session 0x%lx)", 
                     l_stream, l_session_id);
            
            // Call stream's trans read method with FULL UDP packet (header + payload)
            // s_udp_read will parse header and extract payload internally
            if (l_stream->trans && l_stream->trans->ops && l_stream->trans->ops->read) {
                // Pass FULL packet (header + payload) so s_udp_read can determine packet type
                ssize_t l_read = l_stream->trans->ops->read(l_stream, a_es->buf_in, l_total_size);
                
                debug_if(s_debug_more, L_DEBUG, "Stream read returned %zd bytes", l_read);
            } else {
                log_it(L_ERROR, "Stream has no trans read method");
            }
            break;
            
        case DAP_STREAM_UDP_PKT_SESSION_CREATE:
            debug_if(s_debug_more, L_DEBUG, "Dispatching UDP SESSION_CREATE packet to stream %p (session 0x%lx)", 
                     l_stream, l_session_id);
            
            // Pass FULL packet (header + payload) for unified processing
            if (l_stream->trans && l_stream->trans->ops && l_stream->trans->ops->read) {
                ssize_t l_read = l_stream->trans->ops->read(l_stream, a_es->buf_in, l_total_size);
                
                debug_if(s_debug_more, L_DEBUG, "Stream read returned %zd bytes", l_read);
            } else {
                log_it(L_ERROR, "Stream has no trans read method");
            }
            break;
            
        case DAP_STREAM_UDP_PKT_DATA:
            // NOTE: This is for encrypted stream data packets
            debug_if(s_debug_more, L_DEBUG, "Dispatching UDP DATA packet (%u bytes) to stream %p (session 0x%lx)", 
                   l_payload_len, l_stream, l_session_id);
            
            // Pass FULL packet (header + payload) for unified processing
            // s_udp_read will decrypt and process via stream channels
            if (l_stream->trans && l_stream->trans->ops && l_stream->trans->ops->read) {
                ssize_t l_read = l_stream->trans->ops->read(l_stream, a_es->buf_in, l_total_size);
                
                debug_if(s_debug_more, L_DEBUG, "Stream read returned %zd bytes", l_read);
            } else {
                log_it(L_ERROR, "Stream has no trans read method for DATA packet");
            }
            break;
            
        case DAP_STREAM_UDP_PKT_KEEPALIVE:
            debug_if(s_debug_more, L_DEBUG, "Processing UDP KEEPALIVE packet");
            // Just update timestamp (already done above)
            break;
            
        case DAP_STREAM_UDP_PKT_CLOSE:
            log_it(L_INFO, "Processing UDP CLOSE packet for session 0x%lx", l_session_id);
            // Remove session from THIS worker's hash table
            pthread_rwlock_wrlock(&l_udp_srv->worker_locks[l_worker_id]);
            HASH_DEL(l_udp_srv->sessions_per_worker[l_worker_id], l_session);
            pthread_rwlock_unlock(&l_udp_srv->worker_locks[l_worker_id]);
            
            // CRITICAL: Do NOT access trans_ctx->esocket here!
            // This cleanup runs in server thread, but esocket may be on different worker
            // Let dap_stream_delete_unsafe handle esocket cleanup safely
            
            // Delete stream (will handle esocket cleanup in correct worker context)
            if (l_session->stream) {
                dap_stream_delete_unsafe(l_session->stream);
            }
            
            DAP_DELETE(l_session);
            break;
            
        default:
            log_it(L_WARNING, "Unknown UDP packet type %u, dropping", l_type);
            break;
    }
    
    // Release sessions lock (held during control packet processing for thread safety)
    
    // Clear listener socket buffer and flush batches (we've processed the packet)
    a_es->buf_in_size = 0;
    FLUSH_AND_RETURN();
}

#undef FLUSH_AND_RETURN

// Trans server operations callbacks
static void* s_udp_server_new(const char *a_server_name)
{
    return (void*)dap_net_trans_udp_server_new(a_server_name);
}

static int s_udp_server_start(void *a_server, const char *a_cfg_section, 
                               const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    dap_net_trans_udp_server_t *l_udp = (dap_net_trans_udp_server_t *)a_server;
    return dap_net_trans_udp_server_start(l_udp, a_cfg_section, a_addrs, a_ports, a_count);
}

static void s_udp_server_stop(void *a_server)
{
    dap_net_trans_udp_server_t *l_udp = (dap_net_trans_udp_server_t *)a_server;
    dap_net_trans_udp_server_stop(l_udp);
}

static void s_udp_server_delete(void *a_server)
{
    dap_net_trans_udp_server_t *l_udp = (dap_net_trans_udp_server_t *)a_server;
    dap_net_trans_udp_server_delete(l_udp);
}

static const dap_net_trans_server_ops_t s_udp_server_ops = {
    .new = s_udp_server_new,
    .start = s_udp_server_start,
    .stop = s_udp_server_stop,
    .delete = s_udp_server_delete
};

/**
 * @brief Initialize UDP server module
 */
int dap_net_trans_udp_server_init(void)
{
    // Read debug configuration
    if (g_config) {
        s_debug_more = dap_config_get_item_bool_default(g_config, "stream_udp", "debug_more", false);
        if (s_debug_more) {
            log_it(L_NOTICE, "UDP server: verbose debugging ENABLED");
        }
    }
    
    // Register trans server operations for all UDP variants
    int l_ret = dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_BASIC, &s_udp_server_ops);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register UDP_BASIC trans server operations");
        return l_ret;
    }
    
    // Register for other UDP variants too
    dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_RELIABLE, &s_udp_server_ops);
    dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_QUIC_LIKE, &s_udp_server_ops);
    
    log_it(L_NOTICE, "Initialized UDP server module");
    return 0;
}

/**
 * @brief Deinitialize UDP server module
 */
void dap_net_trans_udp_server_deinit(void)
{
    // Unregister trans server operations
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_UDP_BASIC);
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_UDP_RELIABLE);
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_UDP_QUIC_LIKE);
    
    log_it(L_INFO, "UDP server module deinitialized");
}

/**
 * @brief Create new UDP server instance
 */
dap_net_trans_udp_server_t *dap_net_trans_udp_server_new(const char *a_server_name)
{
    if (!a_server_name) {
        log_it(L_ERROR, "Server name is NULL");
        return NULL;
    }

    dap_net_trans_udp_server_t *l_udp_server = DAP_NEW_Z(dap_net_trans_udp_server_t);
    if (!l_udp_server) {
        log_it(L_CRITICAL, "Cannot allocate memory for UDP server");
        return NULL;
    }

    dap_strncpy(l_udp_server->server_name, a_server_name, sizeof(l_udp_server->server_name) - 1);
    
    // Initialize per-worker session management
    l_udp_server->worker_count = dap_events_thread_get_count();
    if (l_udp_server->worker_count == 0) {
        log_it(L_ERROR, "Worker count is 0 - event system not initialized?");
        DAP_DELETE(l_udp_server);
        return NULL;
    }
    
    // Allocate per-worker hash tables
    l_udp_server->sessions_per_worker = DAP_NEW_Z_COUNT(udp_session_entry_t*, l_udp_server->worker_count);
    if (!l_udp_server->sessions_per_worker) {
        log_it(L_CRITICAL, "Cannot allocate per-worker session tables");
        DAP_DELETE(l_udp_server);
        return NULL;
    }
    
    // Allocate per-worker locks
    l_udp_server->worker_locks = DAP_NEW_Z_COUNT(pthread_rwlock_t, l_udp_server->worker_count);
    if (!l_udp_server->worker_locks) {
        log_it(L_CRITICAL, "Cannot allocate per-worker locks");
        DAP_DELETE(l_udp_server->sessions_per_worker);
        DAP_DELETE(l_udp_server);
        return NULL;
    }
    
    // Initialize per-worker locks
    for (uint32_t i = 0; i < l_udp_server->worker_count; i++) {
        pthread_rwlock_init(&l_udp_server->worker_locks[i], NULL);
        l_udp_server->sessions_per_worker[i] = NULL;  // Empty hash table
    }
    
    // Allocate worker contexts
    l_udp_server->worker_contexts = DAP_NEW_Z_COUNT(udp_worker_context_t*, l_udp_server->worker_count);
    if (!l_udp_server->worker_contexts) {
        log_it(L_CRITICAL, "Cannot allocate worker contexts");
        for (uint32_t i = 0; i < l_udp_server->worker_count; i++) {
            pthread_rwlock_destroy(&l_udp_server->worker_locks[i]);
        }
        DAP_DELETE(l_udp_server->worker_locks);
        DAP_DELETE(l_udp_server->sessions_per_worker);
        DAP_DELETE(l_udp_server);
        return NULL;
    }
    
    // Initialize each worker context
    for (uint32_t i = 0; i < l_udp_server->worker_count; i++) {
        udp_worker_context_t *l_ctx = DAP_NEW_Z(udp_worker_context_t);
        if (!l_ctx) {
            log_it(L_CRITICAL, "Cannot allocate worker context %u", i);
            // Cleanup already allocated contexts
            for (uint32_t j = 0; j < i; j++) {
                if (l_udp_server->worker_contexts[j]) {
                    DAP_DELETE(l_udp_server->worker_contexts[j]->batches);
                    DAP_DELETE(l_udp_server->worker_contexts[j]->batch_counts);
                    DAP_DELETE(l_udp_server->worker_contexts[j]->batch_capacities);
                    DAP_DELETE(l_udp_server->worker_contexts[j]->pipe_write_fds);
                    DAP_DELETE(l_udp_server->worker_contexts[j]);
                }
            }
            DAP_DELETE(l_udp_server->worker_contexts);
            for (uint32_t j = 0; j < l_udp_server->worker_count; j++) {
                pthread_rwlock_destroy(&l_udp_server->worker_locks[j]);
            }
            DAP_DELETE(l_udp_server->worker_locks);
            DAP_DELETE(l_udp_server->sessions_per_worker);
            DAP_DELETE(l_udp_server);
            return NULL;
        }
        
        l_ctx->worker_id = i;
        l_ctx->pipe_read_fd = -1;
        l_ctx->pipe_es = NULL;
        
        // Allocate batch buffers
        l_ctx->batches = DAP_NEW_Z_COUNT(udp_cross_worker_packet_t*, l_udp_server->worker_count);
        l_ctx->batch_counts = DAP_NEW_Z_COUNT(size_t, l_udp_server->worker_count);
        l_ctx->batch_capacities = DAP_NEW_Z_COUNT(size_t, l_udp_server->worker_count);
        l_ctx->pipe_write_fds = DAP_NEW_Z_COUNT(int, l_udp_server->worker_count);
        
        if (!l_ctx->batches || !l_ctx->batch_counts || !l_ctx->batch_capacities || !l_ctx->pipe_write_fds) {
            log_it(L_CRITICAL, "Cannot allocate batch buffers for worker %u", i);
            DAP_DELETE(l_ctx->batches);
            DAP_DELETE(l_ctx->batch_counts);
            DAP_DELETE(l_ctx->batch_capacities);
            DAP_DELETE(l_ctx->pipe_write_fds);
            DAP_DELETE(l_ctx);
            // Cleanup
            for (uint32_t j = 0; j < i; j++) {
                if (l_udp_server->worker_contexts[j]) {
                    DAP_DELETE(l_udp_server->worker_contexts[j]->batches);
                    DAP_DELETE(l_udp_server->worker_contexts[j]->batch_counts);
                    DAP_DELETE(l_udp_server->worker_contexts[j]->batch_capacities);
                    DAP_DELETE(l_udp_server->worker_contexts[j]->pipe_write_fds);
                    DAP_DELETE(l_udp_server->worker_contexts[j]);
                }
            }
            DAP_DELETE(l_udp_server->worker_contexts);
            for (uint32_t j = 0; j < l_udp_server->worker_count; j++) {
                pthread_rwlock_destroy(&l_udp_server->worker_locks[j]);
            }
            DAP_DELETE(l_udp_server->worker_locks);
            DAP_DELETE(l_udp_server->sessions_per_worker);
            DAP_DELETE(l_udp_server);
            return NULL;
        }
        
        // Initialize pipe fds to -1
        for (uint32_t j = 0; j < l_udp_server->worker_count; j++) {
            l_ctx->pipe_write_fds[j] = -1;
        }
        
        l_udp_server->worker_contexts[i] = l_ctx;
    }
    
    // Initialize statistics
    atomic_init(&l_udp_server->local_hits, 0);
    atomic_init(&l_udp_server->remote_hits, 0);
    atomic_init(&l_udp_server->session_migrations, 0);
    
    // Initialize listener reference
    l_udp_server->listener_es = NULL;
    
    // Get UDP trans instance
    l_udp_server->trans = dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    if (!l_udp_server->trans) {
        log_it(L_ERROR, "UDP trans not registered");
        // Cleanup worker contexts
        for (uint32_t i = 0; i < l_udp_server->worker_count; i++) {
            if (l_udp_server->worker_contexts[i]) {
                DAP_DELETE(l_udp_server->worker_contexts[i]->batches);
                DAP_DELETE(l_udp_server->worker_contexts[i]->batch_counts);
                DAP_DELETE(l_udp_server->worker_contexts[i]->batch_capacities);
                DAP_DELETE(l_udp_server->worker_contexts[i]->pipe_write_fds);
                DAP_DELETE(l_udp_server->worker_contexts[i]);
            }
            pthread_rwlock_destroy(&l_udp_server->worker_locks[i]);
        }
        DAP_DELETE(l_udp_server->worker_contexts);
        DAP_DELETE(l_udp_server->worker_locks);
        DAP_DELETE(l_udp_server->sessions_per_worker);
        DAP_DELETE(l_udp_server);
        return NULL;
    }

    log_it(L_INFO, "Created UDP server '%s' with %u workers (per-worker hash tables + inter-worker pipes)", 
           a_server_name, l_udp_server->worker_count);
    
    return l_udp_server;
}

/**
 * @brief Start UDP server on specified addresses and ports
 */
int dap_net_trans_udp_server_start(dap_net_trans_udp_server_t *a_udp_server,
                                       const char *a_cfg_section,
                                       const char **a_addrs,
                                       uint16_t *a_ports,
                                       size_t a_count)
{
    if (!a_udp_server || !a_ports || a_count == 0) {
        log_it(L_ERROR, "Invalid parameters for UDP server start");
        return -1;
    }

    if (a_udp_server->server) {
        log_it(L_WARNING, "UDP server already started");
        return -2;
    }

    // Create underlying dap_server_t
    // Set up server callbacks for listener esocket initialization
    dap_events_socket_callbacks_t l_server_callbacks = {
        .new_callback = s_listener_new_callback,   // Initialize shared buffer on listener creation
        .delete_callback = NULL,
        .read_callback = NULL,
        .write_callback = NULL,
        .error_callback = NULL
    };
    
    // UDP client callbacks will be set by dap_stream_add_proc_udp()
    dap_events_socket_callbacks_t l_udp_callbacks = {
        .new_callback = NULL,      // Will be set by dap_stream_add_proc_udp
        .delete_callback = NULL,   // Will be set by dap_stream_add_proc_udp
        .read_callback = NULL,     // Will be set by dap_stream_add_proc_udp
        .write_callback = NULL,    // Will be set by dap_stream_add_proc_udp
        .error_callback = NULL
    };

    a_udp_server->server = dap_server_new(a_cfg_section, &l_server_callbacks, &l_udp_callbacks);
    if (!a_udp_server->server) {
        log_it(L_ERROR, "Failed to create dap_server for UDP");
        return -3;
    }

    // Set UDP server as inheritor (used by s_listener_new_callback)
    a_udp_server->server->_inheritor = a_udp_server;

    // Register UDP stream handlers
    // This sets up all necessary callbacks for UDP processing
    dap_stream_add_proc_udp(a_udp_server->server);
    
    // Override read callback for server listener
    a_udp_server->server->client_callbacks.read_callback = s_udp_server_read_callback;
    
    // Add new_callback for listener initialization (shared buffer setup)
    dap_events_socket_callbacks_t l_listener_callbacks = a_udp_server->server->client_callbacks;
    l_listener_callbacks.new_callback = s_listener_new_callback;

    debug_if(s_debug_more, L_DEBUG, "Registered UDP stream handlers");

    // Initialize inter-worker pipes for batch forwarding
    if (s_init_inter_worker_pipes(a_udp_server) != 0) {
        log_it(L_ERROR, "Failed to initialize inter-worker pipes");
        dap_net_trans_udp_server_stop(a_udp_server);
        return -5;
    }

    // Start listening on all specified address:port pairs
    // Use l_listener_callbacks (client_callbacks + new_callback for shared buffer init)
    for (size_t i = 0; i < a_count; i++) {
        const char *l_addr = (a_addrs && a_addrs[i]) ? a_addrs[i] : "0.0.0.0";
        uint16_t l_port = a_ports[i];

        int l_ret = dap_server_listen_addr_add(a_udp_server->server, l_addr, l_port,
                                                DESCRIPTOR_TYPE_SOCKET_UDP,
                                                &l_listener_callbacks);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to start UDP server on %s:%u", l_addr, l_port);
            dap_net_trans_udp_server_stop(a_udp_server);
            return -4;
        }

        log_it(L_NOTICE, "UDP server '%s' listening on %s:%u",
               a_udp_server->server_name, l_addr, l_port);
    }

    return 0;
}

/**
 * @brief Stop UDP server
 */
void dap_net_trans_udp_server_stop(dap_net_trans_udp_server_t *a_udp_server)
{
    if (!a_udp_server) {
        return;
    }

    // Cleanup all active sessions from per-worker hash tables
    for (uint32_t i = 0; i < a_udp_server->worker_count; i++) {
        pthread_rwlock_wrlock(&a_udp_server->worker_locks[i]);
        
        udp_session_entry_t *l_session, *l_tmp;
        HASH_ITER(hh, a_udp_server->sessions_per_worker[i], l_session, l_tmp) {
            HASH_DEL(a_udp_server->sessions_per_worker[i], l_session);
            
            // CRITICAL: Do NOT access trans_ctx->esocket here!
            // This cleanup runs in server thread, but esocket may be on different worker
            // Let dap_stream_delete_unsafe handle esocket cleanup safely
            
            // Delete stream (will handle esocket cleanup in correct worker context)
            if (l_session->stream) {
                dap_stream_delete_unsafe(l_session->stream);
            }
            
            DAP_DELETE(l_session);
        }
        
        pthread_rwlock_unlock(&a_udp_server->worker_locks[i]);
    }

    if (a_udp_server->server) {
        dap_server_delete(a_udp_server->server);
        a_udp_server->server = NULL;
    }

    log_it(L_INFO, "UDP server '%s' stopped", a_udp_server->server_name);
}

/**
 * @brief Delete UDP server instance
 */
void dap_net_trans_udp_server_delete(dap_net_trans_udp_server_t *a_udp_server)
{
    if (!a_udp_server) {
        return;
    }

    // Ensure server is stopped before deletion
    dap_net_trans_udp_server_stop(a_udp_server);
    
    // Cleanup worker contexts and pipes
    for (uint32_t i = 0; i < a_udp_server->worker_count; i++) {
        udp_worker_context_t *l_ctx = a_udp_server->worker_contexts[i];
        if (l_ctx) {
            // Close pipe fds (esocket cleanup handled by dap_server_delete)
            // Note: read end (l_ctx->pipe_read_fd) is owned by pipe_es which is in reactor
            // Write ends need to be closed manually
            for (uint32_t j = 0; j < a_udp_server->worker_count; j++) {
                if (l_ctx->pipe_write_fds[j] >= 0) {
                    close(l_ctx->pipe_write_fds[j]);
                }
            }
            
            // Free batch buffers
            if (l_ctx->batches) {
                for (uint32_t j = 0; j < a_udp_server->worker_count; j++) {
                    // Free any pending packets in batches
                    if (l_ctx->batches[j]) {
                        for (size_t k = 0; k < l_ctx->batch_counts[j]; k++) {
                            DAP_DELETE(l_ctx->batches[j][k].data);
                        }
                        DAP_DELETE(l_ctx->batches[j]);
                    }
                }
                DAP_DELETE(l_ctx->batches);
            }
            
            DAP_DELETE(l_ctx->batch_counts);
            DAP_DELETE(l_ctx->batch_capacities);
            DAP_DELETE(l_ctx->pipe_write_fds);
            DAP_DELETE(l_ctx);
        }
        
        // Destroy per-worker lock
        pthread_rwlock_destroy(&a_udp_server->worker_locks[i]);
    }
    
    // Free worker arrays
    DAP_DELETE(a_udp_server->worker_contexts);
    DAP_DELETE(a_udp_server->worker_locks);
    DAP_DELETE(a_udp_server->sessions_per_worker);
    
    log_it(L_INFO, "Deleted UDP server: %s", a_udp_server->server_name);
    DAP_DELETE(a_udp_server);
}

