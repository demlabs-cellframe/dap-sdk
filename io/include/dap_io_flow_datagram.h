/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 */

/**
 * @file dap_io_flow_datagram.h
 * @brief DATAGRAM-specific flow management layer
 * 
 * This module provides DATAGRAM-specific flow management on top of generic dap_io_flow.
 * It handles:
 * - DATAGRAM packet framing and header management
 * - Session tracking and timeout
 * - Sequence numbering
 * - Remote address storage
 * 
 * Architecture:
 * ```
 * dap_io_flow (generic L3 API)
 *      ↓
 * dap_io_flow_datagram (DATAGRAM flow management) ← This module
 *      ↓
 * Protocol-specific (Stream DATAGRAM, RTP, QUIC-like, etc.)
 * ```
 */

#pragma once

#include <time.h>
#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif
#include "dap_io_flow.h"
#include "dap_events_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct dap_io_flow_datagram dap_io_flow_datagram_t;
typedef struct dap_io_flow_datagram_ops dap_io_flow_datagram_ops_t;

/**
 * @brief Callback to get remote address for this flow
 * 
 * Protocol-specific callback to retrieve the destination address for sending.
 * - For CLIENT flows: returns stable server address (from trans context)
 * - For SERVER flows: returns client address (from session context, updated per packet)
 * 
 * This callback avoids storing remote_addr in base structure and allows
 * protocol-specific address management without circular dependencies.
 * 
 * @param a_flow Flow instance
 * @param a_addr_out Output buffer for address
 * @param a_addr_len_out Output for address length
 * @return true if address retrieved, false otherwise
 */
typedef bool (*dap_io_flow_datagram_get_remote_addr_cb_t)(
    dap_io_flow_datagram_t *a_flow,
    struct sockaddr_storage *a_addr_out,
    socklen_t *a_addr_len_out
);

/**
 * @brief DATAGRAM-specific flow extension
 * 
 * Extends dap_io_flow_t with DATAGRAM-specific fields.
 * Protocol implementations should extend this further if needed.
 * 
 * NOTE: remote_addr is initialized by datagram layer for SERVER flows.
 * CLIENT flows can use their own stable copy and ignore this field.
 */
struct dap_io_flow_datagram {
    dap_io_flow_t base;                  ///< Generic flow (MUST be first!)
    
    // DATAGRAM-specific fields
    dap_events_socket_t *listener_es;    ///< Listener esocket (for receiving)
    dap_events_socket_t *send_es;        ///< Sending esocket (SERVER only, to avoid loopback)
    
    // Remote address (SERVER: filled by datagram layer, CLIENT: can be ignored)
    struct sockaddr_storage remote_addr; ///< Client address (for SERVER flows)
    socklen_t remote_addr_len;           ///< Address length
    
    // Remote address callback (protocol-specific, OPTIONAL for SERVER with remote_addr)
    dap_io_flow_datagram_get_remote_addr_cb_t get_remote_addr_cb;
    
    // Session tracking
    _Atomic uint32_t seq_num_out;        ///< Outgoing sequence number
    _Atomic uint32_t last_seq_num_in;    ///< Last received sequence number (for replay protection)
    time_t last_activity;                ///< Last packet time (for timeout)
    
    // Protocol-specific extension
    void *protocol_data;                 ///< Protocol-specific data
};

/**
 * @brief DATAGRAM flow operations (protocol-specific VTable)
 * 
 * Protocol implementations provide these callbacks to handle
 * their specific packet types and logic.
 */
struct dap_io_flow_datagram_ops {
    /**
     * @brief Handle incoming DATAGRAM packet
     * 
     * Called when a DATAGRAM packet arrives for this flow.
     * Protocol should parse and process the packet.
     * 
     * @param a_flow DATAGRAM flow
     * @param a_data Packet data (without DATAGRAM framing)
     * @param a_size Data size
     * @return 0 on success, negative on error
     */
    int (*packet_received)(dap_io_flow_datagram_t *a_flow,
                          const uint8_t *a_data,
                          size_t a_size);
    
    /**
     * @brief Create protocol-specific data
     * 
     * Protocol MUST allocate its extended structure (e.g. stream_datagram_session_t)
     * which extends dap_io_flow_datagram_t. The returned pointer should be to the
     * base dap_io_flow_datagram_t (first member of extended structure).
     * 
     * DATAGRAM layer will initialize common fields (remote_addr, listener_es, etc).
     * Protocol should only initialize its specific fields.
     * 
     * @param a_server Flow server instance (can access a_server->_inheritor for protocol-specific server data)
     * @param a_flow NULL (ignored, kept for API consistency)
     * @return Allocated dap_io_flow_datagram_t* (or extended structure) or NULL
     */
    dap_io_flow_datagram_t* (*protocol_create)(dap_io_flow_server_t *a_server,
                                           dap_io_flow_datagram_t *a_flow);
    
    /**
     * @brief Finalize protocol-specific initialization
     * 
     * Called after DATAGRAM layer has initialized common fields (remote_addr, listener_es).
     * Protocol can now access listener_es->worker and complete initialization
     * (e.g. set stream->stream_worker).
     * 
     * @param a_flow Fully initialized DATAGRAM flow
     * @return 0 on success, negative on error
     */
    int (*protocol_finalize)(dap_io_flow_datagram_t *a_flow);
    
    /**
     * @brief Destroy protocol-specific data
     * 
     * Called when DATAGRAM flow is destroyed. Protocol should
     * free its specific data here.
     * 
     * @param a_flow DATAGRAM flow
     */
    void (*protocol_destroy)(dap_io_flow_datagram_t *a_flow);
    
    /**
     * @brief Send packet from protocol
     * 
     * Called by protocol to send a packet. DATAGRAM flow will
     * add sequence numbering and send via DATAGRAM.
     * 
     * @param a_flow DATAGRAM flow
     * @param a_data Data to send
     * @param a_size Data size
     * @return Bytes sent or negative on error
     */
    ssize_t (*protocol_send)(dap_io_flow_datagram_t *a_flow,
                            const uint8_t *a_data,
                            size_t a_size);
};

/**
 * @brief Create DATAGRAM flow server
 * 
 * Helper function to create a flow server with DATAGRAM-specific setup.
 * 
 * @param a_name Server name
 * @param a_ops Generic flow operations
 * @param a_datagram_ops DATAGRAM-specific operations
 * @return Created server or NULL on error
 */
dap_io_flow_server_t* dap_io_flow_server_new_datagram(
    const char *a_name,
    dap_io_flow_ops_t *a_ops,
    dap_io_flow_datagram_ops_t *a_datagram_ops);

/**
 * @brief Send DATAGRAM packet with sequencing
 * 
 * Sends data via DATAGRAM, automatically adding sequence number.
 * 
 * @param a_flow DATAGRAM flow
 * @param a_data Data to send
 * @param a_size Data size
 * @return 0 on success, negative on error
 */
int dap_io_flow_datagram_send(dap_io_flow_datagram_t *a_flow,
                         const uint8_t *a_data,
                         size_t a_size);

/**
 * @brief Update flow activity time
 * 
 * Updates last_activity timestamp. Should be called
 * when any packet is received.
 * 
 * @param a_flow DATAGRAM flow
 */
void dap_io_flow_datagram_update_activity(dap_io_flow_datagram_t *a_flow);

/**
 * @brief Check if flow timed out
 * 
 * Checks if flow has been inactive for too long.
 * 
 * @param a_flow DATAGRAM flow
 * @param a_timeout_sec Timeout in seconds
 * @return true if timed out, false otherwise
 */
bool dap_io_flow_datagram_is_timeout(dap_io_flow_datagram_t *a_flow, uint32_t a_timeout_sec);

/**
 * @brief Get remote address as string
 * 
 * Converts remote address to string for logging.
 * Uses get_remote_addr_cb to retrieve the address.
 * 
 * @param a_flow DATAGRAM flow
 * @return Address string (static buffer, do not free)
 */
const char* dap_io_flow_datagram_get_remote_addr_str(dap_io_flow_datagram_t *a_flow);

/**
 * @brief Create a new datagram flow
 * 
 * Allocates and initializes a basic datagram flow structure.
 * This is used for CLIENT flows that don't need extended structures.
 * For SERVER flows with extended structures, use protocol-specific create callbacks.
 * 
 * @param a_get_remote_addr_cb Callback to get remote address
 * @param a_protocol_data Protocol-specific data (usually dap_stream_t*)
 * @return Allocated flow, or NULL on error
 */
dap_io_flow_datagram_t* dap_io_flow_datagram_new(
    dap_io_flow_datagram_get_remote_addr_cb_t a_get_remote_addr_cb,
    void *a_protocol_data
);

/**
 * @brief Delete a datagram flow created with dap_io_flow_datagram_new()
 * 
 * Only use for flows created with dap_io_flow_datagram_new().
 * For SERVER flows, use protocol-specific destroy callbacks.
 * 
 * @param a_flow Flow to delete
 */
void dap_io_flow_datagram_delete(dap_io_flow_datagram_t *a_flow);

/**
 * @brief Get remote address for sending packets
 * 
 * Calls protocol-specific get_remote_addr_cb to retrieve destination address.
 * - For CLIENT flows: callback returns stable server address (from trans context)
 * - For SERVER flows: callback returns client address (from session context)
 * 
 * This function resolves circular dependencies by delegating address retrieval
 * to protocol-specific code (e.g., dap_net_trans_udp).
 * 
 * @param a_flow DATAGRAM flow
 * @param a_addr_out Output buffer for address
 * @param a_addr_len_out Output for address length
 * @return true if address retrieved, false if callback not set or failed
 */
bool dap_io_flow_datagram_get_remote_addr(
    dap_io_flow_datagram_t *a_flow,
    struct sockaddr_storage *a_addr_out,
    socklen_t *a_addr_len_out
);

#ifdef __cplusplus
}
#endif
