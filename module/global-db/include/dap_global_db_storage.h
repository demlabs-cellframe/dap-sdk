/*
 * DAP Global DB Storage Layer
 * 
 * Direct B-tree storage operations for GlobalDB.
 * No driver abstraction - works directly with file-per-group B-tree files.
 */

#pragma once

#include "dap_global_db.h"
#include "dap_global_db_btree.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Storage initialization
// ============================================================================

/**
 * @brief Initialize native storage
 * @param a_storage_path Base path for storage directory
 * @return 0 on success, negative on error
 */
int dap_global_db_storage_init(const char *a_storage_path);

/**
 * @brief Deinitialize storage, close all groups
 */
void dap_global_db_storage_deinit(void);

/**
 * @brief Flush all dirty pages to disk
 * @return 0 on success
 */
int dap_global_db_storage_flush(void);

// ============================================================================
// Group operations
// ============================================================================

/**
 * @brief Get B-tree handle for group (read-only lookup)
 * @param a_group_name Group name
 * @return B-tree handle or NULL if group doesn't exist
 */
dap_global_db_btree_t *dap_global_db_storage_group_get(const char *a_group_name);

/**
 * @brief Get or create B-tree handle for group
 * @param a_group_name Group name
 * @return B-tree handle or NULL on error
 */
dap_global_db_btree_t *dap_global_db_storage_group_get_or_create(const char *a_group_name);

/**
 * @brief Get list of all groups matching mask
 * @param a_mask Glob pattern (e.g. "local.*", "*")
 * @return List of group names (caller owns, free with dap_list_free_full)
 */
dap_list_t *dap_global_db_storage_get_groups_by_mask(const char *a_mask);

/**
 * @brief Get count of records in group
 * @param a_group_name Group name
 * @param a_with_deleted Include deleted records
 * @return Record count
 */
uint64_t dap_global_db_storage_group_count(const char *a_group_name, bool a_with_deleted);

/**
 * @brief Get count of records after given hash
 * @param a_group_name Group name
 * @param a_hash_from Start hash (exclusive)
 * @param a_with_deleted Include deleted records
 * @return Record count
 */
uint64_t dap_global_db_storage_group_count_from(const char *a_group_name, 
                                           dap_global_db_hash_t a_hash_from,
                                           bool a_with_deleted);

// ============================================================================
// Record operations (work directly with B-tree)
// ============================================================================

/**
 * @brief Read record by text key
 * @param a_group Group name
 * @param a_key Text key
 * @param a_with_deleted Include deleted records
 * @return Store object (caller owns) or NULL
 */
dap_global_db_store_obj_t *dap_global_db_storage_read_by_key(const char *a_group, const char *a_key,
                                              bool a_with_deleted);

/**
 * @brief Read record by driver hash
 * @param a_group Group name
 * @param a_hash Driver hash
 * @return Store object (caller owns) or NULL
 */
dap_global_db_store_obj_t *dap_global_db_storage_read_by_hash(const char *a_group, 
                                               dap_global_db_hash_t a_hash);

/**
 * @brief Read last record in group
 * @param a_group Group name
 * @param a_with_deleted Include deleted records
 * @return Store object (caller owns) or NULL
 */
dap_global_db_store_obj_t *dap_global_db_storage_read_last(const char *a_group, bool a_with_deleted);

/**
 * @brief Read all records in group
 * @param a_group Group name
 * @param a_count_out Output: number of records
 * @param a_with_deleted Include deleted records
 * @return Array of store objects (caller owns) or NULL
 */
dap_global_db_store_obj_t *dap_global_db_storage_read_all(const char *a_group, size_t *a_count_out,
                                           bool a_with_deleted);

/**
 * @brief Read records starting from hash
 * @param a_group Group name
 * @param a_hash_from Start hash (exclusive, blank = from beginning)
 * @param a_max_count Maximum records to return
 * @param a_count_out Output: actual number of records
 * @param a_with_deleted Include deleted records
 * @return Array of store objects (caller owns) or NULL
 */
dap_global_db_store_obj_t *dap_global_db_storage_read_cond(const char *a_group,
                                            dap_global_db_hash_t a_hash_from,
                                            size_t a_max_count,
                                            size_t *a_count_out,
                                            bool a_with_deleted);

/**
 * @brief Read records with timestamp below threshold
 * @param a_group Group name
 * @param a_timestamp Timestamp threshold
 * @param a_count_out Output: number of records
 * @return Array of store objects (caller owns) or NULL
 */
dap_global_db_store_obj_t *dap_global_db_storage_read_below_timestamp(const char *a_group,
                                                       dap_nanotime_t a_timestamp,
                                                       size_t *a_count_out);

/**
 * @brief Write/update record
 * @param a_obj Store object to write
 * @return 0 on success, negative on error
 */
int dap_global_db_storage_write(dap_global_db_store_obj_t *a_obj);

/**
 * @brief Write multiple records
 * @param a_objs Array of store objects
 * @param a_count Number of objects
 * @return 0 on success, negative on error
 */
int dap_global_db_storage_write_multi(dap_global_db_store_obj_t *a_objs, size_t a_count);

/**
 * @brief Delete record by hash (permanent erase)
 * @param a_group Group name
 * @param a_hash Driver hash
 * @return 0 on success, 1 if not found, negative on error
 */
int dap_global_db_storage_erase(const char *a_group, dap_global_db_hash_t a_hash);

/**
 * @brief Check if record exists by text key
 * @param a_group Group name
 * @param a_key Text key
 * @return true if exists
 */
bool dap_global_db_storage_exists_key(const char *a_group, const char *a_key);

/**
 * @brief Check if record exists by hash
 * @param a_group Group name
 * @param a_hash Driver hash
 * @return true if exists
 */
bool dap_global_db_storage_exists_hash(const char *a_group, dap_global_db_hash_t a_hash);

// ============================================================================
// Synchronization helpers
// ============================================================================

/**
 * @brief Read hashes starting from given hash (for sync protocol)
 * @param a_group Group name
 * @param a_hash_from Start hash (exclusive)
 * @return Hash packet (caller owns) or NULL
 */
dap_global_db_hash_pkt_t *dap_global_db_storage_read_hashes(const char *a_group, 
                                                       dap_global_db_hash_t a_hash_from);

/**
 * @brief Get records by array of hashes (for sync protocol)
 * @param a_group Group name
 * @param a_hashes Array of hashes
 * @param a_count Number of hashes
 * @return Packed records (caller owns) or NULL
 */
dap_global_db_pkt_pack_t *dap_global_db_storage_get_by_hash(const char *a_group,
                                                       dap_global_db_hash_t *a_hashes,
                                                       size_t a_count);

#ifdef __cplusplus
}
#endif
