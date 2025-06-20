/*
 * Authors:
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net

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

#include "dap_http2_session.h"
#include "dap_http2_stream.h"
#include "dap_common.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_http2_session"

// Session lifecycle
dap_http2_session_t *dap_http2_session_create(dap_worker_t *a_worker)
{
    // TODO: Implement session creation
    UNUSED(a_worker);
    return NULL;
}

int dap_http2_session_connect(dap_http2_session_t *a_session, 
                              const char *a_addr, 
                              uint16_t a_port, 
                              bool a_use_ssl)
{
    // TODO: Implement connection establishment
    UNUSED(a_session);
    UNUSED(a_addr);
    UNUSED(a_port);
    UNUSED(a_use_ssl);
    return -1;
}

void dap_http2_session_close(dap_http2_session_t *a_session)
{
    // TODO: Implement session close
    UNUSED(a_session);
}

void dap_http2_session_delete(dap_http2_session_t *a_session)
{
    // TODO: Implement session deletion
    UNUSED(a_session);
}

// Configuration
void dap_http2_session_set_timeouts(dap_http2_session_t *a_session,
                                    uint64_t a_connect_timeout_ms,
                                    uint64_t a_read_timeout_ms)
{
    // TODO: Implement timeout configuration
    UNUSED(a_session);
    UNUSED(a_connect_timeout_ms);
    UNUSED(a_read_timeout_ms);
}

void dap_http2_session_set_callbacks(dap_http2_session_t *a_session,
                                     const dap_http2_session_callbacks_t *a_callbacks,
                                     void *a_callbacks_arg)
{
    // TODO: Implement callback configuration
    UNUSED(a_session);
    UNUSED(a_callbacks);
    UNUSED(a_callbacks_arg);
}

// Data operations
int dap_http2_session_send(dap_http2_session_t *a_session, const void *a_data, size_t a_size)
{
    // TODO: Implement data sending
    UNUSED(a_session);
    UNUSED(a_data);
    UNUSED(a_size);
    return -1;
}

// State queries
dap_http2_session_state_t dap_http2_session_get_state(const dap_http2_session_t *a_session)
{
    // TODO: Implement state query
    UNUSED(a_session);
    return DAP_HTTP2_SESSION_STATE_IDLE;
}

bool dap_http2_session_is_connected(const dap_http2_session_t *a_session)
{
    // TODO: Implement connection check
    UNUSED(a_session);
    return false;
}

bool dap_http2_session_is_error(const dap_http2_session_t *a_session)
{
    // TODO: Implement error check
    UNUSED(a_session);
    return false;
}

// Stream association
void dap_http2_session_set_stream(dap_http2_session_t *a_session, dap_http2_stream_t *a_stream)
{
    // TODO: Implement stream association
    UNUSED(a_session);
    UNUSED(a_stream);
}

dap_http2_stream_t *dap_http2_session_get_stream(const dap_http2_session_t *a_session)
{
    // TODO: Implement stream retrieval
    UNUSED(a_session);
    return NULL;
}

/**
 * @brief dap_http2_session_add_stream
 * @param a_session
 * @param a_stream
 * @return
 */
int dap_http2_session_add_stream(dap_http2_session_t *a_session, dap_http2_stream_t *a_stream)
{
    // TODO: Implement stream addition to session
    UNUSED(a_session);
    UNUSED(a_stream);
    return -1;
}

/**
 * @brief dap_http2_session_remove_stream
 * @param a_session
 * @param a_stream
 */
void dap_http2_session_remove_stream(dap_http2_session_t *a_session, dap_http2_stream_t *a_stream)
{
    // TODO: Implement stream removal from session
    UNUSED(a_session);
    UNUSED(a_stream);
}

/**
 * @brief dap_http2_session_find_stream
 * @param a_session
 * @param a_stream_id
 * @return
 */
dap_http2_stream_t *dap_http2_session_find_stream(const dap_http2_session_t *a_session, uint32_t a_stream_id)
{
    // TODO: Implement stream search by ID
    UNUSED(a_session);
    UNUSED(a_stream_id);
    return NULL;
}

/**
 * @brief dap_http2_session_get_streams_count
 * @param a_session
 * @return
 */
size_t dap_http2_session_get_streams_count(const dap_http2_session_t *a_session)
{
    // TODO: Implement streams count retrieval
    UNUSED(a_session);
    return 0;
}

/**
 * @brief dap_http2_session_set_routing_mode
 * @param a_session
 * @param a_mode
 */
void dap_http2_session_set_routing_mode(dap_http2_session_t *a_session, dap_stream_routing_mode_t a_mode)
{
    // TODO: Implement routing mode setting
    UNUSED(a_session);
    UNUSED(a_mode);
}

/**
 * @brief dap_http2_session_get_routing_mode
 * @param a_session
 * @return
 */
dap_stream_routing_mode_t dap_http2_session_get_routing_mode(const dap_http2_session_t *a_session)
{
    // TODO: Implement routing mode retrieval
    UNUSED(a_session);
    return DAP_STREAM_ROUTING_SEQUENTIAL;
}

dap_http2_session_t *dap_http2_session_create_from_socket(dap_worker_t *a_worker, 
                                                          SOCKET a_client_socket)
{
    // TODO: Implement server session creation from accepted socket
    return NULL;
}

bool dap_http2_session_is_client_mode(const dap_http2_session_t *a_session)
{
    // TODO: Check if session has connect_timer (client mode)
    return false;
}

bool dap_http2_session_is_server_mode(const dap_http2_session_t *a_session)
{
    // TODO: Check if session has no connect_timer (server mode)  
    return false;
}

int dap_http2_session_get_remote_addr(const dap_http2_session_t *a_session,
                                      char *a_addr_buf, 
                                      size_t a_buf_size)
{
    // TODO: Extract address from a_session->es->addr_storage
    return -1;
}

uint16_t dap_http2_session_get_remote_port(const dap_http2_session_t *a_session)
{
    // TODO: Extract port from a_session->es->addr_storage
    return 0;
}

time_t dap_http2_session_get_last_activity(const dap_http2_session_t *a_session)
{
    // TODO: Return a_session->es->last_time_active
    return 0;
} 