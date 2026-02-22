/*
 * Authors:
 * DAP SDK Team
 * Copyright (c) 2026
 * All rights reserved.
 *
 * DAP Global DB B-tree Implementation
 * 
 * Persistent B-tree data structure for GlobalDB native storage engine.
 * Keys are dap_global_db_hash_t (16 bytes, sorted as big-endian).
 * Values are variable-length records with text key, value, and signature.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>
#include "dap_common.h"
#include "dap_mmap.h"
#include "dap_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Constants
// ============================================================================

#define DAP_GLOBAL_DB_MAGIC         0x4E424447  // "GDBN" in little-endian
#define DAP_GLOBAL_DB_STORAGE_VERSION       1
#define DAP_GLOBAL_DB_PAGE_SIZE     4096
#define DAP_GLOBAL_DB_MIN_KEYS      2           // Minimum keys per node (t-1 for B-tree order t)
#define DAP_GLOBAL_DB_MAX_KEYS      ((DAP_GLOBAL_DB_PAGE_SIZE - 64) / 32)  // ~126 keys per page

// Page flags
#define DAP_GLOBAL_DB_PAGE_BRANCH         0x0001
#define DAP_GLOBAL_DB_PAGE_LEAF           0x0002
#define DAP_GLOBAL_DB_PAGE_OVERFLOW       0x0004
#define DAP_GLOBAL_DB_PAGE_ROOT           0x0008

/** Leaf entry flag: value+sign stored in overflow chain; entry holds overflow_page_id (8 bytes) after key instead of inline value/sign */
#define DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE  0x01

// MVCC constants
#define DAP_BTREE_MAX_SNAPSHOTS           64  // Maximum concurrent reader snapshots

// ============================================================================
// Types
// ============================================================================

/**
 * @brief B-tree key type - matches dap_global_db_hash_t
 */
typedef struct dap_global_db_key {
    uint64_t bets;      // timestamp in big-endian
    uint64_t becrc;     // CRC in big-endian
} DAP_ALIGN_PACKED dap_global_db_key_t;

/**
 * @brief B-tree file header (64 bytes)
 */
typedef struct dap_global_db_header {
    uint32_t magic;             // DAP_GLOBAL_DB_MAGIC
    uint16_t version;           // DAP_GLOBAL_DB_STORAGE_VERSION
    uint16_t flags;             // Reserved flags
    uint32_t page_size;         // Page size (4096)
    uint32_t reserved1;         // Alignment
    uint64_t root_page;         // Offset of root page (0 if empty)
    uint64_t total_pages;       // Total pages allocated
    uint64_t items_count;       // Total items in tree
    uint32_t tree_height;       // Height of tree
    uint32_t reserved2;         // Alignment
    uint64_t free_list_head;    // Head of free page list
    uint64_t checksum;          // Header checksum
} DAP_ALIGN_PACKED dap_global_db_header_t;

_Static_assert(sizeof(dap_global_db_header_t) == 64, "Header must be 64 bytes");

/**
 * @brief B-tree page header (32 bytes)
 */
typedef struct dap_global_db_page_header {
    uint16_t flags;             // Page flags (BRANCH/LEAF/OVERFLOW)
    uint16_t entries_count;     // Number of entries in page
    uint32_t free_space;        // Free space in page
    uint64_t page_id;           // This page's ID/offset
    uint64_t right_sibling;     // Right sibling page (for forward scan, 0 = rightmost)
    uint64_t left_sibling;      // Left sibling page (for reverse scan, 0 = leftmost)
} DAP_ALIGN_PACKED dap_global_db_page_header_t;

_Static_assert(sizeof(dap_global_db_page_header_t) == 32, "Page header must be 32 bytes");

/**
 * @brief Leaf entry header (variable size)
 * Followed by: text_key[key_len] + (value_data[value_len] + sign_data[sign_len]  OR  overflow_page_id if LEAF_ENTRY_OVERFLOW_VALUE)
 */
typedef struct dap_global_db_leaf_entry {
    dap_global_db_key_t driver_hash;    // 16 bytes - sort key
    uint32_t key_len;                    // Text key length
    uint32_t value_len;                  // Value length
    uint32_t sign_len;                   // Signature length
    uint8_t flags;                       // Record flags
    uint8_t reserved[3];                 // Alignment
    // Followed by: key_data[key_len] + value_data[value_len] + sign_data[sign_len]
} DAP_ALIGN_PACKED dap_global_db_leaf_entry_t;

/**
 * @brief Branch entry (24 bytes)
 */
typedef struct dap_global_db_branch_entry {
    dap_global_db_key_t driver_hash;    // 16 bytes - separator key
    uint64_t child_page;                 // Child page offset
} DAP_ALIGN_PACKED dap_global_db_branch_entry_t;

_Static_assert(sizeof(dap_global_db_branch_entry_t) == 24, "Branch entry must be 24 bytes");

/**
 * @brief B-tree page structure (in-memory representation)
 */
typedef struct dap_global_db_page {
    dap_global_db_page_header_t header;
    bool is_dirty;                       // Page has been modified
    bool is_mmap_ref;                    // Data points into mmap region (don't free)
    bool is_mmap_writable;               // Writable mmap ref (skip COW, header-only writeback)
    bool is_arena;                       // Allocated from arena (don't individually free)
    uint8_t *data;                       // Page data (entries area)
} dap_global_db_page_t;

/**
 * @brief Deferred free batch — pages freed during a COW write transaction.
 *
 * Pages cannot be immediately recycled because active reader snapshots may
 * still reference them. They are reclaimed when min(active_snapshot_txn) > txn_id.
 */
typedef struct dap_btree_deferred_batch {
    uint64_t txn_id;       // Transaction that freed these pages
    uint64_t *page_ids;    // Array of freed page IDs
    size_t count;          // Current number of entries
    size_t capacity;       // Allocated capacity
} dap_btree_deferred_batch_t;

/**
 * @brief B-tree handle
 */
#define DAP_GLOBAL_DB_PATH_MAX  16  // Max tree depth for path cache

typedef struct dap_global_db_btree {
    int fd;                              // File descriptor (kept for header I/O fallback)
    char *filepath;                      // File path
    dap_global_db_header_t header;       // Cached header
    dap_global_db_page_t *root;          // Cached root page (may be NULL)
    dap_global_db_page_t *hot_leaf;      // Cached last-written leaf (write-back, avoid navigation)
    // Cached path from root to hot_leaf parent — LMDB cursor-style optimization.
    // Eliminates full root-to-leaf traversal when hot_leaf fills up during
    // sequential inserts. Updated when hot_leaf is promoted.
    struct dap_btree_path_entry {
        uint64_t page_id;       // Branch page ID at this level
        int child_index;        // Index of child in this branch page
    } hot_path[DAP_GLOBAL_DB_PATH_MAX];
    int hot_path_depth;                  // Number of entries in hot_path (0 = invalid)
    bool read_only;                      // Read-only mode
    bool in_batch;                       // Batch insert mode active (wrlock held externally)
    dap_mmap_t *mmap;                    // Memory-mapped file handle (NULL = legacy I/O)
    dap_arena_t *arena;                  // Bump allocator for temporary page allocations
    pthread_rwlock_t lock;               // Reader-writer lock for thread safety (Phase 3 compat)

    // ---- MVCC state ----
    // Enables lock-free reads via snapshot isolation. Writers use exclusive
    // write_mutex. Readers atomically read mvcc_root and work on a frozen
    // snapshot of the tree without acquiring any lock.
    _Atomic(uint64_t) mvcc_root;         // Latest committed root page ID
    _Atomic(uint64_t) mvcc_txn;          // Latest committed transaction ID
    _Atomic(uint64_t) mvcc_count;        // Latest committed items_count
    _Atomic(uint32_t) mvcc_height;       // Latest committed tree_height
    _Atomic(uint64_t) mvcc_seq;          // Seqlock counter (odd = publish in progress)
    pthread_mutex_t write_mutex;          // Exclusive writer access

    // Snapshot tracking — each slot holds the txn_id of an active reader (0 = unused).
    // Readers CAS from 0 to their txn_id on acquire, store 0 on release.
    _Atomic(uint64_t) snapshot_txns[DAP_BTREE_MAX_SNAPSHOTS];

    // Deferred free pages — COW-freed pages awaiting epoch-based reclamation.
    // Each write transaction appends a batch. Batches are recycled when
    // min(active snapshot_txns) > batch.txn_id.
    dap_btree_deferred_batch_t *deferred_batches;
    size_t deferred_batch_count;
    size_t deferred_batch_capacity;
    dap_btree_deferred_batch_t *deferred_last_batch; // O(1) lookup cache for deferred_free_add
    uint32_t mvcc_commit_counter;                     // Throttle reclaim: run every N commits

    // Overflow read buffer — get_ref copies large values here (valid until next get_ref/get/write)
    uint8_t *overflow_read_buf;
    size_t overflow_read_buf_size;
} dap_global_db_t;

/**
 * @brief B-tree cursor for iteration
 */
typedef struct dap_global_db_cursor {
    dap_global_db_t *tree;               // Tree handle
    dap_global_db_page_t *current_page;  // Current leaf page
    uint16_t current_index;              // Current entry index in page
    bool valid;                          // Cursor is valid
    bool at_end;                         // Cursor at end (no more entries)
    // MVCC snapshot state
    uint64_t snapshot_root;              // Root page at time of cursor creation
    uint64_t snapshot_txn;               // Txn ID at time of cursor creation
    uint64_t snapshot_count;             // items_count at time of cursor creation (from seqlock)
    int snapshot_slot;                   // Snapshot slot index (-1 = not using MVCC)
    // Path stack for snapshot-mode traversal.
    // In snapshot mode, sibling links are NOT used (they may be stale due to
    // in-place updates by writers). Instead, NEXT/PREV pop the path stack to
    // find the next/prev subtree — classic B-tree cursor without sibling links.
    struct dap_btree_path_entry path[DAP_GLOBAL_DB_PATH_MAX];
    int path_depth;
} dap_global_db_cursor_t;

/**
 * @brief Cursor position operations
 */
typedef enum dap_global_db_cursor_op {
    DAP_GLOBAL_DB_FIRST,         // Position at first entry
    DAP_GLOBAL_DB_LAST,          // Position at last entry
    DAP_GLOBAL_DB_NEXT,          // Move to next entry
    DAP_GLOBAL_DB_PREV,          // Move to previous entry
    DAP_GLOBAL_DB_SET,           // Position at exact key
    DAP_GLOBAL_DB_SET_RANGE,     // Position at key or next greater
    DAP_GLOBAL_DB_SET_UPPERBOUND // Position at first key > given key
} dap_global_db_cursor_op_t;

// ============================================================================
// Functions
// ============================================================================

/**
 * @brief Create a new B-tree file
 * @param a_filepath Path to the B-tree file
 * @return Tree handle or NULL on error
 */
dap_global_db_t *dap_global_db_create(const char *a_filepath);

/**
 * @brief Open an existing B-tree file
 * @param a_filepath Path to the B-tree file
 * @param a_read_only Open in read-only mode
 * @return Tree handle or NULL on error
 */
dap_global_db_t *dap_global_db_open(const char *a_filepath, bool a_read_only);

/**
 * @brief Close B-tree and free resources
 * @param a_tree Tree handle
 */
void dap_global_db_close(dap_global_db_t *a_tree);

/**
 * @brief Sync all dirty pages to disk
 * @param a_tree Tree handle
 * @return 0 on success, negative on error
 */
int dap_global_db_sync(dap_global_db_t *a_tree);

/**
 * @brief Insert or update an entry
 * @param a_tree Tree handle
 * @param a_key Driver hash key
 * @param a_text_key Text key string
 * @param a_text_key_len Text key length (including null terminator)
 * @param a_value Value data
 * @param a_value_len Value length
 * @param a_sign Signature data (can be NULL)
 * @param a_sign_len Signature length
 * @param a_flags Record flags
 * @return 0 on success, negative on error
 */
int dap_global_db_insert(dap_global_db_t *a_tree,
                         const dap_global_db_key_t *a_key,
                         const char *a_text_key, uint32_t a_text_key_len,
                         const void *a_value, uint32_t a_value_len,
                         const void *a_sign, uint32_t a_sign_len,
                         uint8_t a_flags);

/**
 * @brief Begin a batch insert transaction.
 *
 * Acquires the write lock and enters batch mode. Subsequent inserts skip
 * per-insert locking and MVCC publishing — changes become visible to
 * readers only after dap_global_db_batch_commit().
 *
 * @param a_tree Tree handle
 * @return 0 on success, negative on error
 */
int dap_global_db_batch_begin(dap_global_db_t *a_tree);

/**
 * @brief Commit a batch insert transaction.
 *
 * Flushes the hot-leaf, publishes the new MVCC snapshot, and releases
 * the write lock. All inserts since batch_begin() become visible atomically.
 *
 * @param a_tree Tree handle
 * @return 0 on success, negative on error
 */
int dap_global_db_batch_commit(dap_global_db_t *a_tree);

/**
 * @brief Delete an entry by driver hash key
 * @param a_tree Tree handle
 * @param a_key Driver hash key
 * @return 0 on success, 1 if not found, negative on error
 */
int dap_global_db_delete(dap_global_db_t *a_tree, const dap_global_db_key_t *a_key);

/**
 * @brief Get entry by driver hash key
 * @param a_tree Tree handle
 * @param a_key Driver hash key
 * @param a_out_text_key Output: text key (allocated, caller frees)
 * @param a_out_value Output: value data (allocated, caller frees)
 * @param a_out_value_len Output: value length
 * @param a_out_sign Output: signature (allocated, caller frees)
 * @param a_out_sign_len Output: signature length
 * @param a_out_flags Output: record flags
 * @return 0 on success, 1 if not found, negative on error
 */
int dap_global_db_fetch(dap_global_db_t *a_tree,
                      const dap_global_db_key_t *a_key,
                      char **a_out_text_key,
                      void **a_out_value, uint32_t *a_out_value_len,
                      void **a_out_sign, uint32_t *a_out_sign_len,
                      uint8_t *a_out_flags);

/**
 * @brief Check if key exists
 * @param a_tree Tree handle
 * @param a_key Driver hash key
 * @return true if exists, false otherwise
 */
bool dap_global_db_exists(dap_global_db_t *a_tree, const dap_global_db_key_t *a_key);

/**
 * @brief Get total number of entries
 * @param a_tree Tree handle
 * @return Number of entries
 */
uint64_t dap_global_db_count(dap_global_db_t *a_tree);

/**
 * @brief Clear all entries (truncate tree)
 * @param a_tree Tree handle
 * @return 0 on success, negative on error
 */
int dap_global_db_clear(dap_global_db_t *a_tree);

// ============================================================================
// Cursor Functions
// ============================================================================

/**
 * @brief Create a cursor for iteration
 * @param a_tree Tree handle
 * @return Cursor handle or NULL on error
 */
dap_global_db_cursor_t *dap_global_db_cursor_create(dap_global_db_t *a_tree);

/**
 * @brief Close cursor and free resources
 * @param a_cursor Cursor handle
 */
void dap_global_db_cursor_close(dap_global_db_cursor_t *a_cursor);

/**
 * @brief Move cursor to position
 * @param a_cursor Cursor handle
 * @param a_op Cursor operation
 * @param a_key Key for SET/SET_RANGE/SET_UPPERBOUND operations (can be NULL for FIRST/LAST/NEXT/PREV)
 * @return 0 on success, 1 if not found, negative on error
 */
int dap_global_db_cursor_move(dap_global_db_cursor_t *a_cursor,
                              dap_global_db_cursor_op_t a_op,
                              const dap_global_db_key_t *a_key);

/**
 * @brief Get current entry at cursor position
 * @param a_cursor Cursor handle
 * @param a_out_key Output: driver hash key
 * @param a_out_text_key Output: text key (allocated, caller frees)
 * @param a_out_value Output: value data (allocated, caller frees)
 * @param a_out_value_len Output: value length
 * @param a_out_sign Output: signature (allocated, caller frees)
 * @param a_out_sign_len Output: signature length
 * @param a_out_flags Output: record flags
 * @return 0 on success, 1 if cursor invalid, negative on error
 */
int dap_global_db_cursor_get(dap_global_db_cursor_t *a_cursor,
                             dap_global_db_key_t *a_out_key,
                             char **a_out_text_key,
                             void **a_out_value, uint32_t *a_out_value_len,
                             void **a_out_sign, uint32_t *a_out_sign_len,
                             uint8_t *a_out_flags);

/**
 * @brief Check if cursor is at a valid position
 * @param a_cursor Cursor handle
 * @return true if valid, false otherwise
 */
bool dap_global_db_cursor_valid(dap_global_db_cursor_t *a_cursor);

// ============================================================================
// Zero-Copy Read API
// ============================================================================

/**
 * @brief Zero-copy data reference.
 *
 * Points directly into the mmap region — no malloc, no memcpy.
 * The reference is valid until the next mutating operation on the tree
 * (insert, delete, clear) or until the tree is closed.
 * Callers MUST NOT free or modify the data.
 */
typedef struct dap_global_db_ref {
    const void *data;    // Pointer into mmap region (NULL if field absent)
    uint32_t len;        // Data length in bytes
} dap_global_db_ref_t;

/**
 * @brief Zero-copy get: returns pointers directly into mmap.
 *
 * Analogous to LMDB's mdb_get() — no memory allocation, no copying.
 * Returned references are valid until the next write operation or close.
 *
 * @param a_tree      Tree handle
 * @param a_key       Driver hash key to look up
 * @param a_out_text_key  Output: text key reference (can be NULL)
 * @param a_out_value     Output: value reference (can be NULL)
 * @param a_out_sign      Output: signature reference (can be NULL)
 * @param a_out_flags     Output: record flags (can be NULL)
 * @return 0 on success, 1 if not found, negative on error
 */
int dap_global_db_get_ref(dap_global_db_t *a_tree,
                                const dap_global_db_key_t *a_key,
                                dap_global_db_ref_t *a_out_text_key,
                                dap_global_db_ref_t *a_out_value,
                                dap_global_db_ref_t *a_out_sign,
                                uint8_t *a_out_flags);

/**
 * @brief Begin a read transaction (scoped snapshot).
 *
 * Acquires a snapshot once; subsequent get/get_ref/exists calls from
 * the same thread reuse it without per-call atomic overhead.
 * Analogous to LMDB mdb_txn_begin(MDB_RDONLY).
 * Thread-local: each thread maintains its own read transaction.
 *
 * @return 0 on success, -1 on error (snapshot slots exhausted)
 */
int dap_global_db_read_begin(dap_global_db_t *a_tree);

/**
 * @brief End a read transaction, releasing the snapshot.
 */
void dap_global_db_read_end(dap_global_db_t *a_tree);

/**
 * @brief Zero-copy cursor get: returns pointers directly into mmap.
 *
 * Like dap_global_db_cursor_get(), but avoids all allocation.
 * Returned refs are valid until cursor_move or any write operation.
 *
 * @return 0 on success, 1 if cursor invalid, negative on error
 */
int dap_global_db_cursor_get_ref(dap_global_db_cursor_t *a_cursor,
                                       dap_global_db_key_t *a_out_key,
                                       dap_global_db_ref_t *a_out_text_key,
                                       dap_global_db_ref_t *a_out_value,
                                       dap_global_db_ref_t *a_out_sign,
                                       uint8_t *a_out_flags);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Compare two B-tree keys
 * @param a_key1 First key
 * @param a_key2 Second key
 * @return <0 if key1 < key2, 0 if equal, >0 if key1 > key2
 */
int dap_global_db_key_compare(const dap_global_db_key_t *a_key1,
                              const dap_global_db_key_t *a_key2);

/**
 * @brief Check if key is blank (all zeros)
 * @param a_key Key to check
 * @return true if blank, false otherwise
 */
bool dap_global_db_key_is_blank(const dap_global_db_key_t *a_key);

/**
 * @brief Verify B-tree structural integrity (debug/test utility).
 *
 * Performs a full recursive traversal of the tree, checking:
 *   - All page_ids are valid (non-zero, within mmap bounds)
 *   - Branch pages: entries sorted, child count = entries_count + 1
 *   - Leaf pages: entries sorted, sibling links consistent with parent structure
 *   - Total entry count matches header.items_count
 *   - Tree height matches actual depth to leaves
 *
 * Acquires rdlock internally. Safe to call from any thread.
 *
 * @param a_tree           Tree handle
 * @param a_out_entry_count Output: total entries found (can be NULL)
 * @return 0 if tree is structurally valid, negative error code otherwise:
 *         -1  invalid argument
 *         -2  page read failure (invalid page_id)
 *         -3  key ordering violation
 *         -4  entry count mismatch
 *         -5  tree height mismatch
 *         -6  leaf depth inconsistency
 *         -7  sibling link inconsistency
 */
int dap_global_db_verify(dap_global_db_t *a_tree, uint64_t *a_out_entry_count);
uint64_t dap_global_db_count_at_root(dap_global_db_t *a_tree, uint64_t a_root);
void dap_global_db_debug_trace(dap_global_db_t *a_tree,
                                      const dap_global_db_key_t *a_key,
                                      const char *a_tag);
void dap_global_db_set_debug(bool a_on);

#ifdef __cplusplus
}
#endif
