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
#include "dap_config.h"
#include "dap_io_flow_udp.h"
#include "dap_io_flow_socket.h"

#define LOG_TAG "dap_io_flow_udp"

// Debug mode
static bool s_debug_more = false;

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
    // Initialize debug mode from config (once)
    static bool s_debug_initialized = false;
    if (!s_debug_initialized && g_config) {
        s_debug_more = dap_config_get_item_bool_default(g_config, "io_flow_udp", "debug_more", false);
        s_debug_initialized = true;
        if (s_debug_more) {
            log_it(L_NOTICE, "IO Flow UDP debug mode ENABLED");
        }
    }
    
    debug_if(s_debug_more, L_DEBUG, "dap_io_flow_server_new_udp: entry, name=%s", a_name ? a_name : "NULL");
    
    if (!a_name || !a_ops || !a_udp_ops) {
        log_it(L_ERROR, "Invalid arguments for UDP flow server");
        return NULL;
    }
    
    // Store UDP ops globally (simplified approach)
    s_udp_ops = a_udp_ops;
    
    debug_if(s_debug_more, L_DEBUG, "Allocating wrapped ops in heap");
    
    // Allocate wrapped ops in heap for this specific server
    dap_io_flow_ops_t *l_wrapped_ops = DAP_NEW(dap_io_flow_ops_t);
    if (!l_wrapped_ops) {
        log_it(L_CRITICAL, "Failed to allocate wrapped ops");
        return NULL;
    }
    
    // Copy and wrap generic ops with UDP-specific wrappers
    *l_wrapped_ops = *a_ops;
    
    // Override callbacks to add UDP handling
    l_wrapped_ops->packet_received = s_udp_packet_received_wrapper;
    l_wrapped_ops->flow_create = s_udp_flow_create_wrapper;
    l_wrapped_ops->flow_destroy = s_udp_flow_destroy_wrapper;
    
    debug_if(s_debug_more, L_DEBUG, "Calling dap_io_flow_server_new");
    
    // Create generic flow server with DATAGRAM boundary type (UDP)
    dap_io_flow_server_t *l_server = dap_io_flow_server_new(
        a_name,
        l_wrapped_ops,  // Pass heap-allocated wrapped ops
        DAP_IO_FLOW_BOUNDARY_DATAGRAM
    );
    
    fprintf(stderr, "dap_io_flow_server_new returned: %p\n", (void*)l_server);
    fflush(stderr);
    
    if (!l_server) {
        fprintf(stderr, "ERROR: Failed to create generic flow server for UDP\n");
        fflush(stderr);
        log_it(L_ERROR, "Failed to create generic flow server for UDP (dap_io_flow_server_new returned NULL)");
        DAP_DELETE(l_wrapped_ops);
        return NULL;
    }
    
    log_it(L_INFO, "Created UDP flow server '%s'", a_name);
    
    debug_if(s_debug_more, L_DEBUG, "dap_io_flow_server_new_udp: success, returning %p", (void*)l_server);
    
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
    
    // Call protocol-specific creation which allocates the full structure
    // Protocol MUST extend dap_io_flow_udp_t and return its base pointer
    if (!s_udp_ops || !s_udp_ops->protocol_create) {
        log_it(L_ERROR, "UDP protocol_create callback is missing");
        return NULL;
    }
    
    // Create extended UDP flow via protocol callback
    // Protocol allocates full structure (e.g. stream_udp_session_t extends dap_io_flow_udp_t)
    dap_io_flow_udp_t *l_udp_flow = s_udp_ops->protocol_create(NULL);
    if (!l_udp_flow) {
        log_it(L_CRITICAL, "Failed to allocate UDP flow via protocol_create");
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
    
    // Finalize protocol-specific initialization (e.g. set stream_worker)
    // Protocol can use listener_es->worker now
    if (s_udp_ops && s_udp_ops->protocol_finalize) {
        if (s_udp_ops->protocol_finalize(l_udp_flow) != 0) {
            log_it(L_ERROR, "Protocol finalize failed");
            s_udp_ops->protocol_destroy(l_udp_flow);
            return NULL;
        }
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


