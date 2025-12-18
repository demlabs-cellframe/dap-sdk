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
typedef struct udp_session_entry {
    uint64_t session_id;               // Session ID from UDP header (hash key)
    dap_stream_t *stream;              // Associated dap_stream_t instance (owns virtual esocket via trans_ctx)
    struct sockaddr_storage remote_addr; // Client address
    socklen_t remote_addr_len;         // Address length
    time_t last_activity;              // Last packet timestamp
    UT_hash_handle hh;                 // uthash handle
    
    // Shared buffer architecture: offset tracking for multi-worker support
    size_t last_read_offset;           // Last position read from shared buffer
    size_t last_read_size;             // Size of last read
    bool processing_in_progress;       // True if currently processing packet
} udp_session_entry_t;

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
    
    // Allocate own buffers
    l_virtual_es->buf_in_size_max = DAP_EVENTS_SOCKET_BUF_SIZE;
    l_virtual_es->buf_in = DAP_NEW_Z_SIZE(byte_t, l_virtual_es->buf_in_size_max);
    l_virtual_es->buf_out_size_max = DAP_EVENTS_SOCKET_BUF_SIZE;
    l_virtual_es->buf_out = DAP_NEW_Z_SIZE(byte_t, l_virtual_es->buf_out_size_max);
    
    if (!l_virtual_es->buf_in || !l_virtual_es->buf_out) {
        log_it(L_CRITICAL, "Failed to allocate buffers for virtual esocket");
        DAP_DEL_Z(l_virtual_es->buf_in);
        DAP_DEL_Z(l_virtual_es->buf_out);
        DAP_DELETE(l_virtual_es);
        return NULL;
    }
    
    l_virtual_es->buf_in_size = 0;
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
    
    // Check if we need to initialize shared buffer (first packet)
    if (!l_udp_srv->listener_es) {
        pthread_rwlock_wrlock(&l_udp_srv->shared_buf_lock);
        if (!l_udp_srv->listener_es) { // Double-check after acquiring lock
            l_udp_srv->listener_es = a_es;
            l_udp_srv->shared_buf = a_es->buf_in;
            l_udp_srv->shared_buf_capacity = a_es->buf_in_size_max;
            debug_if(s_debug_more, L_DEBUG, "Initialized shared buffer: capacity=%zu", 
                   l_udp_srv->shared_buf_capacity);
        }
        pthread_rwlock_unlock(&l_udp_srv->shared_buf_lock);
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
    pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
    
    // Check if this could be a control packet (version byte = 1)
    dap_stream_trans_udp_header_t *l_header = (dap_stream_trans_udp_header_t*)a_es->buf_in;
    uint8_t l_version = l_header->version;
    bool l_looks_like_control = (l_version == 1);
    
    // If we have active session with encryption key AND packet doesn't look like control packet,
    // treat as encrypted stream data
    if (l_session_found && l_session && l_session->stream && 
        l_session->stream->session && l_session->stream->session->key &&
        !l_looks_like_control) {
        
        debug_if(s_debug_more, L_DEBUG, "Found active session with encryption key and non-control packet, treating as encrypted stream data (%zu bytes)", 
               l_udp_srv->shared_buf_size);
        
        // This is encrypted stream data - process directly from shared buffer
        dap_events_socket_t *l_stream_es = l_session->stream->trans_ctx->esocket;
        if (!l_stream_es) {
            log_it(L_ERROR, "Session stream has no esocket");
            pthread_rwlock_unlock(&l_udp_srv->shared_buf_lock);
            a_es->buf_in_size = 0;
            return;
        }
        
        // Shared buffer architecture: Read directly from shared buffer
        // Virtual esocket's buf_in now points to shared buffer data
        // This is multi-worker safe because we hold read lock
        
        // Mark session as processing
        l_session->processing_in_progress = true;
        size_t l_packet_offset = 0;  // Offset within this packet
        size_t l_packet_size = l_udp_srv->shared_buf_size;
        
        // Temporarily redirect virtual esocket buf_in to shared buffer region
        byte_t *l_orig_buf_in = l_stream_es->buf_in;
        size_t l_orig_buf_in_size = l_stream_es->buf_in_size;
        
        l_stream_es->buf_in = l_udp_srv->shared_buf + l_packet_offset;
        l_stream_es->buf_in_size = l_packet_size;
        
        debug_if(s_debug_more, L_DEBUG, "Processing stream data directly from shared buffer (offset=%zu, size=%zu)", 
               l_packet_offset, l_packet_size);
        
        // Process stream data (encrypted channel packets)
        dap_stream_data_proc_read(l_session->stream);
        
        // Restore original buf_in pointer
        l_stream_es->buf_in = l_orig_buf_in;
        l_stream_es->buf_in_size = l_orig_buf_in_size;
        
        // Update session tracking
        l_session->last_read_offset = l_packet_offset;
        l_session->last_read_size = l_packet_size;
        l_session->processing_in_progress = false;
        
        // Release shared buffer read lock
        pthread_rwlock_unlock(&l_udp_srv->shared_buf_lock);
        
        // If there is response data, send it
        if (l_stream_es->buf_out_size > 0 && l_stream_es->callbacks.write_callback) {
            l_stream_es->callbacks.write_callback(l_stream_es, l_stream_es->callbacks.arg);
        }
        
        // Clear listener socket buffer
        a_es->buf_in_size = 0;
        return;
    }
    
    // No active session with key OR packet looks like control - parse as control packet
    // Release read lock first as control packet processing doesn't need shared buffer
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
    // (We already checked for existing session with key above)
    l_session = NULL;
    pthread_rwlock_rdlock(&l_udp_srv->sessions_lock);
    HASH_FIND(hh, l_udp_srv->sessions, &l_session_id, sizeof(l_session_id), l_session);
    pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
    
    // For HANDSHAKE, we need to create a new session
    // SESSION_CREATE must use existing session created by HANDSHAKE
    if (!l_session && l_type == DAP_STREAM_UDP_PKT_HANDSHAKE) {
        log_it(L_INFO, "Creating new UDP session 0x%lx for HANDSHAKE", l_session_id);
        
        // Create new session entry
        l_session = DAP_NEW_Z(udp_session_entry_t);
        if (!l_session) {
            log_it(L_CRITICAL, "Failed to allocate UDP session entry");
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
        
        // Create virtual esocket for this session
        dap_events_socket_t *l_virtual_es = s_create_virtual_udp_esocket(a_es, 
            &l_session->remote_addr, l_session->remote_addr_len);
        if (!l_virtual_es) {
            log_it(L_ERROR, "Failed to create virtual esocket for UDP session");
            DAP_DELETE(l_session);
            a_es->buf_in_size = 0;
            return;
        }
        
        // Create dap_stream_t for this session with virtual esocket
        l_session->stream = dap_stream_new_es_client(l_virtual_es, NULL, false);
        if (!l_session->stream) {
            log_it(L_ERROR, "Failed to create stream for UDP session");
            // Cleanup virtual esocket
            DAP_DEL_Z(l_virtual_es->buf_in);
            DAP_DEL_Z(l_virtual_es->buf_out);
            DAP_DELETE(l_virtual_es);
            DAP_DELETE(l_session);
            a_es->buf_in_size = 0;
            return;
        }
        
        // Virtual esocket is now owned by stream (via trans_ctx->esocket)
        // No need to store separate reference
        
        // Set stream trans to UDP
        if (l_udp_srv->trans) {
            l_session->stream->trans = l_udp_srv->trans;
        }
        
        // Set read callback for virtual esocket to process incoming UDP packets
        if (l_session->stream->trans_ctx && l_session->stream->trans_ctx->esocket) {
            dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)l_session->stream->trans_ctx;
            dap_events_socket_t *l_v_es = l_trans_ctx->esocket;
            
            // CRITICAL: Set stream in trans_ctx so UDP read callback can find it
            l_trans_ctx->stream = l_session->stream;
            l_v_es->_inheritor = l_trans_ctx;  // Point to trans_ctx
            l_v_es->callbacks.read_callback = dap_stream_trans_udp_read_callback;
            debug_if(s_debug_more, L_DEBUG, "Set UDP read callback for virtual esocket %p (trans_ctx->stream=%p)", 
                     l_v_es, l_trans_ctx->stream);
        }
        
        // Add to server's session hash table
        pthread_rwlock_wrlock(&l_udp_srv->sessions_lock);
        HASH_ADD(hh, l_udp_srv->sessions, session_id, sizeof(l_session->session_id), l_session);
        pthread_rwlock_unlock(&l_udp_srv->sessions_lock);
        
        log_it(L_INFO, "Created UDP session 0x%lx with stream %p", l_session_id, l_session->stream);
    }
    
    if (!l_session) {
        log_it(L_WARNING, "No session found for UDP packet (session_id=0x%lx, type=%u), dropping", 
               l_session_id, l_type);
        a_es->buf_in_size = 0;
        return;
    }
    
    // Update activity timestamp
    l_session->last_activity = time(NULL);
    
    // Process packet based on type
    dap_stream_t *l_stream = l_session->stream;
    dap_events_socket_t *l_stream_es = (l_stream && l_stream->trans_ctx) ? l_stream->trans_ctx->esocket : NULL;
    
    if (!l_stream || !l_stream_es) {
        log_it(L_ERROR, "Session has invalid stream or esocket");
        a_es->buf_in_size = 0;
        return;
    }
    
    switch (l_type) {
        case DAP_STREAM_UDP_PKT_HANDSHAKE:
            debug_if(s_debug_more, L_DEBUG, "Processing UDP HANDSHAKE packet for session 0x%lx", l_session_id);
            // Copy UDP packet (with header) to virtual esocket buffer
            if (l_stream_es->buf_in_size + l_total_size <= l_stream_es->buf_in_size_max) {
                memcpy(l_stream_es->buf_in + l_stream_es->buf_in_size, 
                       a_es->buf_in, l_total_size);
                l_stream_es->buf_in_size += l_total_size;
                
                debug_if(s_debug_more, L_DEBUG, "Copied %zu bytes to virtual esocket buffer (now %zu bytes)", 
                       l_total_size, l_stream_es->buf_in_size);
                
                // Trigger read callback to process data from buf_in (don't manually call trans->ops->read)
                if (l_stream_es->callbacks.read_callback) {
                    debug_if(s_debug_more, L_DEBUG, "Calling read_callback for handshake processing");
                    l_stream_es->callbacks.read_callback(l_stream_es, l_stream_es->callbacks.arg);
                    
                    // If there is data to send (response in buf_out), send it now
                    if (l_stream_es->buf_out_size > 0) {
                        debug_if(s_debug_more, L_DEBUG, "Virtual esocket has %zu bytes to send, calling write_callback", 
                               l_stream_es->buf_out_size);
                        if (l_stream_es->callbacks.write_callback) {
                            l_stream_es->callbacks.write_callback(l_stream_es, l_stream_es->callbacks.arg);
                        }
                    }
                } else {
                    log_it(L_ERROR, "No read_callback set for virtual esocket");
                }
            } else {
                log_it(L_WARNING, "Virtual esocket buffer full, dropping HANDSHAKE packet");
            }
            break;
            
        case DAP_STREAM_UDP_PKT_SESSION_CREATE:
            debug_if(s_debug_more, L_DEBUG, "Processing UDP SESSION_CREATE packet for session 0x%lx", l_session_id);
            // Copy UDP packet to virtual esocket buffer
            if (l_stream_es->buf_in_size + l_total_size <= l_stream_es->buf_in_size_max) {
                memcpy(l_stream_es->buf_in + l_stream_es->buf_in_size, 
                       a_es->buf_in, l_total_size);
                l_stream_es->buf_in_size += l_total_size;
                
                // Trigger read callback to process data from buf_in (don't manually call trans->ops->read)
                if (l_stream_es->callbacks.read_callback) {
                    debug_if(s_debug_more, L_DEBUG, "Calling read_callback to process SESSION_CREATE");
                    l_stream_es->callbacks.read_callback(l_stream_es, l_stream_es->callbacks.arg);
                    
                    debug_if(s_debug_more, L_DEBUG, "After read_callback: buf_out_size=%zu", l_stream_es->buf_out_size);
                    
                    // If there is data to send, send it now
                    if (l_stream_es->buf_out_size > 0 && l_stream_es->callbacks.write_callback) {
                        debug_if(s_debug_more, L_DEBUG, "Calling write_callback to send SESSION_CREATE response");
                        l_stream_es->callbacks.write_callback(l_stream_es, l_stream_es->callbacks.arg);
                    } else if (l_stream_es->buf_out_size == 0) {
                        log_it(L_WARNING, "No data to send after processing SESSION_CREATE!");
                    }
                }
            } else {
                log_it(L_WARNING, "Virtual esocket buffer full, dropping SESSION_CREATE packet");
            }
            break;
            
        case DAP_STREAM_UDP_PKT_DATA:
            debug_if(s_debug_more, L_DEBUG, "Processing UDP DATA packet (%u bytes payload) for session 0x%lx", 
                   l_payload_len, l_session_id);
            // Copy only the payload (stream packets), not the UDP header
            if (l_stream_es->buf_in_size + l_payload_len <= l_stream_es->buf_in_size_max) {
                memcpy(l_stream_es->buf_in + l_stream_es->buf_in_size, 
                       l_payload, l_payload_len);
                l_stream_es->buf_in_size += l_payload_len;
                
                // Process stream data
                size_t l_processed = dap_stream_data_proc_read(l_stream);
                debug_if(s_debug_more, L_DEBUG, "Processed %zu bytes of stream data for session 0x%lx", 
                       l_processed, l_session_id);
            } else {
                log_it(L_WARNING, "Virtual esocket buffer full, dropping DATA packet");
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
            
            // Cleanup stream (this will also cleanup trans_ctx->esocket which is the virtual esocket)
            if (l_session->stream) {
                dap_stream_delete_unsafe(l_session->stream);
            }
            
            DAP_DELETE(l_session);
            break;
            
        default:
            log_it(L_WARNING, "Unknown UDP packet type %u, dropping", l_type);
            break;
    }
    
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
    // UDP callbacks will be set by dap_stream_add_proc_udp()
    dap_events_socket_callbacks_t l_udp_callbacks = {
        .new_callback = NULL,      // Will be set by dap_stream_add_proc_udp
        .delete_callback = NULL,   // Will be set by dap_stream_add_proc_udp
        .read_callback = NULL,     // Will be set by dap_stream_add_proc_udp
        .write_callback = NULL,    // Will be set by dap_stream_add_proc_udp
        .error_callback = NULL
    };

    a_udp_server->server = dap_server_new(a_cfg_section, NULL, &l_udp_callbacks);
    if (!a_udp_server->server) {
        log_it(L_ERROR, "Failed to create dap_server for UDP");
        return -3;
    }

    // Set UDP server as inheritor
    a_udp_server->server->_inheritor = a_udp_server;

    // Register UDP stream handlers
    // This sets up all necessary callbacks for UDP processing
    dap_stream_add_proc_udp(a_udp_server->server);
    
    // Override read callback for server listener
    a_udp_server->server->client_callbacks.read_callback = s_udp_server_read_callback;

    debug_if(s_debug_more, L_DEBUG, "Registered UDP stream handlers");

    // Start listening on all specified address:port pairs
    for (size_t i = 0; i < a_count; i++) {
        const char *l_addr = (a_addrs && a_addrs[i]) ? a_addrs[i] : "0.0.0.0";
        uint16_t l_port = a_ports[i];

        int l_ret = dap_server_listen_addr_add(a_udp_server->server, l_addr, l_port,
                                                DESCRIPTOR_TYPE_SOCKET_UDP,
                                                &a_udp_server->server->client_callbacks);
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
        
        // Cleanup stream (this will also cleanup trans_ctx->esocket which is the virtual esocket)
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

