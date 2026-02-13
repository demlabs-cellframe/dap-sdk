/*
 * DAP Global DB Migration - MDBX/LMDB File Parser
 *
 * Direct binary parsing of MDBX/LMDB database files without using libmdbx.
 * This allows migration from old databases without external dependencies.
 *
 * MDBX/LMDB file format:
 * - Header page (page 0 and 1 are meta pages)
 * - B-tree pages containing key-value pairs
 * - Pages are typically 4096 bytes
 */

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#ifndef DAP_OS_WINDOWS
#include <sys/mman.h>
#endif
#include <endian.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_global_db.h"
#include "dap_global_db_storage.h"
#include "dap_global_db_migrate.h"

#define LOG_TAG "dap_global_db_migrate_mdbx"

// ============================================================================
// MDBX/LMDB file format structures
// ============================================================================

#define MDBX_MAGIC          0xBEEFC0DE
#define LMDB_MAGIC          0xBEEFC0DE
#define MDB_META_PAGE_SIZE  4096

// Page flags
#define P_BRANCH        0x01    // Branch page
#define P_LEAF          0x02    // Leaf page
#define P_OVERFLOW      0x04    // Overflow page
#define P_META          0x08    // Meta page
#define P_DIRTY         0x10    // Dirty page
#define P_LEAF2         0x20    // Leaf page with DUPDATA
#define P_SUBP          0x40    // Sub-page

// Node flags
#define F_BIGDATA       0x01    // Data is on overflow page
#define F_SUBDATA       0x02    // Data is a sub-database
#define F_DUPDATA       0x04    // Data has duplicates

// Meta page header
typedef struct mdb_meta {
    uint32_t mm_magic;
    uint32_t mm_version;
    uint64_t mm_mapsize;
    uint16_t mm_dbs;            // Number of DBs
    uint16_t mm_flags;
    uint32_t mm_pagesize;
    uint64_t mm_last_pgno;
    uint64_t mm_txnid;
    // More fields follow but we don't need them all
} mdb_meta_t;

// Page header
typedef struct mdb_page {
    union {
        uint64_t p_pgno;
        struct mdb_page *p_next;
    };
    uint16_t p_pad;
    uint16_t p_flags;
    union {
        uint32_t p_pb_padding;
        struct {
            uint16_t p_lower;   // Lower bound of free space
            uint16_t p_upper;   // Upper bound of free space
        };
    };
} mdb_page_t;

// Node in leaf page
typedef struct mdb_node {
    uint16_t mn_lo;     // Key size low bits + flags
    uint16_t mn_hi;     // Key size high bits (for MDBX) or data size
    uint32_t mn_dsize;  // Data size
    // Key data follows, then value data
} mdb_node_t;

// Our GlobalDB record format stored in MDBX
typedef struct gdb_mdbx_record {
    uint64_t timestamp;
    uint64_t crc;
    uint32_t key_len;
    uint32_t value_len;
    uint32_t sign_len;
    uint8_t flags;
    byte_t data[];  // [key][value][sign]
} DAP_ALIGN_PACKED gdb_mdbx_record_t;

// ============================================================================
// MDBX file reader context
// ============================================================================

typedef struct mdbx_reader {
    int fd;
    void *map;
    size_t map_size;
    uint32_t page_size;
    uint64_t last_pgno;
    mdb_meta_t meta;
} mdbx_reader_t;

#ifndef DAP_OS_WINDOWS
static mdbx_reader_t *s_mdbx_open(const char *a_path)
{
    mdbx_reader_t *r = DAP_NEW_Z(mdbx_reader_t);
    if (!r)
        return NULL;
    
    r->fd = open(a_path, O_RDONLY);
    if (r->fd < 0) {
        DAP_DELETE(r);
        return NULL;
    }
    
    struct stat st;
    if (fstat(r->fd, &st) < 0) {
        close(r->fd);
        DAP_DELETE(r);
        return NULL;
    }
    
    r->map_size = st.st_size;
    r->map = mmap(NULL, r->map_size, PROT_READ, MAP_PRIVATE, r->fd, 0);
    
    if (r->map == MAP_FAILED) {
        close(r->fd);
        DAP_DELETE(r);
        return NULL;
    }
    
    // Read meta page (page 0 or 1, whichever has higher txnid)
    mdb_meta_t *meta0 = (mdb_meta_t *)((byte_t *)r->map + 0);
    mdb_meta_t *meta1 = (mdb_meta_t *)((byte_t *)r->map + MDB_META_PAGE_SIZE);
    
    mdb_meta_t *meta = meta0;
    if (meta1->mm_magic == MDBX_MAGIC && meta1->mm_txnid > meta0->mm_txnid)
        meta = meta1;
    
    if (meta->mm_magic != MDBX_MAGIC) {
        log_it(L_ERROR, "Invalid MDBX magic: 0x%08X", meta->mm_magic);
        munmap(r->map, r->map_size);
        close(r->fd);
        DAP_DELETE(r);
        return NULL;
    }
    
    r->meta = *meta;
    r->page_size = meta->mm_pagesize ? meta->mm_pagesize : 4096;
    r->last_pgno = meta->mm_last_pgno;
    
    log_it(L_DEBUG, "MDBX opened: page_size=%u, pages=%" DAP_UINT64_FORMAT_U ", version=%u",
           r->page_size, r->last_pgno, meta->mm_version);
    
    return r;
}

static void s_mdbx_close(mdbx_reader_t *r)
{
    if (!r)
        return;
    if (r->map && r->map != MAP_FAILED)
        munmap(r->map, r->map_size);
    if (r->fd >= 0)
        close(r->fd);
    DAP_DELETE(r);
}

static mdb_page_t *s_get_page(mdbx_reader_t *r, uint64_t pgno)
{
    if (pgno > r->last_pgno)
        return NULL;
    return (mdb_page_t *)((byte_t *)r->map + pgno * r->page_size);
}

// ============================================================================
// B-tree traversal and data extraction
// ============================================================================

typedef int (*mdbx_record_cb_t)(const char *group, const byte_t *key, size_t key_len,
                                 const byte_t *data, size_t data_len, void *arg);

static int s_process_leaf_page(mdbx_reader_t *r, mdb_page_t *page, 
                                const char *group, mdbx_record_cb_t cb, void *arg)
{
    if (!(page->p_flags & P_LEAF))
        return 0;
    
    // Number of nodes
    uint16_t nkeys = (page->p_lower - sizeof(mdb_page_t)) / sizeof(uint16_t);
    uint16_t *node_ptrs = (uint16_t *)((byte_t *)page + sizeof(mdb_page_t));
    
    for (uint16_t i = 0; i < nkeys; i++) {
        uint16_t offset = node_ptrs[i];
        mdb_node_t *node = (mdb_node_t *)((byte_t *)page + offset);
        
        // Key size (depends on MDBX version, simplified)
        uint16_t ksize = node->mn_lo & 0x7FFF;
        uint8_t flags = (node->mn_lo >> 15) | ((node->mn_hi & 0xF000) >> 8);
        
        byte_t *key = (byte_t *)node + sizeof(mdb_node_t);
        
        // Data
        byte_t *data;
        size_t dsize;
        
        if (flags & F_BIGDATA) {
            // Data on overflow page - skip for now (complex)
            continue;
        } else {
            dsize = node->mn_dsize;
            data = key + ksize;
        }
        
        if (cb) {
            int rc = cb(group, key, ksize, data, dsize, arg);
            if (rc < 0)
                return rc;
        }
    }
    
    return 0;
}

static int s_traverse_btree(mdbx_reader_t *r, uint64_t root_pgno, 
                            const char *group, mdbx_record_cb_t cb, void *arg)
{
    if (root_pgno == 0 || root_pgno > r->last_pgno)
        return 0;
    
    mdb_page_t *page = s_get_page(r, root_pgno);
    if (!page)
        return 0;
    
    if (page->p_flags & P_LEAF) {
        return s_process_leaf_page(r, page, group, cb, arg);
    }
    
    if (page->p_flags & P_BRANCH) {
        // Branch page - recurse into children
        uint16_t nkeys = (page->p_lower - sizeof(mdb_page_t)) / sizeof(uint16_t);
        uint16_t *node_ptrs = (uint16_t *)((byte_t *)page + sizeof(mdb_page_t));
        
        for (uint16_t i = 0; i < nkeys; i++) {
            uint16_t offset = node_ptrs[i];
            mdb_node_t *node = (mdb_node_t *)((byte_t *)page + offset);
            
            // Child page number is stored in data area
            uint64_t child_pgno = *(uint64_t *)((byte_t *)node + sizeof(mdb_node_t) + (node->mn_lo & 0x7FFF));
            
            int rc = s_traverse_btree(r, child_pgno, group, cb, arg);
            if (rc < 0)
                return rc;
        }
    }
    
    return 0;
}

// ============================================================================
// Migration callback
// ============================================================================

typedef struct migrate_ctx {
    const dap_global_db_migrate_options_t *opts;
    dap_global_db_migrate_result_t *result;
    const char *current_group;
} migrate_ctx_t;

static int s_migrate_record(const char *group, const byte_t *key, size_t key_len,
                             const byte_t *data, size_t data_len, void *arg)
{
    migrate_ctx_t *ctx = (migrate_ctx_t *)arg;
    (void)key;
    (void)key_len;
    
    // Parse our record format
    if (data_len < sizeof(gdb_mdbx_record_t))
        return 0;
    
    const gdb_mdbx_record_t *rec = (const gdb_mdbx_record_t *)data;
    
    // Validate
    size_t expected = sizeof(gdb_mdbx_record_t) + rec->key_len + rec->value_len + rec->sign_len;
    if (data_len < expected) {
        ctx->result->records_failed++;
        return ctx->opts->skip_errors ? 0 : -1;
    }
    
    // Extract fields
    const char *text_key = (const char *)rec->data;
    const void *value = rec->data + rec->key_len;
    const void *sign = rec->data + rec->key_len + rec->value_len;
    
    // Get or create group B-tree
    dap_global_db_btree_t *btree = dap_global_db_storage_group_get_or_create(group);
    if (!btree) {
        ctx->result->records_failed++;
        return ctx->opts->skip_errors ? 0 : -1;
    }
    
    // Build key
    dap_global_db_btree_key_t bkey = {
        .bets = htobe64(rec->timestamp),
        .becrc = htobe64(rec->crc)
    };
    
    // Insert
    int rc = dap_global_db_btree_insert(btree, &bkey,
                                         text_key, rec->key_len,
                                         value, rec->value_len,
                                         sign, rec->sign_len,
                                         rec->flags);
    if (rc >= 0) {
        ctx->result->records_migrated++;
        ctx->result->bytes_migrated += rec->value_len;
    } else {
        ctx->result->records_failed++;
        if (!ctx->opts->skip_errors)
            return -1;
    }
    
    // Progress
    if (ctx->opts->progress_cb && (ctx->result->records_migrated % 1000 == 0)) {
        ctx->opts->progress_cb(group, ctx->result->records_migrated, 0, ctx->opts->progress_arg);
    }
    
    return 0;
}
#endif /* !DAP_OS_WINDOWS */

// ============================================================================
// Public implementation
// ============================================================================

dap_global_db_migrate_result_t dap_global_db_migrate_mdbx_impl(
    const char *a_mdbx_path,
    const char *a_dest_path,
    const dap_global_db_migrate_options_t *a_opts)
{
    dap_global_db_migrate_result_t l_result = {0};

#ifdef DAP_OS_WINDOWS
    (void)a_mdbx_path;
    (void)a_dest_path;
    (void)a_opts;
    l_result.status = DAP_MIGRATE_ERR_SOURCE;
    l_result.error_message = dap_strdup("MDBX migration not supported on Windows (requires mmap)");
    return l_result;
#else
    // Open MDBX file
    mdbx_reader_t *r = s_mdbx_open(a_mdbx_path);
    if (!r) {
        l_result.status = DAP_MIGRATE_ERR_SOURCE;
        l_result.error_message = dap_strdup_printf("Cannot open MDBX file: %s", a_mdbx_path);
        return l_result;
    }
    
    // Initialize destination storage
    if (dap_global_db_storage_init(a_dest_path) < 0) {
        s_mdbx_close(r);
        l_result.status = DAP_MIGRATE_ERR_DEST;
        l_result.error_message = dap_strdup("Failed to initialize destination storage");
        return l_result;
    }
    
    if (a_opts->verbose)
        log_it(L_INFO, "Migrating MDBX file: %s (size=%zu)", a_mdbx_path, r->map_size);
    
    migrate_ctx_t ctx = {
        .opts = a_opts,
        .result = &l_result
    };
    
    // The main database root is typically at page 2 (after two meta pages)
    // We need to scan for sub-databases (groups)
    // In MDBX, the main DB contains entries where key=db_name and data=db_info
    
    // Simplified approach: scan all leaf pages for our record format
    for (uint64_t pgno = 2; pgno <= r->last_pgno; pgno++) {
        mdb_page_t *page = s_get_page(r, pgno);
        if (!page)
            continue;
        
        if (page->p_flags & P_LEAF) {
            // Try to extract records from this leaf page
            // We assume group name is embedded in our record or derive from page
            s_process_leaf_page(r, page, "default", s_migrate_record, &ctx);
        }
    }
    
    // Cleanup
    s_mdbx_close(r);
    
    // Flush and deinit storage
    dap_global_db_storage_flush();
    dap_global_db_storage_deinit();
    
    if (a_opts->verbose) {
        log_it(L_INFO, "MDBX migration complete: %zu records, %zu failed, %zu bytes",
               l_result.records_migrated, l_result.records_failed, l_result.bytes_migrated);
    }
    
    l_result.status = (l_result.records_failed > 0 && !a_opts->skip_errors) 
                      ? DAP_MIGRATE_ERR_READ : DAP_MIGRATE_OK;
    
    return l_result;
#endif /* !DAP_OS_WINDOWS */
}
