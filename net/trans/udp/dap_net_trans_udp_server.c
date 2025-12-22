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
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_config.h"
#include "dap_net_trans.h"
#include "dap_net_trans_udp_server.h"
#include "dap_net_trans_udp_stream.h"
#include "dap_stream.h"
#include "dap_net_trans_server.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "uthash.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_net_trans_udp_server"

// Forward declaration from dap_net_trans_udp_stream.c
// This function is made non-static to allow server-side stream initialization
dap_net_trans_udp_ctx_t *s_get_or_create_udp_ctx(dap_stream_t *a_stream);

// Debug flags
static bool s_debug_more = false;  // Extra verbose debugging

// Helper to generate unique UUID for virtual esockets
static inline dap_events_socket_uuid_t dap_events_socket_uuid_generate(void) {
    dap_events_socket_uuid_t l_uuid = 0;
    randombytes((uint8_t*)&l_uuid, sizeof(l_uuid));
    return l_uuid;
}

/**
 * @brief Write callback for virtual UDP esockets
 * 
 * This callback handles write operations for virtual esockets by performing
 * sendto() directly on the physical socket with the client's address.
 * 
 * @param a_es Virtual esocket
 * @param a_arg Pointer to the physical listener socket
 * @return true if data was sent successfully, false otherwise
 */
static bool s_virtual_esocket_write_callback(dap_events_socket_t *a_es, void *a_arg) {
    if (!a_es || !a_es->buf_out_size) {
        return true; // Nothing to write
    }
    
    // Get physical listener socket from arg
    dap_events_socket_t *l_listener = (dap_events_socket_t *)a_arg;
    if (!l_listener) {
        log_it(L_ERROR, "Virtual esocket write: no listener socket");
        return false;
    }
    
    // Send data using sendto with client's address from virtual esocket
    ssize_t l_sent = sendto(l_listener->socket, 
                           (const char *)a_es->buf_out, 
                           a_es->buf_out_size, 
                           0,
                           (struct sockaddr *)&a_es->addr_storage, 
                           a_es->addr_size);
    
    if (l_sent < 0) {
        int l_errno = errno;
        log_it(L_ERROR, "Virtual esocket sendto failed: %s (errno %d)", strerror(l_errno), l_errno);
        return false;
    }
    
    if ((size_t)l_sent < a_es->buf_out_size) {
        log_it(L_WARNING, "Virtual esocket partial send: %zd of %zu bytes", l_sent, a_es->buf_out_size);
        // Shift remaining data
        memmove(a_es->buf_out, a_es->buf_out + l_sent, a_es->buf_out_size - l_sent);
        a_es->buf_out_size -= l_sent;
        return false; // Will retry
    }
    
    debug_if(s_debug_more, L_DEBUG, "Virtual esocket sent %zd bytes via sendto", l_sent);
    a_es->buf_out_size = 0;
    return true;
}

// UDP session mapping structure for server-side demultiplexing
// NEW ARCHITECTURE: No virtual esockets! One physical esocket dispatches to multiple streams.
typedef struct udp_session_entry {
    // Hash key: remote address (IP:port) - uniquely identifies client
    struct sockaddr_storage remote_addr; // Client address (HASH KEY)
    socklen_t remote_addr_len;         // Address length
    
    // Stream associated with this client
    dap_stream_t *stream;              // Associated dap_stream_t instance (NO virtual esocket!)
    uint64_t session_id;               // Session ID from handshake
    
    // Activity tracking
    time_t last_activity;              // Last packet timestamp
    
    // uthash handle (hash by remote_addr)
    UT_hash_handle hh;                 
} udp_session_entry_t;

/**
 * @brief Compare two sockaddr_storage structures for hash table lookup
 * 
 * Compares IP address and port, supporting both IPv4 and IPv6.
 * Used by uthash to find sessions by remote address.
 * 
 * @param a First address
 * @param b Second address
 * @return true if addresses match, false otherwise
 */
static inline bool s_sockaddr_equal(const struct sockaddr_storage *a, const struct sockaddr_storage *b)
{
    if (a->ss_family != b->ss_family)
        return false;
    
    if (a->ss_family == AF_INET) {
        struct sockaddr_in *a4 = (struct sockaddr_in*)a;
        struct sockaddr_in *b4 = (struct sockaddr_in*)b;
        return (a4->sin_port == b4->sin_port) && 
               (a4->sin_addr.s_addr == b4->sin_addr.s_addr);
    } else if (a->ss_family == AF_INET6) {
        struct sockaddr_in6 *a6 = (struct sockaddr_in6*)a;
        struct sockaddr_in6 *b6 = (struct sockaddr_in6*)b;
        return (a6->sin6_port == b6->sin6_port) &&
               (memcmp(&a6->sin6_addr, &b6->sin6_addr, sizeof(struct in6_addr)) == 0);
    }
    
    return false;
}

/**
 * @brief Find session by remote address in hash table
 * 
 * @param a_server UDP server instance
 * @param a_remote_addr Client address to lookup
 * @return Session entry or NULL if not found
 */
static udp_session_entry_t* s_find_session_by_addr(dap_net_trans_udp_server_t *a_server, 
                                                     const struct sockaddr_storage *a_remote_addr)
{
    udp_session_entry_t *l_session, *l_tmp;
    HASH_ITER(hh, a_server->sessions, l_session, l_tmp) {
        if (s_sockaddr_equal(&l_session->remote_addr, a_remote_addr)) {
            return l_session;
        }
    }
    return NULL;
}


/**
 * @brief Create virtual UDP esocket for session
 * 
 * Creates a virtual esocket that shares the physical socket FD with the listener,
 * but has its own buffers and remote address storage. This allows multiple UDP
 * sessions to coexist on a single listener socket.
 * 
 * @param a_listener_es Listener socket to share FD with
 * @param a_remote_addr Client address for this virtual socket
 * @param a_remote_addr_len Client address length
 * @return Virtual esocket or NULL on error
 */
static dap_events_socket_t *s_create_virtual_udp_esocket(
    dap_events_socket_t *a_listener_es,
    struct sockaddr_storage *a_remote_addr,
    socklen_t a_remote_addr_len)
{
    if (!a_listener_es || !a_remote_addr) {
        log_it(L_ERROR, "Invalid arguments for virtual esocket creation");
        return NULL;
    }
    
    // Allocate virtual esocket
    dap_events_socket_t *l_virtual_es = DAP_NEW_Z(dap_events_socket_t);
    if (!l_virtual_es) {
        log_it(L_CRITICAL, "Failed to allocate virtual UDP esocket");
        return NULL;
    }
    
    // Share physical socket FD with listener
    l_virtual_es->socket = a_listener_es->socket;
    l_virtual_es->fd = a_listener_es->fd;
    l_virtual_es->type = DESCRIPTOR_TYPE_SOCKET_UDP;
    
    // SHARED BUFFER ARCHITECTURE:
    // Virtual esockets for encrypted stream data read directly from shared buffer
    // Do NOT allocate buf_in - it will be temporarily pointed to shared buffer regions
    // This prevents double-free and enables zero-copy multi-worker architecture
    l_virtual_es->buf_in = NULL;  // Will be set temporarily during packet processing
    l_virtual_es->buf_in_size = 0;
    l_virtual_es->buf_in_size_max = 0;  // Flag: buf_in not owned by this esocket
    
    // Allocate buf_out for responses (owned by virtual esocket)
    l_virtual_es->buf_out_size_max = DAP_EVENTS_SOCKET_BUF_SIZE;
    l_virtual_es->buf_out = DAP_NEW_Z_SIZE(byte_t, l_virtual_es->buf_out_size_max);
    
    if (!l_virtual_es->buf_out) {
        log_it(L_CRITICAL, "Failed to allocate buf_out for virtual esocket");
        DAP_DELETE(l_virtual_es);
        return NULL;
    }
    
    l_virtual_es->buf_out_size = 0;
    
    // Store remote address
    memcpy(&l_virtual_es->addr_storage, a_remote_addr, a_remote_addr_len);
    l_virtual_es->addr_size = a_remote_addr_len;
    
    // Copy context and server references from listener
    l_virtual_es->context = a_listener_es->context;
    l_virtual_es->worker = a_listener_es->worker;
    l_virtual_es->server = a_listener_es->server;
    
    // Set flags (ready to read/write, but don't close physical socket)
    l_virtual_es->flags = DAP_SOCK_READY_TO_READ | DAP_SOCK_READY_TO_WRITE;
    l_virtual_es->no_close = true; // CRITICAL: don't close shared socket
    
    // Initialize timestamps
    l_virtual_es->last_time_active = time(NULL);
    l_virtual_es->time_connection = l_virtual_es->last_time_active;
    
    // Initialize callbacks (will be set by stream)
    memset(&l_virtual_es->callbacks, 0, sizeof(l_virtual_es->callbacks));
    
    // Set custom write callback to handle UDP sendto
    l_virtual_es->callbacks.write_callback = s_virtual_esocket_write_callback;
    l_virtual_es->callbacks.arg = a_listener_es; // Pass listener socket as arg
    
    // Generate unique UUID
    l_virtual_es->uuid = dap_events_socket_uuid_generate();
    
    debug_if(s_debug_more, L_DEBUG, "Created virtual UDP esocket %p (uuid 0x%016" DAP_UINT64_FORMAT_X ") sharing socket %d",
           l_virtual_es, l_virtual_es->uuid, l_virtual_es->socket);
    
    return l_virtual_es;
}

/**
 * @brief Listener esocket creation callback - initializes shared buffer
 * 
 * Called when physical UDP listener socket is created and added to worker.
 * Sets up shared buffer infrastructure to allow zero-copy reads from multiple 
 * virtual esockets.
 * 
 * @param a_es Listener esocket (physical UDP socket)
 * @param a_arg Unused (always NULL from dap_worker)
 */
static void s_listener_new_callback(dap_events_socket_t *a_es, void *a_arg)
{
    UNUSED(a_arg);
    
    if (!a_es || !a_es->server) {
        log_it(L_ERROR, "Invalid esocket or server in listener new callback");
        return;
    }
    
    dap_net_trans_udp_server_t *l_udp_srv = DAP_NET_TRANS_UDP_SERVER(a_es->server);
    if (!l_udp_srv) {
        log_it(L_ERROR, "No UDP server in server->_inheritor");
        return;
    }
    
    // Initialize shared buffer using listener's buf_in
    pthread_rwlock_wrlock(&l_udp_srv->shared_buf_lock);
    
    if (!l_udp_srv->listener_es) {
        l_udp_srv->listener_es = a_es;
        l_udp_srv->shared_buf = a_es->buf_in;
        l_udp_srv->shared_buf_capacity = a_es->buf_in_size_max;
        
        debug_if(s_debug_more, L_DEBUG, 
               "Listener new callback: initialized shared buffer (listener_es=%p, capacity=%zu)", 
               a_es, l_udp_srv->shared_buf_capacity);
        
        // NEW ARCHITECTURE: Store listener esocket in trans for write dispatcher
        if (l_udp_srv->trans && l_udp_srv->trans->_inheritor) {
            dap_stream_trans_udp_private_t *l_priv = 
                (dap_stream_trans_udp_private_t*)l_udp_srv->trans->_inheritor;
            l_priv->listener_esocket = a_es;
            debug_if(s_debug_more, L_DEBUG, 
                   "Stored listener esocket in trans for write dispatcher");
        }
    }
    
    pthread_rwlock_unlock(&l_udp_srv->shared_buf_lock);
}

/**
 * @brief UDP server read callback - demultiplexes incoming UDP packets
 * 
 * This callback processes incoming UDP datagrams on the server listener socket.
 * It parses the UDP trans header, identifies or creates the corresponding stream,
 * and dispatches the packet for processing.
 * 
 * Packet flow:
 * 1. Parse UDP trans header (dap_stream_trans_udp_header_t)
 * 2. Extract session_id and packet type
 * 3. Lookup or create dap_stream_t for this session
 * 4. Process based on packet type:
 *    - HANDSHAKE: Handle encryption handshake
 *    - SESSION_CREATE: Create new session
 *    - DATA: Forward to dap_stream_data_proc_read
 * 5. Update session activity timestamp
 */
static void s_udp_server_read_callback(dap_events_socket_t *a_es, void *a_arg) {
    (void)a_arg;
    if (!a_es || !a_es->buf_in_size || !a_es->server)
        return;
    
    // Get UDP server instance from listener socket
    dap_net_trans_udp_server_t *l_udp_srv = DAP_NET_TRANS_UDP_SERVER(a_es->server);
    if (!l_udp_srv) {
        log_it(L_ERROR, "No UDP server instance for listener socket");
        a_es->buf_in_size = 0;
        return;
    }
    
    // Shared buffer should already be initialized by s_listener_new_callback
    if (!l_udp_srv->listener_es || !l_udp_srv->shared_buf) {
        log_it(L_ERROR, "Shared buffer not initialized (listener_es=%p, shared_buf=%p)",
               l_udp_srv->listener_es, l_udp_srv->shared_buf);
        a_es->buf_in_size = 0;
        return;
    }
    
    // Lock shared buffer for reading
    pthread_rwlock_rdlock(&l_udp_srv->shared_buf_lock);
    l_udp_srv->shared_buf_size = a_es->buf_in_size;
    
    debug_if(s_debug_more, L_DEBUG, "UDP server received %zu bytes on socket %d (shared buffer)", 
           l_udp_srv->shared_buf_size, a_es->socket);
    
    // Check if we have at least a UDP header
    if (a_es->buf_in_size < sizeof(dap_stream_trans_udp_header_t)) {
        log_it(L_WARNING, "UDP packet too small (%zu bytes), dropping", a_es->buf_in_size);
        a_es->buf_in_size = 0;
        return;
    }
    
    // First, try to find existing session by address
    // If session exists with encryption key AND packet is not a valid control packet,
    // this is encrypted stream data
    udp_session_entry_t *l_session = NULL, *l_tmp = NULL;
    bool l_session_found = false;
    
    pthread_rwlock_rdlock(&l_udp_srv->sessions_lock);
    HASH_ITER(hh, l_udp_srv->sessions, l_session, l_tmp) {
        // Match by remote address
        if (l_session->remote_addr_len == a_es->addr_size &&
            memcmp(&l_session->remote_addr, &a_es->addr_storage, a_es->addr_size) == 0) {
            l_session_found = true;
            break;
        }
    }
    // Check if this could be a control packet (version byte = 1)
    dap_stream_trans_udp_header_t *l_header = (dap_stream_trans_udp_header_t*)a_es->buf_in;
    uint8_t l_version = l_header->version;
    bool l_looks_like_control = (l_version == 1);
    
    // NEW ARCHITECTURE: If we have active session with encryption key AND packet doesn't look like control packet,
    // treat as encrypted stream data and dispatch to stream
    if (l_session_found && l_session && l_session->stream && 
        l_session->stream->session && l_session->stream->session->key &&
        !l_looks_like_control) {
        
        debug_if(s_debug_more, L_DEBUG, "Dispatching encrypted stream data (%zu bytes) to stream %p", 
               l_udp_srv->shared_buf_size, l_session->stream);
        
        // CRITICAL: Keep sessions_lock as READ lock during stream access
        dap_stream_t *l_stream = l_session->stream;
        
        // Dispatch encrypted data to stream for processing
        // Stream will decrypt and process channel packets internally
        if (l_stream->trans && l_stream->trans->ops && l_stream->trans->ops->read) {
            // Temporarily set trans_ctx->esocket to listener for address info and buf_in access
            dap_events_socket_t *l_saved_es = NULL;
            if (l_stream->trans_ctx) {
                l_saved_es = l_stream->trans_ctx->esocket;
                l_stream->trans_ctx->esocket = a_es;
            }
            
            // Stream read will consume from a_es->buf_in (shared buffer)
            ssize_t l_read = l_stream->trans->ops->read(l_stream, NULL, 0);
            
            // Restore original esocket
            if (l_stream->trans_ctx) {
                l_stream->trans_ctx->esocket = l_saved_es;
            }
            
            debug_if(s_debug_more, L_DEBUG, "Stream processed %zd bytes of encrypted data", l_read);
        } else {
            log_it(L_ERROR, "Stream has no trans read method for encrypted data");
        }
        
        // Release locks
        pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
        pthread_rwlock_unlock(&l_udp_srv->shared_buf_lock);
        
        // Clear listener buffer
        a_es->buf_in_size = 0;
        return;
    }
    
    // Release shared buffer lock (control packets don't use shared buffer)
    pthread_rwlock_unlock(&l_udp_srv->shared_buf_lock);
    
    // No active session with key OR packet looks like control - parse as control packet
    // Release read locks first as control packet processing doesn't need them
    pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
    pthread_rwlock_unlock(&l_udp_srv->shared_buf_lock);
    uint8_t l_type = l_header->type;
    uint16_t l_payload_len = ntohs(l_header->length);
    uint32_t l_seq_num = ntohl(l_header->seq_num);
    uint64_t l_session_id = be64toh(l_header->session_id);
    
    debug_if(s_debug_more, L_DEBUG, "UDP control packet: ver=%u type=%u len=%u seq=%u session=0x%lx", 
           l_version, l_type, l_payload_len, l_seq_num, l_session_id);
    
    // Validate version
    if (l_version != 1) {
        log_it(L_WARNING, "Invalid UDP control packet version %u (expected 1), dropping", l_version);
        a_es->buf_in_size = 0;
        return;
    }
    
    // Check if we have full packet
    size_t l_total_size = sizeof(dap_stream_trans_udp_header_t) + l_payload_len;
    if (a_es->buf_in_size < l_total_size) {
        log_it(L_WARNING, "Incomplete UDP packet (%zu < %zu), dropping", a_es->buf_in_size, l_total_size);
        a_es->buf_in_size = 0;
        return;
    }
    
    // Extract payload pointer
    uint8_t *l_payload = a_es->buf_in + sizeof(dap_stream_trans_udp_header_t);
    
    // Lookup or create session for control packets
    // NEW ARCHITECTURE: Hash by remote_addr, not session_id!
    // CRITICAL: Keep sessions_lock as READ lock during entire packet processing
    // This prevents cleanup from deleting session->stream while we access it
    l_session = NULL;
    pthread_rwlock_rdlock(&l_udp_srv->sessions_lock);
    l_session = s_find_session_by_addr(l_udp_srv, &a_es->addr_storage);
    
    // For HANDSHAKE, we need to create a new session
    // SESSION_CREATE must use existing session created by HANDSHAKE
    // Note: Creating new session requires write lock
    if (!l_session && l_type == DAP_STREAM_UDP_PKT_HANDSHAKE) {
        // Upgrade to write lock for session creation
        pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
        pthread_rwlock_wrlock(&l_udp_srv->sessions_lock);
        log_it(L_INFO, "Creating new UDP session 0x%lx for HANDSHAKE from remote addr", l_session_id);
        
        // Create new session entry
        l_session = DAP_NEW_Z(udp_session_entry_t);
        if (!l_session) {
            log_it(L_CRITICAL, "Failed to allocate UDP session entry");
            pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
            a_es->buf_in_size = 0;
            return;
        }
        
        l_session->session_id = l_session_id;
        l_session->last_activity = time(NULL);
        
        // Store client address (from recvfrom in dap_context.c)
        // For UDP listener sockets, remote address is stored in addr_storage during recvfrom
        if (a_es->addr_size > 0) {
            memcpy(&l_session->remote_addr, &a_es->addr_storage, 
                   a_es->addr_size < sizeof(l_session->remote_addr) ? 
                   a_es->addr_size : sizeof(l_session->remote_addr));
            l_session->remote_addr_len = a_es->addr_size;
        }
        
        // NEW ARCHITECTURE: No virtual esocket! Create stream WITHOUT esocket.
        // Stream will use listener's physical esocket for I/O, dispatching by remote_addr.
        l_session->stream = DAP_NEW_Z(dap_stream_t);
        if (!l_session->stream) {
            log_it(L_ERROR, "Failed to create stream for UDP session");
            DAP_DELETE(l_session);
            pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
            a_es->buf_in_size = 0;
            return;
        }
        
        // Initialize trans_ctx WITHOUT esocket (dispatcher will handle I/O)
        l_session->stream->trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
        if (!l_session->stream->trans_ctx) {
            log_it(L_ERROR, "Failed to create trans_ctx for UDP session");
            DAP_DELETE(l_session->stream);
            DAP_DELETE(l_session);
            pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
            a_es->buf_in_size = 0;
            return;
        }
        
        // trans_ctx points back to stream, but NO esocket! Dispatcher handles I/O.
        l_session->stream->trans_ctx->stream = l_session->stream;
        l_session->stream->trans_ctx->esocket = NULL;  // No virtual esocket!
        l_session->stream->trans_ctx->esocket_uuid = 0;
        l_session->stream->trans_ctx->esocket_worker = a_es->worker;
        
        // Set stream trans to UDP
        if (l_udp_srv->trans) {
            l_session->stream->trans = l_udp_srv->trans;
        }
        
        // CRITICAL: Create UDP per-stream context for server-side stream!
        // This is required for handshake processing and write operations.
        dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_or_create_udp_ctx(l_session->stream);
        if (!l_udp_ctx) {
            log_it(L_ERROR, "Failed to create UDP context for server-side stream");
            DAP_DELETE(l_session->stream->trans_ctx);
            DAP_DELETE(l_session->stream);
            DAP_DELETE(l_session);
            pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
            a_es->buf_in_size = 0;
            return;
        }
        
        // Store remote address in UDP context for server-side writes (sendto)
        memcpy(&l_udp_ctx->remote_addr, &a_es->addr_storage, a_es->addr_size);
        l_udp_ctx->remote_addr_len = a_es->addr_size;
        l_udp_ctx->session_id = l_session_id;
        
        log_it(L_DEBUG, "Initialized UDP context for server-side stream %p (session 0x%lx)", 
               l_session->stream, l_session_id);
        
        // Add to server's session hash table by remote_addr (already under write lock)
        // NOTE: uthash will use full remote_addr as key via s_find_session_by_addr iteration
        HASH_ADD(hh, l_udp_srv->sessions, remote_addr, sizeof(l_session->remote_addr), l_session);
        
        log_it(L_INFO, "Created UDP session 0x%lx with stream %p (NO virtual esocket - dispatcher architecture)", 
               l_session_id, l_session->stream);
        
        // Downgrade from write to read lock (keep lock for stream access safety)
        pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
        pthread_rwlock_rdlock(&l_udp_srv->sessions_lock);
    }
    
    // NOTE: sessions_lock is held as READ lock at this point for ALL paths
    // This protects session->stream from being deleted by cleanup in another thread
    
    if (!l_session) {
        log_it(L_WARNING, "No session found for UDP packet (session_id=0x%lx, type=%u), dropping", 
               l_session_id, l_type);
        pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
        a_es->buf_in_size = 0;
        return;
    }
    
    // Update activity timestamp (under sessions_lock)
    l_session->last_activity = time(NULL);
    
    // Access stream UNDER sessions_lock for thread safety
    dap_stream_t *l_stream = l_session->stream;
    
    if (!l_stream) {
        log_it(L_ERROR, "Session has invalid stream");
        pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
        a_es->buf_in_size = 0;
        return;
    }
    
    // NEW ARCHITECTURE: No virtual esocket! Stream reads directly from listener's buf_in.
    // Dispatcher calls stream's read method with data from physical esocket.
    
    switch (l_type) {
        case DAP_STREAM_UDP_PKT_HANDSHAKE:
            debug_if(s_debug_more, L_DEBUG, "Dispatching UDP HANDSHAKE packet to stream %p (session 0x%lx)", 
                     l_stream, l_session_id);
            
            // Call stream's trans read method directly with payload from listener's buf_in
            // Stream will process handshake internally via s_udp_read()
            if (l_stream->trans && l_stream->trans->ops && l_stream->trans->ops->read) {
                // Pass payload directly to stream's read method
                // Server streams have l_ctx->esocket == NULL, so they will use a_buffer instead
                ssize_t l_read = l_stream->trans->ops->read(l_stream, l_payload, l_payload_len);
                
                debug_if(s_debug_more, L_DEBUG, "Stream read returned %zd bytes", l_read);
                
                // If stream has response data, send it via sendto with remote_addr
                if (l_stream->trans && l_stream->trans->ops && l_stream->trans->ops->write) {
                    // Write method will use sendto with l_session->remote_addr
                    // TODO: Implement dispatcher write logic
                }
            } else {
                log_it(L_ERROR, "Stream has no trans read method");
            }
            break;
            
        case DAP_STREAM_UDP_PKT_SESSION_CREATE:
            debug_if(s_debug_more, L_DEBUG, "Dispatching UDP SESSION_CREATE packet to stream %p (session 0x%lx)", 
                     l_stream, l_session_id);
            
            // Same dispatch logic as HANDSHAKE
            if (l_stream->trans && l_stream->trans->ops && l_stream->trans->ops->read) {
                dap_events_socket_t *l_saved_es = NULL;
                if (l_stream->trans_ctx) {
                    l_saved_es = l_stream->trans_ctx->esocket;
                    l_stream->trans_ctx->esocket = a_es;
                }
                
                ssize_t l_read = l_stream->trans->ops->read(l_stream, NULL, 0);
                
                if (l_stream->trans_ctx) {
                    l_stream->trans_ctx->esocket = l_saved_es;
                }
                
                debug_if(s_debug_more, L_DEBUG, "Stream read returned %zd bytes", l_read);
            } else {
                log_it(L_ERROR, "Stream has no trans read method");
            }
            break;
            
        case DAP_STREAM_UDP_PKT_DATA:
            // NOTE: This is for encrypted stream data packets
            debug_if(s_debug_more, L_DEBUG, "Dispatching UDP DATA packet (%u bytes) to stream %p (session 0x%lx)", 
                   l_payload_len, l_stream, l_session_id);
            
            // Dispatch to stream for processing
            if (l_stream->trans && l_stream->trans->ops && l_stream->trans->ops->read) {
                dap_events_socket_t *l_saved_es = NULL;
                if (l_stream->trans_ctx) {
                    l_saved_es = l_stream->trans_ctx->esocket;
                    l_stream->trans_ctx->esocket = a_es;
                }
                
                ssize_t l_read = l_stream->trans->ops->read(l_stream, NULL, 0);
                
                if (l_stream->trans_ctx) {
                    l_stream->trans_ctx->esocket = l_saved_es;
                }
                
                debug_if(s_debug_more, L_DEBUG, "Stream read returned %zd bytes", l_read);
            } else {
                log_it(L_ERROR, "Stream has no trans read method for DATA packet");
            }
            break;
            
        case DAP_STREAM_UDP_PKT_KEEPALIVE:
            debug_if(s_debug_more, L_DEBUG, "Processing UDP KEEPALIVE packet");
            // Just update timestamp (already done above)
            break;
            
        case DAP_STREAM_UDP_PKT_CLOSE:
            log_it(L_INFO, "Processing UDP CLOSE packet for session 0x%lx", l_session_id);
            // Remove session from hash table
            pthread_rwlock_wrlock(&l_udp_srv->sessions_lock);
            HASH_DEL(l_udp_srv->sessions, l_session);
            pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
            
            // CRITICAL: Do NOT access trans_ctx->esocket here!
            // This cleanup runs in server thread, but esocket may be on different worker
            // Let dap_stream_delete_unsafe handle esocket cleanup safely
            
            // Delete stream (will handle esocket cleanup in correct worker context)
            if (l_session->stream) {
                dap_stream_delete_unsafe(l_session->stream);
            }
            
            DAP_DELETE(l_session);
            break;
            
        default:
            log_it(L_WARNING, "Unknown UDP packet type %u, dropping", l_type);
            break;
    }
    
    // Release sessions lock (held during control packet processing for thread safety)
    pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
    
    // Clear listener socket buffer (we've processed the packet)
    a_es->buf_in_size = 0;
}

// Trans server operations callbacks
static void* s_udp_server_new(const char *a_server_name)
{
    return (void*)dap_net_trans_udp_server_new(a_server_name);
}

static int s_udp_server_start(void *a_server, const char *a_cfg_section, 
                               const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    dap_net_trans_udp_server_t *l_udp = (dap_net_trans_udp_server_t *)a_server;
    return dap_net_trans_udp_server_start(l_udp, a_cfg_section, a_addrs, a_ports, a_count);
}

static void s_udp_server_stop(void *a_server)
{
    dap_net_trans_udp_server_t *l_udp = (dap_net_trans_udp_server_t *)a_server;
    dap_net_trans_udp_server_stop(l_udp);
}

static void s_udp_server_delete(void *a_server)
{
    dap_net_trans_udp_server_t *l_udp = (dap_net_trans_udp_server_t *)a_server;
    dap_net_trans_udp_server_delete(l_udp);
}

static const dap_net_trans_server_ops_t s_udp_server_ops = {
    .new = s_udp_server_new,
    .start = s_udp_server_start,
    .stop = s_udp_server_stop,
    .delete = s_udp_server_delete
};

/**
 * @brief Initialize UDP server module
 */
int dap_net_trans_udp_server_init(void)
{
    // Read debug configuration
    if (g_config) {
        s_debug_more = dap_config_get_item_bool_default(g_config, "stream_udp", "debug_more", false);
        if (s_debug_more) {
            log_it(L_NOTICE, "UDP server: verbose debugging ENABLED");
        }
    }
    
    // Register trans server operations for all UDP variants
    int l_ret = dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_BASIC, &s_udp_server_ops);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register UDP_BASIC trans server operations");
        return l_ret;
    }
    
    // Register for other UDP variants too
    dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_RELIABLE, &s_udp_server_ops);
    dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_QUIC_LIKE, &s_udp_server_ops);
    
    log_it(L_NOTICE, "Initialized UDP server module");
    return 0;
}

/**
 * @brief Deinitialize UDP server module
 */
void dap_net_trans_udp_server_deinit(void)
{
    // Unregister trans server operations
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_UDP_BASIC);
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_UDP_RELIABLE);
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_UDP_QUIC_LIKE);
    
    log_it(L_INFO, "UDP server module deinitialized");
}

/**
 * @brief Create new UDP server instance
 */
dap_net_trans_udp_server_t *dap_net_trans_udp_server_new(const char *a_server_name)
{
    if (!a_server_name) {
        log_it(L_ERROR, "Server name is NULL");
        return NULL;
    }

    dap_net_trans_udp_server_t *l_udp_server = DAP_NEW_Z(dap_net_trans_udp_server_t);
    if (!l_udp_server) {
        log_it(L_CRITICAL, "Cannot allocate memory for UDP server");
        return NULL;
    }

    dap_strncpy(l_udp_server->server_name, a_server_name, sizeof(l_udp_server->server_name) - 1);
    
    // Initialize session table and lock
    l_udp_server->sessions = NULL;
    pthread_rwlock_init(&l_udp_server->sessions_lock, NULL);
    
    // Initialize shared buffer infrastructure
    pthread_rwlock_init(&l_udp_server->shared_buf_lock, NULL);
    l_udp_server->shared_buf = NULL;
    l_udp_server->shared_buf_size = 0;
    l_udp_server->shared_buf_capacity = 0;
    l_udp_server->listener_es = NULL;
    
    // Get UDP trans instance
    l_udp_server->trans = dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    if (!l_udp_server->trans) {
        log_it(L_ERROR, "UDP trans not registered");
        pthread_rwlock_destroy(&l_udp_server->sessions_lock);
        pthread_rwlock_destroy(&l_udp_server->shared_buf_lock);
        DAP_DELETE(l_udp_server);
        return NULL;
    }

    log_it(L_INFO, "Created UDP server: %s", a_server_name);
    return l_udp_server;
}

/**
 * @brief Start UDP server on specified addresses and ports
 */
int dap_net_trans_udp_server_start(dap_net_trans_udp_server_t *a_udp_server,
                                       const char *a_cfg_section,
                                       const char **a_addrs,
                                       uint16_t *a_ports,
                                       size_t a_count)
{
    if (!a_udp_server || !a_ports || a_count == 0) {
        log_it(L_ERROR, "Invalid parameters for UDP server start");
        return -1;
    }

    if (a_udp_server->server) {
        log_it(L_WARNING, "UDP server already started");
        return -2;
    }

    // Create underlying dap_server_t
    // Set up server callbacks for listener esocket initialization
    dap_events_socket_callbacks_t l_server_callbacks = {
        .new_callback = s_listener_new_callback,   // Initialize shared buffer on listener creation
        .delete_callback = NULL,
        .read_callback = NULL,
        .write_callback = NULL,
        .error_callback = NULL
    };
    
    // UDP client callbacks will be set by dap_stream_add_proc_udp()
    dap_events_socket_callbacks_t l_udp_callbacks = {
        .new_callback = NULL,      // Will be set by dap_stream_add_proc_udp
        .delete_callback = NULL,   // Will be set by dap_stream_add_proc_udp
        .read_callback = NULL,     // Will be set by dap_stream_add_proc_udp
        .write_callback = NULL,    // Will be set by dap_stream_add_proc_udp
        .error_callback = NULL
    };

    a_udp_server->server = dap_server_new(a_cfg_section, &l_server_callbacks, &l_udp_callbacks);
    if (!a_udp_server->server) {
        log_it(L_ERROR, "Failed to create dap_server for UDP");
        return -3;
    }

    // Set UDP server as inheritor (used by s_listener_new_callback)
    a_udp_server->server->_inheritor = a_udp_server;

    // Register UDP stream handlers
    // This sets up all necessary callbacks for UDP processing
    dap_stream_add_proc_udp(a_udp_server->server);
    
    // Override read callback for server listener
    a_udp_server->server->client_callbacks.read_callback = s_udp_server_read_callback;
    
    // Add new_callback for listener initialization (shared buffer setup)
    dap_events_socket_callbacks_t l_listener_callbacks = a_udp_server->server->client_callbacks;
    l_listener_callbacks.new_callback = s_listener_new_callback;

    debug_if(s_debug_more, L_DEBUG, "Registered UDP stream handlers");

    // Start listening on all specified address:port pairs
    // Use l_listener_callbacks (client_callbacks + new_callback for shared buffer init)
    for (size_t i = 0; i < a_count; i++) {
        const char *l_addr = (a_addrs && a_addrs[i]) ? a_addrs[i] : "0.0.0.0";
        uint16_t l_port = a_ports[i];

        int l_ret = dap_server_listen_addr_add(a_udp_server->server, l_addr, l_port,
                                                DESCRIPTOR_TYPE_SOCKET_UDP,
                                                &l_listener_callbacks);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to start UDP server on %s:%u", l_addr, l_port);
            dap_net_trans_udp_server_stop(a_udp_server);
            return -4;
        }

        log_it(L_NOTICE, "UDP server '%s' listening on %s:%u",
               a_udp_server->server_name, l_addr, l_port);
    }

    return 0;
}

/**
 * @brief Stop UDP server
 */
void dap_net_trans_udp_server_stop(dap_net_trans_udp_server_t *a_udp_server)
{
    if (!a_udp_server) {
        return;
    }

    // Cleanup all active sessions
    pthread_rwlock_wrlock(&a_udp_server->sessions_lock);
    udp_session_entry_t *l_session, *l_tmp;
    HASH_ITER(hh, a_udp_server->sessions, l_session, l_tmp) {
        HASH_DEL(a_udp_server->sessions, l_session);
        
        // CRITICAL: Do NOT access trans_ctx->esocket here!
        // This cleanup runs in server thread, but esocket may be on different worker
        // Let dap_stream_delete_unsafe handle esocket cleanup safely
        
        // Delete stream (will handle esocket cleanup in correct worker context)
        if (l_session->stream) {
            dap_stream_delete_unsafe(l_session->stream);
        }
        
        DAP_DELETE(l_session);
    }
    pthread_rwlock_unlock(&a_udp_server->sessions_lock);

    if (a_udp_server->server) {
        dap_server_delete(a_udp_server->server);
        a_udp_server->server = NULL;
    }

    log_it(L_INFO, "UDP server '%s' stopped", a_udp_server->server_name);
}

/**
 * @brief Delete UDP server instance
 */
void dap_net_trans_udp_server_delete(dap_net_trans_udp_server_t *a_udp_server)
{
    if (!a_udp_server) {
        return;
    }

    // Ensure server is stopped before deletion
    dap_net_trans_udp_server_stop(a_udp_server);
    
    // Destroy locks
    pthread_rwlock_destroy(&a_udp_server->sessions_lock);
    pthread_rwlock_destroy(&a_udp_server->shared_buf_lock);
    
    // Note: shared_buf points to listener esocket buf_in, don't free it

    log_it(L_INFO, "Deleted UDP server: %s", a_udp_server->server_name);
    DAP_DELETE(a_udp_server);
}

