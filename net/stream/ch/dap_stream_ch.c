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
#include "dap_net.h"
#include "dap_strfuncs.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_context.h"
#include "dap_http_client.h"
#include "dap_uuid.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream_ch_gossip.h"
#include "dap_stream_worker.h"
#include <pthread.h>

#define LOG_TAG "dap_stream_ch"

#ifdef  DAP_SYS_DEBUG
enum    {MEMSTAT$K_STM_CH, MEMSTAT$K_NR};
static  dap_memstat_rec_t   s_memstat [MEMSTAT$K_NR] = {
    {.fac_len = sizeof(LOG_TAG) - 1, .fac_name = {LOG_TAG}, .alloc_sz = sizeof(dap_stream_ch_t)},
};
#endif

static bool s_debug_more = false;

/**
 * @brief dap_stream_ch_init Init stream channel module
 * @return Zero if ok others if no
 */
int dap_stream_ch_init()
{
    if(stream_ch_proc_init() != 0 ){
        log_it(L_CRITICAL,"Can't init stream channel proc submodule");
        return -1;
    }
    if(dap_stream_ch_pkt_init() != 0 ){
        log_it(L_CRITICAL,"Can't init stream channel packet submodule");
        return -1;
    }
    if (dap_stream_ch_gossip_init() != 0) {
        log_it(L_CRITICAL,"Can't init stream gossip channel");
        return -1;
    }
#ifdef  DAP_SYS_DEBUG
    for (int i = 0; i < MEMSTAT$K_NR; i++)
        dap_memstat_reg(&s_memstat[i]);
#endif
    s_debug_more = dap_config_get_item_bool_default(g_config, "stream", "debug_channels", false);
    log_it(L_NOTICE,"Module stream channel initialized");
    return 0;
}

/**
 * @brief dap_stream_ch_deinit Destroy stream channel submodule
 */
void dap_stream_ch_deinit()
{
}

#ifdef  DAP_SYS_DEBUG
typedef struct __dap_stm_ch_rec__ {
    dap_stream_ch_t     *stm_ch;
    UT_hash_handle          hh;
} dap_stm_ch_rec_t;

static dap_stm_ch_rec_t     *s_stm_chs = NULL;                          /* @RRL:  A has table to track using of events sockets context */
static pthread_rwlock_t     s_stm_ch_lock = PTHREAD_RWLOCK_INITIALIZER;
#endif

/*
 *   DESCRIPTION: Allocate a new <dap_stream_ch_t> context, add record into the hash table to track usage
 *      of the contexts.
 *
 *   INPUTS:
 *      NONE
 *
 *   IMPLICITE INPUTS:
 *      s_stm_chs;      A hash table
 *
 *   OUTPUTS:
 *      NONE
 *
 *   IMPLICITE OUTPUTS:
 *      s_stm_chs
 *
 *   RETURNS:
 *      non-NULL        A has been allocated <dap_events_socket> context
 *      NULL:           See <errno>
 */
static inline dap_stream_ch_t *dap_stream_ch_alloc (void)
{
dap_stream_ch_t *l_stm_ch;
if ( !(l_stm_ch = DAP_NEW_Z( dap_stream_ch_t )) )                       /* Allocate memory for new dap_events_socket context and the record */
    return  log_it(L_CRITICAL, "Cannot allocate memory for <dap_stream_ch_t> context, errno=%d", errno), NULL;
#ifdef DAP_SYS_DEBUG
int     l_rc;
dap_stm_ch_rec_t    *l_rec;
    if ( !(l_rec = DAP_NEW_Z( dap_stm_ch_rec_t )) )                         /* Allocate memory for new record */
        return  log_it(L_CRITICAL, "Cannot allocate memory for record, errno=%d", errno),
                DAP_DELETE(l_stm_ch), NULL;

    l_rec->stm_ch = l_stm_ch;                                               /* Fill new track record */

                                                                            /* Add new record into the hash table */
    l_rc = pthread_rwlock_wrlock(&s_stm_ch_lock);
    assert(!l_rc);
    HASH_ADD_PTR(s_stm_chs, stm_ch, l_rec);

    s_memstat[MEMSTAT$K_STM_CH].alloc_nr += 1;
    l_rc = pthread_rwlock_unlock(&s_stm_ch_lock);
    assert(!l_rc);
#ifndef DAP_DEBUG
    UNUSED(l_rc);
#endif
#endif
    debug_if(g_debug_reactor, L_NOTICE, "dap_stream_ch_t:%p - is allocated", l_stm_ch);
    return  l_stm_ch;
}

/*
 *   DESCRIPTION: Release has been allocated <dap_stream_ch_t>. Check firstly against hash table.
 *
 *   INPUTS:
 *      a_stm_ch:   A context to be released
 *
 *   IMPLICITE INPUTS:
 *      s_stm_chs;      A hash table
 *
 *   OUTPUT:
 *      NONE
 *
 *   IMPLICITE OUTPUTS:
 *      s_stm_chs
 *
 *   RETURNS:
 *      0:          a_es contains valid pointer
 *      <errno>
 */
static inline int dap_stm_ch_free (
                    dap_stream_ch_t *a_stm_ch
                        )
{
#ifdef DAP_SYS_DEBUG
int     l_rc;
dap_stm_ch_rec_t    *l_rec = NULL;

    l_rc = pthread_rwlock_wrlock(&s_stm_ch_lock);
    assert(!l_rc);
    HASH_FIND_PTR(s_stm_chs, &a_stm_ch, l_rec);
    if ( l_rec && (l_rec->stm_ch == a_stm_ch) )
        HASH_DEL(s_stm_chs, l_rec);                           /* Remove record from the table */

    atomic_fetch_add(&s_memstat[MEMSTAT$K_STM_CH].free_nr, 1);

    l_rc = pthread_rwlock_unlock(&s_stm_ch_lock);
    assert(!l_rc);
#ifndef DAP_DEBUG
    UNUSED(l_rc);
#endif

    if ( !l_rec )
        log_it(L_ERROR, "dap_stream_ch_t:%p - no record found!", a_stm_ch);
    else {
        dap_list_free_full(a_stm_ch->packet_in_notifiers, NULL);
        dap_list_free_full(a_stm_ch->packet_out_notifiers, NULL);
        DAP_DEL_MULTY(l_rec->stm_ch, l_rec);
        debug_if(g_debug_reactor, L_NOTICE, "dap_stream_ch_t:%p - is released", a_stm_ch);
    }
#else
    dap_list_free_full(a_stm_ch->packet_in_notifiers, NULL);
    dap_list_free_full(a_stm_ch->packet_out_notifiers, NULL);
    DAP_DELETE(a_stm_ch);
#endif
    return  0;  /* SS$_SUCCESS */
}

// Deferred free to avoid use-after-free during notifier callbacks
static void s_stream_ch_free_callback(void *a_arg)
{
    dap_stream_ch_t *l_ch = (dap_stream_ch_t *)a_arg;
    if (l_ch)
        dap_stm_ch_free(l_ch);
}

unsigned int dap_new_stream_ch_id() {
    static _Atomic unsigned int stream_ch_id = 0;
    return stream_ch_id++;
}

/**
 * @brief dap_stream_ch_new Creates new stream channel instance
 * @return
 */
dap_stream_ch_t* dap_stream_ch_new(dap_stream_t* a_stream, uint8_t a_id)
{
    dap_stream_ch_proc_t * proc=dap_stream_ch_proc_find(a_id);
    if(proc){
        dap_stream_ch_t **l_channels = DAP_REALLOC_COUNT_RET_VAL_IF_FAIL(a_stream->channel, a_stream->channel_count + 1, NULL);
        a_stream->channel = l_channels;
        dap_stream_ch_t* l_ch_new = dap_stream_ch_alloc();

        l_ch_new->me = l_ch_new;
        l_ch_new->stream = a_stream;
        l_ch_new->proc = proc;
        l_ch_new->ready_to_read = true;
        l_ch_new->closing = false;
        l_ch_new->uuid = dap_new_stream_ch_id();
        pthread_mutex_init(&(l_ch_new->mutex),NULL);

        // Init on stream worker
        dap_stream_worker_t * l_stream_worker = a_stream->stream_worker;
        if (!l_stream_worker) {
            log_it(L_ERROR, "stream_worker is NULL for stream %p, cannot create channel", (void*)a_stream);
            dap_stm_ch_free(l_ch_new);
            return NULL;
        }
        l_ch_new->stream_worker = l_stream_worker;

        pthread_rwlock_wrlock(&l_stream_worker->channels_rwlock);
        HASH_ADD_BYHASHVALUE(hh_worker,l_stream_worker->channels, uuid, sizeof (l_ch_new->uuid), l_ch_new->uuid, l_ch_new);
        pthread_rwlock_unlock(&l_stream_worker->channels_rwlock);


        // Proc new callback
        if(l_ch_new->proc->new_callback) {
            debug_if(s_debug_more, L_DEBUG, "Calling new_callback for channel '%c' (proc=%p, callback=%p)", 
                   a_id, (void*)l_ch_new->proc, (void*)l_ch_new->proc->new_callback);
            l_ch_new->proc->new_callback(l_ch_new,NULL);
            debug_if(s_debug_more, L_DEBUG, "new_callback for channel '%c' completed", a_id);
        } else {
            debug_if(s_debug_more, L_DEBUG, "No new_callback for channel '%c'", a_id);
        }

        a_stream->channel[a_stream->channel_count++] = l_ch_new;
        debug_if(s_debug_more, L_DEBUG, "Channel '%c' added to stream, total channels=%zu", a_id, a_stream->channel_count);

        return l_ch_new;
    }else{
        log_it(L_WARNING, "Unknown stream processor with id %uc", a_id);
        return NULL;
    }
}

/**
 * @brief stream_ch_delete Delete channel instance
 * @param ch Channel delete
 */
void dap_stream_ch_delete(dap_stream_ch_t *a_ch)
{
    dap_stream_worker_t * l_stream_worker = a_ch->stream_worker;

    if(l_stream_worker){
        pthread_rwlock_wrlock(&l_stream_worker->channels_rwlock);
        HASH_DELETE(hh_worker,l_stream_worker->channels, a_ch);
        pthread_rwlock_unlock(&l_stream_worker->channels_rwlock);
    }

    pthread_mutex_lock(&a_ch->mutex);
    a_ch->closing = true;
    if (a_ch->proc)
        if (a_ch->proc->delete_callback)
            a_ch->proc->delete_callback(a_ch, NULL);
    assert(!a_ch->internal);

    size_t l_ch_index = 0;
    for (; l_ch_index < a_ch->stream->channel_count; l_ch_index++)
        if (a_ch->stream->channel[l_ch_index] == a_ch)
            break;
    assert(l_ch_index < a_ch->stream->channel_count);
    a_ch->stream->channel_count--;
    // Channels shift for escape void channels in array
    for (size_t i = l_ch_index; i < a_ch->stream->channel_count; i++)
        a_ch->stream->channel[i] = a_ch->stream->channel[i + 1];
    if (!a_ch->stream->channel_count)
        DAP_DEL_Z(a_ch->stream->channel);

    pthread_mutex_unlock(&a_ch->mutex);

    // Defer actual free to worker's queue to avoid freeing while iterating notifiers
    if (l_stream_worker && l_stream_worker->worker)
        dap_worker_exec_callback_on(l_stream_worker->worker, s_stream_ch_free_callback, a_ch);
    else
        dap_stm_ch_free(a_ch);
}

/**
 * @brief Check ch uuid for presense in stream worker
 * @param a_worker
 * @param a_ch_uuid
 * @return
 */
dap_stream_ch_t *dap_stream_ch_find_by_uuid_unsafe(dap_stream_worker_t * a_worker, dap_stream_ch_uuid_t a_uuid)
{
    dap_stream_ch_t *l_ch = NULL;
    if( a_worker == NULL ){
        log_it(L_WARNING,"Attempt to search for uuid 0x%08x in NULL worker", a_uuid);
        return NULL;
    }

    pthread_rwlock_rdlock(&a_worker->channels_rwlock);
    if ( a_worker->channels)
        HASH_FIND_BYHASHVALUE(hh_worker,a_worker->channels, &a_uuid, sizeof(a_uuid), a_uuid, l_ch);
    pthread_rwlock_unlock(&a_worker->channels_rwlock);

    return l_ch;
}


/**
 * @brief dap_stream_ch_set_ready_to_read
 * @param a_ch
 * @param a_is_ready
 */
void dap_stream_ch_set_ready_to_read_unsafe(dap_stream_ch_t * a_ch,bool a_is_ready)
{
    if( a_ch->ready_to_read != a_is_ready){
        //log_it(L_DEBUG,"Change channel '%c' to %s", (char) ch->proc->id, is_ready?"true":"false");
        a_ch->ready_to_read=a_is_ready;
        dap_events_socket_set_readable_unsafe(a_ch->stream->esocket, a_is_ready);
    }
}

/**
 * @brief dap_stream_ch_set_ready_to_write
 * @param ch
 * @param is_ready
 */
void dap_stream_ch_set_ready_to_write_unsafe(dap_stream_ch_t * ch,bool is_ready)
{
    if(ch->ready_to_write!=is_ready){
        //log_it(L_DEBUG,"Change channel '%c' to %s", (char) ch->proc->id, is_ready?"true":"false");
        ch->ready_to_write=is_ready;
        dap_events_socket_set_writable_unsafe(ch->stream->esocket, is_ready);
    }
}

dap_stream_ch_t *dap_stream_ch_by_id_unsafe(dap_stream_t *a_stream, const char a_ch_id)
{
    dap_return_val_if_fail(a_stream, NULL);
    for (size_t i = 0; i < a_stream->channel_count; i++)
        if (a_stream->channel[i]->proc->id == a_ch_id)
            return a_stream->channel[i];
    return NULL;
}

/*
static void s_print_workers_channels()
{
    uint32_t l_worker_count = dap_events_thread_get_count();
    dap_stream_ch_t* l_msg_ch = NULL;
    dap_stream_ch_t* l_msg_ch_tmp = NULL;

    //print all worker connections
    dap_worker_print_all();
    for (uint32_t i = 0; i < l_worker_count; i++){
        uint32_t l_channel_count = 0;
        dap_worker_t* l_worker = dap_events_worker_get(i);
        if (!l_worker) {
            log_it(L_CRITICAL, "Can't get stream worker - worker thread don't exist");
            continue;
        }
        dap_stream_worker_t* l_stream_worker = DAP_STREAM_WORKER(l_worker);
        if (l_stream_worker->channels)
            HASH_ITER(hh_worker, l_stream_worker->channels, l_msg_ch, l_msg_ch_tmp) {
                //log_it(L_DEBUG, "Worker id = %d, channel uuid = 0x%llx", l_worker->id, l_msg_ch->uuid);
                l_channel_count += 1;
        }
        log_it(L_DEBUG, "Active workers l_channel_count = %d on worker %d", l_channel_count, l_stream_worker->worker->id);
    }
    return;
}
*/

static int s_notifiers_compare(dap_list_t *a_list1, dap_list_t *a_list2)
{
    dap_stream_ch_notifier_t *l_notifier1 = a_list1->data,
                             *l_notifier2 = a_list2->data;
    return l_notifier1->callback != l_notifier2->callback ||
                l_notifier1->arg != l_notifier2->arg;
}

struct place_notifier_arg {
    dap_events_socket_uuid_t es_uuid;
    uint8_t ch_id;
    dap_stream_packet_direction_t direction;
    dap_stream_ch_notify_callback_t callback;
    void *callback_arg;
    bool add;
};

static void s_place_notifier_callback(void *a_arg)
{
    struct place_notifier_arg *l_arg = a_arg;
    assert(l_arg);
    // Check if it was removed from the list
    dap_worker_t *l_worker = dap_worker_get_current();
    if (!l_worker) {
        log_it(L_ERROR, "l_worker is NULL");
        DAP_DELETE(l_arg);
        return;
    }
    dap_events_socket_t *l_es = dap_context_find(l_worker->context, l_arg->es_uuid);
    if (!l_es) {
        log_it(L_DEBUG, "We got place notifier request for client thats now not in list");
        goto ret_n_clear;
    }
    dap_stream_t *l_stream = dap_stream_get_from_es(l_es);
    if (!l_stream) {
        log_it(L_ERROR, "No stream found by events socket descriptor "DAP_FORMAT_ESOCKET_UUID, l_es->uuid);
        goto ret_n_clear;
    }
    debug_if(s_debug_more, L_DEBUG, "s_place_notifier_callback: stream=%p, channel_count=%zu, ch_id='%c'", 
           (void*)l_stream, l_stream->channel_count, l_arg->ch_id);
    if (l_stream->channel_count > 0 && l_stream->channel) {
        for (size_t i = 0; i < l_stream->channel_count; i++) {
            if (l_stream->channel[i] && l_stream->channel[i]->proc) {
                debug_if(s_debug_more, L_DEBUG, "  channel[%zu]: id='%c', proc=%p", i, l_stream->channel[i]->proc->id, (void*)l_stream->channel[i]->proc);
            } else {
                debug_if(s_debug_more, L_DEBUG, "  channel[%zu]: NULL or no proc", i);
            }
        }
    } else {
        debug_if(s_debug_more, L_DEBUG, "  stream has no channels (channel_count=%zu, channel=%p)", 
               l_stream->channel_count, (void*)l_stream->channel);
    }
    dap_stream_ch_t *l_ch = dap_stream_ch_by_id_unsafe(l_stream, l_arg->ch_id);
    if (!l_ch) {
        log_it(L_WARNING, "Stream found, but channel '%c' isn't set", l_arg->ch_id);
        goto ret_n_clear;
    }
    dap_list_t *l_notifiers_list = l_arg->direction == DAP_STREAM_PKT_DIR_IN ? l_ch->packet_in_notifiers
                                                                             : l_ch->packet_out_notifiers;
    dap_stream_ch_notifier_t l_notifier = { .callback = l_arg->callback, .arg = l_arg->callback_arg };
    dap_list_t *l_exist = dap_list_find(l_notifiers_list, &l_notifier, s_notifiers_compare);
    if (l_exist) {
        if (l_arg->add) {
            log_it(L_WARNING, "Notifier already exists for channel '%c' callback %p and arg %p",
                                    l_arg->ch_id, l_notifier.callback, l_notifier.arg);
            goto ret_n_clear;
        } else {
            debug_if(s_debug_more, L_DEBUG, "Notifier deleted for channel '%c' callback %p and arg %p",
                                    l_arg->ch_id, l_notifier.callback, l_notifier.arg);
            DAP_DELETE(l_exist->data);
            l_notifiers_list = dap_list_delete_link(l_notifiers_list, l_exist);
        }
    } else {
        if (l_arg->add) {
            dap_stream_ch_notifier_t *l_to_add = DAP_DUP(&l_notifier);
            if (!l_to_add) {
                log_it(L_CRITICAL, "Not enough memory");
                goto ret_n_clear;
            }
            l_notifiers_list = dap_list_append(l_notifiers_list, l_to_add);
            debug_if(s_debug_more, L_DEBUG, "Notifier added for channel '%c' callback %p and arg %p",
                                    l_arg->ch_id, l_notifier.callback, l_notifier.arg);
        } else {
            log_it(L_WARNING, "Notifier for channel '%c' callback %p and arg %p not found",
                                    l_arg->ch_id, l_notifier.callback, l_notifier.arg);
            goto ret_n_clear;
        }
    }
    if (l_arg->direction == DAP_STREAM_PKT_DIR_IN)
        l_ch->packet_in_notifiers = l_notifiers_list;
    else
        l_ch->packet_out_notifiers = l_notifiers_list;
ret_n_clear:
    DAP_DELETE(l_arg);
}

static int s_stream_ch_place_notifier(dap_stream_node_addr_t *a_stream_addr, uint8_t a_ch_id,
                                      dap_stream_packet_direction_t a_direction, dap_stream_ch_notify_callback_t a_callback,
                                      void *a_callback_arg, bool a_add)
{
    dap_worker_t *l_worker = NULL;
    dap_events_socket_uuid_t l_uuid = dap_stream_find_by_addr(a_stream_addr, &l_worker);
    if (!l_worker || !l_uuid)
        return -1;
    struct place_notifier_arg *l_arg = DAP_NEW(struct place_notifier_arg);
    *l_arg = (struct place_notifier_arg) { .es_uuid = l_uuid, .ch_id = a_ch_id, .direction = a_direction,
                                           .callback = a_callback, .callback_arg = a_callback_arg, .add = a_add };
    dap_worker_exec_callback_on(l_worker, s_place_notifier_callback, l_arg);
    return 0;
}

int dap_stream_ch_add_notifier(dap_stream_node_addr_t *a_stream_addr, uint8_t a_ch_id,
                             dap_stream_packet_direction_t a_direction, dap_stream_ch_notify_callback_t a_callback,
                             void *a_callback_arg)
{
    return s_stream_ch_place_notifier(a_stream_addr, a_ch_id, a_direction, a_callback, a_callback_arg, true);
}

int dap_stream_ch_del_notifier(dap_stream_node_addr_t *a_stream_addr, uint8_t a_ch_id,
                             dap_stream_packet_direction_t a_direction, dap_stream_ch_notify_callback_t a_callback,
                             void *a_callback_arg)
{
    return s_stream_ch_place_notifier(a_stream_addr, a_ch_id, a_direction, a_callback, a_callback_arg, false);
}

/**
 * @brief Get worker for a channel by UUID (MT-safe)
 * @param a_ch_uuid Channel UUID
 * @return Pointer to worker or NULL if channel not found
 * @note This function searches through all stream workers to find the channel
 *       Uses proper locking for thread safety
 */
dap_worker_t *dap_stream_ch_get_worker_mt(dap_stream_ch_uuid_t a_ch_uuid)
{
    uint32_t l_worker_count = dap_events_thread_get_count();
    
    // Search through all workers with proper locking
    for (uint32_t i = 0; i < l_worker_count; i++) {
        dap_worker_t *l_worker = dap_events_worker_get(i);
        if (!l_worker) {
            continue;
        }
        
        // Get stream worker from worker
        dap_stream_worker_t *l_stream_worker = DAP_STREAM_WORKER(l_worker);
        if (!l_stream_worker) {
            continue;
        }
        
        // Lock channels for read access (MT-safe)
        pthread_rwlock_rdlock(&l_stream_worker->channels_rwlock);
        
        // Search through channels hash table
        dap_stream_ch_t *l_ch = NULL;
        dap_stream_ch_t *l_ch_iter = NULL;
        dap_stream_ch_t *l_ch_tmp = NULL;
        
        // Iterate through UTHASH table
        HASH_ITER(hh_worker, l_stream_worker->channels, l_ch_iter, l_ch_tmp) {
            if (memcmp(&l_ch_iter->uuid, &a_ch_uuid, sizeof(dap_stream_ch_uuid_t)) == 0) {
                l_ch = l_ch_iter;
                break;
            }
        }
        
        pthread_rwlock_unlock(&l_stream_worker->channels_rwlock);
        
        // If found, return this worker
        if (l_ch) {
            return l_worker;
        }
    }
    
    return NULL;
}
