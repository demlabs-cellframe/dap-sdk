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

#define LOG_TAG "dap_stream_transport_udp"

// UDP Transport Protocol Version
#define DAP_STREAM_UDP_VERSION 1

// Default configuration values
#define DAP_STREAM_UDP_DEFAULT_MAX_PACKET_SIZE  1400
#define DAP_STREAM_UDP_DEFAULT_KEEPALIVE_MS     30000

// Transport operations forward declarations
static int s_udp_init(dap_stream_transport_t *a_transport, void *a_arg);
static void s_udp_deinit(dap_stream_transport_t *a_transport);
static int s_udp_connect(dap_stream_transport_t *a_transport, const char *a_addr, uint16_t a_port);
static int s_udp_listen(dap_stream_transport_t *a_transport, const char *a_addr, uint16_t a_port);
static int s_udp_accept(dap_stream_transport_t *a_transport, dap_stream_transport_t **a_new_transport);
static int s_udp_handshake_init(dap_stream_transport_t *a_transport,
                                 const dap_stream_handshake_params_t *a_params,
                                 uint8_t **a_out_data, size_t *a_out_size);
static int s_udp_handshake_process(dap_stream_transport_t *a_transport,
                                    const uint8_t *a_data, size_t a_data_size,
                                    uint8_t **a_out_data, size_t *a_out_size);
static int s_udp_session_create(dap_stream_transport_t *a_transport,
                                 const dap_stream_session_params_t *a_params);
static int s_udp_session_start(dap_stream_transport_t *a_transport);
static ssize_t s_udp_read(dap_stream_transport_t *a_transport, void *a_data, size_t a_size);
static ssize_t s_udp_write(dap_stream_transport_t *a_transport, const void *a_data, size_t a_size);
static int s_udp_close(dap_stream_transport_t *a_transport);
static uint32_t s_udp_get_capabilities(dap_stream_transport_t *a_transport);

// UDP transport operations table
static const dap_stream_transport_ops s_udp_ops = {
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
    dap_stream_transport_t *l_transport = DAP_NEW_Z(dap_stream_transport_t);
    if (!l_transport) {
        log_it(L_CRITICAL, "Memory allocation failed for UDP transport");
        return -1;
    }

    l_transport->type = DAP_STREAM_TRANSPORT_TYPE_UDP;
    l_transport->name = dap_strdup("udp");
    l_transport->ops = &s_udp_ops;
    l_transport->internal = NULL;  // Will be allocated during init

    int l_ret = dap_stream_transport_register(l_transport);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register UDP transport: %d", l_ret);
        DAP_DELETE(l_transport->name);
        DAP_DELETE(l_transport);
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
    dap_stream_transport_t *l_transport = dap_stream_transport_find(DAP_STREAM_TRANSPORT_TYPE_UDP);
    if (!l_transport) {
        log_it(L_WARNING, "UDP transport not found for unregistration");
        return -1;
    }

    int l_ret = dap_stream_transport_unregister(l_transport);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to unregister UDP transport: %d", l_ret);
        return l_ret;
    }

    DAP_DELETE(l_transport->name);
    DAP_DELETE(l_transport);
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
    return a_stream->stream_transport->type == DAP_STREAM_TRANSPORT_TYPE_UDP;
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
static int s_udp_init(dap_stream_transport_t *a_transport, void *a_arg)
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
    
    // a_arg can be dap_server_t* or dap_events_socket_t* depending on context
    if (a_arg) {
        // Try to determine if it's a server or socket
        // For now, store as server and get esocket later from stream
        l_priv->server = (dap_server_t*)a_arg;
        l_priv->esocket = NULL;  // Will be set during connect/listen
    } else {
        l_priv->server = NULL;
        l_priv->esocket = NULL;
    }
    
    l_priv->remote_addr_len = 0;
    l_priv->user_data = NULL;

    a_transport->internal = l_priv;
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
        a_transport->internal = NULL;
        log_it(L_DEBUG, "UDP transport deinitialized");
    }
}

/**
 * @brief Connect to remote UDP endpoint
 */
static int s_udp_connect(dap_stream_transport_t *a_transport, const char *a_addr, uint16_t a_port)
{
    if (!a_transport || !a_addr) {
        log_it(L_ERROR, "Invalid arguments for UDP connect");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv) {
        log_it(L_ERROR, "UDP transport not initialized");
        return -1;
    }

    // Parse address and store in remote_addr
    struct sockaddr_in *l_addr_in = (struct sockaddr_in*)&l_priv->remote_addr;
    l_addr_in->sin_family = AF_INET;
    l_addr_in->sin_port = htons(a_port);
    
    if (inet_pton(AF_INET, a_addr, &l_addr_in->sin_addr) != 1) {
        log_it(L_ERROR, "Invalid IPv4 address: %s", a_addr);
        return -1;
    }

    l_priv->remote_addr_len = sizeof(struct sockaddr_in);
    log_it(L_INFO, "UDP transport connected to %s:%u", a_addr, a_port);
    return 0;
}

/**
 * @brief Start listening for UDP connections
 */
static int s_udp_listen(dap_stream_transport_t *a_transport, const char *a_addr, uint16_t a_port)
{
    if (!a_transport) {
        log_it(L_ERROR, "Invalid arguments for UDP listen");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv) {
        log_it(L_ERROR, "UDP transport not initialized");
        return -1;
    }

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
static int s_udp_accept(dap_stream_transport_t *a_transport, dap_stream_transport_t **a_new_transport)
{
    // UDP is connectionless, so "accept" is a no-op
    // Each datagram can be from a different peer
    if (a_new_transport)
        *a_new_transport = a_transport;
    return 0;
}

/**
 * @brief Initialize encryption handshake
 */
static int s_udp_handshake_init(dap_stream_transport_t *a_transport,
                                 const dap_stream_handshake_params_t *a_params,
                                 uint8_t **a_out_data, size_t *a_out_size)
{
    if (!a_transport || !a_params || !a_out_data || !a_out_size) {
        log_it(L_ERROR, "Invalid arguments for UDP handshake init");
        return -1;
    }

    // Create DSHP handshake request
    dap_stream_handshake_request_t *l_request = 
        dap_stream_handshake_request_create(a_params);
    if (!l_request) {
        log_it(L_ERROR, "Failed to create handshake request");
        return -1;
    }

    // Wrap in UDP packet
    size_t l_total_size = sizeof(dap_stream_transport_udp_header_t) + l_request->data_size;
    uint8_t *l_packet = DAP_NEW_Z_SIZE(uint8_t, l_total_size);
    if (!l_packet) {
        dap_stream_handshake_request_free(l_request);
        return -1;
    }

    dap_stream_transport_udp_header_t *l_header = (dap_stream_transport_udp_header_t*)l_packet;
    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
    
    s_create_udp_header(l_header, DAP_STREAM_UDP_PKT_HANDSHAKE, 
                        l_request->data_size, l_priv->seq_num++, 0);
    
    memcpy(l_packet + sizeof(dap_stream_transport_udp_header_t), 
           l_request->data, l_request->data_size);

    *a_out_data = l_packet;
    *a_out_size = l_total_size;

    dap_stream_handshake_request_free(l_request);
    log_it(L_DEBUG, "UDP handshake init prepared: %zu bytes", l_total_size);
    return 0;
}

/**
 * @brief Process incoming handshake data
 */
static int s_udp_handshake_process(dap_stream_transport_t *a_transport,
                                    const uint8_t *a_data, size_t a_data_size,
                                    uint8_t **a_out_data, size_t *a_out_size)
{
    if (!a_transport || !a_data || a_data_size < sizeof(dap_stream_transport_udp_header_t)) {
        log_it(L_ERROR, "Invalid arguments for UDP handshake process");
        return -1;
    }

    // Parse UDP header
    const dap_stream_transport_udp_header_t *l_header = 
        (const dap_stream_transport_udp_header_t*)a_data;
    
    uint8_t l_type;
    uint16_t l_length;
    uint32_t l_seq_num;
    uint64_t l_session_id;
    
    if (s_parse_udp_header(l_header, &l_type, &l_length, &l_seq_num, &l_session_id) != 0) {
        log_it(L_ERROR, "Failed to parse UDP header");
        return -1;
    }

    if (l_type != DAP_STREAM_UDP_PKT_HANDSHAKE) {
        log_it(L_ERROR, "Expected handshake packet, got type %u", l_type);
        return -1;
    }

    // Extract handshake data
    const uint8_t *l_handshake_data = a_data + sizeof(dap_stream_transport_udp_header_t);
    size_t l_handshake_size = a_data_size - sizeof(dap_stream_transport_udp_header_t);

    // Parse DSHP handshake (response or request)
    // This would involve parsing TLV and generating response
    // For now, echo back as placeholder
    
    *a_out_data = DAP_DUP_SIZE(l_handshake_data, l_handshake_size);
    *a_out_size = l_handshake_size;

    log_it(L_DEBUG, "UDP handshake processed: %zu bytes", l_handshake_size);
    return 0;
}

/**
 * @brief Create session
 */
static int s_udp_session_create(dap_stream_transport_t *a_transport,
                                 const dap_stream_session_params_t *a_params)
{
    if (!a_transport || !a_params) {
        log_it(L_ERROR, "Invalid arguments for UDP session create");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv) {
        log_it(L_ERROR, "UDP transport not initialized");
        return -1;
    }

    // Generate session ID
    l_priv->session_id = (uint64_t)time(NULL) | ((uint64_t)rand() << 32);
    log_it(L_INFO, "UDP session created: ID=0x%lx", l_priv->session_id);
    return 0;
}

/**
 * @brief Start session
 */
static int s_udp_session_start(dap_stream_transport_t *a_transport)
{
    if (!a_transport) {
        log_it(L_ERROR, "Invalid transport for session start");
        return -1;
    }

    log_it(L_DEBUG, "UDP session started");
    return 0;
}

/**
 * @brief Read data from UDP transport
 */
static ssize_t s_udp_read(dap_stream_transport_t *a_transport, void *a_data, size_t a_size)
{
    if (!a_transport || !a_data || a_size == 0) {
        log_it(L_ERROR, "Invalid arguments for UDP read");
        return -1;
    }

    // Receive UDP packet and strip header
    uint8_t l_buffer[65536];
    ssize_t l_received = dap_stream_transport_udp_recv_raw(a_transport, l_buffer, sizeof(l_buffer));
    
    if (l_received <= (ssize_t)sizeof(dap_stream_transport_udp_header_t)) {
        return 0;  // No data or header only
    }

    // Parse header
    const dap_stream_transport_udp_header_t *l_header = 
        (const dap_stream_transport_udp_header_t*)l_buffer;
    
    uint8_t l_type;
    uint16_t l_length;
    uint32_t l_seq_num;
    uint64_t l_session_id;
    
    if (s_parse_udp_header(l_header, &l_type, &l_length, &l_seq_num, &l_session_id) != 0) {
        log_it(L_ERROR, "Failed to parse UDP header on read");
        return -1;
    }

    // Copy payload
    size_t l_payload_size = l_received - sizeof(dap_stream_transport_udp_header_t);
    size_t l_copy_size = (l_payload_size < a_size) ? l_payload_size : a_size;
    
    memcpy(a_data, l_buffer + sizeof(dap_stream_transport_udp_header_t), l_copy_size);
    return l_copy_size;
}

/**
 * @brief Write data to UDP transport
 */
static ssize_t s_udp_write(dap_stream_transport_t *a_transport, const void *a_data, size_t a_size)
{
    if (!a_transport || !a_data || a_size == 0) {
        log_it(L_ERROR, "Invalid arguments for UDP write");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
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

    // Create packet with header
    size_t l_total_size = sizeof(dap_stream_transport_udp_header_t) + a_size;
    uint8_t *l_packet = DAP_NEW_Z_SIZE(uint8_t, l_total_size);
    if (!l_packet) {
        log_it(L_CRITICAL, "Memory allocation failed for UDP packet");
        return -1;
    }

    dap_stream_transport_udp_header_t *l_header = (dap_stream_transport_udp_header_t*)l_packet;
    s_create_udp_header(l_header, DAP_STREAM_UDP_PKT_DATA, 
                        a_size, l_priv->seq_num++, l_priv->session_id);
    
    memcpy(l_packet + sizeof(dap_stream_transport_udp_header_t), a_data, a_size);

    // Send packet
    ssize_t l_sent = dap_stream_transport_udp_send_raw(a_transport, l_packet, l_total_size);
    DAP_DELETE(l_packet);

    if (l_sent < 0) {
        log_it(L_ERROR, "UDP send failed");
        return -1;
    }

    return a_size;  // Return payload size, not total packet size
}

/**
 * @brief Close UDP transport
 */
static int s_udp_close(dap_stream_transport_t *a_transport)
{
    if (!a_transport) {
        log_it(L_ERROR, "Invalid transport for close");
        return -1;
    }

    dap_stream_transport_udp_private_t *l_priv = s_get_private(a_transport);
    if (l_priv) {
        log_it(L_INFO, "Closing UDP transport session 0x%lx", l_priv->session_id);
        l_priv->session_id = 0;
        l_priv->seq_num = 0;
    }

    return 0;
}

/**
 * @brief Get transport capabilities
 */
static uint32_t s_udp_get_capabilities(dap_stream_transport_t *a_transport)
{
    UNUSED(a_transport);
    return DAP_STREAM_TRANSPORT_CAP_DATAGRAM |
           DAP_STREAM_TRANSPORT_CAP_LOW_LATENCY;
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
    return (dap_stream_transport_udp_private_t*)a_transport->internal;
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

