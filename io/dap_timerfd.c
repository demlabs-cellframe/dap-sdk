/*
 * Authors:
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * Dmitriy Gerasimov <dmitriy.gerasimov@demlabs.net>
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

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#endif

#include "dap_common.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_events_socket.h"
#include "dap_timerfd.h"
#include "dap_context.h"

#define LOG_TAG "dap_timerfd"
static void s_es_callback_timer(struct dap_events_socket *a_es);

#ifdef DAP_EVENTS_CAPS_IOCP
static void s_es_cb_timer_delete(dap_events_socket_t *a_es, void *a_arg);
#endif

static bool l_debug_timer = false;

#ifdef DAP_OS_WINDOWS
    static HANDLE hTimerQueue = NULL;
#endif

/**
 * @brief dap_events_socket_init Init clients module
 * @return Zero if ok others if no
 */
int dap_timerfd_init()
{
#ifdef DAP_OS_WINDOWS
        hTimerQueue = CreateTimerQueue();
        if (!hTimerQueue) {
            log_it(L_CRITICAL, "Timer queue failed, err %lu", GetLastError());
            return -4;
        }
#endif
    log_it(L_NOTICE, "Initialized timerfd");
    return 0;
}

/**
 * @brief dap_timerfd_start
 * @param a_timeout_ms
 * @param a_callback
 * @return new allocated dap_timerfd_t structure or NULL if error
 */
dap_timerfd_t* dap_timerfd_start(uint64_t a_timeout_ms, dap_timerfd_callback_t a_callback, void *a_callback_arg)
{
     return dap_timerfd_start_on_worker(dap_events_worker_get_auto(), a_timeout_ms, a_callback, a_callback_arg);
}

#ifdef DAP_OS_WINDOWS
void __stdcall TimerRoutine(void* arg, BOOLEAN flag) {
    /* A dumb crutch for unix crap compatibility sake
     * TODO: The timer should be created with WT_EXECUTEINIOTHREAD flag directly on worker thread
     */
    dap_timerfd_t *l_timerfd = (dap_timerfd_t*)arg;
    dap_events_socket_t *l_es = dap_context_find(l_timerfd->worker->context, l_timerfd->esocket_uuid);
    if (!l_es)
        log_it(L_ERROR, "Timer fired on already removed es uuid %zu", l_timerfd->esocket_uuid);
    else if (!PostQueuedCompletionStatus(l_es->context->iocp, 0, l_es->uuid, NULL))
        log_it(L_ERROR, "Sending completion message failed, errno %lu", GetLastError());
}
#endif


/**
 * @brief dap_timerfd_start_on_worker
 * @param a_worker
 * @param a_timeout_ms
 * @param a_callback
 * @param a_callback_arg
 * @return
 */
dap_timerfd_t* dap_timerfd_start_on_worker(dap_worker_t * a_worker, uint64_t a_timeout_ms, dap_timerfd_callback_t a_callback, void *a_callback_arg)
{
    dap_timerfd_t* l_timerfd = dap_timerfd_create( a_timeout_ms, a_callback, a_callback_arg);
    if (!l_timerfd) {
        log_it(L_CRITICAL,"Can't create timer");
        return NULL;
    }
    dap_worker_add_events_socket(a_worker, l_timerfd->events_socket);
    l_timerfd->worker = a_worker;
    return l_timerfd;
}

/**
 * @brief dap_timerfd_create
 * @param a_timeout_ms
 * @param a_callback
 * @param a_callback_arg
 * @return
 */
dap_timerfd_t* dap_timerfd_create(uint64_t a_timeout_ms, dap_timerfd_callback_t a_callback, void *a_callback_arg)
{
    dap_timerfd_t *l_timerfd = DAP_NEW_Z(dap_timerfd_t);
    if(!l_timerfd)
        return NULL;

    dap_events_socket_callbacks_t l_s_callbacks = {
        .timer_callback = s_es_callback_timer
#ifdef DAP_EVENTS_CAPS_IOCP
      , .delete_callback = s_es_cb_timer_delete
#endif
    };

    dap_events_socket_t *l_events_socket = dap_events_socket_wrap_no_add(-1, &l_s_callbacks);
    l_events_socket->type = DESCRIPTOR_TYPE_TIMER;

    // pass l_timerfd to events_socket
    l_events_socket->_inheritor = l_timerfd;

    // fill out dap_timerfd_t structure
    l_timerfd->timeout_ms       = a_timeout_ms;
    l_timerfd->callback         = a_callback;
    l_timerfd->callback_arg     = a_callback_arg;
    l_timerfd->events_socket    = l_events_socket;
    l_timerfd->esocket_uuid     = l_events_socket->uuid;

#if defined DAP_OS_LINUX
    struct itimerspec l_ts;
    int l_tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if(l_tfd == -1) {
        log_it(L_WARNING, "dap_timerfd_start() failed: timerfd_create() errno=%d\n", errno);
        DAP_DELETE(l_timerfd);
        return NULL;
    }
    // repeat never
    l_ts.it_interval.tv_sec = 0;
    l_ts.it_interval.tv_nsec = 0;
    // timeout for timer
    l_ts.it_value.tv_sec = a_timeout_ms / 1000;
    l_ts.it_value.tv_nsec = (a_timeout_ms % 1000) * 1000000;
    if(timerfd_settime(l_tfd, 0, &l_ts, NULL) < 0) {
        log_it(L_WARNING, "dap_timerfd_start() failed: timerfd_settime() errno=%d\n", errno);
        close(l_tfd);
        DAP_DELETE(l_timerfd);
        return NULL;
    }
    l_events_socket->socket = l_tfd;
#elif defined (DAP_OS_BSD)
    l_events_socket->flags = 0;
    l_events_socket->kqueue_base_flags = EV_ONESHOT;
    l_events_socket->kqueue_base_filter = EVFILT_TIMER;
    l_events_socket->socket = arc4random();
#ifdef DAP_OS_DARWIN
    // We have all timers not critical accurate but more power safe
    l_events_socket->kqueue_base_fflags = 0U;
#else
    l_events_socket->kqueue_base_fflags = NOTE_MSECONDS;
#endif
    l_events_socket->kqueue_data = (int64_t)a_timeout_ms;

#elif defined (DAP_OS_WINDOWS)
#ifdef DAP_EVENTS_CAPS_WEPOLL
    l_timerfd->th = NULL;
    SOCKET l_tfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (l_tfd == INVALID_SOCKET) {
        log_it(L_ERROR, "Error creating socket for type 'timer': %d", WSAGetLastError());
        DAP_DELETE(l_timerfd);
        DAP_DELETE(l_events_socket);
        return NULL;
    }
    int buffsize = 4096;
    setsockopt(l_tfd, SOL_SOCKET, SO_RCVBUF, (char *)&buffsize, sizeof(int));

    unsigned long l_mode = 1;
    ioctlsocket(l_tfd, FIONBIO, &l_mode);

    struct sockaddr_in l_addr = { .sin_family = AF_INET, .sin_port = 0, .sin_addr = {{ .S_addr = htonl(INADDR_LOOPBACK) }} };;
    if (bind(l_tfd, (struct sockaddr*)&l_addr, sizeof(l_addr)) < 0) {
        log_it(L_ERROR, "Bind error: %d", WSAGetLastError());
    } else {
        int dummy = 100;
        getsockname(l_tfd, (struct sockaddr*)&l_addr, &dummy);
        l_timerfd->port = l_addr.sin_port;
    }
    if (!CreateTimerQueueTimer(&l_timerfd->th, hTimerQueue,
                               (WAITORTIMERCALLBACK)TimerRoutine, l_timerfd, (DWORD)a_timeout_ms, 0, 0)) {
        log_it(L_CRITICAL, "Timer not set, error %lu", GetLastError());
        DAP_DELETE(l_timerfd);
        return NULL;
    }
    l_events_socket->socket = l_tfd;
#elif defined DAP_EVENTS_CAPS_IOCP
    if (!CreateTimerQueueTimer(&l_timerfd->th, hTimerQueue,
                               (WAITORTIMERCALLBACK)TimerRoutine, l_timerfd, (DWORD)a_timeout_ms, 0, 0)) {
        log_it(L_CRITICAL, "Timer not set, error %lu", GetLastError());
        DAP_DELETE(l_timerfd);
        return NULL;
    }
    l_events_socket->socket = INVALID_SOCKET;
#endif
#endif
    debug_if(g_debug_reactor, L_DEBUG, "Create timer on socket %"DAP_FORMAT_SOCKET, l_timerfd->events_socket->socket);;
#if defined (DAP_OS_LINUX)    
    l_timerfd->tfd = l_tfd;
#endif
    return l_timerfd;
}

void dap_timerfd_reset_unsafe(dap_timerfd_t *a_timerfd)
{
    assert(a_timerfd);
    debug_if(g_debug_reactor, L_ATT, "Reset timer on socket %"DAP_FORMAT_SOCKET, a_timerfd->events_socket->socket);
#if defined DAP_OS_LINUX
    struct itimerspec l_ts;
    // repeat never
    l_ts.it_interval.tv_sec = 0;
    l_ts.it_interval.tv_nsec = 0;
    // timeout for timer
    l_ts.it_value.tv_sec = a_timerfd->timeout_ms / 1000;
    l_ts.it_value.tv_nsec = (a_timerfd->timeout_ms % 1000) * 1000000;
    if(timerfd_settime(a_timerfd->tfd, 0, &l_ts, NULL) < 0) {
        log_it(L_WARNING, "Reset timerfd failed: timerfd_settime() errno=%d\n", errno);
    }
#elif defined (DAP_OS_BSD)
    dap_events_socket_t *l_es = a_timerfd->events_socket;
    // Re-add timer in context
    dap_context_t *l_context = l_es->context;
    l_es->context = NULL;
    dap_context_add(l_context, l_es);
#elif defined (DAP_OS_WINDOWS)
    // Doesn't work with one-shot timers
    //if (!ChangeTimerQueueTimer(hTimerQueue, a_timerfd->th, (DWORD)a_timerfd->timeout_ms, 0))
    DeleteTimerQueueTimer(hTimerQueue, a_timerfd->th, NULL);
    if (!CreateTimerQueueTimer(&a_timerfd->th, hTimerQueue,
                               (WAITORTIMERCALLBACK)TimerRoutine, a_timerfd, (DWORD)a_timerfd->timeout_ms, 0, 0))
        log_it(L_CRITICAL, "Timer not reset, error %lu", GetLastError());
#else
#error "No timer reset realization for your platform"
#endif

#if !defined(DAP_OS_BSD) && !defined (DAP_OS_WINDOWS)
    dap_events_socket_set_readable_unsafe(a_timerfd->events_socket, true);
#endif
}

/**
 * @brief s_es_callback_timer
 * @param a_event_sock
 */
static void s_es_callback_timer(struct dap_events_socket *a_event_sock)
{
    dap_timerfd_t *l_timer_fd = a_event_sock->_inheritor;
    if(!l_timer_fd)
        return;
    // run user's callback
    debug_if(g_debug_reactor, L_ATT, "Call timer on socket %"DAP_FORMAT_SOCKET, l_timer_fd->events_socket->socket);
    if(l_timer_fd && l_timer_fd->callback && l_timer_fd->callback(l_timer_fd->callback_arg)) {
        dap_timerfd_reset_unsafe(l_timer_fd);
    } else {
        debug_if(g_debug_reactor, L_ATT, "Close timer on socket %"DAP_FORMAT_SOCKET, l_timer_fd->events_socket->socket);
#if defined DAP_EVENTS_CAPS_WEPOLL
        DeleteTimerQueueTimer(hTimerQueue, l_timer_fd->th, NULL);
#elif defined DAP_EVENTS_CAPS_KQUEUE
        l_timer_fd->events_socket->kqueue_base_filter = EVFILT_EMPTY;
#endif
        a_event_sock->flags |= DAP_SOCK_SIGNAL_CLOSE;
    }
}

#ifdef DAP_EVENTS_CAPS_IOCP
static void s_es_cb_timer_delete(dap_events_socket_t *a_es, UNUSED_ARG void *a_arg) {
    debug_if(g_debug_reactor, L_DEBUG, "Timer remove cb called on es %p", a_es);
    dap_timerfd_t *l_timer_fd = a_es->_inheritor;
    if(!l_timer_fd)
        return;
    DeleteTimerQueueTimer(hTimerQueue, l_timer_fd->th, NULL);
}
#endif


/**
 * @brief s_timerfd_reset_worker_callback
 * @param a_worker
 * @param a_arg
 */
static void s_timerfd_reset_worker_callback(dap_worker_t *a_worker, void *a_arg)
{
    assert(a_arg);
    dap_events_socket_uuid_t *l_uuid = a_arg;
    dap_events_socket_t *l_sock = dap_context_find(a_worker->context, *l_uuid);
    if (l_sock)
        dap_timerfd_reset_unsafe(l_sock->_inheritor);
    DAP_DELETE(l_uuid);
}

/**
 * @brief dap_timerfd_reset
 * @param a_tfd
 */
void dap_timerfd_reset_mt(dap_worker_t *a_worker, dap_events_socket_uuid_t a_uuid)
{
    if (!a_worker || !a_uuid)
        return;
    dap_events_socket_uuid_t *l_uuid = DAP_DUP(&a_uuid);
    dap_worker_exec_callback_on(a_worker, s_timerfd_reset_worker_callback, l_uuid);
}

/**
 * @brief dap_timerfd_stop
 * @param a_tfd
 * @param a_callback
 */
void dap_timerfd_delete_unsafe(dap_timerfd_t *a_timerfd)
{
    if (!a_timerfd)
        return; 
#if defined DAP_EVENTS_CAPS_WEPOLL
    DeleteTimerQueueTimer(hTimerQueue, a_timerfd->th, NULL);
#endif
    debug_if(g_debug_reactor, L_DEBUG, "Remove timer on socket %"DAP_FORMAT_SOCKET, a_timerfd->events_socket->socket);
    if (a_timerfd->events_socket->context)
       dap_events_socket_remove_and_delete_unsafe(a_timerfd->events_socket, false);
    else
       a_timerfd->events_socket->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

void dap_timerfd_delete_mt(dap_worker_t *a_worker, dap_events_socket_uuid_t a_uuid)
{
    if (!a_worker || !a_uuid)
        return;
    dap_events_socket_remove_and_delete_mt(a_worker, a_uuid);
}


