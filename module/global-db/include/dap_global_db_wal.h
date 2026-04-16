/*
 * DAP Global DB Write-Ahead Log (WAL)
 *
 * Provides ACID guarantees for GlobalDB operations:
 * - Atomicity: operations complete fully or not at all
 * - Durability: committed data survives crashes
 * - Recovery: automatic replay of uncommitted transactions on startup
 *
 * WAL format:
 * [4: magic][4: version][8: sequence]  -- header (16 bytes)
 * [4: length][4: crc32][1: op][data]   -- record
 * ...
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dap_global_db.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Constants
// ============================================================================

#define DAP_GLOBAL_DB_WAL_MAGIC         0x4C415744  // "DWAL" in little-endian
#define DAP_GLOBAL_DB_WAL_VERSION       1
#define DAP_GLOBAL_DB_WAL_SYNC_BYTES    (64 * 1024)  // Sync after 64KB

// WAL operation types
typedef enum dap_global_db_wal_op {
    DAP_GLOBAL_DB_WAL_OP_INSERT = 0x01,
    DAP_GLOBAL_DB_WAL_OP_UPDATE = 0x02,
    DAP_GLOBAL_DB_WAL_OP_DELETE = 0x03,
    DAP_GLOBAL_DB_WAL_OP_COMMIT = 0x10,
    DAP_GLOBAL_DB_WAL_OP_ROLLBACK = 0x11,
    DAP_GLOBAL_DB_WAL_OP_CHECKPOINT = 0x20,
} dap_global_db_wal_op_t;

// ============================================================================
// Types
// ============================================================================

/**
 * @brief WAL file header (16 bytes)
 */
typedef struct dap_global_db_wal_header {
    uint32_t magic;         // DAP_GLOBAL_DB_WAL_MAGIC
    uint32_t version;       // DAP_GLOBAL_DB_WAL_VERSION
    uint64_t sequence;      // Last committed sequence number
} dap_global_db_wal_header_t;
_Static_assert(sizeof(dap_global_db_wal_header_t) == 16, "WAL header wire size");

/**
 * @brief WAL record header (9 bytes + data)
 */
typedef struct dap_global_db_wal_record {
    uint32_t length;        // Total record length (including header)
    uint32_t crc32;         // CRC32 of operation + data
    uint8_t op;             // Operation type (dap_global_db_wal_op_t)
    byte_t data[];          // Variable data
} DAP_ALIGN_PACKED dap_global_db_wal_record_t;

/**
 * @brief WAL handle (opaque)
 */
typedef struct dap_global_db_wal dap_global_db_wal_t;

// ============================================================================
// WAL lifecycle
// ============================================================================

/**
 * @brief Open or create WAL file for a group
 * @param a_wal_path Path to WAL file
 * @return WAL handle or NULL on error
 */
dap_global_db_wal_t *dap_global_db_wal_open(const char *a_wal_path);

/**
 * @brief Close WAL and free resources
 * @param a_wal WAL handle
 */
void dap_global_db_wal_close(dap_global_db_wal_t *a_wal);

/**
 * @brief Perform recovery from WAL (call after open, before normal ops)
 * @param a_wal WAL handle
 * @param a_replay_cb Callback for each record to replay
 * @param a_arg User argument for callback
 * @return Number of records replayed, negative on error
 */
typedef int (*dap_global_db_wal_replay_cb_t)(dap_global_db_wal_op_t a_op,
                                             const byte_t *a_data, size_t a_len,
                                             void *a_arg);

int dap_global_db_wal_recover(dap_global_db_wal_t *a_wal,
                               dap_global_db_wal_replay_cb_t a_replay_cb,
                               void *a_arg);

// ============================================================================
// WAL operations
// ============================================================================

/**
 * @brief Write insert/update record to WAL
 * @param a_wal WAL handle
 * @param a_hash Record hash key
 * @param a_key Text key
 * @param a_value Value data
 * @param a_value_len Value length
 * @param a_sign Signature (may be NULL)
 * @param a_sign_len Signature length
 * @param a_flags Record flags
 * @return 0 on success, negative on error
 */
int dap_global_db_wal_write(dap_global_db_wal_t *a_wal,
                             dap_global_db_hash_t a_hash,
                             const char *a_key,
                             const void *a_value, size_t a_value_len,
                             const void *a_sign, size_t a_sign_len,
                             uint8_t a_flags);

/**
 * @brief Write delete record to WAL
 * @param a_wal WAL handle
 * @param a_hash Record hash key to delete
 * @return 0 on success, negative on error
 */
int dap_global_db_wal_delete(dap_global_db_wal_t *a_wal, dap_global_db_hash_t a_hash);

/**
 * @brief Commit current transaction (marks data as durable)
 * @param a_wal WAL handle
 * @return 0 on success, negative on error
 */
int dap_global_db_wal_commit(dap_global_db_wal_t *a_wal);

/**
 * @brief Checkpoint: truncate WAL after successful B-tree sync
 * @param a_wal WAL handle
 * @return 0 on success, negative on error
 */
int dap_global_db_wal_checkpoint(dap_global_db_wal_t *a_wal);

/**
 * @brief Force sync WAL to disk
 * @param a_wal WAL handle
 * @return 0 on success, negative on error
 */
int dap_global_db_wal_sync(dap_global_db_wal_t *a_wal);

/**
 * @brief Get current WAL size in bytes
 * @param a_wal WAL handle
 * @return Size in bytes
 */
size_t dap_global_db_wal_size(dap_global_db_wal_t *a_wal);

#ifdef __cplusplus
}
#endif
