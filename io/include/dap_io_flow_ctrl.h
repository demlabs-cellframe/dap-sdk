/*
 * Authors:
 * Dmitriy Gerasimov <dmitriy.gerasimov@demlabs.net>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright (c) 2024
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

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "dap_common.h"
#include "dap_timerfd.h"

/**
 * @file dap_io_flow_ctrl.h
 * @brief Flow Control for Datagram Protocols (UDP, SCTP)
 * 
 * Provides reliable delivery over unreliable datagram protocols:
 * - Packet tracking and retransmission (ACK-based)
 * - Sequence tracking and reordering (out-of-order delivery)
 * - Keep-alive mechanism (connection liveness)
 * 
 * All mechanisms are optional and can be enabled/disabled dynamically via flags.
 */

// Forward declarations
typedef struct dap_io_flow dap_io_flow_t;
typedef struct dap_io_flow_ctrl dap_io_flow_ctrl_t;

/**
 * @brief Flow control flags (dynamic enable/disable)
 */
typedef enum {
    DAP_IO_FLOW_CTRL_NONE           = 0x00,   ///< No flow control (raw datagram)
    DAP_IO_FLOW_CTRL_RETRANSMIT     = 0x01,   ///< Enable retransmission on packet loss
    DAP_IO_FLOW_CTRL_REORDER        = 0x02,   ///< Enable reordering buffer for out-of-order packets
    DAP_IO_FLOW_CTRL_KEEPALIVE      = 0x04,   ///< Enable keep-alive pings
    DAP_IO_FLOW_CTRL_CONGESTION     = 0x08,   ///< Enable congestion control (future)
    
    // Common presets
    DAP_IO_FLOW_CTRL_RELIABLE       = 0x03,   ///< RETRANSMIT | REORDER (TCP-like reliable)
    DAP_IO_FLOW_CTRL_VPN            = 0x05,   ///< RETRANSMIT | KEEPALIVE (VPN mode, no reorder)
    DAP_IO_FLOW_CTRL_FULL           = 0x0F,   ///< All mechanisms enabled
} dap_io_flow_ctrl_flags_t;

/**
 * @brief Flow control packet types
 */
typedef enum {
    DAP_IO_FLOW_PKT_DATA            = 0x00,   ///< Regular data packet
    DAP_IO_FLOW_PKT_KEEPALIVE       = 0x01,   ///< Keep-alive ping
    DAP_IO_FLOW_PKT_ACK_ONLY        = 0x02,   ///< Pure ACK (no payload)
    DAP_IO_FLOW_PKT_RETRANSMIT      = 0x04,   ///< Retransmitted packet
} dap_io_flow_pkt_type_t;

/**
 * @brief Flow control header (prepended to all packets)
 * 
 * Size: 24 bytes (aligned)
 */
typedef struct dap_io_flow_header {
    uint64_t seq_num;         ///< Sequence number of this packet
    uint64_t ack_seq;         ///< ACK for highest received in-order sequence
    uint32_t timestamp_ms;    ///< Timestamp for RTT calculation
    uint8_t flags;            ///< Packet type flags
    uint8_t reserved[3];      ///< Reserved for future use
} __attribute__((packed)) dap_io_flow_header_t;

/**
 * @brief Keep-alive callback
 * 
 * Called when keep-alive timeout expires (connection considered dead).
 * Protocol can decide what to do: close, reconnect, notify user, etc.
 * 
 * @param a_flow Flow that timed out
 * @param a_arg User argument
 */
typedef void (*dap_io_flow_keepalive_cb_t)(dap_io_flow_t *a_flow, void *a_arg);

/**
 * @brief Configuration for flow control
 */
typedef struct dap_io_flow_ctrl_config {
    // Retransmission settings
    uint32_t retransmit_timeout_ms;   ///< Timeout before retransmit (default: 200ms)
    uint32_t max_retransmit_count;    ///< Max retries before giving up (default: 5)
    size_t send_window_size;          ///< Send window size in packets (default: 64)
    
    // Reordering settings
    size_t recv_window_size;          ///< Receive buffer size in packets (default: 128)
    uint32_t max_out_of_order_delay_ms;  ///< Max time to buffer out-of-order (default: 1000ms)
    
    // Keep-alive settings
    uint32_t keepalive_interval_ms;   ///< Keep-alive send interval (default: 5000ms)
    uint32_t keepalive_timeout_ms;    ///< Consider dead after this (default: 15000ms)
    dap_io_flow_keepalive_cb_t keepalive_callback;  ///< Timeout callback
    void *keepalive_arg;              ///< User argument for callback
} dap_io_flow_ctrl_config_t;

/**
 * @brief Initialize flow control subsystem
 * 
 * Must be called before using any flow control features.
 * 
 * @return 0 on success, negative on error
 */
int dap_io_flow_ctrl_init(void);

/**
 * @brief Deinitialize flow control subsystem
 */
void dap_io_flow_ctrl_deinit(void);

/**
 * @brief Create flow control for a flow
 * 
 * @param a_flow Flow to attach control to
 * @param a_flags Initial flags (can be changed later)
 * @param a_config Configuration (NULL for defaults)
 * @return Opaque flow control handle, NULL on error
 */
dap_io_flow_ctrl_t* dap_io_flow_ctrl_create(
    dap_io_flow_t *a_flow,
    dap_io_flow_ctrl_flags_t a_flags,
    const dap_io_flow_ctrl_config_t *a_config);

/**
 * @brief Destroy flow control
 * 
 * @param a_ctrl Flow control to destroy
 */
void dap_io_flow_ctrl_delete(dap_io_flow_ctrl_t *a_ctrl);

/**
 * @brief Set flow control flags (dynamic enable/disable)
 * 
 * Can be changed at runtime. Disabling a mechanism will free its resources.
 * 
 * @param a_ctrl Flow control
 * @param a_flags New flags
 * @return 0 on success, negative on error
 */
int dap_io_flow_ctrl_set_flags(dap_io_flow_ctrl_t *a_ctrl, dap_io_flow_ctrl_flags_t a_flags);

/**
 * @brief Get current flow control flags
 * 
 * @param a_ctrl Flow control
 * @return Current flags
 */
dap_io_flow_ctrl_flags_t dap_io_flow_ctrl_get_flags(dap_io_flow_ctrl_t *a_ctrl);

/**
 * @brief Send packet with flow control
 * 
 * Adds flow control header, tracks for retransmission if enabled.
 * 
 * @param a_ctrl Flow control
 * @param a_data Payload data
 * @param a_size Payload size
 * @return 0 on success, negative on error
 */
int dap_io_flow_ctrl_send(dap_io_flow_ctrl_t *a_ctrl, const void *a_data, size_t a_size);

/**
 * @brief Process received packet
 * 
 * Parses flow control header, handles ACKs, reordering, delivers to upper layer.
 * 
 * @param a_ctrl Flow control
 * @param a_packet Received packet (with flow control header)
 * @param a_size Packet size
 * @param[out] a_payload Pointer to payload data (after header)
 * @param[out] a_payload_size Payload size
 * @return 0 on success and payload available, 1 if buffered (out-of-order), negative on error
 */
int dap_io_flow_ctrl_recv(
    dap_io_flow_ctrl_t *a_ctrl,
    const void *a_packet,
    size_t a_size,
    const void **a_payload,
    size_t *a_payload_size);

/**
 * @brief Get statistics for flow control
 * 
 * @param a_ctrl Flow control
 * @param[out] a_sent_packets Total sent packets
 * @param[out] a_retransmitted_packets Retransmitted packets
 * @param[out] a_recv_packets Total received packets
 * @param[out] a_out_of_order_packets Out-of-order packets buffered
 * @param[out] a_duplicate_packets Duplicate packets dropped
 * @param[out] a_lost_packets Packets lost (exceeded max retries)
 */
void dap_io_flow_ctrl_get_stats(
    dap_io_flow_ctrl_t *a_ctrl,
    uint64_t *a_sent_packets,
    uint64_t *a_retransmitted_packets,
    uint64_t *a_recv_packets,
    uint64_t *a_out_of_order_packets,
    uint64_t *a_duplicate_packets,
    uint64_t *a_lost_packets);

/**
 * @brief Get default configuration
 * 
 * @param[out] a_config Configuration structure to fill
 */
void dap_io_flow_ctrl_get_default_config(dap_io_flow_ctrl_config_t *a_config);

#endif // DAP_IO_FLOW_CTRL_H

