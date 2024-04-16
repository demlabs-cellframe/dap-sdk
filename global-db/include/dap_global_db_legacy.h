#pragma once

#include "dap_common.h"
#include "dap_hash.h"
#include "dap_global_db_driver.h"
#include "dap_global_db.h"

#define DAP_DB_LOG_LIST_MAX_SIZE 0xfffff

typedef void (*dap_store_obj_callback_notify_t) (dap_global_db_instance_t *a_dbi, dap_store_obj_t *a_obj, void * a_arg);

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

typedef struct dap_global_db_pkt_old {
    dap_nanotime_t timestamp;
    uint64_t data_size;
    uint32_t obj_count;
    uint8_t data[];
} DAP_ALIGN_PACKED dap_global_db_pkt_old_t;

typedef struct dap_global_db_legacy_list_group {
    char *name;
    uint64_t count;
} dap_global_db_legacy_list_group_t;

typedef struct dap_global_db_legacy_list_obj {
    dap_global_db_pkt_t *pkt;
    dap_hash_fast_t hash;
} dap_global_db_legacy_list_obj_t;

typedef struct dap_global_db_legacy_list {
    dap_list_t *items_list;
    _Atomic(bool) is_process;
    _Atomic(size_t) items_number, items_rest;
    dap_list_t *groups;
    size_t size;
    pthread_cond_t cond;
    pthread_t thread;
    pthread_mutex_t list_mutex;
} dap_global_db_legacy_list_t;

void dap_global_db_sync_init();
void dap_global_db_sync_deinit();

DAP_STATIC_INLINE size_t dap_global_db_legacy_list_obj_get_size(dap_global_db_legacy_list_obj_t *a_obj)
{
    return sizeof(dap_global_db_legacy_list_obj_t) + sizeof(dap_global_db_pkt_t) + a_obj->pkt->data_size;
}
