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

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_global_db_btree.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>

#include "dap_file_ops.h"

#ifdef DAP_OS_WINDOWS
#define O_SYNC 0
#endif

#define LOG_TAG "dap_global_db_btree"

// ============================================================================
// Internal Constants
// ============================================================================

#define BTREE_HEADER_OFFSET     0
#define BTREE_DATA_OFFSET       DAP_GLOBAL_DB_PAGE_SIZE

// B-tree order: minimum degree t = MIN_KEYS + 1
// Each node can have at most 2t-1 keys and 2t children
#define BTREE_ORDER             (DAP_GLOBAL_DB_MIN_KEYS + 1)
#define BTREE_MAX_CHILDREN      (2 * BTREE_ORDER)

// Page layout constants
#define PAGE_DATA_SIZE          (DAP_GLOBAL_DB_PAGE_SIZE - sizeof(dap_global_db_page_header_t))
/**
 * Payload (value+sign) larger than this is stored in an overflow page chain.
 * Threshold must guarantee at least 2 entries per leaf page — otherwise
 * s_split_child degenerates (l_mid = l_count/2 = 0 for 1-entry pages).
 */
#define MAX_INLINE_PAYLOAD      ((PAGE_DATA_SIZE - LEAF_HEADER_SIZE - 2 * LEAF_OFFSET_SIZE) / 2 \
                                  - sizeof(dap_global_db_leaf_entry_t))

// Leaf page data layout constants (moved here for s_page_alloc visibility)
#define LEAF_HEADER_SIZE        sizeof(uint16_t)  // lowest_used_offset cache (O(1) append)
#define LEAF_OFFSET_SIZE        sizeof(uint16_t)  // each offset entry

// Cached lowest-used-offset stored in the first 2 bytes of leaf data.
// Eliminates O(n) min-offset scan in s_leaf_insert_entry.
#define LEAF_LOWEST_OFFSET(data)    (*(uint16_t *)(data))
#define LEAF_LOWEST_OFFSET_INIT     ((uint16_t)PAGE_DATA_SIZE)

static bool s_debug_more = false;
void dap_global_db_set_debug(bool a_on) { s_debug_more = a_on; }

// ============================================================================
// Forward Declarations
// ============================================================================

// Header I/O
static int s_header_read(dap_global_db_t *a_tree);
static int s_header_write(dap_global_db_t *a_tree);
static uint64_t s_header_checksum(dap_global_db_header_t *a_header);

// Inner implementations (no locking) — used by close() and recursive insert
static int s_btree_sync_impl(dap_global_db_t *a_tree);

// Page lifecycle — arena-aware: when a_arena is non-NULL, allocations use the
// bump allocator (O(1) pointer arithmetic) instead of malloc. Arena pages
// are freed in bulk via dap_arena_reset(), not individually.
static dap_global_db_page_t *s_page_alloc(dap_arena_t *a_arena);
static void s_page_free(dap_global_db_page_t *a_page);
static dap_global_db_page_t *s_page_read(dap_global_db_t *a_tree, uint64_t a_page_id, dap_arena_t *a_arena);
static int s_page_write(dap_global_db_t *a_tree, dap_global_db_page_t *a_page);
static uint64_t s_page_allocate_new(dap_global_db_t *a_tree);

// COW — detach mmap ref into a private buffer (arena or heap)
static void s_page_cow(dap_global_db_page_t *a_page, dap_arena_t *a_arena);

// Overflow chain for values larger than one page
static uint64_t s_overflow_write(dap_global_db_t *a_tree, const void *a_value, uint32_t a_value_len,
                                 const void *a_sign, uint32_t a_sign_len, uint64_t a_txn);
static int s_overflow_read(dap_global_db_t *a_tree, uint64_t a_first_page_id,
                           void *a_out_buf, size_t a_buf_size, size_t *a_out_len);
static void s_overflow_free(dap_global_db_t *a_tree, uint64_t a_first_page_id, uint64_t a_txn);
static uint64_t s_page_allocate_contiguous(dap_global_db_t *a_tree, uint32_t a_count);

// B-tree navigation (read-only, no arena needed)
static int s_search_in_page(dap_global_db_page_t *a_page, const dap_global_db_key_t *a_key, bool *a_found);

// Branch page entry access — all mutations, tree needed for arena
static dap_global_db_branch_entry_t *s_branch_entry_at(dap_global_db_page_t *a_page, int a_index);
static void s_branch_set_child(dap_global_db_page_t *a_page, int a_index, uint64_t a_child, dap_arena_t *a_arena);
static void s_branch_set_entry(dap_global_db_page_t *a_page, int a_index, const dap_global_db_key_t *a_key, uint64_t a_child, dap_arena_t *a_arena);
static void s_branch_insert_entry(dap_global_db_page_t *a_page, int a_index, const dap_global_db_key_t *a_key, uint64_t a_child, dap_arena_t *a_arena);
static void s_branch_remove_entry(dap_global_db_page_t *a_page, int a_index, dap_arena_t *a_arena);

// Leaf page entry access
static int s_leaf_entry_count(dap_global_db_page_t *a_page);
static dap_global_db_leaf_entry_t *s_leaf_entry_at(dap_global_db_page_t *a_page, int a_index,
                                                    uint8_t **a_out_data, size_t *a_out_total_size);
static int s_leaf_find_entry(dap_global_db_page_t *a_page, const dap_global_db_key_t *a_key, int *a_out_index);
static inline size_t s_leaf_entry_total_size(uint32_t a_key_len, uint32_t a_value_len, uint32_t a_sign_len);

// Leaf mutations — arena-aware
static int s_leaf_insert_entry(dap_global_db_page_t *a_page, int a_index,
                               const dap_global_db_key_t *a_key, const char *a_text_key, uint32_t a_text_key_len,
                               const void *a_value, uint32_t a_value_len, const void *a_sign, uint32_t a_sign_len,
                               uint8_t a_flags, dap_arena_t *a_arena);
static int s_leaf_insert_entry_overflow(dap_global_db_page_t *a_page, int a_index,
                                        const dap_global_db_key_t *a_key, const char *a_text_key, uint32_t a_text_key_len,
                                        uint64_t a_overflow_page_id, uint32_t a_value_len, uint32_t a_sign_len,
                                        uint8_t a_flags, dap_arena_t *a_arena);
static int s_leaf_delete_entry(dap_global_db_page_t *a_page, int a_index, dap_arena_t *a_arena);
static int s_leaf_update_entry(dap_global_db_page_t *a_page, int a_index,
                               const char *a_text_key, uint32_t a_text_key_len,
                               const void *a_value, uint32_t a_value_len,
                               const void *a_sign, uint32_t a_sign_len,
                               uint8_t a_flags, dap_arena_t *a_arena,
                               dap_global_db_t *a_tree, uint64_t a_txn);
static void s_leaf_compact(dap_global_db_page_t *a_page, dap_arena_t *a_arena);

// Page checks
static inline bool s_header_needs_split(const dap_global_db_page_header_t *a_hdr,
                                        uint32_t a_text_key_len, uint32_t a_value_len,
                                        uint32_t a_sign_len);
static bool s_page_needs_split(dap_global_db_page_t *a_page, uint32_t a_text_key_len,
                               uint32_t a_value_len, uint32_t a_sign_len);

// Split & insert
static int s_split_child(dap_global_db_t *a_tree, dap_global_db_page_t *a_parent,
                          int a_index, dap_global_db_page_t **a_out_child);
static int s_insert_non_full(dap_global_db_t *a_tree, dap_global_db_page_t *a_page,
                             const dap_global_db_key_t *a_key, const char *a_text_key, uint32_t a_text_key_len,
                             const void *a_value, uint32_t a_value_len, const void *a_sign, uint32_t a_sign_len,
                             uint8_t a_flags);

// Hot leaf management
static void s_hot_leaf_flush(dap_global_db_t *a_tree);

// MVCC helpers
static int s_snapshot_acquire(dap_global_db_t *a_tree, uint64_t *a_out_root, uint64_t *a_out_txn, uint64_t *a_out_count);
static void s_snapshot_release(dap_global_db_t *a_tree, int a_slot);
static inline bool s_has_active_snapshots(const dap_global_db_t *a_tree);
static void s_deferred_free_add(dap_global_db_t *a_tree, uint64_t a_txn, uint64_t a_page_id);
static void s_deferred_free_reclaim(dap_global_db_t *a_tree);
static uint64_t s_cow_chain_up(dap_global_db_t *a_tree,
                                const struct dap_btree_path_entry *a_path,
                                int a_path_depth, uint64_t a_new_child_id, uint64_t a_txn);
/** COW a leaf page and set one sibling link (for MVCC: no in-place neighbor update). Returns new page id or 0. */
static uint64_t s_cow_leaf_update_sibling(dap_global_db_t *a_tree, uint64_t a_leaf_id,
                                           int a_set_right, uint64_t a_new_sibling_id, uint64_t a_txn,
                                           dap_arena_t *a_arena);
static void s_mvcc_commit(dap_global_db_t *a_tree);

// ============================================================================
// Key Comparison
// ============================================================================

// Keys are stored big-endian; memcmp gives correct ordering but goes through PLT
// on packed structs. Compare two uint64 fields with bswap64 for native-endian ordering.
static inline int s_key_cmp(const dap_global_db_key_t *a_key1,
                            const dap_global_db_key_t *a_key2)
{
    uint64_t a1, b1;
    memcpy(&a1, &a_key1->bets, 8);
    memcpy(&b1, &a_key2->bets, 8);
    if (a1 != b1) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return __builtin_bswap64(a1) < __builtin_bswap64(b1) ? -1 : 1;
#else
        return a1 < b1 ? -1 : 1;
#endif
    }
    uint64_t a2, b2;
    memcpy(&a2, &a_key1->becrc, 8);
    memcpy(&b2, &a_key2->becrc, 8);
    if (a2 != b2) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return __builtin_bswap64(a2) < __builtin_bswap64(b2) ? -1 : 1;
#else
        return a2 < b2 ? -1 : 1;
#endif
    }
    return 0;
}

// Public API wrapper (for external callers)
int dap_global_db_key_compare(const dap_global_db_key_t *a_key1, const dap_global_db_key_t *a_key2)
{
    int l_ret = s_key_cmp(a_key1, a_key2);
    return l_ret < 0 ? -1 : (l_ret > 0 ? 1 : 0);
}

bool dap_global_db_key_is_blank(const dap_global_db_key_t *a_key)
{
    static const dap_global_db_key_t s_blank = {0};
    return memcmp(a_key, &s_blank, sizeof(dap_global_db_key_t)) == 0;
}

// ============================================================================
// Header Operations
// ============================================================================

static uint64_t s_header_checksum(dap_global_db_header_t *a_header)
{
    // Simple checksum: sum of all 64-bit words except checksum field
    // Copy to aligned buffer to avoid unaligned access
    uint64_t l_sum = 0;
    byte_t *l_ptr = (byte_t *)a_header;
    size_t l_bytes = sizeof(dap_global_db_header_t) - sizeof(uint64_t);
    for (size_t i = 0; i < l_bytes; i += sizeof(uint64_t)) {
        uint64_t l_val;
        memcpy(&l_val, l_ptr + i, sizeof(uint64_t));
        l_sum ^= l_val;
    }
    return l_sum;
}

static int s_header_read(dap_global_db_t *a_tree)
{
    if (a_tree->mmap) {
        void *l_base = dap_mmap_get_ptr(a_tree->mmap);
        memcpy(&a_tree->header, l_base, sizeof(dap_global_db_header_t));
    } else {
        if (lseek(a_tree->fd, BTREE_HEADER_OFFSET, SEEK_SET) < 0) {
            log_it(L_ERROR, "Failed to seek to header: %s", strerror(errno));
            return -1;
        }
        ssize_t l_read = read(a_tree->fd, &a_tree->header, sizeof(dap_global_db_header_t));
        if (l_read != sizeof(dap_global_db_header_t)) {
            log_it(L_ERROR, "Failed to read header: %s", strerror(errno));
            return -1;
        }
    }

    // Validate header
    if (a_tree->header.magic != DAP_GLOBAL_DB_MAGIC) {
        log_it(L_ERROR, "Invalid B-tree magic: 0x%08X", a_tree->header.magic);
        return -1;
    }
    
    if (a_tree->header.version != DAP_GLOBAL_DB_STORAGE_VERSION) {
        log_it(L_ERROR, "Unsupported B-tree version: %u", a_tree->header.version);
        return -1;
    }
    
    uint64_t l_checksum = s_header_checksum(&a_tree->header);
    if (l_checksum != a_tree->header.checksum) {
        log_it(L_ERROR, "Header checksum mismatch");
        return -1;
    }
    
    return 0;
}

static int s_header_write(dap_global_db_t *a_tree)
{
    a_tree->header.checksum = s_header_checksum(&a_tree->header);

    if (a_tree->mmap) {
        void *l_base = dap_mmap_get_ptr(a_tree->mmap);
        memcpy(l_base, &a_tree->header, sizeof(dap_global_db_header_t));
        return 0;
    }

    if (lseek(a_tree->fd, BTREE_HEADER_OFFSET, SEEK_SET) < 0) {
        log_it(L_ERROR, "Failed to seek to header: %s", strerror(errno));
        return -1;
    }
    
    ssize_t l_written = write(a_tree->fd, &a_tree->header, sizeof(dap_global_db_header_t));
    if (l_written != sizeof(dap_global_db_header_t)) {
        log_it(L_ERROR, "Failed to write header: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

// ============================================================================
// Page Operations
// ============================================================================

static dap_global_db_page_t *s_page_alloc(dap_arena_t *a_arena)
{
    dap_global_db_page_t *l_page;
    if (a_arena) {
        l_page = dap_arena_alloc_zero(a_arena, sizeof(*l_page));
        if (!l_page)
            return NULL;
        l_page->data = dap_arena_alloc(a_arena, PAGE_DATA_SIZE);
        if (!l_page->data)
            return NULL;
        memset(l_page->data, 0, PAGE_DATA_SIZE);
        l_page->is_arena = true;
    } else {
        l_page = DAP_NEW_Z(dap_global_db_page_t);
        if (!l_page)
            return NULL;
        l_page->data = DAP_NEW_Z_SIZE(uint8_t, PAGE_DATA_SIZE);
        if (!l_page->data) {
            DAP_DELETE(l_page);
            return NULL;
        }
    }
    l_page->header.free_space = PAGE_DATA_SIZE;
    // Initialize lowest-offset cache for leaf pages (branch pages ignore this field).
    // Set to PAGE_DATA_SIZE = "no entries yet, next entry goes at end minus its size".
    LEAF_LOWEST_OFFSET(l_page->data) = LEAF_LOWEST_OFFSET_INIT;
    return l_page;
}

static void s_page_free(dap_global_db_page_t *a_page)
{
    if (!a_page || a_page->is_arena)
        return;  // Arena handles lifetime via dap_arena_reset()
    if (!a_page->is_mmap_ref)
        DAP_DEL_Z(a_page->data);
    DAP_DELETE(a_page);
}

/**
 * @brief Ensure page data is in a private (writable) buffer, not an mmap reference.
 *
 * Writable mmap refs (is_mmap_writable=true) are already safe to modify
 * in-place — COW is skipped for them. Inlined because the writable-mmap
 * fast path (just a branch-and-return) is hit on every leaf insert/update.
 */
static inline void s_page_cow(dap_global_db_page_t *a_page, dap_arena_t *a_arena)
{
    if (!a_page || !a_page->is_mmap_ref || a_page->is_mmap_writable)
        return;
    uint8_t *l_copy;
    if (a_arena) {
        l_copy = dap_arena_alloc(a_arena, PAGE_DATA_SIZE);
        a_page->is_arena = true;
    } else {
        l_copy = DAP_NEW_SIZE(uint8_t, PAGE_DATA_SIZE);
    }
    memcpy(l_copy, a_page->data, PAGE_DATA_SIZE);
    a_page->data = l_copy;
    a_page->is_mmap_ref = false;
}

static inline uint64_t s_page_offset(uint64_t a_page_id)
{
    return BTREE_DATA_OFFSET + (a_page_id - 1) * DAP_GLOBAL_DB_PAGE_SIZE;
}

static dap_global_db_page_t *s_page_read(dap_global_db_t *a_tree, uint64_t a_page_id, dap_arena_t *a_arena)
{
    if (a_page_id == 0)
        return NULL;

    // mmap fast path: memcpy from mapped region (no syscalls, no lseek)
    if (a_tree->mmap) {
        uint64_t l_offset = s_page_offset(a_page_id);
        if (l_offset + DAP_GLOBAL_DB_PAGE_SIZE > dap_mmap_get_size(a_tree->mmap))
            return NULL;
        uint8_t *l_src = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap) + l_offset;
        dap_global_db_page_t *l_page = s_page_alloc(a_arena);
        if (!l_page)
            return NULL;
        memcpy(&l_page->header, l_src, sizeof(dap_global_db_page_header_t));
        memcpy(l_page->data, l_src + sizeof(dap_global_db_page_header_t), PAGE_DATA_SIZE);
        l_page->is_dirty = false;
        return l_page;
    }

    // Legacy fallback: read()/lseek()
    dap_global_db_page_t *l_page = s_page_alloc(a_arena);
    if (!l_page)
        return NULL;
    
    // pread: single syscall per chunk, no lseek needed
    uint64_t l_offset = s_page_offset(a_page_id);
    ssize_t l_read = pread(a_tree->fd, &l_page->header,
                           sizeof(dap_global_db_page_header_t), l_offset);
    if (l_read != (ssize_t)sizeof(dap_global_db_page_header_t)) {
        log_it(L_ERROR, "Failed to pread page header: %s", strerror(errno));
        s_page_free(l_page);
        return NULL;
    }
    l_read = pread(a_tree->fd, l_page->data, PAGE_DATA_SIZE,
                   l_offset + sizeof(dap_global_db_page_header_t));
    if (l_read != (ssize_t)PAGE_DATA_SIZE) {
        log_it(L_ERROR, "Failed to pread page data: %s", strerror(errno));
        s_page_free(l_page);
        return NULL;
    }
    
    l_page->is_dirty = false;
    return l_page;
}

/**
 * @brief Zero-copy page read for read-only operations (get, exists, cursor).
 *
 * Returns a stack-friendly page whose data pointer references the mmap region
 * directly — NO malloc, NO memcpy. The caller MUST NOT:
 *   1. Call s_page_free() on the result (it's not heap-allocated page->data)
 *   2. Use this during operations that may call s_page_allocate_new() (which
 *      can mremap and relocate the mapping base address).
 *
 * Falls back to s_page_read() when mmap is unavailable.
 * The returned page is filled into caller-provided struct.
 *
 * @return true on success, false on error
 */
static bool s_page_read_ref(dap_global_db_t *a_tree, uint64_t a_page_id,
                            dap_global_db_page_t *a_out)
{
    if (a_page_id == 0 || !a_out)
        return false;

    if (a_tree->mmap) {
        uint64_t l_offset = s_page_offset(a_page_id);
        if (l_offset + DAP_GLOBAL_DB_PAGE_SIZE > dap_mmap_get_size(a_tree->mmap))
            return false;
        uint8_t *l_src = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap) + l_offset;
        // True zero-copy: cast header directly from mmap — 0 memcpy.
        // LMDB approach: (MDB_page*)(map + offset). Safe because we're
        // read-only and the mmap region won't be remapped during reads.
        const dap_global_db_page_header_t *l_hdr =
            (const dap_global_db_page_header_t *)l_src;
        a_out->header = *l_hdr;
        a_out->data = l_src + sizeof(dap_global_db_page_header_t);
        a_out->is_mmap_ref = true;
        a_out->is_dirty = false;
        return true;
    }

    return false;
}

/**
 * @brief Get a writable mmap-backed page for direct in-place modification.
 *
 * Like s_page_read_ref, but sets is_mmap_writable=true so that:
 *   1. s_page_cow() is a no-op (modifications go directly into mmap)
 *   2. s_page_write() only writes the 32-byte header (data is already in mmap)
 *   3. s_page_free() doesn't free the data pointer
 *
 * @return true on success, false if mmap not available or page out of range
 */
static bool s_page_read_writable(dap_global_db_t *a_tree, uint64_t a_page_id,
                                 dap_global_db_page_t *a_out)
{
    if (a_page_id == 0 || !a_out || !a_tree->mmap || a_tree->read_only)
        return false;

    uint64_t l_offset = s_page_offset(a_page_id);
    if (l_offset + DAP_GLOBAL_DB_PAGE_SIZE > dap_mmap_get_size(a_tree->mmap))
        return false;
    uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap) + l_offset;
    memcpy(&a_out->header, l_ptr, sizeof(dap_global_db_page_header_t));
    a_out->data = l_ptr + sizeof(dap_global_db_page_header_t);
    a_out->is_mmap_ref = true;
    a_out->is_mmap_writable = true;
    a_out->is_dirty = false;
    return true;
}

/**
 * @brief Re-resolve a writable mmap page's data pointer after a potential mremap.
 *
 * Called after s_page_allocate_new() which may call dap_mmap_resize(), invalidating
 * all outstanding mmap pointers.
 */
static void s_page_resolve_mmap(dap_global_db_t *a_tree, dap_global_db_page_t *a_page)
{
    if (!a_page || !a_page->is_mmap_ref || !a_tree->mmap)
        return;
    uint64_t l_offset = s_page_offset(a_page->header.page_id);
    uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap) + l_offset;
    a_page->data = l_ptr + sizeof(dap_global_db_page_header_t);
}

static int s_page_write(dap_global_db_t *a_tree, dap_global_db_page_t *a_page)
{
    if (!a_page || a_tree->read_only)
        return -1;

    uint64_t l_offset = s_page_offset(a_page->header.page_id);

    // mmap fast path
    if (a_tree->mmap) {
        if (l_offset + DAP_GLOBAL_DB_PAGE_SIZE > dap_mmap_get_size(a_tree->mmap)) {
            log_it(L_ERROR, "Page %lu offset beyond mmap size", (unsigned long)a_page->header.page_id);
            return -1;
        }
        uint8_t *l_dst = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap) + l_offset;

        // Writable mmap ref: data was modified in-place inside the mmap region,
        // only the 32-byte header (which lives on the stack/struct) needs writeback.
        // This is the LMDB WRITEMAP approach — zero data copies.
        if (a_page->is_mmap_writable) {
            memcpy(l_dst, &a_page->header, sizeof(dap_global_db_page_header_t));
            a_page->is_dirty = false;
            return 0;
        }

        // Heap-backed page: write header + data
        memcpy(l_dst, &a_page->header, sizeof(dap_global_db_page_header_t));
        uint8_t *l_dst_data = l_dst + sizeof(dap_global_db_page_header_t);
        if (a_page->data != l_dst_data)
            memcpy(l_dst_data, a_page->data, PAGE_DATA_SIZE);
        a_page->is_dirty = false;
        return 0;
    }

    // Legacy fallback: pwrite (single syscall, no lseek needed)
    // Write header + data as two pwrite calls (avoids temp buffer for concatenation)
    ssize_t l_written = pwrite(a_tree->fd, &a_page->header,
                               sizeof(dap_global_db_page_header_t), l_offset);
    if (l_written != (ssize_t)sizeof(dap_global_db_page_header_t)) {
        log_it(L_ERROR, "Failed to pwrite page header: %s", strerror(errno));
        return -1;
    }
    l_written = pwrite(a_tree->fd, a_page->data, PAGE_DATA_SIZE,
                       l_offset + sizeof(dap_global_db_page_header_t));
    if (l_written != (ssize_t)PAGE_DATA_SIZE) {
        log_it(L_ERROR, "Failed to pwrite page data: %s", strerror(errno));
        return -1;
    }
    
    a_page->is_dirty = false;
    return 0;
}

// ============================================================================
// Overflow chain: values larger than PAGE_DATA_SIZE
// ============================================================================
// Overflow pages use standard page header; right_sibling = next page id (0 = last).
// Data area = raw payload. No offset array.

static uint64_t s_overflow_write(dap_global_db_t *a_tree, const void *a_value, uint32_t a_value_len,
                                 const void *a_sign, uint32_t a_sign_len, uint64_t a_txn)
{
    size_t l_total = (size_t)a_value_len + (size_t)a_sign_len;
    if (l_total == 0)
        return 0;

    if (a_tree->mmap && l_total > PAGE_DATA_SIZE) {
        uint32_t l_cont = (uint32_t)((l_total - PAGE_DATA_SIZE + DAP_GLOBAL_DB_PAGE_SIZE - 1)
                                     / DAP_GLOBAL_DB_PAGE_SIZE);
        uint32_t l_page_count = 1 + l_cont;
        uint64_t l_first_id = s_page_allocate_contiguous(a_tree, l_page_count);
        if (l_first_id == 0)
            return 0;

        uint8_t *l_base = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap);
        uint64_t l_off = s_page_offset(l_first_id);

        dap_global_db_page_header_t *l_hdr = (dap_global_db_page_header_t *)(l_base + l_off);
        l_hdr->flags = DAP_GLOBAL_DB_PAGE_OVERFLOW | DAP_GLOBAL_DB_PAGE_OVERFLOW_CONTIGUOUS;
        l_hdr->entries_count = (uint16_t)l_cont;
        l_hdr->free_space = 0;
        l_hdr->page_id = l_first_id;
        l_hdr->right_sibling = 0;
        l_hdr->left_sibling = 0;

        uint8_t *l_dst = l_base + l_off + sizeof(dap_global_db_page_header_t);
        if (a_value_len > 0)
            memcpy(l_dst, a_value, a_value_len);
        if (a_sign_len > 0)
            memcpy(l_dst + a_value_len, a_sign, a_sign_len);

        return l_first_id;
    }

    const uint8_t *l_src = (const uint8_t *)a_value;
    size_t l_value_done = 0;
    size_t l_sign_done = 0;
    uint64_t l_first_id = 0;
    uint64_t l_prev_id = 0;

    while (l_value_done < a_value_len || l_sign_done < a_sign_len) {
        uint64_t l_page_id = s_page_allocate_new(a_tree);
        if (l_page_id == 0)
            goto fail;
        if (l_first_id == 0)
            l_first_id = l_page_id;

        dap_global_db_page_t *l_page = s_page_alloc(a_tree->arena);
        if (!l_page)
            goto fail;
        l_page->header.page_id = l_page_id;
        l_page->header.flags = DAP_GLOBAL_DB_PAGE_OVERFLOW;
        l_page->header.entries_count = 0;
        l_page->header.free_space = 0;
        l_page->header.right_sibling = 0;
        l_page->header.left_sibling = 0;

        size_t l_chunk = 0;
        if (l_value_done < a_value_len) {
            size_t l_from_value = a_value_len - l_value_done;
            if (l_from_value > PAGE_DATA_SIZE)
                l_from_value = PAGE_DATA_SIZE;
            memmove(l_page->data, l_src + l_value_done, l_from_value);
            l_chunk += l_from_value;
            l_value_done += l_from_value;
        }
        if (l_chunk < PAGE_DATA_SIZE && l_sign_done < a_sign_len) {
            size_t l_from_sign = a_sign_len - l_sign_done;
            size_t l_room = PAGE_DATA_SIZE - l_chunk;
            if (l_from_sign > l_room)
                l_from_sign = l_room;
            memmove(l_page->data + l_chunk, (const uint8_t *)a_sign + l_sign_done, l_from_sign);
            l_chunk += l_from_sign;
            l_sign_done += l_from_sign;
        }

        if (l_prev_id != 0) {
            dap_global_db_page_t *l_prev = s_page_read(a_tree, l_prev_id, a_tree->arena);
            if (l_prev) {
                l_prev->header.right_sibling = l_page_id;
                s_page_write(a_tree, l_prev);
            }
        }
        s_page_write(a_tree, l_page);
        l_prev_id = l_page_id;
    }
    return l_first_id;
fail:
    if (l_first_id != 0)
        s_overflow_free(a_tree, l_first_id, a_txn);
    return 0;
}

static int s_overflow_read(dap_global_db_t *a_tree, uint64_t a_first_page_id,
                           void *a_out_buf, size_t a_buf_size, size_t *a_out_len)
{
    if (a_first_page_id == 0 || !a_out_buf || a_buf_size == 0)
        return -1;

    if (a_tree->mmap) {
        const uint8_t *l_base = (const uint8_t *)dap_mmap_get_ptr(a_tree->mmap);
        size_t l_mmap_size = dap_mmap_get_size(a_tree->mmap);
        uint64_t l_off = s_page_offset(a_first_page_id);
        if (l_off + DAP_GLOBAL_DB_PAGE_SIZE > l_mmap_size)
            return -1;
        const dap_global_db_page_header_t *l_hdr =
            (const dap_global_db_page_header_t *)(l_base + l_off);

        if (l_hdr->flags & DAP_GLOBAL_DB_PAGE_OVERFLOW_CONTIGUOUS) {
            const uint8_t *l_src = l_base + l_off + sizeof(dap_global_db_page_header_t);
            size_t l_copy = a_buf_size;
            uint32_t l_cont = l_hdr->entries_count;
            size_t l_capacity = PAGE_DATA_SIZE + (size_t)l_cont * DAP_GLOBAL_DB_PAGE_SIZE;
            if (l_copy > l_capacity)
                l_copy = l_capacity;
            memcpy(a_out_buf, l_src, l_copy);
            if (a_out_len)
                *a_out_len = l_copy;
            return 0;
        }

        uint64_t l_page_id = a_first_page_id;
        size_t l_total = 0;
        uint8_t *l_dst = (uint8_t *)a_out_buf;
        while (l_page_id != 0 && l_total < a_buf_size) {
            l_off = s_page_offset(l_page_id);
            if (l_off + DAP_GLOBAL_DB_PAGE_SIZE > l_mmap_size)
                return -1;
            const dap_global_db_page_header_t *l_ph =
                (const dap_global_db_page_header_t *)(l_base + l_off);
            const uint8_t *l_data = l_base + l_off + sizeof(dap_global_db_page_header_t);
            size_t l_chunk = PAGE_DATA_SIZE;
            if (l_total + l_chunk > a_buf_size)
                l_chunk = a_buf_size - l_total;
            memcpy(l_dst, l_data, l_chunk);
            l_dst += l_chunk;
            l_total += l_chunk;
            l_page_id = l_ph->right_sibling;
        }
        if (a_out_len)
            *a_out_len = l_total;
        return 0;
    }

    uint64_t l_page_id = a_first_page_id;
    size_t l_total = 0;
    uint8_t *l_dst = (uint8_t *)a_out_buf;
    while (l_page_id != 0) {
        dap_global_db_page_t l_buf;
        if (!s_page_read_ref(a_tree, l_page_id, &l_buf)) {
            log_it(L_ERROR, "overflow_read: s_page_read_ref failed for page=%llu (first=%llu total_so_far=%zu)",
                   (unsigned long long)l_page_id, (unsigned long long)a_first_page_id, l_total);
            return -1;
        }
        if (!(l_buf.header.flags & DAP_GLOBAL_DB_PAGE_OVERFLOW)) {
            log_it(L_ERROR, "overflow_read: page=%llu flags=0x%x (not overflow), first=%llu total_so_far=%zu entries=%d",
                   (unsigned long long)l_page_id, l_buf.header.flags,
                   (unsigned long long)a_first_page_id, l_total, l_buf.header.entries_count);
            return -1;
        }
        size_t l_chunk = PAGE_DATA_SIZE;
        if (l_total + l_chunk > a_buf_size)
            l_chunk = a_buf_size - l_total;
        memcpy(l_dst, l_buf.data, l_chunk);
        l_dst += l_chunk;
        l_total += l_chunk;
        if (l_total >= a_buf_size)
            break;
        l_page_id = l_buf.header.right_sibling;
    }
    if (a_out_len)
        *a_out_len = l_total;
    return 0;
}

static void s_overflow_free(dap_global_db_t *a_tree, uint64_t a_first_page_id, uint64_t a_txn)
{
    if (a_first_page_id == 0)
        return;

    dap_global_db_page_t l_buf;
    if (!s_page_read_ref(a_tree, a_first_page_id, &l_buf))
        return;

    if (l_buf.header.flags & DAP_GLOBAL_DB_PAGE_OVERFLOW_CONTIGUOUS) {
        uint32_t l_count = 1 + (uint32_t)l_buf.header.entries_count;
        for (uint32_t i = 0; i < l_count; i++)
            s_deferred_free_add(a_tree, a_txn, a_first_page_id + i);
        return;
    }

    uint64_t l_page_id = a_first_page_id;
    s_deferred_free_add(a_tree, a_txn, l_page_id);
    l_page_id = l_buf.header.right_sibling;
    while (l_page_id != 0) {
        if (!s_page_read_ref(a_tree, l_page_id, &l_buf))
            break;
        uint64_t l_next = l_buf.header.right_sibling;
        s_deferred_free_add(a_tree, a_txn, l_page_id);
        l_page_id = l_next;
    }
}

static uint64_t s_page_allocate_new(dap_global_db_t *a_tree)
{
    // Check free list first
    if (a_tree->header.free_list_head != 0) {
        uint64_t l_page_id = a_tree->header.free_list_head;
        dap_global_db_page_t *l_free_page = s_page_read(a_tree, l_page_id, a_tree->arena);
        if (l_free_page) {
            a_tree->header.free_list_head = *(uint64_t *)l_free_page->data;
            // l_free_page freed by arena_reset
            return l_page_id;
        }
    }
    
    // Allocate new page at end
    a_tree->header.total_pages++;
    uint64_t l_new_page_id = a_tree->header.total_pages;

    // Grow mmap if needed
    if (a_tree->mmap) {
        size_t l_needed = s_page_offset(l_new_page_id) + DAP_GLOBAL_DB_PAGE_SIZE;
        size_t l_cur = dap_mmap_get_size(a_tree->mmap);
        if (l_needed > l_cur) {
            // Double to amortize remap cost
            size_t l_new_size = l_cur * 2;
            if (l_new_size < l_needed)
                l_new_size = l_needed;
            if (dap_mmap_resize(a_tree->mmap, l_new_size) != 0) {
                log_it(L_ERROR, "Failed to grow mmap to %zu", l_new_size);
                a_tree->header.total_pages--;
                return 0;
            }
            // mremap may relocate base address — re-resolve any writable mmap refs
            s_page_resolve_mmap(a_tree, a_tree->hot_leaf);
        }
    }

    return l_new_page_id;
}

static uint64_t s_page_allocate_contiguous(dap_global_db_t *a_tree, uint32_t a_count)
{
    if (a_count == 0)
        return 0;
    uint64_t l_first_id = a_tree->header.total_pages + 1;
    a_tree->header.total_pages += a_count;

    if (a_tree->mmap) {
        size_t l_needed = s_page_offset(a_tree->header.total_pages) + DAP_GLOBAL_DB_PAGE_SIZE;
        size_t l_cur = dap_mmap_get_size(a_tree->mmap);
        if (l_needed > l_cur) {
            size_t l_new_size = l_cur * 2;
            if (l_new_size < l_needed)
                l_new_size = l_needed;
            if (dap_mmap_resize(a_tree->mmap, l_new_size) != 0) {
                log_it(L_ERROR, "Failed to grow mmap for contiguous alloc (%u pages)", a_count);
                a_tree->header.total_pages -= a_count;
                return 0;
            }
            s_page_resolve_mmap(a_tree, a_tree->hot_leaf);
        }
    }
    return l_first_id;
}

// ============================================================================
// Branch Page Operations
// ============================================================================

static dap_global_db_branch_entry_t *s_branch_entry_at(dap_global_db_page_t *a_page, int a_index)
{
    if (!a_page->data || !(a_page->header.flags & DAP_GLOBAL_DB_PAGE_BRANCH))
        return NULL;

    dap_global_db_branch_entry_t *l_entries = (dap_global_db_branch_entry_t *)(a_page->data + sizeof(uint64_t));
    return &l_entries[a_index];
}

static uint64_t s_branch_get_child(dap_global_db_page_t *a_page, int a_index)
{
    if (!a_page->data || !(a_page->header.flags & DAP_GLOBAL_DB_PAGE_BRANCH))
        return 0;

    if (a_index == 0)
        return *(uint64_t *)a_page->data;

    dap_global_db_branch_entry_t *l_entry = s_branch_entry_at(a_page, a_index - 1);
    return l_entry ? l_entry->child_page : 0;
}

static void s_branch_set_child(dap_global_db_page_t *a_page, int a_index, uint64_t a_child, dap_arena_t *a_arena)
{
    if (!a_page->data || !(a_page->header.flags & DAP_GLOBAL_DB_PAGE_BRANCH))
        return;
    s_page_cow(a_page, a_arena);
    if (a_index == 0) {
        *(uint64_t *)a_page->data = a_child;
    } else {
        dap_global_db_branch_entry_t *l_entry = s_branch_entry_at(a_page, a_index - 1);
        if (l_entry)
            l_entry->child_page = a_child;
    }
    a_page->is_dirty = true;
}

static void s_branch_set_entry(dap_global_db_page_t *a_page, int a_index, const dap_global_db_key_t *a_key, uint64_t a_child, dap_arena_t *a_arena)
{
    s_page_cow(a_page, a_arena);
    dap_global_db_branch_entry_t *l_entry = s_branch_entry_at(a_page, a_index);
    if (l_entry) {
        l_entry->driver_hash = *a_key;
        l_entry->child_page = a_child;
        a_page->is_dirty = true;
    }
}

static void s_branch_insert_entry(dap_global_db_page_t *a_page, int a_index, const dap_global_db_key_t *a_key, uint64_t a_child, dap_arena_t *a_arena)
{
    s_page_cow(a_page, a_arena);
    // Shift entries from index to the right
    int l_count = a_page->header.entries_count;
    dap_global_db_branch_entry_t *l_entries = (dap_global_db_branch_entry_t *)(a_page->data + sizeof(uint64_t));
    
    for (int i = l_count; i > a_index; i--) {
        l_entries[i] = l_entries[i - 1];
    }
    
    // Insert new entry
    l_entries[a_index].driver_hash = *a_key;
    l_entries[a_index].child_page = a_child;
    
    a_page->header.entries_count++;
    a_page->is_dirty = true;
}

// ============================================================================
// Leaf Page Operations
// ============================================================================

/**
 * @brief Leaf page structure:
 * 
 * Page data layout (from start):
 *   [count:uint16_t][offsets:uint16_t[count]]...free space...[entries from end]
 * 
 * Entries are stored from the end of the page, growing backwards.
 * Offsets array stores the offset (from page data start) of each entry.
 */

static int s_leaf_entry_count(dap_global_db_page_t *a_page)
{
    return a_page->header.entries_count;
}

static inline size_t s_leaf_entry_total_size(uint32_t a_key_len, uint32_t a_value_len, uint32_t a_sign_len)
{
    return sizeof(dap_global_db_leaf_entry_t) + a_key_len + a_value_len + a_sign_len;
}

/** On-disk size when value/sign are in overflow (entry holds key + 8-byte overflow_page_id) */
static inline size_t s_leaf_entry_total_size_overflow(uint32_t a_key_len)
{
    return sizeof(dap_global_db_leaf_entry_t) + a_key_len + sizeof(uint64_t);
}

/**
 * @brief Get leaf entry at index
 * 
 * Leaf entries are stored with:
 * - Offsets array at the beginning of page data
 * - Actual entries packed from the end of page data
 */
static dap_global_db_leaf_entry_t *s_leaf_entry_at(dap_global_db_page_t *a_page, int a_index,
                                                    uint8_t **a_out_data, size_t *a_out_total_size)
{
    if (!a_page->data || !(a_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF))
        return NULL;
    
    int l_count = a_page->header.entries_count;
    if (a_index < 0 || a_index >= l_count)
        return NULL;
    
    // Offsets are stored after the count
    uint16_t *l_offsets = (uint16_t *)(a_page->data + LEAF_HEADER_SIZE);
    uint16_t l_offset = l_offsets[a_index];
    
    dap_global_db_leaf_entry_t *l_entry = (dap_global_db_leaf_entry_t *)(a_page->data + l_offset);
    
    if (a_out_data)
        *a_out_data = (uint8_t *)l_entry + sizeof(dap_global_db_leaf_entry_t);
    
    if (a_out_total_size) {
        if (l_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE)
            *a_out_total_size = s_leaf_entry_total_size_overflow(l_entry->key_len);
        else
            *a_out_total_size = s_leaf_entry_total_size(l_entry->key_len, l_entry->value_len, l_entry->sign_len);
    }
    return l_entry;
}

/**
 * @brief Find entry in leaf page by key (optimized: inline offset access, no checks)
 *
 * Hot path for both random and sequential inserts. Eliminated per-iteration overhead:
 *   - No s_leaf_entry_at call (saves function call + flags check + bounds check)
 *   - Direct offset array access + pointer arithmetic
 *   - s_key_cmp is static inline memcmp
 *
 * @return 0 if found, 1 if not found (a_out_index = insertion point)
 */
static int s_leaf_find_entry(dap_global_db_page_t *a_page, const dap_global_db_key_t *a_key, int *a_out_index)
{
    if (!a_page->data) {
        *a_out_index = 0;
        return -1;
    }
    int l_count = a_page->header.entries_count;
    const uint16_t *l_offsets = (const uint16_t *)(a_page->data + LEAF_HEADER_SIZE);
    const uint8_t *l_data = a_page->data;

    int l_low = 0, l_high = l_count - 1;

    while (l_low <= l_high) {
        int l_mid = (l_low + l_high) >> 1;  // unsigned shift = branchless div2
        const dap_global_db_leaf_entry_t *l_entry =
            (const dap_global_db_leaf_entry_t *)(l_data + l_offsets[l_mid]);
        int l_cmp = s_key_cmp(a_key, &l_entry->driver_hash);
        if (l_cmp == 0) {
            *a_out_index = l_mid;
            return 0;
        }
        if (l_cmp < 0)
            l_high = l_mid - 1;
        else
            l_low = l_mid + 1;
    }

    *a_out_index = l_low;
    return 1;
}

/**
 * @brief Insert entry into leaf page at given index
 */
static int s_leaf_insert_entry(dap_global_db_page_t *a_page, int a_index,
                               const dap_global_db_key_t *a_key, const char *a_text_key, uint32_t a_text_key_len,
                               const void *a_value, uint32_t a_value_len, const void *a_sign, uint32_t a_sign_len,
                               uint8_t a_flags, dap_arena_t *a_arena)
{
    s_page_cow(a_page, a_arena);
    int l_count = a_page->header.entries_count;
    size_t l_entry_size = s_leaf_entry_total_size(a_text_key_len, a_value_len, a_sign_len);
    
    // Calculate space needed
    size_t l_header_size = LEAF_HEADER_SIZE + (l_count + 1) * LEAF_OFFSET_SIZE;
    
    // Check available space
    if (l_header_size + l_entry_size > a_page->header.free_space) {
        return -1;  // No space
    }
    
    // Offsets array
    uint16_t *l_offsets = (uint16_t *)(a_page->data + LEAF_HEADER_SIZE);

    // O(1) offset calculation using cached lowest-used-offset.
    // This replaces the O(n) min-offset scan that was here before.
    uint16_t l_new_offset = LEAF_LOWEST_OFFSET(a_page->data) - (uint16_t)l_entry_size;
    
    // Shift offsets to make room
    for (int i = l_count; i > a_index; i--) {
        l_offsets[i] = l_offsets[i - 1];
    }
    l_offsets[a_index] = l_new_offset;
    
    // Write entry
    dap_global_db_leaf_entry_t *l_entry = (dap_global_db_leaf_entry_t *)(a_page->data + l_new_offset);
    l_entry->driver_hash = *a_key;
    l_entry->key_len = a_text_key_len;
    l_entry->value_len = a_value_len;
    l_entry->sign_len = a_sign_len;
    l_entry->flags = a_flags;
    memset(l_entry->reserved, 0, sizeof(l_entry->reserved));
    
    uint8_t *l_data = (uint8_t *)l_entry + sizeof(dap_global_db_leaf_entry_t);
    if (a_text_key && a_text_key_len > 0) {
        memcpy(l_data, a_text_key, a_text_key_len);
        l_data += a_text_key_len;
    }
    if (a_value && a_value_len > 0) {
        memmove(l_data, a_value, a_value_len);
        l_data += a_value_len;
    }
    if (a_sign && a_sign_len > 0) {
        memmove(l_data, a_sign, a_sign_len);
    }
    
    a_page->header.entries_count++;
    a_page->header.free_space -= (l_entry_size + LEAF_OFFSET_SIZE);
    LEAF_LOWEST_OFFSET(a_page->data) = l_new_offset;  // Update cache
    a_page->is_dirty = true;
    
    return 0;
}

/**
 * @brief Insert leaf entry that stores value/sign in overflow chain (key + overflow_page_id only in leaf)
 */
static int s_leaf_insert_entry_overflow(dap_global_db_page_t *a_page, int a_index,
                                        const dap_global_db_key_t *a_key, const char *a_text_key, uint32_t a_text_key_len,
                                        uint64_t a_overflow_page_id, uint32_t a_value_len, uint32_t a_sign_len,
                                        uint8_t a_flags, dap_arena_t *a_arena)
{
    s_page_cow(a_page, a_arena);
    int l_count = a_page->header.entries_count;
    size_t l_entry_size = s_leaf_entry_total_size_overflow(a_text_key_len);
    size_t l_header_size = LEAF_HEADER_SIZE + (l_count + 1) * LEAF_OFFSET_SIZE;
    if (l_header_size + l_entry_size > a_page->header.free_space)
        return -1;

    uint16_t *l_offsets = (uint16_t *)(a_page->data + LEAF_HEADER_SIZE);
    uint16_t l_new_offset = LEAF_LOWEST_OFFSET(a_page->data) - (uint16_t)l_entry_size;

    for (int i = l_count; i > a_index; i--)
        l_offsets[i] = l_offsets[i - 1];
    l_offsets[a_index] = l_new_offset;

    dap_global_db_leaf_entry_t *l_entry = (dap_global_db_leaf_entry_t *)(a_page->data + l_new_offset);
    l_entry->driver_hash = *a_key;
    l_entry->key_len = a_text_key_len;
    l_entry->value_len = a_value_len;
    l_entry->sign_len = a_sign_len;
    l_entry->flags = a_flags | DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE;
    memset(l_entry->reserved, 0, sizeof(l_entry->reserved));

    uint8_t *l_data = (uint8_t *)l_entry + sizeof(dap_global_db_leaf_entry_t);
    if (a_text_key && a_text_key_len > 0)
        memcpy(l_data, a_text_key, a_text_key_len);
    l_data += a_text_key_len;
    *(uint64_t *)l_data = a_overflow_page_id;

    a_page->header.entries_count++;
    a_page->header.free_space -= (l_entry_size + LEAF_OFFSET_SIZE);
    LEAF_LOWEST_OFFSET(a_page->data) = l_new_offset;
    a_page->is_dirty = true;
    return 0;
}

/**
 * @brief Compact leaf page: re-pack entries contiguously from the end of the page
 *
 * After a split, entries_count is truncated and free_space restored, but the
 * physical entry data remains scattered (some entries may have very low offsets
 * while gaps from removed entries exist above them). Without compaction, the
 * next insert computes l_new_offset = l_min_offset - l_entry_size which can
 * underflow uint16_t if l_min_offset is too small, causing a heap buffer overflow.
 *
 * This function re-packs the remaining entries contiguously from PAGE_DATA_SIZE
 * downward and updates the offsets array accordingly.
 */
static void s_leaf_compact(dap_global_db_page_t *a_page, dap_arena_t *a_arena)
{
    int l_count = a_page->header.entries_count;
    if (l_count == 0)
        return;
    s_page_cow(a_page, a_arena);

    uint16_t *l_offsets = (uint16_t *)(a_page->data + LEAF_HEADER_SIZE);

    // Collect entry sizes and data pointers (use stack for small counts)
    // A B-tree page holds at most a few dozen entries, so this is safe
    struct {
        size_t size;
        uint16_t old_offset;
    } l_entries[l_count];

    size_t l_total_entry_data = 0;
    for (int i = 0; i < l_count; i++) {
        dap_global_db_leaf_entry_t *l_entry =
            (dap_global_db_leaf_entry_t *)(a_page->data + l_offsets[i]);
        l_entries[i].size = (l_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE)
            ? s_leaf_entry_total_size_overflow(l_entry->key_len)
            : s_leaf_entry_total_size(l_entry->key_len, l_entry->value_len, l_entry->sign_len);
        l_entries[i].old_offset = l_offsets[i];
        l_total_entry_data += l_entries[i].size;
    }

    // Re-pack entries contiguously from the end of the page
    // Process in reverse offset order (highest offset first) to avoid
    // overwriting data we haven't moved yet. Sort by old_offset descending.
    // Since we're writing to new positions from the end downward,
    // and the highest-offset entries are already near the end, we use memmove.
    uint16_t l_write_offset = PAGE_DATA_SIZE;

    // Use arena for temp buffer: O(1) bump allocation, freed in bulk with arena_reset.
    // l_total_entry_data <= PAGE_DATA_SIZE (~4KB).
    uint8_t *l_tmp = a_arena
        ? dap_arena_alloc(a_arena, l_total_entry_data)
        : DAP_NEW_SIZE(uint8_t, l_total_entry_data);

    // Copy all entry data to temp buffer (memmove: may overlap in same arena)
    uint8_t *l_dst = l_tmp;
    for (int i = 0; i < l_count; i++) {
        memmove(l_dst, a_page->data + l_entries[i].old_offset, l_entries[i].size);
        l_dst += l_entries[i].size;
    }

    // Write entries back contiguously from the end, update offsets.
    // Use memmove: l_tmp and a_page->data may be in the same arena region.
    l_dst = l_tmp;
    for (int i = 0; i < l_count; i++) {
        l_write_offset -= l_entries[i].size;
        memmove(a_page->data + l_write_offset, l_dst, l_entries[i].size);
        l_offsets[i] = l_write_offset;
        l_dst += l_entries[i].size;
    }

    // Recalculate free_space precisely: gap between end of offsets array and
    // first entry (lowest offset)
    size_t l_offsets_end = LEAF_HEADER_SIZE + l_count * LEAF_OFFSET_SIZE;
    a_page->header.free_space = l_write_offset - l_offsets_end;
    LEAF_LOWEST_OFFSET(a_page->data) = l_write_offset;  // Update cache
    a_page->is_dirty = true;

    if (!a_arena)
        DAP_DELETE(l_tmp);
}

/**
 * @brief Check if page needs split for given entry
 * For leaf pages, checks free space. For branch pages, checks entry count.
 */
static bool s_page_needs_split(dap_global_db_page_t *a_page, uint32_t a_text_key_len, 
                               uint32_t a_value_len, uint32_t a_sign_len)
{
    return s_header_needs_split(&a_page->header, a_text_key_len, a_value_len, a_sign_len);
}

/**
 * @brief Header-only split check — works directly on mmap-mapped header.
 * No page struct needed, no data copy. Used for zero-copy branch traversal.
 */
static inline bool s_header_needs_split(const dap_global_db_page_header_t *a_hdr,
                                        uint32_t a_text_key_len, uint32_t a_value_len,
                                        uint32_t a_sign_len)
{
    if (a_hdr->flags & DAP_GLOBAL_DB_PAGE_LEAF) {
        size_t l_entry_size = (a_value_len + a_sign_len > MAX_INLINE_PAYLOAD)
            ? s_leaf_entry_total_size_overflow(a_text_key_len)
            : s_leaf_entry_total_size(a_text_key_len, a_value_len, a_sign_len);
        size_t l_header_size = LEAF_HEADER_SIZE + (a_hdr->entries_count + 1) * LEAF_OFFSET_SIZE;
        return (l_header_size + l_entry_size > a_hdr->free_space);
    } else {
        return (a_hdr->entries_count >= DAP_GLOBAL_DB_MAX_KEYS);
    }
}

/**
 * @brief Update existing entry in leaf page
 */
static int s_leaf_update_entry(dap_global_db_page_t *a_page, int a_index,
                               const char *a_text_key, uint32_t a_text_key_len,
                               const void *a_value, uint32_t a_value_len,
                               const void *a_sign, uint32_t a_sign_len,
                               uint8_t a_flags, dap_arena_t *a_arena,
                               dap_global_db_t *a_tree, uint64_t a_txn)
{
    s_page_cow(a_page, a_arena);

    uint8_t *l_old_data;
    size_t l_old_size;
    dap_global_db_leaf_entry_t *l_old_entry = s_leaf_entry_at(a_page, a_index, &l_old_data, &l_old_size);
    if (!l_old_entry)
        return -1;

    if (a_tree && (l_old_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE)) {
        uint64_t l_ov_id = *(const uint64_t *)(l_old_data + l_old_entry->key_len);
        s_overflow_free(a_tree, l_ov_id, a_txn);
    }

    dap_global_db_key_t l_key = l_old_entry->driver_hash;
    size_t l_payload = (size_t)a_value_len + (size_t)a_sign_len;
    size_t l_new_size = l_payload > MAX_INLINE_PAYLOAD
        ? s_leaf_entry_total_size_overflow(a_text_key_len)
        : s_leaf_entry_total_size(a_text_key_len, a_value_len, a_sign_len);

    if (l_new_size <= l_old_size && l_payload <= MAX_INLINE_PAYLOAD) {
        // Update in place (inline only)
        l_old_entry->key_len = a_text_key_len;
        l_old_entry->value_len = a_value_len;
        l_old_entry->sign_len = a_sign_len;
        l_old_entry->flags = a_flags & (uint8_t)~DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE;

        uint8_t *l_data = l_old_data;
        if (a_text_key && a_text_key_len > 0) {
            memcpy(l_data, a_text_key, a_text_key_len);
            l_data += a_text_key_len;
        }
        if (a_value && a_value_len > 0) {
            memmove(l_data, a_value, a_value_len);
            l_data += a_value_len;
        }
        if (a_sign && a_sign_len > 0) {
            memmove(l_data, a_sign, a_sign_len);
        }

        a_page->is_dirty = true;
        return 0;
    }

    // Entry grew or switched to/from overflow: delete old, compact, re-insert
    if (s_leaf_delete_entry(a_page, a_index, a_arena) != 0)
        return -1;

    int l_count_after = a_page->header.entries_count;
    size_t l_header_after = LEAF_HEADER_SIZE + (l_count_after + 1) * LEAF_OFFSET_SIZE;
    if (l_header_after + l_new_size > a_page->header.free_space)
        return -1;

    int l_new_index;
    s_leaf_find_entry(a_page, &l_key, &l_new_index);
    if (l_payload > MAX_INLINE_PAYLOAD && a_tree) {
        uint64_t l_ov_id = s_overflow_write(a_tree, a_value, a_value_len, a_sign, a_sign_len, a_txn);
        if (l_ov_id == 0)
            return -1;
        return s_leaf_insert_entry_overflow(a_page, l_new_index, &l_key, a_text_key, a_text_key_len,
                                            l_ov_id, a_value_len, a_sign_len, a_flags, a_arena);
    }
    return s_leaf_insert_entry(a_page, l_new_index, &l_key, a_text_key, a_text_key_len,
                               a_value, a_value_len, a_sign, a_sign_len, a_flags, a_arena);
}

/**
 * @brief Delete entry from leaf page at given index.
 *
 * Removes the entry from the offsets array, decrements count, then compacts
 * the page to reclaim physical space. This keeps free_space accurate and
 * prevents fragmentation from accumulating after repeated delete/insert cycles.
 */
static int s_leaf_delete_entry(dap_global_db_page_t *a_page, int a_index, dap_arena_t *a_arena)
{
    int l_count = a_page->header.entries_count;
    if (a_index < 0 || a_index >= l_count)
        return -1;

    s_page_cow(a_page, a_arena);

    // Get entry size for free_space accounting
    size_t l_entry_size = 0;
    s_leaf_entry_at(a_page, a_index, NULL, &l_entry_size);

    // Shift offsets to close the gap
    uint16_t *l_offsets = (uint16_t *)(a_page->data + LEAF_HEADER_SIZE);
    for (int i = a_index; i < l_count - 1; i++) {
        l_offsets[i] = l_offsets[i + 1];
    }

    a_page->header.entries_count--;
    a_page->header.free_space += l_entry_size + LEAF_OFFSET_SIZE;
    a_page->is_dirty = true;

    // Compact: reclaim physical space
    s_leaf_compact(a_page, a_arena);

    return 0;
}

// ============================================================================
// B-tree Search
// ============================================================================

static int s_search_in_page(dap_global_db_page_t *a_page, const dap_global_db_key_t *a_key, bool *a_found)
{
    *a_found = false;

    if (!a_page)
        return 0;

    if (a_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF) {
        int l_index;
        if (s_leaf_find_entry(a_page, a_key, &l_index) == 0) {
            *a_found = true;
        }
        return l_index;
    }

    if (!(a_page->header.flags & DAP_GLOBAL_DB_PAGE_BRANCH))
        return 0;

    int l_count = a_page->header.entries_count;
    int l_low = 0, l_high = l_count - 1;
    
    while (l_low <= l_high) {
        int l_mid = (l_low + l_high) / 2;
        dap_global_db_branch_entry_t *l_entry = s_branch_entry_at(a_page, l_mid);
        if (!l_entry)
            return l_low;

        int l_cmp = s_key_cmp(a_key, &l_entry->driver_hash);
        if (l_cmp == 0) {
            return l_mid + 1;
        } else if (l_cmp < 0) {
            l_high = l_mid - 1;
        } else {
            l_low = l_mid + 1;
        }
    }
    
    return l_low;
}

// ============================================================================
// B-tree Insertion
// ============================================================================

/**
 * @brief COW-safe split: split child at a_index, modify parent (arena copy),
 * write sibling to mmap (new page, safe for readers), return split child
 * via a_out_child so caller avoids stale mmap re-read.
 *
 * Parent (a_parent) MUST be an arena/heap copy. The child is read as an
 * arena copy internally. Neither child nor parent is written to mmap —
 * they will be COW-written by the caller's batch-write phase.
 *
 * The sibling IS written immediately because it's a new page with no
 * old content — readers using the old root will never navigate to it.
 */
static int s_split_child(dap_global_db_t *a_tree, dap_global_db_page_t *a_parent,
                          int a_index, dap_global_db_page_t **a_out_child)
{
    dap_arena_t *l_arena = a_tree->arena;
    uint64_t l_child_id = s_branch_get_child(a_parent, a_index);
    if (l_child_id == 0) {
        log_it(L_ERROR, "s_split_child: parent page_id=%llu has child[%d]=0 (corrupted tree)",
                (unsigned long long)a_parent->header.page_id, a_index);
        return -1;
    }

    uint64_t l_sibling_id = s_page_allocate_new(a_tree);
    if (l_sibling_id == 0) {
        log_it(L_ERROR, "s_split_child: failed to allocate sibling page");
        return -1;
    }

    // Read child as arena copy (COW: never modify original in mmap)
    dap_global_db_page_t *l_child = s_page_read(a_tree, l_child_id, l_arena);
    if (!l_child)
        return -1;

    // Sibling: new page — safe to write to mmap immediately
    dap_global_db_page_t *l_sibling = s_page_alloc(l_arena);
    if (!l_sibling)
        return -1;

    l_sibling->header.page_id = l_sibling_id;
    l_sibling->header.flags = l_child->header.flags & ~DAP_GLOBAL_DB_PAGE_ROOT;
    l_sibling->header.free_space = PAGE_DATA_SIZE;
    l_sibling->header.right_sibling = l_child->header.right_sibling;
    l_sibling->header.left_sibling = l_child_id;
    l_child->header.flags &= ~DAP_GLOBAL_DB_PAGE_ROOT;
    l_child->header.right_sibling = l_sibling_id;

    int l_count = l_child->header.entries_count;
    if (l_count < 2) {
        log_it(L_ERROR, "s_split_child: page_id=%llu has only %d entries, cannot split",
                (unsigned long long)l_child_id, l_count);
        return -1;
    }
    int l_mid = l_count / 2;

    dap_global_db_key_t l_median_key;

    if (l_child->header.flags & DAP_GLOBAL_DB_PAGE_LEAF) {
        uint16_t *l_child_offsets = (uint16_t *)(l_child->data + LEAF_HEADER_SIZE);
        uint16_t *l_sib_offsets = (uint16_t *)(l_sibling->data + LEAF_HEADER_SIZE);
        uint16_t l_sib_write_pos = PAGE_DATA_SIZE;
        int l_sib_count = l_count - l_mid;
        size_t l_freed_space = 0;

        for (int i = l_mid; i < l_count; i++) {
            dap_global_db_leaf_entry_t *l_entry =
                (dap_global_db_leaf_entry_t *)(l_child->data + l_child_offsets[i]);
            size_t l_entry_size = (l_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE)
                ? s_leaf_entry_total_size_overflow(l_entry->key_len)
                : s_leaf_entry_total_size(l_entry->key_len, l_entry->value_len, l_entry->sign_len);

            if (i == l_mid)
                l_median_key = l_entry->driver_hash;

            l_sib_write_pos -= (uint16_t)l_entry_size;
            memcpy(l_sibling->data + l_sib_write_pos, l_entry, l_entry_size);
            l_sib_offsets[i - l_mid] = l_sib_write_pos;

            l_freed_space += l_entry_size + LEAF_OFFSET_SIZE;
        }

        l_sibling->header.entries_count = l_sib_count;
        l_sibling->header.free_space =
            l_sib_write_pos - (LEAF_HEADER_SIZE + l_sib_count * LEAF_OFFSET_SIZE);
        LEAF_LOWEST_OFFSET(l_sibling->data) = l_sib_write_pos;

        l_child->header.entries_count = l_mid;
        l_child->header.free_space += l_freed_space;
        s_leaf_compact(l_child, l_arena);
    } else {
        dap_global_db_branch_entry_t *l_median = s_branch_entry_at(l_child, l_mid);
        l_median_key = l_median->driver_hash;

        s_branch_set_child(l_sibling, 0, l_median->child_page, l_arena);

        for (int i = l_mid + 1; i < l_count; i++) {
            dap_global_db_branch_entry_t *l_entry = s_branch_entry_at(l_child, i);
            s_branch_insert_entry(l_sibling, i - l_mid - 1, &l_entry->driver_hash, l_entry->child_page, l_arena);
        }

        l_child->header.entries_count = l_mid;
    }

    l_child->is_dirty = true;

    // Insert median into parent (arena/heap copy — no mmap modification)
    s_branch_insert_entry(a_parent, a_index, &l_median_key, l_sibling_id, l_arena);

    // Update old right sibling so its left_sibling points to the new right half.
    // Now AFTER branch_insert_entry: old right neighbor shifted to slot a_index+2.
    // Only for leaf splits (branch pages have no sibling links).
    if ((l_child->header.flags & DAP_GLOBAL_DB_PAGE_LEAF) &&
        l_sibling->header.right_sibling != 0 &&
        a_index + 2 <= (int)a_parent->header.entries_count) {
        uint64_t l_old_right_id = l_sibling->header.right_sibling;
        if (s_branch_get_child(a_parent, a_index + 2) == l_old_right_id) {
            if (a_tree->in_batch) {
                dap_global_db_page_t l_nbr;
                if (s_page_read_writable(a_tree, l_old_right_id, &l_nbr)) {
                    l_nbr.header.left_sibling = l_sibling_id;
                    s_page_write(a_tree, &l_nbr);
                }
            } else {
                uint64_t l_txn = atomic_load(&a_tree->mvcc_txn) + 1;
                uint64_t l_new_old_right = s_cow_leaf_update_sibling(a_tree, l_old_right_id,
                                                                     0, l_sibling_id, l_txn, l_arena);
                if (l_new_old_right != 0) {
                    s_branch_set_child(a_parent, a_index + 2, l_new_old_right, l_arena);
                    l_sibling->header.right_sibling = l_new_old_right;
                }
            }
        }
    }

    // Write ONLY sibling to mmap (new page — safe for readers)
    s_page_write(a_tree, l_sibling);

    // Return modified child copy — caller uses it instead of re-reading from mmap
    if (a_out_child)
        *a_out_child = l_child;

    return 0;
}

/**
 * @brief MVCC COW insert: all traversed pages are arena copies, mmap is
 * never modified in place. After leaf insertion, pages on the path from
 * leaf to root are batch-COW-written to new mmap locations, and the
 * root pointer is updated. Old pages go to the deferred free list.
 */
static int s_insert_non_full(dap_global_db_t *a_tree, dap_global_db_page_t *a_page,
                             const dap_global_db_key_t *a_key, const char *a_text_key, uint32_t a_text_key_len,
                             const void *a_value, uint32_t a_value_len, const void *a_sign, uint32_t a_sign_len,
                             uint8_t a_flags)
{
    dap_arena_t *l_arena = a_tree->arena;
    uint64_t l_txn = atomic_load(&a_tree->mvcc_txn) + 1;

    // ---- Batch mmap fast path: zero-copy traversal for no-split inserts ----
    // Uses mmap refs (32B header copy) instead of arena copies (4KB memcpy).
    // Falls back to the arena path when any child needs a split.
    if (a_tree->in_batch && a_tree->mmap) {
        dap_global_db_page_t l_refs[DAP_GLOBAL_DB_PATH_MAX + 1];
        struct { uint64_t page_id; int child_index; } l_fp[DAP_GLOBAL_DB_PATH_MAX];
        int l_dp = 0, l_ri = 0;

        if (!s_page_read_ref(a_tree, a_page->header.page_id, &l_refs[0]))
            goto arena_path;

        dap_global_db_page_t *lp = &l_refs[0];
        while (!(lp->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
            bool l_found;
            int l_index = s_search_in_page(lp, a_key, &l_found);
            uint64_t l_child_id = s_branch_get_child(lp, l_index);

            if (l_ri + 1 > DAP_GLOBAL_DB_PATH_MAX ||
                !s_page_read_ref(a_tree, l_child_id, &l_refs[l_ri + 1]) ||
                s_page_needs_split(&l_refs[l_ri + 1], a_text_key_len, a_value_len, a_sign_len))
                goto arena_path;

            l_fp[l_dp].page_id = lp->header.page_id;
            l_fp[l_dp].child_index = l_index;
            l_dp++;
            l_ri++;
            lp = &l_refs[l_ri];
        }

        // Leaf reached without splits — enable direct mmap writes
        lp->is_mmap_writable = true;

        int l_idx;
        if (s_leaf_find_entry(lp, a_key, &l_idx) == 0) {
            if (s_leaf_update_entry(lp, l_idx, a_text_key, a_text_key_len,
                                    a_value, a_value_len, a_sign, a_sign_len, a_flags, NULL,
                                    a_tree, l_txn) != 0) {
                a_tree->header.items_count--;
                return 1;
            }
        } else {
            size_t l_payload = (size_t)a_value_len + (size_t)a_sign_len;
            if (l_payload > MAX_INLINE_PAYLOAD) {
                uint64_t l_ov_id = s_overflow_write(a_tree, a_value, a_value_len, a_sign, a_sign_len, l_txn);
                if (l_ov_id == 0)
                    return -1;
                s_page_resolve_mmap(a_tree, lp);
                if (s_leaf_insert_entry_overflow(lp, l_idx, a_key, a_text_key, a_text_key_len,
                                                  l_ov_id, a_value_len, a_sign_len, a_flags, NULL) != 0) {
                    s_overflow_free(a_tree, l_ov_id, l_txn);
                    return -1;
                }
            } else if (s_leaf_insert_entry(lp, l_idx, a_key, a_text_key, a_text_key_len,
                                            a_value, a_value_len, a_sign, a_sign_len, a_flags, NULL) != 0) {
                return -1;
            }
            a_tree->header.items_count++;
        }

        s_page_write(a_tree, lp);

        // Promote leaf as writable mmap hot_leaf
        if (a_tree->hot_leaf)
            s_page_free(a_tree->hot_leaf);
        dap_global_db_page_t *l_hl = DAP_NEW(dap_global_db_page_t);
        if (l_hl) {
            l_hl->header = lp->header;
            uint64_t l_off = s_page_offset(lp->header.page_id);
            l_hl->data = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap) + l_off
                         + sizeof(dap_global_db_page_header_t);
            l_hl->is_mmap_ref = true;
            l_hl->is_mmap_writable = true;
            l_hl->is_arena = false;
            l_hl->is_dirty = false;
            a_tree->hot_leaf = l_hl;
            a_tree->hot_path_depth = l_dp;
            for (int i = 0; i < l_dp; i++) {
                a_tree->hot_path[i].page_id = l_fp[i].page_id;
                a_tree->hot_path[i].child_index = l_fp[i].child_index;
            }
        } else {
            a_tree->hot_leaf = NULL;
            a_tree->hot_path_depth = 0;
        }
        return 0;
    }

arena_path:;
    // Extended path: store arena-copy page pointers alongside their original IDs.
    // Pages on this path will be COW-written at the end.
    struct {
        uint64_t original_page_id;       // Original page ID in mmap (for defer free)
        dap_global_db_page_t *page; // Arena copy (modified in place during traversal)
        int child_index;                 // Index of child followed at this level
    } l_path[DAP_GLOBAL_DB_PATH_MAX];
    int l_depth = 0;

    dap_global_db_page_t *l_page = a_page;

    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        bool l_found;
        int l_index = s_search_in_page(l_page, a_key, &l_found);

        uint64_t l_child_id = s_branch_get_child(l_page, l_index);

        // Read child as arena copy (COW: never modify mmap in place)
        dap_global_db_page_t *l_child = s_page_read(a_tree, l_child_id, l_arena);
        if (!l_child) {
            static _Atomic uint64_t s_read_err_count = 0;
            uint64_t l_cnt = atomic_fetch_add(&s_read_err_count, 1);
            if (l_cnt < 3 || (l_cnt & (l_cnt - 1)) == 0) {
                log_it(L_ERROR, "s_page_read child_id=%llu returned NULL, parent page_id=%llu entries=%d l_index=%d (occurrence #%llu) file=%s",
                        (unsigned long long)l_child_id, (unsigned long long)l_page->header.page_id,
                        l_page->header.entries_count, l_index, (unsigned long long)(l_cnt + 1),
                        a_tree->filepath ? a_tree->filepath : "unknown");
                if (s_debug_more) {
                    for (int _d = 0; _d <= l_page->header.entries_count; _d++)
                        log_it(L_ERROR, "  parent child[%d] = %llu", _d, (unsigned long long)s_branch_get_child(l_page, _d));
                }
            }
            return -1;
        }

        // Preemptive split: if child is full, split before descending
        if (s_page_needs_split(l_child, a_text_key_len, a_value_len, a_sign_len)) {
            dap_global_db_page_t *l_split_child = NULL;
            if (s_split_child(a_tree, l_page, l_index, &l_split_child) != 0) {
                log_it(L_ERROR, "s_split_child failed at l_index=%d", l_index);
                return -1;
            }

            int l_split_index = l_index;  // Child index of the LEFT half

            dap_global_db_branch_entry_t *l_entry = s_branch_entry_at(l_page, l_index);
            if (s_key_cmp(a_key, &l_entry->driver_hash) > 0)
                l_index++;

            l_child_id = s_branch_get_child(l_page, l_index);

            // After split: use modified child copy (original child side),
            // or re-read from mmap (sibling side — already written to mmap)
            if (l_split_child && l_child_id == l_split_child->header.page_id) {
                l_child = l_split_child;
            } else {
                // Descending to the sibling (right) side — the old child (left)
                // is NOT on our traversal path.
                if (l_split_child) {
                    if (a_tree->in_batch) {
                        s_page_write(a_tree, l_split_child);
                    } else {
                        uint64_t l_old_split_id = l_split_child->header.page_id;
                        uint64_t l_new_split_id = s_page_allocate_new(a_tree);
                        if (l_new_split_id == 0) {
                            log_it(L_ERROR, "COW: failed to allocate new page for split child");
                            return -1;
                        }
                        l_split_child->header.page_id = l_new_split_id;
                        s_page_write(a_tree, l_split_child);
                        s_deferred_free_add(a_tree, l_txn, l_old_split_id);
                        s_branch_set_child(l_page, l_split_index, l_new_split_id, l_arena);
                    }
                }
                l_child = s_page_read(a_tree, l_child_id, l_arena);
                if (!l_child)
                    return -1;
            }
        }

        // Record path (page copy + original ID + child index)
        if (l_depth < DAP_GLOBAL_DB_PATH_MAX) {
            l_path[l_depth].original_page_id = l_page->header.page_id;
            l_path[l_depth].page = l_page;
            l_path[l_depth].child_index = l_index;
            l_depth++;
        }

        l_page = l_child;
    }

    // l_page is now a leaf (arena copy) — insert the entry
    uint64_t l_old_leaf_id = l_page->header.page_id;
    int l_index;
    if (s_leaf_find_entry(l_page, a_key, &l_index) == 0) {
        if (s_leaf_update_entry(l_page, l_index, a_text_key, a_text_key_len,
                               a_value, a_value_len, a_sign, a_sign_len, a_flags, l_arena,
                               a_tree, l_txn) != 0) {
            a_tree->header.items_count--;
            return 1;
        }
    } else {
        size_t l_payload = (size_t)a_value_len + (size_t)a_sign_len;
        if (l_payload > MAX_INLINE_PAYLOAD) {
            uint64_t l_ov_id = s_overflow_write(a_tree, a_value, a_value_len, a_sign, a_sign_len, l_txn);
            if (l_ov_id == 0) {
                log_it(L_ERROR, "Overflow write failed");
                return -1;
            }
            if (s_leaf_insert_entry_overflow(l_page, l_index, a_key, a_text_key, a_text_key_len,
                                              l_ov_id, a_value_len, a_sign_len, a_flags, l_arena) != 0) {
                s_overflow_free(a_tree, l_ov_id, l_txn);
                log_it(L_ERROR, "Failed to insert overflow entry - no space");
                return -1;
            }
        } else if (s_leaf_insert_entry(l_page, l_index, a_key, a_text_key, a_text_key_len,
                                       a_value, a_value_len, a_sign, a_sign_len, a_flags, l_arena) != 0) {
            log_it(L_ERROR, "Failed to insert into leaf - no space, entries=%d free=%u l_index=%d",
                    l_page->header.entries_count, l_page->header.free_space, l_index);
            return -1;
        }
        a_tree->header.items_count++;
    }

    if (a_tree->in_batch) {
        // Batch mode: write leaf in-place, only write dirty branches (splits)
        s_page_write(a_tree, l_page);
        for (int i = l_depth - 1; i >= 0; i--) {
            if (l_path[i].page->is_dirty)
                s_page_write(a_tree, l_path[i].page);
        }

        // Promote leaf as writable mmap hot_leaf — data stays in mmap, zero copy
        if (a_tree->hot_leaf)
            s_page_free(a_tree->hot_leaf);
        dap_global_db_page_t *l_hl = DAP_NEW(dap_global_db_page_t);
        if (l_hl) {
            l_hl->header = l_page->header;
            if (a_tree->mmap) {
                uint64_t l_off = s_page_offset(l_page->header.page_id);
                l_hl->data = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap) + l_off
                             + sizeof(dap_global_db_page_header_t);
                l_hl->is_mmap_ref = true;
                l_hl->is_mmap_writable = true;
            } else {
                l_hl->data = DAP_NEW_SIZE(uint8_t, PAGE_DATA_SIZE);
                if (!l_hl->data) { DAP_DELETE(l_hl); l_hl = NULL; goto batch_no_hl; }
                memcpy(l_hl->data, l_page->data, PAGE_DATA_SIZE);
                l_hl->is_mmap_ref = false;
                l_hl->is_mmap_writable = false;
            }
            l_hl->is_arena = false;
            l_hl->is_dirty = false;
            a_tree->hot_leaf = l_hl;
            a_tree->hot_path_depth = l_depth;
            for (int i = 0; i < l_depth; i++) {
                a_tree->hot_path[i].page_id = l_path[i].original_page_id;
                a_tree->hot_path[i].child_index = l_path[i].child_index;
            }
        } else {
batch_no_hl:
            a_tree->hot_leaf = NULL;
            a_tree->hot_path_depth = 0;
        }
        return 0;
    }

    // === COW batch-write phase (allocate-first pattern) ===
    // 1. Allocate new leaf page ID first (before COW neighbors)
    uint64_t l_new_leaf_id = s_page_allocate_new(a_tree);
    if (l_new_leaf_id == 0) {
        log_it(L_ERROR, "COW: failed to allocate new leaf page");
        return -1;
    }

    // 2. COW neighboring leaves so they point to the pre-allocated new leaf ID
    //    Skip when no active snapshots — old pages won't be read by anyone
    uint64_t l_new_left_id = 0, l_new_right_id = 0;
    int l_ci = (l_depth > 0) ? l_path[l_depth - 1].child_index : 0;
    dap_global_db_page_t *l_parent = (l_depth > 0) ? l_path[l_depth - 1].page : NULL;
    bool l_need_cow_siblings = s_has_active_snapshots(a_tree);
    if (l_need_cow_siblings && l_parent && l_page->header.left_sibling != 0 && l_ci > 0) {
        uint64_t l_expected = s_branch_get_child(l_parent, l_ci - 1);
        if (l_expected == l_page->header.left_sibling)
            l_new_left_id = s_cow_leaf_update_sibling(a_tree, l_page->header.left_sibling,
                                                      1, l_new_leaf_id, l_txn, l_arena);
    }
    if (l_need_cow_siblings && l_parent && l_page->header.right_sibling != 0 && l_ci < (int)l_parent->header.entries_count) {
        uint64_t l_expected = s_branch_get_child(l_parent, l_ci + 1);
        if (l_expected == l_page->header.right_sibling)
            l_new_right_id = s_cow_leaf_update_sibling(a_tree, l_page->header.right_sibling,
                                                      0, l_new_leaf_id, l_txn, l_arena);
    }

    // 3. Update leaf's sibling links to the COW'd neighbor IDs
    if (l_new_left_id != 0)
        l_page->header.left_sibling = l_new_left_id;
    if (l_new_right_id != 0)
        l_page->header.right_sibling = l_new_right_id;

    // 4. Write leaf with correct sibling links
    l_page->header.page_id = l_new_leaf_id;
    s_page_write(a_tree, l_page);
    s_deferred_free_add(a_tree, l_txn, l_old_leaf_id);

    // Walk up the path: COW each parent with updated child pointer(s)
    uint64_t l_new_child_id = l_new_leaf_id;
    for (int i = l_depth - 1; i >= 0; i--) {
        dap_global_db_page_t *p = l_path[i].page;
        uint64_t l_old_id = l_path[i].original_page_id;

        // Update child pointer to the new child page
        s_branch_set_child(p, l_path[i].child_index, l_new_child_id, l_arena);
        // At leaf level parent, also set new ids for COW'd left/right neighbors
        if (i == l_depth - 1) {
            if (l_new_left_id != 0)
                s_branch_set_child(p, l_ci - 1, l_new_left_id, l_arena);
            if (l_new_right_id != 0)
                s_branch_set_child(p, l_ci + 1, l_new_right_id, l_arena);
        }

        // Verify no zero child pointers after set_child
        if (s_debug_more) {
            for (int _d = 0; _d <= p->header.entries_count; _d++) {
                uint64_t _c = s_branch_get_child(p, _d);
                if (_c == 0)
                    log_it(L_ERROR, "ZERO child[%d] page_id=%llu entries=%d "
                            "i=%d l_ci=%d new_child=%llu new_left=%llu new_right=%llu ci=%d",
                            _d, (unsigned long long)p->header.page_id, p->header.entries_count,
                            i, l_ci, (unsigned long long)l_new_child_id,
                            (unsigned long long)l_new_left_id, (unsigned long long)l_new_right_id,
                            l_path[i].child_index);
            }
        }

        // Allocate new page for this parent
        uint64_t l_new_id = s_page_allocate_new(a_tree);
        if (l_new_id == 0) {
            log_it(L_ERROR, "COW: failed to allocate new branch page, depth_level=%d", i);
            return -1;
        }
        p->header.page_id = l_new_id;
        p->is_dirty = true;
        s_page_write(a_tree, p);
        s_deferred_free_add(a_tree, l_txn, l_old_id);

        l_new_child_id = l_new_id;
    }

    // Update root to the topmost new page
    a_tree->header.root_page = l_new_child_id;

    // Promote leaf to hot_leaf with private heap buffer
    if (a_tree->hot_leaf)
        s_page_free(a_tree->hot_leaf);

    dap_global_db_page_t *l_hl = DAP_NEW(dap_global_db_page_t);
    if (l_hl) {
        l_hl->header = l_page->header;
        l_hl->data = DAP_NEW_SIZE(uint8_t, PAGE_DATA_SIZE);
        if (!l_hl->data) { DAP_DELETE(l_hl); l_hl = NULL; goto no_hot_leaf; }
        memcpy(l_hl->data, l_page->data, PAGE_DATA_SIZE);
        l_hl->is_mmap_ref = false;
        l_hl->is_mmap_writable = false;
        l_hl->is_arena = false;
        l_hl->is_dirty = false;  // Just written to mmap — clean
        a_tree->hot_leaf = l_hl;

        // Cache the COW path: store NEW page IDs (not originals!)
        a_tree->hot_path_depth = l_depth;
        for (int i = 0; i < l_depth; i++) {
            a_tree->hot_path[i].page_id = l_path[i].page->header.page_id;  // New page IDs
            a_tree->hot_path[i].child_index = l_path[i].child_index;
        }
    } else {
no_hot_leaf:
        a_tree->hot_leaf = NULL;
        a_tree->hot_path_depth = 0;
    }

    return 0;
}

// ============================================================================
// Debug: trace search path for a key (called under rdlock or wrlock)
// ============================================================================
static void s_debug_trace_search(dap_global_db_t *a_tree, const dap_global_db_key_t *a_key,
                                  const char *a_tag)
{
    uint64_t l_root = a_tree->header.root_page;
    if (l_root == 0) {
        debug_if(s_debug_more, L_DEBUG, "[TRACE:%s] root=0 (empty tree)", a_tag);
        return;
    }
    debug_if(s_debug_more, L_DEBUG, "[TRACE:%s] root=%llu height=%llu count=%llu",
            a_tag, (unsigned long long)l_root,
            (unsigned long long)a_tree->header.tree_height,
            (unsigned long long)a_tree->header.items_count);

    dap_global_db_page_t l_buf;
    if (!s_page_read_ref(a_tree, l_root, &l_buf)) {
        log_it(L_ERROR, "[TRACE:%s] FAILED to read root page %llu!", a_tag, (unsigned long long)l_root);
        return;
    }
    int depth = 0;
    while (!(l_buf.header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        bool found;
        int idx = s_search_in_page(&l_buf, a_key, &found);
        uint64_t child = s_branch_get_child(&l_buf, idx);
        debug_if(s_debug_more, L_DEBUG, "[TRACE:%s]  depth=%d branch page_id=%llu entries=%d child_idx=%d -> child=%llu flags=0x%x",
                a_tag, depth, (unsigned long long)l_buf.header.page_id,
                l_buf.header.entries_count, idx, (unsigned long long)child, l_buf.header.flags);
        if (!s_page_read_ref(a_tree, child, &l_buf)) {
            log_it(L_ERROR, "[TRACE:%s]  FAILED to read child page %llu!", a_tag, (unsigned long long)child);
            return;
        }
        depth++;
    }
    int l_idx;
    int rc = s_leaf_find_entry(&l_buf, a_key, &l_idx);
    debug_if(s_debug_more, L_DEBUG, "[TRACE:%s]  depth=%d LEAF page_id=%llu entries=%d found=%d at_idx=%d "
            "left_sib=%llu right_sib=%llu flags=0x%x",
            a_tag, depth, (unsigned long long)l_buf.header.page_id,
            l_buf.header.entries_count, (rc == 0 ? 1 : 0), l_idx,
            (unsigned long long)l_buf.header.left_sibling,
            (unsigned long long)l_buf.header.right_sibling,
            l_buf.header.flags);

    if (a_tree->hot_leaf && a_tree->hot_leaf->is_dirty) {
        debug_if(s_debug_more, L_DEBUG, "[TRACE:%s]  hot_leaf: page_id=%llu entries=%d dirty=%d",
                a_tag, (unsigned long long)a_tree->hot_leaf->header.page_id,
                a_tree->hot_leaf->header.entries_count, a_tree->hot_leaf->is_dirty);
    }
}

// ============================================================================
// Public API - Debug
// ============================================================================
void dap_global_db_debug_trace(dap_global_db_t *a_tree,
                                      const dap_global_db_key_t *a_key,
                                      const char *a_tag)
{
    if (!a_tree || !a_key) return;
    pthread_rwlock_rdlock(&a_tree->lock);
    s_debug_trace_search(a_tree, a_key, a_tag);
    pthread_rwlock_unlock(&a_tree->lock);
}

// ============================================================================
// Public API - Tree Management
// ============================================================================

dap_global_db_t *dap_global_db_create(const char *a_filepath)
{
    dap_return_val_if_fail(a_filepath, NULL);
    
    int l_fd = open(a_filepath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (l_fd < 0) {
        log_it(L_ERROR, "Failed to create B-tree file %s: %s", a_filepath, strerror(errno));
        return NULL;
    }
    
    dap_global_db_t *l_tree = DAP_NEW_Z(dap_global_db_t);
    if (!l_tree) {
        close(l_fd);
        return NULL;
    }
    
    l_tree->fd = l_fd;
    l_tree->filepath = dap_strdup(a_filepath);
    l_tree->read_only = false;
    
    // Initialize header
    l_tree->header.magic = DAP_GLOBAL_DB_MAGIC;
    l_tree->header.version = DAP_GLOBAL_DB_STORAGE_VERSION;
    l_tree->header.flags = 0;
    l_tree->header.page_size = DAP_GLOBAL_DB_PAGE_SIZE;
    l_tree->header.root_page = 0;  // Empty tree
    l_tree->header.total_pages = 0;
    l_tree->header.items_count = 0;
    l_tree->header.tree_height = 0;
    l_tree->header.free_list_head = 0;
    
    // Write header
    if (s_header_write(l_tree) != 0) {
        close(l_fd);
        DAP_DEL_Z(l_tree->filepath);
        DAP_DELETE(l_tree);
        return NULL;
    }
    
    // Allocate space for first page
    if (ftruncate(l_fd, BTREE_DATA_OFFSET) != 0) {
        log_it(L_WARNING, "Failed to truncate file: %s", strerror(errno));
    }

    // Initialize mmap: pre-allocate 64MB to amortize early growth and minimize
    // mremap calls during bulk inserts. On Linux, unused pages don't consume
    // physical RAM (lazy allocation via demand paging), so this is safe.
    // LMDB pre-allocates 1GB by default; our dynamic growth is costlier due
    // to mremap TLB shootdown and page fault overhead on new pages.
    size_t l_initial_mmap = 64 * 1024 * 1024;
    if (l_initial_mmap < (size_t)BTREE_DATA_OFFSET)
        l_initial_mmap = BTREE_DATA_OFFSET;
    l_tree->mmap = dap_mmap_open(a_filepath,
        DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, l_initial_mmap);
    // No madvise on create: Linux MADV_NORMAL (default) gives moderate readahead
    // which benefits sequential bulk inserts that follow. MADV_RANDOM would
    // disable readahead, hurting sequential write by ~10-15%.

    // Arena for temporary allocations during write path (page reads, split
    // buffers, compact temp buffers). Sized for worst-case split with several
    // levels of recursion: each level uses ~1 page + overhead.
    l_tree->arena = dap_arena_new(DAP_GLOBAL_DB_PAGE_SIZE * 32);

    // Initialize reader-writer lock for thread safety (Phase 3 compat)
    pthread_rwlock_init(&l_tree->lock, NULL);

    // Initialize MVCC state
    atomic_store(&l_tree->mvcc_root, l_tree->header.root_page);
    atomic_store(&l_tree->mvcc_txn, 0);
    atomic_store(&l_tree->mvcc_count, l_tree->header.items_count);
    atomic_store(&l_tree->mvcc_height, l_tree->header.tree_height);
    atomic_store(&l_tree->mvcc_seq, 0);
    pthread_mutex_init(&l_tree->write_mutex, NULL);
    for (int i = 0; i < DAP_BTREE_MAX_SNAPSHOTS; i++)
        atomic_store(&l_tree->snapshot_txns[i], 0);

    l_tree->overflow_read_buf = NULL;
    l_tree->overflow_read_buf_size = 0;

    debug_if(s_debug_more, L_INFO, "Created B-tree file: %s", a_filepath);
    return l_tree;
}

dap_global_db_t *dap_global_db_open(const char *a_filepath, bool a_read_only)
{
    dap_return_val_if_fail(a_filepath, NULL);
    
    int l_flags = a_read_only ? O_RDONLY : O_RDWR;
    int l_fd = open(a_filepath, l_flags);
    if (l_fd < 0) {
        log_it(L_DEBUG, "Failed to open B-tree file %s: %s", a_filepath, strerror(errno));
        return NULL;
    }
    
    dap_global_db_t *l_tree = DAP_NEW_Z(dap_global_db_t);
    if (!l_tree) {
        close(l_fd);
        return NULL;
    }
    
    l_tree->fd = l_fd;
    l_tree->filepath = dap_strdup(a_filepath);
    l_tree->read_only = a_read_only;
    
    // Try to open mmap before reading header
    int l_mmap_flags = DAP_MMAP_READ | DAP_MMAP_SHARED;
    if (!a_read_only)
        l_mmap_flags |= DAP_MMAP_WRITE;
    l_tree->mmap = dap_mmap_open(a_filepath, l_mmap_flags, 0);
    if (l_tree->mmap) {
        dap_mmap_advise(l_tree->mmap, DAP_MMAP_ADVISE_RANDOM);
    }

    // Read and validate header (will use mmap if available)
    if (s_header_read(l_tree) != 0) {
        if (l_tree->mmap) dap_mmap_close(l_tree->mmap);
        close(l_fd);
        DAP_DEL_Z(l_tree->filepath);
        DAP_DELETE(l_tree);
        return NULL;
    }

    // Arena for temporary allocations during write path
    l_tree->arena = dap_arena_new(DAP_GLOBAL_DB_PAGE_SIZE * 32);

    // Initialize reader-writer lock for thread safety (Phase 3 compat)
    pthread_rwlock_init(&l_tree->lock, NULL);

    // Initialize MVCC state from on-disk header
    atomic_store(&l_tree->mvcc_root, l_tree->header.root_page);
    atomic_store(&l_tree->mvcc_txn, 0);
    atomic_store(&l_tree->mvcc_count, l_tree->header.items_count);
    atomic_store(&l_tree->mvcc_height, l_tree->header.tree_height);
    atomic_store(&l_tree->mvcc_seq, 0);
    pthread_mutex_init(&l_tree->write_mutex, NULL);
    for (int i = 0; i < DAP_BTREE_MAX_SNAPSHOTS; i++)
        atomic_store(&l_tree->snapshot_txns[i], 0);

    log_it(L_INFO, "Opened B-tree file: %s (items: %lu, pages: %lu, mmap: %s)",
           a_filepath, (unsigned long)l_tree->header.items_count,
           (unsigned long)l_tree->header.total_pages,
           l_tree->mmap ? "yes" : "no");
    
    return l_tree;
}

void dap_global_db_close(dap_global_db_t *a_tree)
{
    if (!a_tree)
        return;
    
    // close() is a lifecycle operation — caller must ensure no other
    // threads are using the tree (same contract as LMDB mdb_env_close).
    if (!a_tree->read_only) {
        s_btree_sync_impl(a_tree);
    }
    
    if (a_tree->root)
        s_page_free(a_tree->root);

    if (a_tree->arena) {
        dap_arena_free(a_tree->arena);
        a_tree->arena = NULL;
    }

    if (a_tree->mmap) {
        dap_mmap_close(a_tree->mmap);
        a_tree->mmap = NULL;
    }
    
    if (a_tree->fd >= 0)
        close(a_tree->fd);
    pthread_rwlock_destroy(&a_tree->lock);

    DAP_DELETE(a_tree->overflow_read_buf);
    a_tree->overflow_read_buf_size = 0;

    // Cleanup MVCC state
    pthread_mutex_destroy(&a_tree->write_mutex);
    if (a_tree->deferred_batches) {
        for (size_t i = 0; i < a_tree->deferred_batch_count; i++)
            DAP_DELETE(a_tree->deferred_batches[i].page_ids);
        DAP_DELETE(a_tree->deferred_batches);
    }

    DAP_DEL_Z(a_tree->filepath);
    DAP_DELETE(a_tree);
}

/**
 * @brief Inner implementation of sync (no locking).
 * Separated so close() can call it under its own lock discipline.
 */
static int s_btree_sync_impl(dap_global_db_t *a_tree)
{
    // Flush hot leaf
    s_hot_leaf_flush(a_tree);

    // Force deferred free reclaim on sync
    s_deferred_free_reclaim(a_tree);
    a_tree->mvcc_commit_counter = 0;

    // Write header
    if (s_header_write(a_tree) != 0)
        return -1;
    
    // Sync to disk
    if (a_tree->mmap) {
        return dap_mmap_sync(a_tree->mmap, DAP_MMAP_SYNC_SYNC);
    }
#ifdef DAP_OS_WINDOWS
    _commit(a_tree->fd);
#else
    fsync(a_tree->fd);
#endif
    
    return 0;
}

int dap_global_db_sync(dap_global_db_t *a_tree)
{
    dap_return_val_if_fail(a_tree && !a_tree->read_only, -1);
    pthread_rwlock_wrlock(&a_tree->lock);
    int l_ret = s_btree_sync_impl(a_tree);
    if (l_ret == 0)
        s_mvcc_commit(a_tree);
    pthread_rwlock_unlock(&a_tree->lock);
    return l_ret;
}

// ============================================================================
// Public API - Data Operations
// ============================================================================

/**
 * @brief Flush hot leaf to storage and release it.
 * Hot leaf modifications are deferred — if the page is dirty,
 * write it to mmap/disk before freeing.
 */
static void s_hot_leaf_flush(dap_global_db_t *a_tree)
{
    if (!a_tree->hot_leaf)
        return;

    dap_global_db_page_t *hl = a_tree->hot_leaf;

    if (hl->is_dirty) {
        if (a_tree->in_batch) {
            s_page_write(a_tree, hl);
            goto done;
        }

        uint64_t l_txn = atomic_load(&a_tree->mvcc_txn) + 1;
        uint64_t l_old_leaf_id = hl->header.page_id;
        dap_arena_t *l_arena = a_tree->arena;

        // 1. Allocate new page for the leaf (allocate-first pattern)
        uint64_t l_new_leaf_id = s_page_allocate_new(a_tree);
        if (l_new_leaf_id == 0) {
            s_page_write(a_tree, hl);
            goto done;
        }

        // 2. Read parent for verify guards and chain-up
        int l_depth = a_tree->hot_path_depth;
        int l_ci = (l_depth > 0) ? a_tree->hot_path[l_depth - 1].child_index : 0;
        uint64_t l_parent_id = (l_depth > 0) ? a_tree->hot_path[l_depth - 1].page_id : 0;
        dap_global_db_page_t *l_parent = (l_depth > 0) ?
            s_page_read(a_tree, l_parent_id, l_arena) : NULL;

        // 3. COW neighboring leaves with verify guards
        //    Skip when no active snapshots — old pages won't be read
        uint64_t l_new_left_id = 0, l_new_right_id = 0;
        bool l_need_cow_siblings = s_has_active_snapshots(a_tree);
        if (l_need_cow_siblings && l_parent && hl->header.left_sibling != 0 && l_ci > 0) {
            uint64_t l_expected = s_branch_get_child(l_parent, l_ci - 1);
            if (l_expected == hl->header.left_sibling)
                l_new_left_id = s_cow_leaf_update_sibling(a_tree, hl->header.left_sibling,
                                                          1, l_new_leaf_id, l_txn, l_arena);
        }
        if (l_need_cow_siblings && l_parent && hl->header.right_sibling != 0 && l_ci < (int)l_parent->header.entries_count) {
            uint64_t l_expected = s_branch_get_child(l_parent, l_ci + 1);
            if (l_expected == hl->header.right_sibling)
                l_new_right_id = s_cow_leaf_update_sibling(a_tree, hl->header.right_sibling,
                                                           0, l_new_leaf_id, l_txn, l_arena);
        }

        // 4. Update leaf's sibling links to COW'd neighbor IDs, then write
        if (l_new_left_id != 0)
            hl->header.left_sibling = l_new_left_id;
        if (l_new_right_id != 0)
            hl->header.right_sibling = l_new_right_id;

        hl->header.page_id = l_new_leaf_id;
        s_page_write(a_tree, hl);
        s_deferred_free_add(a_tree, l_txn, l_old_leaf_id);

        // 5. COW chain-up through parents to root
        if (l_parent) {
            s_branch_set_child(l_parent, l_ci, l_new_leaf_id, l_arena);
            if (l_new_left_id != 0 && l_ci > 0)
                s_branch_set_child(l_parent, l_ci - 1, l_new_left_id, l_arena);
            if (l_new_right_id != 0 && l_ci < (int)l_parent->header.entries_count)
                s_branch_set_child(l_parent, l_ci + 1, l_new_right_id, l_arena);
            uint64_t l_new_parent_id = s_page_allocate_new(a_tree);
            if (l_new_parent_id != 0) {
                l_parent->header.page_id = l_new_parent_id;
                l_parent->is_dirty = true;
                s_page_write(a_tree, l_parent);
                s_deferred_free_add(a_tree, l_txn, l_parent_id);
                uint64_t l_new_root = s_cow_chain_up(a_tree, a_tree->hot_path,
                                                    l_depth - 1, l_new_parent_id, l_txn);
                if (l_new_root != 0)
                    a_tree->header.root_page = l_new_root;
            } else {
                uint64_t l_new_root = s_cow_chain_up(a_tree, a_tree->hot_path,
                                                     l_depth, l_new_leaf_id, l_txn);
                if (l_new_root != 0)
                    a_tree->header.root_page = l_new_root;
            }
        } else if (l_depth > 0) {
            uint64_t l_new_root = s_cow_chain_up(a_tree, a_tree->hot_path,
                                                 l_depth, l_new_leaf_id, l_txn);
            if (l_new_root != 0)
                a_tree->header.root_page = l_new_root;
        } else {
            a_tree->header.root_page = l_new_leaf_id;
        }
    }

done:
    s_page_free(a_tree->hot_leaf);
    a_tree->hot_leaf = NULL;
    a_tree->hot_path_depth = 0;
}

/**
 * @brief Refresh a page from hot_leaf if the page is a stale mmap snapshot.
 *
 * When hot_leaf is dirty, the mmap version of that page is stale (missing
 * recent inserts). This function detects the match and copies hot_leaf data
 * into the provided page buffer WITHOUT any tree mutation — safe for read path.
 *
 * Used by cursor_move to ensure iteration sees up-to-date leaf data.
 */
static void s_page_refresh_from_hot_leaf(const dap_global_db_t *a_tree,
                                         dap_global_db_page_t *a_page)
{
    if (!a_tree->hot_leaf || !a_tree->hot_leaf->is_dirty)
        return;
    if (a_page->header.page_id != a_tree->hot_leaf->header.page_id)
        return;
    /* Page is stale — overwrite with hot_leaf data */
    a_page->header = a_tree->hot_leaf->header;
    memcpy(a_page->data, a_tree->hot_leaf->data, PAGE_DATA_SIZE);
}

/**
 * @brief Inner implementation of insert (no locking).
 * Separated to allow recursive calls without deadlock.
 */
static int s_btree_insert_impl(dap_global_db_t *a_tree,
                         const dap_global_db_key_t *a_key,
                         const char *a_text_key, uint32_t a_text_key_len,
                         const void *a_value, uint32_t a_value_len,
                         const void *a_sign, uint32_t a_sign_len,
                         uint8_t a_flags)
{

    dap_arena_t *l_arena = a_tree->arena;

    // ---- Hot leaf fast path ------------------------------------------------
    // For sequential inserts, the same leaf is hit repeatedly. The hot_leaf is
    // a heap-allocated struct with writable mmap data — no arena involvement.
    //
    // This entire block is aggressively inlined for sequential append:
    //   - Direct pointer arithmetic instead of s_leaf_entry_at (skip bounds/flags checks)
    //   - Raw memcmp instead of dap_global_db_key_compare (skip function call)
    //   - Direct entry write instead of s_leaf_insert_entry (skip COW/space/shift checks)
    if (a_tree->hot_leaf && a_tree->hot_leaf->header.entries_count > 0) {
        dap_global_db_page_t *hl = a_tree->hot_leaf;

        // Inline: get last entry directly from mmap (no function call, no checks)
        uint16_t *l_hl_offsets = (uint16_t *)(hl->data + LEAF_HEADER_SIZE);
        dap_global_db_leaf_entry_t *l_last =
            (dap_global_db_leaf_entry_t *)(hl->data + l_hl_offsets[hl->header.entries_count - 1]);

        // Inline: raw memcmp (16 bytes) — same semantics as dap_global_db_key_compare
        int l_cmp = memcmp(a_key, &l_last->driver_hash, sizeof(dap_global_db_key_t));

        bool l_in_range = false;
        if (l_cmp > 0) {
            l_in_range = (hl->header.right_sibling == 0);
        } else if (l_cmp == 0) {
            l_in_range = true;
        } else {
            // Key < last: check if >= first entry
            dap_global_db_leaf_entry_t *l_first =
                (dap_global_db_leaf_entry_t *)(hl->data + l_hl_offsets[0]);
            l_in_range = (memcmp(a_key, &l_first->driver_hash, sizeof(dap_global_db_key_t)) >= 0);
        }
        if (l_in_range && !s_page_needs_split(hl, a_text_key_len, a_value_len, a_sign_len)) {
            if (l_cmp > 0) {
                // ---- Sequential append (inline or overflow) ----
                int l_count = hl->header.entries_count;
                uint64_t l_txn = (uint64_t)atomic_load(&a_tree->mvcc_txn) + 1;

                if (a_value_len + a_sign_len > MAX_INLINE_PAYLOAD) {
                    uint64_t l_ov_id = s_overflow_write(a_tree, a_value, a_value_len, a_sign, a_sign_len, l_txn);
                    if (l_ov_id == 0) {
                        s_hot_leaf_flush(a_tree);
                        a_tree->hot_path_depth = 0;
                        if (l_arena)
                            dap_arena_reset(l_arena);
                        goto normal_path;
                    }
                    s_page_resolve_mmap(a_tree, hl);
                    l_hl_offsets = (uint16_t *)(hl->data + LEAF_HEADER_SIZE);
                    size_t l_entry_size = s_leaf_entry_total_size_overflow(a_text_key_len);
                    uint16_t l_new_offset = LEAF_LOWEST_OFFSET(hl->data) - (uint16_t)l_entry_size;
                    l_hl_offsets[l_count] = l_new_offset;
                    dap_global_db_leaf_entry_t *l_ent =
                        (dap_global_db_leaf_entry_t *)(hl->data + l_new_offset);
                    l_ent->driver_hash = *a_key;
                    l_ent->key_len = a_text_key_len;
                    l_ent->value_len = a_value_len;
                    l_ent->sign_len = a_sign_len;
                    l_ent->flags = a_flags | DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE;
                    uint8_t *l_dst = (uint8_t *)l_ent + sizeof(dap_global_db_leaf_entry_t);
                    memcpy(l_dst, a_text_key, a_text_key_len);
                *(uint64_t *)(l_dst + a_text_key_len) = l_ov_id;
                    hl->header.entries_count = l_count + 1;
                    hl->header.free_space -= (l_entry_size + LEAF_OFFSET_SIZE);
                    LEAF_LOWEST_OFFSET(hl->data) = l_new_offset;
                    a_tree->header.items_count++;
                    hl->is_dirty = true;
                    return 2;
                }

                // Inline append
                size_t l_entry_size = sizeof(dap_global_db_leaf_entry_t)
                                      + a_text_key_len + a_value_len + a_sign_len;
                uint16_t l_new_offset = LEAF_LOWEST_OFFSET(hl->data) - (uint16_t)l_entry_size;
                l_hl_offsets[l_count] = l_new_offset;
                dap_global_db_leaf_entry_t *l_ent =
                    (dap_global_db_leaf_entry_t *)(hl->data + l_new_offset);
                l_ent->driver_hash = *a_key;
                l_ent->key_len = a_text_key_len;
                l_ent->value_len = a_value_len;
                l_ent->sign_len = a_sign_len;
                l_ent->flags = a_flags;
                uint8_t *l_dst = (uint8_t *)l_ent + sizeof(dap_global_db_leaf_entry_t);
                memcpy(l_dst, a_text_key, a_text_key_len);
                if (a_value_len > 0)
                    memmove(l_dst + a_text_key_len, a_value, a_value_len);
                if (a_sign_len > 0)
                    memmove(l_dst + a_text_key_len + a_value_len, a_sign, a_sign_len);
                hl->header.entries_count = l_count + 1;
                hl->header.free_space -= (l_entry_size + LEAF_OFFSET_SIZE);
                LEAF_LOWEST_OFFSET(hl->data) = l_new_offset;
                a_tree->header.items_count++;
                hl->is_dirty = true;
                return 2;
            } else {
                // Key is within page range — need binary search (non-sequential case)
                int l_idx;
                uint64_t l_txn = (uint64_t)atomic_load(&a_tree->mvcc_txn) + 1;
                if (s_leaf_find_entry(hl, a_key, &l_idx) == 0) {
                    if (s_leaf_update_entry(hl, l_idx, a_text_key, a_text_key_len,
                                            a_value, a_value_len, a_sign, a_sign_len, a_flags, NULL,
                                            a_tree, l_txn) == 0) {
                        hl->is_dirty = true;
                        return 2;
                    }
                } else {
                    if (a_value_len + a_sign_len > MAX_INLINE_PAYLOAD) {
                        uint64_t l_ov_id = s_overflow_write(a_tree, a_value, a_value_len, a_sign, a_sign_len, l_txn);
                        if (l_ov_id == 0) {
                            s_hot_leaf_flush(a_tree);
                            a_tree->hot_path_depth = 0;
                            if (l_arena)
                                dap_arena_reset(l_arena);
                            goto normal_path;
                        }
                        s_page_resolve_mmap(a_tree, hl);
                        if (s_leaf_insert_entry_overflow(hl, l_idx, a_key, a_text_key, a_text_key_len,
                                                         l_ov_id, a_value_len, a_sign_len, a_flags, NULL) == 0) {
                            a_tree->header.items_count++;
                            hl->is_dirty = true;
                            return 2;
                        }
                    } else if (s_leaf_insert_entry(hl, l_idx, a_key, a_text_key, a_text_key_len,
                                                   a_value, a_value_len, a_sign, a_sign_len, a_flags, NULL) == 0) {
                        a_tree->header.items_count++;
                        hl->is_dirty = true;
                        return 2;
                    }
                }
            }
        } else if (l_in_range && l_cmp > 0 && a_tree->hot_path_depth > 0 && a_tree->mmap) {
            if (a_tree->in_batch) {
                // Batch-mode append-only split: no readers exist, safe to modify in-place.
                // Create new empty sibling, insert entry there, update parent in-place.
                int l_depth = a_tree->hot_path_depth;
                uint64_t l_parent_id = a_tree->hot_path[l_depth - 1].page_id;
                int l_ci = a_tree->hot_path[l_depth - 1].child_index;

                // Check parent has room (rare to be full — ~126 entries per branch)
                dap_global_db_page_t l_parent_buf;
                if (!s_page_read_writable(a_tree, l_parent_id, &l_parent_buf) ||
                    l_parent_buf.header.entries_count >= DAP_GLOBAL_DB_MAX_KEYS)
                    goto append_split_fallback;

                uint64_t l_sibling_id = s_page_allocate_new(a_tree);
                if (l_sibling_id == 0)
                    goto append_split_fallback;
                s_page_resolve_mmap(a_tree, hl);
                // Re-read parent (mremap may have invalidated pointer)
                if (!s_page_read_writable(a_tree, l_parent_id, &l_parent_buf))
                    goto append_split_fallback;

                // Create empty sibling leaf
                dap_global_db_page_t *l_sib = s_page_alloc(NULL);
                if (!l_sib)
                    goto append_split_fallback;
                l_sib->header.page_id = l_sibling_id;
                l_sib->header.flags = DAP_GLOBAL_DB_PAGE_LEAF;
                l_sib->header.entries_count = 0;
                l_sib->header.free_space = PAGE_DATA_SIZE - LEAF_HEADER_SIZE;
                l_sib->header.left_sibling = hl->header.page_id;
                l_sib->header.right_sibling = 0;
                LEAF_LOWEST_OFFSET(l_sib->data) = LEAF_LOWEST_OFFSET_INIT;

                // Insert entry into sibling
                uint64_t l_txn = (uint64_t)atomic_load(&a_tree->mvcc_txn) + 1;
                size_t l_payload = (size_t)a_value_len + (size_t)a_sign_len;
                if (l_payload > MAX_INLINE_PAYLOAD) {
                    uint64_t l_ov_id = s_overflow_write(a_tree, a_value, a_value_len,
                                                         a_sign, a_sign_len, l_txn);
                    if (l_ov_id == 0 ||
                        s_leaf_insert_entry_overflow(l_sib, 0, a_key, a_text_key, a_text_key_len,
                                                      l_ov_id, a_value_len, a_sign_len, a_flags, NULL) != 0) {
                        s_page_free(l_sib);
                        goto append_split_fallback;
                    }
                    s_page_resolve_mmap(a_tree, hl);
                    if (!s_page_read_writable(a_tree, l_parent_id, &l_parent_buf)) {
                        s_page_free(l_sib);
                        goto append_split_fallback;
                    }
                } else if (s_leaf_insert_entry(l_sib, 0, a_key, a_text_key, a_text_key_len,
                                                a_value, a_value_len, a_sign, a_sign_len, a_flags, NULL) != 0) {
                    s_page_free(l_sib);
                    goto append_split_fallback;
                }

                // Update hot_leaf's right_sibling and flush
                hl->header.right_sibling = l_sibling_id;
                s_page_write(a_tree, hl);

                // Separator = first key of right half (matches s_split_child convention)
                s_branch_insert_entry(&l_parent_buf, l_ci, a_key, l_sibling_id, NULL);
                s_page_write(a_tree, &l_parent_buf);

                // Write sibling and promote as writable mmap hot_leaf
                s_page_write(a_tree, l_sib);
                if (a_tree->mmap) {
                    DAP_DEL_Z(l_sib->data);
                    uint64_t l_sib_off = s_page_offset(l_sibling_id);
                    l_sib->data = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap) + l_sib_off
                                  + sizeof(dap_global_db_page_header_t);
                    l_sib->is_mmap_ref = true;
                    l_sib->is_mmap_writable = true;
                    l_sib->is_dirty = false;
                }
                s_page_free(a_tree->hot_leaf);
                a_tree->hot_leaf = l_sib;
                a_tree->hot_path[l_depth - 1].child_index = l_ci + 1;
                a_tree->header.items_count++;
                return 2;
            }
append_split_fallback:
            s_hot_leaf_flush(a_tree);
            a_tree->hot_path_depth = 0;
            if (l_arena)
                dap_arena_reset(l_arena);
            goto normal_path;
        }
        s_hot_leaf_flush(a_tree);
    }

normal_path:
    // ---- Normal path -------------------------------------------------------
    // Empty tree - create root
    if (a_tree->header.root_page == 0) {
        uint64_t l_root_id = s_page_allocate_new(a_tree);
        if (l_root_id == 0)
            return -1;
        // Root page allocated on heap (not arena) — survives as hot_leaf
        dap_global_db_page_t *l_root = s_page_alloc(NULL);
        if (!l_root)
            return -1;

        l_root->header.page_id = l_root_id;
        l_root->header.flags = DAP_GLOBAL_DB_PAGE_LEAF | DAP_GLOBAL_DB_PAGE_ROOT;
        l_root->header.left_sibling = 0;
        LEAF_LOWEST_OFFSET(l_root->data) = LEAF_LOWEST_OFFSET_INIT;

        {
            size_t l_payload = (size_t)a_value_len + (size_t)a_sign_len;
            uint64_t l_txn = atomic_load(&a_tree->mvcc_txn) + 1;
            if (l_payload > MAX_INLINE_PAYLOAD) {
                uint64_t l_ov_id = s_overflow_write(a_tree, a_value, a_value_len, a_sign, a_sign_len, l_txn);
                if (l_ov_id == 0 ||
                    s_leaf_insert_entry_overflow(l_root, 0, a_key, a_text_key, a_text_key_len,
                                                 l_ov_id, a_value_len, a_sign_len, a_flags, NULL) != 0) {
                    if (l_ov_id) s_overflow_free(a_tree, l_ov_id, l_txn);
                    s_page_free(l_root);
                    return -1;
                }
            } else if (s_leaf_insert_entry(l_root, 0, a_key, a_text_key, a_text_key_len,
                                          a_value, a_value_len, a_sign, a_sign_len, a_flags, NULL) != 0) {
                s_page_free(l_root);
                return -1;
            }
        }

        a_tree->header.root_page = l_root_id;
        a_tree->header.items_count = 1;
        a_tree->header.tree_height = 1;

        s_page_write(a_tree, l_root);
        a_tree->hot_leaf = l_root;
        a_tree->hot_path_depth = 0;
        s_header_write(a_tree);
        return 0;
    }

    // Batch mmap: skip 4KB arena root copy when no root split needed
    if (a_tree->in_batch && a_tree->mmap) {
        dap_global_db_page_t l_root_ref;
        if (s_page_read_ref(a_tree, a_tree->header.root_page, &l_root_ref) &&
            !s_page_needs_split(&l_root_ref, a_text_key_len, a_value_len, a_sign_len)) {
            int l_ret = s_insert_non_full(a_tree, &l_root_ref, a_key, a_text_key, a_text_key_len,
                                          a_value, a_value_len, a_sign, a_sign_len, a_flags);
            if (l_ret == 1) {
                s_hot_leaf_flush(a_tree);
                if (l_arena)
                    dap_arena_reset(l_arena);
                return s_btree_insert_impl(a_tree, a_key, a_text_key, a_text_key_len,
                                            a_value, a_value_len, a_sign, a_sign_len, a_flags);
            }
            if (l_arena)
                dap_arena_reset(l_arena);
            return l_ret;
        }
    }

    // Read root as arena copy (COW: never modify mmap in place)
    dap_global_db_page_t *l_root = s_page_read(a_tree, a_tree->header.root_page, l_arena);
    if (!l_root)
        return -1;

    // Check if root needs split
    if (s_page_needs_split(l_root, a_text_key_len, a_value_len, a_sign_len)) {
        uint64_t l_new_root_id = s_page_allocate_new(a_tree);
        if (l_new_root_id == 0)
            return -1;
        dap_global_db_page_t *l_new_root = s_page_alloc(l_arena);
        if (!l_new_root)
            return -1;

        l_new_root->header.page_id = l_new_root_id;
        l_new_root->header.flags = DAP_GLOBAL_DB_PAGE_BRANCH | DAP_GLOBAL_DB_PAGE_ROOT;
        l_new_root->header.entries_count = 0;

        l_root->header.flags &= ~DAP_GLOBAL_DB_PAGE_ROOT;
        s_branch_set_child(l_new_root, 0, a_tree->header.root_page, l_arena);

        dap_global_db_page_t *l_split_child = NULL;
        if (s_split_child(a_tree, l_new_root, 0, &l_split_child) != 0)
            return -1;

        // COW split child: the left half of the old root must go to a NEW page
        // so readers with snapshots referencing the old root still see full data.
        if (l_split_child) {
            if (a_tree->in_batch) {
                s_page_write(a_tree, l_split_child);
            } else {
                uint64_t l_old_split_id = l_split_child->header.page_id;
                uint64_t l_new_split_id = s_page_allocate_new(a_tree);
                if (l_new_split_id == 0) {
                    log_it(L_ERROR, "COW: failed to allocate new page for root split child");
                    return -1;
                }
                l_split_child->header.page_id = l_new_split_id;
                s_page_write(a_tree, l_split_child);
                uint64_t l_txn = atomic_load(&a_tree->mvcc_txn) + 1;
                s_deferred_free_add(a_tree, l_txn, l_old_split_id);
                s_branch_set_child(l_new_root, 0, l_new_split_id, l_arena);
            }
        }

        a_tree->header.root_page = l_new_root_id;
        a_tree->header.tree_height++;

        int l_ret = s_insert_non_full(a_tree, l_new_root, a_key, a_text_key, a_text_key_len,
                                      a_value, a_value_len, a_sign, a_sign_len, a_flags);
        // l_new_root is arena — freed by arena_reset
        s_header_write(a_tree);

        // Bulk-free all transient arena allocations from this insert
        if (l_arena)
            dap_arena_reset(l_arena);
        return l_ret;
    }

    int l_ret = s_insert_non_full(a_tree, l_root, a_key, a_text_key, a_text_key_len,
                                  a_value, a_value_len, a_sign, a_sign_len, a_flags);
    // l_root is arena — freed by reset

    // Code 1: entry deleted+needs re-insert via split path
    if (l_ret == 1) {
        s_hot_leaf_flush(a_tree);
        if (l_arena)
            dap_arena_reset(l_arena);
        return s_btree_insert_impl(a_tree, a_key, a_text_key, a_text_key_len,
                                    a_value, a_value_len, a_sign, a_sign_len, a_flags);
    }

    // Bulk-free all transient arena allocations from this insert
    if (l_arena)
        dap_arena_reset(l_arena);

    return l_ret;
}

int dap_global_db_batch_begin(dap_global_db_t *a_tree)
{
    dap_return_val_if_fail(a_tree && !a_tree->read_only && !a_tree->in_batch, -1);
    pthread_rwlock_wrlock(&a_tree->lock);
    a_tree->in_batch = true;
    return 0;
}

int dap_global_db_batch_commit(dap_global_db_t *a_tree)
{
    dap_return_val_if_fail(a_tree && a_tree->in_batch, -1);
    s_mvcc_commit(a_tree);
    a_tree->in_batch = false;
    pthread_rwlock_unlock(&a_tree->lock);
    return 0;
}

int dap_global_db_insert(dap_global_db_t *a_tree,
                         const dap_global_db_key_t *a_key,
                         const char *a_text_key, uint32_t a_text_key_len,
                         const void *a_value, uint32_t a_value_len,
                         const void *a_sign, uint32_t a_sign_len,
                         uint8_t a_flags)
{
    dap_return_val_if_fail(a_tree && a_key && !a_tree->read_only, -1);
    bool l_in_batch = a_tree->in_batch;
    if (!l_in_batch)
        pthread_rwlock_wrlock(&a_tree->lock);
    int l_ret = s_btree_insert_impl(a_tree, a_key, a_text_key, a_text_key_len,
                                     a_value, a_value_len, a_sign, a_sign_len, a_flags);
    if (l_in_batch) {
        if (l_ret == 0) {
            if (++a_tree->mvcc_commit_counter >= 64 || a_tree->deferred_batch_count > 128) {
                s_header_write(a_tree);  // Persist header before freeing old pages
                s_deferred_free_reclaim(a_tree);
                a_tree->mvcc_commit_counter = 0;
            }
        } else if (l_ret == 2)
            l_ret = 0;
    } else {
        if (l_ret == 2)
            l_ret = 0;
        if (l_ret == 0)
            s_mvcc_commit(a_tree);
    }
    if (s_debug_more) {
        char _children_buf[256] = "";
        if (a_tree->header.root_page != 0 && l_ret == 0) {
            dap_arena_t *_a = a_tree->arena;
            dap_global_db_page_t *_r = s_page_read(a_tree, a_tree->header.root_page, _a);
            if (_r && (_r->header.flags & DAP_GLOBAL_DB_PAGE_BRANCH)) {
                int _off = 0;
                for (int _i = 0; _i <= _r->header.entries_count && _off < (int)sizeof(_children_buf) - 20; _i++)
                    _off += snprintf(_children_buf + _off, sizeof(_children_buf) - _off, "%s%llu",
                                     _i ? "," : "", (unsigned long long)s_branch_get_child(_r, _i));
            }
            if (_a) dap_arena_reset(_a);
        }
        debug_if(s_debug_more, L_DEBUG, "INSERT root=%llu items=%llu ret=%d children=[%s]",
                (unsigned long long)a_tree->header.root_page,
                (unsigned long long)a_tree->header.items_count, l_ret, _children_buf);
    }
    if (!l_in_batch)
        pthread_rwlock_unlock(&a_tree->lock);
    return l_ret;
}

/**
 * @brief Add a page to the free list for later reuse by s_page_allocate_new().
 */
static void s_page_add_to_free_list(dap_global_db_t *a_tree, uint64_t a_page_id)
{
    dap_global_db_page_t *l_page = s_page_alloc(a_tree->arena);
    if (!l_page) return;
    l_page->header.page_id = a_page_id;
    l_page->header.flags = 0;
    l_page->header.entries_count = 0;
    *(uint64_t *)l_page->data = a_tree->header.free_list_head;
    s_page_write(a_tree, l_page);
    // l_page freed by arena_reset
    a_tree->header.free_list_head = a_page_id;
}

// ============================================================================
// MVCC Helpers
// ============================================================================

/**
 * @brief Acquire a reader snapshot — lock-free CAS on snapshot slot array.
 *
 * Finds an empty slot (txn_id == 0), sets it to the current mvcc_txn via CAS.
 * Returns the snapshot slot index (for release) and the frozen root/txn.
 *
 * @return Slot index >= 0 on success, -1 if all slots are busy
 */
#define SNAPSHOT_CACHELINE_STRIDE  (64 / sizeof(uint64_t))  // 8 slots per cache line

static _Atomic(int) s_snapshot_hint_gen = 0;
static _Thread_local __attribute__((tls_model("initial-exec"))) int s_tl_snapshot_hint = -1;

static int s_snapshot_acquire(dap_global_db_t *a_tree,
                               uint64_t *a_out_root, uint64_t *a_out_txn,
                               uint64_t *a_out_count)
{
    if (s_tl_snapshot_hint < 0)
        s_tl_snapshot_hint = (atomic_fetch_add(&s_snapshot_hint_gen, SNAPSHOT_CACHELINE_STRIDE))
                              % DAP_BTREE_MAX_SNAPSHOTS;
    int l_start = s_tl_snapshot_hint;
    for (int j = 0; j < DAP_BTREE_MAX_SNAPSHOTS; j++) {
        int i = (l_start + j) % DAP_BTREE_MAX_SNAPSHOTS;
        uint64_t l_expected = 0;
        if (atomic_compare_exchange_strong(&a_tree->snapshot_txns[i], &l_expected, 1)) {
            uint64_t l_txn, l_root, l_count;
            for (;;) {
                uint64_t l_seq1 = atomic_load_explicit(&a_tree->mvcc_seq, memory_order_acquire);
                if (l_seq1 & 1) continue;
                l_txn   = atomic_load_explicit(&a_tree->mvcc_txn, memory_order_relaxed);
                l_root  = atomic_load_explicit(&a_tree->mvcc_root, memory_order_relaxed);
                l_count = atomic_load_explicit(&a_tree->mvcc_count, memory_order_relaxed);
                uint64_t l_seq2 = atomic_load_explicit(&a_tree->mvcc_seq, memory_order_acquire);
                if (l_seq1 == l_seq2)
                    break;
            }
            atomic_store_explicit(&a_tree->snapshot_txns[i], l_txn == 0 ? 1 : l_txn,
                                  memory_order_release);
            s_tl_snapshot_hint = i;
            if (a_out_root)  *a_out_root = l_root;
            if (a_out_txn)   *a_out_txn = l_txn;
            if (a_out_count) *a_out_count = l_count;
            return i;
        }
    }
    log_it(L_WARNING, "MVCC: all %d snapshot slots busy", DAP_BTREE_MAX_SNAPSHOTS);
    return -1;
}

/**
 * @brief Release a reader snapshot slot.
 */
static void s_snapshot_release(dap_global_db_t *a_tree, int a_slot)
{
    if (a_slot >= 0 && a_slot < DAP_BTREE_MAX_SNAPSHOTS)
        atomic_store_explicit(&a_tree->snapshot_txns[a_slot], 0, memory_order_release);
}

// Thread-local read transaction: holds a snapshot across multiple reads.
// Leaf cache amortizes tree traversal: if the key falls within the cached
// leaf's [min, max] key range, we skip root-to-leaf descent entirely.
typedef struct tl_read_txn {
    dap_global_db_t *tree;
    uint64_t snap_root;
    int slot;
    uint64_t cached_leaf_id;
    dap_global_db_key_t cached_leaf_min;
    dap_global_db_key_t cached_leaf_max;
} tl_read_txn_t;

static _Thread_local __attribute__((tls_model("initial-exec"))) tl_read_txn_t s_tl_read_txn = { .tree = NULL, .slot = -1 };

// Per-thread overflow read buffer (replaces per-tree overflow_read_buf for thread-safety)
static _Thread_local __attribute__((tls_model("initial-exec"))) uint8_t *s_tl_overflow_buf = NULL;
static _Thread_local __attribute__((tls_model("initial-exec"))) size_t s_tl_overflow_buf_size = 0;

static inline uint8_t *s_tl_overflow_ensure(size_t a_size)
{
    if (s_tl_overflow_buf_size < a_size) {
        uint8_t *l_new = (uint8_t *)DAP_REALLOC(s_tl_overflow_buf, a_size);
        if (!l_new)
            return NULL;
        s_tl_overflow_buf = l_new;
        s_tl_overflow_buf_size = a_size;
    }
    return s_tl_overflow_buf;
}

int dap_global_db_read_begin(dap_global_db_t *a_tree)
{
    dap_return_val_if_fail(a_tree, -1);
    if (s_tl_read_txn.tree == a_tree && s_tl_read_txn.slot >= 0)
        return 0;
    uint64_t l_snap_root;
    int l_slot = s_snapshot_acquire(a_tree, &l_snap_root, NULL, NULL);
    if (l_slot < 0)
        return -1;
    s_tl_read_txn.tree = a_tree;
    s_tl_read_txn.snap_root = l_snap_root;
    s_tl_read_txn.slot = l_slot;
    s_tl_read_txn.cached_leaf_id = 0;
    return 0;
}

void dap_global_db_read_end(dap_global_db_t *a_tree)
{
    if (s_tl_read_txn.tree == a_tree && s_tl_read_txn.slot >= 0) {
        s_snapshot_release(a_tree, s_tl_read_txn.slot);
        s_tl_read_txn.tree = NULL;
        s_tl_read_txn.slot = -1;
        s_tl_read_txn.cached_leaf_id = 0;
    }
}

static inline int s_tl_read_txn_root(dap_global_db_t *a_tree, uint64_t *a_out_root)
{
    if (s_tl_read_txn.tree == a_tree && s_tl_read_txn.slot >= 0) {
        *a_out_root = s_tl_read_txn.snap_root;
        return 1;
    }
    return 0;
}

static inline void s_tl_leaf_cache_update(uint64_t a_leaf_id,
                                           const dap_global_db_key_t *a_min,
                                           const dap_global_db_key_t *a_max)
{
    if (s_tl_read_txn.slot >= 0 && s_tl_read_txn.cached_leaf_id != a_leaf_id) {
        s_tl_read_txn.cached_leaf_id = a_leaf_id;
        memcpy(&s_tl_read_txn.cached_leaf_min, a_min, sizeof(dap_global_db_key_t));
        memcpy(&s_tl_read_txn.cached_leaf_max, a_max, sizeof(dap_global_db_key_t));
    }
}

/**
 * @brief Record a page ID as freed during a COW write at given txn.
 *
 * The page is not immediately returned to the free list — it goes to a
 * deferred batch so that active reader snapshots can still access it.
 */
static void s_deferred_free_add(dap_global_db_t *a_tree, uint64_t a_txn, uint64_t a_page_id)
{
    dap_btree_deferred_batch_t *l_batch = a_tree->deferred_last_batch;
    if (!l_batch || l_batch->txn_id != a_txn) {
        l_batch = NULL;
        for (size_t i = 0; i < a_tree->deferred_batch_count; i++) {
            if (a_tree->deferred_batches[i].txn_id == a_txn) {
                l_batch = &a_tree->deferred_batches[i];
                break;
            }
        }
    }

    if (!l_batch) {
        if (a_tree->deferred_batch_count >= a_tree->deferred_batch_capacity) {
            size_t l_new_cap = a_tree->deferred_batch_capacity ? a_tree->deferred_batch_capacity * 2 : 4;
            dap_btree_deferred_batch_t *l_new = DAP_NEW_SIZE(dap_btree_deferred_batch_t,
                                                              l_new_cap * sizeof(dap_btree_deferred_batch_t));
            if (!l_new) return;
            if (a_tree->deferred_batches) {
                memcpy(l_new, a_tree->deferred_batches,
                       a_tree->deferred_batch_count * sizeof(dap_btree_deferred_batch_t));
                DAP_DELETE(a_tree->deferred_batches);
            }
            a_tree->deferred_batches = l_new;
            a_tree->deferred_batch_capacity = l_new_cap;
        }
        l_batch = &a_tree->deferred_batches[a_tree->deferred_batch_count++];
        *l_batch = (dap_btree_deferred_batch_t){ .txn_id = a_txn };
    }
    a_tree->deferred_last_batch = l_batch;

    if (l_batch->count >= l_batch->capacity) {
        size_t l_new_cap = l_batch->capacity ? l_batch->capacity * 2 : 16;
        uint64_t *l_new = DAP_NEW_SIZE(uint64_t, l_new_cap * sizeof(uint64_t));
        if (!l_new) return;
        if (l_batch->page_ids) {
            memcpy(l_new, l_batch->page_ids, l_batch->count * sizeof(uint64_t));
            DAP_DELETE(l_batch->page_ids);
        }
        l_batch->page_ids = l_new;
        l_batch->capacity = l_new_cap;
    }

    l_batch->page_ids[l_batch->count++] = a_page_id;
}

static inline bool s_has_active_snapshots(const dap_global_db_t *a_tree)
{
    for (int i = 0; i < DAP_BTREE_MAX_SNAPSHOTS; i++)
        if (atomic_load_explicit(&a_tree->snapshot_txns[i], memory_order_relaxed) != 0)
            return true;
    return false;
}

/**
 * @brief Reclaim deferred free batches whose pages are no longer visible
 * to any active snapshot.
 *
 * Scans all snapshot slots to find the minimum active txn_id. Batches
 * with txn_id < min_active are safe to return to the free list.
 */
static void s_deferred_free_reclaim(dap_global_db_t *a_tree)
{
    uint64_t l_min_active = UINT64_MAX;
    for (int i = 0; i < DAP_BTREE_MAX_SNAPSHOTS; i++) {
        uint64_t l_txn = atomic_load_explicit(&a_tree->snapshot_txns[i], memory_order_acquire);
        if (l_txn > 0 && l_txn < l_min_active)
            l_min_active = l_txn;
    }

    size_t l_write = 0;
    for (size_t i = 0; i < a_tree->deferred_batch_count; i++) {
        dap_btree_deferred_batch_t *b = &a_tree->deferred_batches[i];
        if (b->txn_id < l_min_active) {
            for (size_t j = 0; j < b->count; j++)
                s_page_add_to_free_list(a_tree, b->page_ids[j]);
            DAP_DELETE(b->page_ids);
        } else {
            if (l_write != i)
                a_tree->deferred_batches[l_write] = *b;
            l_write++;
        }
    }
    a_tree->deferred_batch_count = l_write;
    a_tree->deferred_last_batch = NULL;
}

/**
 * @brief COW a leaf and set one sibling link (MVCC: avoid in-place update of neighbor).
 * @param a_set_right 1 = set right_sibling to a_new_sibling_id, 0 = set left_sibling
 * @return New page id, or 0 on alloc/read failure
 */
static uint64_t s_cow_leaf_update_sibling(dap_global_db_t *a_tree, uint64_t a_leaf_id,
                                           int a_set_right, uint64_t a_new_sibling_id, uint64_t a_txn,
                                           dap_arena_t *a_arena)
{
    dap_global_db_page_t *l_old = s_page_read(a_tree, a_leaf_id, a_arena);
    if (!l_old || !(l_old->header.flags & DAP_GLOBAL_DB_PAGE_LEAF))
        return 0;
    uint64_t l_new_id = s_page_allocate_new(a_tree);
    if (l_new_id == 0)
        return 0;
    dap_global_db_page_t *l_new = s_page_alloc(a_arena);
    if (!l_new)
        return 0;
    l_new->header = l_old->header;
    l_new->header.page_id = l_new_id;
    if (a_set_right)
        l_new->header.right_sibling = a_new_sibling_id;
    else
        l_new->header.left_sibling = a_new_sibling_id;
    memcpy(l_new->data, l_old->data, PAGE_DATA_SIZE);
    s_page_write(a_tree, l_new);
    s_deferred_free_add(a_tree, a_txn, a_leaf_id);
    return l_new_id;
}

/**
 * @brief COW chain: walk up the path from leaf to root, creating new page
 * copies at each level with updated child pointers.
 *
 * After a COW insert/delete modifies a leaf (or split creates a new page),
 * this function propagates the new page ID upward:
 *   - For each ancestor on the path: read as copy, update child pointer,
 *     allocate new page, write to new location, defer free old page.
 *   - Returns the new root page ID.
 *
 * @param a_tree          Tree handle (owns arena and deferred free state)
 * @param a_path          Path from root to leaf's parent (a_path[0] = root)
 * @param a_path_depth    Number of entries in path
 * @param a_new_child_id  New page ID at the bottom of the chain (modified leaf)
 * @param a_txn           Transaction ID for deferred free tracking
 * @return New root page ID, or 0 on error
 */
static uint64_t s_cow_chain_up(dap_global_db_t *a_tree,
                                const struct dap_btree_path_entry *a_path,
                                int a_path_depth,
                                uint64_t a_new_child_id,
                                uint64_t a_txn)
{
    dap_arena_t *l_arena = a_tree->arena;
    uint64_t l_child_id = a_new_child_id;

    for (int i = a_path_depth - 1; i >= 0; i--) {
        uint64_t l_old_id = a_path[i].page_id;
        int l_idx = a_path[i].child_index;

        dap_global_db_page_t *l_page = s_page_read(a_tree, l_old_id, l_arena);
        if (!l_page) return 0;

        s_branch_set_child(l_page, l_idx, l_child_id, l_arena);

        uint64_t l_new_id = s_page_allocate_new(a_tree);
        if (l_new_id == 0) return 0;

        l_page->header.page_id = l_new_id;
        l_page->is_dirty = true;
        s_page_write(a_tree, l_page);

        s_deferred_free_add(a_tree, a_txn, l_old_id);
        l_child_id = l_new_id;
    }

    return l_child_id;
}

/**
 * @brief Publish current header state as the new MVCC snapshot.
 *
 * Called after a successful write transaction to make changes visible
 * to new readers. Also attempts to reclaim old deferred free batches.
 */
static void s_mvcc_commit(dap_global_db_t *a_tree)
{
    // Flush dirty hot_leaf to mmap BEFORE publishing the new root.
    if (a_tree->hot_leaf && a_tree->hot_leaf->is_dirty)
        s_hot_leaf_flush(a_tree);

    uint64_t l_new_txn = atomic_load(&a_tree->mvcc_txn) + 1;

    // Debug: verify tree integrity before publishing
#ifndef NDEBUG
    if (a_tree->header.root_page != 0) {
        uint64_t l_actual = dap_global_db_count_at_root(a_tree, a_tree->header.root_page);
        if (l_actual != a_tree->header.items_count) {
            log_it(L_ERROR, "MVCC commit: count mismatch! tree_count=%llu header_items=%llu txn=%llu root=%llu",
                   (unsigned long long)l_actual,
                   (unsigned long long)a_tree->header.items_count,
                   (unsigned long long)l_new_txn,
                   (unsigned long long)a_tree->header.root_page);
            dap_global_db_page_t _rbuf;
            if (s_page_read_ref(a_tree, a_tree->header.root_page, &_rbuf) &&
                (_rbuf.header.flags & DAP_GLOBAL_DB_PAGE_BRANCH)) {
                for (int _ci = 0; _ci <= _rbuf.header.entries_count; _ci++) {
                    uint64_t _cid = s_branch_get_child(&_rbuf, _ci);
                    dap_global_db_page_t _cbuf;
                    if (s_page_read_ref(a_tree, _cid, &_cbuf))
                        log_it(L_ERROR, "  child[%d] page=%llu entries=%d flags=0x%x left_sib=%llu right_sib=%llu",
                               _ci, (unsigned long long)_cid, _cbuf.header.entries_count, _cbuf.header.flags,
                               (unsigned long long)_cbuf.header.left_sibling,
                               (unsigned long long)_cbuf.header.right_sibling);
                    else
                        log_it(L_ERROR, "  child[%d] page=%llu UNREADABLE", _ci, (unsigned long long)_cid);
                }
            }
        }
    }
#endif

    // Seqlock: odd seq signals "publish in progress" to lock-free readers
    atomic_fetch_add_explicit(&a_tree->mvcc_seq, 1, memory_order_release);

    atomic_store_explicit(&a_tree->mvcc_count, a_tree->header.items_count, memory_order_relaxed);
    atomic_store_explicit(&a_tree->mvcc_height, a_tree->header.tree_height, memory_order_relaxed);
    atomic_store_explicit(&a_tree->mvcc_root, a_tree->header.root_page, memory_order_relaxed);
    atomic_store_explicit(&a_tree->mvcc_txn, l_new_txn, memory_order_relaxed);

    // Seqlock: even seq signals "publish complete"
    atomic_fetch_add_explicit(&a_tree->mvcc_seq, 1, memory_order_release);

    // Persist header to mmap/disk BEFORE reclaiming old pages.
    // Without this, an unclean shutdown leaves the on-disk header pointing
    // to the old root page, which may have been freed and reused (e.g. as
    // an OVERFLOW page), causing corruption on restart.
    s_header_write(a_tree);

    if (++a_tree->mvcc_commit_counter >= 64 || a_tree->deferred_batch_count > 128) {
        s_deferred_free_reclaim(a_tree);
        a_tree->mvcc_commit_counter = 0;
    }
}

/**
 * @brief Remove a separator entry from a branch page at a given index,
 *        collapsing child pointers accordingly.
 *
 * After removal, the child at (a_index + 1) is removed (the right side
 * of the separator). The child at a_index is preserved.
 */
static void s_branch_remove_entry(dap_global_db_page_t *a_page, int a_index, dap_arena_t *a_arena)
{
    s_page_cow(a_page, a_arena);
    int l_count = a_page->header.entries_count;
    dap_global_db_branch_entry_t *l_entries =
        (dap_global_db_branch_entry_t *)(a_page->data + sizeof(uint64_t));

    // Shift entries left to close the gap
    for (int i = a_index; i < l_count - 1; i++) {
        l_entries[i] = l_entries[i + 1];
    }
    a_page->header.entries_count--;
    a_page->is_dirty = true;
}

/**
 * @brief Merge all entries from a_src leaf into a_dst leaf.
 *
 * Entries are appended to a_dst in order. Caller must ensure that
 * a_dst has enough free space to hold all of a_src's entries.
 *
 * @return 0 on success, -1 if space insufficient.
 */
static int s_leaf_merge_into(dap_global_db_page_t *a_dst,
                             dap_global_db_page_t *a_src,
                             dap_arena_t *a_arena)
{
    int l_src_count = a_src->header.entries_count;
    for (int i = 0; i < l_src_count; i++) {
        uint8_t *l_data;
        size_t l_total;
        dap_global_db_leaf_entry_t *l_entry = s_leaf_entry_at(a_src, i, &l_data, &l_total);
        if (!l_entry) return -1;

        int l_ins_idx;
        s_leaf_find_entry(a_dst, &l_entry->driver_hash, &l_ins_idx);
        if (l_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE) {
            uint64_t l_ov_id = *(const uint64_t *)(l_data + l_entry->key_len);
            if (s_leaf_insert_entry_overflow(a_dst, l_ins_idx, &l_entry->driver_hash,
                                             (const char *)l_data, l_entry->key_len,
                                             l_ov_id, l_entry->value_len, l_entry->sign_len,
                                             l_entry->flags, a_arena) != 0)
                return -1;
        } else if (s_leaf_insert_entry(a_dst, l_ins_idx, &l_entry->driver_hash,
                                       (const char *)l_data, l_entry->key_len,
                                       l_data + l_entry->key_len, l_entry->value_len,
                                       l_data + l_entry->key_len + l_entry->value_len,
                                       l_entry->sign_len, l_entry->flags, a_arena) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Calculate total byte size of all entries in a leaf page.
 */
static size_t s_leaf_total_entry_bytes(dap_global_db_page_t *a_page)
{
    size_t l_total = 0;
    int l_count = a_page->header.entries_count;
    for (int i = 0; i < l_count; i++) {
        size_t l_size = 0;
        s_leaf_entry_at(a_page, i, NULL, &l_size);
        l_total += l_size + LEAF_OFFSET_SIZE;
    }
    return l_total;
}

// Maximum depth for parent path tracking during delete
#define BTREE_PATH_MAX_DEPTH  32

typedef struct dap_btree_path_entry s_path_entry_t;

static int s_btree_delete_impl(dap_global_db_t *a_tree, const dap_global_db_key_t *a_key)
{
    s_hot_leaf_flush(a_tree);

    if (a_tree->header.root_page == 0)
        return 1;

    dap_arena_t *l_arena = a_tree->arena;
    uint64_t l_txn = atomic_load(&a_tree->mvcc_txn) + 1;

    // Navigate to leaf, recording path for COW chain-up
    s_path_entry_t l_path[BTREE_PATH_MAX_DEPTH];
    int l_depth = 0;

    dap_global_db_page_t *l_page = s_page_read(a_tree, a_tree->header.root_page, l_arena);
    if (!l_page)
        return -1;

    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        bool l_found;
        int l_index = s_search_in_page(l_page, a_key, &l_found);
        if (l_depth < BTREE_PATH_MAX_DEPTH) {
            l_path[l_depth].page_id = l_page->header.page_id;
            l_path[l_depth].child_index = l_index;
            l_depth++;
        }
        uint64_t l_child_id = s_branch_get_child(l_page, l_index);

        l_page = s_page_read(a_tree, l_child_id, l_arena);
        if (!l_page) {
            if (l_arena) dap_arena_reset(l_arena);
            return -1;
        }
    }

    // Find entry in leaf
    int l_index;
    if (s_leaf_find_entry(l_page, a_key, &l_index) != 0) {
        if (l_arena) dap_arena_reset(l_arena);
        return 1;  // Not found
    }

    {
        uint8_t *l_edata = NULL;
        dap_global_db_leaf_entry_t *l_entry = s_leaf_entry_at(l_page, l_index, &l_edata, NULL);
        if (l_entry && (l_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE) && l_edata) {
            uint64_t l_ov_id = *(const uint64_t *)(l_edata + l_entry->key_len);
            uint64_t l_txn = atomic_load(&a_tree->mvcc_txn) + 1;
            s_overflow_free(a_tree, l_ov_id, l_txn);
        }
    }
    if (s_leaf_delete_entry(l_page, l_index, l_arena) != 0) {
        if (l_arena) dap_arena_reset(l_arena);
        return -1;
    }

    a_tree->header.items_count--;

    // ---- Leaf rebalancing after deletion ------------------------------------
    //
    // All modified pages are COW'd to new page_ids to preserve MVCC snapshot
    // isolation.  Old page_ids are deferred-freed (not immediately recycled)
    // so that snapshot readers continue to see the pre-delete tree structure.
    //
    // Merge candidates MUST come from the PARENT's child pointers, NOT from
    // leaf sibling links (which can cross parent boundaries).
    //
    bool l_is_root = (l_page->header.flags & DAP_GLOBAL_DB_PAGE_ROOT) != 0;

    if (!l_is_root && l_page->header.entries_count < DAP_GLOBAL_DB_MIN_KEYS && l_depth > 0) {
        size_t l_our_bytes = s_leaf_total_entry_bytes(l_page);
        int l_our_count = l_page->header.entries_count;

        s_path_entry_t *l_parent_path = &l_path[l_depth - 1];
        dap_global_db_page_t *l_parent = s_page_read(a_tree, l_parent_path->page_id, l_arena);

        bool l_merged = false;
        int l_child_idx = l_parent_path->child_index;

        // Get SAME-PARENT siblings via parent's child pointers
        uint64_t l_right_id = 0;
        uint64_t l_left_id = 0;
        if (l_parent) {
            if (l_child_idx < l_parent->header.entries_count)
                l_right_id = s_branch_get_child(l_parent, l_child_idx + 1);
            if (l_child_idx > 0)
                l_left_id = s_branch_get_child(l_parent, l_child_idx - 1);
        }

        // Try merge with right sibling (same parent)
        if (!l_merged && l_right_id != 0 && l_parent) {
            dap_global_db_page_t *l_right = s_page_read(a_tree, l_right_id, l_arena);
            if (l_right) {
                size_t l_right_bytes = s_leaf_total_entry_bytes(l_right);
                size_t l_combined = l_our_bytes + l_right_bytes;
                size_t l_offsets_space = LEAF_HEADER_SIZE +
                    (l_our_count + l_right->header.entries_count) * LEAF_OFFSET_SIZE;
                if (l_offsets_space + l_combined <= PAGE_DATA_SIZE) {
                    if (s_leaf_merge_into(l_right, l_page, l_arena) == 0) {
                        l_right->header.left_sibling = l_page->header.left_sibling;

                        // Allocate new page for the merged right (allocate-first)
                        uint64_t l_new_right_id = s_page_allocate_new(a_tree);

                        // COW left neighbor with verify guard BEFORE writing merged page
                        uint64_t l_new_left_nb = 0;
                        if (l_page->header.left_sibling != 0 && l_child_idx > 0) {
                            uint64_t l_expected = s_branch_get_child(l_parent, l_child_idx - 1);
                            if (l_expected == l_page->header.left_sibling) {
                                l_new_left_nb = s_cow_leaf_update_sibling(a_tree,
                                    l_page->header.left_sibling, 1,
                                    (l_new_right_id != 0) ? l_new_right_id : l_right_id,
                                    l_txn, l_arena);
                            }
                        }

                        // Update merged page's left_sibling to COW'd neighbor
                        if (l_new_left_nb != 0)
                            l_right->header.left_sibling = l_new_left_nb;

                        // Write merged page
                        if (l_new_right_id != 0) {
                            s_deferred_free_add(a_tree, l_txn, l_right_id);
                            l_right->header.page_id = l_new_right_id;
                            s_page_write(a_tree, l_right);
                        } else {
                            s_page_write(a_tree, l_right);
                            l_new_right_id = l_right_id;
                        }

                        // Update parent with COW'd left neighbor
                        if (l_new_left_nb != 0)
                            s_branch_set_child(l_parent, l_child_idx - 1, l_new_left_nb, l_arena);

                        // Modify parent: replace current child with merged right, remove separator
                        s_branch_set_child(l_parent, l_child_idx, l_new_right_id, l_arena);
                        s_branch_remove_entry(l_parent, l_child_idx, l_arena);

                        // COW chain-up parent through root
                        uint64_t l_new_parent_id = s_page_allocate_new(a_tree);
                        if (l_new_parent_id != 0) {
                            s_deferred_free_add(a_tree, l_txn, l_parent_path->page_id);
                            l_parent->header.page_id = l_new_parent_id;
                            s_page_write(a_tree, l_parent);
                            if (l_depth > 1) {
                                uint64_t l_new_root = s_cow_chain_up(a_tree, l_path,
                                                                      l_depth - 1, l_new_parent_id, l_txn);
                                if (l_new_root != 0)
                                    a_tree->header.root_page = l_new_root;
                            } else {
                                a_tree->header.root_page = l_new_parent_id;
                            }
                        } else {
                            s_page_write(a_tree, l_parent);
                        }

                        // Deferred free the merged-away leaf
                        s_deferred_free_add(a_tree, l_txn, l_page->header.page_id);
                        l_merged = true;
                    }
                }
            }
        }

        // Try merge with left sibling (same parent)
        if (!l_merged && l_left_id != 0 && l_parent) {
            dap_global_db_page_t *l_left = s_page_read(a_tree, l_left_id, l_arena);
            if (l_left) {
                size_t l_left_bytes = s_leaf_total_entry_bytes(l_left);
                size_t l_combined = l_our_bytes + l_left_bytes;
                size_t l_offsets_space = LEAF_HEADER_SIZE +
                    (l_our_count + l_left->header.entries_count) * LEAF_OFFSET_SIZE;
                if (l_offsets_space + l_combined <= PAGE_DATA_SIZE) {
                    if (s_leaf_merge_into(l_left, l_page, l_arena) == 0) {
                        l_left->header.right_sibling = l_page->header.right_sibling;

                        // Allocate new page for the merged left (allocate-first)
                        uint64_t l_new_left_id = s_page_allocate_new(a_tree);

                        // COW right neighbor with verify guard BEFORE writing merged page
                        uint64_t l_new_right_nb = 0;
                        if (l_page->header.right_sibling != 0 && l_child_idx < (int)l_parent->header.entries_count) {
                            uint64_t l_expected = s_branch_get_child(l_parent, l_child_idx + 1);
                            if (l_expected == l_page->header.right_sibling) {
                                l_new_right_nb = s_cow_leaf_update_sibling(a_tree,
                                    l_page->header.right_sibling, 0,
                                    (l_new_left_id != 0) ? l_new_left_id : l_left_id,
                                    l_txn, l_arena);
                            }
                        }

                        // Update merged page's right_sibling to COW'd neighbor
                        if (l_new_right_nb != 0)
                            l_left->header.right_sibling = l_new_right_nb;

                        // Write merged page
                        if (l_new_left_id != 0) {
                            s_deferred_free_add(a_tree, l_txn, l_left_id);
                            l_left->header.page_id = l_new_left_id;
                            s_page_write(a_tree, l_left);
                        } else {
                            s_page_write(a_tree, l_left);
                            l_new_left_id = l_left_id;
                        }

                        // Update parent with COW'd right neighbor
                        if (l_new_right_nb != 0)
                            s_branch_set_child(l_parent, l_child_idx + 1, l_new_right_nb, l_arena);

                        // Modify parent: update left child pointer, remove separator
                        s_branch_set_child(l_parent, l_child_idx - 1, l_new_left_id, l_arena);
                        s_branch_remove_entry(l_parent, l_child_idx - 1, l_arena);

                        // COW chain-up parent through root
                        uint64_t l_new_parent_id = s_page_allocate_new(a_tree);
                        if (l_new_parent_id != 0) {
                            s_deferred_free_add(a_tree, l_txn, l_parent_path->page_id);
                            l_parent->header.page_id = l_new_parent_id;
                            s_page_write(a_tree, l_parent);
                            if (l_depth > 1) {
                                uint64_t l_new_root = s_cow_chain_up(a_tree, l_path,
                                                                      l_depth - 1, l_new_parent_id, l_txn);
                                if (l_new_root != 0)
                                    a_tree->header.root_page = l_new_root;
                            } else {
                                a_tree->header.root_page = l_new_parent_id;
                            }
                        } else {
                            s_page_write(a_tree, l_parent);
                        }

                        // Deferred free the merged-away leaf
                        s_deferred_free_add(a_tree, l_txn, l_page->header.page_id);
                        l_merged = true;
                    }
                }
            }
        }

        // Collapse root if empty
        if (l_parent && (l_parent->header.flags & DAP_GLOBAL_DB_PAGE_ROOT) &&
            l_parent->header.entries_count == 0) {
            uint64_t l_child_page = s_branch_get_child(l_parent, 0);
            if (l_child_page != 0) {
                dap_global_db_page_t *l_new_root = s_page_read(a_tree, l_child_page, l_arena);
                if (l_new_root) {
                    l_new_root->header.flags |= DAP_GLOBAL_DB_PAGE_ROOT;
                    // COW the new root
                    uint64_t l_new_root_id = s_page_allocate_new(a_tree);
                    if (l_new_root_id != 0) {
                        s_deferred_free_add(a_tree, l_txn, l_child_page);
                        l_new_root->header.page_id = l_new_root_id;
                        s_page_write(a_tree, l_new_root);
                        a_tree->header.root_page = l_new_root_id;
                    } else {
                        s_page_write(a_tree, l_new_root);
                        a_tree->header.root_page = l_child_page;
                    }
                }
                s_deferred_free_add(a_tree, l_txn, l_parent->header.page_id);
                a_tree->header.tree_height--;
            }
        }

        if (l_merged) {
            s_header_write(a_tree);
            if (l_arena) dap_arena_reset(l_arena);
            return 0;
        }
    }

    // Root leaf became empty
    if (l_is_root && l_page->header.entries_count == 0) {
        s_deferred_free_add(a_tree, l_txn, l_page->header.page_id);
        a_tree->header.root_page = 0;
        a_tree->header.tree_height = 0;
        s_header_write(a_tree);
        if (l_arena) dap_arena_reset(l_arena);
        return 0;
    }

    // Simple delete (no rebalance): COW the modified leaf + chain-up (allocate-first)
    {
        uint64_t l_old_leaf_id = l_page->header.page_id;
        uint64_t l_new_leaf_id = s_page_allocate_new(a_tree);
        if (l_new_leaf_id != 0) {
            // COW neighbors with verify guards BEFORE writing leaf
            uint64_t l_new_left_id = 0, l_new_right_id = 0;
            dap_global_db_page_t *l_parent = NULL;
            int l_ci = 0;
            uint64_t l_parent_id = 0;
            if (l_depth > 0) {
                l_parent_id = l_path[l_depth - 1].page_id;
                l_ci = l_path[l_depth - 1].child_index;
                l_parent = s_page_read(a_tree, l_parent_id, l_arena);
                if (l_parent) {
                    if (l_page->header.left_sibling != 0 && l_ci > 0) {
                        uint64_t l_expected = s_branch_get_child(l_parent, l_ci - 1);
                        if (l_expected == l_page->header.left_sibling)
                            l_new_left_id = s_cow_leaf_update_sibling(a_tree, l_page->header.left_sibling,
                                                                      1, l_new_leaf_id, l_txn, l_arena);
                    }
                    if (l_page->header.right_sibling != 0 && l_ci < (int)l_parent->header.entries_count) {
                        uint64_t l_expected = s_branch_get_child(l_parent, l_ci + 1);
                        if (l_expected == l_page->header.right_sibling)
                            l_new_right_id = s_cow_leaf_update_sibling(a_tree, l_page->header.right_sibling,
                                                                      0, l_new_leaf_id, l_txn, l_arena);
                    }
                }
            }

            // Update leaf's sibling links to COW'd neighbor IDs, then write
            if (l_new_left_id != 0)
                l_page->header.left_sibling = l_new_left_id;
            if (l_new_right_id != 0)
                l_page->header.right_sibling = l_new_right_id;

            l_page->header.page_id = l_new_leaf_id;
            s_page_write(a_tree, l_page);
            s_deferred_free_add(a_tree, l_txn, l_old_leaf_id);

            // Chain-up with updated parent
            if (l_parent) {
                s_branch_set_child(l_parent, l_ci, l_new_leaf_id, l_arena);
                if (l_new_left_id != 0)
                    s_branch_set_child(l_parent, l_ci - 1, l_new_left_id, l_arena);
                if (l_new_right_id != 0)
                    s_branch_set_child(l_parent, l_ci + 1, l_new_right_id, l_arena);
                uint64_t l_new_parent_id = s_page_allocate_new(a_tree);
                if (l_new_parent_id != 0) {
                    l_parent->header.page_id = l_new_parent_id;
                    l_parent->is_dirty = true;
                    s_page_write(a_tree, l_parent);
                    s_deferred_free_add(a_tree, l_txn, l_parent_id);
                    uint64_t l_new_root = s_cow_chain_up(a_tree, l_path, l_depth - 1,
                                                         l_new_parent_id, l_txn);
                    if (l_new_root != 0)
                        a_tree->header.root_page = l_new_root;
                } else {
                    uint64_t l_new_root = s_cow_chain_up(a_tree, l_path, l_depth,
                                                         l_new_leaf_id, l_txn);
                    if (l_new_root != 0)
                        a_tree->header.root_page = l_new_root;
                }
            } else if (l_depth > 0) {
                uint64_t l_new_root = s_cow_chain_up(a_tree, l_path, l_depth,
                                                     l_new_leaf_id, l_txn);
                if (l_new_root != 0)
                    a_tree->header.root_page = l_new_root;
            } else {
                a_tree->header.root_page = l_new_leaf_id;
            }
        } else {
            s_page_write(a_tree, l_page);
        }
    }
    s_header_write(a_tree);

    if (l_arena) dap_arena_reset(l_arena);
    return 0;
}

int dap_global_db_delete(dap_global_db_t *a_tree, const dap_global_db_key_t *a_key)
{
    dap_return_val_if_fail(a_tree && a_key && !a_tree->read_only, -1);
    pthread_rwlock_wrlock(&a_tree->lock);
    int l_ret = s_btree_delete_impl(a_tree, a_key);
    if (l_ret == 0)
        s_mvcc_commit(a_tree);
    pthread_rwlock_unlock(&a_tree->lock);
    return l_ret;
}

static int s_btree_get_impl(dap_global_db_t *a_tree,
                      const dap_global_db_key_t *a_key,
                      char **a_out_text_key,
                      void **a_out_value, uint32_t *a_out_value_len,
                      void **a_out_sign, uint32_t *a_out_sign_len,
                      uint8_t *a_out_flags,
                      uint64_t a_snapshot_root)
{
    uint64_t l_root = a_snapshot_root ? a_snapshot_root : a_tree->header.root_page;
    if (l_root == 0)
        return 1;  // Not found

    // ---- Hot leaf read-only search (no flush, no mutation) ----
    // Snapshot readers skip hot_leaf: they see only committed mmap data.
    if (!a_snapshot_root && a_tree->hot_leaf && a_tree->hot_leaf->is_dirty) {
        int l_hl_idx;
        if (s_leaf_find_entry(a_tree->hot_leaf, a_key, &l_hl_idx) == 0) {
            uint8_t *l_hl_data = NULL;
            dap_global_db_leaf_entry_t *l_hl_entry =
                s_leaf_entry_at(a_tree->hot_leaf, l_hl_idx, &l_hl_data, NULL);
            if (a_out_text_key && l_hl_entry->key_len > 0) {
                *a_out_text_key = DAP_NEW_Z_SIZE(char, l_hl_entry->key_len);
                memcpy(*a_out_text_key, l_hl_data, l_hl_entry->key_len);
            } else if (a_out_text_key)
                *a_out_text_key = NULL;
            if (a_out_value_len)
                *a_out_value_len = l_hl_entry->value_len;
            if (a_out_sign_len)
                *a_out_sign_len = l_hl_entry->sign_len;
            bool l_is_ov = (l_hl_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE)
                && ((size_t)l_hl_entry->value_len + l_hl_entry->sign_len > MAX_INLINE_PAYLOAD);
            if (l_is_ov) {
                uint64_t l_ov_id = *(const uint64_t *)(l_hl_data + l_hl_entry->key_len);
                size_t l_total = (size_t)l_hl_entry->value_len + (size_t)l_hl_entry->sign_len;
                uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, l_total);
                if (!l_buf || s_overflow_read(a_tree, l_ov_id, l_buf, l_total, NULL) != 0) {
                    DAP_DELETE(l_buf);
                    return -1;
                }
                if (a_out_value && l_hl_entry->value_len > 0) {
                    *a_out_value = DAP_NEW_Z_SIZE(uint8_t, l_hl_entry->value_len);
                    memcpy(*a_out_value, l_buf, l_hl_entry->value_len);
                } else if (a_out_value)
                    *a_out_value = NULL;
                if (a_out_sign && l_hl_entry->sign_len > 0) {
                    *a_out_sign = DAP_NEW_Z_SIZE(uint8_t, l_hl_entry->sign_len);
                    memcpy(*a_out_sign, l_buf + l_hl_entry->value_len, l_hl_entry->sign_len);
                } else if (a_out_sign)
                    *a_out_sign = NULL;
                DAP_DELETE(l_buf);
            } else {
                if (a_out_value && l_hl_entry->value_len > 0) {
                    *a_out_value = DAP_NEW_Z_SIZE(uint8_t, l_hl_entry->value_len);
                    memcpy(*a_out_value, l_hl_data + l_hl_entry->key_len, l_hl_entry->value_len);
                } else if (a_out_value)
                    *a_out_value = NULL;
                if (a_out_sign && l_hl_entry->sign_len > 0) {
                    *a_out_sign = DAP_NEW_Z_SIZE(uint8_t, l_hl_entry->sign_len);
                    memcpy(*a_out_sign, l_hl_data + l_hl_entry->key_len + l_hl_entry->value_len,
                           l_hl_entry->sign_len);
                } else if (a_out_sign)
                    *a_out_sign = NULL;
            }
            if (a_out_flags)
                *a_out_flags = l_is_ov
                    ? l_hl_entry->flags & (uint8_t)~DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE
                    : l_hl_entry->flags;
            return 0;
        }
        // Not found in hot_leaf — fall through to mmap search
    }

    // Zero-copy fast path: read-only traversal, no page allocation → mmap refs safe
    dap_global_db_page_t l_page_buf;
    if (s_page_read_ref(a_tree, l_root, &l_page_buf)) {
        dap_global_db_page_t *l_page = &l_page_buf;
        while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
            bool l_found;
            int l_index = s_search_in_page(l_page, a_key, &l_found);
            uint64_t l_child_id = s_branch_get_child(l_page, l_index);
            // No free needed — stack page with mmap ref
            if (!s_page_read_ref(a_tree, l_child_id, &l_page_buf))
                return -1;
        }

        s_page_refresh_from_hot_leaf(a_tree, l_page);

        int l_index;
        if (s_leaf_find_entry(l_page, a_key, &l_index) != 0)
            return 1;  // Not found

        uint8_t *l_data = NULL;
        dap_global_db_leaf_entry_t *l_entry = s_leaf_entry_at(l_page, l_index, &l_data, NULL);

        if (a_out_text_key && l_entry->key_len > 0) {
            *a_out_text_key = DAP_NEW_Z_SIZE(char, l_entry->key_len);
            memcpy(*a_out_text_key, l_data, l_entry->key_len);
        } else if (a_out_text_key) {
            *a_out_text_key = NULL;
        }
        if (a_out_value_len)
            *a_out_value_len = l_entry->value_len;
        if (a_out_sign_len)
            *a_out_sign_len = l_entry->sign_len;
        bool l_ov = (l_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE)
            && ((size_t)l_entry->value_len + l_entry->sign_len > MAX_INLINE_PAYLOAD);
        if (l_ov) {
            uint64_t l_ov_id = *(const uint64_t *)(l_data + l_entry->key_len);
            size_t l_total = (size_t)l_entry->value_len + (size_t)l_entry->sign_len;
            uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, l_total);
            if (!l_buf || s_overflow_read(a_tree, l_ov_id, l_buf, l_total, NULL) != 0) {
                DAP_DELETE(l_buf);
                return -1;
            }
            if (a_out_value && l_entry->value_len > 0) {
                *a_out_value = DAP_NEW_Z_SIZE(uint8_t, l_entry->value_len);
                memcpy(*a_out_value, l_buf, l_entry->value_len);
            } else if (a_out_value)
                *a_out_value = NULL;
            if (a_out_sign && l_entry->sign_len > 0) {
                *a_out_sign = DAP_NEW_Z_SIZE(uint8_t, l_entry->sign_len);
                memcpy(*a_out_sign, l_buf + l_entry->value_len, l_entry->sign_len);
            } else if (a_out_sign)
                *a_out_sign = NULL;
            DAP_DELETE(l_buf);
        } else {
            if (a_out_value && l_entry->value_len > 0) {
                *a_out_value = DAP_NEW_Z_SIZE(uint8_t, l_entry->value_len);
                memcpy(*a_out_value, l_data + l_entry->key_len, l_entry->value_len);
            } else if (a_out_value)
                *a_out_value = NULL;
            if (a_out_sign && l_entry->sign_len > 0) {
                *a_out_sign = DAP_NEW_Z_SIZE(uint8_t, l_entry->sign_len);
                memcpy(*a_out_sign, l_data + l_entry->key_len + l_entry->value_len, l_entry->sign_len);
            } else if (a_out_sign)
                *a_out_sign = NULL;
        }
        if (a_out_flags)
            *a_out_flags = l_ov ? l_entry->flags & (uint8_t)~DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE
                                : l_entry->flags;
        return 0;
    }

    // Fallback: heap-allocated page read (no mmap, no arena — read path)
    dap_global_db_page_t *l_page = s_page_read(a_tree, l_root, NULL);
    if (!l_page)
        return -1;

    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        bool l_found;
        int l_index = s_search_in_page(l_page, a_key, &l_found);
        uint64_t l_child_id = s_branch_get_child(l_page, l_index);
        s_page_free(l_page);

        l_page = s_page_read(a_tree, l_child_id, NULL);
        if (!l_page)
            return -1;
    }
    
    int l_index;
    if (s_leaf_find_entry(l_page, a_key, &l_index) != 0) {
        s_page_free(l_page);
        return 1;
    }
    
    uint8_t *l_data = NULL;
    dap_global_db_leaf_entry_t *l_entry = s_leaf_entry_at(l_page, l_index, &l_data, NULL);
    
    if (a_out_text_key && l_entry->key_len > 0) {
        *a_out_text_key = DAP_NEW_Z_SIZE(char, l_entry->key_len);
        memcpy(*a_out_text_key, l_data, l_entry->key_len);
    } else if (a_out_text_key) {
        *a_out_text_key = NULL;
    }
    if (a_out_value_len)
        *a_out_value_len = l_entry->value_len;
    if (a_out_sign_len)
        *a_out_sign_len = l_entry->sign_len;
    bool l_ov = (l_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE)
        && ((size_t)l_entry->value_len + l_entry->sign_len > MAX_INLINE_PAYLOAD);
    if (l_ov) {
        uint64_t l_ov_id = *(const uint64_t *)(l_data + l_entry->key_len);
        size_t l_total = (size_t)l_entry->value_len + (size_t)l_entry->sign_len;
        uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, l_total);
        if (!l_buf || s_overflow_read(a_tree, l_ov_id, l_buf, l_total, NULL) != 0) {
            DAP_DELETE(l_buf);
            s_page_free(l_page);
            return -1;
        }
        if (a_out_value && l_entry->value_len > 0) {
            *a_out_value = DAP_NEW_Z_SIZE(uint8_t, l_entry->value_len);
            memcpy(*a_out_value, l_buf, l_entry->value_len);
        } else if (a_out_value)
            *a_out_value = NULL;
        if (a_out_sign && l_entry->sign_len > 0) {
            *a_out_sign = DAP_NEW_Z_SIZE(uint8_t, l_entry->sign_len);
            memcpy(*a_out_sign, l_buf + l_entry->value_len, l_entry->sign_len);
        } else if (a_out_sign)
            *a_out_sign = NULL;
        DAP_DELETE(l_buf);
    } else {
        if (a_out_value && l_entry->value_len > 0) {
            *a_out_value = DAP_NEW_Z_SIZE(uint8_t, l_entry->value_len);
            memcpy(*a_out_value, l_data + l_entry->key_len, l_entry->value_len);
        } else if (a_out_value)
            *a_out_value = NULL;
        if (a_out_sign && l_entry->sign_len > 0) {
            *a_out_sign = DAP_NEW_Z_SIZE(uint8_t, l_entry->sign_len);
            memcpy(*a_out_sign, l_data + l_entry->key_len + l_entry->value_len, l_entry->sign_len);
        } else if (a_out_sign)
            *a_out_sign = NULL;
    }
    if (a_out_flags)
        *a_out_flags = l_ov ? l_entry->flags & (uint8_t)~DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE
                            : l_entry->flags;
    s_page_free(l_page);
    return 0;
}

int dap_global_db_fetch(dap_global_db_t *a_tree,
                      const dap_global_db_key_t *a_key,
                      char **a_out_text_key,
                      void **a_out_value, uint32_t *a_out_value_len,
                      void **a_out_sign, uint32_t *a_out_sign_len,
                      uint8_t *a_out_flags)
{
    dap_return_val_if_fail(a_tree && a_key, -1);
    uint64_t l_snap_root, l_snap_txn;
    int l_slot = s_snapshot_acquire(a_tree, &l_snap_root, &l_snap_txn, NULL);
    if (l_slot >= 0) {
        int l_ret = s_btree_get_impl(a_tree, a_key, a_out_text_key, a_out_value,
                                      a_out_value_len, a_out_sign, a_out_sign_len,
                                      a_out_flags, l_snap_root);
        s_snapshot_release(a_tree, l_slot);
        return l_ret;
    }
    // Fallback: snapshot slots exhausted
    pthread_rwlock_rdlock(&a_tree->lock);
    int l_ret = s_btree_get_impl(a_tree, a_key, a_out_text_key, a_out_value,
                                  a_out_value_len, a_out_sign, a_out_sign_len,
                                  a_out_flags, 0);
    pthread_rwlock_unlock(&a_tree->lock);
    return l_ret;
}

static int s_btree_get_ref_impl(dap_global_db_t *a_tree,
                                const dap_global_db_key_t *a_key,
                                dap_global_db_ref_t *a_out_text_key,
                                dap_global_db_ref_t *a_out_value,
                                dap_global_db_ref_t *a_out_sign,
                                uint8_t *a_out_flags,
                                uint64_t a_snapshot_root)
{
    uint64_t l_root = a_snapshot_root ? a_snapshot_root : a_tree->header.root_page;
    if (l_root == 0) {
        debug_if(s_debug_more, L_WARNING, "get_ref: root=0, tree empty");
        return 1;
    }

    // ---- Hot leaf read-only search (no flush, no mutation) ----
    // Snapshot readers skip hot_leaf.
    if (!a_snapshot_root && a_tree->hot_leaf && a_tree->hot_leaf->is_dirty) {
        int l_hl_idx;
        if (s_leaf_find_entry(a_tree->hot_leaf, a_key, &l_hl_idx) == 0) {
            uint8_t *l_hl_data = NULL;
            dap_global_db_leaf_entry_t *l_hl_entry =
                s_leaf_entry_at(a_tree->hot_leaf, l_hl_idx, &l_hl_data, NULL);
            if (a_out_text_key) {
                a_out_text_key->data = l_hl_entry->key_len > 0
                    ? (void *)l_hl_data : NULL;
                a_out_text_key->len = l_hl_entry->key_len;
            }
            bool l_ov = (l_hl_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE)
                && ((size_t)l_hl_entry->value_len + l_hl_entry->sign_len > MAX_INLINE_PAYLOAD);
            if (l_ov) {
                uint64_t l_ov_id = *(const uint64_t *)(l_hl_data + l_hl_entry->key_len);
                size_t l_total = (size_t)l_hl_entry->value_len + (size_t)l_hl_entry->sign_len;
                bool l_zc_ok = false;
                if (a_tree->mmap) {
                    if (l_total <= PAGE_DATA_SIZE) {
                        l_zc_ok = true;
                    } else {
                        const dap_global_db_page_header_t *l_ov_hdr =
                            (const dap_global_db_page_header_t *)((uint8_t *)dap_mmap_get_ptr(a_tree->mmap)
                                                                  + s_page_offset(l_ov_id));
                        l_zc_ok = !!(l_ov_hdr->flags & DAP_GLOBAL_DB_PAGE_OVERFLOW_CONTIGUOUS);
                    }
                }
                if (l_zc_ok) {
                    uint8_t *l_ov_ptr = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap)
                                        + s_page_offset(l_ov_id)
                                        + sizeof(dap_global_db_page_header_t);
                    if (a_out_value) {
                        a_out_value->data = l_hl_entry->value_len > 0 ? l_ov_ptr : NULL;
                        a_out_value->len = l_hl_entry->value_len;
                    }
                    if (a_out_sign) {
                        a_out_sign->data = l_hl_entry->sign_len > 0
                            ? l_ov_ptr + l_hl_entry->value_len : NULL;
                        a_out_sign->len = l_hl_entry->sign_len;
                    }
                } else {
                    uint8_t *l_buf = s_tl_overflow_ensure(l_total);
                    if (!l_buf) {
                        log_it(L_ERROR, "HL overflow TLS alloc failed: %zu", l_total);
                        return -1;
                    }
                    if (s_overflow_read(a_tree, l_ov_id, l_buf, l_total, NULL) != 0) {
                        log_it(L_ERROR, "HL overflow read failed: ov_id=%llu total=%zu",
                               (unsigned long long)l_ov_id, l_total);
                        return -1;
                    }
                    if (a_out_value) {
                        a_out_value->data = l_hl_entry->value_len > 0 ? l_buf : NULL;
                        a_out_value->len = l_hl_entry->value_len;
                    }
                    if (a_out_sign) {
                        a_out_sign->data = l_hl_entry->sign_len > 0
                            ? l_buf + l_hl_entry->value_len : NULL;
                        a_out_sign->len = l_hl_entry->sign_len;
                    }
                }
            } else {
                if (a_out_value) {
                    a_out_value->data = l_hl_entry->value_len > 0
                        ? (void *)(l_hl_data + l_hl_entry->key_len) : NULL;
                    a_out_value->len = l_hl_entry->value_len;
                }
                if (a_out_sign) {
                    a_out_sign->data = l_hl_entry->sign_len > 0
                        ? (void *)(l_hl_data + l_hl_entry->key_len + l_hl_entry->value_len)
                        : NULL;
                    a_out_sign->len = l_hl_entry->sign_len;
                }
            }
            if (a_out_flags)
                *a_out_flags = l_ov ? l_hl_entry->flags & (uint8_t)~DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE
                                    : l_hl_entry->flags;
            return 0;
        }
        // Not found in hot_leaf — fall through to mmap search
    }

    // ==== True zero-copy path (LMDB approach) ====
    // All data is accessed via direct pointer arithmetic into the mmap region.
    // No memcpy, no struct copy, no allocation — just pointer casts.
    if (a_tree->mmap) {
        uint8_t *l_base = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap);
        size_t l_mmap_size = dap_mmap_get_size(a_tree->mmap);

        // Validate root page once; inner pages are trusted (written by us)
        uint64_t l_root_offset = s_page_offset(l_root);
        if (l_root_offset + DAP_GLOBAL_DB_PAGE_SIZE > l_mmap_size) {
            log_it(L_ERROR, "ZC root page out of mmap: page=%llu offset=%llu mmap_size=%zu",
                   (unsigned long long)l_root, (unsigned long long)l_root_offset, l_mmap_size);
            return -1;
        }

        uint64_t l_page_id = l_root;
        int l_zc_depth = 0;
        for (;;) {
            uint64_t l_offset = s_page_offset(l_page_id);
            const dap_global_db_page_header_t *l_hdr =
                (const dap_global_db_page_header_t *)(l_base + l_offset);
            const uint8_t *l_data = (const uint8_t *)(l_hdr + 1);

            if (l_hdr->flags & DAP_GLOBAL_DB_PAGE_LEAF) {
                // If this leaf is the dirty hot_leaf, search there (mmap may be stale)
                if (!a_snapshot_root && a_tree->hot_leaf && a_tree->hot_leaf->is_dirty
                    && a_tree->hot_leaf->header.page_id == l_page_id) {
                    int l_hl_idx;
                    if (s_leaf_find_entry(a_tree->hot_leaf, a_key, &l_hl_idx) == 0) {
                        uint8_t *l_hl_data = NULL;
                        dap_global_db_leaf_entry_t *l_hl_entry =
                            s_leaf_entry_at(a_tree->hot_leaf, l_hl_idx, &l_hl_data, NULL);
                        if (a_out_text_key) {
                            a_out_text_key->data = l_hl_entry->key_len > 0
                                ? (void *)l_hl_data : NULL;
                            a_out_text_key->len = l_hl_entry->key_len;
                        }
                        bool l_ov = (l_hl_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE)
                            && ((size_t)l_hl_entry->value_len + l_hl_entry->sign_len > MAX_INLINE_PAYLOAD);
                        if (l_ov) {
                            uint64_t l_ov_id = *(const uint64_t *)(l_hl_data + l_hl_entry->key_len);
                            size_t l_total = (size_t)l_hl_entry->value_len + (size_t)l_hl_entry->sign_len;
                            bool l_zc_ok = (l_total <= PAGE_DATA_SIZE);
                            if (!l_zc_ok) {
                                const dap_global_db_page_header_t *l_ov_hdr =
                                    (const dap_global_db_page_header_t *)(l_base + s_page_offset(l_ov_id));
                                l_zc_ok = !!(l_ov_hdr->flags & DAP_GLOBAL_DB_PAGE_OVERFLOW_CONTIGUOUS);
                            }
                            if (l_zc_ok) {
                                uint8_t *l_ov_ptr = l_base + s_page_offset(l_ov_id)
                                                    + sizeof(dap_global_db_page_header_t);
                                if (a_out_value) {
                                    a_out_value->data = l_hl_entry->value_len > 0 ? l_ov_ptr : NULL;
                                    a_out_value->len = l_hl_entry->value_len;
                                }
                                if (a_out_sign) {
                                    a_out_sign->data = l_hl_entry->sign_len > 0
                                        ? l_ov_ptr + l_hl_entry->value_len : NULL;
                                    a_out_sign->len = l_hl_entry->sign_len;
                                }
                            } else {
                                uint8_t *l_buf = s_tl_overflow_ensure(l_total);
                                if (!l_buf) {
                                    log_it(L_ERROR, "ZC-HL overflow TLS alloc failed: %zu", l_total);
                                    return -1;
                                }
                                if (s_overflow_read(a_tree, l_ov_id, l_buf, l_total, NULL) != 0) {
                                    log_it(L_ERROR, "ZC-HL overflow read failed: ov_id=%llu total=%zu",
                                           (unsigned long long)l_ov_id, l_total);
                                    return -1;
                                }
                                if (a_out_value) {
                                    a_out_value->data = l_hl_entry->value_len > 0 ? l_buf : NULL;
                                    a_out_value->len = l_hl_entry->value_len;
                                }
                                if (a_out_sign) {
                                    a_out_sign->data = l_hl_entry->sign_len > 0
                                        ? l_buf + l_hl_entry->value_len : NULL;
                                    a_out_sign->len = l_hl_entry->sign_len;
                                }
                            }
                        } else {
                            if (a_out_value) {
                                a_out_value->data = l_hl_entry->value_len > 0
                                    ? (void *)(l_hl_data + l_hl_entry->key_len) : NULL;
                                a_out_value->len = l_hl_entry->value_len;
                            }
                            if (a_out_sign) {
                                a_out_sign->data = l_hl_entry->sign_len > 0
                                    ? (void *)(l_hl_data + l_hl_entry->key_len + l_hl_entry->value_len)
                                    : NULL;
                                a_out_sign->len = l_hl_entry->sign_len;
                            }
                        }
                        if (a_out_flags)
                            *a_out_flags = l_ov ? l_hl_entry->flags & (uint8_t)~DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE
                                                : l_hl_entry->flags;
                        return 0;
                    }
                }
                // Binary search in leaf — direct pointer into mmap
                int l_count = l_hdr->entries_count;
                const uint16_t *l_offsets = (const uint16_t *)(l_data + LEAF_HEADER_SIZE);
                if (l_count > 0) {
                    const dap_global_db_leaf_entry_t *l_first =
                        (const dap_global_db_leaf_entry_t *)(l_data + l_offsets[0]);
                    const dap_global_db_leaf_entry_t *l_last =
                        (const dap_global_db_leaf_entry_t *)(l_data + l_offsets[l_count - 1]);
                    s_tl_leaf_cache_update(l_page_id, &l_first->driver_hash,
                                           &l_last->driver_hash);
                }
                int l_low = 0, l_high = l_count - 1;
                while (l_low <= l_high) {
                    int l_mid = (l_low + l_high) / 2;
                    const dap_global_db_leaf_entry_t *l_entry =
                        (const dap_global_db_leaf_entry_t *)(l_data + l_offsets[l_mid]);
                    int l_cmp = s_key_cmp(a_key, &l_entry->driver_hash);
                    if (l_cmp == 0) {
                        const uint8_t *l_edata = (const uint8_t *)l_entry
                                                  + sizeof(dap_global_db_leaf_entry_t);
                        if (a_out_text_key) {
                            a_out_text_key->data = l_entry->key_len > 0
                                ? (void *)l_edata : NULL;
                            a_out_text_key->len = l_entry->key_len;
                        }
                        bool l_ov = (l_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE)
                            && ((size_t)l_entry->value_len + l_entry->sign_len > MAX_INLINE_PAYLOAD);
                        if (l_ov) {
                            uint64_t l_ov_id = *(const uint64_t *)(l_edata + l_entry->key_len);
                            size_t l_total = (size_t)l_entry->value_len + (size_t)l_entry->sign_len;
                            bool l_zc_ok = (l_total <= PAGE_DATA_SIZE);
                            if (!l_zc_ok) {
                                const dap_global_db_page_header_t *l_ov_hdr =
                                    (const dap_global_db_page_header_t *)(l_base + s_page_offset(l_ov_id));
                                l_zc_ok = !!(l_ov_hdr->flags & DAP_GLOBAL_DB_PAGE_OVERFLOW_CONTIGUOUS);
                            }
                            if (l_zc_ok) {
                                uint8_t *l_ov_ptr = l_base + s_page_offset(l_ov_id)
                                                    + sizeof(dap_global_db_page_header_t);
                                if (a_out_value) {
                                    a_out_value->data = l_entry->value_len > 0 ? l_ov_ptr : NULL;
                                    a_out_value->len = l_entry->value_len;
                                }
                                if (a_out_sign) {
                                    a_out_sign->data = l_entry->sign_len > 0
                                        ? l_ov_ptr + l_entry->value_len : NULL;
                                    a_out_sign->len = l_entry->sign_len;
                                }
                            } else {
                                uint8_t *l_buf = s_tl_overflow_ensure(l_total);
                                if (!l_buf) {
                                    log_it(L_ERROR, "ZC overflow TLS alloc failed: %zu", l_total);
                                    return -1;
                                }
                                if (s_overflow_read(a_tree, l_ov_id, l_buf, l_total, NULL) != 0) {
                                    log_it(L_ERROR, "ZC overflow read failed: ov_id=%llu total=%zu",
                                           (unsigned long long)l_ov_id, l_total);
                                    return -1;
                                }
                                if (a_out_value) {
                                    a_out_value->data = l_entry->value_len > 0 ? l_buf : NULL;
                                    a_out_value->len = l_entry->value_len;
                                }
                                if (a_out_sign) {
                                    a_out_sign->data = l_entry->sign_len > 0
                                        ? l_buf + l_entry->value_len : NULL;
                                    a_out_sign->len = l_entry->sign_len;
                                }
                            }
                        } else {
                            if (a_out_value) {
                                a_out_value->data = l_entry->value_len > 0
                                    ? (void *)(l_edata + l_entry->key_len) : NULL;
                                a_out_value->len = l_entry->value_len;
                            }
                            if (a_out_sign) {
                                a_out_sign->data = l_entry->sign_len > 0
                                    ? (void *)(l_edata + l_entry->key_len + l_entry->value_len)
                                    : NULL;
                                a_out_sign->len = l_entry->sign_len;
                            }
                        }
                        if (a_out_flags)
                            *a_out_flags = l_ov ? l_entry->flags & (uint8_t)~DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE
                                                : l_entry->flags;
                        return 0;
                    }
                    if (l_cmp < 0)
                        l_high = l_mid - 1;
                    else
                        l_low = l_mid + 1;
                }
                debug_if(s_debug_more, L_WARNING, "ZC leaf miss: page=%llu entries=%d root=%llu depth=%d",
                         (unsigned long long)l_page_id, l_hdr->entries_count,
                         (unsigned long long)l_root, l_zc_depth);
                return 1;  // Not found
            }

            // Branch page — binary search for child, then follow pointer
            const dap_global_db_branch_entry_t *l_entries =
                (const dap_global_db_branch_entry_t *)(l_data + sizeof(uint64_t));
            int l_count = l_hdr->entries_count;
            int l_low = 0, l_high = l_count - 1;
            int l_idx = l_count;  // Default: rightmost child
            while (l_low <= l_high) {
                int l_mid = (l_low + l_high) / 2;
                int l_cmp = s_key_cmp(a_key, &l_entries[l_mid].driver_hash);
                if (l_cmp == 0) {
                    l_idx = l_mid + 1;
                    break;
                }
                if (l_cmp < 0) {
                    l_idx = l_mid;
                    l_high = l_mid - 1;
                } else {
                    l_low = l_mid + 1;
                }
            }
            // Get child page ID and prefetch for next iteration
            if (l_idx == 0)
                l_page_id = *(const uint64_t *)l_data;
            else
                l_page_id = l_entries[l_idx - 1].child_page;
            __builtin_prefetch(l_base + s_page_offset(l_page_id), 0, 3);
            l_zc_depth++;
        }
    }

    // Fallback: no mmap available — use allocating get
    log_it(L_ERROR, "get_ref: no mmap fallback, root=%llu",
           (unsigned long long)l_root);
    return -1;
}

int dap_global_db_get_ref(dap_global_db_t *a_tree,
                                const dap_global_db_key_t *a_key,
                                dap_global_db_ref_t *a_out_text_key,
                                dap_global_db_ref_t *a_out_value,
                                dap_global_db_ref_t *a_out_sign,
                                uint8_t *a_out_flags)
{
    dap_return_val_if_fail(a_tree && a_key, -1);
    uint64_t l_snap_root;
    if (s_tl_read_txn_root(a_tree, &l_snap_root)) {
        if (s_tl_read_txn.cached_leaf_id
            && s_key_cmp(a_key, &s_tl_read_txn.cached_leaf_min) >= 0
            && s_key_cmp(a_key, &s_tl_read_txn.cached_leaf_max) <= 0) {
            int l_ret = s_btree_get_ref_impl(a_tree, a_key, a_out_text_key,
                                              a_out_value, a_out_sign, a_out_flags,
                                              s_tl_read_txn.cached_leaf_id);
            if (l_ret != 1)
                return l_ret;
        }
        return s_btree_get_ref_impl(a_tree, a_key, a_out_text_key, a_out_value,
                                     a_out_sign, a_out_flags, l_snap_root);
    }
    uint64_t l_snap_txn;
    int l_slot = s_snapshot_acquire(a_tree, &l_snap_root, &l_snap_txn, NULL);
    if (l_slot >= 0) {
        int l_ret = s_btree_get_ref_impl(a_tree, a_key, a_out_text_key, a_out_value,
                                          a_out_sign, a_out_flags, l_snap_root);
        s_snapshot_release(a_tree, l_slot);
        return l_ret;
    }
    pthread_rwlock_rdlock(&a_tree->lock);
    int l_ret = s_btree_get_ref_impl(a_tree, a_key, a_out_text_key, a_out_value,
                                      a_out_sign, a_out_flags, 0);
    pthread_rwlock_unlock(&a_tree->lock);
    return l_ret;
}

static bool s_btree_exists_impl(dap_global_db_t *a_tree,
                                const dap_global_db_key_t *a_key,
                                uint64_t a_snapshot_root)
{
    uint64_t l_root = a_snapshot_root ? a_snapshot_root : a_tree->header.root_page;
    if (l_root == 0)
        return false;

    // ---- Hot leaf read-only search (no flush, no mutation) ----
    // Snapshot readers skip hot_leaf.
    if (!a_snapshot_root && a_tree->hot_leaf && a_tree->hot_leaf->is_dirty) {
        int l_hl_idx;
        if (s_leaf_find_entry(a_tree->hot_leaf, a_key, &l_hl_idx) == 0)
            return true;
        // Not found in hot_leaf — fall through to mmap search
    }

    // Zero-copy fast path
    dap_global_db_page_t l_page_buf;
    if (s_page_read_ref(a_tree, l_root, &l_page_buf)) {
        while (!(l_page_buf.header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
            bool l_found;
            int l_index = s_search_in_page(&l_page_buf, a_key, &l_found);
            uint64_t l_child_id = s_branch_get_child(&l_page_buf, l_index);
            if (!s_page_read_ref(a_tree, l_child_id, &l_page_buf))
                return false;
        }
        s_page_refresh_from_hot_leaf(a_tree, &l_page_buf);
        int l_index;
        return s_leaf_find_entry(&l_page_buf, a_key, &l_index) == 0;
    }

    // Fallback
    dap_global_db_page_t *l_page = s_page_read(a_tree, l_root, NULL);
    if (!l_page)
        return false;
    
    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        bool l_found;
        int l_index = s_search_in_page(l_page, a_key, &l_found);
        uint64_t l_child_id = s_branch_get_child(l_page, l_index);
        s_page_free(l_page);
        
        l_page = s_page_read(a_tree, l_child_id, NULL);
        if (!l_page)
            return false;
    }
    
    int l_index;
    bool l_exists = (s_leaf_find_entry(l_page, a_key, &l_index) == 0);
    
    s_page_free(l_page);
    return l_exists;
}

bool dap_global_db_exists(dap_global_db_t *a_tree, const dap_global_db_key_t *a_key)
{
    dap_return_val_if_fail(a_tree && a_key, false);
    uint64_t l_snap_root, l_snap_txn;
    int l_slot = s_snapshot_acquire(a_tree, &l_snap_root, &l_snap_txn, NULL);
    if (l_slot >= 0) {
        bool l_ret = s_btree_exists_impl(a_tree, a_key, l_snap_root);
        s_snapshot_release(a_tree, l_slot);
        return l_ret;
    }
    pthread_rwlock_rdlock(&a_tree->lock);
    bool l_ret = s_btree_exists_impl(a_tree, a_key, 0);
    pthread_rwlock_unlock(&a_tree->lock);
    return l_ret;
}

uint64_t dap_global_db_count(dap_global_db_t *a_tree)
{
    dap_return_val_if_fail(a_tree, 0);
    return atomic_load_explicit(&a_tree->mvcc_count, memory_order_acquire);
}

int dap_global_db_clear(dap_global_db_t *a_tree)
{
    dap_return_val_if_fail(a_tree && !a_tree->read_only, -1);
    pthread_rwlock_wrlock(&a_tree->lock);

    // Discard hot leaf without COW (tree is being destroyed)
    if (a_tree->hot_leaf) {
        s_page_free(a_tree->hot_leaf);
        a_tree->hot_leaf = NULL;
    }
    a_tree->hot_path_depth = 0;

    if (a_tree->root) {
        s_page_free(a_tree->root);
        a_tree->root = NULL;
    }

    // Discard all deferred free batches — tree pages are being truncated,
    // old COW pages must NOT be reclaimed into the free list.
    for (size_t i = 0; i < a_tree->deferred_batch_count; i++)
        DAP_DELETE(a_tree->deferred_batches[i].page_ids);
    a_tree->deferred_batch_count = 0;

    // Reset header
    a_tree->header.root_page = 0;
    a_tree->header.total_pages = 0;
    a_tree->header.items_count = 0;
    a_tree->header.tree_height = 0;
    a_tree->header.free_list_head = 0;

    // Truncate file and reopen mmap
    if (a_tree->mmap) {
        dap_mmap_close(a_tree->mmap);
        a_tree->mmap = NULL;
    }
    if (ftruncate(a_tree->fd, BTREE_DATA_OFFSET) != 0) {
        log_it(L_WARNING, "Failed to truncate file: %s", strerror(errno));
    }
    size_t l_initial = 1024 * 1024;
    a_tree->mmap = dap_mmap_open(a_tree->filepath,
        DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, l_initial);
    if (a_tree->mmap)
        dap_mmap_advise(a_tree->mmap, DAP_MMAP_ADVISE_RANDOM);

    // Reset arena
    if (a_tree->arena)
        dap_arena_reset(a_tree->arena);

    int l_ret = s_header_write(a_tree);
    // Publish clean MVCC state (seqlock-protected)
    atomic_fetch_add_explicit(&a_tree->mvcc_seq, 1, memory_order_release);
    atomic_store_explicit(&a_tree->mvcc_count, 0, memory_order_relaxed);
    atomic_store_explicit(&a_tree->mvcc_height, 0, memory_order_relaxed);
    atomic_store_explicit(&a_tree->mvcc_root, 0, memory_order_relaxed);
    atomic_store_explicit(&a_tree->mvcc_txn,
        atomic_load(&a_tree->mvcc_txn) + 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&a_tree->mvcc_seq, 1, memory_order_release);

    pthread_rwlock_unlock(&a_tree->lock);
    return l_ret;
}

// ============================================================================
// Snapshot-mode cursor helpers (stack-based traversal, no sibling links)
// ============================================================================

/**
 * @brief Navigate from a_root to the leftmost leaf, recording the branch path.
 *
 * The path records (page_id, child_index) at each branch level, so that
 * cursor NEXT/PREV can pop the stack to find sibling subtrees.
 */
static dap_global_db_page_t *s_find_leftmost_leaf_path(
    dap_global_db_t *a_tree, uint64_t a_root,
    struct dap_btree_path_entry *a_path, int *a_path_depth)
{
    *a_path_depth = 0;
    if (a_root == 0)
        return NULL;

    dap_global_db_page_t *l_page = s_page_read(a_tree, a_root, NULL);
    if (!l_page)
        return NULL;

    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        if (*a_path_depth < DAP_GLOBAL_DB_PATH_MAX) {
            a_path[*a_path_depth].page_id = l_page->header.page_id;
            a_path[*a_path_depth].child_index = 0;
            (*a_path_depth)++;
        }
        uint64_t l_child_id = s_branch_get_child(l_page, 0);
        s_page_free(l_page);
        l_page = s_page_read(a_tree, l_child_id, NULL);
        if (!l_page)
            return NULL;
    }
    return l_page;
}

/**
 * @brief Navigate from a_root to the rightmost leaf, recording the branch path.
 */
static dap_global_db_page_t *s_find_rightmost_leaf_path(
    dap_global_db_t *a_tree, uint64_t a_root,
    struct dap_btree_path_entry *a_path, int *a_path_depth)
{
    *a_path_depth = 0;
    if (a_root == 0)
        return NULL;

    dap_global_db_page_t *l_page = s_page_read(a_tree, a_root, NULL);
    if (!l_page)
        return NULL;

    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        int l_count = l_page->header.entries_count;
        if (*a_path_depth < DAP_GLOBAL_DB_PATH_MAX) {
            a_path[*a_path_depth].page_id = l_page->header.page_id;
            a_path[*a_path_depth].child_index = l_count;
            (*a_path_depth)++;
        }
        uint64_t l_child_id = s_branch_get_child(l_page, l_count);
        s_page_free(l_page);
        l_page = s_page_read(a_tree, l_child_id, NULL);
        if (!l_page)
            return NULL;
    }
    return l_page;
}

/**
 * @brief Navigate from a_root to the leaf containing a_key, recording path.
 */
static dap_global_db_page_t *s_find_leaf_for_key_path(
    dap_global_db_t *a_tree, const dap_global_db_key_t *a_key,
    uint64_t a_root,
    struct dap_btree_path_entry *a_path, int *a_path_depth)
{
    *a_path_depth = 0;
    if (a_root == 0)
        return NULL;

    dap_global_db_page_t *l_page = s_page_read(a_tree, a_root, NULL);
    if (!l_page)
        return NULL;

    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        bool l_found;
        int l_index = s_search_in_page(l_page, a_key, &l_found);
        if (*a_path_depth < DAP_GLOBAL_DB_PATH_MAX) {
            a_path[*a_path_depth].page_id = l_page->header.page_id;
            a_path[*a_path_depth].child_index = l_index;
            (*a_path_depth)++;
        }
        uint64_t l_child_id = s_branch_get_child(l_page, l_index);
        s_page_free(l_page);
        l_page = s_page_read(a_tree, l_child_id, NULL);
        if (!l_page)
            return NULL;
    }
    return l_page;
}

/**
 * @brief Find the next leaf by popping the path stack and descending.
 *
 * When the cursor reaches the end of a leaf in snapshot mode, it can't
 * follow right_sibling (those may be stale due to in-place COW updates).
 * Instead, pop the stack to find the next child in the parent branch,
 * then descend to the leftmost leaf of that subtree.
 *
 * @return New leaf page (caller owns), or NULL if at end of tree.
 *         Updates a_path and a_path_depth in place.
 */
static dap_global_db_page_t *s_cursor_snapshot_next_leaf(
    dap_global_db_t *a_tree,
    struct dap_btree_path_entry *a_path, int *a_path_depth)
{
    while (*a_path_depth > 0) {
        (*a_path_depth)--;
        struct dap_btree_path_entry *l_entry = &a_path[*a_path_depth];

        dap_global_db_page_t *l_parent = s_page_read(a_tree, l_entry->page_id, NULL);
        if (!l_parent)
            return NULL;

        int l_next_child = l_entry->child_index + 1;
        if (l_next_child <= l_parent->header.entries_count) {
            // There IS a next child in this branch — descend to its leftmost leaf
            l_entry->child_index = l_next_child;
            (*a_path_depth)++;

            uint64_t l_child_id = s_branch_get_child(l_parent, l_next_child);
            s_page_free(l_parent);

            dap_global_db_page_t *l_page = s_page_read(a_tree, l_child_id, NULL);
            if (!l_page)
                return NULL;

            while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
                if (*a_path_depth < DAP_GLOBAL_DB_PATH_MAX) {
                    a_path[*a_path_depth].page_id = l_page->header.page_id;
                    a_path[*a_path_depth].child_index = 0;
                    (*a_path_depth)++;
                }
                l_child_id = s_branch_get_child(l_page, 0);
                s_page_free(l_page);
                l_page = s_page_read(a_tree, l_child_id, NULL);
                if (!l_page)
                    return NULL;
            }
            return l_page;
        }
        s_page_free(l_parent);
        // No more children at this level — pop one more
    }
    return NULL;  // Exhausted all ancestors — end of tree
}

/**
 * @brief Find the previous leaf by popping the path stack and descending.
 */
static dap_global_db_page_t *s_cursor_snapshot_prev_leaf(
    dap_global_db_t *a_tree,
    struct dap_btree_path_entry *a_path, int *a_path_depth)
{
    while (*a_path_depth > 0) {
        (*a_path_depth)--;
        struct dap_btree_path_entry *l_entry = &a_path[*a_path_depth];

        dap_global_db_page_t *l_parent = s_page_read(a_tree, l_entry->page_id, NULL);
        if (!l_parent)
            return NULL;

        int l_prev_child = l_entry->child_index - 1;
        if (l_prev_child >= 0) {
            l_entry->child_index = l_prev_child;
            (*a_path_depth)++;

            uint64_t l_child_id = s_branch_get_child(l_parent, l_prev_child);
            s_page_free(l_parent);

            dap_global_db_page_t *l_page = s_page_read(a_tree, l_child_id, NULL);
            if (!l_page)
                return NULL;

            while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
                int l_count = l_page->header.entries_count;
                if (*a_path_depth < DAP_GLOBAL_DB_PATH_MAX) {
                    a_path[*a_path_depth].page_id = l_page->header.page_id;
                    a_path[*a_path_depth].child_index = l_count;
                    (*a_path_depth)++;
                }
                l_child_id = s_branch_get_child(l_page, l_count);
                s_page_free(l_page);
                l_page = s_page_read(a_tree, l_child_id, NULL);
                if (!l_page)
                    return NULL;
            }
            return l_page;
        }
        s_page_free(l_parent);
    }
    return NULL;  // Beginning of tree
}

// ============================================================================
// Cursor Operations
// ============================================================================

dap_global_db_cursor_t *dap_global_db_cursor_create(dap_global_db_t *a_tree)
{
    dap_return_val_if_fail(a_tree, NULL);
    
    dap_global_db_cursor_t *l_cursor = DAP_NEW_Z(dap_global_db_cursor_t);
    if (!l_cursor)
        return NULL;
    
    l_cursor->tree = a_tree;
    l_cursor->valid = false;
    l_cursor->at_end = false;
    l_cursor->path_depth = 0;

    // Acquire MVCC snapshot for cursor lifetime
    uint64_t l_snap_root, l_snap_txn, l_snap_count;
    int l_slot = s_snapshot_acquire(a_tree, &l_snap_root, &l_snap_txn, &l_snap_count);
    if (l_slot >= 0) {
        l_cursor->snapshot_slot = l_slot;
        l_cursor->snapshot_root = l_snap_root;
        l_cursor->snapshot_txn = l_snap_txn;
        l_cursor->snapshot_count = l_snap_count;
    } else {
        l_cursor->snapshot_slot = -1;
        l_cursor->snapshot_root = 0;
        l_cursor->snapshot_txn = 0;
        l_cursor->snapshot_count = 0;
    }
    
    return l_cursor;
}

void dap_global_db_cursor_close(dap_global_db_cursor_t *a_cursor)
{
    if (!a_cursor)
        return;
    
    if (a_cursor->current_page)
        s_page_free(a_cursor->current_page);

    if (a_cursor->snapshot_slot >= 0)
        s_snapshot_release(a_cursor->tree, a_cursor->snapshot_slot);
    
    DAP_DELETE(a_cursor);
}

/**
 * @brief Snapshot-mode cursor move: navigates via path stack, no sibling links.
 *
 * In snapshot mode, sibling links (left_sibling/right_sibling) on leaf pages
 * may be stale because writers update neighbors in-place during COW. This
 * function uses a path stack (recorded during initial navigation) to find
 * adjacent leaves by popping up to the parent branch and descending into
 * the next/prev child subtree.
 */
static int s_btree_cursor_move_snapshot_unlocked(dap_global_db_cursor_t *a_cursor,
                                        dap_global_db_cursor_op_t a_op,
                                        const dap_global_db_key_t *a_key)
{
    dap_global_db_t *l_tree = a_cursor->tree;
    uint64_t l_root = a_cursor->snapshot_root;

    // NEXT and PREV: operate on existing position
    if (a_op == DAP_GLOBAL_DB_NEXT || a_op == DAP_GLOBAL_DB_PREV) {
        if (!a_cursor->valid || !a_cursor->current_page)
            return -1;

        if (a_op == DAP_GLOBAL_DB_NEXT) {
            if (a_cursor->current_index + 1 < a_cursor->current_page->header.entries_count) {
                a_cursor->current_index++;
                return 0;
            }
            // End of current leaf — find next leaf via path stack
            s_page_free(a_cursor->current_page);
            a_cursor->current_page = s_cursor_snapshot_next_leaf(l_tree,
                a_cursor->path, &a_cursor->path_depth);
            if (!a_cursor->current_page || a_cursor->current_page->header.entries_count == 0) {
                if (a_cursor->current_page) {
                    s_page_free(a_cursor->current_page);
                    a_cursor->current_page = NULL;
                }
                a_cursor->valid = false;
                a_cursor->at_end = true;
                return 1;
            }
            a_cursor->current_index = 0;
            return 0;
        } else {  // PREV
            if (a_cursor->current_index > 0) {
                a_cursor->current_index--;
                return 0;
            }
            s_page_free(a_cursor->current_page);
            a_cursor->current_page = s_cursor_snapshot_prev_leaf(l_tree,
                a_cursor->path, &a_cursor->path_depth);
            if (!a_cursor->current_page || a_cursor->current_page->header.entries_count == 0) {
                if (a_cursor->current_page) {
                    s_page_free(a_cursor->current_page);
                    a_cursor->current_page = NULL;
                }
                a_cursor->valid = false;
                a_cursor->at_end = true;
                return 1;
            }
            a_cursor->current_index = a_cursor->current_page->header.entries_count - 1;
            return 0;
        }
    }

    // For other ops, free current page and reset
    if (a_cursor->current_page) {
        s_page_free(a_cursor->current_page);
        a_cursor->current_page = NULL;
    }
    a_cursor->valid = false;
    a_cursor->at_end = false;
    a_cursor->path_depth = 0;

    switch (a_op) {
    case DAP_GLOBAL_DB_FIRST:
        a_cursor->current_page = s_find_leftmost_leaf_path(l_tree, l_root,
            a_cursor->path, &a_cursor->path_depth);
        if (a_cursor->current_page && a_cursor->current_page->header.entries_count > 0) {
            a_cursor->current_index = 0;
            a_cursor->valid = true;
        } else {
            a_cursor->at_end = true;
        }
        break;

    case DAP_GLOBAL_DB_LAST:
        a_cursor->current_page = s_find_rightmost_leaf_path(l_tree, l_root,
            a_cursor->path, &a_cursor->path_depth);
        if (a_cursor->current_page && a_cursor->current_page->header.entries_count > 0) {
            a_cursor->current_index = a_cursor->current_page->header.entries_count - 1;
            a_cursor->valid = true;
        } else {
            a_cursor->at_end = true;
        }
        break;

    case DAP_GLOBAL_DB_NEXT:
    case DAP_GLOBAL_DB_PREV:
        return -1;

    case DAP_GLOBAL_DB_SET:
        if (!a_key) return -1;
        a_cursor->current_page = s_find_leaf_for_key_path(l_tree, a_key, l_root,
            a_cursor->path, &a_cursor->path_depth);
        if (a_cursor->current_page) {
            int l_index;
            if (s_leaf_find_entry(a_cursor->current_page, a_key, &l_index) == 0) {
                a_cursor->current_index = l_index;
                a_cursor->valid = true;
            } else {
                return 1;
            }
        }
        break;

    case DAP_GLOBAL_DB_SET_RANGE:
        if (!a_key) return -1;
        a_cursor->current_page = s_find_leaf_for_key_path(l_tree, a_key, l_root,
            a_cursor->path, &a_cursor->path_depth);
        if (a_cursor->current_page) {
            int l_index;
            s_leaf_find_entry(a_cursor->current_page, a_key, &l_index);
            if (l_index < a_cursor->current_page->header.entries_count) {
                a_cursor->current_index = l_index;
                a_cursor->valid = true;
            } else {
                // All entries < key — next leaf via stack
                s_page_free(a_cursor->current_page);
                a_cursor->current_page = s_cursor_snapshot_next_leaf(l_tree,
                    a_cursor->path, &a_cursor->path_depth);
                if (a_cursor->current_page && a_cursor->current_page->header.entries_count > 0) {
                    a_cursor->current_index = 0;
                    a_cursor->valid = true;
                } else {
                    if (a_cursor->current_page) {
                        s_page_free(a_cursor->current_page);
                        a_cursor->current_page = NULL;
                    }
                    a_cursor->at_end = true;
                }
            }
        }
        break;

    case DAP_GLOBAL_DB_SET_UPPERBOUND:
        if (!a_key) return -1;
        a_cursor->current_page = s_find_leaf_for_key_path(l_tree, a_key, l_root,
            a_cursor->path, &a_cursor->path_depth);
        if (a_cursor->current_page) {
            int l_index;
            if (s_leaf_find_entry(a_cursor->current_page, a_key, &l_index) == 0)
                l_index++;  // Skip exact match
            if (l_index < a_cursor->current_page->header.entries_count) {
                a_cursor->current_index = l_index;
                a_cursor->valid = true;
            } else {
                s_page_free(a_cursor->current_page);
                a_cursor->current_page = s_cursor_snapshot_next_leaf(l_tree,
                    a_cursor->path, &a_cursor->path_depth);
                if (a_cursor->current_page && a_cursor->current_page->header.entries_count > 0) {
                    a_cursor->current_index = 0;
                    a_cursor->valid = true;
                } else {
                    if (a_cursor->current_page) {
                        s_page_free(a_cursor->current_page);
                        a_cursor->current_page = NULL;
                    }
                    a_cursor->at_end = true;
                }
            }
        }
        break;
    }

    if (!a_cursor->valid && !a_cursor->at_end)
        return 1;
    return 0;
}

static int s_btree_cursor_move_snapshot(dap_global_db_cursor_t *a_cursor,
                                        dap_global_db_cursor_op_t a_op,
                                        const dap_global_db_key_t *a_key)
{
    pthread_rwlock_rdlock(&a_cursor->tree->lock);
    int l_ret = s_btree_cursor_move_snapshot_unlocked(a_cursor, a_op, a_key);
    pthread_rwlock_unlock(&a_cursor->tree->lock);
    return l_ret;
}

/**
 * @brief Find leftmost leaf page
 */
static dap_global_db_page_t *s_find_leftmost_leaf(dap_global_db_t *a_tree)
{
    if (a_tree->header.root_page == 0)
        return NULL;
    
    dap_global_db_page_t *l_page = s_page_read(a_tree, a_tree->header.root_page, NULL);
    if (!l_page)
        return NULL;
    
    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        uint64_t l_child_id = s_branch_get_child(l_page, 0);
        s_page_free(l_page);
        
        l_page = s_page_read(a_tree, l_child_id, NULL);
        if (!l_page)
            return NULL;
    }
    
    // Refresh from hot_leaf if this leaf is stale (read-only, no mutation)
    s_page_refresh_from_hot_leaf(a_tree, l_page);
    return l_page;
}

/**
 * @brief Find rightmost leaf page
 */
static dap_global_db_page_t *s_find_rightmost_leaf(dap_global_db_t *a_tree)
{
    if (a_tree->header.root_page == 0)
        return NULL;
    
    dap_global_db_page_t *l_page = s_page_read(a_tree, a_tree->header.root_page, NULL);
    if (!l_page)
        return NULL;
    
    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        int l_count = l_page->header.entries_count;
        uint64_t l_child_id = s_branch_get_child(l_page, l_count);
        s_page_free(l_page);
        
        l_page = s_page_read(a_tree, l_child_id, NULL);
        if (!l_page)
            return NULL;
    }
    
    // Refresh from hot_leaf if this leaf is stale (read-only, no mutation)
    s_page_refresh_from_hot_leaf(a_tree, l_page);
    return l_page;
}

/**
 * @brief Find leaf page containing key (or insertion point)
 */
static dap_global_db_page_t *s_find_leaf_for_key(dap_global_db_t *a_tree, const dap_global_db_key_t *a_key)
{
    if (a_tree->header.root_page == 0)
        return NULL;
    
    dap_global_db_page_t *l_page = s_page_read(a_tree, a_tree->header.root_page, NULL);
    if (!l_page)
        return NULL;
    
    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        bool l_found;
        int l_index = s_search_in_page(l_page, a_key, &l_found);
        uint64_t l_child_id = s_branch_get_child(l_page, l_index);
        s_page_free(l_page);
        
        l_page = s_page_read(a_tree, l_child_id, NULL);
        if (!l_page)
            return NULL;
    }
    
    // Refresh from hot_leaf if this leaf is stale (read-only, no mutation)
    s_page_refresh_from_hot_leaf(a_tree, l_page);
    return l_page;
}

static int s_btree_cursor_move_impl(dap_global_db_cursor_t *a_cursor,
                              dap_global_db_cursor_op_t a_op,
                              const dap_global_db_key_t *a_key)
{
    dap_global_db_t *l_tree = a_cursor->tree;

    // NEXT and PREV operate on the existing position — don't free current_page
    if (a_op == DAP_GLOBAL_DB_NEXT || a_op == DAP_GLOBAL_DB_PREV) {
        if (!a_cursor->valid || !a_cursor->current_page)
            return -1;
        
        if (a_op == DAP_GLOBAL_DB_NEXT) {
            // Try next entry in current page
            if (a_cursor->current_index + 1 < a_cursor->current_page->header.entries_count) {
                a_cursor->current_index++;
                return 0;
            }
            // Move to right sibling leaf
            uint64_t l_next_id = a_cursor->current_page->header.right_sibling;
            if (l_next_id == 0) {
                s_page_free(a_cursor->current_page);
                a_cursor->current_page = NULL;
                a_cursor->valid = false;
                a_cursor->at_end = true;
                return 1;  // End of tree
            }
            // Reuse existing page buffer: memcpy from mmap into same allocation
            if (l_tree->mmap) {
                uint64_t l_offset = s_page_offset(l_next_id);
                if (l_offset + DAP_GLOBAL_DB_PAGE_SIZE <= dap_mmap_get_size(l_tree->mmap)) {
                    uint8_t *l_src = (uint8_t *)dap_mmap_get_ptr(l_tree->mmap) + l_offset;
                    memcpy(&a_cursor->current_page->header, l_src, sizeof(dap_global_db_page_header_t));
                    memcpy(a_cursor->current_page->data, l_src + sizeof(dap_global_db_page_header_t), PAGE_DATA_SIZE);
                    a_cursor->current_page->is_dirty = false;
                    a_cursor->current_page->is_mmap_ref = false;
                    // Refresh from hot_leaf if this page is stale
                    s_page_refresh_from_hot_leaf(l_tree, a_cursor->current_page);
                    if (a_cursor->current_page->header.entries_count == 0) {
                        a_cursor->valid = false;
                        a_cursor->at_end = true;
                        return 1;
                    }
                    a_cursor->current_index = 0;
                    return 0;
                }
            }
            // Fallback: free + alloc
            s_page_free(a_cursor->current_page);
            a_cursor->current_page = s_page_read(l_tree, l_next_id, NULL);
            if (!a_cursor->current_page)
                return -1;
            // Refresh from hot_leaf if this page is stale
            s_page_refresh_from_hot_leaf(l_tree, a_cursor->current_page);
            if (a_cursor->current_page->header.entries_count == 0) {
                a_cursor->valid = false;
                a_cursor->at_end = true;
                return 1;
            }
            a_cursor->current_index = 0;
            return 0;
        } else {  // PREV
            // Try previous entry in current page
            if (a_cursor->current_index > 0) {
                a_cursor->current_index--;
                return 0;
            }
            // Move to left sibling leaf
            uint64_t l_prev_id = a_cursor->current_page->header.left_sibling;
            if (l_prev_id == 0) {
                s_page_free(a_cursor->current_page);
                a_cursor->current_page = NULL;
                a_cursor->valid = false;
                a_cursor->at_end = true;
                return 1;  // Beginning of tree
            }
            // Reuse existing page buffer for mmap
            if (l_tree->mmap) {
                uint64_t l_offset = s_page_offset(l_prev_id);
                if (l_offset + DAP_GLOBAL_DB_PAGE_SIZE <= dap_mmap_get_size(l_tree->mmap)) {
                    uint8_t *l_src = (uint8_t *)dap_mmap_get_ptr(l_tree->mmap) + l_offset;
                    memcpy(&a_cursor->current_page->header, l_src, sizeof(dap_global_db_page_header_t));
                    memcpy(a_cursor->current_page->data, l_src + sizeof(dap_global_db_page_header_t), PAGE_DATA_SIZE);
                    a_cursor->current_page->is_dirty = false;
                    a_cursor->current_page->is_mmap_ref = false;
                    // Refresh from hot_leaf if this page is stale
                    s_page_refresh_from_hot_leaf(l_tree, a_cursor->current_page);
                    if (a_cursor->current_page->header.entries_count == 0) {
                        a_cursor->valid = false;
                        a_cursor->at_end = true;
                        return 1;
                    }
                    a_cursor->current_index = a_cursor->current_page->header.entries_count - 1;
                    return 0;
                }
            }
            // Fallback: free + alloc
            s_page_free(a_cursor->current_page);
            a_cursor->current_page = s_page_read(l_tree, l_prev_id, NULL);
            if (!a_cursor->current_page)
                return -1;
            // Refresh from hot_leaf if this page is stale
            s_page_refresh_from_hot_leaf(l_tree, a_cursor->current_page);
            if (a_cursor->current_page->header.entries_count == 0) {
                a_cursor->valid = false;
                a_cursor->at_end = true;
                return 1;
            }
            a_cursor->current_index = a_cursor->current_page->header.entries_count - 1;
            return 0;
        }
    }
    
    // For other ops, free current page and reset state
    if (a_cursor->current_page) {
        s_page_free(a_cursor->current_page);
        a_cursor->current_page = NULL;
    }
    
    a_cursor->valid = false;
    a_cursor->at_end = false;
    
    switch (a_op) {
    case DAP_GLOBAL_DB_FIRST:
        a_cursor->current_page = s_find_leftmost_leaf(l_tree);
        if (a_cursor->current_page && a_cursor->current_page->header.entries_count > 0) {
            a_cursor->current_index = 0;
            a_cursor->valid = true;
        } else {
            a_cursor->at_end = true;
        }
        break;
        
    case DAP_GLOBAL_DB_LAST:
        a_cursor->current_page = s_find_rightmost_leaf(l_tree);
        if (a_cursor->current_page && a_cursor->current_page->header.entries_count > 0) {
            a_cursor->current_index = a_cursor->current_page->header.entries_count - 1;
            a_cursor->valid = true;
        } else {
            a_cursor->at_end = true;
        }
        break;
        
    case DAP_GLOBAL_DB_NEXT:
    case DAP_GLOBAL_DB_PREV:
        // Already handled above — unreachable
        return -1;
        
    case DAP_GLOBAL_DB_SET:
        if (!a_key)
            return -1;
        a_cursor->current_page = s_find_leaf_for_key(l_tree, a_key);
        if (a_cursor->current_page) {
            int l_index;
            if (s_leaf_find_entry(a_cursor->current_page, a_key, &l_index) == 0) {
                a_cursor->current_index = l_index;
                a_cursor->valid = true;
            } else {
                return 1;  // Not found
            }
        }
        break;
        
    case DAP_GLOBAL_DB_SET_RANGE:
        if (!a_key)
            return -1;
        a_cursor->current_page = s_find_leaf_for_key(l_tree, a_key);
        if (a_cursor->current_page) {
            int l_index;
            s_leaf_find_entry(a_cursor->current_page, a_key, &l_index);
            if (l_index < a_cursor->current_page->header.entries_count) {
                a_cursor->current_index = l_index;
                a_cursor->valid = true;
            } else {
                // Move to next page
                if (a_cursor->current_page->header.right_sibling) {
                    uint64_t l_next_id = a_cursor->current_page->header.right_sibling;
                    s_page_free(a_cursor->current_page);
                    a_cursor->current_page = s_page_read(l_tree, l_next_id, NULL);
                    if (a_cursor->current_page) {
                        s_page_refresh_from_hot_leaf(l_tree, a_cursor->current_page);
                        if (a_cursor->current_page->header.entries_count > 0) {
                            a_cursor->current_index = 0;
                            a_cursor->valid = true;
                        } else {
                            a_cursor->at_end = true;
                        }
                    } else {
                        a_cursor->at_end = true;
                    }
                } else {
                    a_cursor->at_end = true;
                }
            }
        }
        break;
        
    case DAP_GLOBAL_DB_SET_UPPERBOUND:
        if (!a_key)
            return -1;
        a_cursor->current_page = s_find_leaf_for_key(l_tree, a_key);
        if (a_cursor->current_page) {
            int l_index;
            if (s_leaf_find_entry(a_cursor->current_page, a_key, &l_index) == 0) {
                l_index++;  // Skip exact match
            }
            if (l_index < a_cursor->current_page->header.entries_count) {
                a_cursor->current_index = l_index;
                a_cursor->valid = true;
            } else {
                // Move to next page
                if (a_cursor->current_page->header.right_sibling) {
                    uint64_t l_next_id = a_cursor->current_page->header.right_sibling;
                    s_page_free(a_cursor->current_page);
                    a_cursor->current_page = s_page_read(l_tree, l_next_id, NULL);
                    if (a_cursor->current_page) {
                        s_page_refresh_from_hot_leaf(l_tree, a_cursor->current_page);
                        if (a_cursor->current_page->header.entries_count > 0) {
                            a_cursor->current_index = 0;
                            a_cursor->valid = true;
                        } else {
                            a_cursor->at_end = true;
                        }
                    } else {
                        a_cursor->at_end = true;
                    }
                } else {
                    a_cursor->at_end = true;
                }
            }
        }
        break;
    }
    
    if (!a_cursor->valid && !a_cursor->at_end)
        return 1;  // Not found
    
    return 0;
}

int dap_global_db_cursor_move(dap_global_db_cursor_t *a_cursor,
                              dap_global_db_cursor_op_t a_op,
                              const dap_global_db_key_t *a_key)
{
    dap_return_val_if_fail(a_cursor && a_cursor->tree, -1);
    if (a_cursor->snapshot_slot >= 0)
        return s_btree_cursor_move_snapshot(a_cursor, a_op, a_key);
    // Fallback: no snapshot — use rwlock + sibling-link traversal
    pthread_rwlock_rdlock(&a_cursor->tree->lock);
    int l_ret = s_btree_cursor_move_impl(a_cursor, a_op, a_key);
    pthread_rwlock_unlock(&a_cursor->tree->lock);
    return l_ret;
}

static int s_btree_cursor_get_impl(dap_global_db_cursor_t *a_cursor,
                             dap_global_db_key_t *a_out_key,
                             char **a_out_text_key,
                             void **a_out_value, uint32_t *a_out_value_len,
                             void **a_out_sign, uint32_t *a_out_sign_len,
                             uint8_t *a_out_flags)
{
    if (!a_cursor->valid || !a_cursor->current_page)
        return 1;  // Invalid cursor
    
    uint8_t *l_data;
    dap_global_db_leaf_entry_t *l_entry = s_leaf_entry_at(a_cursor->current_page, 
                                                          a_cursor->current_index, &l_data, NULL);
    if (!l_entry)
        return -1;
    
    if (a_out_key)
        *a_out_key = l_entry->driver_hash;
    
    if (a_out_text_key && l_entry->key_len > 0) {
        *a_out_text_key = DAP_NEW_Z_SIZE(char, l_entry->key_len);
        memcpy(*a_out_text_key, l_data, l_entry->key_len);
    } else if (a_out_text_key) {
        *a_out_text_key = NULL;
    }

    if (a_out_value_len)
        *a_out_value_len = l_entry->value_len;
    if (a_out_sign_len)
        *a_out_sign_len = l_entry->sign_len;

    // Disambiguate overflow vs deleted-inline: both use flags bit 0x01, but
    // true overflow entries have payload > MAX_INLINE_PAYLOAD (stored externally)
    bool l_is_overflow = (l_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE)
        && ((size_t)l_entry->value_len + l_entry->sign_len > MAX_INLINE_PAYLOAD);

    if (l_is_overflow) {
        uint64_t l_ov_id = *(const uint64_t *)(l_data + l_entry->key_len);
        size_t l_total = (size_t)l_entry->value_len + (size_t)l_entry->sign_len;
        uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, l_total);
        if (!l_buf)
            return -1;
        int l_ov_rc = s_overflow_read(a_cursor->tree, l_ov_id, l_buf, l_total, NULL);
        if (l_ov_rc != 0) {
            DAP_DELETE(l_buf);
            return -1;
        }
        if (a_out_value && l_entry->value_len > 0) {
            *a_out_value = DAP_NEW_Z_SIZE(uint8_t, l_entry->value_len);
            memcpy(*a_out_value, l_buf, l_entry->value_len);
        } else if (a_out_value)
            *a_out_value = NULL;
        if (a_out_sign && l_entry->sign_len > 0) {
            *a_out_sign = DAP_NEW_Z_SIZE(uint8_t, l_entry->sign_len);
            memcpy(*a_out_sign, l_buf + l_entry->value_len, l_entry->sign_len);
        } else if (a_out_sign)
            *a_out_sign = NULL;
        DAP_DELETE(l_buf);
    } else {
        if (a_out_value && l_entry->value_len > 0) {
            *a_out_value = DAP_NEW_Z_SIZE(uint8_t, l_entry->value_len);
            memcpy(*a_out_value, l_data + l_entry->key_len, l_entry->value_len);
        } else if (a_out_value) {
            *a_out_value = NULL;
        }
        if (a_out_sign && l_entry->sign_len > 0) {
            *a_out_sign = DAP_NEW_Z_SIZE(uint8_t, l_entry->sign_len);
            memcpy(*a_out_sign, l_data + l_entry->key_len + l_entry->value_len, l_entry->sign_len);
        } else if (a_out_sign) {
            *a_out_sign = NULL;
        }
    }
    
    if (a_out_flags)
        *a_out_flags = l_is_overflow
            ? l_entry->flags & (uint8_t)~DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE
            : l_entry->flags;
    
    return 0;
}

int dap_global_db_cursor_get(dap_global_db_cursor_t *a_cursor,
                             dap_global_db_key_t *a_out_key,
                             char **a_out_text_key,
                             void **a_out_value, uint32_t *a_out_value_len,
                             void **a_out_sign, uint32_t *a_out_sign_len,
                             uint8_t *a_out_flags)
{
    dap_return_val_if_fail(a_cursor && a_cursor->tree, -1);
    if (a_cursor->snapshot_slot >= 0) {
        // Lock-free: cursor data lives in heap buffer, protected by snapshot
        return s_btree_cursor_get_impl(a_cursor, a_out_key, a_out_text_key,
                                        a_out_value, a_out_value_len,
                                        a_out_sign, a_out_sign_len, a_out_flags);
    }
    pthread_rwlock_rdlock(&a_cursor->tree->lock);
    int l_ret = s_btree_cursor_get_impl(a_cursor, a_out_key, a_out_text_key,
                                         a_out_value, a_out_value_len,
                                         a_out_sign, a_out_sign_len, a_out_flags);
    pthread_rwlock_unlock(&a_cursor->tree->lock);
    return l_ret;
}

static int s_btree_cursor_get_ref_impl(dap_global_db_cursor_t *a_cursor,
                                       dap_global_db_key_t *a_out_key,
                                       dap_global_db_ref_t *a_out_text_key,
                                       dap_global_db_ref_t *a_out_value,
                                       dap_global_db_ref_t *a_out_sign,
                                       uint8_t *a_out_flags)
{
    if (!a_cursor->valid || !a_cursor->current_page)
        return 1;

    uint8_t *l_data;
    dap_global_db_leaf_entry_t *l_entry = s_leaf_entry_at(a_cursor->current_page,
                                                          a_cursor->current_index, &l_data, NULL);
    if (!l_entry)
        return -1;

    if (a_out_key)
        *a_out_key = l_entry->driver_hash;

    bool l_is_overflow = (l_entry->flags & DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE)
        && ((size_t)l_entry->value_len + l_entry->sign_len > MAX_INLINE_PAYLOAD);

    if (a_out_text_key) {
        a_out_text_key->data = l_entry->key_len > 0 ? l_data : NULL;
        a_out_text_key->len  = l_entry->key_len;
    }
    if (!l_is_overflow) {
        if (a_out_value) {
            a_out_value->data = l_entry->value_len > 0 ? l_data + l_entry->key_len : NULL;
            a_out_value->len  = l_entry->value_len;
        }
        if (a_out_sign) {
            a_out_sign->data = l_entry->sign_len > 0
                ? l_data + l_entry->key_len + l_entry->value_len : NULL;
            a_out_sign->len  = l_entry->sign_len;
        }
    } else {
        if (a_out_value)
            a_out_value->data = NULL, a_out_value->len = 0;
        if (a_out_sign)
            a_out_sign->data = NULL, a_out_sign->len = 0;
    }
    if (a_out_flags)
        *a_out_flags = l_is_overflow
            ? l_entry->flags & (uint8_t)~DAP_GLOBAL_DB_LEAF_ENTRY_OVERFLOW_VALUE
            : l_entry->flags;

    return 0;
}

int dap_global_db_cursor_get_ref(dap_global_db_cursor_t *a_cursor,
                                       dap_global_db_key_t *a_out_key,
                                       dap_global_db_ref_t *a_out_text_key,
                                       dap_global_db_ref_t *a_out_value,
                                       dap_global_db_ref_t *a_out_sign,
                                       uint8_t *a_out_flags)
{
    dap_return_val_if_fail(a_cursor && a_cursor->tree, -1);
    if (a_cursor->snapshot_slot >= 0) {
        // Lock-free: cursor data in heap buffer, snapshot-protected
        return s_btree_cursor_get_ref_impl(a_cursor, a_out_key, a_out_text_key,
                                            a_out_value, a_out_sign, a_out_flags);
    }
    pthread_rwlock_rdlock(&a_cursor->tree->lock);
    int l_ret = s_btree_cursor_get_ref_impl(a_cursor, a_out_key, a_out_text_key,
                                             a_out_value, a_out_sign, a_out_flags);
    pthread_rwlock_unlock(&a_cursor->tree->lock);
    return l_ret;
}

bool dap_global_db_cursor_valid(dap_global_db_cursor_t *a_cursor)
{
    if (!a_cursor || !a_cursor->tree)
        return false;
    // cursor->valid is modified only by cursor_move on the same thread.
    // No lock needed regardless of mode.
    return a_cursor->valid;
}

// ============================================================================
// Tree Integrity Verification (debug/test utility)
// ============================================================================

/**
 * @brief Context for recursive tree verification.
 */
typedef struct {
    dap_global_db_t *tree;
    uint64_t entry_count;         // Total entries found in leaves
    int first_leaf_depth;         // Depth of the first leaf encountered (-1 = not set)
    int error_code;               // First error encountered (0 = ok)
    char error_msg[256];          // Description of first error
    uint64_t mmap_size;           // Cached mmap size for bounds checking
} s_verify_ctx_t;

/**
 * @brief Check that a page_id is valid (non-zero, within mmap bounds).
 */
static bool s_verify_page_id_valid(s_verify_ctx_t *a_ctx, uint64_t a_page_id)
{
    if (a_page_id == 0)
        return false;
    uint64_t l_offset = s_page_offset(a_page_id);
    return (l_offset + DAP_GLOBAL_DB_PAGE_SIZE <= a_ctx->mmap_size);
}

/**
 * @brief Recursive verification of a subtree rooted at a_page_id.
 *
 * @param a_ctx      Verification context (accumulates errors/counts)
 * @param a_page_id  Page to verify
 * @param a_depth    Current depth (0 = root level)
 * @param a_lo       Lower bound key (NULL = no lower bound)
 * @param a_hi       Upper bound key (NULL = no upper bound)
 * @param a_expect_leaf  If true, this page MUST be a leaf (for depth consistency)
 */
static void s_verify_subtree(s_verify_ctx_t *a_ctx, uint64_t a_page_id, int a_depth,
                              const dap_global_db_key_t *a_lo,
                              const dap_global_db_key_t *a_hi)
{
    if (a_ctx->error_code != 0)
        return;

    if (!s_verify_page_id_valid(a_ctx, a_page_id)) {
        a_ctx->error_code = -2;
        snprintf(a_ctx->error_msg, sizeof(a_ctx->error_msg),
                 "invalid page_id=%llu at depth=%d (mmap_size=%llu)",
                 (unsigned long long)a_page_id, a_depth,
                 (unsigned long long)a_ctx->mmap_size);
        return;
    }

    dap_global_db_page_t l_buf;
    if (!s_page_read_ref(a_ctx->tree, a_page_id, &l_buf)) {
        a_ctx->error_code = -2;
        snprintf(a_ctx->error_msg, sizeof(a_ctx->error_msg),
                 "s_page_read_ref failed for page_id=%llu at depth=%d",
                 (unsigned long long)a_page_id, a_depth);
        return;
    }
    dap_global_db_page_t *l_page = &l_buf;

    if (l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF) {
        // --- Leaf verification ---

        // Check leaf depth consistency
        if (a_ctx->first_leaf_depth < 0) {
            a_ctx->first_leaf_depth = a_depth;
        } else if (a_depth != a_ctx->first_leaf_depth) {
            a_ctx->error_code = -6;
            snprintf(a_ctx->error_msg, sizeof(a_ctx->error_msg),
                     "leaf depth inconsistency: page_id=%llu depth=%d, expected=%d",
                     (unsigned long long)a_page_id, a_depth, a_ctx->first_leaf_depth);
            return;
        }

        int l_count = l_page->header.entries_count;
        a_ctx->entry_count += (uint64_t)l_count;

        // Check key ordering within leaf
        dap_global_db_key_t l_prev_key;
        for (int i = 0; i < l_count; i++) {
            dap_global_db_leaf_entry_t *l_entry = s_leaf_entry_at(l_page, i, NULL, NULL);
            if (!l_entry) {
                a_ctx->error_code = -2;
                snprintf(a_ctx->error_msg, sizeof(a_ctx->error_msg),
                         "s_leaf_entry_at failed: page_id=%llu idx=%d/%d",
                         (unsigned long long)a_page_id, i, l_count);
                return;
            }

            // Key must be >= lower bound
            if (a_lo && dap_global_db_key_compare(&l_entry->driver_hash, a_lo) < 0) {
                a_ctx->error_code = -3;
                snprintf(a_ctx->error_msg, sizeof(a_ctx->error_msg),
                         "key below lower bound: page_id=%llu idx=%d",
                         (unsigned long long)a_page_id, i);
                return;
            }
            // Key must be < upper bound (strict)
            if (a_hi && dap_global_db_key_compare(&l_entry->driver_hash, a_hi) >= 0) {
                a_ctx->error_code = -3;
                snprintf(a_ctx->error_msg, sizeof(a_ctx->error_msg),
                         "key above/equal upper bound: page_id=%llu idx=%d",
                         (unsigned long long)a_page_id, i);
                return;
            }
            // Keys must be strictly increasing within the leaf
            if (i > 0 && dap_global_db_key_compare(&l_entry->driver_hash, &l_prev_key) <= 0) {
                a_ctx->error_code = -3;
                snprintf(a_ctx->error_msg, sizeof(a_ctx->error_msg),
                         "key ordering violation in leaf: page_id=%llu idx=%d",
                         (unsigned long long)a_page_id, i);
                return;
            }
            l_prev_key = l_entry->driver_hash;
        }
    } else {
        // --- Branch verification ---

        int l_count = l_page->header.entries_count;

        // Branch must have at least 1 child (child[0])
        uint64_t l_child0 = s_branch_get_child(l_page, 0);
        if (l_child0 == 0) {
            a_ctx->error_code = -2;
            snprintf(a_ctx->error_msg, sizeof(a_ctx->error_msg),
                     "branch child[0]=0: page_id=%llu",
                     (unsigned long long)a_page_id);
            return;
        }

        // Check separator key ordering
        dap_global_db_key_t l_prev_sep;
        for (int i = 0; i < l_count; i++) {
            dap_global_db_branch_entry_t *l_entry = s_branch_entry_at(l_page, i);
            if (!l_entry) {
                a_ctx->error_code = -2;
                snprintf(a_ctx->error_msg, sizeof(a_ctx->error_msg),
                         "s_branch_entry_at failed: page_id=%llu idx=%d",
                         (unsigned long long)a_page_id, i);
                return;
            }
            if (i > 0 && dap_global_db_key_compare(&l_entry->driver_hash, &l_prev_sep) <= 0) {
                a_ctx->error_code = -3;
                snprintf(a_ctx->error_msg, sizeof(a_ctx->error_msg),
                         "separator ordering violation in branch: page_id=%llu idx=%d",
                         (unsigned long long)a_page_id, i);
                return;
            }
            l_prev_sep = l_entry->driver_hash;

            // Verify child page_id is valid
            uint64_t l_child = l_entry->child_page;
            if (!s_verify_page_id_valid(a_ctx, l_child)) {
                a_ctx->error_code = -2;
                snprintf(a_ctx->error_msg, sizeof(a_ctx->error_msg),
                         "branch child invalid: page_id=%llu entry[%d].child=%llu",
                         (unsigned long long)a_page_id, i, (unsigned long long)l_child);
                return;
            }
        }

        // Recursively verify all children with proper key bounds
        // child[0]: keys in [a_lo, sep[0])
        {
            const dap_global_db_key_t *l_hi_bound = NULL;
            if (l_count > 0) {
                dap_global_db_branch_entry_t *e = s_branch_entry_at(l_page, 0);
                l_hi_bound = &e->driver_hash;
            }
            s_verify_subtree(a_ctx, l_child0, a_depth + 1, a_lo, l_hi_bound ? l_hi_bound : a_hi);
            if (a_ctx->error_code != 0) return;
        }

        for (int i = 0; i < l_count; i++) {
            dap_global_db_branch_entry_t *e = s_branch_entry_at(l_page, i);
            const dap_global_db_key_t *l_lo_bound = &e->driver_hash;
            const dap_global_db_key_t *l_hi_bound = NULL;
            if (i + 1 < l_count) {
                dap_global_db_branch_entry_t *e_next = s_branch_entry_at(l_page, i + 1);
                l_hi_bound = &e_next->driver_hash;
            }
            s_verify_subtree(a_ctx, e->child_page, a_depth + 1,
                              l_lo_bound, l_hi_bound ? l_hi_bound : a_hi);
            if (a_ctx->error_code != 0) return;
        }
    }
}

static uint64_t s_count_at_root_impl(dap_global_db_t *a_tree, uint64_t a_root, int a_depth_left)
{
    if (!a_tree || a_root == 0)
        return 0;
    if (a_depth_left <= 0) {
        log_it(L_ERROR, "count_at_root: max depth exceeded at page=%llu (cycle?)",
               (unsigned long long)a_root);
        return 0;
    }
    dap_global_db_page_t l_buf;
    if (!s_page_read_ref(a_tree, a_root, &l_buf))
        return 0;
    if (l_buf.header.flags & DAP_GLOBAL_DB_PAGE_LEAF)
        return (uint64_t)l_buf.header.entries_count;
    uint64_t l_total = 0;
    for (int i = 0; i <= l_buf.header.entries_count; i++) {
        uint64_t l_child = s_branch_get_child(&l_buf, i);
        if (l_child == a_root) {
            log_it(L_ERROR, "count_at_root: self-referencing child page=%llu slot=%d",
                   (unsigned long long)a_root, i);
            return 0;
        }
        l_total += s_count_at_root_impl(a_tree, l_child, a_depth_left - 1);
    }
    return l_total;
}

uint64_t dap_global_db_count_at_root(dap_global_db_t *a_tree, uint64_t a_root)
{
    int l_max_depth = (int)a_tree->header.tree_height + 2;
    if (l_max_depth < 4) l_max_depth = 4;
    return s_count_at_root_impl(a_tree, a_root, l_max_depth);
}

int dap_global_db_verify(dap_global_db_t *a_tree, uint64_t *a_out_entry_count)
{
    dap_return_val_if_fail(a_tree, -1);

    pthread_rwlock_wrlock(&a_tree->lock);
    if (a_tree->hot_leaf && a_tree->hot_leaf->is_dirty)
        s_hot_leaf_flush(a_tree);

    if (a_tree->header.root_page == 0) {
        // Empty tree — valid iff items_count == 0
        pthread_rwlock_unlock(&a_tree->lock);
        if (a_out_entry_count)
            *a_out_entry_count = 0;
        if (a_tree->header.items_count != 0) {
            log_it(L_ERROR, "verify: empty tree (root=0) but items_count=%llu",
                   (unsigned long long)a_tree->header.items_count);
            return -4;
        }
        return 0;
    }

    s_verify_ctx_t l_ctx = {
        .tree = a_tree,
        .entry_count = 0,
        .first_leaf_depth = -1,
        .error_code = 0,
        .mmap_size = a_tree->mmap ? dap_mmap_get_size(a_tree->mmap) : UINT64_MAX,
    };

    s_verify_subtree(&l_ctx, a_tree->header.root_page, 0, NULL, NULL);

    if (l_ctx.error_code == 0) {
        // Check entry count matches header
        if (l_ctx.entry_count != a_tree->header.items_count) {
            l_ctx.error_code = -4;
            snprintf(l_ctx.error_msg, sizeof(l_ctx.error_msg),
                     "entry count mismatch: found=%llu, header=%llu",
                     (unsigned long long)l_ctx.entry_count,
                     (unsigned long long)a_tree->header.items_count);
        }
    }

    if (l_ctx.error_code == 0) {
        // Check tree height matches actual leaf depth
        int l_expected_height = l_ctx.first_leaf_depth + 1;
        if ((int)a_tree->header.tree_height != l_expected_height) {
            l_ctx.error_code = -5;
            snprintf(l_ctx.error_msg, sizeof(l_ctx.error_msg),
                     "tree height mismatch: header=%u, actual=%d",
                     (unsigned)a_tree->header.tree_height, l_expected_height);
        }
    }

    pthread_rwlock_unlock(&a_tree->lock);

    if (l_ctx.error_code != 0)
        log_it(L_ERROR, "B-tree verify FAILED (code %d): %s", l_ctx.error_code, l_ctx.error_msg);

    if (a_out_entry_count)
        *a_out_entry_count = l_ctx.entry_count;
    return l_ctx.error_code;
}
