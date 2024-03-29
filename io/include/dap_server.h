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


#pragma once

#include <pthread.h>
#include "uthash.h"
#include "utlist.h"
#include "dap_events_socket.h"
#include "dap_list.h"
#include "dap_cpu_monitor.h"

#ifdef DAP_OS_UNIX
#include <sys/un.h>
#endif
#if defined( DAP_OS_LINUX)

#include <netinet/in.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#define EPOLL_HANDLE  int

#elif defined(DAP_OS_WINDOWS)

#include "winsock.h"
#ifdef DAP_EVENTS_CAPS_IOCP
extern LPFN_ACCEPTEX                pfnAcceptEx;
extern LPFN_GETACCEPTEXSOCKADDRS    pfnGetAcceptExSockaddrs;
#endif

#elif defined(DAP_OS_BSD)

#else
#error "No poll headers for your platform"
#endif

typedef enum dap_server_type {
    DAP_SERVER_TCP,
    DAP_SERVER_TCP_V6,
    DAP_SERVER_UDP,
    DAP_SERVER_LOCAL
} dap_server_type_t;

DAP_STATIC_INLINE const char *dap_server_type_str(dap_server_type_t a_type)
{
    switch (a_type) {
    case DAP_SERVER_TCP:    return "TCP/IPv4";
    case DAP_SERVER_TCP_V6: return "TCP/IPv6";
    case DAP_SERVER_UDP:    return "UDP/IPv4";
    case DAP_SERVER_LOCAL:  return "UNIX LOCAL";
    default:                return "UNKNOWN";
    }
}

struct dap_server;

typedef void (*dap_server_callback_t)( struct dap_server *,void * arg ); // Callback for specific server's operations

typedef struct dap_server {

  dap_server_type_t type;                   // Server's type
  dap_list_t *es_listeners;

  void *_inheritor;

    dap_cpu_stats_t cpu_stats;

    dap_server_callback_t delete_callback;

    dap_events_socket_callbacks_t client_callbacks; // Callbacks for the new clients

    pthread_cond_t started_cond;                // Condition for initialized socket
    pthread_mutex_t started_mutex;              // Mutex for shared operation between mirrored sockets
    bool started;
} dap_server_t;

int dap_server_init( ); // Init server module
void  dap_server_deinit( void ); // Deinit server module

void dap_server_set_default(dap_server_t* a_server);
dap_server_t* dap_server_get_default();

dap_server_t* dap_server_new(char **a_addrs, uint16_t a_count, dap_server_type_t a_type, dap_events_socket_callbacks_t *a_callbacks);
int dap_server_listen_addr_add(dap_server_t *a_server, const char *a_addr, uint16_t a_port, dap_events_socket_callbacks_t *a_callbacks);

void dap_server_delete(dap_server_t *a_server);
