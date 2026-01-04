/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 */

#include <string.h>
#include <arpa/inet.h>
#include "dap_common.h"
#include "dap_io_flow_udp.h"
#include "dap_io_flow_socket.h"

#define LOG_TAG "dap_io_flow_udp"

// Static storage for UDP-specific ops
static dap_io_flow_udp_ops_t *s_udp_ops = NULL;

// Forward declarations for internal wrappers
static void s_udp_packet_received_wrapper(dap_io_flow_server_t *a_srv,
                                         dap_io_flow_t *a_flow,
                                         const uint8_t *a_data,
                                         size_t a_size,
                                         const struct sockaddr_storage *a_remote_addr,
                                         dap_events_socket_t *a_listener_es);

static dap_io_flow_t* s_udp_flow_create_wrapper(dap_io_flow_server_t *a_srv,
                                                const struct sockaddr_storage *a_remote_addr,
                                                dap_events_socket_t *a_listener_es);

static void s_udp_flow_destroy_wrapper(dap_io_flow_t *a_flow);

/**
 * @brief Create UDP flow server
 */
dap_io_flow_server_t* dap_io_flow_server_new_udp(
    const char *a_name,
    dap_io_flow_ops_t *a_ops,
    dap_io_flow_udp_ops_t *a_udp_ops)
{
    if (!a_name || !a_ops || !a_udp_ops) {
        log_it(L_ERROR, "Invalid arguments for UDP flow server");
        return NULL;
    }
    
    // Store UDP ops globally (simplified approach)
    s_udp_ops = a_udp_ops;
    
    // Wrap generic ops with UDP-specific wrappers
    dap_io_flow_ops_t l_wrapped_ops = *a_ops;
    
    // Override packet_received to add UDP handling
    l_wrapped_ops.packet_received = s_udp_packet_received_wrapper;
    
    // Override flow_create to add UDP initialization
    l_wrapped_ops.flow_create = s_udp_flow_create_wrapper;
    
    // Override flow_destroy to add UDP cleanup
    l_wrapped_ops.flow_destroy = s_udp_flow_destroy_wrapper;
    
    // Create generic flow server with DATAGRAM boundary type (UDP)
    dap_io_flow_server_t *l_server = dap_io_flow_server_new(
        a_name,
        &l_wrapped_ops,
        DAP_IO_FLOW_BOUNDARY_DATAGRAM
    );
    
    if (!l_server) {
        log_it(L_ERROR, "Failed to create generic flow server for UDP");
        return NULL;
    }
    
    log_it(L_INFO, "Created UDP flow server '%s'", a_name);
    
    return l_server;
}

/**
 * @brief Send UDP packet with sequencing
 */
int dap_io_flow_udp_send(dap_io_flow_udp_t *a_flow,
                         const uint8_t *a_data,
                         size_t a_size)
{
    if (!a_flow || !a_data || a_size == 0) {
        return -1;
    }
    
    // Increment sequence number atomically
    uint32_t l_seq = atomic_fetch_add(&a_flow->seq_num_out, 1);
    
    // TODO: Optionally prepend sequence number to packet here
    // For now, just send data as-is (protocol can add its own framing)
    
    // Send via socket
    int l_ret = dap_io_flow_socket_send_to(
        a_flow->listener_es,
        a_data,
        a_size,
        &a_flow->remote_addr,
        a_flow->remote_addr_len
    );
    
    if (l_ret < 0) {
        log_it(L_WARNING, "Failed to send UDP packet (seq=%u, size=%zu)", l_seq, a_size);
        return -2;
    }
    
    // Update activity time
    dap_io_flow_udp_update_activity(a_flow);
    
    return 0;
}

/**
 * @brief Update flow activity time
 */
void dap_io_flow_udp_update_activity(dap_io_flow_udp_t *a_flow)
{
    if (a_flow) {
        a_flow->last_activity = time(NULL);
    }
}

/**
 * @brief Check if flow timed out
 */
bool dap_io_flow_udp_is_timeout(dap_io_flow_udp_t *a_flow, uint32_t a_timeout_sec)
{
    if (!a_flow) {
        return true;
    }
    
    time_t l_now = time(NULL);
    time_t l_elapsed = l_now - a_flow->last_activity;
    
    return (l_elapsed > (time_t)a_timeout_sec);
}

/**
 * @brief Get remote address as string
 */
const char* dap_io_flow_udp_get_remote_addr_str(dap_io_flow_udp_t *a_flow)
{
    if (!a_flow) {
        return "(null)";
    }
    
    return dap_io_flow_socket_addr_to_string(&a_flow->remote_addr);
}

// =============================================================================
// Internal wrapper functions
// =============================================================================

/**
 * @brief Wrapper for packet_received - adds UDP-specific handling
 */
static void s_udp_packet_received_wrapper(dap_io_flow_server_t *a_srv,
                                         dap_io_flow_t *a_flow,
                                         const uint8_t *a_data,
                                         size_t a_size,
                                         const struct sockaddr_storage *a_remote_addr,
                                         dap_events_socket_t *a_listener_es)
{
    UNUSED(a_srv);
    UNUSED(a_remote_addr);
    UNUSED(a_listener_es);
    
    if (!a_flow || !a_data || a_size == 0) {
        return;
    }
    
    dap_io_flow_udp_t *l_udp_flow = (dap_io_flow_udp_t*)a_flow;
    
    // Update activity time
    dap_io_flow_udp_update_activity(l_udp_flow);
   
    // Call protocol-specific handler
    if (s_udp_ops && s_udp_ops->packet_received) {
        s_udp_ops->packet_received(l_udp_flow, a_data, a_size);
    }
}

/**
 * @brief Wrapper for flow_create - adds UDP-specific initialization
 */
static dap_io_flow_t* s_udp_flow_create_wrapper(dap_io_flow_server_t *a_srv,
                                                const struct sockaddr_storage *a_remote_addr,
                                                dap_events_socket_t *a_listener_es)
{
    UNUSED(a_srv);
    
    if (!a_remote_addr || !a_listener_es) {
        return NULL;
    }
    
    // Allocate UDP flow (protocol should extend this further if needed)
    dap_io_flow_udp_t *l_udp_flow = DAP_NEW_Z(dap_io_flow_udp_t);
    if (!l_udp_flow) {
        log_it(L_CRITICAL, "Failed to allocate UDP flow");
        return NULL;
    }
    
    // Initialize UDP-specific fields
    memcpy(&l_udp_flow->remote_addr, a_remote_addr, sizeof(struct sockaddr_storage));
    l_udp_flow->remote_addr_len = (a_remote_addr->ss_family == AF_INET) 
        ? sizeof(struct sockaddr_in) 
        : sizeof(struct sockaddr_in6);
    l_udp_flow->listener_es = a_listener_es;
    l_udp_flow->seq_num_out = 0;
    l_udp_flow->seq_num_in_last = 0;
    l_udp_flow->last_activity = time(NULL);
    l_udp_flow->protocol_data = NULL;
    
    // Call protocol-specific creation
    if (s_udp_ops && s_udp_ops->protocol_create) {
        l_udp_flow->protocol_data = s_udp_ops->protocol_create(l_udp_flow);
    }
    
    log_it(L_DEBUG, "Created UDP flow for %s",
           dap_io_flow_udp_get_remote_addr_str(l_udp_flow));
    
    return &l_udp_flow->base;
}

/**
 * @brief Wrapper for flow_destroy - adds UDP-specific cleanup
 */
static void s_udp_flow_destroy_wrapper(dap_io_flow_t *a_flow)
{
    if (!a_flow) {
        return;
    }
    
    dap_io_flow_udp_t *l_udp_flow = (dap_io_flow_udp_t*)a_flow;
    
    log_it(L_DEBUG, "Destroying UDP flow for %s",
           dap_io_flow_udp_get_remote_addr_str(l_udp_flow));
    
    // Call protocol-specific destruction
    if (s_udp_ops && s_udp_ops->protocol_destroy) {
        s_udp_ops->protocol_destroy(l_udp_flow);
    }
    
    // Free UDP flow
    DAP_DELETE(l_udp_flow);
}


