/*
 * Authors:
 * Dmitrii Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 */

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "dap_common.h"
#include "dap_config.h"
#include "dap_enc.h"
#include "dap_enc_key.h"
#include "dap_enc_base64.h"
#include "dap_enc_kdf.h"
#include "dap_enc_kyber.h"
#include "dap_rand.h"  // For randombytes
#include "dap_transport_obfuscation.h"  // For handshake obfuscation
#include "dap_string.h"
#include "dap_net_trans_udp_server.h"
#include "dap_net_trans_udp_stream.h"
#include "dap_net_trans_server.h"
#include "dap_io_flow.h"
#include "dap_io_flow_udp.h"
#include "dap_io_flow_socket.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_worker.h"
#include "dap_stream_session.h"
#include "dap_net_trans.h"
#include "dap_worker.h"
#include "json.h"

#define LOG_TAG "dap_net_trans_udp_server"

// Protocol version
#define DAP_STREAM_UDP_VERSION 1

// Global debug flag
static bool s_debug_more = false;

/**
 * @brief Protocol-specific session extension (extends dap_io_flow_udp_t)
 */
typedef struct stream_udp_session {
    dap_io_flow_udp_t base;              // UDP flow (MUST be first!)
    
    // Stream protocol fields
    dap_stream_t *stream;                // Associated dap_stream_t
    dap_stream_session_t *session;       // Associated dap_stream_session_t
    uint64_t session_id;                 // Session ID from handshake
    dap_enc_key_t *encryption_key;       // Session encryption key
} stream_udp_session_t;

// Forward declarations for protocol handlers
static bool s_stream_udp_should_forward(dap_io_flow_t *a_flow);

// Session/Stream integration callbacks
static void* s_stream_udp_session_create_cb(dap_io_flow_t *a_flow, void *a_session_params);
static void s_stream_udp_session_close_cb(dap_io_flow_t *a_flow, void *a_session_context);
static void* s_stream_udp_stream_create_cb(dap_io_flow_t *a_flow, void *a_stream_params);
static ssize_t s_stream_udp_stream_write_cb(dap_io_flow_t *a_flow, void *a_stream_context,
                                            const uint8_t *a_data, size_t a_size);
static ssize_t s_stream_udp_stream_packet_send_cb(dap_io_flow_t *a_flow, void *a_stream_context,
                                                  const uint8_t *a_packet, size_t a_packet_size);

// Protocol packet handlers
static int s_handle_handshake(stream_udp_session_t *a_session, const uint8_t *a_payload,
                             size_t a_payload_size);
static int s_handle_session_create(stream_udp_session_t *a_session, const uint8_t *a_payload,
                                   size_t a_payload_size);
static int s_handle_data(stream_udp_session_t *a_session, const uint8_t *a_payload,
                        size_t a_payload_size);
static int s_handle_keepalive(stream_udp_session_t *a_session);
static int s_handle_close(stream_udp_session_t *a_session);

// Helper functions
static int s_send_udp_packet(stream_udp_session_t *a_session,
                             uint8_t a_type,
                             const uint8_t *a_payload,
                             size_t a_payload_size);

// UDP flow callbacks
static int s_udp_packet_received_cb(dap_io_flow_udp_t *a_flow,
                                    const uint8_t *a_data,
                                    size_t a_size);
static dap_io_flow_udp_t* s_udp_protocol_create_cb(dap_io_flow_udp_t *a_flow);
static int s_udp_protocol_finalize_cb(dap_io_flow_udp_t *a_flow);
static void s_udp_protocol_destroy_cb(dap_io_flow_udp_t *a_flow);

// VTable for UDP flow
static dap_io_flow_udp_ops_t s_stream_udp_ops = {
    .packet_received = s_udp_packet_received_cb,
    .protocol_create = s_udp_protocol_create_cb,
    .protocol_finalize = s_udp_protocol_finalize_cb,
    .protocol_destroy = s_udp_protocol_destroy_cb,
    .protocol_send = NULL  // We use direct send
};

// Forward declarations for server operations
static int s_udp_server_start_wrapper(void *a_server, const char *a_cfg_section,
                                      const char **a_addrs, uint16_t *a_ports, size_t a_count);

// VTable for generic flow server
static dap_io_flow_ops_t s_stream_flow_ops = {
    .packet_received = NULL,  // Handled by UDP layer
    .flow_create = NULL,      // Handled by UDP layer  
    .flow_destroy = NULL,     // Handled by UDP layer
    .should_forward = s_stream_udp_should_forward,
    .get_packet_boundary = NULL,
    // Session/Stream integration
    .session_create = s_stream_udp_session_create_cb,
    .session_close = s_stream_udp_session_close_cb,
    .stream_create = s_stream_udp_stream_create_cb,
    .stream_write = s_stream_udp_stream_write_cb,
    .stream_packet_send = s_stream_udp_stream_packet_send_cb
};

// Server operations VTable (for trans server API)
static dap_net_trans_server_ops_t s_udp_server_ops = {
    .new = (void* (*)(const char*))dap_net_trans_udp_server_new,
    .start = s_udp_server_start_wrapper,
    .stop = (void (*)(void*))dap_net_trans_udp_server_stop,
    .delete = (void (*)(void*))dap_net_trans_udp_server_delete
};

// =============================================================================
// SERVER OPERATIONS WRAPPERS
// =============================================================================

/**
 * @brief Wrapper for start operation (adapts to unified trans server API)
 * 
 * Creates separate flow server for each address:port pair.
 * Each flow server handles its own sharded listeners and flows.
 */
static int s_udp_server_start_wrapper(void *a_server, const char *a_cfg_section,
                                      const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    UNUSED(a_cfg_section);
    
    if (!a_server || !a_addrs || !a_ports || a_count == 0) {
        log_it(L_ERROR, "Invalid parameters for UDP server start");
        return -1;
    }
    
    dap_net_trans_udp_server_t *l_udp_server = (dap_net_trans_udp_server_t*)a_server;
    
    debug_if(s_debug_more, L_DEBUG, "Starting UDP server '%s' with %zu address:port pairs",
             l_udp_server->server_name, a_count);
    
    // Allocate array for flow servers
    l_udp_server->flow_servers = DAP_NEW_Z_COUNT(dap_io_flow_server_t*, a_count);
    if (!l_udp_server->flow_servers) {
        log_it(L_CRITICAL, "Failed to allocate flow servers array");
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Allocated flow_servers array for %zu servers", a_count);
    
    // Create and start flow server for each address:port pair
    for (size_t i = 0; i < a_count; i++) {
        const char *l_addr = a_addrs[i];
        uint16_t l_port = a_ports[i];
        
        debug_if(s_debug_more, L_DEBUG,
                 "Creating UDP flow server for %s:%u (%zu/%zu)",
                 l_addr ? l_addr : "0.0.0.0", l_port, i + 1, a_count);
        
        // Create unique name for this flow server
        char l_flow_name[300];
        snprintf(l_flow_name, sizeof(l_flow_name), "%s_%s_%u", 
                 l_udp_server->server_name,
                 l_addr ? l_addr : "0.0.0.0",
                 l_port);
        
        debug_if(s_debug_more, L_DEBUG, "Creating flow server '%s'", l_flow_name);
        
        // Create flow server
        dap_io_flow_server_t *l_flow_server = dap_io_flow_server_new_udp(
            l_flow_name, &s_stream_flow_ops, &s_stream_udp_ops);
        
        debug_if(s_debug_more, L_DEBUG, "dap_io_flow_server_new_udp returned %p", 
                 (void*)l_flow_server);
        
        if (!l_flow_server) {
            log_it(L_ERROR, "Failed to create flow server for %s:%u",
                   l_addr ? l_addr : "0.0.0.0", l_port);
            
            // Cleanup previously created flow servers
            for (size_t j = 0; j < i; j++) {
                if (l_udp_server->flow_servers[j]) {
                    dap_io_flow_server_delete(l_udp_server->flow_servers[j]);
                }
            }
            DAP_DELETE(l_udp_server->flow_servers);
            l_udp_server->flow_servers = NULL;
            
            return -2;
        }
        
        debug_if(s_debug_more, L_DEBUG, "Flow server '%s' created, starting listener", l_flow_name);
        
        // Start listener on this flow server
        int l_ret = dap_io_flow_server_listen(l_flow_server, l_addr, l_port);
        
        debug_if(s_debug_more, L_DEBUG, "dap_io_flow_server_listen returned %d", l_ret);
        
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to start listener on %s:%u: %d",
                   l_addr ? l_addr : "0.0.0.0", l_port, l_ret);
            
            // Cleanup this flow server
            dap_io_flow_server_delete(l_flow_server);
            
            // Cleanup previously created flow servers
            for (size_t j = 0; j < i; j++) {
                if (l_udp_server->flow_servers[j]) {
                    dap_io_flow_server_delete(l_udp_server->flow_servers[j]);
                }
            }
            DAP_DELETE(l_udp_server->flow_servers);
            l_udp_server->flow_servers = NULL;
            
            return l_ret;
        }
        
        // Store flow server
        l_udp_server->flow_servers[i] = l_flow_server;
        l_udp_server->flow_servers_count++;
        
        log_it(L_NOTICE, "Started UDP flow server %zu/%zu on %s:%u",
               i + 1, a_count, l_addr ? l_addr : "0.0.0.0", l_port);
    }
    
    debug_if(s_debug_more, L_DEBUG, "Success: returning 0");
    
    log_it(L_NOTICE, "Stream UDP server '%s' started successfully with %zu flow server(s)",
           l_udp_server->server_name, a_count);
    
    return 0;
}

// =============================================================================
// PUBLIC API IMPLEMENTATION
// =============================================================================

int dap_net_trans_udp_server_init(void)
{
    log_it(L_NOTICE, "Initializing Stream UDP server module (io_flow API architecture)");
    
    // Load debug configuration
    s_debug_more = dap_config_get_item_bool_default(g_config, "stream_udp", "debug_more", false);
    
    if (s_debug_more) {
        log_it(L_NOTICE, "Stream UDP debug mode ENABLED");
    }
    
    // Register server operations for all UDP variants
    int l_ret = 0;
    l_ret |= dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_BASIC, &s_udp_server_ops);
    l_ret |= dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_RELIABLE, &s_udp_server_ops);
    l_ret |= dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_QUIC_LIKE, &s_udp_server_ops);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register UDP server operations");
        return l_ret;
    }
    
    return 0;
}

void dap_net_trans_udp_server_deinit(void)
{
    log_it(L_NOTICE, "Deinitializing Stream UDP server module");
    
    // Unregister server operations
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_UDP_BASIC);
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_UDP_RELIABLE);
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_UDP_QUIC_LIKE);
}

dap_net_trans_udp_server_t* dap_net_trans_udp_server_new(const char *a_name)
{
    if (!a_name) {
        log_it(L_ERROR, "Invalid server name");
        return NULL;
    }
    
    dap_net_trans_udp_server_t *l_server = DAP_NEW_Z(dap_net_trans_udp_server_t);
    if (!l_server) {
        log_it(L_CRITICAL, "Failed to allocate UDP server");
        return NULL;
    }
    
    snprintf(l_server->server_name, sizeof(l_server->server_name), "%s", a_name);
    l_server->flow_servers = NULL;
    l_server->flow_servers_count = 0;
    
    log_it(L_NOTICE, "Created Stream UDP server '%s'", a_name);
    
    return l_server;
}

int dap_net_trans_udp_server_start(dap_net_trans_udp_server_t *a_server,
                                   const char *a_addr,
                                   uint16_t a_port)
{
    if (!a_server) {
        log_it(L_ERROR, "Invalid UDP server for start");
        return -1;
    }
    
    // Use wrapper with single address/port
    const char *l_addrs[] = {a_addr};
    uint16_t l_ports[] = {a_port};
    
    return s_udp_server_start_wrapper(a_server, NULL, l_addrs, l_ports, 1);
}

void dap_net_trans_udp_server_stop(dap_net_trans_udp_server_t *a_server)
{
    if (!a_server) {
        return;
    }
    
    log_it(L_NOTICE, "Stopping Stream UDP server '%s'", a_server->server_name);
    
    // Stop all flow servers
    if (a_server->flow_servers) {
        for (size_t i = 0; i < a_server->flow_servers_count; i++) {
            if (a_server->flow_servers[i]) {
                dap_io_flow_server_stop(a_server->flow_servers[i]);
            }
        }
    }
}

void dap_net_trans_udp_server_delete(dap_net_trans_udp_server_t *a_server)
{
    if (!a_server) {
        return;
    }
    
    log_it(L_NOTICE, "Deleting Stream UDP server '%s'", a_server->server_name);
    
    // Delete all flow servers
    if (a_server->flow_servers) {
        for (size_t i = 0; i < a_server->flow_servers_count; i++) {
            if (a_server->flow_servers[i]) {
                dap_io_flow_server_delete(a_server->flow_servers[i]);
            }
        }
        DAP_DELETE(a_server->flow_servers);
        a_server->flow_servers = NULL;
    }
    
    a_server->flow_servers_count = 0;
    
    DAP_DELETE(a_server);
}

// =============================================================================
// PROTOCOL VTABLE IMPLEMENTATION
// =============================================================================

/**
 * @brief UDP packet received callback
 * 
 * Called by UDP flow layer when packet arrives. Parses stream UDP
 * protocol header and dispatches to appropriate handler.
 */
static int s_udp_packet_received_cb(dap_io_flow_udp_t *a_flow,
                                    const uint8_t *a_data,
                                    size_t a_size)
{
    if (!a_data || a_size == 0) {
        return -1;
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t*)a_flow;
    if (!l_session) {
        log_it(L_ERROR, "NULL session in packet callback");
        return -2;
    }
    
    // OBFUSCATED HANDSHAKE DETECTION: Size in range 600-900 bytes
    // This could be an obfuscated handshake packet!
    if (dap_transport_is_obfuscated_handshake_size(a_size)) {
        // Try to deobfuscate as handshake
        uint8_t *l_handshake = NULL;
        size_t l_handshake_size = 0;
        
        int l_ret = dap_transport_deobfuscate_handshake(a_data, a_size,
                                                        &l_handshake, &l_handshake_size);
        
        if (l_ret == 0) {
            // Successfully deobfuscated as handshake!
            debug_if(s_debug_more, L_DEBUG,
                     "Deobfuscated HANDSHAKE: %zu bytes → %zu bytes",
                     a_size, l_handshake_size);
            
            // Initialize session_id if not set
            if (l_session->session_id == 0) {
                randombytes((uint8_t*)&l_session->session_id, sizeof(l_session->session_id));
                debug_if(s_debug_more, L_DEBUG,
                         "HANDSHAKE: generated session_id=0x%lx for session %p",
                         l_session->session_id, l_session);
            }
            
            // Process deobfuscated handshake
            int l_result = s_handle_handshake(l_session, l_handshake, l_handshake_size);
            DAP_DELETE(l_handshake);
            return l_result;
        }
        
        // Deobfuscation failed - might be regular encrypted packet
        // Continue to try decryption with session key
    }
    
    // ALL OTHER PACKETS: Must be encrypted!
    // Decrypt first, then parse inner header
    
    if (!l_session->encryption_key) {
        log_it(L_WARNING, "Received encrypted packet but no encryption key established");
        return -3;
    }
    
    // Allocate buffer for decrypted data
    size_t l_decrypted_max = a_size + 256;  // Extra space for decryption
    uint8_t *l_decrypted = DAP_NEW_SIZE(uint8_t, l_decrypted_max);
    if (!l_decrypted) {
        log_it(L_ERROR, "Failed to allocate decryption buffer");
        return -4;
    }
    
    // Decrypt entire packet
    size_t l_decrypted_size = dap_enc_decode(l_session->encryption_key,
                                             a_data, a_size,
                                             l_decrypted, l_decrypted_max,
                                             DAP_ENC_DATA_TYPE_RAW);
    
    if (l_decrypted_size == 0) {
        log_it(L_ERROR, "Failed to decrypt UDP packet");
        DAP_DELETE(l_decrypted);
        return -5;
    }
    
    // Parse encrypted header (inside decrypted payload)
    if (l_decrypted_size < sizeof(dap_stream_trans_udp_encrypted_header_t)) {
        log_it(L_ERROR, "Decrypted packet too small for header (%zu bytes)", l_decrypted_size);
        DAP_DELETE(l_decrypted);
        return -6;
    }
    
    dap_stream_trans_udp_encrypted_header_t *l_header = 
        (dap_stream_trans_udp_encrypted_header_t*)l_decrypted;
    
    // Extract and validate fields
    uint8_t l_type = l_header->type;
    uint32_t l_seq_num = ntohl(l_header->seq_num);
    uint64_t l_session_id = be64toh(l_header->session_id);
    
    // Validate packet type
    if (l_type < DAP_STREAM_UDP_PKT_SESSION_CREATE || l_type > DAP_STREAM_UDP_PKT_CLOSE) {
        log_it(L_ERROR, "Invalid packet type: %u", l_type);
        DAP_DELETE(l_decrypted);
        return -7;
    }
    
    // Validate session_id
    if (l_session->session_id != 0 && l_session->session_id != l_session_id) {
        log_it(L_ERROR, "Session ID mismatch: packet=0x%lx, session=0x%lx",
               l_session_id, l_session->session_id);
        DAP_DELETE(l_decrypted);
        return -8;
    }
    
    // REPLAY PROTECTION: Validate sequence number
    // seq_num must be strictly increasing (with wraparound support)
    uint32_t l_last_seq = atomic_load(&l_session->base.last_seq_num_in);
    
    // Check for sequence number advance (handle wraparound)
    int32_t l_seq_diff = (int32_t)(l_seq_num - l_last_seq);
    
    if (l_seq_diff <= 0 && l_last_seq != 0) {
        // seq_num is less than or equal to last seen seq_num (possible replay)
        log_it(L_WARNING, "Replay attack detected: seq_num=%u, last_seq=%u (session=0x%lx)",
               l_seq_num, l_last_seq, l_session_id);
        DAP_DELETE(l_decrypted);
        return -9;
    }
    
    // Update last seen sequence number
    atomic_store(&l_session->base.last_seq_num_in, l_seq_num);
    
    debug_if(s_debug_more, L_DEBUG,
             "Sequence validation OK: seq=%u, last_seq=%u, diff=%d",
             l_seq_num, l_last_seq, l_seq_diff);
    
    // Extract payload (after encrypted header)
    const uint8_t *l_payload = l_decrypted + sizeof(dap_stream_trans_udp_encrypted_header_t);
    size_t l_payload_size = l_decrypted_size - sizeof(dap_stream_trans_udp_encrypted_header_t);
    
    debug_if(s_debug_more, L_DEBUG,
             "Decrypted packet: type=%u, seq=%u, session=0x%lx, payload=%zu bytes",
             l_type, l_seq_num, l_session_id, l_payload_size);
    
    // Dispatch by packet type
    int l_result = 0;
    switch (l_type) {
        case DAP_STREAM_UDP_PKT_SESSION_CREATE:
            l_result = s_handle_session_create(l_session, l_payload, l_payload_size);
            break;
            
        case DAP_STREAM_UDP_PKT_DATA:
            l_result = s_handle_data(l_session, l_payload, l_payload_size);
            break;
            
        case DAP_STREAM_UDP_PKT_KEEPALIVE:
            l_result = s_handle_keepalive(l_session);
            break;
            
        case DAP_STREAM_UDP_PKT_CLOSE:
            l_result = s_handle_close(l_session);
            break;
            
        default:
            log_it(L_WARNING, "Unhandled packet type: %u", l_type);
            l_result = -9;
            break;
    }
    
    DAP_DELETE(l_decrypted);
    return l_result;
}

/**
 * @brief Protocol create callback - allocate stream_udp_session_t
 */
static dap_io_flow_udp_t* s_udp_protocol_create_cb(dap_io_flow_udp_t *a_flow)
{
    UNUSED(a_flow);  // We allocate fresh structure
    
    // Allocate FULL structure (stream_udp_session_t extends dap_io_flow_udp_t)
    stream_udp_session_t *l_session = DAP_NEW_Z(stream_udp_session_t);
    if (!l_session) {
        log_it(L_CRITICAL, "Failed to allocate stream_udp_session_t");
        return NULL;
    }
    
    // Note: UDP layer will initialize base fields (remote_addr, listener_es, etc)
    // We only initialize protocol-specific fields here
    
    // Create stream instance
    l_session->stream = DAP_NEW_Z(dap_stream_t);
    if (!l_session->stream) {
        log_it(L_CRITICAL, "Failed to allocate stream");
        DAP_DELETE(l_session);
        return NULL;
    }
    
    // stream_worker will be set in finalize callback after listener_es is available
    l_session->stream->stream_worker = NULL;
    l_session->base.base.stream_context = l_session->stream;
    
    // Initialize encryption key to NULL (will be set during handshake)
    l_session->encryption_key = NULL;
    l_session->session = NULL;
    l_session->session_id = 0;
    
    debug_if(s_debug_more, L_DEBUG,
             "Allocated stream_udp_session_t at %p (stream=%p)",
             l_session, l_session->stream);
    
    return &l_session->base;  // Return pointer to base dap_io_flow_udp_t
}

/**
 * @brief Protocol finalize callback - complete initialization after UDP layer setup
 */
static int s_udp_protocol_finalize_cb(dap_io_flow_udp_t *a_flow)
{
    if (!a_flow || !a_flow->listener_es) {
        log_it(L_ERROR, "Invalid flow or listener_es in finalize");
        return -1;
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t*)a_flow;
    
    // Now we can set stream_worker using listener_es->worker
    l_session->stream->stream_worker = DAP_STREAM_WORKER(a_flow->listener_es->worker);
    
    debug_if(s_debug_more, L_DEBUG,
             "Finalized stream_udp_session_t %p (stream=%p, worker=%u)",
             l_session, l_session->stream,
             a_flow->listener_es->worker ? a_flow->listener_es->worker->id : 0);
    
    return 0;
}

/**
 * @brief Protocol destroy callback - cleanup stream_udp_session_t
 */
static void s_udp_protocol_destroy_cb(dap_io_flow_udp_t *a_flow)
{
    if (!a_flow) {
        return;
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t*)a_flow;
    
    if (l_session->stream) {
        DAP_DELETE(l_session->stream);
    }
    
    if (l_session->encryption_key) {
        dap_enc_key_delete(l_session->encryption_key);
    }
}

static bool s_stream_udp_should_forward(dap_io_flow_t *a_flow)
{
    UNUSED(a_flow);
    return true;  // Always forward for session affinity
}

// =============================================================================
// SESSION/STREAM INTEGRATION CALLBACKS
// =============================================================================

static void* s_stream_udp_session_create_cb(dap_io_flow_t *a_flow, void *a_session_params)
{
    if (!a_flow) {
        return NULL;
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t*)a_flow;
    
    // Create dap_stream_session_t
    dap_stream_session_t *l_stream_session = dap_stream_session_new(0, false);
    if (!l_stream_session) {
        log_it(L_ERROR, "Failed to create stream session");
        return NULL;
    }
    
    // Open session
    if (dap_stream_session_open(l_stream_session) != 0) {
        log_it(L_ERROR, "Failed to open stream session");
        DAP_DELETE(l_stream_session);
        return NULL;
    }
    
    l_session->session = l_stream_session;
    l_session->base.base.session_context = l_stream_session;
    
    UNUSED(a_session_params);
    
    return l_stream_session;
}

static void s_stream_udp_session_close_cb(dap_io_flow_t *a_flow, void *a_session_context)
{
    if (!a_flow || !a_session_context) {
        return;
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t*)a_flow;
    dap_stream_session_t *l_stream_session = (dap_stream_session_t*)a_session_context;
    
    // Close session using session ID
    if (l_stream_session->id) {
        dap_stream_session_close_mt(l_stream_session->id);
    }
    
    l_session->session = NULL;
    l_session->base.base.session_context = NULL;
}

static void* s_stream_udp_stream_create_cb(dap_io_flow_t *a_flow, void *a_stream_params)
{
    if (!a_flow) {
        return NULL;
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t*)a_flow;
    UNUSED(a_stream_params);
    
    return l_session->stream;
}

static ssize_t s_stream_udp_stream_write_cb(dap_io_flow_t *a_flow, void *a_stream_context,
                                            const uint8_t *a_data, size_t a_size)
{
    if (!a_flow || !a_stream_context || !a_data || a_size == 0) {
        return -1;
    }
    
    dap_stream_t *l_stream = (dap_stream_t*)a_stream_context;
    
    // Use transport-agnostic stream data processing
    size_t l_processed = dap_stream_data_proc_read_ext(l_stream, a_data, a_size);
    
    return (ssize_t)l_processed;
}

static ssize_t s_stream_udp_stream_packet_send_cb(dap_io_flow_t *a_flow, void *a_stream_context,
                                                  const uint8_t *a_packet, size_t a_packet_size)
{
    if (!a_flow || !a_stream_context || !a_packet || a_packet_size == 0) {
        return -1;
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t*)a_flow;
    
    // Send packet wrapped in UDP header
    int l_ret = s_send_udp_packet(l_session,
                                  DAP_STREAM_UDP_PKT_DATA,
                                  a_packet,
                                  a_packet_size);
    
    if (l_ret != 0) {
        log_it(L_WARNING, "Failed to send stream packet (%d)", l_ret);
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "Sent stream packet: %zu bytes for session 0x%lx",
             a_packet_size, l_session->session_id);
    
    return (ssize_t)a_packet_size;
}

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * @brief Send UDP packet (encrypted or plaintext for HANDSHAKE)
 * 
 * HANDSHAKE packets: sent as plaintext (just Kyber public key)
 * All other packets: encrypted with session key
 */
static int s_send_udp_packet(stream_udp_session_t *a_session,
                             uint8_t a_type,
                             const uint8_t *a_payload,
                             size_t a_payload_size)
{
    if (!a_session) {
        return -1;
    }
    
    // Get sequence number from UDP flow
    uint32_t l_seq_num = atomic_load(&a_session->base.seq_num_out);
    
    // HANDSHAKE packets are OBFUSCATED (size-based encryption)
    if (a_type == DAP_STREAM_UDP_PKT_HANDSHAKE) {
        // Validate size (must be Kyber public key)
        if (a_payload_size != DAP_STREAM_UDP_HANDSHAKE_SIZE) {
            log_it(L_ERROR, "Invalid handshake payload size: %zu (expected %d)",
                   a_payload_size, DAP_STREAM_UDP_HANDSHAKE_SIZE);
            return -2;
        }
        
        // Obfuscate handshake (encrypt with size-derived key, add random padding)
        uint8_t *l_obfuscated = NULL;
        size_t l_obfuscated_size = 0;
        
        int l_ret = dap_transport_obfuscate_handshake(a_payload, a_payload_size,
                                                      &l_obfuscated, &l_obfuscated_size);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to obfuscate HANDSHAKE packet");
            return -3;
        }
        
        // Send obfuscated handshake
        l_ret = dap_io_flow_udp_send(&a_session->base, l_obfuscated, l_obfuscated_size);
        DAP_DELETE(l_obfuscated);
        
        if (l_ret < 0) {
            log_it(L_ERROR, "Failed to send obfuscated HANDSHAKE packet");
            return -4;
        }
        
        debug_if(s_debug_more, L_DEBUG,
                 "Obfuscated HANDSHAKE sent: %zu → %zu bytes",
                 a_payload_size, l_obfuscated_size);
        return 0;
    }
    
    // ALL OTHER PACKETS: Encrypt entire packet
    
    if (!a_session->encryption_key) {
        log_it(L_ERROR, "No encryption key for sending encrypted packet (type=%u)", a_type);
        return -4;
    }
    
    // Build encrypted header
    dap_stream_trans_udp_encrypted_header_t l_header;
    l_header.type = a_type;
    l_header.seq_num = htonl(l_seq_num);
    l_header.session_id = htobe64(a_session->session_id);
    
    // Build cleartext packet (header + payload)
    size_t l_cleartext_size = sizeof(l_header) + a_payload_size;
    uint8_t *l_cleartext = DAP_NEW_SIZE(uint8_t, l_cleartext_size);
    if (!l_cleartext) {
        log_it(L_ERROR, "Failed to allocate cleartext buffer");
        return -5;
    }
    
    memcpy(l_cleartext, &l_header, sizeof(l_header));
    if (a_payload && a_payload_size > 0) {
        memcpy(l_cleartext + sizeof(l_header), a_payload, a_payload_size);
    }
    
    // Encrypt entire packet
    size_t l_encrypted_max = l_cleartext_size + 256;  // Extra space for encryption overhead
    uint8_t *l_encrypted = DAP_NEW_SIZE(uint8_t, l_encrypted_max);
    if (!l_encrypted) {
        log_it(L_ERROR, "Failed to allocate encryption buffer");
        DAP_DELETE(l_cleartext);
        return -6;
    }
    
    size_t l_encrypted_size = dap_enc_code(a_session->encryption_key,
                                           l_cleartext, l_cleartext_size,
                                           l_encrypted, l_encrypted_max,
                                           DAP_ENC_DATA_TYPE_RAW);
    
    DAP_DELETE(l_cleartext);
    
    if (l_encrypted_size == 0) {
        log_it(L_ERROR, "Failed to encrypt packet (type=%u)", a_type);
        DAP_DELETE(l_encrypted);
        return -7;
    }
    
    // Send encrypted blob (no headers, no magic, just encrypted data)
    int l_ret = dap_io_flow_udp_send(&a_session->base, l_encrypted, l_encrypted_size);
    
    DAP_DELETE(l_encrypted);
    
    if (l_ret < 0) {
        log_it(L_ERROR, "Failed to send encrypted packet (type=%u)", a_type);
        return -8;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "Encrypted packet sent: type=%u, seq=%u, session=0x%lx, encrypted_size=%zu",
             a_type, l_seq_num, a_session->session_id, l_encrypted_size);
    
    return 0;
}

// =============================================================================
// PROTOCOL PACKET HANDLERS
// =============================================================================

/**
 * @brief Handle HANDSHAKE packet (encryption handshake)
 */
static int s_handle_handshake(stream_udp_session_t *a_session, const uint8_t *a_payload,
                             size_t a_payload_size)
{
    if (!a_session || !a_payload) {
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "Processing HANDSHAKE packet (%zu bytes) for session %p",
             a_payload_size, a_session);
    
    // Generate ephemeral Bob key (Kyber512)
    dap_enc_key_t *l_bob_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    if (!l_bob_key) {
        log_it(L_ERROR, "Failed to generate Bob KEM key");
        return -2;
    }
    
    void *l_bob_pub = NULL;
    size_t l_bob_pub_size = 0;
    void *l_shared_key = NULL;
    size_t l_shared_key_size = 0;
    
    // Perform KEM encapsulation (Bob side)
    if (l_bob_key->gen_bob_shared_key) {
        l_shared_key_size = l_bob_key->gen_bob_shared_key(l_bob_key, a_payload, a_payload_size, &l_bob_pub);
        l_shared_key = l_bob_key->shared_key;
        l_bob_pub_size = l_shared_key_size;  // Return value is ciphertext size, not pub key size!
        
        if (!l_bob_pub || l_shared_key_size == 0 || !l_shared_key) {
            log_it(L_ERROR, "Failed to generate shared key from Alice's public key");
            dap_enc_key_delete(l_bob_key);
            return -3;
        }
    } else {
        log_it(L_ERROR, "Key type doesn't support KEM handshake");
        dap_enc_key_delete(l_bob_key);
        return -4;
    }
    
    // Derive HANDSHAKE key from shared secret using KDF-SHAKE256
    dap_enc_key_t *l_handshake_key = dap_enc_kdf_create_cipher_key(
        l_bob_key,
        DAP_ENC_KEY_TYPE_SALSA2012,
        "udp_handshake",
        14,
        0,  // Counter = 0
        32  // Key size
    );
    
    if (!l_handshake_key) {
        log_it(L_ERROR, "Failed to derive handshake key via KDF");
        dap_enc_key_delete(l_bob_key);
        return -5;
    }
    
    log_it(L_INFO, "SERVER: handshake key derived via KDF-SHAKE256");
    
    // Store handshake key in session
    a_session->encryption_key = l_handshake_key;
    
    debug_if(s_debug_more, L_DEBUG,
             "HANDSHAKE: stored encryption_key=%p for session %p (session_id=0x%lx)",
             a_session->encryption_key, a_session, a_session->session_id);
    
    // Send Bob's public key back
    int l_ret = s_send_udp_packet(a_session,
                                  DAP_STREAM_UDP_PKT_HANDSHAKE,
                                  l_bob_pub, l_bob_pub_size);
    
    dap_enc_key_delete(l_bob_key);
    
    debug_if(s_debug_more, L_DEBUG, "HANDSHAKE response sent (ret=%d)", l_ret);
    
    return l_ret;
}

/**
 * @brief Handle SESSION_CREATE packet
 */
static int s_handle_session_create(stream_udp_session_t *a_session, const uint8_t *a_payload,
                                   size_t a_payload_size)
{
    if (!a_session || !a_payload) {
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "Processing SESSION_CREATE packet (%zu bytes)",
             a_payload_size);
    
    // Debug: log session info
    debug_if(s_debug_more, L_DEBUG,
             "SESSION_CREATE for session %p: session_id=0x%lx, encryption_key=%p",
             a_session, a_session->session_id, a_session->encryption_key);
    
    // Decrypt payload with handshake key
    if (!a_session->encryption_key) {
        log_it(L_ERROR, "No handshake key for SESSION_CREATE decryption");
        return -2;
    }
    
    // Allocate buffer for decrypted data
    size_t l_decrypted_max = a_payload_size + 256;  // Extra space for decryption
    uint8_t *l_decrypted = DAP_NEW_SIZE(uint8_t, l_decrypted_max);
    if (!l_decrypted) {
        log_it(L_ERROR, "Failed to allocate decryption buffer");
        return -3;
    }
    
    // Decrypt
    size_t l_decrypted_size = dap_enc_decode(a_session->encryption_key,
                                             a_payload, a_payload_size,
                                             l_decrypted, l_decrypted_max,
                                             DAP_ENC_DATA_TYPE_RAW);
    
    if (l_decrypted_size == 0) {
        log_it(L_ERROR, "Failed to decrypt SESSION_CREATE payload");
        DAP_DELETE(l_decrypted);
        return -4;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "Decrypted SESSION_CREATE: %zu bytes", l_decrypted_size);
    
    // Debug: print decrypted content (as hex and string)
    if (s_debug_more && l_decrypted_size > 0) {
        char l_hex[512] = {0};
        size_t l_print_size = l_decrypted_size > 128 ? 128 : l_decrypted_size;
        for (size_t i = 0; i < l_print_size; i++) {
            sprintf(l_hex + i*3, "%02x ", l_decrypted[i]);
        }
        log_it(L_DEBUG, "Decrypted hex (first %zu bytes): %s", l_print_size, l_hex);
        
        // Print as string (null-terminated)
        char l_str[256] = {0};
        size_t l_str_len = l_decrypted_size < 255 ? l_decrypted_size : 255;
        memcpy(l_str, l_decrypted, l_str_len);
        l_str[l_str_len] = '\0';
        log_it(L_DEBUG, "Decrypted string: '%s'", l_str);
    }
    
    // Parse JSON parameters
    json_object *l_json = json_tokener_parse((const char*)l_decrypted);
    DAP_DELETE(l_decrypted);
    
    if (!l_json) {
        log_it(L_ERROR, "Failed to parse SESSION_CREATE JSON");
        return -5;
    }
    
    // Extract session ID
    json_object *l_session_id_obj = NULL;
    if (json_object_object_get_ex(l_json, "session_id", &l_session_id_obj)) {
        a_session->session_id = json_object_get_int64(l_session_id_obj);
    }
    
    // Extract channels
    json_object *l_channels_obj = NULL;
    if (json_object_object_get_ex(l_json, "channels", &l_channels_obj)) {
        const char *l_channels_str = json_object_get_string(l_channels_obj);
        if (l_channels_str) {
            debug_if(s_debug_more, L_DEBUG,
                     "Session channels: %s", l_channels_str);
            
            // Create session with specified channels
            dap_net_session_params_t l_params = {0};
            l_params.channels = l_channels_str;
            
            s_stream_udp_session_create_cb(&a_session->base.base, &l_params);
        }
    }
    
    json_object_put(l_json);
    
    // Derive session key from handshake key using KDF ratcheting
    uint64_t l_kdf_counter = 1;  // Counter = 1 for first session
    
    dap_enc_key_t *l_session_key = dap_enc_kdf_create_cipher_key(
        a_session->encryption_key,
        DAP_ENC_KEY_TYPE_SALSA2012,
        "udp_session",
        11,
        l_kdf_counter,  // Counter = 1 (ratcheting)
        32
    );
    
    if (!l_session_key) {
        log_it(L_ERROR, "Failed to derive session key via KDF ratcheting");
        return -6;
    }
    
    // Replace handshake key with session key
    dap_enc_key_delete(a_session->encryption_key);
    a_session->encryption_key = l_session_key;
    
    log_it(L_INFO, "SESSION_CREATE completed: session_id=0x%lx", a_session->session_id);
    
    // Send SESSION_CREATE response with KDF counter (UNENCRYPTED!)
    // Client will use this counter to derive the same session key via KDF
    // This provides forward secrecy without transmitting the actual key
    uint64_t l_counter_be = htobe64(l_kdf_counter);
    
    int l_ret = s_send_udp_packet(a_session,
                                  DAP_STREAM_UDP_PKT_SESSION_CREATE,
                                  (const uint8_t*)&l_counter_be, sizeof(l_counter_be));
    
    debug_if(s_debug_more, L_DEBUG, "SESSION_CREATE response sent (ret=%d)", l_ret);
    
    return l_ret;
}

/**
 * @brief Handle DATA packet
 */
static int s_handle_data(stream_udp_session_t *a_session, const uint8_t *a_payload,
                        size_t a_payload_size)
{
    if (!a_session || !a_payload || a_payload_size == 0) {
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "Processing DATA packet (%zu bytes)",
             a_payload_size);
    
    // Process data through stream
    if (a_session->stream && a_session->base.base.stream_context) {
        return (int)s_stream_udp_stream_write_cb(&a_session->base.base,
                                                  a_session->base.base.stream_context,
                                                  a_payload,
                                                  a_payload_size);
    }
    
    return 0;
}

/**
 * @brief Handle KEEPALIVE packet
 */
static int s_handle_keepalive(stream_udp_session_t *a_session)
{
    if (!a_session) {
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Processing KEEPALIVE for session 0x%lx", a_session->session_id);
    
    // Update session activity time (done automatically by UDP flow layer)
    
    // Send KEEPALIVE response
    return s_send_udp_packet(a_session,
                            DAP_STREAM_UDP_PKT_KEEPALIVE,
                            NULL, 0);
}

/**
 * @brief Handle CLOSE packet
 */
static int s_handle_close(stream_udp_session_t *a_session)
{
    if (!a_session) {
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Processing CLOSE");
    
    // Close session
    if (a_session->session && a_session->base.base.session_context) {
        s_stream_udp_session_close_cb(&a_session->base.base, a_session->base.base.session_context);
    }
    
    return 0;
}

