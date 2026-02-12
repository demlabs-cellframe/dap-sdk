/*
 * DAP Global DB Storage Layer
 * 
 * Direct B-tree storage operations for GlobalDB.
 * File-per-group structure with native B-tree implementation.
 */

#include <string.h>
#include <fnmatch.h>
#include <pthread.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_global_db_storage.h"
#include "dap_global_db_pkt.h"
#include "uthash.h"

#define LOG_TAG "gdb_storage"

// ============================================================================
// Constants
// ============================================================================

#define GROUPS_INDEX_FILE   "groups.idx"
#define GROUP_FILE_EXT      ".gdb"
#define GROUPS_INDEX_MAGIC  0x47444249  // "GDBI"
#define GROUPS_INDEX_VERSION 1

// ============================================================================
// Internal types
// ============================================================================

typedef struct gdb_group {
    char *name;                     // Group name (hash key)
    dap_global_db_btree_t *btree;         // B-tree handle
    bool is_dirty;                  // Has unsaved changes
    UT_hash_handle hh;              // Hash handle
} gdb_group_t;

typedef struct gdb_groups_index_header {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t groups_count;
    uint32_t checksum;
} DAP_ALIGN_PACKED gdb_groups_index_header_t;

// ============================================================================
// Static state
// ============================================================================

static gdb_group_t *s_groups = NULL;
static pthread_rwlock_t s_groups_lock = PTHREAD_RWLOCK_INITIALIZER;
static char *s_storage_path = NULL;

// ============================================================================
// Internal helpers
// ============================================================================

static char *s_sanitize_name(const char *a_name)
{
    if (!a_name)
        return NULL;
    char *l_result = dap_strdup(a_name);
    for (char *p = l_result; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' || 
            *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') {
            *p = '_';
        }
    }
    return l_result;
}

static char *s_group_filepath(const char *a_group_name)
{
    if (!a_group_name || !s_storage_path)
        return NULL;
    char *l_safe_name = s_sanitize_name(a_group_name);
    char *l_path = dap_strdup_printf("%s/%s%s", s_storage_path, l_safe_name, GROUP_FILE_EXT);
    DAP_DELETE(l_safe_name);
    return l_path;
}

static gdb_group_t *s_group_open(const char *a_group_name, bool a_create)
{
    if (!a_group_name || !s_storage_path)
        return NULL;
    
    // Check if already open (must be called with lock held)
    gdb_group_t *l_group = NULL;
    HASH_FIND_STR(s_groups, a_group_name, l_group);
    if (l_group)
        return l_group;
    
    char *l_filepath = s_group_filepath(a_group_name);
    if (!l_filepath)
        return NULL;
    
    // Try to open existing
    dap_global_db_btree_t *l_btree = dap_global_db_btree_open(l_filepath, false);
    if (!l_btree && a_create) {
        l_btree = dap_global_db_btree_create(l_filepath);
    }
    DAP_DELETE(l_filepath);
    
    if (!l_btree)
        return NULL;
    
    l_group = DAP_NEW_Z(gdb_group_t);
    l_group->name = dap_strdup(a_group_name);
    l_group->btree = l_btree;
    l_group->is_dirty = false;
    
    HASH_ADD_STR(s_groups, name, l_group);
    return l_group;
}

static void s_group_close(gdb_group_t *a_group)
{
    if (!a_group)
        return;
    if (a_group->btree)
        dap_global_db_btree_close(a_group->btree);
    DAP_DEL_Z(a_group->name);
    DAP_DELETE(a_group);
}

static int s_groups_index_load(void)
{
    char *l_index_path = dap_strdup_printf("%s/%s", s_storage_path, GROUPS_INDEX_FILE);
    
    FILE *l_fp = fopen(l_index_path, "rb");
    if (!l_fp) {
        DAP_DELETE(l_index_path);
        return 0;  // No index - start fresh
    }
    
    gdb_groups_index_header_t l_header;
    if (fread(&l_header, sizeof(l_header), 1, l_fp) != 1) {
        fclose(l_fp);
        DAP_DELETE(l_index_path);
        return -1;
    }
    
    if (l_header.magic != GROUPS_INDEX_MAGIC || l_header.version != GROUPS_INDEX_VERSION) {
        log_it(L_ERROR, "Invalid groups index file");
        fclose(l_fp);
        DAP_DELETE(l_index_path);
        return -1;
    }
    
    for (uint32_t i = 0; i < l_header.groups_count; i++) {
        uint16_t l_name_len;
        if (fread(&l_name_len, sizeof(l_name_len), 1, l_fp) != 1)
            break;
        
        char *l_name = DAP_NEW_Z_SIZE(char, l_name_len + 1);
        if (fread(l_name, l_name_len, 1, l_fp) != 1) {
            DAP_DELETE(l_name);
            break;
        }
        
        // Open the group (creates entry in hash)
        s_group_open(l_name, false);
        DAP_DELETE(l_name);
    }
    
    fclose(l_fp);
    DAP_DELETE(l_index_path);
    return 0;
}

static int s_groups_index_save(void)
{
    char *l_index_path = dap_strdup_printf("%s/%s", s_storage_path, GROUPS_INDEX_FILE);
    
    FILE *l_fp = fopen(l_index_path, "wb");
    if (!l_fp) {
        log_it(L_ERROR, "Failed to create groups index: %s", l_index_path);
        DAP_DELETE(l_index_path);
        return -1;
    }
    
    uint32_t l_count = HASH_COUNT(s_groups);
    gdb_groups_index_header_t l_header = {
        .magic = GROUPS_INDEX_MAGIC,
        .version = GROUPS_INDEX_VERSION,
        .groups_count = l_count,
        .reserved = 0,
        .checksum = 0
    };
    
    fwrite(&l_header, sizeof(l_header), 1, l_fp);
    
    gdb_group_t *l_group, *l_tmp;
    HASH_ITER(hh, s_groups, l_group, l_tmp) {
        uint16_t l_name_len = strlen(l_group->name);
        fwrite(&l_name_len, sizeof(l_name_len), 1, l_fp);
        fwrite(l_group->name, l_name_len, 1, l_fp);
    }
    
    fclose(l_fp);
    DAP_DELETE(l_index_path);
    return 0;
}

// Helper to convert B-tree entry to store_obj
static dap_global_db_store_obj_t *s_btree_entry_to_store_obj(const char *a_group,
                                                    dap_global_db_btree_key_t *a_key,
                                                    char *a_text_key,
                                                    void *a_value, uint32_t a_value_len,
                                                    void *a_sign, uint32_t a_sign_len,
                                                    uint8_t a_flags)
{
    dap_global_db_store_obj_t *l_obj = DAP_NEW_Z(dap_global_db_store_obj_t);
    if (!l_obj) {
        DAP_DEL_MULTY(a_text_key, a_value, a_sign);
        return NULL;
    }
    
    l_obj->group = dap_strdup(a_group);
    l_obj->key = a_text_key;
    l_obj->value = a_value;
    l_obj->value_len = a_value_len;
    l_obj->sign = (dap_sign_t *)a_sign;
    l_obj->flags = a_flags;
    l_obj->timestamp = be64toh(a_key->bets);
    l_obj->crc = be64toh(a_key->becrc);
    
    return l_obj;
}

// ============================================================================
// Public API - Initialization
// ============================================================================

int dap_global_db_storage_init(const char *a_storage_path)
{
    if (s_storage_path) {
        log_it(L_WARNING, "Storage already initialized");
        return 0;
    }
    
    s_storage_path = dap_strdup_printf("%s/gdb-native", a_storage_path);
    
    // Create directory if needed
    if (!dap_dir_test(s_storage_path)) {
        if (dap_mkdir_with_parents(s_storage_path) != 0) {
            log_it(L_ERROR, "Failed to create storage directory: %s", s_storage_path);
            DAP_DEL_Z(s_storage_path);
            return -1;
        }
    }
    
    pthread_rwlock_init(&s_groups_lock, NULL);
    
    if (s_groups_index_load() != 0) {
        log_it(L_WARNING, "Failed to load groups index, starting fresh");
    }
    
    log_it(L_INFO, "Native GlobalDB storage initialized at %s", s_storage_path);
    return 0;
}

void dap_global_db_storage_deinit(void)
{
    if (!s_storage_path)
        return;
    
    pthread_rwlock_wrlock(&s_groups_lock);
    
    s_groups_index_save();
    
    gdb_group_t *l_group, *l_tmp;
    HASH_ITER(hh, s_groups, l_group, l_tmp) {
        HASH_DEL(s_groups, l_group);
        s_group_close(l_group);
    }
    
    pthread_rwlock_unlock(&s_groups_lock);
    pthread_rwlock_destroy(&s_groups_lock);
    
    DAP_DEL_Z(s_storage_path);
    log_it(L_INFO, "Native GlobalDB storage deinitialized");
}

int dap_global_db_storage_flush(void)
{
    pthread_rwlock_rdlock(&s_groups_lock);
    
    gdb_group_t *l_group, *l_tmp;
    HASH_ITER(hh, s_groups, l_group, l_tmp) {
        if (l_group->btree)
            dap_global_db_btree_sync(l_group->btree);
    }
    
    pthread_rwlock_unlock(&s_groups_lock);
    return 0;
}

// ============================================================================
// Public API - Group operations
// ============================================================================

dap_global_db_btree_t *dap_global_db_storage_group_get(const char *a_group_name)
{
    if (!a_group_name)
        return NULL;
    
    pthread_rwlock_rdlock(&s_groups_lock);
    gdb_group_t *l_group = NULL;
    HASH_FIND_STR(s_groups, a_group_name, l_group);
    pthread_rwlock_unlock(&s_groups_lock);
    
    // If not found in cache, try to open existing file
    if (!l_group) {
        pthread_rwlock_wrlock(&s_groups_lock);
        HASH_FIND_STR(s_groups, a_group_name, l_group);
        if (!l_group)
            l_group = s_group_open(a_group_name, false);
        pthread_rwlock_unlock(&s_groups_lock);
    }
    
    return l_group ? l_group->btree : NULL;
}

dap_global_db_btree_t *dap_global_db_storage_group_get_or_create(const char *a_group_name)
{
    if (!a_group_name)
        return NULL;
    
    pthread_rwlock_rdlock(&s_groups_lock);
    gdb_group_t *l_group = NULL;
    HASH_FIND_STR(s_groups, a_group_name, l_group);
    pthread_rwlock_unlock(&s_groups_lock);
    
    if (!l_group) {
        pthread_rwlock_wrlock(&s_groups_lock);
        HASH_FIND_STR(s_groups, a_group_name, l_group);
        if (!l_group)
            l_group = s_group_open(a_group_name, true);
        pthread_rwlock_unlock(&s_groups_lock);
    }
    
    return l_group ? l_group->btree : NULL;
}

dap_list_t *dap_global_db_storage_get_groups_by_mask(const char *a_mask)
{
    dap_return_val_if_fail(a_mask, NULL);
    
    dap_list_t *l_result = NULL;
    
    pthread_rwlock_rdlock(&s_groups_lock);
    
    gdb_group_t *l_group, *l_tmp;
    HASH_ITER(hh, s_groups, l_group, l_tmp) {
        if (fnmatch(a_mask, l_group->name, 0) == 0) {
            l_result = dap_list_append(l_result, dap_strdup(l_group->name));
        }
    }
    
    pthread_rwlock_unlock(&s_groups_lock);
    return l_result;
}

uint64_t dap_global_db_storage_group_count(const char *a_group_name, bool a_with_deleted)
{
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get(a_group_name);
    if (!l_btree)
        return 0;
    
    if (a_with_deleted)
        return dap_global_db_btree_count(l_btree);
    
    // Count excluding deleted
    dap_global_db_btree_cursor_t *l_cursor = dap_global_db_btree_cursor_create(l_btree);
    if (!l_cursor)
        return 0;
    
    uint64_t l_count = 0;
    if (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_FIRST, NULL) == 0) {
        do {
            uint8_t l_flags = 0;
            if (dap_global_db_btree_cursor_get(l_cursor, NULL, NULL, NULL, NULL, NULL, NULL, &l_flags) == 0) {
                if (!(l_flags & DAP_GLOBAL_DB_RECORD_DEL))
                    l_count++;
            }
        } while (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_NEXT, NULL) == 0);
    }
    
    dap_global_db_btree_cursor_close(l_cursor);
    return l_count;
}

uint64_t dap_global_db_storage_group_count_from(const char *a_group_name,
                                           dap_global_db_hash_t a_hash_from,
                                           bool a_with_deleted)
{
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get(a_group_name);
    if (!l_btree)
        return 0;
    
    dap_global_db_btree_cursor_t *l_cursor = dap_global_db_btree_cursor_create(l_btree);
    if (!l_cursor)
        return 0;
    
    dap_global_db_btree_key_t l_start_key = {
        .bets = a_hash_from.bets,
        .becrc = a_hash_from.becrc
    };
    
    int l_rc;
    if (dap_global_db_hash_is_blank(&a_hash_from)) {
        l_rc = dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_FIRST, NULL);
    } else {
        l_rc = dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_SET_UPPERBOUND, &l_start_key);
    }
    
    uint64_t l_count = 0;
    if (l_rc == 0) {
        do {
            if (!a_with_deleted) {
                uint8_t l_flags = 0;
                if (dap_global_db_btree_cursor_get(l_cursor, NULL, NULL, NULL, NULL, NULL, NULL, &l_flags) == 0) {
                    if (l_flags & DAP_GLOBAL_DB_RECORD_DEL)
                        continue;
                }
            }
            l_count++;
        } while (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_NEXT, NULL) == 0);
    }
    
    dap_global_db_btree_cursor_close(l_cursor);
    return l_count;
}

// ============================================================================
// Public API - Record operations
// ============================================================================

dap_global_db_store_obj_t *dap_global_db_storage_read_by_key(const char *a_group, const char *a_key,
                                              bool a_with_deleted)
{
    dap_return_val_if_fail(a_group && a_key, NULL);
    
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get(a_group);
    if (!l_btree)
        return NULL;
    
    dap_global_db_btree_cursor_t *l_cursor = dap_global_db_btree_cursor_create(l_btree);
    if (!l_cursor)
        return NULL;
    
    dap_global_db_store_obj_t *l_result = NULL;
    
    if (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_FIRST, NULL) == 0) {
        do {
            dap_global_db_btree_key_t l_drv_key;
            char *l_text_key = NULL;
            void *l_value = NULL;
            uint32_t l_value_len = 0;
            void *l_sign = NULL;
            uint32_t l_sign_len = 0;
            uint8_t l_flags = 0;
            
            if (dap_global_db_btree_cursor_get(l_cursor, &l_drv_key, &l_text_key,
                                          &l_value, &l_value_len,
                                          &l_sign, &l_sign_len, &l_flags) == 0) {
                if (l_text_key && strcmp(l_text_key, a_key) == 0) {
                    if (!a_with_deleted && (l_flags & DAP_GLOBAL_DB_RECORD_DEL)) {
                        DAP_DEL_MULTY(l_text_key, l_value, l_sign);
                        continue;
                    }
                    l_result = s_btree_entry_to_store_obj(a_group, &l_drv_key,
                                                          l_text_key, l_value, l_value_len,
                                                          l_sign, l_sign_len, l_flags);
                    break;
                }
                DAP_DEL_MULTY(l_text_key, l_value, l_sign);
            }
        } while (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_NEXT, NULL) == 0);
    }
    
    dap_global_db_btree_cursor_close(l_cursor);
    return l_result;
}

dap_global_db_store_obj_t *dap_global_db_storage_read_by_hash(const char *a_group,
                                               dap_global_db_hash_t a_hash)
{
    dap_return_val_if_fail(a_group, NULL);
    
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get(a_group);
    if (!l_btree)
        return NULL;
    
    dap_global_db_btree_key_t l_key = {
        .bets = a_hash.bets,
        .becrc = a_hash.becrc
    };
    
    char *l_text_key = NULL;
    void *l_value = NULL;
    uint32_t l_value_len = 0;
    void *l_sign = NULL;
    uint32_t l_sign_len = 0;
    uint8_t l_flags = 0;
    
    if (dap_global_db_btree_get(l_btree, &l_key, &l_text_key,
                          &l_value, &l_value_len,
                          &l_sign, &l_sign_len, &l_flags) != 0) {
        return NULL;
    }
    
    return s_btree_entry_to_store_obj(a_group, &l_key,
                                       l_text_key, l_value, l_value_len,
                                       l_sign, l_sign_len, l_flags);
}

dap_global_db_store_obj_t *dap_global_db_storage_read_last(const char *a_group, bool a_with_deleted)
{
    dap_return_val_if_fail(a_group, NULL);
    
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get(a_group);
    if (!l_btree)
        return NULL;
    
    dap_global_db_btree_cursor_t *l_cursor = dap_global_db_btree_cursor_create(l_btree);
    if (!l_cursor)
        return NULL;
    
    dap_global_db_store_obj_t *l_result = NULL;
    
    if (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_LAST, NULL) == 0) {
        do {
            dap_global_db_btree_key_t l_drv_key;
            char *l_text_key = NULL;
            void *l_value = NULL;
            uint32_t l_value_len = 0;
            void *l_sign = NULL;
            uint32_t l_sign_len = 0;
            uint8_t l_flags = 0;
            
            if (dap_global_db_btree_cursor_get(l_cursor, &l_drv_key, &l_text_key,
                                          &l_value, &l_value_len,
                                          &l_sign, &l_sign_len, &l_flags) == 0) {
                if (!a_with_deleted && (l_flags & DAP_GLOBAL_DB_RECORD_DEL)) {
                    DAP_DEL_MULTY(l_text_key, l_value, l_sign);
                    continue;
                }
                l_result = s_btree_entry_to_store_obj(a_group, &l_drv_key,
                                                      l_text_key, l_value, l_value_len,
                                                      l_sign, l_sign_len, l_flags);
                break;
            }
        } while (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_PREV, NULL) == 0);
    }
    
    dap_global_db_btree_cursor_close(l_cursor);
    return l_result;
}

dap_global_db_store_obj_t *dap_global_db_storage_read_all(const char *a_group, size_t *a_count_out,
                                           bool a_with_deleted)
{
    dap_return_val_if_fail(a_group, NULL);
    
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get(a_group);
    if (!l_btree) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    
    uint64_t l_total = dap_global_db_btree_count(l_btree);
    if (l_total == 0) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    
    dap_global_db_store_obj_t *l_results = DAP_NEW_Z_COUNT(dap_global_db_store_obj_t, l_total);
    if (!l_results) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    
    dap_global_db_btree_cursor_t *l_cursor = dap_global_db_btree_cursor_create(l_btree);
    if (!l_cursor) {
        DAP_DELETE(l_results);
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    
    size_t l_count = 0;
    if (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_FIRST, NULL) == 0) {
        do {
            dap_global_db_btree_key_t l_drv_key;
            char *l_text_key = NULL;
            void *l_value = NULL;
            uint32_t l_value_len = 0;
            void *l_sign = NULL;
            uint32_t l_sign_len = 0;
            uint8_t l_flags = 0;
            
            if (dap_global_db_btree_cursor_get(l_cursor, &l_drv_key, &l_text_key,
                                          &l_value, &l_value_len,
                                          &l_sign, &l_sign_len, &l_flags) == 0) {
                if (!a_with_deleted && (l_flags & DAP_GLOBAL_DB_RECORD_DEL)) {
                    DAP_DEL_MULTY(l_text_key, l_value, l_sign);
                    continue;
                }
                
                l_results[l_count].group = dap_strdup(a_group);
                l_results[l_count].key = l_text_key;
                l_results[l_count].value = l_value;
                l_results[l_count].value_len = l_value_len;
                l_results[l_count].sign = (dap_sign_t *)l_sign;
                l_results[l_count].flags = l_flags;
                l_results[l_count].timestamp = be64toh(l_drv_key.bets);
                l_results[l_count].crc = be64toh(l_drv_key.becrc);
                l_count++;
            }
        } while (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_NEXT, NULL) == 0);
    }
    
    dap_global_db_btree_cursor_close(l_cursor);
    
    if (a_count_out) *a_count_out = l_count;
    if (l_count == 0) {
        DAP_DELETE(l_results);
        return NULL;
    }
    return l_results;
}

dap_global_db_store_obj_t *dap_global_db_storage_read_cond(const char *a_group,
                                            dap_global_db_hash_t a_hash_from,
                                            size_t a_max_count,
                                            size_t *a_count_out,
                                            bool a_with_deleted)
{
    dap_return_val_if_fail(a_group, NULL);
    
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get(a_group);
    if (!l_btree) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    
    dap_global_db_btree_cursor_t *l_cursor = dap_global_db_btree_cursor_create(l_btree);
    if (!l_cursor) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    
    dap_global_db_btree_key_t l_start_key = {
        .bets = a_hash_from.bets,
        .becrc = a_hash_from.becrc
    };
    
    int l_rc;
    if (dap_global_db_hash_is_blank(&a_hash_from)) {
        l_rc = dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_FIRST, NULL);
    } else {
        l_rc = dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_SET_UPPERBOUND, &l_start_key);
    }
    
    if (l_rc != 0) {
        dap_global_db_btree_cursor_close(l_cursor);
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    
    if (a_max_count == 0)
        a_max_count = DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT;
    
    dap_global_db_store_obj_t *l_results = DAP_NEW_Z_COUNT(dap_global_db_store_obj_t, a_max_count);
    if (!l_results) {
        dap_global_db_btree_cursor_close(l_cursor);
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    
    size_t l_count = 0;
    do {
        if (l_count >= a_max_count)
            break;
        
        dap_global_db_btree_key_t l_drv_key;
        char *l_text_key = NULL;
        void *l_value = NULL;
        uint32_t l_value_len = 0;
        void *l_sign = NULL;
        uint32_t l_sign_len = 0;
        uint8_t l_flags = 0;
        
        if (dap_global_db_btree_cursor_get(l_cursor, &l_drv_key, &l_text_key,
                                      &l_value, &l_value_len,
                                      &l_sign, &l_sign_len, &l_flags) == 0) {
            if (!a_with_deleted && (l_flags & DAP_GLOBAL_DB_RECORD_DEL)) {
                DAP_DEL_MULTY(l_text_key, l_value, l_sign);
                continue;
            }
            
            l_results[l_count].group = dap_strdup(a_group);
            l_results[l_count].key = l_text_key;
            l_results[l_count].value = l_value;
            l_results[l_count].value_len = l_value_len;
            l_results[l_count].sign = (dap_sign_t *)l_sign;
            l_results[l_count].flags = l_flags;
            l_results[l_count].timestamp = be64toh(l_drv_key.bets);
            l_results[l_count].crc = be64toh(l_drv_key.becrc);
            l_count++;
        }
    } while (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_NEXT, NULL) == 0);
    
    dap_global_db_btree_cursor_close(l_cursor);
    
    if (a_count_out) *a_count_out = l_count;
    if (l_count == 0) {
        DAP_DELETE(l_results);
        return NULL;
    }
    return l_results;
}

dap_global_db_store_obj_t *dap_global_db_storage_read_below_timestamp(const char *a_group,
                                                       dap_nanotime_t a_timestamp,
                                                       size_t *a_count_out)
{
    dap_return_val_if_fail(a_group, NULL);
    
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get(a_group);
    if (!l_btree) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    
    dap_global_db_btree_cursor_t *l_cursor = dap_global_db_btree_cursor_create(l_btree);
    if (!l_cursor) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    
    size_t l_alloc = DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT;
    dap_global_db_store_obj_t *l_results = DAP_NEW_Z_COUNT(dap_global_db_store_obj_t, l_alloc);
    if (!l_results) {
        dap_global_db_btree_cursor_close(l_cursor);
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    
    size_t l_count = 0;
    if (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_FIRST, NULL) == 0) {
        do {
            dap_global_db_btree_key_t l_drv_key;
            char *l_text_key = NULL;
            void *l_value = NULL;
            uint32_t l_value_len = 0;
            void *l_sign = NULL;
            uint32_t l_sign_len = 0;
            uint8_t l_flags = 0;
            
            if (dap_global_db_btree_cursor_get(l_cursor, &l_drv_key, &l_text_key,
                                          &l_value, &l_value_len,
                                          &l_sign, &l_sign_len, &l_flags) == 0) {
                dap_nanotime_t l_ts = be64toh(l_drv_key.bets);
                
                if (l_ts >= a_timestamp) {
                    DAP_DEL_MULTY(l_text_key, l_value, l_sign);
                    continue;
                }
                
                if (l_count >= l_alloc) {
                    l_alloc *= 2;
                    dap_global_db_store_obj_t *l_new = DAP_REALLOC_COUNT(l_results, l_alloc);
                    if (!l_new) {
                        DAP_DEL_MULTY(l_text_key, l_value, l_sign);
                        break;
                    }
                    l_results = l_new;
                    memset(l_results + l_count, 0, (l_alloc - l_count) * sizeof(dap_global_db_store_obj_t));
                }
                
                l_results[l_count].group = dap_strdup(a_group);
                l_results[l_count].key = l_text_key;
                l_results[l_count].value = l_value;
                l_results[l_count].value_len = l_value_len;
                l_results[l_count].sign = (dap_sign_t *)l_sign;
                l_results[l_count].flags = l_flags;
                l_results[l_count].timestamp = l_ts;
                l_results[l_count].crc = be64toh(l_drv_key.becrc);
                l_count++;
            }
        } while (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_NEXT, NULL) == 0);
    }
    
    dap_global_db_btree_cursor_close(l_cursor);
    
    if (a_count_out) *a_count_out = l_count;
    if (l_count == 0) {
        DAP_DELETE(l_results);
        return NULL;
    }
    return l_results;
}

int dap_global_db_storage_write(dap_global_db_store_obj_t *a_obj)
{
    dap_return_val_if_fail(a_obj && a_obj->group, -1);
    
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get_or_create(a_obj->group);
    if (!l_btree) {
        log_it(L_ERROR, "Failed to get/create group '%s'", a_obj->group);
        return -2;
    }
    
    dap_global_db_btree_key_t l_key = {
        .bets = htobe64(a_obj->timestamp),
        .becrc = htobe64(a_obj->crc)
    };
    
    if (a_obj->flags & DAP_GLOBAL_DB_RECORD_ERASE) {
        return dap_global_db_btree_delete(l_btree, &l_key);
    }
    
    uint32_t l_sign_len = a_obj->sign ? dap_sign_get_size(a_obj->sign) : 0;
    uint32_t l_key_len = a_obj->key ? strlen(a_obj->key) + 1 : 0;
    
    return dap_global_db_btree_insert(l_btree, &l_key,
                                 a_obj->key, l_key_len,
                                 a_obj->value, a_obj->value_len,
                                 a_obj->sign, l_sign_len,
                                 a_obj->flags);
}

int dap_global_db_storage_write_multi(dap_global_db_store_obj_t *a_objs, size_t a_count)
{
    dap_return_val_if_fail(a_objs && a_count, -1);
    
    int l_ret = 0;
    for (size_t i = 0; i < a_count && !l_ret; i++) {
        l_ret = dap_global_db_storage_write(&a_objs[i]);
    }
    return l_ret;
}

int dap_global_db_storage_erase(const char *a_group, dap_global_db_hash_t a_hash)
{
    dap_return_val_if_fail(a_group, -1);
    
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get(a_group);
    if (!l_btree)
        return 1;  // Not found
    
    dap_global_db_btree_key_t l_key = {
        .bets = a_hash.bets,
        .becrc = a_hash.becrc
    };
    
    return dap_global_db_btree_delete(l_btree, &l_key);
}

bool dap_global_db_storage_exists_key(const char *a_group, const char *a_key)
{
    dap_global_db_store_obj_t *l_obj = dap_global_db_storage_read_by_key(a_group, a_key, true);
    if (l_obj) {
        dap_global_db_store_obj_free(l_obj, 1);
        return true;
    }
    return false;
}

bool dap_global_db_storage_exists_hash(const char *a_group, dap_global_db_hash_t a_hash)
{
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get(a_group);
    if (!l_btree)
        return false;
    
    dap_global_db_btree_key_t l_key = {
        .bets = a_hash.bets,
        .becrc = a_hash.becrc
    };
    
    return dap_global_db_btree_exists(l_btree, &l_key);
}

// ============================================================================
// Synchronization helpers
// ============================================================================

dap_global_db_hash_pkt_t *dap_global_db_storage_read_hashes(const char *a_group,
                                                       dap_global_db_hash_t a_hash_from)
{
    dap_return_val_if_fail(a_group, NULL);
    
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get(a_group);
    if (!l_btree)
        return NULL;
    
    dap_global_db_btree_cursor_t *l_cursor = dap_global_db_btree_cursor_create(l_btree);
    if (!l_cursor)
        return NULL;
    
    dap_global_db_btree_key_t l_start_key = {
        .bets = a_hash_from.bets,
        .becrc = a_hash_from.becrc
    };
    
    int l_rc;
    if (dap_global_db_hash_is_blank(&a_hash_from)) {
        l_rc = dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_FIRST, NULL);
    } else {
        l_rc = dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_SET_UPPERBOUND, &l_start_key);
    }
    
    if (l_rc != 0) {
        dap_global_db_btree_cursor_close(l_cursor);
        return NULL;
    }
    
    // First pass: count hashes
    size_t l_count = 0;
    size_t l_max_count = DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT;
    do {
        l_count++;
        if (l_count >= l_max_count)
            break;
    } while (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_NEXT, NULL) == 0);
    
    if (l_count == 0) {
        dap_global_db_btree_cursor_close(l_cursor);
        return NULL;
    }
    
    // Allocate result
    size_t l_group_len = strlen(a_group) + 1;
    size_t l_alloc_size = sizeof(dap_global_db_hash_pkt_t) + l_group_len + 
                          (l_count + 1) * sizeof(dap_global_db_hash_t);
    dap_global_db_hash_pkt_t *l_result = DAP_NEW_Z_SIZE(dap_global_db_hash_pkt_t, l_alloc_size);
    if (!l_result) {
        dap_global_db_btree_cursor_close(l_cursor);
        return NULL;
    }
    
    l_result->group_name_len = l_group_len;
    l_result->hashes_count = l_count;
    memcpy(l_result->group_n_hashses, a_group, l_group_len);
    
    // Second pass: collect hashes
    dap_global_db_hash_t *l_hashes = (dap_global_db_hash_t *)(l_result->group_n_hashses + l_group_len);
    
    // Reset cursor
    if (dap_global_db_hash_is_blank(&a_hash_from)) {
        l_rc = dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_FIRST, NULL);
    } else {
        l_rc = dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_SET_UPPERBOUND, &l_start_key);
    }
    
    size_t i = 0;
    if (l_rc == 0) {
        do {
            dap_global_db_btree_key_t l_key;
            if (dap_global_db_btree_cursor_get(l_cursor, &l_key, NULL, NULL, NULL, NULL, NULL, NULL) == 0) {
                l_hashes[i].bets = l_key.bets;
                l_hashes[i].becrc = l_key.becrc;
                i++;
            }
            if (i >= l_count)
                break;
        } while (dap_global_db_btree_cursor_move(l_cursor, DAP_GLOBAL_DB_BTREE_NEXT, NULL) == 0);
    }
    
    // Add blank terminator
    l_hashes[i] = c_dap_global_db_hash_blank;
    
    dap_global_db_btree_cursor_close(l_cursor);
    return l_result;
}

dap_global_db_pkt_pack_t *dap_global_db_storage_get_by_hash(const char *a_group,
                                                       dap_global_db_hash_t *a_hashes,
                                                       size_t a_count)
{
    dap_return_val_if_fail(a_group && a_hashes && a_count, NULL);
    
    dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get(a_group);
    if (!l_btree)
        return NULL;
    
    // First pass: calculate total size and count valid records
    size_t l_total_size = 0;
    size_t l_valid_count = 0;
    
    for (size_t i = 0; i < a_count; i++) {
        dap_global_db_btree_key_t l_key = {
            .bets = a_hashes[i].bets,
            .becrc = a_hashes[i].becrc
        };
        
        char *l_text_key = NULL;
        void *l_value = NULL;
        uint32_t l_value_len = 0;
        void *l_sign = NULL;
        uint32_t l_sign_len = 0;
        
        if (dap_global_db_btree_get(l_btree, &l_key, &l_text_key, &l_value, &l_value_len, 
                              &l_sign, &l_sign_len, NULL) == 0) {
            size_t l_key_len = l_text_key ? strlen(l_text_key) + 1 : 0;
            l_total_size += sizeof(dap_global_db_pkt_t) + l_key_len + l_value_len + l_sign_len;
            l_valid_count++;
            DAP_DEL_MULTY(l_text_key, l_value, l_sign);
        }
    }
    
    if (l_valid_count == 0)
        return NULL;
    
    // Allocate result
    size_t l_group_len = strlen(a_group) + 1;
    dap_global_db_pkt_pack_t *l_result = DAP_NEW_Z_SIZE(dap_global_db_pkt_pack_t, 
                                                         sizeof(dap_global_db_pkt_pack_t) + l_total_size);
    if (!l_result)
        return NULL;
    
    l_result->data_size = l_total_size;
    l_result->obj_count = l_valid_count;
    
    // Second pass: fill data
    byte_t *l_pos = l_result->data;
    
    for (size_t i = 0; i < a_count; i++) {
        dap_global_db_btree_key_t l_key = {
            .bets = a_hashes[i].bets,
            .becrc = a_hashes[i].becrc
        };
        
        char *l_text_key = NULL;
        void *l_value = NULL;
        uint32_t l_value_len = 0;
        void *l_sign = NULL;
        uint32_t l_sign_len = 0;
        uint8_t l_flags = 0;
        
        if (dap_global_db_btree_get(l_btree, &l_key, &l_text_key, &l_value, &l_value_len, 
                              &l_sign, &l_sign_len, &l_flags) == 0) {
            size_t l_key_len = l_text_key ? strlen(l_text_key) + 1 : 0;
            // data_len includes: group + key + value + sign (sign_len is computed as data_len - group_len - key_len - value_len)
            size_t l_total_data_len = l_group_len + l_key_len + l_value_len + l_sign_len;
            
            dap_global_db_pkt_t *l_pkt = (dap_global_db_pkt_t *)l_pos;
            l_pkt->timestamp = be64toh(l_key.bets);
            l_pkt->data_len = l_total_data_len;
            l_pkt->key_len = l_key_len;
            l_pkt->group_len = l_group_len;
            l_pkt->value_len = l_value_len;
            l_pkt->flags = l_flags;
            l_pkt->crc = be64toh(l_key.becrc);
            
            byte_t *l_data_pos = l_pkt->data;
            if (l_group_len) {
                memcpy(l_data_pos, a_group, l_group_len);
                l_data_pos += l_group_len;
            }
            if (l_key_len && l_text_key) {
                memcpy(l_data_pos, l_text_key, l_key_len);
                l_data_pos += l_key_len;
            }
            if (l_value_len && l_value) {
                memcpy(l_data_pos, l_value, l_value_len);
                l_data_pos += l_value_len;
            }
            if (l_sign_len && l_sign) {
                memcpy(l_data_pos, l_sign, l_sign_len);
            }
            
            l_pos += sizeof(dap_global_db_pkt_t) + l_total_data_len;
            DAP_DEL_MULTY(l_text_key, l_value, l_sign);
        }
    }
    
    return l_result;
}
