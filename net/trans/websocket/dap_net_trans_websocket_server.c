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
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_net_trans.h"
#include "dap_net_trans_websocket_server.h"
#include "dap_net_trans_websocket_handshake.h"
#include "dap_http_client.h"
#include "dap_http_header.h"
#include "dap_http_header_server.h"  // For dap_http_out_header_add
#include "dap_net_trans_websocket_stream.h"
#include "dap_stream.h"
#include "dap_stream_ctl.h"
#include "dap_enc_http.h"
#include "dap_net_trans_server.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_net_server_common.h"
#include "dap_stream_session.h"
#include "dap_stream_ch.h"
#include "dap_stream_worker.h"
#include "dap_timerfd.h"

#include "dap_net_trans_ctx.h"

#define LOG_TAG "dap_net_trans_websocket_server"

static bool s_debug_more = false;

// Per-connection WebSocket state for server-side de-framing
typedef struct ws_server_conn_state {
    uint8_t *frame_buffer;      // Leftover partial DAP stream data between reads
    size_t frame_buffer_size;   // Allocated size
    size_t frame_buffer_used;   // Used bytes
} ws_server_conn_state_t;

typedef enum ws_upgrade_result {
    WS_UPGRADE_NOT_REQUEST = 0,
    WS_UPGRADE_HANDLED = 1
} ws_upgrade_result_t;

// Forward declarations for server-side esocket callbacks
static void s_ws_server_esocket_read(dap_events_socket_t *a_es, void *a_arg);
static void s_ws_server_esocket_delete(dap_events_socket_t *a_es, void *a_arg);
static void s_ws_server_esocket_error(dap_events_socket_t *a_es, int a_arg);
static void s_ws_server_conn_state_cleanup(dap_net_trans_ctx_t *a_trans_ctx);
static void s_ws_server_disable_stream_keepalive(dap_stream_t *a_stream);

static void s_ws_server_conn_state_cleanup(dap_net_trans_ctx_t *a_trans_ctx)
{
    if (!a_trans_ctx)
        return;

    ws_server_conn_state_t *l_conn = (ws_server_conn_state_t *)a_trans_ctx->transport_priv;
    if (!l_conn)
        return;

    DAP_DEL_Z(l_conn->frame_buffer);
    DAP_DELETE(l_conn);
    a_trans_ctx->transport_priv = NULL;
}

static void s_ws_server_disable_stream_keepalive(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->keepalive_timer)
        return;

    dap_timerfd_t *l_timer = a_stream->keepalive_timer;
    a_stream->keepalive_timer = NULL;

    void *l_arg_to_free = l_timer->callback_arg;
    l_timer->callback_arg = NULL;
    dap_timerfd_delete_unsafe(l_timer);
    DAP_DELETE(l_arg_to_free);
}

// Trans server operations callbacks
static void* s_websocket_server_new(const char *a_server_name)
{
    return (void*)dap_net_trans_websocket_server_new(a_server_name);
}

static int s_websocket_server_start(void *a_server, const char *a_cfg_section, 
                                     const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    dap_net_trans_websocket_server_t *l_ws = (dap_net_trans_websocket_server_t *)a_server;
    return dap_net_trans_websocket_server_start(l_ws, a_cfg_section, a_addrs, a_ports, a_count);
}

static void s_websocket_server_stop(void *a_server)
{
    dap_net_trans_websocket_server_t *l_ws = (dap_net_trans_websocket_server_t *)a_server;
    dap_net_trans_websocket_server_stop(l_ws);
}

static void s_websocket_server_delete(void *a_server)
{
    dap_net_trans_websocket_server_t *l_ws = (dap_net_trans_websocket_server_t *)a_server;
    dap_net_trans_websocket_server_delete(l_ws);
}

static const dap_net_trans_server_ops_t s_websocket_server_ops = {
    .new = s_websocket_server_new,
    .start = s_websocket_server_start,
    .stop = s_websocket_server_stop,
    .delete = s_websocket_server_delete
};

static void s_websocket_upgrade_headers_read(dap_http_client_t *a_http_client, void *a_arg);
static bool s_websocket_upgrade_headers_write(dap_http_client_t *a_http_client, void *a_arg);
static ws_upgrade_result_t s_websocket_handle_upgrade(dap_http_client_t *a_http_client);
static int s_switch_to_websocket_protocol(dap_http_client_t *a_http_client);
static bool s_http_header_has_token(const char *a_value, const char *a_token);
static void s_websocket_set_readable(dap_events_socket_t *a_esocket, bool a_is_ready);
static void s_websocket_set_writable(dap_events_socket_t *a_esocket, bool a_is_ready);

static int s_ascii_strncasecmp(const char *a_left, const char *a_right, size_t a_len)
{
    for (size_t i = 0; i < a_len; i++) {
        unsigned char l_left = (unsigned char)a_left[i];
        unsigned char l_right = (unsigned char)a_right[i];
        int l_diff = tolower(l_left) - tolower(l_right);
        if (l_diff || !l_left || !l_right)
            return l_diff;
    }
    return 0;
}

static bool s_http_header_has_token(const char *a_value, const char *a_token)
{
    if (!a_value || !a_token) {
        return false;
    }

    size_t l_token_len = strlen(a_token);
    if (!l_token_len) {
        return false;
    }

    const char *l_pos = a_value;
    while (*l_pos) {
        while (*l_pos == ' ' || *l_pos == '\t' || *l_pos == ',') {
            l_pos++;
        }

        const char *l_end = l_pos;
        while (*l_end && *l_end != ',') {
            l_end++;
        }

        const char *l_trimmed_end = l_end;
        while (l_trimmed_end > l_pos &&
               (l_trimmed_end[-1] == ' ' || l_trimmed_end[-1] == '\t')) {
            l_trimmed_end--;
        }

        size_t l_value_token_len = (size_t)(l_trimmed_end - l_pos);
        if (l_value_token_len == l_token_len &&
            s_ascii_strncasecmp(l_pos, a_token, l_token_len) == 0) {
            return true;
        }

        l_pos = l_end;
        if (*l_pos == ',') {
            l_pos++;
        }
    }
    return false;
}

static bool s_esocket_has_event_backend(dap_events_socket_t *a_esocket)
{
    return a_esocket && (a_esocket->context ||
                         (a_esocket->worker && a_esocket->worker->context));
}

static void s_websocket_set_readable(dap_events_socket_t *a_esocket, bool a_is_ready)
{
    if (!a_esocket) {
        return;
    }

    if (s_esocket_has_event_backend(a_esocket)) {
        dap_events_socket_set_readable_unsafe(a_esocket, a_is_ready);
    } else if (a_is_ready) {
        a_esocket->flags |= DAP_SOCK_READY_TO_READ;
    } else {
        a_esocket->flags &= ~DAP_SOCK_READY_TO_READ;
    }
}

static void s_websocket_set_writable(dap_events_socket_t *a_esocket, bool a_is_ready)
{
    if (!a_esocket) {
        return;
    }

    if (s_esocket_has_event_backend(a_esocket)) {
        dap_events_socket_set_writable_unsafe(a_esocket, a_is_ready);
    } else if (a_is_ready) {
        a_esocket->flags |= DAP_SOCK_READY_TO_WRITE;
    } else {
        a_esocket->flags &= ~DAP_SOCK_READY_TO_WRITE;
    }
}

/**
 * @brief Initialize WebSocket server module
 */
int dap_net_trans_websocket_server_init(void)
{
    // Register trans server operations
    int l_ret = dap_net_trans_server_register_ops(DAP_NET_TRANS_WEBSOCKET, &s_websocket_server_ops);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register WebSocket trans server operations");
        return l_ret;
    }
    
    log_it(L_NOTICE, "Initialized WebSocket server module");
    return 0;
}

/**
 * @brief Deinitialize WebSocket server module
 */
void dap_net_trans_websocket_server_deinit(void)
{
    // Unregister trans server operations
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_WEBSOCKET);
    
    log_it(L_INFO, "WebSocket server module deinitialized");
}

/**
 * @brief Create new WebSocket server instance
 */
dap_net_trans_websocket_server_t *dap_net_trans_websocket_server_new(const char *a_server_name)
{
    if (!a_server_name) {
        log_it(L_ERROR, "Server name is NULL");
        return NULL;
    }

    dap_net_trans_websocket_server_t *l_ws_server = DAP_NEW_Z(dap_net_trans_websocket_server_t);
    if (!l_ws_server) {
        log_it(L_CRITICAL, "Cannot allocate memory for WebSocket server");
        return NULL;
    }

    dap_strncpy(l_ws_server->server_name, a_server_name, sizeof(l_ws_server->server_name) - 1);
    
    // Get WebSocket trans instance
    l_ws_server->trans = dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    if (!l_ws_server->trans) {
        log_it(L_ERROR, "WebSocket trans not registered");
        DAP_DELETE(l_ws_server);
        return NULL;
    }

    log_it(L_INFO, "Created WebSocket server: %s", a_server_name);
    return l_ws_server;
}

/**
 * @brief Start WebSocket server on specified addresses and ports
 */
int dap_net_trans_websocket_server_start(dap_net_trans_websocket_server_t *a_ws_server,
                                const char *a_cfg_section,
                                const char **a_addrs,
                                uint16_t *a_ports,
                                size_t a_count)
{
    if (!a_ws_server || !a_ports || a_count == 0) {
        log_it(L_ERROR, "Invalid parameters for WebSocket server start");
        return -1;
    }

    if (a_ws_server->server) {
        log_it(L_WARNING, "WebSocket server already started");
        return -2;
    }

    // Create underlying dap_server_t
    dap_events_socket_callbacks_t l_client_callbacks = {
        .new_callback = dap_http_client_new,
        .delete_callback = dap_http_client_delete,
        .read_callback = dap_http_client_read,
        .write_callback = dap_http_client_write_callback,
        .error_callback = dap_http_client_error
    };

    a_ws_server->server = dap_server_new(a_cfg_section, NULL, &l_client_callbacks);
    if (!a_ws_server->server) {
        log_it(L_ERROR, "Failed to create dap_server for WebSocket");
        return -3;
    }

    // Create HTTP server for WebSocket upgrade handling
    a_ws_server->http_server = DAP_NEW_Z(dap_http_server_t);
    if (!a_ws_server->http_server) {
        log_it(L_CRITICAL, "Cannot allocate memory for HTTP server");
        dap_server_delete(a_ws_server->server);
        a_ws_server->server = NULL;
        return -4;
    }

    a_ws_server->server->_inheritor = a_ws_server->http_server;
    a_ws_server->http_server->server = a_ws_server->server;
    dap_strncpy(a_ws_server->http_server->server_name, a_ws_server->server_name,
                sizeof(a_ws_server->http_server->server_name) - 1);

    // Register WebSocket upgrade handler on root path
    // This will handle HTTP requests with "Upgrade: websocket" header
    dap_http_add_proc(a_ws_server->http_server, "/",
                      a_ws_server,  // Pass ws_server as inheritor
                      NULL,  // new_callback
                      NULL,  // delete_callback
                      s_websocket_upgrade_headers_read,
                      s_websocket_upgrade_headers_write,
                      NULL,  // data_read_callback
                      NULL,  // data_write_callback
                      NULL); // error_callback

    debug_if(s_debug_more, L_DEBUG, "Registered WebSocket upgrade handler on path '/'");

    // Register all required handlers for DAP protocol endpoints using unified trans API
    dap_net_trans_server_ctx_t *l_ctx = dap_net_trans_server_ctx_from_http(
        a_ws_server->http_server,
        DAP_NET_TRANS_WEBSOCKET,
        a_ws_server);  // Pass websocket server as trans-specific data
    
    if (!l_ctx) {
        log_it(L_ERROR, "Failed to create trans server ctx");
        dap_net_trans_websocket_server_stop(a_ws_server);
        return -6;
    }

    int l_ret = dap_net_trans_server_register_handlers(l_ctx);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register DAP protocol handlers");
        dap_net_trans_server_ctx_delete(l_ctx);
        dap_net_trans_websocket_server_stop(a_ws_server);
        return -7;
    }

    // Cleanup trans ctx (handlers are registered, ctx is no longer needed)
    dap_net_trans_server_ctx_delete(l_ctx);

    // Start listening on all specified address:port pairs using common accept callback
    for (size_t i = 0; i < a_count; i++) {
        const char *l_addr = (a_addrs && a_addrs[i]) ? a_addrs[i] : "0.0.0.0";
        uint16_t l_port = a_ports[i];

        int l_ret = dap_net_server_listen_addr_add_with_callback(a_ws_server->server, l_addr, l_port,
                                                 DESCRIPTOR_TYPE_SOCKET_LISTENING,
                                                                  NULL,  // No pre_worker_added callback needed
                                                                  NULL);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to start WebSocket server on %s:%u", l_addr, l_port);
            dap_net_trans_websocket_server_stop(a_ws_server);
            return -5;
        }

        log_it(L_NOTICE, "WebSocket server '%s' listening on %s:%u",
               a_ws_server->server_name, l_addr, l_port);
    }

    return 0;
}

/**
 * @brief Stop WebSocket server
 */
void dap_net_trans_websocket_server_stop(dap_net_trans_websocket_server_t *a_ws_server)
{
    if (!a_ws_server) {
        return;
    }

    if (a_ws_server->server) {
        dap_server_delete(a_ws_server->server);
        a_ws_server->server = NULL;
    }

    if (a_ws_server->http_server) {
        DAP_DELETE(a_ws_server->http_server);
        a_ws_server->http_server = NULL;
    }

    log_it(L_INFO, "WebSocket server '%s' stopped", a_ws_server->server_name);
}

/**
 * @brief Register WebSocket upgrade handler on a specific URL path
 */
int dap_net_trans_websocket_server_add_upgrade_handler(dap_net_trans_websocket_server_t *a_ws_server, const char *a_url_path)
{
    if (!a_ws_server || !a_ws_server->http_server || !a_url_path) {
        log_it(L_ERROR, "Invalid parameters for dap_net_trans_websocket_server_add_upgrade_handler");
        return -1;
    }

    // Register WebSocket upgrade handler on the HTTP server
    dap_http_url_proc_t *l_proc = dap_http_add_proc(a_ws_server->http_server,
                                                    a_url_path,
                                                    a_ws_server,  // Pass ws_server as inheritor
                                                    NULL,  // new_callback
                                                    NULL,  // delete_callback
                                                    s_websocket_upgrade_headers_read,
                                                    s_websocket_upgrade_headers_write,
                                                    NULL,  // data_read_callback
                                                    NULL,  // data_write_callback
                                                    NULL); // error_callback
    
    if (!l_proc) {
        log_it(L_ERROR, "Failed to register WebSocket upgrade handler on path '%s'", a_url_path);
        return -2;
    }

    log_it(L_INFO, "Registered WebSocket upgrade handler on path '%s'", a_url_path);
    return 0;
}

/**
 * @brief Delete WebSocket server instance
 */
void dap_net_trans_websocket_server_delete(dap_net_trans_websocket_server_t *a_ws_server)
{
    if (!a_ws_server) {
        return;
    }

    // Ensure server is stopped before deletion
    dap_net_trans_websocket_server_stop(a_ws_server);

    log_it(L_INFO, "Deleted WebSocket server: %s", a_ws_server->server_name);
    DAP_DELETE(a_ws_server);
}

/**
 * @brief Try to handle HTTP request as WebSocket upgrade
 *
 * Called at the top of the HTTP /stream handler. If the request carries
 * WebSocket upgrade headers, the full 101 handshake + protocol switch
 * is performed here. Otherwise returns -1 so the caller can proceed
 * with normal HTTP stream processing.
 */
int dap_net_trans_websocket_try_upgrade(dap_http_client_t *a_http_client)
{
    return s_websocket_handle_upgrade(a_http_client) == WS_UPGRADE_HANDLED ? 0 : -1;
}

// ============================================================================
// WebSocket Upgrade Handlers
// ============================================================================

static void s_websocket_set_http_reply(dap_http_client_t *a_http_client, uint16_t a_status_code)
{
    a_http_client->reply_status_code = a_status_code;
    s_websocket_set_writable(a_http_client->esocket, true);
    s_websocket_set_readable(a_http_client->esocket, false);
}

static ws_upgrade_result_t s_websocket_handle_upgrade(dap_http_client_t *a_http_client)
{
    if (!a_http_client || !a_http_client->esocket) {
        return WS_UPGRADE_NOT_REQUEST;
    }

    dap_http_header_t *l_upgrade = dap_http_header_find(a_http_client->in_headers, "Upgrade");
    dap_http_header_t *l_connection = dap_http_header_find(a_http_client->in_headers, "Connection");
    dap_http_header_t *l_ws_key = dap_http_header_find(a_http_client->in_headers, "Sec-WebSocket-Key");
    dap_http_header_t *l_ws_version = dap_http_header_find(a_http_client->in_headers, "Sec-WebSocket-Version");

    if (!l_upgrade || !l_connection || !l_ws_key || !l_ws_version) {
        return WS_UPGRADE_NOT_REQUEST;
    }

    if (!s_http_header_has_token(l_upgrade->value, "websocket")) {
        log_it(L_WARNING, "Invalid Upgrade header: %s", l_upgrade->value);
        s_websocket_set_http_reply(a_http_client, 400);
        return WS_UPGRADE_HANDLED;
    }

    if (!s_http_header_has_token(l_connection->value, "Upgrade")) {
        log_it(L_WARNING, "Invalid Connection header: %s", l_connection->value);
        s_websocket_set_http_reply(a_http_client, 400);
        return WS_UPGRADE_HANDLED;
    }

    if (strcmp(l_ws_version->value, "13") != 0) {
        log_it(L_WARNING, "Unsupported WebSocket version: %s", l_ws_version->value);
        a_http_client->reply_status_code = 426;
        dap_http_out_header_add(a_http_client, "Sec-WebSocket-Version", "13");
        s_websocket_set_writable(a_http_client->esocket, true);
        s_websocket_set_readable(a_http_client->esocket, false);
        return WS_UPGRADE_HANDLED;
    }

    char l_accept_key[128] = {0};
    if (dap_net_trans_websocket_build_accept_key(l_ws_key->value,
                                                 l_accept_key,
                                                 sizeof(l_accept_key)) != 0) {
        log_it(L_WARNING, "Invalid Sec-WebSocket-Key header");
        s_websocket_set_http_reply(a_http_client, 400);
        return WS_UPGRADE_HANDLED;
    }
    debug_if(s_debug_more, L_DEBUG, "Generated Sec-WebSocket-Accept: %s", l_accept_key);

    log_it(L_INFO, "WebSocket upgrade request accepted from %s",
           a_http_client->esocket->remote_addr_str);

    int l_switch_ret = s_switch_to_websocket_protocol(a_http_client);
    if (l_switch_ret != 0) {
        log_it(L_ERROR, "Failed to switch to WebSocket protocol after upgrade request: %d",
               l_switch_ret);
        s_websocket_set_http_reply(a_http_client, l_switch_ret == -3 ? 404 : 500);
        return WS_UPGRADE_HANDLED;
    }

    // Write 101 directly because the HTTP write path may rewrite reply_status_code
    // before this connection leaves HTTP mode.
    a_http_client->reply_status_code = 101;
    dap_events_socket_write_f_unsafe(a_http_client->esocket,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n",
        l_accept_key);

    s_websocket_set_readable(a_http_client->esocket, true);
    return WS_UPGRADE_HANDLED;
}

/**
 * @brief Handle HTTP headers and check for WebSocket upgrade request
 */
static void s_websocket_upgrade_headers_read(dap_http_client_t *a_http_client, void *a_arg)
{
    UNUSED(a_arg);
    if (!a_http_client) {
        return;
    }

    if (s_websocket_handle_upgrade(a_http_client) == WS_UPGRADE_NOT_REQUEST) {
        // Not a WebSocket upgrade request - handle as regular HTTP
        debug_if(s_debug_more, L_DEBUG, "Not a WebSocket upgrade request");
        a_http_client->state_read = DAP_HTTP_CLIENT_STATE_NONE;
        s_websocket_set_writable(a_http_client->esocket, true);
        s_websocket_set_readable(a_http_client->esocket, false);
        return;
    }
}

/**
 * @brief Write WebSocket upgrade response headers
 */
static bool s_websocket_upgrade_headers_write(dap_http_client_t *a_http_client, void *a_arg)
{
    UNUSED(a_arg);
    if (!a_http_client) {
        return false;
    }

    // After sending 101 Switching Protocols, the connection is now WebSocket
    if (a_http_client->reply_status_code == 101) {
        log_it(L_INFO, "WebSocket upgrade complete");

        // Connection is now WebSocket, keep it open for WebSocket frames
        a_http_client->state_read = DAP_HTTP_CLIENT_STATE_NONE;
        s_websocket_set_readable(a_http_client->esocket, true);
        s_websocket_set_writable(a_http_client->esocket, false);
        
        return true;
    }

    // For non-upgrade responses, close connection
    a_http_client->state_read = DAP_HTTP_CLIENT_STATE_NONE;
    return false;
}

/**
 * @brief Switch HTTP client to WebSocket protocol before sending successful Upgrade
 * 
 * Creates dap_stream_t from HTTP client and sets WebSocket trans.
 * This function must succeed before the 101 Switching Protocols response is sent.
 * 
 * Note: WebSocket trans's private data is shared across all streams,
 * but each stream maintains its own connection state via esocket.
 */
static int s_switch_to_websocket_protocol(dap_http_client_t *a_http_client)
{
    if (!a_http_client || !a_http_client->esocket) {
        log_it(L_ERROR, "Invalid HTTP client");
        return -1;
    }

    // Get WebSocket trans
    dap_net_trans_t *l_ws_trans = dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    if (!l_ws_trans) {
        log_it(L_ERROR, "WebSocket trans not registered");
        return -2;
    }

    // Check if stream already exists (from HTTP processing)
    dap_stream_t *l_stream = NULL;
    bool l_stream_created = false;
    bool l_trans_ctx_created = false;
    bool l_session_opened = false;
    bool l_session_assigned = false;
    dap_stream_session_t *l_prev_session = NULL;
    size_t l_initial_channel_count = 0;
    ws_server_conn_state_t *l_conn_state = NULL;
    char *l_service_key_value = NULL;
    int l_ret = 0;

    if (a_http_client->_inheritor) {
        // Check if inheritor is already a stream
        l_stream = (dap_stream_t*)a_http_client->_inheritor;
        // Verify it's actually a stream (has esocket field)
        if (l_stream && l_stream->esocket == a_http_client->esocket) {
            debug_if(s_debug_more, L_DEBUG, "Reusing existing stream for WebSocket upgrade");
        } else {
            l_stream = NULL;  // Not a valid stream, create new one
        }
    }

    // Extract session_id from URL query string (same as HTTP stream handler)
    unsigned int l_session_id = 0;
    if (a_http_client->in_query_string[0]) {
        if (sscanf(a_http_client->in_query_string, "session_id=%u", &l_session_id) != 1) {
            sscanf(a_http_client->in_query_string, "fj913htmdgaq-d9hf=%u", &l_session_id);
        }
    }

    // Find session (created by stream_ctl stage)
    dap_stream_session_t *l_session = NULL;
    if (l_session_id) {
        l_session = dap_stream_session_id_mt(l_session_id);
        if (!l_session) {
            log_it(L_ERROR, "WebSocket upgrade: session_id=%u not found", l_session_id);
            return -3;
        }
        int l_open_ret = dap_stream_session_open(l_session);
        if (l_open_ret != 0) {
            log_it(L_ERROR, "WebSocket upgrade: failed to open session %u (ret=%d)", l_session_id, l_open_ret);
            return -4;
        }
        l_session_opened = true;
    }

    // Create new stream if needed
    if (!l_stream) {
        dap_stream_node_addr_t *l_node_addr = l_session ? &l_session->node : NULL;
        l_stream = dap_stream_new_es_client(a_http_client->esocket, l_node_addr, false);
        if (!l_stream) {
            log_it(L_ERROR, "Failed to create stream from HTTP client");
            l_ret = -5;
            goto cleanup;
        }
        l_stream_created = true;

        // Server-side stream: set stream_worker from esocket's worker
        if (a_http_client->esocket->worker) {
            l_stream->stream_worker = DAP_STREAM_WORKER(a_http_client->esocket->worker);
        }
        l_stream->is_client_to_uplink = false;  // This is server-side
        s_ws_server_disable_stream_keepalive(l_stream);
    }
    l_initial_channel_count = l_stream->channel_count;

    // dap_stream_new_es_client does NOT create trans_ctx — create it here
    // It is needed by s_ws_server_esocket_read (reads a_es->_inheritor as trans_ctx)
    if (!l_stream->trans_ctx) {
        dap_net_trans_ctx_t *l_new_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
        if (!l_new_ctx) {
            log_it(L_CRITICAL, "Failed to allocate trans_ctx for WebSocket stream");
            l_ret = -6;
            goto cleanup;
        }
        l_stream->trans_ctx = l_new_ctx;
        l_trans_ctx_created = true;
    }

    // Create per-connection WebSocket state for de-framing
    l_conn_state = DAP_NEW_Z(ws_server_conn_state_t);
    if (!l_conn_state) {
        log_it(L_CRITICAL, "Failed to allocate WebSocket server connection state");
        l_ret = -7;
        goto cleanup;
    }

    // Associate stream with session and create channels
    if (l_session) {
        l_prev_session = l_stream->session;
        l_initial_channel_count = l_stream->channel_count;
        l_stream->session = l_session;
        l_session_assigned = true;

        // Extract Service-Key header if present
        dap_http_header_t *l_service_key = dap_http_header_find(a_http_client->in_headers, "Service-Key");
        if (l_service_key) {
            l_service_key_value = strdup(l_service_key->value);
            if (!l_service_key_value) {
                log_it(L_CRITICAL, "Failed to allocate WebSocket session service key");
                l_ret = -8;
                goto cleanup;
            }
        }

        // Create channels from session's active_channels list
        if (l_session->active_channels[0]) {
            size_t l_count = strlen(l_session->active_channels);
            for (size_t i = 0; i < l_count; i++) {
                dap_stream_ch_t *l_ch = dap_stream_ch_new(l_stream, l_session->active_channels[i]);
                if (!l_ch) {
                    log_it(L_ERROR, "Failed to create channel '%c' for WebSocket session %u",
                           l_session->active_channels[i], l_session_id);
                    l_ret = -9;
                    goto cleanup;
                }
            }
            log_it(L_INFO, "WebSocket: created %zu channels for session %u", l_count, l_session_id);
        }

        if (l_service_key_value) {
            l_session->service_key = l_service_key_value;
            l_service_key_value = NULL;
        }
    }

    // Set WebSocket trans for this stream
    l_stream->trans = l_ws_trans;

    l_stream->trans_ctx->trans = l_ws_trans;
    l_stream->trans_ctx->stream = l_stream;
    l_stream->trans_ctx->http_client = a_http_client;
    l_stream->trans_ctx->transport_priv = l_conn_state;
    l_conn_state = NULL;

    // Point esocket->_inheritor at trans_ctx so WebSocket read callback can find the stream
    a_http_client->esocket->_inheritor = l_stream->trans_ctx;
    a_http_client->_inheritor = l_stream;

    // Replace esocket callbacks: HTTP layer is no longer in charge.
    // After upgrade, the esocket handles raw WebSocket frames, not HTTP.
    a_http_client->esocket->callbacks.read_callback = s_ws_server_esocket_read;
    a_http_client->esocket->callbacks.write_callback = NULL;  // Write via trans->ops->write
    a_http_client->esocket->callbacks.delete_callback = s_ws_server_esocket_delete;
    a_http_client->esocket->callbacks.error_callback = s_ws_server_esocket_error;

    log_it(L_INFO, "Successfully switched to WebSocket protocol for stream %p (socket %p)",
           l_stream, a_http_client->esocket);
    return 0;

cleanup:
    DAP_DEL_Z(l_service_key_value);
    if (l_conn_state) {
        DAP_DEL_Z(l_conn_state->frame_buffer);
        DAP_DELETE(l_conn_state);
    }

    if (l_stream) {
        if (l_stream_created && a_http_client->_inheritor == l_stream)
            a_http_client->_inheritor = NULL;
        if (l_stream_created && a_http_client->esocket->_inheritor == l_stream->trans_ctx)
            a_http_client->esocket->_inheritor = NULL;

        if (l_stream_created) {
            l_stream->trans = NULL;
            l_stream->esocket = NULL;
            l_stream->esocket_uuid = 0;
            l_stream->esocket_worker = NULL;
            dap_stream_delete_unsafe(l_stream);
        } else {
            while (l_stream->channel_count > l_initial_channel_count)
                dap_stream_ch_delete(l_stream->channel[l_stream->channel_count - 1]);
            if (l_session_assigned && l_stream->session == l_session)
                l_stream->session = l_prev_session;
            if (l_trans_ctx_created) {
                DAP_DELETE(l_stream->trans_ctx);
                l_stream->trans_ctx = NULL;
            }
        }
    }

    if (l_session_opened && !(l_stream_created && l_session_assigned))
        dap_stream_session_close_mt(l_session->id);

    return l_ret;
}

// ============================================================================
// Server-side esocket callbacks (after WebSocket upgrade)
// ============================================================================

/**
 * @brief Server-side WebSocket read callback
 *
 * De-frames masked client WebSocket frames from buf_in, extracts payloads,
 * and passes raw DAP stream data to dap_stream_data_proc_read_ext.
 */
static void s_ws_server_esocket_read(dap_events_socket_t *a_es, void *a_arg)
{
    (void)a_arg;
    if (!a_es || a_es->buf_in_size == 0) return;

    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_es->_inheritor;
    dap_stream_t *l_stream = l_trans_ctx ? l_trans_ctx->stream : NULL;
    if (!l_stream) {
        log_it(L_ERROR, "WebSocket server read: no stream for esocket");
        return;
    }

    // Get per-connection state from trans_ctx->transport_priv
    ws_server_conn_state_t *l_conn = (ws_server_conn_state_t *)l_trans_ctx->transport_priv;

    size_t l_consumed = 0;
    size_t l_payload_buf_alloc = a_es->buf_in_size;
    uint8_t *l_payload_buf = DAP_NEW_Z_SIZE(uint8_t, l_payload_buf_alloc);
    if (!l_payload_buf) return;
    size_t l_payload_total = 0;

    while (l_consumed < a_es->buf_in_size) {
        dap_ws_opcode_t l_opcode = 0;
        bool l_fin = false;
        uint8_t *l_payload = NULL;
        size_t l_payload_size = 0;
        size_t l_frame_size = 0;

        int l_res = dap_net_trans_websocket_parse_client_frame(
            a_es->buf_in + l_consumed, a_es->buf_in_size - l_consumed,
            &l_opcode, &l_fin, &l_payload, &l_payload_size, &l_frame_size);

        if (l_res == -2) break;  // Incomplete frame
        if (l_res != 0) {
            log_it(L_WARNING, "WebSocket server: invalid client frame, closing connection");
            if (l_consumed > 0)
                dap_events_socket_shrink_buf_in(a_es, l_consumed);
            DAP_DELETE(l_payload_buf);
            dap_net_trans_websocket_send_close(l_stream, DAP_WS_CLOSE_PROTOCOL_ERROR, NULL);
            a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
            return;
        }

        // Handle control frames
        if (l_opcode == DAP_WS_OPCODE_CLOSE) {
            log_it(L_INFO, "WebSocket server: received CLOSE frame");
            DAP_DEL_Z(l_payload);
            l_consumed += l_frame_size;
            // Send close response and signal socket close
            dap_net_trans_websocket_send_close(l_stream, DAP_WS_CLOSE_NORMAL, NULL);
            a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
            break;
        }
        if (l_opcode == DAP_WS_OPCODE_PING) {
            dap_net_trans_websocket_send_pong(l_stream, l_payload, l_payload_size);
            DAP_DEL_Z(l_payload);
            l_consumed += l_frame_size;
            continue;
        }
        if (l_opcode == DAP_WS_OPCODE_PONG) {
            DAP_DEL_Z(l_payload);
            l_consumed += l_frame_size;
            continue;
        }

        // Data frame — accumulate payload
        if (l_payload && l_payload_size > 0) {
            if (l_payload_size > SIZE_MAX - l_payload_total) {
                DAP_DEL_Z(l_payload);
                DAP_DELETE(l_payload_buf);
                return;
            }
            size_t l_payload_needed = l_payload_total + l_payload_size;
            if (l_payload_needed > l_payload_buf_alloc) {
                if (l_payload_needed > SIZE_MAX / 2) {
                    DAP_DEL_Z(l_payload);
                    DAP_DELETE(l_payload_buf);
                    return;
                }
                size_t l_new_alloc = l_payload_needed * 2;
                uint8_t *l_new_payload_buf = DAP_REALLOC(l_payload_buf, l_new_alloc);
                if (!l_new_payload_buf) {
                    DAP_DEL_Z(l_payload);
                    DAP_DELETE(l_payload_buf);
                    return;
                }
                l_payload_buf = l_new_payload_buf;
                l_payload_buf_alloc = l_new_alloc;
            }
            memcpy(l_payload_buf + l_payload_total, l_payload, l_payload_size);
            l_payload_total += l_payload_size;
        }
        DAP_DEL_Z(l_payload);
        l_consumed += l_frame_size;
    }

    // Process de-framed data as raw DAP stream packets
    if (l_payload_total > 0 || (l_conn && l_conn->frame_buffer_used > 0)) {
        uint8_t *l_data = l_payload_buf;
        size_t l_data_size = l_payload_total;

        // Prepend leftover from previous call
        if (l_conn && l_conn->frame_buffer_used > 0) {
            if (l_payload_total > SIZE_MAX - l_conn->frame_buffer_used) {
                DAP_DELETE(l_payload_buf);
                return;
            }
            size_t l_total = l_conn->frame_buffer_used + l_payload_total;
            uint8_t *l_combined = DAP_NEW_Z_SIZE(uint8_t, l_total);
            if (!l_combined) {
                DAP_DELETE(l_payload_buf);
                return;
            }
            memcpy(l_combined, l_conn->frame_buffer, l_conn->frame_buffer_used);
            memcpy(l_combined + l_conn->frame_buffer_used, l_payload_buf, l_payload_total);
            l_data = l_combined;
            l_data_size = l_total;
        }

        size_t l_processed = dap_stream_data_proc_read_ext(l_stream, l_data, l_data_size);
        if (l_stream->delete_deferred || l_stream->delete_in_progress) {
            if (l_data != l_payload_buf)
                DAP_DELETE(l_data);
            DAP_DELETE(l_payload_buf);
            s_ws_server_conn_state_cleanup(l_trans_ctx);
            dap_stream_delete_unsafe(l_stream);
            return;
        }

        // Save unprocessed remainder
        size_t l_remaining = l_data_size - l_processed;
        if (l_remaining > 0 && l_conn) {
            if (l_remaining > l_conn->frame_buffer_size) {
                uint8_t *l_new_frame_buffer = DAP_REALLOC(l_conn->frame_buffer, l_remaining);
                if (!l_new_frame_buffer) {
                    if (l_data != l_payload_buf)
                        DAP_DELETE(l_data);
                    DAP_DELETE(l_payload_buf);
                    return;
                }
                l_conn->frame_buffer = l_new_frame_buffer;
                l_conn->frame_buffer_size = l_remaining;
            }
            memcpy(l_conn->frame_buffer, l_data + l_processed, l_remaining);
            l_conn->frame_buffer_used = l_remaining;
        } else if (l_conn) {
            l_conn->frame_buffer_used = 0;
        }

        if (l_data != l_payload_buf)
            DAP_DELETE(l_data);
    }

    if (l_consumed > 0)
        dap_events_socket_shrink_buf_in(a_es, l_consumed);

    DAP_DELETE(l_payload_buf);
}

/**
 * @brief Server-side WebSocket esocket delete callback
 */
static void s_ws_server_esocket_delete(dap_events_socket_t *a_es, void *a_arg)
{
    (void)a_arg;
    if (!a_es) return;

    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_es->_inheritor;
    if (!l_trans_ctx) return;

    // Clear esocket reference FIRST — we are inside esocket's delete callback,
    // so dap_stream_delete_unsafe must NOT try to re-delete this esocket
    a_es->_inheritor = NULL;
    if (l_trans_ctx->stream) {
        l_trans_ctx->stream->esocket = NULL;
        l_trans_ctx->stream->esocket_uuid = 0;
        l_trans_ctx->stream->esocket_worker = NULL;
    }

    // Clean up per-connection WS state
    s_ws_server_conn_state_cleanup(l_trans_ctx);

    // Clean up stream (will free trans_ctx and channels/session)
    dap_stream_t *l_stream = l_trans_ctx->stream;
    if (l_stream) {
        dap_stream_delete_unsafe(l_stream);
    }
}

/**
 * @brief Server-side WebSocket esocket error callback
 */
static void s_ws_server_esocket_error(dap_events_socket_t *a_es, int a_arg)
{
    (void)a_arg;
    if (!a_es) return;
    log_it(L_ERROR, "WebSocket server: esocket error on fd=%d", a_es->socket);
    a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
}
