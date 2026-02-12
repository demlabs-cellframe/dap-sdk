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
#include "dap_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Constants
// ============================================================================

#define DAP_GLOBAL_DB_BTREE_MAGIC         0x4E424447  // "GDBN" in little-endian
#define DAP_GLOBAL_DB_BTREE_VERSION       1
#define DAP_GLOBAL_DB_BTREE_PAGE_SIZE     4096
#define DAP_GLOBAL_DB_BTREE_MIN_KEYS      2           // Minimum keys per node (t-1 for B-tree order t)
#define DAP_GLOBAL_DB_BTREE_MAX_KEYS      ((DAP_GLOBAL_DB_BTREE_PAGE_SIZE - 64) / 32)  // ~126 keys per page

// Page flags
#define DAP_GLOBAL_DB_PAGE_BRANCH         0x0001
#define DAP_GLOBAL_DB_PAGE_LEAF           0x0002
#define DAP_GLOBAL_DB_PAGE_OVERFLOW       0x0004
#define DAP_GLOBAL_DB_PAGE_ROOT           0x0008

// ============================================================================
// Types
// ============================================================================

/**
 * @brief B-tree key type - matches dap_global_db_hash_t
 */
typedef struct dap_global_db_btree_key {
    uint64_t bets;      // timestamp in big-endian
    uint64_t becrc;     // CRC in big-endian
} DAP_ALIGN_PACKED dap_global_db_btree_key_t;

/**
 * @brief B-tree file header (64 bytes)
 */
typedef struct dap_global_db_btree_header {
    uint32_t magic;             // DAP_GLOBAL_DB_BTREE_MAGIC
    uint16_t version;           // DAP_GLOBAL_DB_BTREE_VERSION
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
} DAP_ALIGN_PACKED dap_global_db_btree_header_t;

_Static_assert(sizeof(dap_global_db_btree_header_t) == 64, "Header must be 64 bytes");

/**
 * @brief B-tree page header (32 bytes)
 */
typedef struct dap_global_db_btree_page_header {
    uint16_t flags;             // Page flags (BRANCH/LEAF/OVERFLOW)
    uint16_t entries_count;     // Number of entries in page
    uint32_t free_space;        // Free space in page
    uint64_t page_id;           // This page's ID/offset
    uint64_t right_sibling;     // Right sibling page (for sequential scan)
    uint64_t parent;            // Parent page (0 for root)
} DAP_ALIGN_PACKED dap_global_db_btree_page_header_t;

_Static_assert(sizeof(dap_global_db_btree_page_header_t) == 32, "Page header must be 32 bytes");

/**
 * @brief Leaf entry header (variable size)
 * Followed by: text_key[key_len] + value[value_len] + sign[sign_len]
 */
typedef struct dap_global_db_btree_leaf_entry {
    dap_global_db_btree_key_t driver_hash;    // 16 bytes - sort key
    uint32_t key_len;                    // Text key length
    uint32_t value_len;                  // Value length
    uint32_t sign_len;                   // Signature length
    uint8_t flags;                       // Record flags
    uint8_t reserved[3];                 // Alignment
    // Followed by: key_data[key_len] + value_data[value_len] + sign_data[sign_len]
} DAP_ALIGN_PACKED dap_global_db_btree_leaf_entry_t;

/**
 * @brief Branch entry (24 bytes)
 */
typedef struct dap_global_db_btree_branch_entry {
    dap_global_db_btree_key_t driver_hash;    // 16 bytes - separator key
    uint64_t child_page;                 // Child page offset
} DAP_ALIGN_PACKED dap_global_db_btree_branch_entry_t;

_Static_assert(sizeof(dap_global_db_btree_branch_entry_t) == 24, "Branch entry must be 24 bytes");

/**
 * @brief B-tree page structure (in-memory representation)
 */
typedef struct dap_global_db_btree_page {
    dap_global_db_btree_page_header_t header;
    bool is_dirty;                       // Page has been modified
    uint8_t *data;                       // Page data (entries area)
    // For branch pages: array of branch entries
    // For leaf pages: variable-size leaf entries
} dap_global_db_btree_page_t;

/**
 * @brief B-tree handle
 */
typedef struct dap_global_db_btree {
    int fd;                              // File descriptor
    char *filepath;                      // File path
    dap_global_db_btree_header_t header;       // Cached header
    dap_global_db_btree_page_t *root;          // Cached root page (may be NULL)
    bool read_only;                      // Read-only mode
    uint64_t txn_id;                     // Current transaction ID
} dap_global_db_btree_t;

/**
 * @brief B-tree cursor for iteration
 */
typedef struct dap_global_db_btree_cursor {
    dap_global_db_btree_t *tree;               // Tree handle
    dap_global_db_btree_page_t *current_page;  // Current leaf page
    uint16_t current_index;              // Current entry index in page
    bool valid;                          // Cursor is valid
    bool at_end;                         // Cursor at end (no more entries)
} dap_global_db_btree_cursor_t;

/**
 * @brief Cursor position operations
 */
typedef enum dap_global_db_btree_cursor_op {
    DAP_GLOBAL_DB_BTREE_FIRST,         // Position at first entry
    DAP_GLOBAL_DB_BTREE_LAST,          // Position at last entry
    DAP_GLOBAL_DB_BTREE_NEXT,          // Move to next entry
    DAP_GLOBAL_DB_BTREE_PREV,          // Move to previous entry
    DAP_GLOBAL_DB_BTREE_SET,           // Position at exact key
    DAP_GLOBAL_DB_BTREE_SET_RANGE,     // Position at key or next greater
    DAP_GLOBAL_DB_BTREE_SET_UPPERBOUND // Position at first key > given key
} dap_global_db_btree_cursor_op_t;

// ============================================================================
// Functions
// ============================================================================

/**
 * @brief Create a new B-tree file
 * @param a_filepath Path to the B-tree file
 * @return Tree handle or NULL on error
 */
dap_global_db_btree_t *dap_global_db_btree_create(const char *a_filepath);

/**
 * @brief Open an existing B-tree file
 * @param a_filepath Path to the B-tree file
 * @param a_read_only Open in read-only mode
 * @return Tree handle or NULL on error
 */
dap_global_db_btree_t *dap_global_db_btree_open(const char *a_filepath, bool a_read_only);

/**
 * @brief Close B-tree and free resources
 * @param a_tree Tree handle
 */
void dap_global_db_btree_close(dap_global_db_btree_t *a_tree);

/**
 * @brief Sync all dirty pages to disk
 * @param a_tree Tree handle
 * @return 0 on success, negative on error
 */
int dap_global_db_btree_sync(dap_global_db_btree_t *a_tree);

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
int dap_global_db_btree_insert(dap_global_db_btree_t *a_tree,
                         const dap_global_db_btree_key_t *a_key,
                         const char *a_text_key, uint32_t a_text_key_len,
                         const void *a_value, uint32_t a_value_len,
                         const void *a_sign, uint32_t a_sign_len,
                         uint8_t a_flags);

/**
 * @brief Delete an entry by driver hash key
 * @param a_tree Tree handle
 * @param a_key Driver hash key
 * @return 0 on success, 1 if not found, negative on error
 */
int dap_global_db_btree_delete(dap_global_db_btree_t *a_tree, const dap_global_db_btree_key_t *a_key);

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
int dap_global_db_btree_get(dap_global_db_btree_t *a_tree,
                      const dap_global_db_btree_key_t *a_key,
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
bool dap_global_db_btree_exists(dap_global_db_btree_t *a_tree, const dap_global_db_btree_key_t *a_key);

/**
 * @brief Get total number of entries
 * @param a_tree Tree handle
 * @return Number of entries
 */
uint64_t dap_global_db_btree_count(dap_global_db_btree_t *a_tree);

/**
 * @brief Clear all entries (truncate tree)
 * @param a_tree Tree handle
 * @return 0 on success, negative on error
 */
int dap_global_db_btree_clear(dap_global_db_btree_t *a_tree);

// ============================================================================
// Cursor Functions
// ============================================================================

/**
 * @brief Create a cursor for iteration
 * @param a_tree Tree handle
 * @return Cursor handle or NULL on error
 */
dap_global_db_btree_cursor_t *dap_global_db_btree_cursor_create(dap_global_db_btree_t *a_tree);

/**
 * @brief Close cursor and free resources
 * @param a_cursor Cursor handle
 */
void dap_global_db_btree_cursor_close(dap_global_db_btree_cursor_t *a_cursor);

/**
 * @brief Move cursor to position
 * @param a_cursor Cursor handle
 * @param a_op Cursor operation
 * @param a_key Key for SET/SET_RANGE/SET_UPPERBOUND operations (can be NULL for FIRST/LAST/NEXT/PREV)
 * @return 0 on success, 1 if not found, negative on error
 */
int dap_global_db_btree_cursor_move(dap_global_db_btree_cursor_t *a_cursor,
                              dap_global_db_btree_cursor_op_t a_op,
                              const dap_global_db_btree_key_t *a_key);

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
int dap_global_db_btree_cursor_get(dap_global_db_btree_cursor_t *a_cursor,
                             dap_global_db_btree_key_t *a_out_key,
                             char **a_out_text_key,
                             void **a_out_value, uint32_t *a_out_value_len,
                             void **a_out_sign, uint32_t *a_out_sign_len,
                             uint8_t *a_out_flags);

/**
 * @brief Check if cursor is at a valid position
 * @param a_cursor Cursor handle
 * @return true if valid, false otherwise
 */
bool dap_global_db_btree_cursor_valid(dap_global_db_btree_cursor_t *a_cursor);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Compare two B-tree keys
 * @param a_key1 First key
 * @param a_key2 Second key
 * @return <0 if key1 < key2, 0 if equal, >0 if key1 > key2
 */
int dap_global_db_btree_key_compare(const dap_global_db_btree_key_t *a_key1,
                              const dap_global_db_btree_key_t *a_key2);

/**
 * @brief Check if key is blank (all zeros)
 * @param a_key Key to check
 * @return true if blank, false otherwise
 */
bool dap_global_db_btree_key_is_blank(const dap_global_db_btree_key_t *a_key);

#ifdef __cplusplus
}
#endif
