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
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#define s6_addr32 s6_addr
#define herror perror
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include "dap_events_socket.h"
#include "dap_net_common.h"
#include "dap_serialize.h"

#define DAP_CFG_PARAM_LISTEN_ADDRS      "listen-address"
#define DAP_CFG_PARAM_SOCK_PATH         "listen-path"
#define DAP_CFG_PARAM_SOCK_PERMISSIONS  "listen-unix-socket-permissions"
#define DAP_CFG_PARAM_LEGACY_PORT       "listen-port-tcp"
#define DAP_CFG_PARAM_WHITE_LIST        "white-list"
#define DAP_CFG_PARAM_BLACK_LIST        "black-list"

typedef struct dap_link_info {
    dap_cluster_node_addr_t node_addr;
    char uplink_addr[DAP_HOSTADDR_STRLEN];
    uint16_t uplink_port;
} DAP_ALIGN_PACKED dap_link_info_t;
DAP_STATIC_ASSERT(sizeof(dap_link_info_t) == sizeof(dap_cluster_node_addr_t) + DAP_HOSTADDR_STRLEN +
                      sizeof(uint16_t),
                  "dap_link_info_t packed wire size");

/** Wire size of @ref dap_link_info_t (packed). */
#define DAP_LINK_INFO_WIRE_SIZE sizeof(dap_link_info_t)

/**
 * @brief Byte-oriented in-memory view of @ref dap_link_info_t (matches wire layout without tail padding).
 */
typedef struct dap_link_info_mem {
    uint8_t node_addr[sizeof(dap_cluster_node_addr_t)];
    uint8_t uplink_addr[DAP_HOSTADDR_STRLEN];
    uint8_t uplink_port_wire[sizeof(uint16_t)];
} dap_link_info_mem_t;

_Static_assert(sizeof(dap_link_info_mem_t) == DAP_LINK_INFO_WIRE_SIZE,
               "dap_link_info_mem_t matches dap_link_info_t wire layout");
_Static_assert(sizeof(dap_link_info_mem_t) == sizeof(dap_link_info_t),
               "dap_link_info_mem_t matches packed dap_link_info_t");

#define DAP_LINK_INFO_SERIALIZE_MAGIC 0xDA5FEF00U

typedef struct dap_net_links {
    uint64_t count_node;
    byte_t nodes_info[];
} dap_net_links_t;
_Static_assert(sizeof(dap_net_links_t) == 8, "dap_net_links_t wire size");

typedef dap_net_links_t dap_net_links_mem_t;

#define DAP_NET_LINKS_MAGIC 0xDA5FEEDBU

#ifdef __cplusplus
extern "C" {
#endif

extern const dap_serialize_field_t g_dap_net_links_fields[];
extern const dap_serialize_schema_t g_dap_net_links_schema;

extern const dap_serialize_field_t g_dap_link_info_fields[];
extern const dap_serialize_schema_t g_dap_link_info_schema;

int dap_net_resolve_host(const char *a_host, const char *a_port, bool a_numeric_only, struct sockaddr_storage *a_addr_out, int *a_family);
int dap_net_parse_config_address(const char *a_src, char *a_addr, uint16_t *a_port, struct sockaddr_storage *a_saddr, int *a_family);
long dap_net_recv(SOCKET sd, unsigned char *buf, size_t bufsize, int timeout);

#ifdef __cplusplus
}
#endif

static inline int dap_link_info_pack(const dap_link_info_mem_t *a_mem, uint8_t *a_wire, size_t a_wire_size)
{
    if (!a_mem || !a_wire || a_wire_size < DAP_LINK_INFO_WIRE_SIZE)
        return -1;
    dap_serialize_result_t l_r =
        dap_serialize_to_buffer_raw(&g_dap_link_info_schema, a_mem, a_wire, a_wire_size, NULL);
    return l_r.error_code;
}

static inline int dap_link_info_unpack(const uint8_t *a_wire, size_t a_wire_size, dap_link_info_mem_t *a_mem)
{
    if (!a_wire || !a_mem || a_wire_size < DAP_LINK_INFO_WIRE_SIZE)
        return -1;
    dap_deserialize_result_t l_r =
        dap_deserialize_from_buffer_raw(&g_dap_link_info_schema, a_wire, a_wire_size, a_mem, NULL);
    return l_r.error_code;
}
