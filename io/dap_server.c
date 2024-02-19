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
#include "dap_strfuncs.h"

#define LOG_TAG "dap_server"

static dap_events_socket_t * s_es_server_create(int a_sock,
                                             dap_events_socket_callbacks_t * a_callbacks, dap_server_t * a_server);
static int s_server_run(dap_server_t * a_server);
static void s_es_server_accept(dap_events_socket_t *a_es_listener, SOCKET a_remote_socket, struct sockaddr_storage *a_remote_addr);
static void s_es_server_error(dap_events_socket_t *a_es, int a_arg);
static void s_es_server_new(dap_events_socket_t *a_es, void * a_arg);

static dap_server_t* s_default_server = NULL;

/**
 * @brief dap_server_init
 * @return
 */
int dap_server_init()
{
    log_it(L_NOTICE,"Server module init");
    return 0;
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
    reuse = 1;
#ifdef SO_REUSEPORT
    if (setsockopt(l_socket_listener, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse)) < 0)
        log_it(L_WARNING, "Can't set up REUSEPORT flag to the socket");
#endif

    void *l_addr_ptr = NULL;
    const char *l_addr = a_addr;
    switch (a_server->type) {
    case DAP_SERVER_TCP:
    case DAP_SERVER_UDP:
        l_es->listener_addr.sin_family = AF_INET;
        l_es->listener_addr.sin_port = htons(l_es->listener_port);
        l_addr_ptr = &l_es->listener_addr.sin_addr;
        break;
    case DAP_SERVER_TCP_V6:
        l_es->listener_addr_v6.sin6_family = AF_INET6;
        l_es->listener_addr_v6.sin6_port = htons(l_es->listener_port);
        l_addr_ptr = &l_es->listener_addr_v6.sin6_addr;
    default:
        break;
    }

    if (a_server->type != DAP_SERVER_LOCAL) {
        if (inet_pton(AF_INET, l_addr, &l_addr_ptr) <= 0) {
            log_it(L_ERROR, "Can't convert address %s to digital form", l_addr);
            goto clean_n_quit;
        }
        strncpy(l_es->listener_addr_str, l_addr, sizeof(l_es->listener_addr_str)); // If NULL we listen everything
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
// preparing
    //create callback
    dap_events_socket_callbacks_t l_callbacks = {0};
    l_callbacks.new_callback = s_es_server_new;
    l_callbacks.accept_callback = s_es_server_accept;
    l_callbacks.error_callback = s_es_server_error;

    if (a_callbacks) {
        l_callbacks.read_callback = a_callbacks->read_callback;
        l_callbacks.write_callback = a_callbacks->write_callback;
        l_callbacks.error_callback = a_callbacks->error_callback;
    }
    l_server->type = a_type;
    char l_curr_ip[INET6_ADDRSTRLEN] = {0};
// func work
    for(size_t i = 0; i < a_count; ++i) {
        // parsing full addr
        int l_ret = -1;
        if (l_server->type != DAP_SERVER_LOCAL) {
            const char *l_curr_port_str = strstr(a_addrs[i], ":");
            uint16_t l_curr_port = 0;
            if (l_curr_port_str) {
                memset(l_curr_ip, 0, sizeof(l_curr_ip));
                strncpy(l_curr_ip, a_addrs[i], dap_min((size_t)(l_curr_port_str - a_addrs[i]), sizeof(l_curr_ip) - 1));
                l_curr_port = atol(++l_curr_port_str);
            } else {
                l_curr_port = atol(a_addrs[i]);
            }
            switch (l_server->type) {
                case DAP_SERVER_TCP:
                case DAP_SERVER_UDP:
                    if (!l_curr_ip[0])
                        strcpy(l_curr_ip, "0.0.0.0");  // If NULL we listen everything
                    break;
                case DAP_SERVER_TCP_V6:
                    if (!l_curr_ip[0])
                        strcpy(l_curr_ip, "::0");
                    break;
                default:
                    break;
            }
            l_ret = dap_server_listen_addr_add(l_server, l_curr_ip, l_curr_port, &l_callbacks);
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
            l_ret = dap_server_listen_addr_add(l_server, l_curr_path, l_listen_unix_socket_permissions, &l_callbacks);
        }
#endif
        if (l_ret)
            continue;
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
    void *l_listener_addr = NULL;
    socklen_t l_listener_addr_len = 0;
    switch (a_server->type) {
    case DAP_SERVER_TCP:
    case DAP_SERVER_UDP:
        l_listener_addr = &l_es->listener_addr;
        l_listener_addr_len = sizeof(l_es->listener_addr);
        break;
    case DAP_SERVER_TCP_V6:
        l_listener_addr = &l_es->listener_addr_v6;
        l_listener_addr_len = sizeof(l_es->listener_addr_v6);
        break;
#ifdef DAP_OS_UNIX
    case DAP_SERVER_LOCAL:
        l_listener_addr = &l_es->listener_path;
        l_listener_addr_len = sizeof(l_es->listener_path);
#endif
    default:
        log_it(L_ERROR, "Can't run server: unsupported server type %s", dap_server_type_str(a_server->type));
    }

    if (bind(l_es->socket, (struct sockaddr *)l_listener_addr, l_listener_addr_len) < 0) {
#ifdef DAP_OS_WINDOWS
        log_it(L_ERROR,"Bind error: %d", WSAGetLastError());
        closesocket(l_es->socket);
#else
        log_it(L_ERROR,"Bind error: %s",strerror(errno));
        close(l_es->socket);
        if ( errno == EACCES ) // EACCES=13
            log_it( L_ERROR, "Server can't start. Permission denied");
#endif
        return -1;
    } else {
        log_it(L_INFO,"Binded %s:%u", l_es->listener_addr_str, l_es->listener_port);
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

#ifdef DAP_EVENTS_CAPS_EPOLL
    //for(size_t l_worker_id = 0; l_worker_id < dap_events_thread_get_count() ; l_worker_id++){
        dap_worker_t *l_w = dap_events_worker_get_auto();//dap_events_worker_get(l_worker_id);
        assert(l_w);
        if (l_es) {
            l_es->type = a_server->type == DAP_SERVER_TCP ? DESCRIPTOR_TYPE_SOCKET_LISTENING : DESCRIPTOR_TYPE_SOCKET_UDP;
            // Prepare for multi thread listening
            l_es->ev_base_flags = EPOLLIN;
#ifdef EPOLLEXCLUSIVE
            // if we have poll exclusive
            l_es->ev_base_flags |= EPOLLET | EPOLLEXCLUSIVE;
#endif
            pthread_mutex_lock(&a_server->started_mutex);
            dap_worker_add_events_socket( l_w, l_es );
            while (!a_server->started)
                pthread_cond_wait(&a_server->started_cond, &a_server->started_mutex);
            pthread_mutex_unlock(&a_server->started_mutex);
        } else{
            log_it(L_WARNING, "Can't wrap event socket for %s:%u server", l_es->listener_addr_str, l_es->listener_port);
            return -2;
        }
    //}
#else
    dap_worker_t *l_w = dap_events_worker_get_auto();
    assert(l_w);
    if (l_es) {
        pthread_mutex_lock(&a_server->started_mutex);
        dap_worker_add_events_socket( l_w, l_es );
        while (!a_server->started)
            pthread_cond_wait(&a_server->started_cond, &a_server->started_mutex);
        pthread_mutex_unlock(&a_server->started_mutex);
    } else {
        log_it(L_WARNING, "Can't wrap event socket server");
        return -3;
    }
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
    log_it(L_DEBUG, "[es:%p] Listening socket (binded on %s:%u) got new incoming connection", a_es_listener, a_es_listener->listener_addr_str, a_es_listener->listener_port);
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
    unsigned short int l_family = a_remote_addr->ss_family;

    switch (l_family) {
#ifdef DAP_OS_UNIX
    case AF_UNIX:
        l_es_new->type = DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT;
        l_es_new->remote_path = *(struct sockaddr_un *)a_remote_addr;
        strncpy(l_es_new->remote_addr_str, l_es_new->remote_path.sun_path, sizeof(l_es_new->remote_addr_str) - 1);
        break;
#endif
    case AF_INET:
        l_es_new->type = DESCRIPTOR_TYPE_SOCKET_CLIENT;
        l_es_new->remote_addr = *(struct sockaddr_in *)a_remote_addr;
        inet_ntop(AF_INET, &l_es_new->remote_addr.sin_addr, l_es_new->remote_addr_str, sizeof(l_es_new->remote_addr_str));
        l_es_new->remote_port = ntohs(l_es_new->remote_addr.sin_port);
        break;
    case AF_INET6:
        l_es_new->type = DESCRIPTOR_TYPE_SOCKET_CLIENT;
        l_es_new->remote_addr_v6 = *(struct sockaddr_in6 *)a_remote_addr;
        inet_ntop(AF_INET6, &l_es_new->remote_addr_v6.sin6_addr, l_es_new->remote_addr_str, sizeof(l_es_new->remote_addr_str));
        l_es_new->remote_port = ntohs(l_es_new->remote_addr_v6.sin6_port);
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
        dap_worker_add_events_socket_auto(l_es_new);
    }
}
