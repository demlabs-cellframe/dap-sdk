/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Contributors:
 * Copyright (c) 2017-2025 Demlabs Ltd <https://demlabs.net>
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

#include <string.h>
#include <arpa/inet.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_net_trans_udp_stream.h"
#include "dap_net_trans_udp_server.h"
#include "dap_net_trans.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_net.h"
#include "dap_enc_kyber.h"  // For Kyber512 KEM functions
#include "dap_transport_obfuscation.h"  // For handshake obfuscation
#include "json.h"  // For json-c API

#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif
#include "dap_stream_handshake.h"
#include "dap_stream.h"
#include "dap_server.h"
#include "dap_enc_server.h"
#include "dap_client.h"
#include "rand/dap_rand.h"
#include "dap_enc_key.h"
#include "dap_enc.h"
#include "dap_enc_kdf.h"
#include "dap_enc_ks.h"
#include "dap_enc_base64.h"
#include "dap_string.h"
#include "dap_net_trans_ctx.h"
#include <json-c/json.h>  // For JSON parsing

#define LOG_TAG "dap_stream_trans_udp"

// Debug flags
static bool s_debug_more = false;  // Extra verbose debugging

// UDP Trans Protocol Version
#define DAP_STREAM_UDP_VERSION 1

// Default configuration values
#define DAP_STREAM_UDP_DEFAULT_MAX_PACKET_SIZE  1400
#define DAP_STREAM_UDP_DEFAULT_KEEPALIVE_MS     30000

// Trans operations forward declarations
static int s_udp_init(dap_net_trans_t *a_trans, dap_config_t *a_config);
static void s_udp_deinit(dap_net_trans_t *a_trans);
static int s_udp_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port, 
                          dap_net_trans_connect_cb_t a_callback);
static int s_udp_listen(dap_net_trans_t *a_trans, const char *a_addr, uint16_t a_port,
                         dap_server_t *a_server);
static int s_udp_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out);
static int s_udp_handshake_init(dap_stream_t *a_stream,
                                 dap_net_handshake_params_t *a_params,
                                 dap_net_trans_handshake_cb_t a_callback);
static int s_udp_handshake_process(dap_stream_t *a_stream,
                                    const void *a_data, size_t a_data_size,
                                    void **a_response, size_t *a_response_size);
static int s_udp_session_create(dap_stream_t *a_stream,
                                 dap_net_session_params_t *a_params,
                                 dap_net_trans_session_cb_t a_callback);
static int s_udp_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_net_trans_ready_cb_t a_callback);
static ssize_t s_udp_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size);
static ssize_t s_udp_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static ssize_t s_udp_write_typed(dap_stream_t *a_stream, uint8_t a_pkt_type, 
                                  const void *a_data, size_t a_size);
static void s_udp_close(dap_stream_t *a_stream);
static uint32_t s_udp_get_capabilities(dap_net_trans_t *a_trans);
static void* s_udp_get_client_context(dap_stream_t *a_stream);
static int s_udp_stage_prepare(dap_net_trans_t *a_trans,
                               const dap_net_stage_prepare_params_t *a_params,
                               dap_net_stage_prepare_result_t *a_result);

// UDP trans operations table
static const dap_net_trans_ops_t s_udp_ops = {
    .init = s_udp_init,
    .deinit = s_udp_deinit,
    .connect = s_udp_connect,
    .listen = s_udp_listen,
    .accept = s_udp_accept,
    .handshake_init = s_udp_handshake_init,
    .handshake_process = s_udp_handshake_process,
    .session_create = s_udp_session_create,
    .session_start = s_udp_session_start,
    .read = s_udp_read,
    .write = s_udp_write,
    .close = s_udp_close,
    .get_capabilities = s_udp_get_capabilities,
    .register_server_handlers = NULL,  // UDP trans registers handlers via dap_stream_add_proc_udp
    .stage_prepare = s_udp_stage_prepare,
    .get_client_context = s_udp_get_client_context
};

// UDP per-stream context is now dap_net_trans_udp_ctx_t (defined in header)
// No need for separate client esocket context - everything is in UDP ctx

// Helper functions
static dap_stream_trans_udp_private_t *s_get_private(dap_net_trans_t *a_trans);
static dap_net_trans_udp_ctx_t *s_get_udp_ctx(dap_stream_t *a_stream);
// Made non-static for server.c to create UDP context for server-side streams
dap_net_trans_udp_ctx_t *s_get_or_create_udp_ctx(dap_stream_t *a_stream);
static int s_udp_handshake_response(dap_stream_t *a_stream, const void *a_data, size_t a_data_size);

/**
 * @brief UDP write callback for client esockets
 * 
 * Sends data from buf_out via send() on the connected UDP socket.
 * For UDP clients, the socket is already connected (via connect()) to the server.
 * 
 * UDP has MTU limitation (~1500 bytes), so we send data in chunks.
 */
static bool s_udp_client_write_callback(dap_events_socket_t *a_es, void *a_arg) {
    (void)a_arg;
    
    if (!a_es || !a_es->buf_out_size) {
        return true; // Nothing to write
    }
    
    // UDP MTU limit - send max 1400 bytes per packet (safe for most networks)
    // Leave room for UDP header (8 bytes) + IP header (20-60 bytes) + Ethernet (18 bytes)
    const size_t UDP_MAX_CHUNK = 1400;
    
    // Send data in chunks to respect MTU
    size_t l_chunk_size = (a_es->buf_out_size > UDP_MAX_CHUNK) ? UDP_MAX_CHUNK : a_es->buf_out_size;
    
    ssize_t l_sent = send(a_es->socket, 
                         (const char *)a_es->buf_out, 
                         l_chunk_size, 
                         0);
    
    if (l_sent < 0) {
        int l_errno = errno;
        if (l_errno == EAGAIN || l_errno == EWOULDBLOCK) {
            // Non-blocking socket, would block - for UDP we don't retry, just drop the packet
            log_it(L_WARNING, "UDP send would block, dropping packet (%zu bytes)", l_chunk_size);
            // Clear buffer to prevent infinite retry loop
            a_es->buf_out_size = 0;
            return true;  // Don't retry for UDP!
        }
        log_it(L_ERROR, "UDP client send failed: %s (errno %d, chunk_size=%zu)", 
               strerror(l_errno), l_errno, l_chunk_size);
        // Clear buffer on error
        a_es->buf_out_size = 0;
        return true;  // Don't retry on error
    }
    
    // Shift sent data out of buffer
    if ((size_t)l_sent < a_es->buf_out_size) {
        memmove(a_es->buf_out, a_es->buf_out + l_sent, a_es->buf_out_size - l_sent);
    }
    a_es->buf_out_size -= l_sent;
    
    // CRITICAL: For UDP, ALWAYS return true to prevent automatic retry
    // UDP is unreliable protocol - if packet can't be sent, it should be dropped, not retried indefinitely
    if (a_es->buf_out_size > 0) {
        log_it(L_WARNING, "UDP: %zu bytes remain in buf_out after send, clearing to prevent retry loop", a_es->buf_out_size);
        a_es->buf_out_size = 0;
    }
    return true;  // Always return true for UDP - no automatic retry!
}

/**
 * @brief UDP read callback for processing incoming packets
 * 
 * This callback is invoked when data arrives on a UDP socket (client or server virtual).
 * The trans_ctx is stored in esocket->_inheritor (always dap_net_trans_ctx_t).
 * 
 * Used by both:
 * - UDP client esockets (direct physical socket)
 * - UDP server virtual esockets (demultiplexed sessions)
 */
void dap_stream_trans_udp_read_callback(dap_events_socket_t *a_es, void *a_arg) {
    (void)a_arg;

    if (!a_es || !a_es->buf_in_size) {
        return;
    }

    debug_if(s_debug_more, L_DEBUG, "UDP client read callback: esocket %p (fd=%d), buf_in_size=%zu, callbacks.arg=%p",
             a_es, a_es->fd, a_es->buf_in_size, a_es->callbacks.arg);

    // Get trans_ctx from callbacks.arg (NOT _inheritor!)
    // _inheritor may point to client (dap_client_t), not trans_ctx!
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_es->callbacks.arg;

    if (!l_trans_ctx) {
        log_it(L_WARNING, "UDP client esocket has no trans_ctx (callbacks.arg is NULL), dropping %zu bytes", a_es->buf_in_size);
        a_es->buf_in_size = 0;
        return;
    }
    
    debug_if(s_debug_more,L_DEBUG, ">>> UDP READ CALLBACK: l_trans_ctx->stream=%p", l_trans_ctx->stream);
    
    if (!l_trans_ctx->stream) {
        log_it(L_WARNING, "UDP client trans_ctx %p has no stream (stream is NULL), dropping %zu bytes", 
               l_trans_ctx, a_es->buf_in_size);
        a_es->buf_in_size = 0;
        return;
    }
    
    dap_stream_t *l_stream = l_trans_ctx->stream;
    
    // Validate stream pointer first
    if (!l_stream) {
        log_it(L_WARNING, "UDP client stream pointer is NULL (stream closed?), dropping %zu bytes", a_es->buf_in_size);
        a_es->buf_in_size = 0;
        return;
    }
    
    // Validate stream->trans before accessing it
    // Check if stream has been deleted (trans would be NULL or invalid)
    if (!l_stream->trans) {
        log_it(L_WARNING, "UDP client stream has NULL trans (use-after-free?), dropping %zu bytes", a_es->buf_in_size);
        // Clear the dangling pointer to prevent future issues
        l_trans_ctx->stream = NULL;
        a_es->buf_in_size = 0;
        return;
    }
    
    // Validate trans operations
    if (!l_stream->trans->ops || !l_stream->trans->ops->read) {
        log_it(L_ERROR, "UDP client stream has invalid trans, dropping %zu bytes", a_es->buf_in_size);
        a_es->buf_in_size = 0;
        return;
    }
    
    // Process ALL packets in buffer (multiple packets may arrive before callback is called)
    // Don't manually call trans->ops->read - reactor fills buf_in automatically
    // Just process what's already in buffer
    int l_iterations = 0;
    const int l_max_iterations = 100; // Safety limit to prevent infinite loops
    
    while (a_es->buf_in_size > 0 && l_iterations < l_max_iterations) {
        size_t l_buf_in_size_before = a_es->buf_in_size;
        
        debug_if(s_debug_more, L_DEBUG, "Processing UDP packet from buf_in, buf_in_size=%zu (iteration %d)", 
                 a_es->buf_in_size, l_iterations);
        
        // Process one packet from buffer (s_udp_read will shrink buf_in)
        ssize_t l_result = s_udp_read(l_stream, NULL, 0);
        
        debug_if(s_debug_more, L_DEBUG, "s_udp_read returned %zd, buf_in_size now=%zu", 
                 l_result, a_es->buf_in_size);
        
        // If buf_in_size didn't change, break to prevent infinite loop
        if (a_es->buf_in_size == l_buf_in_size_before) {
            debug_if(s_debug_more, L_DEBUG, "UDP read: buf_in_size unchanged, breaking loop");
            break;
        }
        
        l_iterations++;
    }
    
    if (l_iterations >= l_max_iterations) {
        log_it(L_WARNING, "UDP client read callback: max iterations reached, %zu bytes remaining", a_es->buf_in_size);
    }
}

/**
 * @brief Register UDP trans adapter
 */
int dap_net_trans_udp_stream_register(void)
{
    // Initialize UDP server module first (registers server operations)
    int l_ret = dap_net_trans_udp_server_init();
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize UDP server module: %d", l_ret);
        return l_ret;
    }

    log_it(L_DEBUG, "dap_net_trans_udp_stream_register: UDP server module initialized, registering trans");
    
    // Register UDP trans operations
    int l_ret_trans = dap_net_trans_register("UDP",
                                                DAP_NET_TRANS_UDP_BASIC,
                                                &s_udp_ops,
                                                DAP_NET_TRANS_SOCKET_UDP,
                                                NULL);  // No inheritor needed at registration
    if (l_ret_trans != 0) {
        log_it(L_ERROR, "Failed to register UDP trans: %d", l_ret_trans);
        dap_net_trans_udp_server_deinit();
        return l_ret_trans;
    }

    log_it(L_NOTICE, "UDP trans registered successfully");
    return 0;
}

/**
 * @brief Unregister UDP trans adapter
 */
int dap_net_trans_udp_stream_unregister(void)
{
    int l_ret = dap_net_trans_unregister(DAP_NET_TRANS_UDP_BASIC);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to unregister UDP trans: %d", l_ret);
        return l_ret;
    }

    // Deinitialize UDP server module
    dap_net_trans_udp_server_deinit();

    log_it(L_NOTICE, "UDP trans unregistered successfully");
    return 0;
}

/**
 * @brief Create default UDP configuration
 */
dap_stream_trans_udp_config_t dap_stream_trans_udp_config_default(void)
{
    dap_stream_trans_udp_config_t l_config = {
        .max_packet_size = DAP_STREAM_UDP_DEFAULT_MAX_PACKET_SIZE,
        .keepalive_ms = DAP_STREAM_UDP_DEFAULT_KEEPALIVE_MS,
        .enable_checksum = true,
        .allow_fragmentation = false
    };
    return l_config;
}

/**
 * @brief Set UDP configuration
 */
int dap_stream_trans_udp_set_config(dap_net_trans_t *a_trans,
                                        const dap_stream_trans_udp_config_t *a_config)
{
    if (!a_trans || !a_config) {
        log_it(L_ERROR, "Invalid arguments for UDP config set");
        return -1;
    }

    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_trans);
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        return -1;
    }

    memcpy(&l_priv->config, a_config, sizeof(dap_stream_trans_udp_config_t));
    log_it(L_DEBUG, "UDP trans configuration updated");
    return 0;
}

/**
 * @brief Get UDP configuration
 */
int dap_stream_trans_udp_get_config(dap_net_trans_t *a_trans,
                                        dap_stream_trans_udp_config_t *a_config)
{
    if (!a_trans || !a_config) {
        log_it(L_ERROR, "Invalid arguments for UDP config get");
        return -1;
    }

    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_trans);
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        return -1;
    }

    memcpy(a_config, &l_priv->config, sizeof(dap_stream_trans_udp_config_t));
    return 0;
}

/**
 * @brief Check if stream is using UDP trans
 */
bool dap_stream_trans_is_udp(const dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans)
        return false;
    return a_stream->trans->type == DAP_NET_TRANS_UDP_BASIC;
}

/**
 * @brief Get UDP server from trans
 */
dap_server_t *dap_stream_trans_udp_get_server(const dap_stream_t *a_stream)
{
    if (!dap_stream_trans_is_udp(a_stream))
        return NULL;

    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_stream->trans);
    return l_priv ? l_priv->server : NULL;
}

/**
 * @brief Get UDP event socket from trans
 */
dap_events_socket_t *dap_stream_trans_udp_get_esocket(const dap_stream_t *a_stream)
{
    if (!dap_stream_trans_is_udp(a_stream))
        return NULL;

    return a_stream->trans_ctx ? a_stream->trans_ctx->esocket : NULL;
}

/**
 * @brief Get current session ID
 */
uint64_t dap_stream_trans_udp_get_session_id(const dap_stream_t *a_stream)
{
    if (!dap_stream_trans_is_udp(a_stream))
        return 0;

    // session_id is now per-stream, not per-transport
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx((dap_stream_t*)a_stream);
    return l_udp_ctx ? l_udp_ctx->session_id : 0;
}

/**
 * @brief Get current sequence number
 */
uint32_t dap_stream_trans_udp_get_seq_num(const dap_stream_t *a_stream)
{
    if (!dap_stream_trans_is_udp(a_stream))
        return 0;

    // seq_num is now per-stream, not per-transport
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx((dap_stream_t*)a_stream);
    return l_udp_ctx ? l_udp_ctx->seq_num : 0;
}

/**
 * @brief Set remote peer address
 * @deprecated Remote address is now per-stream, stored in UDP context.
 *             Use esocket's addr_storage instead.
 */
int dap_stream_trans_udp_set_remote_addr(dap_net_trans_t *a_trans,
                                              const struct sockaddr *a_addr,
                                              socklen_t a_addr_len)
{
    (void)a_trans;
    (void)a_addr;
    (void)a_addr_len;
    
    // remote_addr is now per-stream (in dap_net_trans_udp_ctx_t), not per-transport
    // This function is deprecated
    log_it(L_WARNING, "dap_stream_trans_udp_set_remote_addr is deprecated: remote_addr is now per-stream");
    return 0; // Return success for compatibility
}

/**
 * @brief Get remote peer address
 * @deprecated Remote address is now per-stream, stored in UDP context.
 *             Use esocket's addr_storage instead.
 */
int dap_stream_trans_udp_get_remote_addr(dap_net_trans_t *a_trans,
                                              struct sockaddr *a_addr,
                                              socklen_t *a_addr_len)
{
    (void)a_trans;
    (void)a_addr;
    (void)a_addr_len;
    
    // remote_addr is now per-stream (in dap_net_trans_udp_ctx_t), not per-transport
    // This function is deprecated
    log_it(L_WARNING, "dap_stream_trans_udp_get_remote_addr is deprecated: remote_addr is now per-stream");
    return -1; // Return error to indicate data not available
}

//=============================================================================
// Tranport operations implementation
//=============================================================================

/**
 * @brief Initialize UDP trans
 */
static int s_udp_init(dap_net_trans_t *a_trans, dap_config_t *a_config)
{
    if (!a_trans) {
        log_it(L_ERROR, "Cannot init NULL trans");
        return -1;
    }

    dap_stream_trans_udp_private_t *l_priv = DAP_NEW_Z(dap_stream_trans_udp_private_t);
    if (!l_priv) {
        log_it(L_CRITICAL, "Memory allocation failed for UDP private data");
        return -1;
    }

    // Initialize per-transport (shared) data only
    l_priv->config = dap_stream_trans_udp_config_default();
    l_priv->server = NULL;
    l_priv->user_data = NULL;
    
    // Per-stream data (session_id, seq_num, alice_key, remote_addr) is now in dap_net_trans_udp_ctx_t
    
    // Read debug configuration
    log_it(L_NOTICE, "UDP transport init called: a_config=%p", a_config);
    if (a_config) {
        s_debug_more = dap_config_get_item_bool_default(a_config, "stream_udp", "debug_more", false);
        log_it(L_NOTICE, "UDP transport: read debug_more=%d from config section [stream_udp]", s_debug_more);
        if (s_debug_more) {
            log_it(L_NOTICE, "UDP transport: verbose debugging ENABLED");
        }
    } else {
        log_it(L_WARNING, "UDP transport init: no config provided, debug_more remains disabled");
    }
    
    UNUSED(a_config); // Config can be used to override defaults

    a_trans->_inheritor = l_priv;
    
    // UDP trans doesn't support session control (connectionless)
    a_trans->has_session_control = false;
    a_trans->mtu = DAP_STREAM_UDP_DEFAULT_MAX_PACKET_SIZE;
    
    log_it(L_DEBUG, "UDP trans initialized (uses dap_events_socket for I/O)");
    return 0;
}

/**
 * @brief Deinitialize UDP trans
 */
static void s_udp_deinit(dap_net_trans_t *a_trans)
{
    if (!a_trans)
        return;

    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_trans);
    if (l_priv) {
        // alice_key is now per-stream, cleaned up in s_udp_close
        DAP_DELETE(l_priv);
        a_trans->_inheritor = NULL;
        log_it(L_DEBUG, "UDP trans deinitialized");
    }
}

/**
 * @brief Connect to remote UDP endpoint
 */
static int s_udp_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port,
                          dap_net_trans_connect_cb_t a_callback)
{
    if (!a_stream || !a_host) {
        log_it(L_ERROR, "Invalid arguments for UDP connect");
        return -1;
    }

    if (!a_stream->trans) {
        log_it(L_ERROR, "Stream has no trans");
        return -1;
    }

    // Get UDP per-stream context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_or_create_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        log_it(L_ERROR, "Failed to get UDP context");
        return -1;
    }

    // Parse address and store in remote_addr
    struct sockaddr_in *l_addr_in = (struct sockaddr_in*)&l_udp_ctx->remote_addr;
    l_addr_in->sin_family = AF_INET;
    l_addr_in->sin_port = htons(a_port);
    
    if (inet_pton(AF_INET, a_host, &l_addr_in->sin_addr) != 1) {
        log_it(L_ERROR, "Invalid IPv4 address: %s", a_host);
        return -1;
    }

    l_udp_ctx->remote_addr_len = sizeof(struct sockaddr_in);
    
    debug_if(s_debug_more, L_DEBUG, "UDP trans connected to %s:%u, calling callback %p", 
             a_host, a_port, a_callback);
    
    // Call callback immediately (UDP is connectionless)
    if (a_callback) {
        a_callback(a_stream, 0);
        debug_if(s_debug_more, L_DEBUG, "UDP connect callback completed");
    }
    
    return 0;
}

/**
 * @brief Start listening for UDP connections
 */
static int s_udp_listen(dap_net_trans_t *a_trans, const char *a_addr, uint16_t a_port,
                         dap_server_t *a_server)
{
    if (!a_trans) {
        log_it(L_ERROR, "Invalid arguments for UDP listen");
        return -1;
    }

    dap_stream_trans_udp_private_t *l_priv = 
        (dap_stream_trans_udp_private_t*)a_trans->_inheritor;
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        return -1;
    }

    // Store server reference
    l_priv->server = a_server;
    
    // UDP listening is handled by dap_server_t which creates dap_events_socket_t
    // The server will call callbacks registered via dap_stream_add_proc_udp()
    // which use dap_events_socket for all I/O operations
    log_it(L_INFO, "UDP trans listening on %s:%u (via dap_events_socket)", 
           a_addr ? a_addr : "0.0.0.0", a_port);
    return 0;
}

/**
 * @brief Accept incoming UDP "connection"
 */
static int s_udp_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out)
{
    if (!a_listener || !a_stream_out) {
        log_it(L_ERROR, "Invalid arguments for UDP accept");
        return -1;
    }
    
    // UDP is connectionless, so "accept" creates a new stream for datagram source
    // Stream is created by server layer and associated with socket
    log_it(L_DEBUG, "UDP trans accept");
    return 0;
}

static dap_net_trans_ctx_t *s_udp_get_or_create_ctx(dap_stream_t *a_stream) {
    debug_if(s_debug_more, L_INFO, "s_udp_get_or_create_ctx: stream=%p, trans_ctx=%p", a_stream, a_stream ? a_stream->trans_ctx : NULL);
    if (!a_stream->trans_ctx) {
        debug_if(s_debug_more,L_INFO, "s_udp_get_or_create_ctx: Creating NEW trans_ctx");
        a_stream->trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
        if (a_stream->trans) {
            a_stream->trans_ctx->trans = a_stream->trans;
        }
    }
    debug_if(s_debug_more, L_INFO, "s_udp_get_or_create_ctx: Returning trans_ctx=%p", a_stream->trans_ctx);
    return a_stream->trans_ctx;
}

/**
 * @brief Initialize encryption handshake
 */
static int s_udp_handshake_init(dap_stream_t *a_stream,
                                 dap_net_handshake_params_t *a_params,
                                 dap_net_trans_handshake_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid arguments for UDP handshake init");
        return -1;
    }

    if (!a_stream->trans) {
        log_it(L_ERROR, "Stream has no trans");
        return -1;
    }

    log_it(L_INFO, "UDP handshake init: enc_type=%d, pkey_type=%d",
           a_params->enc_type, a_params->pkey_exchange_type);
    
    // Get or create trans_ctx
    dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(a_stream);
    if (!l_ctx) {
        log_it(L_ERROR, "Failed to get trans_ctx");
        return -1;
    }
    
    l_ctx->handshake_cb = a_callback;
    
    // Get or create UDP per-stream context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_or_create_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        log_it(L_ERROR, "Failed to create UDP stream context");
        return -1;
    }
    
    // Set up read callback for client esocket and link stream
    if (l_ctx->esocket) {
        debug_if(s_debug_more, L_DEBUG, "Setting up UDP client esocket %p (fd=%d) for handshake_init", 
                 l_ctx->esocket, l_ctx->esocket->fd);
        
        // Store stream pointer
        l_udp_ctx->stream = a_stream;
        l_ctx->stream = a_stream;
        
        // IMPORTANT: Store trans_ctx in callbacks.arg (NOT _inheritor!)
        // _inheritor is owned by client infrastructure (may be dap_client_t or NULL)
        // We use callbacks.arg to pass trans_ctx to read callback
        l_ctx->esocket->callbacks.arg = l_ctx;
        
        debug_if(s_debug_more, L_DEBUG, "trans_ctx %p stored in callbacks.arg, trans_ctx->stream = %p, esocket->_inheritor=%p (client)", 
                 l_ctx, a_stream, l_ctx->esocket->_inheritor);
        
        // Set read callback
        if (!l_ctx->esocket->callbacks.read_callback) {
            l_ctx->esocket->callbacks.read_callback = dap_stream_trans_udp_read_callback;
            debug_if(s_debug_more, L_DEBUG, "Set UDP client read callback for esocket %p", l_ctx->esocket);
        }
    }
    
    // Generate random session ID for THIS stream
    if (randombytes((uint8_t*)&l_udp_ctx->session_id, sizeof(l_udp_ctx->session_id)) != 0) {
        log_it(L_ERROR, "Failed to generate random session ID");
        return -1;
    }
    l_udp_ctx->seq_num = 0;
    
    // Generate Alice key for THIS stream
    if (l_udp_ctx->alice_key) {
        dap_enc_key_delete(l_udp_ctx->alice_key);
    }
    
    l_udp_ctx->alice_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    if (!l_udp_ctx->alice_key) {
        log_it(L_ERROR, "Failed to generate Alice key for UDP handshake");
        return -1;
    }
    
    // Use OUR generated public key (not a_params), since we need the matching private key for decapsulation
    void *l_alice_pub = l_udp_ctx->alice_key->pub_key_data;
    size_t l_alice_pub_size = l_udp_ctx->alice_key->pub_key_data_size;
    
    // Send HANDSHAKE packet with alice public key via s_udp_write_typed
    ssize_t l_sent = s_udp_write_typed(a_stream, DAP_STREAM_UDP_PKT_HANDSHAKE, 
                                        l_alice_pub, l_alice_pub_size);
    
    if (l_sent < 0) {
        log_it(L_ERROR, "Failed to send UDP handshake init");
        return -1;
    }
    
    log_it(L_INFO, "UDP handshake init sent: %zd bytes (session_id=%lu)", 
           l_sent, l_udp_ctx->session_id);
    
    return 0;
}

/**
 * @brief Process handshake response from server (client-side)
 * @param a_stream Client stream
 * @param a_data Bob's public key + ciphertext from server
 * @param a_data_size Size of response data
 * @return 0 on success, negative on error
 */
static int s_udp_handshake_response(dap_stream_t *a_stream,
                                     const void *a_data, size_t a_data_size)
{
    if (!a_stream || !a_data || a_data_size == 0) {
        log_it(L_ERROR, "Invalid arguments for UDP handshake response");
        return -1;
    }

    log_it(L_DEBUG, "UDP handshake response: processing %zu bytes", a_data_size);
    
    // Validate size: Bob's ciphertext (768 bytes) + session_id (8 bytes) = 776 bytes
    const size_t EXPECTED_SIZE = CRYPTO_CIPHERTEXTBYTES + sizeof(uint64_t);
    if (a_data_size != EXPECTED_SIZE) {
        log_it(L_ERROR, "Invalid handshake response size: %zu (expected %zu = %d ciphertext + 8 session_id)",
               a_data_size, EXPECTED_SIZE, CRYPTO_CIPHERTEXTBYTES);
        return -1;
    }
    
    // Get Alice's key from UDP per-stream context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        log_it(L_ERROR, "UDP handshake response: no UDP context");
        return -1;
    }
    if (!l_udp_ctx->alice_key) {
        log_it(L_ERROR, "UDP handshake response: no Alice key");
        return -1;
    }
    
    // Deserialize: Bob's ciphertext (CRYPTO_CIPHERTEXTBYTES) + session_id (8 bytes)
    uint8_t l_bob_ciphertext[CRYPTO_CIPHERTEXTBYTES];
    uint64_t l_session_id_be;
    
    int l_ret = dap_deserialize_multy(a_data, a_data_size,
                                      l_bob_ciphertext, (uint64_t)CRYPTO_CIPHERTEXTBYTES,
                                      &l_session_id_be, sizeof(uint64_t),
                                      DOOF_PTR);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to deserialize handshake response");
        return -1;
    }
    
    uint64_t l_server_session_id = be64toh(l_session_id_be);
    
    debug_if(s_debug_more, L_DEBUG,
             "HANDSHAKE response: ciphertext=%zu bytes, server_session_id=0x%lx (replacing client's 0x%lx)",
             (size_t)CRYPTO_CIPHERTEXTBYTES, l_server_session_id, l_udp_ctx->session_id);
    
    // CRITICAL: Replace client's session_id with server's session_id!
    // All subsequent packets MUST use server's session_id
    l_udp_ctx->session_id = l_server_session_id;
    
    // DEBUG: Log Alice's public key that we sent (first 16 bytes)
    if (s_debug_more && l_udp_ctx->alice_key->pub_key_data && l_udp_ctx->alice_key->pub_key_data_size >= 16) {
        char l_hex[49] = {0};
        for (int i = 0; i < 16; i++) {
            sprintf(l_hex + i*3, "%02x ", ((uint8_t*)l_udp_ctx->alice_key->pub_key_data)[i]);
        }
        log_it(L_DEBUG, "CLIENT sent Alice public key (first 16 bytes): %s", l_hex);
    }
    
    // DEBUG: Log received ciphertext (first 16 bytes)
    if (s_debug_more && a_data && a_data_size >= 16) {
        char l_hex[49] = {0};
        for (int i = 0; i < 16; i++) {
            sprintf(l_hex + i*3, "%02x ", ((uint8_t*)a_data)[i]);
        }
        log_it(L_DEBUG, "CLIENT received ciphertext from server (first 16 bytes): %s", l_hex);
    }
    
    dap_enc_key_t *l_alice_key = l_udp_ctx->alice_key;
    log_it(L_DEBUG, "UDP handshake response: alice_key=%p, gen_alice_shared_key=%p", 
           l_alice_key, l_alice_key->gen_alice_shared_key);
    
    // UDP uses BINARY protocol, not JSON!
    // Server sends: Bob's ciphertext (768 bytes) + session_id (8 bytes)
    // We already extracted session_id above, now use ciphertext for KEM
    
    // Perform KEM decapsulation (Alice side) using received ciphertext
    if (!l_alice_key->gen_alice_shared_key) {
        log_it(L_ERROR, "Alice key doesn't support KEM decapsulation");
            return -1;
        }
    
    size_t l_shared_key_size = l_alice_key->gen_alice_shared_key(
        l_alice_key,
        NULL,  // Alice's private key (already in l_alice_key->_inheritor)
        CRYPTO_CIPHERTEXTBYTES,  // Size of Bob's ciphertext
        (uint8_t*)l_bob_ciphertext  // Bob's ciphertext (extracted above)
    );
    
    if (l_shared_key_size == 0 || !l_alice_key->shared_key) {
        log_it(L_ERROR, "Failed to derive shared key from Bob's ciphertext");
        return -1;
    }
    
    log_it(L_INFO, "CLIENT: derived shared key via KEM decapsulation (%zu bytes)", 
           l_shared_key_size);
    
    // Derive HANDSHAKE cipher key from shared secret using KDF-SHAKE256
    dap_enc_key_t *l_handshake_key = dap_enc_kdf_create_cipher_key(
        l_alice_key,
        DAP_ENC_KEY_TYPE_SALSA2012,
        "udp_handshake",
        14,
        0,  // Counter = 0
        32  // Key size
    );
    
    if (!l_handshake_key) {
        log_it(L_ERROR, "Failed to derive handshake cipher key via KDF");
        return -1;
    }
    
    log_it(L_INFO, "CLIENT: handshake key derived via KDF-SHAKE256");
    
    // Store handshake key in UDP context (will be used for encryption/decryption)
    if (l_udp_ctx->handshake_key) {
        log_it(L_WARNING, "CLIENT: replacing existing handshake_key %p with new one %p",
               l_udp_ctx->handshake_key, l_handshake_key);
        dap_enc_key_delete(l_udp_ctx->handshake_key);
    }
    l_udp_ctx->handshake_key = l_handshake_key;
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT: stored handshake_key=%p for session_id=0x%lx",
             l_udp_ctx->handshake_key, l_udp_ctx->session_id);
    
    // Create session if it doesn't exist
    if (!a_stream->session) {
        a_stream->session = dap_stream_session_pure_new();
        if (!a_stream->session) {
            log_it(L_ERROR, "Failed to create session");
            return -1;
        }
    }
    
    // DO NOT create session key here! It will be received and decrypted during SESSION_CREATE
    a_stream->session->key = NULL;
    
    log_it(L_INFO, "UDP handshake complete: CLIENT handshake key established, waiting for session key from server");
    return 0;
}

/**
 * @brief Process incoming handshake data (server-side)
 */
static int s_udp_handshake_process(dap_stream_t *a_stream,
                                    const void *a_data, size_t a_data_size,
                                    void **a_response, size_t *a_response_size)
{
    if (!a_stream || !a_data || a_data_size == 0) {
        log_it(L_ERROR, "Invalid arguments for UDP handshake process");
        return -1;
    }

    log_it(L_DEBUG, "UDP handshake process: %zu bytes", a_data_size);
    
    // DEBUG: Log Alice's public key (first 16 bytes)
    if (a_data && a_data_size >= 16) {
        char l_hex[49] = {0};
        for (int i = 0; i < 16; i++) {
            sprintf(l_hex + i*3, "%02x ", ((uint8_t*)a_data)[i]);
        }
        log_it(L_INFO, "SERVER received Alice public key (first 16 bytes): %s", l_hex);
    }
    
    // Generate ephemeral Bob key (Kyber512)
    dap_enc_key_t *l_bob_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    if (!l_bob_key) {
        log_it(L_ERROR, "Failed to generate Bob key");
        return -1;
    }
    
    void *l_bob_pub = NULL;
    size_t l_bob_pub_size = 0;
    void *l_shared_key = NULL;
    size_t l_shared_key_size = 0;
    
    if (l_bob_key->gen_bob_shared_key) {
        l_shared_key_size = l_bob_key->gen_bob_shared_key(l_bob_key, a_data, a_data_size, &l_bob_pub);
        l_shared_key = l_bob_key->shared_key;  // CORRECT: shared_key, not priv_key_data!
        l_bob_pub_size = l_bob_key->pub_key_data_size;
        
        // Check if key generation succeeded
        if (!l_bob_pub || l_shared_key_size == 0 || !l_shared_key) {
            log_it(L_ERROR, "Failed to generate shared key from client data (invalid public key?)");
            dap_enc_key_delete(l_bob_key);
            return -1;
        }
    } else {
        log_it(L_ERROR, "Key type doesn't support KEM handshake");
        dap_enc_key_delete(l_bob_key);
        return -1;
    }
    
    // Create session and set key
    if (!a_stream->session) {
        a_stream->session = dap_stream_session_pure_new();
    }
    if (a_stream->session) {
        if (a_stream->session->key) dap_enc_key_delete(a_stream->session->key);
        
        // DEBUG: Log Bob's ciphertext (first 16 bytes)
        debug_if(s_debug_more, L_DEBUG, "SERVER: Bob's ciphertext size=%zu", l_bob_pub_size);
        if (s_debug_more && l_bob_pub && l_bob_pub_size >= 16) {
            char l_hex[49] = {0};
            for (int i = 0; i < 16; i++) {
                sprintf(l_hex + i*3, "%02x ", ((uint8_t*)l_bob_pub)[i]);
            }
            log_it(L_DEBUG, "SERVER sending ciphertext to Alice (first 16 bytes): %s", l_hex);
        }
        
        // DEBUG: Log first 16 bytes of shared secret
        debug_if(s_debug_more, L_DEBUG, "SERVER: shared secret size=%zu", l_shared_key_size);
        if (s_debug_more && l_shared_key && l_shared_key_size >= 16) {
            char l_hex[49] = {0};
            for (int i = 0; i < 16; i++) {
                sprintf(l_hex + i*3, "%02x ", ((uint8_t*)l_shared_key)[i]);
            }
            log_it(L_DEBUG, "SERVER shared secret (first 16 bytes): %s", l_hex);
        }
        
        // Create HANDSHAKE key from shared secret using KDF (NOT session key yet!)
        // This key will be used to encrypt/decrypt the session key seed
        // Using KDF with context "handshake" and counter 0 (no ratcheting for handshake)
        dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
        if (l_udp_ctx) {
            if (l_udp_ctx->handshake_key) {
                dap_enc_key_delete(l_udp_ctx->handshake_key);
            }
            
            // Use NEW KDF API: derive handshake key from Bob's KEM key
            l_udp_ctx->handshake_key = dap_enc_kdf_create_cipher_key(
                l_bob_key,                      // KEM key with shared secret
                DAP_ENC_KEY_TYPE_SALSA2012,     // Cipher type for handshake encryption
                "udp_handshake",                // Context string
                14,                             // Context size (strlen("udp_handshake"))
                0,                              // Counter = 0 (no ratcheting for handshake)
                32                              // Key size (32 bytes for SALSA2012)
            );
            
            if (!l_udp_ctx->handshake_key) {
                log_it(L_ERROR, "SERVER: failed to derive handshake key using KDF");
                dap_enc_key_delete(l_bob_key);
                return -1;
            }
            
            log_it(L_INFO, "SERVER: handshake encryption key created using KDF (stream=%p, key=%p)", 
                   a_stream, l_udp_ctx->handshake_key);
        }
        
        // DO NOT create session key here! It will be created during SESSION_CREATE
        a_stream->session->key = NULL;
    }
    
    // Prepare JSON response for client's s_enc_init_response
    char l_bob_pub_b64[DAP_ENC_BASE64_ENCODE_SIZE(l_bob_pub_size) + 1];
    dap_enc_base64_encode(l_bob_pub, l_bob_pub_size, l_bob_pub_b64, DAP_ENC_DATA_TYPE_B64);
    
    char l_session_id_b64[DAP_ENC_BASE64_ENCODE_SIZE(sizeof(uint64_t) * 3) + 1]; // Plenty space for numeric string
    char l_session_id_str[64];
    snprintf(l_session_id_str, sizeof(l_session_id_str), "%lu", (unsigned long)time(NULL));
    dap_enc_base64_encode(l_session_id_str, strlen(l_session_id_str), l_session_id_b64, DAP_ENC_DATA_TYPE_B64);
    
    dap_string_t *l_json_resp = dap_string_new("");
    dap_string_append_printf(l_json_resp, 
        "[{\"session_id\":\"%s\"},{\"bob_message\":\"%s\"}]",
        l_session_id_b64, l_bob_pub_b64);

    if (a_response && a_response_size) {
        // Include null-terminator in response size for JSON parsing on client
        // JSON parser expects null-terminated string
        *a_response = l_json_resp->str;
        *a_response_size = l_json_resp->len + 1;  // +1 to include null-terminator
        DAP_DELETE(l_json_resp); // Free struct, keep str (dap_string->str is null-terminated)
    } else {
        dap_string_free(l_json_resp, true);
    }
    
    // l_bob_pub points to l_bob_key->pub_key_data (NOT separately allocated!)
    // It will be freed automatically when we delete l_bob_key below
    // DO NOT delete l_bob_pub separately - it causes double-free!
    
    // l_shared_key points to internal buffer of l_bob_key, so it is freed when l_bob_key is deleted
    // But we should zero it out if possible before delete (dap_enc_key_delete might do it)
    
    dap_enc_key_delete(l_bob_key);
    
    return 0;
}

/**
 * @brief Create session
 */
static int s_udp_session_create(dap_stream_t *a_stream,
                                 dap_net_session_params_t *a_params,
                                 dap_net_trans_session_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid arguments for UDP session create");
        return -1;
    }

    // Get UDP per-stream context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        log_it(L_ERROR, "No UDP context for session create");
        return -1;
    }

    // Store callback
    dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(a_stream);
    if (!l_ctx) {
        log_it(L_ERROR, "No trans_ctx for session create");
        return -1;
    }
    
    // DUPLICATE PROTECTION: Check if SESSION_CREATE already sent for this stream
    // This prevents multiple identical requests when FSM cycles
    if (l_ctx->session_create_sent) {
        debug_if(s_debug_more, L_DEBUG,
                 "CLIENT: ignoring duplicate session_create call (SESSION_CREATE already sent for this stream)");
        // Still update callback in case it changed
        l_ctx->session_create_cb = a_callback;
        return 0;  // Success, but don't send duplicate request
    }
    
    l_ctx->session_create_cb = a_callback;

    // Serialize session parameters (channels, encryption, etc) to send to server
    // Server needs to know which channels to activate
    const char *l_channels = a_params->channels ? a_params->channels : "";
    size_t l_channels_len = strlen(l_channels);
    
    // SECURITY: SESSION_CREATE must be encrypted with handshake key!
    if (!l_udp_ctx->handshake_key) {
        log_it(L_ERROR, "No handshake key for encrypting SESSION_CREATE");
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT: encrypting SESSION_CREATE with handshake_key=%p (session_id=0x%lx)",
             l_udp_ctx->handshake_key, l_udp_ctx->session_id);
    
    // Prepare JSON payload (NO session_id - it's already in internal header!)
    json_object *l_json = json_object_new_object();
    if (!l_json) {
        log_it(L_ERROR, "Failed to create JSON object for SESSION_CREATE");
        return -1;
    }
    
    // Add channels only (session_id is in internal header)
    json_object_object_add(l_json, "channels", json_object_new_string(l_channels));
    
    // Serialize to JSON string
    const char *l_json_str = json_object_to_json_string(l_json);
    size_t l_json_len = strlen(l_json_str);
    
    // Encrypt JSON with handshake key
    size_t l_encrypted_max = l_json_len + 256;  // Extra space for encryption overhead
    uint8_t *l_encrypted = DAP_NEW_SIZE(uint8_t, l_encrypted_max);
    if (!l_encrypted) {
        log_it(L_ERROR, "Failed to allocate encryption buffer");
        json_object_put(l_json);
        return -1;
    }
    
    size_t l_encrypted_size = dap_enc_code(l_udp_ctx->handshake_key,
                                           l_json_str, l_json_len,
                                           l_encrypted, l_encrypted_max,
                                           DAP_ENC_DATA_TYPE_RAW);
    
    json_object_put(l_json);  // Free JSON object
    
    if (l_encrypted_size == 0) {
        log_it(L_ERROR, "Failed to encrypt SESSION_CREATE payload");
        DAP_DELETE(l_encrypted);
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Encrypted SESSION_CREATE: %zu bytes (from %zu plaintext)", 
             l_encrypted_size, l_json_len);
    
    // Send encrypted SESSION_CREATE packet
    ssize_t l_sent = s_udp_write_typed(a_stream, DAP_STREAM_UDP_PKT_SESSION_CREATE,
                                        l_encrypted, l_encrypted_size);
    
    DAP_DELETE(l_encrypted);  // Free encryption buffer
    
    if (l_sent < 0) {
        log_it(L_ERROR, "Failed to send UDP session create request");
        return -1;
    }
    
    // Mark SESSION_CREATE as sent to prevent duplicates
    l_ctx->session_create_sent = true;
    
    log_it(L_INFO, "UDP session create request sent with channels '%s'", l_channels);
    
    return 0;
}

/**
 * @brief Start session
 */
static int s_udp_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_net_trans_ready_cb_t a_callback)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream for session start");
        return -1;
    }

    log_it(L_DEBUG, "UDP session start: session_id=%u", a_session_id);
    
    // Call callback immediately (UDP session ready)
    if (a_callback) {
        a_callback(a_stream, 0);
    }
    
    return 0;
}

/**
 * @brief Read data from UDP trans
 */
/**
 * @brief UDP read function for OBFUSCATED protocol
 * 
 * NEW ARCHITECTURE with 100% encryption:
 * - HANDSHAKE: Obfuscated packets (600-900 bytes, size-based encryption)
 * - ALL OTHER: Fully encrypted with internal header
 * 
 * NO plaintext metadata, NO fixed sizes!
 */
static ssize_t s_udp_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_stream->trans) {
        log_it(L_ERROR, "Invalid arguments for UDP read: stream or trans is NULL");
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG, "s_udp_read: stream=%p, buffer=%p, size=%zu", 
             a_stream, a_buffer, a_size);
    
    // Get esocket from trans_ctx
    dap_events_socket_t *l_es = NULL;
    dap_net_trans_ctx_t *l_ctx = (dap_net_trans_ctx_t*)a_stream->trans_ctx;
    if (l_ctx) {
        l_es = l_ctx->esocket;
    }

    debug_if(s_debug_more, L_DEBUG, "s_udp_read: ctx=%p, es=%p, buf_in=%p, buf_in_size=%zu", 
             l_ctx, l_es, l_es ? l_es->buf_in : NULL, l_es ? l_es->buf_in_size : 0);

    // NEW PROTOCOL: Two modes (CLIENT uses l_es->buf_in, SERVER uses a_buffer)
    if (!l_es) {
        // SERVER MODE: Server-side receives already-decrypted data from dap_io_flow!
        // The s_udp_packet_received_cb on server has already:
        // 1. Deobfuscated HANDSHAKE (if it was obfuscated)
        // 2. Decrypted SESSION_CREATE/DATA packets
        // 3. Parsed internal headers
        //
        // So here we just receive the final payload!
        
        debug_if(s_debug_more, L_INFO, "s_udp_read: SERVER MODE - data already processed by flow");
        
        if (!a_buffer || a_size == 0) {
            debug_if(s_debug_more, L_DEBUG, "UDP server read: no data");
            return 0;
        }
        
        // TODO: Server-side processing is now done in s_udp_packet_received_cb!
        // This path should not be reached in new architecture.
        log_it(L_WARNING, "SERVER MODE s_udp_read called unexpectedly (size=%zu)", a_size);
        return a_size;  // Consumed
    }
    
    // CLIENT MODE: Read from l_es->buf_in with OBFUSCATION support
    if (!l_es->buf_in || l_es->buf_in_size == 0) {
        debug_if(s_debug_more, L_DEBUG, "UDP CLIENT: no data in buf_in");
        return 0;  // No data available
    }
    
    debug_if(s_debug_more, L_DEBUG, "UDP CLIENT: buf_in_size=%zu", l_es->buf_in_size);
    
    // Get UDP context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_or_create_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        log_it(L_ERROR, "Failed to get UDP context in s_udp_read");
        return -1;
    }
    
    // TRY DEOBFUSCATE AS HANDSHAKE (size 600-900 bytes)
    // Obfuscation key is EPHEMERAL - used only for transport masking!
    if (dap_transport_is_obfuscated_handshake_size(l_es->buf_in_size)) {
        uint8_t *l_handshake = NULL;
        size_t l_handshake_size = 0;
        
        int l_ret = dap_transport_deobfuscate_handshake(l_es->buf_in, l_es->buf_in_size,
                                                        &l_handshake, &l_handshake_size);
        
        if (l_ret == 0) {
            // Successfully deobfuscated as HANDSHAKE!
            // Obfuscation key is NOW DISCARDED - it was just for masking!
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT: Deobfuscated HANDSHAKE: %zu bytes → %zu bytes (obf key discarded)",
                     l_es->buf_in_size, l_handshake_size);
            
            // DUPLICATE PROTECTION: Check if handshake already completed
            if (l_udp_ctx->handshake_key) {
                debug_if(s_debug_more, L_DEBUG,
                         "CLIENT: ignoring duplicate HANDSHAKE response (handshake_key already exists)");
                DAP_DELETE(l_handshake);
                dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
                return 0;  // Ignore duplicate
            }
            
            // Process handshake response (Kyber shared secret derivation)
            int l_result = s_udp_handshake_response(a_stream, l_handshake, l_handshake_size);
            DAP_DELETE(l_handshake);
            
            // Call handshake callback
            if (l_ctx && l_ctx->handshake_cb) {
                debug_if(s_debug_more, L_DEBUG,
                         "CLIENT: calling handshake_cb with result=%d", l_result);
                l_ctx->handshake_cb(a_stream, NULL, 0, l_result);
            }
            
            // Clear buffer
            dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
            return l_es->buf_in_size;  // Consumed
        }
        
        // Deobfuscation failed - might be regular encrypted packet
        // Continue to decrypt with session key
    }
    
    // ENCRYPTED PACKET: Decrypt with session key (derived from Kyber!)
    // NO obfuscation keys here - they were discarded after handshake!
    if (!l_udp_ctx->handshake_key) {
        log_it(L_ERROR, "CLIENT: no session key for decrypting packet");
        return -1;
    }
    
    // Decrypt entire packet
    size_t l_decrypted_max = l_es->buf_in_size + 256;
    uint8_t *l_decrypted = DAP_NEW_SIZE(uint8_t, l_decrypted_max);
    if (!l_decrypted) {
        log_it(L_ERROR, "Failed to allocate decryption buffer");
        return -1;
    }
    
    size_t l_decrypted_size = dap_enc_decode(l_udp_ctx->handshake_key,
                                             l_es->buf_in, l_es->buf_in_size,
                                             l_decrypted, l_decrypted_max,
                                             DAP_ENC_DATA_TYPE_RAW);
    
    if (l_decrypted_size == 0) {
        log_it(L_ERROR, "CLIENT: failed to decrypt packet");
        DAP_DELETE(l_decrypted);
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT: decrypted %zu bytes from %zu bytes encrypted",
             l_decrypted_size, l_es->buf_in_size);
    
    // Parse internal header: [type(1) + seq(4) + session_id(8) + payload]
    if (l_decrypted_size < DAP_STREAM_UDP_INTERNAL_HEADER_SIZE) {
        log_it(L_ERROR, "CLIENT: decrypted packet too small (%zu < %d)",
               l_decrypted_size, DAP_STREAM_UDP_INTERNAL_HEADER_SIZE);
        DAP_DELETE(l_decrypted);
        return -1;
    }
    
    dap_stream_trans_udp_internal_header_t *l_header =
        (dap_stream_trans_udp_internal_header_t*)l_decrypted;
    
    uint8_t l_type = l_header->type;
    uint32_t l_seq_num = ntohl(l_header->seq_num);
    uint64_t l_session_id = be64toh(l_header->session_id);
    
    size_t l_payload_size = l_decrypted_size - DAP_STREAM_UDP_INTERNAL_HEADER_SIZE;
    uint8_t *l_payload = l_decrypted + DAP_STREAM_UDP_INTERNAL_HEADER_SIZE;
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT: packet type=0x%02x, seq=%u, session=0x%lx, payload=%zu bytes",
             l_type, l_seq_num, l_session_id, l_payload_size);
    
    // Validate session_id
    if (l_udp_ctx->session_id != 0 && l_udp_ctx->session_id != l_session_id) {
        log_it(L_ERROR, "CLIENT: session_id mismatch: packet=0x%lx, ctx=0x%lx",
               l_session_id, l_udp_ctx->session_id);
        DAP_DELETE(l_decrypted);
        return -1;
    }
    
    // Update session_id if not set
    if (l_udp_ctx->session_id == 0) {
        l_udp_ctx->session_id = l_session_id;
        debug_if(s_debug_more, L_DEBUG,
                 "CLIENT: set session_id=0x%lx from server", l_session_id);
    }
    
    // Process based on packet type
    switch (l_type) {
        case DAP_STREAM_UDP_PKT_SESSION_CREATE: {
            // SESSION_CREATE response: contains KDF counter for session key derivation
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT: received SESSION_CREATE response (%zu bytes)", l_payload_size);
            
            // DUPLICATE PROTECTION: Check if session already has session key
            // Once session key is established, ignore all subsequent SESSION_CREATE responses
            dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(a_stream);
            if (!l_ctx) {
                log_it(L_ERROR, "CLIENT: no trans_ctx for SESSION_CREATE");
                DAP_DELETE(l_decrypted);
                return -1;
            }
            
            if (!l_ctx->session_create_cb) {
                debug_if(s_debug_more, L_DEBUG,
                         "CLIENT: ignoring duplicate SESSION_CREATE response (session_create_cb already cleared)");
                DAP_DELETE(l_decrypted);
                return a_size;
            }
            
            if (l_payload_size != sizeof(uint64_t)) {
                log_it(L_ERROR, "CLIENT: invalid SESSION_CREATE response size: %zu (expected 8)",
                       l_payload_size);
                DAP_DELETE(l_decrypted);
                return -1;
            }
            
            // Extract KDF counter
            uint64_t l_counter_be;
            memcpy(&l_counter_be, l_payload, sizeof(l_counter_be));
            uint64_t l_kdf_counter = be64toh(l_counter_be);
            
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT: KDF counter=%lu for session key derivation FROM KYBER shared secret",
                     l_kdf_counter);
            
            // Derive session key from handshake key using same counter
            // handshake_key was derived from Kyber shared secret!
            dap_enc_key_t *l_session_key = dap_enc_kdf_create_cipher_key(
                l_udp_ctx->handshake_key,
                DAP_ENC_KEY_TYPE_SALSA2012,
                "udp_session",  // MUST match server context!
                11,
                l_kdf_counter,
                32  // 256-bit session key
            );
            
            if (!l_session_key) {
                log_it(L_ERROR, "CLIENT: failed to derive SESSION key from Kyber-based HANDSHAKE key");
                DAP_DELETE(l_decrypted);
                return -1;
            }
            
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT: derived SESSION key (counter=%lu) from Kyber shared secret chain",
                     l_kdf_counter);
            
            debug_if(s_debug_more, L_DEBUG, "CLIENT: session key established");
            
            // CRITICAL: Set session key in stream->session for dap_client FSM!
            // dap_client checks a_stream->session->key to verify session is ready
            if (!a_stream->session) {
                a_stream->session = dap_stream_session_pure_new();
                if (!a_stream->session) {
                    log_it(L_ERROR, "CLIENT: failed to create stream session");
                    dap_enc_key_delete(l_session_key);
                    DAP_DELETE(l_decrypted);
                    return -1;
                }
            }
            
            // Set session key and ID
            if (a_stream->session->key) {
                dap_enc_key_delete(a_stream->session->key);
            }
            a_stream->session->key = l_session_key;
            a_stream->session->id = l_session_id;
            
            log_it(L_INFO, "CLIENT: session key installed in stream->session (session_id=0x%lx)", l_session_id);
            
            // CRITICAL: Call session_create callback to notify dap_client!
            // Client is waiting for this callback to advance from STAGE_STREAM_CTL
            if (l_ctx->session_create_cb) {
                debug_if(s_debug_more, L_DEBUG,
                         "CLIENT: calling session_create_cb for session 0x%lx",
                         l_session_id);
                l_ctx->session_create_cb(a_stream, (uint32_t)l_session_id, NULL, 0, 0);
                
                // Clear callback to prevent duplicate calls
                l_ctx->session_create_cb = NULL;
            } else {
                log_it(L_WARNING, "CLIENT: no session_create_cb registered! l_ctx=%p", l_ctx);
            }
            
            // NOW replace handshake_key with session_key (after callback)
            // NOTE: We don't delete session_key here because it's now owned by a_stream->session->key!
            // Just replace the pointer in l_udp_ctx for future packet encryption/decryption
            dap_enc_key_delete(l_udp_ctx->handshake_key);
            l_udp_ctx->handshake_key = l_session_key;  // Share the same key object
            
            break;
        }
        
        case DAP_STREAM_UDP_PKT_DATA: {
            // DATA packet: contains stream data
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT: received DATA packet (%zu bytes payload)", l_payload_size);
            
            if (l_payload_size == 0) {
                debug_if(s_debug_more, L_DEBUG, "CLIENT: empty DATA packet");
                break;
            }
            
            // Process stream data using transport-agnostic function
            size_t l_processed = dap_stream_data_proc_read_ext(a_stream,
                                                                l_payload,
                                                                l_payload_size);
            
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT: processed %zu bytes of stream data", l_processed);
            
            break;
        }
        
        case DAP_STREAM_UDP_PKT_KEEPALIVE: {
            // KEEPALIVE: just update activity timestamp
            debug_if(s_debug_more, L_DEBUG, "CLIENT: received KEEPALIVE");
            // Nothing to do, connection is alive
            break;
        }
        
        case DAP_STREAM_UDP_PKT_CLOSE: {
            // CLOSE: server closing connection
            log_it(L_INFO, "CLIENT: received CLOSE from server");
            
            // Mark stream as inactive and close the transport
            a_stream->is_active = false;
            
            if (a_stream->trans && a_stream->trans->ops && a_stream->trans->ops->close) {
                debug_if(s_debug_more, L_DEBUG, "CLIENT: calling transport close");
                a_stream->trans->ops->close(a_stream);
            }
            
            break;
        }
        
        default:
            log_it(L_WARNING, "CLIENT: unknown packet type 0x%02x", l_type);
            break;
    }
    
    // Cleanup
    DAP_DELETE(l_decrypted);
    
    // Clear processed data from buf_in
    dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
    
    return l_es->buf_in_size;  // Consumed all data
}

/**
 * @brief Write data with specified UDP packet type (internal helper)
        uint8_t l_packet_type = l_header->type;
        
        // Validate packet size
        size_t l_expected_size = sizeof(dap_stream_trans_udp_header_t) + l_payload_len;
        if (a_size < l_expected_size) {
            log_it(L_ERROR, "SERVER MODE: packet size mismatch (%zu < %zu)", a_size, l_expected_size);
            return -1;
        }
        
        // Extract payload pointer
        void *l_payload = (uint8_t*)a_buffer + sizeof(dap_stream_trans_udp_header_t);
        
        // Allocate decryption buffer (max size for encrypted payloads)
        size_t l_decrypted_max = l_payload_len * 2;  // Conservative estimate
        uint8_t *l_decrypted = DAP_NEW_Z_SIZE(uint8_t, l_decrypted_max);
        
        debug_if(s_debug_more, L_DEBUG, "SERVER MODE: packet type=%u, payload_len=%u", 
                 l_packet_type, l_payload_len);
        
        dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
        if (!l_udp_ctx) {
            log_it(L_ERROR, "Server stream has no UDP context");
            return -1;
        }
        
        // Process based on packet type
        switch (l_packet_type) {
            case DAP_STREAM_UDP_PKT_HANDSHAKE: {
                // Server: Process handshake request
                debug_if(s_debug_more, L_DEBUG, "Server: processing handshake request (%u bytes)", l_payload_len);
                void *l_response = NULL;
                size_t l_response_size = 0;
                int l_result = s_udp_handshake_process(a_stream, l_payload, l_payload_len, &l_response, &l_response_size);
                
                if (l_result == 0 && l_response && l_response_size > 0) {
                    // Send handshake response via s_udp_write_typed
                    ssize_t l_sent = s_udp_write_typed(a_stream, DAP_STREAM_UDP_PKT_HANDSHAKE,
                                                         l_response, l_response_size);
                    
                    if (l_sent < 0) {
                        log_it(L_ERROR, "SERVER: failed to send HANDSHAKE response");
                    } else {
                        debug_if(s_debug_more, L_DEBUG, "SERVER: handshake response sent successfully (%zd bytes)", l_sent);
                    }
                    
                    DAP_DELETE(l_response);
                }
                break;
            }
            
            case DAP_STREAM_UDP_PKT_SESSION_CREATE: {
                // Server: Process session create request with channels from client
                debug_if(s_debug_more, L_DEBUG, "Server: received SESSION_CREATE request for session 0x%lx", 
                         be64toh(l_header->session_id));
                
                uint64_t l_sess_id = be64toh(l_header->session_id);
                
                // Extract channels from payload (null-terminated string)
                const char *l_channels = (l_payload_len > 0) ? (const char*)l_payload : "";
                log_it(L_INFO, "Server: SESSION_CREATE with channels '%s' for session 0x%lx", 
                       l_channels, l_sess_id);
                
                // Session was already created during HANDSHAKE, update with channels
                if (!a_stream->session) {
                    a_stream->session = dap_stream_session_pure_new();
                    if (a_stream->session) {
                        a_stream->session->id = l_sess_id;
                    }
                }
                
                // CRITICAL: Set active_channels from client request
                if (a_stream->session && l_payload_len > 0) {
                    strncpy(a_stream->session->active_channels, l_channels, 
                            sizeof(a_stream->session->active_channels) - 1);
                    a_stream->session->active_channels[sizeof(a_stream->session->active_channels) - 1] = '\0';
                    
                    // Create channels for the session
                    size_t l_channel_count = strlen(a_stream->session->active_channels);
                    log_it(L_INFO, "Server: creating %zu channels for session 0x%lx", l_channel_count, l_sess_id);
                    
                    for (size_t i = 0; i < l_channel_count; i++) {
                        char l_ch_id = a_stream->session->active_channels[i];
                        dap_stream_ch_t *l_ch = dap_stream_ch_new(a_stream, (uint8_t)l_ch_id);
                        if (!l_ch) {
                            log_it(L_ERROR, "Server: failed to create channel '%c' for session 0x%lx", 
                                   l_ch_id, l_sess_id);
                            break;
                        }
                        l_ch->ready_to_read = true;
                        log_it(L_INFO, "Server: created channel '%c' for session 0x%lx", l_ch_id, l_sess_id);
                    }
                    
                    log_it(L_INFO, "Server: session 0x%lx now has %zu channels", l_sess_id, a_stream->channel_count);
                }
                
                // Use KDF to derive SESSION key from HANDSHAKE key (no seed transmission!)
                if (!l_udp_ctx->handshake_key) {
                    log_it(L_ERROR, "Server: no handshake key for deriving session key");
                    break;
                }
                
                // Counter for ratcheting (use counter = 1 for first session)
                uint64_t l_session_counter = 1;
                
                // Derive session key from handshake key using KDF
                if (a_stream->session->key) {
                    dap_enc_key_delete(a_stream->session->key);
                }
                a_stream->session->key = dap_enc_kdf_create_cipher_key(
                    l_udp_ctx->handshake_key,       // Source key
                    DAP_ENC_KEY_TYPE_SALSA2012,     // Cipher type
                    "udp_session",                   // Context
                    12,                              // Context size
                    l_session_counter,               // Counter
                    32);                             // Key size
                
                if (!a_stream->session->key) {
                    log_it(L_ERROR, "Server: failed to derive session key from handshake key using KDF");
                    break;
                }
                
                log_it(L_INFO, "Server: derived session key for session 0x%lx using KDF (counter=%lu)", 
                       l_sess_id, l_session_counter);
                
                // Send SESSION_CREATE response with KDF counter (client needs it to derive same key)
                uint64_t l_counter_be = htobe64(l_session_counter);
                
                ssize_t l_sent = s_udp_write_typed(a_stream, DAP_STREAM_UDP_PKT_SESSION_CREATE,
                                                     &l_counter_be, sizeof(l_counter_be));
                
                if (l_sent < 0) {
                    log_it(L_ERROR, "Server: failed to send SESSION_CREATE response for session 0x%lx", l_sess_id);
                } else {
                    debug_if(s_debug_more, L_DEBUG, "Server: sent SESSION_CREATE response with KDF counter (%lu) for session 0x%lx", 
                             l_session_counter, l_sess_id);
                }
                break;
            }
            
            case DAP_STREAM_UDP_PKT_DATA: {
                // Server: Process encrypted data packet
                // For DATA packets, we need to decrypt and process stream data
                debug_if(s_debug_more, L_DEBUG, "Server: processing DATA packet (%u bytes encrypted)", l_payload_len);
                
                // Debug: print first 64 bytes of stream packet
                if (l_payload_len > 0) {
                    char l_hex[256] = {0};
                    size_t l_print_size = l_payload_len > 64 ? 64 : l_payload_len;
                    for (size_t i = 0; i < l_print_size; i++) {
                        sprintf(l_hex + i*3, "%02x ", ((uint8_t*)l_payload)[i]);
                    }
                    log_it(L_INFO, "Server: stream packet first %zu bytes: %s", l_print_size, l_hex);
                }
                
                size_t l_decrypted_size = dap_enc_decode(a_stream->session->key, 
                                                         l_payload, l_payload_len,
                                                         l_decrypted, l_decrypted_max,
                                                         DAP_ENC_DATA_TYPE_RAW);
                
                if (l_decrypted_size == 0) {
                    log_it(L_ERROR, "SERVER: failed to decrypt DATA packet");
                    DAP_DELETE(l_decrypted);
                    break;
                }
                
                debug_if(s_debug_more, L_DEBUG, "Server: decrypted %zu bytes from %u bytes encrypted", 
                         l_decrypted_size, l_payload_len);
                
                // Process decrypted stream data by temporarily setting trans_ctx->esocket
                // This allows standard dap_stream_data_proc_read to work
                if (a_stream->trans_ctx) {
                    // Create temporary fake esocket for processing
                    // We'll use trans_ctx->esocket temporarily
                    dap_events_socket_t *l_saved_es = a_stream->trans_ctx->esocket;
                    
                    // Create minimal fake esocket with buf_in pointing to decrypted data
                    dap_events_socket_t l_fake_es = {0};
                    l_fake_es.buf_in = l_decrypted;
                    l_fake_es.buf_in_size = l_decrypted_size;
                    
                    a_stream->trans_ctx->esocket = &l_fake_es;
                    
                    // Process stream data (will parse stream packets and dispatch to channels)
                    size_t l_processed = dap_stream_data_proc_read(a_stream);
                    
                    debug_if(s_debug_more, L_DEBUG, "Server: processed %zu bytes of stream data", l_processed);
                    
                    // Restore original esocket
                    a_stream->trans_ctx->esocket = l_saved_es;
                }
                
                DAP_DELETE(l_decrypted);
                break;
            }
            
            default:
                log_it(L_WARNING, "SERVER: unknown packet type %u", l_packet_type);
                break;
        }
        
        DAP_DELETE(l_decrypted);  // Cleanup decryption buffer for all non-DATA cases
        return a_size;  // Consumed all dispatcher data
    }
    
    // CLIENT MODE: Use l_es->buf_in (existing logic)
    if (!l_es->buf_in) {
        debug_if(s_debug_more, L_DEBUG, "UDP read: no buf_in (l_es=%p, l_ctx=%p) - returning 0", l_es, l_ctx);
        return 0;  // No data available
    }
    
    debug_if(s_debug_more, L_DEBUG, "UDP read: esocket %p (fd=%d), buf_in_size=%zu", 
             l_es, l_es->fd, l_es->buf_in_size);

    // Check if we have enough data for UDP trans header
    if (l_es->buf_in_size < sizeof(dap_stream_trans_udp_header_t)) {
        return 0;
    }
    
    // Peek header
    dap_stream_trans_udp_header_t *l_header = (dap_stream_trans_udp_header_t*)l_es->buf_in;
    
    if (l_header->version == DAP_STREAM_UDP_VERSION) {
        size_t l_payload_size = ntohs(l_header->length);
        size_t l_total_size = sizeof(*l_header) + l_payload_size;
        
        debug_if(s_debug_more, L_DEBUG, "UDP read: version OK, payload_size=%zu, total_size=%zu, buf_in_size=%zu", 
                 l_payload_size, l_total_size, l_es->buf_in_size);
        
        if (l_es->buf_in_size < l_total_size) {
            debug_if(s_debug_more, L_DEBUG, "UDP read: waiting for full packet");
            return 0; // Wait for full packet
        }
        
        // Extract payload
        void *l_payload = NULL;
        if (l_payload_size > 0) {
            l_payload = DAP_NEW_SIZE(void, l_payload_size);
            if (!l_payload) {
                 log_it(L_CRITICAL, "Memory allocation failed");
                 return -1;
            }
            memcpy(l_payload, l_es->buf_in + sizeof(*l_header), l_payload_size);
        }

        dap_net_trans_ctx_t *l_ctx = (dap_net_trans_ctx_t*)a_stream->trans_ctx;
        
        // Get UDP per-stream context
        dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_or_create_udp_ctx(a_stream);
        if (!l_udp_ctx) {
            log_it(L_ERROR, "Failed to get UDP context in s_udp_read");
            if (l_payload) DAP_DELETE(l_payload);
            return -1;
        }
        
        debug_if(s_debug_more, L_DEBUG, "UDP packet processing: type=%u, a_stream=%p, trans_ctx=%p, l_ctx=%p, udp_ctx=%p", 
                 l_header->type, a_stream, a_stream->trans_ctx, l_ctx, l_udp_ctx);

        if (l_header->type == DAP_STREAM_UDP_PKT_HANDSHAKE) {
             debug_if(s_debug_more, L_DEBUG, "HANDSHAKE packet: l_ctx=%p, handshake_cb=%p", 
                      l_ctx, l_ctx ? l_ctx->handshake_cb : NULL);
             if (l_ctx && l_ctx->handshake_cb) {
                 // Client: Received Handshake Response (Bob Key + Ciphertext)
                 
                 // DUPLICATE PROTECTION: Check if handshake already completed
                 dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
                 if (l_udp_ctx && l_udp_ctx->handshake_key) {
                     debug_if(s_debug_more, L_DEBUG, "Client: ignoring duplicate HANDSHAKE response (handshake_key already established), shrinking buf_in by %zu bytes", l_total_size);
                     // CRITICAL: Must shrink buf_in even for duplicates, otherwise buf accumulates!
                     if (l_payload) DAP_DELETE(l_payload);
                     dap_events_socket_shrink_buf_in(l_es, l_total_size);
                     return 0;  // Ignore duplicate but clear buffer
                 }
                 
                 // Process it here to establish encryption, then call callback
                 debug_if(s_debug_more, L_DEBUG, "Client: processing handshake response, calling s_udp_handshake_response");
                 int l_result = s_udp_handshake_response(a_stream, l_payload, l_payload_size);
                 
                 debug_if(s_debug_more, L_DEBUG, "Handshake response processed, result=%d, calling callback", l_result);
                 // Call handshake callback with result (no data, just status)
                log_it(L_INFO, "UDP: About to call handshake_cb=%p with result=%d", l_ctx->handshake_cb, l_result);
                 l_ctx->handshake_cb(a_stream, NULL, 0, l_result);
                // DO NOT clear callback - duplicate protection will handle subsequent responses
                // l_ctx->handshake_cb = NULL;  // REMOVED - duplicate protection now checks handshake_key existence
                 debug_if(s_debug_more, L_DEBUG, "Handshake callback completed");
             } else {
                 // Server: Received Handshake Request (Alice Key)
                 debug_if(s_debug_more, L_DEBUG, "Server: processing handshake request");
                 void *l_response = NULL;
                 size_t l_response_size = 0;
                 s_udp_handshake_process(a_stream, l_payload, l_payload_size, &l_response, &l_response_size);
                 
                 if (l_response && l_response_size > 0) {
                    // Send handshake response via s_udp_write_typed
                    ssize_t l_sent = s_udp_write_typed(a_stream, DAP_STREAM_UDP_PKT_HANDSHAKE,
                                                         l_response, l_response_size);
                    
                    if (l_sent < 0) {
                        log_it(L_ERROR, "SERVER: failed to send HANDSHAKE response");
                    } else {
                        debug_if(s_debug_more, L_DEBUG, "SERVER: handshake response sent successfully (%zd bytes)", l_sent);
                    }
                    
                     DAP_DELETE(l_response);
                 } else {
                     debug_if(s_debug_more, L_DEBUG, "SERVER: handshake process returned no response (l_response=%p, l_response_size=%zu)", 
                              l_response, l_response_size);
                 }
             }
        } else if (l_header->type == DAP_STREAM_UDP_PKT_SESSION_CREATE) {
             debug_if(s_debug_more, L_DEBUG, "Processing SESSION_CREATE packet: l_ctx=%p, session_create_cb=%p", 
                      l_ctx, l_ctx ? l_ctx->session_create_cb : NULL);
             if (l_ctx && l_ctx->session_create_cb) {
                 // Client: Received Session Response
                 
                 // DUPLICATE PROTECTION: Check if session already created
                 uint64_t l_sess_id = be64toh(l_header->session_id);
                 if (a_stream->session && a_stream->session->id == l_sess_id && a_stream->session->key) {
                     debug_if(s_debug_more, L_DEBUG, "Client: ignoring duplicate SESSION_CREATE response for session 0x%lx (already established)", l_sess_id);
                     return a_size;  // Ignore duplicate
                 }
                 
                 debug_if(s_debug_more, L_DEBUG, "Client: parsing SESSION_CREATE response");
                 uint16_t l_payload_len = be16toh(l_header->length);
                 
                 debug_if(s_debug_more, L_DEBUG, "Client: SESSION_CREATE response session_id=0x%lx, payload_len=%u", 
                          l_sess_id, l_payload_len);
                 
                 // Validate stream before processing
                 if (!a_stream) {
                     log_it(L_ERROR, "Cannot process SESSION_CREATE: stream is NULL");
                     return -1;
                 } else if (!a_stream->trans) {
                     log_it(L_ERROR, "Cannot process SESSION_CREATE: stream->trans is NULL");
                     return -1;
                 } else if (!a_stream->trans_ctx) {
                     log_it(L_ERROR, "Cannot process SESSION_CREATE: stream->trans_ctx is NULL");
                     return -1;
                 }
                 
                 // If payload is present, it contains the KDF counter (8 bytes)
                 if (l_payload_len > 0 && l_payload) {
                     // Payload contains KDF counter (8 bytes, big-endian)
                     if (l_payload_len != sizeof(uint64_t)) {
                         log_it(L_ERROR, "Client: invalid SESSION_CREATE payload size (expected 8, got %u)", l_payload_len);
                         return -1;
                     }
                     
                     uint64_t l_session_counter = be64toh(*(uint64_t*)l_payload);
                     log_it(L_INFO, "Client: received KDF counter (%lu) for session 0x%lx", l_session_counter, l_sess_id);
                     
                     if (!l_udp_ctx->handshake_key) {
                         log_it(L_ERROR, "Client: no handshake key for deriving session key");
                         return -1;
                     }
                     
                     dap_enc_key_t *l_session_key = dap_enc_kdf_create_cipher_key(
                         l_udp_ctx->handshake_key,      // Source key
                         DAP_ENC_KEY_TYPE_SALSA2012,    // Cipher type
                         "udp_session",                  // Context
                         12,                             // Context size
                         l_session_counter,              // Counter
                         32);                            // Key size
                     
                     if (!l_session_key) {
                         log_it(L_ERROR, "Client: failed to derive session key using KDF");
                         return -1;
                     }
                     
                     log_it(L_INFO, "Client: derived session key using KDF (counter=%lu)", l_session_counter);
                     
                     // Set session key in stream
                     if (!a_stream->session) {
                         a_stream->session = dap_stream_session_pure_new();
                         if (!a_stream->session) {
                             log_it(L_ERROR, "Client: failed to create session");
                             dap_enc_key_delete(l_session_key);
                             return -1;
                         }
                     }
                     
                     if (a_stream->session->key) {
                         dap_enc_key_delete(a_stream->session->key);
                     }
                     
                    a_stream->session->key = l_session_key;
                    a_stream->session->id = l_sess_id;
                    
                    log_it(L_INFO, "Client: session key installed for session 0x%lx", l_sess_id);
                    
                    // Call callback with session_id (key is already in stream->session->key)
                    debug_if(s_debug_more, L_DEBUG, "Calling session_create_cb: stream=%p, session_id=%u, cb=%p", 
                             a_stream, (uint32_t)l_sess_id, l_ctx->session_create_cb);
                 l_ctx->session_create_cb(a_stream, (uint32_t)l_sess_id, NULL, 0, 0);
                    debug_if(s_debug_more, L_DEBUG, "session_create_cb completed successfully");
             } else {
                    // ERROR: Server must send KDF counter in payload
                    log_it(L_ERROR, "Client: SESSION_CREATE response missing payload (KDF counter expected)");
                    // Call callback with error (session_id=0 indicates failure)
                    l_ctx->session_create_cb(a_stream, 0, NULL, 0, ETIMEDOUT);
                }
                
                // DO NOT clear callback - duplicate protection will handle subsequent responses
                // l_ctx->session_create_cb = NULL;  // REMOVED - causes duplicate responses to be ignored silently
             } else if (!l_ctx->session_create_cb) {
                 // Server: Received Session Request from client
                 // session_id is already set from HANDSHAKE (not 0!)
                 uint64_t l_sess_id = be64toh(l_header->session_id);
                debug_if(s_debug_more, L_DEBUG, "Server: received SESSION_CREATE request for existing session 0x%lx", l_sess_id);
                
                // Session was already created during HANDSHAKE, just confirm it
                 if (!a_stream->session) {
                     a_stream->session = dap_stream_session_pure_new();
                    if (a_stream->session) {
                        a_stream->session->id = l_sess_id;
                    }
                }
                
                // Use KDF to derive SESSION key from HANDSHAKE key (no seed transmission!)
                // This provides forward secrecy through ratcheting
                // Counter can be incremented for each new session to derive unique keys
                if (!l_udp_ctx->handshake_key) {
                    log_it(L_ERROR, "Server: no handshake key for deriving session key");
                    return -1;
                }
                
                // Counter for ratcheting (can be session_id % MAX or separate counter)
                // For now use counter = 1 for first session key derivation
                uint64_t l_session_counter = 1;
                
                // Derive session key from handshake key using KDF
                if (a_stream->session->key) {
                    dap_enc_key_delete(a_stream->session->key);
                }
                a_stream->session->key = dap_enc_kdf_create_cipher_key(
                    l_udp_ctx->handshake_key,       // Source key
                    DAP_ENC_KEY_TYPE_SALSA2012,     // Cipher type
                    "udp_session",                   // Context
                    12,                              // Context size
                    l_session_counter,               // Counter
                    32);                             // Key size
                
                if (!a_stream->session->key) {
                    log_it(L_ERROR, "Server: failed to derive session key from handshake key using KDF");
                    return -1;
                }
                
                log_it(L_INFO, "Server: derived session key for session 0x%lx using KDF (counter=%lu)", 
                       l_sess_id, l_session_counter);
                
                // Prepare response header with counter (client needs it to derive same key)
                uint64_t l_counter_be = htobe64(l_session_counter);
                
                ssize_t l_sent = s_udp_write_typed(a_stream, DAP_STREAM_UDP_PKT_SESSION_CREATE,
                                                     &l_counter_be, sizeof(l_counter_be));
                
                if (l_sent < 0) {
                    log_it(L_ERROR, "Server: failed to send SESSION_CREATE response for session 0x%lx", l_sess_id);
                } else {
                    debug_if(s_debug_more, L_DEBUG, "Server: sent SESSION_CREATE response with KDF counter (%lu) for session 0x%lx", 
                             l_session_counter, l_sess_id);
                }
            } else {
                // Client: Duplicate SESSION_CREATE response (callback already called)
                debug_if(s_debug_more, L_DEBUG, "Ignoring duplicate SESSION_CREATE response (session_id=%lu)", 
                         be64toh(l_header->session_id));
             }
        }

        if (l_payload) DAP_DELETE(l_payload);
        debug_if(s_debug_more, L_DEBUG, "UDP read: calling shrink_buf_in(%zu bytes), buf_in_size=%zu", 
                 l_total_size, l_es->buf_in_size);
        dap_events_socket_shrink_buf_in(l_es, l_total_size);
        debug_if(s_debug_more, L_DEBUG, "UDP read: after shrink, buf_in_size=%zu", l_es->buf_in_size);
        return 0;
    }
    
    return 0;
}

/**
 * @brief Write data with specified UDP packet type (internal helper)
 * 
 * NEW ARCHITECTURE:
 * - Client: Uses own physical UDP esocket (trans_ctx->esocket)
 * - Server: Uses listener's physical esocket + sendto with remote_addr from UDP context
 * - ALWAYS wraps payload in UDP header with specified type
 */
/**
 * @brief Write typed packet (encrypted or plaintext for HANDSHAKE)
 * 
 * HANDSHAKE packets: sent as plaintext (just Kyber public key)
 * All other packets: encrypted with session key
 */
static ssize_t s_udp_write_typed(dap_stream_t *a_stream, uint8_t a_pkt_type,
                                  const void *a_data, size_t a_size)
{
    debug_if(s_debug_more, L_DEBUG, "s_udp_write_typed: type=0x%02x, size=%zu", 
             a_pkt_type, a_size);
    
    if (!a_stream || !a_data || a_size == 0) {
        log_it(L_ERROR, "Invalid arguments for UDP write");
        return -1;
    }

    dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(a_stream);
    if (!l_ctx || !l_ctx->esocket) {
        log_it(L_ERROR, "No trans ctx or esocket for write");
        return -1;
    }

    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        log_it(L_ERROR, "No UDP context for write");
        return -1;
    }

    // HANDSHAKE packets are OBFUSCATED (size-based encryption)
    if (a_pkt_type == DAP_STREAM_UDP_PKT_HANDSHAKE) {
        // Validate size (must be Kyber public key)
        if (a_size != DAP_STREAM_UDP_HANDSHAKE_SIZE) {
            log_it(L_ERROR, "Invalid handshake payload size: %zu (expected %d)",
                   a_size, DAP_STREAM_UDP_HANDSHAKE_SIZE);
            return -1;
    }

        // Obfuscate handshake (encrypt with size-derived key, add random padding)
        uint8_t *l_obfuscated = NULL;
        size_t l_obfuscated_size = 0;
        
        int l_ret = dap_transport_obfuscate_handshake(a_data, a_size,
                                                      &l_obfuscated, &l_obfuscated_size);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to obfuscate HANDSHAKE packet");
        return -1;
    }
        
        // Send obfuscated handshake
        ssize_t l_sent = dap_events_socket_write_unsafe(l_ctx->esocket, 
                                                         l_obfuscated, l_obfuscated_size);
        DAP_DELETE(l_obfuscated);
    
        if (l_sent < 0) {
            log_it(L_ERROR, "Failed to send obfuscated HANDSHAKE packet");
        return -1;
    }

        debug_if(s_debug_more, L_DEBUG,
                 "Obfuscated HANDSHAKE sent: %zu → %zu bytes",
                 a_size, l_obfuscated_size);
        return l_sent;
    }
    
    // ALL OTHER PACKETS: Encrypt entire packet
    
    if (!l_udp_ctx->handshake_key) {
        log_it(L_ERROR, "No encryption key for sending encrypted packet (type=%u)", a_pkt_type);
        return -1;
    }
    
    // Build encrypted header
    dap_stream_trans_udp_encrypted_header_t l_header;
    l_header.type = a_pkt_type;
    l_header.seq_num = htonl(l_udp_ctx->seq_num++);
    l_header.session_id = htobe64(l_udp_ctx->session_id);
    
    // Build cleartext packet (header + payload)
    size_t l_cleartext_size = sizeof(l_header) + a_size;
    uint8_t *l_cleartext = DAP_NEW_SIZE(uint8_t, l_cleartext_size);
    if (!l_cleartext) {
        log_it(L_ERROR, "Failed to allocate cleartext buffer");
        return -1;
    }
    
    memcpy(l_cleartext, &l_header, sizeof(l_header));
    memcpy(l_cleartext + sizeof(l_header), a_data, a_size);
    
    // Encrypt entire packet
    size_t l_encrypted_max = l_cleartext_size + 256;  // Extra space for encryption overhead
    uint8_t *l_encrypted = DAP_NEW_SIZE(uint8_t, l_encrypted_max);
    if (!l_encrypted) {
        log_it(L_ERROR, "Failed to allocate encryption buffer");
        DAP_DELETE(l_cleartext);
        return -1;
    }
    
    // Use handshake_key for SESSION_CREATE, session key for DATA/KEEPALIVE/CLOSE
    dap_enc_key_t *l_enc_key = l_udp_ctx->handshake_key;
    if (a_pkt_type == DAP_STREAM_UDP_PKT_DATA || 
        a_pkt_type == DAP_STREAM_UDP_PKT_KEEPALIVE || 
        a_pkt_type == DAP_STREAM_UDP_PKT_CLOSE) {
        // These packet types require session key
        if (a_stream->session && a_stream->session->key) {
            l_enc_key = a_stream->session->key;
        } else {
            log_it(L_ERROR, "No session key for DATA/KEEPALIVE/CLOSE packet");
            DAP_DELETE(l_cleartext);
            DAP_DELETE(l_encrypted);
            return -1;
        }
    }
    
    size_t l_encrypted_size = dap_enc_code(l_enc_key,
                                           l_cleartext, l_cleartext_size,
                                           l_encrypted, l_encrypted_max,
                                           DAP_ENC_DATA_TYPE_RAW);
    
    DAP_DELETE(l_cleartext);
    
    if (l_encrypted_size == 0) {
        log_it(L_ERROR, "Failed to encrypt packet (type=%u)", a_pkt_type);
        DAP_DELETE(l_encrypted);
        return -1;
    }
    
    // Send encrypted blob (no headers, no magic, just encrypted data)
    ssize_t l_sent = dap_events_socket_write_unsafe(l_ctx->esocket, l_encrypted, l_encrypted_size);
    
    DAP_DELETE(l_encrypted);
    
    if (l_sent < 0) {
        log_it(L_ERROR, "Failed to send encrypted packet (type=%u)", a_pkt_type);
        return -1;
    }

    debug_if(s_debug_more, L_DEBUG,
             "Encrypted packet sent: type=%u, session=0x%lx, encrypted_size=%zu",
             a_pkt_type, l_udp_ctx->session_id, l_encrypted_size);
    
    return l_sent;
}

static ssize_t s_udp_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    // All DATA packets go through here (from dap_stream_pkt_write_unsafe)
    return s_udp_write_typed(a_stream, DAP_STREAM_UDP_PKT_DATA, a_data, a_size);
}

/**
 * @brief Close UDP trans
 * 
 * CRITICAL: This function is responsible for esocket cleanup!
 * It runs in the esocket's worker context, so unsafe access is safe here.
 * We must extract and delete esocket BEFORE dap_stream_delete_unsafe tries to access it.
 */
static void s_udp_close(dap_stream_t *a_stream)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream for close");
        return;
    }

    if (!a_stream->trans) {
        return;
    }

    // Get UDP per-stream context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
    if (l_udp_ctx) {
        log_it(L_INFO, "Closing UDP trans session 0x%lx", l_udp_ctx->session_id);
        
        // Clean up alice_key if present
        if (l_udp_ctx->alice_key) {
            dap_enc_key_delete(l_udp_ctx->alice_key);
            l_udp_ctx->alice_key = NULL;
        }
        
        // Clean up handshake_key if present
        if (l_udp_ctx->handshake_key) {
            dap_enc_key_delete(l_udp_ctx->handshake_key);
            l_udp_ctx->handshake_key = NULL;
    }
    
        // Clear stream pointer to prevent use-after-free
        l_udp_ctx->stream = NULL;
        l_udp_ctx->session_id = 0;
        l_udp_ctx->seq_num = 0;
    }
    
    // CORRECT ARCHITECTURE - 100% THREAD SAFE:
    //
    // ALWAYS use _mt method for esocket deletion - works from ANY thread context
    // No need to check worker - _mt handles it correctly
    
    dap_net_trans_ctx_t *l_ctx = (dap_net_trans_ctx_t*)a_stream->trans_ctx;
    if (l_ctx && l_ctx->esocket_uuid && l_ctx->esocket_worker) {
        debug_if(s_debug_more, L_DEBUG, 
               "UDP close: queueing esocket deletion (UUID 0x%016lx) on its worker",
               l_ctx->esocket_uuid);
        
        // ALWAYS use _mt method - 100% safe from any thread
        dap_events_socket_remove_and_delete_mt(l_ctx->esocket_worker, l_ctx->esocket_uuid);
        
        // Clear pointers (esocket will be deleted asynchronously on its worker)
        l_ctx->esocket = NULL;
        l_ctx->esocket_uuid = 0;
        l_ctx->esocket_worker = NULL;
    }
    
    if (l_ctx) {
        // Clear stream pointer in trans_ctx to prevent use-after-free
        l_ctx->stream = NULL;
        
        // Free UDP context (owned by trans_ctx, not esocket)
        if (l_ctx->_inheritor) {
            DAP_DELETE(l_ctx->_inheritor);
            l_ctx->_inheritor = NULL;
        }
    }
}

/**
 * @brief Prepare UDP socket for client stage
 * 
 * Fully prepares esocket: creates, sets callbacks, and adds to worker.
 * UDP is connectionless, so no connection step is needed.
 * Trans is responsible for complete esocket lifecycle management.
 */
static int s_udp_stage_prepare(dap_net_trans_t *a_trans,
                               const dap_net_stage_prepare_params_t *a_params,
                               dap_net_stage_prepare_result_t *a_result)
{
    if (!a_trans || !a_params || !a_result) {
        log_it(L_ERROR, "Invalid arguments for UDP stage_prepare");
        return -1;
    }
    
    if (!a_params->worker) {
        log_it(L_ERROR, "Worker is required for UDP stage_prepare");
        a_result->error_code = -1;
        return -1;
    }
    
    // Initialize result
    a_result->esocket = NULL;
    a_result->stream = NULL;
    a_result->error_code = 0;
    
    // Create or reuse UDP socket
    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_trans);
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        a_result->error_code = -1;
        return -1;
    }
    
    dap_events_socket_t *l_es = dap_events_socket_create_platform(PF_INET, SOCK_DGRAM, IPPROTO_UDP, a_params->callbacks);
    if (!l_es) {
        log_it(L_ERROR, "Failed to create UDP socket");
        a_result->error_code = -1;
        return -1;
    }
    l_es->type = DESCRIPTOR_TYPE_SOCKET_UDP;
    
    // Set UDP-specific callbacks for client esocket
    l_es->callbacks.read_callback = dap_stream_trans_udp_read_callback;
    l_es->callbacks.write_callback = s_udp_client_write_callback;
    
    // UDP is connectionless - just add to worker
    dap_worker_add_events_socket(a_params->worker, l_es);
    
    log_it(L_DEBUG, "Created UDP socket %p", l_es);
    
    // Resolve host and set address using centralized function
    if (dap_events_socket_resolve_and_set_addr(l_es, a_params->host, a_params->port) < 0) {
        log_it(L_ERROR, "Failed to resolve address for UDP trans: %s:%u", a_params->host, a_params->port);
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }

    log_it(L_DEBUG, "Resolved UDP address: family=%d, size=%zu", l_es->addr_storage.ss_family, (size_t)l_es->addr_size);

    // Explicitly connect UDP socket to set default destination address
    // This allows using send() or write() without specifying address every time
    if (connect(l_es->socket, (struct sockaddr *)&l_es->addr_storage, l_es->addr_size) < 0) {
        log_it(L_ERROR, "Failed to connect UDP socket: %s (socket=%d, family=%d, size=%zu)", 
               strerror(errno), l_es->socket, l_es->addr_storage.ss_family, (size_t)l_es->addr_size);
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    // Get local port assigned by OS
    struct sockaddr_in l_local_addr;
    socklen_t l_local_addr_len = sizeof(l_local_addr);
    if (getsockname(l_es->socket, (struct sockaddr *)&l_local_addr, &l_local_addr_len) == 0) {
        log_it(L_INFO, "UDP socket fd=%d bound to local port %u", l_es->socket, ntohs(l_local_addr.sin_port));
    }
    
    // For UDP: create stream early and return it (stream will be reused for all operations)
    dap_stream_t *l_stream = dap_stream_new_es_client(l_es, (dap_stream_node_addr_t *)a_params->node_addr, a_params->authorized);
    if (!l_stream) {
        log_it(L_CRITICAL, "Failed to create stream for UDP trans");
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    l_stream->trans = a_trans;
    
    // Initialize trans_ctx and link esocket
    dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(l_stream);
    if (!l_ctx) {
        log_it(L_CRITICAL, "Failed to create trans_ctx for UDP stream");
        dap_events_socket_delete_unsafe(l_es, true);
        DAP_DELETE(l_stream);
        a_result->error_code = -1;
        return -1;
    }
    // Set esocket reference with UUID and worker for thread-safe access
    l_ctx->esocket = l_es;
    l_ctx->esocket_uuid = l_es->uuid;
    l_ctx->esocket_worker = l_es->worker;
    l_ctx->stream = l_stream;
    
    // CRITICAL: Set callbacks.arg so read_callback can retrieve trans_ctx!
    l_es->callbacks.arg = l_ctx;
    log_it(L_INFO, "UDP CLIENT CREATED: esocket=%p (fd=%d), stream=%p, trans_ctx=%p", 
           l_es, l_es->socket, l_stream, l_ctx);
    
    // Create UDP per-stream context and store client_ctx
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_or_create_udp_ctx(l_stream);
    if (!l_udp_ctx) {
        log_it(L_CRITICAL, "Failed to create UDP context for stream");
        dap_events_socket_delete_unsafe(l_es, true);
        DAP_DELETE(l_stream);
        a_result->error_code = -1;
        return -1;
    }
    
    // Store client context for later retrieval in get_client_context
    l_udp_ctx->client_ctx = a_params->client_ctx;
    l_udp_ctx->stream = l_stream;
    
    // CRITICAL: Store trans_ctx in callbacks.arg (NOT _inheritor!)
    // _inheritor is for client infrastructure ownership, we use callbacks.arg for trans_ctx
    l_es->callbacks.arg = l_ctx;
    
    log_it(L_DEBUG, "UDP trans created stream %p with trans_ctx %p (stored in callbacks.arg)", l_stream, l_ctx);
    
    a_result->esocket = l_es;
    a_result->stream = l_stream;
    a_result->error_code = 0;
    log_it(L_DEBUG, "UDP socket and stream prepared for %s:%u", a_params->host, a_params->port);
    return 0;
}

/**
 * @brief Get trans capabilities
 */
static uint32_t s_udp_get_capabilities(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return DAP_NET_TRANS_CAP_LOW_LATENCY |
           DAP_NET_TRANS_CAP_BIDIRECTIONAL;
}

/**
 * @brief Get client context from UDP stream's trans_ctx
 * @param a_stream Stream to extract client context from
 * @return Client context (dap_client_t*) or NULL
 * @note For UDP trans, trans_ctx->_inheritor contains dap_udp_client_esocket_ctx_t wrapper
 */
static void* s_udp_get_client_context(dap_stream_t *a_stream)
{
    if (!a_stream) {
        return NULL;
    }
    
    // For UDP, trans_ctx->_inheritor is dap_net_trans_udp_ctx_t
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        return NULL;
    }
    
    return l_udp_ctx->client_ctx;  // Return dap_client_t*
}

//=============================================================================
// Helper functions
//=============================================================================

/**
 * @brief Get private data from trans
 */
static dap_stream_trans_udp_private_t *s_get_private(dap_net_trans_t *a_trans)
{
    if (!a_trans)
        return NULL;
    return (dap_stream_trans_udp_private_t*)a_trans->_inheritor;
}

/**
 * @brief Get UDP per-stream context
 */
static dap_net_trans_udp_ctx_t *s_get_udp_ctx(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans_ctx)
        return NULL;
    return (dap_net_trans_udp_ctx_t*)a_stream->trans_ctx->_inheritor;
}

/**
 * @brief Get or create UDP per-stream context
 */
/**
 * @brief Get or create UDP per-stream context
 * Made non-static for server.c to initialize UDP context for server-side streams
 */
dap_net_trans_udp_ctx_t *s_get_or_create_udp_ctx(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans_ctx)
        return NULL;
    
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t*)a_stream->trans_ctx;
    
    if (!l_trans_ctx->_inheritor) {
        // Create new UDP context
        dap_net_trans_udp_ctx_t *l_udp_ctx = DAP_NEW_Z(dap_net_trans_udp_ctx_t);
        if (!l_udp_ctx) {
            log_it(L_CRITICAL, "Failed to allocate UDP stream context");
            return NULL;
    }
        l_trans_ctx->_inheritor = l_udp_ctx;
        debug_if(s_debug_more, L_DEBUG, "Created UDP context %p for stream %p", l_udp_ctx, a_stream);
    }
    
    return (dap_net_trans_udp_ctx_t*)l_trans_ctx->_inheritor;
}

/**
 * @brief Create UDP header
 */
