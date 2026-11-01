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
#include "dap_time.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_context.h"
#include "dap_context_queue.h"
#include "dap_stream_worker.h"
#include "dap_stream_ch_pkt.h"

#define LOG_TAG "dap_stream_worker"

static bool s_debug_more = false;
static void s_ch_io_callback(void * a_msg);
static void s_ch_send_callback(void *a_msg);


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

        l_stream_worker->queue_ch_io = dap_context_queue_create(l_worker->context, 4096, s_ch_io_callback);
        if(! l_stream_worker->queue_ch_io)
            return -6;
        l_stream_worker->queue_ch_send = dap_context_queue_create(l_worker->context, 4096, s_ch_send_callback);
        if (!l_stream_worker->queue_ch_send)
            return -7;
    }
#ifndef DAP_EVENTS_CAPS_IOCP
    for (uint32_t i = 0; i < l_worker_count; i++){
        dap_worker_t *l_worker_inp = dap_events_worker_get(i);
        dap_stream_worker_t *l_stream_worker_inp = (dap_stream_worker_t *)l_worker_inp->_inheritor;
        l_stream_worker_inp->queue_ch_io_input = DAP_NEW_Z_SIZE(dap_context_queue_t *, sizeof(dap_context_queue_t *) * l_worker_count);
        if (!l_stream_worker_inp->queue_ch_io_input) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return -8;
        }
        for (uint32_t j = 0; j < l_worker_count; j++) {
            dap_worker_t * l_worker = dap_events_worker_get(j);
            dap_stream_worker_t *l_stream_worker = (dap_stream_worker_t*) l_worker->_inheritor;
            // Simply reference the same queue - dap_context_queue handles cross-worker signaling internally
            l_stream_worker_inp->queue_ch_io_input[j] = l_stream_worker->queue_ch_io;
        }
    }
#endif
    return 0;
}

/**
 * @brief s_ch_io_callback
 * @param a_msg
 */
static void s_ch_io_callback(void * a_msg)
{
    int64_t l_t0 = dap_get_monotime_ms();
    dap_stream_worker_msg_io_t * l_msg = (dap_stream_worker_msg_io_t*) a_msg;
    assert(l_msg);

    dap_worker_t *l_worker = dap_worker_get_current();
    dap_stream_worker_t * l_stream_worker = DAP_STREAM_WORKER(l_worker);

    // Check if it was removed from the list
    dap_stream_ch_t *l_msg_ch = NULL;
    pthread_rwlock_rdlock(&l_stream_worker->channels_rwlock);
    HASH_FIND_BYHASHVALUE(hh_worker, l_stream_worker->channels , &l_msg->ch_uuid , sizeof (l_msg->ch_uuid), l_msg->ch_uuid, l_msg_ch );
    pthread_rwlock_unlock(&l_stream_worker->channels_rwlock);
    if (l_msg_ch == NULL) {
        if (l_msg->data_size) {
            log_it(L_WARNING, "Stream ch UUID %016lx not found in worker, dropping %zu bytes",
                   (unsigned long)l_msg->ch_uuid, l_msg->data_size);
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
        size_t l_ret = dap_stream_ch_pkt_write_unsafe(l_msg_ch, l_msg->ch_pkt_type, l_msg->data, l_msg->data_size);
        if (!l_ret) {
            /* Shed on congestion or channel teardown — must stay observable:
             * VPN inner TCP retransmits, control channels surface the loss. */
            static atomic_ullong s_shed_pkts;
            static atomic_llong s_shed_last_log_ms;
            unsigned long long l_total = atomic_fetch_add(&s_shed_pkts, 1) + 1;
            int64_t l_now = dap_get_monotime_ms();
            int64_t l_prev = atomic_load(&s_shed_last_log_ms);
            if (l_now - l_prev >= 5000 &&
                atomic_compare_exchange_strong(&s_shed_last_log_ms, &l_prev, l_now))
                log_it(L_WARNING, "Stream worker: channel pkt shed (egress congested/full), "
                       "total=%llu, last pkt %zu bytes", l_total, l_msg->data_size);
        }
        DAP_DELETE(l_msg->data);
    }
    DAP_DELETE(l_msg);

    /* Worker stall diagnostics: this thread pumps uploads, downloads and
     * keepalives alike — a single slow message (or a long queue wait) freezes
     * the whole tunnel. Log heavy messages and periodic queue depth. */
    static atomic_ullong s_msgs_processed;
    static atomic_ullong s_max_msg_ms;
    static atomic_llong s_stat_last_log_ms;
    int64_t l_dt = dap_get_monotime_ms() - l_t0;
    uint64_t l_prev_max = atomic_load(&s_max_msg_ms);
    if ((uint64_t)l_dt > l_prev_max)
        atomic_store(&s_max_msg_ms, (uint64_t)l_dt);
    uint64_t l_msgs = atomic_fetch_add(&s_msgs_processed, 1) + 1;
    if (l_dt >= 50) {
        static atomic_llong s_slow_last_log_ms;
        int64_t l_now = dap_get_monotime_ms();
        int64_t l_prev = atomic_load(&s_slow_last_log_ms);
        if (l_now - l_prev >= 2000 &&
            atomic_compare_exchange_strong(&s_slow_last_log_ms, &l_prev, l_now))
            log_it(L_WARNING, "Stream worker: single msg took %lld ms (queue depth %zu)",
                   (long long)l_dt, dap_context_queue_count(l_stream_worker->queue_ch_io));
    }
    int64_t l_now = dap_get_monotime_ms();
    int64_t l_prev_stat = atomic_load(&s_stat_last_log_ms);
    if (l_now - l_prev_stat >= 5000 &&
        atomic_compare_exchange_strong(&s_stat_last_log_ms, &l_prev_stat, l_now))
        log_it(L_INFO, "Stream worker: %llu msgs last 5s, queue depth %zu, slowest msg %llu ms",
               (unsigned long long)l_msgs,
               dap_context_queue_count(l_stream_worker->queue_ch_io),
               (unsigned long long)atomic_exchange(&s_max_msg_ms, 0));
}

static void s_ch_send_callback(void *a_msg)
{
    dap_stream_worker_msg_send_t *l_msg = (dap_stream_worker_msg_send_t *)a_msg;
    assert(l_msg);

    dap_worker_t *l_worker = dap_worker_get_current();
    dap_context_t *l_context = l_worker->context;

    // Check if it was removed from the list
    dap_events_socket_t *l_es = dap_context_find(l_context, l_msg->uuid);
    if (!l_es) {
        debug_if(s_debug_more, L_DEBUG, "We got i/o message for client thats now not in list");
        goto ret_n_clear;
    }
    dap_stream_t *l_stream = dap_stream_get_from_es(l_es);
    if (!l_stream) {
        log_it(L_DEBUG, "No stream found by events socket descriptor "DAP_FORMAT_ESOCKET_UUID" (stream teardown in progress)", l_es->uuid);
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
        debug_if(s_debug_more, L_DEBUG, "Lost %zu data", l_msg->data_size);
        DAP_DELETE(l_msg->data);
    }
    DAP_DELETE(l_msg);
}
