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
#include "dap_stream_transport_udp.h"
#include "dap_stream_handshake.h"
#include "dap_stream.h"
#include "dap_server.h"
#include "dap_enc_server.h"
#include "dap_client.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_stream_transport_udp"

// UDP Transport Protocol Version
#define DAP_STREAM_UDP_VERSION 1

// Default configuration values
#define DAP_STREAM_UDP_DEFAULT_MAX_PACKET_SIZE  1400
#define DAP_STREAM_UDP_DEFAULT_KEEPALIVE_MS     30000

// Transport operations forward declarations
static int s_udp_init(dap_stream_transport_t *a_transport, dap_config_t *a_config);
static void s_udp_deinit(dap_stream_transport_t *a_transport);
static int s_udp_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port, 
                          dap_stream_transport_connect_cb_t a_callback);
static int s_udp_listen(dap_stream_transport_t *a_transport, const char *a_addr, uint16_t a_port,
                         dap_server_t *a_server);
static int s_udp_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out);
static int s_udp_handshake_init(dap_stream_t *a_stream,
                                 dap_stream_handshake_params_t *a_params,
                                 dap_stream_transport_handshake_cb_t a_callback);
static int s_udp_handshake_process(dap_stream_t *a_stream,
                                    const void *a_data, size_t a_data_size,
                                    void **a_response, size_t *a_response_size);
static int s_udp_session_create(dap_stream_t *a_stream,
                                 dap_stream_session_params_t *a_params,
                                 dap_stream_transport_session_cb_t a_callback);
static int s_udp_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_stream_transport_ready_cb_t a_callback);
static ssize_t s_udp_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size);
static ssize_t s_udp_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static void s_udp_close(dap_stream_t *a_stream);
static uint32_t s_udp_get_capabilities(dap_stream_transport_t *a_transport);

// UDP transport operations table
static const dap_stream_transport_ops_t s_udp_ops = {
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
    .get_capabilities = s_udp_get_capabilities
};

// Helper functions
static dap_stream_transport_udp_private_t *s_get_private(dap_stream_transport_t *a_transport);
static int s_create_udp_header(dap_stream_transport_udp_header_t *a_header,
                                uint8_t a_type, uint16_t a_length,
                                uint32_t a_seq_num, uint64_t a_session_id);
static int s_parse_udp_header(const dap_stream_transport_udp_header_t *a_header,
                               uint8_t *a_type, uint16_t *a_length,
                               uint32_t *a_seq_num, uint64_t *a_session_id);

/**
 * @brief Register UDP transport adapter
 */
int dap_stream_transport_udp_register(void)
{
    int l_ret = dap_stream_transport_register("UDP",
                                                DAP_STREAM_TRANSPORT_UDP_BASIC,
                                                &s_udp_ops,
                                                NULL);  // No inheritor needed at registration
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register UDP transport: %d", l_ret);
        return l_ret;
    }

    log_it(L_NOTICE, "UDP transport registered successfully");
    return 0;
}

/**
 * @brief Unregister UDP transport adapter
 */
int dap_stream_transport_udp_unregister(void)
{
    int l_ret = dap_stream_transport_unregister(DAP_STREAM_TRANSPORT_UDP_BASIC);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to unregister UDP transport: %d", l_ret);
        return l_ret;
    }

    log_it(L_NOTICE, "UDP transport unregistered successfully");
    return 0;
}

/**
 * @brief Create default UDP configuration
 */
dap_stream_transport_udp_config_t dap_stream_transport_udp_config_default(void)
{
    dap_stream_transport_udp_config_t l_config = {
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
int dap_stream_transport_udp_set_config(dap_stream_transport_t *a_transport,
                                        const dap_stream_transport_udp_config_t *a_config)
{
    if (!a_transport || !a_config) {
        log_it(L_ERROR, "Invalid arguments for UDP config set");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv) {
        log_it(L_ERROR, "UDP transport not initialized");
        return -1;
    }

    memcpy(&l_priv->config, a_config, sizeof(dap_stream_transport_udp_config_t));
    log_it(L_DEBUG, "UDP transport configuration updated");
    return 0;
}

/**
 * @brief Get UDP configuration
 */
int dap_stream_transport_udp_get_config(dap_stream_transport_t *a_transport,
                                        dap_stream_transport_udp_config_t *a_config)
{
    if (!a_transport || !a_config) {
        log_it(L_ERROR, "Invalid arguments for UDP config get");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv) {
        log_it(L_ERROR, "UDP transport not initialized");
        return -1;
    }

    memcpy(a_config, &l_priv->config, sizeof(dap_stream_transport_udp_config_t));
    return 0;
}

/**
 * @brief Check if stream is using UDP transport
 */
bool dap_stream_transport_is_udp(const dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->stream_transport)
        return false;
    return a_stream->stream_transport->type == DAP_STREAM_TRANSPORT_UDP_BASIC;
}

/**
 * @brief Get UDP server from transport
 */
dap_server_t *dap_stream_transport_udp_get_server(const dap_stream_t *a_stream)
{
    if (!dap_stream_transport_is_udp(a_stream))
        return NULL;

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_stream->stream_transport);
    return l_priv ? l_priv->server : NULL;
}

/**
 * @brief Get UDP event socket from transport
 */
dap_events_socket_t *dap_stream_transport_udp_get_esocket(const dap_stream_t *a_stream)
{
    if (!dap_stream_transport_is_udp(a_stream))
        return NULL;

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_stream->stream_transport);
    return l_priv ? l_priv->esocket : NULL;
}

/**
 * @brief Get current session ID
 */
uint64_t dap_stream_transport_udp_get_session_id(const dap_stream_t *a_stream)
{
    if (!dap_stream_transport_is_udp(a_stream))
        return 0;

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_stream->stream_transport);
    return l_priv ? l_priv->session_id : 0;
}

/**
 * @brief Get current sequence number
 */
uint32_t dap_stream_transport_udp_get_seq_num(const dap_stream_t *a_stream)
{
    if (!dap_stream_transport_is_udp(a_stream))
        return 0;

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_stream->stream_transport);
    return l_priv ? l_priv->seq_num : 0;
}

/**
 * @brief Set remote peer address
 */
int dap_stream_transport_udp_set_remote_addr(dap_stream_transport_t *a_transport,
                                              const struct sockaddr *a_addr,
                                              socklen_t a_addr_len)
{
    if (!a_transport || !a_addr) {
        log_it(L_ERROR, "Invalid arguments for set remote addr");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv) {
        log_it(L_ERROR, "UDP transport not initialized");
        return -1;
    }

    memcpy(&l_priv->remote_addr, a_addr, a_addr_len);
    l_priv->remote_addr_len = a_addr_len;
    return 0;
}

/**
 * @brief Get remote peer address
 */
int dap_stream_transport_udp_get_remote_addr(dap_stream_transport_t *a_transport,
                                              struct sockaddr *a_addr,
                                              socklen_t *a_addr_len)
{
    if (!a_transport || !a_addr || !a_addr_len) {
        log_it(L_ERROR, "Invalid arguments for get remote addr");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv) {
        log_it(L_ERROR, "UDP transport not initialized");
        return -1;
    }

    memcpy(a_addr, &l_priv->remote_addr, l_priv->remote_addr_len);
    *a_addr_len = l_priv->remote_addr_len;
    return 0;
}

/**
 * @brief Send raw UDP packet
 */
ssize_t dap_stream_transport_udp_send_raw(dap_stream_transport_t *a_transport,
                                           const void *a_data,
                                           size_t a_data_size)
{
    if (!a_transport || !a_data || a_data_size == 0) {
        log_it(L_ERROR, "Invalid arguments for UDP send raw");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv || !l_priv->esocket) {
        log_it(L_ERROR, "UDP transport not ready for sending");
        return -1;
    }

    // Send via event socket (unsafe version - we're in worker context)
    // Stream is always in worker context, so _unsafe version is safe and efficient
    size_t l_ret = dap_events_socket_write_unsafe(l_priv->esocket, a_data, a_data_size);
    if (l_ret != a_data_size) {
        log_it(L_WARNING, "UDP send incomplete: sent %zu of %zu bytes", l_ret, a_data_size);
    }

    return (ssize_t)l_ret;
}

/**
 * @brief Receive raw UDP packet
 */
ssize_t dap_stream_transport_udp_recv_raw(dap_stream_transport_t *a_transport,
                                           void *a_data,
                                           size_t a_data_size)
{
    if (!a_transport || !a_data || a_data_size == 0) {
        log_it(L_ERROR, "Invalid arguments for UDP recv raw");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv || !l_priv->esocket) {
        log_it(L_ERROR, "UDP transport not ready for receiving");
        return -1;
    }

    // UDP receive is handled by dap_events_socket read callback
    // (s_esocket_data_read in dap_stream.c registered via dap_stream_add_proc_udp)
    // This function is called from within that callback, so data is already available
    // in the esocket buffer. The actual reading is done by the event loop.
    //
    // For now, return 0 indicating data should be read from esocket directly
    log_it(L_DEBUG, "UDP recv_raw - data handled by dap_events_socket read callback");
    return 0;
}

//=============================================================================
// Transport operations implementation
//=============================================================================

/**
 * @brief Initialize UDP transport
 */
static int s_udp_init(dap_stream_transport_t *a_transport, dap_config_t *a_config)
{
    if (!a_transport) {
        log_it(L_ERROR, "Cannot init NULL transport");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = DAP_NEW_Z(dap_stream_transport_udp_private_t);
    if (!l_priv) {
        log_it(L_CRITICAL, "Memory allocation failed for UDP private data");
        return -1;
    }

    l_priv->config = dap_stream_transport_udp_config_default();
    l_priv->session_id = 0;
    l_priv->seq_num = 0;
    l_priv->server = NULL;
    l_priv->esocket = NULL;
    l_priv->remote_addr_len = 0;
    l_priv->user_data = NULL;
    
    UNUSED(a_config); // Config can be used to override defaults

    a_transport->_inheritor = l_priv;
    log_it(L_DEBUG, "UDP transport initialized (uses dap_events_socket for I/O)");
    return 0;
}

/**
 * @brief Deinitialize UDP transport
 */
static void s_udp_deinit(dap_stream_transport_t *a_transport)
{
    if (!a_transport)
        return;

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
    if (l_priv) {
        DAP_DELETE(l_priv);
        a_transport->_inheritor = NULL;
        log_it(L_DEBUG, "UDP transport deinitialized");
    }
}

/**
 * @brief Connect to remote UDP endpoint
 */
static int s_udp_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port,
                          dap_stream_transport_connect_cb_t a_callback)
{
    if (!a_stream || !a_host) {
        log_it(L_ERROR, "Invalid arguments for UDP connect");
        return -1;
    }

    if (!a_stream->stream_transport) {
        log_it(L_ERROR, "Stream has no transport");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = 
        (dap_stream_transport_udp_private_t*)a_stream->stream_transport->_inheritor;
    if (!l_priv) {
        log_it(L_ERROR, "UDP transport not initialized");
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
    l_priv->esocket = a_stream->esocket;  // Store esocket from stream
    
    log_it(L_INFO, "UDP transport connected to %s:%u", a_host, a_port);
    
    // Call callback immediately (UDP is connectionless)
    if (a_callback) {
        a_callback(a_stream, 0);
    }
    
    return 0;
}

/**
 * @brief Start listening for UDP connections
 */
static int s_udp_listen(dap_stream_transport_t *a_transport, const char *a_addr, uint16_t a_port,
                         dap_server_t *a_server)
{
    if (!a_transport) {
        log_it(L_ERROR, "Invalid arguments for UDP listen");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = 
        (dap_stream_transport_udp_private_t*)a_transport->_inheritor;
    if (!l_priv) {
        log_it(L_ERROR, "UDP transport not initialized");
        return -1;
    }

    // Store server reference
    l_priv->server = a_server;
    
    // UDP listening is handled by dap_server_t which creates dap_events_socket_t
    // The server will call callbacks registered via dap_stream_add_proc_udp()
    // which use dap_events_socket for all I/O operations
    log_it(L_INFO, "UDP transport listening on %s:%u (via dap_events_socket)", 
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
    log_it(L_DEBUG, "UDP transport accept");
    return 0;
}

/**
 * @brief Initialize encryption handshake
 */
static int s_udp_handshake_init(dap_stream_t *a_stream,
                                 dap_stream_handshake_params_t *a_params,
                                 dap_stream_transport_handshake_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid arguments for UDP handshake init");
        return -1;
    }

    if (!a_stream->stream_transport) {
        log_it(L_ERROR, "Stream has no transport");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = 
        (dap_stream_transport_udp_private_t*)a_stream->stream_transport->_inheritor;
    if (!l_priv || !l_priv->esocket) {
        log_it(L_ERROR, "UDP transport not ready for handshake");
        return -1;
    }

    log_it(L_INFO, "UDP handshake init: enc_type=%d, pkey_type=%d",
           a_params->enc_type, a_params->pkey_exchange_type);
    
    // Generate random session ID for this connection
    if (randombytes((uint8_t*)&l_priv->session_id, sizeof(l_priv->session_id)) != 0) {
        log_it(L_ERROR, "Failed to generate random session ID");
        return -1;
    }
    l_priv->seq_num = 0;
    
    // Build handshake request using dap_enc_server API
    dap_enc_server_request_t l_enc_request = {
        .enc_type = a_params->enc_type,
        .pkey_exchange_type = a_params->pkey_exchange_type,
        .pkey_exchange_size = a_params->pkey_exchange_size,
        .block_key_size = a_params->block_key_size,
        .protocol_version = a_params->protocol_version,
        .sign_count = 0,  // TODO: get from a_params when available
        .alice_msg = NULL,  // TODO: get from a_params when available
        .alice_msg_size = 0,
        .sign_hashes = NULL,
        .sign_hashes_count = 0
    };
    
    // Process handshake via transport-independent encryption server
    dap_enc_server_response_t *l_enc_response = NULL;
    int l_ret = dap_enc_server_process_request(&l_enc_request, &l_enc_response);
    
    if (l_ret != 0 || !l_enc_response || !l_enc_response->success) {
        log_it(L_ERROR, "UDP handshake init failed: %s",
               l_enc_response && l_enc_response->error_message ? 
               l_enc_response->error_message : "unknown error");
        if (l_enc_response)
            dap_enc_server_response_free(l_enc_response);
        return -1;
    }
    
    // Create UDP packet with HANDSHAKE type
    dap_stream_transport_udp_header_t l_header;
    s_create_udp_header(&l_header, DAP_STREAM_UDP_PKT_HANDSHAKE,
                        (uint16_t)l_enc_response->encrypt_msg_len,
                        l_priv->seq_num++, l_priv->session_id);
    
    // Allocate buffer for header + payload
    size_t l_packet_size = sizeof(l_header) + l_enc_response->encrypt_msg_len;
    uint8_t *l_packet = DAP_NEW_Z_SIZE(uint8_t, l_packet_size);
    if (!l_packet) {
        log_it(L_CRITICAL, "Memory allocation failed for UDP handshake packet");
        dap_enc_server_response_free(l_enc_response);
        return -1;
    }
    
    // Copy header and payload
    memcpy(l_packet, &l_header, sizeof(l_header));
    memcpy(l_packet + sizeof(l_header), l_enc_response->encrypt_msg, 
           l_enc_response->encrypt_msg_len);
    
    // Send via dap_events_socket_write_unsafe
    size_t l_sent = dap_events_socket_write_unsafe(l_priv->esocket, l_packet, l_packet_size);
    
    DAP_DELETE(l_packet);
    dap_enc_server_response_free(l_enc_response);
    
    if (l_sent != l_packet_size) {
        log_it(L_ERROR, "UDP handshake send incomplete: %zu of %zu bytes", l_sent, l_packet_size);
        return -1;
    }
    
    log_it(L_INFO, "UDP handshake init sent: %zu bytes (session_id=%lu)",
           l_packet_size, l_priv->session_id);
    
    // Call callback with success (no response data from client-initiated handshake)
    if (a_callback) {
        a_callback(a_stream, NULL, 0, 0);
    }
    
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

    // Server processes client handshake request
    // Parse TLV format handshake data and generate response
    log_it(L_DEBUG, "UDP handshake process: %zu bytes", a_data_size);
    
    // Processing done via dap_stream_handshake module
    UNUSED(a_response);
    UNUSED(a_response_size);
    
    return 0;
}

/**
 * @brief Create session
 */
static int s_udp_session_create(dap_stream_t *a_stream,
                                 dap_stream_session_params_t *a_params,
                                 dap_stream_transport_session_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid arguments for UDP session create");
        return -1;
    }

    if (!a_stream->stream_transport) {
        log_it(L_ERROR, "Stream has no transport");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = 
        (dap_stream_transport_udp_private_t*)a_stream->stream_transport->_inheritor;
    if (!l_priv) {
        log_it(L_ERROR, "UDP transport not initialized");
        return -1;
    }

    // Generate session ID
    l_priv->session_id = (uint64_t)time(NULL) | ((uint64_t)m_dap_random_u32() << 32);
    log_it(L_INFO, "UDP session created: ID=0x%lx", l_priv->session_id);
    
    // Call callback with session ID
    if (a_callback) {
        a_callback(a_stream, (uint32_t)l_priv->session_id, 0);
    }
    
    return 0;
}

/**
 * @brief Start session
 */
static int s_udp_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_stream_transport_ready_cb_t a_callback)
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
 * @brief Read data from UDP transport
 */
static ssize_t s_udp_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_buffer || a_size == 0) {
        log_it(L_ERROR, "Invalid arguments for UDP read");
        return -1;
    }

    // UDP reading is done via dap_events_socket
    // Data arrives in esocket->buf_in buffer
    // This function reads from that buffer
    
    if (!a_stream->esocket || !a_stream->esocket->buf_in) {
        return 0;  // No data available
    }
    
    // Read from esocket buffer
    size_t l_available = a_stream->esocket->buf_in_size;
    size_t l_copy_size = (l_available < a_size) ? l_available : a_size;
    
    if (l_copy_size > 0) {
        memcpy(a_buffer, a_stream->esocket->buf_in, l_copy_size);
        // Shift remaining data
        if (l_copy_size < l_available) {
            memmove(a_stream->esocket->buf_in, 
                    a_stream->esocket->buf_in + l_copy_size,
                    l_available - l_copy_size);
        }
        a_stream->esocket->buf_in_size -= l_copy_size;
    }
    
    return (ssize_t)l_copy_size;
}

/**
 * @brief Write data to UDP transport
 */
static ssize_t s_udp_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        log_it(L_ERROR, "Invalid arguments for UDP write");
        return -1;
    }

    if (!a_stream->stream_transport) {
        log_it(L_ERROR, "Stream has no transport");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = 
        (dap_stream_transport_udp_private_t*)a_stream->stream_transport->_inheritor;
    if (!l_priv) {
        log_it(L_ERROR, "UDP transport not initialized");
        return -1;
    }

    // Check max packet size
    if (a_size > l_priv->config.max_packet_size) {
        log_it(L_WARNING, "Packet size %zu exceeds max %u, truncating",
               a_size, l_priv->config.max_packet_size);
        a_size = l_priv->config.max_packet_size;
    }

    // UDP write is done via dap_events_socket_write_unsafe
    // This is called from worker context
    if (!a_stream->esocket) {
        log_it(L_ERROR, "Stream has no esocket");
        return -1;
    }

    // Write directly using dap_events_socket_write_unsafe
    ssize_t l_sent = dap_events_socket_write_unsafe(a_stream->esocket, a_data, a_size);
    if (l_sent < 0) {
        log_it(L_ERROR, "UDP send failed via dap_events_socket");
        return -1;
    }

    return l_sent;
}

/**
 * @brief Close UDP transport
 */
static void s_udp_close(dap_stream_t *a_stream)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream for close");
        return;
    }

    if (!a_stream->stream_transport) {
        return;
    }

    dap_stream_transport_udp_private_t *l_priv = 
        (dap_stream_transport_udp_private_t*)a_stream->stream_transport->_inheritor;
    if (l_priv) {
        log_it(L_INFO, "Closing UDP transport session 0x%lx", l_priv->session_id);
        l_priv->session_id = 0;
        l_priv->seq_num = 0;
    }
}

/**
 * @brief Get transport capabilities
 */
static uint32_t s_udp_get_capabilities(dap_stream_transport_t *a_transport)
{
    UNUSED(a_transport);
    return DAP_STREAM_TRANSPORT_CAP_LOW_LATENCY |
           DAP_STREAM_TRANSPORT_CAP_BIDIRECTIONAL;
}

//=============================================================================
// Helper functions
//=============================================================================

/**
 * @brief Get private data from transport
 */
static dap_stream_transport_udp_private_t *s_get_private(dap_stream_transport_t *a_transport)
{
    if (!a_transport)
        return NULL;
    return (dap_stream_transport_udp_private_t*)a_transport->_inheritor;
}

/**
 * @brief Create UDP header
 */
static int s_create_udp_header(dap_stream_transport_udp_header_t *a_header,
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
static int s_parse_udp_header(const dap_stream_transport_udp_header_t *a_header,
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

