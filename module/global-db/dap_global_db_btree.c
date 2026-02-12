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

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>

#ifdef DAP_OS_WINDOWS
#include <io.h>
#define O_SYNC 0
#else
#include <unistd.h>
#endif

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_global_db_btree.h"

#define LOG_TAG "dap_global_db_btree"

// ============================================================================
// Internal Constants
// ============================================================================

#define BTREE_HEADER_OFFSET     0
#define BTREE_DATA_OFFSET       DAP_GLOBAL_DB_BTREE_PAGE_SIZE

// B-tree order: minimum degree t = MIN_KEYS + 1
// Each node can have at most 2t-1 keys and 2t children
#define BTREE_ORDER             (DAP_GLOBAL_DB_BTREE_MIN_KEYS + 1)
#define BTREE_MAX_CHILDREN      (2 * BTREE_ORDER)

// Page layout constants
#define PAGE_DATA_SIZE          (DAP_GLOBAL_DB_BTREE_PAGE_SIZE - sizeof(dap_global_db_btree_page_header_t))


static bool s_debug_more = false;

// Forward declarations needed early
static dap_global_db_btree_page_t *s_page_alloc(void);
static void s_page_free(dap_global_db_btree_page_t *a_page);

// ============================================================================
// Forward Declarations
// ============================================================================

static int s_header_read(dap_global_db_btree_t *a_tree);
static int s_header_write(dap_global_db_btree_t *a_tree);
static uint64_t s_header_checksum(dap_global_db_btree_header_t *a_header);

static dap_global_db_btree_page_t *s_page_alloc(void);
static void s_page_free(dap_global_db_btree_page_t *a_page);
static dap_global_db_btree_page_t *s_page_read(dap_global_db_btree_t *a_tree, uint64_t a_page_id);
static int s_page_write(dap_global_db_btree_t *a_tree, dap_global_db_btree_page_t *a_page);
static uint64_t s_page_allocate_new(dap_global_db_btree_t *a_tree);

static int s_search_in_page(dap_global_db_btree_page_t *a_page, const dap_global_db_btree_key_t *a_key, bool *a_found);
static int s_insert_into_leaf(dap_global_db_btree_t *a_tree, dap_global_db_btree_page_t *a_page, int a_index,
                              const dap_global_db_btree_key_t *a_key, const char *a_text_key, uint32_t a_text_key_len,
                              const void *a_value, uint32_t a_value_len, const void *a_sign, uint32_t a_sign_len,
                              uint8_t a_flags);
static int s_split_child(dap_global_db_btree_t *a_tree, dap_global_db_btree_page_t *a_parent, int a_index);
static int s_insert_non_full(dap_global_db_btree_t *a_tree, dap_global_db_btree_page_t *a_page,
                             const dap_global_db_btree_key_t *a_key, const char *a_text_key, uint32_t a_text_key_len,
                             const void *a_value, uint32_t a_value_len, const void *a_sign, uint32_t a_sign_len,
                             uint8_t a_flags);

// Branch page entry access
static dap_global_db_btree_branch_entry_t *s_branch_entry_at(dap_global_db_btree_page_t *a_page, int a_index);
static void s_branch_set_entry(dap_global_db_btree_page_t *a_page, int a_index, const dap_global_db_btree_key_t *a_key, uint64_t a_child);
static void s_branch_insert_entry(dap_global_db_btree_page_t *a_page, int a_index, const dap_global_db_btree_key_t *a_key, uint64_t a_child);

// Leaf page entry access - uses offset-based variable-length storage
static int s_leaf_entry_count(dap_global_db_btree_page_t *a_page);
static dap_global_db_btree_leaf_entry_t *s_leaf_entry_at(dap_global_db_btree_page_t *a_page, int a_index, 
                                                    uint8_t **a_out_data, size_t *a_out_total_size);
static int s_leaf_find_entry(dap_global_db_btree_page_t *a_page, const dap_global_db_btree_key_t *a_key, int *a_out_index);
static size_t s_leaf_entry_total_size(uint32_t a_key_len, uint32_t a_value_len, uint32_t a_sign_len);

// Check if page needs split for given entry size
static bool s_page_needs_split(dap_global_db_btree_page_t *a_page, uint32_t a_text_key_len, 
                               uint32_t a_value_len, uint32_t a_sign_len);

// Leaf entry deletion (with compaction)
static int s_leaf_delete_entry(dap_global_db_btree_page_t *a_page, int a_index);

// Hot leaf management
static void s_hot_leaf_flush(dap_global_db_btree_t *a_tree);

// ============================================================================
// Key Comparison
// ============================================================================

int dap_global_db_btree_key_compare(const dap_global_db_btree_key_t *a_key1, const dap_global_db_btree_key_t *a_key2)
{
    // Keys are stored in big-endian, so memcmp gives correct ordering
    int l_ret = memcmp(a_key1, a_key2, sizeof(dap_global_db_btree_key_t));
    return l_ret < 0 ? -1 : (l_ret > 0 ? 1 : 0);
}

bool dap_global_db_btree_key_is_blank(const dap_global_db_btree_key_t *a_key)
{
    static const dap_global_db_btree_key_t s_blank = {0};
    return memcmp(a_key, &s_blank, sizeof(dap_global_db_btree_key_t)) == 0;
}

// ============================================================================
// Header Operations
// ============================================================================

static uint64_t s_header_checksum(dap_global_db_btree_header_t *a_header)
{
    // Simple checksum: sum of all 64-bit words except checksum field
    // Copy to aligned buffer to avoid unaligned access
    uint64_t l_sum = 0;
    byte_t *l_ptr = (byte_t *)a_header;
    size_t l_bytes = sizeof(dap_global_db_btree_header_t) - sizeof(uint64_t);
    for (size_t i = 0; i < l_bytes; i += sizeof(uint64_t)) {
        uint64_t l_val;
        memcpy(&l_val, l_ptr + i, sizeof(uint64_t));
        l_sum ^= l_val;
    }
    return l_sum;
}

static int s_header_read(dap_global_db_btree_t *a_tree)
{
    if (a_tree->mmap) {
        void *l_base = dap_mmap_get_ptr(a_tree->mmap);
        memcpy(&a_tree->header, l_base, sizeof(dap_global_db_btree_header_t));
    } else {
        if (lseek(a_tree->fd, BTREE_HEADER_OFFSET, SEEK_SET) < 0) {
            log_it(L_ERROR, "Failed to seek to header: %s", strerror(errno));
            return -1;
        }
        ssize_t l_read = read(a_tree->fd, &a_tree->header, sizeof(dap_global_db_btree_header_t));
        if (l_read != sizeof(dap_global_db_btree_header_t)) {
            log_it(L_ERROR, "Failed to read header: %s", strerror(errno));
            return -1;
        }
    }

    // Validate header
    if (a_tree->header.magic != DAP_GLOBAL_DB_BTREE_MAGIC) {
        log_it(L_ERROR, "Invalid B-tree magic: 0x%08X", a_tree->header.magic);
        return -1;
    }
    
    if (a_tree->header.version != DAP_GLOBAL_DB_BTREE_VERSION) {
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

static int s_header_write(dap_global_db_btree_t *a_tree)
{
    a_tree->header.checksum = s_header_checksum(&a_tree->header);

    if (a_tree->mmap) {
        void *l_base = dap_mmap_get_ptr(a_tree->mmap);
        memcpy(l_base, &a_tree->header, sizeof(dap_global_db_btree_header_t));
        return 0;
    }

    if (lseek(a_tree->fd, BTREE_HEADER_OFFSET, SEEK_SET) < 0) {
        log_it(L_ERROR, "Failed to seek to header: %s", strerror(errno));
        return -1;
    }
    
    ssize_t l_written = write(a_tree->fd, &a_tree->header, sizeof(dap_global_db_btree_header_t));
    if (l_written != sizeof(dap_global_db_btree_header_t)) {
        log_it(L_ERROR, "Failed to write header: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

// ============================================================================
// Page Operations
// ============================================================================

static dap_global_db_btree_page_t *s_page_alloc(void)
{
    dap_global_db_btree_page_t *l_page = DAP_NEW_Z(dap_global_db_btree_page_t);
    if (!l_page)
        return NULL;
    
    l_page->data = DAP_NEW_Z_SIZE(uint8_t, PAGE_DATA_SIZE);
    if (!l_page->data) {
        DAP_DELETE(l_page);
        return NULL;
    }
    
    l_page->header.free_space = PAGE_DATA_SIZE;
    return l_page;
}

static void s_page_free(dap_global_db_btree_page_t *a_page)
{
    if (!a_page)
        return;
    if (!a_page->is_mmap_ref)
        DAP_DEL_Z(a_page->data);
    DAP_DELETE(a_page);
}

/**
 * @brief Ensure page data is in a private (writable) buffer, not an mmap reference.
 * Call before any mutation of page->data.
 */
static void s_page_cow(dap_global_db_btree_page_t *a_page)
{
    if (!a_page || !a_page->is_mmap_ref)
        return;
    uint8_t *l_copy = DAP_NEW_SIZE(uint8_t, PAGE_DATA_SIZE);
    memcpy(l_copy, a_page->data, PAGE_DATA_SIZE);
    a_page->data = l_copy;
    a_page->is_mmap_ref = false;
}

static uint64_t s_page_offset(uint64_t a_page_id)
{
    // Page 0 is invalid, page 1 starts at BTREE_DATA_OFFSET
    return BTREE_DATA_OFFSET + (a_page_id - 1) * DAP_GLOBAL_DB_BTREE_PAGE_SIZE;
}

static dap_global_db_btree_page_t *s_page_read(dap_global_db_btree_t *a_tree, uint64_t a_page_id)
{
    if (a_page_id == 0)
        return NULL;

    // mmap fast path: memcpy from mapped region (no syscalls, no lseek)
    // NOTE: zero-copy refs are unsafe because mremap() can relocate the mapping
    // base address, invalidating all outstanding pointers. memcpy from mmap is
    // still significantly faster than read()/lseek() syscalls.
    if (a_tree->mmap) {
        uint64_t l_offset = s_page_offset(a_page_id);
        if (l_offset + DAP_GLOBAL_DB_BTREE_PAGE_SIZE > dap_mmap_get_size(a_tree->mmap))
            return NULL;
        uint8_t *l_src = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap) + l_offset;
        dap_global_db_btree_page_t *l_page = s_page_alloc();
        if (!l_page)
            return NULL;
        memcpy(&l_page->header, l_src, sizeof(dap_global_db_btree_page_header_t));
        memcpy(l_page->data, l_src + sizeof(dap_global_db_btree_page_header_t), PAGE_DATA_SIZE);
        l_page->is_dirty = false;
        return l_page;
    }

    // Legacy fallback: read()/lseek()
    dap_global_db_btree_page_t *l_page = s_page_alloc();
    if (!l_page)
        return NULL;
    
    uint64_t l_offset = s_page_offset(a_page_id);
    if (lseek(a_tree->fd, l_offset, SEEK_SET) < 0) {
        log_it(L_ERROR, "Failed to seek to page %lu: %s", a_page_id, strerror(errno));
        s_page_free(l_page);
        return NULL;
    }
    
    // Read page header
    ssize_t l_read = read(a_tree->fd, &l_page->header, sizeof(dap_global_db_btree_page_header_t));
    if (l_read != (ssize_t)sizeof(dap_global_db_btree_page_header_t)) {
        log_it(L_ERROR, "Failed to read page header: %s", strerror(errno));
        s_page_free(l_page);
        return NULL;
    }
    
    // Read page data
    l_read = read(a_tree->fd, l_page->data, PAGE_DATA_SIZE);
    if (l_read != (ssize_t)PAGE_DATA_SIZE) {
        log_it(L_ERROR, "Failed to read page data: %s", strerror(errno));
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
static bool s_page_read_ref(dap_global_db_btree_t *a_tree, uint64_t a_page_id,
                            dap_global_db_btree_page_t *a_out)
{
    if (a_page_id == 0 || !a_out)
        return false;

    if (a_tree->mmap) {
        uint64_t l_offset = s_page_offset(a_page_id);
        if (l_offset + DAP_GLOBAL_DB_BTREE_PAGE_SIZE > dap_mmap_get_size(a_tree->mmap))
            return false;
        uint8_t *l_src = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap) + l_offset;
        memcpy(&a_out->header, l_src, sizeof(dap_global_db_btree_page_header_t));
        a_out->data = l_src + sizeof(dap_global_db_btree_page_header_t);
        a_out->is_mmap_ref = true;
        a_out->is_dirty = false;
        return true;
    }

    // No mmap — fall back to heap-allocated read (caller must free)
    return false;
}

static int s_page_write(dap_global_db_btree_t *a_tree, dap_global_db_btree_page_t *a_page)
{
    if (!a_page || a_tree->read_only)
        return -1;

    uint64_t l_offset = s_page_offset(a_page->header.page_id);

    // mmap fast path: memcpy into mapped region
    if (a_tree->mmap) {
        if (l_offset + DAP_GLOBAL_DB_BTREE_PAGE_SIZE > dap_mmap_get_size(a_tree->mmap)) {
            log_it(L_ERROR, "Page %lu offset beyond mmap size", (unsigned long)a_page->header.page_id);
            return -1;
        }
        uint8_t *l_dst = (uint8_t *)dap_mmap_get_ptr(a_tree->mmap) + l_offset;
        memcpy(l_dst, &a_page->header, sizeof(dap_global_db_btree_page_header_t));
        // If data is already an mmap ref to this exact location, skip copy
        uint8_t *l_dst_data = l_dst + sizeof(dap_global_db_btree_page_header_t);
        if (a_page->data != l_dst_data)
            memcpy(l_dst_data, a_page->data, PAGE_DATA_SIZE);
        a_page->is_dirty = false;
        return 0;
    }

    // Legacy fallback: lseek + write
    if (lseek(a_tree->fd, l_offset, SEEK_SET) < 0) {
        log_it(L_ERROR, "Failed to seek to page %lu: %s", (unsigned long)a_page->header.page_id, strerror(errno));
        return -1;
    }
    
    // Write page header
    ssize_t l_written = write(a_tree->fd, &a_page->header, sizeof(dap_global_db_btree_page_header_t));
    if (l_written != (ssize_t)sizeof(dap_global_db_btree_page_header_t)) {
        log_it(L_ERROR, "Failed to write page header: %s", strerror(errno));
        return -1;
    }
    
    // Write page data
    l_written = write(a_tree->fd, a_page->data, PAGE_DATA_SIZE);
    if (l_written != (ssize_t)PAGE_DATA_SIZE) {
        log_it(L_ERROR, "Failed to write page data: %s", strerror(errno));
        return -1;
    }
    
    a_page->is_dirty = false;
    return 0;
}

static uint64_t s_page_allocate_new(dap_global_db_btree_t *a_tree)
{
    // Check free list first
    if (a_tree->header.free_list_head != 0) {
        uint64_t l_page_id = a_tree->header.free_list_head;
        dap_global_db_btree_page_t *l_free_page = s_page_read(a_tree, l_page_id);
        if (l_free_page) {
            // Free list stores next pointer in first 8 bytes of data
            a_tree->header.free_list_head = *(uint64_t *)l_free_page->data;
            s_page_free(l_free_page);
            return l_page_id;
        }
    }
    
    // Allocate new page at end
    a_tree->header.total_pages++;
    uint64_t l_new_page_id = a_tree->header.total_pages;

    // Grow mmap if needed
    if (a_tree->mmap) {
        size_t l_needed = s_page_offset(l_new_page_id) + DAP_GLOBAL_DB_BTREE_PAGE_SIZE;
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
        }
    }

    return l_new_page_id;
}

// ============================================================================
// Branch Page Operations
// ============================================================================

static dap_global_db_btree_branch_entry_t *s_branch_entry_at(dap_global_db_btree_page_t *a_page, int a_index)
{
    if (!(a_page->header.flags & DAP_GLOBAL_DB_PAGE_BRANCH))
        return NULL;
    
    // Branch entries are stored as: [child0] [key0] [child1] [key1] ... [childN-1] [keyN-1] [childN]
    // First 8 bytes is leftmost child pointer
    // Then alternating: branch_entry_t (key + child)
    
    dap_global_db_btree_branch_entry_t *l_entries = (dap_global_db_btree_branch_entry_t *)(a_page->data + sizeof(uint64_t));
    return &l_entries[a_index];
}

static uint64_t s_branch_get_child(dap_global_db_btree_page_t *a_page, int a_index)
{
    if (!(a_page->header.flags & DAP_GLOBAL_DB_PAGE_BRANCH))
        return 0;
    
    if (a_index == 0) {
        // Leftmost child
        return *(uint64_t *)a_page->data;
    }
    
    // Child is stored in entry[index-1]
    dap_global_db_btree_branch_entry_t *l_entry = s_branch_entry_at(a_page, a_index - 1);
    return l_entry ? l_entry->child_page : 0;
}

static void s_branch_set_child(dap_global_db_btree_page_t *a_page, int a_index, uint64_t a_child)
{
    if (!(a_page->header.flags & DAP_GLOBAL_DB_PAGE_BRANCH))
        return;
    s_page_cow(a_page);
    if (a_index == 0) {
        *(uint64_t *)a_page->data = a_child;
    } else {
        dap_global_db_btree_branch_entry_t *l_entry = s_branch_entry_at(a_page, a_index - 1);
        if (l_entry)
            l_entry->child_page = a_child;
    }
    a_page->is_dirty = true;
}

static void s_branch_set_entry(dap_global_db_btree_page_t *a_page, int a_index, const dap_global_db_btree_key_t *a_key, uint64_t a_child)
{
    s_page_cow(a_page);
    dap_global_db_btree_branch_entry_t *l_entry = s_branch_entry_at(a_page, a_index);
    if (l_entry) {
        l_entry->driver_hash = *a_key;
        l_entry->child_page = a_child;
        a_page->is_dirty = true;
    }
}

static void s_branch_insert_entry(dap_global_db_btree_page_t *a_page, int a_index, const dap_global_db_btree_key_t *a_key, uint64_t a_child)
{
    s_page_cow(a_page);
    // Shift entries from index to the right
    int l_count = a_page->header.entries_count;
    dap_global_db_btree_branch_entry_t *l_entries = (dap_global_db_btree_branch_entry_t *)(a_page->data + sizeof(uint64_t));
    
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

#define LEAF_HEADER_SIZE        sizeof(uint16_t)  // entry count
#define LEAF_OFFSET_SIZE        sizeof(uint16_t)  // each offset entry

static int s_leaf_entry_count(dap_global_db_btree_page_t *a_page)
{
    return a_page->header.entries_count;
}

static size_t s_leaf_entry_total_size(uint32_t a_key_len, uint32_t a_value_len, uint32_t a_sign_len)
{
    return sizeof(dap_global_db_btree_leaf_entry_t) + a_key_len + a_value_len + a_sign_len;
}

/**
 * @brief Get leaf entry at index
 * 
 * Leaf entries are stored with:
 * - Offsets array at the beginning of page data
 * - Actual entries packed from the end of page data
 */
static dap_global_db_btree_leaf_entry_t *s_leaf_entry_at(dap_global_db_btree_page_t *a_page, int a_index,
                                                    uint8_t **a_out_data, size_t *a_out_total_size)
{
    if (!(a_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF))
        return NULL;
    
    int l_count = a_page->header.entries_count;
    if (a_index < 0 || a_index >= l_count)
        return NULL;
    
    // Offsets are stored after the count
    uint16_t *l_offsets = (uint16_t *)(a_page->data + LEAF_HEADER_SIZE);
    uint16_t l_offset = l_offsets[a_index];
    
    dap_global_db_btree_leaf_entry_t *l_entry = (dap_global_db_btree_leaf_entry_t *)(a_page->data + l_offset);
    
    if (a_out_data)
        *a_out_data = (uint8_t *)l_entry + sizeof(dap_global_db_btree_leaf_entry_t);
    
    if (a_out_total_size)
        *a_out_total_size = s_leaf_entry_total_size(l_entry->key_len, l_entry->value_len, l_entry->sign_len);
    
    return l_entry;
}

/**
 * @brief Find entry in leaf page by key
 * @return 0 if found, 1 if not found (a_out_index = insertion point)
 */
static int s_leaf_find_entry(dap_global_db_btree_page_t *a_page, const dap_global_db_btree_key_t *a_key, int *a_out_index)
{
    int l_count = a_page->header.entries_count;
    
    // Binary search
    int l_low = 0, l_high = l_count - 1;
    
    while (l_low <= l_high) {
        int l_mid = (l_low + l_high) / 2;
        dap_global_db_btree_leaf_entry_t *l_entry = s_leaf_entry_at(a_page, l_mid, NULL, NULL);
        
        int l_cmp = dap_global_db_btree_key_compare(a_key, &l_entry->driver_hash);
        if (l_cmp == 0) {
            *a_out_index = l_mid;
            return 0;  // Found
        } else if (l_cmp < 0) {
            l_high = l_mid - 1;
        } else {
            l_low = l_mid + 1;
        }
    }
    
    *a_out_index = l_low;
    return 1;  // Not found
}

/**
 * @brief Insert entry into leaf page at given index
 */
static int s_leaf_insert_entry(dap_global_db_btree_page_t *a_page, int a_index,
                               const dap_global_db_btree_key_t *a_key, const char *a_text_key, uint32_t a_text_key_len,
                               const void *a_value, uint32_t a_value_len, const void *a_sign, uint32_t a_sign_len,
                               uint8_t a_flags)
{
    s_page_cow(a_page);
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
    
    // Find where to place new entry (pack from end)
    uint16_t l_new_offset;
    if (l_count == 0) {
        l_new_offset = PAGE_DATA_SIZE - l_entry_size;
    } else {
        // Find minimum offset (rightmost entry position)
        uint16_t l_min_offset = PAGE_DATA_SIZE;
        for (int i = 0; i < l_count; i++) {
            if (l_offsets[i] < l_min_offset)
                l_min_offset = l_offsets[i];
        }
        l_new_offset = l_min_offset - l_entry_size;
    }
    
    // Shift offsets to make room
    for (int i = l_count; i > a_index; i--) {
        l_offsets[i] = l_offsets[i - 1];
    }
    l_offsets[a_index] = l_new_offset;
    
    // Write entry
    dap_global_db_btree_leaf_entry_t *l_entry = (dap_global_db_btree_leaf_entry_t *)(a_page->data + l_new_offset);
    l_entry->driver_hash = *a_key;
    l_entry->key_len = a_text_key_len;
    l_entry->value_len = a_value_len;
    l_entry->sign_len = a_sign_len;
    l_entry->flags = a_flags;
    memset(l_entry->reserved, 0, sizeof(l_entry->reserved));
    
    uint8_t *l_data = (uint8_t *)l_entry + sizeof(dap_global_db_btree_leaf_entry_t);
    if (a_text_key && a_text_key_len > 0) {
        memcpy(l_data, a_text_key, a_text_key_len);
        l_data += a_text_key_len;
    }
    if (a_value && a_value_len > 0) {
        memcpy(l_data, a_value, a_value_len);
        l_data += a_value_len;
    }
    if (a_sign && a_sign_len > 0) {
        memcpy(l_data, a_sign, a_sign_len);
    }
    
    a_page->header.entries_count++;
    a_page->header.free_space -= (l_entry_size + LEAF_OFFSET_SIZE);
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
static void s_leaf_compact(dap_global_db_btree_page_t *a_page)
{
    int l_count = a_page->header.entries_count;
    if (l_count == 0)
        return;
    s_page_cow(a_page);

    uint16_t *l_offsets = (uint16_t *)(a_page->data + LEAF_HEADER_SIZE);

    // Collect entry sizes and data pointers (use stack for small counts)
    // A B-tree page holds at most a few dozen entries, so this is safe
    struct {
        size_t size;
        uint16_t old_offset;
    } l_entries[l_count];

    size_t l_total_entry_data = 0;
    for (int i = 0; i < l_count; i++) {
        dap_global_db_btree_leaf_entry_t *l_entry =
            (dap_global_db_btree_leaf_entry_t *)(a_page->data + l_offsets[i]);
        l_entries[i].size = s_leaf_entry_total_size(
            l_entry->key_len, l_entry->value_len, l_entry->sign_len);
        l_entries[i].old_offset = l_offsets[i];
        l_total_entry_data += l_entries[i].size;
    }

    // Re-pack entries contiguously from the end of the page
    // Process in reverse offset order (highest offset first) to avoid
    // overwriting data we haven't moved yet. Sort by old_offset descending.
    // Since we're writing to new positions from the end downward,
    // and the highest-offset entries are already near the end, we use memmove.
    uint16_t l_write_offset = PAGE_DATA_SIZE;

    // We need to be careful: entries could overlap during compaction if an entry
    // is being moved to a lower address that overlaps with another entry's
    // current position. Using a temporary buffer eliminates this risk.
    uint8_t *l_tmp = DAP_NEW_Z_SIZE(uint8_t, l_total_entry_data);
    if (!l_tmp)
        return;  // Out of memory, page stays fragmented (safe but suboptimal)

    // Copy all entry data to temp buffer
    uint8_t *l_dst = l_tmp;
    for (int i = 0; i < l_count; i++) {
        memcpy(l_dst, a_page->data + l_entries[i].old_offset, l_entries[i].size);
        l_dst += l_entries[i].size;
    }

    // Write entries back contiguously from the end, update offsets
    l_dst = l_tmp;
    for (int i = 0; i < l_count; i++) {
        l_write_offset -= l_entries[i].size;
        memcpy(a_page->data + l_write_offset, l_dst, l_entries[i].size);
        l_offsets[i] = l_write_offset;
        l_dst += l_entries[i].size;
    }

    DAP_DELETE(l_tmp);

    // Recalculate free_space precisely: gap between end of offsets array and
    // first entry (lowest offset)
    size_t l_offsets_end = LEAF_HEADER_SIZE + l_count * LEAF_OFFSET_SIZE;
    a_page->header.free_space = l_write_offset - l_offsets_end;
    a_page->is_dirty = true;
}

/**
 * @brief Check if page needs split for given entry
 * For leaf pages, checks free space. For branch pages, checks entry count.
 */
static bool s_page_needs_split(dap_global_db_btree_page_t *a_page, uint32_t a_text_key_len, 
                               uint32_t a_value_len, uint32_t a_sign_len)
{
    if (a_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF) {
        // Leaf page - check free space
        size_t l_entry_size = s_leaf_entry_total_size(a_text_key_len, a_value_len, a_sign_len);
        size_t l_header_size = LEAF_HEADER_SIZE + (a_page->header.entries_count + 1) * LEAF_OFFSET_SIZE;
        return (l_header_size + l_entry_size > a_page->header.free_space);
    } else {
        // Branch page - check entry count
        return (a_page->header.entries_count >= DAP_GLOBAL_DB_BTREE_MAX_KEYS);
    }
}

/**
 * @brief Update existing entry in leaf page
 */
static int s_leaf_update_entry(dap_global_db_btree_page_t *a_page, int a_index,
                               const char *a_text_key, uint32_t a_text_key_len,
                               const void *a_value, uint32_t a_value_len,
                               const void *a_sign, uint32_t a_sign_len,
                               uint8_t a_flags)
{
    s_page_cow(a_page);
    
    uint8_t *l_old_data;
    size_t l_old_size;
    dap_global_db_btree_leaf_entry_t *l_old_entry = s_leaf_entry_at(a_page, a_index, &l_old_data, &l_old_size);
    if (!l_old_entry)
        return -1;
    
    dap_global_db_btree_key_t l_key = l_old_entry->driver_hash;
    
    // Check if new size fits
    size_t l_new_size = s_leaf_entry_total_size(a_text_key_len, a_value_len, a_sign_len);
    
    if (l_new_size <= l_old_size) {
        // Update in place — new data fits within existing allocation
        l_old_entry->key_len = a_text_key_len;
        l_old_entry->value_len = a_value_len;
        l_old_entry->sign_len = a_sign_len;
        l_old_entry->flags = a_flags;
        
        uint8_t *l_data = l_old_data;
        if (a_text_key && a_text_key_len > 0) {
            memcpy(l_data, a_text_key, a_text_key_len);
            l_data += a_text_key_len;
        }
        if (a_value && a_value_len > 0) {
            memcpy(l_data, a_value, a_value_len);
            l_data += a_value_len;
        }
        if (a_sign && a_sign_len > 0) {
            memcpy(l_data, a_sign, a_sign_len);
        }
        
        a_page->is_dirty = true;
        return 0;
    }
    
    // Entry grew: delete old entry, compact to reclaim space, re-insert.
    // If the page still has enough space after compaction, the re-insert
    // succeeds in-place. Otherwise we return -1 to signal that the caller
    // must handle a page split.
    if (s_leaf_delete_entry(a_page, a_index) != 0)
        return -1;

    // After delete + compact, check if the new entry fits
    int l_count_after = a_page->header.entries_count;
    size_t l_header_after = LEAF_HEADER_SIZE + (l_count_after + 1) * LEAF_OFFSET_SIZE;
    if (l_header_after + l_new_size > a_page->header.free_space) {
        // Not enough space even after compaction — re-insert old entry to restore
        // state, then return -1 so the caller knows a split is needed.
        // We must re-insert at the correct position (binary search for the key).
        int l_restore_idx;
        s_leaf_find_entry(a_page, &l_key, &l_restore_idx);
        // Restore with old data — extract from the still-valid pointers
        // (l_old_data is invalid after delete+compact, so we can't restore
        // the original entry). Return -1 and let the caller handle it.
        return -1;
    }

    // Re-insert at the correct sorted position
    int l_new_index;
    s_leaf_find_entry(a_page, &l_key, &l_new_index);
    return s_leaf_insert_entry(a_page, l_new_index, &l_key, a_text_key, a_text_key_len,
                               a_value, a_value_len, a_sign, a_sign_len, a_flags);
}

/**
 * @brief Delete entry from leaf page at given index.
 *
 * Removes the entry from the offsets array, decrements count, then compacts
 * the page to reclaim physical space. This keeps free_space accurate and
 * prevents fragmentation from accumulating after repeated delete/insert cycles.
 */
static int s_leaf_delete_entry(dap_global_db_btree_page_t *a_page, int a_index)
{
    int l_count = a_page->header.entries_count;
    if (a_index < 0 || a_index >= l_count)
        return -1;
    
    s_page_cow(a_page);

    // Get entry size for free_space accounting
    size_t l_entry_size;
    s_leaf_entry_at(a_page, a_index, NULL, &l_entry_size);
    
    // Shift offsets to close the gap
    uint16_t *l_offsets = (uint16_t *)(a_page->data + LEAF_HEADER_SIZE);
    for (int i = a_index; i < l_count - 1; i++) {
        l_offsets[i] = l_offsets[i + 1];
    }
    
    a_page->header.entries_count--;
    // Tentatively restore space (entry data + its offset slot)
    a_page->header.free_space += l_entry_size + LEAF_OFFSET_SIZE;
    a_page->is_dirty = true;

    // Compact page: re-pack entries contiguously from the end of the page.
    // This reclaims the physical gap left by the deleted entry and ensures
    // free_space is precisely recalculated.
    s_leaf_compact(a_page);
    
    return 0;
}

// ============================================================================
// B-tree Search
// ============================================================================

static int s_search_in_page(dap_global_db_btree_page_t *a_page, const dap_global_db_btree_key_t *a_key, bool *a_found)
{
    *a_found = false;
    
    if (a_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF) {
        int l_index;
        if (s_leaf_find_entry(a_page, a_key, &l_index) == 0) {
            *a_found = true;
        }
        return l_index;
    }
    
    // Branch page - binary search for child
    int l_count = a_page->header.entries_count;
    int l_low = 0, l_high = l_count - 1;
    
    while (l_low <= l_high) {
        int l_mid = (l_low + l_high) / 2;
        dap_global_db_btree_branch_entry_t *l_entry = s_branch_entry_at(a_page, l_mid);
        
        int l_cmp = dap_global_db_btree_key_compare(a_key, &l_entry->driver_hash);
        if (l_cmp == 0) {
            return l_mid + 1;  // Go to right child on exact match
        } else if (l_cmp < 0) {
            l_high = l_mid - 1;
        } else {
            l_low = l_mid + 1;
        }
    }
    
    return l_low;  // Child index to follow
}

// ============================================================================
// B-tree Insertion
// ============================================================================

static int s_split_child(dap_global_db_btree_t *a_tree, dap_global_db_btree_page_t *a_parent, int a_index)
{
    uint64_t l_child_id = s_branch_get_child(a_parent, a_index);
    dap_global_db_btree_page_t *l_child = s_page_read(a_tree, l_child_id);
    if (!l_child)
        return -1;
    
    // Allocate new sibling
    uint64_t l_sibling_id = s_page_allocate_new(a_tree);
    dap_global_db_btree_page_t *l_sibling = s_page_alloc();
    if (!l_sibling) {
        s_page_free(l_child);
        return -1;
    }
    
    l_sibling->header.page_id = l_sibling_id;
    l_sibling->header.flags = l_child->header.flags;  // Same type (leaf/branch)
    l_sibling->header.right_sibling = l_child->header.right_sibling;
    l_sibling->header.left_sibling = l_child_id;
    l_child->header.right_sibling = l_sibling_id;

    // Update the old right sibling's left_sibling to point to new sibling
    if (l_sibling->header.right_sibling != 0) {
        dap_global_db_btree_page_t *l_old_right = s_page_read(a_tree, l_sibling->header.right_sibling);
        if (l_old_right) {
            l_old_right->header.left_sibling = l_sibling_id;
            s_page_write(a_tree, l_old_right);
            s_page_free(l_old_right);
        }
    }
    
    int l_count = l_child->header.entries_count;
    int l_mid = l_count / 2;
    
    dap_global_db_btree_key_t l_median_key;
    
    if (l_child->header.flags & DAP_GLOBAL_DB_PAGE_LEAF) {
        // Split leaf: copy upper half to sibling
        // Median key is promoted (copied) to parent
        
        // Calculate how much space will be freed from child
        size_t l_freed_space = 0;
        
        for (int i = l_mid; i < l_count; i++) {
            uint8_t *l_data;
            size_t l_entry_total_size;
            dap_global_db_btree_leaf_entry_t *l_entry = s_leaf_entry_at(l_child, i, &l_data, &l_entry_total_size);
            
            char *l_text_key = (char *)l_data;
            uint8_t *l_value = l_data + l_entry->key_len;
            uint8_t *l_sign = l_value + l_entry->value_len;
            
            s_leaf_insert_entry(l_sibling, i - l_mid, &l_entry->driver_hash,
                               l_text_key, l_entry->key_len,
                               l_value, l_entry->value_len,
                               l_sign, l_entry->sign_len,
                               l_entry->flags);
            
            if (i == l_mid) {
                l_median_key = l_entry->driver_hash;
            }
            
            // Track freed space (entry + offset)
            l_freed_space += l_entry_total_size + LEAF_OFFSET_SIZE;
        }
        
        // Truncate child and compact: entries remain scattered after removing
        // the upper half, so re-pack them contiguously from the end of the page.
        // Without this, subsequent inserts can compute a negative offset (uint16_t
        // underflow) and write outside the page buffer.
        l_child->header.entries_count = l_mid;
        l_child->header.free_space += l_freed_space;
        s_leaf_compact(l_child);
    } else {
        // Split branch: median key moves to parent, not copied
        dap_global_db_btree_branch_entry_t *l_median = s_branch_entry_at(l_child, l_mid);
        l_median_key = l_median->driver_hash;
        
        // Copy keys and children after median to sibling
        // First child of sibling is the right child of median
        s_branch_set_child(l_sibling, 0, l_median->child_page);
        
        for (int i = l_mid + 1; i < l_count; i++) {
            dap_global_db_btree_branch_entry_t *l_entry = s_branch_entry_at(l_child, i);
            s_branch_insert_entry(l_sibling, i - l_mid - 1, &l_entry->driver_hash, l_entry->child_page);
        }
        
        // Truncate child (remove median and upper half)
        l_child->header.entries_count = l_mid;
    }
    
    l_child->is_dirty = true;
    
    // Insert median key and sibling pointer into parent
    s_branch_insert_entry(a_parent, a_index, &l_median_key, l_sibling_id);
    
    // Write pages
    s_page_write(a_tree, l_child);
    s_page_write(a_tree, l_sibling);
    s_page_write(a_tree, a_parent);
    
    s_page_free(l_child);
    s_page_free(l_sibling);
    
    return 0;
}

static int s_insert_non_full(dap_global_db_btree_t *a_tree, dap_global_db_btree_page_t *a_page,
                             const dap_global_db_btree_key_t *a_key, const char *a_text_key, uint32_t a_text_key_len,
                             const void *a_value, uint32_t a_value_len, const void *a_sign, uint32_t a_sign_len,
                             uint8_t a_flags)
{
    if (a_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF) {
        // Insert into leaf
        int l_index;
        if (s_leaf_find_entry(a_page, a_key, &l_index) == 0) {
            if (s_leaf_update_entry(a_page, l_index, a_text_key, a_text_key_len,
                                   a_value, a_value_len, a_sign, a_sign_len, a_flags) != 0) {
                // Update failed — entry grew beyond page capacity after compact.
                // The old entry was already deleted by s_leaf_update_entry. 
                // The page has space reclaimed but the new value doesn't fit.
                // Write current page state (old entry removed), signal that the
                // caller must re-insert via the normal split path.
                s_page_write(a_tree, a_page);
                a_tree->header.items_count--;  // Will be re-incremented by re-insert
                // Re-do: the key is no longer in the tree; fall through to a fresh
                // insert which will go through the split machinery.
                // We return a special code to trigger re-insert from the top.
                return 1;  // Signal: need re-insert from root (key was removed)
            }
        } else {
            if (s_leaf_insert_entry(a_page, l_index, a_key, a_text_key, a_text_key_len,
                                   a_value, a_value_len, a_sign, a_sign_len, a_flags) != 0) {
                log_it(L_ERROR, "Failed to insert into leaf - no space");
                return -1;
            }
            a_tree->header.items_count++;
        }
        
        s_page_write(a_tree, a_page);

        // Capture leaf as hot_leaf: transfer ownership from caller.
        // The caller (branch handler) checks hot_leaf to skip s_page_free().
        if (a_tree->hot_leaf && a_tree->hot_leaf != a_page)
            s_page_free(a_tree->hot_leaf);
        a_tree->hot_leaf = a_page;
        return 0;
    }
    
    // Branch page - find child and recurse
    bool l_found;
    int l_index = s_search_in_page(a_page, a_key, &l_found);
    
    uint64_t l_child_id = s_branch_get_child(a_page, l_index);
    dap_global_db_btree_page_t *l_child = s_page_read(a_tree, l_child_id);
    if (!l_child)
        return -1;
    
    // Check if child needs split
    if (s_page_needs_split(l_child, a_text_key_len, a_value_len, a_sign_len)) {
        s_page_free(l_child);
        
        if (s_split_child(a_tree, a_page, l_index) != 0)
            return -1;
        
        dap_global_db_btree_branch_entry_t *l_entry = s_branch_entry_at(a_page, l_index);
        if (dap_global_db_btree_key_compare(a_key, &l_entry->driver_hash) > 0) {
            l_index++;
        }
        
        l_child_id = s_branch_get_child(a_page, l_index);
        l_child = s_page_read(a_tree, l_child_id);
        if (!l_child)
            return -1;
    }
    
    int l_ret = s_insert_non_full(a_tree, l_child, a_key, a_text_key, a_text_key_len,
                                  a_value, a_value_len, a_sign, a_sign_len, a_flags);
    
    // If the leaf was captured as hot_leaf, don't free it
    if (a_tree->hot_leaf != l_child)
        s_page_free(l_child);
    return l_ret;
}

// ============================================================================
// Public API - Tree Management
// ============================================================================

dap_global_db_btree_t *dap_global_db_btree_create(const char *a_filepath)
{
    dap_return_val_if_fail(a_filepath, NULL);
    
    int l_fd = open(a_filepath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (l_fd < 0) {
        log_it(L_ERROR, "Failed to create B-tree file %s: %s", a_filepath, strerror(errno));
        return NULL;
    }
    
    dap_global_db_btree_t *l_tree = DAP_NEW_Z(dap_global_db_btree_t);
    if (!l_tree) {
        close(l_fd);
        return NULL;
    }
    
    l_tree->fd = l_fd;
    l_tree->filepath = dap_strdup(a_filepath);
    l_tree->read_only = false;
    
    // Initialize header
    l_tree->header.magic = DAP_GLOBAL_DB_BTREE_MAGIC;
    l_tree->header.version = DAP_GLOBAL_DB_BTREE_VERSION;
    l_tree->header.flags = 0;
    l_tree->header.page_size = DAP_GLOBAL_DB_BTREE_PAGE_SIZE;
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

    // Initialize mmap (pre-allocate 1MB to amortize early growth)
    size_t l_initial_mmap = 1024 * 1024;
    if (l_initial_mmap < (size_t)BTREE_DATA_OFFSET)
        l_initial_mmap = BTREE_DATA_OFFSET;
    l_tree->mmap = dap_mmap_open(a_filepath,
        DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, l_initial_mmap);
    if (l_tree->mmap) {
        dap_mmap_advise(l_tree->mmap, DAP_MMAP_ADVISE_RANDOM);
    }

    debug_if(s_debug_more, L_INFO, "Created B-tree file: %s", a_filepath);
    return l_tree;
}

dap_global_db_btree_t *dap_global_db_btree_open(const char *a_filepath, bool a_read_only)
{
    dap_return_val_if_fail(a_filepath, NULL);
    
    int l_flags = a_read_only ? O_RDONLY : O_RDWR;
    int l_fd = open(a_filepath, l_flags);
    if (l_fd < 0) {
        log_it(L_ERROR, "Failed to open B-tree file %s: %s", a_filepath, strerror(errno));
        return NULL;
    }
    
    dap_global_db_btree_t *l_tree = DAP_NEW_Z(dap_global_db_btree_t);
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

    log_it(L_INFO, "Opened B-tree file: %s (items: %lu, pages: %lu, mmap: %s)",
           a_filepath, (unsigned long)l_tree->header.items_count,
           (unsigned long)l_tree->header.total_pages,
           l_tree->mmap ? "yes" : "no");
    
    return l_tree;
}

void dap_global_db_btree_close(dap_global_db_btree_t *a_tree)
{
    if (!a_tree)
        return;
    
    // Sync and write final header
    if (!a_tree->read_only) {
        dap_global_db_btree_sync(a_tree);
    }
    
    if (a_tree->root)
        s_page_free(a_tree->root);

    if (a_tree->mmap) {
        dap_mmap_close(a_tree->mmap);
        a_tree->mmap = NULL;
    }
    
    if (a_tree->fd >= 0)
        close(a_tree->fd);
    DAP_DEL_Z(a_tree->filepath);
    DAP_DELETE(a_tree);
}

int dap_global_db_btree_sync(dap_global_db_btree_t *a_tree)
{
    dap_return_val_if_fail(a_tree && !a_tree->read_only, -1);

    // Flush hot leaf
    s_hot_leaf_flush(a_tree);
    
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

// ============================================================================
// Public API - Data Operations
// ============================================================================

/**
 * @brief Flush hot leaf to storage and release it.
 * Hot leaf modifications are deferred — if the page is dirty,
 * write it to mmap/disk before freeing.
 */
static void s_hot_leaf_flush(dap_global_db_btree_t *a_tree)
{
    if (!a_tree->hot_leaf)
        return;
    if (a_tree->hot_leaf->is_dirty)
        s_page_write(a_tree, a_tree->hot_leaf);
    s_page_free(a_tree->hot_leaf);
    a_tree->hot_leaf = NULL;
}

int dap_global_db_btree_insert(dap_global_db_btree_t *a_tree,
                         const dap_global_db_btree_key_t *a_key,
                         const char *a_text_key, uint32_t a_text_key_len,
                         const void *a_value, uint32_t a_value_len,
                         const void *a_sign, uint32_t a_sign_len,
                         uint8_t a_flags)
{
    dap_return_val_if_fail(a_tree && a_key && !a_tree->read_only, -1);

    // ---- Hot leaf fast path ------------------------------------------------
    // For sequential inserts, the same leaf is hit repeatedly. Keeping the last
    // written leaf in memory eliminates tree traversal (root + branch reads).
    if (a_tree->hot_leaf && a_tree->hot_leaf->header.entries_count > 0) {
        dap_global_db_btree_page_t *hl = a_tree->hot_leaf;
        dap_global_db_btree_leaf_entry_t *l_last =
            s_leaf_entry_at(hl, hl->header.entries_count - 1, NULL, NULL);
        int l_cmp = dap_global_db_btree_key_compare(a_key, &l_last->driver_hash);

        bool l_in_range = false;
        if (l_cmp > 0) {
            // Key > last entry → only valid for rightmost leaf
            l_in_range = (hl->header.right_sibling == 0);
        } else if (l_cmp == 0) {
            l_in_range = true;  // Update existing
        } else {
            dap_global_db_btree_leaf_entry_t *l_first = s_leaf_entry_at(hl, 0, NULL, NULL);
            l_in_range = (dap_global_db_btree_key_compare(a_key, &l_first->driver_hash) >= 0);
        }

        if (l_in_range && !s_page_needs_split(hl, a_text_key_len, a_value_len, a_sign_len)) {
            int l_idx;
            if (s_leaf_find_entry(hl, a_key, &l_idx) == 0) {
                if (s_leaf_update_entry(hl, l_idx, a_text_key, a_text_key_len,
                                        a_value, a_value_len, a_sign, a_sign_len, a_flags) == 0) {
                    hl->is_dirty = true;
                    // Page write deferred — flushed on eviction or sync
                    return 0;
                }
            } else {
                if (s_leaf_insert_entry(hl, l_idx, a_key, a_text_key, a_text_key_len,
                                        a_value, a_value_len, a_sign, a_sign_len, a_flags) == 0) {
                    a_tree->header.items_count++;
                    hl->is_dirty = true;
                    // Page write deferred — flushed on eviction or sync
                    return 0;
                }
            }
        }
        // Miss — flush hot leaf, proceed normally
        s_hot_leaf_flush(a_tree);
    }

    // ---- Normal path -------------------------------------------------------
    // Empty tree - create root
    if (a_tree->header.root_page == 0) {
        uint64_t l_root_id = s_page_allocate_new(a_tree);
        dap_global_db_btree_page_t *l_root = s_page_alloc();
        if (!l_root)
            return -1;
        
        l_root->header.page_id = l_root_id;
        l_root->header.flags = DAP_GLOBAL_DB_PAGE_LEAF | DAP_GLOBAL_DB_PAGE_ROOT;
        l_root->header.left_sibling = 0;
        
        // Insert first entry
        if (s_leaf_insert_entry(l_root, 0, a_key, a_text_key, a_text_key_len,
                               a_value, a_value_len, a_sign, a_sign_len, a_flags) != 0) {
            s_page_free(l_root);
            return -1;
        }
        
        a_tree->header.root_page = l_root_id;
        a_tree->header.items_count = 1;
        a_tree->header.tree_height = 1;
        
        s_page_write(a_tree, l_root);
        // Capture as hot leaf
        a_tree->hot_leaf = l_root;

        s_header_write(a_tree);
        return 0;
    }
    
    // Read root
    dap_global_db_btree_page_t *l_root = s_page_read(a_tree, a_tree->header.root_page);
    if (!l_root)
        return -1;
    
    // Check if root needs split
    if (s_page_needs_split(l_root, a_text_key_len, a_value_len, a_sign_len)) {
        uint64_t l_new_root_id = s_page_allocate_new(a_tree);
        dap_global_db_btree_page_t *l_new_root = s_page_alloc();
        if (!l_new_root) {
            s_page_free(l_root);
            return -1;
        }
        
        l_new_root->header.page_id = l_new_root_id;
        l_new_root->header.flags = DAP_GLOBAL_DB_PAGE_BRANCH | DAP_GLOBAL_DB_PAGE_ROOT;
        l_new_root->header.entries_count = 0;
        
        l_root->header.flags &= ~DAP_GLOBAL_DB_PAGE_ROOT;
        s_branch_set_child(l_new_root, 0, a_tree->header.root_page);
        
        s_page_write(a_tree, l_root);
        s_page_free(l_root);
        
        if (s_split_child(a_tree, l_new_root, 0) != 0) {
            s_page_free(l_new_root);
            return -1;
        }
        
        a_tree->header.root_page = l_new_root_id;
        a_tree->header.tree_height++;
        
        int l_ret = s_insert_non_full(a_tree, l_new_root, a_key, a_text_key, a_text_key_len,
                                      a_value, a_value_len, a_sign, a_sign_len, a_flags);
        // Don't free if captured as hot_leaf
        if (a_tree->hot_leaf != l_new_root)
            s_page_free(l_new_root);
        s_header_write(a_tree);
        return l_ret;
    }
    
    int l_ret = s_insert_non_full(a_tree, l_root, a_key, a_text_key, a_text_key_len,
                                  a_value, a_value_len, a_sign, a_sign_len, a_flags);
    if (a_tree->hot_leaf != l_root)
        s_page_free(l_root);

    // Return code 1 from s_insert_non_full means: an existing entry was deleted
    // because it grew beyond page capacity, and now the key needs to be re-inserted
    // from the root (will go through the normal split path). Recurse once.
    if (l_ret == 1) {
        s_hot_leaf_flush(a_tree);
        return dap_global_db_btree_insert(a_tree, a_key, a_text_key, a_text_key_len,
                                           a_value, a_value_len, a_sign, a_sign_len, a_flags);
    }
    
    return l_ret;
}

/**
 * @brief Add a page to the free list for later reuse by s_page_allocate_new().
 */
static void s_page_add_to_free_list(dap_global_db_btree_t *a_tree, uint64_t a_page_id)
{
    // Free list: page data starts with a uint64_t "next" pointer.
    dap_global_db_btree_page_t *l_page = s_page_alloc();
    if (!l_page) return;
    l_page->header.page_id = a_page_id;
    l_page->header.flags = 0;
    l_page->header.entries_count = 0;
    *(uint64_t *)l_page->data = a_tree->header.free_list_head;
    s_page_write(a_tree, l_page);
    s_page_free(l_page);
    a_tree->header.free_list_head = a_page_id;
}

/**
 * @brief Remove a separator entry from a branch page at a given index,
 *        collapsing child pointers accordingly.
 *
 * After removal, the child at (a_index + 1) is removed (the right side
 * of the separator). The child at a_index is preserved.
 */
static void s_branch_remove_entry(dap_global_db_btree_page_t *a_page, int a_index)
{
    s_page_cow(a_page);
    int l_count = a_page->header.entries_count;
    dap_global_db_btree_branch_entry_t *l_entries =
        (dap_global_db_btree_branch_entry_t *)(a_page->data + sizeof(uint64_t));

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
static int s_leaf_merge_into(dap_global_db_btree_page_t *a_dst,
                             dap_global_db_btree_page_t *a_src)
{
    int l_src_count = a_src->header.entries_count;
    for (int i = 0; i < l_src_count; i++) {
        uint8_t *l_data;
        size_t l_total;
        dap_global_db_btree_leaf_entry_t *l_entry = s_leaf_entry_at(a_src, i, &l_data, &l_total);
        if (!l_entry) return -1;

        int l_ins_idx;
        s_leaf_find_entry(a_dst, &l_entry->driver_hash, &l_ins_idx);
        if (s_leaf_insert_entry(a_dst, l_ins_idx, &l_entry->driver_hash,
                                (char *)l_data, l_entry->key_len,
                                l_data + l_entry->key_len, l_entry->value_len,
                                l_data + l_entry->key_len + l_entry->value_len,
                                l_entry->sign_len, l_entry->flags) != 0) {
            return -1;  // No space
        }
    }
    return 0;
}

/**
 * @brief Calculate total byte size of all entries in a leaf page.
 */
static size_t s_leaf_total_entry_bytes(dap_global_db_btree_page_t *a_page)
{
    size_t l_total = 0;
    int l_count = a_page->header.entries_count;
    for (int i = 0; i < l_count; i++) {
        size_t l_size;
        s_leaf_entry_at(a_page, i, NULL, &l_size);
        l_total += l_size + LEAF_OFFSET_SIZE;
    }
    return l_total;
}

// Maximum depth for parent path tracking during delete
#define BTREE_PATH_MAX_DEPTH  32

typedef struct {
    uint64_t page_id;
    int      child_index;
} s_path_entry_t;

int dap_global_db_btree_delete(dap_global_db_btree_t *a_tree, const dap_global_db_btree_key_t *a_key)
{
    dap_return_val_if_fail(a_tree && a_key && !a_tree->read_only, -1);

    // Invalidate hot leaf — delete may affect its page
    s_hot_leaf_flush(a_tree);
    
    if (a_tree->header.root_page == 0)
        return 1;  // Not found - empty tree
    
    // Navigate to leaf, recording the parent path for rebalancing
    s_path_entry_t l_path[BTREE_PATH_MAX_DEPTH];
    int l_depth = 0;

    dap_global_db_btree_page_t *l_page = s_page_read(a_tree, a_tree->header.root_page);
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
        s_page_free(l_page);
        
        l_page = s_page_read(a_tree, l_child_id);
        if (!l_page)
            return -1;
    }
    
    // Find entry in leaf
    int l_index;
    if (s_leaf_find_entry(l_page, a_key, &l_index) != 0) {
        s_page_free(l_page);
        return 1;  // Not found
    }
    
    // Delete entry (compacts page and reclaims space)
    if (s_leaf_delete_entry(l_page, l_index) != 0) {
        s_page_free(l_page);
        return -1;
    }
    
    a_tree->header.items_count--;

    // ---- Leaf rebalancing after deletion ------------------------------------
    // If the leaf is empty (or critically underflowing) and is not the root,
    // try to merge it with a sibling to reclaim the page and keep the tree
    // compact. This prevents unbounded growth of nearly-empty pages after
    // repeated delete operations.
    bool l_is_root = (l_page->header.flags & DAP_GLOBAL_DB_PAGE_ROOT) != 0;

    if (!l_is_root && l_page->header.entries_count < DAP_GLOBAL_DB_BTREE_MIN_KEYS && l_depth > 0) {
        // Determine available siblings and whether merging is feasible
        uint64_t l_right_id = l_page->header.right_sibling;
        uint64_t l_left_id  = l_page->header.left_sibling;

        // Compute how much space our entries consume
        size_t l_our_bytes = s_leaf_total_entry_bytes(l_page);
        int l_our_count = l_page->header.entries_count;

        // Read parent for separator management
        s_path_entry_t *l_parent_path = &l_path[l_depth - 1];
        dap_global_db_btree_page_t *l_parent = s_page_read(a_tree, l_parent_path->page_id);

        bool l_merged = false;

        // Try merge with right sibling first
        if (!l_merged && l_right_id != 0 && l_parent) {
            dap_global_db_btree_page_t *l_right = s_page_read(a_tree, l_right_id);
            if (l_right) {
                size_t l_right_bytes = s_leaf_total_entry_bytes(l_right);
                size_t l_combined = l_our_bytes + l_right_bytes;
                // Check if combined entries fit in one page
                size_t l_offsets_space = LEAF_HEADER_SIZE +
                    (l_our_count + l_right->header.entries_count) * LEAF_OFFSET_SIZE;
                if (l_offsets_space + l_combined <= PAGE_DATA_SIZE) {
                    // Merge: move our entries into right sibling, then remove our page
                    if (s_leaf_merge_into(l_right, l_page) == 0) {
                        // Update sibling chain
                        l_right->header.left_sibling = l_page->header.left_sibling;
                        s_page_write(a_tree, l_right);

                        // Update left neighbor's right_sibling
                        if (l_left_id != 0) {
                            dap_global_db_btree_page_t *l_left = s_page_read(a_tree, l_left_id);
                            if (l_left) {
                                l_left->header.right_sibling = l_right_id;
                                s_page_write(a_tree, l_left);
                                s_page_free(l_left);
                            }
                        }

                        // Remove separator from parent
                        int l_sep_idx = l_parent_path->child_index;
                        // We're the left child; the separator at sep_idx points to right child.
                        // After removing, the right child inherits our slot.
                        if (l_sep_idx < l_parent->header.entries_count) {
                            // Replace the child pointer: child[sep_idx] now points to right
                            s_branch_set_child(l_parent, l_sep_idx, l_right_id);
                            s_branch_remove_entry(l_parent, l_sep_idx);
                        } else if (l_sep_idx > 0) {
                            // We're the rightmost child; remove the last separator
                            s_branch_remove_entry(l_parent, l_sep_idx - 1);
                        }
                        s_page_write(a_tree, l_parent);

                        // Free our page
                        s_page_add_to_free_list(a_tree, l_page->header.page_id);
                        l_merged = true;
                    }
                }
                s_page_free(l_right);
            }
        }

        // Try merge with left sibling
        if (!l_merged && l_left_id != 0 && l_parent) {
            dap_global_db_btree_page_t *l_left = s_page_read(a_tree, l_left_id);
            if (l_left) {
                size_t l_left_bytes = s_leaf_total_entry_bytes(l_left);
                size_t l_combined = l_our_bytes + l_left_bytes;
                size_t l_offsets_space = LEAF_HEADER_SIZE +
                    (l_our_count + l_left->header.entries_count) * LEAF_OFFSET_SIZE;
                if (l_offsets_space + l_combined <= PAGE_DATA_SIZE) {
                    // Merge: move our entries into left sibling, then remove our page
                    if (s_leaf_merge_into(l_left, l_page) == 0) {
                        // Update sibling chain
                        l_left->header.right_sibling = l_page->header.right_sibling;
                        s_page_write(a_tree, l_left);

                        // Update right neighbor's left_sibling
                        if (l_right_id != 0) {
                            dap_global_db_btree_page_t *l_right = s_page_read(a_tree, l_right_id);
                            if (l_right) {
                                l_right->header.left_sibling = l_left_id;
                                s_page_write(a_tree, l_right);
                                s_page_free(l_right);
                            }
                        }

                        // Remove separator from parent
                        int l_sep_idx = l_parent_path->child_index;
                        if (l_sep_idx > 0) {
                            s_branch_remove_entry(l_parent, l_sep_idx - 1);
                        } else if (l_parent->header.entries_count > 0) {
                            // We're child[0]; after removing, child[0] = left sibling
                            s_branch_set_child(l_parent, 0, l_left_id);
                            s_branch_remove_entry(l_parent, 0);
                        }
                        s_page_write(a_tree, l_parent);

                        // Free our page
                        s_page_add_to_free_list(a_tree, l_page->header.page_id);
                        l_merged = true;
                    }
                }
                s_page_free(l_left);
            }
        }

        // Collapse root if it became empty (single child remaining)
        if (l_parent && (l_parent->header.flags & DAP_GLOBAL_DB_PAGE_ROOT) &&
            l_parent->header.entries_count == 0) {
            // Root has no separators, just one child — that child becomes new root
            uint64_t l_new_root_id = s_branch_get_child(l_parent, 0);
            if (l_new_root_id != 0) {
                dap_global_db_btree_page_t *l_new_root = s_page_read(a_tree, l_new_root_id);
                if (l_new_root) {
                    l_new_root->header.flags |= DAP_GLOBAL_DB_PAGE_ROOT;
                    s_page_write(a_tree, l_new_root);
                    s_page_free(l_new_root);
                }
                s_page_add_to_free_list(a_tree, l_parent->header.page_id);
                a_tree->header.root_page = l_new_root_id;
                a_tree->header.tree_height--;
            }
        }

        if (l_parent)
            s_page_free(l_parent);

        if (l_merged) {
            // Page was freed, don't write/free it again
            s_page_free(l_page);
            s_header_write(a_tree);
            return 0;
        }
    }

    // If the root leaf became empty, reset the tree
    if (l_is_root && l_page->header.entries_count == 0) {
        s_page_add_to_free_list(a_tree, l_page->header.page_id);
        s_page_free(l_page);
        a_tree->header.root_page = 0;
        a_tree->header.tree_height = 0;
        s_header_write(a_tree);
        return 0;
    }
    
    s_page_write(a_tree, l_page);
    s_page_free(l_page);
    s_header_write(a_tree);
    
    return 0;
}

int dap_global_db_btree_get(dap_global_db_btree_t *a_tree,
                      const dap_global_db_btree_key_t *a_key,
                      char **a_out_text_key,
                      void **a_out_value, uint32_t *a_out_value_len,
                      void **a_out_sign, uint32_t *a_out_sign_len,
                      uint8_t *a_out_flags)
{
    dap_return_val_if_fail(a_tree && a_key, -1);
    
    if (a_tree->header.root_page == 0)
        return 1;  // Not found

    // Flush dirty hot leaf so mmap reads see current data
    if (a_tree->hot_leaf && a_tree->hot_leaf->is_dirty)
        s_hot_leaf_flush(a_tree);

    // Zero-copy fast path: read-only traversal, no page allocation → mmap refs safe
    dap_global_db_btree_page_t l_page_buf;
    if (s_page_read_ref(a_tree, a_tree->header.root_page, &l_page_buf)) {
        dap_global_db_btree_page_t *l_page = &l_page_buf;
        while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
            bool l_found;
            int l_index = s_search_in_page(l_page, a_key, &l_found);
            uint64_t l_child_id = s_branch_get_child(l_page, l_index);
            // No free needed — stack page with mmap ref
            if (!s_page_read_ref(a_tree, l_child_id, &l_page_buf))
                return -1;
        }

        int l_index;
        if (s_leaf_find_entry(l_page, a_key, &l_index) != 0)
            return 1;  // Not found

        uint8_t *l_data = NULL;
        dap_global_db_btree_leaf_entry_t *l_entry = s_leaf_entry_at(l_page, l_index, &l_data, NULL);

        if (a_out_text_key && l_entry->key_len > 0) {
            *a_out_text_key = DAP_NEW_Z_SIZE(char, l_entry->key_len);
            memcpy(*a_out_text_key, l_data, l_entry->key_len);
        } else if (a_out_text_key) {
            *a_out_text_key = NULL;
        }

        if (a_out_value && l_entry->value_len > 0) {
            *a_out_value = DAP_NEW_Z_SIZE(uint8_t, l_entry->value_len);
            memcpy(*a_out_value, l_data + l_entry->key_len, l_entry->value_len);
        } else if (a_out_value) {
            *a_out_value = NULL;
        }

        if (a_out_value_len)
            *a_out_value_len = l_entry->value_len;

        if (a_out_sign && l_entry->sign_len > 0) {
            *a_out_sign = DAP_NEW_Z_SIZE(uint8_t, l_entry->sign_len);
            memcpy(*a_out_sign, l_data + l_entry->key_len + l_entry->value_len, l_entry->sign_len);
        } else if (a_out_sign) {
            *a_out_sign = NULL;
        }

        if (a_out_sign_len)
            *a_out_sign_len = l_entry->sign_len;

        if (a_out_flags)
            *a_out_flags = l_entry->flags;

        return 0;
    }

    // Fallback: heap-allocated page read (no mmap)
    dap_global_db_btree_page_t *l_page = s_page_read(a_tree, a_tree->header.root_page);
    if (!l_page)
        return -1;
    
    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        bool l_found;
        int l_index = s_search_in_page(l_page, a_key, &l_found);
        uint64_t l_child_id = s_branch_get_child(l_page, l_index);
        s_page_free(l_page);
        
        l_page = s_page_read(a_tree, l_child_id);
        if (!l_page)
            return -1;
    }
    
    int l_index;
    if (s_leaf_find_entry(l_page, a_key, &l_index) != 0) {
        s_page_free(l_page);
        return 1;
    }
    
    uint8_t *l_data = NULL;
    dap_global_db_btree_leaf_entry_t *l_entry = s_leaf_entry_at(l_page, l_index, &l_data, NULL);
    
    if (a_out_text_key && l_entry->key_len > 0) {
        *a_out_text_key = DAP_NEW_Z_SIZE(char, l_entry->key_len);
        memcpy(*a_out_text_key, l_data, l_entry->key_len);
    } else if (a_out_text_key) {
        *a_out_text_key = NULL;
    }
    
    if (a_out_value && l_entry->value_len > 0) {
        *a_out_value = DAP_NEW_Z_SIZE(uint8_t, l_entry->value_len);
        memcpy(*a_out_value, l_data + l_entry->key_len, l_entry->value_len);
    } else if (a_out_value) {
        *a_out_value = NULL;
    }
    
    if (a_out_value_len)
        *a_out_value_len = l_entry->value_len;
    
    if (a_out_sign && l_entry->sign_len > 0) {
        *a_out_sign = DAP_NEW_Z_SIZE(uint8_t, l_entry->sign_len);
        memcpy(*a_out_sign, l_data + l_entry->key_len + l_entry->value_len, l_entry->sign_len);
    } else if (a_out_sign) {
        *a_out_sign = NULL;
    }
    
    if (a_out_sign_len)
        *a_out_sign_len = l_entry->sign_len;
    
    if (a_out_flags)
        *a_out_flags = l_entry->flags;
    
    s_page_free(l_page);
    return 0;
}

bool dap_global_db_btree_exists(dap_global_db_btree_t *a_tree, const dap_global_db_btree_key_t *a_key)
{
    dap_return_val_if_fail(a_tree && a_key, false);
    
    if (a_tree->header.root_page == 0)
        return false;

    // Flush dirty hot leaf so mmap reads see current data
    if (a_tree->hot_leaf && a_tree->hot_leaf->is_dirty)
        s_hot_leaf_flush(a_tree);

    // Zero-copy fast path
    dap_global_db_btree_page_t l_page_buf;
    if (s_page_read_ref(a_tree, a_tree->header.root_page, &l_page_buf)) {
        while (!(l_page_buf.header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
            bool l_found;
            int l_index = s_search_in_page(&l_page_buf, a_key, &l_found);
            uint64_t l_child_id = s_branch_get_child(&l_page_buf, l_index);
            if (!s_page_read_ref(a_tree, l_child_id, &l_page_buf))
                return false;
        }
        int l_index;
        return s_leaf_find_entry(&l_page_buf, a_key, &l_index) == 0;
    }

    // Fallback
    dap_global_db_btree_page_t *l_page = s_page_read(a_tree, a_tree->header.root_page);
    if (!l_page)
        return false;
    
    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        bool l_found;
        int l_index = s_search_in_page(l_page, a_key, &l_found);
        uint64_t l_child_id = s_branch_get_child(l_page, l_index);
        s_page_free(l_page);
        
        l_page = s_page_read(a_tree, l_child_id);
        if (!l_page)
            return false;
    }
    
    int l_index;
    bool l_exists = (s_leaf_find_entry(l_page, a_key, &l_index) == 0);
    
    s_page_free(l_page);
    return l_exists;
}

uint64_t dap_global_db_btree_count(dap_global_db_btree_t *a_tree)
{
    dap_return_val_if_fail(a_tree, 0);
    return a_tree->header.items_count;
}

int dap_global_db_btree_clear(dap_global_db_btree_t *a_tree)
{
    dap_return_val_if_fail(a_tree && !a_tree->read_only, -1);
    
    // Free hot leaf and cached root
    s_hot_leaf_flush(a_tree);
    if (a_tree->root) {
        s_page_free(a_tree->root);
        a_tree->root = NULL;
    }
    // Reset header
    a_tree->header.root_page = 0;
    a_tree->header.total_pages = 0;
    a_tree->header.items_count = 0;
    a_tree->header.tree_height = 0;
    a_tree->header.free_list_head = 0;
    
    // Truncate file (closes+reopens mmap to shrink)
    if (a_tree->mmap) {
        dap_mmap_close(a_tree->mmap);
        a_tree->mmap = NULL;
    }
    if (ftruncate(a_tree->fd, BTREE_DATA_OFFSET) != 0) {
        log_it(L_WARNING, "Failed to truncate file: %s", strerror(errno));
    }
    // Re-open mmap
    size_t l_initial = 1024 * 1024;
    a_tree->mmap = dap_mmap_open(a_tree->filepath,
        DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, l_initial);
    if (a_tree->mmap)
        dap_mmap_advise(a_tree->mmap, DAP_MMAP_ADVISE_RANDOM);

    return s_header_write(a_tree);
}

// ============================================================================
// Cursor Operations
// ============================================================================

dap_global_db_btree_cursor_t *dap_global_db_btree_cursor_create(dap_global_db_btree_t *a_tree)
{
    dap_return_val_if_fail(a_tree, NULL);
    
    dap_global_db_btree_cursor_t *l_cursor = DAP_NEW_Z(dap_global_db_btree_cursor_t);
    if (!l_cursor)
        return NULL;
    
    l_cursor->tree = a_tree;
    l_cursor->valid = false;
    l_cursor->at_end = false;
    
    return l_cursor;
}

void dap_global_db_btree_cursor_close(dap_global_db_btree_cursor_t *a_cursor)
{
    if (!a_cursor)
        return;
    
    if (a_cursor->current_page)
        s_page_free(a_cursor->current_page);
    
    DAP_DELETE(a_cursor);
}

/**
 * @brief Find leftmost leaf page
 */
static dap_global_db_btree_page_t *s_find_leftmost_leaf(dap_global_db_btree_t *a_tree)
{
    if (a_tree->header.root_page == 0)
        return NULL;
    
    dap_global_db_btree_page_t *l_page = s_page_read(a_tree, a_tree->header.root_page);
    if (!l_page)
        return NULL;
    
    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        uint64_t l_child_id = s_branch_get_child(l_page, 0);
        s_page_free(l_page);
        
        l_page = s_page_read(a_tree, l_child_id);
        if (!l_page)
            return NULL;
    }
    
    return l_page;
}

/**
 * @brief Find rightmost leaf page
 */
static dap_global_db_btree_page_t *s_find_rightmost_leaf(dap_global_db_btree_t *a_tree)
{
    if (a_tree->header.root_page == 0)
        return NULL;
    
    dap_global_db_btree_page_t *l_page = s_page_read(a_tree, a_tree->header.root_page);
    if (!l_page)
        return NULL;
    
    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        int l_count = l_page->header.entries_count;
        uint64_t l_child_id = s_branch_get_child(l_page, l_count);
        s_page_free(l_page);
        
        l_page = s_page_read(a_tree, l_child_id);
        if (!l_page)
            return NULL;
    }
    
    return l_page;
}

/**
 * @brief Find leaf page containing key (or insertion point)
 */
static dap_global_db_btree_page_t *s_find_leaf_for_key(dap_global_db_btree_t *a_tree, const dap_global_db_btree_key_t *a_key)
{
    if (a_tree->header.root_page == 0)
        return NULL;
    
    dap_global_db_btree_page_t *l_page = s_page_read(a_tree, a_tree->header.root_page);
    if (!l_page)
        return NULL;
    
    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        bool l_found;
        int l_index = s_search_in_page(l_page, a_key, &l_found);
        uint64_t l_child_id = s_branch_get_child(l_page, l_index);
        s_page_free(l_page);
        
        l_page = s_page_read(a_tree, l_child_id);
        if (!l_page)
            return NULL;
    }
    
    return l_page;
}

int dap_global_db_btree_cursor_move(dap_global_db_btree_cursor_t *a_cursor,
                              dap_global_db_btree_cursor_op_t a_op,
                              const dap_global_db_btree_key_t *a_key)
{
    dap_return_val_if_fail(a_cursor && a_cursor->tree, -1);
    
    dap_global_db_btree_t *l_tree = a_cursor->tree;

    // Flush dirty hot leaf so cursor reads see current data
    if (l_tree->hot_leaf && l_tree->hot_leaf->is_dirty)
        s_hot_leaf_flush(l_tree);
    
    // NEXT and PREV operate on the existing position — don't free current_page
    if (a_op == DAP_GLOBAL_DB_BTREE_NEXT || a_op == DAP_GLOBAL_DB_BTREE_PREV) {
        if (!a_cursor->valid || !a_cursor->current_page)
            return -1;
        
        if (a_op == DAP_GLOBAL_DB_BTREE_NEXT) {
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
                if (l_offset + DAP_GLOBAL_DB_BTREE_PAGE_SIZE <= dap_mmap_get_size(l_tree->mmap)) {
                    uint8_t *l_src = (uint8_t *)dap_mmap_get_ptr(l_tree->mmap) + l_offset;
                    memcpy(&a_cursor->current_page->header, l_src, sizeof(dap_global_db_btree_page_header_t));
                    memcpy(a_cursor->current_page->data, l_src + sizeof(dap_global_db_btree_page_header_t), PAGE_DATA_SIZE);
                    a_cursor->current_page->is_dirty = false;
                    a_cursor->current_page->is_mmap_ref = false;
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
            a_cursor->current_page = s_page_read(l_tree, l_next_id);
            if (!a_cursor->current_page || a_cursor->current_page->header.entries_count == 0) {
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
                if (l_offset + DAP_GLOBAL_DB_BTREE_PAGE_SIZE <= dap_mmap_get_size(l_tree->mmap)) {
                    uint8_t *l_src = (uint8_t *)dap_mmap_get_ptr(l_tree->mmap) + l_offset;
                    memcpy(&a_cursor->current_page->header, l_src, sizeof(dap_global_db_btree_page_header_t));
                    memcpy(a_cursor->current_page->data, l_src + sizeof(dap_global_db_btree_page_header_t), PAGE_DATA_SIZE);
                    a_cursor->current_page->is_dirty = false;
                    a_cursor->current_page->is_mmap_ref = false;
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
            a_cursor->current_page = s_page_read(l_tree, l_prev_id);
            if (!a_cursor->current_page || a_cursor->current_page->header.entries_count == 0) {
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
    case DAP_GLOBAL_DB_BTREE_FIRST:
        a_cursor->current_page = s_find_leftmost_leaf(l_tree);
        if (a_cursor->current_page && a_cursor->current_page->header.entries_count > 0) {
            a_cursor->current_index = 0;
            a_cursor->valid = true;
        } else {
            a_cursor->at_end = true;
        }
        break;
        
    case DAP_GLOBAL_DB_BTREE_LAST:
        a_cursor->current_page = s_find_rightmost_leaf(l_tree);
        if (a_cursor->current_page && a_cursor->current_page->header.entries_count > 0) {
            a_cursor->current_index = a_cursor->current_page->header.entries_count - 1;
            a_cursor->valid = true;
        } else {
            a_cursor->at_end = true;
        }
        break;
        
    case DAP_GLOBAL_DB_BTREE_NEXT:
    case DAP_GLOBAL_DB_BTREE_PREV:
        // Already handled above — unreachable
        return -1;
        
    case DAP_GLOBAL_DB_BTREE_SET:
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
        
    case DAP_GLOBAL_DB_BTREE_SET_RANGE:
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
                    a_cursor->current_page = s_page_read(l_tree, l_next_id);
                    if (a_cursor->current_page && a_cursor->current_page->header.entries_count > 0) {
                        a_cursor->current_index = 0;
                        a_cursor->valid = true;
                    } else {
                        a_cursor->at_end = true;
                    }
                } else {
                    a_cursor->at_end = true;
                }
            }
        }
        break;
        
    case DAP_GLOBAL_DB_BTREE_SET_UPPERBOUND:
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
                    a_cursor->current_page = s_page_read(l_tree, l_next_id);
                    if (a_cursor->current_page && a_cursor->current_page->header.entries_count > 0) {
                        a_cursor->current_index = 0;
                        a_cursor->valid = true;
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

int dap_global_db_btree_cursor_get(dap_global_db_btree_cursor_t *a_cursor,
                             dap_global_db_btree_key_t *a_out_key,
                             char **a_out_text_key,
                             void **a_out_value, uint32_t *a_out_value_len,
                             void **a_out_sign, uint32_t *a_out_sign_len,
                             uint8_t *a_out_flags)
{
    dap_return_val_if_fail(a_cursor, -1);
    
    if (!a_cursor->valid || !a_cursor->current_page)
        return 1;  // Invalid cursor
    
    uint8_t *l_data;
    dap_global_db_btree_leaf_entry_t *l_entry = s_leaf_entry_at(a_cursor->current_page, 
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
    
    if (a_out_value && l_entry->value_len > 0) {
        *a_out_value = DAP_NEW_Z_SIZE(uint8_t, l_entry->value_len);
        memcpy(*a_out_value, l_data + l_entry->key_len, l_entry->value_len);
    } else if (a_out_value) {
        *a_out_value = NULL;
    }
    
    if (a_out_value_len)
        *a_out_value_len = l_entry->value_len;
    
    if (a_out_sign && l_entry->sign_len > 0) {
        *a_out_sign = DAP_NEW_Z_SIZE(uint8_t, l_entry->sign_len);
        memcpy(*a_out_sign, l_data + l_entry->key_len + l_entry->value_len, l_entry->sign_len);
    } else if (a_out_sign) {
        *a_out_sign = NULL;
    }
    
    if (a_out_sign_len)
        *a_out_sign_len = l_entry->sign_len;
    
    if (a_out_flags)
        *a_out_flags = l_entry->flags;
    
    return 0;
}

bool dap_global_db_btree_cursor_valid(dap_global_db_btree_cursor_t *a_cursor)
{
    return a_cursor && a_cursor->valid;
}
