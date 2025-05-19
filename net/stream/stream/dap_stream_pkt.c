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
#include "dap_enc_base64.h"

#define LOG_TAG "stream_pkt"

//const uint8_t c_dap_stream_sig [STREAM_PKT_SIG_SIZE] = {0xa0,0x95,0x96,0xa9,0x9e,0x5c,0xfb,0xfa};
const uint8_t c_dap_stream_sig [STREAM_PKT_SIG_SIZE] = {0xcb,0xa6,0x38,0x12,0xef,0x1a,0x02,0xd7};

/**
 * @brief stream_pkt_read
 * @param sid
 * @param pkt
 * @param buf_out
 */
size_t dap_stream_pkt_read_unsafe( dap_stream_t * a_stream, dap_stream_pkt_t * a_pkt, void * a_buf_out, size_t a_buf_out_size)
{
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
    dap_enc_key_t *l_key = a_stream->session->key;
    size_t l_full_size = dap_enc_key_get_enc_size(l_key->type, a_data_size) + sizeof(dap_stream_pkt_hdr_t);

    dap_stream_pkt_t *l_pkt = l_full_size <= sizeof(s_pkt_buf) ? (dap_stream_pkt_t *)s_pkt_buf : DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_stream_pkt_t, l_full_size, 0);
    *l_pkt = (dap_stream_pkt_t) {
            .hdr = (dap_stream_pkt_hdr_t) {
                    .size = dap_enc_code( l_key, a_data, a_data_size, l_pkt + sizeof(*l_pkt),
                                          l_full_size - sizeof(*l_pkt), DAP_ENC_DATA_TYPE_RAW ),
                    .timestamp = dap_time_now(),
                    .type = a_type,
                    .src_addr = g_node_addr.uint64,
                    .dst_addr = a_stream->node.uint64
            }
    };

    memcpy(l_pkt->hdr.sig, c_dap_stream_sig, STREAM_PKT_SIG_SIZE);
    //size_t ret = dap_events_socket_write_unsafe(a_stream->esocket, l_pkt, l_full_size);

    char *l_b64_buf = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(char, DAP_ENC_BASE64_ENCODE_SIZE(l_full_size), 0);
    size_t l_b64_size = dap_enc_base64_encode(l_pkt, l_full_size, l_b64_buf, DAP_ENC_DATA_TYPE_B64);
    if (l_full_size > sizeof(s_pkt_buf))
        DAP_DELETE(l_pkt);
    size_t ret = dap_events_socket_write_unsafe(a_stream->esocket, l_b64_buf, l_b64_size);
    DAP_DELETE(l_b64_buf);
    return ret;
}
