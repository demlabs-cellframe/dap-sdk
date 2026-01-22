/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 */

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "dap_common.h"
#include "dap_config.h"
#include "dap_worker.h"
#include "dap_io_flow_datagram.h"
#include "dap_io_flow_socket.h"

#define LOG_TAG "dap_io_flow_datagram"

// Debug mode
static bool s_debug_more = false;

// Static storage for DATAGRAM-specific ops
static dap_io_flow_datagram_ops_t *s_datagram_ops = NULL;

// Forward declarations for internal wrappers
static void s_datagram_packet_received_wrapper(dap_io_flow_server_t *a_srv,
                                         dap_io_flow_t *a_flow,
                                         const uint8_t *a_data,
                                         size_t a_size,
                                         const struct sockaddr_storage *a_remote_addr,
                                         dap_events_socket_t *a_listener_es);

static dap_io_flow_t* s_datagram_flow_create_wrapper(dap_io_flow_server_t *a_srv,
                                                const struct sockaddr_storage *a_remote_addr,
                                                dap_events_socket_t *a_listener_es);

static void s_datagram_flow_destroy_wrapper(dap_io_flow_t *a_flow);

/**
 * @brief Create DATAGRAM flow server
 */
dap_io_flow_server_t* dap_io_flow_server_new_datagram(
    const char *a_name,
    dap_io_flow_ops_t *a_ops,
    dap_io_flow_datagram_ops_t *a_datagram_ops)
{
    // Initialize debug mode from config (once)
    static bool s_debug_initialized = false;
    if (!s_debug_initialized && g_config) {
        s_debug_more = dap_config_get_item_bool_default(g_config, "io_flow_datagram", "debug_more", false);
        s_debug_initialized = true;
        if (s_debug_more) {
            log_it(L_NOTICE, "IO Flow DATAGRAM debug mode ENABLED");
        }
    }
    
    debug_if(s_debug_more, L_DEBUG, "dap_io_flow_server_new_datagram: entry, name=%s", a_name ? a_name : "NULL");
    
    if (!a_name || !a_ops || !a_datagram_ops) {
        log_it(L_ERROR, "Invalid arguments for DATAGRAM flow server");
        return NULL;
    }
    
    // Store DATAGRAM ops globally (simplified approach)
    s_datagram_ops = a_datagram_ops;
    
    debug_if(s_debug_more, L_DEBUG, "Allocating wrapped ops in heap");
    
    // Allocate wrapped ops in heap for this specific server
    dap_io_flow_ops_t *l_wrapped_ops = DAP_NEW(dap_io_flow_ops_t);
    if (!l_wrapped_ops) {
        log_it(L_CRITICAL, "Failed to allocate wrapped ops");
        return NULL;
    }
    
    // Copy and wrap generic ops with DATAGRAM-specific wrappers
    *l_wrapped_ops = *a_ops;
    
    // Override callbacks to add DATAGRAM handling
    l_wrapped_ops->packet_received = s_datagram_packet_received_wrapper;
    l_wrapped_ops->flow_create = s_datagram_flow_create_wrapper;
    l_wrapped_ops->flow_destroy = s_datagram_flow_destroy_wrapper;
    
    debug_if(s_debug_more, L_DEBUG, "Calling dap_io_flow_server_new");
    
    // Create generic flow server with DATAGRAM boundary type (DATAGRAM)
    dap_io_flow_server_t *l_server = dap_io_flow_server_new(
        a_name,
        l_wrapped_ops,  // Pass heap-allocated wrapped ops
        DAP_IO_FLOW_BOUNDARY_DATAGRAM
    );
    
    fprintf(stderr, "dap_io_flow_server_new returned: %p\n", (void*)l_server);
    fflush(stderr);
    
    if (!l_server) {
        fprintf(stderr, "ERROR: Failed to create generic flow server for DATAGRAM\n");
        fflush(stderr);
        log_it(L_ERROR, "Failed to create generic flow server for DATAGRAM (dap_io_flow_server_new returned NULL)");
        DAP_DELETE(l_wrapped_ops);
        return NULL;
    }
    
    log_it(L_INFO, "Created DATAGRAM flow server '%s'", a_name);
    
    debug_if(s_debug_more, L_DEBUG, "dap_io_flow_server_new_datagram: success, returning %p", (void*)l_server);
    
    return l_server;
}

/**
 * @brief Send DATAGRAM packet with sequencing
 */
int dap_io_flow_datagram_send(dap_io_flow_datagram_t *a_flow,
                        const uint8_t *a_data,
                        size_t a_size)
{
    if (!a_flow || !a_data || a_size == 0) {
        return -1;
    }
    
    // Get destination address via callback
    struct sockaddr_storage l_dest_addr;
    socklen_t l_dest_addr_len;
    
    if (!dap_io_flow_datagram_get_remote_addr(a_flow, &l_dest_addr, &l_dest_addr_len)) {
        log_it(L_ERROR, "Failed to get remote address for datagram send");
        return -1;
    }
    
    // Debug: show destination
    if (l_dest_addr.ss_family == AF_INET) {
        struct sockaddr_in *l_sin = (struct sockaddr_in*)&l_dest_addr;
        char l_addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &l_sin->sin_addr, l_addr_str, sizeof(l_addr_str));
        debug_if(s_debug_more, L_DEBUG,
                 "Sending %zu bytes to %s:%u (listener_es=%p, fd=%d)",
                 a_size, l_addr_str, ntohs(l_sin->sin_port),
                 a_flow->listener_es, a_flow->listener_es ? a_flow->listener_es->fd : -1);
    }
    
    // Increment sequence number atomically
    uint32_t l_seq = atomic_fetch_add(&a_flow->seq_num_out, 1);
    
    // Choose socket: use send_es if available (SERVER), otherwise listener_es (CLIENT)
    dap_events_socket_t *l_send_socket = a_flow->send_es ? a_flow->send_es : a_flow->listener_es;
    
    debug_if(s_debug_more, L_DEBUG,
             "dap_io_flow_datagram_send: BEFORE send, send_socket=%p (fd=%d, worker=%u), "
             "send_es=%p (fd=%d), listener_es=%p (fd=%d), dest=%s",
             l_send_socket,
             l_send_socket ? l_send_socket->fd : -1,
             l_send_socket && l_send_socket->worker ? l_send_socket->worker->id : 999,
             a_flow->send_es,
             a_flow->send_es ? a_flow->send_es->fd : -1,
             a_flow->listener_es,
             a_flow->listener_es ? a_flow->listener_es->fd : -1,
             dap_io_flow_socket_addr_to_string(&l_dest_addr));
    
    int l_ret = dap_io_flow_socket_send_to(
        a_flow->base.server,  // Pass server for is_deleting check (via base flow)
        l_send_socket,  // Use separate send socket for SERVER, listener for CLIENT
        a_data,
        a_size,
        &l_dest_addr,
        l_dest_addr_len
    );
    
    if (l_ret < 0) {
        log_it(L_WARNING, "Failed to send DATAGRAM packet (seq=%u, size=%zu)", l_seq, a_size);
        return -2;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "dap_io_flow_datagram_send: SENT successfully via fd=%d, size=%zu, ret=%d",
             l_send_socket ? l_send_socket->fd : -1, a_size, l_ret);
    
    // Update activity time
    dap_io_flow_datagram_update_activity(a_flow);
    
    return 0;
}

/**
 * @brief Update flow activity time
 */
void dap_io_flow_datagram_update_activity(dap_io_flow_datagram_t *a_flow)
{
    if (a_flow) {
        a_flow->last_activity = time(NULL);
    }
}

/**
 * @brief Check if flow timed out
 */
bool dap_io_flow_datagram_is_timeout(dap_io_flow_datagram_t *a_flow, uint32_t a_timeout_sec)
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
/**
 * @brief Get remote address for sending packets
 * 
 * Calls protocol-specific get_remote_addr_cb to retrieve destination address.
 * If callback not set, uses flow->remote_addr (for SERVER flows).
 */
bool dap_io_flow_datagram_get_remote_addr(
    dap_io_flow_datagram_t *a_flow,
    struct sockaddr_storage *a_addr_out,
    socklen_t *a_addr_len_out)
{
    if (!a_flow || !a_addr_out || !a_addr_len_out) {
        debug_if(s_debug_more, L_ERROR, "Invalid arguments to dap_io_flow_datagram_get_remote_addr");
        return false;
    }
    
    // Callback is REQUIRED - no fallbacks
    if (!a_flow->get_remote_addr_cb) {
        log_it(L_CRITICAL, "Datagram flow has no get_remote_addr_cb callback!");
        return false;
    }
    
    bool l_result = a_flow->get_remote_addr_cb(a_flow, a_addr_out, a_addr_len_out);
    
    debug_if(s_debug_more && l_result, L_DEBUG, "dap_io_flow_datagram_get_remote_addr: callback returned address");
    
    return l_result;
}

/**
 * @brief Create a new datagram flow (for CLIENT use)
 */
dap_io_flow_datagram_t* dap_io_flow_datagram_new(
    dap_io_flow_datagram_get_remote_addr_cb_t a_get_remote_addr_cb,
    void *a_protocol_data)
{
    if (!a_get_remote_addr_cb) {
        debug_if(s_debug_more, L_ERROR, "get_remote_addr_cb is required for datagram flow");
        return NULL;
    }
    
    dap_io_flow_datagram_t *l_flow = DAP_NEW_Z(dap_io_flow_datagram_t);
    if (!l_flow) {
        log_it(L_CRITICAL, "Failed to allocate datagram flow");
        return NULL;
    }
    
    l_flow->get_remote_addr_cb = a_get_remote_addr_cb;
    l_flow->protocol_data = a_protocol_data;
    
    return l_flow;
}

/**
 * @brief Delete a datagram flow created with dap_io_flow_datagram_new()
 */
void dap_io_flow_datagram_delete(dap_io_flow_datagram_t *a_flow)
{
    if (!a_flow) {
        return;
    }
    
    DAP_DELETE(a_flow);
}

/**
 * @brief Get remote address as string (uses callback)
 */
const char* dap_io_flow_datagram_get_remote_addr_str(dap_io_flow_datagram_t *a_flow)
{
    if (!a_flow) {
        return "(null)";
    }
    
    struct sockaddr_storage l_addr;
    socklen_t l_addr_len;
    
    if (!dap_io_flow_datagram_get_remote_addr(a_flow, &l_addr, &l_addr_len)) {
        return "(callback failed)";
    }
    
    return dap_io_flow_socket_addr_to_string(&l_addr);
}

// =============================================================================
// Internal wrapper functions
// =============================================================================

/**
 * @brief Wrapper for packet_received - adds DATAGRAM-specific handling
 */
static void s_datagram_packet_received_wrapper(dap_io_flow_server_t *a_srv,
                                         dap_io_flow_t *a_flow,
                                         const uint8_t *a_data,
                                         size_t a_size,
                                         const struct sockaddr_storage *a_remote_addr,
                                         dap_events_socket_t *a_listener_es)
{
    UNUSED(a_srv);
    UNUSED(a_listener_es);
    
    // NOTE: a_flow should NEVER be NULL here - s_process_flow_packet_common creates it!
    // If it's NULL, something is wrong with flow creation
    if (!a_flow || !a_data || a_size == 0) {
        if (!a_flow) {
            log_it(L_ERROR, "DATAGRAM wrapper: a_flow is NULL - flow creation failed!");
        }
        return;
    }
    
    dap_io_flow_datagram_t *l_datagram_flow = (dap_io_flow_datagram_t*)a_flow;
    
    // NOTE: remote_addr is set ONCE during flow creation (s_datagram_flow_create_wrapper)
    // Flow is tracked by (remote_addr, remote_port), so address CANNOT change for existing flow
    // No need to update remote_addr here!
    
    // Update activity time
    dap_io_flow_datagram_update_activity(l_datagram_flow);
   
    // Call protocol-specific handler
    if (s_datagram_ops && s_datagram_ops->packet_received) {
        s_datagram_ops->packet_received(l_datagram_flow, a_data, a_size);
    }
}

/**
 * @brief Wrapper for flow_create - adds DATAGRAM-specific initialization
 */
static dap_io_flow_t* s_datagram_flow_create_wrapper(dap_io_flow_server_t *a_srv,
                                                const struct sockaddr_storage *a_remote_addr,
                                                dap_events_socket_t *a_listener_es)
{
    if (!a_remote_addr || !a_listener_es) {
        return NULL;
    }
    
    // Call protocol-specific creation which allocates the full structure
    // Protocol MUST extend dap_io_flow_datagram_t and return its base pointer
    if (!s_datagram_ops || !s_datagram_ops->protocol_create) {
        log_it(L_ERROR, "DATAGRAM protocol_create callback is missing");
        return NULL;
    }
    
    // Create extended DATAGRAM flow via protocol callback
    // Protocol allocates full structure (e.g. stream_datagram_session_t extends dap_io_flow_datagram_t)
    // Pass a_srv so protocol can access a_srv->_inheritor for server-specific data
    dap_io_flow_datagram_t *l_datagram_flow = s_datagram_ops->protocol_create(a_srv, NULL);
    if (!l_datagram_flow) {
        log_it(L_CRITICAL, "Failed to allocate DATAGRAM flow via protocol_create");
        return NULL;
    }
    
    // Initialize remote_addr (SERVER flow) - SET ONCE, NEVER CHANGED!
    memcpy(&l_datagram_flow->remote_addr, a_remote_addr, sizeof(struct sockaddr_storage));
    l_datagram_flow->remote_addr_len = (a_remote_addr->ss_family == AF_INET) 
        ? sizeof(struct sockaddr_in) 
        : sizeof(struct sockaddr_in6);
    
    debug_if(s_debug_more, L_DEBUG, "DATAGRAM flow_create: INITIAL remote_addr set");
    
    l_datagram_flow->listener_es = a_listener_es;
    
    // Create separate sending socket ONLY for SERVER flows (to avoid localhost loopback)
    // CLIENT flows use their own listener_es for sending (which is fine, no loopback)
    // SERVER flow marker: a_srv is not NULL
    if (a_srv) {
        // Create separate sending socket for this SERVER session
        int l_send_fd = socket(a_remote_addr->ss_family, SOCK_DGRAM, 0);
        if (l_send_fd < 0) {
            log_it(L_ERROR, "Failed to create sending socket: %s", strerror(errno));
            s_datagram_ops->protocol_destroy(l_datagram_flow);
            return NULL;
        }
        
        // Create esocket wrapper with callbacks (for proper worker integration)
        dap_events_socket_callbacks_t l_send_callbacks = {0};
        // No read callback needed - this socket is write-only
        
        l_datagram_flow->send_es = dap_events_socket_wrap_no_add(l_send_fd, &l_send_callbacks);
        
        if (!l_datagram_flow->send_es) {
            log_it(L_ERROR, "Failed to wrap send socket");
            close(l_send_fd);
            s_datagram_ops->protocol_destroy(l_datagram_flow);
            return NULL;
        }
        
        l_datagram_flow->send_es->type = DESCRIPTOR_TYPE_SOCKET_UDP;
        l_datagram_flow->send_es->flags |= DAP_SOCK_READY_TO_WRITE;
        
        // Add to worker with auto-balancing
        dap_worker_t *l_assigned_worker = dap_worker_add_events_socket_auto(l_datagram_flow->send_es);
        
        log_it(L_NOTICE, "DATAGRAM flow_create: created separate send socket fd=%d (worker=%u) for SERVER flow %s", 
               l_send_fd, 
               l_assigned_worker ? l_assigned_worker->id : 999,
               dap_io_flow_socket_addr_to_string(a_remote_addr));
    } else {
        // CLIENT flow: no separate send socket needed
        l_datagram_flow->send_es = NULL;
        log_it(L_DEBUG, "DATAGRAM flow_create: CLIENT flow, will use listener_es for sending");
    }
    
    debug_if(s_debug_more, L_DEBUG, "DATAGRAM flow created: listener_es=%p (fd=%d), send_es=%p (fd=%d)",
           a_listener_es, a_listener_es ? a_listener_es->fd : -1,
           l_datagram_flow->send_es, l_datagram_flow->send_es ? l_datagram_flow->send_es->fd : -1);
    
    l_datagram_flow->seq_num_out = 0;
    l_datagram_flow->last_seq_num_in = 0;
    l_datagram_flow->last_activity = time(NULL);
    l_datagram_flow->protocol_data = NULL;
    
    // Finalize protocol-specific initialization (e.g. set stream_worker)
    // Protocol can use listener_es->worker now
    if (s_datagram_ops && s_datagram_ops->protocol_finalize) {
        if (s_datagram_ops->protocol_finalize(l_datagram_flow) != 0) {
            log_it(L_ERROR, "Protocol finalize failed");
            s_datagram_ops->protocol_destroy(l_datagram_flow);
            return NULL;
        }
    }
    
    log_it(L_DEBUG, "Created DATAGRAM flow for %s",
           dap_io_flow_datagram_get_remote_addr_str(l_datagram_flow));
    
    return &l_datagram_flow->base;
}

/**
 * @brief Wrapper for flow_destroy - adds DATAGRAM-specific cleanup
 */
static void s_datagram_flow_destroy_wrapper(dap_io_flow_t *a_flow)
{
    if (!a_flow) {
        return;
    }
    
    dap_io_flow_datagram_t *l_datagram_flow = (dap_io_flow_datagram_t*)a_flow;
    
    log_it(L_DEBUG, "Destroying DATAGRAM flow for %s",
           dap_io_flow_datagram_get_remote_addr_str(l_datagram_flow));
    
    // Close separate send socket if exists (SERVER flows)
    if (l_datagram_flow->send_es) {
        // Remove from worker and delete (MT-safe via UUID)
        if (l_datagram_flow->send_es->worker) {
            dap_events_socket_remove_and_delete_mt(l_datagram_flow->send_es->worker, 
                                                   l_datagram_flow->send_es->uuid);
            log_it(L_DEBUG, "Removed and deleted separate send socket from worker");
        } else {
            // Fallback: if no worker, delete directly
            dap_events_socket_delete_unsafe(l_datagram_flow->send_es, true);
            log_it(L_DEBUG, "Deleted separate send socket (no worker)");
        }
        l_datagram_flow->send_es = NULL;
    }
    
    // Call protocol-specific destruction
    if (s_datagram_ops && s_datagram_ops->protocol_destroy) {
        s_datagram_ops->protocol_destroy(l_datagram_flow);
    }
    
    // Free DATAGRAM flow
    DAP_DELETE(l_datagram_flow);
}



