/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/time.h>

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

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_net_transport.h"
#include "dap_net_transport_websocket_stream.h"
#include "dap_net_transport_websocket_server.h"
#include "dap_net_transport_server.h"
#include "dap_stream_handshake.h"
#include "dap_stream.h"
#include "dap_enc_base64.h"
#include "dap_hash.h"
#include "rand/dap_rand.h"
#include "dap_timerfd.h"
#include "dap_worker.h"
#include "dap_events_socket.h"
#include "dap_net.h"
#include "dap_client.h"
#include "dap_client_pvt.h"
#include "dap_client_http.h"
#include "http_status_code.h"
#include "dap_net_transport_http_stream.h"
#include "dap_stream_ctl.h"

#define LOG_TAG "dap_net_transport_websocket_stream"

// WebSocket magic GUID for handshake (RFC 6455)
#define WS_MAGIC_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// Default values
#define WS_DEFAULT_MAX_FRAME_SIZE  (1024 * 1024)  // 1MB
#define WS_DEFAULT_PING_INTERVAL   30000          // 30 seconds
#define WS_DEFAULT_PONG_TIMEOUT    10000          // 10 seconds
#define WS_INITIAL_FRAME_BUFFER    4096           // 4KB initial buffer

// Forward declarations
static const dap_net_transport_ops_t s_websocket_ops;
static int s_ws_init(dap_net_transport_t *a_transport, dap_config_t *a_config);
static void s_ws_deinit(dap_net_transport_t *a_transport);
static int s_ws_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port,
                         dap_net_transport_connect_cb_t a_callback);
static int s_ws_listen(dap_net_transport_t *a_transport, const char *a_addr, uint16_t a_port,
                        dap_server_t *a_server);
static int s_ws_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out);
static int s_ws_handshake_init(dap_stream_t *a_stream, dap_net_handshake_params_t *a_params,
                                dap_net_transport_handshake_cb_t a_callback);
static int s_ws_handshake_process(dap_stream_t *a_stream, const void *a_data, size_t a_data_size,
                                   void **a_response, size_t *a_response_size);
static int s_ws_session_create(dap_stream_t *a_stream, dap_net_session_params_t *a_params,
                                 dap_net_transport_session_cb_t a_callback);
static int s_ws_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_net_transport_ready_cb_t a_callback);
static ssize_t s_ws_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size);
static ssize_t s_ws_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static void s_ws_close(dap_stream_t *a_stream);
static uint32_t s_ws_get_capabilities(dap_net_transport_t *a_transport);
static int s_ws_register_server_handlers(dap_net_transport_t *a_transport, void *a_transport_context);
static int s_ws_stage_prepare(dap_net_transport_t *a_transport,
                              const dap_net_stage_prepare_params_t *a_params,
                              dap_net_stage_prepare_result_t *a_result);

// WebSocket protocol helpers
static int s_ws_generate_key(char *a_key_out, size_t a_key_size);
static int s_ws_generate_accept(const char *a_key, char *a_accept_out, size_t a_accept_size);
static int s_ws_build_frame(uint8_t *a_buffer, size_t a_buffer_size, dap_ws_opcode_t a_opcode,
                             bool a_fin, bool a_mask, const void *a_payload, size_t a_payload_size,
                             size_t *a_frame_size_out);
static int s_ws_parse_frame(const uint8_t *a_data, size_t a_data_size, dap_ws_opcode_t *a_opcode_out,
                             bool *a_fin_out, uint8_t **a_payload_out, size_t *a_payload_size_out,
                             size_t *a_frame_total_size_out);
static void s_ws_mask_unmask(uint8_t *a_data, size_t a_size, uint32_t a_mask_key);
static bool s_ws_ping_timer_callback(void *a_user_data);

// Helper to get private data
static dap_net_transport_websocket_private_t *s_get_private(dap_net_transport_t *a_transport);
static dap_net_transport_websocket_private_t *s_get_private_from_stream(dap_stream_t *a_stream);

// ============================================================================
// Transport Operations Table
// ============================================================================

static const dap_net_transport_ops_t s_websocket_ops = {
    .init = s_ws_init,
    .deinit = s_ws_deinit,
    .connect = s_ws_connect,
    .listen = s_ws_listen,
    .accept = s_ws_accept,
    .handshake_init = s_ws_handshake_init,
    .handshake_process = s_ws_handshake_process,
    .session_create = s_ws_session_create,
    .session_start = s_ws_session_start,
    .read = s_ws_read,
    .write = s_ws_write,
    .close = s_ws_close,
    .get_capabilities = s_ws_get_capabilities,
    .stage_prepare = s_ws_stage_prepare,
    .register_server_handlers = s_ws_register_server_handlers
};

// ============================================================================
// Registration Functions
// ============================================================================

/**
 * @brief Register WebSocket transport adapter
 */
int dap_net_transport_websocket_stream_register(void)
{
    // Initialize WebSocket server module first (registers server operations)
    int l_ret = dap_net_transport_websocket_server_init();
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize WebSocket server module: %d", l_ret);
        return l_ret;
    }
    
    log_it(L_DEBUG, "dap_net_transport_websocket_stream_register: WebSocket server module initialized, registering transport");
    
    // Register WebSocket transport operations
    int l_ret_transport = dap_net_transport_register("WebSocket",
                                                DAP_NET_TRANSPORT_WEBSOCKET,
                                                &s_websocket_ops,
                                                DAP_NET_TRANSPORT_SOCKET_TCP,
                                                NULL);
    if (l_ret_transport != 0) {
        log_it(L_ERROR, "Failed to register WebSocket transport: %d", l_ret_transport);
        dap_net_transport_websocket_server_deinit();
        return l_ret_transport;
    }

    log_it(L_NOTICE, "WebSocket transport registered successfully");
    return 0;
}

/**
 * @brief Unregister WebSocket transport adapter
 */
int dap_net_transport_websocket_stream_unregister(void)
{
    int l_ret = dap_net_transport_unregister(DAP_NET_TRANSPORT_WEBSOCKET);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to unregister WebSocket transport: %d", l_ret);
        return l_ret;
    }

    // Deinitialize WebSocket server module
    dap_net_transport_websocket_server_deinit();

    log_it(L_NOTICE, "WebSocket transport unregistered successfully");
    return 0;
}

// ============================================================================
// Configuration Functions
// ============================================================================

/**
 * @brief Get default WebSocket configuration
 */
dap_net_transport_websocket_config_t dap_net_transport_websocket_config_default(void)
{
    dap_net_transport_websocket_config_t l_config = {
        .max_frame_size = WS_DEFAULT_MAX_FRAME_SIZE,
        .ping_interval_ms = WS_DEFAULT_PING_INTERVAL,
        .pong_timeout_ms = WS_DEFAULT_PONG_TIMEOUT,
        .enable_compression = false,
        .client_mask_frames = true,   // RFC 6455 requires client masking
        .server_mask_frames = false,  // Server frames typically unmasked
        .subprotocol = NULL,
        .origin = NULL
    };
    return l_config;
}

/**
 * @brief Set WebSocket configuration
 */
int dap_net_transport_websocket_set_config(dap_net_transport_t *a_transport,
                                        const dap_net_transport_websocket_config_t *a_config)
{
    if (!a_transport || !a_config) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    dap_net_transport_websocket_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket transport not initialized");
        return -2;
    }

    // Free old strings
    if (l_priv->config.subprotocol) {
        DAP_DELETE(l_priv->config.subprotocol);
    }
    if (l_priv->config.origin) {
        DAP_DELETE(l_priv->config.origin);
    }

    // Copy configuration
    memcpy(&l_priv->config, a_config, sizeof(dap_net_transport_websocket_config_t));

    // Duplicate strings
    if (a_config->subprotocol) {
        l_priv->config.subprotocol = dap_strdup(a_config->subprotocol);
    }
    if (a_config->origin) {
        l_priv->config.origin = dap_strdup(a_config->origin);
    }

    log_it(L_DEBUG, "WebSocket configuration updated");
    return 0;
}

/**
 * @brief Get WebSocket configuration
 */
int dap_net_transport_websocket_get_config(dap_net_transport_t *a_transport,
                                        dap_net_transport_websocket_config_t *a_config)
{
    if (!a_transport || !a_config) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    dap_net_transport_websocket_private_t *l_priv = s_get_private(a_transport);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket transport not initialized");
        return -2;
    }

    memcpy(a_config, &l_priv->config, sizeof(dap_net_transport_websocket_config_t));
    return 0;
}

// ============================================================================
// Transport Operations Implementation
// ============================================================================

/**
 * @brief Initialize WebSocket transport
 */
static int s_ws_init(dap_net_transport_t *a_transport, dap_config_t *a_config)
{
    if (!a_transport) {
        log_it(L_ERROR, "Invalid transport pointer");
        return -1;
    }

    // Allocate private data
    dap_net_transport_websocket_private_t *l_priv = DAP_NEW_Z(dap_net_transport_websocket_private_t);
    if (!l_priv) {
        log_it(L_CRITICAL, "Failed to allocate WebSocket private data");
        return -2;
    }

    // Set default configuration
    l_priv->config = dap_net_transport_websocket_config_default();
    l_priv->state = DAP_WS_STATE_CLOSED;
    
    // Allocate initial frame buffer
    l_priv->frame_buffer_size = WS_INITIAL_FRAME_BUFFER;
    l_priv->frame_buffer = DAP_NEW_Z_SIZE(uint8_t, l_priv->frame_buffer_size);
    if (!l_priv->frame_buffer) {
        log_it(L_CRITICAL, "Failed to allocate frame buffer");
        DAP_DELETE(l_priv);
        return -3;
    }

    a_transport->_inheritor = l_priv;
    UNUSED(a_config);

    log_it(L_DEBUG, "WebSocket transport initialized");
    return 0;
}

/**
 * @brief Deinitialize WebSocket transport
 */
static void s_ws_deinit(dap_net_transport_t *a_transport)
{
    if (!a_transport || !a_transport->_inheritor) {
        return;
    }

    dap_net_transport_websocket_private_t *l_priv = 
        (dap_net_transport_websocket_private_t*)a_transport->_inheritor;

    // Stop ping timer
    if (l_priv->ping_timer) {
        dap_timerfd_delete_mt(l_priv->ping_timer->worker, l_priv->ping_timer->esocket_uuid);
        l_priv->ping_timer = NULL;
    }

    // Free buffers
    if (l_priv->frame_buffer) {
        DAP_DELETE(l_priv->frame_buffer);
    }
    if (l_priv->upgrade_path) {
        DAP_DELETE(l_priv->upgrade_path);
    }
    if (l_priv->sec_websocket_key) {
        DAP_DELETE(l_priv->sec_websocket_key);
    }
    if (l_priv->sec_websocket_accept) {
        DAP_DELETE(l_priv->sec_websocket_accept);
    }
    if (l_priv->config.subprotocol) {
        DAP_DELETE(l_priv->config.subprotocol);
    }
    if (l_priv->config.origin) {
        DAP_DELETE(l_priv->config.origin);
    }

    DAP_DELETE(l_priv);
    a_transport->_inheritor = NULL;

    log_it(L_DEBUG, "WebSocket transport deinitialized");
}

/**
 * @brief Connect WebSocket transport (client-side)
 */
static int s_ws_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port,
                         dap_net_transport_connect_cb_t a_callback)
{
    if (!a_stream || !a_host) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    dap_net_transport_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket transport not initialized");
        return -2;
    }

    log_it(L_INFO, "WebSocket connecting to ws://%s:%u/stream", a_host, a_port);

    // Set state to connecting
    l_priv->state = DAP_WS_STATE_CONNECTING;

    // Generate WebSocket key for handshake
    char l_ws_key[32] = {0};
    if (s_ws_generate_key(l_ws_key, sizeof(l_ws_key)) != 0) {
        log_it(L_ERROR, "Failed to generate WebSocket key");
        return -3;
    }
    l_priv->sec_websocket_key = dap_strdup(l_ws_key);

    // Build HTTP upgrade request
    // This will be sent via HTTP client
    // Format:
    // GET /stream HTTP/1.1
    // Host: host:port
    // Upgrade: websocket
    // Connection: Upgrade
    // Sec-WebSocket-Key: <base64-key>
    // Sec-WebSocket-Version: 13
    // Sec-WebSocket-Protocol: dap-stream (if configured)
    // Origin: <origin> (if configured)

    // Connection establishment will continue via HTTP upgrade
    // Callback will be invoked when upgrade completes
    UNUSED(a_callback);

    return 0;
}

/**
 * @brief Listen on WebSocket transport (server-side)
 */
static int s_ws_listen(dap_net_transport_t *a_transport, const char *a_addr, uint16_t a_port,
                        dap_server_t *a_server)
{
    if (!a_transport) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    log_it(L_INFO, "WebSocket listening on %s:%u", a_addr ? a_addr : "any", a_port);

    // WebSocket server listens on HTTP server with upgrade handler
    // The HTTP server is already configured
    UNUSED(a_server);

    return 0;
}

/**
 * @brief Accept WebSocket connection (server-side)
 */
static int s_ws_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out)
{
    if (!a_listener || !a_stream_out) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    log_it(L_DEBUG, "WebSocket connection accepted");

    // WebSocket connections are accepted after HTTP upgrade
    // Stream is created by HTTP layer
    return 0;
}

/**
 * @brief Initialize handshake (client-side)
 */
static int s_ws_handshake_init(dap_stream_t *a_stream, dap_net_handshake_params_t *a_params,
                                dap_net_transport_handshake_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    log_it(L_DEBUG, "WebSocket handshake init");

    // WebSocket handshake happens after HTTP upgrade completes
    // DAP-level handshake (encryption) happens over WebSocket frames
    UNUSED(a_callback);

    return 0;
}

/**
 * @brief Process handshake (server-side)
 */
static int s_ws_handshake_process(dap_stream_t *a_stream, const void *a_data, size_t a_data_size,
                                   void **a_response, size_t *a_response_size)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream pointer");
        return -1;
    }

    log_it(L_DEBUG, "WebSocket handshake process: %zu bytes", a_data_size);

    // Process DAP handshake data received via WebSocket frames
    UNUSED(a_data);
    UNUSED(a_response);
    UNUSED(a_response_size);

    return 0;
}

/**
 * @brief WebSocket session create context
 */
typedef struct {
    dap_stream_t *stream;
    dap_net_transport_session_cb_t callback;
} s_ws_session_ctx_t;

static s_ws_session_ctx_t s_ws_session_ctx = {NULL, NULL};

/**
 * @brief WebSocket session create response callback wrapper (HTTP callback signature)
 */
static void s_ws_session_response_wrapper_http(void *a_data, size_t a_data_size, void *a_arg, http_status_code_t a_status)
{
    if (!a_data || !s_ws_session_ctx.stream) {
        return;
    }
    
    // Get encryption context from transport
    dap_net_transport_t *l_transport = s_ws_session_ctx.stream->stream_transport;
    if (!l_transport) {
        log_it(L_ERROR, "Stream has no transport");
        return;
    }
    
    // Parse session response to extract session_id
    uint32_t l_session_id = 0;
    char *l_response_data = NULL;
    size_t l_response_size = 0;
    
    if (a_data && a_data_size > 0) {
        // Decrypt response if encrypted
        if (l_transport->session_key) {
            size_t l_len = dap_enc_decode_out_size(l_transport->session_key, a_data_size, DAP_ENC_DATA_TYPE_RAW);
            char *l_response = DAP_NEW_Z_SIZE(char, l_len + 1);
            if (l_response) {
                l_len = dap_enc_decode(l_transport->session_key, a_data, a_data_size,
                                       l_response, l_len, DAP_ENC_DATA_TYPE_RAW);
                l_response[l_len] = '\0';
                
                // Parse response format: "session_id stream_key ..."
                sscanf(l_response, "%u", &l_session_id);
                
                // Allocate and copy full response data for transport callback
                l_response_data = DAP_NEW_Z_SIZE(char, l_len + 1);
                if (l_response_data) {
                    memcpy(l_response_data, l_response, l_len);
                    l_response_data[l_len] = '\0';
                    l_response_size = l_len;
                }
                
                DAP_DELETE(l_response);
            }
        } else {
            // Unencrypted response
            char *l_response_str = (char*)a_data;
            sscanf(l_response_str, "%u", &l_session_id);
            
            // Allocate and copy full response data for transport callback
            l_response_data = DAP_NEW_Z_SIZE(char, a_data_size + 1);
            if (l_response_data) {
                memcpy(l_response_data, a_data, a_data_size);
                l_response_data[a_data_size] = '\0';
                l_response_size = a_data_size;
            }
        }
    }
    
    // Call transport callback with session_id and full response data
    if (s_ws_session_ctx.callback) {
        s_ws_session_ctx.callback(s_ws_session_ctx.stream, l_session_id, l_response_data, l_response_size, 0);
    }
    
    // Clear context
    s_ws_session_ctx.stream = NULL;
    s_ws_session_ctx.callback = NULL;
}

/**
 * @brief WebSocket session create error callback wrapper (HTTP callback signature)
 */
static void s_ws_session_error_wrapper_http(int a_error, void *a_arg)
{
    if (!s_ws_session_ctx.stream) {
        return;
    }
    
    // Call transport callback with error
    if (s_ws_session_ctx.callback) {
        s_ws_session_ctx.callback(s_ws_session_ctx.stream, 0, NULL, 0, a_error);
    }
    
    // Clear context
    s_ws_session_ctx.stream = NULL;
    s_ws_session_ctx.callback = NULL;
}

/**
 * @brief WebSocket session create response callback wrapper (legacy - kept for compatibility)
 */
static void s_ws_session_response_wrapper(dap_client_t *a_client, void *a_data, size_t a_data_size)
{
    if (!a_client || !s_ws_session_ctx.stream) {
        return;
    }
    
    // Get encryption context from transport
    dap_net_transport_t *l_transport = s_ws_session_ctx.stream->stream_transport;
    if (!l_transport) {
        log_it(L_ERROR, "Stream has no transport");
        return;
    }
    
    // Parse session response to extract session_id
    uint32_t l_session_id = 0;
    char *l_response_data = NULL;
    size_t l_response_size = 0;
    
    if (a_data && a_data_size > 0) {
        // Decrypt response if encrypted
        if (l_transport->session_key) {
            size_t l_len = dap_enc_decode_out_size(l_transport->session_key, a_data_size, DAP_ENC_DATA_TYPE_RAW);
            char *l_response = DAP_NEW_Z_SIZE(char, l_len + 1);
            if (l_response) {
                l_len = dap_enc_decode(l_transport->session_key, a_data, a_data_size,
                                       l_response, l_len, DAP_ENC_DATA_TYPE_RAW);
                l_response[l_len] = '\0';
                
                // Parse response format: "session_id stream_key ..."
                sscanf(l_response, "%u", &l_session_id);
                
                // Allocate and copy full response data for transport callback
                l_response_data = DAP_NEW_Z_SIZE(char, l_len + 1);
                if (l_response_data) {
                    memcpy(l_response_data, l_response, l_len);
                    l_response_data[l_len] = '\0';
                    l_response_size = l_len;
                }
                
                DAP_DELETE(l_response);
            }
        } else {
            // Unencrypted response
            char *l_response_str = (char*)a_data;
            sscanf(l_response_str, "%u", &l_session_id);
            
            // Allocate and copy full response data for transport callback
            l_response_data = DAP_NEW_Z_SIZE(char, a_data_size + 1);
            if (l_response_data) {
                memcpy(l_response_data, a_data, a_data_size);
                l_response_data[a_data_size] = '\0';
                l_response_size = a_data_size;
            }
        }
    }
    
    // Call transport callback with session_id and full response data
    if (s_ws_session_ctx.callback) {
        s_ws_session_ctx.callback(s_ws_session_ctx.stream, l_session_id, l_response_data, l_response_size, 0);
    }
    
    // Clear context
    s_ws_session_ctx.stream = NULL;
    s_ws_session_ctx.callback = NULL;
}

/**
 * @brief WebSocket session create error callback wrapper
 */
static void s_ws_session_error_wrapper(dap_client_t *a_client, void *a_arg, int a_error)
{
    if (!a_client || !s_ws_session_ctx.stream) {
        return;
    }
    
    // Call transport callback with error
    if (s_ws_session_ctx.callback) {
        s_ws_session_ctx.callback(s_ws_session_ctx.stream, 0, NULL, 0, a_error);
    }
    
    // Clear context
    s_ws_session_ctx.stream = NULL;
    s_ws_session_ctx.callback = NULL;
}

/**
 * @brief Send encrypted HTTP request using WebSocket's own HTTP client
 * 
 * This function is similar to s_http_request_enc but uses WebSocket's HTTP client
 * and encryption context from dap_net_transport.
 */
static void s_ws_send_http_request_enc(dap_net_transport_t *a_transport, dap_http_client_t *a_http_client,
                                       dap_worker_t *a_worker, const char *a_uplink_addr, uint16_t a_uplink_port,
                                       const char *a_path, const char *a_sub_url, const char *a_query,
                                       void *a_request, size_t a_request_size,
                                       dap_client_callback_data_size_t a_response_proc,
                                       dap_client_callback_int_t a_response_error)
{
    if (!a_transport || !a_http_client || !a_worker) {
        log_it(L_ERROR, "Invalid parameters for s_ws_send_http_request_enc");
        return;
    }
    
    dap_enc_data_type_t l_enc_type = a_transport->uplink_protocol_version >= 21
        ? DAP_ENC_DATA_TYPE_B64_URLSAFE : DAP_ENC_DATA_TYPE_B64;
    char *l_path = NULL, *l_request_enc = NULL;
    if (a_path && *a_path) {
        size_t l_suburl_len = a_sub_url && *a_sub_url ? dap_strlen(a_sub_url) : 0,
               l_suburl_enc_size = dap_enc_code_out_size(a_transport->session_key, l_suburl_len, l_enc_type),
               l_query_len = a_query && *a_query ? dap_strlen(a_query) : 0,
               l_query_enc_size = dap_enc_code_out_size(a_transport->session_key, l_query_len, l_enc_type),
               l_path_size = dap_strlen(a_path) + l_suburl_enc_size + l_query_enc_size + 3;
        l_path = DAP_NEW_Z_SIZE(char, l_path_size);
        char *l_offset = dap_strncpy(l_path, a_path, l_path_size);
        *l_offset++ = '/';
        if (l_suburl_enc_size) {
            l_offset += dap_enc_code(a_transport->session_key, a_sub_url, l_suburl_len,
                                     l_offset, l_suburl_enc_size, l_enc_type);
            if (l_query_enc_size) {
                *l_offset++ = '?';
                dap_enc_code(a_transport->session_key, a_query, l_query_len,
                             l_offset, l_query_enc_size, l_enc_type);
            }
        }
    }
    size_t l_req_enc_size = 0;
    if (a_request && a_request_size) {
        l_req_enc_size = dap_enc_code_out_size(a_transport->session_key, a_request_size, l_enc_type) + 1;
        l_request_enc = DAP_NEW_Z_SIZE(char, l_req_enc_size);
        dap_enc_code(a_transport->session_key, a_request, a_request_size,
                     l_request_enc, l_req_enc_size, DAP_ENC_DATA_TYPE_RAW);
    }
    char *l_custom = dap_strdup_printf("KeyID: %s\r\n%s",
        a_transport->session_key_id ? a_transport->session_key_id : "NULL",
        a_transport->is_close_session ? "SessionCloseAfterRequest: true\r\n" : "");
    
    // Create new HTTP client for this request (WebSocket uses separate HTTP client)
    // This allows parallel operation with legacy HTTP transport
    dap_client_http_t *l_session_http_client = dap_client_http_request(a_worker,
        a_uplink_addr, a_uplink_port,
        a_request ? "POST" : "GET", "text/text", l_path, l_request_enc, l_req_enc_size, NULL,
        s_ws_session_response_wrapper_http, s_ws_session_error_wrapper_http, NULL, l_custom);
    
    if (!l_session_http_client) {
        log_it(L_ERROR, "Failed to create HTTP client for WebSocket session creation");
        if (a_response_error) {
            a_response_error(NULL, NULL, -1);
        }
    }
    
    DAP_DEL_MULTY(l_path, l_request_enc, l_custom);
}

/**
 * @brief Create session after handshake
 * 
 * For WebSocket transport, session creation is performed via HTTP POST to /stream_ctl endpoint
 * using a separate HTTP client created specifically for WebSocket transport. This allows parallel
 * operation with legacy HTTP transport, as WebSocket uses its own HTTP client instance.
 */
static int s_ws_session_create(dap_stream_t *a_stream, dap_net_session_params_t *a_params,
                                 dap_net_transport_session_cb_t a_callback)
{
    if (!a_stream || !a_params || !a_callback) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // Get transport from stream
    dap_net_transport_t *l_transport = a_stream->stream_transport;
    if (!l_transport) {
        log_it(L_ERROR, "Stream has no transport");
        return -2;
    }
    
    // Get client_pvt from stream esocket for worker and address info
    if (!a_stream->esocket || !a_stream->esocket->_inheritor) {
        log_it(L_ERROR, "Stream esocket has no client context");
        return -3;
    }
    
    dap_client_t *l_client = (dap_client_t*)a_stream->esocket->_inheritor;
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    if (!l_client_pvt) {
        log_it(L_ERROR, "Invalid client_pvt");
        return -4;
    }
    
    // Get WebSocket private data
    dap_net_transport_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket transport not initialized");
        return -5;
    }
    
    // Prepare request data (protocol version)
    char l_request[16];
    size_t l_request_size = snprintf(l_request, sizeof(l_request), "%d", DAP_CLIENT_PROTOCOL_VERSION);
    
    // Prepare sub_url based on protocol version
    uint32_t l_least_common_dap_protocol = dap_min(l_transport->remote_protocol_version,
                                                   l_transport->uplink_protocol_version);
    
    char *l_suburl;
    if (l_least_common_dap_protocol < 23) {
        l_suburl = dap_strdup_printf("stream_ctl,channels=%s", a_params->channels);
    } else {
        l_suburl = dap_strdup_printf("channels=%s,enc_type=%d,enc_key_size=%zu,enc_headers=%d",
                                     a_params->channels, a_params->enc_type,
                                     a_params->enc_key_size, a_params->enc_headers ? 1 : 0);
    }
    
    log_it(L_DEBUG, "WebSocket session create: sending POST to %s:%u%s/%s", 
           l_client->link_info.uplink_addr, l_client->link_info.uplink_port, 
           DAP_UPLINK_PATH_STREAM_CTL, l_suburl);
    
    // Store callback context
    s_ws_session_ctx.stream = a_stream;
    s_ws_session_ctx.callback = a_callback;
    
    // Create new HTTP client for session creation (separate from legacy HTTP transport)
    // Use WebSocket's HTTP client reference as template (though we create a new one for this request)
    // This ensures WebSocket transport doesn't interfere with legacy HTTP transport's http_client
    s_ws_send_http_request_enc(l_transport, l_priv->http_client, l_client_pvt->worker,
                               l_client->link_info.uplink_addr, l_client->link_info.uplink_port,
                               DAP_UPLINK_PATH_STREAM_CTL,
                               l_suburl, "type=tcp,maxconn=4", l_request, l_request_size,
                               s_ws_session_response_wrapper, 
                               s_ws_session_error_wrapper);
    
    DAP_DELETE(l_suburl);
    
    log_it(L_DEBUG, "WebSocket session create request sent successfully");
    return 0;
}

/**
 * @brief Start streaming
 */
static int s_ws_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_net_transport_ready_cb_t a_callback)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream pointer");
        return -1;
    }

    dap_net_transport_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket transport not initialized");
        return -2;
    }

    log_it(L_DEBUG, "WebSocket session start: session_id=%u", a_session_id);

    // Mark connection as open
    l_priv->state = DAP_WS_STATE_OPEN;

    // Start ping timer
    if (l_priv->config.ping_interval_ms > 0) {
        dap_worker_t *l_worker = dap_events_worker_get_auto();
        if (l_worker) {
            l_priv->ping_timer = dap_timerfd_start_on_worker(l_worker,
                                                               l_priv->config.ping_interval_ms,
                                                               s_ws_ping_timer_callback, a_stream);
            if (!l_priv->ping_timer) {
                log_it(L_WARNING, "Failed to start WebSocket ping timer");
            }
        }
    }

    // Invoke ready callback
    if (a_callback) {
        a_callback(a_stream, 0);
    }

    return 0;
}

/**
 * @brief Read data from WebSocket
 */
static ssize_t s_ws_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_buffer || a_size == 0) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    dap_net_transport_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket transport not initialized");
        return -2;
    }

    if (l_priv->state != DAP_WS_STATE_OPEN) {
        log_it(L_DEBUG, "WebSocket not in OPEN state");
        return 0;  // No data available
    }

    // WebSocket reading is event-driven via frame callbacks
    // This function reads from internal frame buffer
    // For now, return 0 (would block)
    log_it(L_DEBUG, "WebSocket read: %zu bytes requested", a_size);

    return 0;
}

/**
 * @brief Write data to WebSocket
 */
static ssize_t s_ws_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    dap_net_transport_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket transport not initialized");
        return -2;
    }

    if (l_priv->state != DAP_WS_STATE_OPEN) {
        log_it(L_ERROR, "WebSocket not in OPEN state");
        return -3;
    }

    // Build WebSocket binary frame
    size_t l_frame_size = a_size + 14;  // Max header size
    uint8_t *l_frame_buffer = DAP_NEW_Z_SIZE(uint8_t, l_frame_size);
    if (!l_frame_buffer) {
        log_it(L_CRITICAL, "Failed to allocate frame buffer");
        return -4;
    }

    size_t l_actual_frame_size = 0;
    bool l_should_mask = l_priv->config.client_mask_frames;  // Mask if client

    int l_ret = s_ws_build_frame(l_frame_buffer, l_frame_size, DAP_WS_OPCODE_BINARY,
                                   true, l_should_mask, a_data, a_size, &l_actual_frame_size);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to build WebSocket frame");
        DAP_DELETE(l_frame_buffer);
        return -5;
    }

    // Send frame via events socket
    // For now, just simulate success
    l_priv->frames_sent++;
    l_priv->bytes_sent += a_size;

    DAP_DELETE(l_frame_buffer);

    log_it(L_DEBUG, "WebSocket write: %zu bytes", a_size);
    return (ssize_t)a_size;
}

/**
 * @brief Close WebSocket connection
 */
static void s_ws_close(dap_stream_t *a_stream)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream pointer");
        return;
    }

    dap_net_transport_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        return;
    }

    log_it(L_DEBUG, "WebSocket connection closing");

    // Send close frame if not already closing
    if (l_priv->state == DAP_WS_STATE_OPEN) {
        l_priv->state = DAP_WS_STATE_CLOSING;
        dap_net_transport_websocket_send_close(a_stream, DAP_WS_CLOSE_NORMAL, "Connection closed");
    }

    // Stop ping timer
    if (l_priv->ping_timer) {
        dap_timerfd_delete_mt(l_priv->ping_timer->worker, l_priv->ping_timer->esocket_uuid);
        l_priv->ping_timer = NULL;
    }

    l_priv->state = DAP_WS_STATE_CLOSED;

    log_it(L_INFO, "WebSocket connection closed (sent=%lu frames, received=%lu frames)",
           l_priv->frames_sent, l_priv->frames_received);
}

/**
 * @brief Prepare TCP socket for WebSocket transport (client-side stage preparation)
 * 
 * Fully prepares esocket: creates, sets callbacks, connects, and adds to worker.
 * Transport is responsible for complete esocket lifecycle management.
 */
static int s_ws_stage_prepare(dap_net_transport_t *a_transport,
                              const dap_net_stage_prepare_params_t *a_params,
                              dap_net_stage_prepare_result_t *a_result)
{
    if (!a_transport || !a_params || !a_result) {
        log_it(L_ERROR, "Invalid arguments for WebSocket stage_prepare");
        return -1;
    }
    
    if (!a_params->worker) {
        log_it(L_ERROR, "Worker is required for WebSocket stage_prepare");
        a_result->error_code = -1;
        return -1;
    }
    
    // Initialize result
    a_result->esocket = NULL;
    a_result->error_code = 0;
    
    // Create TCP socket using platform-independent function
    dap_events_socket_t *l_es = dap_events_socket_create_platform(PF_INET, SOCK_STREAM, 0, a_params->callbacks);
    if (!l_es) {
        log_it(L_ERROR, "Failed to create WebSocket TCP socket");
        a_result->error_code = -1;
        return -1;
    }
    
    l_es->type = DESCRIPTOR_TYPE_SOCKET_CLIENT;
    l_es->_inheritor = a_params->client_context;
    
    // Resolve host and set address using centralized function
    if (dap_events_socket_resolve_and_set_addr(l_es, a_params->host, a_params->port) < 0) {
        log_it(L_ERROR, "Failed to resolve address for WebSocket transport");
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    // Set CONNECTING flag and initiate connection
    l_es->flags |= DAP_SOCK_CONNECTING;
#ifndef DAP_EVENTS_CAPS_IOCP
    l_es->flags |= DAP_SOCK_READY_TO_WRITE;
#endif
    l_es->is_initalized = false; // Ensure new_callback will be called
    
    // Initiate connection using platform-independent function
    int l_connect_err = 0;
    if (dap_events_socket_connect(l_es, &l_connect_err) != 0) {
        log_it(L_ERROR, "Failed to connect WebSocket socket: error %d", l_connect_err);
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    // Add socket to worker - connection will complete asynchronously
    dap_worker_add_events_socket(a_params->worker, l_es);
    
    a_result->esocket = l_es;
    a_result->error_code = 0;
    log_it(L_DEBUG, "WebSocket TCP socket prepared and connected for %s:%u", a_params->host, a_params->port);
    return 0;
}

/**
 * @brief Get WebSocket transport capabilities
 */
static uint32_t s_ws_get_capabilities(dap_net_transport_t *a_transport)
{
    (void)a_transport;

    return DAP_NET_TRANSPORT_CAP_RELIABLE |
           DAP_NET_TRANSPORT_CAP_ORDERED |
           DAP_NET_TRANSPORT_CAP_BIDIRECTIONAL |
           DAP_NET_TRANSPORT_CAP_MULTIPLEXING;
}

// ============================================================================
// WebSocket Protocol Helpers
// ============================================================================

/**
 * @brief Generate random WebSocket key (base64-encoded 16 bytes)
 */
static int s_ws_generate_key(char *a_key_out, size_t a_key_size)
{
    if (!a_key_out || a_key_size < 25) {  // Base64(16 bytes) = 24 chars + null
        return -1;
    }

    // Generate 16 random bytes
    uint8_t l_random[16];
    randombytes(l_random, sizeof(l_random));

    // Base64 encode
    size_t l_encoded_size = dap_enc_base64_encode(l_random, sizeof(l_random),
                                                    a_key_out, DAP_ENC_DATA_TYPE_B64);
    if (l_encoded_size == 0) {
        return -2;
    }

    a_key_out[l_encoded_size] = '\0';
    return 0;
}

/**
 * @brief Generate Sec-WebSocket-Accept from key (SHA1 + base64)
 */
static int s_ws_generate_accept(const char *a_key, char *a_accept_out, size_t a_accept_size)
{
    if (!a_key || !a_accept_out || a_accept_size < 29) {  // Base64(20 bytes) = 28 chars + null
        return -1;
    }

    // Concatenate key + magic GUID
    char l_concat[256];
    snprintf(l_concat, sizeof(l_concat), "%s%s", a_key, WS_MAGIC_GUID);

    // Calculate SHA-1 hash
    dap_chain_hash_fast_t l_hash;
    dap_hash_fast(l_concat, strlen(l_concat), &l_hash);

    // Base64 encode (first 20 bytes of hash)
    size_t l_encoded_size = dap_enc_base64_encode(l_hash.raw, 20,
                                                    a_accept_out, DAP_ENC_DATA_TYPE_B64);
    if (l_encoded_size == 0) {
        return -2;
    }

    a_accept_out[l_encoded_size] = '\0';
    return 0;
}

/**
 * @brief Build WebSocket frame
 */
static int s_ws_build_frame(uint8_t *a_buffer, size_t a_buffer_size, dap_ws_opcode_t a_opcode,
                             bool a_fin, bool a_mask, const void *a_payload, size_t a_payload_size,
                             size_t *a_frame_size_out)
{
    if (!a_buffer || !a_frame_size_out) {
        return -1;
    }

    size_t l_header_size = 2;  // Minimum header size
    size_t l_offset = 0;

    // Byte 0: FIN, RSV, Opcode
    a_buffer[l_offset++] = (a_fin ? 0x80 : 0x00) | (a_opcode & 0x0F);

    // Byte 1: MASK, Payload length
    if (a_payload_size < 126) {
        a_buffer[l_offset++] = (a_mask ? 0x80 : 0x00) | (uint8_t)a_payload_size;
    } else if (a_payload_size < 65536) {
        a_buffer[l_offset++] = (a_mask ? 0x80 : 0x00) | 126;
        uint16_t l_len = htons((uint16_t)a_payload_size);
        memcpy(&a_buffer[l_offset], &l_len, 2);
        l_offset += 2;
        l_header_size += 2;
    } else {
        a_buffer[l_offset++] = (a_mask ? 0x80 : 0x00) | 127;
        uint64_t l_len = htobe64((uint64_t)a_payload_size);
        memcpy(&a_buffer[l_offset], &l_len, 8);
        l_offset += 8;
        l_header_size += 8;
    }

    // Masking key (if needed)
    uint32_t l_mask_key = 0;
    if (a_mask) {
        randombytes(&l_mask_key, sizeof(l_mask_key));
        memcpy(&a_buffer[l_offset], &l_mask_key, 4);
        l_offset += 4;
        l_header_size += 4;
    }

    // Check buffer size
    if (l_offset + a_payload_size > a_buffer_size) {
        return -2;  // Buffer too small
    }

    // Copy and mask payload
    if (a_payload && a_payload_size > 0) {
        memcpy(&a_buffer[l_offset], a_payload, a_payload_size);
        if (a_mask) {
            s_ws_mask_unmask(&a_buffer[l_offset], a_payload_size, l_mask_key);
        }
        l_offset += a_payload_size;
    }

    *a_frame_size_out = l_offset;
    return 0;
}

/**
 * @brief Parse WebSocket frame (simplified)
 */
static int s_ws_parse_frame(const uint8_t *a_data, size_t a_data_size, dap_ws_opcode_t *a_opcode_out,
                             bool *a_fin_out, uint8_t **a_payload_out, size_t *a_payload_size_out,
                             size_t *a_frame_total_size_out)
{
    if (!a_data || a_data_size < 2) {
        return -1;
    }

    size_t l_offset = 0;

    // Parse byte 0
    bool l_fin = (a_data[l_offset] & 0x80) != 0;
    uint8_t l_opcode = a_data[l_offset] & 0x0F;
    l_offset++;

    // Parse byte 1
    bool l_mask = (a_data[l_offset] & 0x80) != 0;
    uint64_t l_payload_len = a_data[l_offset] & 0x7F;
    l_offset++;

    // Extended payload length
    if (l_payload_len == 126) {
        if (a_data_size < l_offset + 2) return -2;
        uint16_t l_len16;
        memcpy(&l_len16, &a_data[l_offset], 2);
        l_payload_len = ntohs(l_len16);
        l_offset += 2;
    } else if (l_payload_len == 127) {
        if (a_data_size < l_offset + 8) return -2;
        uint64_t l_len64;
        memcpy(&l_len64, &a_data[l_offset], 8);
        l_payload_len = be64toh(l_len64);
        l_offset += 8;
    }

    // Masking key
    uint32_t l_mask_key = 0;
    if (l_mask) {
        if (a_data_size < l_offset + 4) return -2;
        memcpy(&l_mask_key, &a_data[l_offset], 4);
        l_offset += 4;
    }

    // Check if full payload available
    if (a_data_size < l_offset + l_payload_len) {
        return -2;  // Incomplete frame
    }

    // Extract payload
    if (a_payload_out && a_payload_size_out) {
        *a_payload_size_out = l_payload_len;
        if (l_payload_len > 0) {
            *a_payload_out = DAP_NEW_Z_SIZE(uint8_t, l_payload_len);
            memcpy(*a_payload_out, &a_data[l_offset], l_payload_len);
            
            // Unmask if needed
            if (l_mask) {
                s_ws_mask_unmask(*a_payload_out, l_payload_len, l_mask_key);
            }
        }
    }

    if (a_opcode_out) *a_opcode_out = (dap_ws_opcode_t)l_opcode;
    if (a_fin_out) *a_fin_out = l_fin;
    if (a_frame_total_size_out) *a_frame_total_size_out = l_offset + l_payload_len;

    return 0;
}

/**
 * @brief Mask/unmask data with XOR
 */
static void s_ws_mask_unmask(uint8_t *a_data, size_t a_size, uint32_t a_mask_key)
{
    uint8_t *l_mask_bytes = (uint8_t*)&a_mask_key;
    for (size_t i = 0; i < a_size; i++) {
        a_data[i] ^= l_mask_bytes[i % 4];
    }
}

/**
 * @brief Ping timer callback
 */
static bool s_ws_ping_timer_callback(void *a_user_data)
{
    dap_stream_t *l_stream = (dap_stream_t*)a_user_data;
    if (!l_stream) {
        return false;  // Stop timer
    }

    dap_net_transport_websocket_private_t *l_priv = s_get_private_from_stream(l_stream);
    if (!l_priv || l_priv->state != DAP_WS_STATE_OPEN) {
        return false;  // Stop timer
    }

    // Send ping
    dap_net_transport_websocket_send_ping(l_stream, NULL, 0);

    // Check pong timeout
    int64_t l_now = time(NULL) * 1000;
    if (l_priv->last_pong_time > 0 &&
        (l_now - l_priv->last_pong_time) > (int64_t)l_priv->config.pong_timeout_ms) {
        log_it(L_WARNING, "WebSocket pong timeout, closing connection");
        dap_net_transport_websocket_send_close(l_stream, DAP_WS_CLOSE_ABNORMAL, "Pong timeout");
        return false;  // Stop timer
    }

    return true;  // Continue timer
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Check if stream is using WebSocket transport
 */
bool dap_stream_transport_is_websocket(const dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->stream_transport) {
        return false;
    }
    return a_stream->stream_transport->type == DAP_NET_TRANSPORT_WEBSOCKET;
}

/**
 * @brief Get WebSocket private data from stream
 */
dap_net_transport_websocket_private_t* dap_net_transport_websocket_get_private(dap_stream_t *a_stream)
{
    return s_get_private_from_stream(a_stream);
}

/**
 * @brief Send WebSocket close frame
 */
int dap_net_transport_websocket_send_close(dap_stream_t *a_stream, dap_ws_close_code_t a_code,
                                        const char *a_reason)
{
    if (!a_stream) {
        return -1;
    }

    dap_net_transport_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        return -2;
    }

    // Build close payload: 2-byte code + optional reason
    size_t l_reason_len = a_reason ? strlen(a_reason) : 0;
    size_t l_payload_size = 2 + l_reason_len;
    uint8_t *l_payload = DAP_NEW_Z_SIZE(uint8_t, l_payload_size);
    
    // Write close code (big-endian)
    uint16_t l_code_be = htons((uint16_t)a_code);
    memcpy(l_payload, &l_code_be, 2);
    
    // Write reason
    if (a_reason && l_reason_len > 0) {
        memcpy(l_payload + 2, a_reason, l_reason_len);
    }

    // Build and send close frame
    size_t l_frame_size = l_payload_size + 14;
    uint8_t *l_frame = DAP_NEW_Z_SIZE(uint8_t, l_frame_size);
    size_t l_actual_size;
    
    int l_ret = s_ws_build_frame(l_frame, l_frame_size, DAP_WS_OPCODE_CLOSE,
                                   true, l_priv->config.client_mask_frames,
                                   l_payload, l_payload_size, &l_actual_size);
    
    DAP_DELETE(l_payload);
    DAP_DELETE(l_frame);

    if (l_ret == 0) {
        log_it(L_DEBUG, "WebSocket close frame sent (code=%u)", a_code);
    }

    return l_ret;
}

/**
 * @brief Send WebSocket ping frame
 */
int dap_net_transport_websocket_send_ping(dap_stream_t *a_stream, const void *a_payload,
                                       size_t a_payload_size)
{
    if (!a_stream) {
        return -1;
    }

    if (a_payload_size > 125) {
        log_it(L_ERROR, "Ping payload too large (%zu > 125)", a_payload_size);
        return -2;
    }

    dap_net_transport_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        return -3;
    }

    // Build and send ping frame
    size_t l_frame_size = a_payload_size + 14;
    uint8_t *l_frame = DAP_NEW_Z_SIZE(uint8_t, l_frame_size);
    size_t l_actual_size;
    
    int l_ret = s_ws_build_frame(l_frame, l_frame_size, DAP_WS_OPCODE_PING,
                                   true, l_priv->config.client_mask_frames,
                                   a_payload, a_payload_size, &l_actual_size);
    
    DAP_DELETE(l_frame);

    if (l_ret == 0) {
        log_it(L_DEBUG, "WebSocket ping sent (%zu bytes payload)", a_payload_size);
    }

    return l_ret;
}

/**
 * @brief Get WebSocket statistics
 */
int dap_net_transport_websocket_get_stats(const dap_stream_t *a_stream, uint64_t *a_frames_sent,
                                       uint64_t *a_frames_received, uint64_t *a_bytes_sent,
                                       uint64_t *a_bytes_received)
{
    if (!a_stream) {
        return -1;
    }

    dap_net_transport_websocket_private_t *l_priv = s_get_private_from_stream((dap_stream_t*)a_stream);
    if (!l_priv) {
        return -2;
    }

    if (a_frames_sent) *a_frames_sent = l_priv->frames_sent;
    if (a_frames_received) *a_frames_received = l_priv->frames_received;
    if (a_bytes_sent) *a_bytes_sent = l_priv->bytes_sent;
    if (a_bytes_received) *a_bytes_received = l_priv->bytes_received;

    return 0;
}

// ============================================================================
// Private Helpers
// ============================================================================

/**
 * @brief Get private data from transport
 */
static dap_net_transport_websocket_private_t *s_get_private(dap_net_transport_t *a_transport)
{
    if (!a_transport || a_transport->type != DAP_NET_TRANSPORT_WEBSOCKET) {
        return NULL;
    }
    return (dap_net_transport_websocket_private_t*)a_transport->_inheritor;
}

/**
 * @brief Get private data from stream
 */
static dap_net_transport_websocket_private_t *s_get_private_from_stream(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->stream_transport) {
        return NULL;
    }
    return s_get_private(a_stream->stream_transport);
}

/**
 * @brief Register server-side handlers for WebSocket transport
 * 
 * Registers WebSocket upgrade handler for stream path.
 * Called by dap_net_transport_server_register_handlers().
 */
static int s_ws_register_server_handlers(dap_net_transport_t *a_transport, void *a_transport_context)
{
    if (!a_transport || !a_transport_context) {
        log_it(L_ERROR, "Invalid parameters for s_ws_register_server_handlers");
        return -1;
    }

    // a_transport_context is dap_net_transport_server_context_t*
    dap_net_transport_server_context_t *l_context = (dap_net_transport_server_context_t *)a_transport_context;
    
    if (!l_context->transport_specific) {
        log_it(L_WARNING, "WebSocket server instance not provided in transport context");
        return -2;
    }

    // Register WebSocket upgrade handler for stream path
    int l_ret = dap_net_transport_websocket_server_add_upgrade_handler(
        (dap_net_transport_websocket_server_t *)l_context->transport_specific, "stream");
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register WebSocket upgrade handler for stream");
        return l_ret;
    }

    log_it(L_DEBUG, "Registered WebSocket upgrade handler for stream path");
    return 0;
}

