/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Contributors:
 * Copyright (c) 2017-2025 Demlabs Ltd <https://demlabs.net>
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "dap_net_trans.h"
#include "dap_server.h"
#include "dap_events_socket.h"

/**
 * @file dap_net_trans_udp_stream.h
 * @brief UDP Trans Adapter for DAP Stream Protocol
 * 
 * This module implements a UDP-based trans layer for DAP Stream,
 * providing connectionless datagram communication. Unlike TCP/HTTP,
 * UDP offers low latency at the cost of reliability (no guaranteed delivery).
 * 
 * **Features:**
 * - Connectionless datagram trans
 * - Low latency (no connection establishment)
 * - No built-in reliability (best-effort delivery)
 * - NAT-friendly with proper configuration
 * - Stateless operation (no connection tracking)
 * 
 * **Use Cases:**
 * - Low-latency applications
 * - Real-time data streaming
 * - Applications tolerant to packet loss
 * - Bypassing TCP-based DPI
 * 
 * **Limitations:**
 * - No guaranteed delivery (packets can be lost)
 * - No ordering guarantees
 * - No flow control
 * - No congestion control
 * - MTU limitations (typically 1500 bytes)
 * 
 * **Architecture:**
 * ```
 * Application
 *     ↓
 * DAP Stream
 *     ↓
 * Trans Abstraction Layer
 *     ↓
 * UDP Trans Adapter ← This module
 *     ↓
 * UDP Socket (dap_events_socket_t)
 *     ↓
 * Network (UDP/IP)
 * ```
 * 
 * **Protocol Stack:**
 * ```
 * +---------------------------+
 * | DAP Stream Packet         |
 * +---------------------------+
 * | DSHP Handshake (TLV)      |
 * +---------------------------+
 * | UDP Trans Header      |
 * +---------------------------+
 * | UDP Datagram              |
 * +---------------------------+
 * ```
 * 
 * **UDP Trans Header Format:**
 * ```
 * 0                   1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Version (4)  |     Type (8)      |         Length (16)         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Sequence Number (32)                   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Session ID (64)                        |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         Payload ...                           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * ```
 * 
 * **Packet Types:**
 * - HANDSHAKE (0x01) - Encryption handshake
 * - SESSION_CREATE (0x02) - Session establishment
 * - DATA (0x03) - Stream data
 * - KEEPALIVE (0x04) - Connection heartbeat
 * - CLOSE (0x05) - Session termination
 * 
 * @see dap_stream_trans.h
 * @see dap_stream_handshake.h
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UDP trans packet types
 */
typedef enum dap_stream_trans_udp_pkt_type {
    DAP_STREAM_UDP_PKT_HANDSHAKE       = 0x01,  ///< Encryption handshake packet
    DAP_STREAM_UDP_PKT_SESSION_CREATE  = 0x02,  ///< Session creation packet
    DAP_STREAM_UDP_PKT_DATA            = 0x03,  ///< Stream data packet
    DAP_STREAM_UDP_PKT_KEEPALIVE       = 0x04,  ///< Keepalive heartbeat
    DAP_STREAM_UDP_PKT_CLOSE           = 0x05   ///< Connection close packet
} dap_stream_trans_udp_pkt_type_t;

/**
 * @brief UDP trans packet header (18 bytes)
 * 
 * This header is prepended to all UDP packets to enable
 * stateless operation and packet identification.
 */
typedef struct dap_stream_trans_udp_header {
    uint8_t version;        ///< Protocol version (currently 1)
    uint8_t type;           ///< Packet type (dap_stream_trans_udp_pkt_type_t)
    uint16_t length;        ///< Payload length (network byte order)
    uint32_t seq_num;       ///< Sequence number (network byte order)
    uint64_t session_id;    ///< Session ID (network byte order)
} DAP_ALIGN_PACKED dap_stream_trans_udp_header_t;

/**
 * @brief UDP trans configuration
 */
typedef struct dap_stream_trans_udp_config {
    uint16_t max_packet_size;   ///< Maximum UDP packet size (default 1400)
    uint32_t keepalive_ms;      ///< Keepalive interval in milliseconds
    bool enable_checksum;       ///< Enable payload checksum validation
    bool allow_fragmentation;   ///< Allow IP fragmentation (not recommended)
} dap_stream_trans_udp_config_t;

/**
 * @brief UDP trans private data (per-transport, shared)
 */
typedef struct dap_stream_trans_udp_private {
    dap_server_t *server;               ///< UDP server instance
    dap_stream_trans_udp_config_t config;  ///< Configuration
    void *user_data;                    ///< User-defined data
} dap_stream_trans_udp_private_t;

/**
 * @brief UDP stream context (per-stream, stored in trans_ctx->_inheritor)
 * 
 * Each stream has its own UDP context with unique session_id, seq_num, etc.
 * This allows multiple concurrent UDP connections to share one transport.
 */
typedef struct dap_net_trans_udp_ctx {
    uint64_t session_id;                ///< Session ID for this stream
    uint32_t seq_num;                   ///< Sequence number for this stream
    struct sockaddr_storage remote_addr; ///< Remote peer address
    socklen_t remote_addr_len;          ///< Remote address length
    dap_enc_key_t *alice_key;           ///< Client: Alice's KEM key for handshake
    dap_enc_key_t *handshake_key;       ///< HANDSHAKE key (from Kyber512 shared secret) - used to encrypt/decrypt session key
    void *client_ctx;                   ///< dap_client_t* from stage_prepare (if any)
    dap_stream_t *stream;               ///< Associated stream (back-reference)
} dap_net_trans_udp_ctx_t;

/**
 * @brief Register UDP trans adapter
 * 
 * Registers the UDP trans implementation with the
 * trans registry. Must be called during initialization.
 * 
 * @return 0 on success, negative error code on failure
 * 
 * @note Call this after dap_stream_trans_registry_init()
 * 
 * Example:
 * ```c
 * dap_stream_trans_registry_init();
 * dap_stream_trans_http_register();
 * dap_stream_trans_udp_register();  // Register UDP
 * ```
 */
int dap_net_trans_udp_stream_register(void);

/**
 * @brief Unregister UDP trans adapter
 * 
 * Removes the UDP trans from the registry.
 * Call during cleanup.
 * 
 * @return 0 on success, negative error code on failure
 */
int dap_net_trans_udp_stream_unregister(void);

/**
 * @brief Create UDP trans configuration with defaults
 * 
 * Creates a UDP configuration structure with sensible defaults:
 * - max_packet_size: 1400 bytes (safe for most networks)
 * - keepalive_ms: 30000 (30 seconds)
 * - enable_checksum: true
 * - allow_fragmentation: false
 * 
 * @return Configuration structure with defaults
 */
dap_stream_trans_udp_config_t dap_stream_trans_udp_config_default(void);

/**
 * @brief Set UDP trans configuration
 * 
 * Updates the configuration for an existing UDP trans.
 * 
 * @param a_trans Trans instance
 * @param a_config New configuration
 * @return 0 on success, negative error code on failure
 */
int dap_stream_trans_udp_set_config(dap_net_trans_t *a_trans,
                                        const dap_stream_trans_udp_config_t *a_config);

/**
 * @brief Get UDP trans configuration
 * 
 * Retrieves the current configuration.
 * 
 * @param a_trans Trans instance
 * @param[out] a_config Configuration structure to fill
 * @return 0 on success, negative error code on failure
 */
int dap_stream_trans_udp_get_config(dap_net_trans_t *a_trans,
                                        dap_stream_trans_udp_config_t *a_config);

/**
 * @brief Check if a stream is using UDP trans
 * 
 * Helper function to determine if a stream is using UDP.
 * 
 * @param a_stream Stream instance
 * @return true if using UDP trans, false otherwise
 */
bool dap_stream_trans_is_udp(const dap_stream_t *a_stream);

/**
 * @brief Get UDP server from trans
 * 
 * Retrieves the underlying UDP server instance.
 * Useful for low-level operations.
 * 
 * @param a_stream Stream instance
 * @return UDP server instance or NULL if not UDP trans
 */
dap_server_t *dap_stream_trans_udp_get_server(const dap_stream_t *a_stream);

/**
 * @brief Get UDP event socket from trans
 * 
 * Retrieves the underlying event socket.
 * 
 * @param a_stream Stream instance
 * @return Event socket or NULL if not UDP trans
 */
dap_events_socket_t *dap_stream_trans_udp_get_esocket(const dap_stream_t *a_stream);

/**
 * @brief Get current session ID
 * 
 * Returns the 64-bit session ID for the UDP connection.
 * 
 * @param a_stream Stream instance
 * @return Session ID or 0 if not UDP trans
 */
uint64_t dap_stream_trans_udp_get_session_id(const dap_stream_t *a_stream);

/**
 * @brief Get current sequence number
 * 
 * Returns the current packet sequence number.
 * 
 * @param a_stream Stream instance
 * @return Sequence number or 0 if not UDP trans
 */
uint32_t dap_stream_trans_udp_get_seq_num(const dap_stream_t *a_stream);

/**
 * @brief Set remote peer address
 * 
 * Manually sets the remote peer address for outgoing packets.
 * Normally determined automatically during connection.
 * 
 * @param a_trans Trans instance
 * @param a_addr Remote address structure
 * @param a_addr_len Address length
 * @return 0 on success, negative error code on failure
 */
int dap_stream_trans_udp_set_remote_addr(dap_net_trans_t *a_trans,
                                              const struct sockaddr *a_addr,
                                              socklen_t a_addr_len);

/**
 * @brief Get remote peer address
 * 
 * Retrieves the remote peer address.
 * 
 * @param a_trans Trans instance
 * @param[out] a_addr Buffer to fill with address
 * @param[out] a_addr_len Address length
 * @return 0 on success, negative error code on failure
 */
int dap_stream_trans_udp_get_remote_addr(dap_net_trans_t *a_trans,
                                              struct sockaddr *a_addr,
                                              socklen_t *a_addr_len);

/**
 * @brief UDP read callback for processing incoming packets
 * 
 * This callback processes UDP packets from buf_in buffer. It is used by both:
 * - UDP client esockets (direct physical socket)
 * - UDP server virtual esockets (demultiplexed sessions)
 * 
 * @param a_es Event socket with data in buf_in
 * @param a_arg Callback argument (unused)
 */
void dap_stream_trans_udp_read_callback(dap_events_socket_t *a_es, void *a_arg);


#ifdef __cplusplus
}
#endif

