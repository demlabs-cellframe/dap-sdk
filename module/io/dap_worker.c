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

#if defined (DAP_OS_LINUX)
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#elif defined (DAP_OS_BSD)
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#elif defined (DAP_OS_WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <io.h>
#include <winternl.h>
#include <ntstatus.h>
#endif

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
int dap_worker_context_callback_started(dap_context_t *a_context, void *a_arg)
{
    dap_worker_t *l_worker = (dap_worker_t*) a_arg;
    assert(l_worker);
    if (s_worker)
        return log_it(L_ERROR, "Worker %u is already assigned to current thread %ld",
                               s_worker->id, s_worker->context->thread_id),
            -1;
    s_worker = l_worker;
#if defined(DAP_EVENTS_CAPS_KQUEUE)
    a_context->kqueue_fd = kqueue();

    if (a_context->kqueue_fd == -1)
        return log_it (L_CRITICAL, "kqueue(), error %d: \"%s\"", errno, dap_strerror(errno)), -1;

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
        return log_it(L_ERROR, "epoll_create() error %d: \"%s\"", l_errno, dap_strerror(l_errno)), -1;
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
        case DESCRIPTOR_TYPE_SOCKET_RAW:
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

    dap_context_t *l_ctx = a_es ? a_es->context : NULL;
    if (!l_ctx || l_ctx->signal_exit || !l_ctx->esockets) {
        debug_if(g_debug_reactor, L_INFO, "Skip delete for es %"DAP_UINT64_FORMAT_U" because context is gone", *l_es_uuid_ptr);
        DAP_DELETE(l_es_uuid_ptr);
        return;
    }
    dap_events_socket_t * l_es = dap_context_find(l_ctx, *l_es_uuid_ptr);
    if (l_es && l_es->context == l_ctx){
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
        log_it(L_INFO, "While we were sending the reassign message, esocket %" DAP_UINT64_FORMAT_x " has been disconnected", l_msg->esocket_uuid);
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
    if ( !l_msg_es ) {
        log_it(L_ERROR, "Es %"DAP_UINT64_FORMAT_U" not found on worker %d. Lost %zu bytes",
                         l_msg->esocket_uuid, a_es->worker->id, l_msg->data_size);
        return DAP_DEL_MULTY(l_msg->data, l_msg);
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

/**
 * @brief dap_worker_thread_loop
 * @param a_context
 * @return
 */
int dap_worker_thread_loop(dap_context_t * a_context)
{
    int l_errno;
    dap_events_socket_t *l_cur = NULL;

#ifdef DAP_EVENTS_CAPS_IOCP
    DWORD l_bytes = 0, l_entries_num = 0;
    OVERLAPPED_ENTRY ol_entries[MAX_IOCP_ENTRIES] = { };
    do {
        if ( !GetQueuedCompletionStatusEx(a_context->iocp, ol_entries, MAX_IOCP_ENTRIES, &l_entries_num, INFINITE, FALSE) ) {
            switch ( l_errno = GetLastError() ) {
            case WAIT_IO_COMPLETION: // Soma APC fired??
                log_it(L_ERROR, "An APC fired while in non-alertable waiting");
                break;
            case ERROR_ABANDONED_WAIT_0:
                log_it(L_ERROR, "Completion port on context %u is closed", a_context->id);
                break;
            default:
                log_it(L_ERROR, "GetQueuedCompletionStatusEx() failed, error %u: \"%s\"", l_errno, dap_strerror(l_errno));
            }
            break;
        }
        dap_overlapped_t *ol;
        HANDLE ev; WINBOOL ev_signaled;
        per_io_type_t op;
        DWORD flags;
        debug_if(g_debug_reactor, L_INFO, "Completed %lu items in context #%d", l_entries_num, a_context->id);
        for ( ULONG i = 0; i < l_entries_num; dap_overlapped_free(ol), op = '\0', ev = NULL, ev_signaled = FALSE, ++i ) {
            l_errno = 0;
            l_bytes = ol_entries[i].dwNumberOfBytesTransferred;
            if (( ol = (dap_overlapped_t*)ol_entries[i].lpOverlapped )) {
                op = ol->op;
                ev = ol->ol.hEvent;
                ev_signaled = ev ? WaitForSingleObject(ev, 0) == WAIT_OBJECT_0 : FALSE;
            }
            switch (op) {
            case io_call: {
                dap_per_io_func func = (dap_per_io_func)ol_entries[i].lpCompletionKey;
                debug_if(g_debug_reactor, L_DEBUG, "Calling per-i/o function %zx", ol_entries[i].lpCompletionKey);
                func(a_context, &ol->ol);
                continue;
            }
            case io_read:
            case io_write:
                l_cur = ev ? (dap_events_socket_t*)ol_entries[i].lpCompletionKey
                           : dap_context_find(a_context, (dap_events_socket_uuid_t)ol_entries[i].lpCompletionKey);
                if ( !l_cur ) {
                    if (ev) log_it(L_ERROR, "Completion of op '%c', but key is null! Lost %lu bytes", op, l_bytes);
                    else    log_it(L_ERROR, "Completion of op '%c', but key "DAP_FORMAT_ESOCKET_UUID" not found! Lost %lu bytes",
                                            op, (dap_events_socket_uuid_t)ol_entries[i].lpCompletionKey, l_bytes);
                    continue;
                }
                break;
            default:
                l_cur = (dap_events_socket_t*)ol_entries[i].lpCompletionKey;
                if ( !l_cur ) {
                    log_it(L_ERROR, "Completion with null key! Dump it");
                    continue;
                }
                break;
            }

            uint32_t l_cur_flags = l_cur->flags;
            DWORD l_buf_in_size = l_cur->buf_in_size, l_buf_out_size = l_cur->buf_out_size;
            debug_if(g_debug_reactor, L_DEBUG, "\n\tCompletion on \"%s\" "DAP_FORMAT_ESOCKET_UUID", bytes: %lu, operation: '%c', "
                     "flags: %d [%s:%s:%s:%s:%s], sizes in/out: %lu/%lu, OL event state: %s, pending read / write: %d / %d",
                     dap_events_socket_get_type_str(l_cur), l_cur->uuid, l_bytes,
                     op ? op : ' ', l_cur_flags,
                     l_cur_flags & DAP_SOCK_READY_TO_READ  ? "READ"    : "",
                     l_cur_flags & DAP_SOCK_READY_TO_WRITE ? "WRITE"   : "",
                     l_cur_flags & DAP_SOCK_CONNECTING     ? "CONN"    : "",
                     l_cur_flags & DAP_SOCK_SIGNAL_CLOSE   ? "CLOSE"   : "",
                     l_cur->no_close                       ? "NOCLOSE" : "",
                     l_buf_in_size, l_buf_out_size,
                     ev ? ev_signaled ? "SET" : "UNSET" : "N/A",
                     l_cur->pending_read, l_cur->pending_write);
            if ( /*!l_cur->context && */ FLAG_CLOSE(l_cur->flags) ) { // Pending delete, socket will be closed
                if ( op == io_read || l_cur->type == DESCRIPTOR_TYPE_TIMER )
                    l_cur->pending_read = 0;
                else if (l_cur->pending_write)
                    --l_cur->pending_write;
                if ( !l_cur->pending_read && !l_cur->pending_write )
                    dap_events_socket_delete_unsafe(l_cur, FLAG_KEEP_INHERITOR(l_cur->flags));
                continue;
            }
            switch (l_cur->type) {
            case DESCRIPTOR_TYPE_SOCKET_LISTENING:
                // AcceptEx completed
                l_cur->pending_read = 0;
                if ( NT_ERROR(ol->ol.Internal) ) {
                    log_it(L_ERROR, "\"AcceptEx\" on "DAP_FORMAT_ESOCKET_UUID" : %zu failed, ntstatus 0x%llx : %s",
                                    l_cur->uuid, l_cur->socket, ol->ol.Internal, dap_str_ntstatus(ol->ol.Internal));
                    closesocket(l_cur->socket2);
                    if ( ol->ol.Internal == STATUS_CONNECTION_RESET ) {
                        l_errno = WSAECONNRESET; // It's ok, just continue accept()'ing
                        dap_events_socket_set_readable_unsafe_ex(l_cur, true, ol);
                        ol = NULL;
                    } else {
                        // l_errno = pfnRtlNtStatusToDosError(ol->ol.Internal);
                        /*
                            TODO: though another syscall is discouraged here, there's no way to obtain WSA last error
                            which the cross-platform error-handling functions rely on, since NtStatusToDosError()
                            returns irrelevant error code for completed WSA*(). NTSTATUS propagation needs tweaking
                            error handlers for Windows. We'll probably get down to it later
                        */
                        WSAGetOverlappedResult(l_cur->socket, &ol->ol, &l_bytes, FALSE, &flags);
                        l_errno = WSAGetLastError();
                    }
                    break;
                }

                if (!l_cur->callbacks.accept_callback) {
                    log_it(L_ERROR, "Listening socket "DAP_FORMAT_ESOCKET_UUID" : %zu has no accept callback, nothing to do. Dump eet",
                           l_cur->uuid, l_cur->socket);
                    l_cur->flags = DAP_SOCK_SIGNAL_CLOSE;
                    break;
                }
                if ( setsockopt(l_cur->socket2, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&l_cur->socket, sizeof(l_cur->socket)) ) {
                    l_errno = WSAGetLastError();
                    log_it(L_ERROR, "setsockopt SO_UPDATE_ACCEPT_CONTEXT failed, errno %d", l_errno);
                    break;
                }
                LPSOCKADDR l_local_addr = NULL, l_remote_addr = NULL;
                INT l_local_addr_len = 0, l_remote_addr_len = 0;
                pfnGetAcceptExSockaddrs( l_cur->buf_in, 0, sizeof(SOCKADDR_STORAGE) + 16, sizeof(SOCKADDR_STORAGE) + 16,
                                         &l_local_addr, &l_local_addr_len, &l_remote_addr, &l_remote_addr_len );
                l_cur->callbacks.accept_callback(l_cur, l_cur->socket2, (struct sockaddr_storage*)l_remote_addr);
                dap_events_socket_set_readable_unsafe_ex(l_cur, true, ol);
                ol = NULL;
            break; // a.k.a TCP server

            case DESCRIPTOR_TYPE_SOCKET_LOCAL_LISTENING:
            case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
            case DESCRIPTOR_TYPE_PIPE:
            case DESCRIPTOR_TYPE_FILE:
                // TODO: do required stuff
            break;

            case DESCRIPTOR_TYPE_TIMER:
                l_cur->pending_read = 0;
                if ( !l_cur->callbacks.timer_callback ) {
                    log_it(L_ERROR, "Es %p has no timer callback, nothing to do. Dump eet", l_cur);
                    l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
                }
                else
                    l_cur->callbacks.timer_callback(l_cur);
                break;

            case DESCRIPTOR_TYPE_SOCKET_CLIENT:
            case DESCRIPTOR_TYPE_SOCKET_UDP:
                /* TODO: analyze flags?
                 *
                 * if (!WSAGetOverlappedResult(l_cur->socket, ol, &l_bytes, FALSE, &flags)) {
                    log_it(L_ERROR, "Getting op result failed, errno %d", WSAGetLastError());
                    l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
                    break;
                }
                */
                switch (op) {
                case io_read:
                    if ( !ev ) {
                        // TODO: read_mt() or read_inter() command called?
                        dap_events_socket_set_readable_unsafe_ex(l_cur, true, ol);
                        ol = NULL;
                        continue;
                    } else {
                        l_cur->pending_read = 0;
                        if ( !l_bytes ) {
                            if ( NT_ERROR(ol->ol.Internal) ) {
                                // l_errno = pfnRtlNtStatusToDosError(ol->ol.Internal);
                                /*
                                    TODO: though another syscall is discouraged here, there's no way to obtain WSA last error
                                    which the cross-platform error-handling functions rely on, since NtStatusToDosError()
                                    returns irrelevant error code for completed WSA*(). NTSTATUS propagation needs tweaking
                                    error handlers for Windows. We'll probably get down to it later
                                */
                                WSAGetOverlappedResult(l_cur->socket, &ol->ol, &l_bytes, FALSE, &flags);
                                l_errno = WSAGetLastError();
                                log_it(L_ERROR, "Connection to %s : %u closed with error %d: \"%s\", ntstatus 0x%llx",
                                                l_cur->remote_addr_str, l_cur->remote_port, l_errno, dap_strerror(l_errno),
                                                ol->ol.Internal);
                            } else {
                                log_it(L_INFO, "Connection to %s : %u closed", l_cur->remote_addr_str, l_cur->remote_port);
                                if (!l_cur->no_close)
                                    l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
                            }
                            break;
                        } else //if (ev_signaled)
                            l_cur->buf_in_size += l_bytes;
                    }
                    if (l_cur->callbacks.read_callback) {
                        l_cur->last_time_active = time(NULL);
                        debug_if(g_debug_reactor, L_DEBUG, "Received %lu bytes from socket %zu", l_bytes, l_cur->socket);
                        l_cur->callbacks.read_callback(l_cur, l_cur->callbacks.arg);
                        if (!l_cur->context) {
                            debug_if(g_debug_reactor, L_DEBUG, "Es %p : %zu unattached from context %u", l_cur, l_cur->socket, a_context->id);
                            continue;
                        } else if ( FLAG_READ_NOCLOSE(l_cur->flags) ) {
                            // Continue reading
                            dap_events_socket_set_readable_unsafe_ex(l_cur, true, ol);
                            ol = NULL; // OL is being reused, prevent from deleting it
                        }
                    } else {
                        log_it(L_ERROR, "Es %zu has no read callback, nothing to do. Dump %lu bytes", l_cur->socket, l_bytes);
                        l_cur->flags &= ~DAP_SOCK_READY_TO_READ;
                    }
                break; // io_read

                case io_write:
                    if ( !ev ) {
                        // Likely a cross-thread write
                        dap_events_socket_set_writable_unsafe_ex(l_cur, true, l_bytes, ol);
                        ol = NULL; // Prevent from deletion
                        continue;
                    } else if ( l_cur->pending_write )
                        --l_cur->pending_write;
                    if ( !l_cur->server && l_cur->flags & DAP_SOCK_CONNECTING ) {
                        if ( NT_ERROR(ol->ol.Internal) ) {
                            // l_errno = pfnRtlNtStatusToDosError(ol->ol.Internal);
                            /*
                                TODO: though another syscall is discouraged here, there's no way to obtain WSA last error
                                which the cross-platform error-handling functions rely on, since NtStatusToDosError()
                                returns irrelevant error code for completed WSA*(). NTSTATUS propagation needs tweaking
                                error handlers for Windows. We'll probably get down to it later
                            */
                            WSAGetOverlappedResult(l_cur->socket, &ol->ol, &l_bytes, FALSE, &flags);
                            l_errno = WSAGetLastError();
                            log_it(L_ERROR, "ConnectEx to %s : %u failed with error %d: \"%s\", ntstatus 0x%llx",
                                            l_cur->remote_addr_str, l_cur->remote_port, l_errno, dap_strerror(l_errno),
                                            ol->ol.Internal);
                            /* TODO: optimization
                            switch (l_errno) {
                                case WSAECONNREFUSED:
                                case WSAENETUNREACH:
                                case WSAETIMEDOUT:
                                    // Retry with the same socket!
                                    break;
                                default: break;
                            } */
                            break;
                        } else if ( setsockopt(l_cur->socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) ) {
                            l_errno = WSAGetLastError();
                            log_it(L_ERROR, "setsockopt SO_UPDATE_CONNECT_CONTEXT failed, errno %d", l_errno);
                            break;
                        }
                        log_it(L_INFO, "ConnectEx to %s : %u succeeded",
                                        l_cur->remote_addr_str, l_cur->remote_port);
                        l_cur->flags &= ~DAP_SOCK_CONNECTING;
                        if (l_cur->callbacks.connected_callback)
                            l_cur->callbacks.connected_callback(l_cur);
                        //if ( !(l_cur->flags & DAP_SOCK_READY_TO_READ) ) {
                        dap_events_socket_set_readable_unsafe_ex(l_cur, true, ol);
                        ol = NULL;
                        //}
                        break;
                    } else if ( !l_bytes ) {
                        if ( NT_ERROR(ol->ol.Internal) ) {
                            // l_errno = pfnRtlNtStatusToDosError(ol->ol.Internal);
                            /*
                                TODO: though another syscall is discouraged here, there's no way to obtain WSA last error
                                which the cross-platform error-handling functions rely on, since NtStatusToDosError()
                                returns irrelevant error code for completed WSA*(). NTSTATUS propagation needs tweaking
                                error handlers for Windows. We'll probably get down to it later
                            */
                            WSAGetOverlappedResult(l_cur->socket, &ol->ol, &l_bytes, FALSE, &flags);
                            l_errno = WSAGetLastError();
                            log_it(L_ERROR, "Connection on es %zu to remote %s : %u closed with error %d: %s, ntstatus 0x%llx",
                                           l_cur->socket, l_cur->remote_addr_str, l_cur->remote_port, l_errno, dap_strerror(l_errno),
                                           ol->ol.Internal);
                        } else
                            log_it(L_INFO, "Connection on es %zu to remote %s : %u closed",
                                           l_cur->socket, l_cur->remote_addr_str, l_cur->remote_port);
                        break;
                    }
                    if (l_cur->callbacks.write_callback)
                        l_cur->callbacks.write_callback(l_cur, l_cur->callbacks.arg);
                    if ( l_cur->callbacks.write_finished_callback && !l_cur->buf_out_size && (l_cur->flags & DAP_SOCK_READY_TO_WRITE) )
                        l_cur->callbacks.write_finished_callback(l_cur, l_cur->callbacks.arg);

                break; // io_write
                default: break;
                }
            break;

            case DESCRIPTOR_TYPE_QUEUE:
                dap_events_socket_queue_proc_input_unsafe(l_cur);
                l_cur->flags &= ~DAP_SOCK_READY_TO_WRITE;
            break;

            case DESCRIPTOR_TYPE_EVENT:
                dap_events_socket_event_proc_input_unsafe(l_cur);
            break;

            case DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL:
            break;

            default:
                log_it(L_ERROR, "Es %p has unknown type %d. Dump eet", l_cur, l_cur->type);
                l_cur->flags = DAP_SOCK_SIGNAL_CLOSE;
            break;
            }

            if (g_debug_reactor) {
                char states[127] = { '\0' }, shift = 0;
                if (l_cur->flags != l_cur_flags) {
                    l_cur_flags = l_cur->flags;
                    shift = snprintf(states, sizeof(states), ", flags changed to [%s:%s:%s:%s:%s]",
                                         l_cur_flags & DAP_SOCK_READY_TO_READ   ? "READ"    : "",
                                         l_cur_flags & DAP_SOCK_READY_TO_WRITE  ? "WRITE"   : "",
                                         l_cur_flags & DAP_SOCK_CONNECTING      ? "CONN"    : "",
                                         l_cur_flags & DAP_SOCK_SIGNAL_CLOSE    ? "CLOSE"   : "",
                                         l_cur->no_close                        ? "NOCLOSE" : "");
                }
                if (l_cur->buf_in_size != l_buf_in_size) {
                    shift += snprintf(states + shift, sizeof(states) - shift, ", BUF_IN size: %lu -> %lu",
                                          l_buf_in_size, l_cur->buf_in_size);
                    l_buf_in_size = l_cur->buf_in_size;
                }
                if (l_cur->buf_out_size != l_buf_out_size) {
                    shift += snprintf(states + shift, sizeof(states) - shift, ", BUF_OUT size: %lu -> %lu",
                                          l_buf_out_size, l_cur->buf_out_size);
                    l_buf_out_size = l_cur->buf_out_size;
                }
                if (ev)
                    shift += snprintf(states + shift, sizeof(states) - shift, ", OL event is %s",
                                      ev_signaled ? "SET" : "UNSET");
                snprintf(states + shift, sizeof(states) - shift, ", pending read / write: %d / %d",
                         l_cur->pending_read, l_cur->pending_write);

                log_it(L_DEBUG, "Finished completion of i/o op '%c' on es "DAP_FORMAT_ESOCKET_UUID"%s",
                                op ? op : ' ', l_cur->uuid, states);
            }
            if (l_errno) {
                if ( l_cur->callbacks.error_callback )
                    l_cur->callbacks.error_callback(l_cur, l_errno);
                if ( !l_cur->no_close )
                    l_cur->flags = DAP_SOCK_SIGNAL_CLOSE;
            }
            // Final check
            if ( FLAG_CLOSE(l_cur->flags) )
                dap_events_socket_remove_and_delete_unsafe(l_cur, false);
        }
    } while (!a_context->signal_exit);
#else  // IOCP
    ssize_t l_bytes_sent = 0, l_bytes_read = 0, l_sockets_max;
    int l_selected_sockets = 0;
    do {
#ifdef DAP_EVENTS_CAPS_EPOLL
        struct epoll_event *l_epoll_events = a_context->epoll_events;
        l_selected_sockets = epoll_wait(a_context->epoll_fd, l_epoll_events, DAP_EVENTS_SOCKET_MAX, -1);
        l_sockets_max = l_selected_sockets;
#elif defined(DAP_EVENTS_CAPS_POLL)
        l_selected_sockets = poll(a_context->poll, a_context->poll_count, -1);
        l_sockets_max = a_context->poll_count;
#elif defined(DAP_EVENTS_CAPS_KQUEUE)
        l_selected_sockets = kevent(a_context->kqueue_fd,NULL,0,a_context->kqueue_events_selected,a_context->kqueue_events_selected_count_max,
                                                        NULL);
        l_sockets_max = l_selected_sockets;
#elif defined DAP_EVENTS_CAPS_IOCP

#else
#error "Unimplemented poll wait analog for this platform"
#endif
        if(l_selected_sockets == -1) {
            if( (l_errno = errno) == EINTR)
                continue;
#ifdef DAP_OS_WINDOWS
            log_it(L_ERROR, "Context thread %d got errno %d", a_context->id, WSAGetLastError());
#else
            log_it(L_ERROR, "Context thread %d got error: %d: \"%s\"", a_context->id, l_errno, dap_strerror(l_errno));
            assert(l_errno);
#endif
            break;
        }

        a_context->esockets_selected = l_selected_sockets;
        time_t l_cur_time = time( NULL);
        for (a_context->esocket_current = 0; a_context->esocket_current < l_sockets_max; a_context->esocket_current++) {
            ssize_t n = a_context->esocket_current;
            bool l_flag_hup, l_flag_rdhup, l_flag_read, l_flag_write, l_flag_error, l_flag_nval, l_flag_msg, l_flag_pri;

#ifdef DAP_EVENTS_CAPS_EPOLL
            l_cur = (dap_events_socket_t *) l_epoll_events[n].data.ptr;
            uint32_t l_cur_flags = l_epoll_events[n].events;
            l_flag_hup      = l_cur_flags & EPOLLHUP;
            l_flag_rdhup    = l_cur_flags & EPOLLRDHUP;
            l_flag_write    = l_cur_flags & EPOLLOUT;
            l_flag_read     = l_cur_flags & EPOLLIN;
            l_flag_error    = l_cur_flags & EPOLLERR;
            l_flag_pri      = l_cur_flags & EPOLLPRI;
            l_flag_nval     = false;
            l_flag_msg = false;
#elif defined ( DAP_EVENTS_CAPS_POLL)
            short l_cur_flags =a_context->poll[n].revents;

            if (a_context->poll[n].fd == -1) // If it was deleted on previous iterations
                continue;

            if (!l_cur_flags) // No events for this socket
                continue;

            l_flag_hup =  l_cur_flags& POLLHUP;
            l_flag_rdhup = l_cur_flags & POLLRDHUP;
            l_flag_write = (l_cur_flags & POLLOUT) || (l_cur_flags &POLLWRNORM)|| (l_cur_flags &POLLWRBAND ) ;
            l_flag_read = l_cur_flags & POLLIN || (l_cur_flags &POLLRDNORM)|| (l_cur_flags &POLLRDBAND );
            l_flag_error = l_cur_flags & POLLERR;
            l_flag_nval = l_cur_flags & POLLNVAL;
            l_flag_pri = l_cur_flags & POLLPRI;
            l_flag_msg = l_cur_flags & POLLMSG;
            l_cur = a_context->poll_esocket[n];
            //log_it(L_DEBUG, "flags: returned events 0x%0X requested events 0x%0X",a_context->poll[n].revents,a_context->poll[n].events );
#elif defined (DAP_EVENTS_CAPS_KQUEUE)
        l_flag_hup=l_flag_rdhup=l_flag_read=l_flag_write=l_flag_error=l_flag_nval=l_flag_msg =l_flag_pri = false;
        struct kevent * l_kevent_selected = &a_context->kqueue_events_selected[n];
        if ( l_kevent_selected->filter == EVFILT_USER){ // If we have USER event it sends little different pointer
            dap_events_socket_w_data_t * l_es_w_data = (dap_events_socket_w_data_t *) l_kevent_selected->udata;
            if(l_es_w_data){
                //if(g_debug_reactor)
                //    log_it(L_DEBUG,"EVFILT_USER: udata=%p", l_es_w_data);

                l_cur = l_es_w_data->esocket;
                if(l_cur){
                    memcpy(&l_cur->kqueue_event_catched_data, l_es_w_data, sizeof (*l_es_w_data)); // Copy event info for further processing

                    if ( l_cur->pipe_out == NULL){ // If we're not the input for pipe or queue
                                                   // we must drop write flag and set read flag
                        l_flag_read  = true;
                    }else{
                        l_flag_write = true;
                    }
                    void * l_ptr = &l_cur->kqueue_event_catched_data;
                    if(l_es_w_data != l_ptr){
                        DAP_DELETE(l_es_w_data);
                    }else if (g_debug_reactor){
                        log_it(L_DEBUG,"Own event signal without actual event data");
                    }
                }
            } else // Looks it was deleted on previous iteration
                l_cur = NULL;
        }else{
            switch (l_kevent_selected->filter) {
                case EVFILT_TIMER:
                case EVFILT_READ: l_flag_read = true; break;
                case EVFILT_WRITE: l_flag_write = true; break;
                case EVFILT_EXCEPT : l_flag_rdhup = true; break;
                default:
                    log_it(L_CRITICAL,"Unknown filter type in polling, exit thread");
                    return -1;
            }
            if (l_kevent_selected->flags & EV_EOF)
                l_flag_rdhup = true;
            l_cur = (dap_events_socket_t*) l_kevent_selected->udata;

            if (l_cur && l_kevent_selected->filter == EVFILT_TIMER && l_cur->type != DESCRIPTOR_TYPE_TIMER) {
                log_it(L_WARNING, "Filer type and socket descriptor type mismatch");
                continue;
            }
        }
        if (l_cur)
            l_cur->kqueue_event_catched = l_kevent_selected;
#ifndef DAP_OS_DARWIN
            u_int l_cur_flags = l_kevent_selected->flags;
#else
            uint32_t l_cur_flags = l_kevent_selected->flags;
#endif

#else
#error "Unimplemented fetch esocket after poll"
#endif

            // Previously deleted socket, its really bad when it appears
            if (!l_cur || !l_cur->context || l_cur->context != a_context) {
                log_it(L_ATT, "dap_events_socket was destroyed earlier");
                continue;
            }
            switch (l_cur->type) {
            case DESCRIPTOR_TYPE_SOCKET_CLIENT:
            case DESCRIPTOR_TYPE_SOCKET_UDP:
            case DESCRIPTOR_TYPE_SOCKET_LISTENING:
#ifdef DAP_OS_UNIX
            case DESCRIPTOR_TYPE_SOCKET_LOCAL_LISTENING:
            case DESCRIPTOR_TYPE_SOCKET_RAW:
#endif
            case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
            case DESCRIPTOR_TYPE_TIMER:
            case DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL:
                if (l_cur->socket == INVALID_SOCKET) {
                    log_it(L_ATT, "dap_events_socket have invalid socket number");
                    continue;
                } break;
            // TODO define condition for invalid socket with other descriptor types
            case DESCRIPTOR_TYPE_QUEUE:
            case DESCRIPTOR_TYPE_PIPE:
            case DESCRIPTOR_TYPE_EVENT:
            case DESCRIPTOR_TYPE_FILE:
                if (l_cur->fd == -1 || l_cur->fd2 == -1) {

                }
            default: break;
            }

            if(g_debug_reactor) {
                log_it(L_DEBUG, "--Context #%u esocket %p uuid 0x%016"DAP_UINT64_FORMAT_x" type %d fd=%"DAP_FORMAT_SOCKET" flags=0x%0X (%s:%s:%s:%s:%s:%s:%s:%s)--",
                       a_context->id, l_cur, l_cur->uuid, l_cur->type, l_cur->socket,
                    l_cur_flags, l_flag_read?"read":"", l_flag_write?"write":"", l_flag_error?"error":"",
                    l_flag_hup?"hup":"", l_flag_rdhup?"rdhup":"", l_flag_msg?"msg":"", l_flag_nval?"nval":"",
                       l_flag_pri?"pri":"");
            }

            int l_sock_err = 0, l_sock_err_size = sizeof(l_sock_err);
            //connection already closed (EPOLLHUP - shutdown has been made in both directions)

            if( l_flag_hup ) {
                switch (l_cur->type ){
                case DESCRIPTOR_TYPE_SOCKET_UDP:
                case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
                case DESCRIPTOR_TYPE_SOCKET_CLIENT:
                case DESCRIPTOR_TYPE_SOCKET_RAW: {
                    getsockopt(l_cur->socket, SOL_SOCKET, SO_ERROR, (void *)&l_sock_err, (socklen_t *)&l_sock_err_size);
#ifndef DAP_OS_WINDOWS
                    if (l_sock_err) {
                         log_it(L_DEBUG, "Socket %d error %d", l_cur->socket, l_sock_err);
#else
                    log_it(L_DEBUG, "Socket %"DAP_FORMAT_SOCKET" will be shutdown (EPOLLHUP), error %d", l_cur->socket, WSAGetLastError());
#endif
                    dap_events_socket_set_readable_unsafe(l_cur, false);
                    dap_events_socket_set_writable_unsafe(l_cur, false);
                    l_cur->buf_out_size = 0;
                    l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
                    l_flag_error = l_flag_write = false;
                    if (l_cur->callbacks.error_callback)
                        l_cur->callbacks.error_callback(l_cur, l_sock_err); // Call callback to process error event
#ifndef DAP_OS_WINDOWS
                        log_it(L_INFO, "Socket shutdown (EPOLLHUP): %s", dap_strerror(l_sock_err));
                    }
#endif
                    break;
                }
                default:
                    if(g_debug_reactor)
                        log_it(L_WARNING, "HUP event on esocket %p (%"DAP_FORMAT_SOCKET") type %d", l_cur, l_cur->socket, l_cur->type );
                }
            }

            if(l_flag_nval ){
                log_it(L_WARNING, "NVAL flag armed for socket %p (%"DAP_FORMAT_SOCKET")", l_cur, l_cur->socket);
                l_cur->buf_out_size = 0;
                l_cur->buf_in_size = 0;
                l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
                if (l_cur->callbacks.error_callback)
                    l_cur->callbacks.error_callback(l_cur, l_sock_err); // Call callback to process error event
                if (l_cur->fd == 0 || l_cur->fd == -1) {
#ifdef DAP_OS_WINDOWS
                    log_it(L_ERROR, "Wrong fd: %d", l_cur->fd);
#else
                    assert(errno);
#endif
                }
                // If its not null or -1 we should try first to remove it from poll. Assert only if it doesn't help
            }

            if(l_flag_error) {
                switch (l_cur->type ){
                case DESCRIPTOR_TYPE_SOCKET_LISTENING:
                case DESCRIPTOR_TYPE_SOCKET_CLIENT:
                case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
                    getsockopt(l_cur->socket, SOL_SOCKET, SO_ERROR, (void *)&l_sock_err, (socklen_t *)&l_sock_err_size);
                    log_it(L_ERROR, "Socket error %d: \"%s\"", l_sock_err, dap_strerror(l_sock_err));
                default: break;
                }
                dap_events_socket_set_readable_unsafe(l_cur, false);
                dap_events_socket_set_writable_unsafe(l_cur, false);
                l_cur->buf_out_size = 0;
                if (!l_cur->no_close)
                    l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
                if(l_cur->callbacks.error_callback)
                    l_cur->callbacks.error_callback(l_cur, l_sock_err); // Call callback to process error event
            }

            if (l_flag_read && !(l_cur->flags & DAP_SOCK_SIGNAL_CLOSE)) {

                //log_it(L_DEBUG, "Comes connection with type %d", l_cur->type);
                if(l_cur->buf_in_size_max && l_cur->buf_in_size >= l_cur->buf_in_size_max ) {
                    log_it(L_WARNING, "Buffer is full when there is smth to read. Its dropped! esocket %p (%"DAP_FORMAT_SOCKET")", l_cur, l_cur->socket);
                    l_cur->buf_in_size = 0;
                }

                bool l_must_read_smth = false;
                switch (l_cur->type) {
                    case DESCRIPTOR_TYPE_PIPE:
                    case DESCRIPTOR_TYPE_FILE:
                        l_must_read_smth = true;
#ifdef DAP_OS_WINDOWS
                        l_bytes_read = dap_recvfrom(l_cur->socket, l_cur->buf_in + l_cur->buf_in_size, l_cur->buf_in_size_max - l_cur->buf_in_size);
#else
                        l_bytes_read = read(l_cur->socket, (char *) (l_cur->buf_in + l_cur->buf_in_size),
                                            l_cur->buf_in_size_max - l_cur->buf_in_size);
#endif
                        l_errno = errno;
                    break;
                    case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
                    case DESCRIPTOR_TYPE_SOCKET_CLIENT:
                        l_must_read_smth = true;
                        l_bytes_read = recv(l_cur->fd, (char *) (l_cur->buf_in + l_cur->buf_in_size),
                                            l_cur->buf_in_size_max - l_cur->buf_in_size, 0);
#ifdef DAP_OS_WINDOWS
                        l_errno = WSAGetLastError();
#else
                        l_errno = errno;
#endif
                    break;
                    case DESCRIPTOR_TYPE_SOCKET_UDP:
                        l_must_read_smth = true;
                        l_bytes_read = recvfrom(l_cur->fd, (char *) (l_cur->buf_in + l_cur->buf_in_size),
                                                l_cur->buf_in_size_max - l_cur->buf_in_size, 0,
                                                (struct sockaddr *)&l_cur->addr_storage, &l_cur->addr_size);

#ifdef DAP_OS_WINDOWS
                        l_errno = WSAGetLastError();
#else
                        l_errno = errno;
#endif
                    break;
                    case DESCRIPTOR_TYPE_SOCKET_RAW:
                        l_must_read_smth = true;
                        if ( l_cur->flags & DAP_SOCK_MSG_ORIENTED ) {
                            struct iovec iov = { l_cur->buf_in, l_cur->buf_in_size_max - l_cur->buf_in_size };
                            struct msghdr msg = { .msg_name = &l_cur->addr_storage, .msg_namelen = l_cur->addr_size, .msg_iov = &iov, .msg_iovlen = 1 };
                            l_bytes_read = recvmsg(l_cur->fd, &msg, 0);
                        } else
                            l_bytes_read = recvfrom(l_cur->fd, (char *) (l_cur->buf_in + l_cur->buf_in_size),
                                                    l_cur->buf_in_size_max - l_cur->buf_in_size, 0,
                                                    (struct sockaddr*)&l_cur->addr_storage, &l_cur->addr_size);
                        l_errno = errno;
                    break;
                    case DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL: {
                        l_must_read_smth = true;
#ifndef DAP_NET_CLIENT_NO_SSL
                        WOLFSSL *l_ssl = SSL(l_cur);
                        l_bytes_read =  wolfSSL_read(l_ssl, (char *) (l_cur->buf_in + l_cur->buf_in_size),
                                                     l_cur->buf_in_size_max - l_cur->buf_in_size);
                        l_errno = wolfSSL_get_error(l_ssl, 0);
                        if (l_bytes_read > 0 && g_debug_reactor)
                            log_it(L_DEBUG, "SSL read: %s", (char *)(l_cur->buf_in + l_cur->buf_in_size));
#endif
                    }
                    break;

                    case DESCRIPTOR_TYPE_SOCKET_LISTENING:
#ifdef DAP_OS_UNIX
                    case DESCRIPTOR_TYPE_SOCKET_LOCAL_LISTENING:
#endif
                        // Accept connection
                        if (l_cur->callbacks.accept_callback) {
                            struct sockaddr_storage l_addr_storage = { };
                            socklen_t l_remote_addr_size = sizeof(l_addr_storage);
                            SOCKET l_remote_socket = accept(l_cur->socket, (struct sockaddr*)&l_addr_storage, &l_remote_addr_size);
#ifdef DAP_OS_WINDOWS
                            /*u_long l_mode = 1;
                            ioctlsocket((SOCKET)l_remote_socket, (long)FIONBIO, &l_mode); */
                            // no need, since l_cur->socket is already NBIO
                            if (l_remote_socket == INVALID_SOCKET) {
                                int l_errno = WSAGetLastError();
                                if (l_errno == WSAEWOULDBLOCK)
                                    continue;
                                else {
                                    log_it(L_WARNING,"Can't accept on socket %"DAP_FORMAT_SOCKET", WSA errno: %d", l_cur->socket, l_errno);
                                    break;
                                }
                            }
#else
                            fcntl( l_remote_socket, F_SETFL, O_NONBLOCK);
                            int l_errno = errno;
                            if ( l_remote_socket == INVALID_SOCKET ){
                                if( l_errno == EAGAIN || l_errno == EWOULDBLOCK){// Everything is good, we'll receive ACCEPT on next poll
                                    continue;
                                }else{
                                    log_it(L_WARNING, "accept() on socket %d error %d: \"%s\"",
                                                      l_cur->socket, l_errno, dap_strerror(l_errno));
                                    break;
                                }
                            }
#endif
                            l_cur->callbacks.accept_callback(l_cur, l_remote_socket, &l_addr_storage);
                        }else
                            log_it(L_ERROR,"No accept_callback on listening socket");
                    break;
                    case DESCRIPTOR_TYPE_TIMER:{
                        /* if we not reading data from socket, he triggered again */
#ifdef DAP_OS_WINDOWS
                        l_bytes_read = dap_recvfrom(l_cur->socket, NULL, 0);
#elif defined(DAP_OS_LINUX)
                        uint64_t val;
                        read( l_cur->fd, &val, 8);
#endif
                        if (l_cur->callbacks.timer_callback)
                            l_cur->callbacks.timer_callback(l_cur);
                        else
                            log_it(L_ERROR, "Socket %"DAP_FORMAT_SOCKET" with timer callback fired, but callback is NULL ", l_cur->socket);

                    } break;
                    case DESCRIPTOR_TYPE_QUEUE:
                        dap_events_socket_queue_proc_input_unsafe(l_cur);
                        dap_events_socket_set_writable_unsafe(l_cur, false);
                        continue;
                    case DESCRIPTOR_TYPE_EVENT:
                        dap_events_socket_event_proc_input_unsafe(l_cur);
                    break;
                    default: break;
                }

                if (l_must_read_smth){ // Socket/Descriptor read
                    if(l_bytes_read > 0) {
                        if (l_cur->type == DESCRIPTOR_TYPE_SOCKET_CLIENT  || l_cur->type == DESCRIPTOR_TYPE_SOCKET_UDP) {
                            l_cur->last_time_active = l_cur_time;
                        }
                        l_cur->buf_in_size += l_bytes_read;
                        if(g_debug_reactor)
                            log_it(L_DEBUG, "Received %zd bytes for fd %d ", l_bytes_read, l_cur->fd);
                        if (l_cur->callbacks.read_callback) {
                            // Call callback to process read event. At the end of callback buf_in_size should be zero if everything was read well
                            l_cur->callbacks.read_callback(l_cur, l_cur->callbacks.arg);
                            if (l_cur->context == NULL ){ // esocket was unassigned in callback, we don't need any ops with it now,
                                                         // continue to poll another esockets
                                continue;
                            }
                        }else{
                            log_it(L_WARNING, "We have incoming %zd data but no read callback on socket %"DAP_FORMAT_SOCKET", removing from read set",
                                   l_bytes_read, l_cur->socket);
                            dap_events_socket_set_readable_unsafe(l_cur,false);
                        }
                    }
                    else if(l_bytes_read < 0) {
#ifdef DAP_OS_WINDOWS
                        if (l_cur->type != DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL && l_errno != WSAEWOULDBLOCK) {
                            log_it(L_ERROR, "Can't recv on socket %zu, WSA error: %d", l_cur->socket, l_errno);
#else
                        if (l_cur->type != DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL && l_errno != EAGAIN && l_errno != EWOULDBLOCK)
                        { // If we have non-blocking socket
                            log_it(L_ERROR, "recv() error %d: \"%s\"", l_errno, dap_strerror(errno));
#endif
                            dap_events_socket_set_readable_unsafe(l_cur, false);
                            if (!l_cur->no_close)
                                l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
                            l_cur->buf_out_size = 0;
                        }
#ifndef DAP_NET_CLIENT_NO_SSL
                        if (l_cur->type == DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL && l_errno != SSL_ERROR_WANT_READ && l_errno != SSL_ERROR_WANT_WRITE) {
                            char l_err_str[80];
                            wolfSSL_ERR_error_string(l_errno, l_err_str);
                            log_it(L_ERROR, "Some error occured in SSL read(): %s (code %d)", l_err_str, l_errno);
                            dap_events_socket_set_readable_unsafe(l_cur, false);
                            if (!l_cur->no_close)
                                l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
                            l_cur->buf_out_size = 0;
                        }
#endif
                    }
                    else if (!l_flag_rdhup && !l_flag_error && !(l_cur->flags & DAP_SOCK_CONNECTING )) {
                        log_it(L_DEBUG, "EPOLLIN triggered but nothing to read");
                        //dap_events_socket_set_readable_unsafe(l_cur,false);
                    }
                }
            }

            // Possibly have data to read despite EPOLLRDHUP
            if (l_flag_rdhup){
                switch (l_cur->type ){
                    case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
                    case DESCRIPTOR_TYPE_SOCKET_UDP:
                    case DESCRIPTOR_TYPE_SOCKET_CLIENT:
                    case DESCRIPTOR_TYPE_SOCKET_RAW:
                    case DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL:
                            dap_events_socket_set_readable_unsafe(l_cur, false);
                            dap_events_socket_set_writable_unsafe(l_cur, false);
                            l_cur->buf_out_size = 0;
                            l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
                            l_flag_error = l_flag_write = false;
                    break;
                    default:{}
                }
                if(g_debug_reactor)
                    log_it(L_DEBUG, "RDHUP event on esocket %p (%"DAP_FORMAT_SOCKET") type %d", l_cur, l_cur->socket, l_cur->type);
            }
            // If its outgoing connection
            if (l_flag_write && !(l_cur->flags & DAP_SOCK_SIGNAL_CLOSE) &&
                    ((!l_cur->server && l_cur->flags & DAP_SOCK_CONNECTING && l_cur->type == DESCRIPTOR_TYPE_SOCKET_CLIENT) ||
                    (l_cur->type == DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL && l_cur->flags & DAP_SOCK_CONNECTING))) {
                if (l_cur->type == DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL) {
#ifndef DAP_NET_CLIENT_NO_SSL
                    WOLFSSL *l_ssl = SSL(l_cur);
                    int l_res = wolfSSL_negotiate(l_ssl);
                    if (l_res != WOLFSSL_SUCCESS) {
                        char l_err_str[80];
                        int l_err = wolfSSL_get_error(l_ssl, l_res);
                        if (l_err != WOLFSSL_ERROR_WANT_READ && l_err != WOLFSSL_ERROR_WANT_WRITE) {
                            wolfSSL_ERR_error_string(l_err, l_err_str);
                            log_it(L_ERROR, "SSL handshake error \"%s\" with code %d", l_err_str, l_err);
                            if ( l_cur->callbacks.error_callback )
                                l_cur->callbacks.error_callback(l_cur, l_error);
                        }
                    } else {
                        if(g_debug_reactor)
                            log_it(L_NOTICE, "SSL handshake done with %s", l_cur->remote_addr_str);
                        l_cur->flags ^= DAP_SOCK_CONNECTING;
                        if (l_cur->callbacks.connected_callback)
                            l_cur->callbacks.connected_callback(l_cur);
                        dap_context_poll_update(l_cur);
                    }
#endif
                } else {
                    socklen_t l_error_len = sizeof(l_errno);

                    getsockopt(l_cur->socket, SOL_SOCKET, SO_ERROR, (void *)&l_errno, &l_error_len);
                    if(l_errno == EINPROGRESS) {
                        log_it(L_DEBUG, "Connecting with %s in progress...", l_cur->remote_addr_str);
                    }else if (l_errno){
                        log_it(L_ERROR, "Connecting with %s error %d: \"%s\"",
                                        l_cur->remote_addr_str, l_errno, dap_strerror(l_errno));
                        if ( l_cur->callbacks.error_callback )
                            l_cur->callbacks.error_callback(l_cur, l_errno);
                    }else{
                        debug_if(g_debug_reactor, L_NOTICE, "Connected with %s",l_cur->remote_addr_str);
                        l_cur->flags ^= DAP_SOCK_CONNECTING;
                        if (l_cur->callbacks.connected_callback)
                            l_cur->callbacks.connected_callback(l_cur);
                        dap_context_poll_update(l_cur);
                    }
                }
            }

            l_bytes_sent = 0;
            bool l_write_repeat = false;
            if (l_flag_write && (l_cur->flags & DAP_SOCK_READY_TO_WRITE) && !(l_cur->flags & DAP_SOCK_CONNECTING) && !(l_cur->flags & DAP_SOCK_SIGNAL_CLOSE)) {
                if (l_cur->callbacks.write_callback)
                    l_write_repeat = l_cur->callbacks.write_callback(l_cur, l_cur->callbacks.arg);  /* Call callback to process write event */
                debug_if(g_debug_reactor, L_DEBUG, "Main loop output: %zu bytes to send, repeat next time: %s",
                                                    l_cur->buf_out_size, l_write_repeat ? "true" : "false");
                /*
                 * Socket is ready to write and not going to close
                 */
                if ( l_cur->context && l_cur->buf_out_size ){ // esocket wasn't unassigned in callback, we need some other ops with it
                    switch (l_cur->type){
                    case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
                    case DESCRIPTOR_TYPE_SOCKET_CLIENT: {
                        l_bytes_sent = send(l_cur->socket, (const char *)l_cur->buf_out,
                                            l_cur->buf_out_size, MSG_DONTWAIT | MSG_NOSIGNAL);
                        if (l_bytes_sent == -1)
#ifdef DAP_OS_WINDOWS
                            l_errno = WSAGetLastError();
#else
                            l_errno = errno;
#endif
                        else
                            l_errno = 0;
                    }
                    break;
                    case DESCRIPTOR_TYPE_SOCKET_UDP:
                        l_bytes_sent = sendto(l_cur->socket, (const char *)l_cur->buf_out,
                                              l_cur->buf_out_size, MSG_DONTWAIT | MSG_NOSIGNAL,
                                              (struct sockaddr *)&l_cur->addr_storage, l_cur->addr_size);
#ifdef DAP_OS_WINDOWS
                        dap_events_socket_set_writable_unsafe(l_cur,false);
                        l_errno = WSAGetLastError();
#else
                        l_errno = errno;
#endif
                    break;
                    case DESCRIPTOR_TYPE_SOCKET_RAW:
                        if ( l_cur->flags & DAP_SOCK_MSG_ORIENTED ) { 
                            struct iovec iov = { l_cur->buf_out, l_cur->buf_out_size_max - l_cur->buf_out_size };
                            struct msghdr msg = { .msg_name = &l_cur->addr_storage, .msg_namelen = l_cur->addr_size, .msg_iov = &iov, .msg_iovlen = 1 };
                            l_bytes_sent = sendmsg(l_cur->fd, &msg, 0);
                        } else
                            l_bytes_sent = sendto(l_cur->socket, (const char *)l_cur->buf_out,
                                                  l_cur->buf_out_size, MSG_DONTWAIT | MSG_NOSIGNAL,
                                                  (struct sockaddr*)&l_cur->addr_storage, l_cur->addr_size);
                        l_errno = errno;
                    break;
                    case DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL: {
#ifndef DAP_NET_CLIENT_NO_SSL
                        WOLFSSL *l_ssl = SSL(l_cur);
                        l_bytes_sent = wolfSSL_write(l_ssl, (char *)(l_cur->buf_out), l_cur->buf_out_size);
                        if (l_bytes_sent > 0)
                            log_it(L_DEBUG, "SSL write: %s", (char *)(l_cur->buf_out));
                        l_errno = wolfSSL_get_error(l_ssl, 0);
#endif
                    }
                    case DESCRIPTOR_TYPE_QUEUE:
                        if (l_cur->flags & DAP_SOCK_QUEUE_PTR && l_cur->buf_out_size>= sizeof (void*)) {
#if defined(DAP_EVENTS_CAPS_QUEUE_PIPE2)
                            l_bytes_sent = write(l_cur->fd, l_cur->buf_out, /* sizeof(void *) */ l_cur->buf_out_size);
                            l_errno = l_bytes_sent < (ssize_t)l_cur->buf_out_size ? errno : 0;
                            debug_if(l_errno, L_ERROR, "Writing to pipe %zu bytes failed, sent %zd only...", l_cur->buf_out_size, l_bytes_sent);
#elif defined (DAP_EVENTS_CAPS_QUEUE_POSIX)
                            l_bytes_sent = mq_send(a_es->mqd, (const char *)&a_arg,sizeof (a_arg),0);
#elif defined (DAP_EVENTS_CAPS_QUEUE_MQUEUE)
                            l_bytes_sent = mq_send(l_cur->mqd , (const char *)l_cur->buf_out,sizeof (void*),0);
                            if(l_bytes_sent == 0)
                                l_bytes_sent = sizeof (void*);
                            l_errno = errno;
                            if (l_bytes_sent == -1 && l_errno == EINVAL) // To make compatible with other
                                l_errno = EAGAIN;                        // non-blocking sockets
#elif defined (DAP_EVENTS_CAPS_KQUEUE)
                            struct kevent* l_event=&l_cur->kqueue_event;
                            dap_events_socket_w_data_t * l_es_w_data = DAP_NEW_Z(dap_events_socket_w_data_t);
                            l_es_w_data->esocket = l_cur;
                            memcpy(&l_es_w_data->ptr, l_cur->buf_out,sizeof(l_cur));
                            EV_SET(l_event,l_cur->socket, l_cur->kqueue_base_filter,l_cur->kqueue_base_flags, l_cur->kqueue_base_fflags,l_cur->kqueue_data, l_es_w_data);
                            int l_n = kevent(a_context->kqueue_fd,l_event,1,NULL,0,NULL);
                            if (l_n == 1){
                                l_bytes_sent = sizeof(l_cur);
                            }else{
                                l_errno = errno;
                                log_it(L_WARNING,"queue ptr send error: kevent %p errno: %d", l_es_w_data, l_errno);
                                DAP_DELETE(l_es_w_data);
                            }

#else
#error "Not implemented dap_events_socket_queue_ptr_send() for this platform"
#endif
                        }else{
                             assert("Not implemented non-ptr queue send from outgoing buffer");
                             // TODO Implement non-ptr queue output
                         }
                    break;
                    case DESCRIPTOR_TYPE_PIPE:
                    case DESCRIPTOR_TYPE_FILE:
                        l_bytes_sent = write(l_cur->fd, (char *) (l_cur->buf_out), l_cur->buf_out_size );
                        l_errno = errno;
                    break;
                    default:
                        log_it(L_WARNING, "Socket %"DAP_FORMAT_SOCKET" is not SOCKET, PIPE or FILE but has WRITE state on. Switching it off", l_cur->socket);
                        dap_events_socket_set_writable_unsafe(l_cur,false);
                    }

                    if (l_bytes_sent < 0) {
#ifdef DAP_OS_WINDOWS
                        if (l_cur->type != DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL && l_errno != WSAEWOULDBLOCK) {
                            log_it(L_ERROR, "Can't send to socket %zu, WSA error: %d", l_cur->socket, l_errno);
#else
                        if (l_cur->type != DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL && l_errno != EAGAIN && l_errno != EWOULDBLOCK)
                        { // If we have non-blocking socket
                            log_it(L_ERROR, "send() error %d: \"%s\"", l_errno, dap_strerror(l_errno));
#endif
                            if (!l_cur->no_close)
                                l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
                            l_cur->buf_out_size = 0;
                        }
#ifndef DAP_NET_CLIENT_NO_SSL
                        if (l_cur->type == DESCRIPTOR_TYPE_SOCKET_CLIENT_SSL && l_errno != SSL_ERROR_WANT_READ && l_errno != SSL_ERROR_WANT_WRITE) {
                            char l_err_str[80];
                            wolfSSL_ERR_error_string(l_errno, l_err_str);
                            log_it(L_ERROR, "Some error occured in SSL write(): %s (code %d)", l_err_str, l_errno);
                            if (!l_cur->no_close)
                                l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
                            l_cur->buf_out_size = 0;
                        }
#endif
                    } else if (l_bytes_sent) {
                        debug_if(g_debug_reactor, L_DEBUG, "Output: %zu from %zu bytes are sent", l_bytes_sent, l_cur->buf_out_size);
                        if (l_bytes_sent <= (ssize_t) l_cur->buf_out_size) {
                            l_cur->buf_out_size -= l_bytes_sent;
                            if (l_cur->buf_out_size)
                                memmove(l_cur->buf_out, &l_cur->buf_out[l_bytes_sent], l_cur->buf_out_size);
                            else if (l_cur->callbacks.write_finished_callback)    /* Optionaly call I/O completion routine */
                                l_cur->callbacks.write_finished_callback(l_cur, l_cur->callbacks.arg);
                        } else {
                            log_it(L_ERROR, "Wrong bytes sent, %zd more then was in buffer %zd",l_bytes_sent, l_cur->buf_out_size);
                            l_cur->buf_out_size = 0;
                        }
                    }
                }
                /*
                 * If whole buffer has been sent - clear "write flag" for socket/file descriptor to prevent
                 * generation of unexpected I/O events like POLLOUT and consuming CPU by this.
                 */
                if (!l_cur->buf_out_size && !l_write_repeat)
                    dap_events_socket_set_writable_unsafe(l_cur, false); /* Clear "enable write flag" */
            }

            if (l_cur->flags & DAP_SOCK_SIGNAL_CLOSE)
            {
                if (l_cur->buf_out_size == 0 || !l_flag_write) {
                    if(g_debug_reactor)
                        log_it(L_INFO, "Process signal to close %s sock %"DAP_FORMAT_SOCKET" (ptr %p uuid 0x%016"DAP_UINT64_FORMAT_x") type %d [context #%u]",
                           l_cur->remote_addr_str, l_cur->socket, l_cur, l_cur->uuid,
                               l_cur->type, a_context->id);

                    for (ssize_t nn = n + 1; nn < l_sockets_max; nn++) { // Check for current selection if it has event duplication
                        dap_events_socket_t *l_es_selected = NULL;
#ifdef DAP_EVENTS_CAPS_EPOLL
                        l_es_selected = (dap_events_socket_t *) l_epoll_events[nn].data.ptr;
#elif defined ( DAP_EVENTS_CAPS_POLL)
                        l_es_selected = a_context->poll_esocket[nn];
#elif defined (DAP_EVENTS_CAPS_KQUEUE)
                        struct kevent * l_kevent_selected = &a_context->kqueue_events_selected[nn];
                        if ( l_kevent_selected->filter == EVFILT_USER){ // If we have USER event it sends little different pointer
                            dap_events_socket_w_data_t * l_es_w_data = (dap_events_socket_w_data_t *) l_kevent_selected->udata;
                            l_es_selected = l_es_w_data ? l_es_w_data->esocket : NULL;
                            // Clean up l_es_w_data if it's not the same as the cached data - JUST LLM proposed, not tested
                            //if (l_es_w_data && l_es_selected && l_es_w_data != &l_es_selected->kqueue_event_catched_data)
                            //    DAP_DELETE(l_es_w_data);
                         }else{
                            l_es_selected = (dap_events_socket_t*) l_kevent_selected->udata;
                        }
#else
#error "No selection esockets left to proc implemenetation"
#endif
                        if (!l_es_selected || l_es_selected == l_cur) {
                            if (g_debug_reactor) {
                                if (!l_es_selected)
                                    log_it(L_ATT,"NULL esocket found when cleaning selected list at index %zd/%zd", nn, l_sockets_max);
                                else 
                                    log_it(L_ATT,"Duplicate esockets %" DAP_FORMAT_SOCKET " removed from selected event list at index %zd/%zd",
                                                                                             l_es_selected->socket, nn, l_sockets_max);
                            }
                            n=nn; // TODO here we need to make smth like poll() array compressing.
                                  // Here we expect thats event duplicates goes together in it. If not - we lose some events between.
                        }
                    }
                    dap_events_socket_remove_and_delete_unsafe( l_cur, false);
#ifdef DAP_EVENTS_CAPS_KQUEUE
                    a_context->kqueue_events_count--;
#endif
                } else {
                    if(g_debug_reactor)
                        log_it(L_INFO, "Got signal to close %s sock %"DAP_FORMAT_SOCKET" [context #%u] type %d but buffer is not empty(%zu)",
                           l_cur->remote_addr_str, l_cur->socket, l_cur->type, a_context->id,
                           l_cur->buf_out_size);
                }
            }

        }
#ifdef DAP_EVENTS_CAPS_POLL
        /***********************************************************/
        /* If the compress_array flag was turned on, we need       */
        /* to squeeze together the array and decrement the number  */
        /* of file descriptors.                                    */
        /***********************************************************/
        if ( a_context->poll_compress){
            a_context->poll_compress = false;
            for (size_t i = 0; i < a_context->poll_count ; i++)  {
                if ( a_context->poll[i].fd == -1){
                    if( a_context->poll_count){
                        for(size_t j = i; j < a_context->poll_count-1; j++){
                             a_context->poll[j].fd = a_context->poll[j+1].fd;
                             a_context->poll[j].events = a_context->poll[j+1].events;
                             a_context->poll[j].revents = a_context->poll[j+1].revents;
                             a_context->poll_esocket[j] = a_context->poll_esocket[j+1];
                             if(a_context->poll_esocket[j])
                                 a_context->poll_esocket[j]->poll_index = j;
                        }
                    }
                    i--;
                    a_context->poll_count--;
                }
            }
        }
#endif
    } while(!a_context->signal_exit);
#endif // IOCP

    log_it(L_ATT,"Context :%u finished", a_context->id);
    return 0;
}
