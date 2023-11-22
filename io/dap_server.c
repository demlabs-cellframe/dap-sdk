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

#if defined(DAP_OS_WINDOWS)
#include "wepoll.h"
#include <ws2tcpip.h>

#elif defined(DAP_OS_LINUX)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#elif defined (DAP_OS_BSD)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <sys/un.h>
#include <netdb.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>


#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <signal.h>
#include <utlist.h>
#if ! defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if ! defined (__USE_GNU)
#define __USE_GNU
#endif
#include <sched.h>
#include "dap_common.h"
#include "dap_config.h"
#include "dap_server.h"
#include "dap_worker.h"
#include "dap_events.h"

#define LOG_TAG "dap_server"

static void s_es_server_new(dap_events_socket_t *a_es, void *a_arg);
static dap_events_socket_t * s_es_server_create(int a_sock,
                                             dap_events_socket_callbacks_t * a_callbacks, dap_server_t * a_server);
static int s_server_run(dap_server_t * a_server, dap_events_socket_callbacks_t *a_callbacks );
#ifdef DAP_EVENTS_CAPS_IOCP
static void s_es_server_new_ex(dap_events_socket_t *a_es, void *a_arg);
static void s_es_server_accept_ex(dap_events_socket_t *a_es, SOCKET a_remote_socket, struct sockaddr* a_remote_addr);
#endif
static void s_es_server_accept(dap_events_socket_t *a_es, SOCKET a_remote_socket, struct sockaddr* a_remote_addr);
static void s_es_server_error(dap_events_socket_t *a_es, int a_arg);

static dap_server_t* s_default_server = NULL;

/**
 * @brief dap_server_init
 * @return
 */
int dap_server_init()
{
    int l_ret = 0;
#ifdef DAP_EVENTS_CAPS_IOCP
    SOCKET l_socket = socket(AF_INET, SOCK_STREAM, 0);
    DWORD l_bytes = 0;
    static GUID l_guid_AcceptEx             = WSAID_ACCEPTEX,
                l_guid_GetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS,
                l_guid_ConnectEx            = WSAID_CONNECTEX;
    if (
            WSAIoctl(l_socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &l_guid_AcceptEx,  sizeof(l_guid_AcceptEx),
                     &pfn_AcceptEx,     sizeof(pfn_AcceptEx),
                     &l_bytes, NULL, NULL) == SOCKET_ERROR

            || WSAIoctl(l_socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                        &l_guid_GetAcceptExSockaddrs,   sizeof(l_guid_GetAcceptExSockaddrs),
                        &pfn_GetAcceptExSockaddrs,      sizeof(pfn_GetAcceptExSockaddrs),
                        &l_bytes, NULL, NULL) == SOCKET_ERROR

            || WSAIoctl(l_socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                        &l_guid_ConnectEx,  sizeof(l_guid_ConnectEx),
                        &pfn_ConnectEx,     sizeof(pfn_ConnectEx),
                        &l_bytes, NULL, NULL) == SOCKET_ERROR
            )
    {
        log_it(L_ERROR, "WSAIoctl() error %d", WSAGetLastError());
        l_ret = -1;
    }
    closesocket(l_socket);
#endif
    log_it(L_NOTICE, "Server module init%s", l_ret ? " failed" : "");
    return l_ret;
}

/**
 * @brief dap_server_deinit
 */
void dap_server_deinit()
{
}

void dap_server_set_default(dap_server_t* a_server)
{
    s_default_server = a_server;
}

dap_server_t* dap_server_get_default()
{
    return s_default_server;
}

/**
 * @brief dap_server_delete
 * @param a_server
 */
void dap_server_delete(dap_server_t *a_server)
{
    while (a_server->es_listeners) {
        dap_events_socket_t *l_es = (dap_events_socket_t *)a_server->es_listeners->data;
        dap_events_socket_remove_and_delete_mt(l_es->context->worker, l_es->uuid); // TODO unsafe moment. Replace storage to uuids
        dap_list_t *l_tmp = a_server->es_listeners;
        a_server->es_listeners = l_tmp->next;
        DAP_DELETE(l_tmp);
    }
    if(a_server->delete_callback)
        a_server->delete_callback(a_server,NULL);

    if( a_server->_inheritor )
        DAP_DELETE( a_server->_inheritor );

    pthread_mutex_destroy(&a_server->started_mutex);
    pthread_cond_destroy(&a_server->started_cond);

    DAP_DELETE(a_server);
}

/**
 * @brief dap_server_new_local
 * @param a_events
 * @param a_path
 * @param a_mode
 * @param a_callbacks
 * @return
 */
dap_server_t* dap_server_new_local(const char * a_path, const char* a_mode, dap_events_socket_callbacks_t *a_callbacks)
{
#ifdef DAP_OS_UNIX
    dap_server_t *l_server =  DAP_NEW_Z(dap_server_t);
    if (!l_server) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    l_server->socket_listener=-1; // To diff it from 0 fd
    l_server->type = SERVER_LOCAL;
    l_server->socket_listener = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (l_server->socket_listener < 0) {
        int l_errno = errno;
        log_it (L_ERROR,"Socket error %s (%d)",strerror(l_errno), l_errno);
        DAP_DELETE(l_server);
        return NULL;
    }

    log_it(L_NOTICE,"Listen socket %d created...", l_server->socket_listener);

    // Set path
    if(a_path){
        l_server->listener_path.sun_family =  AF_UNIX;
        strncpy(l_server->listener_path.sun_path,a_path,sizeof(l_server->listener_path.sun_path)-1);
        if ( access( a_path , R_OK) != -1 )
            unlink( a_path );
    }

    mode_t l_listen_unix_socket_permissions = 0770;
    if (a_mode){
        sscanf(a_mode,"%ou", &l_listen_unix_socket_permissions );
    }


    if(s_server_run(l_server,a_callbacks)==0){
        if(a_path)
            chmod(a_path,l_listen_unix_socket_permissions);
        return l_server;
    }else
        return NULL;
#else
    log_it(L_ERROR, "Local server is not implemented for your platform");
    return NULL;
#endif
}

/**
 * @brief dap_server_new
 * @param a_events
 * @param a_addr
 * @param a_port
 * @param a_type
 * @return
 */
dap_server_t *dap_server_new(const char *a_addr, uint16_t a_port, dap_server_type_t a_type, dap_events_socket_callbacks_t *a_callbacks)
{
    dap_server_t *l_server =  DAP_NEW_Z(dap_server_t);
    if (!l_server) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
#ifndef DAP_OS_WINDOWS
    l_server->socket_listener=-1; // To diff it from 0 fd
#else
    l_server->socket_listener = INVALID_SOCKET;
#endif

    strncpy(l_server->address, a_addr ? a_addr : "0.0.0.0", sizeof(l_server->address) ); // If NULL we listen everything
    l_server->port = a_port;
    switch (a_type) {
    case SERVER_TCP:
        l_server->socket_listener = socket(AF_INET, SOCK_STREAM, 0);
        break;
    case SERVER_UDP:
    case SERVER_LOCAL:
        l_server->socket_listener = socket(AF_INET, SOCK_DGRAM, 0);
        break;
    default:
        log_it(L_ERROR, "Unknown server type %d", a_type);
        DAP_DELETE(l_server);
        return NULL;
    }
    l_server->type = a_type;

#ifdef DAP_OS_WINDOWS
    if (l_server->socket_listener == INVALID_SOCKET) {
        log_it(L_ERROR, "Socket error: %d", WSAGetLastError());
#else
    if (l_server->socket_listener < 0) {
        int l_errno = errno;
        log_it (L_ERROR,"Socket error %s (%d)",strerror(l_errno), l_errno);
#endif
        DAP_DELETE(l_server);
        return NULL;
    }

    log_it(L_NOTICE,"Listen socket %"DAP_FORMAT_SOCKET" created...", l_server->socket_listener);
    int reuse=1;

    if (setsockopt(l_server->socket_listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
        log_it(L_WARNING, "Can't set up REUSEADDR flag to the socket");
#ifdef SO_REUSEPORT
    reuse=1;
    if (setsockopt(l_server->socket_listener, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse)) < 0)
        log_it(L_WARNING, "Can't set up REUSEPORT flag to the socket");
#endif

    l_server->listener_addr.sin_family = AF_INET;
    l_server->listener_addr.sin_port = htons(l_server->port);
    inet_pton(AF_INET, l_server->address, &(l_server->listener_addr.sin_addr));

    return !s_server_run(l_server, a_callbacks) ? l_server : NULL;
}

/**
 * @brief s_server_run
 * @param a_server
 * @param a_callbacks
 */
static int s_server_run(dap_server_t *a_server, dap_events_socket_callbacks_t *a_callbacks)
{
    assert(a_server);

    struct sockaddr * l_listener_addr =
#ifndef DAP_OS_WINDOWS
            a_server->type == SERVER_LOCAL ?
                (struct sockaddr *) &(a_server->listener_path) :
#endif
                (struct sockaddr *) &(a_server->listener_addr);

    socklen_t l_listener_addr_len =
#ifndef DAP_OS_WINDOWS
            a_server->type == SERVER_LOCAL ?
                sizeof(a_server->listener_path) :
#endif
                sizeof(a_server->listener_addr);

    if ( bind(a_server->socket_listener, l_listener_addr, l_listener_addr_len) == SOCKET_ERROR ) {
#ifdef DAP_OS_WINDOWS
        log_it(L_ERROR,"Bind error: %d", WSAGetLastError());
        closesocket(a_server->socket_listener);
#else
        log_it(L_ERROR,"Bind error: %s",strerror(errno));
        close(a_server->socket_listener);
        if ( errno == EACCES ) // EACCES=13
            log_it( L_ERROR, "Server can't start. Permission denied");
#endif
        DAP_DELETE(a_server);
        return -1;
    } else {
        log_it(L_INFO, "Binded %s:%u", a_server->address, a_server->port);
        listen(a_server->socket_listener, SOMAXCONN);
    }
#ifdef DAP_OS_WINDOWS
     u_long l_mode = 1;
     ioctlsocket(a_server->socket_listener, (long) FIONBIO, &l_mode);
#else
    fcntl( a_server->socket_listener, F_SETFL, O_NONBLOCK);
#endif
    pthread_mutex_init(&a_server->started_mutex,NULL);
    pthread_cond_init(&a_server->started_cond,NULL);

    dap_events_socket_callbacks_t l_callbacks = {
#ifdef DAP_EVENTS_CAPS_IOCP
        .accept_callback = s_es_server_accept_ex,
        .new_callback    = s_es_server_new_ex,
#else
        .accept_callback = s_es_server_accept,
        .new_callback    = s_es_server_new,
#endif
        .read_callback   = a_callbacks ? a_callbacks->read_callback     : NULL,
        .write_callback  = a_callbacks ? a_callbacks->write_callback    : NULL,
        .error_callback  = s_es_server_error
    };

#ifdef DAP_EVENTS_CAPS_EPOLL
    for(size_t l_worker_id = 0; l_worker_id < dap_events_thread_get_count() ; l_worker_id++){
        dap_worker_t *l_w = dap_events_worker_get(l_worker_id);
        assert(l_w);
        dap_events_socket_t * l_es = dap_events_socket_wrap2( a_server, a_server->socket_listener, &l_callbacks);
        a_server->es_listeners = dap_list_append(a_server->es_listeners, l_es);

        if (l_es) {
            l_es->type = a_server->type == SERVER_TCP ? DESCRIPTOR_TYPE_SOCKET_LISTENING : DESCRIPTOR_TYPE_SOCKET_UDP;
            // Prepare for multi thread listening
            l_es->ev_base_flags = EPOLLIN;
#ifdef EPOLLEXCLUSIVE
            // if we have poll exclusive
            l_es->ev_base_flags |= EPOLLET | EPOLLEXCLUSIVE;
#endif
            l_es->_inheritor = a_server;
            pthread_mutex_lock(&a_server->started_mutex);
            dap_worker_add_events_socket( l_w, l_es );
            while (!a_server->started)
                pthread_cond_wait(&a_server->started_cond, &a_server->started_mutex);
            pthread_mutex_unlock(&a_server->started_mutex);
        } else{
            log_it(L_WARNING, "Can't wrap event socket for %s:%u server", a_server->address, a_server->port);
            return -2;
        }
    }
#else
    dap_events_socket_t *l_es_server = dap_events_socket_wrap2(a_server, a_server->socket_listener, &l_callbacks);
    if (!l_es_server) {
        log_it(L_ERROR, "Failed to wrap server listening socket");
        DAP_DELETE(a_server);
        return -3;
    }
#ifdef DAP_EVENTS_CAPS_EPOLL
    l_es->ev_base_flags = EPOLLIN;
#ifdef EPOLLEXCLUSIVE
    l_es->ev_base_flags |= EPOLLET | EPOLLEXCLUSIVE;
#endif
#endif
    a_server->es_listeners = dap_list_append(a_server->es_listeners, l_es_server);
    l_es_server->type = a_server->type == SERVER_TCP ? DESCRIPTOR_TYPE_SOCKET_LISTENING : DESCRIPTOR_TYPE_SOCKET_UDP;
    l_es_server->_inheritor = a_server;
    pthread_mutex_lock(&a_server->started_mutex);
    dap_worker_add_events_socket_auto(l_es_server);
    while (!a_server->started)
        pthread_cond_wait(&a_server->started_cond, &a_server->started_mutex);
    pthread_mutex_unlock(&a_server->started_mutex);
#endif
    return 0;
}

/**
 * @brief s_es_server_new
 * @param a_es
 * @param a_arg
 */
static void s_es_server_new(dap_events_socket_t *a_es, void * a_arg)
{
    log_it(L_DEBUG, "Created server socket %p on worker %u", a_es, a_es->context->worker->id);
    dap_server_t *l_server = (dap_server_t*) a_es->_inheritor;
    pthread_mutex_lock( &l_server->started_mutex);
    l_server->started = true;
    pthread_cond_broadcast( &l_server->started_cond);
    pthread_mutex_unlock( &l_server->started_mutex);
}

/**
 * @brief s_es_server_error
 * @param a_es
 * @param a_arg
 */
static void s_es_server_error(dap_events_socket_t *a_es, int a_arg)
{
    (void) a_arg;
    (void) a_es;
    char l_buf[128];
    strerror_r(errno, l_buf, sizeof (l_buf));
    log_it(L_WARNING, "Listening socket error: %s, ", l_buf);
}

/**
 * @brief s_es_server_accept
 * @param a_events
 * @param a_remote_socket
 * @param a_remote_addr
 */

#ifdef DAP_EVENTS_CAPS_IOCP
static void s_es_server_new_ex(dap_events_socket_t *a_es, void *a_arg) {
    dap_events_socket_set_readable_unsafe(a_es, true);
    s_es_server_accept_ex(a_es, INVALID_SOCKET, NULL); // Initial AcceptEx
    s_es_server_new(a_es, a_arg);
}

static void s_es_server_accept_ex(dap_events_socket_t *a_es, SOCKET a_remote_socket, struct sockaddr *a_remote_addr) {
    if (a_remote_socket != INVALID_SOCKET) {
        s_es_server_accept(a_es, a_remote_socket, a_remote_addr);
    }

    // Accept the next connection...

}

#endif

static void s_es_server_accept(dap_events_socket_t *a_es, SOCKET a_remote_socket, struct sockaddr *a_remote_addr)
{
    socklen_t a_remote_addr_size = sizeof(*a_remote_addr);
    a_es->buf_in_size = 0; // It should be 1 so we reset it to 0
    //log_it(L_DEBUG, "Server socket %d is active",i);
    dap_server_t *l_server = (dap_server_t*)a_es->_inheritor;
    assert(l_server);

    dap_events_socket_t * l_es_new = NULL;
    log_it(L_DEBUG, "[es:%p] Listening socket (binded on %s:%u) got new incoming connection", a_es, l_server->address,l_server->port);
    log_it(L_DEBUG, "[es:%p] Accepted new connection (sock %"DAP_FORMAT_SOCKET" from %"DAP_FORMAT_SOCKET")", a_es, a_remote_socket, a_es->socket);
    l_es_new = s_es_server_create(a_remote_socket,&l_server->client_callbacks,l_server);
    //l_es_new->is_dont_reset_write_flag = true; // By default all income connection has this flag
    getnameinfo(a_remote_addr,a_remote_addr_size, l_es_new->hostaddr, DAP_EVSOCK$SZ_HOSTNAME,
                l_es_new->service, DAP_EVSOCK$SZ_SERVICE, NI_NUMERICHOST | NI_NUMERICSERV);
    struct in_addr l_addr_remote = ((struct sockaddr_in*)a_remote_addr)->sin_addr;
    inet_ntop(AF_INET, &l_addr_remote, l_es_new->hostaddr, sizeof(l_addr_remote));
    log_it(L_INFO, "Connection accepted from %s : %s", l_es_new->hostaddr, l_es_new->service);
    dap_worker_t *l_worker = dap_events_worker_get_auto();
    if (l_worker->id == a_es->worker->id) {
#ifdef DAP_OS_UNIX
#if defined (SO_INCOMING_CPU)
        int l_cpu = l_worker->id;
        setsockopt(l_es_new->socket , SOL_SOCKET, SO_INCOMING_CPU, &l_cpu, sizeof(l_cpu));
#endif
#endif
        l_es_new->worker = l_worker;
        l_es_new->last_time_active = time(NULL);
        if (l_es_new->callbacks.new_callback)
            l_es_new->callbacks.new_callback(l_es_new, NULL);
        l_es_new->is_initalized = true;
        if (dap_worker_add_events_socket_unsafe(l_worker, l_es_new)) {
            log_it(L_CRITICAL, "Can't add event socket's handler to worker i/o poll mechanism with error %d", errno);
            return;
        }
        debug_if(g_debug_reactor, L_INFO, "Direct addition of esocket %p uuid 0x%"DAP_UINT64_FORMAT_x" to worker %d",
                 l_es_new, l_es_new->uuid, l_worker->id);
    } else {
        dap_worker_add_events_socket(l_worker, l_es_new);
#ifdef DAP_EVENTS_CAPS_IOCP
        dap_events_socket_set_readable_mt(l_worker, l_es_new->uuid, true);
#endif
    }
}


/**
 * @brief s_esocket_new
 * @param a_events
 * @param a_sock
 * @param a_callbacks
 * @param a_server
 * @return
 */
static dap_events_socket_t * s_es_server_create(int a_sock, dap_events_socket_callbacks_t * a_callbacks,
                                                dap_server_t * a_server)
{
    dap_events_socket_t * ret = NULL;
    if (a_sock > 0)  {
        ret = dap_events_socket_wrap_no_add(a_sock, a_callbacks);
        ret->type = DESCRIPTOR_TYPE_SOCKET_CLIENT;
        ret->server = a_server;
    } else {
        log_it(L_CRITICAL,"Accept error: %s",strerror(errno));
    }
    return ret;
}
