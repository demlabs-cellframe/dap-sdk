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
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_enc.h"
#include "dap_enc_key.h"
#include "dap_stream.h"
#include "dap_stream_pkt.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream_pkt.h"

#define LOG_TAG "stream_pkt"

const uint8_t c_dap_stream_sig [STREAM_PKT_SIG_SIZE] = {0xa0,0x95,0x96,0xa9,0x9e,0x5c,0xfb,0xfa};

/**
 * @brief stream_pkt_read
 * @param sid
 * @param pkt
 * @param buf_out
 */
size_t dap_stream_pkt_read_unsafe( dap_stream_t * a_stream, dap_stream_pkt_t * a_pkt, void * a_buf_out, size_t a_buf_out_size)
{
    // Handle unencrypted packet or missing session key
    if (!a_stream->session || !a_stream->session->key) {
        size_t l_copy_size = a_pkt->hdr.size > a_buf_out_size ? a_buf_out_size : a_pkt->hdr.size;
        memcpy(a_buf_out, a_pkt->data, l_copy_size);
        return l_copy_size;
    }
    return a_stream->session->key->dec_na(a_stream->session->key,a_pkt->data,a_pkt->hdr.size,a_buf_out, a_buf_out_size);
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
    if (a_data_size > DAP_STREAM_PKT_FRAGMENT_SIZE)
        return log_it(L_ERROR, "Too big fragment size %zu", a_data_size), 0;
    static _Thread_local char s_pkt_buf[DAP_STREAM_PKT_FRAGMENT_SIZE + sizeof(dap_stream_pkt_hdr_t) + 0x40] = { 0 };
    a_stream->is_active = true;
    
    dap_enc_key_t *l_key = (a_stream->session) ? a_stream->session->key : NULL;
    size_t l_full_size;
    
    if (l_key) {
        l_full_size = dap_enc_key_get_enc_size(l_key->type, a_data_size) + sizeof(dap_stream_pkt_hdr_t);
    } else {
        l_full_size = a_data_size + sizeof(dap_stream_pkt_hdr_t);
    }
    
    dap_stream_pkt_hdr_t *l_pkt_hdr = (dap_stream_pkt_hdr_t*)s_pkt_buf;
    
    if (l_key) {
    *l_pkt_hdr = (dap_stream_pkt_hdr_t) { .size = dap_enc_code( l_key, a_data, a_data_size, s_pkt_buf + sizeof(*l_pkt_hdr),
                                                                l_full_size - sizeof(*l_pkt_hdr), DAP_ENC_DATA_TYPE_RAW ),
                                          .timestamp = dap_time_now(), .type = a_type,
                                          .src_addr = g_node_addr.uint64, .dst_addr = a_stream->node.uint64 };
    } else {
        // No encryption
        memcpy(s_pkt_buf + sizeof(*l_pkt_hdr), a_data, a_data_size);
        *l_pkt_hdr = (dap_stream_pkt_hdr_t) { .size = (uint32_t)a_data_size,
                                              .timestamp = dap_time_now(), .type = a_type,
                                              .src_addr = g_node_addr.uint64, .dst_addr = a_stream->node.uint64 };
    }
    
    memcpy(l_pkt_hdr->sig, c_dap_stream_sig, sizeof(l_pkt_hdr->sig));
    if (a_stream->trans_ctx && a_stream->trans_ctx->esocket)
        return dap_events_socket_write_unsafe(a_stream->trans_ctx->esocket, s_pkt_buf, l_full_size);
    return 0;
}

/**
 * @brief dap_stream_pkt_write_mt
 * @param a_stream_session
 * @param a_es
 * @param a_data
 * @param a_data_size
 * @return
 */
size_t dap_stream_pkt_write_mt(dap_worker_t * a_w,dap_events_socket_uuid_t a_es_uuid, dap_enc_key_t *a_key, const void * a_data, size_t a_data_size)
{
#ifdef DAP_EVENTS_CAPS_IOCP
    static _Thread_local char s_pkt_buf[DAP_STREAM_PKT_FRAGMENT_SIZE + sizeof(dap_stream_pkt_hdr_t) + 0x40] = { 0 };
    size_t l_full_size = dap_enc_key_get_enc_size(a_key->type, a_data_size) + sizeof(dap_stream_pkt_hdr_t);
    dap_stream_pkt_hdr_t *l_pkt_hdr = (dap_stream_pkt_hdr_t*)s_pkt_buf;
    *l_pkt_hdr = (dap_stream_pkt_hdr_t) { .size = dap_enc_code( a_key, a_data, a_data_size, s_pkt_buf + sizeof(*l_pkt_hdr),
                                                                l_full_size - sizeof(*l_pkt_hdr), DAP_ENC_DATA_TYPE_RAW ) };
    memcpy(l_pkt_hdr->sig, c_dap_stream_sig, sizeof(l_pkt_hdr->sig));
    return dap_events_socket_write_mt(a_w, a_es_uuid, s_pkt_buf, l_full_size);
#else
    dap_worker_msg_io_t *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_worker_msg_io_t, 0);
    l_msg->esocket_uuid = a_es_uuid;
    l_msg->data_size = dap_enc_code_out_size(a_key, a_data_size, DAP_ENC_DATA_TYPE_RAW);
    l_msg->data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(byte_t, l_msg->data_size, 0, l_msg);
    dap_stream_pkt_hdr_t *l_pkt_hdr = (dap_stream_pkt_hdr_t*)l_msg->data;
    memcpy(l_pkt_hdr->sig, c_dap_stream_sig, sizeof(l_pkt_hdr->sig));
    l_msg->data_size = sizeof(*l_pkt_hdr) + dap_enc_code(a_key, a_data, a_data_size,
        ((byte_t*)l_msg->data) + sizeof(*l_pkt_hdr), l_msg->data_size, DAP_ENC_DATA_TYPE_RAW);

    int l_ret = dap_events_socket_queue_ptr_send(a_w->queue_es_io, l_msg);
    return l_ret
        ? log_it(L_ERROR, "Can't send msg to queue %d, error %d", a_w->queue_es_io->fd, l_ret), DAP_DEL_MULTY(l_msg->data, l_msg), 0
        : a_data_size;
#endif
}

