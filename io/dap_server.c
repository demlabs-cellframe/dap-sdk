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
#include "dap_net.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_server"

static void s_es_server_new     (dap_events_socket_t *a_es, void *a_arg);
static int s_server_run         (dap_server_t * a_server);
static void s_es_server_accept  (dap_events_socket_t *a_es_listener, SOCKET a_remote_socket, struct sockaddr_storage *a_remote_addr);
static void s_es_server_error   (dap_events_socket_t *a_es, int a_arg);

#ifdef DAP_EVENTS_CAPS_IOCP
static void s_es_server_new_ex      (dap_events_socket_t *a_es, void *a_arg);
static void s_es_server_accept_ex   (dap_events_socket_t *a_es, SOCKET a_remote_socket, struct sockaddr_storage *a_remote_addr);
LPFN_ACCEPTEX               pfn_AcceptEx                = NULL;
LPFN_GETACCEPTEXSOCKADDRS   pfn_GetAcceptExSockaddrs    = NULL;
#endif

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
// sanity check
    dap_return_if_pass(!a_server);
// func work
    while (a_server->es_listeners) {
        dap_events_socket_t *l_es = (dap_events_socket_t *)a_server->es_listeners->data;
        dap_events_socket_remove_and_delete_mt(l_es->worker, l_es->uuid); // TODO unsafe moment. Replace storage to uuids
        dap_list_t *l_tmp = a_server->es_listeners;
        a_server->es_listeners = l_tmp->next;
        DAP_DELETE(l_tmp);
    }
    if(a_server->delete_callback)
        a_server->delete_callback(a_server,NULL);

    DAP_DEL_Z( a_server->_inheritor );

    pthread_mutex_destroy(&a_server->started_mutex);
    pthread_cond_destroy(&a_server->started_cond);

    DAP_DELETE(a_server);
}

/**
 * @brief add listen addr to server
 * @param a_server - server to add addr
 * @param a_addr - addr or path to local
 * @param a_port - port or read mode
 * @param a_callbacks - pointer to callbacks
 * @return
 */
int dap_server_listen_addr_add(dap_server_t *a_server, const char *a_addr, uint16_t a_port, dap_events_socket_callbacks_t *a_callbacks)
{
// sanity check
    dap_return_val_if_pass(!a_server, -1);
    if (!a_addr || !a_port) {
        log_it(L_ERROR, "Listener addr %s %u unspecified", a_addr, a_port);
        return -4;;
    }
// preparing
    SOCKET l_socket_listener = INVALID_SOCKET;
    switch (a_server->type) {
    case DAP_SERVER_TCP:
        l_socket_listener = socket(AF_INET, SOCK_STREAM, 0);
        break;
    case DAP_SERVER_TCP_V6:
        l_socket_listener = socket(AF_INET6, SOCK_STREAM, 0);
        break;
    case DAP_SERVER_UDP:
        l_socket_listener = socket(AF_INET, SOCK_DGRAM, 0);
        break;
#ifdef DAP_OS_UNIX
    case DAP_SERVER_LOCAL:
        l_socket_listener = socket(AF_LOCAL, SOCK_STREAM, 0);
        break;
#endif
    default:
        log_it(L_ERROR, "Specified server type %s is not implemented for your platform",
               dap_server_type_str(a_server->type));
        return -1;
    }
#ifdef DAP_OS_WINDOWS
    if (l_socket_listener == INVALID_SOCKET) {
        log_it(L_ERROR, "Socket error: %d", WSAGetLastError());
#else
    if (l_socket_listener < 0) {
        int l_errno = errno;
        log_it (L_ERROR,"Socket error %s (%d)", strerror(l_errno), l_errno);
#endif
        return -2;
    }
    log_it(L_NOTICE,"Listen socket %"DAP_FORMAT_SOCKET" created...", l_socket_listener);
// func work
    // Create socket
    dap_events_socket_t *l_es = dap_events_socket_wrap_listener(a_server, l_socket_listener, a_callbacks);

    if (a_server->type != DAP_SERVER_LOCAL)
        l_es->listener_port = a_port;
    else
        l_es->permission = a_port;
    int reuse = 1;
    if (setsockopt(l_socket_listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
        log_it(L_WARNING, "Can't set up REUSEADDR flag to the socket");
#ifdef SO_REUSEPORT
    if (setsockopt(l_socket_listener, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse)) < 0)
        log_it(L_WARNING, "Can't set up REUSEPORT flag to the socket");
#endif

    if ( dap_net_resolve_host(a_addr, dap_itoa(l_es->listener_port), &l_es->addr_storage, true) ) {
        log_it(L_ERROR, "Wrong listen address '%s : %u'", a_addr, l_es->listener_port);
        goto clean_n_quit;
    }

    if (a_server->type != DAP_SERVER_LOCAL) {
        strncpy(l_es->listener_addr_str, a_addr, sizeof(l_es->listener_addr_str)); // If NULL we listen everything
    }
#ifdef DAP_OS_UNIX
    else {
        l_es->listener_path.sun_family = AF_UNIX;
        strncpy(l_es->listener_path.sun_path, a_addr, sizeof(l_es->listener_path.sun_path) - 1);
        if (access(l_es->listener_path.sun_path, R_OK) == -1) {
            log_it(L_ERROR, "Listener path %s is unavailable", l_es->listener_path.sun_path);
            goto clean_n_quit;
        }
        unlink(l_es->listener_path.sun_path);
    }
#endif

    a_server->es_listeners = dap_list_prepend(a_server->es_listeners, l_es);
    if (s_server_run(a_server))
        goto clean_n_quit;

#ifdef DAP_OS_UNIX
    if (a_server->type == DAP_SERVER_LOCAL) {
        chmod(l_es->listener_path.sun_path, l_es->permission);
    }
#endif

    return 0;

clean_n_quit:
    a_server->es_listeners = dap_list_remove(a_server->es_listeners, l_es);
    dap_events_socket_delete_unsafe(l_es, false);
    return -3;
}

/**
 * @brief dap_server_new
 * @param a_events
 * @param a_addr
 * @param a_port
 * @param a_type
 * @return
 */
dap_server_t *dap_server_new(char **a_addrs, uint16_t a_count, dap_server_type_t a_type, dap_events_socket_callbacks_t *a_callbacks)
{
// sanity check
    dap_return_val_if_pass(!a_addrs || !a_count, NULL);
// memory alloc
    dap_server_t *l_server =  NULL;
    DAP_NEW_Z_RET_VAL(l_server, dap_server_t, NULL, NULL);
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

    l_server->type = a_type;
    char l_cur_ip[INET6_ADDRSTRLEN] = { '\0' }; uint16_t l_cur_port = 0;

    for (size_t i = 0; i < a_count; ++i) {
        int l_add_res = 0;
        if (l_server->type != DAP_SERVER_LOCAL) {
            if ( dap_net_parse_hostname(a_addrs[i], l_cur_ip, &l_cur_port) )
                log_it( L_ERROR, "Incorrect format of address \"%s\", fix net config and restart node", a_addrs[i] );
            else {
                if ( (l_add_res = dap_server_listen_addr_add(l_server, l_cur_ip, l_cur_port, &l_callbacks)) )
                    log_it( L_ERROR, "Can't add address \"%s : %u\" to listen in server, errno %d", l_cur_ip, l_cur_port, l_add_res);
            }
        }
#ifdef DAP_OS_UNIX
        else {
            char l_curr_path[MAX_PATH] = {0};
            mode_t l_listen_unix_socket_permissions = 0770;
            const char *l_curr_mode_str = strstr(a_addrs[i], ":");
            if (!l_curr_mode_str) {
                strncpy(l_curr_path, a_addrs[i], sizeof(l_curr_path) - 1);
            } else {
                l_curr_mode_str++;
                strncpy(l_curr_path, a_addrs[i], dap_min((size_t)(l_curr_mode_str - a_addrs[i]), sizeof(l_curr_path) - 1));
                sscanf(l_curr_mode_str,"%ou", &l_listen_unix_socket_permissions );
            }
            if ( l_add_res = dap_server_listen_addr_add(l_server, l_curr_path, l_listen_unix_socket_permissions, &l_callbacks) ) {
                log_it( L_ERROR, "Can't add path \"%s ( %d )\" to listen in server, errno %d",
                        l_curr_path, l_listen_unix_socket_permissions, l_add_res );
            }
        }
#endif
    }

    if (!l_server->es_listeners) {
        log_it(L_ERROR, "Server not created");
        DAP_DELETE(l_server);
        return NULL;
    }
    return l_server;
}

/**
 * @brief s_server_run
 * @param a_server
 * @param a_callbacks
 */
static int s_server_run(dap_server_t *a_server)
{
// sanity check
    dap_return_val_if_pass(!a_server || !a_server->es_listeners, -1);
// func work
    dap_events_socket_t *l_es = (dap_events_socket_t *)a_server->es_listeners->data;

    if (bind(l_es->socket, (struct sockaddr*)&l_es->addr_storage, sizeof(struct sockaddr_storage)) < 0) {
#ifdef DAP_OS_WINDOWS
        log_it(L_ERROR, "Bind error: %d", WSAGetLastError());
        closesocket(l_es->socket);
#else
        log_it(L_ERROR,"Bind error: %s",strerror(errno));
        close(l_es->socket);
        if ( errno == EACCES ) // EACCES=13
            log_it( L_ERROR, "Server can't start. Permission denied");
#endif
        return -1;
    } else {
        log_it(L_INFO, "Binded %s:%u", l_es->listener_addr_str, l_es->listener_port);
        listen(l_es->socket, SOMAXCONN);
    }
#ifdef DAP_OS_WINDOWS
     u_long l_mode = 1;
     ioctlsocket(l_es->socket, (long)FIONBIO, &l_mode);
#else
    fcntl(l_es->socket, F_SETFL, O_NONBLOCK);
#endif
    pthread_mutex_init(&a_server->started_mutex,NULL);
    pthread_cond_init(&a_server->started_cond,NULL);

#if defined DAP_EVENTS_CAPS_IOCP
    l_es->op_events[io_op_read] = CreateEvent(0, TRUE, FALSE, NULL);
#elif defined DAP_EVENTS_CAPS_EPOLL
    l_es->ev_base_flags = EPOLLIN;
#ifdef EPOLLEXCLUSIVE
    l_es->ev_base_flags |= EPOLLET | EPOLLEXCLUSIVE;
#endif
#endif
    l_es->type = a_server->type == DAP_SERVER_TCP ? DESCRIPTOR_TYPE_SOCKET_LISTENING : DESCRIPTOR_TYPE_SOCKET_UDP;
    l_es->_inheritor = a_server;
    pthread_mutex_lock(&a_server->started_mutex);
    dap_worker_add_events_socket_auto(l_es);
    while (!a_server->started)
        pthread_cond_wait(&a_server->started_cond, &a_server->started_mutex);
    pthread_mutex_unlock(&a_server->started_mutex);
    return 0;
}

/**
 * @brief s_es_server_new
 * @param a_es
 * @param a_arg
 */
static void s_es_server_new(dap_events_socket_t *a_es, void * a_arg)
{
    log_it(L_DEBUG, "Created server socket %p on worker %u", a_es, a_es->worker->id);;
    dap_server_t *l_server = a_es->server;
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

#ifdef DAP_EVENTS_CAPS_IOCP
static void s_es_server_new_ex(dap_events_socket_t *a_es, void *a_arg) {
    dap_events_socket_set_readable_unsafe(a_es, true);
    s_es_server_accept_ex(a_es, INVALID_SOCKET, NULL); // Initial AcceptEx
    s_es_server_new(a_es, a_arg);
}

static void s_es_server_accept_ex(dap_events_socket_t *a_es, SOCKET a_remote_socket, struct sockaddr_storage *a_remote_addr)
{
    if (a_remote_socket != INVALID_SOCKET) {
        s_es_server_accept(a_es, a_remote_socket, a_remote_addr);
    }
    // Accept the next connection...
}
#endif

/**
 * @brief s_es_server_accept
 * @param a_events
 * @param a_remote_socket
 * @param a_remote_addr
 */
static void s_es_server_accept(dap_events_socket_t *a_es_listener, SOCKET a_remote_socket, struct sockaddr_storage *a_remote_addr)
{
    dap_server_t *l_server = a_es_listener->server;
    assert(l_server);

    dap_events_socket_t * l_es_new = NULL;
    log_it(L_DEBUG, "[es:%p] Listening socket %"DAP_FORMAT_SOCKET" binded on %s:%u "
                    "accepted new connection from remote %"DAP_FORMAT_SOCKET"",
           a_es_listener, a_es_listener->socket, a_es_listener->listener_addr_str, a_es_listener->listener_port, a_remote_socket);
    if (a_remote_socket < 0) {
#ifdef DAP_OS_WINDOWS
        log_it(L_ERROR, "Accept error: %d", WSAGetLastError());
#else
        log_it(L_ERROR, "Accept error: %s", strerror(errno));
#endif
        return;
    }
    l_es_new = dap_events_socket_wrap_no_add(a_remote_socket, &l_server->client_callbacks);
    l_es_new->server = l_server;
    unsigned short l_family = a_remote_addr->ss_family;
    
    l_es_new->type = DESCRIPTOR_TYPE_SOCKET_CLIENT;
    l_es_new->addr_storage = *a_remote_addr;
    char l_port_str[NI_MAXSERV];

    switch (l_family) {
#ifdef DAP_OS_UNIX
    case AF_UNIX:
        l_es_new->type = DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT;
        strncpy(l_es_new->remote_addr_str, ((struct sockaddr_un*)a_remote_addr)->sun_path, sizeof(l_es_new->remote_addr_str) - 1);
        break;
#endif
    case AF_INET:
    case AF_INET6:
        if (getnameinfo((struct sockaddr*)a_remote_addr, sizeof(*a_remote_addr), l_es_new->remote_addr_str,
            sizeof(l_es_new->remote_addr_str), l_port_str, sizeof(l_port_str), NI_NUMERICHOST | NI_NUMERICSERV))
        {
#ifdef DAP_OS_WINDOWS
            log_it(L_ERROR, "getnameinfo error: %d", WSAGetLastError());
#else
            log_it(L_ERROR, "getnameinfo error: %s", strerror(errno));
#endif
            return;
        } 
        l_es_new->remote_port = strtol(l_port_str, NULL, 10);
        break;
    default:
        log_it(L_ERROR, "Unsupported protocol family %hu from accept()", l_family);
    }
    
    log_it(L_DEBUG, "[es:%p] Accepted new connection (sock %"DAP_FORMAT_SOCKET" from %"DAP_FORMAT_SOCKET")",
                                                        a_es_listener, a_remote_socket, a_es_listener->socket);
    log_it(L_INFO, "Connection accepted from %s : %hu", l_es_new->remote_addr_str, l_es_new->remote_port);
    dap_worker_t *l_worker = dap_events_worker_get_auto();
    if (l_worker->id == a_es_listener->worker->id) {
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
