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
#include "dap_io_flow_socket.h"
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
    
    if (a_es->type != DESCRIPTOR_TYPE_SOCKET_UDP && 
        a_es->type != DESCRIPTOR_TYPE_SOCKET_CLIENT) {
        log_it(L_ERROR, "Socket is not datagram type");
        return -2;
    }
    
    // Check if we're in the esocket's worker thread
    dap_worker_t *l_current_worker = dap_worker_get_current();
    dap_worker_t *l_target_worker = a_es->worker;
    
    if (l_current_worker == l_target_worker) {
        // FAST PATH: Same worker, direct write
        memcpy(&a_es->addr_storage, a_addr, a_addr_len);
        a_es->addr_size = a_addr_len;
        
        return dap_events_socket_write_unsafe(a_es, a_data, a_size);
    } else {
        // SLOW PATH: Cross-worker, use callback
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
                                                 dap_events_socket_callbacks_t *a_callbacks)
{
    if (!a_server || !a_callbacks) {
        log_it(L_ERROR, "Invalid arguments for sharded listeners");
        return -1;
    }
    
    uint32_t l_worker_count = dap_proc_thread_get_count();
    bool l_enable_sharding = (l_worker_count > 1);
    
    // Create listener socket for each worker
    for (uint32_t i = 0; i < l_worker_count; i++) {
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
        
        // Set SO_REUSEPORT for sharding
        if (l_enable_sharding) {
#ifdef SO_REUSEPORT
            l_opt = 1;
            if (setsockopt(l_socket, SOL_SOCKET, SO_REUSEPORT, &l_opt, sizeof(l_opt)) < 0) {
                log_it(L_WARNING, "SO_REUSEPORT is not supported");
            }
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
        
        // Wrap socket in esocket
        dap_events_socket_t *l_es = dap_events_socket_wrap_no_add(l_socket, a_callbacks);
        if (!l_es) {
            log_it(L_ERROR, "Failed to wrap socket for worker %u", i);
            close(l_socket);
            return -5;
        }
        
        l_es->type = (a_socket_type == SOCK_DGRAM) ? DESCRIPTOR_TYPE_SOCKET_UDP : DESCRIPTOR_TYPE_SOCKET_CLIENT;
        
        // Add to worker
        dap_worker_add_events_socket_unsafe(l_worker, l_es);
        
        // Socket is ready to send packets
        l_es->flags |= DAP_SOCK_READY_TO_WRITE;
    }
    
    log_it(L_INFO, "Created %u sharded listener%s on %s:%u (type=%d, protocol=%d)",
           l_worker_count, (l_worker_count > 1) ? "s" : "",
           a_addr ? a_addr : "0.0.0.0", a_port, a_socket_type, a_protocol);
    
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

