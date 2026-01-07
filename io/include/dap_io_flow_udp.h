/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 */

/**
 * @file dap_io_flow_udp.h
 * @brief UDP-specific flow management layer
 * 
 * This module provides UDP-specific flow management on top of generic dap_io_flow.
 * It handles:
 * - UDP packet framing and header management
 * - Session tracking and timeout
 * - Sequence numbering
 * - Remote address storage
 * 
 * Architecture:
 * ```
 * dap_io_flow (generic L3 API)
 *      ↓
 * dap_io_flow_udp (UDP flow management) ← This module
 *      ↓
 * Protocol-specific (Stream UDP, RTP, QUIC-like, etc.)
 * ```
 */

#pragma once

#include <time.h>
#include <netinet/in.h>
#include "dap_io_flow.h"
#include "dap_events_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct dap_io_flow_udp dap_io_flow_udp_t;
typedef struct dap_io_flow_udp_ops dap_io_flow_udp_ops_t;

/**
 * @brief UDP-specific flow extension
 * 
 * Extends dap_io_flow_t with UDP-specific fields.
 * Protocol implementations should extend this further if needed.
 */
struct dap_io_flow_udp {
    dap_io_flow_t base;                  ///< Generic flow (MUST be first!)
    
    // UDP-specific fields
    struct sockaddr_storage remote_addr; ///< Remote address
    socklen_t remote_addr_len;           ///< Address length
    dap_events_socket_t *listener_es;    ///< Listener esocket
    
    // Session tracking
    _Atomic uint32_t seq_num_out;        ///< Outgoing sequence number
    uint32_t seq_num_in_last;            ///< Last received sequence number
    time_t last_activity;                ///< Last packet time (for timeout)
    
    // Protocol-specific extension
    void *protocol_data;                 ///< Protocol-specific data
};

/**
 * @brief UDP flow operations (protocol-specific VTable)
 * 
 * Protocol implementations provide these callbacks to handle
 * their specific packet types and logic.
 */
struct dap_io_flow_udp_ops {
    /**
     * @brief Handle incoming UDP packet
     * 
     * Called when a UDP packet arrives for this flow.
     * Protocol should parse and process the packet.
     * 
     * @param a_flow UDP flow
     * @param a_data Packet data (without UDP framing)
     * @param a_size Data size
     * @return 0 on success, negative on error
     */
    int (*packet_received)(dap_io_flow_udp_t *a_flow,
                          const uint8_t *a_data,
                          size_t a_size);
    
    /**
     * @brief Create protocol-specific data
     * 
     * Protocol MUST allocate its extended structure (e.g. stream_udp_session_t)
     * which extends dap_io_flow_udp_t. The returned pointer should be to the
     * base dap_io_flow_udp_t (first member of extended structure).
     * 
     * UDP layer will initialize common fields (remote_addr, listener_es, etc).
     * Protocol should only initialize its specific fields.
     * 
     * @param a_flow NULL (ignored, kept for API consistency)
     * @return Allocated dap_io_flow_udp_t* (or extended structure) or NULL
     */
    dap_io_flow_udp_t* (*protocol_create)(dap_io_flow_udp_t *a_flow);
    
    /**
     * @brief Finalize protocol-specific initialization
     * 
     * Called after UDP layer has initialized common fields (remote_addr, listener_es).
     * Protocol can now access listener_es->worker and complete initialization
     * (e.g. set stream->stream_worker).
     * 
     * @param a_flow Fully initialized UDP flow
     * @return 0 on success, negative on error
     */
    int (*protocol_finalize)(dap_io_flow_udp_t *a_flow);
    
    /**
     * @brief Destroy protocol-specific data
     * 
     * Called when UDP flow is destroyed. Protocol should
     * free its specific data here.
     * 
     * @param a_flow UDP flow
     */
    void (*protocol_destroy)(dap_io_flow_udp_t *a_flow);
    
    /**
     * @brief Send packet from protocol
     * 
     * Called by protocol to send a packet. UDP flow will
     * add sequence numbering and send via UDP.
     * 
     * @param a_flow UDP flow
     * @param a_data Data to send
     * @param a_size Data size
     * @return Bytes sent or negative on error
     */
    ssize_t (*protocol_send)(dap_io_flow_udp_t *a_flow,
                            const uint8_t *a_data,
                            size_t a_size);
};

/**
 * @brief Create UDP flow server
 * 
 * Helper function to create a flow server with UDP-specific setup.
 * 
 * @param a_name Server name
 * @param a_ops Generic flow operations
 * @param a_udp_ops UDP-specific operations
 * @return Created server or NULL on error
 */
dap_io_flow_server_t* dap_io_flow_server_new_udp(
    const char *a_name,
    dap_io_flow_ops_t *a_ops,
    dap_io_flow_udp_ops_t *a_udp_ops);

/**
 * @brief Send UDP packet with sequencing
 * 
 * Sends data via UDP, automatically adding sequence number.
 * 
 * @param a_flow UDP flow
 * @param a_data Data to send
 * @param a_size Data size
 * @return 0 on success, negative on error
 */
int dap_io_flow_udp_send(dap_io_flow_udp_t *a_flow,
                         const uint8_t *a_data,
                         size_t a_size);

/**
 * @brief Update flow activity time
 * 
 * Updates last_activity timestamp. Should be called
 * when any packet is received.
 * 
 * @param a_flow UDP flow
 */
void dap_io_flow_udp_update_activity(dap_io_flow_udp_t *a_flow);

/**
 * @brief Check if flow timed out
 * 
 * Checks if flow has been inactive for too long.
 * 
 * @param a_flow UDP flow
 * @param a_timeout_sec Timeout in seconds
 * @return true if timed out, false otherwise
 */
bool dap_io_flow_udp_is_timeout(dap_io_flow_udp_t *a_flow, uint32_t a_timeout_sec);

/**
 * @brief Get remote address as string
 * 
 * Converts remote address to string for logging.
 * 
 * @param a_flow UDP flow
 * @return Address string (static buffer, do not free)
 */
const char* dap_io_flow_udp_get_remote_addr_str(dap_io_flow_udp_t *a_flow);

#ifdef __cplusplus
}
#endif

