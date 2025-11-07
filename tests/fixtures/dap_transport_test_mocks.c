/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 *
 * This file is part of DAP the open source project
 *
 *    DAP is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file dap_transport_test_mocks.c
 * @brief Common mock implementations for transport unit tests
 */

#include "dap_transport_test_mocks.h"
#include "dap_http_client.h"
#include "dap_list.h"
#include "dap_events_socket.h"

// ============================================================================
// Common Mock Server Instances
// ============================================================================

static dap_server_t s_mock_server = {0};
static dap_http_server_t s_mock_http_server = {0};
static dap_http_client_t s_mock_http_client = {0};

dap_server_t* dap_transport_test_get_mock_server(void)
{
    return &s_mock_server;
}

dap_http_server_t* dap_transport_test_get_mock_http_server(void)
{
    return &s_mock_http_server;
}

dap_http_client_t* dap_transport_test_get_mock_http_client(void)
{
    return &s_mock_http_client;
}

// ============================================================================
// Common Mock Wrappers
// ============================================================================

// Wrapper for dap_server_new
DAP_MOCK_WRAPPER_CUSTOM(dap_server_t*, dap_server_new,
    PARAM(const char*, a_cfg_section),
    PARAM(dap_events_socket_callbacks_t*, a_server_callbacks),
    PARAM(dap_events_socket_callbacks_t*, a_client_callbacks)
)
{
    UNUSED(a_cfg_section);
    UNUSED(a_server_callbacks);
    UNUSED(a_client_callbacks);
    
    // Return mock server if set, otherwise return default mock
    if (g_mock_dap_server_new && g_mock_dap_server_new->return_value.ptr) {
        return (dap_server_t*)g_mock_dap_server_new->return_value.ptr;
    }
    
    return dap_transport_test_get_mock_server();
}

// Wrapper for dap_http_server_new
DAP_MOCK_WRAPPER_CUSTOM(dap_server_t*, dap_http_server_new,
    PARAM(const char*, a_cfg_section),
    PARAM(const char*, a_server_name)
)
{
    UNUSED(a_cfg_section);
    UNUSED(a_server_name);
    
    // Return mock server if set, otherwise return default mock
    dap_server_t *l_server = dap_transport_test_get_mock_server();
    if (g_mock_dap_http_server_new && g_mock_dap_http_server_new->return_value.ptr) {
        l_server = (dap_server_t*)g_mock_dap_http_server_new->return_value.ptr;
    }
    
    // Set _inheritor to point to mock HTTP server structure
    // This is required for DAP_HTTP_SERVER macro to work
    l_server->_inheritor = dap_transport_test_get_mock_http_server();
    
    return l_server;
}

// Wrapper for dap_server_listen_addr_add - create mock listener to satisfy dap_net_server_listen_addr_add_with_callback
DAP_MOCK_WRAPPER_CUSTOM(int, dap_server_listen_addr_add,
    PARAM(dap_server_t*, a_server),
    PARAM(const char*, a_addr),
    PARAM(uint16_t, a_port),
    PARAM(dap_events_desc_type_t, a_type),
    PARAM(dap_events_socket_callbacks_t*, a_callbacks)
)
{
    UNUSED(a_type);
    
    // Return mock value if set, otherwise return 0 (success)
    if (g_mock_dap_server_listen_addr_add && g_mock_dap_server_listen_addr_add->return_value.i != 0) {
        return g_mock_dap_server_listen_addr_add->return_value.i;
    }
    
    // Create a mock listener socket to satisfy dap_net_server_listen_addr_add_with_callback expectations
    // The real dap_net_server_listen_addr_add_with_callback checks if a_server->es_listeners contains a listener
    if (a_server && a_addr && a_callbacks) {
        dap_events_socket_t *l_mock_listener = DAP_NEW_Z(dap_events_socket_t);
        if (l_mock_listener) {
            // Set up minimal fields needed for the check in dap_net_server_listen_addr_add_with_callback
            dap_strncpy(l_mock_listener->listener_addr_str, a_addr, DAP_HOSTADDR_STRLEN - 1);
            l_mock_listener->listener_port = a_port;
            l_mock_listener->socket = INVALID_SOCKET; // Mock socket
            l_mock_listener->server = a_server;
            l_mock_listener->type = a_type;
            
            // Copy callbacks (required by real function check)
            l_mock_listener->callbacks = *a_callbacks;
            
            // Add to server's listeners list (matching real function behavior)
            a_server->es_listeners = dap_list_prepend(a_server->es_listeners, l_mock_listener);
        }
    }
    
    return 0;
}

// Mock wrapper for dap_server_delete - just verify it's called, don't actually delete
DAP_MOCK_WRAPPER_CUSTOM(void, dap_server_delete,
    PARAM(dap_server_t *, a_server)
)
{
    // Just verify the call, don't actually delete anything
    // In real implementation this would free the server, but in tests we use static mocks
    (void)a_server;
}

// Wrapper for dap_net_server_listen_addr_add_with_callback
// This wrapper intercepts calls and returns success without calling real function
DAP_MOCK_WRAPPER_CUSTOM(int, dap_net_server_listen_addr_add_with_callback,
    PARAM(dap_server_t*, a_server),
    PARAM(const char*, a_addr),
    PARAM(uint16_t, a_port),
    PARAM(dap_events_desc_type_t, a_type),
    PARAM(dap_net_server_pre_worker_added_callback_t, a_pre_worker_added),
    PARAM(void*, a_user_data)
)
{
    UNUSED(a_type);
    UNUSED(a_pre_worker_added);
    UNUSED(a_user_data);
    
    // Debug: verify mock is being called
    // fprintf(stderr, "MOCK CALLED: dap_net_server_listen_addr_add_with_callback(%s:%u)\n", a_addr ? a_addr : "NULL", a_port);
    
    // Always return success when wrapped (if we're here, --wrap worked)
    // Return configured value if set, otherwise return 0 (success)
    if (g_mock_dap_net_server_listen_addr_add_with_callback) {
        if (g_mock_dap_net_server_listen_addr_add_with_callback->return_value.i != 0) {
            return g_mock_dap_net_server_listen_addr_add_with_callback->return_value.i;
        }
    }
    
    // Create a mock listener socket to satisfy any code that checks es_listeners
    if (a_server && a_addr) {
        dap_events_socket_t *l_mock_listener = DAP_NEW_Z(dap_events_socket_t);
        if (l_mock_listener) {
            // Set up minimal fields
            dap_strncpy(l_mock_listener->listener_addr_str, a_addr, DAP_HOSTADDR_STRLEN - 1);
            l_mock_listener->listener_port = a_port;
            l_mock_listener->socket = INVALID_SOCKET; // Mock socket
            l_mock_listener->server = a_server;
            l_mock_listener->type = a_type;
            
            // Set a dummy accept callback (required by real function check)
            l_mock_listener->callbacks.accept_callback = (dap_events_socket_callback_accept_t)0x1; // Non-NULL dummy
            
            // Add to server's listeners list
            a_server->es_listeners = dap_list_prepend(a_server->es_listeners, l_mock_listener);
        }
    }
    
    return 0;
}

// Wrapper for enc_http_add_proc
DAP_MOCK_WRAPPER_CUSTOM(dap_http_url_proc_t*, enc_http_add_proc,
    PARAM(dap_http_server_t*, a_server),
    PARAM(const char*, a_url_path)
)
{
    UNUSED(a_server);
    UNUSED(a_url_path);
    
    // Return mock value if set, otherwise return NULL
    if (g_mock_enc_http_add_proc && g_mock_enc_http_add_proc->return_value.ptr) {
        return (dap_http_url_proc_t*)g_mock_enc_http_add_proc->return_value.ptr;
    }
    return NULL;
}

// Wrapper for dap_stream_add_proc_http
DAP_MOCK_WRAPPER_CUSTOM(dap_http_url_proc_t*, dap_stream_add_proc_http,
    PARAM(dap_http_server_t*, a_server),
    PARAM(const char*, a_url_path)
)
{
    UNUSED(a_server);
    UNUSED(a_url_path);
    
    // Return mock value if set, otherwise return NULL
    if (g_mock_dap_stream_add_proc_http && g_mock_dap_stream_add_proc_http->return_value.ptr) {
        return (dap_http_url_proc_t*)g_mock_dap_stream_add_proc_http->return_value.ptr;
    }
    return NULL;
}

// Wrapper for dap_http_client_new
DAP_MOCK_WRAPPER_CUSTOM(dap_http_client_t*, dap_http_client_new,
    PARAM(const char*, a_host),
    PARAM(uint16_t, a_port)
)
{
    UNUSED(a_host);
    UNUSED(a_port);
    
    // Return mock client if set, otherwise return default mock
    if (g_mock_dap_http_client_new && g_mock_dap_http_client_new->return_value.ptr) {
        return (dap_http_client_t*)g_mock_dap_http_client_new->return_value.ptr;
    }
    
    return dap_transport_test_get_mock_http_client();
}

// Wrapper for dap_http_client_delete
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_http_client_delete, (dap_http_client_t *a_client), (a_client));

// Wrapper for dap_http_client_write
DAP_MOCK_WRAPPER_CUSTOM(ssize_t, dap_http_client_write,
    PARAM(dap_http_client_t*, a_client),
    PARAM(const void*, a_data),
    PARAM(size_t, a_size)
)
{
    UNUSED(a_client);
    UNUSED(a_data);
    
    // Return mock value if set, otherwise return size (success)
    if (g_mock_dap_http_client_write && g_mock_dap_http_client_write->return_value.i != 0) {
        return (ssize_t)g_mock_dap_http_client_write->return_value.i;
    }
    return (ssize_t)a_size;
}

// Wrapper for dap_http_init
DAP_MOCK_WRAPPER_CUSTOM(int, dap_http_init,
    void
)
{
    // Return mock value if set, otherwise return 0 (success)
    if (g_mock_dap_http_init && g_mock_dap_http_init->return_value.i != 0) {
        return g_mock_dap_http_init->return_value.i;
    }
    return 0;
}

// Wrapper for dap_http_deinit
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(dap_http_deinit, (), ());

// Wrapper for enc_http_init
DAP_MOCK_WRAPPER_CUSTOM(int, enc_http_init,
    void
)
{
    // Return mock value if set, otherwise return 0 (success)
    if (g_mock_enc_http_init && g_mock_enc_http_init->return_value.i != 0) {
        return g_mock_enc_http_init->return_value.i;
    }
    return 0;
}

// Wrapper for enc_http_deinit
DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(enc_http_deinit, (), ());

// Wrapper for dap_stream_ctl_add_proc
DAP_MOCK_WRAPPER_CUSTOM(dap_http_url_proc_t*, dap_stream_ctl_add_proc,
    PARAM(dap_http_server_t*, a_server),
    PARAM(const char*, a_url_path)
)
{
    UNUSED(a_server);
    UNUSED(a_url_path);
    
    // Return mock value if set, otherwise return NULL
    if (g_mock_dap_stream_ctl_add_proc && g_mock_dap_stream_ctl_add_proc->return_value.ptr) {
        return (dap_http_url_proc_t*)g_mock_dap_stream_ctl_add_proc->return_value.ptr;
    }
    return NULL;
}

