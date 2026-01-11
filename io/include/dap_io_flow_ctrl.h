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
 * @brief Flow Control Mechanisms for Datagram Protocols (UDP, SCTP)
 * 
 * Provides reliable delivery mechanisms over unreliable datagram protocols:
 * - Packet tracking and retransmission (ACK-based)
 * - Sequence tracking and reordering (out-of-order delivery)
 * - Keep-alive mechanism (connection liveness)
 * 
 * ARCHITECTURE:
 * - Flow Control provides MECHANISMS (windows, timers, tracking)
 * - Transport protocols provide PACKET FORMAT (headers, serialization)
 * - Separation via CALLBACKS - transport decides how to pack/unpack data
 * 
 * This allows each protocol (UDP, SCTP, future protocols) to use its own
 * packet format while reusing the same flow control logic.
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
 * @brief Packet metadata for flow control
 * 
 * Transport protocol extracts this info from its packet format.
 */
typedef struct dap_io_flow_pkt_metadata {
    uint64_t seq_num;         ///< Sequence number of this packet
    uint64_t ack_seq;         ///< ACK for highest received in-order sequence
    uint32_t timestamp_ms;    ///< Timestamp for RTT calculation (optional)
    bool is_keepalive;        ///< Is this a keep-alive packet?
    bool is_retransmit;       ///< Is this a retransmitted packet?
} dap_io_flow_pkt_metadata_t;

/**
 * @brief Callback: Prepare packet for sending
 * 
 * Called by flow control before sending packet. Transport should:
 * 1. Allocate buffer for packet (header + payload)
 * 2. Add protocol-specific header with seq_num, ack_seq, etc
 * 3. Copy payload after header
 * 4. Return pointer to full packet and its size
 * 
 * @param a_flow Flow that is sending
 * @param a_metadata Flow control metadata (seq_num, ack_seq, etc)
 * @param a_payload Payload data to send
 * @param a_payload_size Payload size
 * @param[out] a_packet_out Pointer to full packet (header + payload)
 * @param[out] a_packet_size_out Full packet size
 * @param a_arg User argument
 * @return 0 on success, negative on error
 */
typedef int (*dap_io_flow_ctrl_packet_prepare_cb_t)(
    dap_io_flow_t *a_flow,
    const dap_io_flow_pkt_metadata_t *a_metadata,
    const void *a_payload,
    size_t a_payload_size,
    void **a_packet_out,
    size_t *a_packet_size_out,
    void *a_arg);

/**
 * @brief Callback: Parse received packet
 * 
 * Called by flow control when packet arrives. Transport should:
 * 1. Parse protocol-specific header
 * 2. Extract seq_num, ack_seq, flags into metadata
 * 3. Return pointer to payload (after header) and its size
 * 
 * @param a_flow Flow that received packet
 * @param a_packet Full packet (header + payload)
 * @param a_packet_size Full packet size
 * @param[out] a_metadata Flow control metadata extracted from header
 * @param[out] a_payload_out Pointer to payload (inside a_packet)
 * @param[out] a_payload_size_out Payload size
 * @param a_arg User argument
 * @return 0 on success, negative on error
 */
typedef int (*dap_io_flow_ctrl_packet_parse_cb_t)(
    dap_io_flow_t *a_flow,
    const void *a_packet,
    size_t a_packet_size,
    dap_io_flow_pkt_metadata_t *a_metadata,
    const void **a_payload_out,
    size_t *a_payload_size_out,
    void *a_arg);

/**
 * @brief Callback: Send packet to network
 * 
 * Called by flow control (original send + retransmits). Transport should
 * send packet via its native send function (sendto, sctp_sendmsg, etc).
 * 
 * @param a_flow Flow to send on
 * @param a_packet Packet to send (already prepared with header)
 * @param a_packet_size Packet size
 * @param a_arg User argument
 * @return 0 on success, negative on error
 */
typedef int (*dap_io_flow_ctrl_packet_send_cb_t)(
    dap_io_flow_t *a_flow,
    const void *a_packet,
    size_t a_packet_size,
    void *a_arg);

/**
 * @brief Callback: Free packet buffer
 * 
 * Called by flow control to free packet allocated by prepare callback.
 * 
 * @param a_packet Packet buffer to free
 * @param a_arg User argument
 */
typedef void (*dap_io_flow_ctrl_packet_free_cb_t)(
    void *a_packet,
    void *a_arg);

/**
 * @brief Callback: Deliver payload to upper layer
 * 
 * Called by flow control when in-order payload is ready. Transport should
 * deliver to stream layer or application.
 * 
 * @param a_flow Flow that received data
 * @param a_payload Payload data (validated and in-order)
 * @param a_payload_size Payload size
 * @param a_arg User argument
 * @return 0 on success, negative on error
 */
typedef int (*dap_io_flow_ctrl_payload_deliver_cb_t)(
    dap_io_flow_t *a_flow,
    const void *a_payload,
    size_t a_payload_size,
    void *a_arg);

/**
 * @brief Callback: Keep-alive timeout
 * 
 * Called when keep-alive timeout expires (connection considered dead).
 * Protocol can decide what to do: close, reconnect, notify user, etc.
 * 
 * @param a_flow Flow that timed out
 * @param a_arg User argument
 */
typedef void (*dap_io_flow_ctrl_keepalive_timeout_cb_t)(
    dap_io_flow_t *a_flow,
    void *a_arg);

/**
 * @brief Flow control callbacks (transport-provided)
 * 
 * Transport protocol implements these to integrate with flow control.
 */
typedef struct dap_io_flow_ctrl_callbacks {
    dap_io_flow_ctrl_packet_prepare_cb_t packet_prepare;     ///< Add header before send
    dap_io_flow_ctrl_packet_parse_cb_t packet_parse;         ///< Parse header on receive
    dap_io_flow_ctrl_packet_send_cb_t packet_send;           ///< Send to network
    dap_io_flow_ctrl_packet_free_cb_t packet_free;           ///< Free packet buffer
    dap_io_flow_ctrl_payload_deliver_cb_t payload_deliver;   ///< Deliver to upper layer
    dap_io_flow_ctrl_keepalive_timeout_cb_t keepalive_timeout; ///< Keep-alive timeout
    void *arg;                                                 ///< User argument for all callbacks
} dap_io_flow_ctrl_callbacks_t;

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
 * @param a_callbacks Transport callbacks (required)
 * @return Opaque flow control handle, NULL on error
 */
dap_io_flow_ctrl_t* dap_io_flow_ctrl_create(
    dap_io_flow_t *a_flow,
    dap_io_flow_ctrl_flags_t a_flags,
    const dap_io_flow_ctrl_config_t *a_config,
    const dap_io_flow_ctrl_callbacks_t *a_callbacks);

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
 * @brief Send payload with flow control
 * 
 * Flow control will:
 * 1. Call packet_prepare callback to add header
 * 2. Track packet for retransmission if enabled
 * 3. Call packet_send callback to send to network
 * 
 * @param a_ctrl Flow control
 * @param a_payload Payload data (transport-specific data, no header)
 * @param a_payload_size Payload size
 * @return 0 on success, negative on error
 */
int dap_io_flow_ctrl_send(dap_io_flow_ctrl_t *a_ctrl, const void *a_payload, size_t a_payload_size);

/**
 * @brief Process received packet
 * 
 * Flow control will:
 * 1. Call packet_parse callback to extract metadata
 * 2. Process ACKs for sent packets
 * 3. Handle reordering if enabled
 * 4. Call payload_deliver callback for in-order data
 * 
 * @param a_ctrl Flow control
 * @param a_packet Received packet (with transport-specific header)
 * @param a_packet_size Packet size
 * @return 0 on success and delivered, 1 if buffered (out-of-order), negative on error
 */
int dap_io_flow_ctrl_recv(
    dap_io_flow_ctrl_t *a_ctrl,
    const void *a_packet,
    size_t a_packet_size);

/**
 * @brief Update keep-alive activity
 * 
 * Call this when ANY packet arrives (data or keep-alive) to reset timeout.
 * 
 * @param a_ctrl Flow control
 */
void dap_io_flow_ctrl_keepalive_activity(dap_io_flow_ctrl_t *a_ctrl);

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

