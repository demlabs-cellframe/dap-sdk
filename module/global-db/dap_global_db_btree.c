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

// ============================================================================
// Internal Structures  
// ============================================================================

/**
 * @brief In-memory page cache entry
 */
typedef struct page_cache_entry {
    uint64_t page_id;
    dap_global_db_btree_page_t *page;
    struct page_cache_entry *next;
    struct page_cache_entry *prev;
} page_cache_entry_t;

/**
 * @brief Page cache (simple LRU)
 */
typedef struct page_cache {
    page_cache_entry_t *head;
    page_cache_entry_t *tail;
    size_t count;
    size_t max_count;
} page_cache_t;

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
    if (lseek(a_tree->fd, BTREE_HEADER_OFFSET, SEEK_SET) < 0) {
        log_it(L_ERROR, "Failed to seek to header: %s", strerror(errno));
        return -1;
    }
    
    ssize_t l_read = read(a_tree->fd, &a_tree->header, sizeof(dap_global_db_btree_header_t));
    if (l_read != sizeof(dap_global_db_btree_header_t)) {
        log_it(L_ERROR, "Failed to read header: %s", strerror(errno));
        return -1;
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
    DAP_DEL_Z(a_page->data);
    DAP_DELETE(a_page);
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
    if (l_read != sizeof(dap_global_db_btree_page_header_t)) {
        log_it(L_ERROR, "Failed to read page header: %s", strerror(errno));
        s_page_free(l_page);
        return NULL;
    }
    
    // Read page data
    l_read = read(a_tree->fd, l_page->data, PAGE_DATA_SIZE);
    if (l_read != PAGE_DATA_SIZE) {
        log_it(L_ERROR, "Failed to read page data: %s", strerror(errno));
        s_page_free(l_page);
        return NULL;
    }
    
    l_page->is_dirty = false;
    return l_page;
}

static int s_page_write(dap_global_db_btree_t *a_tree, dap_global_db_btree_page_t *a_page)
{
    if (!a_page || a_tree->read_only)
        return -1;
    
    uint64_t l_offset = s_page_offset(a_page->header.page_id);
    if (lseek(a_tree->fd, l_offset, SEEK_SET) < 0) {
        log_it(L_ERROR, "Failed to seek to page %lu: %s", a_page->header.page_id, strerror(errno));
        return -1;
    }
    
    // Write page header
    ssize_t l_written = write(a_tree->fd, &a_page->header, sizeof(dap_global_db_btree_page_header_t));
    if (l_written != sizeof(dap_global_db_btree_page_header_t)) {
        log_it(L_ERROR, "Failed to write page header: %s", strerror(errno));
        return -1;
    }
    
    // Write page data
    l_written = write(a_tree->fd, a_page->data, PAGE_DATA_SIZE);
    if (l_written != PAGE_DATA_SIZE) {
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
    return a_tree->header.total_pages;
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
    dap_global_db_btree_branch_entry_t *l_entry = s_branch_entry_at(a_page, a_index);
    if (l_entry) {
        l_entry->driver_hash = *a_key;
        l_entry->child_page = a_child;
        a_page->is_dirty = true;
    }
}

static void s_branch_insert_entry(dap_global_db_btree_page_t *a_page, int a_index, const dap_global_db_btree_key_t *a_key, uint64_t a_child)
{
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
    // For simplicity, we delete and re-insert
    // This is not optimal but keeps code simple
    
    uint8_t *l_old_data;
    size_t l_old_size;
    dap_global_db_btree_leaf_entry_t *l_old_entry = s_leaf_entry_at(a_page, a_index, &l_old_data, &l_old_size);
    if (!l_old_entry)
        return -1;
    
    dap_global_db_btree_key_t l_key = l_old_entry->driver_hash;
    
    // Check if new size fits
    size_t l_new_size = s_leaf_entry_total_size(a_text_key_len, a_value_len, a_sign_len);
    
    if (l_new_size <= l_old_size) {
        // Update in place
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
    
    // Need to delete and re-insert (page compaction needed for larger updates)
    // For now, return error if entry grew - caller should handle page split
    return -1;
}

/**
 * @brief Delete entry from leaf page at given index
 */
static int s_leaf_delete_entry(dap_global_db_btree_page_t *a_page, int a_index)
{
    int l_count = a_page->header.entries_count;
    if (a_index < 0 || a_index >= l_count)
        return -1;
    
    // Get entry size
    size_t l_entry_size;
    s_leaf_entry_at(a_page, a_index, NULL, &l_entry_size);
    
    // Shift offsets
    uint16_t *l_offsets = (uint16_t *)(a_page->data + LEAF_HEADER_SIZE);
    for (int i = a_index; i < l_count - 1; i++) {
        l_offsets[i] = l_offsets[i + 1];
    }
    
    // Note: We don't actually reclaim the entry space here (would need compaction)
    // The free_space is not increased - this is a simplification
    // For production, implement page compaction
    
    a_page->header.entries_count--;
    a_page->is_dirty = true;
    
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
    l_sibling->header.parent = a_parent->header.page_id;
    l_sibling->header.right_sibling = l_child->header.right_sibling;
    l_child->header.right_sibling = l_sibling_id;
    
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
        
        // Truncate child and restore free space
        l_child->header.entries_count = l_mid;
        l_child->header.free_space += l_freed_space;
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
            // Key exists - update
            if (s_leaf_update_entry(a_page, l_index, a_text_key, a_text_key_len,
                                   a_value, a_value_len, a_sign, a_sign_len, a_flags) != 0) {
                // Update failed (entry grew) - would need split, return error
                log_it(L_ERROR, "Entry update requires split - not implemented");
                return -1;
            }
        } else {
            // New key - insert
            if (s_leaf_insert_entry(a_page, l_index, a_key, a_text_key, a_text_key_len,
                                   a_value, a_value_len, a_sign, a_sign_len, a_flags) != 0) {
                log_it(L_ERROR, "Failed to insert into leaf - no space");
                return -1;
            }
            a_tree->header.items_count++;
        }
        
        s_page_write(a_tree, a_page);
        return 0;
    }
    
    // Branch page - find child and recurse
    bool l_found;
    int l_index = s_search_in_page(a_page, a_key, &l_found);
    
    uint64_t l_child_id = s_branch_get_child(a_page, l_index);
    dap_global_db_btree_page_t *l_child = s_page_read(a_tree, l_child_id);
    if (!l_child)
        return -1;
    
    // Check if child needs split (checks free space for leaf, entry count for branch)
    if (s_page_needs_split(l_child, a_text_key_len, a_value_len, a_sign_len)) {
        s_page_free(l_child);
        
        // Split child first
        if (s_split_child(a_tree, a_page, l_index) != 0)
            return -1;
        
        // Determine which child to use after split
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
    
    log_it(L_INFO, "Created B-tree file: %s", a_filepath);
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
    
    // Read and validate header
    if (s_header_read(l_tree) != 0) {
        close(l_fd);
        DAP_DEL_Z(l_tree->filepath);
        DAP_DELETE(l_tree);
        return NULL;
    }
    
    log_it(L_INFO, "Opened B-tree file: %s (items: %lu, pages: %lu)",
           a_filepath, l_tree->header.items_count, l_tree->header.total_pages);
    
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
    
    close(a_tree->fd);
    DAP_DEL_Z(a_tree->filepath);
    DAP_DELETE(a_tree);
}

int dap_global_db_btree_sync(dap_global_db_btree_t *a_tree)
{
    dap_return_val_if_fail(a_tree && !a_tree->read_only, -1);
    
    // Write header
    if (s_header_write(a_tree) != 0)
        return -1;
    
    // Sync to disk
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

int dap_global_db_btree_insert(dap_global_db_btree_t *a_tree,
                         const dap_global_db_btree_key_t *a_key,
                         const char *a_text_key, uint32_t a_text_key_len,
                         const void *a_value, uint32_t a_value_len,
                         const void *a_sign, uint32_t a_sign_len,
                         uint8_t a_flags)
{
    dap_return_val_if_fail(a_tree && a_key && !a_tree->read_only, -1);
    
    // Empty tree - create root
    if (a_tree->header.root_page == 0) {
        uint64_t l_root_id = s_page_allocate_new(a_tree);
        dap_global_db_btree_page_t *l_root = s_page_alloc();
        if (!l_root)
            return -1;
        
        l_root->header.page_id = l_root_id;
        l_root->header.flags = DAP_GLOBAL_DB_PAGE_LEAF | DAP_GLOBAL_DB_PAGE_ROOT;
        l_root->header.parent = 0;
        
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
        s_header_write(a_tree);
        s_page_free(l_root);
        
        return 0;
    }
    
    // Read root
    dap_global_db_btree_page_t *l_root = s_page_read(a_tree, a_tree->header.root_page);
    if (!l_root)
        return -1;
    
    // Check if root needs split (checks free space for leaf, entry count for branch)
    if (s_page_needs_split(l_root, a_text_key_len, a_value_len, a_sign_len)) {
        // Create new root
        uint64_t l_new_root_id = s_page_allocate_new(a_tree);
        dap_global_db_btree_page_t *l_new_root = s_page_alloc();
        if (!l_new_root) {
            s_page_free(l_root);
            return -1;
        }
        
        l_new_root->header.page_id = l_new_root_id;
        l_new_root->header.flags = DAP_GLOBAL_DB_PAGE_BRANCH | DAP_GLOBAL_DB_PAGE_ROOT;
        l_new_root->header.entries_count = 0;
        
        // Old root becomes first child
        l_root->header.flags &= ~DAP_GLOBAL_DB_PAGE_ROOT;
        l_root->header.parent = l_new_root_id;
        s_branch_set_child(l_new_root, 0, a_tree->header.root_page);
        
        // Split old root
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
        s_page_free(l_new_root);
        s_header_write(a_tree);
        return l_ret;
    }
    
    int l_ret = s_insert_non_full(a_tree, l_root, a_key, a_text_key, a_text_key_len,
                                  a_value, a_value_len, a_sign, a_sign_len, a_flags);
    s_page_free(l_root);
    s_header_write(a_tree);
    
    return l_ret;
}

int dap_global_db_btree_delete(dap_global_db_btree_t *a_tree, const dap_global_db_btree_key_t *a_key)
{
    dap_return_val_if_fail(a_tree && a_key && !a_tree->read_only, -1);
    
    if (a_tree->header.root_page == 0)
        return 1;  // Not found - empty tree
    
    // Find the entry
    dap_global_db_btree_page_t *l_page = s_page_read(a_tree, a_tree->header.root_page);
    if (!l_page)
        return -1;
    
    // Navigate to leaf
    while (!(l_page->header.flags & DAP_GLOBAL_DB_PAGE_LEAF)) {
        bool l_found;
        int l_index = s_search_in_page(l_page, a_key, &l_found);
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
    
    // Delete entry
    if (s_leaf_delete_entry(l_page, l_index) != 0) {
        s_page_free(l_page);
        return -1;
    }
    
    a_tree->header.items_count--;
    
    // Note: Full B-tree deletion with rebalancing is complex
    // This simplified version just deletes without rebalancing
    // For production, implement proper underflow handling
    
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
    
    // Navigate to leaf
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
    
    // Find entry
    int l_index;
    if (s_leaf_find_entry(l_page, a_key, &l_index) != 0) {
        s_page_free(l_page);
        return 1;  // Not found
    }
    
    uint8_t *l_data = NULL;
    dap_global_db_btree_leaf_entry_t *l_entry = s_leaf_entry_at(l_page, l_index, &l_data, NULL);
    
    // Copy out data
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
    
    // Reset header
    a_tree->header.root_page = 0;
    a_tree->header.total_pages = 0;
    a_tree->header.items_count = 0;
    a_tree->header.tree_height = 0;
    a_tree->header.free_list_head = 0;
    
    // Truncate file
    if (ftruncate(a_tree->fd, BTREE_DATA_OFFSET) != 0) {
        log_it(L_WARNING, "Failed to truncate file: %s", strerror(errno));
    }
    
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
    
    // Free current page
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
        // Need to reload previous state (simplified - just return error if no current page)
        return -1;
        
    case DAP_GLOBAL_DB_BTREE_PREV:
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
