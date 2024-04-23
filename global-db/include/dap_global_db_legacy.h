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

typedef struct dap_global_db_legacy_list {
    dap_global_db_driver_hash_t *current_hash;
    _Atomic(size_t) items_number, items_rest;
    dap_list_t *groups;
} dap_global_db_legacy_list_t;

/**
 * @brief Multiples data into a_old_pkt structure from a_new_pkt structure.
 * @param a_old_pkt a pointer to the old object
 * @param a_new_pkt a pointer to the new object
 * @return Returns a pointer to the multiple object
 */
dap_global_db_pkt_old_t *dap_global_db_pkt_pack_old(dap_global_db_pkt_old_t *a_old_pkt, dap_global_db_pkt_old_t *a_new_pkt);

/**
 * @brief Serializes an object into a packed structure.
 * @param a_store_obj a pointer to the object to be serialized
 * @return Returns a pointer to the packed sructure if successful, otherwise NULL.
 */
dap_global_db_pkt_old_t *dap_global_db_pkt_serialize_old(dap_store_obj_t *a_store_obj);
