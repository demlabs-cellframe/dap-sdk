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

#include "dap_common.h"
#include "dap_config.h"
#include "dap_events_socket.h"
#include "dap_context_queue.h"
#include "dap_worker.h"
#include "dap_enc.h"
#include "dap_enc_key.h"
#include "dap_stream.h"
#include "dap_stream_pkt.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream_pkt.h"

#ifndef DAP_OS_WASM
#include "dap_io_flow_datagram.h"
#endif

#include "dap_serialize.h"

#define LOG_TAG "stream_pkt"

static bool s_debug_more = false;

/*
 * dap_serialize schemas for wire-format ↔ aligned in-memory conversion
 */

const dap_serialize_field_t g_dap_stream_pkt_hdr_fields[] = {
    {
        .name = "sig",
        .type = DAP_SERIALIZE_TYPE_BYTES_FIXED,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_stream_pkt_hdr_mem_t, sig),
        .size = STREAM_PKT_SIG_SIZE,
    },
    {
        .name = "size",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_stream_pkt_hdr_mem_t, size),
        .size = sizeof(uint32_t),
    },
    {
        .name = "timestamp",
        .type = DAP_SERIALIZE_TYPE_UINT64,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_stream_pkt_hdr_mem_t, timestamp),
        .size = sizeof(uint64_t),
    },
    {
        .name = "type",
        .type = DAP_SERIALIZE_TYPE_UINT8,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_stream_pkt_hdr_mem_t, type),
        .size = sizeof(uint8_t),
    },
    {
        .name = "src_addr",
        .type = DAP_SERIALIZE_TYPE_UINT64,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_stream_pkt_hdr_mem_t, src_addr),
        .size = sizeof(uint64_t),
    },
    {
        .name = "dst_addr",
        .type = DAP_SERIALIZE_TYPE_UINT64,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_stream_pkt_hdr_mem_t, dst_addr),
        .size = sizeof(uint64_t),
    },
};

const dap_serialize_schema_t g_dap_stream_pkt_hdr_schema = {
    .name = "stream_pkt_hdr",
    .version = 1,
    .struct_size = sizeof(dap_stream_pkt_hdr_mem_t),
    .field_count = sizeof(g_dap_stream_pkt_hdr_fields) / sizeof(g_dap_stream_pkt_hdr_fields[0]),
    .fields = g_dap_stream_pkt_hdr_fields,
    .magic = DAP_STREAM_PKT_HDR_MAGIC,
    .validate_func = NULL,
};

const dap_serialize_field_t g_dap_stream_srv_pkt_fields[] = {
    {
        .name = "session_id",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_stream_srv_pkt_mem_t, session_id),
        .size = sizeof(uint32_t),
    },
    {
        .name = "enc_type",
        .type = DAP_SERIALIZE_TYPE_UINT8,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_stream_srv_pkt_mem_t, enc_type),
        .size = sizeof(uint8_t),
    },
    {
        .name = "coockie",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_stream_srv_pkt_mem_t, coockie),
        .size = sizeof(uint32_t),
    },
};

const dap_serialize_schema_t g_dap_stream_srv_pkt_schema = {
    .name = "stream_srv_pkt",
    .version = 1,
    .struct_size = sizeof(dap_stream_srv_pkt_mem_t),
    .field_count = sizeof(g_dap_stream_srv_pkt_fields) / sizeof(g_dap_stream_srv_pkt_fields[0]),
    .fields = g_dap_stream_srv_pkt_fields,
    .magic = DAP_STREAM_SRV_PKT_MAGIC,
    .validate_func = NULL,
};

#ifndef DAP_OS_WASM
static inline ssize_t s_stream_send_datagram_unsafe(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        return 0;
    }
    
    if (!a_stream->trans_ctx || !a_stream->trans_ctx->esocket) {
        log_it(L_ERROR, "Stream has no trans_ctx or esocket");
        return 0;
    }
    
    if (!a_stream->flow) {
        log_it(L_ERROR, "Datagram stream has no flow set");
        return 0;
    }
    
    dap_events_socket_t *l_es = a_stream->trans_ctx->esocket;
    dap_io_flow_datagram_t *l_flow = (dap_io_flow_datagram_t*)a_stream->flow;
    
    struct sockaddr_storage l_dest_addr;
    socklen_t l_dest_addr_len;
    
    if (!dap_io_flow_datagram_get_remote_addr(l_flow, &l_dest_addr, &l_dest_addr_len)) {
        log_it(L_ERROR, "Failed to get datagram destination address from flow callback");
        return 0;
    }
    
    return dap_events_socket_sendto_unsafe(l_es, a_data, a_size, &l_dest_addr, l_dest_addr_len);
}
#endif

const uint8_t c_dap_stream_sig [STREAM_PKT_SIG_SIZE] = {0xa0,0x95,0x96,0xa9,0x9e,0x5c,0xfb,0xfa};

/**
 * @brief stream_pkt_read
 * @param sid
 * @param pkt
 * @param buf_out
 */
size_t dap_stream_pkt_read_unsafe( dap_stream_t * a_stream, dap_stream_pkt_t * a_pkt, void * a_buf_out, size_t a_buf_out_size)
{
    // Initialize debug flag on first call
    static bool s_init_once = false;
    if (!s_init_once) {
        s_init_once = true;
        s_debug_more = dap_config_get_item_bool_default(g_config, "stream_pkt", "debug_more", false);
    }
    
    dap_stream_pkt_hdr_mem_t l_hdr;
    if (dap_stream_pkt_hdr_unpack((const uint8_t *)a_pkt, DAP_STREAM_PKT_HDR_WIRE_SIZE, &l_hdr) != 0) {
        log_it(L_WARNING, "dap_stream_pkt_read_unsafe: header unpack failed (stream=%p)", a_stream);
        return 0;
    }
    const uint8_t *l_payload = (const uint8_t *)a_pkt + DAP_STREAM_PKT_HDR_WIRE_SIZE;

    debug_if(s_debug_more, L_DEBUG, "dap_stream_pkt_read_unsafe: ENTRY stream=%p, session=%p, key=%p, pkt_size=%u, buf_out_size=%zu",
             a_stream, a_stream->session, a_stream->session ? a_stream->session->key : NULL, l_hdr.size, a_buf_out_size);
    
    // Handle unencrypted packet or missing session key
    if (!a_stream->session || !a_stream->session->key) {
        log_it(L_WARNING, "dap_stream_pkt_read_unsafe: NO SESSION or NO KEY! stream=%p", a_stream);
        size_t l_copy_size = l_hdr.size > a_buf_out_size ? a_buf_out_size : l_hdr.size;
        memcpy(a_buf_out, l_payload, l_copy_size);
        return l_copy_size;
    }
    
    size_t l_result = a_stream->session->key->dec_na(a_stream->session->key, l_payload, l_hdr.size, a_buf_out, a_buf_out_size);
    
    debug_if(s_debug_more, L_DEBUG, "dap_stream_pkt_read_unsafe: RETURNED dec_na result=%zu (stream=%p, session=%p, key=%p)",
             l_result, a_stream, a_stream->session, a_stream->session->key);
    
    return l_result;
}

/**
 * @brief stream_ch_pkt_write
 * @param ch
 * @param data
 * @param data_size
 * @return
 */

size_t dap_stream_pkt_write_unsafe(dap_stream_t *a_stream, uint8_t a_type, const void *a_data, size_t a_data_size)
{
    // Initialize debug flag on first call
    static bool s_init_once = false;
    if (!s_init_once) {
        s_init_once = true;
        s_debug_more = dap_config_get_item_bool_default(g_config, "stream_pkt", "debug_more", false);
    }
    
    if (a_data_size > DAP_STREAM_PKT_FRAGMENT_SIZE)
        return log_it(L_ERROR, "Too big fragment size %zu", a_data_size), 0;
    static _Thread_local char s_pkt_buf[DAP_STREAM_PKT_FRAGMENT_SIZE + sizeof(dap_stream_pkt_hdr_t) + 0x40] = { 0 };
    a_stream->is_active = true;
    
    dap_enc_key_t *l_key = (a_stream->session) ? a_stream->session->key : NULL;
    
    debug_if(s_debug_more, L_DEBUG, "dap_stream_pkt_write_unsafe: stream=%p, session=%p, key=%p, type=0x%02x, data_size=%zu", 
             a_stream, a_stream->session, l_key, a_type, a_data_size);
    
    dap_stream_pkt_hdr_mem_t l_hdr_mem = {0};
    memcpy(l_hdr_mem.sig, c_dap_stream_sig, sizeof(l_hdr_mem.sig));
    l_hdr_mem.timestamp = dap_time_now();
    l_hdr_mem.type = a_type;
    l_hdr_mem.src_addr = g_node_addr.uint64;
    l_hdr_mem.dst_addr = a_stream->node.uint64;

    size_t l_full_size;

    if (a_data_size == 0 || !a_data) {
        l_full_size = DAP_STREAM_PKT_HDR_WIRE_SIZE;
        l_hdr_mem.size = 0;
    } else if (l_key) {
        l_full_size = dap_enc_key_get_enc_size(l_key->type, a_data_size) + DAP_STREAM_PKT_HDR_WIRE_SIZE;
        l_hdr_mem.size = (uint32_t)dap_enc_code(l_key, a_data, a_data_size, (uint8_t *)s_pkt_buf + DAP_STREAM_PKT_HDR_WIRE_SIZE,
                                                l_full_size - DAP_STREAM_PKT_HDR_WIRE_SIZE, DAP_ENC_DATA_TYPE_RAW);
    } else {
        l_full_size = a_data_size + DAP_STREAM_PKT_HDR_WIRE_SIZE;
        memcpy(s_pkt_buf + DAP_STREAM_PKT_HDR_WIRE_SIZE, a_data, a_data_size);
        l_hdr_mem.size = (uint32_t)a_data_size;
    }

    if (dap_stream_pkt_hdr_pack(&l_hdr_mem, (uint8_t *)s_pkt_buf, sizeof(s_pkt_buf)) != 0) {
        log_it(L_ERROR, "dap_stream_pkt_write_unsafe: header pack failed");
        return 0;
    }
    
    // NEW ARCHITECTURE: Use trans->ops->write if available (for UDP dispatcher)
    // Try trans_ctx->trans first (SERVER), then fall back to stream->trans (CLIENT)
    dap_net_trans_t *l_trans = NULL;
    if (a_stream->trans_ctx && a_stream->trans_ctx->trans) {
        l_trans = a_stream->trans_ctx->trans;
    } else if (a_stream->trans) {
        l_trans = a_stream->trans;
    }
    
    if (l_trans && l_trans->ops && l_trans->ops->write) {
        return l_trans->ops->write(a_stream, s_pkt_buf, l_full_size);
    }
    else if (a_stream->trans_ctx && a_stream->trans_ctx->esocket) {
        dap_events_socket_t *l_es = a_stream->trans_ctx->esocket;
#ifndef DAP_OS_WASM
        if (dap_events_socket_is_datagram(l_es))
            return s_stream_send_datagram_unsafe(a_stream, s_pkt_buf, l_full_size);
#endif
        return dap_events_socket_write_unsafe(l_es, s_pkt_buf, l_full_size);
    }
    return 0;
}

size_t dap_stream_pkt_write_mt(dap_worker_t * a_w,dap_events_socket_uuid_t a_es_uuid, dap_enc_key_t *a_key, const void * a_data, size_t a_data_size)
{
#ifdef DAP_EVENTS_CAPS_IOCP
    static _Thread_local char s_pkt_buf[DAP_STREAM_PKT_FRAGMENT_SIZE + sizeof(dap_stream_pkt_hdr_t) + 0x40] = { 0 };
    size_t l_full_size = dap_enc_key_get_enc_size(a_key->type, a_data_size) + DAP_STREAM_PKT_HDR_WIRE_SIZE;
    dap_stream_pkt_hdr_mem_t l_hdr_mem = {0};
    memcpy(l_hdr_mem.sig, c_dap_stream_sig, sizeof(l_hdr_mem.sig));
    l_hdr_mem.size = (uint32_t)dap_enc_code(a_key, a_data, a_data_size, (uint8_t *)s_pkt_buf + DAP_STREAM_PKT_HDR_WIRE_SIZE,
                                             l_full_size - DAP_STREAM_PKT_HDR_WIRE_SIZE, DAP_ENC_DATA_TYPE_RAW);
    if (dap_stream_pkt_hdr_pack(&l_hdr_mem, (uint8_t *)s_pkt_buf, sizeof(s_pkt_buf)) != 0) {
        log_it(L_ERROR, "dap_stream_pkt_write_mt: header pack failed");
        return 0;
    }
    return dap_events_socket_write(a_w, a_es_uuid, s_pkt_buf, l_full_size);
#else
    dap_worker_msg_io_t *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_worker_msg_io_t, 0);
    l_msg->esocket_uuid = a_es_uuid;
    size_t l_cipher_max = dap_enc_code_out_size(a_key, a_data_size, DAP_ENC_DATA_TYPE_RAW);
    l_msg->data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(byte_t, l_cipher_max + DAP_STREAM_PKT_HDR_WIRE_SIZE, 0, l_msg);
    byte_t *l_d = (byte_t *)l_msg->data;
    size_t l_enc_sz = dap_enc_code(a_key, a_data, a_data_size,
        l_d + DAP_STREAM_PKT_HDR_WIRE_SIZE, l_cipher_max, DAP_ENC_DATA_TYPE_RAW);
    dap_stream_pkt_hdr_mem_t l_hdr_mem = {0};
    memcpy(l_hdr_mem.sig, c_dap_stream_sig, sizeof(l_hdr_mem.sig));
    l_hdr_mem.size = (uint32_t)l_enc_sz;
    l_msg->data_size = DAP_STREAM_PKT_HDR_WIRE_SIZE + l_enc_sz;
    if (dap_stream_pkt_hdr_pack(&l_hdr_mem, l_d, l_msg->data_size) != 0) {
        log_it(L_ERROR, "dap_stream_pkt_write_mt: header pack failed");
        DAP_DEL_MULTY(l_msg->data, l_msg);
        return 0;
    }

    size_t l_ret = dap_events_socket_queue_ptr_send(a_w->queue_es_io, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't send msg to queue");
        DAP_DEL_MULTY(l_msg->data, l_msg);
        return 0;
    }
    return a_data_size;
#endif
}

/**
 * @brief Send keepalive packet to keep connection alive
 * @param a_stream Stream instance
 */
void dap_stream_send_keepalive(dap_stream_t *a_stream)
{
    if (!a_stream) return;
    dap_stream_pkt_write_unsafe(a_stream, STREAM_PKT_TYPE_KEEPALIVE, NULL, 0);
}
