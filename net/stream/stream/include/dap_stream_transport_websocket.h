/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file dap_stream_transport_websocket.h
 * @brief WebSocket Transport Adapter for DAP Stream Protocol
 * 
 * This module implements a WebSocket-based transport layer for DAP Stream,
 * providing HTTP-upgrade WebSocket communication for DPI bypass and
 * firewall traversal.
 * 
 * **Features:**
 * - WebSocket Protocol (RFC 6455) implementation
 * - HTTP upgrade handshake
 * - Frame-based bidirectional communication
 * - Automatic fragmentation for large messages
 * - Ping/Pong heartbeat mechanism
 * - Text and binary frame support
 * - Client and server role support
 * 
 * **Use Cases:**
 * - Bypassing HTTP-only firewalls
 * - NAT traversal
 * - DPI evasion (looks like legitimate WebSocket traffic)
 * - Browser-compatible communication
 * - Reverse proxy friendly
 * 
 * **WebSocket Frame Format:**
 * ```
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-------+-+-------------+-------------------------------+
 * |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 * |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
 * |N|V|V|V|       |S|             |   (if payload len==126/127)   |
 * | |1|2|3|       |K|             |                               |
 * +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
 * |     Extended payload length continued, if payload len == 127  |
 * + - - - - - - - - - - - - - - - +-------------------------------+
 * |                               |Masking-key, if MASK set to 1  |
 * +-------------------------------+-------------------------------+
 * | Masking-key (continued)       |          Payload Data         |
 * +-------------------------------- - - - - - - - - - - - - - - - +
 * :                     Payload Data continued ...                :
 * + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
 * |                     Payload Data continued ...                |
 * +---------------------------------------------------------------+
 * ```
 * 
 * **Architecture:**
 * ```
 * Application
 *     ↓
 * DAP Stream
 *     ↓
 * Transport Abstraction Layer
 *     ↓
 * WebSocket Transport ← This module
 *     ↓
 * HTTP Upgrade (for handshake)
 *     ↓
 * TCP Socket (dap_events_socket_t)
 *     ↓
 * Network (TCP/IP)
 * ```
 * 
 * @see dap_stream_transport.h
 * @see RFC 6455 - The WebSocket Protocol
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dap_stream_transport.h"
#include "dap_events_socket.h"
#include "dap_http_client.h"
#include "dap_timerfd.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// WebSocket Protocol Constants
// ============================================================================

/// WebSocket protocol version (RFC 6455)
#define DAP_WS_PROTOCOL_VERSION 13

/// WebSocket opcode values
typedef enum dap_ws_opcode {
    DAP_WS_OPCODE_CONTINUATION  = 0x00,  ///< Continuation frame
    DAP_WS_OPCODE_TEXT          = 0x01,  ///< Text frame (UTF-8)
    DAP_WS_OPCODE_BINARY        = 0x02,  ///< Binary frame
    DAP_WS_OPCODE_CLOSE         = 0x08,  ///< Connection close
    DAP_WS_OPCODE_PING          = 0x09,  ///< Ping heartbeat
    DAP_WS_OPCODE_PONG          = 0x0A   ///< Pong response
} dap_ws_opcode_t;

/// WebSocket close status codes (RFC 6455)
typedef enum dap_ws_close_code {
    DAP_WS_CLOSE_NORMAL             = 1000,  ///< Normal closure
    DAP_WS_CLOSE_GOING_AWAY         = 1001,  ///< Endpoint is going away
    DAP_WS_CLOSE_PROTOCOL_ERROR     = 1002,  ///< Protocol error
    DAP_WS_CLOSE_UNSUPPORTED        = 1003,  ///< Unsupported data type
    DAP_WS_CLOSE_NO_STATUS          = 1005,  ///< No status received (reserved)
    DAP_WS_CLOSE_ABNORMAL           = 1006,  ///< Abnormal closure (reserved)
    DAP_WS_CLOSE_INVALID_PAYLOAD    = 1007,  ///< Invalid frame payload
    DAP_WS_CLOSE_POLICY_VIOLATION   = 1008,  ///< Policy violation
    DAP_WS_CLOSE_TOO_LARGE          = 1009,  ///< Message too large
    DAP_WS_CLOSE_EXTENSION_REQUIRED = 1010,  ///< Extension negotiation failed
    DAP_WS_CLOSE_UNEXPECTED         = 1011   ///< Unexpected condition
} dap_ws_close_code_t;

// ============================================================================
// Configuration Structures
// ============================================================================

/**
 * @brief WebSocket transport configuration
 */
typedef struct dap_stream_transport_ws_config {
    uint32_t max_frame_size;         ///< Maximum WebSocket frame size (bytes)
    uint32_t ping_interval_ms;       ///< Ping interval (milliseconds)
    uint32_t pong_timeout_ms;        ///< Pong response timeout (milliseconds)
    bool enable_compression;         ///< Enable permessage-deflate extension
    bool client_mask_frames;         ///< Client-to-server frame masking (RFC 6455 требует true)
    bool server_mask_frames;         ///< Server-to-client frame masking (обычно false)
    char *subprotocol;               ///< WebSocket subprotocol (e.g., "dap-stream")
    char *origin;                    ///< Origin header for client connections
} dap_stream_transport_ws_config_t;

/**
 * @brief WebSocket frame header (RFC 6455)
 */
typedef struct __attribute__((packed)) dap_ws_frame_header {
    uint8_t opcode : 4;              ///< Opcode (4 bits)
    uint8_t rsv3 : 1;                ///< Reserved bit 3
    uint8_t rsv2 : 1;                ///< Reserved bit 2
    uint8_t rsv1 : 1;                ///< Reserved bit 1 (compression)
    uint8_t fin : 1;                 ///< FIN bit (final fragment)
    
    uint8_t payload_len : 7;         ///< Payload length (7 bits)
    uint8_t mask : 1;                ///< Mask bit
    
    // Extended payload length and masking key follow dynamically
} dap_ws_frame_header_t;

/**
 * @brief WebSocket connection state
 */
typedef enum dap_ws_state {
    DAP_WS_STATE_CONNECTING     = 0,  ///< HTTP upgrade in progress
    DAP_WS_STATE_OPEN           = 1,  ///< WebSocket connection established
    DAP_WS_STATE_CLOSING        = 2,  ///< Close frame sent, waiting for response
    DAP_WS_STATE_CLOSED         = 3   ///< Connection closed
} dap_ws_state_t;

/**
 * @brief WebSocket transport private data
 */
typedef struct dap_stream_transport_ws_private {
    dap_stream_transport_ws_config_t config;  ///< Configuration
    dap_ws_state_t state;                     ///< Connection state
    
    // HTTP upgrade
    char *upgrade_path;                       ///< WebSocket upgrade path (e.g., "/stream")
    char *sec_websocket_key;                  ///< Client's Sec-WebSocket-Key
    char *sec_websocket_accept;               ///< Server's Sec-WebSocket-Accept
    
    // Frame processing
    uint8_t *frame_buffer;                    ///< Buffer for incoming frame assembly
    size_t frame_buffer_size;                 ///< Allocated buffer size
    size_t frame_buffer_used;                 ///< Used buffer space
    uint64_t payload_remaining;               ///< Bytes remaining in current frame
    bool is_fragmented;                       ///< Currently receiving fragmented message
    dap_ws_opcode_t fragment_opcode;          ///< Opcode of first fragment
    
    // Masking
    uint32_t client_mask_key;                 ///< Current masking key for client frames
    
    // Heartbeat
    dap_timerfd_t *ping_timer;                ///< Ping interval timer
    int64_t last_pong_time;                   ///< Timestamp of last pong received
    
    // Events socket
    dap_events_socket_t *esocket;             ///< Underlying events socket
    dap_http_client_t *http_client;           ///< HTTP client (for upgrade)
    
    // Statistics
    uint64_t frames_sent;                     ///< Total frames sent
    uint64_t frames_received;                 ///< Total frames received
    uint64_t bytes_sent;                      ///< Total bytes sent
    uint64_t bytes_received;                  ///< Total bytes received
} dap_stream_transport_ws_private_t;

// ============================================================================
// Registration Functions
// ============================================================================

/**
 * @brief Register WebSocket transport adapter
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_websocket_register(void);

/**
 * @brief Unregister WebSocket transport adapter
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_websocket_unregister(void);

// ============================================================================
// Configuration Functions
// ============================================================================

/**
 * @brief Get default WebSocket transport configuration
 * @return Default configuration structure
 */
dap_stream_transport_ws_config_t dap_stream_transport_ws_config_default(void);

/**
 * @brief Set WebSocket transport configuration
 * @param a_transport WebSocket transport instance
 * @param a_config Configuration structure
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_ws_set_config(dap_stream_transport_t *a_transport,
                                        const dap_stream_transport_ws_config_t *a_config);

/**
 * @brief Get WebSocket transport configuration
 * @param a_transport WebSocket transport instance
 * @param a_config Output configuration structure
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_ws_get_config(dap_stream_transport_t *a_transport,
                                        dap_stream_transport_ws_config_t *a_config);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Check if stream is using WebSocket transport
 * @param a_stream Stream to check
 * @return true if WebSocket transport, false otherwise
 */
bool dap_stream_transport_is_websocket(const dap_stream_t *a_stream);

/**
 * @brief Get WebSocket private data from stream
 * @param a_stream Stream instance
 * @return WebSocket private data or NULL
 */
dap_stream_transport_ws_private_t* dap_stream_transport_ws_get_private(dap_stream_t *a_stream);

/**
 * @brief Send WebSocket close frame
 * @param a_stream Stream to close
 * @param a_code Close status code
 * @param a_reason Close reason string (optional, can be NULL)
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_ws_send_close(dap_stream_t *a_stream, 
                                        dap_ws_close_code_t a_code,
                                        const char *a_reason);

/**
 * @brief Send WebSocket ping frame
 * @param a_stream Stream to send ping on
 * @param a_payload Ping payload (optional, can be NULL)
 * @param a_payload_size Ping payload size (max 125 bytes)
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_ws_send_ping(dap_stream_t *a_stream,
                                       const void *a_payload,
                                       size_t a_payload_size);

/**
 * @brief Get WebSocket connection statistics
 * @param a_stream Stream instance
 * @param a_frames_sent Output: frames sent
 * @param a_frames_received Output: frames received
 * @param a_bytes_sent Output: bytes sent
 * @param a_bytes_received Output: bytes received
 * @return 0 on success, negative error code on failure
 */
int dap_stream_transport_ws_get_stats(const dap_stream_t *a_stream,
                                       uint64_t *a_frames_sent,
                                       uint64_t *a_frames_received,
                                       uint64_t *a_bytes_sent,
                                       uint64_t *a_bytes_received);

#ifdef __cplusplus
}
#endif

