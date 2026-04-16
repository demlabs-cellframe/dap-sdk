/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.

This file is part of DAP the open source project.

DAP is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DAP is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

See more details here <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include <time.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_net_trans_dns_stream.h"
#include "dap_net_trans_dns_server.h"
#include "dap_net_trans.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_net.h"

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
#include "dap_enc_kdf.h"
#include "dap_client.h"
#include "dap_client_fsm.h"
#include "dap_net_trans_ctx.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_stream_trans_dns"

static bool s_debug_more = false;
// DNS Trans Protocol Version
#define DAP_STREAM_DNS_VERSION 1

// Default configuration values
#define DAP_STREAM_DNS_DEFAULT_MAX_RECORD_SIZE  255
#define DAP_STREAM_DNS_DEFAULT_MAX_QUERY_SIZE   512
#define DAP_STREAM_DNS_DEFAULT_TIMEOUT_MS       5000

// Trans operations forward declarations
static int s_dns_init(dap_net_trans_t *a_trans, dap_config_t *a_config);
static void s_dns_deinit(dap_net_trans_t *a_trans);
static int s_dns_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port, 
                          dap_net_trans_connect_cb_t a_callback);
static int s_dns_listen(dap_net_trans_t *a_trans, const char *a_addr, uint16_t a_port,
                         dap_server_t *a_server);
static int s_dns_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out);
static int s_dns_handshake_init(dap_stream_t *a_stream,
                                 dap_net_handshake_params_t *a_params,
                                 dap_net_trans_handshake_cb_t a_callback);
static int s_dns_handshake_process(dap_stream_t *a_stream,
                                    const void *a_data, size_t a_data_size,
                                    void **a_response, size_t *a_response_size);
static int s_dns_session_create(dap_stream_t *a_stream,
                                 dap_net_session_params_t *a_params,
                                 dap_net_trans_session_cb_t a_callback);
static int s_dns_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_net_trans_ready_cb_t a_callback);
static ssize_t s_dns_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size);
static ssize_t s_dns_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static void s_dns_close(dap_stream_t *a_stream);
static uint32_t s_dns_get_capabilities(dap_net_trans_t *a_trans);
static size_t s_dns_get_max_packet_size(dap_net_trans_t *a_trans);
static int s_dns_stage_prepare(dap_net_trans_t *a_trans,
                               const dap_net_stage_prepare_params_t *a_params,
                               dap_net_stage_prepare_result_t *a_result);

// DNS trans operations table
static const dap_net_trans_ops_t s_dns_ops = {
    .init = s_dns_init,
    .deinit = s_dns_deinit,
    .connect = s_dns_connect,
    .listen = s_dns_listen,
    .accept = s_dns_accept,
    .handshake_init = s_dns_handshake_init,
    .handshake_process = s_dns_handshake_process,
    .session_create = s_dns_session_create,
    .session_start = s_dns_session_start,
    .read = s_dns_read,
    .write = s_dns_write,
    .close = s_dns_close,
    .get_capabilities = s_dns_get_capabilities,
    .register_server_handlers = NULL,
    .stage_prepare = s_dns_stage_prepare,
    .get_max_packet_size = s_dns_get_max_packet_size
};

// Helper functions
static dap_stream_trans_dns_private_t *s_get_private(dap_net_trans_t *a_trans);
static dns_client_ctx_t *s_get_or_create_client_ctx(dap_stream_t *a_stream);
static void s_dns_client_read_cb(dap_events_socket_t *a_es, void *a_arg);

/**
 * @brief Register DNS tunnel trans adapter
 */
int dap_net_trans_dns_stream_register(void)
{
    // Initialize DNS server module first (registers server operations)
    int l_ret = dap_net_trans_dns_server_init();
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize DNS server module: %d", l_ret);
        return l_ret;
    }

    log_it(L_DEBUG, "dap_net_trans_dns_stream_register: DNS server module initialized, registering trans");
    
    // Register DNS trans operations
    int l_ret_trans = dap_net_trans_register("DNS_TUNNEL",
                                                DAP_NET_TRANS_DNS_TUNNEL,
                                                &s_dns_ops,
                                                DAP_NET_TRANS_SOCKET_UDP,
                                                NULL);
    if (l_ret_trans != 0) {
        log_it(L_ERROR, "Failed to register DNS tunnel trans: %d", l_ret_trans);
        dap_net_trans_dns_server_deinit();
        return l_ret_trans;
    }

    log_it(L_NOTICE, "DNS tunnel trans registered successfully");
    return 0;
}

/**
 * @brief Unregister DNS tunnel trans adapter
 */
int dap_net_trans_dns_stream_unregister(void)
{
    int l_ret = dap_net_trans_unregister(DAP_NET_TRANS_DNS_TUNNEL);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to unregister DNS tunnel trans: %d", l_ret);
        return l_ret;
    }

    // Deinitialize DNS server module
    dap_net_trans_dns_server_deinit();

    log_it(L_NOTICE, "DNS tunnel trans unregistered successfully");
    return 0;
}

/**
 * @brief Create default DNS tunnel configuration
 */
dap_stream_trans_dns_config_t dap_stream_trans_dns_config_default(void)
{
    dap_stream_trans_dns_config_t l_config = {
        .max_record_size = DAP_STREAM_DNS_DEFAULT_MAX_RECORD_SIZE,
        .max_query_size = DAP_STREAM_DNS_DEFAULT_MAX_QUERY_SIZE,
        .query_timeout_ms = DAP_STREAM_DNS_DEFAULT_TIMEOUT_MS,
        .use_base32 = true,  // Base32 is more DNS-friendly
        .enable_compression = false,
        .domain_suffix = NULL  // Will be set by application
    };
    return l_config;
}

/**
 * @brief Set DNS tunnel configuration
 */
int dap_stream_trans_dns_set_config(dap_net_trans_t *a_trans,
                                        const dap_stream_trans_dns_config_t *a_config)
{
    if (!a_trans || !a_config) {
        log_it(L_ERROR, "Invalid parameters for DNS tunnel config");
        return -1;
    }

    dap_stream_trans_dns_private_t *l_priv = s_get_private(a_trans);
    if (!l_priv) {
        log_it(L_ERROR, "DNS tunnel trans not initialized");
        return -2;
    }

    // Copy configuration
    l_priv->config = *a_config;
    
    // Copy domain suffix string if provided
    if (a_config->domain_suffix) {
        DAP_DELETE(l_priv->config.domain_suffix);
        l_priv->config.domain_suffix = dap_strdup(a_config->domain_suffix);
    }

    log_it(L_DEBUG, "DNS tunnel configuration updated");
    return 0;
}

/**
 * @brief Get DNS tunnel configuration
 */
int dap_stream_trans_dns_get_config(dap_net_trans_t *a_trans,
                                         dap_stream_trans_dns_config_t *a_config)
{
    if (!a_trans || !a_config) {
        log_it(L_ERROR, "Invalid parameters for DNS tunnel config get");
        return -1;
    }

    dap_stream_trans_dns_private_t *l_priv = s_get_private(a_trans);
    if (!l_priv) {
        log_it(L_ERROR, "DNS tunnel trans not initialized");
        return -2;
    }

    *a_config = l_priv->config;
    
    // Copy domain suffix string
    if (l_priv->config.domain_suffix) {
        a_config->domain_suffix = dap_strdup(l_priv->config.domain_suffix);
    }

    return 0;
}

/**
 * @brief Check if stream is using DNS tunnel trans
 */
bool dap_stream_trans_is_dns(const dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans) {
        return false;
    }
    return a_stream->trans->type == DAP_NET_TRANS_DNS_TUNNEL;
}

/**
 * @brief Get DNS tunnel private data from stream
 */
dap_stream_trans_dns_private_t* dap_stream_trans_dns_get_private(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans) {
        return NULL;
    }

    if (a_stream->trans->type != DAP_NET_TRANS_DNS_TUNNEL) {
        return NULL;
    }

    return (dap_stream_trans_dns_private_t*)a_stream->trans->_inheritor;
}

// ============================================================================
// Trans Operations Implementation
// ============================================================================
// Note: Current implementation uses raw UDP-like approach for data transport.
// Full DNS tunnel with TXT record encoding/decoding can be layered on top.

/**
 * @brief Initialize DNS tunnel trans
 */
static int s_dns_init(dap_net_trans_t *a_trans, dap_config_t *a_config)
{
    UNUSED(a_config);
    
    if (!a_trans) {
        log_it(L_ERROR, "Invalid trans parameter");
        return -1;
    }

    // Allocate private data
    dap_stream_trans_dns_private_t *l_priv = DAP_NEW_Z(dap_stream_trans_dns_private_t);
    if (!l_priv) {
        log_it(L_CRITICAL, "Cannot allocate memory for DNS tunnel trans");
        return -2;
    }

    // Set default configuration
    l_priv->config = dap_stream_trans_dns_config_default();
    
    // Set as inheritor
    a_trans->_inheritor = l_priv;

    // DNS trans doesn't support session control (connectionless)
    a_trans->has_session_control = false;

    log_it(L_INFO, "DNS tunnel trans initialized");
    return 0;
}

/**
 * @brief Deinitialize DNS tunnel trans
 */
static void s_dns_deinit(dap_net_trans_t *a_trans)
{
    if (!a_trans) {
        return;
    }

    dap_stream_trans_dns_private_t *l_priv = s_get_private(a_trans);
    if (l_priv) {
        DAP_DELETE(l_priv->config.domain_suffix);
        DAP_DELETE(l_priv);
        a_trans->_inheritor = NULL;
    }

    log_it(L_INFO, "DNS tunnel trans deinitialized");
}

/**
 * @brief Connect DNS tunnel trans
 * @note Uses UDP-like connectionless approach for basic functionality
 *       Full DNS query/response parsing can be added later
 */
static int s_dns_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port, 
                          dap_net_trans_connect_cb_t a_callback)
{
    if (!a_stream || !a_host) {
        log_it(L_ERROR, "Invalid arguments for DNS connect");
        return -1;
    }

    if (!a_stream->trans) {
        log_it(L_ERROR, "Stream has no trans");
        return -1;
    }

    dap_stream_trans_dns_private_t *l_priv = s_get_private(a_stream->trans);
    if (!l_priv) {
        log_it(L_ERROR, "DNS trans not initialized");
        return -1;
    }

    // DNS is connectionless like UDP, so we can store connection info
    // and call callback immediately
    
    // Parse address and store in remote_addr
    struct sockaddr_in *l_addr_in = (struct sockaddr_in*)&l_priv->remote_addr;
    l_addr_in->sin_family = AF_INET;
    l_addr_in->sin_port = htons(a_port);
    
    if (inet_pton(AF_INET, a_host, &l_addr_in->sin_addr) != 1) {
        log_it(L_ERROR, "Invalid IPv4 address: %s", a_host);
        return -1;
    }

    l_priv->remote_addr_len = sizeof(struct sockaddr_in);
    l_priv->esocket = a_stream->esocket;
    
    // Update esocket address storage for sendto
    if (l_priv->esocket) {
        memcpy(&l_priv->esocket->addr_storage, &l_priv->remote_addr, l_priv->remote_addr_len);
        l_priv->esocket->addr_size = l_priv->remote_addr_len;
    }
    
    log_it(L_INFO, "DNS tunnel trans connecting to %s:%u", a_host, a_port);
    
    // Call callback immediately (DNS is connectionless)
    if (a_callback) {
        a_callback(a_stream, 0);
    }
    
    return 0;
}

/**
 * @brief Start listening for DNS tunnel connections
 */
static int s_dns_listen(dap_net_trans_t *a_trans, const char *a_addr, uint16_t a_port,
                         dap_server_t *a_server)
{
    if (!a_trans) {
        log_it(L_ERROR, "Invalid arguments for DNS tunnel listen");
        return -1;
    }

    // DNS listening is handled by dap_net_trans_dns_server
    // This function is called from the trans layer
    log_it(L_INFO, "DNS tunnel trans listening on %s:%u (via dap_net_trans_dns_server)", 
           a_addr ? a_addr : "0.0.0.0", a_port);
    return 0;
}

/**
 * @brief Accept incoming DNS tunnel "connection"
 * @note DNS is connectionless, so "accept" just returns success
 */
static int s_dns_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out)
{
    if (!a_listener || !a_stream_out) {
        log_it(L_ERROR, "Invalid arguments for DNS accept");
        return -1;
    }
    
    // DNS is connectionless, so "accept" creates a new stream for DNS query source
    // Stream is created by server layer and associated with socket
    debug_if(s_debug_more, L_DEBUG, "DNS tunnel trans accept");
    return 0;
}

/**
 * @brief Initialize encryption handshake (async)
 *
 * Sends alice_pub_key to server and installs a read callback.
 * The actual handshake completes when the server responds with
 * bob_ciphertext, processed in s_dns_client_read_cb.
 */
static int s_dns_handshake_init(dap_stream_t *a_stream,
                                 dap_net_handshake_params_t *a_params,
                                 dap_net_trans_handshake_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid arguments for DNS handshake init");
        return -1;
    }

    if (!a_stream->trans || !a_stream->esocket) {
        log_it(L_ERROR, "Stream has no trans or esocket");
        return -1;
    }

    if (a_params->alice_pub_key_size == 0) {
        log_it(L_ERROR, "DNS handshake: alice_pub_key is empty");
        return -1;
    }

    log_it(L_INFO, "DNS handshake init: enc_type=%d, pkey_type=%d, key_size=%zu",
           a_params->enc_type, a_params->pkey_exchange_type, a_params->alice_pub_key_size);

    dap_events_socket_t *l_es = a_stream->esocket;

    a_stream->trans_ctx->handshake_cb = a_callback;
    l_es->callbacks.read_callback = s_dns_client_read_cb;

    size_t l_sent = dap_events_socket_sendto_unsafe(l_es,
                                                   a_params->alice_pub_key,
                                                   a_params->alice_pub_key_size,
                                                   &l_es->addr_storage,
                                                   l_es->addr_size);
    if (l_sent != a_params->alice_pub_key_size) {
        log_it(L_ERROR, "DNS handshake send failed: %zu of %zu bytes",
               l_sent, a_params->alice_pub_key_size);
        return -1;
    }

    log_it(L_INFO, "DNS handshake: sent alice_pub_key (%zu bytes), waiting for server response",
           a_params->alice_pub_key_size);

    return 0;
}

/**
 * @brief Process handshake data (server-side)
 *
 * Server-side handshake processing for DNS transport.
 * DNS handshake uses the same pattern as UDP: the handshake data
 * (alice public key) is sent as raw bytes and processed by
 * dap_stream_handshake on the server side. This function delegates
 * to the standard handshake processing.
 */
static int s_dns_handshake_process(dap_stream_t *a_stream,
                                    const void *a_data, size_t a_data_size,
                                    void **a_response, size_t *a_response_size)
{
    if (!a_stream || !a_data || a_data_size == 0) {
        log_it(L_ERROR, "Invalid arguments for DNS handshake process");
        return -1;
    }

    debug_if(s_debug_more, L_DEBUG, "DNS handshake process: %zu bytes (delegated to dap_stream_handshake)", a_data_size);

    // The handshake data is raw alice_pub_key sent by the client.
    // On the server, this is processed by dap_stream_handshake module.
    // No DNS-specific transformation is needed.
    if (a_response) *a_response = NULL;
    if (a_response_size) *a_response_size = 0;

    return 0;
}

/**
 * @brief Create session
 * @note Uses UDP-like approach for basic functionality
 */
static int s_dns_session_create(dap_stream_t *a_stream,
                                 dap_net_session_params_t *a_params,
                                 dap_net_trans_session_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid arguments for DNS session create");
        return -1;
    }

    if (!a_stream->trans) {
        log_it(L_ERROR, "Stream has no trans");
        return -1;
    }

    // Generate session ID (similar to UDP)
    uint64_t l_session_id = (uint64_t)time(NULL) | ((uint64_t)m_dap_random_u32() << 32);
    log_it(L_INFO, "DNS session created: ID=0x%lx", l_session_id);
    
    // Call callback with session ID (no full response data for DNS trans)
    if (a_callback) {
        a_callback(a_stream, (uint32_t)l_session_id, NULL, 0, 0);
    }
    
    return 0;
}

/**
 * @brief Start session
 * @note Uses UDP-like approach for basic functionality
 */
static int s_dns_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_net_trans_ready_cb_t a_callback)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream for DNS session start");
        return -1;
    }

    debug_if(s_debug_more, L_DEBUG, "DNS session start: session_id=%u", a_session_id);
    
    // Call callback immediately (DNS session ready, similar to UDP)
    if (a_callback) {
        a_callback(a_stream, 0);
    }
    
    return 0;
}

/**
 * @brief Read data from DNS tunnel
 *
 * Called from client read path with (NULL, 0). Reads from esocket->buf_in
 * and processes the data as raw DAP stream packets via dap_stream_data_proc_read.
 * Returns the number of bytes consumed so the caller can shrink buf_in.
 */
static ssize_t s_dns_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    UNUSED(a_buffer);
    UNUSED(a_size);

    if (!a_stream) {
        return -1;
    }

    if (!a_stream->esocket || a_stream->esocket->buf_in_size == 0) {
        return 0;
    }

    // DNS data in buf_in is raw stream data (no DNS-specific framing in current impl).
    // Delegate to dap_stream_data_proc_read which processes DAP stream packets
    // from the esocket's buf_in and returns bytes consumed.
    return (ssize_t)dap_stream_data_proc_read(a_stream);
}

/**
 * @brief Write data to DNS tunnel
 * @note Uses UDP-like approach: writes directly to esocket
 *       Full DNS query generation with encoding can be added later
 */
typedef struct dns_client_sendto_args {
    dap_events_socket_t *esocket;
    void *data;
    size_t size;
    struct sockaddr_storage addr;
    socklen_t addr_len;
} dns_client_sendto_args_t;

static void s_dns_client_sendto_callback(void *a_arg)
{
    dns_client_sendto_args_t *l_args = (dns_client_sendto_args_t *)a_arg;
    if(l_args->esocket)
        dap_events_socket_sendto_unsafe(l_args->esocket,
            l_args->data, l_args->size,
            &l_args->addr, l_args->addr_len);
    DAP_DELETE(l_args->data);
    DAP_DELETE(l_args);
}

/**
 * @brief Write data to DNS tunnel
 * @note Worker-aware: if called from a different worker thread,
 *       marshals the sendto onto the esocket's owner worker via callback.
 */
static ssize_t s_dns_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if(!a_stream || !a_data || a_size == 0) {
        log_it(L_ERROR, "Invalid arguments for DNS write");
        return -1;
    }

    dap_events_socket_t *l_es = a_stream->esocket;
    if(!l_es) {
        log_it(L_ERROR, "DNS write: no esocket");
        return -1;
    }

    dap_worker_t *l_current = dap_worker_get_current();
    dap_worker_t *l_target = l_es->worker;

    if(l_current == l_target) {
        size_t l_sent = dap_events_socket_sendto_unsafe(l_es, a_data, a_size,
                                                        &l_es->addr_storage, l_es->addr_size);
        if(l_sent != a_size)
            log_it(L_WARNING, "DNS write incomplete: %zu of %zu bytes (flags=0x%x)", l_sent, a_size, l_es->flags);
        return (ssize_t)l_sent;
    }

    dns_client_sendto_args_t *l_args = DAP_NEW_Z(dns_client_sendto_args_t);
    if(!l_args)
        return -1;
    l_args->esocket = l_es;
    l_args->data = DAP_DUP_SIZE(a_data, a_size);
    if(!l_args->data) {
        DAP_DELETE(l_args);
        return -1;
    }
    l_args->size = a_size;
    memcpy(&l_args->addr, &l_es->addr_storage, l_es->addr_size);
    l_args->addr_len = l_es->addr_size;
    dap_worker_exec_callback_on(l_target, s_dns_client_sendto_callback, l_args);
    return (ssize_t)a_size;
}

/**
 * @brief Close DNS tunnel connection
 *
 * Cleans up DNS-specific state: resets the esocket reference,
 * clears the remote address, and resets query/sequence counters.
 */
static void s_dns_close(dap_stream_t *a_stream)
{
    if (!a_stream)
        return;

    if (a_stream->trans) {
        dap_stream_trans_dns_private_t *l_priv = s_get_private(a_stream->trans);
        if (l_priv) {
            l_priv->esocket = NULL;
            memset(&l_priv->remote_addr, 0, sizeof(l_priv->remote_addr));
            l_priv->remote_addr_len = 0;
            l_priv->query_id = 0;
            l_priv->seq_num = 0;
        }
    }

    debug_if(s_debug_more, L_DEBUG, "DNS tunnel trans closed");
}

/**
 * @brief Prepare DNS socket for client stage
 * 
 * Fully prepares esocket: creates, sets callbacks, and adds to worker.
 * DNS tunneling uses UDP (connectionless), so no connection step is needed.
 * Trans is responsible for complete esocket lifecycle management.
 */
static int s_dns_stage_prepare(dap_net_trans_t *a_trans,
                               const dap_net_stage_prepare_params_t *a_params,
                               dap_net_stage_prepare_result_t *a_result)
{
    if (!a_trans || !a_params || !a_result) {
        log_it(L_ERROR, "Invalid arguments for DNS stage_prepare");
        return -1;
    }
    
    if (!a_params->worker) {
        log_it(L_ERROR, "Worker is required for DNS stage_prepare");
        a_result->error_code = -1;
        return -1;
    }
    
    // Initialize result
    a_result->esocket = NULL;
    a_result->stream = NULL;
    a_result->error_code = 0;
    
    // DNS tunneling uses UDP socket - create using platform-independent function
    dap_events_socket_t *l_es = dap_events_socket_create_platform(PF_INET, SOCK_DGRAM, IPPROTO_UDP, a_params->callbacks);
    if (!l_es) {
        log_it(L_ERROR, "Failed to create DNS socket");
        a_result->error_code = -1;
        return -1;
    }
    
    l_es->type = DESCRIPTOR_TYPE_SOCKET_UDP;
    l_es->_inheritor = a_params->client_ctx;
    
    int l_buf_size = 4 * 1024 * 1024;
    setsockopt(l_es->fd, SOL_SOCKET, SO_RCVBUF, (const char *)&l_buf_size, sizeof(l_buf_size));
    setsockopt(l_es->fd, SOL_SOCKET, SO_SNDBUF, (const char *)&l_buf_size, sizeof(l_buf_size));
    
    // Resolve host and set address using centralized function
    if (dap_events_socket_resolve_and_set_addr(l_es, a_params->host, a_params->port) < 0) {
        log_it(L_ERROR, "Failed to resolve address for DNS trans");
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }

#ifdef DAP_OS_WINDOWS
    {
        struct sockaddr_in l_bind_addr = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = 0 };
        if(bind(l_es->socket, (struct sockaddr *)&l_bind_addr, sizeof(l_bind_addr)) < 0) {
            log_it(L_ERROR, "Failed to bind UDP socket for DNS trans: %d", WSAGetLastError());
            dap_events_socket_delete_unsafe(l_es, true);
            a_result->error_code = -1;
            return -1;
        }
        l_es->addr_size = sizeof(struct sockaddr_in);
    }
#endif
    
    // DNS tunneling uses UDP (connectionless) - just add to worker
    dap_worker_add_events_socket(a_params->worker, l_es);
    
    // Create stream for this connection (same pattern as HTTP/WebSocket/UDP)
    dap_stream_t *l_stream = dap_stream_new_es_client(l_es, (dap_stream_node_addr_t *)a_params->node_addr, a_params->authorized);
    if (!l_stream) {
        log_it(L_CRITICAL, "Failed to create stream for DNS trans");
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    l_stream->trans = a_trans;
    
    a_result->esocket = l_es;
    a_result->stream = l_stream;
    a_result->error_code = 0;
    debug_if(s_debug_more, L_DEBUG, "DNS socket and stream prepared for %s:%u", a_params->host, a_params->port);
    return 0;
}

/**
 * @brief Get DNS tunnel trans capabilities
 */
static uint32_t s_dns_get_capabilities(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    
    // DNS tunnel characteristics:
    // - Connectionless (like UDP)
    // - Low latency (no connection establishment)
    // - Built-in obfuscation (looks like DNS)
    // - No reliability guarantees
    // - Limited payload size (DNS TXT records)
    return DAP_NET_TRANS_CAP_OBFUSCATION |
           DAP_NET_TRANS_CAP_LOW_LATENCY |
           DAP_NET_TRANS_CAP_BIDIRECTIONAL;
}

/**
 * @brief Max packet size for DNS tunnel (UDP-based, same conservative limit as UDP transport)
 */
static size_t s_dns_get_max_packet_size(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return 1200;
}

static dns_client_ctx_t *s_get_or_create_client_ctx(dap_stream_t *a_stream)
{
    (void)a_stream;
    return NULL;
}

/**
 * @brief Client read callback — processes server's KEM response
 *
 * When the DNS server responds with bob_ciphertext, this callback:
 * 1. Retrieves alice's KEM key from dap_net_trans_ctx (FSM-owned)
 * 2. Performs KEM decapsulation to derive the shared secret
 * 3. Derives a symmetric handshake_key via KDF
 * 4. Sets the stream_key on dap_net_trans_ctx
 * 5. Calls the stored handshake callback to progress the FSM
 */
static void s_dns_client_read_cb(dap_events_socket_t *a_es, void *a_arg)
{
    (void)a_arg;

    if (!a_es || a_es->buf_in_size == 0)
        return;

    dap_client_t *l_client = (dap_client_t *)a_es->_inheritor;
    if (!l_client) {
        log_it(L_ERROR, "DNS client read: no client context on esocket");
        a_es->buf_in_size = 0;
        return;
    }

    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(l_client);
    dap_net_trans_ctx_t *l_tc = l_fsm ? l_fsm->trans_ctx : NULL;
    if (!l_fsm || !l_tc || !l_tc->stream) {
        log_it(L_ERROR, "DNS client read: no trans_ctx or stream");
        a_es->buf_in_size = 0;
        return;
    }

    dap_stream_t *l_stream = l_tc->stream;
    if (!l_stream->trans_ctx) {
        log_it(L_ERROR, "DNS client read: no trans_ctx");
        a_es->buf_in_size = 0;
        return;
    }

    if (!l_stream->trans_ctx->handshake_cb) {
        size_t l_bytes = dap_stream_data_proc_read_ext(l_stream, a_es->buf_in, a_es->buf_in_size);
        if (l_bytes > 0)
            dap_events_socket_shrink_buf_in(a_es, l_bytes);
        a_es->buf_in_size = 0;
        return;
    }

    log_it(L_INFO, "DNS client: received server handshake response (%zu bytes)", a_es->buf_in_size);

    dap_enc_key_t *l_alice_key = l_tc->session_key_open;
    if (!l_alice_key || !l_alice_key->gen_alice_shared_key) {
        log_it(L_ERROR, "DNS client: no alice KEM key for decapsulation");
        l_stream->trans_ctx->handshake_cb(l_stream, NULL, 0, -1);
        a_es->buf_in_size = 0;
        return;
    }

    size_t l_shared_size = l_alice_key->gen_alice_shared_key(l_alice_key,
                                                             l_alice_key->priv_key_data,
                                                             a_es->buf_in_size,
                                                             a_es->buf_in);
    if (l_shared_size == 0 || !l_alice_key->shared_key) {
        log_it(L_ERROR, "DNS client: KEM decapsulation failed (shared_size=%zu)", l_shared_size);
        l_stream->trans_ctx->handshake_cb(l_stream, NULL, 0, -1);
        a_es->buf_in_size = 0;
        return;
    }

    log_it(L_INFO, "DNS client: KEM decapsulation succeeded");

    dap_enc_key_t *l_handshake_key = dap_enc_kdf_create_cipher_key(
        l_alice_key,
        DAP_ENC_KEY_TYPE_SALSA2012,
        "dns_handshake", 13,
        0, 32);

    if (!l_handshake_key) {
        log_it(L_ERROR, "DNS client: KDF failed");
        l_stream->trans_ctx->handshake_cb(l_stream, NULL, 0, -1);
        a_es->buf_in_size = 0;
        return;
    }

    if (l_tc->stream_key)
        dap_enc_key_delete(l_tc->stream_key);
    l_tc->stream_key = dap_enc_key_dup(l_handshake_key);
    dap_enc_key_delete(l_handshake_key);

    log_it(L_INFO, "DNS client: handshake complete, stream_key established");

    dap_net_trans_handshake_cb_t l_cb = l_stream->trans_ctx->handshake_cb;
    l_stream->trans_ctx->handshake_cb = NULL;

    l_cb(l_stream, NULL, 0, 0);

    a_es->buf_in_size = 0;
}

/**
 * @brief Get private data from trans
 */
static dap_stream_trans_dns_private_t *s_get_private(dap_net_trans_t *a_trans)
{
    if (!a_trans || !a_trans->_inheritor) {
        return NULL;
    }
    
    return (dap_stream_trans_dns_private_t*)a_trans->_inheritor;
}

