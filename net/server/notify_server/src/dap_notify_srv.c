/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2021
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

#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include "dap_events_socket.h"
#include "dap_common.h"
#include "dap_config.h"
#include "dap_list.h"
#include "dap_strfuncs.h"
#include "dap_server.h"
#include "dap_events.h"
#include "dap_notify_srv.h"
#include "dap_proc_thread.h"

#define LOG_TAG "notify_server"


dap_server_t * s_notify_server = NULL;

dap_events_socket_handler_hh_t * s_notify_server_clients = NULL;
pthread_rwlock_t s_notify_server_clients_mutex = PTHREAD_RWLOCK_INITIALIZER;

static bool s_notify_server_callback_queue(void * a_arg);
static void s_notify_server_callback_new(dap_events_socket_t * a_es, void * a_arg);
static void s_notify_server_callback_delete(dap_events_socket_t * a_es, void * a_arg);
static dap_notify_data_user_callback_t s_notify_data_user_callback = NULL;
dap_events_socket_callback_t s_notify_server_callback_new_ex = NULL;

void dap_notify_data_set_user_callback(dap_notify_data_user_callback_t callback)
{
    s_notify_data_user_callback = callback;
}


void dap_notify_srv_set_callback_new(dap_events_socket_callback_t a_cb) {
    s_notify_server_callback_new_ex = a_cb;
}

/**
 * @brief dap_notify_server_init
 * @param a_notify_socket_path
 * @return
 */
int dap_notify_server_init()
{
    dap_events_socket_callbacks_t l_client_callbacks = {
        .new_callback = s_notify_server_callback_new,
        .delete_callback = s_notify_server_callback_delete
    };
    s_notify_server = dap_server_new("notify_server", NULL, &l_client_callbacks);
    return s_notify_server
        ? ( log_it(L_INFO,"Notify server initalized"), 0 )
        : ( log_it(L_WARNING, "Notify server not initalized"), -1 );
}

/**
 * @brief dap_notify_server_deinit
 */
void dap_notify_server_deinit()
{

}

void s_notify_server_broadcast(const char *a_data)
{
    size_t l_str_len = dap_strlen(a_data);
    if ( !l_str_len )
        return;
    if (s_notify_data_user_callback)
        s_notify_data_user_callback(a_data);
    pthread_rwlock_rdlock(&s_notify_server_clients_mutex);
    for (dap_events_socket_handler_hh_t *it = s_notify_server_clients; it; it = it->hh.next) {
        uint32_t l_worker_id = it->worker_id;
        if ( l_worker_id >= dap_events_thread_get_count() ) {
            log_it(L_ERROR, "Wrong worker id %u for interthread communication", l_worker_id);
            continue;
        }
        dap_events_socket_write(dap_events_worker_get(l_worker_id), it->uuid, 
#ifdef DAP_EVENTS_CAPS_IOCP
            a_data,
#else
            DAP_DUP_SIZE((char*)a_data, l_str_len + 1),
#endif        
            l_str_len + 1);
    }
    pthread_rwlock_unlock(&s_notify_server_clients_mutex);
}

/**
 * @brief dap_notify_server_send_fmt_mt
 * @param a_format
 * @return
 */
int dap_notify_server_send(const char *a_data)
{
    return dap_proc_thread_callback_add_pri(NULL, s_notify_server_callback_queue, dap_strdup(a_data), DAP_QUEUE_MSG_PRIORITY_LOW);
}


/**
 * @brief dap_notify_server_send_fmt_mt
 * @param a_format
 * @return
 */
int dap_notify_server_send_f(const char *a_format, ...)
{
    va_list ap, ap_copy;
    va_start(ap, a_format);
    va_copy(ap_copy, ap);
    ssize_t l_str_size = vsnprintf(NULL, 0, a_format, ap);
    va_end(ap);
    if (l_str_size < 0) {
        va_end(ap_copy);
        log_it(L_ERROR,"Can't write out formatted data '%s'", a_format);
        return l_str_size;
    }
    l_str_size++; // include trailing 0
    char *l_str = DAP_NEW_SIZE(char, l_str_size);
    if (!l_str) {
        va_end(ap_copy);
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return -1;
    }
    vsnprintf(l_str, l_str_size, a_format, ap_copy);
    va_end(ap_copy);
    
    return dap_proc_thread_callback_add_pri(NULL, s_notify_server_callback_queue, l_str, DAP_QUEUE_MSG_PRIORITY_LOW);
}

/**
 * @brief s_notify_server_inter_queue
 * @param a_es
 * @param a_arg
 */
static bool s_notify_server_callback_queue(void * a_arg)
{
    s_notify_server_broadcast(a_arg);
    DAP_DELETE(a_arg);
    return false;
}

/**
 * @brief s_notify_server_callback_new
 * @param a_es
 * @param a_arg
 */
static void s_notify_server_callback_new(dap_events_socket_t * a_es, UNUSED_ARG void *a_arg)
{
    dap_events_socket_handler_hh_t *l_hh_new = NULL;
    pthread_rwlock_wrlock(&s_notify_server_clients_mutex);
    HASH_FIND(hh,s_notify_server_clients, &a_es->uuid, sizeof (a_es->uuid), l_hh_new);
    if (l_hh_new){
        uint64_t *l_uuid_u64 =(uint64_t*) &a_es->uuid;
        log_it(L_WARNING,"Trying to add notify client with uuid 0x%016"DAP_UINT64_FORMAT_X" but already present this UUID in list, updating only esocket pointer if so", *l_uuid_u64);
        l_hh_new->esocket = a_es;
        l_hh_new->worker_id = a_es->worker->id;
    } else {
        if (!a_es->context || !a_es->worker) {
            log_it(L_ERROR, "Invalid esocket arg with uuid %"DAP_UINT64_FORMAT_U": broken context", a_es->uuid);
            pthread_rwlock_unlock(&s_notify_server_clients_mutex);
            return;
        }
        l_hh_new = DAP_NEW_Z(dap_events_socket_handler_hh_t);
        if (!l_hh_new) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            pthread_rwlock_unlock(&s_notify_server_clients_mutex);
            return;
        }
        l_hh_new->esocket = a_es;
        l_hh_new->uuid = a_es->uuid;
        l_hh_new->worker_id = a_es->worker->id;
        a_es->no_close = true;
        HASH_ADD(hh, s_notify_server_clients, uuid, sizeof (l_hh_new->uuid), l_hh_new);
    }
    pthread_rwlock_unlock(&s_notify_server_clients_mutex);
    if (s_notify_server_callback_new_ex)
        s_notify_server_callback_new_ex(a_es, NULL);
}

/**
 * @brief s_notify_server_callback_delete
 * @param a_es
 * @param a_arg
 */
static void s_notify_server_callback_delete(dap_events_socket_t * a_es, void * a_arg)
{
    (void) a_arg;
    dap_events_socket_handler_hh_t * l_hh_new = NULL;
    pthread_rwlock_wrlock(&s_notify_server_clients_mutex);
    HASH_FIND(hh,s_notify_server_clients, &a_es->uuid, sizeof (a_es->uuid), l_hh_new);
    if (l_hh_new){
        HASH_DELETE(hh,s_notify_server_clients, l_hh_new);
    }else{
        uint64_t *l_uuid_u64 =(uint64_t*) &a_es->uuid;
        log_it(L_WARNING,"Trying to remove notify client with uuid 0x%016"DAP_UINT64_FORMAT_X" but can't find such client in table", *l_uuid_u64);
    }
    pthread_rwlock_unlock(&s_notify_server_clients_mutex);
}
