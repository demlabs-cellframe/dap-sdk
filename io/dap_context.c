/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2022
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
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#if ! defined (_GNU_SOURCE)
#define _GNU_SOURCE         /* See feature_test_macros(7) */
#endif
#include <fcntl.h>
#include <sys/types.h>
#ifdef DAP_OS_UNIX
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/resource.h>
#elif defined DAP_OS_WINDOWS
#include <ws2tcpip.h>
#endif

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

#ifdef DAP_OS_DARWIN
#define NOTE_READ NOTE_LOWAT

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL SO_NOSIGPIPE
#endif

#endif

#if defined (DAP_EVENTS_CAPS_QUEUE_MQUEUE)
#include <sys/time.h>
#include <sys/resource.h>
#endif

#define LOG_TAG "dap_context"

#include "dap_common.h"
#include "dap_uuid.h"
#include "dap_context.h"
#include "dap_list.h"
#include "dap_events.h"
#include "dap_proc_thread.h"
#include "dap_worker.h"

static _Thread_local dap_context_t *s_context = NULL;

static void *s_context_thread(void *arg); // Context thread
/**
 * @brief dap_context_init
 * @return
 */
int dap_context_init()
{
#ifdef DAP_OS_UNIX
    struct rlimit l_fdlimit;
    if (getrlimit(RLIMIT_NOFILE, &l_fdlimit))
        return -1;

    rlim_t l_oldlimit = l_fdlimit.rlim_cur;
    l_fdlimit.rlim_cur = l_fdlimit.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &l_fdlimit))
        return -2;
    log_it(L_INFO, "Set maximum opened descriptors from %" DAP_UINT64_FORMAT_U " to %"DAP_UINT64_FORMAT_U, l_oldlimit, l_fdlimit.rlim_cur);
#endif
    return 0;
}

void dap_context_deinit()
{
}

dap_context_t* dap_context_current() {
    return s_context;
}

/**
 * @brief dap_context_new
 * @return
 */
dap_context_t * dap_context_new(int a_type)
{
   dap_context_t * l_context = DAP_NEW_Z(dap_context_t);
   if (!l_context) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
   }
   static atomic_uint_fast64_t s_context_id_max = 0;
   l_context->id = s_context_id_max;
   l_context->type = a_type;
   s_context_id_max++;
   return l_context;
}

/**
 * @brief dap_context_run     Run new context in dedicated thread.
 * @param a_context           Context object
 * @param a_cpu_id            CPU id on wich it will be assigned (if platform allows). -1 means no CPU affinity
 * @param a_sched_policy      Schedule policy
 * @param a_priority          Thread priority. 0 means default
 * @param a_flags             Flags specified context. 0 if default
 * @param a_callback_loop_before  Callback thats executes in thread just after initializetion but before main loop begins
 * @param a_callback_loop_after  Callback thats executes in thread just after main loop stops
 * @param a_callback_arg Custom argument for callbacks
 * @return Returns zero if succes, others if error (pthread_create() return code)
 */
int dap_context_run(dap_context_t * a_context,int a_cpu_id, int a_sched_policy, int a_priority,
                    uint32_t a_flags,
                    dap_context_callback_t a_callback_loop_before,
                    dap_context_callback_t a_callback_loop_after,
                    void * a_callback_arg )
{
    dap_context_msg_run_t * l_msg = DAP_NEW_Z(dap_context_msg_run_t);
    int l_ret;

    // Check for OOM
    if(! l_msg){
        log_it(L_CRITICAL, "Can't allocate memory for context create message");
        return ENOMEM;
    }

    // Prefill message structure for new context's thread
    l_msg->context = a_context;
    l_msg->priority = a_priority;
    l_msg->sched_policy = a_sched_policy;
    l_msg->cpu_id = a_cpu_id;
    l_msg->flags = a_flags;
    l_msg->callback_started = a_callback_loop_before;
    l_msg->callback_stopped = a_callback_loop_after;
    l_msg->callback_arg = a_callback_arg;

    // If we have to wait for started thread (and initialization inside )
    if( a_flags & DAP_CONTEXT_FLAG_WAIT_FOR_STARTED){
        // Init kernel objects
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
#ifndef DAP_OS_DARWIN
        pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
        pthread_mutex_init(&a_context->started_mutex, NULL);
        pthread_cond_init( &a_context->started_cond, &attr);

        // Prepare timer
        struct timespec l_timeout;
        clock_gettime(CLOCK_REALTIME, &l_timeout);
        l_timeout.tv_sec += DAP_CONTEXT_WAIT_FOR_STARTED_TIME;
        // Lock started mutex and try to run a thread
        pthread_mutex_lock(&a_context->started_mutex);

        l_ret = pthread_create(&a_context->thread_id, NULL, s_context_thread, l_msg);

        if(l_ret == 0){ // If everything is good we're waiting for DAP_CONTEXT_WAIT_FOR_STARTED_TIME seconds
            while (!a_context->started && !l_ret)
                l_ret = pthread_cond_timedwait(&a_context->started_cond, &a_context->started_mutex, &l_timeout);
            if ( l_ret== ETIMEDOUT ){ // Timeout
                log_it(L_CRITICAL, "Timeout %d seconds is out: context #%u thread don't respond", DAP_CONTEXT_WAIT_FOR_STARTED_TIME,a_context->id);
            } else if (l_ret != 0){ // Another error
                log_it(L_CRITICAL, "Can't wait on condition: %d error code", l_ret);
            } else // All is good
                log_it(L_NOTICE, "Context %u started", a_context->id);
        }else{ // Thread haven't started
            log_it(L_ERROR,"Can't create new thread for context %u", a_context->id );
            DAP_DELETE(l_msg);
        }
        pthread_mutex_unlock(&a_context->started_mutex);
    }else{ // Here we wait for nothing, just run it
        l_ret = pthread_create( &a_context->thread_id , NULL, s_context_thread, l_msg);
        if(l_ret != 0){ // Check for error, if present lets cleanup the memory for l_msg
            log_it(L_ERROR,"Can't create new thread for context %u", a_context->id );
            DAP_DELETE(l_msg);
        }
    }
    return l_ret;
}

/**
 * @brief dap_context_stop_n_kill
 * @param a_context
 */
void dap_context_stop_n_kill(dap_context_t * a_context)
{
    pthread_t l_thread_id = a_context->thread_id;
    switch (a_context->type) {
    case DAP_CONTEXT_TYPE_WORKER:
        dap_events_socket_event_signal(a_context->event_exit, 1);
        break;
    case DAP_CONTEXT_TYPE_PROC_THREAD: {
        dap_proc_thread_t *l_thread = DAP_PROC_THREAD(a_context);
        pthread_mutex_lock(&l_thread->queue_lock);
        a_context->signal_exit = true;
        pthread_cond_signal(&l_thread->queue_event);
        pthread_mutex_unlock(&l_thread->queue_lock);
    }
    default:
        break;
    }
    pthread_join(l_thread_id, NULL);
}



/**
 * @brief s_context_thread Context working thread
 * @param arg
 * @return
 */
static void *s_context_thread(void *a_arg)
{
    dap_context_msg_run_t * l_msg = (dap_context_msg_run_t*) a_arg;
    dap_context_t * l_context = l_msg->context;
    assert(l_context);
    if (s_context)
        return log_it( L_ERROR, "Context %d already bound to current thread", s_context->id ), NULL;
    s_context = l_context;
    l_context->cpu_id = l_msg->cpu_id;
    int l_priority = l_msg->priority;
#ifdef DAP_OS_WINDOWS
    switch (l_priority) {
    case THREAD_PRIORITY_TIME_CRITICAL:
    case THREAD_PRIORITY_HIGHEST:
    case THREAD_PRIORITY_ABOVE_NORMAL:
    case THREAD_PRIORITY_BELOW_NORMAL:
    case THREAD_PRIORITY_LOWEST:
    case THREAD_PRIORITY_IDLE:
        break;
    default:
        l_priority = THREAD_PRIORITY_NORMAL;
    }
    if ( !DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &l_context->th, 0, FALSE, DUPLICATE_SAME_ACCESS) )
        log_it(L_ERROR, "DuplicateHandle() failed, error %d: \"%s\"", GetLastError(), dap_strerror(GetLastError()));
    if ( !SetThreadAffinityMask( l_context->th, (DWORD_PTR)(1 << l_msg->cpu_id) ) ) 
        log_it(L_ERROR, "SetThreadAffinityMask() failed, error %d: \"%s\"", GetLastError(), dap_strerror(GetLastError()));
    if ( !SetThreadPriority(l_context->th, l_priority) )
        log_it(L_ERROR, "Couldn't set thread priority, error %d: \"%s\"", GetLastError(), dap_strerror(GetLastError()));
#else
    if(l_msg->cpu_id!=-1)
        dap_cpu_assign_thread_on(l_msg->cpu_id );
    if (l_msg->sched_policy != DAP_CONTEXT_POLICY_DEFAULT) {
        struct sched_param l_sched_params = {0};
        int l_sched_policy;

        switch(l_msg->sched_policy) {
        case DAP_CONTEXT_POLICY_FIFO: l_sched_policy = SCHED_FIFO; break;
        case DAP_CONTEXT_POLICY_ROUND_ROBIN: l_sched_policy = SCHED_RR; break;
        default:
#if defined (DAP_OS_LINUX)
            l_sched_policy = SCHED_BATCH;
#else
            l_sched_policy = SCHED_OTHER;
#endif  // DAP_OS_LINUX
        }
        int l_prio_min = sched_get_priority_min(l_sched_policy);
        int l_prio_max = sched_get_priority_max(l_sched_policy);
        switch (l_priority) {
        case DAP_CONTEXT_PRIORITY_NORMAL:
            l_priority = (l_prio_max - l_prio_min) / 2;
            break;
        case DAP_CONTEXT_PRIORITY_HIGH:
            l_priority = l_prio_max - (l_prio_max / 5);
            break;
        case DAP_CONTEXT_PRIORITY_LOW:
            l_priority = l_prio_min + (l_prio_max / 5);
        }
        if (l_priority < l_prio_min)
            l_priority = l_prio_min;
        if (l_priority > l_prio_max)
            l_priority = l_prio_max;
        l_sched_params.sched_priority = l_priority;
        pthread_setschedparam(pthread_self(), l_sched_policy, &l_sched_params);;
    }
#endif // DAP_OS_WINDOWS
    // Now we're running and initalized for sure, so we can assign flags to the current context
    l_context->running_flags = l_msg->flags;
    l_context->is_running = true;
    // Started callback execution
    if (l_msg->callback_started &&
            l_msg->callback_started(l_context, l_msg->callback_arg))
        // Can't initialize
        l_context->signal_exit = true;
    if (l_msg->flags & DAP_CONTEXT_FLAG_WAIT_FOR_STARTED) {
        pthread_mutex_lock(&l_context->started_mutex); // If we're too fast and calling thread haven't switched on cond_wait line
        l_context->started = true;
        pthread_cond_broadcast(&l_context->started_cond);
        pthread_mutex_unlock(&l_context->started_mutex);
    }
    if (l_context->signal_exit)
        return NULL;
    // Initialization success
    switch (l_context->type) {
    case DAP_CONTEXT_TYPE_WORKER:
        dap_worker_thread_loop(l_context);
        break;
    case DAP_CONTEXT_TYPE_PROC_THREAD:
        dap_proc_thread_loop(l_context);
    default:
        break;
    }
    // Stopped callback execution
    if (l_msg->callback_stopped)
        l_msg->callback_stopped(l_context, l_msg->callback_arg);

    log_it(L_NOTICE,"Exiting context #%u", l_context->id);

    // Free memory. Because nobody expected to work with context outside itself it have to be safe
    pthread_cond_destroy(&l_context->started_cond);
    pthread_mutex_destroy(&l_context->started_mutex);
    DAP_DELETE(l_context);
    DAP_DELETE(l_msg);

    return NULL;
}

/** Need be moved back to worker
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
                    l_cur->context 
                        ? dap_events_socket_remove_and_delete_unsafe(l_cur, FLAG_KEEP_INHERITOR(l_cur->flags))
                        : dap_events_socket_delete_unsafe(l_cur, FLAG_KEEP_INHERITOR(l_cur->flags));                    
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
    socklen_t l_error_len = sizeof(l_errno);
    char l_error_buf[128] = {0};
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
            if( errno == EINTR)
                continue;
#ifdef DAP_OS_WINDOWS
            log_it(L_ERROR, "Context thread %d got errno %d", a_context->id, WSAGetLastError());
#else
            log_it(L_ERROR, "Context thread %d got error: %d: \"%s\"", a_context->id, errno, dap_strerror(errno));
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
            case DESCRIPTOR_TYPE_SOCKET_RAW:
            case DESCRIPTOR_TYPE_SOCKET_CLIENT:
            case DESCRIPTOR_TYPE_SOCKET_UDP:
            case DESCRIPTOR_TYPE_SOCKET_LISTENING:
#ifdef DAP_OS_UNIX
            case DESCRIPTOR_TYPE_SOCKET_LOCAL_LISTENING:
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
                case DESCRIPTOR_TYPE_SOCKET_RAW:
                case DESCRIPTOR_TYPE_SOCKET_UDP:
                case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
                case DESCRIPTOR_TYPE_SOCKET_CLIENT: {
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
                        log_it(L_INFO, "Socket shutdown (EPOLLHUP): %s", strerror(l_sock_err));
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
#ifdef DAP_OS_WINDOWS
                    log_it(L_ERROR, "Winsock error: %d", l_sock_err);
#else
                    log_it(L_ERROR, "Socket error: %s", strerror(l_sock_err));
#endif
                default: ;
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
                // Security fix: improve buffer overflow protection
                if(l_cur->buf_in_size_max && l_cur->buf_in_size >= l_cur->buf_in_size_max ) {
                    log_it(L_WARNING, "Buffer is full when there is smth to read. Its dropped! esocket %p (%"DAP_FORMAT_SOCKET")", l_cur, l_cur->socket);
                    l_cur->buf_in_size = 0;
                }
                
                // Additional security check: ensure we have valid buffer space
                if (!l_cur->buf_in || !l_cur->buf_in_size_max || l_cur->buf_in_size > l_cur->buf_in_size_max) {
                    log_it(L_ERROR, "Invalid buffer state for reading");
                    continue;
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
                                                (struct sockaddr*)&l_cur->addr_storage, &l_cur->addr_size);

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
                                    log_it(L_WARNING,"accept() on socket %d error %d: \"%s\"",l_cur->socket, l_errno, dap_strerror(l_errno));
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
                            log_it(L_ERROR, "Some error occured in recv() function: %s", strerror(errno));
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
                    case DESCRIPTOR_TYPE_SOCKET_RAW:
                    case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
                    case DESCRIPTOR_TYPE_SOCKET_UDP:
                    case DESCRIPTOR_TYPE_SOCKET_CLIENT:
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
                            l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
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
                    l_error_len = sizeof(l_errno);
                    getsockopt(l_cur->socket, SOL_SOCKET, SO_ERROR, (void *)&l_errno, &l_error_len);
                    if(l_errno == EINPROGRESS) {
                        log_it(L_DEBUG, "Connecting with %s in progress...", l_cur->remote_addr_str);
                    }else if (l_errno){
                        log_it(L_ERROR,"Connecting with %s failed, error %d: \"%s\"", l_cur->remote_addr_str,
                                        l_errno, dap_strerror(l_errno));
                        l_cur->flags |= DAP_SOCK_SIGNAL_CLOSE;
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
                                              (struct sockaddr*)&l_cur->addr_storage, l_cur->addr_size);
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
                            log_it(L_ERROR, "Some error occured in send(): %s (code %d)", strerror(l_errno), l_errno);
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
                        if (l_cur->type == DESCRIPTOR_TYPE_SOCKET_CLIENT || l_cur->type == DESCRIPTOR_TYPE_SOCKET_UDP)
                            l_cur->last_time_active = l_cur_time;
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
                        struct kevent * l_kevent_selected = &a_context->kqueue_events_selected[n];
                        if ( l_kevent_selected->filter == EVFILT_USER){ // If we have USER event it sends little different pointer
                            dap_events_socket_w_data_t * l_es_w_data = (dap_events_socket_w_data_t *) l_kevent_selected->udata;
                            l_es_selected = l_es_w_data->esocket;
                        }else{
                            l_es_selected = (dap_events_socket_t*) l_kevent_selected->udata;
                        }
#else
#error "No selection esockets left to proc implemenetation"
#endif
                        if(l_es_selected == NULL || l_es_selected == l_cur ){
                            if(l_es_selected == NULL)
                                log_it(L_CRITICAL,"NULL esocket found when cleaning selected list");
                            else if(g_debug_reactor)
                                log_it(L_INFO,"Duplicate esockets removed from selected event list");
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

/**
 * @brief dap_context_poll_update
 * @param a_esocket
 */
int dap_context_poll_update(dap_events_socket_t * a_esocket)
{
#if defined DAP_EVENTS_CAPS_IOCP
    // There's no proper way, neither a need to do this when running IOCP
#elif defined (DAP_EVENTS_CAPS_EPOLL)
    int events = a_esocket->ev_base_flags | EPOLLERR;

    // Check & add
    if( a_esocket->flags & DAP_SOCK_READY_TO_READ )
        events |= EPOLLIN;

    if( a_esocket->flags & DAP_SOCK_READY_TO_WRITE || a_esocket->flags &DAP_SOCK_CONNECTING )
        events |= EPOLLOUT;

    a_esocket->ev.events = events;

    if( a_esocket->context){
        if ( epoll_ctl(a_esocket->context->epoll_fd, EPOLL_CTL_MOD, a_esocket->socket, &a_esocket->ev) ){
#ifdef DAP_OS_WINDOWS
            int l_errno = WSAGetLastError();
#else
            int l_errno = errno;
#endif
            return log_it(L_CRITICAL, "Error updating client socket state in the epoll_fd %"DAP_FORMAT_HANDLE": \"%s\" (%d)",
                a_esocket->context->epoll_fd, dap_strerror(l_errno), l_errno), -1;
        }
    }

#elif defined (DAP_EVENTS_CAPS_POLL)
    if( a_esocket->context && a_esocket->is_initalized){
        if (a_esocket->poll_index < a_esocket->context->poll_count ){
            struct pollfd * l_poll = &a_esocket->context->poll[a_esocket->poll_index];
            l_poll->events = a_esocket->poll_base_flags | POLLERR ;
            // Check & add
            if( a_esocket->flags & DAP_SOCK_READY_TO_READ )
                l_poll->events |= POLLIN;
            if( a_esocket->flags & DAP_SOCK_READY_TO_WRITE || a_esocket->flags &DAP_SOCK_CONNECTING )
                l_poll->events |= POLLOUT;
        }else{
            log_it(L_ERROR, "Wrong poll index when remove from context (unsafe): %u when total count %u", a_esocket->poll_index,
                   a_esocket->context->poll_count);
            return -666;
        }
    }

#elif defined (DAP_EVENTS_CAPS_KQUEUE)
    if (a_esocket->socket != -1  ){ // Not everything we add in poll
        struct kevent * l_event = &a_esocket->kqueue_event;
        short l_filter  =a_esocket->kqueue_base_filter;
        u_short l_flags =a_esocket->kqueue_base_flags;
        u_int l_fflags =a_esocket->kqueue_base_fflags;

        int l_kqueue_fd = a_esocket->context->kqueue_fd;
        if ( l_kqueue_fd == -1 ){
            log_it(L_ERROR, "Esocket is not assigned with anything ,exit");
        }

        // Check & add
        bool l_is_error=false;
        int l_errno=0;
        if (a_esocket->type == DESCRIPTOR_TYPE_EVENT || a_esocket->type == DESCRIPTOR_TYPE_QUEUE ){
            // Do nothing
        }else{
            EV_SET(l_event, a_esocket->socket, l_filter,l_flags| EV_ADD,l_fflags,a_esocket->kqueue_data,a_esocket);
            if (l_filter) {
                if( kevent( l_kqueue_fd,l_event,1,NULL,0,NULL) == -1 ){
                    l_is_error = true;
                    l_errno = errno;
                }
            }
            if (!l_is_error) {
                if( a_esocket->flags & DAP_SOCK_READY_TO_READ ){
                    EV_SET(l_event, a_esocket->socket, EVFILT_READ,l_flags| EV_ADD,l_fflags,a_esocket->kqueue_data,a_esocket);
                    if( kevent( l_kqueue_fd,l_event,1,NULL,0,NULL) == -1 ){
                        l_is_error = true;
                        l_errno = errno;
                    }
                }
            }
            if( !l_is_error){
                if( a_esocket->flags & DAP_SOCK_READY_TO_WRITE || a_esocket->flags &DAP_SOCK_CONNECTING ){
                    EV_SET(l_event, a_esocket->socket, EVFILT_WRITE,l_flags| EV_ADD,l_fflags,a_esocket->kqueue_data,a_esocket);
                    if(kevent( l_kqueue_fd,l_event,1,NULL,0,NULL) == -1){
                        l_is_error = true;
                        l_errno = errno;
                    }
                }
            }
        }
        if (l_is_error && l_errno == EBADF){
            log_it(L_ATT,"Poll update: socket %d (%p ) disconnected, rise CLOSE flag to remove from queue, lost %zu:%zu bytes",
                   a_esocket->socket,a_esocket,a_esocket->buf_in_size,a_esocket->buf_out_size);
            a_esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
            a_esocket->buf_in_size = a_esocket->buf_out_size = 0; // Reset everything from buffer, we close it now all
        }else if ( l_is_error && l_errno != EINPROGRESS && l_errno != ENOENT){
            char l_errbuf[128];
            l_errbuf[0]=0;
            strerror_r(l_errno, l_errbuf, sizeof (l_errbuf));
            log_it(L_ERROR,"Can't update client socket state on kqueue fd %d: \"%s\" (%d)",
                l_kqueue_fd, l_errbuf, l_errno);
        }
    }

#else
#error "Not defined dap_events_socket_set_writable_unsafe for your platform"
#endif
    return 0;
}


/**
 * @brief dap_context_add_events_socket_unsafe
 * @param IOa_context
 * @param a_esocket
 */
int dap_context_add(dap_context_t * a_context, dap_events_socket_t * a_es )
{
    // Check & add
    bool l_is_error=false;
    int l_errno=0;

    if(a_es == NULL){
        log_it(L_ERROR, "Can't add NULL esocket to the context");
        return -1;
    }
    if (a_context == NULL || a_context->type != DAP_CONTEXT_TYPE_WORKER) {
        log_it(L_ERROR, "Can't add esocket to the bad context");
        return -2;
    }

#ifdef DAP_EVENTS_CAPS_IOCP
    // TODO: reassignment requires some extra calls to WDK. Also there must be no pending I/O ops
    /*
        #include <ntifs.h>
        int len = sizeof(FILE_COMPLETION_INFORMATION);
        FILE_COMPLETION_INFORMATION fci = (FILE_COMPLETION_INFORMATION) {
            .Port = NULL
        };
        IO_STATUS_BLOCK iosb;
        if ( STATUS_SUCCESS == 
            NtSetInformationFile((HANDLE)a_es->socket, &iosb, &fci, len, FileReplaceCompletionInformation) )
        { //ok };
    */
    if ( a_es->socket && a_es->socket != INVALID_SOCKET
          && !(a_context->iocp = CreateIoCompletionPort((HANDLE)a_es->socket, a_context->iocp, (ULONG_PTR)a_es, 0)) ) {
        l_errno = GetLastError();
        l_is_error = true;
    } else {
        debug_if(g_debug_reactor, L_DEBUG, "Es \"%s\" "DAP_FORMAT_ESOCKET_UUID" added to context #%d IOCP", 
                 dap_events_socket_get_type_str(a_es), a_es->uuid, a_context->id);
    }
#elif defined DAP_EVENTS_CAPS_EPOLL
    // Init events for EPOLL
    a_es->ev.events = a_es->ev_base_flags ;
    if(a_es->flags & DAP_SOCK_READY_TO_READ )
        a_es->ev.events |= EPOLLIN;
    if(a_es->flags & DAP_SOCK_READY_TO_WRITE )
        a_es->ev.events |= EPOLLOUT;
    a_es->ev.data.ptr = a_es;
    int l_ret = epoll_ctl(a_context->epoll_fd, EPOLL_CTL_ADD, a_es->socket, &a_es->ev);
    if (l_ret != 0 ){
        l_is_error = true;
        l_errno = errno;
    }
#elif defined (DAP_EVENTS_CAPS_POLL)
    if (  a_context->poll_count == a_context->poll_count_max ){ // realloc
        a_context->poll_count_max *= 2;
        log_it(L_WARNING, "Too many descriptors (%u), resizing array twice to %zu", a_context->poll_count, a_context->poll_count_max);
        a_context->poll =DAP_REALLOC(a_context->poll, a_context->poll_count_max * sizeof(*a_context->poll));
        a_context->poll_esocket =DAP_REALLOC(a_context->poll_esocket, a_context->poll_count_max * sizeof(*a_context->poll_esocket));
    }
    a_context->poll[a_context->poll_count].fd = a_es->socket;
    a_es->poll_index = a_context->poll_count;
    a_context->poll[a_context->poll_count].events = a_es->poll_base_flags;
    if( a_es->flags & DAP_SOCK_READY_TO_READ )
        a_context->poll[a_context->poll_count].events |= POLLIN;
    if( (a_es->flags & DAP_SOCK_READY_TO_WRITE) || (a_es->flags & DAP_SOCK_CONNECTING) )
        a_context->poll[a_context->poll_count].events |= POLLOUT;


    a_context->poll_esocket[a_context->poll_count] = a_es;
    a_context->poll_count++;
#elif defined (DAP_EVENTS_CAPS_KQUEUE)
    if ( a_es->type == DESCRIPTOR_TYPE_QUEUE ){
        goto lb_exit;
    }
    if ( a_es->type == DESCRIPTOR_TYPE_EVENT /*&& a_es->pipe_out*/){
        goto lb_exit;
    }

    struct kevent l_event;
    u_short l_flags = a_es->kqueue_base_flags;
    u_int   l_fflags = a_es->kqueue_base_fflags;
    short l_filter = a_es->kqueue_base_filter;
    int l_kqueue_fd =a_context->kqueue_fd;
    if ( l_kqueue_fd == -1 ){
        log_it(L_ERROR, "Esocket is not assigned with anything ,exit");
        l_is_error = true;
        l_errno = -1;
        goto lb_exit;
    }
    if( l_filter){
        EV_SET(&l_event, a_es->socket, l_filter,l_flags| EV_ADD,l_fflags,a_es->kqueue_data,a_es);
        if( kevent( l_kqueue_fd,&l_event,1,NULL,0,NULL) != 0 ){
            l_is_error = true;
            l_errno = errno;
            goto lb_exit;
        }else if (g_debug_reactor){
            log_it(L_DEBUG, "kevent set custom filter %d on fd %d",l_filter, a_es->socket);
        }
    }else{
        if( a_es->flags & DAP_SOCK_READY_TO_READ ){
            EV_SET(&l_event, a_es->socket, EVFILT_READ,l_flags| EV_ADD,l_fflags,a_es->kqueue_data,a_es);
            if( kevent( l_kqueue_fd,&l_event,1,NULL,0,NULL) != 0 ){
                l_is_error = true;
                l_errno = errno;
                goto lb_exit;
            }else if (g_debug_reactor){
                log_it(L_DEBUG, "kevent set EVFILT_READ on fd %d", a_es->socket);
            }

        }
        if( !l_is_error){
            if( a_es->flags & DAP_SOCK_READY_TO_WRITE || a_es->flags &DAP_SOCK_CONNECTING ){
                EV_SET(&l_event, a_es->socket, EVFILT_WRITE,l_flags| EV_ADD,l_fflags,a_es->kqueue_data,a_es);
                if(kevent( l_kqueue_fd,&l_event,1,NULL,0,NULL) != 0){
                    l_is_error = true;
                    l_errno = errno;
                    goto lb_exit;
                }else if (g_debug_reactor){
                    log_it(L_DEBUG, "kevent set EVFILT_WRITE on fd %d", a_es->socket);
                }
            }
        }
    }
lb_exit:
#else
#error "Unimplemented new esocket on context callback for current platform"
#endif
    if (l_is_error && l_errno != EEXIST) {
#ifdef DAP_EVENTS_CAPS_IOCP
        log_it(L_ERROR, "IOCP update failed, errno %lu %llu", l_errno, a_es->socket);
#else
        log_it(L_ERROR,"Can't update client socket state on poll/epoll/kqueue fd %" DAP_FORMAT_SOCKET ", error %d: \"%s\"",
            a_es->socket, l_errno, dap_strerror(l_errno) );
#endif
        return l_errno;
    }

    if (a_es->context)
        // TODO: should we remove the es from old context or just duplicate it?...
        log_it(L_WARNING, "Context switch detected on es %p : %" DAP_FORMAT_SOCKET, a_es, a_es->socket);
    a_es->context = a_context;
    a_es->worker = DAP_WORKER(a_context);
    //if (a_es->socket && a_es->socket != INVALID_SOCKET) {
        // Add in context HT
        dap_events_socket_t *l_es_sought = NULL;
        HASH_FIND_BYHASHVALUE(hh, a_context->esockets, &a_es->uuid, sizeof(a_es->uuid), a_es->uuid, l_es_sought);
        if (!l_es_sought) {
            HASH_ADD_BYHASHVALUE(hh, a_context->esockets, uuid, sizeof(a_es->uuid), a_es->uuid, a_es);
            a_context->event_sockets_count++;
        }
    //}
    return 0;
}

/**
 * @brief dap_context_remove Removes esocket from its own context
 * @param a_esocket Esocket to remove from its own context (if present
 * @return Zero if everything is ok, others if error
 */
int dap_context_remove( dap_events_socket_t * a_es)
{
    dap_context_t * l_context = a_es->context;
    int l_ret = 0;
    if (!l_context) {
        log_it(L_WARNING, "No context assigned to esocket %"DAP_FORMAT_SOCKET, a_es->socket);
        return -1;
    }
    dap_events_socket_t *l_es = NULL;
    HASH_FIND_BYHASHVALUE(hh, l_context->esockets, &a_es->uuid, sizeof(a_es->uuid), a_es->uuid, l_es);
    if (!l_es || l_es != a_es)
        log_it(L_ERROR, "Try to remove unexistent socket %p", a_es);
    else {
        l_context->event_sockets_count--;
        HASH_DELETE(hh, l_context->esockets, a_es);
    }

#if defined DAP_EVENTS_CAPS_IOCP
    /* TODO: there's a weird undocumented technique of "removing" from IOCP, but we barely need it */
#elif defined(DAP_EVENTS_CAPS_EPOLL)

    //Check if its present on current selection
    for (ssize_t n = l_context->esocket_current + 1; n< l_context->esockets_selected; n++ ){
        struct epoll_event * l_event = &l_context->epoll_events[n];
        if ( l_event->data.ptr == a_es ) // Found in selection
            l_event->data.ptr = NULL; // signal to skip on its iteration
    }

    // remove from epoll
    if ( epoll_ctl( l_context->epoll_fd, EPOLL_CTL_DEL, a_es->socket, &a_es->ev) == -1 )
        return log_it(L_CRITICAL, "Error removing event socket's handler from the epoll_fd %"DAP_FORMAT_HANDLE" \"%s\" (%d)",
                l_context->epoll_fd, dap_strerror(errno), errno), -1;
#elif defined(DAP_EVENTS_CAPS_KQUEUE)
    if (a_es->socket == -1) {
        log_it(L_ERROR, "Trying to remove bad socket from kqueue, a_es=%p", a_es);
    } else if (a_es->type == DESCRIPTOR_TYPE_EVENT || a_es->type == DESCRIPTOR_TYPE_QUEUE) {
        log_it(L_ERROR, "Removing non-kqueue socket from context %u is impossible", l_context->id);
    } else if (a_es->type == DESCRIPTOR_TYPE_TIMER && a_es->kqueue_base_filter == EVFILT_EMPTY) {
        // Nothing to do, it was already removed from kqueue cause of one shot strategy
    } else {
        for (ssize_t n = l_context->esocket_current+1; n< l_context->esockets_selected; n++ ){
            struct kevent * l_kevent_selected = &l_context->kqueue_events_selected[n];
            dap_events_socket_t * l_cur = NULL;

            // Extract current esocket
            if ( l_kevent_selected->filter == EVFILT_USER){
                dap_events_socket_w_data_t * l_es_w_data = (dap_events_socket_w_data_t *) l_kevent_selected->udata;
                if(l_es_w_data){
                    l_cur = l_es_w_data->esocket;
                }
            }else{
                l_cur = (dap_events_socket_t*) l_kevent_selected->udata;
            }

            // Compare it with current thats removing
            if (l_cur == a_es){
                l_kevent_selected->udata = NULL; // Singal to the loop to remove it from processing
            }

        }

        // Delete from kqueue
        struct kevent * l_event = &a_es->kqueue_event;
        EV_SET(l_event, a_es->socket, a_es->kqueue_base_filter, EV_DELETE, 0, 0, a_es);
        if (a_es->kqueue_base_filter){
            if ( kevent( l_context->kqueue_fd,l_event,1,NULL,0,NULL) == -1 ) {
                int l_errno = errno;
                char l_errbuf[128];
                strerror_r(l_errno, l_errbuf, sizeof (l_errbuf));
                log_it( L_ERROR,"Can't remove event socket's handler %d from the kqueue %d filter %d \"%s\" (%d)", a_es->socket,
                    l_context->kqueue_fd,a_es->kqueue_base_filter,  l_errbuf, l_errno);
            }
        }
        // Delete from flags ready
        if(a_es->flags & DAP_SOCK_READY_TO_WRITE){
            l_event->filter = EVFILT_WRITE;
            if ( kevent( l_context->kqueue_fd,l_event,1,NULL,0,NULL) == -1 ) {
                int l_errno = errno;
                char l_errbuf[128];
                strerror_r(l_errno, l_errbuf, sizeof (l_errbuf));
                log_it( L_ERROR,"Can't remove event socket's handler %d from the kqueue %d filter EVFILT_WRITE \"%s\" (%d)", a_es->socket,
                    l_context->kqueue_fd, l_errbuf, l_errno);
            }
        }
        if(a_es->flags & DAP_SOCK_READY_TO_READ){
            l_event->filter = EVFILT_READ;
            if ( kevent( l_context->kqueue_fd,l_event,1,NULL,0,NULL) == -1 ) {
                int l_errno = errno;
                char l_errbuf[128];
                strerror_r(l_errno, l_errbuf, sizeof (l_errbuf));
                log_it( L_ERROR,"Can't remove event socket's handler %d from the kqueue %d filter EVFILT_READ \"%s\" (%d)", a_es->socket,
                    l_context->kqueue_fd, l_errbuf, l_errno);
            }
        }
    }

#elif defined (DAP_EVENTS_CAPS_POLL)
    if (a_es->poll_index < l_context->poll_count ){
        l_context->poll[a_es->poll_index].fd = -1;
        a_es->context->poll_esocket[a_es->poll_index]=NULL;
        l_context->poll_compress = true;
    }else{
        log_it(L_ERROR, "Wrong poll index when remove from worker (unsafe): %u when total count %u", a_es->poll_index, l_context->poll_count);
        l_ret = -2;
    }
#else
#error "Unimplemented new esocket on worker callback for current platform"
#endif
    a_es->context = NULL;
    return l_ret;
}

/**
 * @brief dap_context_find
 * @param a_context
 * @param a_es_uuid
 * @return
 */
dap_events_socket_t *dap_context_find(dap_context_t * a_context, dap_events_socket_uuid_t a_es_uuid )
{
    dap_events_socket_t *l_es = NULL;
    if (a_context && a_context->esockets)
        HASH_FIND_BYHASHVALUE(hh, a_context->esockets, &a_es_uuid, sizeof(a_es_uuid), a_es_uuid, l_es);
    return l_es;
}

/**
 * @brief dap_context_create_queue
 * @param a_context
 * @param a_callback
 * @return
 */
 dap_events_socket_t * dap_context_create_queue(dap_context_t * a_context, dap_events_socket_callback_queue_ptr_t a_callback)
{
    dap_events_socket_t * l_es = DAP_NEW_Z(dap_events_socket_t);
    if(!l_es){
        log_it(L_CRITICAL,"Memory allocation error");
        return NULL;
    }

    l_es->type = DESCRIPTOR_TYPE_QUEUE;
    l_es->flags = DAP_SOCK_QUEUE_PTR;
    l_es->uuid = dap_new_es_id();

    l_es->callbacks.queue_ptr_callback = a_callback; // Arm event callback

#if defined(DAP_EVENTS_CAPS_QUEUE_PIPE2)
    pthread_rwlock_init(&l_es->buf_out_lock, NULL);
#endif

#if defined DAP_EVENTS_CAPS_IOCP
    l_es->socket = INVALID_SOCKET;
    l_es->buf_out = DAP_ALMALLOC(MEMORY_ALLOCATION_ALIGNMENT, sizeof(SLIST_HEADER));
    InitializeSListHead((PSLIST_HEADER)l_es->buf_out);
#else
    l_es->buf_in_size_max = l_es->buf_out_size_max = DAP_QUEUE_MAX_MSGS * sizeof(void*);
    l_es->buf_in    = DAP_NEW_Z_SIZE(byte_t, l_es->buf_in_size_max);
    l_es->buf_out   = DAP_NEW_Z_SIZE(byte_t, l_es->buf_out_size_max);
#if defined(DAP_EVENTS_CAPS_EPOLL)
    l_es->ev_base_flags = EPOLLIN | EPOLLERR | EPOLLRDHUP | EPOLLHUP;
#elif defined(DAP_EVENTS_CAPS_POLL)
    l_es->poll_base_flags = POLLIN | POLLERR | POLLRDHUP | POLLHUP;
#elif defined(DAP_EVENTS_CAPS_KQUEUE)
    l_es->kqueue_event_catched_data.esocket = l_es;
    //l_es->kqueue_base_flags =  EV_ONESHOT;
    l_es->kqueue_base_fflags = NOTE_FFNOP | NOTE_TRIGGER;
    l_es->kqueue_base_filter = EVFILT_USER;
    l_es->socket = arc4random();
#else
#error "Not defined s_create_type_queue_ptr for your platform"
#endif
#endif

#if defined(DAP_EVENTS_CAPS_QUEUE_PIPE2) || defined(DAP_EVENTS_CAPS_QUEUE_PIPE)
    int l_pipe[2];
#if defined(DAP_EVENTS_CAPS_QUEUE_PIPE2)
    if( pipe2(l_pipe,O_DIRECT | O_NONBLOCK ) < 0 ){
#elif defined(DAP_EVENTS_CAPS_QUEUE_PIPE)
    if( pipe(l_pipe) < 0 ){
#endif
        return DAP_DELETE(l_es), log_it(L_ERROR, "pipe() failed, error %d: '%s'", errno, dap_strerror(errno)), NULL;
    }
    l_es->fd = l_pipe[0];
    l_es->fd2 = l_pipe[1];

#if defined(DAP_EVENTS_CAPS_QUEUE_PIPE)
    // If we have no pipe2() we should set nonblock mode via fcntl
    if (l_es->fd > 0 && l_es->fd2 > 0 ) {
    int l_flags = fcntl(l_es->fd, F_GETFL, 0);
    if (l_flags != -1){
        l_flags |= O_NONBLOCK);
        fcntl(l_es->fd, F_SETFL, l_flags) == 0);
    }
    l_flags = fcntl(l_es->fd2, F_GETFL, 0);
    if (l_flags != -1){
        l_flags |= O_NONBLOCK);
        fcntl(l_es->fd2, F_SETFL, l_flags) == 0);
    }
    }
#endif

#if !defined (DAP_OS_ANDROID)
    FILE* l_sys_max_pipe_size_fd = fopen("/proc/sys/fs/pipe-max-size", "r");
    if (l_sys_max_pipe_size_fd) {
        char l_file_buf[64] = "";
        fread(l_file_buf, sizeof(l_file_buf), 1, l_sys_max_pipe_size_fd);
        uint64_t l_sys_max_pipe_size = strtoull(l_file_buf, 0, 10);
        fcntl(l_pipe[0], F_SETPIPE_SZ, l_sys_max_pipe_size);
        fclose(l_sys_max_pipe_size_fd);
    }
#endif

#elif defined (DAP_EVENTS_CAPS_QUEUE_MQUEUE)
    int  l_errno;
    char l_errbuf[128] = {0}, l_mq_name[64] = {0};
    struct mq_attr l_mq_attr;
    static atomic_uint l_mq_last_number = 0;


    l_mq_attr.mq_maxmsg = DAP_QUEUE_MAX_MSGS;                               // Don't think we need to hold more than 1024 messages
    l_mq_attr.mq_msgsize = sizeof (void*);                                  // We send only pointer on memory (???!!!),
                                                                            // so use it with shared memory if you do access from another process

    l_es->mqd_id = atomic_fetch_add( &l_mq_last_number, 1);
    snprintf(l_mq_name,sizeof (l_mq_name), "/%s-queue_ptr-%u", dap_get_appname(), l_es->mqd_id );
    // if ( (l_errno = mq_unlink(l_mq_name)) )                                 /* Mark this MQ to be deleted as the process will be terminated */
    //    log_it(L_DEBUG, "mq_unlink(%s)->%d", l_mq_name, l_errno);

    if ( 0 >= (l_es->mqd = mq_open(l_mq_name, O_CREAT|O_RDWR |O_NONBLOCK, 0700, &l_mq_attr)) )
    {
        log_it(L_CRITICAL,"Can't create mqueue descriptor %s: \"%s\" code %d (%s)", l_mq_name, l_errbuf, errno,
                           (strerror_r(errno, l_errbuf, sizeof (l_errbuf)), l_errbuf) );

        DAP_DELETE(l_es->buf_in);
        DAP_DELETE(l_es);
        return NULL;
    }

#elif defined DAP_EVENTS_CAPS_WEPOLL
    l_es->socket        = socket(AF_INET, SOCK_DGRAM, 0);

    if (l_es->socket == INVALID_SOCKET) {
        log_it(L_ERROR, "Error creating socket for TYPE_QUEUE: %d", WSAGetLastError());
        DAP_DELETE(l_es);
        return NULL;
    }

    int buffsize = 1024;
    setsockopt(l_es->socket, SOL_SOCKET, SO_RCVBUF, (char *)&buffsize, sizeof(int));

    int reuse = 1;
    if (setsockopt(l_es->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
        log_it(L_WARNING, "Can't set up REUSEADDR flag to the socket, err: %d", WSAGetLastError());

    unsigned long l_mode = 1;
    ioctlsocket(l_es->socket, FIONBIO, &l_mode);

    struct sockaddr_in l_addr = { .sin_family = AF_INET, .sin_port = 0, .sin_addr = {{ .S_addr = htonl(INADDR_LOOPBACK) }} };

    if (bind(l_es->socket, (struct sockaddr*)&l_addr, sizeof(l_addr)) < 0) {
        log_it(L_ERROR, "Bind error: %d", WSAGetLastError());
    } else {
        int dummy = 100;
        getsockname(l_es->socket, (struct sockaddr*)&l_addr, &dummy);
        l_es->port = l_addr.sin_port;
    }
#elif defined DAP_EVENTS_CAPS_IOCP
    // Nothing to do
#elif defined (DAP_EVENTS_CAPS_KQUEUE)
    // We don't create descriptor for kqueue at all
#else
#error "Not implemented s_create_type_queue_ptr() on your platform"
#endif

    if ( a_context) {
        if(dap_context_add(a_context, l_es)) {
#ifdef DAP_OS_WINDOWS
            errno = WSAGetLastError();
#endif
            log_it(L_ERROR, "Can't add esocket %"DAP_FORMAT_SOCKET" to polling, err %d", l_es->socket, errno);
        }
    }

    return l_es;
}

/**
 * @brief s_create_type_event
 * @param a_context
 * @param a_callback
 * @return
 */
dap_events_socket_t * dap_context_create_event(dap_context_t * a_context, dap_events_socket_callback_event_t a_callback)
{
    dap_events_socket_t * l_es = DAP_NEW_Z(dap_events_socket_t); if (!l_es) return NULL;
    l_es->buf_out_size_max = l_es->buf_in_size_max = 1;
    l_es->buf_out = DAP_NEW_Z_SIZE(byte_t, l_es->buf_out_size_max);
    l_es->type = DESCRIPTOR_TYPE_EVENT;
    l_es->uuid = dap_new_es_id();

    l_es->callbacks.event_callback = a_callback; // Arm event callback
#if defined DAP_EVENTS_CAPS_IOCP
    l_es->socket = INVALID_SOCKET;
    l_es->flags |= DAP_SOCK_READY_TO_READ;
#elif defined(DAP_EVENTS_CAPS_EPOLL)
    l_es->ev_base_flags = EPOLLIN | EPOLLERR | EPOLLRDHUP | EPOLLHUP;
#elif defined(DAP_EVENTS_CAPS_POLL)
    l_es->poll_base_flags = POLLIN | POLLERR | POLLRDHUP | POLLHUP;
#elif defined(DAP_EVENTS_CAPS_KQUEUE)
    l_es->kqueue_base_flags =  EV_ONESHOT;
    l_es->kqueue_base_fflags = NOTE_FFNOP | NOTE_TRIGGER;
    l_es->kqueue_base_filter = EVFILT_USER;
    l_es->socket = arc4random();
    l_es->kqueue_event_catched_data.esocket = l_es;
#else
#error "Not defined s_create_type_event for your platform"
#endif

#ifdef DAP_EVENTS_CAPS_EVENT_EVENTFD
    if ( (l_es->fd = eventfd(0,EFD_NONBLOCK) ) < 0 )
        return DAP_DELETE(l_es), log_it(L_ERROR, "Can't create eventfd, error %d: '%s'", errno, dap_strerror(errno)), NULL;
    l_es->fd2 = l_es->fd;
#elif defined DAP_EVENTS_CAPS_WEPOLL
    l_es->socket        = socket(AF_INET, SOCK_DGRAM, 0);

    if (l_es->socket == INVALID_SOCKET) {
        log_it(L_ERROR, "Error creating socket for TYPE_EVENT: %d", WSAGetLastError());
        DAP_DELETE(l_es);
        return NULL;
    }

    int buffsize = 1024;
    setsockopt(l_es->socket, SOL_SOCKET, SO_RCVBUF, (char *)&buffsize, sizeof(int));

    unsigned long l_mode = 1;
    ioctlsocket(l_es->socket, FIONBIO, &l_mode);

    int reuse = 1;
    if (setsockopt(l_es->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
        log_it(L_WARNING, "Can't set up REUSEADDR flag to the socket, err: %d", WSAGetLastError());

    struct sockaddr_in l_addr = { .sin_family = AF_INET, .sin_port = 0, .sin_addr = {{ .S_addr = htonl(INADDR_LOOPBACK) }} };

    if (bind(l_es->socket, (struct sockaddr*)&l_addr, sizeof(l_addr)) < 0) {
        log_it(L_ERROR, "Bind error: %d", WSAGetLastError());
    } else {
        int dummy = 100;
        getsockname(l_es->socket, (struct sockaddr*)&l_addr, &dummy);
        l_es->port = l_addr.sin_port;
    }
#elif defined DAP_EVENTS_CAPS_IOCP
    // Do nothing ...
#elif defined(DAP_EVENTS_CAPS_KQUEUE)
    // nothing to do
#else
#error "Not defined dap_context_create_event() on your platform"
#endif
    if(a_context)
        dap_context_add(a_context,l_es);
    return l_es;
}


/**
 * @brief dap_context_create_pipe
 * @param a_context
 * @param a_callback
 * @param a_flags
 * @return
 */
dap_events_socket_t * dap_context_create_pipe(dap_context_t * a_context, dap_events_socket_callback_t a_callback, uint32_t a_flags)
{
#ifdef DAP_OS_WINDOWS
    UNUSED(a_callback);
    UNUSED(a_flags);
    return NULL;
#else
    UNUSED(a_flags);
    dap_events_socket_t * l_es = DAP_NEW_Z(dap_events_socket_t);
    if (!l_es) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    l_es->type = DESCRIPTOR_TYPE_PIPE;
    l_es->uuid = dap_uuid_generate_uint64();
    l_es->callbacks.read_callback = a_callback; // Arm event callback
#if defined(DAP_EVENTS_CAPS_EPOLL)
    l_es->ev_base_flags = EPOLLIN | EPOLLERR | EPOLLRDHUP | EPOLLHUP;
#elif defined(DAP_EVENTS_CAPS_POLL)
    l_es->poll_base_flags = POLLIN | POLLERR | POLLRDHUP | POLLHUP;
#elif defined(DAP_EVENTS_CAPS_KQUEUE)
    l_es->kqueue_event_catched_data.esocket = l_es;
    l_es->kqueue_base_flags = EV_ENABLE | EV_CLEAR;
    l_es->kqueue_base_fflags = NOTE_DELETE | NOTE_REVOKE ;
#if !defined(DAP_OS_DARWIN)
    l_es->kqueue_base_fflags |= NOTE_CLOSE | NOTE_CLOSE_WRITE ;
#endif
    l_es->kqueue_base_filter = EVFILT_VNODE;
#else
#error "Not defined s_create_type_pipe for your platform"
#endif

#if defined(DAP_EVENTS_CAPS_PIPE_POSIX)
    int l_pipe[2];
    if( pipe(l_pipe) < 0 )
        return DAP_DELETE(l_es), log_it( L_ERROR, "Error detected, can't create pipe(), error %d: '%s'", errno, dap_strerror(errno)), NULL;
    l_es->fd = l_pipe[0];
    l_es->fd2 = l_pipe[1];
#if defined DAP_OS_UNIX
    fcntl( l_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl( l_pipe[1], F_SETFL, O_NONBLOCK);
    // this sort of fd doesn't suit ioctlsocket()...
#endif

#else
#error "No defined s_create_type_pipe() for your platform"
#endif
    dap_context_add(a_context,l_es);
    return l_es;
#endif
}

/**
 * @brief dap_context_create_queues
 * @param a_callback
 */
void dap_context_create_queues( dap_events_socket_callback_queue_ptr_t a_callback)
{
    // TODO complete queues create
}
