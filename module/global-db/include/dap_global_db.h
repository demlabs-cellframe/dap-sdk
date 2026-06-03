/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * Demlabs Ltd.   https://demlabs.net
 * Copyright  (c) 2022
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

#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_time.h"
#include "dap_list.h"
#include "dap_sign.h"
#include "dap_guuid.h"

// ============================================================================
// Storage limits and defaults
// ============================================================================
#define DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX   128UL   /* Maximum size of group name */
#define DAP_GLOBAL_DB_GROUPS_COUNT_MAX      1024UL  /* Maximum number of groups */
#define DAP_GLOBAL_DB_KEY_SIZE_MAX          512UL   /* Maximum key length */

#define DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT 256UL /* Default count of records for conditional read */
#define DAP_GLOBAL_DB_COND_READ_KEYS_DEFAULT  512UL /* Default count of keys for conditional read */

// ============================================================================
// Record flags
// ============================================================================
// Flags stored in DB
#define DAP_GLOBAL_DB_RECORD_DEL        BIT(0)      /* Record deletion marker (propagated over sync) */
#define DAP_GLOBAL_DB_RECORD_PINNED     BIT(1)      /* Record protected from TTL cleanup */
// Auxiliary flags (not stored in DB)
#define DAP_GLOBAL_DB_RECORD_NEW        BIT(6)      /* Record is newly generated locally */
#define DAP_GLOBAL_DB_RECORD_ERASE      BIT(7)      /* Record will be permanently erased */

// ============================================================================
// Core types
// ============================================================================

/**
 * @brief Hash key for GlobalDB records (16 bytes, big-endian sorted)
 */
typedef struct dap_global_db_hash {
    dap_nanotime_t bets;    // Timestamp in big-endian
    uint64_t becrc;         // CRC in big-endian
} DAP_ALIGN_PACKED dap_global_db_hash_t;
_Static_assert(sizeof(dap_global_db_hash_t) == 16, "db_hash must be 16 bytes");

/**
 * @brief Store object - main data record structure
 */
typedef struct dap_global_db_store_obj {
    char *group;                // Group name (table analogue)
    char *key;                  // Unique key within group
    byte_t *value;              // Value data
    size_t value_len;           // Value length
    uint8_t flags;              // Record flags
    dap_sign_t *sign;           // Cryptographic signature
    dap_nanotime_t timestamp;   // Creation timestamp (nanoseconds since EPOCH)
    uint64_t crc;               // Integrity checksum
    byte_t ext[];               // Extra data for sync callbacks
} dap_global_db_store_obj_t;

/**
 * @brief Operation type for notifications
 */
typedef enum dap_global_db_optype {
    DAP_GLOBAL_DB_OPTYPE_ADD = 0x61,    /* 'a' - INSERT / OVERWRITE */
    DAP_GLOBAL_DB_OPTYPE_DEL = 0x64,    /* 'd' - DELETE */
} dap_global_db_optype_t;

// Forward declarations for packet types
typedef struct dap_global_db_hash_pkt dap_global_db_hash_pkt_t;
typedef struct dap_global_db_pkt_pack dap_global_db_pkt_pack_t;

// Blank hash constant - defined in dap_global_db_obj.c
extern const dap_global_db_hash_t c_dap_global_db_hash_blank;

// ============================================================================
// Inline utility functions for hash operations
// ============================================================================

DAP_STATIC_INLINE dap_global_db_optype_t dap_global_db_store_obj_get_type(dap_global_db_store_obj_t *a_obj)
{
    return a_obj->flags & DAP_GLOBAL_DB_RECORD_DEL ? DAP_GLOBAL_DB_OPTYPE_DEL : DAP_GLOBAL_DB_OPTYPE_ADD;
}

DAP_STATIC_INLINE dap_global_db_hash_t dap_global_db_hash_get(dap_global_db_store_obj_t *a_obj)
{
    dap_global_db_hash_t l_ret = { .bets = htobe64(a_obj->timestamp), .becrc = htobe64(a_obj->crc) };
    return l_ret;
}

DAP_STATIC_INLINE int dap_global_db_hash_compare(dap_global_db_hash_t *a_hash1, dap_global_db_hash_t *a_hash2)
{
    int l_ret = memcmp(a_hash1, a_hash2, sizeof(dap_global_db_hash_t));
    return l_ret < 0 ? -1 : (l_ret > 0 ? 1 : 0);
}

DAP_STATIC_INLINE int dap_global_db_store_obj_hash_compare(dap_global_db_store_obj_t *a_obj1, dap_global_db_store_obj_t *a_obj2)
{
    if (!a_obj1)
        return a_obj2 ? -1 : 0;
    if (!a_obj2)
        return 1;
    dap_global_db_hash_t l_hash1 = dap_global_db_hash_get(a_obj1),
                   l_hash2 = dap_global_db_hash_get(a_obj2);
    return dap_global_db_hash_compare(&l_hash1, &l_hash2);
}

DAP_STATIC_INLINE bool dap_global_db_store_obj_compare(dap_global_db_store_obj_t *a_obj1, dap_global_db_store_obj_t *a_obj2)
{
    return dap_global_db_store_obj_hash_compare(a_obj1, a_obj2) || a_obj1->flags != a_obj2->flags ||
        a_obj1->value_len != a_obj2->value_len || memcmp(a_obj1->value, a_obj2->value, a_obj1->value_len) ||
        dap_sign_get_size(a_obj1->sign) != dap_sign_get_size(a_obj2->sign) || 
        memcmp(a_obj1->sign, a_obj2->sign, dap_sign_get_size(a_obj1->sign)) ||
        strcmp(a_obj1->key, a_obj2->key) || strcmp(a_obj1->group, a_obj2->group);
}

DAP_STATIC_INLINE const char *dap_global_db_hash_print(dap_global_db_hash_t a_hash)
{
    return dap_guuid_to_hex_str(dap_guuid_compose(a_hash.bets, a_hash.becrc));
}

DAP_STATIC_INLINE bool dap_global_db_hash_is_blank(dap_global_db_hash_t *a_blank_candidate)
{
    return !memcmp(a_blank_candidate, &c_dap_global_db_hash_blank, sizeof(dap_global_db_hash_t));
}

// ============================================================================
// Store object management - see dap_global_db_obj.h
// ============================================================================
#include "dap_global_db_obj.h"

// ============================================================================
// GlobalDB configuration
// ============================================================================
#define DAP_GLOBAL_DB_VERSION               3
#define DAP_GLOBAL_DB_LOCAL_GENERAL         "local.general"
#define DAP_GLOBAL_DB_LOCAL_LAST_HASH       "local.lasthash"
#define DAP_GLOBAL_DB_SYNC_WAIT_TIMEOUT     5 // seconds

#define DAP_GLOBAL_DB_TTL_DEL       "111"
#define DAP_GLOBAL_DB_MANUAL_DEL    "222"

typedef struct dap_global_db_cluster dap_global_db_cluster_t;

// Global DB instance with settings data
typedef struct dap_global_db_instance {
    uint32_t version;     // Current GlobalDB version
    char *storage_path;   // GlobalDB storage path
    dap_list_t *whitelist;
    dap_list_t *blacklist;
    uint64_t store_time_limit;
    dap_global_db_cluster_t *clusters;
    dap_enc_key_t *signing_key;
    uint32_t sync_idle_time;
} dap_global_db_instance_t;

typedef struct dap_global_db_obj {
    char *key;
    uint8_t *value;
    size_t value_len;
    dap_nanotime_t timestamp;
    bool is_pinned;
} dap_global_db_obj_t;

typedef void (*dap_global_db_callback_t)(dap_global_db_instance_t *a_dbi, void * a_arg);

/**
 *  @brief callback for single result
 *  @arg a_rc DAP_GLOBAL_DB_RC_SUCCESS if success others if not
 */
typedef void (*dap_global_db_callback_result_t)(dap_global_db_instance_t *a_dbi, int a_rc, const char *a_group, const char * a_key, const void * a_value,
                                                 const size_t a_value_size, dap_nanotime_t a_value_ts, bool a_is_pinned, void * a_arg);

/**
 *  @brief callback for single raw result
 *  @arg a_rc DAP_GLOBAL_DB_RC_SUCCESS if success others if not
 *  @return none.
 */
typedef void (*dap_global_db_callback_result_raw_t)(dap_global_db_instance_t *a_dbi, int a_rc, dap_global_db_store_obj_t * a_store_obj, void * a_arg);


/**
 *  @brief callback for multiple result, with pagination
 *  @arg a_rc DAP_GLOBAL_DB_RC_SUCCESS if success others if not
 *  @arg a_values_total Total values number
 *  @arg a_values_shift Current shift from beginning of values set
 *  @arg a_values_count Current number of items in a_values
 *  @arg a_values Current items (page of items)
 *  @arg a_arg Custom argument
 *  @return none.
 */
typedef bool (*dap_global_db_callback_results_t)(dap_global_db_instance_t *a_dbi,
                                                  int a_rc, const char *a_group,
                                                  const size_t a_values_total, const size_t a_values_count,
                                                  dap_global_db_obj_t *a_values, void *a_arg);
/**
 *  @brief callback for multiple raw result, with pagination
 *  @arg a_rc DAP_GLOBAL_DB_RC_SUCCESS if success other sif not
 *  @arg a_values_total Total values number
 *  @arg a_values_shift Current shift from beginning of values set
 *  @arg a_values_count Current number of items in a_values
 *  @arg a_values Current items (page of items)
 *  @return none.
 */
typedef bool (*dap_global_db_callback_results_raw_t) (dap_global_db_instance_t *a_dbi,
                                                      int a_rc, const char *a_group,
                                                      const size_t a_values_current, const size_t a_values_count,
                                                      dap_global_db_store_obj_t *a_values, void *a_arg);
// Return codes
#define DAP_GLOBAL_DB_RC_SUCCESS     0
#define DAP_GLOBAL_DB_RC_NOT_FOUND   1
#define DAP_GLOBAL_DB_RC_PROGRESS    2
#define DAP_GLOBAL_DB_RC_NO_RESULTS -1
#define DAP_GLOBAL_DB_RC_CRITICAL   -3
#define DAP_GLOBAL_DB_RC_ERROR      -6

extern int g_dap_global_db_debug_more;
extern bool g_dap_global_db_wal_enabled;

int dap_global_db_init();
void dap_global_db_deinit();
int dap_global_db_clean_init();
int dap_global_db_clean_deinit();

void dap_global_db_instance_deinit();
dap_global_db_instance_t *dap_global_db_instance_get_default();

// For context unification sometimes we need to exec inside GlobalDB context
int dap_global_db_context_exec(dap_global_db_callback_t a_callback, void * a_arg);

// Copy global_db_obj array
dap_global_db_obj_t *dap_global_db_objs_copy(const dap_global_db_obj_t *a_objs_src, size_t a_count);

// Clear global_db_obj array
void dap_global_db_objs_delete(dap_global_db_obj_t *a_objs, size_t a_count);

// === Async functions ===
int dap_global_db_get(const char *a_group, const char *a_key,dap_global_db_callback_result_t a_callback, void *a_arg);
int dap_global_db_get_raw(const char *a_group, const char *a_key,dap_global_db_callback_result_raw_t a_callback, void *a_arg);

int dap_global_db_get_del_ts(const char *a_group, const char *a_key,dap_global_db_callback_result_t a_callback, void *a_arg);
int dap_global_db_get_last(const char *a_group, dap_global_db_callback_result_t a_callback, void *a_arg);
int dap_global_db_get_last_raw(const char *a_group, dap_global_db_callback_result_raw_t a_callback, void *a_arg);
int dap_global_db_get_all(const char *a_group, size_t l_results_page_size, dap_global_db_callback_results_t a_callback, void *a_arg);
int dap_global_db_get_all_raw(const char *a_group, size_t l_results_page_size, dap_global_db_callback_results_raw_t a_callback, void *a_arg);

int dap_global_db_set(const char *a_group, const char *a_key, const void * a_value, const size_t a_value_length, bool a_pin_value, dap_global_db_callback_result_t a_callback, void *a_arg);
int dap_global_db_set_raw(dap_global_db_store_obj_t *a_store_objs, size_t a_store_objs_count, dap_global_db_callback_results_raw_t a_callback, void *a_arg);

int dap_global_db_pin(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg);
int dap_global_db_unpin(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg);
int dap_global_db_del(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg);
int dap_global_db_del_ex(const char * a_group, const char *a_key, const void * a_value, const size_t a_value_len,
                                                              dap_global_db_callback_result_t a_callback, void *a_arg);
int dap_global_db_flush( dap_global_db_callback_result_t a_callback, void *a_arg);

// Set multiple. In callback writes total processed objects to a_values_total and a_values_count to the a_values_count as well
int dap_global_db_set_multiple_zc(const char *a_group, dap_global_db_obj_t * a_values, size_t a_values_count, dap_global_db_callback_results_t a_callback, void *a_arg);

// === Sync functions ===
byte_t *dap_global_db_get_sync(const char *a_group, const char *a_key, size_t *a_data_size, bool *a_is_pinned, dap_nanotime_t *a_ts);
dap_global_db_store_obj_t *dap_global_db_get_raw_sync(const char *a_group, const char *a_key);

dap_nanotime_t dap_global_db_get_del_ts_sync(const char *a_group, const char *a_key);
byte_t *dap_global_db_get_last_sync(const char *a_group, char **a_key, size_t *a_data_size, bool *a_is_pinned, dap_nanotime_t *a_ts);
dap_global_db_store_obj_t *dap_global_db_get_last_raw_sync(const char *a_group);
dap_global_db_obj_t *dap_global_db_get_all_sync(const char *a_group, size_t *a_objs_count);
dap_global_db_store_obj_t *dap_global_db_get_all_raw_sync(const char *a_group, size_t *a_objs_count);

int dap_global_db_set_sync(const char *a_group, const char *a_key, const void *a_value, const size_t a_value_length, bool a_pin_value);
// set raw with cluster roles and rights checks
int dap_global_db_set_raw_sync(dap_global_db_store_obj_t *a_store_objs, size_t a_store_objs_count);

int dap_global_db_pin_sync(const char *a_group, const char *a_key);
int dap_global_db_unpin_sync(const char *a_group, const char *a_key);
int dap_global_db_del_sync(const char *a_group, const char *a_key);
int dap_global_db_del_sync_ex(const char *a_group, const char *a_key, const char * a_value, size_t a_value_size);
int dap_global_db_flush_sync();

bool dap_global_db_isalnum_group_key(const dap_global_db_store_obj_t *a_obj, bool a_not_null_key);
bool dap_global_db_group_match_mask(const char *a_group, const char *a_mask);

int dap_global_db_erase_table_sync(const char *a_group);
int dap_global_db_erase_table(const char *a_group, dap_global_db_callback_result_t a_callback, void *a_arg);

// === Storage group operations (direct B-tree access) ===
typedef struct dap_global_db_btree dap_global_db_t;

dap_list_t *dap_global_db_get_groups_by_mask(const char *a_mask);
uint64_t dap_global_db_group_count(const char *a_group_name, bool a_with_deleted);
bool dap_global_db_exists_hash(const char *a_group, dap_global_db_hash_t a_hash);

dap_global_db_hash_pkt_t *dap_global_db_read_hashes(const char *a_group, dap_global_db_hash_t a_hash_from);
dap_global_db_pkt_pack_t *dap_global_db_get_by_hash(const char *a_group, dap_global_db_hash_t *a_hashes, size_t a_count);

// Lightweight init/deinit for standalone tools (e.g. migrate)
int dap_global_db_groups_init(const char *a_storage_path);
void dap_global_db_groups_deinit(void);
int dap_global_db_groups_flush(void);
dap_global_db_t *dap_global_db_group_get_or_create(const char *a_group_name);
size_t dap_global_db_group_clear(const char *a_group, bool a_pinned);

// WAL (Write-Ahead Log) — optional durability layer, disabled by default.
// Enable via config: [global_db] wal_enabled = true
DAP_STATIC_INLINE void dap_global_db_set_wal_enabled(bool a_enabled) { g_dap_global_db_wal_enabled = a_enabled; }
DAP_STATIC_INLINE bool dap_global_db_get_wal_enabled(void) { return g_dap_global_db_wal_enabled; }