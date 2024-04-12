#pragma once

#include "dap_common.h"
#include "dap_hash.h"
#include "dap_global_db_driver.h"
#include "dap_global_db.h"

#define F_DB_LOG_ADD_EXTRA_GROUPS   1
#define F_DB_LOG_SYNC_FROM_ZERO     2

#define GROUP_LOCAL_NODE_LAST_ID    "local.node.last_id"
#define GROUP_LOCAL_NODE_ADDR       "local.node-addr"


#define GDB_SYNC_ALWAYS_FROM_ZERO       // For debug purposes
// for dap_db_log_list_xxx()

#define DAP_DB_LOG_LIST_MAX_SIZE 0xfffff

typedef void (*dap_store_obj_callback_notify_t) (dap_global_db_context_t *a_context, dap_store_obj_t *a_obj, void * a_arg);

// Callback table item
typedef struct dap_sync_group_item {
    char *group_mask;
    char *net_name;
} dap_sync_group_item_t;

// New cluster architecture w/o sync groups
typedef struct dap_global_db_notify_item {
    char *group_mask;
    dap_store_obj_callback_notify_t callback_notify;
    void *callback_arg;
    uint64_t ttl;
} dap_global_db_notify_item_t;

typedef struct dap_global_db_pkt {
    dap_nanotime_t timestamp;
    uint64_t data_size;
    uint32_t obj_count;
    uint8_t data[];
} DAP_ALIGN_PACKED dap_global_db_pkt_t;

typedef struct dap_db_log_list_group {
    char *name;
    uint64_t last_id_synced;
    uint64_t count;
} dap_db_log_list_group_t;

typedef struct dap_db_log_list_obj {
    dap_global_db_pkt_t *pkt;
    dap_hash_fast_t hash;
} dap_db_log_list_obj_t;

typedef struct dap_db_log_list {
    dap_list_t *items_list;
    _Atomic(bool) is_process;
    _Atomic(size_t) items_number, items_rest;
    dap_list_t *groups;
    size_t size;
    pthread_cond_t cond;
    pthread_t thread;
    pthread_mutex_t list_mutex;
    dap_global_db_context_t *db_context;
} dap_db_log_list_t;

void dap_global_db_sync_init();
void dap_global_db_sync_deinit();

DAP_STATIC_INLINE size_t dap_db_log_list_obj_get_size(dap_db_log_list_obj_t *a_obj)
{
    return sizeof(dap_db_log_list_obj_t) + sizeof(dap_global_db_pkt_t) + a_obj->pkt->data_size;
}

/**
 * Setup callbacks and filters
 */
// Add group name that will be synchronized
void dap_global_db_add_sync_group(const char *a_net_name, const char *a_group_mask, dap_store_obj_callback_notify_t a_callback, void *a_arg);
void dap_global_db_add_sync_extra_group(const char *a_net_name, const char *a_group_mask, dap_store_obj_callback_notify_t a_callback, void *a_arg);
dap_list_t *dap_chain_db_get_sync_groups(const char *a_net_name);
dap_list_t *dap_chain_db_get_sync_extra_groups(const char *a_net_name);
dap_list_t * dap_global_db_get_sync_groups_all();
dap_list_t * dap_global_db_get_sync_groups_extra_all();

// Notificated groups. Automaticaly added with add_sync_groups
int dap_global_db_add_notify_group_mask(dap_global_db_instance_t *a_dbi, const char *a_group_mask, dap_store_obj_callback_notify_t a_callback, void *a_arg, uint64_t a_ttl);
dap_global_db_notify_item_t *dap_global_db_get_notify_group(dap_global_db_instance_t *a_dbi, const char *a_group_name);

// Set last id for remote node
bool dap_db_set_last_id_remote(uint64_t a_node_addr, uint64_t a_id, char *a_group);
// Get last id for remote node
uint64_t dap_db_get_last_id_remote(uint64_t a_node_addr, char *a_group);

dap_global_db_pkt_t *dap_global_db_pkt_serialize(dap_store_obj_t *a_store_obj);
dap_global_db_pkt_t *dap_global_db_pkt_pack(dap_global_db_pkt_t *a_old_pkt, dap_global_db_pkt_t *a_new_pkt);
dap_store_obj_t *dap_global_db_pkt_deserialize(const dap_global_db_pkt_t *a_pkt, size_t *a_objs_count);

char *dap_store_packet_get_group(dap_global_db_pkt_t *a_pkt);
uint64_t dap_store_packet_get_id(dap_global_db_pkt_t *a_pkt);
void dap_global_db_pkt_change_id(dap_global_db_pkt_t *a_pkt, uint64_t a_id);

dap_db_log_list_t *dap_db_log_list_start(const char *a_net_name, uint64_t a_node_addr, int a_flags);
dap_db_log_list_obj_t *dap_db_log_list_get(dap_db_log_list_t *a_db_log_list);
dap_db_log_list_obj_t **dap_db_log_list_get_multiple(dap_db_log_list_t *a_db_log_list, size_t a_size_limit, size_t *a_count);

void dap_db_log_list_delete(dap_db_log_list_t *a_db_log_list);
int dap_global_db_remote_apply_obj_unsafe(dap_global_db_context_t *a_global_db_context, dap_store_obj_t *a_obj, size_t *a_count,
                                          dap_global_db_callback_results_raw_t a_callback, void *a_arg);
int dap_global_db_remote_apply_obj(dap_store_obj_t *a_obj, size_t a_count, dap_global_db_callback_results_raw_t a_callback, void *a_arg);
