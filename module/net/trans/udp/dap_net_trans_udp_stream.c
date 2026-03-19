/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Contributors:
 * Copyright (c) 2017-2025 Demlabs Ltd <https://demlabs.net>
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_net_trans_udp_stream.h"
#include "dap_net_trans_udp_server.h"
#include "dap_net_trans.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_timerfd.h"
#include "dap_net.h"
#include "dap_enc_kyber.h"
#include "dap_transport_obfuscation.h"
#include "dap_json.h"
#include "dap_io_flow.h"
#include "dap_io_flow_ctrl.h"
#include "dap_io_flow_datagram.h"
#include "dap_arena.h"

#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif
#include "dap_stream_handshake.h"
#include "dap_stream.h"
#include "dap_stream_session.h"
#include "dap_stream_ch.h"
#include "dap_stream_esocket_ops.h"
#include "dap_server.h"
#include "dap_enc_server.h"
#include "dap_client.h"
#include "rand/dap_rand.h"
#include "dap_enc_key.h"
#include "dap_enc.h"
#include "dap_enc_kdf.h"
#include "dap_enc_ks.h"
#include "dap_enc_base64.h"
#include "dap_string.h"
#include "dap_net_trans_ctx.h"
#include "dap_json.h"  // For JSON API

#define LOG_TAG "dap_stream_trans_udp"

// Stream creation counter (from dap_stream.c)
extern _Atomic uint64_t dap_stream_created_count;

// Debug flags
static bool s_debug_more = false;  // Extra verbose debugging

// UDP Trans Protocol Version
#define DAP_STREAM_UDP_VERSION 1

// Default configuration values
#define DAP_STREAM_UDP_DEFAULT_MAX_PACKET_SIZE  1400
#define DAP_STREAM_UDP_DEFAULT_KEEPALIVE_MS     30000

// HANDSHAKE RETRANSMISSION configuration
// (separate from Flow Control because HANDSHAKE uses obfuscation, not FC headers)
#define HANDSHAKE_RETRANSMIT_TIMEOUT_MS     500     // Initial timeout: 500ms
#define HANDSHAKE_RETRANSMIT_MAX_RETRIES    10      // Max retries before giving up
#define HANDSHAKE_RETRANSMIT_BACKOFF_FACTOR 1.5     // Exponential backoff factor

// Trans operations forward declarations
static int s_udp_init(dap_net_trans_t *a_trans, dap_config_t *a_config);
static void s_udp_deinit(dap_net_trans_t *a_trans);
static int s_udp_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port, 
                          dap_net_trans_connect_cb_t a_callback);
static int s_udp_listen(dap_net_trans_t *a_trans, const char *a_addr, uint16_t a_port,
                         dap_server_t *a_server);
static int s_udp_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out);
static int s_udp_handshake_init(dap_stream_t *a_stream,
                                 dap_net_handshake_params_t *a_params,
                                 dap_net_trans_handshake_cb_t a_callback);
static int s_udp_handshake_process(dap_stream_t *a_stream,
                                    const void *a_data, size_t a_data_size,
                                    void **a_response, size_t *a_response_size);
static int s_udp_session_create(dap_stream_t *a_stream,
                                 dap_net_session_params_t *a_params,
                                 dap_net_trans_session_cb_t a_callback);
static int s_udp_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_net_trans_ready_cb_t a_callback);
static ssize_t s_udp_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size);
static ssize_t s_udp_write(dap_stream_t *a_stream, const void *a_data, size_t a_size);
static ssize_t s_udp_write_typed(dap_stream_t *a_stream, uint8_t a_pkt_type, 
                                  const void *a_data, size_t a_size);
static void s_udp_close(dap_stream_t *a_stream);
static uint32_t s_udp_get_capabilities(dap_net_trans_t *a_trans);
static void* s_udp_get_client_context(dap_stream_t *a_stream);
static int s_udp_stage_prepare(dap_net_trans_t *a_trans,
                               const dap_net_stage_prepare_params_t *a_params,
                               dap_net_stage_prepare_result_t *a_result);
static size_t s_udp_get_max_packet_size(dap_net_trans_t *a_trans);
static void s_udp_check_session_op(dap_net_trans_t *a_trans, uint32_t a_session_id,
                                   dap_events_socket_t *a_esocket);

// UDP trans operations table
static const dap_net_trans_ops_t s_udp_ops = {
    .init = s_udp_init,
    .deinit = s_udp_deinit,
    .connect = s_udp_connect,
    .listen = s_udp_listen,
    .accept = s_udp_accept,
    .handshake_init = s_udp_handshake_init,
    .handshake_process = s_udp_handshake_process,
    .session_create = s_udp_session_create,
    .session_start = s_udp_session_start,
    .read = s_udp_read,
    .write = s_udp_write,
    .close = s_udp_close,
    .get_capabilities = s_udp_get_capabilities,
    .register_server_handlers = NULL,
    .stage_prepare = s_udp_stage_prepare,
    .get_client_context = s_udp_get_client_context,
    .check_session = s_udp_check_session_op,
    .get_max_packet_size = s_udp_get_max_packet_size
};

// UDP per-stream context is now dap_net_trans_udp_ctx_t (defined in header)
// No need for separate client esocket context - everything is in UDP ctx

// Helper functions
static dap_stream_trans_udp_private_t *s_get_private(dap_net_trans_t *a_trans);
static dap_net_trans_udp_ctx_t *s_get_udp_ctx(dap_stream_t *a_stream);

// Handshake retransmission timer callback
static bool s_handshake_retransmit_timer_cb(void *a_arg);
// Made non-static for server.c to create UDP context for server-side streams
dap_net_trans_udp_ctx_t *s_get_or_create_udp_ctx(dap_stream_t *a_stream);
static int s_udp_handshake_response(dap_stream_t *a_stream, const void *a_data, size_t a_data_size);

//===================================================================
// CLIENT-SIDE FLOW CONTROL CALLBACKS
//===================================================================

/**
 * @brief Flow Control: Prepare packet (add FC+UDP header + encrypt)
 * 
 * This is called by dap_io_flow_ctrl_send() to wrap the payload
 * in a full packet with FC header + UDP header, then encrypt.
 * 
 * @param a_flow Flow Control instance (unused here, we use arg)
 * @param a_metadata FC metadata (seq_num, ack_seq, etc.)
 * @param a_payload Pure payload (no headers)
 * @param a_payload_size Payload size
 * @param a_packet_out [OUT] Fully prepared packet (allocated by us)
 * @param a_packet_size_out [OUT] Total packet size
 * @param a_arg dap_net_trans_udp_ctx_t*
 * @return 0 on success, negative on error
 */
static int s_client_flow_ctrl_packet_prepare_cb(
    dap_io_flow_t *a_flow,
    const dap_io_flow_pkt_metadata_t *a_metadata,
    const void *a_payload,
    size_t a_payload_size,
    void **a_packet_out,
    size_t *a_packet_size_out,
    void *a_arg)
{
    (void)a_flow;
    dap_net_trans_udp_ctx_t *l_ctx = (dap_net_trans_udp_ctx_t *)a_arg;
    if (!l_ctx || !a_metadata) {
        log_it(L_ERROR, "CLIENT FC prepare: invalid arguments");
        return -1;
    }
    
    // NOTE: HANDSHAKE packets are NOT sent through Flow Control
    // They use obfuscation format which is incompatible with FC protocol
    
    // Build full UDP header (FC + UDP fields)
    dap_stream_trans_udp_full_header_t l_hdr = {
        .seq_num = a_metadata->seq_num,
        .ack_seq = a_metadata->ack_seq,
        .timestamp_ms = a_metadata->timestamp_ms,
        .fc_flags = 0,
        .type = l_ctx->last_send_type,  // Retrieved from context
        .session_id = l_ctx->session_id,
    };
    
    // Serialize header
    size_t l_hdr_size = dap_serialize_calc_size_raw(&g_udp_full_header_schema, NULL, &l_hdr, NULL);
    if (l_hdr_size == 0) {
        log_it(L_ERROR, "CLIENT FC prepare: failed to calc header size");
        return -2;
    }
    
    // Allocate [header + payload]
    size_t l_cleartext_size = l_hdr_size + a_payload_size;
    uint8_t *l_cleartext = DAP_NEW_Z_SIZE(uint8_t, l_cleartext_size);
    if (!l_cleartext) {
        log_it(L_ERROR, "CLIENT FC prepare: OOM for cleartext (%zu bytes)", l_cleartext_size);
        return -3;
    }
    
    // Serialize header into cleartext
    dap_serialize_result_t l_ser_result = dap_serialize_to_buffer_raw(
        &g_udp_full_header_schema,
        &l_hdr,
        l_cleartext,
        l_hdr_size,
        NULL  // context
    );
    if (l_ser_result.error_code != 0) {
        log_it(L_ERROR, "CLIENT FC prepare: failed to serialize header: %s",
               l_ser_result.error_message ? l_ser_result.error_message : "unknown");
        DAP_DELETE(l_cleartext);
        return -4;
    }
    
    // Copy payload after header
    if (a_payload_size > 0) {
        memcpy(l_cleartext + l_hdr_size, a_payload, a_payload_size);
    }
    
    // ENCRYPT entire [header + payload]
    if (!l_ctx->handshake_key) {
        log_it(L_ERROR, "CLIENT FC prepare: no encryption key");
        DAP_DELETE(l_cleartext);
        return -5;
    }
    
    size_t l_encrypted_size = l_cleartext_size + 256;  // Extra space for enc overhead
    uint8_t *l_encrypted = DAP_NEW_Z_SIZE(uint8_t, l_encrypted_size);
    if (!l_encrypted) {
        log_it(L_ERROR, "CLIENT FC prepare: OOM for encrypted");
        DAP_DELETE(l_cleartext);
        return -6;
    }
    
    size_t l_final_encrypted_size = dap_enc_code(
        l_ctx->handshake_key,
        l_cleartext,
        l_cleartext_size,
        l_encrypted,
        l_encrypted_size,
        DAP_ENC_DATA_TYPE_RAW
    );
    
    DAP_DEL_Z(l_cleartext);  // Free cleartext
    
    if (l_final_encrypted_size == 0) {
        log_it(L_ERROR, "CLIENT FC prepare: encryption failed");
        DAP_DELETE(l_encrypted);
        return -7;
    }
    
    // Return encrypted packet
    *a_packet_out = l_encrypted;
    *a_packet_size_out = l_final_encrypted_size;
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT FC prepare: seq=%" DAP_UINT64_FORMAT_U ", ack=%" DAP_UINT64_FORMAT_U ", type=%u, payload=%zu → packet=%zu bytes",
             a_metadata->seq_num, a_metadata->ack_seq, l_hdr.type, a_payload_size, l_final_encrypted_size);
    
    return 0;
}

/**
 * @brief Flow Control: Parse packet (decrypt + extract FC+UDP header)
 */
static int s_client_flow_ctrl_packet_parse_cb(
    dap_io_flow_t *a_flow,
    const void *a_packet,
    size_t a_packet_size,
    dap_io_flow_pkt_metadata_t *a_metadata_out,
    const void **a_payload_out,
    size_t *a_payload_size_out,
    void *a_arg)
{
    (void)a_flow;
    dap_net_trans_udp_ctx_t *l_ctx = (dap_net_trans_udp_ctx_t *)a_arg;
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT FC parse ENTRY: packet_size=%zu, ctx=%p", a_packet_size, l_ctx);
    
    if (!l_ctx || !a_packet || !a_metadata_out) {
        log_it(L_ERROR, "CLIENT FC parse: invalid arguments");
        return -1;
    }
    
    // DECRYPT entire packet
    if (!l_ctx->handshake_key) {
        log_it(L_ERROR, "CLIENT FC parse: no decryption key");
        return -2;
    }
    
    size_t l_decrypted_size = a_packet_size + 256;
    uint8_t *l_decrypted = DAP_NEW_Z_SIZE(uint8_t, l_decrypted_size);
    if (!l_decrypted) {
        log_it(L_ERROR, "CLIENT FC parse: OOM for decrypted");
        return -3;
    }
    
    size_t l_final_decrypted_size = dap_enc_decode(
        l_ctx->handshake_key,
        a_packet,
        a_packet_size,
        l_decrypted,
        l_decrypted_size,
        DAP_ENC_DATA_TYPE_RAW
    );
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT FC parse: AFTER decrypt: decrypted_size=%zu (packet_size was %zu)",
             l_final_decrypted_size, a_packet_size);
    
    if (l_final_decrypted_size == 0) {
        log_it(L_ERROR, "CLIENT FC parse: decryption failed");
        DAP_DELETE(l_decrypted);
        return -4;
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
        log_it(L_ERROR, "CLIENT FC parse: deserialize failed: %s (decrypted_size=%zu)",
               l_deser_result.error_message ? l_deser_result.error_message : "unknown",
               l_final_decrypted_size);
        DAP_DELETE(l_decrypted);
        return -5;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT FC parse: AFTER deserialize: seq_num=%" DAP_UINT64_FORMAT_U ", ack_seq=%" DAP_UINT64_FORMAT_U ", type=%u",
             l_hdr.seq_num, l_hdr.ack_seq, l_hdr.type);
    
    if (l_deser_result.error_code != 0) {
        log_it(L_ERROR, "CLIENT FC parse: failed to deserialize header: %s",
               l_deser_result.error_message ? l_deser_result.error_message : "unknown");
        DAP_DELETE(l_decrypted);
        return -5;
    }
    
    // Fill metadata
    a_metadata_out->seq_num = l_hdr.seq_num;
    a_metadata_out->ack_seq = l_hdr.ack_seq;
    a_metadata_out->timestamp_ms = l_hdr.timestamp_ms;
    a_metadata_out->is_keepalive = (l_hdr.type == DAP_STREAM_UDP_PKT_KEEPALIVE);
    // Detect retransmits by checking RETRANSMIT flag in FC header
    a_metadata_out->is_retransmit = (l_hdr.fc_flags & DAP_IO_FLOW_CTRL_HDR_FLAG_RETRANSMIT) != 0;
    
    // CRITICAL: Store l_decrypted for FC to free after delivery!
    // FC will call DAP_DELETE(metadata->private_ctx) after payload_deliver
    a_metadata_out->private_ctx = l_decrypted;
    
    // Payload starts after header
    size_t l_hdr_size = sizeof(dap_stream_trans_udp_full_header_t);
    size_t l_payload_size = (l_final_decrypted_size > l_hdr_size) ? 
                            (l_final_decrypted_size - l_hdr_size) : 0;
    
    // Return pointer into decrypted buffer
    *a_payload_out = (l_payload_size > 0) ? (l_decrypted + l_hdr_size) : NULL;
    *a_payload_size_out = l_payload_size;
    
    // NOTE: l_decrypted buffer ownership handled via metadata->private_ctx
    // FC will free it after delivery (immediate or buffered)
    
    // Store type for deliver callback
    // CRITICAL: Only store type for DATA packets with payload!
    // ACK packets (seq=0) should NOT overwrite last_recv_type
    if (l_hdr.seq_num != 0 && l_payload_size > 0) {
        l_ctx->last_recv_type = l_hdr.type;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT FC parse: seq=%" DAP_UINT64_FORMAT_U ", ack=%" DAP_UINT64_FORMAT_U ", type=%u, payload=%zu bytes",
             l_hdr.seq_num, l_hdr.ack_seq, l_hdr.type, *a_payload_size_out);
    
    return 0;
}

/**
 * @brief Flow Control: Send packet (low-level send via esocket)
 */
static int s_client_flow_ctrl_packet_send_cb(
    dap_io_flow_t *a_flow,
    const void *a_packet,
    size_t a_packet_size,
    void *a_arg)
{
    (void)a_flow;
    dap_net_trans_udp_ctx_t *l_udp_ctx = (dap_net_trans_udp_ctx_t *)a_arg;
    if (!l_udp_ctx || !l_udp_ctx->stream) {
        log_it(L_ERROR, "CLIENT FC send: invalid UDP context");
        return -1;
    }
    
    // Get trans_ctx and esocket from stream
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t*)l_udp_ctx->stream->trans_ctx;
    if (!l_trans_ctx || !l_trans_ctx->esocket) {
        log_it(L_ERROR, "CLIENT FC send: no trans_ctx or esocket");
        return -1;
    }
    
    // Log destination address for debugging
    struct sockaddr_in *l_sin = (struct sockaddr_in *)&l_udp_ctx->remote_addr;
    if (l_udp_ctx->remote_addr.ss_family == AF_INET) {
        char l_addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &l_sin->sin_addr, l_addr_str, sizeof(l_addr_str));
        
        // Log every 100th packet to see if address changes
        static uint64_t s_addr_log_count = 0;
        s_addr_log_count++;
        if (s_addr_log_count % 100 == 0 || s_addr_log_count < 5) {
            log_it(L_INFO, "CLIENT FC send: dest=%s:%u (count=%" DAP_UINT64_FORMAT_U ")",
                   l_addr_str, ntohs(l_sin->sin_port), s_addr_log_count);
        }
    }
    
    // CRITICAL: Use udp_ctx->remote_addr (server address), NOT esocket->addr_storage!
    // esocket->addr_storage is overwritten by recvfrom() on every incoming packet!
    size_t l_written = dap_events_socket_sendto_unsafe(l_trans_ctx->esocket, a_packet, a_packet_size,
                                                       &l_udp_ctx->remote_addr,
                                                       l_udp_ctx->remote_addr_len);
    
    if (l_written != a_packet_size) {
        log_it(L_ERROR, "CLIENT FC send: failed to write %zu bytes (written: %zu), errno=%d (%s)", 
               a_packet_size, l_written, errno, strerror(errno));
        return -1;
    }
    
    // Log every 50th packet + errors
    static uint64_t s_send_count = 0;
    s_send_count++;
    if (s_send_count % 50 == 0 || s_send_count < 10) {
        log_it(L_DEBUG, "CLIENT FC send: successfully sent %zu bytes (count=%" DAP_UINT64_FORMAT_U ")", l_written, s_send_count);
    }
    
    return 0;
}

/**
 * @brief Flow Control: Deliver payload (call protocol handler)
 */
static int s_client_flow_ctrl_payload_deliver_cb(
    dap_io_flow_t *a_flow,
    const void *a_payload,
    size_t a_payload_size,
    void *a_arg)
{
    (void)a_flow;
    
    dap_net_trans_udp_ctx_t *l_ctx = (dap_net_trans_udp_ctx_t *)a_arg;
    
    debug_if(s_debug_more, L_DEBUG, "CLIENT FC deliver ENTRY: payload_size=%zu, arg=%p, ctx=%p", 
             a_payload_size, a_arg, l_ctx);
    
    if (!l_ctx || !l_ctx->stream) {
        log_it(L_ERROR, "CLIENT FC deliver: invalid context or no stream");
        return -1;
    }
    
    uint8_t l_type = l_ctx->last_recv_type;
    dap_stream_t *l_stream = l_ctx->stream;
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT FC deliver: type=%u, payload=%zu bytes", l_type, a_payload_size);
    
    // Dispatch based on packet type
    switch (l_type) {
        case DAP_STREAM_UDP_PKT_SESSION_CREATE: {
            // SESSION_CREATE response
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT FC deliver: SESSION_CREATE response (%zu bytes)", a_payload_size);
            
            // Check if session already has session key
            dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t*)l_stream->trans_ctx;
            if (!l_trans_ctx || !l_trans_ctx->session_create_cb) {
                debug_if(s_debug_more, L_DEBUG,
                         "CLIENT FC deliver: ignoring duplicate SESSION_CREATE");
                return 0;
            }
            
            if (a_payload_size != sizeof(uint64_t)) {
                log_it(L_ERROR, "CLIENT FC deliver: invalid SESSION_CREATE size: %zu", a_payload_size);
                return -1;
            }
            
            // Extract KDF counter
            uint64_t l_counter_be;
            memcpy(&l_counter_be, a_payload, sizeof(l_counter_be));
            uint64_t l_kdf_counter = be64toh(l_counter_be);
            
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT FC deliver: KDF counter=%" DAP_UINT64_FORMAT_U "", l_kdf_counter);
            
            // Derive session key
            dap_enc_key_t *l_session_key = dap_enc_kdf_create_cipher_key(
                l_ctx->handshake_key,
                DAP_ENC_KEY_TYPE_SALSA2012,
                "udp_session",
                11,
                l_kdf_counter,
                32
            );
            
            if (!l_session_key) {
                log_it(L_ERROR, "CLIENT FC deliver: failed to derive session key");
                return -1;
            }
            
            debug_if(s_debug_more, L_DEBUG, "CLIENT FC deliver: session key established");
            
            // Set session key in stream->session
            if (!l_stream->session) {
                l_stream->session = dap_stream_session_pure_new();
                if (!l_stream->session) {
                    log_it(L_ERROR, "CLIENT FC deliver: failed to create stream session");
                    dap_enc_key_delete(l_session_key);
                    return -1;
                }
            }
            
            if (l_stream->session->key) {
                dap_enc_key_delete(l_stream->session->key);
            }
            l_stream->session->key = l_session_key;
            l_stream->session->id = l_ctx->session_id;
            
            log_it(L_INFO, "CLIENT FC deliver: session key installed (session_id=0x%" DAP_UINT64_FORMAT_x ")", l_ctx->session_id);
            
            // Call session_create callback
            if (l_trans_ctx->session_create_cb) {
                debug_if(s_debug_more, L_DEBUG,
                         "CLIENT FC deliver: calling session_create_cb");
                l_trans_ctx->session_create_cb(l_stream, (uint32_t)l_ctx->session_id, NULL, 0, 0);
                l_trans_ctx->session_create_cb = NULL;
            }
            
            // Replace handshake_key with session_key for encryption
            // CRITICAL: Don't delete handshake_key here - it's already replaced!
            // session_key is stored in l_stream->session->key and l_ctx->handshake_key points to it
            // To avoid double-free, we set handshake_key to session_key (same pointer)
            // Cleanup will only delete once via l_stream->session->key
            dap_enc_key_delete(l_ctx->handshake_key);
            l_ctx->handshake_key = l_session_key;  // Point to same object as session->key
            
            break;
        }
        
        case DAP_STREAM_UDP_PKT_DATA: {
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT FC deliver: DATA packet (%zu bytes)", a_payload_size);
            
            if (a_payload_size > 0) {
                dap_stream_data_proc_read_ext(l_stream, (const uint8_t*)a_payload, a_payload_size);
            }
            break;
        }
        
        case DAP_STREAM_UDP_PKT_KEEPALIVE: {
            // KEEPALIVE
            debug_if(s_debug_more, L_DEBUG, "CLIENT FC deliver: KEEPALIVE");
            break;
        }
        
        case DAP_STREAM_UDP_PKT_CLOSE: {
            // CLOSE
            log_it(L_INFO, "CLIENT FC deliver: CLOSE from server");
            l_stream->is_active = false;
            if (l_stream->trans && l_stream->trans->ops && l_stream->trans->ops->close) {
                l_stream->trans->ops->close(l_stream);
            }
            break;
        }
        
        default:
            log_it(L_WARNING, "CLIENT FC deliver: unknown packet type 0x%02x", l_type);
            return -1;
    }
    
    return 0;
}

/**
 * @brief Flow Control: Free packet callback
 */
static void s_client_flow_ctrl_packet_free_cb(void *a_packet, void *a_arg)
{
    (void)a_arg;
    if (a_packet) {
        DAP_DELETE(a_packet);
    }
}

/**
 * @brief Flow Control: Keepalive timeout callback
 * 
 * Called when keepalive timeout expires (no activity for keepalive_timeout_ms).
 * For UDP client, we don't use FC keepalive - dap_stream has its own mechanism.
 */
static void s_client_flow_ctrl_keepalive_timeout_cb(dap_io_flow_t *a_flow, void *a_arg)
{
    (void)a_flow;
    (void)a_arg;
    // Not used - dap_stream handles keepalive
    debug_if(s_debug_more, L_DEBUG, "CLIENT FC keepalive timeout (ignored - dap_stream handles it)");
}

/**
 * @brief Ensure Flow Control is created for client UDP context
 * 
 * This function creates flow_ctrl if it doesn't exist yet.
 * Can be called from multiple places (handshake_init, session_start) safely.
 * 
 * @param a_udp_ctx UDP context
 * @return 0 on success (or already created), negative on error
 */
static int s_ensure_client_flow_ctrl(dap_net_trans_udp_ctx_t *a_udp_ctx)
{
    if (!a_udp_ctx) {
        log_it(L_ERROR, "s_ensure_client_flow_ctrl: NULL udp_ctx");
        return -1;
    }
    
    // Already created?
    if (a_udp_ctx->flow_ctrl) {
        debug_if(s_debug_more, L_DEBUG, "CLIENT: flow_ctrl already exists");
        return 0;
    }
    
    // Ensure base flow is allocated for client
    if (!a_udp_ctx->base) {
        a_udp_ctx->base = DAP_NEW_Z(dap_io_flow_t);
        if (!a_udp_ctx->base) {
            log_it(L_ERROR, "Failed to allocate base flow for client UDP");
            return -2;
        }
        log_it(L_DEBUG, "CLIENT: Created base flow for Flow Control");
    }
    
    // Initialize base flow fields (for Flow Control)
    memcpy(&a_udp_ctx->base->remote_addr, &a_udp_ctx->remote_addr, 
           sizeof(a_udp_ctx->remote_addr));
    a_udp_ctx->base->remote_addr_len = a_udp_ctx->remote_addr_len;
    a_udp_ctx->base->last_activity = time(NULL);
    a_udp_ctx->base->boundary_type = DAP_IO_FLOW_BOUNDARY_DATAGRAM;
    
    dap_io_flow_ctrl_config_t l_fc_config = {
        .retransmit_timeout_ms = 100,   // 100ms for localhost (was 1000ms - TOO SLOW!)
        .max_retransmit_count = 20,     // Increased for large transfers
        .send_window_size = 65536,      // 64K packets in-flight (~64MB for 1KB packets)
        .recv_window_size = 65536,      // 64K packets reorder buffer
        .max_out_of_order_delay_ms = 10000,  // 10 seconds out-of-order window
        .keepalive_interval_ms = 0,     // Not used (dap_stream has own keepalive)
        .keepalive_timeout_ms = 0,      // Not used
    };
    
    dap_io_flow_ctrl_callbacks_t l_fc_callbacks = {
        .packet_prepare = s_client_flow_ctrl_packet_prepare_cb,
        .packet_parse = s_client_flow_ctrl_packet_parse_cb,
        .packet_send = s_client_flow_ctrl_packet_send_cb,
        .payload_deliver = s_client_flow_ctrl_payload_deliver_cb,
        .packet_free = s_client_flow_ctrl_packet_free_cb,
        .keepalive_timeout = s_client_flow_ctrl_keepalive_timeout_cb,
        .arg = a_udp_ctx,
    };
    
    dap_io_flow_ctrl_flags_t l_fc_flags = DAP_IO_FLOW_CTRL_RETRANSMIT | 
                                           DAP_IO_FLOW_CTRL_REORDER;
    // NOTE: No KEEPALIVE flag - dap_stream has its own keep-alive!
    
    a_udp_ctx->flow_ctrl = dap_io_flow_ctrl_create(
        a_udp_ctx->base,  // Client flow = allocated base dap_io_flow_t
        l_fc_flags,
        &l_fc_config,
        &l_fc_callbacks
    );
    
    if (!a_udp_ctx->flow_ctrl) {
        log_it(L_ERROR, "Failed to create Flow Control for client UDP");
        return -3;
    }
    
    log_it(L_NOTICE, "Client-side Flow Control created: retransmit=%dms, max_retries=%d",
           l_fc_config.retransmit_timeout_ms, l_fc_config.max_retransmit_count);
    
    return 0;
}

/**
 * @brief Handshake retransmission timer callback
 * 
 * This callback is called periodically to retransmit HANDSHAKE packets
 * that haven't received a response yet. Uses exponential backoff.
 * 
 * @param a_arg Pointer to dap_net_trans_udp_ctx_t
 * @return true to continue timer, false to stop
 */
static bool s_handshake_retransmit_timer_cb(void *a_arg)
{
    dap_net_trans_udp_ctx_t *l_udp_ctx = (dap_net_trans_udp_ctx_t *)a_arg;
    
    if (!l_udp_ctx) {
        log_it(L_ERROR, "HANDSHAKE RETRANS: NULL udp_ctx in timer callback");
        return false;  // Stop timer
    }
    
    // Check if handshake completed or context is being destroyed
    if (l_udp_ctx->handshake_complete) {
        log_it(L_DEBUG, "HANDSHAKE RETRANS: handshake completed, stopping timer");
        // Clear saved payload
        if (l_udp_ctx->handshake_payload) {
            DAP_DELETE(l_udp_ctx->handshake_payload);
            l_udp_ctx->handshake_payload = NULL;
            l_udp_ctx->handshake_payload_size = 0;
        }
        l_udp_ctx->handshake_timer = NULL;  // Timer is being deleted
        return false;  // Stop timer
    }
    
    // Check if we have saved payload to retransmit
    if (!l_udp_ctx->handshake_payload || l_udp_ctx->handshake_payload_size == 0) {
        log_it(L_WARNING, "HANDSHAKE RETRANS: no payload to retransmit");
        l_udp_ctx->handshake_timer = NULL;
        return false;  // Stop timer
    }
    
    // Check max retries
    if (l_udp_ctx->handshake_retries >= HANDSHAKE_RETRANSMIT_MAX_RETRIES) {
        log_it(L_ERROR, "HANDSHAKE RETRANS: max retries (%d) exceeded, giving up",
               HANDSHAKE_RETRANSMIT_MAX_RETRIES);
        // Clear saved payload
        DAP_DELETE(l_udp_ctx->handshake_payload);
        l_udp_ctx->handshake_payload = NULL;
        l_udp_ctx->handshake_payload_size = 0;
        l_udp_ctx->handshake_timer = NULL;
        // Notify FSM about handshake timeout so it can transition to error state
        if (l_udp_ctx->stream && l_udp_ctx->stream->trans_ctx) {
            dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)l_udp_ctx->stream->trans_ctx;
            if (l_trans_ctx->handshake_cb) {
                log_it(L_WARNING, "HANDSHAKE RETRANS: notifying FSM of ETIMEDOUT");
                l_trans_ctx->handshake_cb(l_udp_ctx->stream, NULL, 0, ETIMEDOUT);
            }
        }
        return false;  // Stop timer
    }
    
    // Increment retry counter
    l_udp_ctx->handshake_retries++;
    
    log_it(L_INFO, "HANDSHAKE RETRANS: retransmitting (attempt %u/%d)",
           l_udp_ctx->handshake_retries, HANDSHAKE_RETRANSMIT_MAX_RETRIES);
    
    // Get trans_ctx and esocket
    if (!l_udp_ctx->stream || !l_udp_ctx->stream->trans_ctx) {
        log_it(L_ERROR, "HANDSHAKE RETRANS: no stream or trans_ctx");
        l_udp_ctx->handshake_timer = NULL;
        return false;
    }
    
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)l_udp_ctx->stream->trans_ctx;
    if (!l_trans_ctx->esocket) {
        log_it(L_ERROR, "HANDSHAKE RETRANS: no esocket");
        l_udp_ctx->handshake_timer = NULL;
        return false;
    }
    
    // Obfuscate handshake (same as initial send)
    uint8_t *l_obfuscated = NULL;
    size_t l_obfuscated_size = 0;
    
    int l_ret = dap_transport_obfuscate_handshake(l_udp_ctx->handshake_payload,
                                                   l_udp_ctx->handshake_payload_size,
                                                   &l_obfuscated, &l_obfuscated_size);
    if (l_ret != 0) {
        log_it(L_ERROR, "HANDSHAKE RETRANS: failed to obfuscate");
        return true;  // Keep timer running, try again
    }
    
    // Send obfuscated handshake
    ssize_t l_sent = dap_events_socket_sendto_unsafe(l_trans_ctx->esocket,
                                                      l_obfuscated, l_obfuscated_size,
                                                      &l_udp_ctx->remote_addr,
                                                      l_udp_ctx->remote_addr_len);
    DAP_DELETE(l_obfuscated);
    
    if (l_sent < 0) {
        log_it(L_ERROR, "HANDSHAKE RETRANS: send failed (errno=%d)", errno);
        return true;  // Keep timer running, try again
    }
    
    log_it(L_DEBUG, "HANDSHAKE RETRANS: sent %zd bytes (attempt %u)",
           l_sent, l_udp_ctx->handshake_retries);
    
    return true;  // Keep timer running
}

/**
 * @brief Cancel handshake retransmission timer
 * 
 * Called when handshake response is received to stop retransmissions.
 * 
 * @param a_udp_ctx UDP context
 */
static void s_cancel_handshake_timer(dap_net_trans_udp_ctx_t *a_udp_ctx)
{
    if (!a_udp_ctx)
        return;
    
    // Mark handshake as complete to stop timer callback
    a_udp_ctx->handshake_complete = true;
    
    // Delete timer if exists
    if (a_udp_ctx->handshake_timer) {
        dap_timerfd_delete_unsafe(a_udp_ctx->handshake_timer);
        a_udp_ctx->handshake_timer = NULL;
        log_it(L_DEBUG, "HANDSHAKE: retransmission timer cancelled");
    }
    
    // Free saved payload
    if (a_udp_ctx->handshake_payload) {
        DAP_DELETE(a_udp_ctx->handshake_payload);
        a_udp_ctx->handshake_payload = NULL;
        a_udp_ctx->handshake_payload_size = 0;
    }
}

/**
 * @brief UDP read callback for processing incoming packets
 * 
 * This callback is invoked when data arrives on a UDP socket (client or server virtual).
 * The trans_ctx is stored in esocket->_inheritor (always dap_net_trans_ctx_t).
 * 
 * Used by both:
 * - UDP client esockets (direct physical socket)
 * - UDP server virtual esockets (demultiplexed sessions)
 */
void dap_stream_trans_udp_read_callback(dap_events_socket_t *a_es, void *a_arg) {
    (void)a_arg;

    if (!a_es || !a_es->buf_in_size) {
        return;
    }

    debug_if(s_debug_more, L_DEBUG, "UDP client read callback: esocket %p (fd=%d), buf_in_size=%zu, callbacks.arg=%p",
             a_es, a_es->fd, a_es->buf_in_size, a_es->callbacks.arg);

    // Get trans_ctx from callbacks.arg (NOT _inheritor!)
    // _inheritor may point to client (dap_client_t), not trans_ctx!
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_es->callbacks.arg;

    if (!l_trans_ctx) {
        log_it(L_WARNING, "UDP client esocket has no trans_ctx (callbacks.arg is NULL), dropping %zu bytes", a_es->buf_in_size);
        a_es->buf_in_size = 0;
        return;
    }
    
    // CRITICAL: Validate stream pointer - may be NULL during cleanup!
    if (!l_trans_ctx->stream) {
        log_it(L_WARNING, "UDP client trans_ctx %p has no stream (stream is NULL), dropping %zu bytes", 
               l_trans_ctx, a_es->buf_in_size);
        a_es->buf_in_size = 0;
        return;
    }
    
    debug_if(s_debug_more, L_DEBUG, ">>> UDP READ CALLBACK: l_trans_ctx=%p, stream=%p", 
             l_trans_ctx, l_trans_ctx->stream);
    
    dap_stream_t *l_stream = l_trans_ctx->stream;
    
    // Validate stream pointer first
    if (!l_stream) {
        log_it(L_WARNING, "UDP client stream pointer is NULL (stream closed?), dropping %zu bytes", a_es->buf_in_size);
        a_es->buf_in_size = 0;
        return;
    }
    
    // Validate stream->trans before accessing it
    // Check if stream has been deleted (trans would be NULL or invalid)
    if (!l_stream->trans) {
        log_it(L_WARNING, "UDP client stream has NULL trans (use-after-free?), dropping %zu bytes", a_es->buf_in_size);
        // Clear the dangling pointer to prevent future issues
        l_trans_ctx->stream = NULL;
        a_es->buf_in_size = 0;
        return;
    }
    
    // Validate trans operations
    if (!l_stream->trans->ops || !l_stream->trans->ops->read) {
        log_it(L_ERROR, "UDP client stream has invalid trans, dropping %zu bytes", a_es->buf_in_size);
        a_es->buf_in_size = 0;
        return;
    }
    
    // Process ALL packets in buffer (multiple packets may arrive before callback is called)
    // Don't manually call trans->ops->read - reactor fills buf_in automatically
    // Just process what's already in buffer
    int l_iterations = 0;
    const int l_max_iterations = 100; // Safety limit to prevent infinite loops
    
    while (a_es->buf_in_size > 0 && l_iterations < l_max_iterations) {
        size_t l_buf_in_size_before = a_es->buf_in_size;
        
        debug_if(s_debug_more, L_DEBUG, "Processing UDP packet from buf_in, buf_in_size=%zu (iteration %d)", 
                 a_es->buf_in_size, l_iterations);
        
        // Process one packet from buffer (s_udp_read will shrink buf_in)
        ssize_t l_result = s_udp_read(l_stream, NULL, 0);
        
        debug_if(s_debug_more, L_DEBUG, "CLIENT: s_udp_read returned %zd, buf_in_size now=%zu", 
                 l_result, a_es->buf_in_size);
        
        // If buf_in_size didn't change, break to prevent infinite loop
        if (a_es->buf_in_size == l_buf_in_size_before) {
            debug_if(s_debug_more, L_DEBUG, "UDP read: buf_in_size unchanged, breaking loop");
            break;
        }
        
        l_iterations++;
    }
    
    if (l_iterations >= l_max_iterations) {
        log_it(L_WARNING, "UDP client read callback: max iterations reached, %zu bytes remaining", a_es->buf_in_size);
    }
}

/**
 * @brief Register UDP trans adapter
 */
int dap_net_trans_udp_stream_register(void)
{
    // Initialize UDP server module first (registers server operations)
    int l_ret = dap_net_trans_udp_server_init();
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize UDP server module: %d", l_ret);
        return l_ret;
    }

    log_it(L_DEBUG, "dap_net_trans_udp_stream_register: UDP server module initialized, registering trans");
    
    // Register UDP trans operations
    int l_ret_trans = dap_net_trans_register("UDP",
                                                DAP_NET_TRANS_UDP_BASIC,
                                                &s_udp_ops,
                                                DAP_NET_TRANS_SOCKET_UDP,
                                                NULL);  // No inheritor needed at registration
    if (l_ret_trans != 0) {
        log_it(L_ERROR, "Failed to register UDP trans: %d", l_ret_trans);
        dap_net_trans_udp_server_deinit();
        return l_ret_trans;
    }

    log_it(L_NOTICE, "UDP trans registered successfully");
    
    return 0;
}

/**
 * @brief Unregister UDP trans adapter
 */
int dap_net_trans_udp_stream_unregister(void)
{
    int l_ret = dap_net_trans_unregister(DAP_NET_TRANS_UDP_BASIC);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to unregister UDP trans: %d", l_ret);
        return l_ret;
    }

    // Deinitialize UDP server module
    dap_net_trans_udp_server_deinit();

    log_it(L_NOTICE, "UDP trans unregistered successfully");
    return 0;
}

/**
 * @brief Create default UDP configuration
 */
dap_stream_trans_udp_config_t dap_stream_trans_udp_config_default(void)
{
    dap_stream_trans_udp_config_t l_config = {
        .max_packet_size = DAP_STREAM_UDP_DEFAULT_MAX_PACKET_SIZE,
        .keepalive_ms = DAP_STREAM_UDP_DEFAULT_KEEPALIVE_MS,
        .enable_checksum = true,
        .allow_fragmentation = false
    };
    return l_config;
}

/**
 * @brief Set UDP configuration
 */
int dap_stream_trans_udp_set_config(dap_net_trans_t *a_trans,
                                        const dap_stream_trans_udp_config_t *a_config)
{
    if (!a_trans || !a_config) {
        log_it(L_ERROR, "Invalid arguments for UDP config set");
        return -1;
    }

    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_trans);
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        return -1;
    }

    memcpy(&l_priv->config, a_config, sizeof(dap_stream_trans_udp_config_t));
    log_it(L_DEBUG, "UDP trans configuration updated");
    return 0;
}

/**
 * @brief Get UDP configuration
 */
int dap_stream_trans_udp_get_config(dap_net_trans_t *a_trans,
                                        dap_stream_trans_udp_config_t *a_config)
{
    if (!a_trans || !a_config) {
        log_it(L_ERROR, "Invalid arguments for UDP config get");
        return -1;
    }

    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_trans);
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        return -1;
    }

    memcpy(a_config, &l_priv->config, sizeof(dap_stream_trans_udp_config_t));
    return 0;
}

/**
 * @brief Check if stream is using UDP trans
 */
bool dap_stream_trans_is_udp(const dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans)
        return false;
    return a_stream->trans->type == DAP_NET_TRANS_UDP_BASIC;
}

/**
 * @brief Get UDP server from trans
 */
dap_server_t *dap_stream_trans_udp_get_server(const dap_stream_t *a_stream)
{
    if (!dap_stream_trans_is_udp(a_stream))
        return NULL;

    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_stream->trans);
    return l_priv ? l_priv->server : NULL;
}

/**
 * @brief Get UDP event socket from trans
 */
dap_events_socket_t *dap_stream_trans_udp_get_esocket(const dap_stream_t *a_stream)
{
    if (!dap_stream_trans_is_udp(a_stream))
        return NULL;

    return a_stream->trans_ctx ? a_stream->trans_ctx->esocket : NULL;
}

/**
 * @brief Get current session ID
 */
uint64_t dap_stream_trans_udp_get_session_id(const dap_stream_t *a_stream)
{
    if (!dap_stream_trans_is_udp(a_stream))
        return 0;

    // session_id is now per-stream, not per-transport
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx((dap_stream_t*)a_stream);
    return l_udp_ctx ? l_udp_ctx->session_id : 0;
}

/**
 * @brief Get current sequence number
 */
uint32_t dap_stream_trans_udp_get_seq_num(const dap_stream_t *a_stream)
{
    if (!dap_stream_trans_is_udp(a_stream))
        return 0;

    // seq_num is now per-stream, not per-transport
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx((dap_stream_t*)a_stream);
    return l_udp_ctx ? l_udp_ctx->seq_num : 0;
}

/**
 * @brief Set remote peer address
 * @deprecated Remote address is now per-stream, stored in UDP context.
 *             Use esocket's addr_storage instead.
 */
int dap_stream_trans_udp_set_remote_addr(dap_net_trans_t *a_trans,
                                              const struct sockaddr *a_addr,
                                              socklen_t a_addr_len)
{
    (void)a_trans;
    (void)a_addr;
    (void)a_addr_len;
    
    // remote_addr is now per-stream (in dap_net_trans_udp_ctx_t), not per-transport
    // This function is deprecated
    log_it(L_WARNING, "dap_stream_trans_udp_set_remote_addr is deprecated: remote_addr is now per-stream");
    return 0; // Return success for compatibility
}

/**
 * @brief Get remote peer address
 * @deprecated Remote address is now per-stream, stored in UDP context.
 *             Use esocket's addr_storage instead.
 */
int dap_stream_trans_udp_get_remote_addr(dap_net_trans_t *a_trans,
                                              struct sockaddr *a_addr,
                                              socklen_t *a_addr_len)
{
    (void)a_trans;
    (void)a_addr;
    (void)a_addr_len;
    
    // remote_addr is now per-stream (in dap_net_trans_udp_ctx_t), not per-transport
    // This function is deprecated
    log_it(L_WARNING, "dap_stream_trans_udp_get_remote_addr is deprecated: remote_addr is now per-stream");
    return -1; // Return error to indicate data not available
}

//=============================================================================
// Tranport operations implementation
//=============================================================================

/**
 * @brief Callback to get remote address for datagram flow
 * 
 * For CLIENT flows: returns stable server address from trans UDP context
 * For SERVER flows: returns client address from stream session
 * 
 */
static bool s_get_remote_addr_cb(dap_io_flow_datagram_t *a_flow,
                                  struct sockaddr_storage *a_addr_out,
                                  socklen_t *a_addr_len_out)
{
    if (!a_flow || !a_addr_out || !a_addr_len_out) {
        log_it(L_ERROR, "Invalid arguments to s_get_remote_addr_cb");
        return false;
    }
    
    // Get stream from protocol_data (set in s_udp_stage_prepare or server flow creation)
    dap_stream_t *l_stream = (dap_stream_t*)a_flow->protocol_data;
    if (!l_stream) {
        log_it(L_ERROR, "No stream in datagram flow protocol_data!");
        return false;
    }
    
    // CLIENT flow: get address from trans UDP context (stable copy)
    if (l_stream->is_client_to_uplink || !l_stream->_server_session) {
        // Use helper function to get UDP context (handles both client and server-side)
        dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(l_stream);
        if (!l_udp_ctx) {
            log_it(L_ERROR, "CLIENT stream has no UDP context!");
            return false;
        }
        
        if (l_udp_ctx->remote_addr.ss_family == 0 || l_udp_ctx->remote_addr_len == 0) {
            log_it(L_ERROR, "CLIENT UDP context has no remote_addr!");
            return false;
        }
        
        memcpy(a_addr_out, &l_udp_ctx->remote_addr, sizeof(struct sockaddr_storage));
        *a_addr_len_out = l_udp_ctx->remote_addr_len;
        return true;
    }
    
    // SERVER flow: get address from flow->remote_addr (filled by datagram layer)
    if (a_flow->remote_addr.ss_family == 0 || a_flow->remote_addr_len == 0) {
        log_it(L_ERROR, "SERVER flow has no remote_addr!");
        return false;
    }
    
    memcpy(a_addr_out, &a_flow->remote_addr, sizeof(struct sockaddr_storage));
    *a_addr_len_out = a_flow->remote_addr_len;
    return true;
}

/**
 * @brief Initialize UDP trans
 */
static int s_udp_init(dap_net_trans_t *a_trans, dap_config_t *a_config)
{
    if (!a_trans) {
        log_it(L_ERROR, "Cannot init NULL trans");
        return -1;
    }

    dap_stream_trans_udp_private_t *l_priv = DAP_NEW_Z(dap_stream_trans_udp_private_t);
    if (!l_priv) {
        log_it(L_CRITICAL, "Memory allocation failed for UDP private data");
        return -1;
    }

    // Initialize per-transport (shared) data only
    l_priv->config = dap_stream_trans_udp_config_default();
    l_priv->server = NULL;
    l_priv->user_data = NULL;
    
    // Per-stream data (session_id, seq_num, alice_key, remote_addr) is now in dap_net_trans_udp_ctx_t
    
    // Read debug configuration
    if (a_config) {
        s_debug_more = dap_config_get_item_bool_default(a_config, "stream_udp", "debug_more", false);
        log_it( L_INFO, "UDP transport: read debug_more=%d from config section [stream_udp]", s_debug_more);
    } else {
        log_it(L_WARNING, "UDP transport init: no config provided, debug_more remains disabled");
    }
    
    UNUSED(a_config); // Config can be used to override defaults

    a_trans->_inheritor = l_priv;
    
    // UDP trans doesn't support session control (connectionless)
    a_trans->has_session_control = false;
    a_trans->mtu = DAP_STREAM_UDP_DEFAULT_MAX_PACKET_SIZE;
    
    log_it(L_DEBUG, "UDP trans initialized (uses dap_events_socket for I/O)");
    return 0;
}

/**
 * @brief Deinitialize UDP trans
 */
static void s_udp_deinit(dap_net_trans_t *a_trans)
{
    if (!a_trans)
        return;

    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_trans);
    if (l_priv) {
        // alice_key is now per-stream, cleaned up in s_udp_close
        DAP_DELETE(l_priv);
        a_trans->_inheritor = NULL;
        log_it(L_DEBUG, "UDP trans deinitialized");
    }
}

/**
 * @brief Connect to remote UDP endpoint
 */
static int s_udp_connect(dap_stream_t *a_stream, const char *a_host, uint16_t a_port,
                          dap_net_trans_connect_cb_t a_callback)
{
    if (!a_stream || !a_host) {
        log_it(L_ERROR, "Invalid arguments for UDP connect");
        return -1;
    }

    if (!a_stream->trans) {
        log_it(L_ERROR, "Stream has no trans");
        return -1;
    }

    // Get UDP per-stream context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_or_create_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        log_it(L_ERROR, "Failed to get UDP context");
        return -1;
    }

    // Parse address and store in remote_addr
    struct sockaddr_in *l_addr_in = (struct sockaddr_in*)&l_udp_ctx->remote_addr;
    l_addr_in->sin_family = AF_INET;
    l_addr_in->sin_port = htons(a_port);
    
    if (inet_pton(AF_INET, a_host, &l_addr_in->sin_addr) != 1) {
        log_it(L_ERROR, "Invalid IPv4 address: %s", a_host);
        return -1;
    }

    l_udp_ctx->remote_addr_len = sizeof(struct sockaddr_in);
    
    debug_if(s_debug_more, L_DEBUG, "UDP trans connected to %s:%u, calling callback %p", 
             a_host, a_port, a_callback);
    
    // Call callback immediately (UDP is connectionless)
    if (a_callback) {
        a_callback(a_stream, 0);
        debug_if(s_debug_more, L_DEBUG, "UDP connect callback completed");
    }
    
    return 0;
}

/**
 * @brief Start listening for UDP connections
 */
static int s_udp_listen(dap_net_trans_t *a_trans, const char *a_addr, uint16_t a_port,
                         dap_server_t *a_server)
{
    if (!a_trans) {
        log_it(L_ERROR, "Invalid arguments for UDP listen");
        return -1;
    }

    dap_stream_trans_udp_private_t *l_priv = 
        (dap_stream_trans_udp_private_t*)a_trans->_inheritor;
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        return -1;
    }

    // Store server reference
    l_priv->server = a_server;
    
    // UDP listening is handled by dap_server_t which creates dap_events_socket_t
    // The server will call callbacks registered via dap_net_trans_udp_stream_add_proc()
    // which use dap_events_socket for all I/O operations
    log_it(L_INFO, "UDP trans listening on %s:%u (via dap_events_socket)", 
           a_addr ? a_addr : "0.0.0.0", a_port);
    return 0;
}

/**
 * @brief Accept incoming UDP "connection"
 */
static int s_udp_accept(dap_events_socket_t *a_listener, dap_stream_t **a_stream_out)
{
    if (!a_listener || !a_stream_out) {
        log_it(L_ERROR, "Invalid arguments for UDP accept");
        return -1;
    }
    
    // UDP is connectionless, so "accept" creates a new stream for datagram source
    // Stream is created by server layer and associated with socket
    log_it(L_DEBUG, "UDP trans accept");
    return 0;
}

static dap_net_trans_ctx_t *s_udp_get_or_create_ctx(dap_stream_t *a_stream) {
    debug_if(s_debug_more, L_INFO, "s_udp_get_or_create_ctx: stream=%p, trans_ctx=%p", a_stream, a_stream ? a_stream->trans_ctx : NULL);
    if (!a_stream->trans_ctx) {
        debug_if(s_debug_more,L_INFO, "s_udp_get_or_create_ctx: Creating NEW trans_ctx");
        a_stream->trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
        if (a_stream->trans) {
            a_stream->trans_ctx->trans = a_stream->trans;
        }
    }
    debug_if(s_debug_more, L_INFO, "s_udp_get_or_create_ctx: Returning trans_ctx=%p", a_stream->trans_ctx);
    return a_stream->trans_ctx;
}

/**
 * @brief Initialize encryption handshake
 */
static int s_udp_handshake_init(dap_stream_t *a_stream,
                                 dap_net_handshake_params_t *a_params,
                                 dap_net_trans_handshake_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid arguments for UDP handshake init");
        return -1;
    }

    if (!a_stream->trans) {
        log_it(L_ERROR, "Stream has no trans");
        return -1;
    }

    log_it(L_INFO, "UDP handshake init: enc_type=%d, pkey_type=%d",
           a_params->enc_type, a_params->pkey_exchange_type);
    
    // Get or create trans_ctx
    dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(a_stream);
    if (!l_ctx) {
        log_it(L_ERROR, "Failed to get trans_ctx");
        return -1;
    }
    
    l_ctx->handshake_cb = a_callback;
    
    // Get or create UDP per-stream context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_or_create_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        log_it(L_ERROR, "Failed to create UDP stream context");
        return -1;
    }
    
    // Set up read callback for client esocket and link stream
    log_it(L_NOTICE, "HANDSHAKE INIT: esocket=%p", l_ctx->esocket);
    if (l_ctx->esocket) {
        log_it(L_NOTICE, "Setting up UDP client esocket %p (fd=%d) for handshake_init", 
                 l_ctx->esocket, l_ctx->esocket->fd);
        
        // Store stream pointer
        l_udp_ctx->stream = a_stream;
        l_ctx->stream = a_stream;
        
        // IMPORTANT: Store trans_ctx in callbacks.arg (NOT _inheritor!)
        // _inheritor is owned by client infrastructure (may be dap_client_t or NULL)
        // We use callbacks.arg to pass trans_ctx to read callback
        l_ctx->esocket->callbacks.arg = l_ctx;
        
        debug_if(s_debug_more, L_DEBUG, "trans_ctx %p stored in callbacks.arg, trans_ctx->stream = %p, esocket->_inheritor=%p (client)", 
                 l_ctx, a_stream, l_ctx->esocket->_inheritor);
        
        // Set read callback
        if (!l_ctx->esocket->callbacks.read_callback) {
            l_ctx->esocket->callbacks.read_callback = dap_stream_trans_udp_read_callback;
            debug_if(s_debug_more, L_DEBUG, "Set UDP client read callback for esocket %p", l_ctx->esocket);
        }
    }
    
    // Generate random session ID for THIS stream
    if (randombytes((uint8_t*)&l_udp_ctx->session_id, sizeof(l_udp_ctx->session_id)) != 0) {
        log_it(L_ERROR, "Failed to generate random session ID");
        return -1;
    }
    l_udp_ctx->seq_num = 0;
    
    // Generate Alice key for THIS stream
    if (l_udp_ctx->alice_key) {
        dap_enc_key_delete(l_udp_ctx->alice_key);
    }
    
    l_udp_ctx->alice_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    if (!l_udp_ctx->alice_key) {
        log_it(L_ERROR, "Failed to generate Alice key for UDP handshake");
        return -1;
    }
    
    // Use OUR generated public key (not a_params), since we need the matching private key for decapsulation
    void *l_alice_pub = l_udp_ctx->alice_key->pub_key_data;
    size_t l_alice_pub_size = l_udp_ctx->alice_key->pub_key_data_size;
    
    // Save handshake payload for retransmission
    l_udp_ctx->handshake_payload = DAP_DUP_SIZE(l_alice_pub, l_alice_pub_size);
    if (!l_udp_ctx->handshake_payload) {
        log_it(L_ERROR, "Failed to allocate handshake payload for retransmission");
        return -1;
    }
    l_udp_ctx->handshake_payload_size = l_alice_pub_size;
    l_udp_ctx->handshake_retries = 0;
    l_udp_ctx->handshake_complete = false;
    
    // Send HANDSHAKE packet with alice public key via s_udp_write_typed
    ssize_t l_sent = s_udp_write_typed(a_stream, DAP_STREAM_UDP_PKT_HANDSHAKE, 
                                        l_alice_pub, l_alice_pub_size);
    
    if (l_sent < 0) {
        log_it(L_ERROR, "Failed to send UDP handshake init");
        DAP_DELETE(l_udp_ctx->handshake_payload);
        l_udp_ctx->handshake_payload = NULL;
        return -1;
    }
    
    // Start handshake retransmission timer
    dap_worker_t *l_worker = l_ctx->esocket ? l_ctx->esocket->worker : dap_worker_get_current();
    log_it(L_NOTICE, "HANDSHAKE: esocket=%p, esocket->worker=%p, worker_current=%p",
           l_ctx->esocket, l_ctx->esocket ? l_ctx->esocket->worker : NULL, dap_worker_get_current());
    if (l_worker) {
        l_udp_ctx->handshake_timer = dap_timerfd_start_on_worker(
            l_worker,
            HANDSHAKE_RETRANSMIT_TIMEOUT_MS,
            s_handshake_retransmit_timer_cb,
            l_udp_ctx
        );
        if (l_udp_ctx->handshake_timer) {
            log_it(L_NOTICE, "HANDSHAKE: retransmission timer STARTED (timeout=%dms, max_retries=%d)",
                   HANDSHAKE_RETRANSMIT_TIMEOUT_MS, HANDSHAKE_RETRANSMIT_MAX_RETRIES);
        } else {
            log_it(L_ERROR, "HANDSHAKE: FAILED to start retransmission timer!");
        }
    } else {
        log_it(L_ERROR, "HANDSHAKE: NO WORKER AVAILABLE, timer NOT started (esocket=%p)", l_ctx->esocket);
    }
    
    log_it(L_INFO, "UDP handshake init sent: %zd bytes (session_id=%" DAP_UINT64_FORMAT_U ")", 
           l_sent, l_udp_ctx->session_id);
    
    return 0;
}

/**
 * @brief Process handshake response from server (client-side)
 * @param a_stream Client stream
 * @param a_data Bob's public key + ciphertext from server
 * @param a_data_size Size of response data
 * @return 0 on success, negative on error
 */
static int s_udp_handshake_response(dap_stream_t *a_stream,
                                     const void *a_data, size_t a_data_size)
{
    if (!a_stream || !a_data || a_data_size == 0) {
        log_it(L_ERROR, "Invalid arguments for UDP handshake response");
        return -1;
    }

    log_it(L_DEBUG, "UDP handshake response: processing %zu bytes", a_data_size);
    
    // Validate size: Bob's ciphertext (768 bytes) + session_id (8 bytes) = 776 bytes
    const size_t EXPECTED_SIZE = CRYPTO_CIPHERTEXTBYTES + sizeof(uint64_t);
    if (a_data_size != EXPECTED_SIZE) {
        log_it(L_ERROR, "Invalid handshake response size: %zu (expected %zu = %d ciphertext + 8 session_id)",
               a_data_size, EXPECTED_SIZE, CRYPTO_CIPHERTEXTBYTES);
        return -1;
    }
    
    // Get Alice's key from UDP per-stream context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        log_it(L_ERROR, "UDP handshake response: no UDP context");
        return -1;
    }
    if (!l_udp_ctx->alice_key) {
        log_it(L_ERROR, "UDP handshake response: no Alice key");
        return -1;
    }
    
    // Deserialize: Bob's ciphertext (CRYPTO_CIPHERTEXTBYTES) + session_id (8 bytes)
    uint8_t l_bob_ciphertext[CRYPTO_CIPHERTEXTBYTES];
    uint64_t l_session_id_be;
    
    int l_ret = dap_deserialize_multy(a_data, a_data_size,
                                      l_bob_ciphertext, (uint64_t)CRYPTO_CIPHERTEXTBYTES,
                                      &l_session_id_be, sizeof(uint64_t),
                                      DOOF_PTR);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to deserialize handshake response");
        return -1;
    }
    
    uint64_t l_server_session_id = be64toh(l_session_id_be);
    
    debug_if(s_debug_more, L_DEBUG,
             "HANDSHAKE response: ciphertext=%zu bytes, server_session_id=0x%" DAP_UINT64_FORMAT_x " (replacing client's 0x%" DAP_UINT64_FORMAT_x ")",
             (size_t)CRYPTO_CIPHERTEXTBYTES, l_server_session_id, l_udp_ctx->session_id);
    
    // CRITICAL: Replace client's session_id with server's session_id!
    // All subsequent packets MUST use server's session_id
    l_udp_ctx->session_id = l_server_session_id;
    
    // DEBUG: Log Alice's public key that we sent (first 16 bytes)
    if (s_debug_more && l_udp_ctx->alice_key->pub_key_data && l_udp_ctx->alice_key->pub_key_data_size >= 16) {
        char l_hex[49] = {0};
        for (int i = 0; i < 16; i++) {
            sprintf(l_hex + i*3, "%02x ", ((uint8_t*)l_udp_ctx->alice_key->pub_key_data)[i]);
        }
        log_it(L_DEBUG, "CLIENT sent Alice public key (first 16 bytes): %s", l_hex);
    }
    
    // DEBUG: Log received ciphertext (first 16 bytes)
    if (s_debug_more && a_data && a_data_size >= 16) {
        char l_hex[49] = {0};
        for (int i = 0; i < 16; i++) {
            sprintf(l_hex + i*3, "%02x ", ((uint8_t*)a_data)[i]);
        }
        log_it(L_DEBUG, "CLIENT received ciphertext from server (first 16 bytes): %s", l_hex);
    }
    
    dap_enc_key_t *l_alice_key = l_udp_ctx->alice_key;
    log_it(L_DEBUG, "UDP handshake response: alice_key=%p, gen_alice_shared_key=%p", 
           l_alice_key, l_alice_key->gen_alice_shared_key);
    
    // UDP uses BINARY protocol, not JSON!
    // Server sends: Bob's ciphertext (768 bytes) + session_id (8 bytes)
    // We already extracted session_id above, now use ciphertext for KEM
    
    // Perform KEM decapsulation (Alice side) using received ciphertext
    if (!l_alice_key->gen_alice_shared_key) {
        log_it(L_ERROR, "Alice key doesn't support KEM decapsulation");
            return -1;
        }
    
    size_t l_shared_key_size = l_alice_key->gen_alice_shared_key(
        l_alice_key,
        NULL,  // Alice's private key (already in l_alice_key->_inheritor)
        CRYPTO_CIPHERTEXTBYTES,  // Size of Bob's ciphertext
        (uint8_t*)l_bob_ciphertext  // Bob's ciphertext (extracted above)
    );
    
    if (l_shared_key_size == 0 || !l_alice_key->shared_key) {
        log_it(L_ERROR, "Failed to derive shared key from Bob's ciphertext");
        return -1;
    }
    
    log_it(L_INFO, "CLIENT: derived shared key via KEM decapsulation (%zu bytes)", 
           l_shared_key_size);
    
    // Derive HANDSHAKE cipher key from shared secret using KDF-SHAKE256
    dap_enc_key_t *l_handshake_key = dap_enc_kdf_create_cipher_key(
        l_alice_key,
        DAP_ENC_KEY_TYPE_SALSA2012,
        "udp_handshake",
        14,
        0,  // Counter = 0
        32  // Key size
    );
    
    if (!l_handshake_key) {
        log_it(L_ERROR, "Failed to derive handshake cipher key via KDF");
        return -1;
    }
    
    log_it(L_INFO, "CLIENT: handshake key derived via KDF-SHAKE256");
    
    // Store handshake key in UDP context (will be used for encryption/decryption)
    if (l_udp_ctx->handshake_key) {
        log_it(L_WARNING, "CLIENT: replacing existing handshake_key %p with new one %p",
               l_udp_ctx->handshake_key, l_handshake_key);
        dap_enc_key_delete(l_udp_ctx->handshake_key);
    }
    l_udp_ctx->handshake_key = l_handshake_key;
    
    // CRITICAL: Cancel handshake retransmission timer - handshake succeeded!
    s_cancel_handshake_timer(l_udp_ctx);
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT: stored handshake_key=%p for session_id=0x%" DAP_UINT64_FORMAT_x "",
             l_udp_ctx->handshake_key, l_udp_ctx->session_id);
    
    // Create session if it doesn't exist
    if (!a_stream->session) {
        a_stream->session = dap_stream_session_pure_new();
        if (!a_stream->session) {
            log_it(L_ERROR, "Failed to create session");
            return -1;
        }
    }
    
    // DO NOT create session key here! It will be received and decrypted during SESSION_CREATE
    a_stream->session->key = NULL;
    
    log_it(L_INFO, "UDP handshake complete: CLIENT handshake key established, waiting for session key from server");
    return 0;
}

/**
 * @brief Process incoming handshake data (server-side)
 */
static int s_udp_handshake_process(dap_stream_t *a_stream,
                                    const void *a_data, size_t a_data_size,
                                    void **a_response, size_t *a_response_size)
{
    if (!a_stream || !a_data || a_data_size == 0) {
        log_it(L_ERROR, "Invalid arguments for UDP handshake process");
        return -1;
    }

    log_it(L_DEBUG, "UDP handshake process: %zu bytes", a_data_size);
    
    // DEBUG: Log Alice's public key (first 16 bytes)
    if (a_data && a_data_size >= 16) {
        char l_hex[49] = {0};
        for (int i = 0; i < 16; i++) {
            sprintf(l_hex + i*3, "%02x ", ((uint8_t*)a_data)[i]);
        }
        log_it(L_INFO, "SERVER received Alice public key (first 16 bytes): %s", l_hex);
    }
    
    // Generate ephemeral Bob key (Kyber512)
    dap_enc_key_t *l_bob_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, NULL, 0, 0);
    if (!l_bob_key) {
        log_it(L_ERROR, "Failed to generate Bob key");
        return -1;
    }
    
    void *l_bob_pub = NULL;
    size_t l_bob_pub_size = 0;
    void *l_shared_key = NULL;
    size_t l_shared_key_size = 0;
    
    if (l_bob_key->gen_bob_shared_key) {
        l_shared_key_size = l_bob_key->gen_bob_shared_key(l_bob_key, a_data, a_data_size, &l_bob_pub);
        l_shared_key = l_bob_key->shared_key;  // CORRECT: shared_key, not priv_key_data!
        l_bob_pub_size = l_bob_key->pub_key_data_size;
        
        // Check if key generation succeeded
        if (!l_bob_pub || l_shared_key_size == 0 || !l_shared_key) {
            log_it(L_ERROR, "Failed to generate shared key from client data (invalid public key?)");
            dap_enc_key_delete(l_bob_key);
            return -1;
        }
    } else {
        log_it(L_ERROR, "Key type doesn't support KEM handshake");
        dap_enc_key_delete(l_bob_key);
        return -1;
    }
    
    // Create session and set key
    if (!a_stream->session) {
        a_stream->session = dap_stream_session_pure_new();
    }
    if (a_stream->session) {
        if (a_stream->session->key) dap_enc_key_delete(a_stream->session->key);
        
        // DEBUG: Log Bob's ciphertext (first 16 bytes)
        debug_if(s_debug_more, L_DEBUG, "SERVER: Bob's ciphertext size=%zu", l_bob_pub_size);
        if (s_debug_more && l_bob_pub && l_bob_pub_size >= 16) {
            char l_hex[49] = {0};
            for (int i = 0; i < 16; i++) {
                sprintf(l_hex + i*3, "%02x ", ((uint8_t*)l_bob_pub)[i]);
            }
            log_it(L_DEBUG, "SERVER sending ciphertext to Alice (first 16 bytes): %s", l_hex);
        }
        
        // DEBUG: Log first 16 bytes of shared secret
        debug_if(s_debug_more, L_DEBUG, "SERVER: shared secret size=%zu", l_shared_key_size);
        if (s_debug_more && l_shared_key && l_shared_key_size >= 16) {
            char l_hex[49] = {0};
            for (int i = 0; i < 16; i++) {
                sprintf(l_hex + i*3, "%02x ", ((uint8_t*)l_shared_key)[i]);
            }
            log_it(L_DEBUG, "SERVER shared secret (first 16 bytes): %s", l_hex);
        }
        
        // Create HANDSHAKE key from shared secret using KDF (NOT session key yet!)
        // This key will be used to encrypt/decrypt the session key seed
        // Using KDF with context "handshake" and counter 0 (no ratcheting for handshake)
        dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
        if (l_udp_ctx) {
            if (l_udp_ctx->handshake_key) {
                dap_enc_key_delete(l_udp_ctx->handshake_key);
            }
            
            // Use NEW KDF API: derive handshake key from Bob's KEM key
            l_udp_ctx->handshake_key = dap_enc_kdf_create_cipher_key(
                l_bob_key,                      // KEM key with shared secret
                DAP_ENC_KEY_TYPE_SALSA2012,     // Cipher type for handshake encryption
                "udp_handshake",                // Context string
                14,                             // Context size (strlen("udp_handshake"))
                0,                              // Counter = 0 (no ratcheting for handshake)
                32                              // Key size (32 bytes for SALSA2012)
            );
            
            if (!l_udp_ctx->handshake_key) {
                log_it(L_ERROR, "SERVER: failed to derive handshake key using KDF");
                dap_enc_key_delete(l_bob_key);
                return -1;
            }
            
            log_it(L_INFO, "SERVER: handshake encryption key created using KDF (stream=%p, key=%p)", 
                   a_stream, l_udp_ctx->handshake_key);
        }
        
        // DO NOT create session key here! It will be created during SESSION_CREATE
        a_stream->session->key = NULL;
    }
    
    // Prepare JSON response for client's s_enc_init_response
    char l_bob_pub_b64[DAP_ENC_BASE64_ENCODE_SIZE(l_bob_pub_size) + 1];
    dap_enc_base64_encode(l_bob_pub, l_bob_pub_size, l_bob_pub_b64, DAP_ENC_DATA_TYPE_B64);
    
    char l_session_id_b64[DAP_ENC_BASE64_ENCODE_SIZE(sizeof(uint64_t) * 3) + 1]; // Plenty space for numeric string
    char l_session_id_str[64];
    snprintf(l_session_id_str, sizeof(l_session_id_str), "%lu", (unsigned long)time(NULL));
    dap_enc_base64_encode(l_session_id_str, strlen(l_session_id_str), l_session_id_b64, DAP_ENC_DATA_TYPE_B64);
    
    dap_string_t *l_json_resp = dap_string_new("");
    dap_string_append_printf(l_json_resp, 
        "[{\"session_id\":\"%s\"},{\"bob_message\":\"%s\"}]",
        l_session_id_b64, l_bob_pub_b64);

    if (a_response && a_response_size) {
        // Include null-terminator in response size for JSON parsing on client
        // JSON parser expects null-terminated string
        *a_response = l_json_resp->str;
        *a_response_size = l_json_resp->len + 1;  // +1 to include null-terminator
        DAP_DELETE(l_json_resp); // Free struct, keep str (dap_string->str is null-terminated)
    } else {
        dap_string_free(l_json_resp, true);
    }
    
    // l_bob_pub points to l_bob_key->pub_key_data (NOT separately allocated!)
    // It will be freed automatically when we delete l_bob_key below
    // DO NOT delete l_bob_pub separately - it causes double-free!
    
    // l_shared_key points to internal buffer of l_bob_key, so it is freed when l_bob_key is deleted
    // But we should zero it out if possible before delete (dap_enc_key_delete might do it)
    
    dap_enc_key_delete(l_bob_key);
    
    return 0;
}

/**
 * @brief Create session
 */
static int s_udp_session_create(dap_stream_t *a_stream,
                                 dap_net_session_params_t *a_params,
                                 dap_net_trans_session_cb_t a_callback)
{
    if (!a_stream || !a_params) {
        log_it(L_ERROR, "Invalid arguments for UDP session create");
        return -1;
    }

    // Get UDP per-stream context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        log_it(L_ERROR, "No UDP context for session create");
        return -1;
    }

    // Store callback
    dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(a_stream);
    if (!l_ctx) {
        log_it(L_ERROR, "No trans_ctx for session create");
        return -1;
    }
    
    // DUPLICATE PROTECTION: Check if SESSION_CREATE already sent for this stream
    // This prevents multiple identical requests when FSM cycles
    if (l_ctx->session_create_sent) {
        debug_if(s_debug_more, L_DEBUG,
                 "CLIENT: ignoring duplicate session_create call (SESSION_CREATE already sent for this stream)");
        // Still update callback in case it changed
        l_ctx->session_create_cb = a_callback;
        return 0;  // Success, but don't send duplicate request
    }
    
    l_ctx->session_create_cb = a_callback;

    // Serialize session parameters (channels, encryption, etc) to send to server
    // Server needs to know which channels to activate
    const char *l_channels = a_params->channels ? a_params->channels : "";
    size_t l_channels_len = strlen(l_channels);
    
    // SECURITY: SESSION_CREATE must be encrypted with handshake key!
    if (!l_udp_ctx->handshake_key) {
        log_it(L_ERROR, "No handshake key for encrypting SESSION_CREATE");
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT: encrypting SESSION_CREATE with handshake_key=%p (session_id=0x%" DAP_UINT64_FORMAT_x ")",
             l_udp_ctx->handshake_key, l_udp_ctx->session_id);
    
    // Prepare JSON payload (NO session_id - it's already in internal header!)
    dap_json_t *l_json = dap_json_object_new();
    if (!l_json) {
        log_it(L_ERROR, "Failed to create JSON object for SESSION_CREATE");
        return -1;
    }
    
    // Add channels only (session_id is in internal header)
    dap_json_object_add_string(l_json, "channels", l_channels);
    
    // Serialize to JSON string
    char *l_json_str = dap_json_to_string(l_json);
    size_t l_json_len = l_json_str ? strlen(l_json_str) : 0;
    
    debug_if(s_debug_more, L_DEBUG, "SESSION_CREATE JSON: '%s' (%zu bytes)", l_json_str ? l_json_str : "", l_json_len);
    
    // Send SESSION_CREATE packet (s_udp_write_typed will handle encryption)
    ssize_t l_sent = -1;
    if (l_json_str) {
        l_sent = s_udp_write_typed(a_stream, DAP_STREAM_UDP_PKT_SESSION_CREATE,
                                    l_json_str, l_json_len);
        DAP_DELETE(l_json_str);
    }
    
    dap_json_object_free(l_json);  // Free JSON object
    
    if (l_sent < 0) {
        log_it(L_ERROR, "Failed to send UDP session create request");
        return -1;
    }
    
    // Mark SESSION_CREATE as sent to prevent duplicates
    l_ctx->session_create_sent = true;
    
    debug_if(s_debug_more, L_DEBUG, "UDP session create request sent with channels '%s'", l_channels);
    
    return 0;
}

/**
 * @brief Start session
 */
static int s_udp_session_start(dap_stream_t *a_stream, uint32_t a_session_id,
                                dap_net_trans_ready_cb_t a_callback)
{
    debug_if(s_debug_more, L_DEBUG, "s_udp_session_start ENTRY: stream=%p, session_id=%u", a_stream, a_session_id);
    
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream for session start");
        return -1;
    }

    debug_if(s_debug_more, L_DEBUG, "UDP session start: session_id=%u", a_session_id);
    
    // Get UDP context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_or_create_udp_ctx(a_stream);
    
    debug_if(s_debug_more, L_DEBUG, "s_udp_session_start: AFTER get_udp_ctx: l_udp_ctx=%p", l_udp_ctx);
    
    if (!l_udp_ctx) {
        log_it(L_ERROR, "Failed to get UDP context for session start");
        return -2;
    }
    
    debug_if(s_debug_more, L_DEBUG, "SESSION_START: udp_ctx=%p, stream=%p, flow_ctrl=%p (BEFORE FC create)", 
           l_udp_ctx, a_stream, l_udp_ctx->flow_ctrl);
    
    // IDEMPOTENCY CHECK: If FC already created, just process buffered packets
    if (l_udp_ctx->flow_ctrl) {
        debug_if(s_debug_more, L_DEBUG,
                 "SESSION_START: FC already exists (%p), skipping creation", l_udp_ctx->flow_ctrl);
        
        // Process buffered packets if present
        if (l_udp_ctx->buffered_packets && l_udp_ctx->buffered_count > 0) {
            debug_if(s_debug_more, L_DEBUG,
                     "SESSION_START: Processing %zu buffered packets", l_udp_ctx->buffered_count);
            
            for (size_t i = 0; i < l_udp_ctx->buffered_count; i++) {
                dap_io_flow_ctrl_recv(l_udp_ctx->flow_ctrl, 
                                      l_udp_ctx->buffered_packets[i], 
                                      l_udp_ctx->buffered_packet_sizes[i]);
                DAP_DELETE(l_udp_ctx->buffered_packets[i]);
            }
            
            DAP_DELETE(l_udp_ctx->buffered_packets);
            DAP_DELETE(l_udp_ctx->buffered_packet_sizes);
            l_udp_ctx->buffered_packets = NULL;
            l_udp_ctx->buffered_packet_sizes = NULL;
            l_udp_ctx->buffered_count = 0;
            l_udp_ctx->buffered_capacity = 0;
        }
        
        l_udp_ctx->fc_creating = false;
        
        if (a_callback) {
            a_callback(a_stream, 0);
        }
        return 0;
    }
    
    // Create Flow Control for reliable delivery (client-side) via helper
    int l_fc_ret = s_ensure_client_flow_ctrl(l_udp_ctx);
    if (l_fc_ret != 0) {
        log_it(L_ERROR, "SESSION_START: failed to create flow_ctrl: %d", l_fc_ret);
        return l_fc_ret;
    }
    
    // CRITICAL: Process ALL buffered packets (in order)!
    // These packets arrived between SESSION_CREATE and FC creation
    debug_if(s_debug_more, L_DEBUG,
             "SESSION_START: Checking buffer: buffered_packets=%p, buffered_count=%zu",
             l_udp_ctx->buffered_packets, l_udp_ctx->buffered_count);
    
    if (l_udp_ctx->buffered_packets && l_udp_ctx->buffered_count > 0) {
        log_it(L_NOTICE, "SESSION_START: Processing %zu buffered packets through FC",
               l_udp_ctx->buffered_count);
        
        for (size_t i = 0; i < l_udp_ctx->buffered_count; i++) {
            log_it(L_NOTICE, "SESSION_START: Processing buffered packet #%zu (%zu bytes)",
                   i + 1, l_udp_ctx->buffered_packet_sizes[i]);
            
            int l_ret = dap_io_flow_ctrl_recv(l_udp_ctx->flow_ctrl, 
                                              l_udp_ctx->buffered_packets[i], 
                                              l_udp_ctx->buffered_packet_sizes[i]);
            if (l_ret != 0) {
                log_it(L_WARNING, "SESSION_START: Failed to process buffered packet #%zu: ret=%d", 
                       i + 1, l_ret);
            }
            
            // Free buffer
            DAP_DELETE(l_udp_ctx->buffered_packets[i]);
        }
        
        log_it(L_NOTICE, "SESSION_START: All %zu buffered packets processed", l_udp_ctx->buffered_count);
        
        // Free buffer arrays
        DAP_DELETE(l_udp_ctx->buffered_packets);
        DAP_DELETE(l_udp_ctx->buffered_packet_sizes);
        l_udp_ctx->buffered_packets = NULL;
        l_udp_ctx->buffered_packet_sizes = NULL;
        l_udp_ctx->buffered_count = 0;
        l_udp_ctx->buffered_capacity = 0;
    }
    
    // Clear fc_creating flag - FC is ready now!
    l_udp_ctx->fc_creating = false;
    log_it(L_NOTICE, "SESSION_START: Cleared fc_creating flag - FC is ready");
    
    // Call callback immediately (UDP session ready)
    if (a_callback) {
        a_callback(a_stream, 0);
    }
    
    return 0;
}

/**
 * @brief Read data from UDP trans
 */
/**
 * @brief UDP read function for OBFUSCATED protocol
 * 
 * NEW ARCHITECTURE with 100% encryption:
 * - HANDSHAKE: Obfuscated packets (600-900 bytes, size-based encryption)
 * - ALL OTHER: Fully encrypted with internal header
 * 
 * NO plaintext metadata, NO fixed sizes!
 */
static ssize_t s_udp_read(dap_stream_t *a_stream, void *a_buffer, size_t a_size)
{
    if (!a_stream || !a_stream->trans) {
        log_it(L_ERROR, "Invalid arguments for UDP read: stream or trans is NULL");
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG, "s_udp_read: stream=%p, buffer=%p, size=%zu", 
             a_stream, a_buffer, a_size);
    
    // Get esocket from trans_ctx
    dap_events_socket_t *l_es = NULL;
    dap_net_trans_ctx_t *l_ctx = (dap_net_trans_ctx_t*)a_stream->trans_ctx;
    if (l_ctx) {
        l_es = l_ctx->esocket;
    }

    debug_if(s_debug_more, L_DEBUG, "s_udp_read: ctx=%p, es=%p, buf_in=%p, buf_in_size=%zu", 
             l_ctx, l_es, l_es ? l_es->buf_in : NULL, l_es ? l_es->buf_in_size : 0);

    // NEW PROTOCOL: Two modes (CLIENT uses l_es->buf_in, SERVER uses a_buffer)
    if (!l_es) {
        // SERVER MODE: Server-side receives already-decrypted data from dap_io_flow!
        // The s_udp_packet_received_cb on server has already:
        // 1. Deobfuscated HANDSHAKE (if it was obfuscated)
        // 2. Decrypted SESSION_CREATE/DATA packets
        // 3. Parsed internal headers
        //
        // So here we just receive the final payload!
        
        debug_if(s_debug_more, L_INFO, "s_udp_read: SERVER MODE - data already processed by flow");
        
        if (!a_buffer || a_size == 0) {
            debug_if(s_debug_more, L_DEBUG, "UDP server read: no data");
            return 0;
        }
        
        // TODO: Server-side processing is now done in s_udp_packet_received_cb!
        // This path should not be reached in new architecture.
        log_it(L_WARNING, "SERVER MODE s_udp_read called unexpectedly (size=%zu)", a_size);
        return a_size;  // Consumed
    }
    
    // CLIENT MODE: Read from l_es->buf_in with OBFUSCATION support
    if (!l_es->buf_in || l_es->buf_in_size == 0) {
        debug_if(s_debug_more, L_DEBUG, "UDP CLIENT: no data in buf_in");
        return 0;  // No data available
    }
    
    debug_if(s_debug_more, L_DEBUG, "UDP CLIENT: buf_in_size=%zu", l_es->buf_in_size);
    
    // Get UDP context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_or_create_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        log_it(L_ERROR, "Failed to get UDP context in s_udp_read");
        return -1;
    }
    
    log_it(L_DEBUG, "UDP_READ: udp_ctx=%p, stream=%p, flow_ctrl=%p", 
           l_udp_ctx, a_stream, l_udp_ctx->flow_ctrl);
    
    // TRY DEOBFUSCATE AS HANDSHAKE (size 600-900 bytes)
    // Obfuscation key is EPHEMERAL - used only for transport masking!
    if (dap_transport_is_obfuscated_handshake_size(l_es->buf_in_size)) {
        uint8_t *l_handshake = NULL;
        size_t l_handshake_size = 0;
        
        int l_ret = dap_transport_deobfuscate_handshake(l_es->buf_in, l_es->buf_in_size,
                                                        &l_handshake, &l_handshake_size);
        
        if (l_ret == 0) {
            // Successfully deobfuscated as HANDSHAKE!
            // Obfuscation key is NOW DISCARDED - it was just for masking!
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT: Deobfuscated HANDSHAKE: %zu bytes → %zu bytes (obf key discarded)",
                     l_es->buf_in_size, l_handshake_size);
            
            // DUPLICATE PROTECTION: Check if handshake already completed
            if (l_udp_ctx->handshake_key) {
                debug_if(s_debug_more, L_DEBUG,
                         "CLIENT: ignoring duplicate HANDSHAKE response (handshake_key already exists)");
                DAP_DELETE(l_handshake);
                dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
                return 0;  // Ignore duplicate
            }
            
            // Process handshake response (Kyber shared secret derivation)
            int l_result = s_udp_handshake_response(a_stream, l_handshake, l_handshake_size);
            DAP_DELETE(l_handshake);
            
            // Call handshake callback
            if (l_ctx && l_ctx->handshake_cb) {
                debug_if(s_debug_more, L_DEBUG,
                         "CLIENT: calling handshake_cb with result=%d", l_result);
                l_ctx->handshake_cb(a_stream, NULL, 0, l_result);
            }
            
            // Clear buffer
            dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
            return l_es->buf_in_size;  // Consumed
        }
        
        // Deobfuscation failed - might be regular encrypted packet
        // Continue to decrypt with session key
    }
    
    // ENCRYPTED PACKET: Process via Flow Control OR buffer (if FC not ready)
    
    log_it(L_DEBUG, "CLIENT: udp_ctx=%p, flow_ctrl=%p, fc_creating=%d, buf_in_size=%zu", 
           l_udp_ctx, l_udp_ctx ? l_udp_ctx->flow_ctrl : NULL, l_udp_ctx ? l_udp_ctx->fc_creating : 0,
           l_es->buf_in_size);
    
    // BUFFER PATH: FC is being created (after SESSION_CREATE, before SESSION_START complete)
    if (l_udp_ctx->fc_creating || !l_udp_ctx->flow_ctrl) {
        log_it(L_NOTICE, "CLIENT: FC not ready (fc_creating=%d, flow_ctrl=%p) - checking packet type", 
               l_udp_ctx->fc_creating, l_udp_ctx->flow_ctrl);
        
        // Decrypt packet to check type
        if (!l_udp_ctx->handshake_key) {
            log_it(L_ERROR, "CLIENT: no decryption key for packet");
            dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
            return -1;
        }
        
        size_t l_decrypted_max = l_es->buf_in_size + 256;
        uint8_t *l_decrypted = DAP_NEW_SIZE(uint8_t, l_decrypted_max);
        if (!l_decrypted) {
            log_it(L_ERROR, "CLIENT: OOM for decryption");
            dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
            return -1;
        }
        
        size_t l_decrypted_size = dap_enc_decode(l_udp_ctx->handshake_key,
                                                 l_es->buf_in, l_es->buf_in_size,
                                                 l_decrypted, l_decrypted_max,
                                                 DAP_ENC_DATA_TYPE_RAW);
        
        if (l_decrypted_size == 0 || l_decrypted_size < sizeof(dap_stream_trans_udp_full_header_t)) {
            log_it(L_ERROR, "CLIENT: failed to decrypt or packet too small");
            DAP_DELETE(l_decrypted);
            dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
            return -1;
        }
        
        // Parse header
        dap_stream_trans_udp_full_header_t l_header;
        dap_deserialize_result_t l_deser = dap_deserialize_from_buffer_raw(
            &g_udp_full_header_schema,
            l_decrypted,
            sizeof(dap_stream_trans_udp_full_header_t),
            &l_header,
            NULL
        );
        
        if (l_deser.error_code != 0) {
            log_it(L_ERROR, "CLIENT: failed to parse header: %s",
                   l_deser.error_message ? l_deser.error_message : "unknown");
            DAP_DELETE(l_decrypted);
            dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
            return -1;
        }
        
        uint8_t l_type = l_header.type;
        size_t l_payload_size = l_decrypted_size - sizeof(dap_stream_trans_udp_full_header_t);
        const uint8_t *l_payload = l_decrypted + sizeof(dap_stream_trans_udp_full_header_t);
        
        log_it(L_NOTICE, "CLIENT: First packet type=%u (SESSION_CREATE=0x%02x, DATA=0x%02x)",
               l_type, DAP_STREAM_UDP_PKT_SESSION_CREATE, DAP_STREAM_UDP_PKT_DATA);
        
        if (l_type == DAP_STREAM_UDP_PKT_SESSION_CREATE) {
            // SESSION_CREATE response - process immediately to establish session key!
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT: Processing SESSION_CREATE response (%zu bytes)",
                     l_payload_size);
            
            if (l_payload_size != sizeof(uint64_t)) {
                log_it(L_ERROR, "CLIENT: invalid SESSION_CREATE size: %zu", l_payload_size);
                DAP_DELETE(l_decrypted);
                dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
                return -1;
            }
            
            // Extract KDF counter
            uint64_t l_kdf_counter_be;
            memcpy(&l_kdf_counter_be, l_payload, sizeof(l_kdf_counter_be));
            uint64_t l_kdf_counter = be64toh(l_kdf_counter_be);
            
            log_it(L_INFO, "CLIENT: Deriving session key with KDF counter=%" DAP_UINT64_FORMAT_U "", l_kdf_counter);
            
            // Derive session key
            dap_enc_key_t *l_session_key = dap_enc_kdf_create_cipher_key(
                l_udp_ctx->handshake_key,
                DAP_ENC_KEY_TYPE_SALSA2012,
                "udp_session",
                11,
                l_kdf_counter,
                32
            );
            
            if (!l_session_key) {
                log_it(L_ERROR, "CLIENT: failed to derive session key");
                DAP_DELETE(l_decrypted);
                dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
                return -1;
            }
            
            // Replace handshake key with session key
            // CRITICAL: After this, handshake_key and session->key point to SAME object!
            // Cleanup must avoid double-free
            dap_enc_key_delete(l_udp_ctx->handshake_key);
            l_udp_ctx->handshake_key = l_session_key;  // Same as a_stream->session->key
            
            // Set in stream->session
            if (!a_stream->session) {
                a_stream->session = dap_stream_session_pure_new();
                if (!a_stream->session) {
                    log_it(L_ERROR, "CLIENT: failed to create stream session");
                    DAP_DELETE(l_decrypted);
                    dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
                    return -1;
                }
            }
            
            if (a_stream->session->key) {
                dap_enc_key_delete(a_stream->session->key);
            }
            a_stream->session->key = l_session_key;
            a_stream->session->id = l_udp_ctx->session_id;
            
            log_it(L_INFO, "CLIENT: session key installed (stream=%p, session=%p, key=%p, session_id=0x%" DAP_UINT64_FORMAT_x ")",
                   a_stream, a_stream->session, a_stream->session->key, l_udp_ctx->session_id);
            
            // Call session_create_cb for client stage transition
            dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t*)a_stream->trans_ctx;
            if (l_trans_ctx && l_trans_ctx->session_create_cb) {
                debug_if(s_debug_more, L_DEBUG, "CLIENT: Calling session_create_cb (stream=%p, session=%p, key=%p)",
                         a_stream, a_stream->session, a_stream->session ? a_stream->session->key : NULL);
                l_trans_ctx->session_create_cb(a_stream, (uint32_t)l_udp_ctx->session_id, NULL, 0, 0);
                l_trans_ctx->session_create_cb = NULL;
            }
            
            // CRITICAL: Now create Flow Control!
            // Client workflow doesn't have natural SESSION_START stage, so we call it directly
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT: Calling s_udp_session_start to create FC (session key already set)");
            
            int l_ret = s_udp_session_start(a_stream, (uint32_t)l_udp_ctx->session_id, NULL);
            if (l_ret != 0) {
                log_it(L_ERROR, "CLIENT: Failed to create FC: ret=%d", l_ret);
                DAP_DELETE(l_decrypted);
                dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
                return -1;
            }
            
            DAP_DELETE(l_decrypted);
            dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
            return l_es->buf_in_size;
            
        } else {
            // DATA or other packet - buffer for FC processing
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT: Buffering packet type=%u (%zu bytes) for FC", l_type, l_es->buf_in_size);
            
            DAP_DELETE(l_decrypted);  // Don't need decrypted version - FC will decrypt again
            
            // Allocate/resize buffer arrays if needed
            if (!l_udp_ctx->buffered_packets) {
                l_udp_ctx->buffered_capacity = 16;  // Initial capacity
                l_udp_ctx->buffered_packets = DAP_NEW_Z_COUNT(uint8_t*, l_udp_ctx->buffered_capacity);
                l_udp_ctx->buffered_packet_sizes = DAP_NEW_Z_COUNT(size_t, l_udp_ctx->buffered_capacity);
                l_udp_ctx->buffered_count = 0;
            } else if (l_udp_ctx->buffered_count >= l_udp_ctx->buffered_capacity) {
                // Resize arrays
                size_t l_new_capacity = l_udp_ctx->buffered_capacity * 2;
                uint8_t **l_new_packets = DAP_NEW_Z_COUNT(uint8_t*, l_new_capacity);
                size_t *l_new_sizes = DAP_NEW_Z_COUNT(size_t, l_new_capacity);
                
                memcpy(l_new_packets, l_udp_ctx->buffered_packets, 
                       l_udp_ctx->buffered_count * sizeof(uint8_t*));
                memcpy(l_new_sizes, l_udp_ctx->buffered_packet_sizes,
                       l_udp_ctx->buffered_count * sizeof(size_t));
                
                DAP_DELETE(l_udp_ctx->buffered_packets);
                DAP_DELETE(l_udp_ctx->buffered_packet_sizes);
                
                l_udp_ctx->buffered_packets = l_new_packets;
                l_udp_ctx->buffered_packet_sizes = l_new_sizes;
                l_udp_ctx->buffered_capacity = l_new_capacity;
            }
            
            // Buffer ENCRYPTED packet (FC needs encrypted input)
            uint8_t *l_buffer = DAP_NEW_SIZE(uint8_t, l_es->buf_in_size);
            if (!l_buffer) {
                log_it(L_ERROR, "CLIENT: Failed to allocate buffer");
                dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
                return -1;
            }
            
            memcpy(l_buffer, l_es->buf_in, l_es->buf_in_size);
            l_udp_ctx->buffered_packets[l_udp_ctx->buffered_count] = l_buffer;
            l_udp_ctx->buffered_packet_sizes[l_udp_ctx->buffered_count] = l_es->buf_in_size;
            l_udp_ctx->buffered_count++;
            
            debug_if(s_debug_more, L_DEBUG, "CLIENT: Buffered packet #%zu (type=%u) - FC will process after creation",
                   l_udp_ctx->buffered_count, l_type);
            
            dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
            return l_es->buf_in_size;
        }
    } else {
        // FLOW CONTROL PATH: FC is ready - pass packet directly
        debug_if(s_debug_more, L_DEBUG,
                 "CLIENT: Passing packet to FC (%p), size=%zu", l_udp_ctx->flow_ctrl, l_es->buf_in_size);
        
        int l_ret = dap_io_flow_ctrl_recv(l_udp_ctx->flow_ctrl, l_es->buf_in, l_es->buf_in_size);
        
        if (l_ret != 0) {
            log_it(L_WARNING, "CLIENT: Flow Control packet processing failed: ret=%d", l_ret);
            dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
            return -1;
        }
        
        // Clear buffer - packet consumed by FC
        size_t l_consumed = l_es->buf_in_size;
        dap_events_socket_shrink_buf_in(l_es, l_consumed);
        
        return l_consumed;
    }
}

static ssize_t s_udp_write_typed(dap_stream_t *a_stream, uint8_t a_pkt_type,
                                  const void *a_data, size_t a_size)
{
    debug_if(s_debug_more, L_DEBUG, "s_udp_write_typed: type=0x%02x, size=%zu", 
             a_pkt_type, a_size);
    
    if (!a_stream || !a_data || a_size == 0) {
        log_it(L_CRITICAL, "UDP write: Invalid arguments: stream=%p, data=%p, size=%zu", 
               a_stream, a_data, a_size);
        return -1;
    }

    dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(a_stream);
    if (!l_ctx || !l_ctx->esocket) {
        log_it(L_ERROR, "UDP write: No trans ctx or esocket for write (ctx=%p, esocket=%p)", 
               l_ctx, l_ctx ? l_ctx->esocket : NULL);
        return -1;
    }

    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        log_it(L_ERROR, "UDP write: No UDP context for write");
        return -1;
    }
    
    // Log remote_addr for debugging
    if (l_udp_ctx->remote_addr.ss_family == AF_INET) {
        struct sockaddr_in *l_sin = (struct sockaddr_in*)&l_udp_ctx->remote_addr;
        char l_addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &l_sin->sin_addr, l_addr_str, sizeof(l_addr_str));
        log_it(L_DEBUG, "UDP write: remote_addr=%s:%u, addr_len=%u",
               l_addr_str, ntohs(l_sin->sin_port), l_udp_ctx->remote_addr_len);
    } else {
        log_it(L_WARNING, "UDP write: remote_addr has INVALID family: %d",
               l_udp_ctx->remote_addr.ss_family);
    }
    

    // HANDSHAKE packets are OBFUSCATED (size-based encryption)
    // NOTE: HANDSHAKE cannot use Flow Control because:
    // 1. It uses obfuscation format, not FC headers
    // 2. Server responds with obfuscated packet (not FC packet)
    // 3. FC expects symmetric protocol (both sides use seq/ack)
    // TODO: Implement separate handshake retransmission timer
    if (a_pkt_type == DAP_STREAM_UDP_PKT_HANDSHAKE) {
        // Validate size (must be Kyber public key)
        if (a_size != DAP_STREAM_UDP_HANDSHAKE_SIZE) {
            log_it(L_ERROR, "Invalid handshake payload size: %zu (expected %d)",
                   a_size, DAP_STREAM_UDP_HANDSHAKE_SIZE);
            return -1;
        }
        
        // Obfuscate handshake (encrypt with size-derived key, add random padding)
        uint8_t *l_obfuscated = NULL;
        size_t l_obfuscated_size = 0;
        
        int l_ret = dap_transport_obfuscate_handshake(a_data, a_size,
                                                      &l_obfuscated, &l_obfuscated_size);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to obfuscate HANDSHAKE packet");
            return -1;
        }
        
        // Send obfuscated handshake (CLIENT UDP)
        // CRITICAL: Use l_udp_ctx->remote_addr, NOT esocket->addr_storage!
        ssize_t l_sent = dap_events_socket_sendto_unsafe(l_ctx->esocket, 
                                                         l_obfuscated, l_obfuscated_size,
                                                         &l_udp_ctx->remote_addr,
                                                         l_udp_ctx->remote_addr_len);
        DAP_DELETE(l_obfuscated);
    
        if (l_sent < 0) {
            log_it(L_ERROR, "Failed to send obfuscated HANDSHAKE packet");
            return -1;
        }

        debug_if(s_debug_more, L_DEBUG,
                 "Obfuscated HANDSHAKE sent: %zu → %zu bytes",
                 a_size, l_obfuscated_size);
        return l_sent;
    }
    
    // ALL OTHER PACKETS: Send via Flow Control OR direct (fallback)
    
    debug_if(s_debug_more, L_DEBUG, "Checking encryption keys for packet type %u", a_pkt_type);
    
    if (!l_udp_ctx->handshake_key) {
        log_it(L_ERROR, "No encryption key for sending encrypted packet (type=%u)", a_pkt_type);
        return -1;
    }
    
    // FLOW CONTROL PATH (NEW): Send via FC
    if (l_udp_ctx->flow_ctrl) {
        debug_if(s_debug_more, L_DEBUG,
                 "CLIENT: sending packet via Flow Control: type=%u, size=%zu", a_pkt_type, a_size);
        
        // Store packet type for prepare callback
        l_udp_ctx->last_send_type = a_pkt_type;
        
        // Send PURE PAYLOAD via Flow Control
        // FC will call s_client_flow_ctrl_packet_prepare_cb to add headers + encrypt
        int l_ret = dap_io_flow_ctrl_send(l_udp_ctx->flow_ctrl, a_data, a_size);
        if (l_ret != 0) {
            debug_if(s_debug_more, L_DEBUG,
                     "CLIENT: Flow Control send failed: ret=%d, type=%u%s",
                     l_ret, a_pkt_type, l_ret == -10 ? " (FC deleting)" : "");
            return -1;
        }
        
        debug_if(s_debug_more, L_DEBUG,
                 "CLIENT: Flow Control sent %zu bytes (type=%u)", a_size, a_pkt_type);
        
        return a_size;
    }
    
    // FALLBACK PATH (NO FLOW CONTROL): Direct encryption + send
    // This is for backward compatibility or if FC is disabled
    
    debug_if(s_debug_more, L_DEBUG,
             "CLIENT: Flow Control DISABLED - using direct send");
    
    // Build full UDP header (FC fields + UDP fields)
    // NOTE: Client doesn't use Flow Control in fallback mode, so FC fields are zeroed
    dap_stream_trans_udp_full_header_t l_full_hdr = {
        .seq_num = l_udp_ctx->seq_num++,
        .ack_seq = 0,  // No FC in fallback
        .timestamp_ms = (uint32_t)(dap_nanotime_now() / 1000000),
        .fc_flags = 0,  // No FC flags
        .type = a_pkt_type,
        .session_id = l_udp_ctx->session_id,
    };
    
    // Serialize using dap_serialize
    size_t l_hdr_size = sizeof(dap_stream_trans_udp_full_header_t);
    uint8_t l_hdr_buffer[sizeof(dap_stream_trans_udp_full_header_t)];
    
    dap_serialize_result_t l_ser_result = dap_serialize_to_buffer_raw(
        &g_udp_full_header_schema,
        &l_full_hdr,
        l_hdr_buffer,
        l_hdr_size,
        NULL
    );
    
    if (l_ser_result.error_code != 0) {
        log_it(L_ERROR, "Failed to serialize full header: %s",
               l_ser_result.error_message ? l_ser_result.error_message : "unknown");
        return -1;
    }
    
    // Build cleartext packet: [serialized_header + payload]
    size_t l_cleartext_size = l_hdr_size + a_size;
    uint8_t *l_cleartext = DAP_NEW_SIZE(uint8_t, l_cleartext_size);
    if (!l_cleartext) {
        log_it(L_ERROR, "Failed to allocate cleartext buffer");
        return -1;
    }
    
    memcpy(l_cleartext, l_hdr_buffer, l_hdr_size);
    if (a_size > 0) {
        memcpy(l_cleartext + l_hdr_size, a_data, a_size);
    }
    
    // Encrypt entire packet
    size_t l_encrypted_max = l_cleartext_size + 256;  // Extra space for encryption overhead
    uint8_t *l_encrypted = DAP_NEW_SIZE(uint8_t, l_encrypted_max);
    if (!l_encrypted) {
        log_it(L_ERROR, "Failed to allocate encryption buffer");
        DAP_DELETE(l_cleartext);
        return -1;
    }
    
    // Use handshake_key for SESSION_CREATE, session key for DATA/KEEPALIVE/CLOSE
    dap_enc_key_t *l_enc_key = l_udp_ctx->handshake_key;
    if (a_pkt_type == DAP_STREAM_UDP_PKT_DATA || 
        a_pkt_type == DAP_STREAM_UDP_PKT_KEEPALIVE || 
        a_pkt_type == DAP_STREAM_UDP_PKT_CLOSE) {
        // These packet types require session key
        if (a_stream->session && a_stream->session->key) {
            l_enc_key = a_stream->session->key;
        } else {
            log_it(L_ERROR, "No session key for DATA/KEEPALIVE/CLOSE packet (stream=%p, session=%p, session->key=%p)",
                   a_stream, a_stream->session, a_stream->session ? a_stream->session->key : NULL);
            DAP_DELETE(l_cleartext);
            DAP_DELETE(l_encrypted);
            return -1;
        }
    }
    
    debug_if(s_debug_more, L_DEBUG, "Encrypting packet: key=%p, cleartext=%zu bytes",
             l_enc_key, l_cleartext_size);
    
    size_t l_encrypted_size = dap_enc_code(l_enc_key,
                                           l_cleartext, l_cleartext_size,
                                           l_encrypted, l_encrypted_max,
                                           DAP_ENC_DATA_TYPE_RAW);
    
    debug_if(s_debug_more, L_DEBUG, "Encrypted: %zu bytes (expected ~%zu)",
             l_encrypted_size, l_cleartext_size + 8);
    
    log_it(L_WARNING, "UDP write: cleartext_size=%zu, encrypted_size=%zu", 
           l_cleartext_size, l_encrypted_size);
    
    DAP_DELETE(l_cleartext);
    
    if (l_encrypted_size == 0) {
        log_it(L_ERROR, "Failed to encrypt packet (type=%u)", a_pkt_type);
        DAP_DELETE(l_encrypted);
        return -1;
    }
    
    // Send encrypted blob (no headers, no magic, just encrypted data) - CLIENT UDP
    // CRITICAL: Use l_udp_ctx->remote_addr, NOT esocket->addr_storage!
    ssize_t l_sent = dap_events_socket_sendto_unsafe(l_ctx->esocket, l_encrypted, l_encrypted_size,
                                                     &l_udp_ctx->remote_addr,
                                                     l_udp_ctx->remote_addr_len);
    
    DAP_DELETE(l_encrypted);
    
    if (l_sent < 0) {
        log_it(L_ERROR, "Failed to send encrypted packet (type=%u)", a_pkt_type);
        return -1;
    }

    debug_if(s_debug_more, L_DEBUG,
             "Encrypted packet sent: type=%u, session=0x%" DAP_UINT64_FORMAT_x ", encrypted_size=%zu",
             a_pkt_type, l_udp_ctx->session_id, l_encrypted_size);
    
    return l_sent;
}

static ssize_t s_udp_write(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    debug_if(s_debug_more, L_DEBUG, "s_udp_write: stream=%p, size=%zu", a_stream, a_size);
    
    // All DATA packets go through here (from dap_stream_pkt_write_unsafe)
    ssize_t result = s_udp_write_typed(a_stream, DAP_STREAM_UDP_PKT_DATA, a_data, a_size);
    
    debug_if(s_debug_more, L_DEBUG, "s_udp_write: returning %zd", result);
    return result;
}

/**
 * @brief Close UDP trans
 * 
 * CRITICAL: This function is responsible for esocket cleanup!
 * It runs in the esocket's worker context, so unsafe access is safe here.
 * We must extract and delete esocket BEFORE dap_stream_delete_unsafe tries to access it.
 */
static void s_udp_close(dap_stream_t *a_stream)
{
    if (!a_stream) {
        log_it(L_ERROR, "Invalid stream for close");
        return;
    }

    if (!a_stream->trans_ctx) {
        return;
    }

    // Get UDP per-stream context
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
    if (l_udp_ctx) {
        log_it(L_INFO, "Closing UDP trans session 0x%" DAP_UINT64_FORMAT_x "", l_udp_ctx->session_id);
        
        // Clean up Flow Control if present
        if (l_udp_ctx->flow_ctrl) {
            debug_if(s_debug_more, L_DEBUG, "Deleting client-side Flow Control");
            dap_io_flow_ctrl_delete(l_udp_ctx->flow_ctrl);
            l_udp_ctx->flow_ctrl = NULL;
        }
        
        // Clean up base flow if present
        if (l_udp_ctx->base) {
            debug_if(s_debug_more, L_DEBUG, "Freeing base flow structure");
            DAP_DELETE(l_udp_ctx->base);
            l_udp_ctx->base = NULL;
        }
        
        // Clean up buffered packets if present
        if (l_udp_ctx->buffered_packets) {
            debug_if(s_debug_more, L_DEBUG, "Freeing %zu buffered packets", l_udp_ctx->buffered_count);
            for (size_t i = 0; i < l_udp_ctx->buffered_count; i++) {
                if (l_udp_ctx->buffered_packets[i]) {
                    DAP_DELETE(l_udp_ctx->buffered_packets[i]);
                }
            }
            DAP_DELETE(l_udp_ctx->buffered_packets);
            DAP_DELETE(l_udp_ctx->buffered_packet_sizes);
            l_udp_ctx->buffered_packets = NULL;
            l_udp_ctx->buffered_packet_sizes = NULL;
            l_udp_ctx->buffered_count = 0;
            l_udp_ctx->buffered_capacity = 0;
        }
        
        // Cancel handshake retransmission timer if present
        s_cancel_handshake_timer(l_udp_ctx);
        
        // Clean up alice_key if present
        if (l_udp_ctx->alice_key) {
            dap_enc_key_delete(l_udp_ctx->alice_key);
            l_udp_ctx->alice_key = NULL;
        }
        
        // Clean up handshake_key if present
        // CRITICAL: Check if handshake_key points to session key to avoid double-free!
        // After SESSION_CREATE, handshake_key and stream->session->key point to same object
        bool l_is_session_key = (a_stream->session && a_stream->session->key == l_udp_ctx->handshake_key);
        
        if (l_udp_ctx->handshake_key && !l_is_session_key) {
            dap_enc_key_delete(l_udp_ctx->handshake_key);
            l_udp_ctx->handshake_key = NULL;
        } else if (l_is_session_key) {
            // Just clear pointer, key will be deleted with session
            l_udp_ctx->handshake_key = NULL;
        }
    
        // Clear stream pointer to prevent use-after-free
        l_udp_ctx->stream = NULL;
        l_udp_ctx->session_id = 0;
        l_udp_ctx->seq_num = 0;
    }
    
    // CORRECT ARCHITECTURE - 100% THREAD SAFE:
    //
    // ALWAYS use _mt method for esocket deletion - works from ANY thread context
    // No need to check worker - _mt handles it correctly
    
    dap_net_trans_ctx_t *l_ctx = (dap_net_trans_ctx_t*)a_stream->trans_ctx;
    if (l_ctx && l_ctx->esocket_uuid && l_ctx->esocket_worker) {
        debug_if(s_debug_more, L_DEBUG, 
               "UDP close: queueing esocket deletion (UUID 0x%016" DAP_UINT64_FORMAT_x ") on its worker",
               l_ctx->esocket_uuid);
        
        // CRITICAL: Clear callbacks BEFORE async delete to prevent use-after-free!
        // Esocket may still receive events between now and actual deletion
        // Setting callbacks to NULL prevents them from accessing freed trans_ctx/stream
        if (l_ctx->esocket) {
            l_ctx->esocket->callbacks.read_callback = NULL;
            l_ctx->esocket->callbacks.write_callback = NULL;
            l_ctx->esocket->callbacks.error_callback = NULL;
            l_ctx->esocket->callbacks.arg = NULL;  // Critical: prevents use-after-free in callbacks
        }
        
        // ALWAYS use _mt method - 100% safe from any thread
        dap_events_socket_remove_and_delete_mt(l_ctx->esocket_worker, l_ctx->esocket_uuid);
        
        // Clear pointers (esocket will be deleted asynchronously on its worker)
        l_ctx->esocket = NULL;
        l_ctx->esocket_uuid = 0;
        l_ctx->esocket_worker = NULL;
    }
    
    if (l_ctx) {
        // Clear stream pointer in trans_ctx to prevent use-after-free
        l_ctx->stream = NULL;
        
        // Free UDP context (owned by trans_ctx, not esocket)
        if (l_ctx->_inheritor) {
            DAP_DELETE(l_ctx->_inheritor);
            l_ctx->_inheritor = NULL;
        }
    }
}

/**
 * @brief Prepare UDP socket for client stage
 * 
 * Fully prepares esocket: creates, sets callbacks, and adds to worker.
 * UDP is connectionless, so no connection step is needed.
 * Trans is responsible for complete esocket lifecycle management.
 */
static int s_udp_stage_prepare(dap_net_trans_t *a_trans,
                               const dap_net_stage_prepare_params_t *a_params,
                               dap_net_stage_prepare_result_t *a_result)
{
    if (!a_trans || !a_params || !a_result) {
        log_it(L_ERROR, "Invalid arguments for UDP stage_prepare");
        return -1;
    }
    
    if (!a_params->worker) {
        log_it(L_ERROR, "Worker is required for UDP stage_prepare");
        a_result->error_code = -1;
        return -1;
    }
    
    // Initialize result
    a_result->esocket = NULL;
    a_result->stream = NULL;
    a_result->error_code = 0;
    
    // Create or reuse UDP socket
    dap_stream_trans_udp_private_t *l_priv = s_get_private(a_trans);
    if (!l_priv) {
        log_it(L_ERROR, "UDP trans not initialized");
        a_result->error_code = -1;
        return -1;
    }
    
    dap_events_socket_t *l_es = dap_events_socket_create_platform(PF_INET, SOCK_DGRAM, IPPROTO_UDP, a_params->callbacks);
    if (!l_es) {
        log_it(L_ERROR, "Failed to create UDP socket");
        a_result->error_code = -1;
        return -1;
    }
    l_es->type = DESCRIPTOR_TYPE_SOCKET_UDP;
    l_es->is_initalized = true;  // CRITICAL: Mark as initialized for write operations!
    
    // Set UDP read callback for client esocket
    // NO write_callback needed - reactor handles packet_queue automatically!
    l_es->callbacks.read_callback = dap_stream_trans_udp_read_callback;
    
    log_it(L_DEBUG, "Created UDP socket %p", l_es);
    
    // Resolve host and set address using centralized function
    if (dap_events_socket_resolve_and_set_addr(l_es, a_params->host, a_params->port) < 0) {
        log_it(L_ERROR, "Failed to resolve address for UDP trans: %s:%u", a_params->host, a_params->port);
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }

    log_it(L_DEBUG, "Resolved UDP address: family=%d, size=%zu", l_es->addr_storage.ss_family, (size_t)l_es->addr_size);

    // NOTE: DO NOT connect() for Flow Control scenarios!
    // Flow Control needs bidirectional communication with server's listener port.
    // connect() would filter packets, breaking ACK reception.
    // We use sendto() with addr_storage for all sends.
    
    // CRITICAL: Bind UDP socket to get a local port BEFORE first send!
    // Without bind(), OS may not assign a port, breaking server responses!
    // MUST bind BEFORE dap_worker_add_events_socket so reactor monitors correct socket state!
    struct sockaddr_in l_bind_addr;
    memset(&l_bind_addr, 0, sizeof(l_bind_addr));
    l_bind_addr.sin_family = AF_INET;
    l_bind_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to all interfaces
    l_bind_addr.sin_port = 0;  // Let OS choose free port
    
    if (bind(l_es->socket, (struct sockaddr *)&l_bind_addr, sizeof(l_bind_addr)) < 0) {
        log_it(L_ERROR, "Failed to bind UDP socket: %s", strerror(errno));
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    // Set LARGE socket buffers (64 MB) to prevent packet loss under high load with many clients
    int l_buffer_size = 64 * 1024 * 1024;  // 64 MB
    if (setsockopt(l_es->socket, SOL_SOCKET, SO_RCVBUF, &l_buffer_size, sizeof(l_buffer_size)) < 0) {
        log_it(L_WARNING, "Failed to set SO_RCVBUF to %d bytes: %s", l_buffer_size, strerror(errno));
    } else {
        log_it(L_INFO, "Set SO_RCVBUF to %d bytes (64 MB) for UDP client socket", l_buffer_size);
    }
    
    if (setsockopt(l_es->socket, SOL_SOCKET, SO_SNDBUF, &l_buffer_size, sizeof(l_buffer_size)) < 0) {
        log_it(L_WARNING, "Failed to set SO_SNDBUF to %d bytes: %s", l_buffer_size, strerror(errno));
    } else {
        log_it(L_INFO, "Set SO_SNDBUF to %d bytes (64 MB) for UDP client socket", l_buffer_size);
    }
    
    // Get local port assigned by OS
    struct sockaddr_in l_local_addr;
    socklen_t l_local_addr_len = sizeof(l_local_addr);
    if (getsockname(l_es->socket, (struct sockaddr *)&l_local_addr, &l_local_addr_len) == 0) {
        log_it(L_INFO, "UDP socket fd=%d bound to local port %u", l_es->socket, ntohs(l_local_addr.sin_port));
    }
    
    // CRITICAL: Add socket to worker AFTER bind() so reactor monitors properly configured socket!
    // UDP is connectionless - add to worker after bind
    dap_worker_add_events_socket(a_params->worker, l_es);
    
    // CRITICAL: Set readable+writable state for UDP - reactor needs it to handle I/O!
    // Without this, UDP packets won't be received/sent!
    dap_events_socket_set_readable_unsafe(l_es, true);
    dap_events_socket_set_writable_unsafe(l_es, true);
    
    // For UDP: create stream early and return it (stream will be reused for all operations)
    dap_stream_t *l_stream = dap_stream_new_es_client(l_es, (dap_cluster_node_addr_t *)a_params->node_addr, a_params->authorized);
    if (!l_stream) {
        log_it(L_CRITICAL, "Failed to create stream for UDP trans");
        dap_events_socket_delete_unsafe(l_es, true);
        a_result->error_code = -1;
        return -1;
    }
    
    l_stream->trans = a_trans;
    
    // Initialize trans_ctx and link esocket
    dap_net_trans_ctx_t *l_ctx = s_udp_get_or_create_ctx(l_stream);
    if (!l_ctx) {
        log_it(L_CRITICAL, "Failed to create trans_ctx for UDP stream");
        dap_events_socket_delete_unsafe(l_es, true);
        DAP_DELETE(l_stream);
        a_result->error_code = -1;
        return -1;
    }
    // Set esocket reference with UUID and worker for thread-safe access
    l_ctx->esocket = l_es;
    l_ctx->esocket_uuid = l_es->uuid;
    l_ctx->esocket_worker = l_es->worker;
    l_ctx->stream = l_stream;
    
    // CRITICAL: Set callbacks.arg so read_callback can retrieve trans_ctx!
    l_es->callbacks.arg = l_ctx;
    log_it(L_INFO, "UDP CLIENT CREATED: esocket=%p (fd=%d), stream=%p, trans_ctx=%p", 
           l_es, l_es->socket, l_stream, l_ctx);
    
    // Create UDP per-stream context and store client_ctx
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_or_create_udp_ctx(l_stream);
    if (!l_udp_ctx) {
        log_it(L_CRITICAL, "Failed to create UDP context for stream");
        dap_events_socket_delete_unsafe(l_es, true);
        DAP_DELETE(l_stream);
        a_result->error_code = -1;
        return -1;
    }
    
    // Store client context for later retrieval in get_client_context
    l_udp_ctx->client_ctx = a_params->client_ctx;
    l_udp_ctx->stream = l_stream;
    
    // CRITICAL: Copy remote_addr from esocket to UDP context!
    // esocket->addr_storage will be overwritten by recvfrom(), so we need a stable copy.
    memcpy(&l_udp_ctx->remote_addr, &l_es->addr_storage, sizeof(struct sockaddr_storage));
    l_udp_ctx->remote_addr_len = l_es->addr_size;
    
    // Log remote_addr for debugging
    if (l_udp_ctx->remote_addr.ss_family == AF_INET) {
        struct sockaddr_in *l_sin = (struct sockaddr_in*)&l_udp_ctx->remote_addr;
        char l_addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &l_sin->sin_addr, l_addr_str, sizeof(l_addr_str));
        log_it(L_INFO, "UDP CLIENT: initialized remote_addr=%s:%u",
               l_addr_str, ntohs(l_sin->sin_port));
    }
    
    // CRITICAL: Create CLIENT datagram flow for address resolution callback
    // This allows stream->flow to work uniformly for CLIENT and SERVER
    l_stream->flow = dap_io_flow_datagram_new(s_get_remote_addr_cb, l_stream);
    if (!l_stream->flow) {
        log_it(L_CRITICAL, "Failed to allocate CLIENT datagram flow");
        dap_events_socket_delete_unsafe(l_es, true);
        DAP_DELETE(l_stream);
        a_result->error_code = -1;
        return -1;
    }
    
    debug_if(s_debug_more, L_INFO, "UDP CLIENT: created datagram flow %p for stream %p", l_stream->flow, l_stream);
    
    // CRITICAL: Store trans_ctx in callbacks.arg (NOT _inheritor!)
    // _inheritor is for client infrastructure ownership, we use callbacks.arg for trans_ctx
    l_es->callbacks.arg = l_ctx;
    
    log_it(L_DEBUG, "UDP trans created stream %p with trans_ctx %p (stored in callbacks.arg)", l_stream, l_ctx);
    
    a_result->esocket = l_es;
    a_result->stream = l_stream;
    a_result->error_code = 0;
    log_it(L_DEBUG, "UDP socket and stream prepared for %s:%u", a_params->host, a_params->port);
    return 0;
}

/**
 * @brief Get trans capabilities
 */
static uint32_t s_udp_get_capabilities(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return DAP_NET_TRANS_CAP_LOW_LATENCY |
           DAP_NET_TRANS_CAP_BIDIRECTIONAL;
}

/**
 * @brief Get client context from UDP stream's trans_ctx
 * @param a_stream Stream to extract client context from
 * @return Client context (dap_client_t*) or NULL
 * @note For UDP trans, trans_ctx->_inheritor contains dap_udp_client_esocket_ctx_t wrapper
 */
static void* s_udp_get_client_context(dap_stream_t *a_stream)
{
    if (!a_stream) {
        return NULL;
    }
    
    // For UDP, trans_ctx->_inheritor is dap_net_trans_udp_ctx_t
    dap_net_trans_udp_ctx_t *l_udp_ctx = s_get_udp_ctx(a_stream);
    if (!l_udp_ctx) {
        return NULL;
    }
    
    return l_udp_ctx->client_ctx;  // Return dap_client_t*
}

//=============================================================================
// Helper functions
//=============================================================================

/**
 * @brief Get private data from trans
 */
static dap_stream_trans_udp_private_t *s_get_private(dap_net_trans_t *a_trans)
{
    if (!a_trans)
        return NULL;
    return (dap_stream_trans_udp_private_t*)a_trans->_inheritor;
}

/**
 * @brief Get UDP per-stream context
 */
static dap_net_trans_udp_ctx_t *s_get_udp_ctx(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans_ctx)
        return NULL;
    return (dap_net_trans_udp_ctx_t*)a_stream->trans_ctx->_inheritor;
}

/**
 * @brief Get or create UDP per-stream context
 */
/**
 * @brief Get or create UDP per-stream context
 * Made non-static for server.c to initialize UDP context for server-side streams
 */
dap_net_trans_udp_ctx_t *s_get_or_create_udp_ctx(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans_ctx)
        return NULL;
    
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t*)a_stream->trans_ctx;
    
    if (!l_trans_ctx->_inheritor) {
        // Create new UDP context
        dap_net_trans_udp_ctx_t *l_udp_ctx = DAP_NEW_Z(dap_net_trans_udp_ctx_t);
        if (!l_udp_ctx) {
            log_it(L_CRITICAL, "Failed to allocate UDP stream context");
            return NULL;
    }
        l_trans_ctx->_inheritor = l_udp_ctx;
        debug_if(s_debug_more, L_DEBUG, "Created UDP context %p for stream %p", l_udp_ctx, a_stream);
    }
    
    return (dap_net_trans_udp_ctx_t*)l_trans_ctx->_inheritor;
}

/**
 * @brief Get maximum UDP packet size for fragmentation
 * @param a_trans Trans instance
 * @return Maximum safe UDP payload size in bytes
 * @note Returns conservative MTU (1200 bytes) to account for:
 *       - Standard IPv4 MTU: 1500 bytes
 *       - IPv4 header: 20 bytes
 *       - UDP header: 8 bytes
 *       - Internal UDP stream headers: ~50 bytes
 *       - Encryption overhead (SALSA2012): ~20 bytes
 *       - Safety margin for network overhead
 */
static size_t s_udp_get_max_packet_size(dap_net_trans_t *a_trans)
{
    (void)a_trans;
    // Conservative UDP payload size to avoid fragmentation
    // Actual UDP packet will be larger due to headers + encryption
    return DAP_STREAM_UDP_MAX_PAYLOAD_SIZE;  // 1200 bytes
}

//===================================================================
// UDP STREAM SERVER CALLBACKS (moved from dap_stream.c)
//===================================================================

static void s_udp_esocket_new(dap_events_socket_t *a_esocket, void *a_arg);

/**
 * @brief Create new stream instance for UDP client
 * @param a_esocket Event socket for the UDP connection
 * @return New stream instance or NULL
 */
dap_stream_t *dap_net_trans_udp_stream_new(dap_events_socket_t *a_esocket)
{
    dap_stream_t *l_stm = DAP_NEW_Z(dap_stream_t);
    assert(l_stm);
    if (!l_stm) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    atomic_fetch_add(&dap_stream_created_count, 1);

    l_stm->trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
    if (l_stm->trans_ctx) {
        l_stm->trans_ctx->esocket = a_esocket;
        l_stm->trans_ctx->esocket_uuid = a_esocket->uuid;
        l_stm->trans_ctx->esocket_worker = a_esocket->worker;
        l_stm->trans_ctx->stream = l_stm;
        dap_strncpy(l_stm->trans_ctx->remote_addr_str, a_esocket->remote_addr_str,
                    sizeof(l_stm->trans_ctx->remote_addr_str) - 1);
        l_stm->trans_ctx->remote_port = a_esocket->remote_port;
    }

    a_esocket->_inheritor = l_stm->trans_ctx;
    dap_stream_add_to_list(l_stm);
    log_it(L_NOTICE, "New stream instance udp");
    return l_stm;
}

/**
 * @brief Check session status, open if needed (UDP SERVICE_PACKET handler)
 * @param a_id Session ID
 * @param a_esocket Stream event socket
 */
static void s_udp_check_session(unsigned int a_id, dap_events_socket_t *a_esocket)
{
    dap_stream_session_t *l_session = dap_stream_session_id_mt(a_id);

    if (l_session == NULL) {
        log_it(L_ERROR, "No session id %u was found", a_id);
        return;
    }

    log_it(L_INFO, "Session id %u was found with media_id = %d", a_id, l_session->media_id);

    if (dap_stream_session_open(l_session) != 0) {
        log_it(L_ERROR, "Can't open session id %u", a_id);
        return;
    }

    dap_stream_t *l_stream;
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_esocket->_inheritor;
    if (!l_trans_ctx || !l_trans_ctx->stream)
        l_stream = dap_net_trans_udp_stream_new(a_esocket);
    else
        l_stream = l_trans_ctx->stream;

    l_stream->session = l_session;

    if (l_session->create_empty)
        log_it(L_INFO, "Session created empty");

    log_it(L_INFO, "Opened stream session technical and data channels");

    for (size_t i = 0; i < sizeof(l_session->active_channels); i++)
        if (l_session->active_channels[i])
            dap_stream_ch_new(l_stream, l_session->active_channels[i]);

    dap_stream_states_update(l_stream);

#ifdef DAP_EVENTS_CAPS_IOCP
    a_esocket->flags |= DAP_SOCK_READY_TO_READ;
#else
    dap_events_socket_set_readable_unsafe(a_esocket, true);
#endif
}

/**
 * @brief New connection callback for UDP client
 */
static void s_udp_esocket_new(dap_events_socket_t *a_esocket, UNUSED_ARG void *a_arg)
{
    dap_net_trans_udp_stream_new(a_esocket);
}

/**
 * @brief Add processor callbacks for UDP streaming
 * @param a_udp_server UDP server instance
 */
void dap_net_trans_udp_stream_add_proc(dap_server_t *a_udp_server)
{
    a_udp_server->client_callbacks.read_callback = dap_stream_esocket_read_cb;
    a_udp_server->client_callbacks.write_callback = dap_stream_esocket_write_cb;
    a_udp_server->client_callbacks.delete_callback = dap_stream_esocket_delete_cb;
    a_udp_server->client_callbacks.new_callback = s_udp_esocket_new;
    a_udp_server->client_callbacks.worker_assign_callback = dap_stream_esocket_worker_assign_cb;
    a_udp_server->client_callbacks.worker_unassign_callback = dap_stream_esocket_worker_unassign_cb;
}

/**
 * @brief Check session for UDP stream (called from dap_stream on SERVICE_PACKET)
 * @param a_id Session ID
 * @param a_esocket Event socket
 */
void dap_net_trans_udp_stream_check_session(unsigned int a_id, dap_events_socket_t *a_esocket)
{
    s_udp_check_session(a_id, a_esocket);
}

static void s_udp_check_session_op(dap_net_trans_t *a_trans, uint32_t a_session_id,
                                   dap_events_socket_t *a_esocket)
{
    UNUSED(a_trans);
    s_udp_check_session(a_session_id, a_esocket);
}

/**
 * @brief Create UDP header
 */
