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
#include "dap_events.h"
#include "dap_proc_thread.h"
#include "dap_worker.h"

struct dap_context_msg_run {
    dap_context_t * context;
    dap_context_callback_t callback_started;
    dap_context_callback_t callback_stopped;
    int priority;
    int sched_policy;
    int cpu_id;
    int flags;
    void *callback_arg;
};

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
dap_context_t *dap_context_new(dap_context_type_t a_type)
{
   static atomic_uint_fast64_t s_context_id_max = 0;
   dap_context_t * l_context = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_context_t, NULL);
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
    struct dap_context_msg_run * l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct dap_context_msg_run, ENOMEM);
    int l_ret;

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
#if !defined(DAP_OS_DARWIN) && !defined(DAP_OS_ANDROID)
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
                log_it(L_CRITICAL, "Timeout %d seconds is out: context #%u thread don't respond", DAP_CONTEXT_WAIT_FOR_STARTED_TIME, a_context->id);
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
    struct dap_context_msg_run * l_msg = (struct dap_context_msg_run*) a_arg;
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
            char l_errbuf[128];
            l_errbuf[0]=0;
            strerror_r(l_errno, l_errbuf, sizeof (l_errbuf));
            log_it(L_ERROR,"Can't update client socket state in the epoll_fd %"DAP_FORMAT_HANDLE": \"%s\" (%d)",
                   a_esocket->context->epoll_fd, l_errbuf, l_errno);
            return l_errno;
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

    /* Prevent putting the same esocket into two context hash tables at once.
     * If it is already registered on another context, detach it first.
     * If it is already on the target context, skip re-adding. */
    if (a_es->context) {
        if (a_es->context == a_context) {
            debug_if(g_debug_reactor, L_DEBUG, "Es %p already attached to context #%u, skip add", a_es, a_context->id);
            return 0;
        }
        log_it(L_WARNING, "Context switch detected on es %p : %" DAP_FORMAT_SOCKET ", moving from context %u to %u",
               a_es, a_es->socket, a_es->context->id, a_context->id);
        dap_context_remove(a_es);
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
    if ( epoll_ctl( l_context->epoll_fd, EPOLL_CTL_DEL, a_es->socket, &a_es->ev) == -1 ) {
        int l_errno = errno;
        char l_errbuf[128];
        strerror_r(l_errno, l_errbuf, sizeof (l_errbuf));
        log_it( L_ERROR,"Can't remove event socket's handler from the epoll_fd %"DAP_FORMAT_HANDLE"  \"%s\" (%d)",
                l_context->epoll_fd, l_errbuf, l_errno);
        l_ret = l_errno;
    } //else
      //  log_it( L_DEBUG,"Removed epoll's event from context #%u", l_context->id );
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
