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

// === PRIVATE DATA STRUCTURE ===
// Hidden from public API - true encapsulation
typedef struct dap_http2_session_private {
    // === CONNECTION MANAGEMENT ===
    dap_events_socket_t *es;              // Contains sockaddr_storage
    dap_worker_t *worker;
    
    // === ENCRYPTION (unified) ===
    dap_session_encryption_type_t encryption_type;
    void *encryption_context;
    
    // === CONNECTION TIMEOUTS ===
    dap_timerfd_t *connect_timer;             // Connect timeout timer
    uint64_t connect_timeout_ms;              // Connect timeout value
    
    // === UNIVERSAL SESSION STATE ===
    time_t ts_created;
    time_t ts_established;                // connect() or accept() time
    
    // === SINGLE STREAM MANAGEMENT ===
    dap_http2_stream_callbacks_t *stream_callbacks;
    
    // === FACTORY PATTERN SUPPORT ===
    void *worker_assignment_context;        // For assigned_to_worker callback only
    
} dap_http2_session_private_t;

// === USER IDEA V2 CONSTANTS ===
// Computed once at module load time for optimal performance
static const size_t s_session_pvt_alignof = _Alignof(dap_http2_session_private_t),
    s_session_pvt_offt = (sizeof(dap_http2_session_t) + s_session_pvt_alignof - 1) & ~(s_session_pvt_alignof - 1),
    s_session_full_size = s_session_pvt_offt + sizeof(dap_http2_session_private_t);

// Forward declarations for callback functions
static void s_session_connected_callback(dap_events_socket_t *a_esocket);
static void s_session_read_callback(dap_events_socket_t *a_esocket, void *a_data, size_t a_data_size);
static void s_session_worker_assign_callback(dap_events_socket_t *a_esocket, dap_worker_t *a_worker);
static void s_session_error_callback(dap_events_socket_t *a_esocket, int a_error);
static void s_session_delete_callback(dap_events_socket_t *a_esocket, void *a_arg);
static bool s_session_connect_timeout_callback(void *a_arg);

#ifndef DAP_NET_CLIENT_NO_SSL
static void s_session_ssl_connected_callback(dap_events_socket_t *a_esocket);
#endif

// Session lifecycle
dap_http2_session_t *dap_http2_session_create(dap_worker_t *a_worker, uint64_t a_connect_timeout_ms)
{
    // USER IDEA V2: Single block allocation with optimal alignment
    dap_http2_session_t *l_session = DAP_NEW_Z_SIZE(dap_http2_session_t, s_session_full_size);
    if (!l_session) {
        return NULL;
    }
    
    // USER IDEA V2: private_data points to offset from STRUCTURE  
    l_session->private_data = (dap_http2_session_private_t*)((uint8_t*)l_session + s_session_pvt_offt);
    
    // Initialize private data
    dap_http2_session_private_t *l_private = l_session->private_data;
    *l_private = (dap_http2_session_private_t){ 
        .worker = a_worker ? a_worker : dap_worker_get_auto(), 
        .ts_created = time(NULL), 
        .connect_timeout_ms = a_connect_timeout_ms ? a_connect_timeout_ms : 30000
    };
    
    log_it(L_DEBUG, "Created HTTP2 session %p on worker %u (timeout: %llu ms)", 
           l_session, l_private->worker->id, l_private->connect_timeout_ms);
    return l_session;
}

dap_http2_session_t *dap_http2_session_create_default(dap_worker_t *a_worker)
{
    return dap_http2_session_create(a_worker, 0); // 0 = use default timeout
}

int dap_http2_session_connect(dap_http2_session_t *a_session, 
                              const char *a_addr, 
                              uint16_t a_port, 
                              bool a_use_ssl)
{
    dap_return_val_if_fail_err( a_session && a_addr,
        -EINVAL, "Invalid parameters for session connect" );
    
    // Get private data
    dap_http2_session_private_t *l_private = a_session->private_data;
    if (!l_private) {
        return -EINVAL;
    }
    
    log_it(L_DEBUG, "Connecting HTTP2 session %p to %s:%u (SSL: %s)",
                    a_session, a_addr, a_port, a_use_ssl ? "enabled" : "disabled");
#ifdef DAP_NET_CLIENT_NO_SSL
    if ( a_use_ssl )
        return log_it(L_ERROR, "SSL requested but SSL support is disabled"), -ENOTSUP;
#endif
    struct sockaddr_storage l_addr_storage = { };
    int l_addrlen = dap_net_resolve_host(a_addr, dap_itoa(a_port), false, &l_addr_storage, NULL);
    if ( l_addrlen < 0 )
        return log_it(L_ERROR, "Failed to resolve host '%s : %u'", a_addr, a_port), -EHOSTUNREACH;
    int l_error = 0;

#ifdef DAP_OS_WINDOWS
#define m_set_error(a_error) do { a_error = WSAGetLastError(); } while (0)
#else
#define m_set_error(a_error) do { a_error = errno; } while (0)
#endif

#ifdef DAP_OS_WINDOWS
    SOCKET l_socket;
#else
    int l_socket;
#endif
    l_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if ( l_socket == INVALID_SOCKET ) {
        m_set_error(l_error);
        return log_it(L_ERROR, "socket() error %d: %s", l_error, dap_strerror(l_error)), -l_error;
    }

#ifdef DAP_OS_WINDOWS
    u_long l_socket_flags = 1;
    if ( ioctlsocket(l_socket, FIONBIO, &l_socket_flags) ) {
        l_error = WSAGetLastError();
        const char *l_fun = "ioctlsocket()"; 
#else
    int l_socket_flags = fcntl(l_socket, F_GETFL);
    if ( l_socket_flags == -1 || fcntl(l_socket, F_SETFL, l_socket_flags | O_NONBLOCK) == -1 ) {
        l_error = errno;
        const char *l_fun = "fcntl()";
#endif
        closesocket(l_socket);
        return log_it(L_ERROR, "%s error %d: %s", l_fun, l_error, dap_strerror(l_error)), -l_error;
    }
    static dap_events_socket_callbacks_t l_session_callbacks = {
        .connected_callback = 
#ifndef DAP_NET_CLIENT_NO_SSL
            a_use_ssl ? s_session_ssl_connected_callback : s_session_connected_callback,
#else
            s_session_connected_callback,
#endif
        .read_callback = s_session_read_callback,
        .error_callback = s_session_error_callback,
        .delete_callback = s_session_delete_callback,
        .worker_assign_callback = s_session_worker_assign_callback
    };

    dap_events_socket_t *l_ev_socket = dap_events_socket_wrap_no_add(l_socket, &l_session_callbacks);
    if ( !l_ev_socket ) {
        closesocket(l_socket);
        return log_it(L_ERROR, "Failed to wrap socket in events socket"), -ENOMEM;
    }

    l_ev_socket->_inheritor = a_session;
    l_private->es = l_ev_socket;

    l_ev_socket->addr_storage = l_addr_storage;
    l_private->encryption_type = a_use_ssl ? DAP_SESSION_ENCRYPTION_TLS : DAP_SESSION_ENCRYPTION_NONE;
    dap_strncpy(l_ev_socket->remote_addr_str, a_addr, INET6_ADDRSTRLEN - 1);
    l_ev_socket->remote_port = a_port;
    l_ev_socket->flags |= DAP_SOCK_CONNECTING;
    l_ev_socket->type = DESCRIPTOR_TYPE_SOCKET_CLIENT;

    // Connection
#ifdef DAP_EVENTS_CAPS_IOCP
    // Windows IOCP approach
    log_it(L_DEBUG, "Connecting to %s:%u", a_addr, a_port);
    l_ev_socket->flags &= ~DAP_SOCK_READY_TO_READ;
    l_ev_socket->flags |= DAP_SOCK_READY_TO_WRITE;
    dap_worker_add_events_socket(l_private->worker, l_ev_socket);
#else
    // Unix/Linux approach
    l_ev_socket->flags |= DAP_SOCK_READY_TO_WRITE;
    if ( 0 == connect(l_socket, (struct sockaddr*)&l_ev_socket->addr_storage, l_addrlen) ) {
        log_it(L_DEBUG, "Connected immediately to %s:%u", a_addr, a_port);
        dap_worker_add_events_socket(l_private->worker, l_ev_socket);
        dap_worker_exec_callback_on(l_private->worker, l_session_callbacks.connected_callback, l_ev_socket);
        return 0;
    }
    m_set_error(l_error);
    switch (l_error) {
#ifdef DAP_OS_WINDOWS
    case WSAEWOULDBLOCK:
#else
    case EINPROGRESS:
#endif
        // Connection in progress
        log_it(L_DEBUG, "Connecting to %s:%u ...", a_addr, a_port);
        dap_worker_add_events_socket(l_private->worker, l_ev_socket);
        break;
    default:
        log_it(L_ERROR, "Connection to %s:%u failed, error %d: \"%s\"", a_addr, a_port, l_error, dap_strerror(l_error));
        dap_events_socket_delete_unsafe(l_ev_socket, true);
        l_private->es = NULL;
        return -l_error;
    }
#endif

    // TODO: remove this crappy timer and implement timerfd / QueueTimer handle in events system
    dap_events_socket_uuid_t *l_ev_uuid_ptr = DAP_DUP(&l_ev_socket->uuid);
    if (l_ev_uuid_ptr) {
        l_private->connect_timer = dap_timerfd_start_on_worker(
            l_private->worker, l_private->connect_timeout_ms,
            s_session_connect_timeout_callback, l_ev_uuid_ptr);
        if (!l_private->connect_timer) {
            log_it(L_WARNING, "Failed to start connect timer");
            DAP_DELETE(l_ev_uuid_ptr);
        }
    }
    return 0;
}

void dap_http2_session_close(dap_http2_session_t *a_session)
{
    if (!a_session) {
        return;
    }

    log_it(L_DEBUG, "Closing HTTP2 session %p", a_session);

    // Get private data
    dap_http2_session_private_t *l_private = a_session->private_data;
    if (!l_private) {
        return;
    }

    // Clean up timers
    if (l_private->connect_timer) {
        if (l_private->connect_timer->callback_arg) {
            DAP_DELETE(l_private->connect_timer->callback_arg);
        }
        dap_timerfd_delete_unsafe(l_private->connect_timer);
        l_private->connect_timer = NULL;
    }
    dap_events_socket_remove_and_delete_unsafe(l_private->es, true);
    l_private->es = NULL;
    // TODO: Clean up read timer on stream level

    /*if (a_session->es) {
        a_session->es->flags |= DAP_SOCK_SIGNAL_CLOSE;
        // Note: socket will be deleted by events system, which will call our delete callback
    }*/
}

void dap_http2_session_delete(dap_http2_session_t *a_session)
{
    if (!a_session) {
        return;
    }

    log_it(L_DEBUG, "Deleting HTTP2 session %p", a_session);

    // Close session first
    dap_http2_session_close(a_session);

    // Clean up stream
    if (a_session->stream) {
        dap_http2_stream_delete(a_session->stream);
        a_session->stream = NULL;
    }

    // Clean up socket (should already be done in close, but safety)
    dap_http2_session_private_t *l_private = a_session->private_data;
    if (l_private && l_private->es) {
        l_private->es->_inheritor = NULL;
        dap_events_socket_delete_unsafe(l_private->es, true);
        l_private->es = NULL;
    }

    // Free session structure
    DAP_DELETE(a_session);
}

// Configuration
void dap_http2_session_set_connect_timeout(dap_http2_session_t *a_session,
                                           uint64_t a_connect_timeout_ms)
{
    if (!a_session) {
        return;
    }
    dap_http2_session_private_t *l_private = a_session->private_data;
    if (l_private) {
        l_private->connect_timeout_ms = a_connect_timeout_ms;
    }
}

uint64_t dap_http2_session_get_connect_timeout(const dap_http2_session_t *a_session)
{
    if (!a_session) {
        return 0;
    }
    dap_http2_session_private_t *l_private = a_session->private_data;
    return l_private ? l_private->connect_timeout_ms : 0;
}

void dap_http2_session_set_callbacks(dap_http2_session_t *a_session,
                                     const dap_http2_session_callbacks_t *a_callbacks,
                                     void *a_callbacks_arg)
{
    if (!a_session) {
        log_it(L_ERROR, "Invalid arguments to set session callbacks");
        return;
    }

    if (a_callbacks) {
        a_session->callbacks = *a_callbacks;
    } else {
        memset(&a_session->callbacks, 0, sizeof(a_session->callbacks));
    }
    a_session->callbacks_arg = a_callbacks_arg;

    log_it(L_DEBUG, "Session %p callbacks set. Connected cb: %p",
           a_session, a_session->callbacks.connected);
}

int dap_http2_session_upgrade(dap_http2_session_t *a_session,
                             const dap_session_upgrade_context_t *a_upgrade_context)
{
    if (!a_session || !a_upgrade_context) {
        log_it(L_ERROR, "Invalid parameters for session upgrade");
        return -1;
    }
    
    // TODO: Implementation - upgrade encryption and callback
    log_it(L_DEBUG, "Upgrading session %p with encryption type %d", 
           a_session, a_upgrade_context->encryption_type);
    UNUSED(a_upgrade_context);
    return -1;
}

// Data operations
int dap_http2_session_send(dap_http2_session_t *a_session, const void *a_data, size_t a_size)
{
    if (!a_session) {
        log_it(L_ERROR, "Invalid session in dap_http2_session_send");
        return -1;
    }
    
    dap_http2_session_private_t *l_private = a_session->private_data;
    if (!l_private || !l_private->es) {
        log_it(L_ERROR, "Invalid private data or esocket in dap_http2_session_send");
        return -1;
    }
    
    if (!a_data || a_size == 0) {
        log_it(L_WARNING, "Empty data in dap_http2_session_send");
        return 0;
    }
    
    // Send data through session esocket
    size_t l_bytes_sent = dap_events_socket_write_unsafe(l_private->es, a_data, a_size);
    if (l_bytes_sent != a_size) {
        log_it(L_ERROR, "Failed to send all data: %zu/%zu bytes sent", l_bytes_sent, a_size);
        return -1;
    }
    
    log_it(L_DEBUG, "Sent %zu bytes through HTTP2 session", a_size);
    return (int)l_bytes_sent;
}

/**
 * @brief Get direct write buffer info for zero-copy operations
 * @param a_session Session instance
 * @param a_write_ptr Output: pointer to write position in buffer
 * @param a_size_ptr Output: pointer to current buffer size (for direct increment)
 * @param a_available_space Output: available space in buffer
 * @return 0 on success, negative on error
 */
int dap_http2_session_get_write_buffer_info(dap_http2_session_t *a_session, 
                                           void **a_write_ptr, 
                                           size_t **a_size_ptr, 
                                           size_t *a_available_space)
{
    if (!a_session || !a_write_ptr || !a_size_ptr || !a_available_space) {
        log_it(L_ERROR, "Invalid arguments in dap_http2_session_get_write_buffer_info");
        return -1;
    }
    
    dap_http2_session_private_t *l_private = a_session->private_data;
    if (!l_private || !l_private->es) {
        log_it(L_ERROR, "Invalid private data or esocket in dap_http2_session_get_write_buffer_info");
        return -1;
    }
    
    dap_events_socket_t *l_es = l_private->es;
    
    // Return direct pointers to buffer info
    *a_write_ptr = l_es->buf_out + l_es->buf_out_size;      // Write position
    *a_size_ptr = &l_es->buf_out_size;                      // Size pointer for direct increment
    *a_available_space = l_es->buf_out_size_max - l_es->buf_out_size; // Available space
    
    return 0;
}

/**
 * @brief UNIVERSAL WRITE FUNCTION (NEW ARCHITECTURE)
 * Single point for all write operations through stream callbacks
 * @param a_session Session instance (stream obtained from session)
 * @return Number of bytes written or 0 on error
 */
size_t dap_http2_session_write_direct_stream(dap_http2_session_t *a_session)
{
    if (!a_session) {
        log_it(L_ERROR, "Invalid session in dap_http2_session_write_direct_stream");
        return 0;
    }
    
    dap_http2_session_private_t *l_private = a_session->private_data;
    if (!l_private || !l_private->es) {
        log_it(L_ERROR, "Invalid private data or esocket in dap_http2_session_write_direct_stream");
        return 0;
    }
    
    // Get stream from session
    dap_http2_stream_t *l_stream = dap_http2_session_get_stream(a_session);
    if (!l_stream) {
        log_it(L_ERROR, "Session has no stream");
        return 0;
    }
    
    if (!l_stream->callbacks.write_cb) {
        log_it(L_ERROR, "Stream has no write callback");
        return 0;
    }
    
    dap_events_socket_t *l_es = l_private->es;
    
    // === PROTECTION CONSTANTS ===
    const int MAX_RETRIES = 5;
    const size_t MAX_REASONABLE_REQUEST_SIZE = 1024 * 1024; // 1MB
    
    int l_retry_count = 0;
    size_t l_temp_buffer_size = 0;
    void *l_temp_buffer = NULL;
    
    // === PHASE 1: Zero-Copy Attempt ===
    
    // Try socket buffer first (zero-copy)
    size_t l_available_space = l_es->buf_out_size_max - l_es->buf_out_size;
    void *l_write_ptr = l_es->buf_out + l_es->buf_out_size;
    
    log_it(L_DEBUG, "Attempting zero-copy write (available: %zu bytes)", l_available_space);
    
    ssize_t l_result = l_stream->callbacks.write_cb(l_stream, l_write_ptr, l_available_space, l_stream->callback_context);
    
    if (l_result > 0) {
        // SUCCESS: Zero-copy worked
        l_es->buf_out_size += (size_t)l_result;
        dap_events_socket_set_writable_unsafe(l_es, true);
        
        log_it(L_DEBUG, "Zero-copy success: %zd bytes written directly to socket buffer", l_result);
        return (size_t)l_result;
        
    } else if (l_result < 0) {
        // ERROR: Formatting failed
        log_it(L_ERROR, "Stream write callback failed with error: %zd", l_result);
        return 0;
        
    } else {
        // l_result == 0: Need more space
        log_it(L_DEBUG, "Zero-copy failed: need larger buffer (available was %zu)", l_available_space);
    }
    
    // === PHASE 2: Retry with Dynamic Buffer ===
    
    // Start with double the socket buffer size (reasonable first guess)
    l_temp_buffer_size = l_es->buf_out_size_max * 2;
    
    while (l_retry_count < MAX_RETRIES) {
        
        // Protection: Check reasonable size limit
        if (l_temp_buffer_size > MAX_REASONABLE_REQUEST_SIZE) {
            log_it(L_ERROR, "Temporary buffer size %zu exceeds reasonable limit %zu", 
                   l_temp_buffer_size, MAX_REASONABLE_REQUEST_SIZE);
            break;
        }
        
        // Allocate temporary buffer
        l_temp_buffer = DAP_NEW_Z_SIZE(uint8_t, l_temp_buffer_size);
        if (!l_temp_buffer) {
            log_it(L_ERROR, "Failed to allocate temporary buffer of size %zu", l_temp_buffer_size);
            break;
        }
        
        log_it(L_DEBUG, "Retry #%d: trying with %zu byte temp buffer", l_retry_count + 1, l_temp_buffer_size);
        
        // Try write callback with temporary buffer
        l_result = l_stream->callbacks.write_cb(l_stream, l_temp_buffer, l_temp_buffer_size, l_stream->callback_context);
        
        if (l_result > 0) {
            // SUCCESS: Copy from temp buffer to socket
            size_t l_written = (size_t)l_result;
            
            // Copy to socket buffer (may require multiple writes)
            size_t l_copied = dap_events_socket_write_unsafe(l_es, l_temp_buffer, l_written);
            
            if (l_copied == l_written) {
                log_it(L_DEBUG, "Retry success: %zu bytes written via temporary buffer", l_written);
                DAP_DELETE(l_temp_buffer);
                return l_written;
            } else {
                log_it(L_ERROR, "Failed to copy all data from temp buffer: %zu/%zu bytes", 
                       l_copied, l_written);
                DAP_DELETE(l_temp_buffer);
                return 0;
            }
            
        } else if (l_result < 0) {
            // ERROR: Formatting failed
            log_it(L_ERROR, "Stream write callback failed on retry #%d with error: %zd", 
                   l_retry_count + 1, l_result);
            DAP_DELETE(l_temp_buffer);
            return 0;
            
        } else {
            // l_result == 0: Still need more space
            log_it(L_DEBUG, "Retry #%d: still need more space (tried %zu bytes)", 
                   l_retry_count + 1, l_temp_buffer_size);
            
            DAP_DELETE(l_temp_buffer);
            l_temp_buffer = NULL;
            
            // Exponential backoff for next attempt
            l_temp_buffer_size *= 2;
            l_retry_count++;
        }
    }
    
    // === PHASE 3: Error Handling ===
    
    // Clean up any remaining temp buffer
    if (l_temp_buffer) {
        DAP_DELETE(l_temp_buffer);
    }
    
    if (l_retry_count >= MAX_RETRIES) {
        log_it(L_ERROR, "Write failed after %d retries (max buffer tried: %zu bytes)", 
               MAX_RETRIES, l_temp_buffer_size / 2);
    } else {
        log_it(L_ERROR, "Write failed due to memory allocation or size limits");
    }
    
    return 0;
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

// Single stream management
dap_http2_stream_t *dap_http2_session_create_stream(dap_http2_session_t *a_session, const dap_http2_stream_callbacks_t *a_callbacks)
{
    if (!a_session) {
        log_it(L_ERROR, "Session is NULL, cannot create stream");
        return NULL;
    }
    
    if (a_session->stream) {
        log_it(L_WARNING, "Session %p already has a stream", a_session);
        return a_session->stream;
    }
    
    log_it(L_DEBUG, "Creating single stream for session %p", a_session);
    
    dap_http2_stream_t *l_stream = dap_http2_stream_create(a_session);
    if (!l_stream) {
        log_it(L_CRITICAL, "Failed to create stream for session %p", a_session);
        return NULL;
    }
    
    // Set protocol callbacks for the stream
    if (a_callbacks) {
        l_stream->callbacks = *a_callbacks;
    }
    l_stream->callback_context = a_session->callbacks_arg;
    
    a_session->stream = l_stream;
    
    log_it(L_INFO, "Stream %p created and linked to session %p", l_stream, a_session);
    
    return l_stream;
}

dap_http2_stream_t *dap_http2_session_get_stream(const dap_http2_session_t *a_session)
{
    if (!a_session) {
        return NULL;
    }
    return a_session->stream;
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

    // Get private data for timer cleanup
    dap_http2_session_private_t *l_private = l_session->private_data;
    if (!l_private) {
        log_it(L_ERROR, "Session connected callback: no private data");
        return;
    }
    
    // Clean up connect timer
    if (l_private->connect_timer) {
        if (l_private->connect_timer->callback_arg) {
            DAP_DELETE(l_private->connect_timer->callback_arg);
        }
        dap_timerfd_delete_unsafe(l_private->connect_timer);
        l_private->connect_timer = NULL;
    }

    l_private->ts_established = time(NULL);

    // TODO: Setup read timeout timer on stream level

    // Call user callback if set
    if (l_session->callbacks.connected)
        l_session->callbacks.connected(l_session);
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

    // Forward data to stream for processing (NEW ARCHITECTURE)
    if (l_session->stream) {
        size_t l_processed = dap_http2_stream_process_data(l_session->stream, a_data, a_data_size);
        log_it(L_DEBUG, "Stream processed %zu/%zu bytes", l_processed, a_data_size);
    } else {
        log_it(L_DEBUG, "No stream to forward data to");
    }

    // TODO: Reset read timer on stream level
}

// === STREAM CREATION HELPER ===

/**
 * @brief Called when socket is assigned to a worker
 */
static void s_session_worker_assign_callback(dap_events_socket_t *a_esocket, UNUSED_ARG dap_worker_t *a_worker)
{
    if (!a_esocket || !a_esocket->_inheritor) {
        log_it(L_ERROR, "Invalid arguments in session worker assign callback");
        return;
    }

    dap_http2_session_t *l_session = (dap_http2_session_t*)a_esocket->_inheritor;
    
    // Get private data for stream callbacks
    dap_http2_session_private_t *l_private = l_session->private_data;
    if (!l_private) {
        log_it(L_ERROR, "Session worker assign callback: no private data");
        return;
    }
    
    dap_http2_stream_t *l_stream = dap_http2_session_create_stream(l_session, l_private->stream_callbacks);
    if (!l_stream) {
        log_it(L_ERROR, "Failed to create stream!");
        return;
    }
    DAP_DEL_Z(l_private->stream_callbacks);
    if (l_session->callbacks.assigned)
        l_session->callbacks.assigned(l_session);
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

    // Clear socket reference in private data
    dap_http2_session_private_t *l_private = l_session->private_data;
    if (l_private) {
        l_private->es = NULL;
    }

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

size_t dap_http2_session_process_data(dap_http2_session_t *a_session, 
                                      const void *a_data, 
                                      size_t a_size)
{
    // TODO: Process incoming session data
    UNUSED(a_session);
    UNUSED(a_data);
    return a_size;
}

dap_session_encryption_type_t dap_http2_session_get_encryption_type(const dap_http2_session_t *a_session)
{
    if (!a_session) {
        return DAP_SESSION_ENCRYPTION_NONE;
    }
    dap_http2_session_private_t *l_private = a_session->private_data;
    return l_private ? l_private->encryption_type : DAP_SESSION_ENCRYPTION_NONE;
}

// === PRIVATE DATA ACCESS FUNCTIONS (for internal use) ===

/**
 * @brief Get events socket (internal use only)
 */
dap_events_socket_t* dap_http2_session_get_events_socket(const dap_http2_session_t *a_session)
{
    if (!a_session) {
        return NULL;
    }
    dap_http2_session_private_t *l_private = a_session->private_data;
    return l_private ? l_private->es : NULL;
}

/**
 * @brief Get worker (internal use only)
 */
dap_worker_t* dap_http2_session_get_worker(const dap_http2_session_t *a_session)
{
    if (!a_session) {
        return NULL;
    }
    dap_http2_session_private_t *l_private = a_session->private_data;
    return l_private ? l_private->worker : NULL;
}

dap_session_upgrade_interface_t* dap_http2_session_get_upgrade_interface(dap_http2_session_t *a_session)
{
    // TODO: Return upgrade interface for stream communication
    UNUSED(a_session);
    return NULL;
}
