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
#include "dap_context_queue.h"
#include "dap_uuid.h"
#include "dap_events.h"
#include "dap_io_flow_socket.h"
#include "dap_timerfd.h"
#include "dap_context.h"
#include "dap_events_socket.h"
#include "dap_net.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_events_socket"

static bool s_debug_more = false;

// =============================================================================
// DATAGRAM PACKET QUEUE (for non-blocking sendto on UDP, SCTP, etc.)
// =============================================================================

#define DAP_PACKET_QUEUE_INITIAL_CAPACITY 16
#define DAP_PACKET_QUEUE_MAX_CAPACITY 4096  // Limit to prevent memory exhaustion

// Maximum UDP datagram size (theoretical max UDP payload)
#ifndef DAP_UDP_MAX_DATAGRAM_SIZE
#define DAP_UDP_MAX_DATAGRAM_SIZE 65507  // 65535 - 8 (UDP header) - 20 (IP header)
#endif
/**
 * @brief Create datagram packet queue
 */
static dap_events_socket_packet_queue_t* s_packet_queue_create(void)
{
    dap_events_socket_packet_queue_t *l_queue = DAP_NEW_Z(dap_events_socket_packet_queue_t);
    if (!l_queue) {
        return NULL;
    }
    
    l_queue->packets = DAP_NEW_Z_COUNT(dap_events_socket_packet_t, DAP_PACKET_QUEUE_INITIAL_CAPACITY);
    if (!l_queue->packets) {
        DAP_DELETE(l_queue);
        return NULL;
    }
    
    l_queue->capacity = DAP_PACKET_QUEUE_INITIAL_CAPACITY;
    l_queue->count = 0;
    l_queue->head = 0;
    
    return l_queue;
}

/**
 * @brief Delete datagram packet queue and free all packets
 */
static void s_packet_queue_delete(dap_events_socket_packet_queue_t *a_queue)
{
    if (!a_queue) {
        return;
    }
    
    // Free all packet data
    for (size_t i = 0; i < a_queue->count; i++) {
        size_t idx = (a_queue->head + i) % a_queue->capacity;
        DAP_DELETE(a_queue->packets[idx].data);
    }
    
    DAP_DELETE(a_queue->packets);
    DAP_DELETE(a_queue);
}

/**
 * @brief Enqueue datagram packet
 * @return 0 on success, -1 on error
 */
static int s_packet_queue_push(dap_events_socket_packet_queue_t *a_queue,
                                const uint8_t *a_data, size_t a_size,
                                const struct sockaddr_storage *a_addr, socklen_t a_addr_len)
{
    if (!a_queue || !a_data || a_size == 0 || !a_addr) {
        return -1;
    }
    
    // Check capacity limit
    if (a_queue->count >= DAP_PACKET_QUEUE_MAX_CAPACITY) {
        log_it(L_WARNING, "Packet queue full (%zu packets), dropping packet", a_queue->count);
        return -1;
    }
    
    // Grow array if needed (double capacity)
    if (a_queue->count >= a_queue->capacity) {
        size_t l_new_capacity = a_queue->capacity * 2;
        if (l_new_capacity > DAP_PACKET_QUEUE_MAX_CAPACITY) {
            l_new_capacity = DAP_PACKET_QUEUE_MAX_CAPACITY;
        }
        
        dap_events_socket_packet_t *l_new_packets = 
            DAP_NEW_Z_COUNT(dap_events_socket_packet_t, l_new_capacity);
        if (!l_new_packets) {
            log_it(L_ERROR, "Failed to grow packet queue");
            return -1;
        }
        
        // Copy existing packets (preserving ring buffer order)
        for (size_t i = 0; i < a_queue->count; i++) {
            size_t old_idx = (a_queue->head + i) % a_queue->capacity;
            l_new_packets[i] = a_queue->packets[old_idx];
        }
        
        DAP_DELETE(a_queue->packets);
        a_queue->packets = l_new_packets;
        a_queue->capacity = l_new_capacity;
        a_queue->head = 0;  // Reset head after reallocation
    }
    
    // Add packet to tail
    size_t tail_idx = (a_queue->head + a_queue->count) % a_queue->capacity;
    
    a_queue->packets[tail_idx].data = DAP_NEW_SIZE(uint8_t, a_size);
    if (!a_queue->packets[tail_idx].data) {
        log_it(L_ERROR, "Failed to allocate packet data");
        return -1;
    }
    
    memcpy(a_queue->packets[tail_idx].data, a_data, a_size);
    a_queue->packets[tail_idx].size = a_size;
    memcpy(&a_queue->packets[tail_idx].addr, a_addr, sizeof(struct sockaddr_storage));
    a_queue->packets[tail_idx].addr_len = a_addr_len;
    
    a_queue->count++;
    
    debug_if(g_debug_reactor, L_DEBUG, 
             "Packet queue: enqueued packet %zu bytes (queue size now: %zu)",
             a_size, a_queue->count);
    
    return 0;
}

/**
 * @brief Dequeue and send first datagram packet
 * @return Number of bytes sent, 0 if queue empty, -1 on error, -2 if would block
 * 
 * NOTE: This function is NOT static because it's called from dap_context.c
 */
ssize_t s_packet_queue_pop_and_send(dap_events_socket_packet_queue_t *a_queue, int a_fd)
{
    if (!a_queue || a_queue->count == 0) {
        return 0;  // Empty queue
    }
    
    dap_events_socket_packet_t *l_pkt = &a_queue->packets[a_queue->head];
    
    ssize_t l_sent = sendto(a_fd, l_pkt->data, l_pkt->size, 0,
                            (struct sockaddr*)&l_pkt->addr, l_pkt->addr_len);
    
    if (l_sent < 0) {
        int l_errno = errno;
        if (l_errno == EAGAIN || l_errno == EWOULDBLOCK) {
            return -2;  // Would block, keep packet in queue
        }
        log_it(L_ERROR, "Datagram sendto failed: %s", strerror(l_errno));
        // Drop this packet and continue
        DAP_DELETE(l_pkt->data);
        a_queue->head = (a_queue->head + 1) % a_queue->capacity;
        a_queue->count--;
        return -1;
    }
    
    // Successfully sent, remove from queue
    debug_if(g_debug_reactor, L_DEBUG,
             "Packet queue: sent packet %zd bytes (queue size now: %zu)",
             l_sent, a_queue->count - 1);
    
    DAP_DELETE(l_pkt->data);
    a_queue->head = (a_queue->head + 1) % a_queue->capacity;
    a_queue->count--;
    
    return l_sent;
}

// =============================================================================
// END DATAGRAM PACKET QUEUE
// =============================================================================

const char *s_socket_type_to_str[DESCRIPTOR_TYPE_MAX] = { 
    "CLIENT", "LOCAL CLIENT", "SERVER", "LOCAL SERVER", "UDP CLIENT", "SSL CLIENT", "RAW", 
    "FILE", "PIPE", "QUEUE", "TIMER", "EVENT"
};

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
    dap_events_socket_t *a_es = (dap_events_socket_t*)a_ol->Pointer;
    dap_worker_t *l_new_worker = (dap_worker_t*)a_ol->Internal;
    if ( a_es->was_reassigned && a_es->flags & DAP_SOCK_REASSIGN_ONCE )
        log_it(L_INFO, "Multiple worker switches for %p are forbidden", a_es);
    else
        dap_events_socket_reassign_between_workers_unsafe(a_es, l_new_worker);
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
        log_it(L_ERROR, "Can't open /proc/sys/fs/mqueue/msg_max file for writing, errno=%d", errno);
    }
#endif
    dap_timerfd_init();
    dap_io_flow_socket_init();
    return 0;
}

/**
 * @brief dap_events_socket_deinit Deinit clients module
 */
void dap_events_socket_deinit(void)
{
    dap_io_flow_socket_deinit();
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
void dap_events_socket_assign_on_worker_mt(dap_events_socket_t * a_es, struct dap_worker * a_worker)
{
    a_es->last_ping_request = time(NULL);
   // debug_if(s_debug_more, L_DEBUG, "Assigned %p on worker %u", a_es, a_worker->id);
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
    debug_if(s_debug_more, L_DEBUG, "Reassign between %u->%u workers: %p (%d)  ", l_worker->id, a_worker_new->id, a_es, a_es->fd );

    dap_context_remove(a_es);
    a_es->was_reassigned = true;
    if (a_es->callbacks.worker_unassign_callback)
        a_es->callbacks.worker_unassign_callback(a_es, l_worker);

    dap_worker_add_events_socket(a_worker_new, a_es);
}

/**
 * @brief dap_events_socket_reassign_between_workers_mt
 * @param a_worker_old
 * @param a_es
 * @param a_worker_new
 */
void dap_events_socket_reassign_between_workers_mt(dap_worker_t * a_worker_old, dap_events_socket_t * a_es, dap_worker_t * a_worker_new)
{
#ifdef DAP_EVENTS_CAPS_IOCP
    dap_overlapped_t *ol = DAP_NEW_Z(dap_overlapped_t);
    ol->ol.Internal = (ULONG_PTR)a_worker_new;
    ol->ol.Pointer = a_es;
    ol->op = io_call;
    if ( !PostQueuedCompletionStatus(a_worker_old->context->iocp, 0, (ULONG_PTR)s_es_reassign, (OVERLAPPED*)ol) ) {
        log_it(L_ERROR, "Can't reassign es %p, error %d", a_es, GetLastError());
        dap_overlapped_free(ol);
    }
    return;
#else
    if(a_es == NULL || a_worker_new == NULL || a_worker_old == NULL){
        log_it(L_ERROR, "Argument is not initialized, can't call dap_events_socket_reassign_between_workers_mt");
        return;
    }
    dap_worker_msg_reassign_t * l_msg = DAP_NEW_Z(dap_worker_msg_reassign_t);
    if (!l_msg) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return;
    }
    l_msg->esocket = a_es;
    l_msg->esocket_uuid = a_es->uuid;
    l_msg->worker_new = a_worker_new;
    if( !dap_context_queue_push(a_worker_old->queue_es_reassign, l_msg) ){
#ifdef DAP_OS_WINDOWS
        log_it(L_ERROR,"Haven't sent reassign message with esocket %"DAP_UINT64_FORMAT_U, a_es ? a_es->socket : (SOCKET)-1);
#else
        log_it(L_ERROR,"Haven't sent reassign message with esocket %d", a_es?a_es->socket:-1);
#endif
        DAP_DELETE(l_msg);
    }
#endif
}



/**
 * @brief dap_events_socket_create_type_pipe_mt
 * @param a_w
 * @param a_callback
 * @param a_flags
 * @return
 */
dap_events_socket_t * dap_events_socket_create_type_pipe_mt(dap_worker_t * a_w, dap_events_socket_callback_t a_callback, uint32_t a_flags)
{
    dap_events_socket_t * l_es = dap_context_create_pipe(NULL, a_callback, a_flags);
    dap_worker_add_events_socket_unsafe(a_w, l_es);
    return  l_es;
}

/**
 * @brief Create platform-independent socket with specified parameters
 * 
 * Creates a socket with the specified domain, type, and protocol,
 * sets it to non-blocking mode, and wraps it in dap_events_socket_t.
 * This function centralizes all platform-dependent socket creation logic.
 * 
 * @param a_domain Socket domain (AF_INET, AF_INET6, etc.)
 * @param a_type Socket type (SOCK_STREAM, SOCK_DGRAM, etc.)
 * @param a_protocol Protocol (IPPROTO_TCP, IPPROTO_UDP, etc.)
 * @param a_callbacks Socket callbacks structure
 * @return Created dap_events_socket_t or NULL on error
 */
dap_events_socket_t *dap_events_socket_create_platform(int a_domain, int a_type, int a_protocol,
                                                         dap_events_socket_callbacks_t *a_callbacks)
{
    if (!a_callbacks) {
        log_it(L_ERROR, "Callbacks are NULL");
        return NULL;
    }

#ifdef DAP_OS_WINDOWS
    SOCKET l_sock = socket(a_domain, a_type, a_protocol);
    if (l_sock == INVALID_SOCKET) {
        int l_err = WSAGetLastError();
        log_it(L_ERROR, "Socket create error %d", l_err);
        return NULL;
    }
    
    // Set socket non-blocking
    u_long l_socket_flags = 1;
    if (ioctlsocket(l_sock, (long)FIONBIO, &l_socket_flags) == SOCKET_ERROR) {
        log_it(L_ERROR, "Can't set socket %zu to nonblocking mode, error %d", l_sock, WSAGetLastError());
        closesocket(l_sock);
        return NULL;
    }
#else
    int l_sock = socket(a_domain, a_type, a_protocol);
    if (l_sock == INVALID_SOCKET) {
        int l_err = errno;
        log_it(L_ERROR, "Error %d with socket create", l_err);
        return NULL;
    }
    
    // Set socket non-blocking
    int l_socket_flags = fcntl(l_sock, F_GETFL);
    if (l_socket_flags == -1) {
        log_it(L_ERROR, "Error %d can't get socket flags", errno);
        close(l_sock);
        return NULL;
    }
    if (fcntl(l_sock, F_SETFL, l_socket_flags | O_NONBLOCK) == -1) {
        log_it(L_ERROR, "Error %d can't set socket flags", errno);
        close(l_sock);
        return NULL;
    }
#endif

    // Wrap socket
    dap_events_socket_t *l_es = dap_events_socket_wrap_no_add(l_sock, a_callbacks);
    if (!l_es) {
        log_it(L_ERROR, "Failed to wrap socket");
#ifdef DAP_OS_WINDOWS
        closesocket(l_sock);
#else
        close(l_sock);
#endif
        return NULL;
    }

    return l_es;
}

/**
 * @brief Resolve hostname and set address in events socket
 * 
 * Centralized function for resolving hostname/IP and setting address information
 * in dap_events_socket_t structure.
 * 
 * @param a_es Events socket to set address in
 * @param a_host Hostname or IP address
 * @param a_port Port number
 * @return 0 on success, negative error code on failure
 */
int dap_events_socket_resolve_and_set_addr(dap_events_socket_t *a_es, const char *a_host, uint16_t a_port)
{
    if (!a_es || !a_host) {
        log_it(L_ERROR, "Invalid arguments for resolve_and_set_addr");
        return -1;
    }

    // Resolve host
    int l_addrlen = dap_net_resolve_host(a_host, dap_itoa(a_port), false, &a_es->addr_storage, NULL);
    if (l_addrlen < 0) {
        log_it(L_ERROR, "Wrong remote address '%s : %u'", a_host, a_port);
        return -1;
    }
    
    // Set the address size (crucial for connect/sendto calls)
    a_es->addr_size = (socklen_t)l_addrlen;
    
    a_es->remote_port = a_port;
    dap_strncpy(a_es->remote_addr_str, a_host, DAP_HOSTADDR_STRLEN);
    
    return 0;
}

/**
 * @brief Initiate non-blocking socket connection
 * 
 * Initiates a non-blocking connection for the socket. The socket must have
 * address information set (via dap_events_socket_resolve_and_set_addr or manually).
 * 
 * @param a_es Events socket to connect
 * @param a_error_code Output parameter for error code (0 on success, errno/WSAGetLastError on error)
 * @return 0 on success (including EINPROGRESS/WSAEWOULDBLOCK), -1 on immediate failure
 */
int dap_events_socket_connect(dap_events_socket_t *a_es, int *a_error_code)
{
    if (!a_es) {
        if (a_error_code) *a_error_code = EINVAL;
        return -1;
    }
    
    if (a_es->socket == INVALID_SOCKET || a_es->socket == -1) {
        if (a_error_code) *a_error_code = EBADF;
        log_it(L_ERROR, "Invalid socket in dap_events_socket_connect");
        return -1;
    }
    
    // Initiate non-blocking connection
    int l_err = connect(a_es->socket, (struct sockaddr *) &a_es->addr_storage, sizeof(struct sockaddr_in));
    if (l_err == 0) {
        // Connected immediately - this is rare but possible
        if (a_error_code) *a_error_code = 0;
        debug_if(s_debug_more, L_DEBUG, "Connected immediately to %s:%u!", a_es->remote_addr_str, a_es->remote_port);
        return 0;
    }
    
    // Check if error is expected (EINPROGRESS/WSAEWOULDBLOCK)
    int l_connect_errno;
#ifdef DAP_OS_WINDOWS
    l_connect_errno = WSAGetLastError();
    if (l_connect_errno != WSAEWOULDBLOCK) {
#else
    l_connect_errno = errno;
    if (l_connect_errno != EINPROGRESS) {
#endif
        // Real connection error - fail immediately
        if (a_error_code) *a_error_code = l_connect_errno;
        log_it(L_ERROR, "Remote address can't connect (%s:%hu) with sock_id %"DAP_FORMAT_SOCKET": \"%s\" (code %d)",
               a_es->remote_addr_str, a_es->remote_port, a_es->socket, dap_strerror(l_connect_errno), l_connect_errno);
        return -1;
    }
    
    // EINPROGRESS/WSAEWOULDBLOCK is expected - connection will complete asynchronously
    if (a_error_code) *a_error_code = 0;
    return 0;
}

/**
 * @brief dap_events_socket_create
 * @param a_type
 * @param a_callbacks
 * @return
 */
dap_events_socket_t * dap_events_socket_create(dap_events_desc_type_t a_type, dap_events_socket_callbacks_t* a_callbacks)
{
    int l_type = SOCK_STREAM, l_fam = AF_INET, l_prot = IPPROTO_IP;

    switch(a_type) {
    case DESCRIPTOR_TYPE_SOCKET_CLIENT:
        // Use platform-independent function for TCP client socket
        return dap_events_socket_create_platform(AF_INET, SOCK_STREAM, 0, a_callbacks);
    case DESCRIPTOR_TYPE_SOCKET_UDP:
        // Use platform-independent function for UDP socket
        return dap_events_socket_create_platform(AF_INET, SOCK_DGRAM, IPPROTO_UDP, a_callbacks);
    case DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT:
#ifdef DAP_OS_UNIX
        l_fam = AF_LOCAL;
#elif defined DAP_OS_WINDOWS
        l_fam = AF_INET;  // Windows doesn't support AF_LOCAL, use AF_INET as fallback
#endif
        // Use platform-independent function for local socket
        {
            dap_events_socket_t *l_es = dap_events_socket_create_platform(l_fam, SOCK_STREAM, 0, a_callbacks);
            if (l_es) {
                l_es->type = a_type;
            }
            return l_es;
        }
#ifdef DAP_OS_LINUX
    case DESCRIPTOR_TYPE_SOCKET_RAW:
        // RAW sockets require special handling - use platform-specific code
        l_type = SOCK_RAW;
        // Fall through to platform-specific code below
        break;
#endif
    default:
        log_it(L_CRITICAL,"Can't create socket type %d", a_type );
        return NULL;
    }

    // Special case handling for RAW sockets (Linux only) - use platform-specific code
    // For other types, we should have returned above
#ifdef DAP_OS_LINUX
    if (a_type == DESCRIPTOR_TYPE_SOCKET_RAW) {
        dap_events_socket_t *l_es = dap_events_socket_create_platform(l_fam, l_type, l_prot, a_callbacks);
        if (l_es) {
            l_es->type = a_type;
        }
        return l_es;
    }
#endif

    // Should not reach here for standard socket types
    log_it(L_ERROR, "Unhandled socket type %d", a_type);
    return NULL;
}

/**
 * @brief dap_events_socket_create_type_pipe_unsafe
 * @param a_w
 * @param a_callback
 * @param a_flags
 * @return
 */
dap_events_socket_t * dap_events_socket_create_type_pipe_unsafe(dap_worker_t * a_w, dap_events_socket_callback_t a_callback, uint32_t a_flags)
{
    dap_events_socket_t * l_es = dap_context_create_pipe(NULL, a_callback, a_flags);
    dap_worker_add_events_socket_unsafe(a_w, l_es);
    return  l_es;
}

/**
 * @brief Create write end esocket for existing pipe (cross-platform)
 * 
 * Creates an esocket wrapper around the write end (fd2) of an existing pipe.
 * This function handles platform-specific differences:
 * - POSIX: wraps fd2 from pipe
 * - Windows: returns NULL (pipes not supported via this mechanism)
 * 
 * @param a_worker Worker to attach write end to
 * @param a_pipe_read_es Existing pipe esocket (with read end)
 * @param a_callbacks Callbacks for write end
 * @return Created write end esocket or NULL on error
 */
dap_events_socket_t * dap_events_socket_create_type_pipe_write_end_unsafe(dap_worker_t * a_worker, 
                                                                          dap_events_socket_t * a_pipe_read_es,
                                                                          dap_events_socket_callbacks_t * a_callbacks)
{
    if (!a_worker || !a_pipe_read_es || !a_callbacks) {
        log_it(L_ERROR, "Invalid arguments for pipe write end creation");
        return NULL;
    }
    
#ifdef DAP_OS_WINDOWS
    // On Windows, pipes are not supported via this mechanism
    log_it(L_ERROR, "Pipe write end creation not supported on Windows");
    return NULL;
#else
    // On POSIX, wrap fd2 (write end)
    if (a_pipe_read_es->fd2 < 0) {
        log_it(L_ERROR, "Invalid pipe write fd (fd2=%d)", a_pipe_read_es->fd2);
        return NULL;
    }
    
    dap_events_socket_t *l_write_es = dap_events_socket_wrap_no_add(a_pipe_read_es->fd2, a_callbacks);
    if (!l_write_es) {
        log_it(L_ERROR, "Failed to wrap pipe write end");
        return NULL;
    }
    
    l_write_es->type = DESCRIPTOR_TYPE_PIPE;
    l_write_es->flags = DAP_SOCK_READY_TO_WRITE;
    
    if (dap_worker_add_events_socket_unsafe(a_worker, l_write_es) != 0) {
        log_it(L_ERROR, "Failed to add pipe write esocket to worker");
        dap_events_socket_delete_unsafe(l_write_es, false);
        return NULL;
    }
    
    return l_write_es;
#endif
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
 * @brief dap_events_socket_create_type_event_mt
 * @param a_w
 * @param a_callback
 * @return
 */
dap_events_socket_t * dap_events_socket_create_type_event_mt(dap_worker_t * a_w, dap_events_socket_callback_event_t a_callback)
{
    dap_events_socket_t * l_es = dap_context_create_event(NULL, a_callback);
    // If no worker - don't assign
    if ( a_w)
        dap_events_socket_assign_on_worker_mt(l_es,a_w);
    return  l_es;
}

/**
 * @brief dap_events_socket_create_type_event_unsafe
 * @param a_w
 * @param a_callback
 * @return
 */
dap_events_socket_t * dap_events_socket_create_type_event_unsafe(dap_worker_t * a_w, dap_events_socket_callback_event_t a_callback)
{

    dap_events_socket_t * l_es = dap_context_create_event(NULL, a_callback);
    // If no worker - don't assign
    if (a_w)
        dap_worker_add_events_socket_unsafe(a_w, l_es);
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
            //debug_if(s_debug_more, L_DEBUG, "socket %d timed out", a_sockfd)
            return -2;
        }
        if (l_res == -1) {
            if (errno == EINTR)
                continue;
            debug_if(s_debug_more, L_DEBUG, "socket %"DAP_FORMAT_SOCKET" waiting errno=%d", a_sockfd, errno);
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
        if (a_es->buf_out_size_max > SIZE_MAX - l_basic_buf_size) {
            log_it(L_ERROR, "Integer overflow in buffer size calculation (queue)");
            pthread_rwlock_unlock(&a_es->buf_out_lock);
            return;
        }
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
        if (ret != 0) {
            log_it(L_WARNING, "eventfd_write(fd=%d, val=%"PRIu64") FAILED: ret=%d errno=%d (%s)",
                   a_es->fd2, a_value, ret, l_errno, dap_strerror(l_errno));
        }
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

    if(a_es->context)
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
    l_es->buf_in_size_max = DAP_EVENTS_SOCKET_BUF_SIZE;
    l_es->buf_in = DAP_NEW_Z_SIZE(byte_t, l_es->buf_in_size_max);
    l_es->buf_out_size_max = DAP_EVENTS_SOCKET_BUF_SIZE;
    l_es->buf_out = DAP_NEW_Z_SIZE(byte_t, l_es->buf_out_size_max);
    return l_es;
}

/**
 * @brief Close socket descriptor (platform-independent)
 * 
 * Closes a socket descriptor in a platform-independent way.
 * 
 * @param a_socket Socket descriptor to close
 * @return 0 on success, -1 on error
 */
int dap_events_socket_close_descriptor(SOCKET a_socket)
{
    if (a_socket == INVALID_SOCKET || a_socket == -1) {
        return -1;
    }
#ifdef DAP_OS_WINDOWS
    return closesocket(a_socket);
#else
    return close(a_socket);
#endif
}

/**
 * @brief Close socket descriptors in events socket and reset them to INVALID_SOCKET
 * 
 * Closes both socket and socket2 descriptors in the events socket structure
 * and sets them to INVALID_SOCKET.
 * 
 * @param a_esocket Events socket to close descriptors for
 */
void dap_events_socket_close(dap_events_socket_t *a_esocket)
{
    if (!a_esocket) {
        return;
    }
    
    if (a_esocket->socket > 0
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
        dap_events_socket_close_descriptor(a_esocket->socket);
    }
    if (a_esocket->fd2 > 0) {
        dap_events_socket_close_descriptor(a_esocket->fd2);
    }
    a_esocket->socket = a_esocket->socket2 = INVALID_SOCKET;
}

/**
 * @brief dap_events_socket_get_local_addr - Get local address of socket
 * @param a_es Event socket
 * @param a_addr Output address structure
 * @param a_addr_len Input/output address length
 * @return 0 on success, -1 on error
 */
int dap_events_socket_get_local_addr(dap_events_socket_t *a_es, struct sockaddr_storage *a_addr, socklen_t *a_addr_len)
{
    if (!a_es || !a_addr || !a_addr_len || a_es->socket == INVALID_SOCKET) {
        return -1;
    }
    return getsockname(a_es->socket, (struct sockaddr *)a_addr, a_addr_len);
}

/**
 * @brief dap_events_socket_get_local_port - Get local port of socket
 * @param a_es Event socket
 * @return Port number in host byte order, 0 on error
 */
uint16_t dap_events_socket_get_local_port(dap_events_socket_t *a_es)
{
    if (!a_es || a_es->socket == INVALID_SOCKET) {
        return 0;
    }
    struct sockaddr_storage l_addr;
    socklen_t l_len = sizeof(l_addr);
    if (getsockname(a_es->socket, (struct sockaddr *)&l_addr, &l_len) != 0) {
        return 0;
    }
    if (l_addr.ss_family == AF_INET) {
        return ntohs(((struct sockaddr_in *)&l_addr)->sin_port);
    } else if (l_addr.ss_family == AF_INET6) {
        return ntohs(((struct sockaddr_in6 *)&l_addr)->sin6_port);
    }
    return 0;
}

/**
 * @brief dap_events_socket_remove_and_delete_unsafe
 * @param a_es
 * @param a_preserve_inheritor
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
            l_res = ERROR_IO_PENDING;
            func = "closesocket";
            //l_res = CancelIoEx((HANDLE)a_es->socket, NULL) ? ERROR_IO_PENDING : GetLastError();
            //func = "CancelIoEx";
        }
        dap_events_socket_close(a_es);
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
        dap_events_socket_close(a_es);
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
        WSABUF wsabuf = { .buf = (char*)a_es->buf_in + a_es->buf_in_size, .len = a_es->buf_in_size_max - a_es->buf_in_size };

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
            a_ol ? a_es->flags = DAP_SOCK_SIGNAL_CLOSE : dap_events_socket_remove_and_delete_mt(a_es->worker, a_es->uuid);
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
            a_ol ? a_es->flags = DAP_SOCK_SIGNAL_CLOSE : dap_events_socket_remove_and_delete_mt(a_es->worker, a_es->uuid);
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
void dap_events_socket_set_writable_unsafe( dap_events_socket_t *a_esocket, bool a_is_ready )
{
    if (!a_esocket || a_is_ready == (bool)(a_esocket->flags & DAP_SOCK_READY_TO_WRITE))
        return;

    if ( a_is_ready )
        a_esocket->flags |= DAP_SOCK_READY_TO_WRITE;
    else
        a_esocket->flags &= ~DAP_SOCK_READY_TO_WRITE;

#ifdef DAP_EVENTS_CAPS_EVENT_KEVENT
    if( a_esocket->type != DESCRIPTOR_TYPE_EVENT &&
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


#endif

/**
 * @brief dap_events_socket_delete_unsafe
 * @param a_esocket
 * @param a_preserve_inheritor
 */
void dap_events_socket_delete_unsafe(dap_events_socket_t *a_esocket, bool a_preserve_inheritor)
{
    dap_return_if_fail(a_esocket);
    
    debug_if(g_debug_reactor, L_DEBUG, "Deleting esocket "DAP_FORMAT_ESOCKET_UUID" type %s", 
             a_esocket->uuid, dap_events_socket_get_type_str(a_esocket));
    
#ifndef DAP_EVENTS_CAPS_IOCP
    dap_events_socket_close(a_esocket);
#endif
    
    // Clean up packet queue for datagram sockets
    if (a_esocket->packet_queue) {
        s_packet_queue_delete(a_esocket->packet_queue);
        a_esocket->packet_queue = NULL;
    }
    
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
 * @param a_w
 * @param a_es_uuid
 */
void dap_events_socket_remove_and_delete_mt(dap_worker_t *a_w, dap_events_socket_uuid_t a_es_uuid)
{
    dap_return_if_fail(a_w);
#ifdef DAP_EVENTS_CAPS_IOCP
    dap_overlapped_t *ol = DAP_NEW_Z(dap_overlapped_t);
    ol->ol.Internal = (ULONG_PTR)a_es_uuid;
    ol->ol.Offset = DAP_SOCK_SIGNAL_CLOSE;
    ol->ol.OffsetHigh = 1;
    ol->op = io_call;
    if ( !PostQueuedCompletionStatus(a_w->context->iocp, 0, (ULONG_PTR)s_es_set_flag, (OVERLAPPED*)ol) ) {
        log_it(L_ERROR, "Can't schedule deletion of %"DAP_UINT64_FORMAT_U" in context #%d, error %d",
               a_es_uuid, a_w->context->id, GetLastError());
        dap_overlapped_free(ol);
    }
#else
    dap_events_socket_uuid_t * l_es_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
    if (!l_es_uuid_ptr) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return;
    }
    *l_es_uuid_ptr = a_es_uuid;

    if( !dap_context_queue_push(a_w->queue_es_delete, l_es_uuid_ptr) ){
        log_it(L_ERROR,"Can't send %"DAP_UINT64_FORMAT_U" uuid in queue",a_es_uuid);
        DAP_DELETE(l_es_uuid_ptr);
    }
#endif
}

/**
 * @brief dap_events_socket_set_readable_mt
 * @param a_w
 * @param a_es_uuid
 * @param a_is_ready
 */
void dap_events_socket_set_readable_mt(dap_worker_t * a_w, dap_events_socket_uuid_t a_es_uuid, bool a_is_ready)
{
    dap_return_if_fail(a_w);
#ifdef DAP_EVENTS_CAPS_IOCP
    dap_overlapped_t *ol = DAP_NEW_Z(dap_overlapped_t);
    ol->ol.Internal = (ULONG_PTR)a_es_uuid;
    ol->ol.Offset = DAP_SOCK_READY_TO_READ;
    ol->ol.OffsetHigh = (DWORD)a_is_ready;
    ol->op = io_call;
    if ( !PostQueuedCompletionStatus(a_w->context->iocp, 0, (ULONG_PTR)s_es_set_flag, (OVERLAPPED*)ol) ) {
        log_it(L_ERROR, "Can't schedule reading from %"DAP_UINT64_FORMAT_U" in context #%d, error %d",
               a_es_uuid, a_w->context->id, GetLastError());
        dap_overlapped_free(ol);
    }
#else
    dap_worker_msg_io_t * l_msg = DAP_NEW_Z(dap_worker_msg_io_t); if (! l_msg) return;
    l_msg->esocket_uuid = a_es_uuid;
    if (a_is_ready)
        l_msg->flags_set = DAP_SOCK_READY_TO_READ;
    else
        l_msg->flags_unset = DAP_SOCK_READY_TO_READ;

    if( !dap_context_queue_push(a_w->queue_es_io, l_msg) ){
        log_it(L_ERROR, "set readable mt: wasn't send pointer to queue with set readble flag");
        DAP_DELETE(l_msg);
    }
#endif
}

/**
 * @brief dap_events_socket_set_writable_mt
 * @param a_w
 * @param a_es_uuid
 * @param a_is_ready
 */
void dap_events_socket_set_writable_mt(dap_worker_t *a_w, dap_events_socket_uuid_t a_es_uuid, bool a_is_ready)
{
    dap_return_if_fail(a_w);
#ifdef DAP_EVENTS_CAPS_IOCP
    dap_overlapped_t *ol = DAP_NEW_Z(dap_overlapped_t);
    ol->ol.Internal = (ULONG_PTR)a_es_uuid;
    ol->ol.Offset = DAP_SOCK_READY_TO_WRITE;
    ol->ol.OffsetHigh = (DWORD)a_is_ready;
    ol->op = io_call;
    if ( !PostQueuedCompletionStatus(a_w->context->iocp, 0, (ULONG_PTR)s_es_set_flag, (OVERLAPPED*)ol) ) {
        log_it(L_ERROR, "Can't schedule writing to %"DAP_UINT64_FORMAT_U" in context #%d, error %d",
               a_es_uuid, a_w->context->id, GetLastError());
        dap_overlapped_free(ol);
    }
#else
    dap_worker_msg_io_t * l_msg = DAP_NEW_Z(dap_worker_msg_io_t); if (!l_msg) return;
    l_msg->esocket_uuid = a_es_uuid;

    if (a_is_ready)
        l_msg->flags_set = DAP_SOCK_READY_TO_WRITE;
    else
        l_msg->flags_unset = DAP_SOCK_READY_TO_WRITE;

    if( !dap_context_queue_push(a_w->queue_es_io, l_msg) ){
        log_it(L_ERROR, "set writable mt: wasn't send pointer to queue");
        DAP_DELETE(l_msg);
    }
#endif
}

#ifndef DAP_EVENTS_CAPS_IOCP
void dap_events_socket_assign_on_worker_inter(dap_events_socket_t * a_es_input, dap_events_socket_t * a_es)
{
    if (!a_es)
        log_it(L_ERROR, "Can't send NULL esocket in interthreads pipe input");
    if (!a_es_input)
        log_it(L_ERROR, "Interthreads pipe input is NULL");
    if (! a_es || ! a_es_input)
        return;

    a_es->last_ping_request = time(NULL);
    //debug_if(s_debug_more, L_DEBUG, "Interthread assign esocket %p(fd %d) on input esocket %p (fd %d)", a_es, a_es->fd,
    //       a_es_input, a_es_input->fd);
    dap_worker_add_events_socket_inter(a_es_input,a_es);

}
#endif

/**
 * @brief dap_events_socket_write
 * @param a_es_uuid
 * @param a_data
 * @param a_data_size
 * @param a_callback_success
 * @param a_callback_error
 * @return
 */
size_t dap_events_socket_write(dap_events_socket_uuid_t a_es_uuid, const void * a_data, size_t a_data_size,
                               dap_events_socket_callback_t a_callback_success,
                               dap_events_socket_callback_error_t a_callback_error, void * a_arg)
{
   dap_context_t * l_context = dap_context_current();
   if(l_context){ // We found it
       dap_events_socket_t * l_queue;
       // TODO complete things
   }
   return 0;
}


/**
 * @brief dap_events_socket_write_mt
 * @param a_w
 * @param a_es_uuid
 * @param a_data
 * @param l_data_size
 * @return
 */
size_t dap_events_socket_write_mt(dap_worker_t * a_w,dap_events_socket_uuid_t a_es_uuid, void *a_data, size_t a_data_size)
{
#ifdef DAP_EVENTS_CAPS_IOCP
    dap_overlapped_t *ol = DAP_NEW_SIZE(dap_overlapped_t, sizeof(dap_overlapped_t) + a_data_size);
    *ol = (dap_overlapped_t) { .op = io_write };
    memcpy(ol->buf, a_data, a_data_size);
    debug_if(g_debug_reactor, L_INFO, "Write %lu bytes to es ["DAP_FORMAT_ESOCKET_UUID": worker %d]", a_data_size, a_es_uuid, a_w->id);
    return PostQueuedCompletionStatus(a_w->context->iocp, a_data_size, (ULONG_PTR)a_es_uuid, (OVERLAPPED*)ol)
        ? a_data_size
        : ( DAP_DELETE(ol), log_it(L_ERROR, "Can't schedule writing to %"DAP_UINT64_FORMAT_U" in context #%d, error %d",
                                   a_es_uuid, a_w->context->id, GetLastError()), 0 );
#else
    dap_worker_msg_io_t * l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_worker_msg_io_t, 0);
    l_msg->esocket_uuid = a_es_uuid;
    if (a_data && a_data_size)
        l_msg->data = (char*)a_data; // DAP_DUP_SIZE(a_data, a_data_size);
    l_msg->data_size = a_data_size;
    l_msg->flags_set = DAP_SOCK_READY_TO_WRITE;

    if( !dap_context_queue_push(a_w->queue_es_io, l_msg) ){
        log_it(L_ERROR, "write mt: wasn't send pointer to queue");
        DAP_DEL_MULTY(l_msg->data, l_msg);
        return 0;
    }
    return a_data_size;
#endif
}

/**
 * @brief dap_events_socket_write_f_mt
 * @param a_es_uuid
 * @param a_format
 * @return
 */
size_t dap_events_socket_write_f_mt(dap_worker_t * a_w,dap_events_socket_uuid_t a_es_uuid, const char * a_format,...)
{
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
    return PostQueuedCompletionStatus(a_w->context->iocp, l_data_size, a_es_uuid, (OVERLAPPED*)ol)
        ? l_data_size
        : ( DAP_DELETE(ol), log_it(L_ERROR, "Can't schedule writing to %"DAP_UINT64_FORMAT_U" in context #%d, error %d",
               a_es_uuid, a_w->context->id, GetLastError()), 0 );
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

    if( !dap_context_queue_push(a_w->queue_es_io, l_msg) ){
        log_it(L_ERROR, "Write f mt: wasn't send pointer to queue");
        DAP_DELETE(l_msg->data);
        DAP_DELETE(l_msg);
        return 0;
    }
    return l_data_size;
#endif
}

/**
 * @brief Ensure sufficient space in output buffer and return write position
 * @param a_es Event socket
 * @param a_required_size Required additional space
 * @return Pointer to write position in buffer, or NULL on error
 */
static inline byte_t *s_events_socket_ensure_buf_space(dap_events_socket_t *a_es, size_t a_required_size)
{
    static const size_t l_basic_buf_size = DAP_EVENTS_SOCKET_BUF_LIMIT / 4;
    byte_t *l_buf_out;
    
    if (a_es->buf_out_size_max < a_es->buf_out_size + a_required_size) {
        if (__builtin_add_overflow(a_es->buf_out_size_max, dap_max(l_basic_buf_size, a_required_size), &a_es->buf_out_size_max)) {
            log_it(L_ERROR, "Integer overflow in buffer size calculation");
            return NULL;
        }
        if (!(l_buf_out = DAP_REALLOC(a_es->buf_out, a_es->buf_out_size_max))) {
            log_it(L_ERROR, "Can't increase capacity: OOM!");
            return NULL;
        }
        a_es->buf_out = l_buf_out;
        debug_if(g_debug_reactor, L_MSG, "[!] Socket %"DAP_FORMAT_SOCKET": increase capacity to %zu, actual size: %zu", 
                 a_es->fd, a_es->buf_out_size_max, a_es->buf_out_size);
    } else if ((a_es->buf_out_size + a_required_size <= l_basic_buf_size / 4) && (a_es->buf_out_size_max > l_basic_buf_size)) {
        a_es->buf_out_size_max = l_basic_buf_size;
        a_es->buf_out = DAP_REALLOC(a_es->buf_out, a_es->buf_out_size_max);
        debug_if(g_debug_reactor, L_MSG, "[!] Socket %"DAP_FORMAT_SOCKET": decrease capacity to %zu, actual size: %zu",
                 a_es->fd, a_es->buf_out_size_max, a_es->buf_out_size);
    }
    
    return a_es->buf_out + a_es->buf_out_size;
}

/**
 * @brief Finalize write operation - update buffer size and set flags
 * @param a_es Event socket
 * @param a_bytes_written Number of bytes written
 */
static inline void s_events_socket_finalize_write(dap_events_socket_t *a_es, size_t a_bytes_written)
{
    a_es->buf_out_size += a_bytes_written;
    debug_if(g_debug_reactor, L_DEBUG, "Write %zu bytes to \"%s\" "DAP_FORMAT_ESOCKET_UUID", total size: %zu",
             a_bytes_written, dap_events_socket_get_type_str(a_es), a_es->uuid, a_es->buf_out_size);
    dap_events_socket_set_writable_unsafe(a_es, true);
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
    
    if (a_es->type == DESCRIPTOR_TYPE_SOCKET_UDP) {
        log_it(L_CRITICAL, "ARCHITECTURE VIOLATION: dap_events_socket_write_unsafe() called on UDP socket! "
               "UDP requires explicit destination address per packet. Use dap_events_socket_sendto_unsafe() instead!");
        return 0;
    }

    // TCP/stream sockets: Use buffered writes
    byte_t *l_write_pos = s_events_socket_ensure_buf_space(a_es, a_data_size);
    if (!l_write_pos)
        return 0;
    
    memcpy(l_write_pos, a_data, a_data_size);
    s_events_socket_finalize_write(a_es, a_data_size);
    return a_data_size;
}

/**
 * @brief Send datagram (UDP/SCTP) to specific address (UNSAFE version)
 * 
 * Specialized function for datagram sockets that accepts destination address explicitly.
 * This is more efficient than dap_events_socket_write_unsafe() for UDP because:
 * - Avoids overwriting a_es->addr_storage (which may be shared across packets)
 * - Directly queues packet with correct destination in packet_queue
 * - Supports multiple concurrent sendto operations without race conditions
 * 
 * UNSAFE: Must be called from socket's owner worker thread only.
 * 
 * @param a_es Event socket (must be DESCRIPTOR_TYPE_SOCKET_UDP or CLIENT)
 * @param a_data Data buffer to send
 * @param a_data_size Size of data
 * @param a_addr Destination address
 * @param a_addr_len Address length
 * @return Number of bytes queued/sent, or 0 on error
 */
size_t dap_events_socket_sendto_unsafe(dap_events_socket_t *a_es, 
                                       const void *a_data, 
                                       size_t a_data_size,
                                       const struct sockaddr_storage *a_addr,
                                       socklen_t a_addr_len)
{
    if (!a_es || !a_data || a_data_size == 0 || !a_addr) {
        log_it(L_ERROR, "Invalid arguments for sendto_unsafe");
        return 0;
    }
    
    if (a_es->flags & DAP_SOCK_SIGNAL_CLOSE) {
        debug_if(g_debug_reactor, L_NOTICE, "Trying to sendto into closing socket %"DAP_FORMAT_SOCKET, a_es->fd);
        return 0;
    }
    
    // Only for datagram sockets
    if (a_es->type != DESCRIPTOR_TYPE_SOCKET_UDP && 
        a_es->type != DESCRIPTOR_TYPE_SOCKET_CLIENT) {
        log_it(L_ERROR, "sendto_unsafe called on non-datagram socket (type=%d, fd=%d, uuid=0x%016lx, flags=0x%08x)", 
               a_es->type, a_es->fd, a_es->uuid, a_es->flags);
        return 0;
    }
    
    // Check maximum datagram size
    if (a_data_size > DAP_UDP_MAX_DATAGRAM_SIZE) {
        log_it(L_ERROR, "UDP datagram too large: %zu bytes (max %d bytes)",
               a_data_size, DAP_UDP_MAX_DATAGRAM_SIZE);
        return 0;
    }
    
    // If queue already has packets, add to queue (maintain ordering!)
    if (a_es->packet_queue && a_es->packet_queue->count > 0) {
        if (s_packet_queue_push(a_es->packet_queue, a_data, a_data_size, a_addr, a_addr_len) == 0) {
            // Mark socket as writable to trigger queue flush
            dap_events_socket_set_writable_unsafe(a_es, true);
            debug_if(g_debug_reactor, L_DEBUG,
                     "Datagram queued (queue not empty): %zu bytes (queue size: %zu)",
                     a_data_size, a_es->packet_queue->count);
            return a_data_size;  // Queued successfully
        }
        return 0;  // Queue full
    }
    
    // Try direct sendto
    debug_if(g_debug_reactor, L_DEBUG, "Attempting direct sendto: fd=%d, size=%zu", 
             a_es->fd, a_data_size);
    
    ssize_t l_sent = sendto(a_es->fd, a_data, a_data_size, 0,
                            (struct sockaddr*)a_addr, a_addr_len);
    
    if (l_sent < 0) {
        int l_errno = errno;
        if (l_errno == EAGAIN || l_errno == EWOULDBLOCK) {
            // Socket would block, create queue and add packet
            if (!a_es->packet_queue) {
                a_es->packet_queue = s_packet_queue_create();
                if (!a_es->packet_queue) {
                    log_it(L_ERROR, "Failed to create packet queue");
                    return 0;
                }
            }
            
            if (s_packet_queue_push(a_es->packet_queue, a_data, a_data_size, a_addr, a_addr_len) == 0) {
                // Mark socket as writable to trigger queue flush later
                dap_events_socket_set_writable_unsafe(a_es, true);
                
                debug_if(g_debug_reactor, L_DEBUG,
                         "Datagram sendto would block, queued %zu bytes", a_data_size);
                return a_data_size;  // Queued successfully
            }
            return 0;  // Queue full
        }
        
        // Permanent error
        log_it(L_ERROR, "Datagram sendto failed: %s", strerror(l_errno));
        return 0;
    }
    
    debug_if(g_debug_reactor, L_DEBUG,
             "Datagram direct sendto: sent %zd bytes (requested %zu)",
             l_sent, a_data_size);
    
    return (size_t)l_sent;
}

/**
 * @brief dap_events_socket_write_f Write formatted text to the client
 * @param a_es Conn instance
 * @param a_format Format
 * @return Number of bytes that were placed into the buffer
 */
ssize_t dap_events_socket_write_f_unsafe(dap_events_socket_t *a_es, const char *a_format, ...)
{
    dap_return_val_if_fail(a_es && a_es->buf_out && a_format, -1);
    if (a_es->flags & DAP_SOCK_SIGNAL_CLOSE) {
        debug_if(g_debug_reactor, L_NOTICE, "Trying to write into closing socket %"DAP_FORMAT_SOCKET, a_es->fd);
        return -1;
    }
    
    va_list ap, ap_copy;
    va_start(ap, a_format);
    va_copy(ap_copy, ap);
    
    // Determine exact size of formatted string
    int l_data_size = vsnprintf(NULL, 0, a_format, ap);
    va_end(ap);
    
    if (l_data_size < 0) {
        log_it(L_ERROR, "Can't determine formatted data size for '%s'", a_format);
        va_end(ap_copy);
        return -1;
    }
    
    // Prepare space in buffer
    byte_t *l_write_pos = s_events_socket_ensure_buf_space(a_es, l_data_size);
    if (!l_write_pos) {
        va_end(ap_copy);
        return -1;
    }
    
    // Format DIRECTLY into event socket buffer
    int l_actual_size = vsnprintf((char *)l_write_pos, l_data_size + 1, a_format, ap_copy);
    va_end(ap_copy);
    
    if (l_actual_size != l_data_size) {
        log_it(L_ERROR, "Formatted data size mismatch: expected %d, got %d", l_data_size, l_actual_size);
        return -1;
    }
    
    s_events_socket_finalize_write(a_es, l_data_size);
    return l_data_size;
}

/**
 * @brief dap_events_socket_pop_from_buf_in
 * @param a_es Event socket instance
 * @param a_data Output buffer to copy data to
 * @param a_data_size Maximum size to read
 * @return Number of bytes actually copied
 */
size_t dap_events_socket_pop_from_buf_in(dap_events_socket_t *a_es, void *a_data, size_t a_data_size)
{
    dap_return_val_if_pass_err(!a_es || !a_data || !a_data_size || !a_es->buf_in || !a_es->buf_in_size,
                                0, "Sanity check error");
    
    if ( a_data_size < a_es->buf_in_size )
    {
        memcpy(a_data, a_es->buf_in, a_data_size);
        memmove(a_es->buf_in, a_es->buf_in + a_data_size, a_es->buf_in_size -= a_data_size);
    } else {
        memcpy(a_data, a_es->buf_in, a_es->buf_in_size);
        a_data_size = a_es->buf_in_size;
        a_es->buf_in_size = 0;
    }    
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
