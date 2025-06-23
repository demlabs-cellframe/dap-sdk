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
#include "dap_net.h"
#include "dap_context.h"

#define LOG_TAG "dap_http2_session"

// Forward declarations for callback functions
static void s_session_connected_callback(dap_events_socket_t *a_esocket);
static void s_session_read_callback(dap_events_socket_t *a_esocket, void *a_data, size_t a_data_size);
static void s_session_error_callback(dap_events_socket_t *a_esocket, int a_error);
static void s_session_delete_callback(dap_events_socket_t *a_esocket, void *a_arg);
static bool s_session_connect_timeout_callback(void *a_arg);

#ifndef DAP_NET_CLIENT_NO_SSL
static void s_session_ssl_connected_callback(dap_events_socket_t *a_esocket);
#endif

// Session lifecycle
dap_http2_session_t *dap_http2_session_create(dap_worker_t *a_worker)
{
    if (!a_worker) {
        log_it(L_ERROR, "Invalid worker parameter for session creation");
        return NULL;
    }

    // Allocate session structure
    dap_http2_session_t *l_session = DAP_NEW_Z(dap_http2_session_t);
    if (!l_session) {
        log_it(L_CRITICAL, "Failed to allocate memory for HTTP2 session");
        return NULL;
    }

    // Initialize session
    l_session->worker = a_worker;
    l_session->state = DAP_HTTP2_SESSION_STATE_IDLE;
    l_session->is_ssl = false;
    l_session->ts_created = time(NULL);
    l_session->ts_established = 0;
    l_session->current_stream = NULL;
    l_session->next_stream_id = 1;  // Client streams start with odd numbers
    
    // Initialize timers to NULL
    l_session->connect_timer = NULL;
    l_session->read_timer = NULL;
    
    // Initialize callbacks
    memset(&l_session->callbacks, 0, sizeof(l_session->callbacks));
    l_session->callbacks_arg = NULL;
    
    // Socket will be created later in connect()
    l_session->es = NULL;

    log_it(L_DEBUG, "Created HTTP2 session %p on worker %u", l_session, a_worker->id);
    
    return l_session;
}

int dap_http2_session_connect(dap_http2_session_t *a_session, 
                              const char *a_addr, 
                              uint16_t a_port, 
                              bool a_use_ssl)
{
    if (!a_session || !a_addr) {
        log_it(L_ERROR, "Invalid parameters for session connect");
        return -EINVAL;
    }

    if (a_session->state != DAP_HTTP2_SESSION_STATE_IDLE) {
        log_it(L_ERROR, "Session is not in IDLE state, cannot connect");
        return -EBUSY;
    }

    log_it(L_DEBUG, "Connecting HTTP2 session %p to %s:%u (SSL: %s)", 
           a_session, a_addr, a_port, a_use_ssl ? "yes" : "no");

    // Create socket
#ifdef DAP_OS_WINDOWS
    SOCKET l_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (l_socket == INVALID_SOCKET) {
        int l_error = WSAGetLastError();
        log_it(L_ERROR, "Socket create error: %d", l_error);
        return -l_error;
    }
#else
    int l_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (l_socket == -1) {
        int l_error = errno;
        log_it(L_ERROR, "Error %d with socket create", l_error);
        return -l_error;
    }
#endif

    // Set socket non-blocking
#ifdef DAP_OS_WINDOWS
    u_long l_socket_flags = 1;
    if (ioctlsocket(l_socket, FIONBIO, &l_socket_flags)) {
        int l_error = WSAGetLastError();
        log_it(L_ERROR, "Error setting socket non-blocking: %d", l_error);
        closesocket(l_socket);
        return -l_error;
    }
#else
    int l_socket_flags = fcntl(l_socket, F_GETFL);
    if (l_socket_flags == -1) {
        int l_error = errno;
        log_it(L_ERROR, "Error %d can't get socket flags", l_error);
        close(l_socket);
        return -l_error;
    }
    if (fcntl(l_socket, F_SETFL, l_socket_flags | O_NONBLOCK) == -1) {
        int l_error = errno;
        log_it(L_ERROR, "Error %d can't set socket non-blocking", l_error);
        close(l_socket);
        return -l_error;
    }
#endif

    // Setup socket callbacks (will be defined later)
    static dap_events_socket_callbacks_t l_session_callbacks = {
        .connected_callback = s_session_connected_callback,
        .read_callback = s_session_read_callback,
        .error_callback = s_session_error_callback,
        .delete_callback = s_session_delete_callback
    };

    // Wrap socket in events socket
    dap_events_socket_t *l_ev_socket = dap_events_socket_wrap_no_add(l_socket, &l_session_callbacks);
    if (!l_ev_socket) {
        log_it(L_ERROR, "Failed to wrap socket in events socket");
#ifdef DAP_OS_WINDOWS
        closesocket(l_socket);
#else
        close(l_socket);
#endif
        return -ENOMEM;
    }

    // Link session to socket
    l_ev_socket->_inheritor = a_session;
    a_session->es = l_ev_socket;
    a_session->is_ssl = a_use_ssl;

    // Resolve address
    if (dap_net_resolve_host(a_addr, dap_itoa(a_port), false, 
                            &l_ev_socket->addr_storage, NULL) < 0) {
        log_it(L_ERROR, "Failed to resolve host '%s:%u'", a_addr, a_port);
        dap_events_socket_delete_unsafe(l_ev_socket, true);
        a_session->es = NULL;
        return -EHOSTUNREACH;
    }

    // Set remote address info
    dap_strncpy(l_ev_socket->remote_addr_str, a_addr, INET6_ADDRSTRLEN - 1);
    l_ev_socket->remote_port = a_port;

    // Setup socket for connection
    l_ev_socket->flags |= DAP_SOCK_CONNECTING;
    l_ev_socket->type = DESCRIPTOR_TYPE_SOCKET_CLIENT;

    // Set SSL callback if needed
    if (a_use_ssl) {
#ifndef DAP_NET_CLIENT_NO_SSL
        l_ev_socket->callbacks.connected_callback = s_session_ssl_connected_callback;
#else
        log_it(L_ERROR, "SSL requested but SSL support is disabled");
        dap_events_socket_delete_unsafe(l_ev_socket, true);
        a_session->es = NULL;
        return -ENOTSUP;
#endif
    }

    // Change session state
    a_session->state = DAP_HTTP2_SESSION_STATE_CONNECTING;

    // Attempt connection
#ifdef DAP_EVENTS_CAPS_IOCP
    // Windows IOCP path
    l_ev_socket->flags |= DAP_SOCK_READY_TO_WRITE;
    dap_worker_add_events_socket(a_session->worker, l_ev_socket);
    
    // Setup connect timeout
    dap_events_socket_uuid_t *l_ev_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
    if (l_ev_uuid_ptr) {
        *l_ev_uuid_ptr = l_ev_socket->uuid;
        a_session->connect_timer = dap_timerfd_start_on_worker(
            a_session->worker, 30000, // 30 second timeout
            s_session_connect_timeout_callback, l_ev_uuid_ptr);
        if (!a_session->connect_timer) {
            log_it(L_WARNING, "Failed to start connect timer");
            DAP_DELETE(l_ev_uuid_ptr);
        }
    }
    return 0;
#else
    // Unix/Linux path
    l_ev_socket->flags |= DAP_SOCK_READY_TO_WRITE;
    int l_connect_result = connect(l_socket, (struct sockaddr *)&l_ev_socket->addr_storage, 
                                  sizeof(struct sockaddr_in));
    
    if (l_connect_result == 0) {
        // Connected immediately
        log_it(L_DEBUG, "Connected immediately to %s:%u", a_addr, a_port);
        dap_worker_add_events_socket(a_session->worker, l_ev_socket);
        if (a_use_ssl) {
#ifndef DAP_NET_CLIENT_NO_SSL
            s_session_ssl_connected_callback(l_ev_socket);
#endif
        } else {
            s_session_connected_callback(l_ev_socket);
        }
        return 0;
    }
#ifdef DAP_OS_WINDOWS
    else if (l_connect_result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
#else
    else if (l_connect_result == -1 && errno == EINPROGRESS) {
#endif
        // Connection in progress
        log_it(L_DEBUG, "Connection to %s:%u in progress", a_addr, a_port);
        dap_worker_add_events_socket(a_session->worker, l_ev_socket);
        
        // Setup connect timeout
        dap_events_socket_uuid_t *l_ev_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
        if (l_ev_uuid_ptr) {
            *l_ev_uuid_ptr = l_ev_socket->uuid;
            a_session->connect_timer = dap_timerfd_start_on_worker(
                a_session->worker, 30000, // 30 second timeout
                s_session_connect_timeout_callback, l_ev_uuid_ptr);
            if (!a_session->connect_timer) {
                log_it(L_WARNING, "Failed to start connect timer");
                DAP_DELETE(l_ev_uuid_ptr);
            }
        }
        return 0;
    } else {
        // Connection failed
#ifdef DAP_OS_WINDOWS
        int l_error = WSAGetLastError();
#else
        int l_error = errno;
#endif
        log_it(L_ERROR, "Connect failed: %d (\"%s\")", l_error, dap_strerror(l_error));
        a_session->state = DAP_HTTP2_SESSION_STATE_ERROR;
        dap_events_socket_delete_unsafe(l_ev_socket, true);
        a_session->es = NULL;
        return -l_error;
    }
#endif
}

void dap_http2_session_close(dap_http2_session_t *a_session)
{
    if (!a_session) {
        return;
    }

    log_it(L_DEBUG, "Closing HTTP2 session %p", a_session);

    // Update state if not already in error
    if (a_session->state != DAP_HTTP2_SESSION_STATE_ERROR) {
        a_session->state = DAP_HTTP2_SESSION_STATE_CLOSING;
    }

    // Clean up timers
    if (a_session->connect_timer) {
        if (a_session->connect_timer->callback_arg) {
            DAP_DELETE(a_session->connect_timer->callback_arg);
        }
        dap_timerfd_delete_unsafe(a_session->connect_timer);
        a_session->connect_timer = NULL;
    }

    if (a_session->read_timer) {
        if (a_session->read_timer->callback_arg) {
            DAP_DELETE(a_session->read_timer->callback_arg);
        }
        dap_timerfd_delete_unsafe(a_session->read_timer);
        a_session->read_timer = NULL;
    }

    // Close socket
    if (a_session->es) {
        a_session->es->flags |= DAP_SOCK_SIGNAL_CLOSE;
        // Note: socket will be deleted by events system, which will call our delete callback
    }
}

void dap_http2_session_delete(dap_http2_session_t *a_session)
{
    if (!a_session) {
        return;
    }

    log_it(L_DEBUG, "Deleting HTTP2 session %p", a_session);

    // Close session first
    dap_http2_session_close(a_session);

    // Clean up current stream
    if (a_session->current_stream) {
        dap_http2_stream_delete(a_session->current_stream);
        a_session->current_stream = NULL;
    }

    // Clean up socket (should already be done in close, but safety)
    if (a_session->es) {
        a_session->es->_inheritor = NULL;
        dap_events_socket_delete_unsafe(a_session->es, true);
        a_session->es = NULL;
    }

    // Free session structure
    DAP_DELETE(a_session);
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

const char* dap_http2_session_error_to_str(dap_http2_session_error_t a_error)
{
    switch (a_error) {
        case DAP_HTTP2_SESSION_ERROR_NONE:           return "None";
        case DAP_HTTP2_SESSION_ERROR_CONNECT_TIMEOUT: return "Connect timeout";
        case DAP_HTTP2_SESSION_ERROR_READ_TIMEOUT:   return "Read timeout";
        case DAP_HTTP2_SESSION_ERROR_NETWORK:        return "Network error";
        case DAP_HTTP2_SESSION_ERROR_SSL:            return "SSL error";
        case DAP_HTTP2_SESSION_ERROR_RESOLVE:        return "DNS resolve error";
        default:                                     return "Unknown";
    }
}

// === CALLBACK FUNCTIONS ===

/**
 * @brief Called when socket connection is established
 */
static void s_session_connected_callback(dap_events_socket_t *a_esocket)
{
    if (!a_esocket || !a_esocket->_inheritor) {
        log_it(L_ERROR, "Invalid arguments in session connected callback");
        return;
    }

    dap_http2_session_t *l_session = (dap_http2_session_t *)a_esocket->_inheritor;
    
    log_it(L_INFO, "HTTP2 session %p connected to %s:%u", 
           l_session, a_esocket->remote_addr_str, a_esocket->remote_port);

    // Clean up connect timer
    if (l_session->connect_timer) {
        if (l_session->connect_timer->callback_arg) {
            DAP_DELETE(l_session->connect_timer->callback_arg);
        }
        dap_timerfd_delete_unsafe(l_session->connect_timer);
        l_session->connect_timer = NULL;
    }

    // Update session state
    l_session->state = DAP_HTTP2_SESSION_STATE_CONNECTED;
    l_session->ts_established = time(NULL);

    // Setup read timeout timer
    dap_events_socket_uuid_t *l_ev_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
    if (l_ev_uuid_ptr) {
        *l_ev_uuid_ptr = a_esocket->uuid;
        l_session->read_timer = dap_timerfd_start_on_worker(
            l_session->worker, 60000, // 60 second read timeout
            s_session_connect_timeout_callback, l_ev_uuid_ptr);
        if (!l_session->read_timer) {
            log_it(L_WARNING, "Failed to start read timer");
            DAP_DELETE(l_ev_uuid_ptr);
        }
    }

    // Call user callback if set
    if (l_session->callbacks.connected) {
        l_session->callbacks.connected(l_session);
    }
}

/**
 * @brief Called when data is received from socket
 */
static void s_session_read_callback(dap_events_socket_t *a_esocket, void *a_data, size_t a_data_size)
{
    if (!a_esocket || !a_esocket->_inheritor || !a_data || a_data_size == 0) {
        log_it(L_ERROR, "Invalid arguments in session read callback");
        return;
    }

    dap_http2_session_t *l_session = (dap_http2_session_t *)a_esocket->_inheritor;
    
    log_it(L_DEBUG, "HTTP2 session %p received %zu bytes", l_session, a_data_size);

    // Call user callback if set
    if (l_session->callbacks.data_received) {
        l_session->callbacks.data_received(l_session, a_data, a_data_size);
    }

    // Reset read timer
    if (l_session->read_timer) {
        dap_timerfd_reset(l_session->read_timer);
    }
}

/**
 * @brief Called when socket error occurs
 */
static void s_session_error_callback(dap_events_socket_t *a_esocket, int a_error)
{
    if (!a_esocket || !a_esocket->_inheritor) {
        log_it(L_ERROR, "Invalid arguments in session error callback");
        return;
    }

    dap_http2_session_t *l_session = (dap_http2_session_t *)a_esocket->_inheritor;
    
    log_it(L_ERROR, "HTTP2 session %p socket error: %d (\"%s\")", 
           l_session, a_error, dap_strerror(a_error));

    // Update session state
    l_session->state = DAP_HTTP2_SESSION_STATE_ERROR;

    // Call user callback if set
    if (l_session->callbacks.error) {
        dap_http2_session_error_t l_session_error = DAP_HTTP2_SESSION_ERROR_NETWORK;
        // Map socket errors to session errors
        switch (a_error) {
            case ETIMEDOUT:
                l_session_error = DAP_HTTP2_SESSION_ERROR_READ_TIMEOUT;
                break;
            case EHOSTUNREACH:
            case ENETUNREACH:
                l_session_error = DAP_HTTP2_SESSION_ERROR_RESOLVE;
                break;
            default:
                l_session_error = DAP_HTTP2_SESSION_ERROR_NETWORK;
                break;
        }
        l_session->callbacks.error(l_session, l_session_error);
    }
}

/**
 * @brief Called when socket is being deleted
 */
static void s_session_delete_callback(dap_events_socket_t *a_esocket, void *a_arg)
{
    UNUSED(a_arg);
    
    if (!a_esocket || !a_esocket->_inheritor) {
        return;
    }

    dap_http2_session_t *l_session = (dap_http2_session_t *)a_esocket->_inheritor;
    
    log_it(L_DEBUG, "HTTP2 session %p socket being deleted", l_session);

    // Update session state
    if (l_session->state != DAP_HTTP2_SESSION_STATE_ERROR) {
        l_session->state = DAP_HTTP2_SESSION_STATE_CLOSED;
    }

    // Clear socket reference
    l_session->es = NULL;

    // Call user callback if set
    if (l_session->callbacks.closed) {
        l_session->callbacks.closed(l_session);
    }
}

/**
 * @brief Connect timeout callback
 */
static bool s_session_connect_timeout_callback(void *a_arg)
{
    if (!a_arg) {
        return false;
    }

    dap_events_socket_uuid_t *l_ev_uuid_ptr = (dap_events_socket_uuid_t *)a_arg;
    dap_events_socket_t *l_esocket = dap_context_find(dap_worker_get_current()->context, *l_ev_uuid_ptr);
    
    DAP_DELETE(l_ev_uuid_ptr);

    if (!l_esocket || !l_esocket->_inheritor) {
        return false;
    }

    dap_http2_session_t *l_session = (dap_http2_session_t *)l_esocket->_inheritor;
    
    log_it(L_WARNING, "HTTP2 session %p connect timeout", l_session);

    // Update session state
    l_session->state = DAP_HTTP2_SESSION_STATE_ERROR;

    // Close socket
    l_esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;

    // Call user error callback
    if (l_session->callbacks.error) {
        l_session->callbacks.error(l_session, DAP_HTTP2_SESSION_ERROR_CONNECT_TIMEOUT);
    }

    return false; // Don't repeat timer
}

#ifndef DAP_NET_CLIENT_NO_SSL
/**
 * @brief SSL connection callback
 */
static void s_session_ssl_connected_callback(dap_events_socket_t *a_esocket)
{
    // TODO: Implement SSL handshake logic
    // For now, just call regular connected callback
    log_it(L_DEBUG, "SSL handshake initiated for session");
    s_session_connected_callback(a_esocket);
}
#endif 