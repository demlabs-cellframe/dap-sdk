/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2020
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
#include "dap_common.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_context.h"
#include "dap_stream_worker.h"
#include "dap_stream_ch_pkt.h"

#define LOG_TAG "dap_stream_worker"

static void s_ch_io_callback(dap_events_socket_t * a_es, void * a_msg);
static void s_ch_send_callback(dap_events_socket_t *a_es, void *a_msg);
static size_t s_cb_msg_buf_clean(char *a_buf_out, size_t a_buf_size) ;


/**
 * @brief dap_stream_worker_init
 * @return
 */
int dap_stream_worker_init()
{
    uint32_t l_worker_count = dap_events_thread_get_count();
    for (uint32_t i = 0; i < l_worker_count; i++){
        dap_worker_t * l_worker = dap_events_worker_get(i);
        if (!l_worker) {
            log_it(L_CRITICAL,"Can't init stream worker,- worker thread don't exist");
            return -2;
        }
        if (l_worker->_inheritor){
            log_it(L_CRITICAL,"Can't init stream worker,- core worker has already inheritor");
            return -1;
        }
        dap_stream_worker_t *l_stream_worker =  DAP_NEW_Z(dap_stream_worker_t);
        if(!l_stream_worker)
            return -5;
        l_worker->_inheritor = l_stream_worker;
        l_stream_worker->worker = l_worker;
        pthread_rwlock_init( &l_stream_worker->channels_rwlock, NULL);

        l_stream_worker->queue_ch_io = dap_events_socket_create_type_queue_ptr( l_worker, s_ch_io_callback);
        if(! l_stream_worker->queue_ch_io)
            return -6;
        l_stream_worker->queue_ch_send = dap_events_socket_create_type_queue_ptr(l_worker, s_ch_send_callback);
        l_stream_worker->queue_ch_send->cb_buf_cleaner = s_cb_msg_buf_clean; 
        if (!l_stream_worker->queue_ch_send)
            return -7;
    }
    return 0;
}

/**
 * @brief s_ch_io_callback
 * @param a_es
 * @param a_msg
 */
static void s_ch_io_callback(dap_events_socket_t * a_es, void * a_msg)
{
    dap_stream_worker_t * l_stream_worker = DAP_STREAM_WORKER( a_es->worker );
    dap_stream_worker_msg_io_t * l_msg = (dap_stream_worker_msg_io_t*) a_msg;

    assert(l_msg);
    // Check if it was removed from the list
    dap_stream_ch_t *l_msg_ch = NULL;
    pthread_rwlock_rdlock(&l_stream_worker->channels_rwlock);
    HASH_FIND_BYHASHVALUE(hh_worker, l_stream_worker->channels , &l_msg->ch_uuid , sizeof (l_msg->ch_uuid), l_msg->ch_uuid, l_msg_ch );
    pthread_rwlock_unlock(&l_stream_worker->channels_rwlock);
    if (l_msg_ch == NULL) {
        if (l_msg->data_size) {
            log_it(L_DEBUG, "We got i/o message for client thats now not in list. Lost %zu data", l_msg->data_size);
            DAP_DELETE(l_msg->data);
        }
        DAP_DELETE(l_msg);
        return;
    }

    if (l_msg->flags_set & DAP_SOCK_READY_TO_READ)
        dap_stream_ch_set_ready_to_read_unsafe(l_msg_ch, true);
    if (l_msg->flags_unset & DAP_SOCK_READY_TO_READ)
        dap_stream_ch_set_ready_to_read_unsafe(l_msg_ch, false);
    if (l_msg->flags_set & DAP_SOCK_READY_TO_WRITE)
        dap_stream_ch_set_ready_to_write_unsafe(l_msg_ch, true);
    if (l_msg->flags_unset & DAP_SOCK_READY_TO_WRITE)
        dap_stream_ch_set_ready_to_write_unsafe(l_msg_ch, false);
    if (l_msg->data_size && l_msg->data) {
        dap_stream_ch_pkt_write_unsafe(l_msg_ch, l_msg->ch_pkt_type, l_msg->data, l_msg->data_size);
        DAP_DELETE(l_msg->data);
    }
    DAP_DELETE(l_msg);
}

static void s_ch_send_callback(dap_events_socket_t *a_es, void *a_msg)
{
    dap_stream_worker_msg_send_t *l_msg = (dap_stream_worker_msg_send_t *)a_msg;
    assert(l_msg);
    // Check if it was removed from the list
    dap_events_socket_t *l_es = dap_context_find(a_es->context, l_msg->uuid);
    if (!l_es) {
        log_it(L_DEBUG, "We got i/o message for client thats now not in list");
        goto ret_n_clear;
    }
    dap_stream_t *l_stream = dap_stream_get_from_es(l_es);
    if (!l_stream) {
        log_it(L_ERROR, "No stream found by events socket descriptor "DAP_FORMAT_ESOCKET_UUID, l_es->uuid);
        goto ret_n_clear;
    }
    dap_stream_ch_t *l_ch = dap_stream_ch_by_id_unsafe(l_stream, l_msg->ch_id);
    if (!l_ch) {
        log_it(L_WARNING, "Stream found, but channel '%c' isn't set", l_msg->ch_id);
        goto ret_n_clear;
    }
    dap_stream_ch_pkt_write_unsafe(l_ch, l_msg->ch_pkt_type, l_msg->data, l_msg->data_size);
    DAP_DEL_Z(l_msg->data);
ret_n_clear:
    if (l_msg->data) {
        log_it(L_DEBUG, "Lost %zu data", l_msg->data_size);
        DAP_DELETE(l_msg->data);
    }
    DAP_DELETE(l_msg);
}

static size_t s_cb_msg_buf_clean(char *a_buf_out, size_t a_buf_size) 
{
    size_t l_total_size = 0;
    for (size_t shift = 0; shift < a_buf_size; shift += sizeof(dap_stream_worker_msg_send_t*)) {
        dap_stream_worker_msg_send_t* l_msg = *(dap_stream_worker_msg_send_t**)(a_buf_out + shift);
        l_total_size += l_msg->data_size;
        DAP_DELETE(l_msg->data);
        DAP_DELETE(l_msg);
    }
    return l_total_size;
}
