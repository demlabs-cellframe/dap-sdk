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
#ifdef DAP_EVENTS_CAPS_WEPOLL
#include "wepoll.h"
#endif
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#if defined(DAP_OS_LINUX)
#include <sys/epoll.h>
#include <sys/timerfd.h>
#elif defined (DAP_OS_BSD)
#include <sys/event.h>
#endif
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
#include "dap_file_utils.h"

#define LOG_TAG "dap_server"

bool s_server_enabled = false;

static void s_es_server_new     (dap_events_socket_t *a_es, void *a_arg);
static void s_es_server_accept  (dap_events_socket_t *a_es_listener, SOCKET a_remote_socket, struct sockaddr_storage *a_remote_addr);
static void s_es_server_error   (dap_events_socket_t *a_es, int a_arg);

static dap_server_t* s_default_server = NULL;

/**
 * @brief dap_server_init
 * @return
 */
int dap_server_init()
{
    s_server_enabled = dap_config_get_item_bool_default(g_config, "server", "enabled", false);
    return debug_if(s_server_enabled, L_INFO, "Server module initialized"), 0;
}

bool dap_server_enabled() {
    return s_server_enabled;
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
 * @brief add listen addr to server
 * @param a_server - server to add addr
 * @param a_addr - addr or path to local
 * @param a_port - port or read mode
 * @param a_callbacks - pointer to callbacks
 * @return
 */
int dap_server_listen_addr_add( dap_server_t *a_server, const char *a_addr, uint16_t a_port, 
                                dap_events_desc_type_t a_type, dap_events_socket_callbacks_t *a_callbacks )
{
    dap_return_val_if_fail_err(a_server && a_addr, -1, "Invalid argument");
    struct sockaddr_storage l_saddr = { };
    int l_fam, l_len = 0;
    SOCKET l_socket = INVALID_SOCKET;
    switch (a_type) {
    case DESCRIPTOR_TYPE_SOCKET_LISTENING: case DESCRIPTOR_TYPE_SOCKET_UDP:
        l_len = dap_net_resolve_host(a_addr, dap_itoa(a_port), true, &l_saddr, &l_fam);
        break;
    case DESCRIPTOR_TYPE_SOCKET_LOCAL_LISTENING:
#if defined DAP_OS_LINUX || defined DAP_OS_DARWIN
    {
        char *l_dir = dap_path_get_dirname(a_addr);
        dap_mkdir_with_parents(l_dir);
        int a = access(l_dir, W_OK|R_OK);
        DAP_DELETE(l_dir);
        if (a == -1) {
            log_it(L_ERROR, "Path %s is unavailable", a_addr);
            l_fam = AF_UNSPEC;
            break;
        }
        unlink(a_addr);
        struct sockaddr_un l_unaddr = { .sun_family = AF_UNIX };
        dap_strncpy(l_unaddr.sun_path, a_addr, sizeof(l_unaddr.sun_path) - 1);
        l_len = SUN_LEN(&l_unaddr);
        memcpy(&l_saddr, &l_unaddr, sizeof(l_unaddr));
        l_fam = AF_UNIX;
    } break;
#else
        log_it(L_ERROR, "Can't use UNIX socket on this platform");
        return 1;
#endif
    default: // TODO implement pipes (file-based on Windows)
        l_fam = AF_UNSPEC;
        break;
    }

    switch (l_fam) {
    case AF_INET: case AF_INET6: case AF_UNIX:
        l_socket = socket(l_fam, a_type == DESCRIPTOR_TYPE_SOCKET_UDP ? SOCK_DGRAM : SOCK_STREAM, 0);
    break;
    default:
        log_it(L_ERROR, "Can't resolve address \"%s : %d\" and add it to server!", a_addr, a_port);
        return 2;
    }
#ifdef DAP_OS_WINDOWS
    _set_errno(WSAGetLastError());
#endif
    if (l_socket < 0) {
        log_it (L_ERROR,"Socket error %d: \"%s\"", errno, dap_strerror(errno));
        return 3;
    }
    log_it(L_INFO, "Created socket %"DAP_FORMAT_SOCKET" [%s : %d] ", l_socket, a_addr, a_port);

#ifdef DAP_OS_WINDOWS
#define close_socket_due_to_fail(fun) _set_errno(WSAGetLastError()); \
                                      log_it(L_ERROR, fun " failed, errno %d: \"%s\"", \
                                                      errno, dap_strerror(errno)); \
                                      closesocket(l_socket);
#else
#define close_socket_due_to_fail(fun) log_it(L_ERROR, fun " failed, errno %d: \"%s\"", \
                                                      errno, dap_strerror(errno)); \
                                      close(l_socket);
#endif
    u_long l_option = 1;
    if ( setsockopt(l_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&l_option, sizeof(int)) < 0 ) {
        close_socket_due_to_fail("setsockopt(SO_REUSEADDR)");
        return 4;
    }

#ifdef SO_REUSEPORT
    l_option = 1;
    if ( setsockopt(l_socket, SOL_SOCKET, SO_REUSEPORT, (const char*)&l_option, sizeof(int)) < 0 )
        debug_if(a_server->ext_log, L_INFO, "setsockopt(SO_REUSEPORT) is not supported");
#endif

    if ( bind(l_socket, (struct sockaddr*)&l_saddr, l_len) < 0 ) {
        close_socket_due_to_fail("bind()");
        return 6;
    }
    log_it(L_INFO, "Socket %d \"%s : %d\" binded", l_socket, a_addr, a_port);

    if (a_type != DESCRIPTOR_TYPE_SOCKET_UDP) {
        if ( listen(l_socket, SOMAXCONN) < 0 ) {
            close_socket_due_to_fail("listen()");
            return 5;
        }
    }
#undef close_socket_due_to_fail
#ifdef DAP_OS_WINDOWS
    l_option = 1;
    ioctlsocket(l_socket, (long)FIONBIO, &l_option);
#else
    fcntl(l_socket, F_SETFL, O_NONBLOCK);
#endif
    dap_events_socket_t *l_es = dap_events_socket_wrap_listener(a_server, l_socket, a_callbacks);
#ifdef DAP_EVENTS_CAPS_EPOLL
    l_es->ev_base_flags = EPOLLIN;
#ifdef EPOLLEXCLUSIVE
    l_es->ev_base_flags |= EPOLLET | EPOLLEXCLUSIVE;
#endif
#endif
    dap_strncpy(l_es->listener_addr_str, a_addr, INET6_ADDRSTRLEN);
    l_es->listener_port = a_port;
    l_es->addr_storage = l_saddr;
    l_es->type = a_type;
    l_es->no_close = true;
    a_server->es_listeners = dap_list_prepend(a_server->es_listeners, l_es);
    return dap_worker_add_events_socket_auto(l_es) ? 0 : -1;
}

int dap_server_callbacks_set(dap_server_t* a_server, dap_events_socket_callbacks_t *a_server_cbs, dap_events_socket_callbacks_t *a_client_cbs) {
    //TODO
    return 0;
}

/**
 * @brief dap_server_new
 * @param a_events
 * @param a_addr
 * @param a_port
 * @param a_type
 * @return
 */
dap_server_t *dap_server_new(const char *a_cfg_section, dap_events_socket_callbacks_t *a_server_callbacks, dap_events_socket_callbacks_t *a_client_callbacks)
{
    dap_server_t *l_server = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_server_t, NULL);
    dap_events_socket_callbacks_t l_callbacks = {
        .accept_callback = s_es_server_accept,
        .new_callback    = s_es_server_new,
        .read_callback   = a_server_callbacks ? a_server_callbacks->read_callback   : NULL,
        .write_callback  = a_server_callbacks ? a_server_callbacks->write_callback  : NULL,
        .error_callback  = s_es_server_error
    };
    if (a_client_callbacks)
        l_server->client_callbacks = *a_client_callbacks;
    if (a_cfg_section) {
        uint16_t l_count = 0, i;
#if defined DAP_OS_LINUX || defined DAP_OS_DARWIN
        char **l_paths = dap_config_get_item_str_path_array(g_config, a_cfg_section, DAP_CFG_PARAM_SOCK_PATH, &l_count);
        mode_t l_mode = 0666;
            //strtol( dap_config_get_item_str_default(g_config, a_cfg_section, DAP_CFG_PARAM_SOCK_PERMISSIONS, "0666"), NULL, 8 );
        for (i = 0; i < l_count; ++i) {
            if ( dap_server_listen_addr_add(l_server, l_paths[i], l_mode, DESCRIPTOR_TYPE_SOCKET_LOCAL_LISTENING, &l_callbacks) )
                log_it(L_ERROR, "Can't add path \"%s\" to server", l_paths[i]);
            else
                if ( 0 > chmod(l_paths[i], l_mode) )
                    log_it(L_ERROR, "chmod() on socket path failed, errno %d: \"%s\"",
                                    errno, dap_strerror(errno));
        }
        dap_config_get_item_str_path_array_free(l_paths, l_count);
#endif
        const char **l_addrs = dap_config_get_array_str(g_config, a_cfg_section, DAP_CFG_PARAM_LISTEN_ADDRS, &l_count);
        for (i = 0; i < l_count; ++i) {
            char l_cur_ip[INET6_ADDRSTRLEN] = { '\0' }; uint16_t l_cur_port = 0;
            if ( 0 > dap_net_parse_config_address( l_addrs[i], l_cur_ip, &l_cur_port, NULL, NULL) ) {
                log_it( L_ERROR, "Incorrect format of address \"%s\", fix [server] section in cellframe-node.cfg and restart",
                                 l_addrs[i] );
                continue;
            }
            if ( !l_cur_port ) // Probably need old format
                l_cur_port = dap_config_get_item_int16(g_config, a_cfg_section, DAP_CFG_PARAM_LEGACY_PORT);
            if ( dap_server_listen_addr_add(l_server, l_cur_ip, l_cur_port, DESCRIPTOR_TYPE_SOCKET_LISTENING, &l_callbacks) )
                log_it( L_ERROR, "Can't add address \"%s : %u\" to listen in server", l_cur_ip, l_cur_port);
        }

        l_server->whitelist = dap_config_get_array_str(g_config, a_cfg_section, DAP_CFG_PARAM_WHITE_LIST, NULL);
        l_server->blacklist = dap_config_get_array_str(g_config, a_cfg_section, DAP_CFG_PARAM_BLACK_LIST, NULL);

        if (l_server->whitelist && l_server->blacklist) {
            log_it(L_CRITICAL, "Server can't have both black- and whitelists, fix section [%s]", a_cfg_section);
            l_server->whitelist = NULL; /* Blacklist will have priority */
        }
    }
    if (!l_server->es_listeners) {
        log_it(L_INFO, "Server with no listeners created. "
                       "You may add them later with dap_server_listen_addr_add()");
    }
    l_server->ext_log = dap_config_get_item_bool_default(g_config, a_cfg_section, "debug-more", false);
    return l_server;
}

/**
 * @brief s_es_server_new
 * @param a_es
 * @param a_arg
 */
static void s_es_server_new(dap_events_socket_t *a_es, void * a_arg)
{
    log_it(L_DEBUG, "Created server socket %d with uuid "DAP_FORMAT_ESOCKET_UUID" on worker %u", a_es->socket, a_es->uuid, a_es->worker->id);
}

/**
 * @brief s_es_server_error
 * @param a_es
 * @param a_arg
 */
static void s_es_server_error(dap_events_socket_t *a_es, int a_errno)
{
    log_it(L_WARNING, "Server socket %d error %d: %s", a_es->socket, a_errno, dap_strerror(a_errno));
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

    dap_events_socket_t *l_es_new = NULL;
    debug_if(l_server->ext_log, L_DEBUG, "Listening socket %"DAP_FORMAT_SOCKET" uuid "DAP_FORMAT_ESOCKET_UUID" binded on %s:%u "
                                         "accepted new connection from remote %"DAP_FORMAT_SOCKET"",
                                         a_es_listener->socket, a_es_listener->uuid,
                                         a_es_listener->listener_addr_str, a_es_listener->listener_port, a_remote_socket);
    if (a_remote_socket < 0) {
#ifdef DAP_OS_WINDOWS
        _set_errno(WSAGetLastError());
#endif
        log_it(L_ERROR, "Server socket %d accept() error %d: %s",
                        a_es_listener->socket, errno, dap_strerror(errno));
        return;
    }
    char l_remote_addr_str[INET6_ADDRSTRLEN] = "", l_port_str[NI_MAXSERV] = "";

    dap_events_desc_type_t l_es_type = DESCRIPTOR_TYPE_SOCKET_CLIENT;
    switch (a_remote_addr->ss_family) {
#ifdef DAP_OS_UNIX
    case AF_UNIX:
        l_es_type = DESCRIPTOR_TYPE_SOCKET_LOCAL_CLIENT;
        debug_if(l_server->ext_log, L_INFO, "Connection accepted at \"%s\", socket %"DAP_FORMAT_SOCKET,
                                            a_es_listener->remote_addr_str, a_remote_socket);
        break;
#endif
    case AF_INET:
    case AF_INET6:
        if ( getnameinfo((struct sockaddr*)a_remote_addr, sizeof(*a_remote_addr), 
                         l_remote_addr_str, sizeof(l_remote_addr_str),
                         l_port_str, sizeof(l_port_str), NI_NUMERICHOST | NI_NUMERICSERV) )
        {
#ifdef DAP_OS_WINDOWS
            _set_errno(WSAGetLastError());
#endif
            log_it(L_ERROR, "getnameinfo() error %d: %s", errno, dap_strerror(errno));
            closesocket(a_remote_socket);
            return;
        }
        if (( l_server->whitelist
            ? !dap_str_find(l_server->whitelist, l_remote_addr_str) 
            : !!dap_str_find(l_server->blacklist, l_remote_addr_str) )) {
                closesocket(a_remote_socket);
                return debug_if(l_server->ext_log, L_INFO, "Connection from %s : %s denied. Dump it",
                                l_remote_addr_str, l_port_str);
            }
                
        debug_if(l_server->ext_log, L_INFO, "Connection accepted from %s : %s, socket %"DAP_FORMAT_SOCKET,
                                            l_remote_addr_str, l_port_str, a_remote_socket);
        int one = 1;
        if ( setsockopt(a_remote_socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one)) < 0 )
            log_it(L_WARNING, "Can't disable Nagle alg, error %d: %s", errno, dap_strerror(errno));
        break;
    default:
        closesocket(a_remote_socket);
        return log_it(L_ERROR, "Unsupported protocol family %hu from accept()", a_remote_addr->ss_family);
    }
    l_es_new = dap_events_socket_wrap_no_add(a_remote_socket, &l_server->client_callbacks);
    l_es_new->server = l_server;
    l_es_new->type = l_es_type;
    l_es_new->addr_storage = *a_remote_addr;
    l_es_new->remote_port = strtol(l_port_str, NULL, 10);
    dap_strncpy(l_es_new->remote_addr_str, l_remote_addr_str, sizeof(INET6_ADDRSTRLEN));
    dap_worker_add_events_socket( dap_events_worker_get_auto(), l_es_new );
}

/**
 * @brief Delete server
 * @param a_server
 */
void dap_server_delete(dap_server_t *a_server)
{
    dap_return_if_pass(!a_server);
    while (a_server->es_listeners) {
        dap_events_socket_t *l_es = (dap_events_socket_t *)a_server->es_listeners->data;
        dap_events_socket_remove_and_delete(l_es->worker, l_es->uuid); // TODO unsafe moment. Replace storage to uuids
        dap_list_t *l_tmp = a_server->es_listeners;
        a_server->es_listeners = l_tmp->next;
        DAP_DELETE(l_tmp);
    }
    if(a_server->delete_callback)
        a_server->delete_callback(a_server,NULL);

    //DAP_DELETE(a_server->_inheritor);
    DAP_DELETE(a_server);
}
