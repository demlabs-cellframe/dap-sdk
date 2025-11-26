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
#include "dap_net_transport_dns_stream.h"
#include "dap_net_transport_dns_server.h"
#include "dap_net_transport.h"  // For dap_net_transport_t and dap_net_transport_ops_t
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
#include "dap_stream_handshake.h"  // For handshake structures
#include "dap_stream.h"
#include "dap_server.h"
#include "dap_enc_server.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_stream_transport_dns"

// DNS Transport Protocol Version
#define DAP_STREAM_DNS_VERSION 1

// Default configuration values
#define DAP_STREAM_DNS_DEFAULT_MAX_RECORD_SIZE  255
#define DAP_STREAM_DNS_DEFAULT_MAX_QUERY_SIZE   512
#define DAP_STREAM_DNS_DEFAULT_TIMEOUT_MS       5000

// Transport operations forward declarations
static int s_dns_init(dap_net_transport_t *a_transport, dap_config_t *a_config);
static void s_dns_deinit(dap_net_transport_t *a_transport);
static int s_dns_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port, 
                          dap_net_transport_connect_cb_t a_callback);
static int s_dns_listen(dap_net_transport_t *a_transport, const char *a_addr, uint16_t a_port,
                         dap_server_t *a_server);
static int s_dns_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out);
static int s_dns_handshake_init(dap_stream_t *a_stream,
                                 dap_net_handshake_params_t *a_params,
                                 dap_net_transport_handshake_cb_t a_callback);
static int s_dns_handshake_process(dap_stream_t *a_stream,
                                    const void *a_data, size_t a_data_size,
                                    void **a_response, size_t *a_response_size);
static int s_dns_session_create(dap_stream_t *a_stream,
                                 dap_net_session_params_t *a_params,
                                 dap_net_transport_session_cb_t a_callback);
static int s_dns_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_net_transport_ready_cb_t a_callback);
static ssize_t s_dns_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size);
static ssize_t s_dns_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static void s_dns_close(dap_stream_t *a_stream);
static uint32_t s_dns_get_capabilities(dap_net_transport_t *a_transport);
static int s_dns_stage_prepare(dap_net_transport_t *a_transport,
                               const dap_net_stage_prepare_params_t *a_params,
                               dap_net_stage_prepare_result_t *a_result);

// DNS transport operations table
static const dap_net_transport_ops_t s_dns_ops = {
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
    .register_server_handlers = NULL,  // DNS transport registers handlers via DNS server implementation
    .stage_prepare = s_dns_stage_prepare
};

// Helper functions
static dap_stream_transport_dns_private_t *s_get_private(dap_net_transport_t *a_transport);

/**
 * @brief Register DNS tunnel transport adapter
 */
int dap_net_transport_dns_stream_register(void)
{
    // Initialize DNS server module first (registers server operations)
    int l_ret = dap_net_transport_dns_server_init();
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize DNS server module: %d", l_ret);
        return l_ret;
    }

    log_it(L_DEBUG, "dap_net_transport_dns_stream_register: DNS server module initialized, registering transport");
    
    // Register DNS transport operations
    int l_ret_transport = dap_net_transport_register("DNS_TUNNEL",
                                                DAP_NET_TRANSPORT_DNS_TUNNEL,
                                                &s_dns_ops,
                                                DAP_NET_TRANSPORT_SOCKET_UDP,
                                                NULL);
    if (l_ret_transport != 0) {
        log_it(L_ERROR, "Failed to register DNS tunnel transport: %d", l_ret_transport);
        dap_net_transport_dns_server_deinit();
        return l_ret_transport;
    }

    log_it(L_NOTICE, "DNS tunnel transport registered successfully");
    return 0;
}

/**
 * @brief Unregister DNS tunnel transport adapter
 */
int dap_net_transport_dns_stream_unregister(void)
{
    int l_ret = dap_net_transport_unregister(DAP_NET_TRANSPORT_DNS_TUNNEL);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to unregister DNS tunnel transport: %d", l_ret);
        return l_ret;
    }

    // Deinitialize DNS server module
    dap_net_transport_dns_server_deinit();

    log_it(L_NOTICE, "DNS tunnel transport unregistered successfully");
    return 0;
}

/**
 * @brief Create default DNS tunnel configuration
 */
dap_stream_transport_dns_config_t dap_stream_transport_dns_config_default(void)
{
    dap_stream_transport_dns_config_t l_config = {
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
int dap_stream_transport_dns_set_config(dap_net_transport_t *a_transport,
                                        const dap_stream_transport_dns_config_t *a_config)
{
    if (!a_transport || !a_config) {
        log_it(L_ERROR, "Invalid parameters for DNS tunnel config");
        return -1;
    }

    dap_stream_transport_dns_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv) {
        log_it(L_ERROR, "DNS tunnel transport not initialized");
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
int dap_stream_transport_dns_get_config(dap_net_transport_t *a_transport,
                                         dap_stream_transport_dns_config_t *a_config)
{
    if (!a_transport || !a_config) {
        log_it(L_ERROR, "Invalid parameters for DNS tunnel config get");
        return -1;
    }

    dap_stream_transport_dns_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv) {
        log_it(L_ERROR, "DNS tunnel transport not initialized");
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
 * @brief Check if stream is using DNS tunnel transport
 */
bool dap_stream_transport_is_dns(const dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->stream_transport) {
        return false;
    }
    return a_stream->stream_transport->type == DAP_NET_TRANSPORT_DNS_TUNNEL;
}

/**
 * @brief Get DNS tunnel private data from stream
 */
dap_stream_transport_dns_private_t* dap_stream_transport_dns_get_private(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->stream_transport) {
        return NULL;
    }

    if (a_stream->stream_transport->type != DAP_NET_TRANSPORT_DNS_TUNNEL) {
        return NULL;
    }

    return (dap_stream_transport_dns_private_t*)a_stream->stream_transport->_inheritor;
}

// ============================================================================
// Transport Operations Implementation (Stubs)
// ============================================================================
// Note: Full DNS tunnel implementation requires DNS query/response parsing,
//       TXT record encoding/decoding, chunking, etc. These are stubs for now.

/**
 * @brief Initialize DNS tunnel transport
 */
static int s_dns_init(dap_net_transport_t *a_transport, dap_config_t *a_config)
{
    UNUSED(a_config);
    
    if (!a_transport) {
        log_it(L_ERROR, "Invalid transport parameter");
        return -1;
    }

    // Allocate private data
    dap_stream_transport_dns_private_t *l_priv = DAP_NEW_Z(dap_stream_transport_dns_private_t);
    if (!l_priv) {
        log_it(L_CRITICAL, "Cannot allocate memory for DNS tunnel transport");
        return -2;
    }

    // Set default configuration
    l_priv->config = dap_stream_transport_dns_config_default();
    
    // Set as inheritor
    a_transport->_inheritor = l_priv;

    // DNS transport doesn't support session control (connectionless)
    a_transport->has_session_control = false;

    log_it(L_INFO, "DNS tunnel transport initialized");
    return 0;
}

/**
 * @brief Deinitialize DNS tunnel transport
 */
static void s_dns_deinit(dap_net_transport_t *a_transport)
{
    if (!a_transport) {
        return;
    }

    dap_stream_transport_dns_private_t *l_priv = s_get_private(a_transport);
    if (l_priv) {
        DAP_DELETE(l_priv->config.domain_suffix);
        DAP_DELETE(l_priv);
        a_transport->_inheritor = NULL;
    }

    log_it(L_INFO, "DNS tunnel transport deinitialized");
}

/**
 * @brief Connect DNS tunnel transport
 * @note Uses UDP-like connectionless approach for basic functionality
 *       Full DNS query/response parsing can be added later
 */
static int s_dns_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port, 
                          dap_net_transport_connect_cb_t a_callback)
{
    if (!a_stream || !a_host) {
        log_it(L_ERROR, "Invalid arguments for DNS connect");
        return -1;
    }

    if (!a_stream->stream_transport) {
        log_it(L_ERROR, "Stream has no transport");
        return -1;
    }

    dap_stream_transport_dns_private_t *l_priv = s_get_private(a_stream->stream_transport);
    if (!l_priv) {
        log_it(L_ERROR, "DNS transport not initialized");
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
    l_priv->esocket = a_stream->esocket;  // Store esocket from stream
    
    // Update esocket address storage for sendto
    if (l_priv->esocket) {
        memcpy(&l_priv->esocket->addr_storage, &l_priv->remote_addr, l_priv->remote_addr_len);
        l_priv->esocket->addr_size = l_priv->remote_addr_len;
    }
    
    log_it(L_INFO, "DNS tunnel transport connecting to %s:%u", a_host, a_port);
    
    // Call callback immediately (DNS is connectionless)
    if (a_callback) {
        a_callback(a_stream, 0);
    }
    
    return 0;
}

/**
 * @brief Start listening for DNS tunnel connections
 */
static int s_dns_listen(dap_net_transport_t *a_transport, const char *a_addr, uint16_t a_port,
                         dap_server_t *a_server)
{
    if (!a_transport) {
        log_it(L_ERROR, "Invalid arguments for DNS tunnel listen");
        return -1;
    }

    // DNS listening is handled by dap_net_transport_dns_server
    // This function is called from the transport layer
    log_it(L_INFO, "DNS tunnel transport listening on %s:%u (via dap_net_transport_dns_server)", 
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
    log_it(L_DEBUG, "DNS tunnel transport accept");
    return 0;
}

/**
 * @brief Initialize encryption handshake
 * @note Uses UDP-like approach for basic functionality
 *       Full DNS-based handshake can be added later
 */
static int s_dns_handshake_init(dap_stream_t *a_stream,
                                 dap_net_handshake_params_t *a_params,
                                 dap_net_transport_handshake_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid arguments for DNS handshake init");
        return -1;
    }

    if (!a_stream->stream_transport) {
        log_it(L_ERROR, "Stream has no transport");
        return -1;
    }

    log_it(L_INFO, "DNS handshake init: enc_type=%d, pkey_type=%d",
           a_params->enc_type, a_params->pkey_exchange_type);
    
    // DNS handshake uses UDP-like approach for basic functionality
    // Full DNS query/response handshake can be implemented later
    // For now, use transport-independent encryption server
    
    // Build handshake request using dap_enc_server API
    dap_enc_server_request_t l_enc_request = {
        .enc_type = a_params->enc_type,
        .pkey_exchange_type = a_params->pkey_exchange_type,
        .pkey_exchange_size = a_params->pkey_exchange_size,
        .block_key_size = a_params->block_key_size,
        .protocol_version = a_params->protocol_version,
        .sign_count = 0,
        .alice_msg = a_params->alice_pub_key,
        .alice_msg_size = a_params->alice_pub_key_size,
        .sign_hashes = NULL,
        .sign_hashes_count = 0
    };
    
    // Process handshake via transport-independent encryption server
    dap_enc_server_response_t *l_enc_response = NULL;
    int l_ret = dap_enc_server_process_request(&l_enc_request, &l_enc_response);
    
    if (l_ret != 0 || !l_enc_response || !l_enc_response->success) {
        log_it(L_ERROR, "DNS handshake init failed: %s",
               l_enc_response && l_enc_response->error_message ? 
               l_enc_response->error_message : "unknown error");
        if (l_enc_response)
            dap_enc_server_response_free(l_enc_response);
        return -1;
    }
    
    // Send handshake data via esocket (similar to UDP)
    if (a_stream->esocket && l_enc_response->encrypt_msg_len > 0) {
        size_t l_sent = dap_events_socket_write_unsafe(a_stream->esocket, 
                                                       l_enc_response->encrypt_msg,
                                                       l_enc_response->encrypt_msg_len);
        if (l_sent != l_enc_response->encrypt_msg_len) {
            log_it(L_ERROR, "DNS handshake send incomplete: %zu of %zu bytes", 
                   l_sent, l_enc_response->encrypt_msg_len);
            dap_enc_server_response_free(l_enc_response);
            return -1;
        }
    }
    
    dap_enc_server_response_free(l_enc_response);
    
    log_it(L_INFO, "DNS handshake init completed");
    
    // Call callback with success
    if (a_callback) {
        a_callback(a_stream, NULL, 0, 0);
    }
    
    return 0;
}

/**
 * @brief Process handshake response
 * @note Uses UDP-like approach for basic functionality
 */
static int s_dns_handshake_process(dap_stream_t *a_stream,
                                    const void *a_data, size_t a_data_size,
                                    void **a_response, size_t *a_response_size)
{
    if (!a_stream || !a_data || a_data_size == 0) {
        log_it(L_ERROR, "Invalid arguments for DNS handshake process");
        return -1;
    }

    // DNS handshake processing uses UDP-like approach
    // Full DNS response parsing can be added later
    log_it(L_DEBUG, "DNS handshake process: %zu bytes", a_data_size);
    
    // Processing done via dap_stream_handshake module
    UNUSED(a_response);
    UNUSED(a_response_size);
    
    return 0;
}

/**
 * @brief Create session
 * @note Uses UDP-like approach for basic functionality
 */
static int s_dns_session_create(dap_stream_t *a_stream,
                                 dap_net_session_params_t *a_params,
                                 dap_net_transport_session_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid arguments for DNS session create");
        return -1;
    }

    if (!a_stream->stream_transport) {
        log_it(L_ERROR, "Stream has no transport");
        return -1;
    }

    // Generate session ID (similar to UDP)
    uint64_t l_session_id = (uint64_t)time(NULL) | ((uint64_t)m_dap_random_u32() << 32);
    log_it(L_INFO, "DNS session created: ID=0x%lx", l_session_id);
    
    // Call callback with session ID (no full response data for DNS transport)
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
                                dap_net_transport_ready_cb_t a_callback)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream for DNS session start");
        return -1;
    }

    log_it(L_DEBUG, "DNS session start: session_id=%u", a_session_id);
    
    // Call callback immediately (DNS session ready, similar to UDP)
    if (a_callback) {
        a_callback(a_stream, 0);
    }
    
    return 0;
}

/**
 * @brief Read data from DNS tunnel
 * @note Uses UDP-like approach: reads from esocket buffer
 *       Full DNS response parsing can be added later
 */
static ssize_t s_dns_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_buffer || a_size == 0) {
        log_it(L_ERROR, "Invalid arguments for DNS read");
        return -1;
    }

    // DNS reading is done via dap_events_socket (similar to UDP)
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
 * @brief Write data to DNS tunnel
 * @note Uses UDP-like approach: writes directly to esocket
 *       Full DNS query generation with encoding can be added later
 */
static ssize_t s_dns_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        log_it(L_ERROR, "Invalid arguments for DNS write");
        return -1;
    }

    if (!a_stream->esocket) {
        log_it(L_ERROR, "Stream has no esocket");
        return -1;
    }

    // DNS writing is done via dap_events_socket (similar to UDP)
    // Full DNS query generation with encoding can be added later
    size_t l_sent = dap_events_socket_write_unsafe(a_stream->esocket, a_data, a_size);
    
    if (l_sent != a_size) {
        log_it(L_WARNING, "DNS write incomplete: %zu of %zu bytes", l_sent, a_size);
    }
    
    return (ssize_t)l_sent;
}

/**
 * @brief Close DNS tunnel connection
 */
static void s_dns_close(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->stream_transport) {
        return;
    }

    log_it(L_DEBUG, "DNS tunnel transport close");
    // TODO: Implement DNS tunnel cleanup
}

/**
 * @brief Prepare DNS socket for client stage
 * 
 * Fully prepares esocket: creates, sets callbacks, and adds to worker.
 * DNS tunneling uses UDP (connectionless), so no connection step is needed.
 * Transport is responsible for complete esocket lifecycle management.
 */
static int s_dns_stage_prepare(dap_net_transport_t *a_transport,
                               const dap_net_stage_prepare_params_t *a_params,
                               dap_net_stage_prepare_result_t *a_result)
{
    if (!a_transport || !a_params || !a_result) {
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
    a_result->error_code = 0;
    
    // DNS tunneling uses UDP socket - create using platform-independent function
    dap_events_socket_t *l_es = dap_events_socket_create_platform(PF_INET, SOCK_DGRAM, IPPROTO_UDP, a_params->callbacks);
    if (!l_es) {
        log_it(L_ERROR, "Failed to create DNS socket");
        a_result->error_code = -1;
        return -1;
    }
    
    l_es->type = DESCRIPTOR_TYPE_SOCKET_UDP;
    l_es->_inheritor = a_params->client_context;
    
    // Resolve host and set address using centralized function
    if (dap_events_socket_resolve_and_set_addr(l_es, a_params->host, a_params->port) < 0) {
        log_it(L_ERROR, "Failed to resolve address for DNS transport");
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    // DNS tunneling uses UDP (connectionless) - just add to worker
    dap_worker_add_events_socket(a_params->worker, l_es);
    
    a_result->esocket = l_es;
    a_result->error_code = 0;
    log_it(L_DEBUG, "DNS socket prepared and added to worker for %s:%u", a_params->host, a_params->port);
    return 0;
}

/**
 * @brief Get DNS tunnel transport capabilities
 */
static uint32_t s_dns_get_capabilities(dap_net_transport_t *a_transport)
{
    UNUSED(a_transport);
    
    // DNS tunnel characteristics:
    // - Connectionless (like UDP)
    // - Low latency (no connection establishment)
    // - Built-in obfuscation (looks like DNS)
    // - No reliability guarantees
    // - Limited payload size (DNS TXT records)
    return DAP_NET_TRANSPORT_CAP_OBFUSCATION |
           DAP_NET_TRANSPORT_CAP_LOW_LATENCY;
}

/**
 * @brief Get private data from transport
 */
static dap_stream_transport_dns_private_t *s_get_private(dap_net_transport_t *a_transport)
{
    if (!a_transport || !a_transport->_inheritor) {
        return NULL;
    }
    
    return (dap_stream_transport_dns_private_t*)a_transport->_inheritor;
}

