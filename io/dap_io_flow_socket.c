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

#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "dap_common.h"
#include "dap_list.h"
#include "dap_io_flow_socket.h"
#include "dap_io_flow_ebpf.h"
#include "dap_worker.h"
#include "dap_server.h"
#include "dap_proc_thread.h"

#define LOG_TAG "dap_io_flow_socket"

// Thread-local buffer for address formatting
static __thread char s_addr_str_buf[INET6_ADDRSTRLEN + 8];

/**
 * @brief Arguments for cross-worker sendto callback
 */
typedef struct flow_sendto_args {
    dap_events_socket_t *esocket;
    uint8_t *data;
    size_t size;
    struct sockaddr_storage addr;
    socklen_t addr_len;
} flow_sendto_args_t;

/**
 * @brief Callback executed in listener's worker thread for sendto
 */
static void s_flow_sendto_callback(void *a_arg)
{
    flow_sendto_args_t *l_args = (flow_sendto_args_t*)a_arg;
    if (!l_args || !l_args->esocket) {
        DAP_DELETE(l_args);
        return;
    }
    
    dap_events_socket_t *l_es = l_args->esocket;
    
    // Update addr_storage for reactor's sendto
    memcpy(&l_es->addr_storage, &l_args->addr, l_args->addr_len);
    l_es->addr_size = l_args->addr_len;
    
    // Write data to buf_out (reactor will sendto)
    dap_events_socket_write_unsafe(l_es, l_args->data, l_args->size);
    
    // Cleanup
    DAP_DELETE(l_args->data);
    DAP_DELETE(l_args);
}

// =============================================================================
// PUBLIC API IMPLEMENTATION
// =============================================================================

int dap_io_flow_socket_send_to(dap_events_socket_t *a_es,
                                const uint8_t *a_data,
                                size_t a_size,
                                const struct sockaddr_storage *a_addr,
                                socklen_t a_addr_len)
{
    if (!a_es || !a_data || a_size == 0 || !a_addr) {
        log_it(L_ERROR, "Invalid arguments for sendto");
        return -1;
    }
    
    // DEBUG: Always log socket type
    log_it(L_DEBUG, "dap_io_flow_socket_send_to: esocket=%p, fd=%d, type=%d (UDP=%d, CLIENT=%d)",
           a_es, a_es->fd, a_es->type, DESCRIPTOR_TYPE_SOCKET_UDP, DESCRIPTOR_TYPE_SOCKET_CLIENT);
    
    if (a_es->type != DESCRIPTOR_TYPE_SOCKET_UDP && 
        a_es->type != DESCRIPTOR_TYPE_SOCKET_CLIENT) {
        log_it(L_ERROR, "Socket is not datagram type (esocket=%p, fd=%d, type=%d)", a_es, a_es->fd, a_es->type);
        return -2;
    }
    
    // Check if we're in the esocket's worker thread
    dap_worker_t *l_current_worker = dap_worker_get_current();
    dap_worker_t *l_target_worker = a_es->worker;
    
    log_it(L_DEBUG, "dap_io_flow_socket_send_to: size=%zu, current_worker=%u, target_worker=%u, fd=%d", 
           a_size, l_current_worker ? l_current_worker->id : 999, 
           l_target_worker ? l_target_worker->id : 999, a_es->fd);
    
    if (l_current_worker == l_target_worker) {
        // FAST PATH: Same worker, direct write
        memcpy(&a_es->addr_storage, a_addr, a_addr_len);
        a_es->addr_size = a_addr_len;
        
        // Debug: log destination address
        char l_addr_str[INET6_ADDRSTRLEN] = {0};
        uint16_t l_port = 0;
        if (a_addr->ss_family == AF_INET) {
            struct sockaddr_in *l_sin = (struct sockaddr_in*)a_addr;
            inet_ntop(AF_INET, &l_sin->sin_addr, l_addr_str, sizeof(l_addr_str));
            l_port = ntohs(l_sin->sin_port);
        } else if (a_addr->ss_family == AF_INET6) {
            struct sockaddr_in6 *l_sin6 = (struct sockaddr_in6*)a_addr;
            inet_ntop(AF_INET6, &l_sin6->sin6_addr, l_addr_str, sizeof(l_addr_str));
            l_port = ntohs(l_sin6->sin6_port);
        }
        
        log_it(L_DEBUG, "dap_io_flow_socket_send_to: sending %zu bytes to %s:%u via fd=%d",
               a_size, l_addr_str, l_port, a_es->fd);
        
        int l_ret = dap_events_socket_write_unsafe(a_es, a_data, a_size);
        log_it(L_DEBUG, "dap_io_flow_socket_send_to: FAST PATH write returned %d", l_ret);
        return l_ret;
    } else {
        // SLOW PATH: Cross-worker, use callback
        log_it(L_DEBUG, "dap_io_flow_socket_send_to: SLOW PATH (cross-worker)");
        flow_sendto_args_t *l_args = DAP_NEW_Z(flow_sendto_args_t);
        if (!l_args) {
            log_it(L_ERROR, "Failed to allocate sendto args");
            return -3;
        }
        
        l_args->esocket = a_es;
        l_args->data = DAP_NEW_SIZE(uint8_t, a_size);
        if (!l_args->data) {
            log_it(L_ERROR, "Failed to allocate data buffer");
            DAP_DELETE(l_args);
            return -4;
        }
        
        memcpy(l_args->data, a_data, a_size);
        l_args->size = a_size;
        memcpy(&l_args->addr, a_addr, a_addr_len);
        l_args->addr_len = a_addr_len;
        
        dap_worker_exec_callback_on(l_target_worker, s_flow_sendto_callback, l_args);
        
        return a_size;  // Queued successfully
    }
}

int dap_io_flow_socket_forward_packet(dap_events_socket_t *a_pipe_es,
                                       void *a_packet_ptr)
{
    if (!a_pipe_es || !a_packet_ptr) {
        log_it(L_ERROR, "Invalid arguments for packet forwarding");
        return -1;
    }
    
    if (a_pipe_es->type != DESCRIPTOR_TYPE_PIPE) {
        log_it(L_ERROR, "Socket is not PIPE type");
        return -2;
    }
    
    // Write pointer directly to pipe buf_out (ZERO-COPY)
    size_t l_written = dap_events_socket_write_unsafe(a_pipe_es, &a_packet_ptr, sizeof(void*));
    
    if (l_written != sizeof(void*)) {
        log_it(L_ERROR, "Failed to write packet pointer to pipe");
        return -3;
    }
    
    // Set writable flag to trigger reactor flush
    dap_events_socket_set_writable_unsafe(a_pipe_es, true);
    
    return 0;
}

int dap_io_flow_socket_create_sharded_listeners(dap_server_t *a_server,
                                                 const char *a_addr,
                                                 uint16_t a_port,
                                                 int a_socket_type,
                                                 int a_protocol,
                                                 dap_events_socket_callbacks_t *a_callbacks,
                                                 dap_io_flow_lb_tier_t *a_lb_tier_out)
{
    if (!a_server || !a_callbacks) {
        log_it(L_ERROR, "Invalid arguments for sharded listeners");
        return -1;
    }
    
    uint32_t l_worker_count = dap_proc_thread_get_count();
    bool l_is_udp = (a_socket_type == SOCK_DGRAM);
    
    // Detect best available load balancing tier
    dap_io_flow_lb_tier_t l_lb_tier = DAP_IO_FLOW_LB_TIER_NONE;
    if (l_is_udp && l_worker_count > 1) {
        l_lb_tier = dap_io_flow_detect_lb_tier();
    }
    
    // Return tier to caller
    if (a_lb_tier_out) {
        *a_lb_tier_out = l_lb_tier;
    }
    
    // Determine if we need SO_REUSEPORT (Tier 2: eBPF only)
    bool l_enable_reuseport = (l_lb_tier == DAP_IO_FLOW_LB_TIER_EBPF);
    
    // Determine number of listeners to create
    uint32_t l_num_listeners = 1;
    if (l_enable_reuseport) {
        // Kernel-level load balancing (Tier 2): one listener per worker with SO_REUSEPORT + eBPF
        l_num_listeners = l_worker_count;
        log_it(L_NOTICE, "Creating %u sharded listeners with SO_REUSEPORT + eBPF",
               l_num_listeners);
    } else {
        // Application-level (Tier 1) or no LB (Tier 0): single listener
        // Application-level will manually forward packets via queues
        log_it(L_NOTICE, "Creating single listener (tier=%d: %s)",
               l_lb_tier,
               l_lb_tier == DAP_IO_FLOW_LB_TIER_APPLICATION ? 
                   "application-level queue forwarding" : "no load balancing");
    }
    
    // Create listener socket for each worker
    for (uint32_t i = 0; i < l_num_listeners; i++) {
        dap_worker_t *l_worker = dap_events_worker_get(i);
        if (!l_worker) {
            log_it(L_ERROR, "Failed to get worker %u", i);
            return -2;
        }
        
        // Create socket
        int l_socket = socket(
            (a_addr && strchr(a_addr, ':')) ? AF_INET6 : AF_INET,
            a_socket_type,
            a_protocol);
        
        if (l_socket < 0) {
            log_it(L_ERROR, "Failed to create socket for worker %u", i);
            return -3;
        }
        
        // Set SO_REUSEADDR
        int l_opt = 1;
        if (setsockopt(l_socket, SOL_SOCKET, SO_REUSEADDR, &l_opt, sizeof(l_opt)) < 0) {
            log_it(L_WARNING, "Failed to set SO_REUSEADDR");
        }
        
        // Set SO_REUSEPORT if needed (Tier 2 or Tier 3)
        if (l_enable_reuseport) {
#ifdef SO_REUSEPORT
            l_opt = 1;
            if (setsockopt(l_socket, SOL_SOCKET, SO_REUSEPORT, &l_opt, sizeof(l_opt)) < 0) {
                log_it(L_ERROR, "SO_REUSEPORT failed: %s", strerror(errno));
                close(l_socket);
                return -6;
            }
#else
            log_it(L_ERROR, "SO_REUSEPORT not supported on this platform");
            close(l_socket);
            return -7;
#endif
        }
        
        // Bind socket
        struct sockaddr_storage l_bind_addr = {0};
        socklen_t l_addr_len;
        
        if (a_addr && strchr(a_addr, ':')) {
            // IPv6
            struct sockaddr_in6 *l_sa6 = (struct sockaddr_in6*)&l_bind_addr;
            l_sa6->sin6_family = AF_INET6;
            l_sa6->sin6_port = htons(a_port);
            if (a_addr) {
                inet_pton(AF_INET6, a_addr, &l_sa6->sin6_addr);
            } else {
                l_sa6->sin6_addr = in6addr_any;
            }
            l_addr_len = sizeof(struct sockaddr_in6);
        } else {
            // IPv4
            struct sockaddr_in *l_sa4 = (struct sockaddr_in*)&l_bind_addr;
            l_sa4->sin_family = AF_INET;
            l_sa4->sin_port = htons(a_port);
            if (a_addr) {
                inet_pton(AF_INET, a_addr, &l_sa4->sin_addr);
            } else {
                l_sa4->sin_addr.s_addr = INADDR_ANY;
            }
            l_addr_len = sizeof(struct sockaddr_in);
        }
        
        if (bind(l_socket, (struct sockaddr*)&l_bind_addr, l_addr_len) < 0) {
            log_it(L_ERROR, "Failed to bind socket for worker %u: %s", i, strerror(errno));
            close(l_socket);
            return -4;
        }
        
        // Attach load balancing mechanism (first socket only, eBPF only)
        if (i == 0 && l_is_udp && l_lb_tier == DAP_IO_FLOW_LB_TIER_EBPF) {
            // Tier 2: eBPF with SO_ATTACH_REUSEPORT_EBPF
            if (dap_io_flow_ebpf_attach_socket(l_socket) != 0) {
                log_it(L_CRITICAL, "FATAL: eBPF attach failed");
                close(l_socket);
                return -98;
            }
        }
        // Tier 1: Application-level - no kernel attachment needed
        
        // Wrap socket in esocket
        dap_events_socket_t *l_es = dap_events_socket_wrap_no_add(l_socket, a_callbacks);
        if (!l_es) {
            log_it(L_ERROR, "Failed to wrap socket for worker %u", i);
            close(l_socket);
            return -5;
        }
        
        l_es->type = (a_socket_type == SOCK_DGRAM) ? DESCRIPTOR_TYPE_SOCKET_UDP : DESCRIPTOR_TYPE_SOCKET_CLIENT;
        
        // CRITICAL FIX: Initialize addr_size for UDP sockets!
        // recvfrom() requires addr_size to be set to the buffer size BEFORE the call.
        // Without this, the first recvfrom() won't populate addr_storage!
        if (l_es->type == DESCRIPTOR_TYPE_SOCKET_UDP) {
            l_es->addr_size = sizeof(struct sockaddr_storage);
        }
        
        // Add to worker
        dap_worker_add_events_socket_unsafe(l_worker, l_es);
        
        // CRITICAL: is_initalized must be set for sockets added directly via _unsafe
        // (not through queue). Required for poll() systems and general correctness.
        l_es->is_initalized = true;
        
        debug_if(g_debug_reactor, L_DEBUG, 
                 "Sharded listener #%u: fd=%d added to worker %u", 
                 i, l_socket, l_worker->id);
        
        // Add to server's listener list
        a_server->es_listeners = dap_list_append(a_server->es_listeners, l_es);
        
        // Socket is ready to send packets - use proper API to set flag and update epoll
        dap_events_socket_set_writable_unsafe(l_es, true);
    }
    
    // Final summary
    const char *l_tier_desc = "unknown";
    switch (l_lb_tier) {
        case DAP_IO_FLOW_LB_TIER_EBPF:
            l_tier_desc = "eBPF (kernel sticky sessions with FNV-1a)";
            break;
        case DAP_IO_FLOW_LB_TIER_APPLICATION:
            l_tier_desc = "Application-level (queue-based distribution)";
            break;
        default:
            l_tier_desc = "None (single listener)";
            break;
    }
    
    log_it(L_INFO, "✅ Created %u listener%s on %s:%u - %s",
           l_num_listeners, (l_num_listeners > 1) ? "s" : "",
           a_addr ? a_addr : "0.0.0.0", a_port, l_tier_desc);
    
    return 0;
}

int dap_io_flow_socket_get_remote_addr(dap_events_socket_t *a_es,
                                        struct sockaddr_storage *a_addr,
                                        socklen_t *a_addr_len)
{
    if (!a_es || !a_addr || !a_addr_len) {
        return -1;
    }
    
    memcpy(a_addr, &a_es->addr_storage, a_es->addr_size);
    *a_addr_len = a_es->addr_size;
    
    return 0;
}

const char* dap_io_flow_socket_addr_to_string(const struct sockaddr_storage *a_addr)
{
    if (!a_addr) {
        return "(null)";
    }
    
    char ip_buf[INET6_ADDRSTRLEN];
    uint16_t port = 0;
    
    if (a_addr->ss_family == AF_INET) {
        const struct sockaddr_in *l_sa4 = (const struct sockaddr_in*)a_addr;
        inet_ntop(AF_INET, &l_sa4->sin_addr, ip_buf, sizeof(ip_buf));
        port = ntohs(l_sa4->sin_port);
    } else if (a_addr->ss_family == AF_INET6) {
        const struct sockaddr_in6 *l_sa6 = (const struct sockaddr_in6*)a_addr;
        inet_ntop(AF_INET6, &l_sa6->sin6_addr, ip_buf, sizeof(ip_buf));
        port = ntohs(l_sa6->sin6_port);
    } else {
        return "(unknown)";
    }
    
    snprintf(s_addr_str_buf, sizeof(s_addr_str_buf), "%s:%u", ip_buf, port);
    return s_addr_str_buf;
}

bool dap_io_flow_socket_addr_equal(const struct sockaddr_storage *a,
                                    const struct sockaddr_storage *b)
{
    if (!a || !b) {
        return false;
    }
    
    if (a->ss_family != b->ss_family) {
        return false;
    }
    
    if (a->ss_family == AF_INET) {
        const struct sockaddr_in *l_a4 = (const struct sockaddr_in*)a;
        const struct sockaddr_in *l_b4 = (const struct sockaddr_in*)b;
        return (l_a4->sin_addr.s_addr == l_b4->sin_addr.s_addr) &&
               (l_a4->sin_port == l_b4->sin_port);
    } else if (a->ss_family == AF_INET6) {
        const struct sockaddr_in6 *l_a6 = (const struct sockaddr_in6*)a;
        const struct sockaddr_in6 *l_b6 = (const struct sockaddr_in6*)b;
        return (memcmp(&l_a6->sin6_addr, &l_b6->sin6_addr, sizeof(struct in6_addr)) == 0) &&
               (l_a6->sin6_port == l_b6->sin6_port);
    }
    
    return false;
}

uint32_t dap_io_flow_socket_addr_hash(const struct sockaddr_storage *a_addr)
{
    if (!a_addr) {
        return 0;
    }
    
    uint32_t hash = 0;
    
    if (a_addr->ss_family == AF_INET) {
        const struct sockaddr_in *l_sa4 = (const struct sockaddr_in*)a_addr;
        hash = l_sa4->sin_addr.s_addr ^ l_sa4->sin_port;
    } else if (a_addr->ss_family == AF_INET6) {
        const struct sockaddr_in6 *l_sa6 = (const struct sockaddr_in6*)a_addr;
        const uint32_t *l_addr_words = (const uint32_t*)&l_sa6->sin6_addr;
        hash = l_addr_words[0] ^ l_addr_words[1] ^ l_addr_words[2] ^ l_addr_words[3] ^ l_sa6->sin6_port;
    }
    
    return hash;
}

