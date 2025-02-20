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


#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#if defined (DAP_OS_LINUX)
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <limits.h>
#elif defined (DAP_OS_BSD)
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#elif defined (DAP_OS_WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#endif



#if defined (DAP_EVENTS_CAPS_QUEUE_MQUEUE)
#include <sys/time.h>
#include <sys/resource.h>
#endif

#ifdef DAP_OS_BSD
#include <sys/event.h>
#include <err.h>

#ifndef DAP_OS_DARWIN
#include <pthread_np.h>
typedef cpuset_t cpu_set_t; // Adopt BSD CPU setstructure to POSIX variant
#else
#define NOTE_READ NOTE_LOWAT

#endif

#endif


#include <fcntl.h>
#include <pthread.h>

#include "dap_common.h"
#include "dap_config.h"
#include "dap_list.h"
#include "dap_worker.h"
#include "dap_uuid.h"
#include "dap_events.h"

#include "dap_timerfd.h"
#include "dap_context.h"
#include "dap_events_socket.h"

#define LOG_TAG "dap_events_socket"

// Item for QUEUE_PTR input esocket
struct queue_ptr_input_item{
    dap_events_socket_t * esocket;
    void * ptr;
    struct queue_ptr_input_item * next;
};

// QUEUE_PTR input esocket pvt section
struct queue_ptr_input_pvt{
    dap_events_socket_t * esocket;
    struct queue_ptr_input_item * items_first;
    struct queue_ptr_input_item * items_last;
};
#define PVT_QUEUE_PTR_INPUT(a) ( (struct queue_ptr_input_pvt*) (a)->_pvt )

static uint64_t s_delayed_ops_timeout_ms = 5000;
bool s_remove_and_delete_unsafe_delayed_delete_callback(void * a_arg);

static pthread_attr_t s_attr_detached;                                      /* Thread's creation attribute = DETACHED ! */


#ifdef   DAP_SYS_DEBUG
enum    {MEMSTAT$K_EVSOCK, MEMSTAT$K_BUF_IN, MEMSTAT$K_BUF_OUT, MEMSTAT$K_BUF_OUT_EXT, MEMSTAT$K_NR};
static  dap_memstat_rec_t   s_memstat [MEMSTAT$K_NR] = {
    {.fac_len = sizeof(LOG_TAG) - 1, .fac_name = {LOG_TAG}, .alloc_sz = sizeof(dap_events_socket_t)},
    {.fac_len = sizeof(LOG_TAG ".buf_in") - 1, .fac_name = {LOG_TAG ".buf_in"}, .alloc_sz = DAP_EVENTS_SOCKET_BUF_SIZE},
    {.fac_len = sizeof(LOG_TAG ".buf_out") - 1, .fac_name = {LOG_TAG ".buf_out"}, .alloc_sz = DAP_EVENTS_SOCKET_BUF_SIZE},
    {.fac_len = sizeof(LOG_TAG ".buf_out_ext") - 1, .fac_name = {LOG_TAG ".buf_out_ext"}, .alloc_sz = DAP_EVENTS_SOCKET_BUF_LIMIT}
};
#endif  /* DAP_SYS_DEBUG */

static dap_events_socket_t  *s_esockets = NULL;
static pthread_rwlock_t     s_evsocks_lock = PTHREAD_RWLOCK_INITIALIZER;
unsigned int dap_new_es_id() {
    static _Atomic unsigned es_id = 0;
    return es_id++;
}
/*
 *   DESCRIPTION: Allocate a new <dap_events_socket> context, add record into the hash table to track usage
 *      of the contexts.
 *
 *   INPUTS:
 *      NONE
 *
 *   IMPLICITE INPUTS:
 *      s_evsocks;      A hash table
 *
 *   OUTPUTS:
 *      NONE
 *
 *   IMPLICITE OUTPUTS:
 *      s_evsocks
 *
 *   RETURNS:
 *      non-NULL        A has been allocated <dap_events_socket> context
 *      NULL:           See <errno>
 */
static inline dap_events_socket_t *s_dap_evsock_alloc (void)
{
    dap_events_socket_t *l_es;
    if ( !(l_es = DAP_NEW_Z( dap_events_socket_t )) )                   /* Allocate memory for new dap_events_socket context and the record */
        return  log_it(L_CRITICAL, "Cannot allocate memory for <dap_events_socket> context, errno=%d", errno), NULL;                                                /* Fill new track record */
    l_es->uuid = dap_new_es_id();
#ifdef DAP_SYS_DEBUG
    pthread_rwlock_wrlock(&s_evsocks_lock);                             /* Add new record into the hash table */
    HASH_ADD(hh2, s_esockets, uuid, sizeof(l_es->uuid), l_es);
    pthread_rwlock_unlock(&s_evsocks_lock);
#endif
    debug_if(g_debug_reactor, L_DEBUG, "Created blank es %p, uuid " DAP_FORMAT_ESOCKET_UUID,
                              l_es, l_es->uuid);
    return  l_es;
}

#if defined DAP_EVENTS_CAPS_IOCP
static void s_es_set_flag(dap_context_t *a_c, OVERLAPPED *a_ol) {
    dap_events_socket_t *a_es = dap_context_find(a_c, (dap_events_socket_uuid_t)a_ol->Internal);
    if (!a_es)
        return log_it(L_ERROR, "Es #"DAP_FORMAT_ESOCKET_UUID" not found in context #%d", a_ol->Internal, a_c->id);
    uint32_t flag = (uint32_t)a_ol->Offset;
    if (a_ol->OffsetHigh) {
        switch (flag) {
        case DAP_SOCK_READY_TO_READ:
            debug_if(g_debug_reactor, L_DEBUG, "Set READ flag on es "DAP_FORMAT_ESOCKET_UUID, a_es->uuid);
            dap_events_socket_set_readable_unsafe_ex(a_es, true, NULL);
        break;
        case DAP_SOCK_READY_TO_WRITE:
            debug_if(g_debug_reactor, L_DEBUG, "Set WRITE flag on es "DAP_FORMAT_ESOCKET_UUID, a_es->uuid);
            dap_events_socket_set_writable_unsafe_ex(a_es, true, 0, NULL);
        break;
        case DAP_SOCK_SIGNAL_CLOSE:
            debug_if(g_debug_reactor, L_DEBUG, "Set CLOSE flag on es "DAP_FORMAT_ESOCKET_UUID, a_es->uuid);
            dap_events_socket_remove_and_delete_unsafe(a_es, false);
        break;
        default:
            debug_if(g_debug_reactor, L_DEBUG, "Set flag %u on es "DAP_FORMAT_ESOCKET_UUID, flag, a_es->uuid);
            a_es->flags |= flag;
        }
    } else
        a_es->flags &= ~flag;
}

static void s_es_reassign(dap_context_t *a_c, OVERLAPPED *a_ol) {
    dap_events_socket_t *a_es = dap_context_find(a_c, (dap_events_socket_uuid_t)a_ol->Internal);
    if (!a_es)
        return log_it(L_ERROR, "Es #"DAP_FORMAT_ESOCKET_UUID" not found in context #%d", a_ol->Internal, a_c->id);
    dap_worker_t *l_new_worker = (dap_worker_t*)a_ol->Pointer;
    if ( a_es->was_reassigned && a_es->flags & DAP_SOCK_REASSIGN_ONCE )
        log_it(L_INFO, "Multiple worker switches for %p are forbidden", a_es);
    else
        dap_events_socket_reassign_between_workers_unsafe(a_es, l_new_worker);
}

int dap_events_socket_queue_data_send(dap_events_socket_t *a_es, const void *a_data, size_t a_size) {
    queue_entry_t *l_entry = DAP_ALMALLOC(MEMORY_ALLOCATION_ALIGNMENT, sizeof(queue_entry_t));
    *l_entry = (queue_entry_t) {
        .size = a_size,
        .data = a_size ? DAP_DUP_SIZE((char*)a_data, a_size) : (void*)a_data
    };
    if (g_debug_reactor) {
        if (a_size)
            log_it(L_DEBUG, "Enqueue %zu bytes into "DAP_FORMAT_ESOCKET_UUID, a_size, a_es->uuid);
        else
            log_it(L_DEBUG, "Enqueue ptr %p into "DAP_FORMAT_ESOCKET_UUID, a_data, a_es->uuid);
    }
    return InterlockedPushEntrySList((PSLIST_HEADER)a_es->buf_out, &(l_entry->entry))
        ? a_size : PostQueuedCompletionStatus(a_es->context->iocp, a_size, (ULONG_PTR)a_es, NULL)
            ? a_size : ( DAP_ALFREE(l_entry), log_it(L_ERROR, "Enqueue into es "DAP_FORMAT_ESOCKET_UUID" failed, errno %d",
                                                              a_es->uuid, GetLastError()), 0 );
}
#endif

/*
 *   DESCRIPTION: Release has been allocated dap_events_context. Check firstly against hash table.
 *
 *   INPUTS:
 *      a_marker:       An comment for the record, ASCIZ
 *
 *   IMPLICITE INPUTS:
 *      s_evsocks;      A hash table
 *
 *   OUTPUT:
 *      NONE
 *
 *   IMPLICITE OUTPUTS:
 *      s_evsocks
 *
 *   RETURNS:
 *      0:          a_es contains valid pointer
 *      <errno>
 */
static inline void s_dap_evsock_free(dap_events_socket_t *a_es)
{
#ifdef DAP_SYS_DEBUG
    pthread_rwlock_wrlock(&s_evsocks_lock);
    dap_events_socket_t *l_es = NULL;
    HASH_FIND(hh2, s_esockets, &a_es->uuid, sizeof(l_es->uuid), l_es);
    if (l_es)
        HASH_DELETE(hh2, s_esockets, l_es); /* Remove record from the table */
    pthread_rwlock_unlock(&s_evsocks_lock);
    if (!l_es)
        log_it(L_ERROR, "dap_events_socket:%p - uuid %zu not found", a_es, a_es->uuid);
    else if (l_es != a_es)
        log_it(L_WARNING, "[!] Esockets %p and %p share the same UUID %zu, possibly a dup!", a_es, l_es, a_es->uuid);
#endif
    debug_if(g_debug_reactor, L_DEBUG, "Release es %p \"%s\" uuid " DAP_FORMAT_ESOCKET_UUID,
                              a_es, dap_events_socket_get_type_str(a_es), a_es->uuid);
    DAP_DELETE(a_es);
}

/**
 * @brief dap_events_socket_init Init clients module
 * @return Zero if ok others if no
 */
int dap_events_socket_init( void )
{
    log_it(L_NOTICE,"Initialized events socket module");

#ifdef  DAP_SYS_DEBUG
    for (int i = 0; i < MEMSTAT$K_NR; i++)
        dap_memstat_reg(&s_memstat[i]);
#endif


    /*
     * @RRL: #6157
     * Use this thread's attribute to eliminate resource consuming by terminated threads
     */
    pthread_attr_init(&s_attr_detached);
    pthread_attr_setdetachstate(&s_attr_detached, PTHREAD_CREATE_DETACHED);

#if defined (DAP_EVENTS_CAPS_QUEUE_MQUEUE)
#include <sys/time.h>
#include <sys/resource.h>
    struct rlimit l_mqueue_limit;
    l_mqueue_limit.rlim_cur = RLIM_INFINITY;
    l_mqueue_limit.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_MSGQUEUE,&l_mqueue_limit);
    char l_cmd[256] ={0};
    snprintf(l_cmd, sizeof (l_cmd) - 1, "rm /dev/mqueue/%s-queue_ptr*", dap_get_appname());
    system(l_cmd);
    FILE *l_mq_msg_max = fopen("/proc/sys/fs/mqueue/msg_max", "w");
    if (l_mq_msg_max) {
        fprintf(l_mq_msg_max, "%d", DAP_QUEUE_MAX_MSGS);
        fclose(l_mq_msg_max);
    } else {
        log_it(L_ERROR, "Сan't open /proc/sys/fs/mqueue/msg_max file for writing, errno=%d", errno);
    }
#endif
    dap_timerfd_init();
    return 0;
}

/**
 * @brief dap_events_socket_deinit Deinit clients module
 */
void dap_events_socket_deinit(void)
{
}

/**
 * @brief dap_events_socket_wrap
 * @param a_events
 * @param w
 * @param s
 * @param a_callbacks
 * @return
 */
dap_events_socket_t *dap_events_socket_wrap_no_add( SOCKET a_sock, dap_events_socket_callbacks_t *a_callbacks )
{
    assert(a_callbacks);
    if (!a_callbacks) {
        log_it(L_CRITICAL, "Invalid arguments in dap_events_socket_wrap_no_add");
        return NULL;
    }

    dap_events_socket_t *l_es = s_dap_evsock_alloc(); /* @RRL: #6901 */
    if (!l_es)
        return NULL;

    l_es->socket = a_sock;
    if (a_callbacks)
        l_es->callbacks = *a_callbacks;

    l_es->flags = DAP_SOCK_READY_TO_READ;

    l_es->buf_in_size_max = DAP_EVENTS_SOCKET_BUF_SIZE;
    l_es->buf_out_size_max = DAP_EVENTS_SOCKET_BUF_SIZE;

    l_es->buf_in     = a_callbacks->timer_callback ? NULL : DAP_NEW_Z_SIZE(byte_t, l_es->buf_in_size_max);
    l_es->buf_out    = a_callbacks->timer_callback ? NULL : DAP_NEW_Z_SIZE(byte_t, l_es->buf_out_size_max);

#ifdef   DAP_SYS_DEBUG
    atomic_fetch_add(&s_memstat[MEMSTAT$K_BUF_OUT].alloc_nr, 1);
    atomic_fetch_add(&s_memstat[MEMSTAT$K_BUF_IN].alloc_nr, 1);
#endif

    l_es->buf_in_size = l_es->buf_out_size = 0;
#if defined(DAP_EVENTS_CAPS_EPOLL)
    l_es->ev_base_flags = EPOLLERR | EPOLLRDHUP | EPOLLHUP;
#elif defined(DAP_EVENTS_CAPS_POLL)
    l_es->poll_base_flags = POLLERR | POLLRDHUP | POLLHUP;
#elif defined(DAP_EVENTS_CAPS_KQUEUE)
    l_es->kqueue_event_catched_data.esocket = l_es;
    l_es->kqueue_base_flags = 0;
    l_es->kqueue_base_filter = 0;
#endif

    //log_it( L_DEBUG,"Dap event socket wrapped around %d sock a_events = %X", a_sock, a_events );

    return l_es;
}

/**
 * @brief dap_events_socket_assign_on_worker
 * @param a_es
 * @param a_worker
 */
void dap_events_socket_assign_on_worker(dap_events_socket_t * a_es, struct dap_worker * a_worker)
{
    a_es->last_ping_request = time(NULL);
   // log_it(L_DEBUG, "Assigned %p on worker %u", a_es, a_worker->id);
    dap_worker_add_events_socket(a_worker, a_es);
}

/**
 * @brief dap_events_socket_reassign_between_workers_unsafe
 * @param a_es
 * @param a_worker_new
 */
void dap_events_socket_reassign_between_workers_unsafe(dap_events_socket_t * a_es, dap_worker_t * a_worker_new)
{
    dap_worker_t *l_worker = a_es->worker;
    log_it(L_DEBUG, "Reassign between %u->%u workers: %p (%d)  ", l_worker->id, a_worker_new->id, a_es, a_es->fd );

    dap_context_remove(a_es);
    a_es->was_reassigned = true;
    if (a_es->callbacks.worker_unassign_callback)
        a_es->callbacks.worker_unassign_callback(a_es, l_worker);

    dap_worker_add_events_socket(a_worker_new, a_es);
}

/**
 * @brief dap_events_socket_reassign_between_workers
 * @param a_worker_old
 * @param a_es
 * @param a_worker_new
 */
void dap_events_socket_reassign_between_workers(dap_worker_t *a_worker_old, dap_events_socket_uuid_t a_es_uuid, dap_worker_t *a_worker_new)
{
    dap_return_if_fail(a_worker_new && a_worker_old);
    if (a_worker_old == dap_worker_get_current()) {
        dap_events_socket_t *l_es = dap_context_find(a_worker_old->context, a_es_uuid);
        if (!l_es) {
            log_it(L_WARNING, "UUID " DAP_UINT64_FORMAT_x " doesn't exists in worker %u", a_worker_old->id);
            return;
        }
        dap_events_socket_reassign_between_workers_unsafe(l_es, a_worker_new);
    }
#ifdef DAP_EVENTS_CAPS_IOCP
    dap_overlapped_t *ol = DAP_NEW_Z(dap_overlapped_t);
    ol->ol.Pointer = a_worker_new;
    ol->ol.Internal = a_es_uuid;
    ol->op = io_call;
    if ( !PostQueuedCompletionStatus(a_worker_old->context->iocp, 0, (ULONG_PTR)s_es_reassign, (OVERLAPPED*)ol) ) {
        log_it(L_ERROR, "Can't reassign es % " DAP_UINT64_FORMAT_x ",  error %d", a_es_uuid, GetLastError());
        dap_overlapped_free(ol);
    }
    return;
#else
    dap_worker_msg_reassign_t *l_msg = DAP_NEW_Z(dap_worker_msg_reassign_t);
    if (!l_msg) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return;
    }
    l_msg->esocket_uuid = a_es_uuid;
    l_msg->worker_new = a_worker_new;
    if( dap_events_socket_queue_ptr_send(a_worker_old->queue_es_reassign, l_msg) != 0 ){
#ifdef DAP_OS_WINDOWS
        log_it(L_ERROR,"Haven't sent reassign message with esocket %"DAP_UINT64_FORMAT_U, a_es ? a_es->socket : (SOCKET)-1);
#else
        log_it(L_ERROR,"Haven't sent reassign message with esocket %" DAP_UINT64_FORMAT_x, a_es_uuid);
#endif
        DAP_DELETE(l_msg);
    }
#endif
}



/**
 * @brief dap_events_socket_create_type_pipe
 * @param a_w
 * @param a_callback
 * @param a_flags
 * @return
 */
dap_events_socket_t * dap_events_socket_create_type_pipe(dap_worker_t *a_w, dap_events_socket_callback_t a_callback, uint32_t a_flags)
{
    dap_events_socket_t *l_es = dap_context_create_pipe(NULL, a_callback, a_flags);
    // If no worker - don't assign
    if (a_w)
        dap_events_socket_assign_on_worker(l_es, a_w);
    return l_es;
}

/**
 * @brief dap_events_socket_create
 * @param a_type
 * @param a_callbacks
 * @return
 */
dap_events_socket_t * dap_events_socket_create(dap_events_desc_type_t a_type, dap_events_socket_callbacks_t* a_callbacks)
{
    int l_sock_type = SOCK_STREAM;
    int l_sock_class = AF_INET;

    switch(a_type){
        case DESCRIPTOR_TYPE_SOCKET_CLIENT:
        break;
        case DESCRIPTOR_TYPE_SOCKET_UDP :
            l_sock_type = SOCK_DGRAM;
        break;
        case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
#ifdef DAP_OS_UNIX
            l_sock_class = AF_LOCAL;
#elif defined DAP_OS_WINDOWS
            l_sock_class = AF_INET;
#endif
        break;
        default:
            log_it(L_CRITICAL,"Can't create socket type %d", a_type );
            return NULL;
    }

#ifdef DAP_OS_WINDOWS
    SOCKET l_sock = socket(l_sock_class, l_sock_type, IPPROTO_IP);
    u_long l_socket_flags = 1;
    if (ioctlsocket((SOCKET)l_sock, (long)FIONBIO, &l_socket_flags))
        log_it(L_ERROR, "Error ioctl %d", WSAGetLastError());
#else
    int l_sock = socket(l_sock_class, l_sock_type, 0);
    int l_sock_flags = fcntl( l_sock, F_GETFL);
    l_sock_flags |= O_NONBLOCK;
    fcntl( l_sock, F_SETFL, l_sock_flags);

    if (l_sock == INVALID_SOCKET) {
        log_it(L_ERROR, "Socket create error");
        return NULL;
    }
#endif
    dap_events_socket_t * l_es =dap_events_socket_wrap_no_add(l_sock,a_callbacks);
    if(!l_es){
        log_it(L_CRITICAL,"Can't allocate memory for the new esocket");
        return NULL;
    }
    l_es->type = a_type ;
    debug_if(g_debug_reactor, L_DEBUG,"Created socket %"DAP_FORMAT_SOCKET" type %d", l_sock,l_es->type);
    return l_es;
}

/**
 * @brief s_socket_type_queue_ptr_input_callback_delete
 * @param a_es
 * @param a_arg
 */
static void s_socket_type_queue_ptr_input_callback_delete(dap_events_socket_t * a_es, void * a_arg)
{
    (void) a_arg;
    for (struct queue_ptr_input_item * l_item = PVT_QUEUE_PTR_INPUT(a_es)->items_first; l_item;  ){
        struct queue_ptr_input_item * l_item_next= l_item->next;
        DAP_DELETE(l_item);
        l_item= l_item_next;
    }
    PVT_QUEUE_PTR_INPUT(a_es)->items_first = PVT_QUEUE_PTR_INPUT(a_es)->items_last = NULL;
}

/**
 * @brief dap_events_socket_create_type_queue_mt
 * @param a_w
 * @param a_callback
 * @param a_flags
 * @return
 */
dap_events_socket_t * dap_events_socket_create_type_queue_ptr(dap_worker_t * a_w, dap_events_socket_callback_queue_ptr_t a_callback)
{
    dap_events_socket_t * l_es = dap_context_create_queue(NULL, a_callback);
    assert(l_es);
    // If no worker - don't assign
    if ( a_w)
        dap_events_socket_assign_on_worker(l_es,a_w);
    return  l_es;
}


/**
 * @brief dap_events_socket_queue_proc_input
 * @param a_esocket
 */
int dap_events_socket_queue_proc_input_unsafe(dap_events_socket_t * a_esocket)
{

#ifdef DAP_EVENTS_CAPS_WEPOLL
    ssize_t l_read = dap_recvfrom(a_esocket->socket, a_esocket->buf_in, a_esocket->buf_in_size_max);
    int l_errno = WSAGetLastError();
    if (l_read == SOCKET_ERROR) {
        log_it(L_ERROR, "Queue socket %zu received invalid data, error %d", a_esocket->socket, l_errno);
        return -1;
    }
#endif
    if (a_esocket->callbacks.queue_callback){
        if (a_esocket->flags & DAP_SOCK_QUEUE_PTR){

#if defined(DAP_EVENTS_CAPS_QUEUE_PIPE2)
            int l_read_errno = 0;
            char l_body[PIPE_BUF] = { '\0' };
            ssize_t l_read_ret = read(a_esocket->fd, l_body, PIPE_BUF);
            l_read_errno = errno;
            if(l_read_ret > 0) {
                //debug_if(l_read_ret > (ssize_t)sizeof(void*), L_MSG, "[!] Read %ld bytes from pipe [es %d]", l_read_ret, a_esocket->fd2);
                if (l_read_ret % sizeof(void*)) {
                    log_it(L_CRITICAL, "[!] Read unaligned chunk [%zd bytes] from pipe, skip it", l_read_ret);
                    return -3;
                }
                for (long shift = 0; shift < l_read_ret; shift += sizeof(void*)) {
                    void *l_queue_ptr = *(void**)(l_body + shift);
                    a_esocket->callbacks.queue_ptr_callback(a_esocket, l_queue_ptr);
                }
            }
            else if ((l_read_errno != EAGAIN) && (l_read_errno != EWOULDBLOCK))
                log_it(L_ERROR, "Can't read message from pipe");
#elif defined (DAP_EVENTS_CAPS_QUEUE_MQUEUE)
            char l_body[DAP_QUEUE_MAX_BUFLEN * DAP_QUEUE_MAX_MSGS] = { '\0' };
            ssize_t l_ret, l_shift;
            for (l_ret = 0, l_shift = 0;
                 ((l_ret = mq_receive(a_esocket->mqd, l_body + l_shift, sizeof(void*), NULL)) == sizeof(void*)) && ((size_t)l_shift < sizeof(l_body) - sizeof(void*));
                 l_shift += l_ret)
            {
                void *l_queue_ptr = *(void**)(l_body + l_shift);
                a_esocket->callbacks.queue_ptr_callback(a_esocket, l_queue_ptr);
            }
            if (l_ret == -1) {
                switch (errno) {
                case EAGAIN:
                    debug_if(g_debug_reactor, L_INFO, "Received and processed %lu callbacks in 1 pass", l_shift / 8);
                    break;
                default:
                    return log_it(L_ERROR, "mq_receive error in esocket queue_ptr:\"%s\" code %d", dap_strerror(errno), errno), -1;
                }
            }
#elif defined DAP_EVENTS_CAPS_WEPOLL
            if(l_read > 0) {
                debug_if(g_debug_reactor, L_NOTICE, "Got %ld bytes from socket", l_read);
                for (long shift = 0; shift < l_read; shift += sizeof(void*)) {
                    void *l_queue_ptr = *(void **)(a_esocket->buf_in + shift);
                    a_esocket->callbacks.queue_ptr_callback(a_esocket, l_queue_ptr);
                }
            }
            else if ((l_errno != EAGAIN) && (l_errno != EWOULDBLOCK))  // we use blocked socket for now but who knows...
                log_it(L_ERROR, "Can't read message from socket");
#elif defined DAP_EVENTS_CAPS_KQUEUE
            void *l_queue_ptr = a_esocket->kqueue_event_catched_data.data;
            if(g_debug_reactor)
                log_it(L_INFO,"Queue ptr received %p ptr on input", l_queue_ptr);
            if(a_esocket->callbacks.queue_ptr_callback)
                a_esocket->callbacks.queue_ptr_callback (a_esocket, l_queue_ptr);
#elif defined DAP_EVENTS_CAPS_IOCP
            queue_entry_t *l_work_item = (queue_entry_t*)InterlockedFlushSList((PSLIST_HEADER)a_esocket->buf_out), *l_tmp, *l_prev;
            if (!l_work_item)
                return log_it(L_ERROR, "Queue "DAP_FORMAT_ESOCKET_UUID" is empty", a_esocket->uuid), -3;
            // Reverse list for FIFO usage
            if (l_work_item->entry.Next) {
                for (l_prev = NULL; l_work_item; l_work_item = l_tmp) {
                    l_tmp = (queue_entry_t*)l_work_item->entry.Next;
                    l_work_item->entry.Next = (SLIST_ENTRY*)l_prev;
                    l_prev = l_work_item;
                }
                l_work_item = l_prev;
            }

            UINT l_count = 0;
            for( ; l_work_item && (l_tmp = (queue_entry_t*)l_work_item->entry.Next, 1); ++l_count, l_work_item = l_tmp ) {
                a_esocket->callbacks.queue_ptr_callback(a_esocket, l_work_item->data);
                DAP_ALFREE(l_work_item);
            }
            debug_if(g_debug_reactor, L_DEBUG, "Dequeued %u items from "DAP_FORMAT_ESOCKET_UUID, l_count, a_esocket->uuid);
#else
#error "No Queue fetch mechanism implemented on your platform"
#endif
        } else {
#ifdef DAP_EVENTS_CAPS_KQUEUE
            void * l_queue_ptr = a_esocket->kqueue_event_catched_data.data;
            size_t l_queue_ptr_size = a_esocket->kqueue_event_catched_data.size;
            if(g_debug_reactor)
                log_it(L_INFO,"Queue received %zd bytes on input", l_queue_ptr_size);

            a_esocket->callbacks.queue_callback(a_esocket, l_queue_ptr, l_queue_ptr_size);
#elif !defined(DAP_OS_WINDOWS)
            read(a_esocket->socket, a_esocket->buf_in, a_esocket->buf_in_size_max );
#endif
        }
    }else{
        log_it(L_ERROR, "Queue socket %"DAP_FORMAT_SOCKET" accepted data but callback is NULL ", a_esocket->socket);
#ifdef DAP_EVENTS_CAPS_IOCP
        for ( queue_entry_t *l_work_item = (queue_entry_t*)InterlockedFlushSList((PSLIST_HEADER)a_esocket->buf_out), *l_tmp;
              l_work_item && (l_tmp = (queue_entry_t*)l_work_item->entry.Next, 1); DAP_ALFREE(l_work_item), l_work_item = l_tmp ) { }
#endif
        return -2;
    }
    return 0;
}

/**
 * @brief dap_events_socket_create_type_event
 * @param a_w
 * @param a_callback
 * @return
 */
dap_events_socket_t * dap_events_socket_create_type_event(dap_worker_t * a_w, dap_events_socket_callback_event_t a_callback)
{
    dap_events_socket_t * l_es = dap_context_create_event(NULL, a_callback);
    // If no worker - don't assign
    if ( a_w)
        dap_events_socket_assign_on_worker(l_es,a_w);
    return  l_es;
}

/**
 * @brief dap_events_socket_event_proc_input_unsafe
 * @param a_esocket
 */
void dap_events_socket_event_proc_input_unsafe(dap_events_socket_t *a_esocket)
{
    if (a_esocket->callbacks.event_callback ){
#if defined(DAP_EVENTS_CAPS_EVENT_EVENTFD )
        eventfd_t l_value;
        if(eventfd_read( a_esocket->fd, &l_value)==0 ){ // would block if not ready
            a_esocket->callbacks.event_callback(a_esocket, l_value);
        }else if ( (errno != EAGAIN) && (errno != EWOULDBLOCK) )
            log_it(L_WARNING, "Can't read packet from event fd, error %d: \"%s\"", errno, dap_strerror(errno));
        else
            return; // do nothing
#elif defined DAP_EVENTS_CAPS_WEPOLL
        u_short l_value;
        int l_ret;
        switch (l_ret = dap_recvfrom(a_esocket->socket, &l_value, sizeof(char))) {
        case SOCKET_ERROR:
            log_it(L_CRITICAL, "Can't read from event socket, error: %d", WSAGetLastError());
            break;
        case 0:
            return;
        default:
            a_esocket->callbacks.event_callback(a_esocket, l_value);
            return;
        }
#elif defined (DAP_EVENTS_CAPS_KQUEUE)
    a_esocket->callbacks.event_callback(a_esocket, a_esocket->kqueue_event_catched_data.value);
#elif defined DAP_EVENTS_CAPS_IOCP
    a_esocket->callbacks.event_callback(a_esocket, 1);
#else
#error "No Queue fetch mechanism implemented on your platform"
#endif
    } else
        log_it(L_ERROR, "Event socket %"DAP_FORMAT_SOCKET" accepted data but callback is NULL ", a_esocket->socket);
}

#ifdef DAP_EVENTS_CAPS_QUEUE_PIPE2

/**
 *  Waits on the socket
 *  return 0: timeout, 1: may send data, -1 error
 */
static int s_wait_send_socket(SOCKET a_sockfd, long timeout_ms)
{
    struct timeval l_tv;
    l_tv.tv_sec = timeout_ms / 1000;
    l_tv.tv_usec = (timeout_ms % 1000) * 1000;

    fd_set l_outfd;
    FD_ZERO(&l_outfd);
    FD_SET(a_sockfd, &l_outfd);

    while (1) {
#ifdef DAP_OS_WINDOWS
        int l_res = select(1, NULL, &l_outfd, NULL, &l_tv);
#else
        int l_res = select(a_sockfd + 1, NULL, &l_outfd, NULL, &l_tv);
#endif
        if (l_res == 0) {
            //log_it(L_DEBUG, "socket %d timed out", a_sockfd)
            return -2;
        }
        if (l_res == -1) {
            if (errno == EINTR)
                continue;
            log_it(L_DEBUG, "socket %"DAP_FORMAT_SOCKET" waiting errno=%d", a_sockfd, errno);
            return l_res;
        }
        break;
    };

    if (FD_ISSET(a_sockfd, &l_outfd))
        return 0;

    return -1;
}


/**
 * @brief dap_events_socket_buf_thread
 * @param arg
 * @return
 */
static void *s_dap_events_socket_buf_thread(void *arg)
{
    dap_events_socket_t *l_es = (dap_events_socket_t *)arg;
    if (!l_es) {
        log_it(L_ERROR, "NULL esocket in queue service thread");
        pthread_exit(0);
    }
    SOCKET l_sock = INVALID_SOCKET;
#if defined(DAP_EVENTS_CAPS_QUEUE_PIPE2)
        l_sock = l_es->fd2;
#elif defined(DAP_EVENTS_CAPS_QUEUE_MQUEUE)
        l_sock = l_es->mqd;
#endif
    while (1) {
        pthread_rwlock_wrlock(&l_es->buf_out_lock);
        errno = 0;
        ssize_t l_write_ret = write(l_sock, l_es->buf_out, dap_min((size_t)PIPE_BUF, l_es->buf_out_size));
        if (l_write_ret == -1) {
            switch (errno) {
            case EAGAIN:
                pthread_rwlock_unlock(&l_es->buf_out_lock);
                struct timeval l_tv = { .tv_sec = 120 };
                fd_set l_outfd; FD_ZERO(&l_outfd);
                FD_SET(l_sock, &l_outfd);
                sched_yield();
                switch ( select(l_sock + 1, NULL, &l_outfd, NULL, &l_tv) ) {
                case 0:
                    log_it(L_ERROR, "Es %p (fd %d) waiting timeout, data lost!",
                           l_es, l_es->fd2);
                case -1:
                    pthread_rwlock_wrlock(&l_es->buf_out_lock);
                    if (l_es->cb_buf_cleaner) {
                        size_t l_dropped = l_es->cb_buf_cleaner((char*)l_es->buf_out, l_es->buf_out_size);
                        log_it(L_INFO, "Drop %zu bytes on es %p (%d)", l_dropped, l_es, l_es->fd2);
                    }
                    l_es->buf_out_size = 0;
                    pthread_rwlock_unlock(&l_es->buf_out_lock);
                    pthread_exit(NULL);
                default:
                    if ( FD_ISSET(l_sock, &l_outfd) )
                        continue;
                    break;
                } break;
            default:
                log_it(L_CRITICAL, "[!] Can't write data to pipe! Errno %d", errno);
                l_es->buf_out_size = 0;
                pthread_rwlock_unlock(&l_es->buf_out_lock);
                pthread_exit(NULL);
            }
        } else if (l_write_ret == (ssize_t)l_es->buf_out_size) {
            debug_if(g_debug_reactor, L_DEBUG, "[!] Sent all %zd bytes to pipe [es %d]", l_write_ret, l_sock);
            l_es->buf_out_size = 0;
            pthread_rwlock_unlock(&l_es->buf_out_lock);
            break;
        } else if (l_write_ret) {
            debug_if(g_debug_reactor, L_DEBUG, "[!] Sent %zu / %zu bytes to pipe [es %d]", l_write_ret, l_es->buf_out_size, l_sock);
            l_es->buf_out_size -= l_write_ret;
            memmove(l_es->buf_out, l_es->buf_out + l_write_ret, l_es->buf_out_size);
        }

        if (l_write_ret % sizeof(arg))
            log_it(L_CRITICAL, "[!] Sent unaligned chunk [%zd bytes] to pipe, possible data corruption!", l_write_ret);
        pthread_rwlock_unlock(&l_es->buf_out_lock);
    }
    pthread_exit(NULL);
}

static void s_add_ptr_to_buf(dap_events_socket_t * a_es, void* a_arg)
{
    static atomic_uint_fast64_t l_thd_count;
    static const size_t l_basic_buf_size = DAP_QUEUE_MAX_MSGS * sizeof(void*);
    pthread_rwlock_wrlock(&a_es->buf_out_lock);
    if (!a_es->buf_out_size) {
        if (write(a_es->fd2, &a_arg, sizeof(a_arg)) == sizeof(a_arg)) {
            pthread_rwlock_unlock(&a_es->buf_out_lock);
            return;
        }
        int l_rc;
        pthread_t l_thread;
        atomic_fetch_add(&l_thd_count, 1);
        if ((l_rc = pthread_create(&l_thread, &s_attr_detached /* @RRL: #6157 */, s_dap_events_socket_buf_thread, a_es))) {
            log_it(L_ERROR, "[#%"DAP_UINT64_FORMAT_U"] Cannot start thread, drop a_es: %p, a_arg: %p, rc: %d",
                   atomic_load(&l_thd_count), a_es, a_arg, l_rc);
            pthread_rwlock_unlock(&a_es->buf_out_lock);
            return;
        }
        debug_if(g_debug_reactor, L_DEBUG, "[#%"DAP_UINT64_FORMAT_U"] Created thread %"DAP_UINT64_FORMAT_x", a_es: %p, a_arg: %p",
                     atomic_load(&l_thd_count), (uint64_t)l_thread, a_es, a_arg);
    } else if (a_es->buf_out_size_max < a_es->buf_out_size + sizeof(void*)) {
        a_es->buf_out_size_max += l_basic_buf_size;
        a_es->buf_out = DAP_REALLOC(a_es->buf_out, a_es->buf_out_size_max);
        debug_if(g_debug_reactor, L_MSG, "Es %p (%d): increase capacity to %zu, actual size: %zu",
               a_es, a_es->fd, a_es->buf_out_size_max, a_es->buf_out_size);
    } else if ((a_es->buf_out_size + sizeof(void*) <= l_basic_buf_size / 2) && (a_es->buf_out_size_max > l_basic_buf_size)) {
        a_es->buf_out_size_max = l_basic_buf_size;
        a_es->buf_out = DAP_REALLOC(a_es->buf_out, a_es->buf_out_size_max);
        debug_if(g_debug_reactor, L_MSG, "Es %p (%d): decrease capacity to %zu, actual size: %zu",
               a_es, a_es->fd, a_es->buf_out_size_max, a_es->buf_out_size);
    }
    *(void**)(a_es->buf_out + a_es->buf_out_size) = a_arg;
    a_es->buf_out_size += sizeof(a_arg);
    pthread_rwlock_unlock(&a_es->buf_out_lock);
}
#endif

/**
 * @brief dap_events_socket_event_signal
 * @param a_es
 * @param a_value
 * @return
 */
int dap_events_socket_event_signal( dap_events_socket_t * a_es, uint64_t a_value)
{
    dap_return_val_if_fail(a_es, -1);
#if defined(DAP_EVENTS_CAPS_EVENT_EVENTFD)
    int ret = eventfd_write( a_es->fd2,a_value);
        int l_errno = errno;
        if (ret == 0 )
            return  0;
        else if ( ret < 0)
            return l_errno;
        else
            return 1;
#elif defined DAP_EVENTS_CAPS_WEPOLL
    return dap_sendto(a_es->socket, a_es->port, NULL, 0) == SOCKET_ERROR ? WSAGetLastError() : NO_ERROR;
#elif defined (DAP_EVENTS_CAPS_IOCP)
    return PostQueuedCompletionStatus(a_es->context->iocp, a_value, (ULONG_PTR)a_es, NULL) ? GetLastError() : NO_ERROR;
#elif defined (DAP_EVENTS_CAPS_KQUEUE)
    struct kevent l_event={0};
    dap_events_socket_w_data_t * l_es_w_data = DAP_NEW_Z(dap_events_socket_w_data_t);
    l_es_w_data->esocket = a_es;
    l_es_w_data->value = a_value;

    EV_SET(&l_event,a_es->socket, EVFILT_USER, EV_ADD | EV_ONESHOT , NOTE_FFNOP | NOTE_TRIGGER ,(intptr_t) a_es->socket, l_es_w_data);

    int l_n;

    if(a_es->pipe_out){ // If we have pipe out - we send events directly to the pipe out kqueue fd
        if(a_es->pipe_out->context)
            l_n = kevent(a_es->pipe_out->context->kqueue_fd,&l_event,1,NULL,0,NULL);
        else {
            log_it(L_WARNING,"Trying to send pointer in pipe out queue thats not assigned to any worker or proc thread");
            l_n = -1;
        }
    }else if(a_es->context)
        l_n = kevent(a_es->context->kqueue_fd,&l_event,1,NULL,0,NULL);
    else
        l_n = -1;

    if(l_n == -1){
        log_it(L_ERROR,"Haven't sent pointer in pipe out queue, code %d", l_n);
        DAP_DELETE(l_es_w_data);
    }
    return l_n;
#else
#error "Not implemented dap_events_socket_event_signal() for this platform"
#endif
}

/**
 * @brief dap_events_socket_wrap_listener
 * @param a_server
 * @param a_sock
 * @param a_callbacks
 * @return
 */
dap_events_socket_t *dap_events_socket_wrap_listener(dap_server_t *a_server, SOCKET a_sock, dap_events_socket_callbacks_t *a_callbacks)
{
    if (!a_callbacks || !a_server) {
        log_it(L_CRITICAL, "Invalid arguments in dap_events_socket_wrap_listener");
        return NULL;
    }
    dap_events_socket_t *l_es = s_dap_evsock_alloc();
    if (!l_es)
        return NULL;

    l_es->socket = a_sock;
    l_es->server = a_server;
    l_es->callbacks = *a_callbacks;

#ifdef   DAP_SYS_DEBUG
    atomic_fetch_add(&s_memstat[MEMSTAT$K_BUF_OUT].alloc_nr, 1);
    atomic_fetch_add(&s_memstat[MEMSTAT$K_BUF_IN].alloc_nr, 1);
#endif

    l_es->flags = DAP_SOCK_READY_TO_READ;
    l_es->last_time_active = l_es->last_ping_request = time( NULL );
    l_es->buf_in = DAP_NEW_Z_SIZE(byte_t, 2 * sizeof(struct sockaddr_storage) + 32);
    return l_es;
}

/**
 * @brief s_remove_and_delete_unsafe_delayed_delete_callback
 * @param arg
 * @return
 */
bool s_remove_and_delete_unsafe_delayed_delete_callback(void * a_arg)
{
    dap_worker_t * l_worker = dap_worker_get_current();
    dap_events_socket_uuid_w_data_t * l_es_handler = (dap_events_socket_uuid_w_data_t*) a_arg;
    assert(l_es_handler);
    assert(l_worker);
    dap_events_socket_t * l_es;
    if( (l_es = dap_context_find(l_worker->context, l_es_handler->esocket_uuid)) != NULL)
        dap_events_socket_remove_and_delete_unsafe( l_es, l_es_handler->value == 1);
    DAP_DELETE(l_es_handler);

    return false;
}

/**
 * @brief dap_events_socket_remove_and_delete_unsafe_delayed
 * @param a_es
 * @param a_preserve_inheritor
 */
void dap_events_socket_remove_and_delete_unsafe_delayed( dap_events_socket_t *a_es, bool a_preserve_inheritor )
{
    dap_events_socket_uuid_w_data_t * l_es_handler = DAP_NEW_Z(dap_events_socket_uuid_w_data_t);
    if (!l_es_handler) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return;
    }
    l_es_handler->esocket_uuid = a_es->uuid;
    l_es_handler->value = a_preserve_inheritor ? 1 : 0;
    //dap_events_socket_descriptor_close(a_es);

    dap_worker_t * l_worker = a_es->worker;
    dap_context_remove(a_es);
    a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
    dap_timerfd_start_on_worker(l_worker, s_delayed_ops_timeout_ms,
                                s_remove_and_delete_unsafe_delayed_delete_callback, l_es_handler );
}

/**
 * @brief dap_events_socket_descriptor_close
 * @param a_socket
 */
void dap_events_socket_descriptor_close(dap_events_socket_t *a_esocket)
{
    if ( a_esocket->socket > 0
#ifdef DAP_OS_BSD
        && a_esocket->type != DESCRIPTOR_TYPE_TIMER
#endif    
     ) {
#ifdef DAP_OS_WINDOWS
        //LINGER  lingerStruct = { .l_onoff = 1, .l_linger = 5 };
        //setsockopt(a_esocket->socket, SOL_SOCKET, SO_LINGER, (char*)&lingerStruct, sizeof(lingerStruct) );
        // We must set { 1, 0 } when connections must be reset (RST)
        shutdown(a_esocket->socket, SD_BOTH);
#endif
        closesocket(a_esocket->socket);
    }
    if ( a_esocket->fd2 > 0 )
        closesocket(a_esocket->fd2);
    a_esocket->socket = a_esocket->socket2 = INVALID_SOCKET;
}

/**
 * @brief dap_events_socket_remove Removes the client from the list
 * @param sc Connection instance
 */
void dap_events_socket_remove_and_delete_unsafe( dap_events_socket_t *a_es, bool preserve_inheritor )
{
    assert(a_es);
    debug_if(g_debug_reactor, L_DEBUG, "Remove es %p [%s] \"%s\" uuid "DAP_FORMAT_ESOCKET_UUID"",
             a_es, a_es->socket == INVALID_SOCKET ? "" : dap_itoa(a_es->socket),
             dap_events_socket_get_type_str(a_es), a_es->uuid);
    if( a_es->callbacks.delete_callback )
        a_es->callbacks.delete_callback( a_es, a_es->callbacks.arg );
    dap_context_remove(a_es);

#ifdef DAP_EVENTS_CAPS_IOCP
    int l_res = 0;
    const char *func = "Delete";
    a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
    if (preserve_inheritor)
        a_es->flags |= DAP_SOCK_KEEP_INHERITOR;
    switch (a_es->type) {
    case DESCRIPTOR_TYPE_SOCKET_CLIENT:
    case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
    case DESCRIPTOR_TYPE_SOCKET_LISTENING:
    case DESCRIPTOR_TYPE_SOCKET_LOCAL_LISTENING:
    case DESCRIPTOR_TYPE_SOCKET_UDP:
    case DESCRIPTOR_TYPE_FILE:
    case DESCRIPTOR_TYPE_PIPE:
        if ( a_es->pending_read || a_es->pending_write ) {
            l_res = CancelIoEx((HANDLE)a_es->socket, NULL) ? ERROR_IO_PENDING : GetLastError();
            func = "CancelIoEx";
        } else
            dap_events_socket_descriptor_close(a_es);
    break;
    case DESCRIPTOR_TYPE_QUEUE:
        for ( queue_entry_t *l_work_item = (queue_entry_t*)InterlockedFlushSList((PSLIST_HEADER)a_es->buf_out), *l_tmp;
              l_work_item && ( (l_tmp = (queue_entry_t*)l_work_item->entry.Next), 1 ); DAP_ALFREE(l_work_item), l_work_item = l_tmp ) { }
        DAP_ALFREE(a_es->buf_out);
        a_es->buf_out = NULL;
    break;
    case DESCRIPTOR_TYPE_TIMER: {
        dap_timerfd_t *l_timerfd = (dap_timerfd_t*)a_es->_inheritor;
        l_res = a_es->pending_read ? ERROR_IO_PENDING : ( l_timerfd->events_socket = NULL, dap_del_queuetimer(l_timerfd->th) );
        func = "Delete Queue Timer";
    } break;
    default: break;
    }

    switch (l_res) {
    case 0:
        debug_if(g_debug_reactor, L_DEBUG, "\"%s\" on es "DAP_FORMAT_ESOCKET_UUID" completed immediately", func, a_es->uuid);
    break;
    case ERROR_IO_PENDING:
        debug_if(g_debug_reactor, L_DEBUG, "Pending \"%s\" on es "DAP_FORMAT_ESOCKET_UUID, func, a_es->uuid);
        return;
    default:
        debug_if(g_debug_reactor, L_DEBUG, "\"%s\" on es "DAP_FORMAT_ESOCKET_UUID" failed, error %d: \"%s\"",
                                           func, a_es->uuid, l_res, dap_strerror(l_res));
        dap_events_socket_descriptor_close(a_es);
        return;
    }
    debug_if(g_debug_reactor && FLAG_KEEP_INHERITOR(a_es->flags), L_DEBUG, "Keep inheritor of "DAP_FORMAT_ESOCKET_UUID, a_es->uuid);
    dap_events_socket_delete_unsafe( a_es, FLAG_KEEP_INHERITOR(a_es->flags) );
#else
    dap_events_socket_delete_unsafe(a_es, preserve_inheritor);
#endif
}

#ifdef DAP_EVENTS_CAPS_IOCP

void dap_events_socket_set_readable_unsafe_ex(dap_events_socket_t *a_es, bool a_is_ready, dap_overlapped_t *a_ol) {
    if (a_es->flags & DAP_SOCK_SIGNAL_CLOSE) {
        debug_if(g_debug_reactor, L_DEBUG, "Attempt to %sset read flag on closed socket %p, dump it",
                                           a_is_ready ? "" : "un", a_es);
        return dap_overlapped_free(a_ol);
    }
    if (!a_is_ready) {
        a_es->flags &= ~DAP_SOCK_READY_TO_READ;
        return dap_overlapped_free(a_ol);
    }
    long l_err = ERROR_OPERATION_ABORTED;
    const char *func = "";
    dap_overlapped_t *ol = NULL;
    if (a_es->pending_read) {
        debug_if( g_debug_reactor, L_DEBUG, DAP_FORMAT_ESOCKET_UUID" : %zu \"%s\" already has pending read, dump it",
                                            a_es->uuid, a_es->socket, dap_events_socket_get_type_str(a_es) );
        l_err = ERROR_IO_PENDING;
    } else {
        a_es->pending_read = 1;
        a_es->flags |= DAP_SOCK_READY_TO_READ;
        DWORD flags = 0, bytes = 0;
        if (a_ol) {
            ol = a_ol;
            if (ol->ol.hEvent) ResetEvent(ol->ol.hEvent);
            else ol->ol.hEvent = CreateEvent(0, TRUE, FALSE, NULL);
        } else {
            ol = DAP_NEW(dap_overlapped_t);
            *ol = (dap_overlapped_t){ .ol.hEvent = CreateEvent(0, TRUE, FALSE, NULL) };
        }
        ol->op = io_read;
        WSABUF wsabuf = { .buf = a_es->buf_in + a_es->buf_in_size, .len = a_es->buf_in_size_max - a_es->buf_in_size };

        switch (a_es->type) {
        case DESCRIPTOR_TYPE_SOCKET_CLIENT:
        case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
            l_err = WSARecv( a_es->socket, &wsabuf, 1, &bytes, &flags, (OVERLAPPED*)ol, NULL ) ? WSAGetLastError() : ERROR_SUCCESS;
            func = "WSARecv";
            break;

        case DESCRIPTOR_TYPE_SOCKET_UDP: {
            INT l_len = sizeof(a_es->addr_storage);
            l_err = WSARecvFrom( a_es->socket, &wsabuf, 1, &bytes, &flags,
                                (LPSOCKADDR)&a_es->addr_storage, &l_len, 
                                (OVERLAPPED*)ol, NULL ) ? WSAGetLastError() : ERROR_SUCCESS;
            func = "WSARecvFrom";
        } break;

        case DESCRIPTOR_TYPE_SOCKET_LISTENING:
        case DESCRIPTOR_TYPE_SOCKET_LOCAL_LISTENING:
            if ( (a_es->socket2 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET ) {
                log_it(L_ERROR, "Failed to create socket for accept()'ing, errno %d", WSAGetLastError());
                break;
            }
            u_long l_option = 1;
            if ( setsockopt(a_es->socket2, SOL_SOCKET, SO_REUSEADDR, (const char*)&l_option, sizeof(int)) < 0 ) {
                _set_errno( WSAGetLastError() );
                log_it(L_ERROR, "setsockopt(SO_REUSEADDR) on socket %d failed, error %d: \"%s\"",
                                a_es->socket2, errno, dap_strerror(errno));
            }
            l_err = pfnAcceptEx( a_es->socket, a_es->socket2, (LPVOID)(a_es->buf_in), 0,
                                 sizeof(SOCKADDR_STORAGE) + 16, sizeof(SOCKADDR_STORAGE) + 16, 
                                 &bytes, (OVERLAPPED*)ol ) ? ERROR_SUCCESS : WSAGetLastError();
            func = "AcceptEx";
            break;

        case DESCRIPTOR_TYPE_FILE:
        case DESCRIPTOR_TYPE_PIPE:
            l_err = ReadFile( a_es->h, a_es->buf_in, a_es->buf_in_size_max, &bytes, (OVERLAPPED*)ol ) ? ERROR_SUCCESS : GetLastError();
            func = "ReadFile";
            break;

        default:
            log_it(L_ERROR, "Unsupported es %p type: %d, dump it", a_es, a_es->type);
            a_es->flags &= ~DAP_SOCK_READY_TO_READ;
            break;
        }
    }

    switch (l_err) {
    case ERROR_SUCCESS:
    case ERROR_IO_PENDING:
        debug_if(g_debug_reactor, L_DEBUG, "Pending \"%s\" on [%s] "DAP_FORMAT_ESOCKET_UUID,
                                           func, dap_events_socket_get_type_str(a_es), a_es->uuid);
        return;
    default:
        a_es->pending_read = 0;
        log_it(L_ERROR, "Operation \"%s\" on [%s] "DAP_FORMAT_ESOCKET_UUID" failed with error %ld: \"%s\"",
                        func, dap_events_socket_get_type_str(a_es), a_es->uuid, l_err, dap_strerror(l_err));
        if ( a_es->callbacks.error_callback )
            a_es->callbacks.error_callback(a_es, l_err);
        if ( !a_es->no_close )
            a_ol ? a_es->flags = DAP_SOCK_SIGNAL_CLOSE : dap_events_socket_remove_and_delete(a_es->worker, a_es->uuid);
    }
    return dap_overlapped_free(ol);
}

void dap_events_socket_set_writable_unsafe_ex( dap_events_socket_t *a_es, bool a_is_ready, size_t a_size, dap_overlapped_t *a_ol )
{
    if (a_es->flags & DAP_SOCK_SIGNAL_CLOSE) {
        debug_if(g_debug_reactor, L_DEBUG, "Attempt to %sset write flag on closed socket %p, dump it",
                                           a_is_ready ? "" : "un", a_es);
        return dap_overlapped_free(a_ol);
    }
    if (!a_is_ready) {
        a_es->flags &= ~DAP_SOCK_READY_TO_WRITE;
        return dap_overlapped_free(a_ol);
    }
    ++a_es->pending_write;
    a_es->flags |= DAP_SOCK_READY_TO_WRITE;
    int l_err = ERROR_OPERATION_ABORTED;
    DWORD flags = 0, bytes = 0;
    const char *func = "Write";
    dap_overlapped_t *ol = NULL;
    if (a_ol) {
        ol = a_es->buf_out_size ? DAP_REALLOC( a_ol, sizeof(dap_overlapped_t) + a_size + a_es->buf_out_size ) : a_ol;
        if (ol->ol.hEvent)
            ResetEvent(ol->ol.hEvent);
        else
            ol->ol.hEvent = CreateEvent(0, TRUE, FALSE, NULL);
        ol->ol.Internal = 0;
        ol->op = io_write;
    } else {
        ol = DAP_NEW_SIZE(dap_overlapped_t, sizeof(dap_overlapped_t) + a_es->buf_out_size);
        *ol = (dap_overlapped_t) { .ol.hEvent = CreateEvent(0, TRUE, FALSE, NULL), .op = io_write };
        a_size = 0;
    }
    if (a_es->buf_out_size)
        memcpy(ol->buf + a_size, a_es->buf_out, a_es->buf_out_size);
    a_size += a_es->buf_out_size;

    switch (a_es->type) {
    case DESCRIPTOR_TYPE_SOCKET_CLIENT:
    case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
        if (a_es->flags & DAP_SOCK_CONNECTING) {
            SOCKADDR_IN l_addr_any = {
                .sin_family = AF_INET, .sin_port = 0,
                .sin_addr   = {{ .S_addr = INADDR_ANY }}
            };
            if ( bind(a_es->socket, (PSOCKADDR)&l_addr_any, sizeof(l_addr_any)) ) {
                log_it(L_ERROR, "Failed to create socket for connect(), errno %d", WSAGetLastError());
                break;
            }
            l_err = pfnConnectEx(a_es->socket, (PSOCKADDR)&a_es->addr_storage, sizeof(SOCKADDR),
                                  NULL, 0, NULL, (OVERLAPPED*)ol) ? ERROR_SUCCESS : WSAGetLastError();
            func = "ConnectEx";
        } else {
            if ( a_size ) {
                l_err = WSASend( a_es->socket, &(WSABUF){ .len = a_size, .buf = ol->buf },
                                 1, &bytes, flags, (OVERLAPPED*)ol, NULL ) ? WSAGetLastError() : ERROR_SUCCESS;
                func = "WSASend";
            } else
                l_err = PostQueuedCompletionStatus( a_es->context->iocp, 0, (ULONG_PTR)a_es, (OVERLAPPED*)ol )
                    ? ERROR_SUCCESS : GetLastError();
        }
    break;
    case DESCRIPTOR_TYPE_SOCKET_UDP:
        if ( a_size ) {
            l_err = WSASendTo( a_es->socket, &(WSABUF) { .len = a_size, .buf = ol->buf },
                               1, &bytes, flags, (LPSOCKADDR)&a_es->addr_storage, 
                               sizeof(a_es->addr_storage), (OVERLAPPED*)ol, NULL ) ? WSAGetLastError() : ERROR_SUCCESS;
            func = "WSASendTo";
        } else
            l_err = PostQueuedCompletionStatus(a_es->context->iocp, 0, (ULONG_PTR)a_es, (OVERLAPPED*)ol)
                    ? ERROR_SUCCESS : GetLastError();
    break;
    case DESCRIPTOR_TYPE_FILE:
    case DESCRIPTOR_TYPE_PIPE:
        if ( a_size ) {
            l_err = WriteFile(a_es->h, a_es->buf_out, a_es->buf_out_size, NULL, (OVERLAPPED*)ol) ? ERROR_SUCCESS : GetLastError();
            func = "WriteFile";
        } else
            l_err = PostQueuedCompletionStatus(a_es->context->iocp, 0, (ULONG_PTR)a_es, (OVERLAPPED*)ol)
                    ? ERROR_SUCCESS : GetLastError();
    break;
    default:
        log_it(L_ERROR, "Unsupported es %p type %d, dump it", a_es, a_es->type);
        a_es->flags &= ~DAP_SOCK_READY_TO_WRITE;
        break;
    }

    switch (l_err) {
    case ERROR_SUCCESS:
    case ERROR_IO_PENDING:
        debug_if(g_debug_reactor, L_DEBUG, "Pending \"%s\" on [%s] "DAP_FORMAT_ESOCKET_UUID,
                                           func, dap_events_socket_get_type_str(a_es), a_es->uuid);
        a_es->buf_out_size = 0;
        return;
    default:
        --a_es->pending_write;
        log_it(L_ERROR, "Operation \"%s\" on [%s] "DAP_FORMAT_ESOCKET_UUID" failed with error %ld: \"%s\"",
                        func, dap_events_socket_get_type_str(a_es), a_es->uuid, l_err, dap_strerror(l_err));
        if ( a_es->callbacks.error_callback )
            a_es->callbacks.error_callback(a_es, l_err);
        if ( !a_es->no_close )
            a_ol ? a_es->flags = DAP_SOCK_SIGNAL_CLOSE : dap_events_socket_remove_and_delete(a_es->worker, a_es->uuid);
    }
    return dap_overlapped_free(ol);
}

#else

/**
 * @brief dap_events_socket_ready_to_read
 * @param sc
 * @param isReady
 */
void dap_events_socket_set_readable_unsafe( dap_events_socket_t *a_esocket, bool a_is_ready )
{
    if( a_is_ready == (bool)(a_esocket->flags & DAP_SOCK_READY_TO_READ))
        return;
    if ( a_is_ready ){
        a_esocket->flags |= DAP_SOCK_READY_TO_READ;
    }else{
        a_esocket->flags &= ~DAP_SOCK_READY_TO_READ;
    }
#ifdef DAP_EVENTS_CAPS_EVENT_KEVENT
    if( a_esocket->type != DESCRIPTOR_TYPE_EVENT &&
        a_esocket->type != DESCRIPTOR_TYPE_QUEUE &&
        a_esocket->type != DESCRIPTOR_TYPE_TIMER  ){
        struct kevent l_event;
        uint16_t l_op_flag = a_is_ready? EV_ADD : EV_DELETE;
        EV_SET(&l_event, a_esocket->socket, EVFILT_READ,
               a_esocket->kqueue_base_flags | l_op_flag,a_esocket->kqueue_base_fflags ,
               a_esocket->kqueue_data,a_esocket);
        int l_kqueue_fd = a_esocket->context? a_esocket->context->kqueue_fd : -1;
        if( l_kqueue_fd>0 ){
            int l_kevent_ret = kevent(l_kqueue_fd,&l_event,1,NULL,0,NULL);
            int l_errno = errno;
            if ( l_kevent_ret == -1 && l_errno != EINPROGRESS ){
                if (l_errno == EBADF){
                    log_it(L_ATT,"Set readable: socket %d (%p ) disconnected, rise CLOSE flag to remove from queue, lost %"DAP_UINT64_FORMAT_U":%" DAP_UINT64_FORMAT_U
                           " bytes",a_esocket->socket,a_esocket,a_esocket->buf_in_size,a_esocket->buf_out_size);
                    a_esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
                    a_esocket->buf_in_size = a_esocket->buf_out_size = 0; // Reset everything from buffer, we close it now all
                }else{
                    log_it(L_ERROR,"Can't update client socket %d state on kqueue fd for set_read op %d: \"%s\" (%d)",
                                    a_esocket->socket, l_kqueue_fd, dap_strerror(l_errno), l_errno);
                }
            }
        }
    }else
        log_it(L_WARNING,"Trying to set readable/writable event, queue or timer thats you shouldnt do");
#else
    dap_context_poll_update(a_esocket);
#endif
}

/**
 * @brief dap_events_socket_ready_to_write
 * @param a_esocket
 * @param isReady
 */
void dap_events_socket_set_writable_unsafe(dap_events_socket_t *a_esocket, bool a_is_ready )
{
    if (!a_esocket || a_is_ready == (bool)(a_esocket->flags & DAP_SOCK_READY_TO_WRITE))
        return;

    if ( a_is_ready )
        a_esocket->flags |= DAP_SOCK_READY_TO_WRITE;
    else
        a_esocket->flags &= ~DAP_SOCK_READY_TO_WRITE;

#ifdef DAP_EVENTS_CAPS_EVENT_KEVENT
    if( a_esocket->type != DESCRIPTOR_TYPE_EVENT &&
        a_esocket->type != DESCRIPTOR_TYPE_QUEUE &&
        a_esocket->type != DESCRIPTOR_TYPE_TIMER  ){
        struct kevent l_event;
        uint16_t l_op_flag = a_is_ready? EV_ADD : EV_DELETE;
        int l_expected_reply = a_is_ready? 1: 0;
        EV_SET(&l_event, a_esocket->socket, EVFILT_WRITE,
               a_esocket->kqueue_base_flags | l_op_flag,a_esocket->kqueue_base_fflags ,
               a_esocket->kqueue_data,a_esocket);
        int l_kqueue_fd = a_esocket->context? a_esocket->context->kqueue_fd : -1;
        if( l_kqueue_fd>0 ){
            int l_kevent_ret=kevent(l_kqueue_fd,&l_event,1,NULL,0,NULL);
            int l_errno = errno;
            if ( l_kevent_ret == -1 && l_errno != EINPROGRESS && l_errno != ENOENT ){
                if (l_errno == EBADF){
                    log_it(L_ATT,"Set writable: socket %d (%p ) disconnected, rise CLOSE flag to remove from queue, lost %"DAP_UINT64_FORMAT_U":%" DAP_UINT64_FORMAT_U
                           " bytes",a_esocket->socket,a_esocket,a_esocket->buf_in_size,a_esocket->buf_out_size);
                    a_esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
                    a_esocket->buf_in_size = a_esocket->buf_out_size = 0; // Reset everything from buffer, we close it now all
                }else{
                    log_it(L_ERROR,"Can't update client socket %d state on kqueue fd for set_write op %d: \"%s\" (%d)",
                                    a_esocket->socket, l_kqueue_fd, dap_strerror(l_errno), l_errno);
                }
            }
        }
    }else
        log_it(L_WARNING,"Trying to set readable/writable event, queue or timer thats you shouldnt do");
#else
    dap_context_poll_update(a_esocket);
#endif
}

/**
 * @brief dap_events_socket_send_event
 * @param a_es
 * @param a_arg
 */
int dap_events_socket_queue_ptr_send( dap_events_socket_t *a_es, void *a_arg)
{
    dap_return_val_if_fail(a_es && a_arg, -1);

    int l_ret = -1024, l_errno=0;

    if (g_debug_reactor)
        log_it(L_DEBUG,"Sent ptr %p to queue "DAP_FORMAT_ESOCKET_UUID, a_arg, a_es->uuid);

#if defined(DAP_EVENTS_CAPS_QUEUE_PIPE2)
    s_add_ptr_to_buf(a_es, a_arg);
    return 0;
#elif defined (DAP_EVENTS_CAPS_QUEUE_MQUEUE)
    assert(a_es);
    assert(a_es->mqd);
    //struct timespec tmo = {0};
    //tmo.tv_sec = 7 + time(NULL);
    if (!mq_send(a_es->mqd, (const char*)&a_arg, sizeof(a_arg), 0)) {
        debug_if (g_debug_reactor, L_DEBUG,"Sent ptr %p to esocket queue %p (%d)", a_arg, a_es, a_es? a_es->fd : -1);
        return 0;
    }
    switch (l_errno = errno) {
    case EINVAL:
    case EINTR:
    case EWOULDBLOCK:
        log_it(L_ERROR, "Can't send ptr to queue (err %d), will be resent again in a while...", l_errno);
        log_it(L_ERROR, "Number of pending messages: %ld", a_es->buf_out_size);
        s_add_ptr_to_buf(a_es, a_arg);
        return 0;
    default:
        return log_it(L_ERROR, "Can't send ptr to queue, error %d:\"%s\"", l_errno, dap_strerror(l_errno)), l_errno;
    }
    l_ret = mq_send(a_es->mqd, (const char *)&a_arg, sizeof (a_arg), 0);
    l_errno = errno;
    if ( l_ret == EPERM){
        log_it(L_ERROR,"No permissions to send data in mqueue");
    }

    if (l_errno == EINVAL || l_errno == EINTR || l_errno == ETIMEDOUT)
        l_errno = EAGAIN;
    if (l_ret == 0)
        l_ret = sizeof(a_arg);
    else if (l_ret > 0)
        l_ret = -l_ret;
#elif defined (DAP_EVENTS_CAPS_QUEUE_POSIX)
    struct timespec l_timeout;
    clock_gettime(CLOCK_REALTIME, &l_timeout);
    l_timeout.tv_sec+=2; // Not wait more than 1 second to get and 2 to send
    int ret = mq_timedsend(a_es->mqd, (const char *)&a_arg,sizeof (a_arg),0, &l_timeout );
    int l_errno = errno;
    if (ret == sizeof(a_arg) )
        return  0;
    else
        return l_errno;
#elif defined DAP_EVENTS_CAPS_WEPOLL
    //return dap_sendto(a_es->socket, a_es->port, &a_arg, sizeof(void*)) == SOCKET_ERROR ? WSAGetLastError() : NO_ERROR;
    queue_entry_t *l_work_item = DAP_ALMALLOC(MEMORY_ALLOCATION_ALIGNMENT, sizeof(queue_entry_t));
    l_work_item->data = a_arg;
    InterlockedPushEntrySList((PSLIST_HEADER)a_es->_pvt, &(l_work_item->entry));
    return dap_sendto(a_es->socket, a_es->port, &a_arg, sizeof(void*)) == SOCKET_ERROR ? WSAGetLastError() : NO_ERROR;

#elif defined (DAP_EVENTS_CAPS_KQUEUE)
    struct kevent l_event={0};
    dap_events_socket_w_data_t * l_es_w_data = DAP_NEW_Z(dap_events_socket_w_data_t);
    if(!l_es_w_data ) // Out of memory
        return -666;

    l_es_w_data->esocket = a_es;
    l_es_w_data->ptr = a_arg;
    EV_SET(&l_event,a_es->socket+arc4random()  , EVFILT_USER,EV_ADD | EV_ONESHOT, NOTE_FFNOP | NOTE_TRIGGER ,0, l_es_w_data);
    int l_n;
    if(a_es->pipe_out){ // If we have pipe out - we send events directly to the pipe out kqueue fd
        if(a_es->pipe_out->context){
            if( g_debug_reactor) log_it(L_DEBUG, "Sent kevent() with ptr %p to pipe_out worker on esocket %d",a_arg,a_es);
            l_n = kevent(a_es->pipe_out->context->kqueue_fd,&l_event,1,NULL,0,NULL);
        }
        else {
            log_it(L_WARNING,"Trying to send pointer in pipe out queue thats not assigned to any worker or proc thread");
            l_n = 0;
            DAP_DELETE(l_es_w_data);
        }
    }else if(a_es->context){
        l_n = kevent(a_es->context->kqueue_fd,&l_event,1,NULL,0,NULL);
        if( g_debug_reactor) log_it(L_DEBUG, "Sent kevent() with ptr %p to worker on esocket %d",a_arg,a_es);
    }else {
        log_it(L_WARNING,"Trying to send pointer in queue thats not assigned to any worker or proc thread");
        l_n = 0;
        DAP_DELETE(l_es_w_data);
    }

    if(l_n != -1 ){
        return 0;
    } else {
        l_errno = errno;
        log_it(L_ERROR,"Sending kevent error code %d", l_errno);
        return l_errno;
    }

#else
#error "Not implemented dap_events_socket_queue_ptr_send() for this platform"
#endif
    return l_ret == sizeof(a_arg) ? 0 : ( log_it(L_ERROR,"Send queue ptr error %d: \"%s\"", l_errno, dap_strerror(l_errno)), l_errno );
}

#endif

/**
 * @brief dap_events_socket_delete_unsafe
 * @param a_esocket
 * @param a_preserve_inheritor
 */
void dap_events_socket_delete_unsafe(dap_events_socket_t *a_esocket, bool a_preserve_inheritor)
{
    dap_return_if_fail(a_esocket);
#ifndef DAP_EVENTS_CAPS_IOCP
    dap_events_socket_descriptor_close(a_esocket);
#endif
    DAP_DEL_MULTY(a_esocket->_pvt, a_esocket->buf_in, a_esocket->buf_out);
    if (!a_preserve_inheritor)
        DAP_DELETE(a_esocket->_inheritor);
#ifdef   DAP_SYS_DEBUG
    atomic_fetch_add(&s_memstat[MEMSTAT$K_BUF_OUT].free_nr, 1);
    atomic_fetch_add(&s_memstat[MEMSTAT$K_BUF_IN].free_nr, 1);
#endif
    s_dap_evsock_free( a_esocket );
}

/**
 * @brief dap_events_socket_remove_and_delete
 * @param a_worker
 * @param a_es_uuid
 */
void dap_events_socket_remove_and_delete(dap_worker_t *a_worker, dap_events_socket_uuid_t a_es_uuid)
{
    dap_return_if_fail(a_worker);
    if (a_worker == dap_worker_get_current()) {
        dap_events_socket_t *l_es = dap_context_find(a_worker->context, a_es_uuid);
        if (!l_es) {
            log_it(L_WARNING, "UUID " DAP_UINT64_FORMAT_x " doesn't exists in worker %u", a_worker->id);
            return;
        }
        dap_events_socket_remove_and_delete_unsafe(l_es, false);
    }
#ifdef DAP_EVENTS_CAPS_IOCP
    dap_overlapped_t *ol = DAP_NEW_Z(dap_overlapped_t);
    ol->ol.Internal = (ULONG_PTR)a_es_uuid;
    ol->ol.Offset = DAP_SOCK_SIGNAL_CLOSE;
    ol->ol.OffsetHigh = 1;
    ol->op = io_call;
    if ( !PostQueuedCompletionStatus(a_worker->context->iocp, 0, (ULONG_PTR)s_es_set_flag, (OVERLAPPED*)ol) ) {
        log_it(L_ERROR, "Can't schedule deletion of %"DAP_UINT64_FORMAT_U" in context #%d, error %d",
               a_es_uuid, a_worker->context->id, GetLastError());
        dap_overlapped_free(ol);
    }
#else
    dap_events_socket_uuid_t * l_es_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
    if (!l_es_uuid_ptr) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return;
    }
    *l_es_uuid_ptr = a_es_uuid;

    if( dap_events_socket_queue_ptr_send( a_worker->queue_es_delete, l_es_uuid_ptr ) != 0 ){
        log_it(L_ERROR,"Can't send %"DAP_UINT64_FORMAT_U" uuid in queue",a_es_uuid);
        DAP_DELETE(l_es_uuid_ptr);
    }
#endif
}

/**
 * @brief dap_events_socket_set_readable
 * @param a_worker
 * @param a_es_uuid
 * @param a_is_ready
 */
void dap_events_socket_set_readable(dap_worker_t *a_worker, dap_events_socket_uuid_t a_es_uuid, bool a_is_ready)
{
    dap_return_if_fail(a_worker);
    if (a_worker == dap_worker_get_current()) {
        dap_events_socket_t *l_es = dap_context_find(a_worker->context, a_es_uuid);
        if (!l_es) {
            log_it(L_WARNING, "UUID " DAP_UINT64_FORMAT_x " doesn't exists in worker %u", a_worker->id);
            return;
        }
        return dap_events_socket_set_readable_unsafe(l_es, a_is_ready);
    }
#ifdef DAP_EVENTS_CAPS_IOCP
    dap_overlapped_t *ol = DAP_NEW_Z(dap_overlapped_t);
    ol->ol.Internal = (ULONG_PTR)a_es_uuid;
    ol->ol.Offset = DAP_SOCK_READY_TO_READ;
    ol->ol.OffsetHigh = (DWORD)a_is_ready;
    ol->op = io_call;
    if ( !PostQueuedCompletionStatus(a_worker->context->iocp, 0, (ULONG_PTR)s_es_set_flag, (OVERLAPPED*)ol) ) {
        log_it(L_ERROR, "Can't schedule reading from %"DAP_UINT64_FORMAT_U" in context #%d, error %d",
               a_es_uuid, a_worker->context->id, GetLastError());
        dap_overlapped_free(ol);
    }
#else
    dap_worker_msg_io_t *l_msg = DAP_NEW_Z_RET_IF_FAIL(dap_worker_msg_io_t);
    l_msg->esocket_uuid = a_es_uuid;
    if (a_is_ready)
        l_msg->flags_set = DAP_SOCK_READY_TO_READ;
    else
        l_msg->flags_unset = DAP_SOCK_READY_TO_READ;

    int l_ret = dap_events_socket_queue_ptr_send(a_worker->queue_es_io, l_msg);
    if (l_ret) {
        log_it(L_ERROR, "dap_events_socket_queue_ptr_send() error %d", l_ret);
        DAP_DELETE(l_msg);
    }
#endif
}

/**
 * @brief dap_events_socket_set_writable
 * @param a_worker
 * @param a_es_uuid
 * @param a_is_ready
 */
void dap_events_socket_set_writable(dap_worker_t *a_worker, dap_events_socket_uuid_t a_es_uuid, bool a_is_ready)
{
    dap_return_if_fail(a_worker);
    if (a_worker == dap_worker_get_current()) {
        dap_events_socket_t *l_es = dap_context_find(a_worker->context, a_es_uuid);
        if (!l_es) {
            log_it(L_WARNING, "UUID " DAP_UINT64_FORMAT_x " doesn't exists in worker %u", a_worker->id);
            return;
        }
        return dap_events_socket_set_writable_unsafe(l_es, a_is_ready);
    }
#ifdef DAP_EVENTS_CAPS_IOCP
    dap_overlapped_t *ol = DAP_NEW_Z(dap_overlapped_t);
    ol->ol.Internal = (ULONG_PTR)a_es_uuid;
    ol->ol.Offset = DAP_SOCK_READY_TO_WRITE;
    ol->ol.OffsetHigh = (DWORD)a_is_ready;
    ol->op = io_call;
    if ( !PostQueuedCompletionStatus(a_worker->context->iocp, 0, (ULONG_PTR)s_es_set_flag, (OVERLAPPED*)ol) ) {
        log_it(L_ERROR, "Can't schedule writing to %"DAP_UINT64_FORMAT_U" in context #%d, error %d",
               a_es_uuid, a_worker->context->id, GetLastError());
        dap_overlapped_free(ol);
    }
#else
    dap_worker_msg_io_t * l_msg = DAP_NEW_Z(dap_worker_msg_io_t); if (!l_msg) return;
    l_msg->esocket_uuid = a_es_uuid;

    if (a_is_ready)
        l_msg->flags_set = DAP_SOCK_READY_TO_WRITE;
    else
        l_msg->flags_unset = DAP_SOCK_READY_TO_WRITE;

    int l_ret = dap_events_socket_queue_ptr_send(a_worker->queue_es_io, l_msg );
    if (l_ret) {
        log_it(L_ERROR, "set writable mt: wasn't send pointer to queue: code %d", l_ret);
        DAP_DELETE(l_msg);
    }
#endif
}

/**
 * @brief dap_events_socket_write
 * @param a_worker
 * @param a_es_uuid
 * @param a_data
 * @param l_data_size
 * @return
 */
size_t dap_events_socket_write(dap_worker_t *a_worker, dap_events_socket_uuid_t a_es_uuid, const void *a_data, size_t a_data_size)
{
    dap_return_val_if_fail(a_worker, 0);
    if (a_worker == dap_worker_get_current()) {
        dap_events_socket_t *l_es = dap_context_find(a_worker->context, a_es_uuid);
        return l_es
            ? dap_events_socket_write_unsafe(l_es, a_data, a_data_size)
            : ( log_it(L_WARNING, "UUID " DAP_UINT64_FORMAT_x " doesn't exists in worker %u", a_worker->id), 0 );
    }

#ifdef DAP_EVENTS_CAPS_IOCP
    dap_overlapped_t *ol = DAP_NEW_SIZE(dap_overlapped_t, sizeof(dap_overlapped_t) + a_data_size);
    *ol = (dap_overlapped_t) { .op = io_write };
    memcpy(ol->buf, a_data, a_data_size);
    debug_if(g_debug_reactor, L_INFO, "Write %lu bytes to es ["DAP_FORMAT_ESOCKET_UUID": worker %d]", a_data_size, a_es_uuid, a_worker->id);
    return PostQueuedCompletionStatus(a_worker->context->iocp, a_data_size, (ULONG_PTR)a_es_uuid, (OVERLAPPED*)ol)
        ? a_data_size
        : ( DAP_DELETE(ol), log_it(L_ERROR, "Can't schedule writing to %"DAP_UINT64_FORMAT_U" in context #%d, error %d",
                                   a_es_uuid, a_worker->context->id, GetLastError()), 0 );
#else
    dap_worker_msg_io_t *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_worker_msg_io_t, 0);
    l_msg->esocket_uuid = a_es_uuid;
    l_msg->data = DAP_DUP_SIZE((char*)a_data, a_data_size);
    l_msg->data_size = a_data_size;
    l_msg->flags_set = DAP_SOCK_READY_TO_WRITE;

    int l_ret = dap_events_socket_queue_ptr_send(a_worker->queue_es_io, l_msg);
    return l_ret
        ? ( log_it(L_ERROR, "queue_ptr_send() error %d", l_ret), DAP_DEL_MULTY(l_msg->data, l_msg), 0 )
        : a_data_size;
#endif
}

/**
 * @brief dap_events_socket_write_f
 * @param a_es_uuid
 * @param a_format
 * @return
 */
size_t dap_events_socket_write_f(dap_worker_t *a_worker, dap_events_socket_uuid_t a_es_uuid, const char *a_format, ...)
{
    dap_return_val_if_fail(a_worker, 0);
    va_list ap, ap_copy;
    va_start(ap,a_format);
    va_copy(ap_copy, ap);
    int l_data_size = vsnprintf(NULL,0,a_format,ap);
    va_end(ap);
    if (l_data_size <0 ){
        log_it(L_ERROR, "Write f mt: can't write out formatted data '%s' with values", a_format);
        va_end(ap_copy);
        return 0;
    }
    ++l_data_size; // include trailing 0
#ifdef DAP_EVENTS_CAPS_IOCP
    dap_overlapped_t *ol = DAP_NEW_Z_SIZE(dap_overlapped_t, sizeof(dap_overlapped_t) + l_data_size);
    *ol = (dap_overlapped_t) { .op = io_write };
    vsprintf(ol->buf, a_format, ap_copy);
    return PostQueuedCompletionStatus(a_worker->context->iocp, l_data_size, a_es_uuid, (OVERLAPPED*)ol)
        ? l_data_size
        : ( DAP_DELETE(ol), log_it(L_ERROR, "Can't schedule writing to %"DAP_UINT64_FORMAT_U" in context #%d, error %d",
               a_es_uuid, a_worker->context->id, GetLastError()), 0 );
#else
    dap_worker_msg_io_t * l_msg = DAP_NEW_Z(dap_worker_msg_io_t);
    if (!l_msg) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        va_end(ap_copy);
        return 0;
    }
    l_msg->esocket_uuid = a_es_uuid;
    l_msg->data_size = l_data_size;
    l_msg->data = DAP_NEW_SIZE(void, l_data_size);
    if (!l_msg->data) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        va_end(ap_copy);
        DAP_DEL_Z(l_msg);
        return 0;
    }
    l_msg->flags_set = DAP_SOCK_READY_TO_WRITE;
    l_data_size = vsprintf(l_msg->data,a_format,ap_copy);
    va_end(ap_copy);

    if (a_worker == dap_worker_get_current()) {
        dap_events_socket_t *l_es = dap_context_find(a_worker->context, a_es_uuid);
        if (!l_es) {
            log_it(L_WARNING, "UUID " DAP_UINT64_FORMAT_x " doesn't exists in worker %u", a_worker->id);
            return 0;
        }
        size_t ret = dap_events_socket_write_unsafe(l_es, l_msg->data, l_msg->data_size);
        return DAP_DEL_MULTY(l_msg->data, l_msg), ret;
    }
    int l_ret = dap_events_socket_queue_ptr_send(a_worker->queue_es_io, l_msg);
    return l_ret
        ? log_it(L_ERROR, "dap_events_socket_queue_ptr_send() error %d", l_ret), DAP_DEL_MULTY(l_msg->data, l_msg), 0
        : l_data_size;
#endif
}

/**
 * @brief dap_events_socket_write Write data to the client
 * @param a_es Esocket instance
 * @param a_data Pointer to data
 * @param a_data_size Size of data to write
 * @return Number of bytes that were placed into the buffer
 */
size_t dap_events_socket_write_unsafe(dap_events_socket_t *a_es, const void *a_data, size_t a_data_size)
{
    if (!a_es) {
        log_it(L_ERROR, "Attempt to write into NULL esocket!");
        return 0;
    }
    if (a_es->flags & DAP_SOCK_SIGNAL_CLOSE) {
        debug_if(g_debug_reactor, L_NOTICE, "Trying to write into closing socket %"DAP_FORMAT_SOCKET, a_es->fd);
        return 0;
    }
    if (g_debug_reactor && a_es->context && dap_worker_get_current() != DAP_WORKER(a_es->context)) {
        log_it(L_ERROR, "Trying to write to foreign context %p", a_es->context);
        return -1;
    }
#ifdef DAP_EVENTS_CAPS_IOCP
    if (a_es->type == DESCRIPTOR_TYPE_QUEUE)
        return dap_events_socket_queue_data_send(a_es, a_data, a_data_size);
#endif
    static const size_t l_basic_buf_size = DAP_EVENTS_SOCKET_BUF_LIMIT / 4;
    byte_t *l_buf_out;
    if (a_es->buf_out_size_max < a_es->buf_out_size + a_data_size) {
        a_es->buf_out_size_max += dap_max(l_basic_buf_size, a_data_size);
        if (!( l_buf_out = DAP_REALLOC(a_es->buf_out, a_es->buf_out_size_max) ))
            return log_it(L_ERROR, "Can't increase capacity: OOM!"), 0;
        a_es->buf_out = l_buf_out;
        debug_if(g_debug_reactor, L_MSG, "[!] Socket %"DAP_FORMAT_SOCKET": increase capacity to %zu, actual size: %zu", a_es->fd, a_es->buf_out_size_max, a_es->buf_out_size);
    } else if ((a_es->buf_out_size + a_data_size <= l_basic_buf_size / 4) && (a_es->buf_out_size_max > l_basic_buf_size)) {
        a_es->buf_out_size_max = l_basic_buf_size;
        a_es->buf_out = DAP_REALLOC(a_es->buf_out, a_es->buf_out_size_max);
        debug_if(g_debug_reactor, L_MSG, "[!] Socket %"DAP_FORMAT_SOCKET": decrease capacity to %zu, actual size: %zu",
               a_es->fd, a_es->buf_out_size_max, a_es->buf_out_size);
    }
    memcpy(a_es->buf_out + a_es->buf_out_size, a_data, a_data_size);
    a_es->buf_out_size += a_data_size;
    debug_if(g_debug_reactor, L_DEBUG, "Write %zu bytes to \"%s\" "DAP_FORMAT_ESOCKET_UUID", total size: %zu",
             a_data_size, dap_events_socket_get_type_str(a_es), a_es->uuid, a_es->buf_out_size);
    dap_events_socket_set_writable_unsafe(a_es, true);
    return a_data_size;
}

/**
 * @brief dap_events_socket_write_f Write formatted text to the client
 * @param a_es Conn instance
 * @param a_format Format
 * @return Number of bytes that were placed into the buffer
 */
ssize_t dap_events_socket_write_f_unsafe(dap_events_socket_t *a_es, const char *a_format, ...)
{
    if(!a_es->buf_out){
        log_it(L_ERROR,"Can't write formatted data to NULL buffer output");
        return 0;
    }

    va_list ap, ap_copy;
    va_start(ap, a_format);
    va_copy(ap_copy, ap);
    ssize_t l_ret = vsnprintf(NULL, 0, a_format, ap);
    va_end(ap);
    if (l_ret < 0) {
        va_end(ap_copy);
        log_it(L_ERROR,"Can't write out formatted data '%s'", a_format);
        return l_ret;
    }
    size_t l_buf_size = l_ret + 1;
    char *l_buf = DAP_NEW_Z_SIZE(char, l_buf_size);
    if (!l_buf) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        va_end(ap_copy);
        return 0;
    }
    vsprintf(l_buf, a_format, ap_copy);
    va_end(ap_copy);
    l_ret = dap_events_socket_write_unsafe(a_es, l_buf, l_buf_size - 1);
    DAP_DELETE(l_buf);
    return l_ret;
}

/**
 * @brief dap_events_socket_pop_from_buf_in
 * @param a_essc
 * @param a_data
 * @param a_data_size
 * @return
 */
size_t dap_events_socket_pop_from_buf_in(dap_events_socket_t *a_es, void *a_data, size_t a_data_size)
{
    if ( a_data_size < a_es->buf_in_size)
    {
        memcpy(a_data, a_es->buf_in, a_data_size);
        memmove(a_es->buf_in, a_es->buf_in + a_data_size, a_es->buf_in_size - a_data_size);
    } else {
        if ( a_data_size > a_es->buf_in_size )
            a_data_size = a_es->buf_in_size;
        memcpy(a_data, a_es->buf_in, a_data_size);
    }
    a_es->buf_in_size -= a_data_size;
    return a_data_size;
}


/**
 * @brief dap_events_socket_shrink_client_buf_in Shrink input buffer (shift it left)
 * @param cl Client instance
 * @param shrink_size Size on wich we shrink the buffer with shifting it left
 */
void dap_events_socket_shrink_buf_in(dap_events_socket_t * a_es, size_t shrink_size)
{
    if ( (!shrink_size) || (!a_es->buf_in_size) )
        return;                                                             /* Nothing to do - OK */

    if (a_es->buf_in_size > shrink_size)
        memmove(a_es->buf_in , a_es->buf_in + shrink_size, a_es->buf_in_size -= shrink_size);
    else {
        //log_it(WARNING,"Shrinking size of input buffer on amount bigger than actual buffer's size");
        a_es->buf_in_size = 0;
    }
}


/*
 *  DESCRIPTION: Insert specified data data block at beging of the <buf_out> area.
 *      If there is not a room for inserting - no <buf_out> is changed.
 *
 *  INPUTS:
 *      cl:         A events socket context area
 *      data:       A buffer with data to be inserted
 *      data_sz:    A size of the data in the buffer
 *
 *  IMPLICITE OUTPUTS:
 *      a_es->buf_out
 *      a_es->buf_out_sz
 *
 *  RETURNS:
 *      0:          SUCCESS
 *      -ENOMEM:    No room for data to be inserted
 */
size_t dap_events_socket_insert_buf_out(dap_events_socket_t * a_es, void *a_data, size_t a_data_size)
{
    if ( (!a_data_size) || (!a_data) )
        return  0;                                                          /* Nothing to do - OK */

    if ( (a_es->buf_out_size_max - a_es->buf_out_size) < a_data_size )
        return  -ENOMEM;                                                    /* No room for data to be inserted */

    memmove(a_es->buf_out + a_data_size, a_es->buf_out, a_es->buf_out_size); /* Move existing data to right */
    memcpy(a_es->buf_out, a_data, a_data_size);                             /* Place new data at begin of the buffer */
    a_es->buf_out_size += a_data_size;                                       /* Ajust buffer's data lenght */

    return  a_data_size;
}
