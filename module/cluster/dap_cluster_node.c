/*
 * DAP Cluster Node Address — implementation
 */

#include <stdio.h>
#include <ctype.h>
#include "dap_cluster_node.h"
#include "dap_sign.h"
#include "dap_cert.h"
#include "dap_pkey.h"

#define LOG_TAG "dap_cluster_node"

dap_cluster_node_addr_t g_node_addr = {};

dap_node_addr_str_t dap_cluster_node_addr_to_str_(dap_cluster_node_addr_t a_addr)
{
    dap_node_addr_str_t l_ret = { };
    snprintf((char *)&l_ret, sizeof(l_ret),
             NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS_S(a_addr));
    return l_ret;
}

int dap_cluster_node_addr_from_str(dap_cluster_node_addr_t *a_addr, const char *a_str)
{
    if (!a_addr || !a_str)
        return -2;
    bool l_ok = true;
    size_t l_len = 0;
    for (; l_ok && *(a_str + l_len) && l_len < 23; l_len++)
        l_ok &= *(a_str + l_len) == ':' || isxdigit(*(a_str + l_len));
    l_ok &= l_len == 18 || l_len == 22;
    return l_ok
        ? (sscanf(a_str, NODE_ADDR_FP_STR, NODE_ADDR_FPS_ARGS(a_addr)) == 4
           || sscanf(a_str, "0x%016" DAP_UINT64_FORMAT_x, (uint64_t *)a_addr) == 1
           ? 0 : -1)
        : -4;
}

dap_cluster_node_addr_t dap_cluster_node_addr_from_sign(dap_sign_t *a_sign)
{
    dap_cluster_node_addr_t l_ret = { };
    dap_return_val_if_pass(!a_sign, l_ret);

    dap_hash_fast_t l_node_addr_hash;
    if ( dap_sign_get_pkey_hash(a_sign, &l_node_addr_hash) )
        dap_cluster_node_addr_from_hash(&l_node_addr_hash, &l_ret);
    return l_ret;
}

dap_cluster_node_addr_t dap_cluster_node_addr_from_cert(dap_cert_t *a_cert)
{
    dap_cluster_node_addr_t l_ret = { };
    dap_return_val_if_pass(!a_cert, l_ret);

    dap_hash_fast_t l_node_addr_hash;
    if ( dap_cert_get_pkey_hash(a_cert, DAP_HASH_TYPE_SHA3_256, (byte_t *)&l_node_addr_hash, sizeof(l_node_addr_hash)) == 0 )
        dap_cluster_node_addr_from_hash(&l_node_addr_hash, &l_ret);
    return l_ret;
}

dap_cluster_node_addr_t dap_cluster_node_addr_from_pkey(dap_pkey_t *a_pkey)
{
    dap_cluster_node_addr_t l_ret = { };
    dap_return_val_if_pass(!a_pkey, l_ret);

    dap_hash_fast_t l_node_addr_hash;
    if ( dap_pkey_get_hash(a_pkey, &l_node_addr_hash) )
        dap_cluster_node_addr_from_hash(&l_node_addr_hash, &l_ret);
    return l_ret;
}
