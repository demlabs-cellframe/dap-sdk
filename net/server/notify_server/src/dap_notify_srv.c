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

#define LOG_TAG "notify_server"


dap_server_t * s_notify_server = NULL;
dap_events_socket_t * s_notify_server_queue = NULL;

dap_events_socket_t ** s_notify_server_queue_inter = NULL;


dap_events_socket_handler_hh_t * s_notify_server_clients = NULL;
pthread_rwlock_t s_notify_server_clients_mutex = PTHREAD_RWLOCK_INITIALIZER;

static void s_notify_server_callback_queue(dap_events_socket_t * a_es, void * a_arg);
static void s_notify_server_callback_new(dap_events_socket_t * a_es, void * a_arg);
static void s_notify_server_callback_delete(dap_events_socket_t * a_es, void * a_arg);


/**
 * @brief dap_notify_server_init
 * @param a_notify_socket_path
 * @return
 */
int dap_notify_server_init()
{
    uint16_t l_notify_addrs_count = 0;
    char **l_notify_addrs = dap_config_get_array_str(g_config, "notify_server", "listen_path", &l_notify_addrs_count);
    if( l_notify_addrs ) {
        s_notify_server = dap_server_new(l_notify_addrs, l_notify_addrs_count, DAP_SERVER_LOCAL, NULL);
    } else if ( (l_notify_addrs = dap_config_get_array_str(g_config, "notify_server", "listen_address", &l_notify_addrs_count)) ) {
        s_notify_server = dap_server_new(l_notify_addrs, l_notify_addrs_count, DAP_SERVER_TCP, NULL);
    } else {
        log_it(L_INFO,"Notify server is not configured, nothing to init but thats okay");
        return 0;
    }

    if (!s_notify_server) {
        log_it(L_ERROR,"Notify server not initalized, check config");
        return -1;
    }
    s_notify_server->client_callbacks.new_callback = s_notify_server_callback_new;
    s_notify_server->client_callbacks.delete_callback = s_notify_server_callback_delete;
    s_notify_server_queue = dap_events_socket_create_type_queue_ptr_mt(dap_events_worker_get_auto(),s_notify_server_callback_queue);
    uint32_t l_workers_count = dap_events_thread_get_count();
    DAP_NEW_Z_COUNT_RET_VAL(s_notify_server_queue_inter, dap_events_socket_t *, l_workers_count, -2, NULL);
    for(uint32_t i = 0; i < l_workers_count; i++){
        s_notify_server_queue_inter[i] = dap_events_socket_queue_ptr_create_input(s_notify_server_queue);
        dap_events_socket_assign_on_worker_mt(s_notify_server_queue_inter[i], dap_events_worker_get(i));
    }

    log_it(L_NOTICE,"Notify server initalized");
    return 0;
}

/**
 * @brief dap_notify_server_deinit
 */
void dap_notify_server_deinit()
{

}

/**
 * @brief dap_notify_server_create_inter
 * @return
 */
struct dap_events_socket * dap_notify_server_create_inter()
{
    return NULL;
}

/**
 * @brief dap_notify_server_send_fmt_inter
 * @param a_input
 * @param a_format
 * @return
 */
int dap_notify_server_send_f_inter(uint32_t a_worker_id, const char * a_format,...)
{
    if(!s_notify_server_queue_inter) // If not initialized - nothing to notify
        return 0;
    if(a_worker_id>= dap_events_thread_get_count()){
        log_it(L_ERROR,"Wrong worker id %u for send_f_inter() function", a_worker_id);
        return -10;
    }
    dap_events_socket_t * l_input = s_notify_server_queue_inter[a_worker_id];
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
        log_it(L_CRITICAL, "%s", g_error_memory_alloc);
        return -1;
    }
    vsprintf(l_str, a_format, ap_copy);
    va_end(ap_copy);
    int l_ret = dap_events_socket_queue_ptr_send_to_input(l_input, l_str);
    DAP_DELETE(l_str);
    return l_ret;
}

/**
 * @brief dap_notify_server_send_fmt_mt
 * @param a_format
 * @return
 */
int dap_notify_server_send_mt(const char *a_data)
{
    if(!s_notify_server_queue) // If not initialized - nothing to notify
        return 0;
    return dap_events_socket_queue_ptr_send(s_notify_server_queue, dap_strdup(a_data));
}


/**
 * @brief dap_notify_server_send_fmt_mt
 * @param a_format
 * @return
 */
int dap_notify_server_send_f_mt(const char *a_format, ...)
{
    if(!s_notify_server_queue) // If not initialized - nothing to notify
        return 0;
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
        log_it(L_CRITICAL, "%s", g_error_memory_alloc);
        return -1;
    }
    vsprintf(l_str, a_format, ap_copy);
    va_end(ap_copy);
    int l_ret = dap_events_socket_queue_ptr_send(s_notify_server_queue, l_str);
    DAP_DELETE(l_str);
    return l_ret;
}

/**
 * @brief s_notify_server_inter_queue
 * @param a_es
 * @param a_arg
 */
static void s_notify_server_callback_queue(dap_events_socket_t * a_es, void * a_arg)
{
    pthread_rwlock_rdlock(&s_notify_server_clients_mutex);
    dap_events_socket_handler_hh_t * l_socket_handler = NULL,* l_tmp = NULL;
    HASH_ITER(hh, s_notify_server_clients, l_socket_handler, l_tmp){
        uint32_t l_worker_id = l_socket_handler->worker_id;
        if(l_worker_id>= dap_events_thread_get_count()){
            log_it(L_ERROR,"Wrong worker id %u for send_inter() function", l_worker_id);
            continue;
        }
        size_t l_str_len = a_arg? strlen((char*)a_arg): 0;
        if(l_str_len){
#ifdef DAP_EVENTS_CAPS_IOCP
            dap_events_socket_write_mt( dap_events_worker_get(l_worker_id),
                                       l_socket_handler->uuid, a_arg, l_str_len + 1 );
#else
            dap_events_socket_write_inter(a_es->worker->queue_es_io_input[l_worker_id],
                                          l_socket_handler->uuid,
                                          a_arg, l_str_len + 1);
#endif
        }
    }
    pthread_rwlock_unlock(&s_notify_server_clients_mutex);
    DAP_DELETE(a_arg);
}

/**
 * @brief s_notify_server_callback_new
 * @param a_es
 * @param a_arg
 */
static void s_notify_server_callback_new(dap_events_socket_t * a_es, UNUSED_ARG void *a_arg)
{
    dap_events_socket_handler_hh_t * l_hh_new;
    pthread_rwlock_wrlock(&s_notify_server_clients_mutex);
    HASH_FIND(hh,s_notify_server_clients, &a_es->uuid, sizeof (a_es->uuid), l_hh_new);
    if (l_hh_new){
        uint64_t *l_uuid_u64 =(uint64_t*) &a_es->uuid;
        log_it(L_WARNING,"Trying to add notify client with uuid 0x%016"DAP_UINT64_FORMAT_X" but already present this UUID in list, updating only esocket pointer if so", *l_uuid_u64);
        l_hh_new->esocket = a_es;
        l_hh_new->worker_id = a_es->worker->id;
    } else {
        if (!a_es->context || !a_es->worker) {
            log_it(L_ERROR, "Invalid esocket arg with uuid %zu: broken context", a_es->uuid);
            pthread_rwlock_unlock(&s_notify_server_clients_mutex);
            return;
        }
        l_hh_new = DAP_NEW_Z(dap_events_socket_handler_hh_t);
        if (!l_hh_new) {
            log_it(L_CRITICAL, "%s", g_error_memory_alloc);
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
