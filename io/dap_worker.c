/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2017
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
#include <time.h>
#include <errno.h>
#include <unistd.h>

#include "dap_context.h"
#include "dap_math_ops.h"
#include "dap_worker.h"
#include "dap_timerfd.h"
#include "dap_events.h"
#include "dap_enc_base64.h"
#include "dap_common.h"
#include "dap_config.h"

#ifndef DAP_NET_CLIENT_NO_SSL
#include <wolfssl/options.h>
#include "wolfssl/ssl.h"
#endif

#define LOG_TAG "dap_worker"

typedef struct dap_worker_msg_callback {
    dap_worker_callback_t callback; // Callback for specific client operations
    void * arg;
} dap_worker_msg_callback_t;

static _Thread_local dap_worker_t* s_worker = NULL;

static time_t s_connection_timeout = 60;    // seconds

static bool s_socket_all_check_activity( void * a_arg);
#ifndef DAP_EVENTS_CAPS_IOCP
static void s_queue_add_es_callback( dap_events_socket_t * a_es, void * a_arg);
static void s_queue_delete_es_callback( dap_events_socket_t * a_es, void * a_arg);
static void s_queue_es_reassign_callback( dap_events_socket_t * a_es, void * a_arg);
static void s_queue_es_io_callback( dap_events_socket_t * a_es, void * a_arg);
#endif
static void s_queue_callback_callback( dap_events_socket_t * a_es, void * a_arg);

dap_worker_t *dap_worker_get_current() {
    return s_worker;
}

/**
 * @brief dap_worker_init
 * @param a_threads_count
 * @param conn_timeout
 * @return
 */
int dap_worker_init( size_t a_conn_timeout )
{
    if ( a_conn_timeout )
      s_connection_timeout = a_conn_timeout;

    return 0;
}

void dap_worker_deinit( )
{
}

/**
 * @brief s_event_exit_callback
 * @param a_es
 * @param a_flags
 */
static void s_event_exit_callback( dap_events_socket_t * a_es, uint64_t a_flags)
{
    (void) a_flags;
    a_es->context->signal_exit = true;
    if (g_debug_reactor)
        log_it(L_DEBUG, "Context #%u signaled to exit", a_es->context->id);
}

/**
 * @brief dap_worker_context_callback_started
 * @param a_context
 * @param a_arg
 * @return
 */
int dap_worker_context_callback_started(dap_context_t * a_context, void *a_arg)
{
    dap_worker_t *l_worker = (dap_worker_t*) a_arg;
    assert(l_worker);
    if (s_worker)
        return log_it(L_ERROR, "Worker %d is already assigned to current thread %ld",
                               s_worker->id, s_worker->context->thread_id),
            -1;
    s_worker = l_worker;
#if defined(DAP_EVENTS_CAPS_KQUEUE)
    a_context->kqueue_fd = kqueue();

    if (a_context->kqueue_fd == -1 ){
        int l_errno = errno;
        char l_errbuf[255];
        strerror_r(l_errno,l_errbuf,sizeof(l_errbuf));
        log_it (L_CRITICAL,"Can't create kqueue(): '%s' code %d",l_errbuf,l_errno);
        return -1;
    }

    a_context->kqueue_events_selected_count_max = 100;
    a_context->kqueue_events_count_max = DAP_EVENTS_SOCKET_MAX;
    a_context->kqueue_events_selected = DAP_NEW_Z_SIZE(struct kevent, a_context->kqueue_events_selected_count_max *sizeof(struct kevent));
#elif defined(DAP_EVENTS_CAPS_POLL)
    a_context->poll_count_max = DAP_EVENTS_SOCKET_MAX;
    a_context->poll = DAP_NEW_Z_SIZE(struct pollfd,a_context->poll_count_max*sizeof (struct pollfd));
    a_context->poll_esocket = DAP_NEW_Z_SIZE(dap_events_socket_t*,a_context->poll_count_max*sizeof (dap_events_socket_t*));
#elif defined(DAP_EVENTS_CAPS_EPOLL)
    a_context->epoll_fd = epoll_create( DAP_MAX_EVENTS_COUNT );
#ifdef DAP_OS_WINDOWS
    if (!a_context->epoll_fd) {
        int l_errno = WSAGetLastError();
#else
    if ( a_context->epoll_fd == -1 ) {
        int l_errno = errno;
#endif
        char l_errbuf[128];
        strerror_r(l_errno, l_errbuf, sizeof (l_errbuf));
        og_it(L_CRITICAL, "Error create epoll fd: %s (%d)", l_errbuf, l_errno);
        return -1;
    }
#elif defined DAP_EVENTS_CAPS_IOCP
    if ( !(a_context->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1)) )
        return log_it(L_CRITICAL, "Creating IOCP failed! Error %d: \"%s\"",
                                  GetLastError(), dap_strerror(GetLastError())), -1;
#else
#error "Unimplemented dap_context_init for this platform"
#endif
#ifndef DAP_EVENTS_CAPS_IOCP
    l_worker->queue_es_new      = dap_context_create_queue(a_context, s_queue_add_es_callback);
    l_worker->queue_es_delete   = dap_context_create_queue(a_context, s_queue_delete_es_callback);
    l_worker->queue_es_io       = dap_context_create_queue(a_context, s_queue_es_io_callback);
    l_worker->queue_es_reassign = dap_context_create_queue(a_context, s_queue_es_reassign_callback );
#endif
    l_worker->queue_callback    = dap_context_create_queue(a_context, s_queue_callback_callback);

    l_worker->timer_check_activity = dap_timerfd_create (s_connection_timeout * 1000 / 2,
                                                        s_socket_all_check_activity, l_worker);
    l_worker->timer_check_activity->worker = l_worker;
    dap_worker_add_events_socket_unsafe(l_worker, l_worker->timer_check_activity->events_socket);
    a_context->event_exit = dap_context_create_event(a_context, s_event_exit_callback);
    return 0;
}

/**
 * @brief dap_worker_context_callback_stopped
 * @param a_context
 * @param a_arg
 * @return
 */
int dap_worker_context_callback_stopped(dap_context_t *a_context, void *a_arg)
{
    dap_return_val_if_fail(a_context && a_arg, -1);
    //TODO add deinit code for queues and others
    dap_context_remove(a_context->event_exit);
    dap_events_socket_delete_unsafe(a_context->event_exit, false);  // check ticket 9030

    dap_worker_t *l_worker = a_arg;
    assert(l_worker);
    log_it(L_NOTICE,"Exiting thread #%u", l_worker->id);
    return 0;
}

int dap_worker_add_events_socket_unsafe(dap_worker_t *a_worker, dap_events_socket_t *a_esocket)
{
    int err = dap_context_add(a_worker->context, a_esocket);
    if (!err) {
        switch (a_esocket->type) {
        case DESCRIPTOR_TYPE_SOCKET_UDP:
        case DESCRIPTOR_TYPE_SOCKET_CLIENT:
        case DESCRIPTOR_TYPE_SOCKET_LISTENING:
            a_esocket->last_time_active = time(NULL);
#ifdef SO_INCOMING_CPU
            int l_cpu = a_worker->context->cpu_id;
            setsockopt(a_esocket->socket , SOL_SOCKET, SO_INCOMING_CPU, &l_cpu, sizeof(l_cpu));
#endif
        default: break;
        }
    }
    return err;
}

#ifndef DAP_EVENTS_CAPS_IOCP
/**
 * @brief s_new_es_callback
 * @param a_es
 * @param a_arg
 */

static int s_queue_es_add(dap_events_socket_t *a_es, void * a_arg)
{
    assert(a_es);
    dap_context_t * l_context = a_es->context;
    assert(l_context);
    dap_worker_t * l_worker = a_es->worker;
    assert(l_worker);
    if (!a_arg)
        return log_it(L_ERROR,"NULL esocket accepted to add on worker #%u", l_worker->id), -1;
    dap_events_socket_t * l_es_new =(dap_events_socket_t *) a_arg;

    debug_if(g_debug_reactor, L_DEBUG, "Added es %p \"%s\" [%s] to worker #%d",
             l_es_new, dap_events_socket_get_type_str(l_es_new),
             l_es_new->socket == INVALID_SOCKET ? "" : dap_itoa(l_es_new->socket),
             l_worker->id);

#ifdef DAP_EVENTS_CAPS_KQUEUE
    if(l_es_new->socket!=0 && l_es_new->socket != -1 &&
            l_es_new->type != DESCRIPTOR_TYPE_EVENT &&
        l_es_new->type != DESCRIPTOR_TYPE_QUEUE &&
        l_es_new->type != DESCRIPTOR_TYPE_TIMER
            )
#else
    if (l_es_new->socket != 0 && l_es_new->socket != INVALID_SOCKET)
#endif
        if (dap_context_find(l_context, l_es_new->uuid)) {
            // Socket already present in worker, it's OK
            return -2;
        }

    if ( dap_worker_add_events_socket_unsafe(l_worker, l_es_new) ) {
        log_it(L_ERROR, "Can't add event socket's handler to worker i/o poll mechanism with error %d", errno);
        return -3;
    }

    // We need to differ new and reassigned esockets. If its new - is_initialized is false
    if (!l_es_new->is_initalized && l_es_new->callbacks.new_callback)
        l_es_new->callbacks.new_callback(l_es_new, NULL);

    //log_it(L_DEBUG, "Added socket %d on worker %u", l_es_new->socket, w->id);
    if (l_es_new->callbacks.worker_assign_callback)
        l_es_new->callbacks.worker_assign_callback(l_es_new, l_worker);

    l_es_new->is_initalized = true;
    return 0;
}

DAP_STATIC_INLINE void s_queue_add_es_callback(dap_events_socket_t *a_es, void * a_arg) { s_queue_es_add(a_es, a_arg); }

/**
 * @brief s_delete_es_callback
 * @param a_es
 * @param a_arg
 */
static void s_queue_delete_es_callback( dap_events_socket_t * a_es, void * a_arg)
{
    assert(a_arg);
    dap_events_socket_uuid_t * l_es_uuid_ptr = (dap_events_socket_uuid_t*) a_arg;
    dap_events_socket_t * l_es;
    if ( (l_es = dap_context_find(a_es->context, *l_es_uuid_ptr)) != NULL ){
        //l_es->flags |= DAP_SOCK_SIGNAL_CLOSE; // Send signal to socket to kill
        dap_events_socket_remove_and_delete_unsafe(l_es, false);
    }else
        debug_if(g_debug_reactor, L_INFO, "While we were sending the delete() message, esocket %"DAP_UINT64_FORMAT_U" has been disconnected ", *l_es_uuid_ptr);
    DAP_DELETE(l_es_uuid_ptr);
}

/**
 * @brief s_reassign_es_callback
 * @param a_es
 * @param a_arg
 */
static void s_queue_es_reassign_callback( dap_events_socket_t * a_es, void * a_arg)
{
    assert(a_es);
    dap_context_t * l_context = a_es->context;
    assert(l_context);
    dap_worker_msg_reassign_t * l_msg = (dap_worker_msg_reassign_t*) a_arg;
    assert(l_msg);
    dap_events_socket_t * l_es_reassign;
    if ( ( l_es_reassign = dap_context_find(l_context, l_msg->esocket_uuid))!= NULL ){
        if( l_es_reassign->was_reassigned && l_es_reassign->flags & DAP_SOCK_REASSIGN_ONCE) {
            log_it(L_INFO, "Reassgment request with DAP_SOCK_REASSIGN_ONCE allowed only once, declined reassigment from %u to %u",
                   l_es_reassign->worker->id, l_msg->worker_new->id);

        }else{
            dap_events_socket_reassign_between_workers_unsafe(l_es_reassign,l_msg->worker_new);
        }
    }else{
        log_it(L_INFO, "While we were sending the reassign message, esocket %p has been disconnected", l_msg->esocket);
    }
    DAP_DELETE(l_msg);
}

/**
 * @brief s_pipe_data_out_read_callback
 * @param a_es
 * @param a_arg
 */
static void s_queue_es_io_callback( dap_events_socket_t * a_es, void * a_arg)
{
    assert(a_es);
    dap_context_t * l_context = a_es->context;
    assert(l_context);
    dap_worker_msg_io_t * l_msg = a_arg;
    assert(l_msg);
    // Check if it was removed from the list
    dap_events_socket_t *l_msg_es = dap_context_find(l_context, l_msg->esocket_uuid);
    if ( l_msg_es == NULL){
        log_it(L_INFO, "We got i/o message for esocket %"DAP_UINT64_FORMAT_U" thats now not in list. Lost %zu data", l_msg->esocket_uuid, l_msg->data_size);
        DAP_DELETE(l_msg->data);
        DAP_DELETE(l_msg);
        return;
    }

    if (l_msg->flags_set & DAP_SOCK_CONNECTING)
        if (!  (l_msg_es->flags & DAP_SOCK_CONNECTING) ){
            l_msg_es->flags |= DAP_SOCK_CONNECTING;
            dap_context_poll_update(l_msg_es);
        }

    if (l_msg->flags_set & DAP_SOCK_CONNECTING)
        if (!  (l_msg_es->flags & DAP_SOCK_CONNECTING) ){
            l_msg_es->flags ^= DAP_SOCK_CONNECTING;
            dap_context_poll_update(l_msg_es);
        }

    if (l_msg->flags_set & DAP_SOCK_READY_TO_READ)
        dap_events_socket_set_readable_unsafe(l_msg_es, true);
    if (l_msg->flags_unset & DAP_SOCK_READY_TO_READ)
        dap_events_socket_set_readable_unsafe(l_msg_es, false);
    if (l_msg->flags_set & DAP_SOCK_READY_TO_WRITE)
        dap_events_socket_set_writable_unsafe(l_msg_es, true);
    if (l_msg->flags_unset & DAP_SOCK_READY_TO_WRITE)
        dap_events_socket_set_writable_unsafe(l_msg_es, false);
    if (l_msg->data_size && l_msg->data) {
        dap_events_socket_write_unsafe(l_msg_es, l_msg->data,l_msg->data_size);
        DAP_DELETE(l_msg->data);
    }
    DAP_DELETE(l_msg);
}
#else
void s_es_assign_to_context(dap_context_t *a_c, OVERLAPPED *a_ol) {
    dap_events_socket_t *l_es = (dap_events_socket_t*)a_ol->Pointer;
    if (!l_es->worker)
        return log_it(L_ERROR, "Es %p error: worker unset");
    dap_events_socket_t *l_sought_es = dap_context_find(a_c, l_es->uuid);
    if ( l_sought_es ) {
        if ( l_sought_es == l_es )
            log_it(L_ERROR, "Es %p and %p assigned to context %d have the same uid "DAP_FORMAT_ESOCKET_UUID"! Possibly a dup!",
                                    l_es, l_sought_es, a_c->id, l_es->uuid);
        return;
    }
    int l_err = dap_worker_add_events_socket_unsafe(l_es->worker, l_es);
    debug_if(l_err || g_debug_reactor, l_err ? L_ERROR : L_DEBUG, "%s es "DAP_FORMAT_ESOCKET_UUID" \"%s\" [%s] to worker #%d in context %d",
                                       l_err ? "Can't add" : "Added", l_es->uuid, dap_events_socket_get_type_str(l_es),
                                       l_es->socket == INVALID_SOCKET ? "" : dap_itoa(l_es->socket),
                                       l_es->worker->id, a_c->id);
    if (l_err)
        return;

    if (!l_es->is_initalized && l_es->callbacks.new_callback)
        l_es->callbacks.new_callback(l_es, NULL);
    if (l_es->callbacks.worker_assign_callback)
        l_es->callbacks.worker_assign_callback(l_es, l_es->worker);

    l_es->is_initalized = true;
    if (l_es->type >= DESCRIPTOR_TYPE_FILE)
        return;

    if ( FLAG_READ_NOCLOSE(l_es->flags) ) {
        dap_events_socket_set_readable_unsafe(l_es, true);
    } else if ( FLAG_WRITE_NOCLOSE(l_es->flags) ) {
        dap_events_socket_set_writable_unsafe(l_es, true);
    }
}
#endif

/**
 * @brief s_queue_callback
 * @param a_es
 * @param a_arg
 */
static void s_queue_callback_callback(dap_events_socket_t UNUSED_ARG *a_es, void *a_arg)
{
    dap_worker_msg_callback_t * l_msg = (dap_worker_msg_callback_t *) a_arg;
    assert(l_msg);
    assert(l_msg->callback);
    l_msg->callback(l_msg->arg);
    DAP_DELETE(l_msg);
}

/**
 * @brief s_socket_all_check_activity
 * @param a_arg
 */
static bool s_socket_all_check_activity( void * a_arg)
{
    dap_worker_t *l_worker = (dap_worker_t*) a_arg;
    assert(l_worker);
    time_t l_curtime = time(NULL); // + 1000;
    //dap_ctime_r(&l_curtime, l_curtimebuf);
    //log_it(L_DEBUG,"Check sockets activity on worker #%u at %s", l_worker->id, l_curtimebuf);
    bool l_removed;
    do {
        l_removed = false;
        size_t l_esockets_counter = 0;
        dap_events_socket_t *l_es, *l_tmp;
        HASH_ITER(hh, l_worker->context->esockets, l_es, l_tmp) {
            u_int l_esockets_count = HASH_CNT(hh, l_worker->context->esockets);
            if (l_esockets_counter >= l_worker->context->event_sockets_count || l_esockets_counter++ >= l_esockets_count){
                log_it(L_ERROR, "Something wrong with context's esocket table: %u esockets in context, %u in table but we're on %zu iteration",
                       l_worker->context->event_sockets_count, l_esockets_count, l_esockets_counter);
                    break;
            }
            if (l_es->type == DESCRIPTOR_TYPE_SOCKET_CLIENT &&
                    !(l_es->flags & DAP_SOCK_SIGNAL_CLOSE) &&
                     l_curtime >= l_es->last_time_active + s_connection_timeout &&
                    !l_es->no_close) {
                log_it( L_INFO, "Socket %"DAP_FORMAT_SOCKET" timeout (diff %"DAP_UINT64_FORMAT_U" ), closing...",
                                l_es->socket, l_curtime -  (time_t)l_es->last_time_active - s_connection_timeout );
                if (l_es->callbacks.error_callback) {
                    l_es->callbacks.error_callback(l_es, ETIMEDOUT);
                }
                dap_events_socket_remove_and_delete_unsafe(l_es, false);
                l_removed = true;
                break;  // Start new cycle from beginning (cause next socket might been removed too)
            }
        }
    } while (l_removed);
    return true;
}

/**
 * @brief sap_worker_add_events_socket
 * @param a_events_socket
 * @param a_worker
 */
void dap_worker_add_events_socket(dap_worker_t *a_worker, dap_events_socket_t *a_events_socket)
{
    dap_return_if_fail(a_worker && a_events_socket);
    int l_ret = 0;
    const char *l_type_str = dap_events_socket_get_type_str(a_events_socket);
    SOCKET l_s = a_events_socket->socket;
    dap_events_socket_uuid_t l_uuid = a_events_socket->uuid;
#ifdef DAP_EVENTS_CAPS_IOCP
    a_events_socket->worker = a_worker;
    if ( dap_worker_get_current() == a_worker )
        s_es_assign_to_context(a_worker->context, &(OVERLAPPED){ .Pointer = a_events_socket });
    else {
        a_events_socket->worker = a_worker;
        dap_overlapped_t *ol = DAP_NEW_Z(dap_overlapped_t);
        ol->ol.Pointer = a_events_socket;
        ol->op = io_call;
        l_ret = PostQueuedCompletionStatus(a_worker->context->iocp, 0, (ULONG_PTR)s_es_assign_to_context, (OVERLAPPED*)ol)
            ? 0 : ( DAP_DELETE(ol), GetLastError() );
    }
#else
    l_ret = dap_worker_get_current() == a_worker
        ? s_queue_es_add(a_worker->queue_es_new, a_events_socket)
        : dap_events_socket_queue_ptr_send(a_worker->queue_es_new, a_events_socket);
#endif
    if (l_ret)
        log_it(L_ERROR, "Can't %s es \"%s\" [%s], uuid "DAP_FORMAT_ESOCKET_UUID" to worker #%d, error %d: \"%s\"",
               dap_worker_get_current() == a_worker ? "assign" : "send",
               l_type_str, dap_itoa(l_s), l_uuid, a_worker->id, l_ret, dap_strerror(l_ret));
    else 
        debug_if(g_debug_reactor, L_DEBUG,
               "%s es \"%s\" [%s], uuid "DAP_FORMAT_ESOCKET_UUID" to worker #%d",
               dap_worker_get_current() == a_worker ? "Assigned" : "Sent",
               l_type_str, dap_itoa(l_s), l_uuid, a_worker->id);
}

#ifndef DAP_EVENTS_CAPS_IOCP
/**
 * @brief dap_worker_add_events_socket_inter
 * @param a_es_input
 * @param a_events_socket
 */
void dap_worker_add_events_socket_inter(dap_events_socket_t *a_es_input, dap_events_socket_t *a_events_socket)
{
    dap_return_if_fail(a_es_input && a_events_socket);
    if( dap_events_socket_queue_ptr_send_to_input( a_es_input, a_events_socket ) )
        log_it(L_ERROR, "Cant send pointer to interthread queue input: \"%s\"(code %d)",
                        dap_strerror(errno), errno);
}

/**
 * @brief Send callback to the worker queue's input
 * @param a_es_input Queue's input
 * @param a_callback Callback
 * @param a_arg Argument for callback
 */
void dap_worker_exec_callback_inter(dap_events_socket_t * a_es_input, dap_worker_callback_t a_callback, void * a_arg)
{
    dap_return_if_fail(a_es_input && a_callback);
    dap_worker_msg_callback_t * l_msg = DAP_NEW_Z(dap_worker_msg_callback_t);
    if (!l_msg) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return;
    }
    l_msg->callback = a_callback;
    l_msg->arg = a_arg;
    if ( dap_events_socket_queue_ptr_send_to_input (a_es_input ,l_msg ) )
        log_it(L_ERROR, "Cant send pointer to queue input: \"%s\"(code %d)",
                        dap_strerror(errno), errno);

}
#endif

/**
 * @brief dap_worker_exec_callback_on
 */
void dap_worker_exec_callback_on(dap_worker_t * a_worker, dap_worker_callback_t a_callback, void * a_arg)
{
    dap_return_if_fail(a_worker && a_callback);
    dap_worker_msg_callback_t *l_msg = DAP_NEW_Z(dap_worker_msg_callback_t);
    if (!l_msg) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return;
    }
    *l_msg = (dap_worker_msg_callback_t) { .callback = a_callback, .arg = a_arg };
    if ( dap_events_socket_queue_ptr_send( a_worker->queue_callback, l_msg ) )
        log_it(L_ERROR, "Cant send pointer to queue input: \"%s\"(code %d)",
                        dap_strerror(errno), errno);

}

/**
 * @brief dap_worker_add_events_socket
 * @param a_worker
 * @param a_events_socket
 */
dap_worker_t *dap_worker_add_events_socket_auto( dap_events_socket_t *a_es)
{
    dap_return_val_if_fail(a_es, NULL);
    dap_worker_t *l_worker = dap_events_worker_get_auto();
    return dap_worker_add_events_socket(l_worker, a_es), l_worker;
}



