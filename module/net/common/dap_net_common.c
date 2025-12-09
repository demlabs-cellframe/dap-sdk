/*
* Authors:
* Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
* Roman Khlopkov <roman.khlopkov@demlabs.net>
* Cellframe       https://cellframe.net
* DeM Labs Inc.   https://demlabs.net
* Copyright  (c) 2017-2023
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

#include "dap_net_common.h"
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include "dap_common.h"
#include "dap_config.h"

#define LOG_TAG "dap_net_common"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert stream node address to static string representation
 * @param a_address Node address
 * @return Static string with node address
 */
dap_node_addr_str_t dap_stream_node_addr_to_str_static_(dap_stream_node_addr_t a_address)
{
    dap_node_addr_str_t l_ret = { };
    snprintf((char*)&l_ret, sizeof(l_ret), NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS_S(a_address));
    return l_ret;
}

/**
 * @brief Parse stream node address from string
 * @param a_addr Output node address
 * @param a_addr_str Input string
 * @return 0 on success, negative on error
 */
int dap_stream_node_addr_from_str(dap_stream_node_addr_t *a_addr, const char *a_addr_str)
{
    if (!a_addr || !a_addr_str)
        return -2;
    bool l_res = true;
    size_t l_len = 0;
    for (; l_res && *(a_addr_str + l_len) && l_len < 23; l_len++) {
        l_res &= *(a_addr_str + l_len) == ':' || isxdigit(*(a_addr_str + l_len));
    }
    l_res &= l_len == 18 || l_len == 22;
    return l_res ? (sscanf(a_addr_str, NODE_ADDR_FP_STR, NODE_ADDR_FPS_ARGS(a_addr)) == 4
        || sscanf(a_addr_str, "0x%016" DAP_UINT64_FORMAT_x, (uint64_t*)a_addr) == 1
        ? 0 : -1) : -4;
}

/**
 * @brief Internal parser for stream node addresses (registered as custom parser)
 * @param a_cfg Config object
 * @param a_config Config name
 * @param a_section Section name
 * @param a_out_data Output pointer (will be cast to dap_stream_node_addr_t**)
 * @param a_out_count Output count of addresses
 * @return 0 on success, negative on error
 */
static int s_stream_addrs_parser(struct dap_conf *a_cfg, const char *a_config, const char *a_section, 
                                  void **a_out_data, uint16_t *a_out_count)
{
    dap_return_val_if_pass(!a_cfg || !a_config || !a_section || !a_out_data || !a_out_count, -1);
    
    const char **l_nodes_addrs = dap_config_get_array_str(a_cfg, a_config, a_section, a_out_count);
    if (*a_out_count) {
        log_it(L_DEBUG, "Start parse stream addrs in config %s section %s", a_config, a_section);
        dap_stream_node_addr_t *l_addrs = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(dap_stream_node_addr_t, *a_out_count, -2);
        
        for (uint16_t i = 0; i < *a_out_count; ++i) {
            if (dap_stream_node_addr_from_str(&l_addrs[i], l_nodes_addrs[i])) {
                log_it(L_ERROR, "Incorrect format of %s address \"%s\", fix net config and restart node", 
                       a_section, l_nodes_addrs[i]);
                DAP_DELETE(l_addrs);
                return -3;
            }
            log_it(L_DEBUG, "Stream addr " NODE_ADDR_FP_STR " parsed successfully", 
                   NODE_ADDR_FP_ARGS_S(l_addrs[i]));
        }
        *a_out_data = l_addrs;
    }
    return 0;
}

/**
 * @brief Initialize network common module (registers config parsers)
 * @return 0 on success, negative on error
 */
int dap_net_common_init(void)
{
    log_it(L_INFO, "Initializing DAP Net Common module");
    
    // Register stream addresses parser
    int l_ret = dap_config_register_parser("stream_addrs", s_stream_addrs_parser);
    if (l_ret < 0) {
        log_it(L_ERROR, "Failed to register stream_addrs parser: %d", l_ret);
        return l_ret;
    }
    
    log_it(L_INFO, "DAP Net Common module initialized");
    return 0;
}

/**
 * @brief Deinitialize network common module
 */
void dap_net_common_deinit(void)
{
    log_it(L_INFO, "Deinitializing DAP Net Common module");
    // Parsers are cleaned up by dap_config_deinit()
}

/**
 * @brief Parse stream node addresses from config file (convenience wrapper)
 * @param a_cfg Config object
 * @param a_config Config name
 * @param a_section Section name
 * @param a_addrs Output array of addresses (will be allocated)
 * @param a_addrs_count Output count of addresses
 * @return 0 on success, negative on error
 */
int dap_net_common_parse_stream_addrs(void *a_cfg, const char *a_config, const char *a_section, 
                                       dap_stream_node_addr_t **a_addrs, uint16_t *a_addrs_count)
{
    return dap_config_call_parser("stream_addrs", a_cfg, a_config, a_section, 
                                   (void**)a_addrs, a_addrs_count);
}

// ========================================================================
// Cluster callbacks registry
// ========================================================================
static dap_cluster_callbacks_t s_cluster_callbacks[DAP_CLUSTER_TYPE_VIRTUAL + 1] = { 0 };

int dap_cluster_callbacks_register(dap_cluster_type_t a_cluster_type,
                                   dap_cluster_member_add_callback_t a_add_cb,
                                   dap_cluster_member_delete_callback_t a_del_cb,
                                   void *a_arg)
{
    if (a_cluster_type <= DAP_CLUSTER_TYPE_INVALID || a_cluster_type > DAP_CLUSTER_TYPE_VIRTUAL)
        return -1;
    s_cluster_callbacks[a_cluster_type].add_callback = a_add_cb;
    s_cluster_callbacks[a_cluster_type].delete_callback = a_del_cb;
    s_cluster_callbacks[a_cluster_type].arg = a_arg;
    return 0;
}

dap_cluster_callbacks_t* dap_cluster_callbacks_get(dap_cluster_type_t a_cluster_type)
{
    if (a_cluster_type <= DAP_CLUSTER_TYPE_INVALID || a_cluster_type > DAP_CLUSTER_TYPE_VIRTUAL)
        return NULL;
    return &s_cluster_callbacks[a_cluster_type];
}

#ifdef __cplusplus
}
#endif
