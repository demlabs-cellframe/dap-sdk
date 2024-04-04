/*
 Copyright (c) 2017-2018 (c) Project "DeM Labs Inc" https://github.com/demlabsinc
  All rights reserved.

 This file is part of DAP (Demlabs Application Protocol) the open source project

    DAP (Demlabs Application Protocol) is free software: you can redistribute it and/or modify
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
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#include <pthread.h>
#endif

#include "dap_common.h"
//#include "config.h"


#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_http_client.h"

#include "dap_enc.h"
#include "dap_enc_key.h"

#include "dap_stream.h"
#include "dap_stream_pkt.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_pkt.h"


#include "dap_enc_iaes.h"

#define LOG_TAG "stream_pkt"

const uint8_t c_dap_stream_sig [STREAM_PKT_SIG_SIZE] = {0xa0,0x95,0x96,0xa9,0x9e,0x5c,0xfb,0xfa};

dap_stream_pkt_t * dap_stream_pkt_detect(void * a_data, size_t data_size)
{
    uint8_t * sig_start=(uint8_t*) a_data;
    dap_stream_pkt_t * hpkt = NULL;
    size_t length_left = data_size;

    while ( (sig_start = memchr(sig_start, c_dap_stream_sig[0], length_left)) ) {
        length_left = data_size - (size_t)(sig_start - (uint8_t *)a_data);
        if(length_left < sizeof(c_dap_stream_sig) )
            break;

        if ( !memcmp(sig_start, c_dap_stream_sig, sizeof(c_dap_stream_sig)) ) {
            hpkt = (dap_stream_pkt_t *)sig_start;
            if (length_left < sizeof(dap_stream_ch_pkt_hdr_t)) {
                //log_it(L_ERROR, "Too small packet size %zu", length_left); // it's not an error, just random case
                hpkt = NULL;
                break;
            }
            if(hpkt->hdr.size > DAP_STREAM_PKT_SIZE_MAX ){
                log_it(L_ERROR, "Too big packet size %u (%#x), type:%d(%#x)",
                       hpkt->hdr.size, hpkt->hdr.size, hpkt->hdr.type, hpkt->hdr.type);
                hpkt = NULL;
            }
            break;
        } else
            sig_start++;
    }

    return hpkt;
}

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
    size_t l_full_size = dap_enc_key_get_enc_size(l_key, a_data_size) + sizeof(dap_stream_pkt_hdr_t);
    dap_stream_pkt_hdr_t *l_pkt_hdr = (dap_stream_pkt_hdr_t*)s_pkt_buf;
    *l_pkt_hdr = (dap_stream_pkt_hdr_t) { .size = dap_enc_code( l_key, a_data, a_data_size, s_pkt_buf + sizeof(*l_pkt_hdr),
                                                                l_full_size - sizeof(*l_pkt_hdr), DAP_ENC_DATA_TYPE_RAW ),
                                          .type = a_type,
                                          .timestamp = dap_time_now(),
                                          .src_addr = g_node_addr.uint64,
                                          .dst_addr = a_stream->node.uint64 };
    memcpy(l_pkt_hdr->sig, c_dap_stream_sig, sizeof(l_pkt_hdr->sig));
    return dap_events_socket_write_unsafe(a_stream->esocket, s_pkt_buf, l_full_size);
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
    size_t l_full_size = dap_enc_key_get_enc_size(a_key, a_data_size) + sizeof(dap_stream_pkt_hdr_t);
    dap_stream_pkt_hdr_t *l_pkt_hdr = (dap_stream_pkt_hdr_t*)s_pkt_buf;
    *l_pkt_hdr = (dap_stream_pkt_hdr_t) { .size = dap_enc_code( a_key, a_data, a_data_size, s_pkt_buf + sizeof(*l_pkt_hdr),
                                                                l_full_size - sizeof(*l_pkt_hdr), DAP_ENC_DATA_TYPE_RAW ) };
    memcpy(l_pkt_hdr->sig, c_dap_stream_sig, sizeof(l_pkt_hdr->sig));
    return dap_events_socket_write_mt(a_w, a_es_uuid, s_pkt_buf, l_full_size);
#else
    dap_worker_msg_io_t * l_msg = DAP_NEW_Z(dap_worker_msg_io_t);
    if (!l_msg) {
        log_it(L_CRITICAL, "%s", g_error_memory_alloc);
        return 0;
    }
    dap_stream_pkt_hdr_t *l_pkt_hdr;
    l_msg->esocket_uuid = a_es_uuid;
    l_msg->data_size = 16-a_data_size%16+a_data_size+sizeof(*l_pkt_hdr);
    l_msg->data = DAP_NEW_SIZE(void,l_msg->data_size);
    if (!l_msg) {
        log_it(L_CRITICAL, "%s", g_error_memory_alloc);
        DAP_DEL_Z(l_msg);
        return 0;
    }
    l_pkt_hdr=(dap_stream_pkt_hdr_t*) l_msg->data;
    memset(l_pkt_hdr,0,sizeof(*l_pkt_hdr));
    memcpy(l_pkt_hdr->sig,c_dap_stream_sig,sizeof(l_pkt_hdr->sig));
    l_msg->data_size=sizeof (*l_pkt_hdr) +dap_enc_code(a_key, a_data,a_data_size, ((byte_t*)l_msg->data)+sizeof (*l_pkt_hdr),l_msg->data_size-sizeof (*l_pkt_hdr),DAP_ENC_DATA_TYPE_RAW);

    int l_ret= dap_events_socket_queue_ptr_send(a_w->queue_es_io, l_msg );
    if (l_ret!=0){
        log_it(L_ERROR, "Wasn't send pointer to queue: code %d", l_ret);
        DAP_DELETE(l_msg->data);
        DAP_DELETE(l_msg);
        return 0;
    }
    return a_data_size;
#endif
}

