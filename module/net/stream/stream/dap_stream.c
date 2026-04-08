/*
 Copyright (c) 2017-2018 (c) Project "DeM Labs Inc" https://github.com/demlabsinc
  All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#include <pthread.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include "dap_common.h"
#include "dap_stream.h"
#include "dap_stream_pkt.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_session.h"
#include "dap_strfuncs.h"
#include "dap_ht.h"
#include "dap_dl.h"
#include "dap_enc.h"
#include "dap_enc_ks.h"
#include "dap_net_trans.h"
#include "dap_net_trans_ctx.h"

#include "dap_stream_esocket_ops.h"
#include "dap_timerfd.h"
#include "dap_context.h"
#include "dap_events_socket.h"
#include "dap_io_flow_datagram.h"
#include "dap_stream_worker.h"

#define LOG_TAG "dap_stream"

#define DAP_STREAM_CLOSE_TIMEOUT_MS 0

static dap_stream_member_callback_t s_member_add_callback = NULL;
static dap_stream_member_callback_t s_member_del_callback = NULL;
static dap_stream_from_esocket_callback_t s_client_esocket_callback = NULL;
static dap_stream_t         *s_streams = NULL;
static dap_enc_key_type_t   s_stream_get_preferred_encryption_type = DAP_ENC_KEY_TYPE_IAES;
static bool s_dump_packet_headers = false;
static bool s_debug = false;

_Atomic uint64_t dap_stream_created_count = 0;
static _Atomic uint64_t s_streams_destroyed = 0;

static void s_stream_proc_pkt_in(dap_stream_t *a_stream, dap_stream_pkt_t *l_pkt);
void dap_stream_delete_from_list(dap_stream_t *a_stream);

bool dap_stream_get_dump_packet_headers(){ return s_dump_packet_headers; }
bool dap_stream_get_debug(){ return s_debug; }

static bool s_detect_loose_packet(dap_stream_t *a_stream);

typedef struct authorized_stream {
    union {
        unsigned long num;
        void *pointer;
    } id;
    dap_cluster_node_addr_t node;
    dap_ht_handle_t hh;
} authorized_stream_t;

static pthread_rwlock_t     s_streams_lock = PTHREAD_RWLOCK_INITIALIZER;
static dap_stream_t         *s_authorized_streams = NULL;

static int s_add_stream_info(authorized_stream_t **a_hash_table, authorized_stream_t *a_item, dap_stream_t *a_stream);

bool dap_stream_callback_server_keepalive(void *a_arg);
bool dap_stream_callback_client_keepalive(void *a_arg);

static int s_stream_add_stream_info(dap_stream_t *a_stream, uint64_t a_id);

/**
 * @brief Write data via transport layer (wrapper for trans->ops->write)
 * 
 * Calls transport-specific write callback. For UDP this goes through Flow Control.
 * FAIL-FAST: Returns error if trans->ops->write is not set (no fallback).
 * 
 * @param a_stream Stream instance
 * @param a_data Data to write
 * @param a_size Data size
 * @return Number of bytes written, or 0 on error
 */
ssize_t dap_stream_trans_write_unsafe(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0)
        return 0;

    dap_net_trans_t *l_trans = NULL;
    if (a_stream->trans_ctx && a_stream->trans_ctx->trans)
        l_trans = a_stream->trans_ctx->trans;
    else if (a_stream->trans)
        l_trans = a_stream->trans;

    if (!l_trans || !l_trans->ops || !l_trans->ops->write) {
        log_it(L_CRITICAL, "Stream trans has no write callback - ARCHITECTURE BUG!");
        return 0;
    }

    debug_if(s_debug, L_DEBUG,
             "dap_stream_trans_write_unsafe: writing %zu bytes via trans->ops->write", a_size);

    ssize_t l_ret = l_trans->ops->write(a_stream, a_data, a_size);
    if (l_ret < 0) {
        log_it(L_WARNING, "trans->ops->write failed: ret=%zd", l_ret);
        return 0;
    }
    return l_ret;
}

static inline ssize_t s_stream_send_datagram_unsafe(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0)
        return 0;
    if (!a_stream->flow) {
        log_it(L_ERROR, "Datagram stream %p has no flow set", a_stream);
        return 0;
    }
    return dap_stream_trans_write_unsafe(a_stream, a_data, a_size);
}

ssize_t dap_stream_send_unsafe(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0)
        return 0;
    if (!a_stream->trans_ctx || !a_stream->trans_ctx->esocket) {
        log_it(L_ERROR, "Stream has no trans_ctx or esocket");
        return 0;
    }
    dap_events_socket_t *l_es = a_stream->trans_ctx->esocket;
    if (dap_events_socket_is_datagram(l_es)) {
        debug_if(s_debug, L_DEBUG, "dap_stream_send_unsafe: stream %p using datagram path, flow=%p", a_stream, a_stream->flow);
        return s_stream_send_datagram_unsafe(a_stream, a_data, a_size);
    }
    return dap_events_socket_write_unsafe(l_es, a_data, a_size);
}


dap_enc_key_type_t dap_stream_get_preferred_encryption_type()
{
    return s_stream_get_preferred_encryption_type;
}

static void s_stream_load_preferred_encryption_type(dap_config_t *a_config)
{
    if (!a_config)
        return;
    const char *l_preferred_encryption_name = dap_config_get_item_str(a_config, "stream", "preferred_encryption");
    if (l_preferred_encryption_name) {
        dap_enc_key_type_t l_found_key_type = dap_enc_key_type_find_by_name(l_preferred_encryption_name);
        if (l_found_key_type != DAP_ENC_KEY_TYPE_INVALID)
            s_stream_get_preferred_encryption_type = l_found_key_type;
    }
    log_it(L_NOTICE, "Encryption type is set to %s", dap_enc_get_type_name(s_stream_get_preferred_encryption_type));
}

static int s_stream_init_node_addr_cert(void)
{
    dap_cert_t *l_addr_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
    if (!l_addr_cert) {
        const char *l_cert_folder = dap_cert_get_folder(DAP_CERT_FOLDER_PATH_DEFAULT);
        if (l_cert_folder) {
            char *l_cert_path = dap_strdup_printf("%s/" DAP_STREAM_NODE_ADDR_CERT_NAME ".dcert", l_cert_folder);
            l_addr_cert = dap_cert_generate(DAP_STREAM_NODE_ADDR_CERT_NAME, l_cert_path, DAP_STREAM_NODE_ADDR_CERT_TYPE);
            DAP_DELETE(l_cert_path);
        } else
            return -1;
    }
    g_node_addr = dap_cluster_node_addr_from_cert(l_addr_cert);
    return 0;
}

int dap_stream_init(dap_config_t *a_config)
{
    if (dap_stream_ch_init() != 0) {
        log_it(L_CRITICAL, "Can't init channel types submodule");
        return -1;
    }
    if (dap_stream_worker_init() != 0) {
        log_it(L_CRITICAL, "Can't init stream worker extention submodule");
        return -2;
    }

    if (s_stream_init_node_addr_cert()) {
        log_it(L_ERROR, "Can't initialize certificate containing secure node address");
        return -3;
    }

    s_stream_load_preferred_encryption_type(a_config);
    s_dump_packet_headers = dap_config_get_item_bool_default(a_config, "stream", "debug_dump_stream_headers", false);
    s_debug = dap_config_get_item_bool_default(a_config, "stream", "debug_more", false);

    log_it(L_NOTICE, "Init streaming module");
    return 0;
}

/**
 * @brief stream_media_deinit Deinint Stream module
 */
void dap_stream_deinit()
{
    // Transport layer is deinitialized automatically via dap_module system
    // No need to call dap_net_transport_deinit() manually
    
    dap_stream_ch_deinit( );
}

void dap_stream_set_member_callbacks(dap_stream_member_callback_t a_add,
                                     dap_stream_member_callback_t a_del)
{
    s_member_add_callback = a_add;
    s_member_del_callback = a_del;
}


/**
 * @brief dap_stream_new_es
 * @param a_es
 * @return
 */
dap_stream_t *dap_stream_new_es_client(dap_events_socket_t *a_esocket, dap_cluster_node_addr_t *a_addr, bool a_authorized)
{
    dap_stream_t *l_ret = DAP_NEW_Z(dap_stream_t);
    if (!l_ret) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    l_ret->trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
    if (l_ret->trans_ctx) {
        l_ret->trans_ctx->esocket = a_esocket;
        l_ret->trans_ctx->esocket_uuid = a_esocket->uuid;
        l_ret->trans_ctx->esocket_worker = a_esocket->worker;
        l_ret->trans_ctx->stream = l_ret;  // Back-reference
        // Cache remote address for cross-thread access (safe snapshot)
        dap_strncpy(l_ret->trans_ctx->remote_addr_str, a_esocket->remote_addr_str, sizeof(l_ret->trans_ctx->remote_addr_str) - 1);
        l_ret->trans_ctx->remote_port = a_esocket->remote_port;
    }

    l_ret->is_client_to_uplink = true;
    l_ret->trans_ctx->esocket->callbacks.worker_assign_callback = dap_stream_esocket_worker_assign_cb;
    l_ret->trans_ctx->esocket->callbacks.worker_unassign_callback = dap_stream_esocket_worker_unassign_cb;
    if (a_addr)
        l_ret->node = *a_addr;
    l_ret->authorized = a_authorized;
    return l_ret;
}


/**
 * @brief dap_stream_delete_unsafe
 * @param a_stream
 */
void dap_stream_delete_unsafe(dap_stream_t *a_stream)
{  
    if(!a_stream) {
        log_it(L_ERROR,"stream delete NULL instance");
        return;
    }
    if (a_stream->stat_packets_lost || a_stream->stat_packets_replayed)
        log_it(L_NOTICE, "Stream closed: %zu packets lost, %zu replayed",
               a_stream->stat_packets_lost, a_stream->stat_packets_replayed);
    dap_stream_delete_from_list(a_stream);
    // a_stream->esocket_uuid = 0;
    while (a_stream->channel_count)
        dap_stream_ch_delete(a_stream->channel[a_stream->channel_count - 1]);

    if(a_stream->session) {
        // Graceful close with configurable timeout
        // Configure via DAP_STREAM_CLOSE_TIMEOUT_MS
        #if DAP_STREAM_CLOSE_TIMEOUT_MS > 0
            // Future: Implement delayed close using dap_timerfd
            // This would allow pending data to be sent before session close
            // For now, fallback to immediate close
            log_it(L_DEBUG, "Stream close timeout configured but not yet implemented, closing immediately");
        #endif
        dap_stream_session_close_mt(a_stream->session->id);
    }

    // Call trans->ops->close() FIRST to let transport manage esocket
    // This allows transport to extract esocket, set trans_ctx->esocket=NULL, and handle cleanup
    // Must be called BEFORE accessing trans_ctx->esocket directly
    dap_net_trans_t *l_trans = a_stream->trans;
    if (l_trans) {
        a_stream->trans = NULL;  // Prevent double close
        if (l_trans->ops && l_trans->ops->close) {
            l_trans->ops->close(a_stream);
        }
    }

    if (a_stream->trans_ctx && a_stream->trans_ctx->esocket && a_stream->trans_ctx->esocket_worker) {
        dap_worker_t *l_current = dap_worker_get_current();
        if (l_current == a_stream->trans_ctx->esocket_worker) {
            dap_events_socket_remove_and_delete_unsafe(a_stream->trans_ctx->esocket, false);
        } else {
            dap_events_socket_remove_and_delete_mt(a_stream->trans_ctx->esocket_worker,
                                                   a_stream->trans_ctx->esocket_uuid);
        }
        a_stream->trans_ctx->esocket = NULL;
        a_stream->trans_ctx->esocket_uuid = 0;
        a_stream->trans_ctx->esocket_worker = NULL;
    }
    DAP_DELETE(a_stream->trans_ctx);
    a_stream->trans_ctx = NULL;


    DAP_DEL_Z(a_stream->buf_fragments);
    DAP_DELETE(a_stream);
    log_it(L_NOTICE,"Stream connection is over");
}

/**
 * @brief Process incoming stream data from external buffer (transport agnostic)
 * @param a_stream Stream instance
 * @param a_data Data buffer to process
 * @param a_data_size Size of data buffer
 * @return Number of bytes processed
 */
size_t dap_stream_data_proc_read_ext(dap_stream_t *a_stream, const void *a_data, size_t a_data_size)
{
    if (!a_stream || !a_data || a_data_size == 0) {
        return 0;
    }
    
    byte_t *l_pos = (byte_t*)a_data;
    byte_t *l_end = l_pos + a_data_size;
    size_t l_shift = 0, l_processed_size = 0;
    
    while (l_pos < l_end && (l_pos = memchr(l_pos, c_dap_stream_sig[0], (size_t)(l_end - l_pos)))) {
        if ((size_t)(l_end - l_pos) < sizeof(dap_stream_pkt_hdr_t)) {
            break;
        }
        
        if (!memcmp(l_pos, c_dap_stream_sig, sizeof(c_dap_stream_sig))) {
            dap_stream_pkt_t *l_pkt = (dap_stream_pkt_t*)l_pos;
            if (l_pkt->hdr.size > DAP_STREAM_PKT_SIZE_MAX) {
                log_it(L_ERROR, "Invalid packet size %u, dump it", l_pkt->hdr.size);
                l_shift = sizeof(dap_stream_pkt_hdr_t);
            } else if ((l_shift = sizeof(dap_stream_pkt_hdr_t) + l_pkt->hdr.size) <= (size_t)(l_end - l_pos)) {
                debug_if(s_dump_packet_headers, L_DEBUG, "Processing full packet, size %zu", l_shift);
                s_stream_proc_pkt_in(a_stream, l_pkt);
            } else {
                break;
            }
            l_pos += l_shift;
            l_processed_size += l_shift;
        } else {
            ++l_pos;
        }
    }
    
    debug_if(s_dump_packet_headers && l_processed_size, L_DEBUG, 
             "Processed %zu / %zu bytes", l_processed_size, a_data_size);
    
    return l_processed_size;
}

/**
 * @brief Process incoming stream data from esocket buffer (legacy wrapper)
 * @param a_stream Stream instance
 * @return Number of bytes processed
 */
size_t dap_stream_data_proc_read(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->trans_ctx || !a_stream->trans_ctx->esocket)
        return 0;
    dap_events_socket_t *l_es = a_stream->trans_ctx->esocket;
    if (!l_es->buf_in)
        return 0;
    return dap_stream_data_proc_read_ext(a_stream, l_es->buf_in, l_es->buf_in_size);
}

/**
 * @brief stream_proc_pkt_in
 * @param sid
 */
static void s_stream_proc_pkt_in(dap_stream_t * a_stream, dap_stream_pkt_t *a_pkt)
{
    size_t a_pkt_size = sizeof(dap_stream_pkt_hdr_t) + a_pkt->hdr.size;
    bool l_is_clean_fragments = false;
    a_stream->is_active = true;

    debug_if(s_dump_packet_headers, L_INFO, "s_stream_proc_pkt_in: stream=%p, packet type=0x%02X size=%u", 
           a_stream, a_pkt->hdr.type, a_pkt->hdr.size);

    switch (a_pkt->hdr.type) {
    case STREAM_PKT_TYPE_FRAGMENT_PACKET: {

        debug_if(s_dump_packet_headers, L_INFO, "Processing FRAGMENT_PACKET, size=%u", a_pkt->hdr.size);

        size_t l_fragm_dec_size = dap_enc_decode_out_size(a_stream->session->key, a_pkt->hdr.size, DAP_ENC_DATA_TYPE_RAW);
        debug_if(s_dump_packet_headers, L_DEBUG, "FRAG: stream=%p, session=%p, key=%p, fragm_dec_size=%zu",
                 a_stream, a_stream->session, a_stream->session ? a_stream->session->key : NULL, l_fragm_dec_size);
        
        a_stream->pkt_cache = DAP_NEW_Z_SIZE(byte_t, l_fragm_dec_size);
        dap_stream_fragment_pkt_t *l_fragm_pkt = (dap_stream_fragment_pkt_t*)a_stream->pkt_cache;
        
        debug_if(s_dump_packet_headers, L_DEBUG, "FRAG: CALLING dap_stream_pkt_read_unsafe (stream=%p, pkt=%p, out=%p, out_size=%zu)",
                 a_stream, a_pkt, l_fragm_pkt, l_fragm_dec_size);
        
        size_t l_dec_pkt_size = dap_stream_pkt_read_unsafe(a_stream, a_pkt, l_fragm_pkt, l_fragm_dec_size);

        debug_if(s_dump_packet_headers, L_DEBUG, "FRAG: dap_stream_pkt_read_unsafe returned l_dec_pkt_size=%zu (expected_min=%zu)",
                 l_dec_pkt_size, sizeof(dap_stream_fragment_pkt_t));

        if(l_dec_pkt_size == 0) {
            debug_if(s_dump_packet_headers, L_WARNING, "Input: can't decode packet size = %zu (stream=%p)", a_pkt_size, a_stream);
            l_is_clean_fragments = true;
            break;
        }
        if(l_dec_pkt_size != l_fragm_pkt->size + sizeof(dap_stream_fragment_pkt_t)) {
            debug_if(s_dump_packet_headers, L_WARNING, "Input: decoded packet has bad size = %zu, decoded size = %zu",
                     l_fragm_pkt->size + sizeof(dap_stream_fragment_pkt_t), l_dec_pkt_size);
            l_is_clean_fragments = true;
            break;
        }

        debug_if(s_dump_packet_headers, L_INFO, "Fragment decoded: size=%u mem_shift=%u filled=%zu", 
               l_fragm_pkt->size, l_fragm_pkt->mem_shift, a_stream->buf_fragments_size_filled);

        if(a_stream->buf_fragments_size_filled != l_fragm_pkt->mem_shift) {
            debug_if(s_dump_packet_headers, L_WARNING, "Input: wrong fragment position %u, have to be %zu. Drop packet",
                     l_fragm_pkt->mem_shift, a_stream->buf_fragments_size_filled);
            l_is_clean_fragments = true;
            break;
        } else {
            if(!a_stream->buf_fragments || a_stream->buf_fragments_size_total < l_fragm_pkt->full_size) {
                DAP_DEL_Z(a_stream->buf_fragments);
                a_stream->buf_fragments = DAP_NEW_Z_SIZE(uint8_t, l_fragm_pkt->full_size);
                a_stream->buf_fragments_size_total = l_fragm_pkt->full_size;
            }
            memcpy(a_stream->buf_fragments + l_fragm_pkt->mem_shift, l_fragm_pkt->data, l_fragm_pkt->size);
            a_stream->buf_fragments_size_filled += l_fragm_pkt->size;
        }

        // Not last fragment, otherwise go to parsing STREAM_PKT_TYPE_DATA_PACKET
        if(a_stream->buf_fragments_size_filled < l_fragm_pkt->full_size) {
            debug_if(s_debug, L_DEBUG, "Fragment not complete yet: filled=%zu full=%u", 
                   a_stream->buf_fragments_size_filled, l_fragm_pkt->full_size);
            break;
        }
        // All fragments collected, move forward
        debug_if(s_debug, L_INFO, "All fragments collected! Falling through to DATA_PACKET processing");
    }
    case STREAM_PKT_TYPE_DATA_PACKET: {
        dap_stream_ch_pkt_t *l_ch_pkt;
        size_t l_dec_pkt_size;

        debug_if(s_debug, L_INFO, "Processing DATA_PACKET: from_fragment=%s", 
               (a_pkt->hdr.type == STREAM_PKT_TYPE_FRAGMENT_PACKET) ? "yes" : "no");

        if (a_pkt->hdr.type == STREAM_PKT_TYPE_FRAGMENT_PACKET) {
            l_ch_pkt = (dap_stream_ch_pkt_t*)a_stream->buf_fragments;
            l_dec_pkt_size = a_stream->buf_fragments_size_total;
        } else {
            size_t l_pkt_dec_size = dap_enc_decode_out_size(a_stream->session->key, a_pkt->hdr.size, DAP_ENC_DATA_TYPE_RAW);
            a_stream->pkt_cache = DAP_NEW_Z_SIZE(byte_t, l_pkt_dec_size);
            l_ch_pkt = (dap_stream_ch_pkt_t*)a_stream->pkt_cache;
            l_dec_pkt_size = dap_stream_pkt_read_unsafe(a_stream, a_pkt, l_ch_pkt, l_pkt_dec_size);
            
            debug_if(s_dump_packet_headers, L_INFO, 
                     "DATA_PACKET decryption: key=%p, encrypted_size=%u, expected_dec=%zu, actual_dec=%zu",
                     a_stream->session->key, a_pkt->hdr.size, l_pkt_dec_size, l_dec_pkt_size);
        }

        if (l_dec_pkt_size < sizeof(l_ch_pkt->hdr)) {
            log_it(L_WARNING, "Input: decoded size %zu is lesser than size of packet header %zu", l_dec_pkt_size, sizeof(l_ch_pkt->hdr));
            l_is_clean_fragments = true;
            break;
        }
        if (l_dec_pkt_size != l_ch_pkt->hdr.data_size + sizeof(l_ch_pkt->hdr)) {
            log_it(L_WARNING, "Input: decoded packet BAD SIZE: expected_dec=%zu (hdr.data_size=%u + hdr_size=%zu), actual_dec=%zu",
                   l_ch_pkt->hdr.data_size + sizeof(l_ch_pkt->hdr), l_ch_pkt->hdr.data_size, sizeof(l_ch_pkt->hdr), l_dec_pkt_size);
            l_is_clean_fragments = true;
            break;
        }

        if (!s_detect_loose_packet(a_stream)) {
            dap_stream_ch_t * l_ch = NULL;

            debug_if(s_dump_packet_headers, L_INFO, "Looking for channel '%c' (0x%02x) in stream (channel_count=%zu)",
                   (char)l_ch_pkt->hdr.id, l_ch_pkt->hdr.id, a_stream->channel_count);

            for(size_t i=0;i<a_stream->channel_count;i++){
                if(a_stream->channel[i]->proc){
                    if(a_stream->channel[i]->proc->id == l_ch_pkt->hdr.id ){
                        l_ch=a_stream->channel[i];
                        break;
                    }
                }
            }
            if(l_ch) {
                l_ch->stat.bytes_read += l_ch_pkt->hdr.data_size;
                if(l_ch->proc && l_ch->proc->packet_in_callback) {
                    bool l_security_check_passed = l_ch->proc->packet_in_callback(l_ch, l_ch_pkt);
                    debug_if(s_dump_packet_headers, L_INFO, "Income channel packet: id='%c' size=%u type=0x%02X seq_id=0x%016"
                                                            DAP_UINT64_FORMAT_X" enc_type=0x%02X (stream=%p)", (char)l_ch_pkt->hdr.id,
                                                            l_ch_pkt->hdr.data_size, l_ch_pkt->hdr.type, l_ch_pkt->hdr.seq_id, l_ch_pkt->hdr.enc_type,
                                                            a_stream);
                    for (dap_list_t *it = l_ch->packet_in_notifiers; !l_ch->closing && it && l_security_check_passed; it = it->next) {
                        dap_stream_ch_notifier_t *l_notifier = it->data;
                        assert(l_notifier);
                        l_notifier->callback(l_ch, l_ch_pkt->hdr.type, l_ch_pkt->data, l_ch_pkt->hdr.data_size, l_notifier->arg);
                    }
                    if (l_ch->closing)
                        break;
                }
            } else{
                log_it(L_WARNING, "Input: unprocessed channel packet id '%c'",(char) l_ch_pkt->hdr.id );
            }
        }
        // packet already defragmented
        if(a_pkt->hdr.type == STREAM_PKT_TYPE_FRAGMENT_PACKET) {
            l_is_clean_fragments = true;
        }
    } break;
    case STREAM_PKT_TYPE_SERVICE_PACKET: {
        if (a_pkt_size != sizeof(dap_stream_pkt_t) + sizeof(dap_stream_srv_pkt_t)) {
            log_it(L_WARNING, "Input: incorrect service packet size %zu, estimated %zu", a_pkt_size - sizeof(dap_stream_pkt_t), sizeof(dap_stream_srv_pkt_t));
            break;
        }
        dap_stream_srv_pkt_t *l_srv_pkt = (dap_stream_srv_pkt_t *)a_pkt->data;
        uint32_t l_session_id = l_srv_pkt->session_id;
        if (a_stream->trans_ctx && a_stream->trans_ctx->trans
                && a_stream->trans_ctx->trans->ops && a_stream->trans_ctx->trans->ops->check_session)
            a_stream->trans_ctx->trans->ops->check_session(
                    a_stream->trans_ctx->trans, l_session_id, a_stream->trans_ctx->esocket);
    } break;
    case STREAM_PKT_TYPE_KEEPALIVE: {
        debug_if(s_debug, L_DEBUG, "Keep alive check recieved");
        dap_stream_pkt_write_unsafe(a_stream, STREAM_PKT_TYPE_ALIVE, NULL, 0);
        if (a_stream->keepalive_timer)
            dap_timerfd_reset_unsafe(a_stream->keepalive_timer);
    } break;
    case STREAM_PKT_TYPE_ALIVE:
        a_stream->is_active = false; // To prevent keep-alive concurrency
        debug_if(s_debug, L_DEBUG, "Keep alive response recieved");
        break;
    default:
        log_it(L_WARNING, "Unknown header type");
    }
    // Clean memory
    DAP_DEL_Z(a_stream->pkt_cache);
    if(l_is_clean_fragments) {
        DAP_DEL_Z(a_stream->buf_fragments);
        a_stream->buf_fragments_size_total = a_stream->buf_fragments_size_filled = 0;
    }
}

static bool s_detect_loose_packet(dap_stream_t * a_stream) {
    dap_stream_ch_pkt_t *l_ch_pkt = a_stream->buf_fragments_size_filled
            ? (dap_stream_ch_pkt_t*)a_stream->buf_fragments
            : (dap_stream_ch_pkt_t*)a_stream->pkt_cache;

    long long l_count_lost_packets =
            l_ch_pkt->hdr.seq_id || a_stream->client_last_seq_id_packet
            ? (long long) l_ch_pkt->hdr.seq_id - (long long) (a_stream->client_last_seq_id_packet + 1)
            : 0;

    if (l_count_lost_packets > 0)
        a_stream->stat_packets_lost += (size_t)l_count_lost_packets;
    else if (l_count_lost_packets < 0)
        a_stream->stat_packets_replayed++;
    debug_if(s_debug, L_DEBUG, "Current seq_id: %" DAP_UINT64_FORMAT_U ", last: %zu",
                                l_ch_pkt->hdr.seq_id, a_stream->client_last_seq_id_packet);
    a_stream->client_last_seq_id_packet = l_ch_pkt->hdr.seq_id;
    return l_count_lost_packets < 0;
}

void dap_stream_set_client_esocket_callback(dap_stream_from_esocket_callback_t a_callback)
{
    s_client_esocket_callback = a_callback;
}

dap_stream_t *dap_stream_get_from_es(dap_events_socket_t *a_es)
{
    if (a_es->server) {
        dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_es->_inheritor;
        return l_trans_ctx ? l_trans_ctx->stream : NULL;
    } else {
        return s_client_esocket_callback ? s_client_esocket_callback(a_es) : NULL;
    }
}

/**
 * @brief s_callback_keepalive
 * @param a_arg
 * @return
 */
static bool s_callback_keepalive(void *a_arg, bool a_server_side)
{
    if (!a_arg)
        return false;
    dap_events_socket_uuid_t * l_es_uuid = (dap_events_socket_uuid_t*) a_arg;
    dap_worker_t * l_worker = dap_worker_get_current();
    if (!l_worker) {
        log_it(L_ERROR, "l_worker is NULL");
        return false;
    }
    dap_events_socket_t * l_es = dap_context_find(l_worker->context, *l_es_uuid);
    if(l_es) {
        assert(a_server_side == !!l_es->server);
        dap_stream_t *l_stream = dap_stream_get_from_es(l_es);
        assert(l_stream);
        if (l_stream->is_active) {
            l_stream->is_active = false;
            return true;
        }
        if(s_debug)
            log_it(L_DEBUG,"Keepalive for sock fd %"DAP_FORMAT_SOCKET" uuid 0x%016"DAP_UINT64_FORMAT_x, l_es->socket, *l_es_uuid);
        dap_stream_pkt_hdr_t l_pkt = {};
        l_pkt.type = STREAM_PKT_TYPE_KEEPALIVE;
        memcpy(l_pkt.sig, c_dap_stream_sig, sizeof(l_pkt.sig));
        dap_stream_send_unsafe(l_stream, &l_pkt, sizeof(l_pkt));
        return true;
    }else{
        if(s_debug)
            log_it(L_INFO,"Keepalive for sock uuid %016"DAP_UINT64_FORMAT_x" removed", *l_es_uuid);
        DAP_DELETE(l_es_uuid);
        return false; // Socket is removed from worker
    }
}

bool dap_stream_callback_client_keepalive(void *a_arg)
{
    return s_callback_keepalive(a_arg, false);
}

bool dap_stream_callback_server_keepalive(void *a_arg)
{
    return s_callback_keepalive(a_arg, true);
}

int s_stream_add_to_hashtable(dap_stream_t *a_stream)
{
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: entering, stream=%p", (void*)a_stream);
    dap_stream_t *l_double = NULL;
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: searching for duplicate");
    dap_ht_find(s_authorized_streams, &a_stream->node, sizeof(a_stream->node), l_double);
    if (l_double) {
        log_it(L_DEBUG, "Stream already present in hash table for node "NODE_ADDR_FP_STR"", NODE_ADDR_FP_ARGS_S(a_stream->node));
        return -1;
    }
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: no duplicate found, setting primary=true");
    a_stream->primary = true;
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: adding to hash table");
    dap_ht_add_keyptr(s_authorized_streams, &a_stream->node, sizeof(a_stream->node), a_stream);
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: added to hash, notifying member add");
    if (s_member_add_callback)
        s_member_add_callback(&a_stream->node);
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: member add notified, notifying stream event");
    dap_stream_event_notify_add(&a_stream->node, a_stream->is_client_to_uplink);
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: completed successfully");
    return 0;
}

void dap_stream_delete_from_list(dap_stream_t *a_stream)
{
    dap_return_if_fail(a_stream);
    int lock = pthread_rwlock_wrlock(&s_streams_lock);
    assert(lock != EDEADLK);
    if ( lock == EDEADLK )
        return log_it(L_CRITICAL, "! Attempt to aquire streams lock recursively !");

    // Check if stream is in the list (prev is set by DL_APPEND/DL_DELETE)
    // Client-side streams may never be added if worker_assign didn't fire
    dap_stream_t *l_stream = NULL;
    bool l_in_list = false;
    dap_dl_foreach(s_streams, l_stream) {
        if (l_stream == a_stream) {
            l_in_list = true;
            break;
        }
    }
    l_stream = NULL;
    if (l_in_list)
        dap_dl_delete(s_streams, a_stream);
    if (a_stream->authorized) {
        // It's an authorized stream, try to replace it in hastable
        if (a_stream->primary)
            dap_ht_del(s_authorized_streams, a_stream);
        dap_dl_foreach(s_streams, l_stream)
            if (l_stream->node.uint64 == a_stream->node.uint64)
                break;
        if (l_stream) {
            s_stream_add_to_hashtable(l_stream);
            dap_stream_event_notify_replace(&a_stream->node, l_stream->is_client_to_uplink);
        } else {
            if (s_member_del_callback)
                s_member_del_callback(&a_stream->node);
            dap_stream_event_notify_delete(&a_stream->node);
        }
    }
    pthread_rwlock_unlock(&s_streams_lock);
}

int dap_stream_add_to_list(dap_stream_t *a_stream)
{
    dap_return_val_if_fail(a_stream, -1);
    int l_ret = 0;
    int lock = pthread_rwlock_wrlock(&s_streams_lock);
    assert(lock != EDEADLK);
    if ( lock == EDEADLK )
        return log_it(L_CRITICAL, "! Attempt to aquire streams lock recursively !"), -666;
    dap_stream_t *l_tmp = NULL;
    dap_dl_foreach(s_streams, l_tmp) {
        if (l_tmp == a_stream) {
            pthread_rwlock_unlock(&s_streams_lock);
            return 0;
        }
    }
    dap_dl_append(s_streams, a_stream);
    if (a_stream->authorized)
        l_ret = s_stream_add_to_hashtable(a_stream);
    pthread_rwlock_unlock(&s_streams_lock);
    return l_ret;
}

/**
 * @brief dap_stream_find_by_addr find a_stream with current node
 * @param a_addr - autorrized node address
 * @param a_worker - pointer to worker
 * @return  esocket_uuid if ok 0 if not
 */
dap_events_socket_uuid_t dap_stream_find_by_addr(dap_cluster_node_addr_t *a_addr, dap_worker_t **a_worker)
{
    dap_return_val_if_fail(a_addr && a_addr->uint64, 0);
    dap_stream_t *l_auth_stream = NULL;
    dap_events_socket_uuid_t l_ret = 0;
    int lock = pthread_rwlock_rdlock(&s_streams_lock);
    assert(lock != EDEADLK);
    if ( lock == EDEADLK )
        return log_it(L_CRITICAL, "! Attempt to aquire streams lock recursively !"), 0;

    dap_ht_find(s_authorized_streams, a_addr, sizeof(*a_addr), l_auth_stream);
    if (l_auth_stream) {
        if (a_worker)
            *a_worker = l_auth_stream->stream_worker->worker;
        if (l_auth_stream->trans_ctx && l_auth_stream->trans_ctx->esocket)
            l_ret = l_auth_stream->trans_ctx->esocket->uuid;
    } else if (a_worker)
        *a_worker = NULL;
    pthread_rwlock_unlock(&s_streams_lock);
    return l_ret;
}

dap_list_t *dap_stream_find_all_by_addr(dap_cluster_node_addr_t *a_addr)
{
    dap_list_t *l_ret = NULL;
    dap_return_val_if_fail(a_addr, l_ret);
    dap_stream_t *l_stream;

    int lock = pthread_rwlock_rdlock(&s_streams_lock);
    assert(lock != EDEADLK);
    if ( lock == EDEADLK )
        return log_it(L_CRITICAL, "! Attempt to aquire streams lock recursively !"), NULL;

    dap_dl_foreach(s_streams, l_stream) {
        if (!l_stream->authorized || a_addr->uint64 != l_stream->node.uint64)
            continue;
        dap_events_socket_uuid_ctrl_t *l_ret_item = DAP_NEW(dap_events_socket_uuid_ctrl_t);
        if (!l_ret_item) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            dap_list_free_full(l_ret, NULL);
            return NULL;
        }
        l_ret_item->worker = l_stream->stream_worker->worker;
        if (l_stream->trans_ctx && l_stream->trans_ctx->esocket)
            l_ret_item->uuid = l_stream->trans_ctx->esocket->uuid;
        l_ret = dap_list_append(l_ret, l_ret_item);
    }
    pthread_rwlock_unlock(&s_streams_lock);
    return l_ret;
}

static void s_stream_fill_info(dap_stream_t *a_stream, dap_stream_info_t *a_out_info)
{
    a_out_info->node_addr = a_stream->node;
    if (a_stream->trans_ctx && a_stream->trans_ctx->remote_addr_str[0]) {
        a_out_info->remote_addr_str = dap_strdup_printf("%-*s", INET_ADDRSTRLEN - 1, a_stream->trans_ctx->remote_addr_str);
        a_out_info->remote_port = a_stream->trans_ctx->remote_port;
    }
    a_out_info->channels = DAP_NEW_Z_SIZE_RET_IF_FAIL(char, a_stream->channel_count + 1, a_out_info->remote_addr_str);
    for (size_t i = 0; i < a_stream->channel_count; i++)
        a_out_info->channels[i] = a_stream->channel[i]->proc->id;
    a_out_info->total_packets_sent = a_stream->seq_id;
    a_out_info->is_uplink = a_stream->is_client_to_uplink;
}

dap_stream_info_t *dap_stream_get_all_links_info(size_t *a_count)
{
    dap_return_val_if_pass(!s_streams, NULL);
    int lock = pthread_rwlock_rdlock(&s_streams_lock);
    assert(lock != EDEADLK);
    if (lock == EDEADLK)
        return log_it(L_CRITICAL, "! Attempt to aquire streams lock recursively !"), NULL;

    size_t l_streams_count = 0, i = 0;
    dap_dl_count(s_streams, l_streams_count);
    if (!l_streams_count) {
        pthread_rwlock_unlock(&s_streams_lock);
        return NULL;
    }
    dap_stream_info_t *l_ret = DAP_NEW_Z_COUNT(dap_stream_info_t, l_streams_count);
    if (!l_ret) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        pthread_rwlock_unlock(&s_streams_lock);
        return NULL;
    }
    dap_stream_t *it = NULL;
    dap_dl_foreach(s_streams, it)
        s_stream_fill_info(it, l_ret + i++);
    pthread_rwlock_unlock(&s_streams_lock);
    if (a_count)
        *a_count = i;
    return l_ret;
}

dap_stream_info_t *dap_stream_get_links_info_by_addrs(dap_cluster_node_addr_t *a_addrs,
                                                       size_t a_addrs_count, size_t *a_count)
{
    dap_return_val_if_pass(!a_addrs || !a_addrs_count, NULL);
    int lock = pthread_rwlock_rdlock(&s_streams_lock);
    assert(lock != EDEADLK);
    if (lock == EDEADLK)
        return log_it(L_CRITICAL, "! Attempt to aquire streams lock recursively !"), NULL;

    dap_stream_info_t *l_ret = DAP_NEW_Z_COUNT(dap_stream_info_t, a_addrs_count);
    if (!l_ret) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        pthread_rwlock_unlock(&s_streams_lock);
        return NULL;
    }
    size_t i = 0;
    for (size_t n = 0; n < a_addrs_count; n++) {
        dap_stream_t *it = NULL;
        dap_ht_find(s_authorized_streams, a_addrs + n, sizeof(*a_addrs), it);
        if (!it) {
            log_it(L_ERROR, "Address " NODE_ADDR_FP_STR " not found in streams HT",
                   NODE_ADDR_FP_ARGS(a_addrs + n));
            continue;
        }
        s_stream_fill_info(it, l_ret + i++);
    }
    pthread_rwlock_unlock(&s_streams_lock);
    if (a_count)
        *a_count = i;
    return l_ret;
}



void dap_stream_delete_links_info(dap_stream_info_t *a_info, size_t a_count)
{
    dap_return_if_fail(a_info && a_count);
    for (size_t i = 0; i < a_count; i++) {
        dap_stream_info_t *it = a_info + i;
        DAP_DEL_Z(it->remote_addr_str);
        DAP_DEL_Z(it->channels);
    }
    DAP_DELETE(a_info);
}
