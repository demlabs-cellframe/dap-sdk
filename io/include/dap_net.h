/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2020
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

#ifdef WIN32
// for Windows
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>

#define s6_addr32 s6_addr
#define herror perror
#else
// for Unix-like systems
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include "dap_events_socket.h"

#define DAP_CFG_PARAM_LISTEN_ADDRS      "listen-address"
#define DAP_CFG_PARAM_SOCK_PATH         "listen-path"
#define DAP_CFG_PARAM_SOCK_PERMISSIONS  "listen-unix-socket-permissions"
#define DAP_CFG_PARAM_LEGACY_PORT       "listen-port-tcp"
#define DAP_CFG_PARAM_WHITE_LIST        "white-list"
#define DAP_CFG_PARAM_BLACK_LIST        "black-list"

typedef struct dap_link_info {
    dap_stream_node_addr_t node_addr;
    char uplink_addr[DAP_HOSTADDR_STRLEN];
    uint16_t uplink_port;
} DAP_ALIGN_PACKED dap_link_info_t;

typedef struct dap_net_links {
    uint64_t count_node;
    byte_t nodes_info[];
} DAP_ALIGN_PACKED dap_net_links_t;

#ifdef __cplusplus
extern "C" {
#endif

int dap_net_resolve_host(const char *a_host, const char *a_port, bool a_numeric_only, struct sockaddr_storage *a_addr_out, int *a_family);
int dap_net_parse_config_address(const char *a_src, char *a_addr, uint16_t *a_port, struct sockaddr_storage *a_saddr, int *a_family);
long dap_net_recv(SOCKET sd, unsigned char *buf, size_t bufsize, int timeout);

#ifdef __cplusplus
}
#endif
