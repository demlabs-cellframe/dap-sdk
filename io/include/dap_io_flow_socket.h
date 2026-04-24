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
 * @file dap_io_flow_socket.h
 * @brief Socket utilities for IO Flow
 * 
 * This module provides low-level socket utilities for dap_io_flow:
 * - UDP send/receive with sockaddr management
 * - Zero-copy cross-worker packet forwarding via pipes
 * - Socket sharding utilities (SO_REUSEPORT)
 * - Address comparison and hashing
 * 
 * These functions complement the generic dap_io_flow API for
 * socket-specific operations.
 */

#pragma once

#if defined(DAP_OS_WINDOWS) || defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif
#include <stdint.h>
#include <stdbool.h>
#include "dap_events_socket.h"
#include "dap_server.h"
#include "dap_io_flow.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// INITIALIZATION
// =============================================================================

/**
 * @brief Initialize dap_io_flow_socket module
 * @details Reads debug flags from config
 * @return 0 on success
 */
int dap_io_flow_socket_init(void);

// =============================================================================
// TIER CONTROL (for testing)
// =============================================================================

/**
 * @brief Force a specific load balancing tier (for testing)
 * @param a_tier Tier to force (DAP_IO_FLOW_LB_TIER_*), or -1 to auto-detect
 * @note This affects all subsequent server creations until reset
 */
void dap_io_flow_set_forced_tier(int a_tier);

/**
 * @brief Get current forced tier setting
 * @return Forced tier or -1 if auto-detect
 */
int dap_io_flow_get_forced_tier(void);

/**
 * @brief Get human-readable tier name
 * @param a_tier Tier enum value
 * @return Tier name string
 */
const char* dap_io_flow_tier_name(dap_io_flow_lb_tier_t a_tier);

/**
 * @brief Deinitialize dap_io_flow_socket module
 */
void dap_io_flow_socket_deinit(void);

// =============================================================================
// SOCKET SEND/RECEIVE API
// =============================================================================

/**
 * @brief Send datagram to specific address (thread-safe)
 * 
 * Updates esocket's addr_storage and queues data for sending.
 * The reactor will call sendto() with the stored address.
 * 
 * Thread-safe: Can be called from any worker thread.
 * If called from different worker than esocket's owner, uses
 * dap_worker_exec_callback_on for cross-thread safety.
 * 
 * @param a_es Listener esocket (UDP socket)
 * @param a_data Data to send
 * @param a_size Data size
 * @param a_addr Destination address
 * @param a_addr_len Address length
 * @return Bytes queued for sending, or negative on error
 */
int dap_io_flow_socket_send_to(
    dap_io_flow_server_t *a_server,
    dap_events_socket_t *a_es,
    const uint8_t *a_data,
    size_t a_size,
    const struct sockaddr_storage *a_addr,
    socklen_t a_addr_len);

/**
 * @brief Forward packet pointer to another worker via pipe (ZERO-COPY)
 * 
 * Writes packet pointer directly to pipe's buf_out. The reactor
 * will flush buf_out to the pipe when ready. Target worker reads
 * pointer from its pipe and processes the packet.
 * 
 * Ownership: Packet ownership transfers to target worker.
 * Caller must NOT free the packet after this call.
 * 
 * @param a_pipe_es Pipe write-end esocket (to target worker)
 * @param a_packet_ptr Pointer to packet structure
 * @return 0 on success, negative on error
 */
int dap_io_flow_socket_forward_packet(
    dap_events_socket_t *a_pipe_es,
    void *a_packet_ptr);

/**
 * @brief Create sharded listeners (SO_REUSEPORT)
 * 
 * Creates N sockets (one per worker), all bound to the same
 * address:port with SO_REUSEPORT. Kernel distributes incoming
 * packets across sockets for maximum throughput.
 * 
 * Each socket is added to its respective worker thread.
 * SO_INCOMING_CPU is set to hint the kernel for CPU affinity.
 * 
 * @param a_server Server instance
 * @param a_addr Address to bind (NULL = INADDR_ANY)
 * @param a_port Port to bind
 * @param a_socket_type Socket type (SOCK_DGRAM, SOCK_SEQPACKET, etc.)
 * @param a_protocol Protocol (IPPROTO_UDP, IPPROTO_SCTP, etc.)
 * @param a_callbacks Callbacks for listener sockets
 * @return 0 on success, negative on error
 */
int dap_io_flow_socket_create_sharded_listeners(
    dap_server_t *a_server,
    const char *a_addr,
    uint16_t a_port,
    int a_socket_type,
    int a_protocol,
    dap_events_socket_callbacks_t *a_callbacks,
    dap_io_flow_lb_tier_t *a_lb_tier_out);  ///< [out] Detected load balancing tier

/**
 * @brief Get esocket's remote address
 * 
 * Returns the address from last received packet (stored in addr_storage).
 * Only valid for UDP sockets after recvfrom.
 * 
 * @param a_es Events socket
 * @param[out] a_addr Remote address (will be filled)
 * @param[out] a_addr_len Address length (will be filled)
 * @return 0 on success, negative on error
 */
int dap_io_flow_socket_get_remote_addr(
    dap_events_socket_t *a_es,
    struct sockaddr_storage *a_addr,
    socklen_t *a_addr_len);

/**
 * @brief Format sockaddr_storage to string (for logging)
 * 
 * Converts address to "IP:port" format.
 * Thread-safe (uses thread-local buffer).
 * 
 * @param a_addr Address to format
 * @return String representation (in thread-local buffer)
 */
const char* dap_io_flow_socket_addr_to_string(
    const struct sockaddr_storage *a_addr);

/**
 * @brief Compare two sockaddr_storage addresses
 * 
 * Compares IP address and port. Supports both IPv4 and IPv6.
 * Used for flow lookups by remote address.
 * 
 * @param a First address
 * @param b Second address
 * @return true if addresses are equal, false otherwise
 */
bool dap_io_flow_socket_addr_equal(
    const struct sockaddr_storage *a,
    const struct sockaddr_storage *b);

/**
 * @brief Hash sockaddr_storage for hash tables
 * 
 * Computes hash from IP address and port.
 * Used with uthash for flow lookups.
 * 
 * @param a_addr Address to hash
 * @return Hash value
 */
uint32_t dap_io_flow_socket_addr_hash(
    const struct sockaddr_storage *a_addr);

#ifdef __cplusplus
}
#endif
