/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * Roman Khlopkov <roman.khlopkov@demlabs.net>
 * Cellframe       https://cellframe.net
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.
 */

#include "dap_net_common.h"
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include "dap_common.h"
#include "dap_config.h"

#define LOG_TAG "dap_net_common"

static int s_stream_addrs_parser(struct dap_conf *a_cfg, const char *a_config, const char *a_section,
                                  void **a_out_data, uint16_t *a_out_count)
{
    dap_return_val_if_pass(!a_cfg || !a_config || !a_section || !a_out_data || !a_out_count, -1);

    const char **l_nodes_addrs = dap_config_get_array_str(a_cfg, a_config, a_section, a_out_count);
    if (*a_out_count) {
        log_it(L_DEBUG, "Start parse stream addrs in config %s section %s", a_config, a_section);
        dap_cluster_node_addr_t *l_addrs = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(dap_cluster_node_addr_t, *a_out_count, -2);

        for (uint16_t i = 0; i < *a_out_count; ++i) {
            if (dap_cluster_node_addr_from_str(&l_addrs[i], l_nodes_addrs[i])) {
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

int dap_net_common_init(void)
{
    log_it(L_INFO, "Initializing DAP Net Common module");
    int l_ret = dap_config_register_parser("stream_addrs", s_stream_addrs_parser);
    if (l_ret < 0) {
        log_it(L_ERROR, "Failed to register stream_addrs parser: %d", l_ret);
        return l_ret;
    }
    log_it(L_INFO, "DAP Net Common module initialized");
    return 0;
}

void dap_net_common_deinit(void)
{
    log_it(L_INFO, "Deinitializing DAP Net Common module");
}

int dap_net_common_parse_stream_addrs(void *a_cfg, const char *a_config, const char *a_section,
                                       dap_cluster_node_addr_t **a_addrs, uint16_t *a_addrs_count)
{
    return dap_config_call_parser("stream_addrs", a_cfg, a_config, a_section,
                                   (void**)a_addrs, a_addrs_count);
}
