/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * Cellframe       https://cellframe.net
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * DAP Cluster Node Address — fundamental node identity type.
 * Designed for future extraction into a standalone library.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_hash_compat.h"

typedef struct dap_sign dap_sign_t;
typedef struct dap_cert dap_cert_t;
typedef struct dap_pkey dap_pkey_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef union dap_cluster_node_addr {
    uint64_t uint64;
    uint16_t words[sizeof(uint64_t) / 2];
    uint8_t raw[sizeof(uint64_t)];
} DAP_ALIGN_PACKED dap_cluster_node_addr_t;

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define NODE_ADDR_FP_STR        "%04hX::%04hX::%04hX::%04hX"
#define NODE_ADDR_FP_ARGS(a)    (a)->words[2],(a)->words[3],(a)->words[0],(a)->words[1]
#define NODE_ADDR_FPS_ARGS(a)   &(a)->words[2],&(a)->words[3],&(a)->words[0],&(a)->words[1]
#define NODE_ADDR_FP_ARGS_S(a)  (a).words[2],(a).words[3],(a).words[0],(a).words[1]
#define NODE_ADDR_FPS_ARGS_S(a) &(a).words[2],&(a).words[3],&(a).words[0],&(a).words[1]
#else
#define NODE_ADDR_FP_STR        "%04hX::%04hX::%04hX::%04hX"
#define NODE_ADDR_FP_ARGS(a)    (a)->words[3],(a)->words[2],(a)->words[1],(a)->words[0]
#define NODE_ADDR_FPS_ARGS(a)   &(a)->words[3],&(a)->words[2],&(a)->words[1],&(a)->words[0]
#define NODE_ADDR_FP_ARGS_S(a)  (a).words[3],(a).words[2],(a).words[1],(a).words[0]
#define NODE_ADDR_FPS_ARGS_S(a) &(a).words[3],&(a).words[2],&(a).words[1],&(a).words[0]
#endif

static inline bool dap_cluster_node_addr_is_blank(const dap_cluster_node_addr_t *a_addr) {
    return !a_addr || !a_addr->uint64;
}

#define DAP_NODE_ADDR_LEN 23
typedef union dap_node_addr_str {
    char s[DAP_NODE_ADDR_LEN];
} dap_node_addr_str_t;

int dap_cluster_node_addr_from_str(dap_cluster_node_addr_t *a_addr, const char *a_str);
dap_node_addr_str_t dap_cluster_node_addr_to_str_(dap_cluster_node_addr_t a_addr);
#define dap_cluster_node_addr_to_str(a) dap_cluster_node_addr_to_str_(a).s

DAP_STATIC_INLINE bool dap_cluster_node_addr_str_check(const char *a_addr_str)
{
    if (!a_addr_str)
        return false;
    size_t l_str_len = strlen(a_addr_str);
    if (l_str_len == 22) {
        for (int n =0; n < 22; n+= 6) {
            if (!dap_is_xdigit(a_addr_str[n]) || !dap_is_xdigit(a_addr_str[n + 1]) ||
                !dap_is_xdigit(a_addr_str[n + 2]) || !dap_is_xdigit(a_addr_str[n + 3])) {
                return false;
            }
        }
        for (int n = 4; n < 18; n += 6) {
            if (a_addr_str[n] != ':' || a_addr_str[n + 1] != ':')
                return false;
        }
        return true;
    }
    return false;
}

DAP_STATIC_INLINE char* dap_cluster_node_addr_to_str_alloc(dap_cluster_node_addr_t a_addr, bool a_hex)
{
    if (a_hex)
        return dap_strdup_printf("0x%016" DAP_UINT64_FORMAT_x, a_addr.uint64);
    return dap_strdup_printf(NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS_S(a_addr));
}

DAP_STATIC_INLINE void dap_cluster_node_addr_from_hash(dap_hash_fast_t *a_hash, dap_cluster_node_addr_t *a_node_addr)
{
    a_node_addr->words[3] = *(uint16_t *)a_hash->raw;
    a_node_addr->words[2] = *(uint16_t *)(a_hash->raw + sizeof(uint16_t));
    a_node_addr->words[1] = *(uint16_t *)(a_hash->raw + DAP_CHAIN_HASH_FAST_SIZE - sizeof(uint16_t) * 2);
    a_node_addr->words[0] = *(uint16_t *)(a_hash->raw + DAP_CHAIN_HASH_FAST_SIZE - sizeof(uint16_t));
}

extern dap_cluster_node_addr_t g_node_addr;

dap_cluster_node_addr_t dap_cluster_node_addr_from_sign(dap_sign_t *a_sign);
dap_cluster_node_addr_t dap_cluster_node_addr_from_cert(dap_cert_t *a_cert);
dap_cluster_node_addr_t dap_cluster_node_addr_from_pkey(dap_pkey_t *a_pkey);

#ifdef __cplusplus
}
#endif
