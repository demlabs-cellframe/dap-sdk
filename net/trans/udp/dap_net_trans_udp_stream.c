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
#include "dap_enc_ks.h"
#include "dap_enc_base64.h"
#include "dap_string.h"
#include "dap_net_trans_ctx.h"

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

// UDP client esocket context (stored in esocket->_inheritor)
// This wrapper allows both dap_client (via client_ctx) and dap_stream (via stream)
// to access their respective contexts without conflicts
typedef struct dap_udp_client_esocket_ctx {
    void *client_ctx;              // Original client context (dap_client_t*) from stage_prepare
    dap_stream_t *stream;          // Associated stream (set in handshake_init)
} dap_udp_client_esocket_ctx_t;

// Helper functions
static dap_stream_trans_udp_private_t *s_get_private(dap_net_trans_t *a_trans);
static int s_udp_handshake_response(dap_stream_t *a_stream, const void *a_data, size_t a_data_size);
static int s_create_udp_header(dap_stream_trans_udp_header_t *a_header,
                                uint8_t a_type, uint16_t a_length,
                                uint32_t a_seq_num, uint64_t a_session_id);
static int s_parse_udp_header(const dap_stream_trans_udp_header_t *a_header,
                               uint8_t *a_type, uint16_t *a_length,
                               uint32_t *a_seq_num, uint64_t *a_session_id);

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

    debug_if(s_debug_more, L_DEBUG, "UDP client read callback: esocket %p (fd=%d), buf_in_size=%zu, _inheritor=%p",
             a_es, a_es->fd, a_es->buf_in_size, a_es->_inheritor);

    // Get trans_ctx from esocket->_inheritor (ALWAYS dap_net_trans_ctx_t)
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_es->_inheritor;

    if (!l_trans_ctx) {
        log_it(L_WARNING, "UDP client esocket has no trans_ctx (_inheritor is NULL), dropping %zu bytes", a_es->buf_in_size);
        a_es->buf_in_size = 0;
        return;
    }
    
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

    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_stream->trans);
    return l_priv ? l_priv->session_id : 0;
}

/**
 * @brief Get current sequence number
 */
uint32_t dap_stream_trans_udp_get_seq_num(const dap_stream_t *a_stream)
{
    if (!dap_stream_trans_is_udp(a_stream))
        return 0;

    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_stream->trans);
    return l_priv ? l_priv->seq_num : 0;
}

/**
 * @brief Set remote peer address
 */
int dap_stream_trans_udp_set_remote_addr(dap_net_trans_t *a_trans,
                                              const struct sockaddr *a_addr,
                                              socklen_t a_addr_len)
{
    if (!a_trans || !a_addr) {
        log_it(L_ERROR, "Invalid arguments for set remote addr");
        return -1;
    }

    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_trans);
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        return -1;
    }

    memcpy(&l_priv->remote_addr, a_addr, a_addr_len);
    l_priv->remote_addr_len = a_addr_len;
    return 0;
}

/**
 * @brief Get remote peer address
 */
int dap_stream_trans_udp_get_remote_addr(dap_net_trans_t *a_trans,
                                              struct sockaddr *a_addr,
                                              socklen_t *a_addr_len)
{
    if (!a_trans || !a_addr || !a_addr_len) {
        log_it(L_ERROR, "Invalid arguments for get remote addr");
        return -1;
    }

    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_trans);
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        return -1;
    }

    memcpy(a_addr, &l_priv->remote_addr, l_priv->remote_addr_len);
    *a_addr_len = l_priv->remote_addr_len;
    return 0;
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

    l_priv->config = dap_stream_trans_udp_config_default();
    l_priv->session_id = 0;
    l_priv->seq_num = 0;
    l_priv->server = NULL;
    l_priv->remote_addr_len = 0;
    l_priv->alice_key = NULL;
    l_priv->user_data = NULL;
    
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
        if (l_priv->alice_key) {
            dap_enc_key_delete(l_priv->alice_key);
        }
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

    dap_stream_trans_udp_private_t *l_priv = 
        (dap_stream_trans_udp_private_t*)a_stream->trans->_inheritor;
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        return -1;
    }

    // Parse address and store in remote_addr
    struct sockaddr_in *l_addr_in = (struct sockaddr_in*)&l_priv->remote_addr;
    l_addr_in->sin_family = AF_INET;
    l_addr_in->sin_port = htons(a_port);
    
    if (inet_pton(AF_INET, a_host, &l_addr_in->sin_addr) != 1) {
        log_it(L_ERROR, "Invalid IPv4 address: %s", a_host);
        return -1;
    }

    l_priv->remote_addr_len = sizeof(struct sockaddr_in);
    
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
    if (!a_stream->trans_ctx) {
        a_stream->trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
        if (a_stream->trans) {
            a_stream->trans_ctx->trans = a_stream->trans;
        }
    }
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

    dap_stream_trans_udp_private_t *l_priv = 
        (dap_stream_trans_udp_private_t*)a_stream->trans->_inheritor;
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        return -1;
    }

    log_it(L_INFO, "UDP handshake init: enc_type=%d, pkey_type=%d",
           a_params->enc_type, a_params->pkey_exchange_type);
    
    // Store callback
    dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(a_stream);
    l_ctx->handshake_cb = a_callback;
    
    // Set up read callback for client esocket and link stream
    if (l_ctx->esocket) {
        debug_if(s_debug_more, L_DEBUG, "Setting up UDP client esocket %p (fd=%d) for handshake_init", 
                 l_ctx->esocket, l_ctx->esocket->fd);
        
        // Move UDP context from esocket->callbacks.arg to trans_ctx->_inheritor
        dap_udp_client_esocket_ctx_t *l_udp_ctx = (dap_udp_client_esocket_ctx_t *)l_ctx->esocket->callbacks.arg;
        
        if (!l_udp_ctx) {
            log_it(L_ERROR, "UDP client esocket has no context");
            return -1;
        }
        
        // Store UDP context in trans_ctx->_inheritor
        l_ctx->_inheritor = l_udp_ctx;
        
        // Store stream pointer in BOTH structures
        l_udp_ctx->stream = a_stream;  // For UDP-specific logic
        l_ctx->stream = a_stream;       // For trans_ctx callback
        
        // Set esocket->_inheritor to trans_ctx (clean architecture!)
        l_ctx->esocket->_inheritor = l_ctx;
        l_ctx->esocket->callbacks.arg = NULL; // Clear temporary storage
        
        debug_if(s_debug_more, L_DEBUG, "Set esocket->_inheritor = trans_ctx %p, trans_ctx->stream = %p", 
                 l_ctx, a_stream);
        
        // Set read callback
        if (!l_ctx->esocket->callbacks.read_callback) {
            l_ctx->esocket->callbacks.read_callback = dap_stream_trans_udp_read_callback;
            debug_if(s_debug_more, L_DEBUG, "Set UDP client read callback for esocket %p", l_ctx->esocket);
        }
    }
    
    // Generate random session ID for this connection
    if (randombytes((uint8_t*)&l_priv->session_id, sizeof(l_priv->session_id)) != 0) {
        log_it(L_ERROR, "Failed to generate random session ID");
        return -1;
    }
    l_priv->seq_num = 0;
    

    if (l_priv->alice_key) {
        dap_enc_key_delete(l_priv->alice_key);
    }
    
    l_priv->alice_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    if (!l_priv->alice_key) {
        log_it(L_ERROR, "Failed to generate Alice key for UDP handshake");
        return -1;
    }
    
    // Use OUR generated public key (not a_params), since we need the matching private key for decapsulation
    void *l_alice_pub = l_priv->alice_key->pub_key_data;
    size_t l_alice_pub_size = l_priv->alice_key->pub_key_data_size;
    
    // Create UDP packet with HANDSHAKE type
    dap_stream_trans_udp_header_t l_header;
    s_create_udp_header(&l_header, DAP_STREAM_UDP_PKT_HANDSHAKE,
                        (uint16_t)l_alice_pub_size,
                        l_priv->seq_num++, l_priv->session_id);
    
    // Allocate buffer for header + payload
    size_t l_packet_size = sizeof(l_header) + l_alice_pub_size;
    uint8_t *l_packet = DAP_NEW_Z_SIZE(uint8_t, l_packet_size);
    if (!l_packet) {
        log_it(L_CRITICAL, "Memory allocation failed for UDP handshake packet");
        return -1;
    }
    
    // Copy header and payload
    memcpy(l_packet, &l_header, sizeof(l_header));
    memcpy(l_packet + sizeof(l_header), l_alice_pub, l_alice_pub_size);
    
    // Send via dap_events_socket_write_unsafe
    // Use trans_ctx's esocket
    if (!l_ctx || !l_ctx->esocket) {
        log_it(L_ERROR, "No esocket in trans ctx for handshake init");
        return -1;
    }
    dap_events_socket_t *l_es = l_ctx->esocket;
    
    size_t l_sent = dap_events_socket_write_unsafe(l_es, l_packet, l_packet_size);
    
    DAP_DELETE(l_packet);
    
    if (l_sent != l_packet_size) {
        log_it(L_ERROR, "UDP handshake send incomplete: %zu of %zu bytes", l_sent, l_packet_size);
        return -1;
    }
    
    log_it(L_INFO, "UDP handshake init sent: %zu bytes (session_id=%lu)",
           l_packet_size, l_priv->session_id);
    
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
    
    // Get Alice's key from trans private data
    dap_stream_trans_udp_private_t *l_priv = 
        (dap_stream_trans_udp_private_t*)a_stream->trans->_inheritor;
    if (!l_priv) {
        log_it(L_ERROR, "UDP handshake response: no priv data");
        return -1;
    }
    if (!l_priv->alice_key) {
        log_it(L_ERROR, "UDP handshake response: no Alice key");
        return -1;
    }
    
    dap_enc_key_t *l_alice_key = l_priv->alice_key;
    log_it(L_DEBUG, "UDP handshake response: alice_key=%p, dec_na=%p", 
           l_alice_key, l_alice_key->dec_na);
    
    // Use Alice's key to decrypt and derive shared secret from Bob's response
    // For Kyber512, we need to use gen_alice_shared_key
    void *l_shared_key = NULL;
    size_t l_shared_key_size = 0;
    
    // Kyber512 uses gen_alice_shared_key with alice_key itself (not priv_key_data)
    if (l_alice_key->type == DAP_ENC_KEY_TYPE_KEM_KYBER512) {
        log_it(L_DEBUG, "Calling gen_alice_shared_key with %zu byte ciphertext", a_data_size);
        
        // The function signature expects:
        // dap_enc_kyber512_gen_alice_shared_key(key, priv_data, cypher_size, cypher_msg)
        // But looking at server side, it just passes the key object, not priv_data separately
        // Let's pass the key's priv_key_data directly (it should contain the secret key)
        l_shared_key_size = dap_enc_kyber512_gen_alice_shared_key(l_alice_key, 
                                                                    l_alice_key->priv_key_data,
                                                                    a_data_size,
                                                                    (uint8_t*)a_data);
        l_shared_key = l_alice_key->priv_key_data; // Shared key is stored here after call
        
        log_it(L_DEBUG, "gen_alice_shared_key returned: shared_key_size=%zu", l_shared_key_size);
        
        if (l_shared_key_size == 0) {
            log_it(L_ERROR, "Failed to derive shared key from Bob's response");
            return -1;
        }
    } else {
        log_it(L_ERROR, "Unsupported key type for UDP handshake: %d (expected Kyber512)", l_alice_key->type);
        return -1;
    }
    
    log_it(L_DEBUG, "UDP handshake: derived shared secret (%zu bytes)", l_shared_key_size);
    
    // Create session and set encryption key
    if (!a_stream->session) {
        a_stream->session = dap_stream_session_pure_new();
        if (!a_stream->session) {
            log_it(L_ERROR, "Failed to create session");
            return -1;
        }
    }
    
    if (a_stream->session->key) {
        dap_enc_key_delete(a_stream->session->key);
    }
    
    // Create session encryption key from shared secret (SALSA2012)
    a_stream->session->key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SALSA2012, 
                                                        l_shared_key, l_shared_key_size, 
                                                        NULL, 0, 32);
    if (!a_stream->session->key) {
        log_it(L_ERROR, "Failed to create session encryption key");
        return -1;
    }
    
    log_it(L_INFO, "UDP handshake complete: session encryption established");
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
        l_shared_key = l_bob_key->priv_key_data;
        l_bob_pub_size = l_bob_key->pub_key_data_size;
        
        // Check if key generation succeeded
        if (!l_bob_pub || l_shared_key_size == 0) {
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
        // Create session key from shared secret
        // Using SALSA2012 for session encryption
        a_stream->session->key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SALSA2012, l_shared_key, l_shared_key_size, NULL, 0, 32);
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
        *a_response = l_json_resp->str;
        *a_response_size = l_json_resp->len;
        DAP_DELETE(l_json_resp); // Free struct, keep str
    } else {
        dap_string_free(l_json_resp, true);
    }
    
    // l_bob_pub was allocated by gen_bob_shared_key
    DAP_DELETE(l_bob_pub);
    
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

    if (!a_stream->trans) {
        log_it(L_ERROR, "Stream has no trans");
        return -1;
    }

    dap_stream_trans_udp_private_t *l_priv = 
        (dap_stream_trans_udp_private_t*)a_stream->trans->_inheritor;
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        return -1;
    }

    // Store callback
    dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(a_stream);
    l_ctx->session_create_cb = a_callback;

    // Create UDP packet with SESSION_CREATE type
    dap_stream_trans_udp_header_t l_header;
    s_create_udp_header(&l_header, DAP_STREAM_UDP_PKT_SESSION_CREATE,
                        0, 
                        l_priv->seq_num++, 0); // Session ID 0 indicates request for new session
    
    size_t l_packet_size = sizeof(l_header);
    
    // Send via dap_events_socket_write_unsafe
    if (!l_ctx || !l_ctx->esocket) {
        log_it(L_ERROR, "No esocket in trans ctx for session create");
        return -1;
    }
    dap_events_socket_t *l_es = l_ctx->esocket;
    
    size_t l_sent = dap_events_socket_write_unsafe(l_es, &l_header, l_packet_size);
    
    if (l_sent != l_packet_size) {
        log_it(L_ERROR, "UDP session create send incomplete");
        return -1;
    }
    
    log_it(L_INFO, "UDP session create request sent");
    
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
static ssize_t s_udp_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_stream->trans) {
        log_it(L_ERROR, "Invalid arguments for UDP read: stream or trans is NULL");
        return -1;
    }
    
    // Get esocket from trans_ctx
    dap_events_socket_t *l_es = NULL;
    dap_net_trans_ctx_t *l_ctx = (dap_net_trans_ctx_t*)a_stream->trans_ctx;
    if (l_ctx) {
        l_es = l_ctx->esocket;
    }

    if (!l_es || !l_es->buf_in) {
        debug_if(s_debug_more, L_DEBUG, "UDP read: no esocket or buf_in (l_es=%p, l_ctx=%p)", l_es, l_ctx);
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
        
        dap_stream_trans_udp_private_t *l_priv = 
            (dap_stream_trans_udp_private_t*)a_stream->trans->_inheritor;

        dap_net_trans_ctx_t *l_ctx = (dap_net_trans_ctx_t*)a_stream->trans_ctx;
        
        debug_if(s_debug_more, L_DEBUG, "UDP packet processing: type=%u, a_stream=%p, trans_ctx=%p, l_ctx=%p", 
                 l_header->type, a_stream, a_stream->trans_ctx, l_ctx);

        if (l_header->type == DAP_STREAM_UDP_PKT_HANDSHAKE) {
             if (l_ctx && l_ctx->handshake_cb) {
                 // Client: Received Handshake Response (Bob Key + Ciphertext)
                 // Process it here to establish encryption, then call callback
                 debug_if(s_debug_more, L_DEBUG, "Client: processing handshake response, calling s_udp_handshake_response");
                 int l_result = s_udp_handshake_response(a_stream, l_payload, l_payload_size);
                 
                 debug_if(s_debug_more, L_DEBUG, "Handshake response processed, result=%d, calling callback", l_result);
                 // Call handshake callback with result (no data, just status)
                 l_ctx->handshake_cb(a_stream, NULL, 0, l_result);
                 l_ctx->handshake_cb = NULL;
                 debug_if(s_debug_more, L_DEBUG, "Handshake callback completed");
             } else {
                 // Server: Received Handshake Request (Alice Key)
                 void *l_response = NULL;
                 size_t l_response_size = 0;
                 s_udp_handshake_process(a_stream, l_payload, l_payload_size, &l_response, &l_response_size);
                 
                 if (l_response && l_response_size > 0) {
                     // Send response
                     dap_stream_trans_udp_header_t l_resp_hdr;
                     s_create_udp_header(&l_resp_hdr, DAP_STREAM_UDP_PKT_HANDSHAKE,
                                         (uint16_t)l_response_size, l_priv->seq_num++, l_header->session_id);
                     
                     size_t l_resp_total = sizeof(l_resp_hdr) + l_response_size;
                     uint8_t *l_resp_pkt = DAP_NEW_Z_SIZE(uint8_t, l_resp_total);
                     memcpy(l_resp_pkt, &l_resp_hdr, sizeof(l_resp_hdr));
                     memcpy(l_resp_pkt + sizeof(l_resp_hdr), l_response, l_response_size);
                     
                     dap_events_socket_write_unsafe(l_es, l_resp_pkt, l_resp_total);
                     DAP_DELETE(l_resp_pkt);
                     DAP_DELETE(l_response);
                 }
             }
        } else if (l_header->type == DAP_STREAM_UDP_PKT_SESSION_CREATE) {
             debug_if(s_debug_more, L_DEBUG, "Processing SESSION_CREATE packet: l_ctx=%p, session_create_cb=%p", 
                      l_ctx, l_ctx ? l_ctx->session_create_cb : NULL);
             if (l_ctx && l_ctx->session_create_cb) {
                 // Client: Received Session Response
                 debug_if(s_debug_more, L_DEBUG, "Client: parsing SESSION_CREATE response");
                 uint64_t l_sess_id = be64toh(l_header->session_id);
                 
                 // Validate stream before calling callback
                 if (!a_stream) {
                     log_it(L_ERROR, "Cannot call session_create_cb: stream is NULL");
                 } else if (!a_stream->trans) {
                     log_it(L_ERROR, "Cannot call session_create_cb: stream->trans is NULL");
                 } else if (!a_stream->trans_ctx) {
                     log_it(L_ERROR, "Cannot call session_create_cb: stream->trans_ctx is NULL");
                 } else {
                     debug_if(s_debug_more, L_DEBUG, "Calling session_create_cb: stream=%p, session_id=%u, cb=%p", 
                              a_stream, (uint32_t)l_sess_id, l_ctx->session_create_cb);
                     l_ctx->session_create_cb(a_stream, (uint32_t)l_sess_id, NULL, 0, 0);
                     debug_if(s_debug_more, L_DEBUG, "session_create_cb completed successfully");
                 }
                 l_ctx->session_create_cb = NULL;
             } else if (l_header->session_id == 0) {
                 // Server: Received Session Request (session_id == 0 means request)
                 debug_if(s_debug_more, L_DEBUG, "Server: processing SESSION_CREATE request");
                 if (!a_stream->session) {
                     a_stream->session = dap_stream_session_pure_new();
                 }
                 
                 uint64_t l_srv_sess_id = (uint64_t)time(NULL) | ((uint64_t)m_dap_random_u32() << 32);
                 l_priv->session_id = l_srv_sess_id;
                 if (a_stream->session) a_stream->session->id = l_srv_sess_id;
                 
                 // Send response
                 dap_stream_trans_udp_header_t l_resp_hdr;
                 s_create_udp_header(&l_resp_hdr, DAP_STREAM_UDP_PKT_SESSION_CREATE,
                                     0, l_priv->seq_num++, l_srv_sess_id);
                                     
                 dap_events_socket_write_unsafe(l_es, &l_resp_hdr, sizeof(l_resp_hdr));
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
    
    // Fallback for RAW Stream Packets (Data)
    size_t l_available = l_es->buf_in_size;
    size_t l_copy_size = (l_available < a_size) ? l_available : a_size;
    
    if (l_copy_size > 0) {
        memcpy(a_buffer, l_es->buf_in, l_copy_size);
        // Shift remaining data
        if (l_copy_size < l_available) {
            memmove(l_es->buf_in, 
                    l_es->buf_in + l_copy_size,
                    l_available - l_copy_size);
        }
        l_es->buf_in_size -= l_copy_size;
    }
    
    return (ssize_t)l_copy_size;
}

/**
 * @brief Write data to UDP trans
 */
static ssize_t s_udp_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        log_it(L_ERROR, "Invalid arguments for UDP write");
        return -1;
    }

    if (!a_stream->trans) {
        log_it(L_ERROR, "Stream has no trans");
        return -1;
    }

    dap_stream_trans_udp_private_t *l_priv = 
        (dap_stream_trans_udp_private_t*)a_stream->trans->_inheritor;
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        return -1;
    }

    // Check max packet size
    if (a_size > l_priv->config.max_packet_size) {
        log_it(L_WARNING, "Packet size %zu exceeds max %u, truncating",
               a_size, l_priv->config.max_packet_size);
        a_size = l_priv->config.max_packet_size;
    }

    // UDP write is done via dap_events_socket_write_unsafe
    // This is called from worker ctx
    // dap_events_socket_t *l_es = l_priv->esocket; // NO! Use trans_ctx
    dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(a_stream);
    if (!l_ctx || !l_ctx->esocket) {
        log_it(L_ERROR, "No esocket in trans ctx for write");
        return -1;
    }
    dap_events_socket_t *l_es = l_ctx->esocket;
    
    if (!l_es) {
        log_it(L_ERROR, "Trans has no esocket");
        return -1;
    }

    // Write directly using dap_events_socket_write_unsafe
    ssize_t l_sent = dap_events_socket_write_unsafe(l_es, a_data, a_size);
    if (l_sent < 0) {
        log_it(L_ERROR, "UDP send failed via dap_events_socket");
        return -1;
    }

    return l_sent;
}

/**
 * @brief Close UDP trans
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

    dap_stream_trans_udp_private_t *l_priv =
        (dap_stream_trans_udp_private_t*)a_stream->trans->_inheritor;
    if (l_priv) {
        log_it(L_INFO, "Closing UDP trans session 0x%lx", l_priv->session_id);
        l_priv->session_id = 0;
        l_priv->seq_num = 0;
    }
    
    // Clear stream pointer in trans_ctx to prevent use-after-free
    dap_net_trans_ctx_t *l_ctx = (dap_net_trans_ctx_t*)a_stream->trans_ctx;
    if (l_ctx) {
        // Clear stream pointer in UDP context
        if (l_ctx->_inheritor) {
            dap_udp_client_esocket_ctx_t *l_udp_ctx = (dap_udp_client_esocket_ctx_t *)l_ctx->_inheritor;
            l_udp_ctx->stream = NULL; // Prevent dangling pointer
        }
        l_ctx->stream = NULL;
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
    
    // UDP is connectionless - just add to worker
    dap_worker_add_events_socket(a_params->worker, l_es);
    
    log_it(L_DEBUG, "Created UDP socket %p", l_es);
    
    // Create UDP client context to store in trans_ctx->_inheritor (will be set later)
    dap_udp_client_esocket_ctx_t *l_udp_ctx = DAP_NEW_Z(dap_udp_client_esocket_ctx_t);
    if (!l_udp_ctx) {
        log_it(L_CRITICAL, "Failed to allocate UDP client context");
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    // Store client context (dap_client_t*) for later retrieval
    l_udp_ctx->client_ctx = a_params->client_ctx;
    l_udp_ctx->stream = NULL; // Will be set below when stream is created
    
    // Store UDP context temporarily in esocket->callbacks.arg until trans_ctx is created
    // When trans_ctx is created, it should be moved to trans_ctx->_inheritor
    // and esocket->_inheritor should point to trans_ctx
    l_es->callbacks.arg = l_udp_ctx;
    
    // Resolve host and set address using centralized function
    if (dap_events_socket_resolve_and_set_addr(l_es, a_params->host, a_params->port) < 0) {
        log_it(L_ERROR, "Failed to resolve address for UDP trans: %s:%u", a_params->host, a_params->port);
        DAP_DELETE(l_udp_ctx);
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
        DAP_DELETE(l_udp_ctx);
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    // For UDP: create stream early and return it (stream will be reused for all operations)
    dap_stream_t *l_stream = dap_stream_new_es_client(l_es, (dap_stream_node_addr_t *)a_params->node_addr, a_params->authorized);
    if (!l_stream) {
        log_it(L_CRITICAL, "Failed to create stream for UDP trans");
        DAP_DELETE(l_udp_ctx);
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    l_stream->trans = a_trans;
    l_udp_ctx->stream = l_stream;
    
    // Initialize trans_ctx and link esocket
    dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(l_stream);
    l_ctx->esocket = l_es;
    l_ctx->stream = l_stream;
    l_ctx->_inheritor = l_udp_ctx;
    
    // CRITICAL: Link esocket->_inheritor to trans_ctx so read callback can find stream
    l_es->_inheritor = l_ctx;
    
    log_it(L_DEBUG, "UDP trans created stream %p with trans_ctx %p (will be reused for all operations)", l_stream, l_ctx);
    
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
    if (!a_stream || !a_stream->trans_ctx) {
        return NULL;
    }
    
    dap_net_trans_ctx_t *l_ctx = (dap_net_trans_ctx_t*)a_stream->trans_ctx;
    if (!l_ctx->_inheritor) {
        return NULL;
    }
    
    // For UDP, trans_ctx->_inheritor is dap_udp_client_esocket_ctx_t
    dap_udp_client_esocket_ctx_t *l_udp_ctx = 
        (dap_udp_client_esocket_ctx_t*)l_ctx->_inheritor;
    
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
 * @brief Create UDP header
 */
static int s_create_udp_header(dap_stream_trans_udp_header_t *a_header,
                                uint8_t a_type, uint16_t a_length,
                                uint32_t a_seq_num, uint64_t a_session_id)
{
    if (!a_header)
        return -1;

    a_header->version = DAP_STREAM_UDP_VERSION;
    a_header->type = a_type;
    a_header->length = htons(a_length);
    a_header->seq_num = htonl(a_seq_num);
    a_header->session_id = htobe64(a_session_id);

    return 0;
}

/**
 * @brief Parse UDP header
 */
static int s_parse_udp_header(const dap_stream_trans_udp_header_t *a_header,
                               uint8_t *a_type, uint16_t *a_length,
                               uint32_t *a_seq_num, uint64_t *a_session_id)
{
    if (!a_header)
        return -1;

    if (a_header->version != DAP_STREAM_UDP_VERSION) {
        log_it(L_ERROR, "Unsupported UDP protocol version: %u", a_header->version);
        return -1;
    }

    if (a_type)
        *a_type = a_header->type;
    if (a_length)
        *a_length = ntohs(a_header->length);
    if (a_seq_num)
        *a_seq_num = ntohl(a_header->seq_num);
    if (a_session_id)
        *a_session_id = be64toh(a_header->session_id);

    return 0;
}

