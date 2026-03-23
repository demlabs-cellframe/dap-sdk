/*
 * Authors:
 * Roman Khlopkov <roman.khlopkov@demlabs.net>
 * Cellframe       https://cellframe.net
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * DAP Cluster — node grouping, membership and broadcast.
 * Designed for future extraction into a standalone library.
 */

#pragma once

#include <stdint.h>
#include <pthread.h>
#include "dap_common.h"
#include "dap_ht.h"
#include "dap_list.h"
#include "dap_guuid.h"
#include "dap_json.h"
#include "dap_cluster_node.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DAP_CLUSTER_GLOBAL   "global"
#define DAP_CLUSTER_LOCAL    "local"

typedef enum dap_cluster_type {
    DAP_CLUSTER_TYPE_INVALID = 0,
    DAP_CLUSTER_TYPE_EMBEDDED,
    DAP_CLUSTER_TYPE_AUTONOMIC,
    DAP_CLUSTER_TYPE_ISOLATED,
    DAP_CLUSTER_TYPE_SYSTEM,
    DAP_CLUSTER_TYPE_VIRTUAL
} dap_cluster_type_t;

typedef enum dap_cluster_status {
    DAP_CLUSTER_STATUS_DISABLED = 0,
    DAP_CLUSTER_STATUS_ENABLED
} dap_cluster_status_t;

typedef struct dap_cluster dap_cluster_t;
typedef struct dap_cluster_member dap_cluster_member_t;

typedef void (*dap_cluster_change_callback_t)(dap_cluster_member_t *a_member, void *a_arg);
typedef dap_cluster_change_callback_t dap_cluster_member_add_callback_t;
typedef dap_cluster_change_callback_t dap_cluster_member_delete_callback_t;

typedef struct dap_cluster_callbacks {
    dap_cluster_member_add_callback_t add_callback;
    dap_cluster_member_delete_callback_t delete_callback;
    void *arg;
} dap_cluster_callbacks_t;

typedef struct dap_cluster_member {
    dap_cluster_node_addr_t addr;
    int role;
    bool persistent;
    void *info;
    dap_cluster_t *cluster;
    dap_ht_handle_t hh;
} dap_cluster_member_t;

typedef struct dap_cluster {
    char *mnemonim;
    dap_guuid_t guuid;
    dap_cluster_type_t type;
    dap_cluster_status_t status;
    pthread_rwlock_t members_lock;
    dap_cluster_member_t *members;
    dap_cluster_change_callback_t members_add_callback;
    dap_cluster_change_callback_t members_delete_callback;
    void *callbacks_arg;
    void *_inheritor;
    dap_ht_handle_t hh, hh_str;
} dap_cluster_t;

int dap_cluster_callbacks_register(dap_cluster_type_t a_type,
                                    dap_cluster_member_add_callback_t a_add_cb,
                                    dap_cluster_member_delete_callback_t a_del_cb,
                                    void *a_arg);
dap_cluster_callbacks_t *dap_cluster_callbacks_get(dap_cluster_type_t a_type);

int dap_cluster_init(void);

dap_cluster_t *dap_cluster_new(const char *a_mnemonim, dap_guuid_t a_guuid, dap_cluster_type_t a_type);
void dap_cluster_delete(dap_cluster_t *a_cluster);
dap_cluster_t *dap_cluster_find(dap_guuid_t a_uuid);
dap_cluster_t *dap_cluster_by_mnemonim(const char *a_mnemonim);

dap_cluster_member_t *dap_cluster_member_add(dap_cluster_t *a_cluster, dap_cluster_node_addr_t *a_addr, int a_role, void *a_info);
dap_cluster_member_t *dap_cluster_member_find_unsafe(dap_cluster_t *a_cluster, dap_cluster_node_addr_t *a_member_addr);
int dap_cluster_member_find_role(dap_cluster_t *a_cluster, dap_cluster_node_addr_t *a_member_addr);
size_t dap_cluster_members_count(dap_cluster_t *a_cluster);
DAP_STATIC_INLINE bool dap_cluster_is_empty(dap_cluster_t *a_cluster) { return !dap_cluster_members_count(a_cluster); }
int dap_cluster_member_delete(dap_cluster_t *a_cluster, dap_cluster_node_addr_t *a_member_addr);
void dap_cluster_delete_all_members(dap_cluster_t *a_cluster);
void dap_cluster_broadcast(dap_cluster_t *a_cluster, const char a_ch_id, uint8_t a_type, const void *a_data, size_t a_data_size,
                           dap_cluster_node_addr_t *a_exclude_aray, size_t a_exclude_array_size);
dap_json_t *dap_cluster_get_links_info_json(dap_cluster_t *a_cluster);
char *dap_cluster_get_links_info(dap_cluster_t *a_cluster);
void dap_cluster_link_delete_from_all(dap_list_t *a_cluster_list, dap_cluster_node_addr_t *a_addr);
dap_cluster_node_addr_t dap_cluster_get_random_link(dap_cluster_t *a_cluster);
dap_cluster_node_addr_t *dap_cluster_get_all_members_addrs(dap_cluster_t *a_cluster, size_t *a_count, int a_role);
void dap_cluster_members_register(dap_cluster_t *a_cluster);

#ifdef __cplusplus
}
#endif
