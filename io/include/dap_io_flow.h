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

/**
 * @file dap_io_flow.h
 * @brief Universal IO Flow API for Datagram Protocols
 * 
 * This module provides a universal infrastructure for datagram-based protocols
 * (UDP, RTP, SCTP, QUIC, DCCP) with integrated session/stream lifecycle management.
 * 
 * Key Features:
 * - Socket sharding (SO_REUSEPORT, per-worker sockets)
 * - Per-worker hash tables for flows (zero-lock-contention)
 * - Zero-copy inter-worker packet forwarding via pipes
 * - Integrated session/stream control for dap_stream integration
 * - Protocol-agnostic design with VTable callbacks
 * 
 * Architecture:
 * ```
 * Application Protocol
 *        ↓
 *   dap_stream API
 *        ↓
 *   dap_io_flow Session/Stream Control
 *        ↓
 *   dap_io_flow_server Core (this module)
 *        ↓
 *   dap_events_socket_t + dap_context (reactor)
 *        ↓
 *   Network Socket (kernel)
 * ```
 */

#pragma once

#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include "uthash.h"
#include "dap_events_socket.h"
#include "dap_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for flow types
typedef struct dap_io_flow dap_io_flow_t;
typedef struct dap_io_flow_server dap_io_flow_server_t;
typedef struct dap_io_flow_ops dap_io_flow_ops_t;

/**
 * @brief Load balancing tier enum
 * 
 * Graceful degradation from best (eBPF) to fallback (application-level):
 * - Tier 2 (eBPF): Kernel SO_ATTACH_REUSEPORT_EBPF with FNV-1a hash, full sticky sessions
 * - Tier 1 (Application): User-space FNV-1a hash + cross-worker queue forwarding
 * - Tier 0 (None): Single socket, no load balancing
 * 
 * Note: Classic BPF (SO_ATTACH_BPF) does NOT support SO_REUSEPORT load balancing.
 * Only eBPF (SO_ATTACH_REUSEPORT_EBPF) provides kernel-level sticky sessions.
 */
typedef enum {
    DAP_IO_FLOW_LB_TIER_NONE = 0,        // No load balancing (single socket)
    DAP_IO_FLOW_LB_TIER_APPLICATION = 1, // Application-level via queues
    DAP_IO_FLOW_LB_TIER_EBPF = 2         // eBPF with SO_ATTACH_REUSEPORT_EBPF
} dap_io_flow_lb_tier_t;

/**
 * @brief Data boundary types for different protocols
 * 
 * Defines how protocol reads and processes incoming data:
 * - DATAGRAM: Whole datagrams (UDP, RTP, DCCP) - one recv = one packet
 * - RECORD: Record boundaries (SCTP) - MSG_EOR flag
 * - STREAM: Stream with internal framing (QUIC) - accumulate + parse
 * - CUSTOM: Custom boundary detection via callback
 */
typedef enum {
    DAP_IO_FLOW_BOUNDARY_DATAGRAM,  ///< Whole datagram protocols (UDP, RTP, DCCP)
    DAP_IO_FLOW_BOUNDARY_RECORD,    ///< Record-based (SCTP with MSG_EOR)
    DAP_IO_FLOW_BOUNDARY_STREAM,    ///< Stream with framing (QUIC)
    DAP_IO_FLOW_BOUNDARY_CUSTOM     ///< Custom via get_packet_boundary callback
} dap_io_flow_boundary_type_t;

/**
 * @brief Universal flow structure
 * 
 * Represents a communication flow (connection/session).
 * Flow is identified by remote_addr and managed in per-worker hash tables
 * for zero-lock-contention lookups.
 * 
 * Session/stream integration is provided through generic context pointers
 * that can point to any external session/stream management structures
 * (like dap_stream_session_t, dap_stream_t, or custom implementations).
 */
struct dap_io_flow {
    // Network identity (hash key)
    struct sockaddr_storage remote_addr;    ///< Remote address (hash key)
    socklen_t remote_addr_len;              ///< Address length
    
    // Flow management
    time_t last_activity;                   ///< Last packet timestamp (for timeout)
    uint32_t owner_worker_id;               ///< Owner worker ID
    _Atomic size_t remote_access_count;     ///< Cross-worker access counter
    
    // Protocol configuration
    dap_io_flow_boundary_type_t boundary_type;  ///< Data boundary type
    void *protocol_data;                    ///< Protocol-specific state
    
    // Generic session/stream integration contexts (optional, can be NULL)
    void *session_context;                  ///< External session context (e.g., dap_stream_session_t*)
    void *stream_context;                   ///< External stream context (e.g., dap_stream_t*)
    
    // Hash table handle
    UT_hash_handle hh;
};

/**
 * @brief Flow operations (VTable)
 * 
 * Protocol implementations provide these callbacks to handle protocol-specific
 * logic. External session/stream management can be integrated through these callbacks.
 * 
 * All callbacks are called in the context of the worker thread that owns the flow.
 */
struct dap_io_flow_ops {
    // ===== Packet handling =====
    
    /**
     * @brief Packet/datagram received callback
     * 
     * Called when data arrives. For DATAGRAM - once per packet,
     * for RECORD - once per record, for STREAM - after accumulation.
     * 
     * @param server Flow server instance
     * @param flow Flow instance (or NULL if no flow exists yet)
     * @param data Raw packet data
     * @param size Packet size
     * @param remote_addr Source address
     * @param listener_es Listener socket that received the packet
     */
    void (*packet_received)(dap_io_flow_server_t *server,
                           dap_io_flow_t *flow,
                           const uint8_t *data,
                           size_t size,
                           const struct sockaddr_storage *remote_addr,
                           dap_events_socket_t *listener_es);
    
    // ===== Flow lifecycle =====
    
    /**
     * @brief Flow creation callback
     * 
     * Called when first packet arrives from unknown address.
     * Protocol should allocate its flow structure (embedding dap_io_flow_t)
     * and return pointer to the base.
     * 
     * @param server Flow server instance
     * @param remote_addr Client address
     * @param listener_es Listener socket
     * @return New flow instance or NULL on error
     */
    dap_io_flow_t* (*flow_create)(dap_io_flow_server_t *server,
                                  const struct sockaddr_storage *remote_addr,
                                  dap_events_socket_t *listener_es);
    
    /**
     * @brief Flow destruction callback
     * 
     * Called when flow is being removed (timeout, explicit close, server stop).
     * Protocol should cleanup its user_data and free the flow structure.
     * 
     * @param flow Flow to destroy
     */
    void (*flow_destroy)(dap_io_flow_t *flow);
    
    // ===== Cross-worker forwarding =====
    
    /**
     * @brief Cross-worker forwarding decision callback (optional)
     * 
     * Called when packet arrives on worker A but flow lives on worker B.
     * If returns true, packet is forwarded via pipe (zero-copy).
     * If returns false, packet is dropped.
     * 
     * Default (NULL): always forward
     * 
     * @param flow Flow instance
     * @return true to forward, false to drop
     */
    bool (*should_forward)(dap_io_flow_t *flow);
    
    // ===== Packet boundary detection =====
    
    /**
     * @brief Packet boundary detection for STREAM/CUSTOM types
     * 
     * For STREAM protocols: returns size of next frame/packet in buffer.
     * Returns:
     *  > 0: size of complete packet (packet_received will be called)
     *  0: insufficient data, need to accumulate more
     *  < 0: parsing error, connection will be closed
     * 
     * @param flow Flow instance
     * @param buf Buffer with accumulated data
     * @param buf_size Buffer size
     * @return Packet size, 0, or negative on error
     */
    ssize_t (*get_packet_boundary)(dap_io_flow_t *flow,
                                   const uint8_t *buf,
                                   size_t buf_size);
    
    // ===== Generic session/stream integration callbacks (optional) =====
    
    /**
     * @brief Session creation callback (optional)
     * 
     * Allows protocol to create an external session object and link it to flow.
     * The returned context will be stored in flow->session_context.
     * 
     * Example: Create dap_stream_session_t and return it as void*.
     * 
     * @param flow Flow instance
     * @param session_params Protocol-specific session parameters
     * @return Session context pointer (stored in flow->session_context) or NULL
     */
    void* (*session_create)(dap_io_flow_t *flow,
                           void *session_params);
    
    /**
     * @brief Session close callback (optional)
     * 
     * Called when session should be closed. Protocol should cleanup
     * session context and free resources.
     * 
     * @param flow Flow instance
     * @param session_context Session context (from flow->session_context)
     */
    void (*session_close)(dap_io_flow_t *flow,
                         void *session_context);
    
    /**
     * @brief Stream creation callback (optional)
     * 
     * Allows protocol to create an external stream object and link it to flow.
     * The returned context will be stored in flow->stream_context.
     * 
     * Example: Create dap_stream_t and return it as void*.
     * 
     * @param flow Flow instance
     * @param stream_params Protocol-specific stream parameters
     * @return Stream context pointer (stored in flow->stream_context) or NULL
     */
    void* (*stream_create)(dap_io_flow_t *flow,
                          void *stream_params);
    
    /**
     * @brief Stream write callback (optional)
     * 
     * Called when protocol wants to write data to the stream.
     * 
     * Example: Pass data to dap_stream for processing by channels.
     * 
     * @param flow Flow instance
     * @param stream_context Stream context (from flow->stream_context)
     * @param data Data to write to stream
     * @param size Data size
     * @return Bytes written or negative on error
     */
    ssize_t (*stream_write)(dap_io_flow_t *flow,
                           void *stream_context,
                           const uint8_t *data,
                           size_t size);
    
    /**
     * @brief Stream packet send callback (optional)
     * 
     * Called when stream wants to send a packet through flow.
     * Protocol should handle packet wrapping and call dap_io_flow_send().
     * 
     * Example: Wrap stream packet in protocol header, encrypt, send via flow.
     * 
     * @param flow Flow instance
     * @param stream_context Stream context (from flow->stream_context)
     * @param packet Stream packet data
     * @param packet_size Stream packet size
     * @return Bytes sent or negative on error
     */
    ssize_t (*stream_packet_send)(dap_io_flow_t *flow,
                                 void *stream_context,
                                 const uint8_t *packet,
                                 size_t packet_size);
};

/**
 * @brief IO Flow server - universal server for datagram protocols
 * 
 * Manages multiple flows with socket sharding, per-worker hash tables,
 * and zero-copy inter-worker packet forwarding.
 */
struct dap_io_flow_server {
    char *name;                             ///< Server name (for logging)
    dap_io_flow_ops_t *ops;                 ///< Protocol operations (VTable)
    dap_io_flow_boundary_type_t boundary_type;  ///< Data boundary type
    void *_inheritor;                       ///< User data (protocol-specific server)
    
    dap_server_t *dap_server;               ///< Underlying DAP server
    dap_io_flow_lb_tier_t lb_tier;          ///< Load balancing tier (set by socket creation)
    
    // Per-worker flow hash tables (zero-lock-contention)
    dap_io_flow_t **flows_per_worker;       ///< Flow hash tables [worker_id]
    pthread_rwlock_t *flow_locks_per_worker; ///< RW locks for hash tables
    
    // Inter-worker queues for zero-copy packet forwarding
    dap_events_socket_t ***inter_worker_queues;  ///< Queue outputs [src_worker][dst_worker]
    dap_events_socket_t **queue_inputs;     ///< Queue inputs [worker_id]
    
    // Synchronization for graceful cleanup
    pthread_mutex_t cleanup_mutex;          ///< Mutex for cleanup synchronization
    pthread_cond_t cleanup_cond;            ///< Condition variable for cleanup completion
    _Atomic uint32_t pending_cleanups;      ///< Number of pending cleanup operations (scheduled)
    _Atomic uint32_t active_callbacks;      ///< Number of callbacks CURRENTLY executing (not yet returned)
    
    // Statistics
    _Atomic size_t local_hits;              ///< Local flow lookups
    _Atomic size_t remote_hits;             ///< Remote flow lookups
    _Atomic size_t cross_worker_packets;    ///< Cross-worker forwarded packets
    
    bool is_running;                        ///< Server is running
};

// =============================================================================
// Public API - Core
// =============================================================================

/**
 * @brief Create IO flow server
 * 
 * Allocates server structure, initializes per-worker hash tables,
 * sets up inter-worker pipes for zero-copy forwarding.
 * 
 * @param a_name Server name (for logging/debugging)
 * @param a_ops Protocol operations (VTable)
 * @param a_boundary_type Data boundary type
 * @return Server instance or NULL on error
 */
dap_io_flow_server_t* dap_io_flow_server_new(
    const char *a_name,
    dap_io_flow_ops_t *a_ops,
    dap_io_flow_boundary_type_t a_boundary_type);

/**
 * @brief Start listening on address:port
 * 
 * Creates sharded listeners (one per worker thread) with SO_REUSEPORT.
 * Kernel automatically distributes incoming packets across workers for maximum throughput.
 * 
 * @param a_server Server instance
 * @param a_addr Address to bind (NULL = INADDR_ANY)
 * @param a_port Port to bind
 * @return 0 on success, negative on error
 */
int dap_io_flow_server_listen(
    dap_io_flow_server_t *a_server,
    const char *a_addr,
    uint16_t a_port);

/**
 * @brief Stop IO flow server
 * 
 * Stops listening, closes all sockets, but keeps flows alive.
 * Call dap_io_flow_server_delete() to fully cleanup.
 * 
 * @param a_server Server instance
 */
void dap_io_flow_server_stop(dap_io_flow_server_t *a_server);

/**
 * @brief Delete IO flow server and cleanup all resources
 * 
 * Calls flow_destroy for all active flows, frees hash tables,
 * closes pipes, deallocates server structure.
 * 
 * @param a_server Server instance
 */
void dap_io_flow_server_delete(dap_io_flow_server_t *a_server);

/**
 * @brief Set inheritor (user data) for flow server
 * 
 * This allows protocol implementations to associate custom data with the server.
 * 
 * @param a_server Flow server
 * @param a_inheritor User data pointer
 */
void dap_io_flow_server_set_inheritor(dap_io_flow_server_t *a_server, void *a_inheritor);

/**
 * @brief Get inheritor (user data) from flow server
 * 
 * @param a_server Flow server
 * @return User data pointer or NULL
 */
void* dap_io_flow_server_get_inheritor(dap_io_flow_server_t *a_server);

/**
 * @brief Find flow by remote address
 * 
 * Performs optimistic lookup: checks local worker first (fast path),
 * then scans other workers if needed (slow path). Thread-safe.
 * 
 * @param a_server Server instance
 * @param a_remote_addr Client address to lookup
 * @return Flow instance or NULL if not found
 */
dap_io_flow_t* dap_io_flow_find(
    dap_io_flow_server_t *a_server,
    const struct sockaddr_storage *a_remote_addr);

/**
 * @brief Delete all flows for this server
 * 
 * Iterates all worker hash tables and calls flow_destroy callback for each flow.
 * This ensures proper cleanup of flows before server deletion, preventing
 * use-after-free when flows hold pointers to server resources (e.g., listener_es).
 * 
 * CRITICAL: Must be called BEFORE dap_io_flow_server_delete() to avoid dangling
 * pointers in flows that might still be referenced (e.g., by retransmit timers).
 * 
 * Thread-safe: Acquires write locks on all worker hash tables.
 * 
 * @param a_server Server instance
 * @return Number of flows deleted, or -1 on error
 */
int dap_io_flow_delete_all_flows(dap_io_flow_server_t *a_server);

/**
 * @brief Send data to flow's remote address
 * 
 * Queues data for sending via appropriate listener socket.
 * Handles cross-worker sending automatically.
 * 
 * @param a_flow Flow instance
 * @param a_data Data to send
 * @param a_size Data size
 * @return Bytes sent or negative on error
 */
int dap_io_flow_send(
    dap_io_flow_t *a_flow,
    const uint8_t *a_data,
    size_t a_size);

/**
 * @brief Get server statistics
 * 
 * @param a_server Server instance
 * @param[out] a_local_hits Local flow lookups
 * @param[out] a_remote_hits Remote flow lookups
 * @param[out] a_cross_worker_packets Cross-worker forwarded packets
 */
void dap_io_flow_server_get_stats(
    dap_io_flow_server_t *a_server,
    size_t *a_local_hits,
    size_t *a_remote_hits,
    size_t *a_cross_worker_packets);

#ifdef __cplusplus
}
#endif

