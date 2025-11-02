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
#include <strings.h>  // For strcasestr
#include <openssl/sha.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_net_transport_websocket_server.h"
#include "dap_http_client.h"
#include "dap_http_header.h"
#include "dap_http_header_server.h"  // For dap_http_out_header_add
#include "dap_net_transport_websocket_stream.h"
#include "dap_stream.h"
#include "dap_stream_ctl.h"
#include "dap_enc_http.h"
#include "dap_enc_base64.h"
#include "dap_net_transport_server.h"
#include "dap_events_socket.h"
#include "dap_net_server_common.h"

#define LOG_TAG "dap_net_transport_websocket_server"

// WebSocket GUID for Sec-WebSocket-Accept calculation (RFC 6455)
#define WEBSOCKET_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

// Transport server operations callbacks
static void* s_websocket_server_new(const char *a_server_name)
{
    return (void*)dap_net_transport_websocket_server_new(a_server_name);
}

static int s_websocket_server_start(void *a_server, const char *a_cfg_section, 
                                     const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    dap_net_transport_websocket_server_t *l_ws = (dap_net_transport_websocket_server_t *)a_server;
    return dap_net_transport_websocket_server_start(l_ws, a_cfg_section, a_addrs, a_ports, a_count);
}

static void s_websocket_server_stop(void *a_server)
{
    dap_net_transport_websocket_server_t *l_ws = (dap_net_transport_websocket_server_t *)a_server;
    dap_net_transport_websocket_server_stop(l_ws);
}

static void s_websocket_server_delete(void *a_server)
{
    dap_net_transport_websocket_server_t *l_ws = (dap_net_transport_websocket_server_t *)a_server;
    dap_net_transport_websocket_server_delete(l_ws);
}

static const dap_net_transport_server_ops_t s_websocket_server_ops = {
    .new = s_websocket_server_new,
    .start = s_websocket_server_start,
    .stop = s_websocket_server_stop,
    .delete = s_websocket_server_delete
};

static void s_websocket_upgrade_headers_read(dap_http_client_t *a_http_client, void *a_arg);
static bool s_websocket_upgrade_headers_write(dap_http_client_t *a_http_client, void *a_arg);
static bool s_generate_accept_key(const char *a_client_key, char *a_accept_key, size_t a_accept_key_size);
static int s_switch_to_websocket_protocol(dap_http_client_t *a_http_client);

/**
 * @brief Initialize WebSocket server module
 */
int dap_net_transport_websocket_server_init(void)
{
    // Register transport server operations
    int l_ret = dap_net_transport_server_register_ops(DAP_STREAM_TRANSPORT_WEBSOCKET, &s_websocket_server_ops);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register WebSocket transport server operations");
        return l_ret;
    }
    
    log_it(L_NOTICE, "Initialized WebSocket server module");
    return 0;
}

/**
 * @brief Deinitialize WebSocket server module
 */
void dap_net_transport_websocket_server_deinit(void)
{
    // Unregister transport server operations
    dap_net_transport_server_unregister_ops(DAP_STREAM_TRANSPORT_WEBSOCKET);
    
    log_it(L_INFO, "WebSocket server module deinitialized");
}

/**
 * @brief Create new WebSocket server instance
 */
dap_net_transport_websocket_server_t *dap_net_transport_websocket_server_new(const char *a_server_name)
{
    if (!a_server_name) {
        log_it(L_ERROR, "Server name is NULL");
        return NULL;
    }

    dap_net_transport_websocket_server_t *l_ws_server = DAP_NEW_Z(dap_net_transport_websocket_server_t);
    if (!l_ws_server) {
        log_it(L_CRITICAL, "Cannot allocate memory for WebSocket server");
        return NULL;
    }

    dap_strncpy(l_ws_server->server_name, a_server_name, sizeof(l_ws_server->server_name) - 1);
    
    // Get WebSocket transport instance
    l_ws_server->transport = dap_stream_transport_find(DAP_STREAM_TRANSPORT_WEBSOCKET);
    if (!l_ws_server->transport) {
        log_it(L_ERROR, "WebSocket transport not registered");
        DAP_DELETE(l_ws_server);
        return NULL;
    }

    log_it(L_INFO, "Created WebSocket server: %s", a_server_name);
    return l_ws_server;
}

/**
 * @brief Start WebSocket server on specified addresses and ports
 */
int dap_net_transport_websocket_server_start(dap_net_transport_websocket_server_t *a_ws_server,
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

    log_it(L_DEBUG, "Registered WebSocket upgrade handler on path '/'");

    // Register all required handlers for DAP protocol endpoints using unified transport API
    dap_net_transport_server_context_t *l_context = dap_net_transport_server_context_from_http(
        a_ws_server->http_server,
        DAP_STREAM_TRANSPORT_WEBSOCKET,
        a_ws_server);  // Pass websocket server as transport-specific data
    
    if (!l_context) {
        log_it(L_ERROR, "Failed to create transport server context");
        dap_net_transport_websocket_server_stop(a_ws_server);
        return -6;
    }

    int l_ret = dap_net_transport_server_register_handlers(l_context);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register DAP protocol handlers");
        dap_net_transport_server_context_delete(l_context);
        dap_net_transport_websocket_server_stop(a_ws_server);
        return -7;
    }

    // Cleanup transport context (handlers are registered, context is no longer needed)
    dap_net_transport_server_context_delete(l_context);

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
            dap_net_transport_websocket_server_stop(a_ws_server);
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
void dap_net_transport_websocket_server_stop(dap_net_transport_websocket_server_t *a_ws_server)
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
int dap_net_transport_websocket_server_add_upgrade_handler(dap_net_transport_websocket_server_t *a_ws_server, const char *a_url_path)
{
    if (!a_ws_server || !a_ws_server->http_server || !a_url_path) {
        log_it(L_ERROR, "Invalid parameters for dap_net_transport_websocket_server_add_upgrade_handler");
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
void dap_net_transport_websocket_server_delete(dap_net_transport_websocket_server_t *a_ws_server)
{
    if (!a_ws_server) {
        return;
    }

    // Ensure server is stopped before deletion
    dap_net_transport_websocket_server_stop(a_ws_server);

    log_it(L_INFO, "Deleted WebSocket server: %s", a_ws_server->server_name);
    DAP_DELETE(a_ws_server);
}

// ============================================================================
// WebSocket Upgrade Handlers
// ============================================================================

/**
 * @brief Handle HTTP headers and check for WebSocket upgrade request
 */
static void s_websocket_upgrade_headers_read(dap_http_client_t *a_http_client, void *a_arg)
{
    if (!a_http_client) {
        return;
    }

    // Check for WebSocket upgrade headers
    dap_http_header_t *l_upgrade = dap_http_header_find(a_http_client->in_headers, "Upgrade");
    dap_http_header_t *l_connection = dap_http_header_find(a_http_client->in_headers, "Connection");
    dap_http_header_t *l_ws_key = dap_http_header_find(a_http_client->in_headers, "Sec-WebSocket-Key");
    dap_http_header_t *l_ws_version = dap_http_header_find(a_http_client->in_headers, "Sec-WebSocket-Version");

    // Check if this is a WebSocket upgrade request
    if (!l_upgrade || !l_connection || !l_ws_key || !l_ws_version) {
        // Not a WebSocket upgrade request - handle as regular HTTP
        log_it(L_DEBUG, "Not a WebSocket upgrade request");
        a_http_client->state_read = DAP_HTTP_CLIENT_STATE_NONE;
        dap_events_socket_set_writable_unsafe(a_http_client->esocket, true);
        dap_events_socket_set_readable_unsafe(a_http_client->esocket, false);
        return;
    }

    // Validate upgrade headers
    if (strcasestr(l_upgrade->value, "websocket") == NULL) {
        log_it(L_WARNING, "Invalid Upgrade header: %s", l_upgrade->value);
        a_http_client->reply_status_code = 400; // Bad Request
        dap_events_socket_set_writable_unsafe(a_http_client->esocket, true);
        dap_events_socket_set_readable_unsafe(a_http_client->esocket, false);
        return;
    }

    if (strcasestr(l_connection->value, "Upgrade") == NULL) {
        log_it(L_WARNING, "Invalid Connection header: %s", l_connection->value);
        a_http_client->reply_status_code = 400; // Bad Request
        dap_events_socket_set_writable_unsafe(a_http_client->esocket, true);
        dap_events_socket_set_readable_unsafe(a_http_client->esocket, false);
        return;
    }

    // Check WebSocket version (should be 13)
    if (strcmp(l_ws_version->value, "13") != 0) {
        log_it(L_WARNING, "Unsupported WebSocket version: %s", l_ws_version->value);
        a_http_client->reply_status_code = 426; // Upgrade Required
        dap_http_out_header_add(a_http_client, "Sec-WebSocket-Version", "13");
        dap_events_socket_set_writable_unsafe(a_http_client->esocket, true);
        dap_events_socket_set_readable_unsafe(a_http_client->esocket, false);
        return;
    }

    // Valid WebSocket upgrade request - prepare 101 Switching Protocols response
    a_http_client->reply_status_code = 101;
    
    // Calculate Sec-WebSocket-Accept
    char l_accept_key[128] = {0};
    if (!s_generate_accept_key(l_ws_key->value, l_accept_key, sizeof(l_accept_key))) {
        log_it(L_ERROR, "Failed to generate Sec-WebSocket-Accept key");
        a_http_client->reply_status_code = 500; // Internal Server Error
        dap_events_socket_set_writable_unsafe(a_http_client->esocket, true);
        dap_events_socket_set_readable_unsafe(a_http_client->esocket, false);
        return;
    }

    // Add WebSocket upgrade response headers
    dap_http_out_header_add(a_http_client, "Upgrade", "websocket");
    dap_http_out_header_add(a_http_client, "Connection", "Upgrade");
    dap_http_out_header_add(a_http_client, "Sec-WebSocket-Accept", l_accept_key);

    log_it(L_INFO, "WebSocket upgrade request accepted from %s",
           a_http_client->esocket->remote_addr_str);

    // Trigger response write
    dap_events_socket_set_writable_unsafe(a_http_client->esocket, true);
    dap_events_socket_set_readable_unsafe(a_http_client->esocket, false);
}

/**
 * @brief Write WebSocket upgrade response headers
 */
static bool s_websocket_upgrade_headers_write(dap_http_client_t *a_http_client, void *a_arg)
{
    if (!a_http_client) {
        return false;
    }

    // After sending 101 Switching Protocols, the connection is now WebSocket
    if (a_http_client->reply_status_code == 101) {
        log_it(L_INFO, "WebSocket upgrade complete, switching to WebSocket protocol");
        
        // Switch socket handlers to WebSocket read/write callbacks
        if (s_switch_to_websocket_protocol(a_http_client) != 0) {
            log_it(L_ERROR, "Failed to switch to WebSocket protocol");
            a_http_client->state_read = DAP_HTTP_CLIENT_STATE_NONE;
            return false;
        }
        
        // Connection is now WebSocket, keep it open for WebSocket frames
        a_http_client->state_read = DAP_HTTP_CLIENT_STATE_NONE;
        dap_events_socket_set_readable_unsafe(a_http_client->esocket, true);
        dap_events_socket_set_writable_unsafe(a_http_client->esocket, false);
        
        return true;
    }

    // For non-upgrade responses, close connection
    a_http_client->state_read = DAP_HTTP_CLIENT_STATE_NONE;
    return false;
}

/**
 * @brief Switch HTTP client to WebSocket protocol after successful Upgrade
 * 
 * Creates dap_stream_t from HTTP client and sets WebSocket transport.
 * This function is called after 101 Switching Protocols response is sent.
 * 
 * Note: WebSocket transport's private data is shared across all streams,
 * but each stream maintains its own connection state via esocket.
 */
static int s_switch_to_websocket_protocol(dap_http_client_t *a_http_client)
{
    if (!a_http_client || !a_http_client->esocket) {
        log_it(L_ERROR, "Invalid HTTP client");
        return -1;
    }

    // Get WebSocket transport
    dap_stream_transport_t *l_ws_transport = dap_stream_transport_find(DAP_STREAM_TRANSPORT_WEBSOCKET);
    if (!l_ws_transport) {
        log_it(L_ERROR, "WebSocket transport not registered");
        return -2;
    }

    // Check if stream already exists (from HTTP processing)
    dap_stream_t *l_stream = NULL;
    if (a_http_client->_inheritor) {
        // Check if inheritor is already a stream
        l_stream = (dap_stream_t*)a_http_client->_inheritor;
        // Verify it's actually a stream (has esocket field)
        if (l_stream && l_stream->esocket == a_http_client->esocket) {
            log_it(L_DEBUG, "Reusing existing stream for WebSocket upgrade");
        } else {
            l_stream = NULL;  // Not a valid stream, create new one
        }
    }

    // Create new stream if needed
    if (!l_stream) {
        // Use dap_stream_new_es_client to create stream from existing socket
        l_stream = dap_stream_new_es_client(a_http_client->esocket, NULL, false);
        if (!l_stream) {
            log_it(L_ERROR, "Failed to create stream from HTTP client");
            return -3;
        }
        
        // Store stream in HTTP client's inheritor
        a_http_client->_inheritor = l_stream;
    }

    // Set WebSocket transport for this stream
    l_stream->stream_transport = l_ws_transport;
    
    // Get WebSocket transport private data (shared across all streams)
    // Note: This is transport-level configuration, not stream-specific
    dap_stream_transport_ws_private_t *l_ws_priv = dap_stream_transport_ws_get_private(l_stream);
    if (l_ws_priv) {
        // Mark WebSocket as OPEN (handshake completed for this connection)
        l_ws_priv->state = DAP_WS_STATE_OPEN;
        
        // Store socket reference for this connection
        l_ws_priv->esocket = a_http_client->esocket;
        l_ws_priv->http_client = a_http_client;  // Keep reference for cleanup
        
        // Store accept key from headers (already calculated)
        dap_http_header_t *l_accept_header = dap_http_header_find(a_http_client->out_headers, "Sec-WebSocket-Accept");
        if (l_accept_header && !l_ws_priv->sec_websocket_accept) {
            l_ws_priv->sec_websocket_accept = dap_strdup(l_accept_header->value);
        }
    } else {
        log_it(L_WARNING, "WebSocket transport private data not initialized - transport may not be ready");
    }

    // Set socket callbacks for WebSocket read/write
    // Note: WebSocket read/write will be handled by transport layer
    // The socket callbacks remain HTTP-based, but transport layer handles WebSocket frames
    
    log_it(L_INFO, "Successfully switched to WebSocket protocol for stream %p (socket %p)", 
           l_stream, a_http_client->esocket);
    return 0;
}

/**
 * @brief Generate Sec-WebSocket-Accept key from client key (RFC 6455)
 * 
 * The server takes the value of Sec-WebSocket-Key header, concatenates
 * it with the GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", takes SHA-1
 * hash of the result, and base64-encodes it.
 */
static bool s_generate_accept_key(const char *a_client_key, char *a_accept_key, size_t a_accept_key_size)
{
    if (!a_client_key || !a_accept_key || a_accept_key_size < 29) {  // Base64(20 bytes) = 28 chars + null
        return false;
    }

    // Concatenate client key with WebSocket GUID
    char l_concat[256] = {0};
    snprintf(l_concat, sizeof(l_concat), "%s%s", a_client_key, WEBSOCKET_GUID);

    // Calculate SHA-1 hash using OpenSSL
    unsigned char l_hash[SHA_DIGEST_LENGTH];  // SHA-1 = 20 bytes
    SHA1((const unsigned char *)l_concat, strlen(l_concat), l_hash);

    // Base64 encode the hash
    size_t l_encoded_size = dap_enc_base64_encode(l_hash, SHA_DIGEST_LENGTH,
                                                    a_accept_key, DAP_ENC_DATA_TYPE_B64);
    if (l_encoded_size == 0) {
        log_it(L_ERROR, "Failed to base64 encode WebSocket accept key");
        return false;
    }

    a_accept_key[l_encoded_size] = '\0';

    log_it(L_DEBUG, "Generated Sec-WebSocket-Accept: %s", a_accept_key);
    return true;
}

