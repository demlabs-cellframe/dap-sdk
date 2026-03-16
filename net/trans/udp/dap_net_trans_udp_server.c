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
#include "dap_io_flow_datagram.h"
#include "dap_io_flow_socket.h"
#include "dap_context_queue.h"
#include "dap_io_flow_ctrl.h"  // Flow Control for reliable delivery
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_net_trans_qos.h"
#include "dap_stream_worker.h"
#include "dap_stream_session.h"
#include "dap_net_trans.h"
#include "dap_worker.h"
#include "dap_thread_pool.h"
#include "json.h"

#define LOG_TAG "dap_net_trans_udp_server"

// Protocol version
#define DAP_STREAM_UDP_VERSION 1

// Global debug flag
static bool s_debug_more = false;

// Global thread pool for heavy KEM operations
static dap_thread_pool_t *s_kem_thread_pool = NULL;

//===================================================================
// UDP EXTENDED FLOW CONTROL HEADER SCHEMA (dap_serialize)
//===================================================================

/**
 * @brief UDP Extended Flow Control Header Serialization Schema
 * 
 * Расширяет базовую FC схему (dap_io_flow_ctrl_base_fields) с UDP-специфичными полями.
 * Эта схема используется для (де)сериализации полного UDP заголовка.
 * 
 * CRITICAL: Entire structure is encrypted!
 * No header bytes visible to DPI.
 */
const dap_serialize_field_t g_udp_full_header_fields[] = {
    // ===== БАЗОВЫЕ FC ПОЛЯ (наследуются от g_dap_io_flow_ctrl_base_fields) =====
    {
        .name = "seq_num",
        .type = DAP_SERIALIZE_TYPE_UINT64,
        .flags = DAP_SERIALIZE_FLAG_BIG_ENDIAN,
        .offset = offsetof(dap_stream_trans_udp_full_header_t, seq_num),
        .size = sizeof(uint64_t),
    },
    {
        .name = "ack_seq",
        .type = DAP_SERIALIZE_TYPE_UINT64,
        .flags = DAP_SERIALIZE_FLAG_BIG_ENDIAN,
        .offset = offsetof(dap_stream_trans_udp_full_header_t, ack_seq),
        .size = sizeof(uint64_t),
    },
    {
        .name = "timestamp_ms",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_BIG_ENDIAN,
        .offset = offsetof(dap_stream_trans_udp_full_header_t, timestamp_ms),
        .size = sizeof(uint32_t),
    },
    {
        .name = "fc_flags",
        .type = DAP_SERIALIZE_TYPE_UINT8,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_stream_trans_udp_full_header_t, fc_flags),
        .size = sizeof(uint8_t),
    },
    
    // ===== UDP-СПЕЦИФИЧНЫЕ ПОЛЯ (расширение) =====
    {
        .name = "type",
        .type = DAP_SERIALIZE_TYPE_UINT8,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_stream_trans_udp_full_header_t, type),
        .size = sizeof(uint8_t),
    },
    {
        .name = "session_id",
        .type = DAP_SERIALIZE_TYPE_UINT64,
        .flags = DAP_SERIALIZE_FLAG_BIG_ENDIAN,
        .offset = offsetof(dap_stream_trans_udp_full_header_t, session_id),
        .size = sizeof(uint64_t),
    },
};

const size_t g_udp_full_header_field_count = 
    sizeof(g_udp_full_header_fields) / sizeof(g_udp_full_header_fields[0]);

/**
 * @brief UDP Extended FC schema definition
 */
const dap_serialize_schema_t g_udp_full_header_schema = {
    .name = "udp_full_header",
    .version = 1,
    .struct_size = sizeof(dap_stream_trans_udp_full_header_t),
    .field_count = g_udp_full_header_field_count,
    .fields = g_udp_full_header_fields,
    .magic = DAP_STREAM_UDP_FULL_HEADER_MAGIC,  // Custom magic for UDP extended FC schema
    .validate_func = NULL,
};

//===================================================================
// PROTOCOL SESSION STRUCTURE
//===================================================================

// Thread-local context for passing UDP server to protocol_create callback
static _Thread_local dap_net_trans_udp_server_t *s_tls_current_server = NULL;

/**
 * @brief Protocol-specific session extension (extends dap_io_flow_datagram_t)
 */
typedef struct stream_udp_session {
    dap_io_flow_datagram_t base;              // UDP flow (MUST be first!)
    
    // Stream protocol fields
    dap_stream_t *stream;                // Associated dap_stream_t
    dap_stream_session_t *session;       // Associated dap_stream_session_t
    uint64_t session_id;                 // Session ID from handshake
    dap_enc_key_t *encryption_key;       // Session encryption key
    
    // NOTE: Flow Control is now in base.base.flow_ctrl (dap_io_flow_t)
    // This ensures FC lifecycle is tied to flow, not session
    
    // Handshake state protection
    _Atomic bool kem_task_pending;       // KEM task in progress (prevent duplicate HANDSHAKE)
    
    // Packet type tracking (for FC callbacks)
    _Atomic uint8_t last_send_type;     // Last packet type sent (for packet_prepare_cb)
    _Atomic uint8_t last_recv_type;     // Last packet type received (for payload_deliver_cb)
} stream_udp_session_t;

/**
 * @brief KEM task context for thread pool offload
 */
typedef struct kem_task_ctx {
    stream_udp_session_t *session;       // Session handle (must remain valid!)
    uint8_t *alice_pub_key;              // Alice's public key (copied)
    size_t alice_pub_key_size;           // Alice's public key size
    dap_events_socket_uuid_t session_uuid; // Session UUID for validation
} kem_task_ctx_t;

/**
 * @brief Listener disable callback arguments
 */
typedef struct {
    dap_io_flow_server_t *server;
    dap_events_socket_t *listener;
} listener_disable_args_t;

/**
 * @brief KEM task result
 */
typedef struct kem_task_result {
    dap_enc_key_t *handshake_key;        // Derived handshake key
    uint8_t *bob_ciphertext;             // Bob's ciphertext (to send to client)
    size_t bob_ciphertext_size;          // Bob's ciphertext size
    uint64_t session_id;                 // Session ID
    int error_code;                      // 0 on success, negative on error
} kem_task_result_t;

// Forward declarations for protocol handlers
static bool s_stream_udp_should_forward(dap_io_flow_t *a_flow);

// Flow Control callbacks (transport-provided)
static int s_flow_ctrl_packet_prepare_cb(dap_io_flow_t *a_flow,
                                          const dap_io_flow_pkt_metadata_t *a_metadata,
                                          const void *a_payload, size_t a_payload_size,
                                          void **a_packet_out, size_t *a_packet_size_out,
                                          void *a_arg);
static int s_flow_ctrl_packet_parse_cb(dap_io_flow_t *a_flow,
                                        const void *a_packet, size_t a_packet_size,
                                        dap_io_flow_pkt_metadata_t *a_metadata,
                                        const void **a_payload_out, size_t *a_payload_size_out,
                                        void *a_arg);
static int s_flow_ctrl_packet_send_cb(dap_io_flow_t *a_flow,
                                       const void *a_packet, size_t a_packet_size,
                                       void *a_arg);
static void s_flow_ctrl_packet_free_cb(void *a_packet, void *a_arg);
static int s_flow_ctrl_payload_deliver_cb(dap_io_flow_t *a_flow,
                                           const void *a_payload, size_t a_payload_size,
                                           void *a_arg);
static void s_flow_ctrl_keepalive_timeout_cb(dap_io_flow_t *a_flow, void *a_arg);

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

// Packet processing helpers
static int s_process_encrypted_udp_packet(stream_udp_session_t *a_session,
                                          const uint8_t *a_encrypted_data,
                                          size_t a_encrypted_size);

// Flow Control callbacks (static, shared by all sessions)
// NOTE: Defined AFTER forward declarations as callbacks reference static functions
static const dap_io_flow_ctrl_callbacks_t s_flow_ctrl_callbacks = {
    .packet_prepare = s_flow_ctrl_packet_prepare_cb,
    .packet_parse = s_flow_ctrl_packet_parse_cb,
    .packet_send = s_flow_ctrl_packet_send_cb,
    .packet_free = s_flow_ctrl_packet_free_cb,
    .payload_deliver = s_flow_ctrl_payload_deliver_cb,
    .keepalive_timeout = s_flow_ctrl_keepalive_timeout_cb,
    .arg = NULL,  // Not used, session is passed via protocol_ctx
};

// Helper functions
static int s_send_udp_packet(stream_udp_session_t *a_session,
                             uint8_t a_type,
                             const uint8_t *a_payload,
                             size_t a_payload_size);

// =============================================================================
// SERVER-SIDE TRANS OPERATIONS (for stream->trans on server)
// =============================================================================

/**
 * @brief Server-side write callback for trans->ops
 * 
 * This is called when dap_stream_pkt_write_unsafe() uses trans->ops->write.
 * On server, we need to find the flow and call stream_packet_send callback.
 * 
 * Architecture:
 * stream->trans->ops->write (this function) 
 *   -> gets stream_udp_session_t via stream->_server_session
 *   -> calls s_send_udp_packet with DAP_STREAM_UDP_PKT_DATA
 *   -> which encrypts and sends via dap_io_flow_datagram_send
 */
static ssize_t s_udp_server_trans_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        return -1;
    }
    
    // Get session from stream backlink
    stream_udp_session_t *l_session = (stream_udp_session_t*)a_stream->_server_session;
    if (!l_session) {
        log_it(L_ERROR, "Server stream has no _server_session backlink");
        return -1;
    }
    
    // Send packet wrapped in UDP header
    int l_ret = s_send_udp_packet(l_session,
                                  DAP_STREAM_UDP_PKT_DATA,
                                  (const uint8_t*)a_data,
                                  a_size);
    
    if (l_ret != 0) {
        debug_if(s_debug_more, L_DEBUG,
                 "Send stream packet failed: ret=%d", l_ret);
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "Server trans write: sent %zu bytes for session 0x%lx",
             a_size, l_session->session_id);
    
    return (ssize_t)a_size;
}

/**
 * @brief Get maximum packet size for UDP transport (server-side)
 */
static size_t s_udp_server_get_max_packet_size(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return DAP_STREAM_UDP_MAX_PAYLOAD_SIZE;
}

// Server-side trans operations
static const dap_net_trans_ops_t s_udp_server_trans_ops = {
    .init = NULL,  // Not used on server (already initialized)
    .deinit = NULL,
    .connect = NULL,  // Server doesn't connect
    .listen = NULL,   // Handled by dap_io_flow_server
    .accept = NULL,   // Handled by dap_io_flow_server
    .handshake_init = NULL,
    .handshake_process = NULL,
    .session_create = NULL,
    .session_start = NULL,
    .read = NULL,     // Handled by dap_io_flow callbacks
    .write = s_udp_server_trans_write,  // THE KEY CALLBACK!
    .close = NULL,    // Handled by dap_io_flow
    .get_capabilities = NULL,
    .register_server_handlers = NULL,
    .stage_prepare = NULL,
    .get_client_context = NULL,
    .get_max_packet_size = s_udp_server_get_max_packet_size
};

// UDP flow callbacks
static bool s_get_remote_addr_cb(dap_io_flow_datagram_t *a_flow,
                                  struct sockaddr_storage *a_addr_out,
                                  socklen_t *a_addr_len_out);
static int s_udp_packet_received_cb(dap_io_flow_datagram_t *a_flow,
                                    const uint8_t *a_data,
                                    size_t a_size);
static dap_io_flow_datagram_t* s_udp_protocol_create_cb(dap_io_flow_server_t *a_server,
                                                     dap_io_flow_datagram_t *a_flow);
static int s_udp_protocol_finalize_cb(dap_io_flow_datagram_t *a_flow);
static void s_udp_protocol_destroy_cb(dap_io_flow_datagram_t *a_flow);

// VTable for UDP flow
static dap_io_flow_datagram_ops_t s_stream_udp_ops = {
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
        dap_io_flow_server_t *l_flow_server = dap_io_flow_server_new_datagram(
            l_flow_name, &s_stream_flow_ops, &s_stream_udp_ops);
        
        debug_if(s_debug_more, L_DEBUG, "dap_io_flow_server_new_datagram returned %p", 
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
        
        // CRITICAL: Store UDP server pointer in flow server for protocol callbacks!
        dap_io_flow_server_set_inheritor(l_flow_server, l_udp_server);
        
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
    
    // Create thread pool for KEM operations
    // Use 0 for auto-detect CPU count, each thread will be bound to its own CPU core
    s_kem_thread_pool = dap_thread_pool_create(0, 256);  // Auto CPU count, max 256 queued tasks
    if (!s_kem_thread_pool) {
        log_it(L_ERROR, "Failed to create KEM thread pool");
        return -1;
    }
    log_it(L_NOTICE, "KEM thread pool created (auto CPU count with affinity, max queue 256)");
    
    // Register server operations for all UDP variants
    int l_ret = 0;
    l_ret |= dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_BASIC, &s_udp_server_ops);
    l_ret |= dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_RELIABLE, &s_udp_server_ops);
    l_ret |= dap_net_trans_server_register_ops(DAP_NET_TRANS_UDP_QUIC_LIKE, &s_udp_server_ops);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to register UDP server operations");
        dap_thread_pool_delete(s_kem_thread_pool);
        s_kem_thread_pool = NULL;
        return l_ret;
    }
    
    return 0;
}

void dap_net_trans_udp_server_deinit(void)
{
    log_it(L_NOTICE, "Deinitializing Stream UDP server module");
    
    // Shutdown KEM thread pool
    if (s_kem_thread_pool) {
        log_it(L_INFO, "Shutting down KEM thread pool...");
        dap_thread_pool_delete(s_kem_thread_pool);
        s_kem_thread_pool = NULL;
    }
    
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
    
    // CRITICAL: Create SERVER-SPECIFIC trans with server-side operations!
    // This trans will be used by all streams on this server
    l_server->trans = DAP_NEW_Z(dap_net_trans_t);
    if (!l_server->trans) {
        log_it(L_CRITICAL, "Failed to allocate server trans");
        DAP_DELETE(l_server);
        return NULL;
    }
    
    l_server->trans->type = DAP_NET_TRANS_UDP_BASIC;
    l_server->trans->ops = &s_udp_server_trans_ops;  // SERVER ops, not client!
    l_server->trans->socket_type = DAP_NET_TRANS_SOCKET_UDP;
    l_server->trans->_inheritor = NULL;  // Not needed for server trans
    
    log_it(L_NOTICE, "Created Stream UDP server '%s' with dedicated server trans %p", 
           a_name, l_server->trans);
    
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
    
    // Step 1: Mark all flow servers as deleting
    if (a_server->flow_servers) {
        for (size_t i = 0; i < a_server->flow_servers_count; i++) {
            dap_io_flow_server_t *l_fs = a_server->flow_servers[i];
            if (l_fs) {
                atomic_store(&l_fs->is_deleting, true);
                debug_if(s_debug_more, L_DEBUG, "Marked flow_server[%zu] as deleting", i);
            }
        }
    }
    
    // Step 2: Delete all flows (synchronously)
    if (a_server->flow_servers) {
        for (size_t i = 0; i < a_server->flow_servers_count; i++) {
            if (a_server->flow_servers[i]) {
                log_it(L_INFO, "Deleting flows for flow_server[%zu]...", i);
                int l_deleted = dap_io_flow_delete_all_flows(a_server->flow_servers[i]);
                log_it(L_INFO, "Deleted %d flows for flow_server[%zu]", l_deleted, i);
            }
        }
    }
    
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
    
    // Delete server-specific trans
    if (a_server->trans) {
        DAP_DELETE(a_server->trans);
        a_server->trans = NULL;
    }
    
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
static int s_udp_packet_received_cb(dap_io_flow_datagram_t *a_flow,
                                    const uint8_t *a_data,
                                    size_t a_size)
{
    if (!a_data || a_size == 0) {
        return -1;
    }
    
    if(s_debug_more){    // Log source address for debugging loopback issue
        char l_src_addr_str[64] = "unknown";
        if (a_flow->remote_addr.ss_family == AF_INET) {
            struct sockaddr_in *l_sin = (struct sockaddr_in*)&a_flow->remote_addr;
            snprintf(l_src_addr_str, sizeof(l_src_addr_str), "%s:%u",
                    inet_ntoa(l_sin->sin_addr), ntohs(l_sin->sin_port));
        }
        
        debug_if(s_debug_more, L_DEBUG, "SERVER: packet received: size=%zu, flow=%p, src=%s", 
                a_size, a_flow, l_src_addr_str);
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t*)a_flow;
    if (!l_session) {
        log_it(L_ERROR, "NULL session in packet callback");
        return -2;
    }
    
    // CRITICAL: Filter localhost loopback packets!
    // When SERVER sends via listener socket on localhost, kernel may deliver packets back to sender.
    // Check if source port == listener port (our own echo coming back)
    if (a_flow->remote_addr.ss_family == AF_INET && a_flow->listener_es) {
        struct sockaddr_in *l_remote = (struct sockaddr_in*)&a_flow->remote_addr;
        struct sockaddr_in l_local;
        socklen_t l_local_len = sizeof(l_local);
        
        if (getsockname(a_flow->listener_es->fd, (struct sockaddr*)&l_local, &l_local_len) == 0) {
            uint16_t l_remote_port = ntohs(l_remote->sin_port);
            uint16_t l_local_port = ntohs(l_local.sin_port);
            
            if (l_remote_port == l_local_port) {
                debug_if(s_debug_more, L_DEBUG,
                         "SERVER: IGNORING loopback packet from port %u (our listener port)", l_remote_port);
                return 0; // Ignore loopback
            }
        }
    }
    
    // OBFUSCATED HANDSHAKE DETECTION: Size in range [MIN, MAX] AND session not established
    // CRITICAL: Only try deobfuscation if session_id == 0 (handshake not completed)!
    // Otherwise FC packets (which may be in same size range) will be incorrectly treated as obfuscated.
    if (l_session->session_id == 0 && dap_transport_is_obfuscated_handshake_size(a_size)) {
        // Try to deobfuscate as handshake
        uint8_t *l_handshake = NULL;
        size_t l_handshake_size = 0;
        
        debug_if(s_debug_more, L_DEBUG,
                 "SERVER: Attempting to deobfuscate packet (size=%zu)", a_size);
        
        int l_ret = dap_transport_deobfuscate_handshake(a_data, a_size,
                                                        &l_handshake, &l_handshake_size);
        
        debug_if(s_debug_more, L_DEBUG,
                 "SERVER: Deobfuscation result: ret=%d", l_ret);
        
        if (l_ret == 0) {
            // Successfully deobfuscated as handshake!
            debug_if(s_debug_more, L_DEBUG,
                     "Deobfuscated HANDSHAKE: %zu bytes → %zu bytes",
                     a_size, l_handshake_size);
            
            // Initialize session_id
            randombytes((uint8_t*)&l_session->session_id, sizeof(l_session->session_id));
            debug_if(s_debug_more, L_DEBUG,
                     "HANDSHAKE: generated session_id=0x%lx for session %p",
                     l_session->session_id, l_session);
            
            // Process deobfuscated handshake
            int l_result = s_handle_handshake(l_session, l_handshake, l_handshake_size);
            DAP_DELETE(l_handshake);
            return l_result;
        }
        
        // Deobfuscation failed - might be regular encrypted packet
        log_it(L_WARNING, "SERVER: Deobfuscation failed (ret=%d), treating as encrypted packet", l_ret);
        // Continue to try decryption with session key
    } else {
        debug_if(s_debug_more, L_DEBUG,
                 "SERVER: Packet size %zu not in obfuscated range OR session already established (session_id=0x%lx)",
                 a_size, l_session->session_id);
    }
    
    // ALL OTHER PACKETS: Must be encrypted!
    // 
    // If Flow Control is enabled: pass to flow control for retransmission/reordering
    // Otherwise: decrypt + dispatch directly
    
    // Flow control from base flow structure (lifecycle tied to flow)
    dap_io_flow_ctrl_t *l_flow_ctrl = l_session->base.base.flow_ctrl;
    
    if (l_flow_ctrl) {
        // FLOW CONTROL ENABLED: Pass packet to flow control layer
        // Flow control will handle retransmission, reordering, ACKs
        // and eventually call s_flow_ctrl_payload_deliver_cb for in-order delivery
        
        debug_if(s_debug_more, L_DEBUG,
                 "Passing packet to Flow Control: size=%zu", a_size);
        
        int l_ret = dap_io_flow_ctrl_recv(l_flow_ctrl, a_data, a_size);
        if (l_ret != 0) {
            log_it(L_WARNING, "Flow Control packet processing failed: ret=%d", l_ret);
            return -3;
        }
        
        return 0;  // Flow control handles the rest
    } else {
        // FLOW CONTROL DISABLED: Process encrypted packet directly
        debug_if(s_debug_more, L_DEBUG,
                 "Processing encrypted packet directly (Flow Control disabled): size=%zu", a_size);
        
        return s_process_encrypted_udp_packet(l_session, a_data, a_size);
    }
}


/**
 * @brief Callback to get remote address for datagram flow (SERVER side)
 * 
 * For SERVER flows: returns client address from stream's trans UDP context
 * 
 * This callback resolves circular dependencies between stream and datagram layers.
 */
static bool s_get_remote_addr_cb(dap_io_flow_datagram_t *a_flow,
                                  struct sockaddr_storage *a_addr_out,
                                  socklen_t *a_addr_len_out)
{
    if (!a_flow || !a_addr_out || !a_addr_len_out) {
        log_it(L_ERROR, "Invalid arguments to s_get_remote_addr_cb (SERVER)");
        return false;
    }
    
    // Get stream from protocol_data (set in s_udp_protocol_finalize_cb)
    dap_stream_t *l_stream = (dap_stream_t*)a_flow->protocol_data;
    if (!l_stream) {
        log_it(L_ERROR, "No stream in datagram flow protocol_data! (SERVER)");
        return false;
    }
    
    // SERVER flow: get address from flow->remote_addr (filled by datagram layer)
    if (a_flow->remote_addr.ss_family == 0 || a_flow->remote_addr_len == 0) {
        log_it(L_ERROR, "SERVER flow has no remote_addr!");
        return false;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "SERVER s_get_remote_addr_cb: returning remote_addr=%s",
             dap_io_flow_socket_addr_to_string(&a_flow->remote_addr));
    
    memcpy(a_addr_out, &a_flow->remote_addr, sizeof(struct sockaddr_storage));
    *a_addr_len_out = a_flow->remote_addr_len;
    return true;
}

/**
 * @brief Protocol create callback - allocate stream_udp_session_t
 */
static dap_io_flow_datagram_t* s_udp_protocol_create_cb(dap_io_flow_server_t *a_server,
                                                     dap_io_flow_datagram_t *a_flow)
{
    UNUSED(a_flow);  // We allocate fresh structure
    
    // Get UDP server from flow server's _inheritor
    dap_net_trans_udp_server_t *l_udp_server = (dap_net_trans_udp_server_t*)dap_io_flow_server_get_inheritor(a_server);
    if (!l_udp_server) {
        log_it(L_ERROR, "Flow server has no UDP server in _inheritor");
        return NULL;
    }
    
    // Allocate FULL structure (stream_udp_session_t extends dap_io_flow_datagram_t)
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
    
    // CRITICAL: Set trans from UDP server so stream can use trans->ops->write for sending packets!
    l_session->stream->trans = l_udp_server->trans;
    if (!l_session->stream->trans) {
        log_it(L_WARNING, "UDP server has no trans configured");
    }
    
    // CRITICAL: Set backlink from stream to session for trans->ops->write!
    l_session->stream->_server_session = l_session;
    
    // stream_worker will be set in finalize callback after listener_es is available
    l_session->stream->stream_worker = NULL;
    l_session->base.base.stream_context = l_session->stream;
    
    // Initialize encryption key to NULL (will be set during handshake)
    l_session->encryption_key = NULL;
    l_session->session = NULL;
    l_session->session_id = 0;
    
    // Initialize handshake state protection
    atomic_store(&l_session->kem_task_pending, false);
    
    debug_if(s_debug_more, L_DEBUG,
             "Allocated stream_udp_session_t at %p (stream=%p)",
             l_session, l_session->stream);
    
    return &l_session->base;  // Return pointer to base dap_io_flow_datagram_t
}

/**
 * @brief Protocol finalize callback - complete initialization after UDP layer setup
 */
static int s_udp_protocol_finalize_cb(dap_io_flow_datagram_t *a_flow)
{
    if (!a_flow || !a_flow->listener_es) {
        log_it(L_ERROR, "Invalid flow or listener_es in finalize");
        return -1;
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t*)a_flow;
    
    // Now we can set stream_worker using listener_es->worker
    l_session->stream->stream_worker = DAP_STREAM_WORKER(a_flow->listener_es->worker);
    
    // CRITICAL: Set callback for getting remote address (resolves circular dependencies)
    a_flow->get_remote_addr_cb = s_get_remote_addr_cb;
    a_flow->protocol_data = l_session->stream;  // Store stream for callback
    
    // CRITICAL: Link SERVER flow to stream (same as CLIENT)
    l_session->stream->flow = &l_session->base;
    
    debug_if(s_debug_more, L_DEBUG,
             "Finalized stream_udp_session_t %p (stream=%p, worker=%u)",
             l_session, l_session->stream,
             a_flow->listener_es->worker ? a_flow->listener_es->worker->id : 0);
    
    return 0;
}

/**
 * @brief Protocol destroy callback - cleanup stream_udp_session_t
 */
static void s_udp_protocol_destroy_cb(dap_io_flow_datagram_t *a_flow)
{
    if (!a_flow) {
        return;
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t*)a_flow;
    
    // CRITICAL: Delete Flow Control FIRST to stop retransmits!
    // This prevents use-after-free when FC tries to send after flow is deleted.
    // Flow control is in base flow structure now
    if (l_session->base.base.flow_ctrl) {
        dap_io_flow_ctrl_delete(l_session->base.base.flow_ctrl);
        l_session->base.base.flow_ctrl = NULL;
    }
    
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
    dap_net_session_params_t *l_params = (dap_net_session_params_t*)a_session_params;
    
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
    
    // NOTE: Do NOT copy encryption_key here! It will be set later after session key derivation
    // to avoid dangling pointer when handshake key is deleted.
    
    // CRITICAL: Link session to stream for dap_stream_data_proc_read_ext!
    // dap_stream.c relies on stream->session->key for packet decryption
    if (l_session->stream) {
        l_session->stream->session = l_stream_session;
        debug_if(s_debug_more,L_DEBUG, "Linked stream->session for stream %p", l_session->stream);
        
        // CRITICAL: Create channels from session params!
        if (l_params && l_params->channels) {
            size_t l_channel_count = strlen(l_params->channels);
            debug_if(s_debug_more,L_INFO, "Creating %zu channels for session: %s", l_channel_count, l_params->channels);
            
            // Copy channels to session->active_channels
            strncpy(l_stream_session->active_channels, l_params->channels, 
                   sizeof(l_stream_session->active_channels) - 1);
            l_stream_session->active_channels[sizeof(l_stream_session->active_channels) - 1] = '\0';
            
            // Create each channel
            for (size_t i = 0; i < l_channel_count; i++) {
                char l_ch_id = l_params->channels[i];
                dap_stream_ch_t *l_ch = dap_stream_ch_new(l_session->stream, (uint8_t)l_ch_id);
                if (!l_ch) {
                    log_it(L_ERROR, "Failed to create channel '%c'", l_ch_id);
                    continue;
                }
                l_ch->ready_to_read = true;
                debug_if(s_debug_more,L_INFO, "Created channel '%c' for stream %p", l_ch_id, l_session->stream);
            }
            
            debug_if(s_debug_more,L_INFO, "Stream %p now has %zu channels", l_session->stream, l_session->stream->channel_count);
        }
    }
    
    // Create Flow Control (DAP_IO_FLOW_CTRL_RELIABLE: retransmit + reorder, NO keepalive)
    // DAP Stream has its own keep-alive mechanism, so we don't enable flow control keep-alive
    // CRITICAL: Window sizes must be large enough to handle fragmented packets!
    // For 10MB @ 988 bytes/fragment = ~10,600 fragments. Use 64K window for safety.
    // Retransmit timeout MUST be aggressive for high packet rate scenarios!
    dap_io_flow_ctrl_config_t l_fc_config = {
        .retransmit_timeout_ms = 100,      // 100ms for localhost (was 1000ms - TOO SLOW!)
        .max_retransmit_count = 20,        // Increased for large transfers with many packets
        .send_window_size = 16384*4,       // 64K packets (was 256 - CAUSED PACKET LOSS!)
        .recv_window_size = 16384*4,       // 64K packets (was 256 - CAUSED PACKET LOSS!)
        .max_out_of_order_delay_ms = 10000, // 10 seconds for large transfers
        .keepalive_interval_ms = 0,        // Not used (DAP Stream handles keep-alive)
        .keepalive_timeout_ms = 0,         // Not used
    };
    
    dap_io_flow_ctrl_t *l_flow_ctrl = dap_io_flow_ctrl_create(
        &l_session->base.base,             // dap_io_flow_t*
        DAP_IO_FLOW_CTRL_RELIABLE,         // Flags: RETRANSMIT | REORDER (no KEEPALIVE)
        &l_fc_config,
        &s_flow_ctrl_callbacks
    );
    
    // Use atomic store for thread safety (flow_ctrl accessed from multiple workers)
    // NOTE: flow_ctrl is in base.base (dap_io_flow_t), not directly in session
    l_session->base.base.flow_ctrl = l_flow_ctrl;
    
    if (!l_flow_ctrl) {
        log_it(L_ERROR, "Failed to create Flow Control for session");
        // Continue without flow control (fallback to unreliable UDP)
    } else {
        log_it(L_INFO, "Flow Control enabled for session (RELIABLE mode: retransmit + reorder)");
    }
    
    return l_stream_session;
}

static void s_stream_udp_session_close_cb(dap_io_flow_t *a_flow, void *a_session_context)
{
    if (!a_flow || !a_session_context) {
        return;
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t*)a_flow;
    dap_stream_session_t *l_stream_session = (dap_stream_session_t*)a_session_context;
    
    // Delete Flow Control
    // NOTE: flow_ctrl is in base.base (dap_io_flow_t), not directly in session
    // dap_io_flow_ctrl_delete is now synchronous - waits for all active operations
    dap_io_flow_ctrl_t *l_flow_ctrl = l_session->base.base.flow_ctrl;
    if (l_flow_ctrl) {
        l_session->base.base.flow_ctrl = NULL;  // Clear pointer first
        dap_io_flow_ctrl_delete(l_flow_ctrl);   // Then delete (waits for operations)
        debug_if(s_debug_more, L_DEBUG, "Flow Control deleted for session");
    }
    
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
    
    // CRITICAL: Log destination address for this send
    char l_dest_addr[64] = "UNKNOWN";
    if (a_session->base.remote_addr.ss_family == AF_INET) {
        struct sockaddr_in *l_sin = (struct sockaddr_in*)&a_session->base.remote_addr;
        snprintf(l_dest_addr, sizeof(l_dest_addr), "%s:%u", 
                 inet_ntoa(l_sin->sin_addr), ntohs(l_sin->sin_port));
    }
    
    debug_if(s_debug_more,L_DEBUG, "s_send_udp_packet: type=%u, size=%zu, session=0x%lx, dest=%s, flow=%p",
           a_type, a_payload_size, a_session->session_id, l_dest_addr, (void*)&a_session->base);
    
    // Get sequence number from UDP flow
    uint32_t l_seq_num = atomic_load(&a_session->base.seq_num_out);
    
    // HANDSHAKE packets are OBFUSCATED (size-based encryption)
    if (a_type == DAP_STREAM_UDP_PKT_HANDSHAKE) {
        // NO SIZE VALIDATION HERE!
        // Alice sends: 800 bytes (CRYPTO_PUBLICKEYBYTES)
        // Bob sends: 768 bytes (CRYPTO_CIPHERTEXTBYTES)
        // Size varies, validation is at protocol level (before calling s_send_udp_packet)
        
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
        l_ret = dap_io_flow_datagram_send(&a_session->base, l_obfuscated, l_obfuscated_size);
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
    
    // ALL OTHER PACKETS: ВСЕГДА используем новую схему (FC+UDP full_header)!
    
    // Get flow_ctrl - may be NULL if session is closing
    // NOTE: flow_ctrl is in base.base (dap_io_flow_t), not directly in session
    dap_io_flow_ctrl_t *l_flow_ctrl = a_session->base.base.flow_ctrl;
    
    if (!a_session->encryption_key && !l_flow_ctrl) {
        log_it(L_ERROR, "No encryption key and no Flow Control for sending packet (type=%u)", a_type);
        return -4;
    }
    
    int l_ret;
    
    // CRITICAL: SESSION_CREATE response must be sent WITHOUT Flow Control!
    // It should not occupy seq=1 in FC sequence, allowing DATA packets to start from seq=1.
    // SESSION_CREATE uses its own encryption with handshake key.
    bool l_use_fc = (l_flow_ctrl != NULL) && (a_type != DAP_STREAM_UDP_PKT_SESSION_CREATE);
    
    // If Flow Control is enabled AND not SESSION_CREATE: send PURE PAYLOAD
    // FC callback построит full_header и зашифрует
    if (l_use_fc) {
        atomic_store(&a_session->last_send_type, a_type);
        
        debug_if(s_debug_more, L_DEBUG,
                 "Sending PURE PAYLOAD to FC: type=%u, payload_size=%zu", a_type, a_payload_size);
        
        l_ret = dap_io_flow_ctrl_send(l_flow_ctrl, a_payload, a_payload_size);
        
        if (l_ret != 0) {
            debug_if(s_debug_more, L_DEBUG,
                     "Flow Control send failed (type=%u): ret=%d%s",
                     a_type, l_ret, l_ret == -10 ? " (FC deleting)" : "");
            return -8;
        }
        
        debug_if(s_debug_more, L_DEBUG,
                 "Payload sent via FC: type=%u, seq=%u, session=0x%lx",
                 a_type, l_seq_num, a_session->session_id);
        return 0;
    }
    
    // FLOW CONTROL DISABLED: Используем ТУ ЖЕ схему (full_header), шифруем вручную
    if (!a_session->encryption_key) {
        log_it(L_ERROR, "No encryption key for direct send (type=%u)", a_type);
        return -5;
    }
    
    // Build full header (ТА ЖЕ структура что и в FC!)
    dap_stream_trans_udp_full_header_t l_full_hdr = {
        .seq_num = l_seq_num,
        .ack_seq = 0,
        .timestamp_ms = (uint32_t)(dap_nanotime_now() / 1000000),
        .fc_flags = 0,
        .type = a_type,
        .session_id = a_session->session_id,
    };
    
    // Serialize using dap_serialize
    size_t l_hdr_size = sizeof(dap_stream_trans_udp_full_header_t);
    uint8_t l_hdr_buffer[sizeof(dap_stream_trans_udp_full_header_t)];
    
    dap_serialize_result_t l_ser_result = dap_serialize_to_buffer_raw(
        &g_udp_full_header_schema, &l_full_hdr, l_hdr_buffer, l_hdr_size, NULL);
    
    if (l_ser_result.error_code != 0) {
        log_it(L_ERROR, "Failed to serialize full header: %s",
               l_ser_result.error_message ? l_ser_result.error_message : "unknown");
        return -6;
    }
    
    // Build cleartext: [serialized_header + payload]
    size_t l_cleartext_size = l_hdr_size + a_payload_size;
    uint8_t *l_cleartext = DAP_NEW_SIZE(uint8_t, l_cleartext_size);
    if (!l_cleartext) {
        log_it(L_ERROR, "Failed to allocate cleartext buffer");
        return -7;
    }
    
    memcpy(l_cleartext, l_hdr_buffer, l_hdr_size);
    if (a_payload && a_payload_size > 0) {
        memcpy(l_cleartext + l_hdr_size, a_payload, a_payload_size);
    }
    
    // Encrypt
    size_t l_encrypted_max = l_cleartext_size + 256;
    uint8_t *l_encrypted = DAP_NEW_SIZE(uint8_t, l_encrypted_max);
    if (!l_encrypted) {
        log_it(L_ERROR, "Failed to allocate encryption buffer");
        DAP_DELETE(l_cleartext);
        return -8;
    }
    
    size_t l_encrypted_size = dap_enc_code(a_session->encryption_key,
                                           l_cleartext, l_cleartext_size,
                                           l_encrypted, l_encrypted_max,
                                           DAP_ENC_DATA_TYPE_RAW);
    
    DAP_DELETE(l_cleartext);
    
    if (l_encrypted_size == 0) {
        log_it(L_ERROR, "Failed to encrypt packet (type=%u)", a_type);
        DAP_DELETE(l_encrypted);
        return -9;
    }
    
    // Send
    debug_if(s_debug_more, L_DEBUG,
             "Sending ENCRYPTED (FC disabled): type=%u, size=%zu", a_type, l_encrypted_size);
    
    l_ret = dap_io_flow_datagram_send(&a_session->base, l_encrypted, l_encrypted_size);
    DAP_DELETE(l_encrypted);
    
    if (l_ret < 0) {
        log_it(L_ERROR, "Failed to send encrypted packet (type=%u)", a_type);
        return -10;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "Encrypted packet sent: type=%u, seq=%u, session=0x%lx, encrypted_size=%zu",
             a_type, l_seq_num, a_session->session_id, l_encrypted_size);
    
    return 0;
}

// =============================================================================
// PROTOCOL PACKET HANDLERS
// =============================================================================



//===================================================================
// PACKET PROCESSING HELPERS
//===================================================================

/**
 * @brief Process encrypted UDP packet (decrypt + dispatch)
 * 
 * This function is called either directly from s_udp_packet_received_cb
 * (if Flow Control is disabled) or from s_flow_ctrl_payload_deliver_cb
 * (if Flow Control is enabled).
 * 
 * @param a_session UDP session
 * @param a_encrypted_data Encrypted packet data
 * @param a_encrypted_size Encrypted packet size
 * @return 0 on success, negative on error
 */
static int s_process_encrypted_udp_packet(stream_udp_session_t *a_session,
                                          const uint8_t *a_encrypted_data,
                                          size_t a_encrypted_size)
{
    if (!a_session || !a_encrypted_data || a_encrypted_size == 0) {
        return -1;
    }
    
    if (!a_session->encryption_key) {
        log_it(L_WARNING, "Received encrypted packet but no encryption key established");
        return -3;
    }
    
    // Allocate buffer for decrypted data
    size_t l_decrypted_max = a_encrypted_size + 256;  // Extra space for decryption
    uint8_t *l_decrypted = DAP_NEW_SIZE(uint8_t, l_decrypted_max);
    if (!l_decrypted) {
        log_it(L_ERROR, "Failed to allocate decryption buffer");
        return -4;
    }
    
    // Decrypt entire packet
    size_t l_decrypted_size = dap_enc_decode(a_session->encryption_key,
                                             a_encrypted_data, a_encrypted_size,
                                             l_decrypted, l_decrypted_max,
                                             DAP_ENC_DATA_TYPE_RAW);
    
    if (l_decrypted_size == 0) {
        log_it(L_ERROR, "Failed to decrypt UDP packet");
        DAP_DELETE(l_decrypted);
        return -5;
    }
    
    // Parse NEW full header using dap_serialize
    if (l_decrypted_size < sizeof(dap_stream_trans_udp_full_header_t)) {
        log_it(L_ERROR, "Decrypted packet too small for full header (%zu bytes)", l_decrypted_size);
        DAP_DELETE(l_decrypted);
        return -6;
    }
    
    // Deserialize full header
    dap_stream_trans_udp_full_header_t l_hdr;
    dap_deserialize_result_t l_deser_result = dap_deserialize_from_buffer_raw(
        &g_udp_full_header_schema,
        l_decrypted,
        sizeof(dap_stream_trans_udp_full_header_t),
        &l_hdr,
        NULL
    );
    
    if (l_deser_result.error_code != 0) {
        log_it(L_ERROR, "Failed to deserialize full header: %s",
               l_deser_result.error_message ? l_deser_result.error_message : "unknown");
        DAP_DELETE(l_decrypted);
        return -6;
    }
    
    // Extract and validate fields
    uint8_t l_type = l_hdr.type;
    uint64_t l_seq_num = l_hdr.seq_num;
    uint64_t l_session_id = l_hdr.session_id;
    
    // Validate packet type
    if (l_type < DAP_STREAM_UDP_PKT_SESSION_CREATE || l_type > DAP_STREAM_UDP_PKT_CLOSE) {
        log_it(L_ERROR, "Invalid packet type: %u", l_type);
        DAP_DELETE(l_decrypted);
        return -7;
    }
    
    // Validate session_id
    if (a_session->session_id != 0 && a_session->session_id != l_session_id) {
        log_it(L_ERROR, "Session ID mismatch: packet=0x%lx, session=0x%lx",
               l_session_id, a_session->session_id);
        DAP_DELETE(l_decrypted);
        return -8;
    }
    
    // REPLAY PROTECTION: Validate sequence number
    // NOTE: If Flow Control is enabled, replay protection is handled by flow control layer
    // This is legacy protection for when Flow Control is disabled
    // NOTE: flow_ctrl is in base.base (dap_io_flow_t), not directly in session
    if (!a_session->base.base.flow_ctrl) {
        uint64_t l_last_seq = atomic_load(&a_session->base.last_seq_num_in);
        
        // Check for sequence number advance (handle wraparound)
        int64_t l_seq_diff = (int64_t)(l_seq_num - l_last_seq);
        
        if (l_seq_diff <= 0 && l_last_seq != 0) {
            // seq_num is less than or equal to last seen seq_num (possible replay)
            log_it(L_WARNING, "Replay attack detected: seq_num=%lu, last_seq=%lu (session=0x%lx)",
                   l_seq_num, l_last_seq, l_session_id);
            DAP_DELETE(l_decrypted);
            return -9;
        }
        
        // Update last seen sequence number
        atomic_store(&a_session->base.last_seq_num_in, l_seq_num);
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "Decrypted packet: type=%u, seq=%lu, session=0x%lx",
             l_type, l_seq_num, l_session_id);
    
    // Extract payload (after full header)
    const uint8_t *l_payload = l_decrypted + sizeof(dap_stream_trans_udp_full_header_t);
    size_t l_payload_size = l_decrypted_size - sizeof(dap_stream_trans_udp_full_header_t);
    
    // Dispatch by packet type
    int l_result = 0;
    switch (l_type) {
        case DAP_STREAM_UDP_PKT_SESSION_CREATE:
            l_result = s_handle_session_create(a_session, l_payload, l_payload_size);
            break;
            
        case DAP_STREAM_UDP_PKT_DATA:
            l_result = s_handle_data(a_session, l_payload, l_payload_size);
            break;
            
        case DAP_STREAM_UDP_PKT_KEEPALIVE:
            l_result = s_handle_keepalive(a_session);
            break;
            
        case DAP_STREAM_UDP_PKT_CLOSE:
            l_result = s_handle_close(a_session);
            break;
            
        default:
            log_it(L_ERROR, "Unhandled packet type: %u", l_type);
            l_result = -10;
            break;
    }
    
    DAP_DELETE(l_decrypted);
    return l_result;
}

//===================================================================
// PROTOCOL PACKET HANDLERS
//===================================================================

/**
 * @brief KEM task function (executed in thread pool)
 * 
 * Performs heavy cryptographic operations:
 * 1. Generate Bob's ephemeral Kyber512 key
 * 2. Perform KEM encapsulation with Alice's public key
 * 3. Derive handshake encryption key via KDF-SHAKE256
 * 
 * @param a_arg Pointer to kem_task_ctx_t
 * @return Pointer to kem_task_result_t (must be freed by callback)
 */
static void* s_kem_task_func(void *a_arg)
{
    kem_task_ctx_t *l_ctx = (kem_task_ctx_t *)a_arg;
    if (!l_ctx) {
        return NULL;
    }
    
    kem_task_result_t *l_result = DAP_NEW_Z(kem_task_result_t);
    if (!l_result) {
        return NULL;
    }
    
    l_result->session_id = l_ctx->session->session_id;
    l_result->error_code = 0;
    
    // Generate ephemeral Bob key (Kyber512)
    dap_enc_key_t *l_bob_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    
    if (!l_bob_key) {
        log_it(L_ERROR, "[KEM Task] Failed to generate Bob KEM key");
        l_result->error_code = -2;
        return l_result;
    }
    
    void *l_bob_pub = NULL;
    size_t l_shared_key_size = 0;
    
    // Perform KEM encapsulation (Bob side)
    if (l_bob_key->gen_bob_shared_key) {
        l_shared_key_size = l_bob_key->gen_bob_shared_key(l_bob_key, l_ctx->alice_pub_key, l_ctx->alice_pub_key_size, &l_bob_pub);
        
        if (!l_bob_pub || l_shared_key_size == 0 || !l_bob_key->shared_key) {
            log_it(L_ERROR, "[KEM Task] Failed to generate shared key from Alice's public key");
            dap_enc_key_delete(l_bob_key);
            l_result->error_code = -3;
            return l_result;
        }
        
        // Copy Bob's ciphertext for response
        l_result->bob_ciphertext_size = l_shared_key_size;
        l_result->bob_ciphertext = DAP_NEW_SIZE(uint8_t, l_shared_key_size);
        if (!l_result->bob_ciphertext) {
            dap_enc_key_delete(l_bob_key);
            l_result->error_code = -4;
            return l_result;
        }
        memcpy(l_result->bob_ciphertext, l_bob_pub, l_shared_key_size);
    } else {
        log_it(L_ERROR, "[KEM Task] Key type doesn't support KEM handshake");
        dap_enc_key_delete(l_bob_key);
        l_result->error_code = -5;
        return l_result;
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
        log_it(L_ERROR, "[KEM Task] Failed to derive handshake key via KDF");
        dap_enc_key_delete(l_bob_key);
        DAP_DELETE(l_result->bob_ciphertext);
        l_result->error_code = -6;
        return l_result;
    }
    
    l_result->handshake_key = l_handshake_key;
    
    // Cleanup Bob's key (shared secret is now in handshake_key)
    dap_enc_key_delete(l_bob_key);
    
    return l_result;
}

/**
 * @brief Structure for scheduling reactor callback from KEM worker thread
 */
typedef struct kem_reactor_callback_arg {
    stream_udp_session_t *session;
    kem_task_result_t *result;
} kem_reactor_callback_arg_t;

/**
 * @brief KEM reactor callback (called IN REACTOR THREAD)
 * 
 * This is the SECOND stage callback that runs in the reactor thread.
 * Here we can safely access session and send packets.
 * 
 * @param a_arg Pointer to kem_reactor_callback_arg_t
 */
static void s_kem_reactor_callback(void *a_arg)
{
    kem_reactor_callback_arg_t *l_arg = (kem_reactor_callback_arg_t *)a_arg;
    if (!l_arg) {
        log_it(L_ERROR, "[KEM Reactor] Invalid argument");
        return;
    }
    
    stream_udp_session_t *l_session = l_arg->session;
    kem_task_result_t *l_result = l_arg->result;
    
    if (!l_result) {
        log_it(L_ERROR, "[KEM Reactor] No result");
        DAP_DELETE(l_arg);
        return;
    }
    
    // Validate session still exists
    if (!l_session || !l_session->base.base.stream_context || !l_session->base.listener_es) {
        debug_if(s_debug_more, L_DEBUG,
                 "[KEM Reactor] Session %p invalid or deleted", l_session);
        goto cleanup_reactor;
    }
    
    if (l_result->error_code != 0) {
        log_it(L_ERROR, "[KEM Reactor] KEM task failed with error %d", l_result->error_code);
        goto cleanup_reactor;
    }
    
    // Store handshake key in session (NOW SAFE - in reactor thread!)
    l_session->encryption_key = l_result->handshake_key;
    
    debug_if(s_debug_more, L_DEBUG,
             "[KEM Reactor] Stored encryption_key=%p for session %p (session_id=0x%lx)",
             l_session->encryption_key, l_session, l_session->session_id);
    
    // Build handshake response: Bob's ciphertext + session_id
    size_t l_response_size = l_result->bob_ciphertext_size + sizeof(uint64_t);
    uint64_t l_session_id_be = htobe64(l_result->session_id);
    uint8_t *l_response = dap_serialize_multy(NULL, l_response_size,
                                              l_result->bob_ciphertext, l_result->bob_ciphertext_size,
                                              &l_session_id_be, sizeof(uint64_t),
                                              DOOF_PTR);
    if (!l_response) {
        log_it(L_ERROR, "[KEM Reactor] Failed to serialize handshake response");
        goto cleanup_reactor;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "[KEM Reactor] HANDSHAKE response: Bob ciphertext (%zu bytes) + session_id (0x%lx)",
             l_result->bob_ciphertext_size, l_result->session_id);
    
    // Send handshake response (NOW SAFE - in reactor thread!)
    int l_ret = s_send_udp_packet(l_session,
                                  DAP_STREAM_UDP_PKT_HANDSHAKE,
                                  l_response, l_response_size);
    
    DAP_DELETE(l_response);
    
    debug_if(s_debug_more, L_DEBUG, "[KEM Reactor] HANDSHAKE response sent (ret=%d)", l_ret);
    
cleanup_reactor:
    // Reset kem_task_pending flag (allow retries on error)
    // NOTE: On success, encryption_key is set so duplicate check will catch retransmits
    if (l_session) {
        atomic_store(&l_session->kem_task_pending, false);
    }
    
    // Free result
    if (l_result) {
        DAP_DELETE(l_result->bob_ciphertext);
        DAP_DELETE(l_result);
    }
    DAP_DELETE(l_arg);
}

/**
 * @brief KEM task completion callback (called from worker thread)
 * 
 * This callback is executed in the thread pool worker context.
 * It schedules a reactor callback to complete the handshake.
 * 
 * @param a_pool Thread pool that executed the task
 * @param a_worker_thread Worker thread that executed the task
 * @param a_result Pointer to kem_task_result_t (from task function)
 * @param a_arg Pointer to kem_task_ctx_t (original context)
 */
static void s_kem_task_callback(dap_thread_pool_t *a_pool,
                                dap_thread_t a_worker_thread,
                                void *a_result,
                                void *a_arg)
{
    kem_task_result_t *l_result = (kem_task_result_t *)a_result;
    kem_task_ctx_t *l_ctx = (kem_task_ctx_t *)a_arg;
    
    debug_if(s_debug_more, L_DEBUG,
             "[KEM Callback] Worker thread %lu (pool %p) completed KEM task",
             (unsigned long)a_worker_thread, a_pool);
    
    if (!l_result || !l_ctx) {
        log_it(L_ERROR, "[KEM Callback] Invalid arguments");
        if (l_result) {
            DAP_DELETE(l_result->bob_ciphertext);
            DAP_DELETE(l_result);
        }
        if (l_ctx) {
            DAP_DELETE(l_ctx->alice_pub_key);
            DAP_DELETE(l_ctx);
        }
        return;
    }
    
    // Prepare argument for reactor callback
    kem_reactor_callback_arg_t *l_reactor_arg = DAP_NEW_Z(kem_reactor_callback_arg_t);
    if (!l_reactor_arg) {
        log_it(L_CRITICAL, "[KEM Callback] Failed to allocate reactor callback arg");
        DAP_DELETE(l_result->bob_ciphertext);
        DAP_DELETE(l_result);
        DAP_DELETE(l_ctx->alice_pub_key);
        DAP_DELETE(l_ctx);
        return;
    }
    
    l_reactor_arg->session = l_ctx->session;
    l_reactor_arg->result = l_result;
    
    // Get session's OWNER worker (NOT listener's worker!)
    // CRITICAL FIX: For Application-level LB, listener is on worker 0 but flow
    // may be on any worker (determined by hash). Must use flow's owner_worker_id
    // to avoid data race when modifying session fields.
    stream_udp_session_t *l_session = l_ctx->session;
    if (!l_session) {
        log_it(L_ERROR, "[KEM Callback] Session invalid");
        DAP_DELETE(l_result->bob_ciphertext);
        DAP_DELETE(l_result);
        DAP_DELETE(l_reactor_arg);
        DAP_DELETE(l_ctx->alice_pub_key);
        DAP_DELETE(l_ctx);
        return;
    }
    
    // Use flow's owner_worker_id instead of listener_es->worker
    uint32_t l_owner_worker_id = l_session->base.base.owner_worker_id;
    dap_worker_t *l_worker = dap_events_worker_get(l_owner_worker_id);
    
    // Fallback to listener's worker if owner worker not available (shouldn't happen)
    if (!l_worker && l_session->base.listener_es && l_session->base.listener_es->worker) {
        log_it(L_WARNING, "[KEM Callback] Owner worker %u not found, falling back to listener's worker",
               l_owner_worker_id);
        l_worker = l_session->base.listener_es->worker;
    }
    
    if (!l_worker) {
        log_it(L_ERROR, "[KEM Callback] No valid worker found for session");
        DAP_DELETE(l_result->bob_ciphertext);
        DAP_DELETE(l_result);
        DAP_DELETE(l_reactor_arg);
        DAP_DELETE(l_ctx->alice_pub_key);
        DAP_DELETE(l_ctx);
        return;
    }
    
    // Schedule reactor callback on OWNER worker (thread-safe session modification)
    dap_worker_exec_callback_on(l_worker, s_kem_reactor_callback, l_reactor_arg);
    
    debug_if(s_debug_more, L_DEBUG,
             "[KEM Callback] Scheduled reactor callback for session %p on worker %p",
             l_session, l_worker);
    
    // Cleanup context (result will be freed in reactor callback)
    DAP_DELETE(l_ctx->alice_pub_key);
    DAP_DELETE(l_ctx);
}

//===================================================================
// PROTOCOL PACKET HANDLERS (Synchronous fallback)
//===================================================================

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
    
    // CRITICAL: Prevent duplicate HANDSHAKE processing!
    // Client may retransmit HANDSHAKE if it doesn't receive response in time.
    // Without this check, we'd create multiple KEM tasks causing race conditions.
    if (a_session->encryption_key != NULL) {
        debug_if(s_debug_more, L_DEBUG,
                 "SERVER: Ignoring duplicate HANDSHAKE - encryption_key already set for session %p",
                 a_session);
        return 0;  // Already completed handshake, ignore duplicate
    }
    
    // Use atomic CAS to prevent concurrent KEM task creation
    bool l_expected = false;
    if (!atomic_compare_exchange_strong(&a_session->kem_task_pending, &l_expected, true)) {
        debug_if(s_debug_more, L_DEBUG,
                 "SERVER: Ignoring duplicate HANDSHAKE - KEM task already pending for session %p",
                 a_session);
        return 0;  // KEM task already in progress, ignore duplicate
    }
    
    // Now kem_task_pending is true, and we have exclusive ownership to create KEM task

    // QoS probe detection: payload starts with DAP_QOS_PROBE_MAGIC → echo, skip KEM
    if (dap_qos_is_probe(a_payload, a_payload_size)) {
        debug_if(s_debug_more, L_DEBUG, "QoS probe detected (%zu bytes), building echo", a_payload_size);
        atomic_store(&a_session->kem_task_pending, false);

        void  *l_echo = NULL;
        size_t l_echo_size = 0;
        if (dap_qos_build_echo(a_payload, a_payload_size, &l_echo, &l_echo_size) == 0) {
            if (l_echo_size <= 65507) {
                s_send_udp_packet(a_session, DAP_STREAM_UDP_PKT_HANDSHAKE,
                                  (const uint8_t *)l_echo, l_echo_size);
            } else {
                log_it(L_WARNING, "QoS BW echo too large for UDP (%zu bytes), sending header only",
                       l_echo_size);
                s_send_udp_packet(a_session, DAP_STREAM_UDP_PKT_HANDSHAKE,
                                  (const uint8_t *)l_echo, sizeof(dap_qos_echo_pkt_t));
            }
            DAP_DELETE(l_echo);
        }
        return 0;
    }

    // Check if thread pool is available
    if (!s_kem_thread_pool) {
        log_it(L_WARNING, "KEM thread pool not available, falling back to synchronous processing");
        atomic_store(&a_session->kem_task_pending, false);  // Reset flag
        // TODO: Implement synchronous fallback (same code as s_kem_task_func)
        return -10;
    }
    
    // Create task context
    kem_task_ctx_t *l_ctx = DAP_NEW_Z(kem_task_ctx_t);
    if (!l_ctx) {
        log_it(L_ERROR, "Failed to allocate KEM task context");
        atomic_store(&a_session->kem_task_pending, false);  // Reset flag on failure
        return -2;
    }
    
    l_ctx->session = a_session;
    l_ctx->session_uuid = 0;  // UUID validation not needed - encryption_key check is sufficient
    l_ctx->alice_pub_key_size = a_payload_size;
    l_ctx->alice_pub_key = DAP_NEW_SIZE(uint8_t, a_payload_size);
    
    if (!l_ctx->alice_pub_key) {
        log_it(L_ERROR, "Failed to allocate Alice public key buffer");
        atomic_store(&a_session->kem_task_pending, false);  // Reset flag on failure
        DAP_DELETE(l_ctx);
        return -3;
    }
    
    memcpy(l_ctx->alice_pub_key, a_payload, a_payload_size);
    
    // Submit task to thread pool
    int l_ret = dap_thread_pool_submit(s_kem_thread_pool,
                                      s_kem_task_func,
                                      l_ctx,
                                      s_kem_task_callback,
                                      l_ctx);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to submit KEM task to thread pool: %d", l_ret);
        atomic_store(&a_session->kem_task_pending, false);  // Reset flag on failure
        DAP_DELETE(l_ctx->alice_pub_key);
        DAP_DELETE(l_ctx);
        return -4;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "HANDSHAKE: KEM task submitted to thread pool for session %p (session_id=0x%lx)",
             a_session, a_session->session_id);
    
    // Return immediately - response will be sent from callback
    return 0;
}

/**
 * @brief Handle SESSION_CREATE packet
 */
static int s_handle_session_create(stream_udp_session_t *a_session, const uint8_t *a_payload,
                                   size_t a_payload_size)
{
    if (!a_session || !a_payload || a_payload_size == 0) {
        log_it(L_ERROR, "Invalid arguments for SESSION_CREATE handler");
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "Processing SESSION_CREATE: payload_size=%zu (JSON plaintext)",
             a_payload_size);
    
    // NOTE: Payload from s_process_encrypted_udp_packet is already decrypted JSON plaintext
    // No inner encryption envelope anymore (simplified architecture)
    
    // Ensure null-termination for JSON parsing
    char *l_json_str = DAP_NEW_SIZE(char, a_payload_size + 1);
    if (!l_json_str) {
        log_it(L_ERROR, "Failed to allocate buffer for JSON string");
        return -3;
    }
    
    memcpy(l_json_str, a_payload, a_payload_size);
    l_json_str[a_payload_size] = '\0';
    
    debug_if(s_debug_more, L_DEBUG, "SERVER: SESSION_CREATE JSON: '%.80s'", l_json_str);
    
    // Parse JSON directly from payload
    json_object *l_json = json_tokener_parse(l_json_str);
    DAP_DELETE(l_json_str);
    
    if (!l_json) {
        log_it(L_ERROR, "Failed to parse SESSION_CREATE JSON (size=%zu)", a_payload_size);
        return -4;
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
    
    debug_if(s_debug_more, L_DEBUG, "SERVER: Deriving session key with KDF counter=%lu", l_kdf_counter);
    
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
    
    // CRITICAL: Set session key in stream->session IMMEDIATELY after derivation!
    // This must be done BEFORE sending SESSION_CREATE response, because client
    // may start sending data immediately after receiving the response.
    // We can safely set the new key here because incoming data packets will use session key,
    // while SESSION_CREATE response is encrypted with handshake key (still in a_session->encryption_key).
    if (a_session->stream && a_session->stream->session) {
        a_session->stream->session->key = l_session_key;
        debug_if(s_debug_more, L_DEBUG, "SERVER: Set session key in stream->session->key (stream=%p, session=%p, session key=%p)",
               a_session->stream, a_session->stream->session, l_session_key);
    } else {
        log_it(L_ERROR, "SERVER: Cannot set stream->session->key! stream=%p, session=%p",
               a_session->stream, a_session->stream ? a_session->stream->session : NULL);
    }
    
    log_it(L_INFO, "SESSION_CREATE completed: session_id=0x%lx", a_session->session_id);
    
    // CRITICAL: Send SESSION_CREATE response using HANDSHAKE key (still in a_session->encryption_key)!
    // Client will derive session key after receiving this counter, so response must use handshake key
    uint64_t l_counter_be = htobe64(l_kdf_counter);
    
    int l_ret = s_send_udp_packet(a_session,
                                  DAP_STREAM_UDP_PKT_SESSION_CREATE,
                                  (const uint8_t*)&l_counter_be, sizeof(l_counter_be));
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to send SESSION_CREATE response: %d", l_ret);
        // Rollback: remove session key from stream->session before deleting it
        if (a_session->stream && a_session->stream->session) {
            a_session->stream->session->key = a_session->encryption_key; // Restore handshake key
        }
        dap_enc_key_delete(l_session_key);
        return l_ret;
    }
    
    debug_if(s_debug_more, L_DEBUG, "SESSION_CREATE response sent (ret=%d), now replacing handshake key in a_session", l_ret);
    
    // NOW delete old handshake key and replace it in a_session (session key already set in stream->session)
    dap_enc_key_delete(a_session->encryption_key);
    a_session->encryption_key = l_session_key;
    
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
        debug_if(s_debug_more, L_DEBUG, "SERVER: calling s_stream_udp_stream_write_cb (stream=%p, size=%zu)",
               a_session->stream, a_payload_size);
        int l_ret = (int)s_stream_udp_stream_write_cb(&a_session->base.base,
                                                  a_session->base.base.stream_context,
                                                  a_payload,
                                                  a_payload_size);
        debug_if(s_debug_more, L_DEBUG, "SERVER: s_stream_udp_stream_write_cb returned %d", l_ret);
        return l_ret;
    } else {
        log_it(L_WARNING, "SERVER: s_handle_data: stream=%p or stream_context=%p is NULL!",
               a_session->stream, a_session->base.base.stream_context);
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

//===================================================================
// FLOW CONTROL CALLBACKS (NEW API with dap_serialize)
//===================================================================

/**
 * @brief Prepare packet with Flow Control header (NEW ARCHITECTURE)
 * 
 * CRITICAL: This callback ENCRYPTS entire packet!
 * 
 * Architecture:
 * 1. Payload приходит УЖЕ в виде [UDP old header + data] (НЕ ЗАШИФРОВАН!)
 * 2. Мы строим [FC+UDP full header]
 * 3. Собираем: [FC+UDP full header] + [legacy UDP header + data]
 * 4. ШИФРУЕМ весь блок
 * 5. Возвращаем зашифрованный пакет
 * 
 * DPI видит: случайные байты
 * 
 * @param a_flow Flow instance
 * @param a_metadata Flow control metadata (seq_num, ack_seq, etc)
 * @param a_payload CLEARTEXT payload (old UDP header + data)
 * @param a_payload_size Payload size
 * @param[out] a_packet_out ENCRYPTED packet buffer
 * @param[out] a_packet_size_out ENCRYPTED packet size
 * @param a_arg User argument (NULL for UDP)
 * @return 0 on success, negative on error
 */
static int s_flow_ctrl_packet_prepare_cb(dap_io_flow_t *a_flow,
                                          const dap_io_flow_pkt_metadata_t *a_metadata,
                                          const void *a_payload, size_t a_payload_size,
                                          void **a_packet_out, size_t *a_packet_size_out,
                                          void *a_arg)
{
    UNUSED(a_arg);
    
    debug_if(s_debug_more, L_DEBUG,
             "FC prepare_cb ENTRY: a_metadata=%p, seq=%lu, ack=%lu, ts=%u, keepalive=%d, retrans=%d",
             a_metadata, a_metadata ? a_metadata->seq_num : 0,
             a_metadata ? a_metadata->ack_seq : 0,
             a_metadata ? a_metadata->timestamp_ms : 0,
             a_metadata ? a_metadata->is_keepalive : 0,
             a_metadata ? a_metadata->is_retransmit : 0);
    
    if (!a_flow || !a_metadata || !a_packet_out || !a_packet_size_out) {
        return -1;
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t *)a_flow;
    
    // Keepalive packets have no payload
    if (!a_payload) {
        a_payload_size = 0;
    }
    
    // Build full UDP header (FC fields + UDP fields)
    // ВАЖНО: Payload содержит ЧИСТЫЙ DATA (без old_header)!
    // Type и session_id берём из session context!
    
    dap_stream_trans_udp_full_header_t l_full_hdr = {
        // FC fields
        .seq_num = a_metadata->seq_num,
        .ack_seq = a_metadata->ack_seq,
        .timestamp_ms = a_metadata->timestamp_ms,
        .fc_flags = 0,
        
        // UDP fields (из session context)
        .type = atomic_load(&l_session->last_send_type),
        .session_id = l_session->session_id,
    };
    
    // Set FC flags
    if (a_metadata->is_keepalive) {
        l_full_hdr.fc_flags |= DAP_IO_FLOW_CTRL_HDR_FLAG_KEEPALIVE;
    }
    if (a_metadata->is_retransmit) {
        l_full_hdr.fc_flags |= DAP_IO_FLOW_CTRL_HDR_FLAG_RETRANSMIT;
    }
    
    // Serialize full header to network byte order using dap_serialize
    size_t l_hdr_size = sizeof(dap_stream_trans_udp_full_header_t);
    uint8_t l_hdr_buffer[sizeof(dap_stream_trans_udp_full_header_t)];
    
    dap_serialize_result_t l_ser_result = dap_serialize_to_buffer_raw(
        &g_udp_full_header_schema,
        &l_full_hdr,
        l_hdr_buffer,
        l_hdr_size,
        NULL  // no context
    );
    
    if (l_ser_result.error_code != 0) {
        log_it(L_ERROR, "Failed to serialize UDP full header: %s", 
               l_ser_result.error_message ? l_ser_result.error_message : "unknown error");
        return -2;
    }
    
    // Build cleartext packet: [serialized_header] + [payload]
    size_t l_cleartext_size = l_hdr_size + a_payload_size;
    uint8_t *l_cleartext = DAP_NEW_SIZE(uint8_t, l_cleartext_size);
    if (!l_cleartext) {
        log_it(L_ERROR, "Failed to allocate cleartext buffer (%zu bytes)", l_cleartext_size);
        return -3;
    }
    
    memcpy(l_cleartext, l_hdr_buffer, l_hdr_size);
    if (a_payload_size > 0) {
        memcpy(l_cleartext + l_hdr_size, a_payload, a_payload_size);
    }
    
    // ENCRYPT entire packet
    if (!l_session->encryption_key) {
        log_it(L_ERROR, "No encryption key for FC packet prepare");
        DAP_DELETE(l_cleartext);
        return -3;
    }
    
    size_t l_encrypted_max = l_cleartext_size + 256;  // Encryption overhead
    uint8_t *l_encrypted = DAP_NEW_SIZE(uint8_t, l_encrypted_max);
    if (!l_encrypted) {
        log_it(L_ERROR, "Failed to allocate encryption buffer");
        DAP_DELETE(l_cleartext);
        return -4;
    }
    
    size_t l_encrypted_size = dap_enc_code(l_session->encryption_key,
                                           l_cleartext, l_cleartext_size,
                                           l_encrypted, l_encrypted_max,
                                           DAP_ENC_DATA_TYPE_RAW);
    DAP_DELETE(l_cleartext);
    
    if (l_encrypted_size == 0) {
        log_it(L_ERROR, "Failed to encrypt FC packet");
        DAP_DELETE(l_encrypted);
        return -5;
    }
    
    *a_packet_out = l_encrypted;
    *a_packet_size_out = l_encrypted_size;
    
    debug_if(s_debug_more, L_DEBUG, 
             "Prepared ENCRYPTED FC packet: seq=%lu, ack=%lu, type=%u, encrypted_size=%zu",
             l_full_hdr.seq_num, l_full_hdr.ack_seq, l_full_hdr.type, l_encrypted_size);
    
    return 0;
}

/**
 * @brief Parse packet and extract Flow Control header (NEW ARCHITECTURE)
 * 
 * CRITICAL: This callback DECRYPTS entire packet!
 * 
 * Architecture:
 * 1. Входной a_packet - ЗАШИФРОВАННЫЙ блок
 * 2. ДЕШИФРУЕМ его
 * 3. Парсим [FC+UDP full header]
 * 4. Возвращаем DECRYPTED payload
 * 5. Metadata заполняем из FC+UDP header
 * 
 * ВАЖНО: Дешифрованный буфер НЕ освобождается здесь!
 * Он передаётся через a_payload_out и будет освобождён позже.
 * 
 * @param a_flow Flow instance
 * @param a_packet ENCRYPTED packet
 * @param a_packet_size ENCRYPTED packet size
 * @param a_metadata [out] Parsed metadata (FC + UDP info)
 * @param a_payload_out [out] DECRYPTED payload pointer
 * @param a_payload_size_out [out] DECRYPTED payload size
 * @param a_arg User argument (NULL for UDP)
 * @return 0 on success, negative on error
 */
static int s_flow_ctrl_packet_parse_cb(dap_io_flow_t *a_flow,
                                        const void *a_packet, size_t a_packet_size,
                                        dap_io_flow_pkt_metadata_t *a_metadata,
                                        const void **a_payload_out, size_t *a_payload_size_out,
                                        void *a_arg)
{
    UNUSED(a_arg);
    
    if (!a_flow || !a_packet || !a_metadata || !a_payload_out || !a_payload_size_out) {
        return -1;
    }
    
    stream_udp_session_t *l_session = (stream_udp_session_t *)a_flow;
    
    // DECRYPT entire packet
    if (!l_session->encryption_key) {
        log_it(L_WARNING, "No encryption key for FC packet parse");
        return -2;
    }
    
    size_t l_decrypted_max = a_packet_size + 256;
    uint8_t *l_decrypted = DAP_NEW_SIZE(uint8_t, l_decrypted_max);
    if (!l_decrypted) {
        log_it(L_ERROR, "Failed to allocate decryption buffer");
        return -3;
    }
    
    size_t l_decrypted_size = dap_enc_decode(l_session->encryption_key,
                                             a_packet, a_packet_size,
                                             l_decrypted, l_decrypted_max,
                                             DAP_ENC_DATA_TYPE_RAW);
    if (l_decrypted_size == 0) {
        log_it(L_ERROR, "Failed to decrypt FC packet");
        DAP_DELETE(l_decrypted);
        return -4;
    }
    
    // Check size
    if (l_decrypted_size < sizeof(dap_stream_trans_udp_full_header_t)) {
        log_it(L_WARNING, "Decrypted packet too small for full header: %zu < %zu",
               l_decrypted_size, sizeof(dap_stream_trans_udp_full_header_t));
        DAP_DELETE(l_decrypted);
        return -5;
    }
    
    // Parse full header using dap_serialize (network byte order → host)
    dap_stream_trans_udp_full_header_t l_hdr;
    dap_deserialize_result_t l_deser_result = dap_deserialize_from_buffer_raw(
        &g_udp_full_header_schema,
        l_decrypted,
        sizeof(dap_stream_trans_udp_full_header_t),
        &l_hdr,
        NULL  // no context
    );
    
    if (l_deser_result.error_code != 0) {
        log_it(L_ERROR, "Failed to deserialize UDP full header: %s",
               l_deser_result.error_message ? l_deser_result.error_message : "unknown error");
        DAP_DELETE(l_decrypted);
        return -6;
    }
    
    // Fill metadata (FC fields)
    a_metadata->seq_num = l_hdr.seq_num;
    a_metadata->ack_seq = l_hdr.ack_seq;
    a_metadata->timestamp_ms = l_hdr.timestamp_ms;
    a_metadata->is_keepalive = (l_hdr.fc_flags & DAP_IO_FLOW_CTRL_HDR_FLAG_KEEPALIVE) != 0;
    a_metadata->is_retransmit = (l_hdr.fc_flags & DAP_IO_FLOW_CTRL_HDR_FLAG_RETRANSMIT) != 0;
    
    // CRITICAL: Store l_decrypted for FC to free after delivery!
    a_metadata->private_ctx = l_decrypted;
    
    // Store UDP-specific info in session (для payload_deliver callback)
    atomic_store(&l_session->last_recv_type, l_hdr.type);
    
    // Payload starts after full header
    *a_payload_out = l_decrypted + sizeof(dap_stream_trans_udp_full_header_t);
    *a_payload_size_out = l_decrypted_size - sizeof(dap_stream_trans_udp_full_header_t);
    
    // NOTE: l_decrypted will be freed by FC after delivery via metadata->private_ctx
    
    debug_if(s_debug_more, L_DEBUG,
             "Parsed DECRYPTED FC packet: seq=%lu, ack=%lu, type=%u, session=0x%lx, payload_size=%zu",
             l_hdr.seq_num, l_hdr.ack_seq, l_hdr.type, l_hdr.session_id, *a_payload_size_out);
    
    return 0;
}

/**
 * @brief Send packet via UDP
 * 
 * @param a_flow Flow instance (cast to stream_udp_session_t)
 * @param a_packet Packet to send
 * @param a_packet_size Packet size
 * @param a_arg User argument (NULL for UDP)
 * @return 0 on success, negative on error
 */
static int s_flow_ctrl_packet_send_cb(dap_io_flow_t *a_flow,
                                       const void *a_packet, size_t a_packet_size,
                                       void *a_arg)
{
    UNUSED(a_arg);
    
    stream_udp_session_t *l_session = (stream_udp_session_t *)a_flow;
    if (!l_session || !a_packet) {
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "SERVER: Sending FC packet: size=%zu, session=%p", a_packet_size, l_session);
    
    // Send via UDP flow
    ssize_t l_ret = dap_io_flow_datagram_send(&l_session->base, a_packet, a_packet_size);
    
    debug_if(s_debug_more, L_DEBUG,
             "SERVER: dap_io_flow_datagram_send returned: %zd", l_ret);
    
    if (l_ret < 0) {
        log_it(L_WARNING, "Failed to send Flow Control packet: ret=%zd", l_ret);
        return -2;
    }
    
    return 0;
}

/**
 * @brief Free packet buffer
 * 
 * @param a_packet Packet buffer to free
 * @param a_arg User argument (NULL for UDP)
 */
static void s_flow_ctrl_packet_free_cb(void *a_packet, void *a_arg)
{
    UNUSED(a_arg);
    
    if (a_packet) {
        DAP_DELETE(a_packet);
    }
}

/**
 * @brief Deliver in-order payload to upper layer (NEW ARCHITECTURE)
 * 
 * CRITICAL: Payload ALREADY DECRYPTED в parse_cb!
 * 
 * Payload содержит: [старый UDP encrypted header] + [data]
 * Но это УЖЕ расшифрованный блок!
 * 
 * Нам нужно:
 * 1. Парсить старый UDP header (type, session_id)
 * 2. Dispatch к protocol handlers
 * 3. ОСВОБОДИТЬ буфер (выделенный в parse_cb)
 * 
 * @param a_flow Flow instance (cast to stream_udp_session_t)
 * @param a_payload DECRYPTED payload ([old UDP header] + [data])
 * @param a_payload_size Payload size
 * @param a_arg User argument (NULL for UDP)
 * @return 0 on success
 */
static int s_flow_ctrl_payload_deliver_cb(dap_io_flow_t *a_flow,
                                           const void *a_payload, size_t a_payload_size,
                                           void *a_arg)
{
    UNUSED(a_arg);
    
    stream_udp_session_t *l_session = (stream_udp_session_t *)a_flow;
    if (!l_session || !a_payload) {
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Delivering DECRYPTED reordered payload: size=%zu", a_payload_size);
    
    // Get type from session (stored by parse_cb)
    uint8_t l_type = atomic_load(&l_session->last_recv_type);
    
    debug_if(s_debug_more, L_DEBUG,
             "Delivering payload: type=%u, session=0x%lx, size=%zu",
             l_type, l_session->session_id, a_payload_size);
    
    // Dispatch to protocol handlers (payload is PURE DATA, no headers!)
    int l_ret = 0;
    switch (l_type) {
        case DAP_STREAM_UDP_PKT_SESSION_CREATE:
            l_ret = s_handle_session_create(l_session, a_payload, a_payload_size);
            break;
        case DAP_STREAM_UDP_PKT_DATA:
            l_ret = s_handle_data(l_session, a_payload, a_payload_size);
            break;
        case DAP_STREAM_UDP_PKT_KEEPALIVE:
            l_ret = s_handle_keepalive(l_session);
            break;
        case DAP_STREAM_UDP_PKT_CLOSE:
            l_ret = s_handle_close(l_session);
            break;
        default:
            log_it(L_WARNING, "Unknown packet type in FC payload: %u", l_type);
            l_ret = -3;
            break;
    }
    
    // NOTE: Payload buffer ownership handled via metadata->private_ctx
    // FC will free it after delivery (immediate or buffered)
    
    return l_ret;
}

/**
 * @brief Handle keep-alive timeout
 * 
 * For DAP Stream: do nothing (stream has its own keep-alive)
 * 
 * @param a_flow Flow instance
 * @param a_arg User argument (NULL for UDP)
 */
static void s_flow_ctrl_keepalive_timeout_cb(dap_io_flow_t *a_flow, void *a_arg)
{
    UNUSED(a_arg);
    
    stream_udp_session_t *l_session = (stream_udp_session_t *)a_flow;
    if (!l_session) {
        return;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Flow Control keep-alive timeout for session 0x%lx",
             l_session->session_id);
    
    // For DAP Stream: do nothing, stream handles its own keep-alive
    // For other protocols: might close connection, reconnect, or notify upper layer
}



