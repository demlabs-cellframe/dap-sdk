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
#include "dap_net_trans.h"
#include "dap_net_trans_websocket_stream.h"
#include "dap_net_trans_websocket_server.h"
#include "dap_net_trans_server.h"
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
#include "dap_net_trans_http_stream.h"
#include "dap_stream_ctl.h"
#include "dap_enc_server.h"

#define LOG_TAG "dap_net_trans_websocket_stream"

// WebSocket magic GUID for handshake (RFC 6455)
#define WS_MAGIC_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// Default values
#define WS_DEFAULT_MAX_FRAME_SIZE  (1024 * 1024)  // 1MB
#define WS_DEFAULT_PING_INTERVAL   30000          // 30 seconds
#define WS_DEFAULT_PONG_TIMEOUT    10000          // 10 seconds
#define WS_INITIAL_FRAME_BUFFER    4096           // 4KB initial buffer

// Forward declarations
static const dap_net_trans_ops_t s_websocket_ops;
static int s_ws_init(dap_net_trans_t *a_trans, dap_config_t *a_config);
static void s_ws_deinit(dap_net_trans_t *a_trans);
static int s_ws_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port,
                         dap_net_trans_connect_cb_t a_callback);
static int s_ws_listen(dap_net_trans_t *a_trans, const char *a_addr, uint16_t a_port,
                        dap_server_t *a_server);
static int s_ws_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out);
static int s_ws_handshake_init(dap_stream_t *a_stream, dap_net_handshake_params_t *a_params,
                                dap_net_trans_handshake_cb_t a_callback);
static int s_ws_handshake_process(dap_stream_t *a_stream, const void *a_data, size_t a_data_size,
                                   void **a_response, size_t *a_response_size);
static int s_ws_session_create(dap_stream_t *a_stream, dap_net_session_params_t *a_params,
                                 dap_net_trans_session_cb_t a_callback);
static int s_ws_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_net_trans_ready_cb_t a_callback);
static ssize_t s_ws_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size);
static ssize_t s_ws_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static void s_ws_close(dap_stream_t *a_stream);
static uint32_t s_ws_get_capabilities(dap_net_trans_t *a_trans);
static int s_ws_register_server_handlers(dap_net_trans_t *a_trans, void *a_trans_ctx);
static int s_ws_stage_prepare(dap_net_trans_t *a_trans,
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
static dap_net_trans_websocket_private_t *s_get_private(dap_net_trans_t *a_trans);
static dap_net_trans_websocket_private_t *s_get_private_from_stream(dap_stream_t *a_stream);

// ============================================================================
// Trans Operations Table
// ============================================================================

static const dap_net_trans_ops_t s_websocket_ops = {
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
 * @brief Register WebSocket trans adapter
 */
int dap_net_trans_websocket_stream_register(void)
{
    // Initialize WebSocket server module first (registers server operations)
    int l_ret = dap_net_trans_websocket_server_init();
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize WebSocket server module: %d", l_ret);
        return l_ret;
    }
    
    log_it(L_DEBUG, "dap_net_trans_websocket_stream_register: WebSocket server module initialized, registering trans");
    
    // Register WebSocket trans operations
    int l_ret_trans = dap_net_trans_register("WebSocket",
                                                DAP_NET_TRANS_WEBSOCKET,
                                                &s_websocket_ops,
                                                DAP_NET_TRANS_SOCKET_TCP,
                                                NULL);
    if (l_ret_trans != 0) {
        log_it(L_ERROR, "Failed to register WebSocket trans: %d", l_ret_trans);
        dap_net_trans_websocket_server_deinit();
        return l_ret_trans;
    }

    log_it(L_NOTICE, "WebSocket trans registered successfully");
    return 0;
}

/**
 * @brief Unregister WebSocket trans adapter
 */
int dap_net_trans_websocket_stream_unregister(void)
{
    int l_ret = dap_net_trans_unregister(DAP_NET_TRANS_WEBSOCKET);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to unregister WebSocket trans: %d", l_ret);
        return l_ret;
    }

    // Deinitialize WebSocket server module
    dap_net_trans_websocket_server_deinit();

    log_it(L_NOTICE, "WebSocket trans unregistered successfully");
    return 0;
}

// ============================================================================
// Configuration Functions
// ============================================================================

/**
 * @brief Get default WebSocket configuration
 */
dap_net_trans_websocket_config_t dap_net_trans_websocket_config_default(void)
{
    dap_net_trans_websocket_config_t l_config = {
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
int dap_net_trans_websocket_set_config(dap_net_trans_t *a_trans,
                                        const dap_net_trans_websocket_config_t *a_config)
{
    if (!a_trans || !a_config) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    dap_net_trans_websocket_private_t *l_priv = s_get_private(a_trans);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket trans not initialized");
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
    memcpy(&l_priv->config, a_config, sizeof(dap_net_trans_websocket_config_t));

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
int dap_net_trans_websocket_get_config(dap_net_trans_t *a_trans,
                                        dap_net_trans_websocket_config_t *a_config)
{
    if (!a_trans || !a_config) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    dap_net_trans_websocket_private_t *l_priv = s_get_private(a_trans);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket trans not initialized");
        return -2;
    }

    memcpy(a_config, &l_priv->config, sizeof(dap_net_trans_websocket_config_t));
    return 0;
}

// ============================================================================
// Trans Operations Implementation
// ============================================================================

/**
 * @brief Initialize WebSocket trans
 */
static int s_ws_init(dap_net_trans_t *a_trans, dap_config_t *a_config)
{
    if (!a_trans) {
        log_it(L_ERROR, "Invalid trans pointer");
        return -1;
    }

    // Allocate private data
    dap_net_trans_websocket_private_t *l_priv = DAP_NEW_Z(dap_net_trans_websocket_private_t);
    if (!l_priv) {
        log_it(L_CRITICAL, "Failed to allocate WebSocket private data");
        return -2;
    }

    // Set default configuration
    l_priv->config = dap_net_trans_websocket_config_default();
    l_priv->state = DAP_WS_STATE_CLOSED;
    
    // Allocate initial frame buffer
    l_priv->frame_buffer_size = WS_INITIAL_FRAME_BUFFER;
    l_priv->frame_buffer = DAP_NEW_Z_SIZE(uint8_t, l_priv->frame_buffer_size);
    if (!l_priv->frame_buffer) {
        log_it(L_CRITICAL, "Failed to allocate frame buffer");
        DAP_DELETE(l_priv);
        return -3;
    }

    a_trans->_inheritor = l_priv;
    UNUSED(a_config);

    log_it(L_DEBUG, "WebSocket trans initialized");
    return 0;
}

/**
 * @brief Deinitialize WebSocket trans
 */
static void s_ws_deinit(dap_net_trans_t *a_trans)
{
    if (!a_trans || !a_trans->_inheritor) {
        return;
    }

    dap_net_trans_websocket_private_t *l_priv = 
        (dap_net_trans_websocket_private_t*)a_trans->_inheritor;

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
    a_trans->_inheritor = NULL;

    log_it(L_DEBUG, "WebSocket trans deinitialized");
}

/**
 * @brief Connect WebSocket trans (client-side)
 */
static int s_ws_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port,
                         dap_net_trans_connect_cb_t a_callback)
{
    if (!a_stream || !a_host) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    dap_net_trans_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket trans not initialized");
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
 * @brief Listen on WebSocket trans (server-side)
 */
static int s_ws_listen(dap_net_trans_t *a_trans, const char *a_addr, uint16_t a_port,
                        dap_server_t *a_server)
{
    if (!a_trans) {
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
 * @brief WebSocket handshake ctx
 */
typedef struct {
    dap_stream_t *stream;
    dap_net_trans_handshake_cb_t callback;
    dap_client_t *client;
    void *old_callback_arg;
} ws_handshake_ctx_t;

/**
 * @brief WebSocket handshake response wrapper
 */
static void s_ws_handshake_response_wrapper(void *a_data, size_t a_data_size, void *a_arg, http_status_code_t a_status)
{
    ws_handshake_ctx_t *l_ctx = (ws_handshake_ctx_t *)a_arg;
    if (!l_ctx) return;
    
    if (l_ctx->callback) {
        l_ctx->callback(l_ctx->stream, a_data, a_data_size, 0);
    }
    
    // Restore callback arg
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_ctx->client);
    if (l_client_pvt) {
        l_client_pvt->callback_arg = l_ctx->old_callback_arg;
    }
    
    DAP_DELETE(l_ctx);
}

/**
 * @brief WebSocket handshake error wrapper
 */
static void s_ws_handshake_error_wrapper(int a_error, void *a_arg)
{
    ws_handshake_ctx_t *l_ctx = (ws_handshake_ctx_t *)a_arg;
    if (!l_ctx) return;
    
    if (l_ctx->callback) {
        l_ctx->callback(l_ctx->stream, NULL, 0, a_error);
    }
    
    // Restore callback arg
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_ctx->client);
    if (l_client_pvt) {
        l_client_pvt->callback_arg = l_ctx->old_callback_arg;
    }
    
    DAP_DELETE(l_ctx);
}

/**
 * @brief Initialize handshake (client-side)
 */
static int s_ws_handshake_init(dap_stream_t *a_stream, dap_net_handshake_params_t *a_params,
                                dap_net_trans_handshake_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    log_it(L_DEBUG, "WebSocket handshake init (via HTTP)");
    
    dap_client_t *l_client = (dap_client_t*)a_stream->trans_ctx->esocket->_inheritor;
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    if (!l_client_pvt) {
        log_it(L_ERROR, "Invalid client_pvt");
        return -2;
    }

    // Prepare handshake data (alice public key with signatures)
    size_t l_data_size = a_params->alice_pub_key_size;
    uint8_t *l_data = DAP_DUP_SIZE(a_params->alice_pub_key, l_data_size);
    if (!l_data) {
        log_it(L_ERROR, "Failed to allocate handshake data");
        return -4;
    }
    
    // Add certificates signatures
    size_t l_sign_count = 0;
    dap_cert_t *l_node_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
    
    if (a_params->auth_cert) {
        l_sign_count += dap_cert_add_sign_to_data(a_params->auth_cert, &l_data, &l_data_size,
                                                   a_params->alice_pub_key, a_params->alice_pub_key_size);
    }
    
    if (l_node_cert) {
        l_sign_count += dap_cert_add_sign_to_data(l_node_cert, &l_data, &l_data_size,
                                                   a_params->alice_pub_key, a_params->alice_pub_key_size);
    }
    
    // Encode to base64
    size_t l_data_str_size_max = DAP_ENC_BASE64_ENCODE_SIZE(l_data_size);
    char *l_data_str = DAP_NEW_Z_SIZE(char, l_data_str_size_max + 1);
    if (!l_data_str) {
        DAP_DELETE(l_data);
        log_it(L_ERROR, "Failed to allocate base64 buffer");
        return -5;
    }
    
    size_t l_data_str_enc_size = dap_enc_base64_encode(l_data, l_data_size, l_data_str, DAP_ENC_DATA_TYPE_B64);
    DAP_DELETE(l_data);
    
    // Build URL with query parameters
    char l_enc_init_url[1024] = { '\0' };
    snprintf(l_enc_init_url, sizeof(l_enc_init_url), DAP_UPLINK_PATH_ENC_INIT
                 "/gd4y5yh78w42aaagh" "?enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zu,block_key_size=%zu,protocol_version=%d,sign_count=%zu",
                 a_params->enc_type, a_params->pkey_exchange_type, a_params->pkey_exchange_size,
                 a_params->block_key_size, a_params->protocol_version, l_sign_count);
    
    log_it(L_DEBUG, "WebSocket handshake init: sending POST to %s:%u%s", 
           l_client->link_info.uplink_addr, l_client->link_info.uplink_port, l_enc_init_url);
           
    // Create ctx
    ws_handshake_ctx_t *l_ctx = DAP_NEW_Z(ws_handshake_ctx_t);
    l_ctx->stream = a_stream;
    l_ctx->callback = a_callback;
    l_ctx->client = l_client;
    l_ctx->old_callback_arg = l_client_pvt->callback_arg;
    
    l_client_pvt->callback_arg = l_ctx;
    
    // Send HTTP request using dap_client_http_request
    // We use the client's worker and address
    dap_client_http_t *l_http_client = dap_client_http_request(l_client_pvt->worker,
                                            l_client->link_info.uplink_addr,
                                            l_client->link_info.uplink_port,
                                            "POST", "text/text", l_enc_init_url, l_data_str,
                                            l_data_str_enc_size, NULL, s_ws_handshake_response_wrapper, 
                                            s_ws_handshake_error_wrapper, l_ctx, NULL);
                                            
    DAP_DELETE(l_data_str);
    
    if (!l_http_client) {
        log_it(L_ERROR, "Failed to create HTTP request for WebSocket handshake");
        l_client_pvt->callback_arg = l_ctx->old_callback_arg;
        DAP_DELETE(l_ctx);
        return -6;
    }

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

    // Processing handled by s_enc_init_response via callback
    // We don't need to parse JSON here as dap_client_pvt handles it
    UNUSED(a_data);
    
    if (a_response) *a_response = NULL;
    if (a_response_size) *a_response_size = 0;

    return 0;
}

/**
 * @brief WebSocket session create ctx
 */
typedef struct {
    dap_stream_t *stream;
    dap_net_trans_session_cb_t callback;
    dap_enc_key_t *session_key;
} ws_session_ctx_t;

/**
 * @brief WebSocket session create response callback wrapper (HTTP callback signature)
 */
static void s_ws_session_response_wrapper_http(void *a_data, size_t a_data_size, void *a_arg, http_status_code_t a_status)
{
    ws_session_ctx_t *l_ctx = (ws_session_ctx_t *)a_arg;
    if (!l_ctx || !l_ctx->stream) {
        DAP_DEL_Z(l_ctx);
        return;
    }
    
    // Parse session response to extract session_id
    uint32_t l_session_id = 0;
    char *l_response_data = NULL;
    size_t l_response_size = 0;
    
    if (a_data && a_data_size > 0) {
        // Decrypt response if encrypted
        if (l_ctx->session_key) {
            size_t l_len = dap_enc_decode_out_size(l_ctx->session_key, a_data_size, DAP_ENC_DATA_TYPE_RAW);
            char *l_response = DAP_NEW_Z_SIZE(char, l_len + 1);
            if (l_response) {
                l_len = dap_enc_decode(l_ctx->session_key, a_data, a_data_size,
                                       l_response, l_len, DAP_ENC_DATA_TYPE_RAW);
                l_response[l_len] = '\0';
                
                // Parse response format: "session_id stream_key ..."
                sscanf(l_response, "%u", &l_session_id);
                
                // Allocate and copy full response data for trans callback
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
            
            // Allocate and copy full response data for trans callback
            l_response_data = DAP_NEW_Z_SIZE(char, a_data_size + 1);
            if (l_response_data) {
                memcpy(l_response_data, a_data, a_data_size);
                l_response_data[a_data_size] = '\0';
                l_response_size = a_data_size;
            }
        }
    }
    
    // Call trans callback with session_id and full response data
    if (l_ctx->callback) {
                if (l_response_data) {
            l_ctx->callback(l_ctx->stream, l_session_id, l_response_data, l_response_size, 0);
        } else {
            // Failed to parse or decrypt response, or empty response
            l_ctx->callback(l_ctx->stream, 0, NULL, 0, -1);
    }
    }
    
    DAP_DELETE(l_ctx);
}

/**
 * @brief WebSocket session create error callback wrapper (HTTP callback signature)
 */
static void s_ws_session_error_wrapper_http(int a_error, void *a_arg)
{
    ws_session_ctx_t *l_ctx = (ws_session_ctx_t *)a_arg;
    if (!l_ctx) return;

    if (l_ctx->stream && l_ctx->callback) {
        l_ctx->callback(l_ctx->stream, 0, NULL, 0, a_error);
    }
    
    DAP_DELETE(l_ctx);
}

/**
 * @brief Send encrypted HTTP request using WebSocket's own HTTP client
 */
static void s_ws_send_http_request_enc(dap_enc_key_t *a_session_key, const char *a_session_key_id,
                                       dap_http_client_t *a_http_client,
                                       dap_worker_t *a_worker, const char *a_uplink_addr, uint16_t a_uplink_port,
                                       const char *a_path, const char *a_sub_url, const char *a_query,
                                       void *a_request, size_t a_request_size,
                                       dap_client_http_callback_data_t a_response_proc,
                                       dap_client_http_callback_error_t a_response_error,
                                       void *a_callbacks_arg)
{
    if (!a_session_key || !a_worker) {
        log_it(L_ERROR, "Invalid parameters for s_ws_send_http_request_enc: key=%p, worker=%p", 
               a_session_key, a_worker);
        // Free ctx if provided, as callback won't be called
        if (a_callbacks_arg) DAP_DELETE(a_callbacks_arg);
        return;
    }
    
    // We don't strictly need a_http_client as we create a new one, so ignoring it if NULL
    dap_enc_data_type_t l_enc_type = DAP_ENC_DATA_TYPE_B64_URLSAFE;
    
    char *l_path = NULL, *l_request_enc = NULL;
    if (a_path && *a_path) {
        size_t l_suburl_len = a_sub_url && *a_sub_url ? dap_strlen(a_sub_url) : 0,
               l_suburl_enc_size = dap_enc_code_out_size(a_session_key, l_suburl_len, l_enc_type),
               l_query_len = a_query && *a_query ? dap_strlen(a_query) : 0,
               l_query_enc_size = dap_enc_code_out_size(a_session_key, l_query_len, l_enc_type),
               l_path_size = dap_strlen(a_path) + l_suburl_enc_size + l_query_enc_size + 3;
        l_path = DAP_NEW_Z_SIZE(char, l_path_size);
        char *l_offset = dap_strncpy(l_path, a_path, l_path_size);
        *l_offset++ = '/';
        if (l_suburl_enc_size) {
            l_offset += dap_enc_code(a_session_key, a_sub_url, l_suburl_len,
                                     l_offset, l_suburl_enc_size, l_enc_type);
            if (l_query_enc_size) {
                *l_offset++ = '?';
                dap_enc_code(a_session_key, a_query, l_query_len,
                             l_offset, l_query_enc_size, l_enc_type);
            }
        }
    }
    size_t l_req_enc_size = 0;
    if (a_request && a_request_size) {
        l_req_enc_size = dap_enc_code_out_size(a_session_key, a_request_size, l_enc_type) + 1;
        l_request_enc = DAP_NEW_Z_SIZE(char, l_req_enc_size);
        dap_enc_code(a_session_key, a_request, a_request_size,
                     l_request_enc, l_req_enc_size, DAP_ENC_DATA_TYPE_RAW);
    }
    char *l_custom = dap_strdup_printf("KeyID: %s\r\n%s",
        a_session_key_id ? a_session_key_id : "NULL",
        "SessionCloseAfterRequest: true\r\n"); // Always close session stream for control requests
    
    // Create new HTTP client for this request
    dap_client_http_t *l_session_http_client = dap_client_http_request(a_worker,
        a_uplink_addr, a_uplink_port,
        a_request ? "POST" : "GET", "text/text", l_path, l_request_enc, l_req_enc_size, NULL,
        a_response_proc, a_response_error, a_callbacks_arg, l_custom);
    
    if (!l_session_http_client) {
        log_it(L_ERROR, "Failed to create HTTP client for WebSocket session creation");
        if (a_response_error) {
            a_response_error(-1, a_callbacks_arg);
        } else {
            if (a_callbacks_arg) DAP_DELETE(a_callbacks_arg);
        }
    }
    
    DAP_DEL_MULTY(l_path, l_request_enc, l_custom);
}

/**
 * @brief Create session after handshake
 * 
 * For WebSocket trans, session creation is performed via HTTP POST to /stream_ctl endpoint
 * using a separate HTTP client created specifically for WebSocket trans. This allows parallel
 * operation with legacy HTTP trans, as WebSocket uses its own HTTP client instance.
 */
static int s_ws_session_create(dap_stream_t *a_stream, dap_net_session_params_t *a_params,
                                 dap_net_trans_session_cb_t a_callback)
{
    if (!a_stream || !a_params || !a_callback) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }
    
    // Get trans from stream
    dap_net_trans_t *l_trans = a_stream->trans;
    if (!l_trans) {
        log_it(L_ERROR, "Stream has no trans");
        return -2;
    }
    
    // Get client_pvt from stream esocket for worker and address info
    if (!a_stream->trans_ctx->esocket || !a_stream->trans_ctx->esocket->_inheritor) {
        log_it(L_ERROR, "Stream esocket has no client ctx");
        return -3;
    }
    
    dap_client_t *l_client = (dap_client_t*)a_stream->trans_ctx->esocket->_inheritor;
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    if (!l_client_pvt) {
        log_it(L_ERROR, "Invalid client_pvt");
        return -4;
    }
    
    // Get WebSocket private data
    dap_net_trans_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket trans not initialized");
        return -5;
    }
    
    // Prepare request data (protocol version)
    char l_request[16];
    size_t l_request_size = snprintf(l_request, sizeof(l_request), "%d", DAP_CLIENT_PROTOCOL_VERSION);
    
    // Prepare sub_url based on protocol version
    dap_net_trans_ctx_t *l_ctx = a_stream->trans_ctx;
    uint32_t l_least_common_dap_protocol = dap_min(l_ctx->remote_protocol_version,
                                                   l_ctx->uplink_protocol_version);
    
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
    
    // Allocate ctx
    ws_session_ctx_t *l_ws_ctx = DAP_NEW_Z(ws_session_ctx_t);
    l_ws_ctx->stream = a_stream;
    l_ws_ctx->callback = a_callback;
    l_ws_ctx->session_key = l_client_pvt->session_key; // Use key from client_pvt
    
    // Use client keys directly from client_pvt
    // dap_client_pvt already populated session_key and session_key_id in STAGE_ENC_INIT
    s_ws_send_http_request_enc(l_client_pvt->session_key, l_client_pvt->session_key_id,
                               l_priv->http_client, l_client_pvt->worker,
                               l_client->link_info.uplink_addr, l_client->link_info.uplink_port,
                               DAP_UPLINK_PATH_STREAM_CTL,
                               l_suburl, "type=tcp,maxconn=4", l_request, l_request_size,
                               s_ws_session_response_wrapper_http, 
                               s_ws_session_error_wrapper_http, l_ws_ctx);
    
    DAP_DELETE(l_suburl);
    
    log_it(L_DEBUG, "WebSocket session create request sent successfully");
    return 0;
}

/**
 * @brief Start streaming
 */
static int s_ws_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_net_trans_ready_cb_t a_callback)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream pointer");
        return -1;
    }

    dap_net_trans_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket trans not initialized");
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

    dap_net_trans_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket trans not initialized");
        return -2;
    }

    if (l_priv->state != DAP_WS_STATE_OPEN) {
        return 0;  // No data available
    }

    if (!l_priv->esocket || l_priv->esocket->buf_in_size == 0) {
    return 0;
    }

    // Process WebSocket frames from buf_in
    size_t l_total_read = 0;
    size_t l_consumed_total = 0;
    
    // We loop to consume as many frames as possible to fill a_buffer
    while (l_total_read < a_size && l_priv->esocket->buf_in_size > l_consumed_total) {
        dap_ws_opcode_t l_opcode = 0;
        bool l_fin = false;
        uint8_t *l_payload = NULL;
        size_t l_payload_size = 0;
        size_t l_frame_len = 0;
        
        uint8_t *l_ptr = l_priv->esocket->buf_in + l_consumed_total;
        size_t l_remaining = l_priv->esocket->buf_in_size - l_consumed_total;
        
        int l_res = s_ws_parse_frame(l_ptr, l_remaining, &l_opcode, &l_fin, &l_payload, &l_payload_size, &l_frame_len);
        
        if (l_res == -2) {
            // Incomplete frame
            break;
        }
        
        if (l_res != 0) {
            log_it(L_ERROR, "WebSocket frame parse error");
            // Consume 1 byte to try to recover (or close connection)
            l_consumed_total++;
            continue;
        }
        
        // Handle control frames
        if (l_opcode == DAP_WS_OPCODE_CLOSE) {
            log_it(L_INFO, "WebSocket received CLOSE frame");
            s_ws_close(a_stream);
            l_consumed_total += l_frame_len;
            DAP_DEL_Z(l_payload);
            // If we read some data before close, return it.
            // If we return -1 here, it signals error/close to dap_stream.
            return l_total_read > 0 ? (ssize_t)l_total_read : -1;
        }
        
        if (l_opcode == DAP_WS_OPCODE_PING) {
            // Send Pong
            dap_net_trans_websocket_send_pong(a_stream, l_payload, l_payload_size);
            DAP_DEL_Z(l_payload);
            l_consumed_total += l_frame_len;
            continue;
        }
        
        if (l_opcode == DAP_WS_OPCODE_PONG) {
            l_priv->last_pong_time = time(NULL) * 1000;
            DAP_DEL_Z(l_payload);
            l_consumed_total += l_frame_len;
            continue;
        }
        
        // Data frames (Binary or Text)
        if (l_payload && l_payload_size > 0) {
            size_t l_to_copy = dap_min(l_payload_size, a_size - l_total_read);
            memcpy((uint8_t*)a_buffer + l_total_read, l_payload, l_to_copy);
            l_total_read += l_to_copy;
            
            // If we couldn't copy all payload, we have a problem.
            // dap_stream assumes stream behavior. We just dropped data.
            // Ideally we should buffer it.
            if (l_to_copy < l_payload_size) {
                log_it(L_WARNING, "WebSocket read buffer too small (%zu < %zu), dropping %zu bytes", 
                       a_size - (l_total_read - l_to_copy), l_payload_size, l_payload_size - l_to_copy);
            }
        }
        
        DAP_DEL_Z(l_payload);
        l_consumed_total += l_frame_len;
    }
    
    if (l_consumed_total > 0) {
        dap_events_socket_shrink_buf_in(l_priv->esocket, l_consumed_total);
    }

    return (ssize_t)l_total_read;
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

    dap_net_trans_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        log_it(L_ERROR, "WebSocket trans not initialized");
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
    if (l_priv->esocket) {
        size_t l_sent = dap_events_socket_write_unsafe(l_priv->esocket, l_frame_buffer, l_actual_frame_size);
        if (l_sent == l_actual_frame_size) {
    l_priv->frames_sent++;
    l_priv->bytes_sent += a_size;
            log_it(L_DEBUG, "WebSocket write: %zu bytes (frame: %zu)", a_size, l_actual_frame_size);
        } else {
            log_it(L_ERROR, "WebSocket write incomplete or failed");
            // Return failure even if partial sent?
            // dap_stream expects number of bytes written from a_data.
            // If we failed to send full frame, we effectively failed.
            l_ret = -6;
        }
    } else {
        l_ret = -7;
    }

    DAP_DELETE(l_frame_buffer);

    if (l_ret == 0) return (ssize_t)a_size;
    return -1;
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

    dap_net_trans_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        return;
    }

    log_it(L_DEBUG, "WebSocket connection closing");

    // Send close frame if not already closing
    if (l_priv->state == DAP_WS_STATE_OPEN) {
        l_priv->state = DAP_WS_STATE_CLOSING;
        dap_net_trans_websocket_send_close(a_stream, DAP_WS_CLOSE_NORMAL, "Connection closed");
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
 * @brief Prepare TCP socket for WebSocket trans (client-side stage preparation)
 * 
 * Fully prepares esocket: creates, sets callbacks, connects, and adds to worker.
 * Trans is responsible for complete esocket lifecycle management.
 */
static int s_ws_stage_prepare(dap_net_trans_t *a_trans,
                              const dap_net_stage_prepare_params_t *a_params,
                              dap_net_stage_prepare_result_t *a_result)
{
    if (!a_trans || !a_params || !a_result) {
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
    a_result->stream = NULL;
    a_result->error_code = 0;
    
    // Create TCP socket using platform-independent function
    dap_events_socket_t *l_es = dap_events_socket_create_platform(PF_INET, SOCK_STREAM, 0, a_params->callbacks);
    if (!l_es) {
        log_it(L_ERROR, "Failed to create WebSocket TCP socket");
        a_result->error_code = -1;
        return -1;
    }
    
    l_es->type = DESCRIPTOR_TYPE_SOCKET_CLIENT;
    l_es->_inheritor = a_params->client_ctx;
    
    // Resolve host and set address using centralized function
    if (dap_events_socket_resolve_and_set_addr(l_es, a_params->host, a_params->port) < 0) {
        log_it(L_ERROR, "Failed to resolve address for WebSocket trans");
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
    
    // Create stream for this connection
    dap_stream_t *l_stream = dap_stream_new_es_client(l_es, (dap_stream_node_addr_t *)a_params->node_addr, a_params->authorized);
    if (!l_stream) {
        log_it(L_CRITICAL, "Failed to create stream for WebSocket trans");
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    // Set transport reference
    l_stream->trans = a_trans;
    
    a_result->esocket = l_es;
    a_result->stream = l_stream;
    a_result->error_code = 0;
    log_it(L_DEBUG, "WebSocket TCP socket and stream prepared for %s:%u", a_params->host, a_params->port);
    return 0;
}

/**
 * @brief Get WebSocket trans capabilities
 */
static uint32_t s_ws_get_capabilities(dap_net_trans_t *a_trans)
{
    (void)a_trans;

    return DAP_NET_TRANS_CAP_RELIABLE |
           DAP_NET_TRANS_CAP_ORDERED |
           DAP_NET_TRANS_CAP_BIDIRECTIONAL |
           DAP_NET_TRANS_CAP_MULTIPLEXING;
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

    dap_net_trans_websocket_private_t *l_priv = s_get_private_from_stream(l_stream);
    if (!l_priv || l_priv->state != DAP_WS_STATE_OPEN) {
        return false;  // Stop timer
    }

    // Send ping
    dap_net_trans_websocket_send_ping(l_stream, NULL, 0);

    // Check pong timeout
    int64_t l_now = time(NULL) * 1000;
    if (l_priv->last_pong_time > 0 &&
        (l_now - l_priv->last_pong_time) > (int64_t)l_priv->config.pong_timeout_ms) {
        log_it(L_WARNING, "WebSocket pong timeout, closing connection");
        dap_net_trans_websocket_send_close(l_stream, DAP_WS_CLOSE_ABNORMAL, "Pong timeout");
        return false;  // Stop timer
    }

    return true;  // Continue timer
}

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Check if stream is using WebSocket trans
 */
bool dap_stream_trans_is_websocket(const dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans) {
        return false;
    }
    return a_stream->trans->type == DAP_NET_TRANS_WEBSOCKET;
}

/**
 * @brief Get WebSocket private data from stream
 */
dap_net_trans_websocket_private_t* dap_net_trans_websocket_get_private(dap_stream_t *a_stream)
{
    return s_get_private_from_stream(a_stream);
}

/**
 * @brief Send WebSocket close frame
 */
int dap_net_trans_websocket_send_close(dap_stream_t *a_stream, dap_ws_close_code_t a_code,
                                        const char *a_reason)
{
    if (!a_stream) {
        return -1;
    }

    dap_net_trans_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
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
int dap_net_trans_websocket_send_ping(dap_stream_t *a_stream, const void *a_payload,
                                       size_t a_payload_size)
{
    if (!a_stream) {
        return -1;
    }

    if (a_payload_size > 125) {
        log_it(L_ERROR, "Ping payload too large (%zu > 125)", a_payload_size);
        return -2;
    }

    dap_net_trans_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
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
    
    if (l_ret == 0) {
        if (l_priv->esocket) {
            size_t l_sent = dap_events_socket_write_unsafe(l_priv->esocket, l_frame, l_actual_size);
            if (l_sent == l_actual_size) {
                log_it(L_DEBUG, "WebSocket ping sent (%zu bytes payload)", a_payload_size);
                l_priv->frames_sent++;
                l_priv->bytes_sent += l_actual_size;
            } else {
                log_it(L_ERROR, "WebSocket ping send failed or incomplete");
                l_ret = -5;
            }
        }
    }
    
    DAP_DELETE(l_frame);

    return l_ret;
}

/**
 * @brief Send WebSocket pong frame
 */
int dap_net_trans_websocket_send_pong(dap_stream_t *a_stream, const void *a_payload,
                                       size_t a_payload_size)
{
    if (!a_stream) {
        return -1;
    }

    if (a_payload_size > 125) {
        log_it(L_ERROR, "Pong payload too large (%zu > 125)", a_payload_size);
        return -2;
    }

    dap_net_trans_websocket_private_t *l_priv = s_get_private_from_stream(a_stream);
    if (!l_priv) {
        return -3;
    }

    // Build and send pong frame
    size_t l_frame_size = a_payload_size + 14;
    uint8_t *l_frame = DAP_NEW_Z_SIZE(uint8_t, l_frame_size);
    size_t l_actual_size;
    
    int l_ret = s_ws_build_frame(l_frame, l_frame_size, DAP_WS_OPCODE_PONG,
                                   true, l_priv->config.client_mask_frames,
                                   a_payload, a_payload_size, &l_actual_size);

    if (l_ret == 0) {
        if (l_priv->esocket) {
            size_t l_sent = dap_events_socket_write_unsafe(l_priv->esocket, l_frame, l_actual_size);
            if (l_sent == l_actual_size) {
                log_it(L_DEBUG, "WebSocket pong sent (%zu bytes payload)", a_payload_size);
                l_priv->frames_sent++;
                l_priv->bytes_sent += l_actual_size;
            } else {
                log_it(L_ERROR, "WebSocket pong send failed or incomplete");
                l_ret = -5;
            }
        }
    }
    
    DAP_DELETE(l_frame);

    return l_ret;
}

/**
 * @brief Get WebSocket statistics
 */
int dap_net_trans_websocket_get_stats(const dap_stream_t *a_stream, uint64_t *a_frames_sent,
                                       uint64_t *a_frames_received, uint64_t *a_bytes_sent,
                                       uint64_t *a_bytes_received)
{
    if (!a_stream) {
        return -1;
    }

    dap_net_trans_websocket_private_t *l_priv = s_get_private_from_stream((dap_stream_t*)a_stream);
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
 * @brief Get private data from trans
 */
static dap_net_trans_websocket_private_t *s_get_private(dap_net_trans_t *a_trans)
{
    if (!a_trans || a_trans->type != DAP_NET_TRANS_WEBSOCKET) {
        return NULL;
    }
    return (dap_net_trans_websocket_private_t*)a_trans->_inheritor;
}

/**
 * @brief Get private data from stream
 */
static dap_net_trans_websocket_private_t *s_get_private_from_stream(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans) {
        return NULL;
    }
    return s_get_private(a_stream->trans);
}

/**
 * @brief Register server-side handlers for WebSocket trans
 * 
 * Registers WebSocket upgrade handler for stream path.
 * Called by dap_net_trans_server_register_handlers().
 */
static int s_ws_register_server_handlers(dap_net_trans_t *a_trans, void *a_trans_ctx)
{
    if (!a_trans || !a_trans_ctx) {
        log_it(L_ERROR, "Invalid parameters for s_ws_register_server_handlers");
        return -1;
    }

    // a_trans_ctx is dap_net_trans_server_ctx_t*
    dap_net_trans_server_ctx_t *l_ctx = (dap_net_trans_server_ctx_t *)a_trans_ctx;
    
    if (!l_ctx->trans_specific) {
        log_it(L_WARNING, "WebSocket server instance not provided in trans ctx");
        return -2;
    }

    // Register WebSocket upgrade handler for stream path
    int l_ret = dap_net_trans_websocket_server_add_upgrade_handler(
        (dap_net_trans_websocket_server_t *)l_ctx->trans_specific, "stream");
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register WebSocket upgrade handler for stream");
        return l_ret;
    }

    log_it(L_DEBUG, "Registered WebSocket upgrade handler for stream path");
    return 0;
}

