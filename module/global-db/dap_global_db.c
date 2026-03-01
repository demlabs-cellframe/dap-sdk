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
#include <stddef.h>
#include <string.h>
#include "dap_fnmatch.h"

#include "dap_common.h"
#include "dap_config.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_time.h"
#include "dap_context.h"
#include "dap_worker.h"
#include "dap_cert.h"
#include "dap_enc_ks.h"
#include "dap_proc_thread.h"
#include "dap_global_db.h"
#include "dap_global_db_btree.h"
#include "dap_global_db_wal.h"
#include "dap_global_db_cluster.h"
#include "dap_global_db_pkt.h"
#include "dap_stream.h"
#include "dap_ht.h"

#define LOG_TAG "dap_global_db"

int g_dap_global_db_debug_more = false;

// ============================================================================
// Group management (merged from dap_global_db_storage.c)
// ============================================================================

#define GROUPS_INDEX_FILE   "groups.idx"
#define GROUP_FILE_EXT      ".gdb"
#define GROUP_WAL_EXT       ".gdb.wal"
#define GROUPS_INDEX_MAGIC  0x47444249
#define GROUPS_INDEX_VERSION 1

typedef struct gdb_group {
    char *name;
    dap_global_db_t *btree;
    dap_global_db_wal_t *wal;
    bool is_dirty;
    dap_ht_handle_t hh;
} gdb_group_t;

typedef struct gdb_groups_index_header {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t groups_count;
    uint32_t checksum;
} DAP_ALIGN_PACKED gdb_groups_index_header_t;

static gdb_group_t *s_groups = NULL;
static pthread_rwlock_t s_groups_lock = PTHREAD_RWLOCK_INITIALIZER;
static char *s_storage_path = NULL;
bool g_dap_global_db_wal_enabled = false;

// Queue I/O message op code
enum queue_io_msg_opcode {
    MSG_OPCODE_UNDEFINED = 0,
    MSG_OPCODE_GET,
    MSG_OPCODE_GET_RAW,
    MSG_OPCODE_GET_DEL_TS,
    MSG_OPCODE_GET_LAST,
    MSG_OPCODE_GET_LAST_RAW,
    MSG_OPCODE_GET_ALL,
    MSG_OPCODE_GET_ALL_RAW,
    MSG_OPCODE_SET,
    MSG_OPCODE_SET_RAW,
    MSG_OPCODE_SET_MULTIPLE,
    MSG_OPCODE_PIN,
    MSG_OPCODE_DELETE,
    MSG_OPCODE_FLUSH
};

// Queue i/o message
struct queue_io_msg{
    enum queue_io_msg_opcode opcode; // Opcode

    // For each message opcode we have only one callback
    union{
        dap_global_db_callback_t             callback;
        dap_global_db_callback_result_t      callback_result;
        dap_global_db_callback_result_raw_t  callback_result_raw;
        dap_global_db_callback_results_t     callback_results;
        dap_global_db_callback_results_raw_t callback_results_raw;
    };
    // Custom argument passed to the callback
    void *callback_arg;
    union {
        struct { // Get all request
            dap_global_db_hash_t last_hash;
            uint64_t values_page_size;
            uint64_t total_records;
            uint64_t processed_records;
        };
        struct { // Raw set request
            dap_global_db_store_obj_t *values_raw;
            uint64_t values_raw_total;
        };
        struct { // Set multiply zero-copy
            dap_global_db_obj_t *values;
            uint64_t values_count;
        };
        struct { // Value for singe request
            void *value;
            size_t value_length;
            bool value_is_pinned;
            char *group;  // Group
            char *key; // Key
        };
    };
    dap_global_db_instance_t *dbi;
};

static pthread_cond_t s_check_db_cond = PTHREAD_COND_INITIALIZER; // Check version condition
static pthread_mutex_t s_check_db_mutex = PTHREAD_MUTEX_INITIALIZER; // Check version condition mutex
#define INVALID_RETCODE +100500
static int s_check_db_ret = INVALID_RETCODE; // Check version return value
static dap_timerfd_t* s_check_pinned_db_objs_timer;
static dap_nanotime_t s_minimal_ttl = 3600000000000;  //def half an hour
static size_t s_gdb_auto_clean_period = 3600 / 2;  // def half an hour

static dap_global_db_instance_t *s_dbi = NULL; // GlobalDB instance is only static now

// Version check& update functiosn
static int s_check_db_version();
static void s_check_db_version_callback_get (dap_global_db_instance_t *a_dbi, int a_errno, const char * a_group, const char * a_key,
                                             const void * a_value, const size_t a_value_len,
                                             dap_nanotime_t value_ts,bool a_is_pinned, void * a_arg);
static void s_check_db_version_callback_set (dap_global_db_instance_t *a_dbi, int a_errno, const char * a_group, const char * a_key,
                                             const void * a_value, const size_t a_value_len,
                                             dap_nanotime_t value_ts,bool a_is_pinned, void * a_arg);
// GlobalDB context start/stop callbacks
static void s_context_callback_started( dap_context_t * a_context, void *a_arg);
static int s_context_callback_stopped( dap_context_t * a_context, void *a_arg);

static int s_gdb_clean_init();
static void s_gdb_clean_deinit();
static void s_check_pinned_db_objs_deinit();

static int s_pinned_objs_group_init();
static int s_add_pinned_obj_in_pinned_group(dap_global_db_store_obj_t * a_objs);
static void s_del_pinned_obj_from_pinned_group_by_source_group(const char * a_group, const char* a_key);
static bool s_check_is_obj_pinned(const char * a_group, const char * a_key);
DAP_STATIC_INLINE char *dap_get_local_pinned_groups_mask(const char *a_group);
DAP_STATIC_INLINE char *dap_get_group_from_pinned_groups_mask(const char *a_group);

// Opcode to string
static const char *s_msg_opcode_to_str(enum queue_io_msg_opcode a_opcode);

// Queue i/o processing callback
static bool s_queue_io_callback(void *a_arg);

// Queue i/o message processing functions
static void s_msg_opcode_get(struct queue_io_msg * a_msg);
static void s_msg_opcode_get_raw(struct queue_io_msg * a_msg);
static void s_msg_opcode_get_del_ts(struct queue_io_msg * a_msg);
static void s_msg_opcode_get_last(struct queue_io_msg * a_msg);
static void s_msg_opcode_get_last_raw(struct queue_io_msg * a_msg);
static bool s_msg_opcode_get_all(struct queue_io_msg * a_msg);
static bool s_msg_opcode_get_all_raw(struct queue_io_msg * a_msg);
static void s_msg_opcode_set(struct queue_io_msg * a_msg);
static void s_msg_opcode_set_raw(struct queue_io_msg * a_msg);
static void s_msg_opcode_set_multiple_zc(struct queue_io_msg * a_msg);
static void s_msg_opcode_pin(struct queue_io_msg * a_msg);
static void s_msg_opcode_delete(struct queue_io_msg * a_msg);
static void s_msg_opcode_flush(struct queue_io_msg * a_msg);

// Free memor for queue i/o message
static void s_queue_io_msg_delete( struct queue_io_msg * a_msg);

// convert dap_global_db_store_obj_t to dap_global_db_obj_t
static dap_global_db_obj_t* s_objs_from_store_objs(dap_global_db_store_obj_t *a_store_objs, size_t a_values_count);

// ============================================================================
// Group management implementation (merged from dap_global_db_storage.c)
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

static int s_wal_replay_cb(dap_global_db_wal_op_t a_op,
                           const byte_t *a_data, size_t a_len, void *a_arg)
{
    dap_global_db_t *l_btree = (dap_global_db_t *)a_arg;
    if (a_op == DAP_GLOBAL_DB_WAL_OP_INSERT) {
        if (a_len < sizeof(dap_global_db_key_t) + 1 + 12)
            return -1;
        const byte_t *p = a_data;
        dap_global_db_key_t l_key;
        memcpy(&l_key, p, sizeof(l_key)); p += sizeof(l_key);
        uint8_t l_flags = *p++;
        uint32_t l_key_len; memcpy(&l_key_len, p, 4); p += 4;
        const char *l_text_key = l_key_len ? (const char *)p : NULL; p += l_key_len;
        uint32_t l_val_len; memcpy(&l_val_len, p, 4); p += 4;
        const void *l_value = l_val_len ? p : NULL; p += l_val_len;
        uint32_t l_sign_len; memcpy(&l_sign_len, p, 4); p += 4;
        const void *l_sign = l_sign_len ? p : NULL;
        return dap_global_db_insert(l_btree, &l_key, l_text_key, l_key_len,
                                     l_value, l_val_len, l_sign, l_sign_len, l_flags);
    } else if (a_op == DAP_GLOBAL_DB_WAL_OP_DELETE) {
        if (a_len < sizeof(dap_global_db_key_t))
            return -1;
        dap_global_db_key_t l_key;
        memcpy(&l_key, a_data, sizeof(l_key));
        return dap_global_db_delete(l_btree, &l_key);
    }
    return 0;
}

static gdb_group_t *s_group_open(const char *a_group_name, bool a_create)
{
    if (!a_group_name || !s_storage_path)
        return NULL;

    gdb_group_t *l_group = NULL;
    dap_ht_find_str(s_groups, a_group_name, l_group);
    if (l_group)
        return l_group;

    char *l_filepath = s_group_filepath(a_group_name);
    if (!l_filepath)
        return NULL;

    dap_global_db_t *l_btree = dap_global_db_open(l_filepath, false);
    if (!l_btree && a_create)
        l_btree = dap_global_db_create(l_filepath);
    DAP_DELETE(l_filepath);

    if (!l_btree)
        return NULL;

    l_group = DAP_NEW_Z(gdb_group_t);
    l_group->name = dap_strdup(a_group_name);
    l_group->btree = l_btree;
    l_group->is_dirty = false;

    if (g_dap_global_db_wal_enabled) {
        char *l_wal_path = dap_strdup_printf("%s/%s%s", s_storage_path,
                                              s_sanitize_name(a_group_name), GROUP_WAL_EXT);
        l_group->wal = dap_global_db_wal_open(l_wal_path);
        DAP_DELETE(l_wal_path);
        if (l_group->wal) {
            int l_replayed = dap_global_db_wal_recover(l_group->wal, s_wal_replay_cb, l_btree);
            if (l_replayed > 0) {
                log_it(L_INFO, "WAL recovery for group '%s': replayed %d records", a_group_name, l_replayed);
                dap_global_db_sync(l_btree);
                dap_global_db_wal_checkpoint(l_group->wal);
            }
        }
    }

    dap_ht_add_str(s_groups, name, l_group);
    return l_group;
}

static void s_group_close(gdb_group_t *a_group)
{
    if (!a_group)
        return;
    if (a_group->wal) {
        dap_global_db_wal_checkpoint(a_group->wal);
        dap_global_db_wal_close(a_group->wal);
    }
    if (a_group->btree)
        dap_global_db_close(a_group->btree);
    DAP_DEL_Z(a_group->name);
    DAP_DELETE(a_group);
}

static int s_groups_index_load(void)
{
    char *l_index_path = dap_strdup_printf("%s/%s", s_storage_path, GROUPS_INDEX_FILE);
    FILE *l_fp = fopen(l_index_path, "rb");
    if (!l_fp) {
        DAP_DELETE(l_index_path);
        return 0;
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

    uint32_t l_count = dap_ht_count(s_groups);
    gdb_groups_index_header_t l_header = {
        .magic = GROUPS_INDEX_MAGIC,
        .version = GROUPS_INDEX_VERSION,
        .groups_count = l_count,
        .reserved = 0,
        .checksum = 0
    };

    fwrite(&l_header, sizeof(l_header), 1, l_fp);
    gdb_group_t *l_group, *l_tmp;
    dap_ht_foreach(s_groups, l_group, l_tmp) {
        uint16_t l_name_len = strlen(l_group->name);
        fwrite(&l_name_len, sizeof(l_name_len), 1, l_fp);
        fwrite(l_group->name, l_name_len, 1, l_fp);
    }

    fclose(l_fp);
    DAP_DELETE(l_index_path);
    return 0;
}

static dap_global_db_store_obj_t *s_btree_entry_to_store_obj(const char *a_group,
                                                    dap_global_db_key_t *a_key,
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
// Storage init/deinit (internal, called from dap_global_db_init/deinit)
// ============================================================================

static int s_storage_init(const char *a_storage_path)
{
    if (s_storage_path) {
        log_it(L_WARNING, "Storage already initialized");
        return 0;
    }
    s_storage_path = dap_strdup_printf("%s/gdb-native", a_storage_path);
    if (!dap_dir_test(s_storage_path)) {
        if (dap_mkdir_with_parents(s_storage_path) != 0) {
            log_it(L_ERROR, "Failed to create storage directory: %s", s_storage_path);
            DAP_DEL_Z(s_storage_path);
            return -1;
        }
    }
    pthread_rwlock_init(&s_groups_lock, NULL);
    if (s_groups_index_load() != 0)
        log_it(L_WARNING, "Failed to load groups index, starting fresh");
    log_it(L_INFO, "Native GlobalDB storage initialized at %s", s_storage_path);
    return 0;
}

static void s_storage_deinit(void)
{
    if (!s_storage_path)
        return;
    pthread_rwlock_wrlock(&s_groups_lock);
    s_groups_index_save();
    gdb_group_t *l_group, *l_tmp;
    dap_ht_foreach(s_groups, l_group, l_tmp) {
        dap_ht_del(s_groups, l_group);
        s_group_close(l_group);
    }
    pthread_rwlock_unlock(&s_groups_lock);
    pthread_rwlock_destroy(&s_groups_lock);
    DAP_DEL_Z(s_storage_path);
    log_it(L_INFO, "Native GlobalDB storage deinitialized");
}

static int s_storage_flush(void)
{
    pthread_rwlock_rdlock(&s_groups_lock);
    gdb_group_t *l_group, *l_tmp;
    dap_ht_foreach(s_groups, l_group, l_tmp) {
        if (l_group->btree)
            dap_global_db_sync(l_group->btree);
        if (l_group->wal)
            dap_global_db_wal_checkpoint(l_group->wal);
    }
    pthread_rwlock_unlock(&s_groups_lock);
    return 0;
}

// ============================================================================
// Group access (static + public)
// ============================================================================

static gdb_group_t *s_group_find(const char *a_group_name, bool a_create)
{
    if (!a_group_name)
        return NULL;
    pthread_rwlock_rdlock(&s_groups_lock);
    gdb_group_t *l_group = NULL;
    dap_ht_find_str(s_groups, a_group_name, l_group);
    pthread_rwlock_unlock(&s_groups_lock);
    if (!l_group) {
        pthread_rwlock_wrlock(&s_groups_lock);
        dap_ht_find_str(s_groups, a_group_name, l_group);
        if (!l_group)
            l_group = s_group_open(a_group_name, a_create);
        pthread_rwlock_unlock(&s_groups_lock);
    }
    return l_group;
}

static dap_global_db_t *s_group_get(const char *a_group_name)
{
    gdb_group_t *l_group = s_group_find(a_group_name, false);
    return l_group ? l_group->btree : NULL;
}

static dap_global_db_t *s_group_get_or_create(const char *a_group_name)
{
    gdb_group_t *l_group = s_group_find(a_group_name, true);
    return l_group ? l_group->btree : NULL;
}

dap_list_t *dap_global_db_get_groups_by_mask(const char *a_mask)
{
    dap_return_val_if_fail(a_mask, NULL);
    dap_list_t *l_result = NULL;
    pthread_rwlock_rdlock(&s_groups_lock);
    gdb_group_t *l_group, *l_tmp;
    dap_ht_foreach(s_groups, l_group, l_tmp) {
        if (dap_fnmatch(a_mask, l_group->name, 0) == 0)
            l_result = dap_list_append(l_result, dap_strdup(l_group->name));
    }
    pthread_rwlock_unlock(&s_groups_lock);
    return l_result;
}

uint64_t dap_global_db_group_count(const char *a_group_name, bool a_with_deleted)
{
    dap_global_db_t *l_btree = s_group_get(a_group_name);
    if (!l_btree)
        return 0;
    if (a_with_deleted)
        return dap_global_db_count(l_btree);
    dap_global_db_cursor_t *l_cursor = dap_global_db_cursor_create(l_btree);
    if (!l_cursor)
        return 0;
    uint64_t l_count = 0;
    if (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_FIRST, NULL) == 0) {
        do {
            uint8_t l_flags = 0;
            if (dap_global_db_cursor_get(l_cursor, NULL, NULL, NULL, NULL, NULL, NULL, &l_flags) == 0) {
                if (!(l_flags & DAP_GLOBAL_DB_RECORD_DEL))
                    l_count++;
            }
        } while (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_NEXT, NULL) == 0);
    }
    dap_global_db_cursor_close(l_cursor);
    return l_count;
}

// ============================================================================
// Storage record operations (static)
// ============================================================================

static dap_global_db_store_obj_t *s_storage_read_by_key(const char *a_group, const char *a_key,
                                              bool a_with_deleted)
{
    dap_return_val_if_fail(a_group && a_key, NULL);
    dap_global_db_t *l_btree = s_group_get(a_group);
    if (!l_btree)
        return NULL;
    dap_global_db_cursor_t *l_cursor = dap_global_db_cursor_create(l_btree);
    if (!l_cursor)
        return NULL;
    dap_global_db_store_obj_t *l_result = NULL;
    if (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_FIRST, NULL) == 0) {
        do {
            dap_global_db_key_t l_drv_key;
            char *l_text_key = NULL;
            void *l_value = NULL;
            uint32_t l_value_len = 0;
            void *l_sign = NULL;
            uint32_t l_sign_len = 0;
            uint8_t l_flags = 0;
            if (dap_global_db_cursor_get(l_cursor, &l_drv_key, &l_text_key,
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
        } while (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_NEXT, NULL) == 0);
    }
    dap_global_db_cursor_close(l_cursor);
    return l_result;
}

static dap_global_db_store_obj_t *s_storage_read_last(const char *a_group, bool a_with_deleted)
{
    dap_return_val_if_fail(a_group, NULL);
    dap_global_db_t *l_btree = s_group_get(a_group);
    if (!l_btree)
        return NULL;
    dap_global_db_cursor_t *l_cursor = dap_global_db_cursor_create(l_btree);
    if (!l_cursor)
        return NULL;
    dap_global_db_store_obj_t *l_result = NULL;
    if (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_LAST, NULL) == 0) {
        do {
            dap_global_db_key_t l_drv_key;
            char *l_text_key = NULL;
            void *l_value = NULL;
            uint32_t l_value_len = 0;
            void *l_sign = NULL;
            uint32_t l_sign_len = 0;
            uint8_t l_flags = 0;
            if (dap_global_db_cursor_get(l_cursor, &l_drv_key, &l_text_key,
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
        } while (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_PREV, NULL) == 0);
    }
    dap_global_db_cursor_close(l_cursor);
    return l_result;
}

static dap_global_db_store_obj_t *s_storage_read_all(const char *a_group, size_t *a_count_out,
                                           bool a_with_deleted)
{
    dap_return_val_if_fail(a_group, NULL);
    dap_global_db_t *l_btree = s_group_get(a_group);
    if (!l_btree) {
        log_it(L_DEBUG, "s_storage_read_all: group=%s btree=NULL", a_group);
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    uint64_t l_total = dap_global_db_count(l_btree);
    log_it(L_DEBUG, "s_storage_read_all: group=%s count=%"PRIu64, a_group, l_total);
    if (l_total == 0) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    dap_global_db_store_obj_t *l_results = DAP_NEW_Z_COUNT(dap_global_db_store_obj_t, l_total);
    if (!l_results) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    dap_global_db_cursor_t *l_cursor = dap_global_db_cursor_create(l_btree);
    if (!l_cursor) {
        DAP_DELETE(l_results);
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    size_t l_count = 0;
    if (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_FIRST, NULL) == 0) {
        do {
            dap_global_db_key_t l_drv_key;
            char *l_text_key = NULL;
            void *l_value = NULL;
            uint32_t l_value_len = 0;
            void *l_sign = NULL;
            uint32_t l_sign_len = 0;
            uint8_t l_flags = 0;
            if (dap_global_db_cursor_get(l_cursor, &l_drv_key, &l_text_key,
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
        } while (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_NEXT, NULL) == 0);
    }
    dap_global_db_cursor_close(l_cursor);
    if (a_count_out) *a_count_out = l_count;
    if (l_count == 0) {
        DAP_DELETE(l_results);
        return NULL;
    }
    return l_results;
}

static dap_global_db_store_obj_t *s_storage_read_cond(const char *a_group,
                                            dap_global_db_hash_t a_hash_from,
                                            size_t a_max_count,
                                            size_t *a_count_out,
                                            bool a_with_deleted)
{
    dap_return_val_if_fail(a_group, NULL);
    dap_global_db_t *l_btree = s_group_get(a_group);
    if (!l_btree) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    dap_global_db_cursor_t *l_cursor = dap_global_db_cursor_create(l_btree);
    if (!l_cursor) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    dap_global_db_key_t l_start_key = {
        .bets = a_hash_from.bets,
        .becrc = a_hash_from.becrc
    };
    int l_rc;
    if (dap_global_db_hash_is_blank(&a_hash_from))
        l_rc = dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_FIRST, NULL);
    else
        l_rc = dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_SET_UPPERBOUND, &l_start_key);
    if (l_rc != 0) {
        dap_global_db_cursor_close(l_cursor);
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    if (a_max_count == 0)
        a_max_count = DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT;
    dap_global_db_store_obj_t *l_results = DAP_NEW_Z_COUNT(dap_global_db_store_obj_t, a_max_count);
    if (!l_results) {
        dap_global_db_cursor_close(l_cursor);
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    size_t l_count = 0;
    do {
        if (l_count >= a_max_count)
            break;
        dap_global_db_key_t l_drv_key;
        char *l_text_key = NULL;
        void *l_value = NULL;
        uint32_t l_value_len = 0;
        void *l_sign = NULL;
        uint32_t l_sign_len = 0;
        uint8_t l_flags = 0;
        if (dap_global_db_cursor_get(l_cursor, &l_drv_key, &l_text_key,
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
    } while (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_NEXT, NULL) == 0);
    dap_global_db_cursor_close(l_cursor);
    if (a_count_out) *a_count_out = l_count;
    if (l_count == 0) {
        DAP_DELETE(l_results);
        return NULL;
    }
    return l_results;
}

static dap_global_db_store_obj_t *s_storage_read_below_timestamp(const char *a_group,
                                                       dap_nanotime_t a_timestamp,
                                                       size_t *a_count_out)
{
    dap_return_val_if_fail(a_group, NULL);
    dap_global_db_t *l_btree = s_group_get(a_group);
    if (!l_btree) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    dap_global_db_cursor_t *l_cursor = dap_global_db_cursor_create(l_btree);
    if (!l_cursor) {
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    size_t l_alloc = DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT;
    dap_global_db_store_obj_t *l_results = DAP_NEW_Z_COUNT(dap_global_db_store_obj_t, l_alloc);
    if (!l_results) {
        dap_global_db_cursor_close(l_cursor);
        if (a_count_out) *a_count_out = 0;
        return NULL;
    }
    size_t l_count = 0;
    if (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_FIRST, NULL) == 0) {
        do {
            dap_global_db_key_t l_drv_key;
            char *l_text_key = NULL;
            void *l_value = NULL;
            uint32_t l_value_len = 0;
            void *l_sign = NULL;
            uint32_t l_sign_len = 0;
            uint8_t l_flags = 0;
            if (dap_global_db_cursor_get(l_cursor, &l_drv_key, &l_text_key,
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
        } while (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_NEXT, NULL) == 0);
    }
    dap_global_db_cursor_close(l_cursor);
    if (a_count_out) *a_count_out = l_count;
    if (l_count == 0) {
        DAP_DELETE(l_results);
        return NULL;
    }
    return l_results;
}

static int s_storage_write(dap_global_db_store_obj_t *a_obj)
{
    dap_return_val_if_fail(a_obj && a_obj->group, -1);
    gdb_group_t *l_group = s_group_find(a_obj->group, true);
    if (!l_group || !l_group->btree) {
        log_it(L_ERROR, "Failed to get/create group '%s'", a_obj->group);
        return -2;
    }
    dap_global_db_key_t l_key = {
        .bets = htobe64(a_obj->timestamp),
        .becrc = htobe64(a_obj->crc)
    };
    dap_global_db_hash_t l_hash = { .bets = l_key.bets, .becrc = l_key.becrc };
    if (a_obj->flags & DAP_GLOBAL_DB_RECORD_ERASE) {
        if (l_group->wal)
            dap_global_db_wal_delete(l_group->wal, l_hash);
        return dap_global_db_delete(l_group->btree, &l_key);
    }
    uint32_t l_sign_len = a_obj->sign ? dap_sign_get_size(a_obj->sign) : 0;
    uint32_t l_key_len = a_obj->key ? strlen(a_obj->key) + 1 : 0;
    if (l_group->wal)
        dap_global_db_wal_write(l_group->wal, l_hash, a_obj->key,
                                 a_obj->value, a_obj->value_len,
                                 a_obj->sign, l_sign_len, a_obj->flags);
    return dap_global_db_insert(l_group->btree, &l_key,
                                 a_obj->key, l_key_len,
                                 a_obj->value, a_obj->value_len,
                                 a_obj->sign, l_sign_len,
                                 a_obj->flags);
}

static int s_storage_erase(const char *a_group, dap_global_db_hash_t a_hash)
{
    dap_return_val_if_fail(a_group, -1);
    gdb_group_t *l_group = s_group_find(a_group, false);
    if (!l_group || !l_group->btree)
        return 1;
    if (l_group->wal)
        dap_global_db_wal_delete(l_group->wal, a_hash);
    dap_global_db_key_t l_key = {
        .bets = a_hash.bets,
        .becrc = a_hash.becrc
    };
    return dap_global_db_delete(l_group->btree, &l_key);
}

static bool s_storage_exists_key(const char *a_group, const char *a_key)
{
    dap_global_db_store_obj_t *l_obj = s_storage_read_by_key(a_group, a_key, true);
    if (l_obj) {
        dap_global_db_store_obj_free(l_obj, 1);
        return true;
    }
    return false;
}

bool dap_global_db_exists_hash(const char *a_group, dap_global_db_hash_t a_hash)
{
    dap_global_db_t *l_btree = s_group_get(a_group);
    if (!l_btree)
        return false;
    dap_global_db_key_t l_key = {
        .bets = a_hash.bets,
        .becrc = a_hash.becrc
    };
    return dap_global_db_exists(l_btree, &l_key);
}

// ============================================================================
// Sync protocol helpers (public, used by ch)
// ============================================================================

dap_global_db_hash_pkt_t *dap_global_db_read_hashes(const char *a_group,
                                                       dap_global_db_hash_t a_hash_from)
{
    dap_return_val_if_fail(a_group, NULL);
    dap_global_db_t *l_btree = s_group_get(a_group);
    if (!l_btree)
        return NULL;
    dap_global_db_cursor_t *l_cursor = dap_global_db_cursor_create(l_btree);
    if (!l_cursor)
        return NULL;
    dap_global_db_key_t l_start_key = {
        .bets = a_hash_from.bets,
        .becrc = a_hash_from.becrc
    };
    int l_rc;
    if (dap_global_db_hash_is_blank(&a_hash_from))
        l_rc = dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_FIRST, NULL);
    else
        l_rc = dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_SET_UPPERBOUND, &l_start_key);
    if (l_rc != 0) {
        dap_global_db_cursor_close(l_cursor);
        return NULL;
    }
    size_t l_count = 0;
    size_t l_max_count = DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT;
    do {
        l_count++;
        if (l_count >= l_max_count)
            break;
    } while (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_NEXT, NULL) == 0);
    if (l_count == 0) {
        dap_global_db_cursor_close(l_cursor);
        return NULL;
    }
    size_t l_group_len = strlen(a_group) + 1;
    size_t l_alloc_size = sizeof(dap_global_db_hash_pkt_t) + l_group_len +
                          (l_count + 1) * sizeof(dap_global_db_hash_t);
    dap_global_db_hash_pkt_t *l_result = DAP_NEW_Z_SIZE(dap_global_db_hash_pkt_t, l_alloc_size);
    if (!l_result) {
        dap_global_db_cursor_close(l_cursor);
        return NULL;
    }
    l_result->group_name_len = l_group_len;
    l_result->hashes_count = l_count;
    memcpy(l_result->group_n_hashses, a_group, l_group_len);
    dap_global_db_hash_t *l_hashes = (dap_global_db_hash_t *)(l_result->group_n_hashses + l_group_len);
    if (dap_global_db_hash_is_blank(&a_hash_from))
        l_rc = dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_FIRST, NULL);
    else
        l_rc = dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_SET_UPPERBOUND, &l_start_key);
    size_t i = 0;
    if (l_rc == 0) {
        do {
            dap_global_db_key_t l_key;
            if (dap_global_db_cursor_get(l_cursor, &l_key, NULL, NULL, NULL, NULL, NULL, NULL) == 0) {
                l_hashes[i].bets = l_key.bets;
                l_hashes[i].becrc = l_key.becrc;
                i++;
            }
            if (i >= l_count)
                break;
        } while (dap_global_db_cursor_move(l_cursor, DAP_GLOBAL_DB_NEXT, NULL) == 0);
    }
    l_hashes[i] = c_dap_global_db_hash_blank;
    dap_global_db_cursor_close(l_cursor);
    return l_result;
}

dap_global_db_pkt_pack_t *dap_global_db_get_by_hash(const char *a_group,
                                                       dap_global_db_hash_t *a_hashes,
                                                       size_t a_count)
{
    dap_return_val_if_fail(a_group && a_hashes && a_count, NULL);
    dap_global_db_t *l_btree = s_group_get(a_group);
    if (!l_btree)
        return NULL;
    size_t l_group_len = strlen(a_group) + 1;
    size_t l_total_size = 0;
    size_t l_valid_count = 0;
    for (size_t i = 0; i < a_count; i++) {
        dap_global_db_key_t l_key = {
            .bets = a_hashes[i].bets,
            .becrc = a_hashes[i].becrc
        };
        char *l_text_key = NULL;
        void *l_value = NULL;
        uint32_t l_value_len = 0;
        void *l_sign = NULL;
        uint32_t l_sign_len = 0;
        if (dap_global_db_fetch(l_btree, &l_key, &l_text_key, &l_value, &l_value_len,
                              &l_sign, &l_sign_len, NULL) == 0) {
            size_t l_key_len = l_text_key ? strlen(l_text_key) + 1 : 0;
            l_total_size += sizeof(dap_global_db_pkt_t) + l_group_len + l_key_len + l_value_len + l_sign_len;
            l_valid_count++;
            DAP_DEL_MULTY(l_text_key, l_value, l_sign);
        }
    }
    if (l_valid_count == 0)
        return NULL;
    dap_global_db_pkt_pack_t *l_result = DAP_NEW_Z_SIZE(dap_global_db_pkt_pack_t,
                                                         sizeof(dap_global_db_pkt_pack_t) + l_total_size);
    if (!l_result)
        return NULL;
    l_result->data_size = l_total_size;
    l_result->obj_count = l_valid_count;
    byte_t *l_pos = l_result->data;
    for (size_t i = 0; i < a_count; i++) {
        dap_global_db_key_t l_key = {
            .bets = a_hashes[i].bets,
            .becrc = a_hashes[i].becrc
        };
        char *l_text_key = NULL;
        void *l_value = NULL;
        uint32_t l_value_len = 0;
        void *l_sign = NULL;
        uint32_t l_sign_len = 0;
        uint8_t l_flags = 0;
        if (dap_global_db_fetch(l_btree, &l_key, &l_text_key, &l_value, &l_value_len,
                              &l_sign, &l_sign_len, &l_flags) == 0) {
            size_t l_key_len = l_text_key ? strlen(l_text_key) + 1 : 0;
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
            if (l_sign_len && l_sign)
                memcpy(l_data_pos, l_sign, l_sign_len);
            l_pos += sizeof(dap_global_db_pkt_t) + l_total_data_len;
            DAP_DEL_MULTY(l_text_key, l_value, l_sign);
        }
    }
    return l_result;
}

// Lightweight storage init for standalone tools (migrate)
int dap_global_db_groups_init(const char *a_storage_path)
{
    return s_storage_init(a_storage_path);
}

void dap_global_db_groups_deinit(void)
{
    s_storage_deinit();
}

int dap_global_db_groups_flush(void)
{
    return s_storage_flush();
}

dap_global_db_t *dap_global_db_group_get_or_create(const char *a_group_name)
{
    return s_group_get_or_create(a_group_name);
}


// ============================================================================
// Main GlobalDB API
// ============================================================================

int dap_global_db_init()
{
    int l_rc = 0;

    // Debug config
    g_dap_global_db_debug_more = dap_config_get_item_bool_default(g_config, "global_db", "debug_more", false);
    g_dap_global_db_wal_enabled = dap_config_get_item_bool_default(g_config, "global_db", "wal_enabled", false);

    if (s_dbi)
        return 0;

    // Create and run its own context
    if (s_dbi == NULL) {
        s_dbi = DAP_NEW_Z(dap_global_db_instance_t);
        if (!s_dbi) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            l_rc = -5;
            goto lb_return;
        }

        char *l_gdb_path_cfg = dap_config_get_item_str_path_default(g_config, "global_db", "path", NULL);
        s_dbi->storage_path = l_gdb_path_cfg ? l_gdb_path_cfg : dap_strdup_printf("%s/var/lib/global_db", g_sys_dir_path);

        dap_cert_t *l_signing_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
        if (l_signing_cert)
            s_dbi->signing_key = l_signing_cert->enc_key;
        else
            log_it(L_ERROR, "Can't find node addr cerificate, all new records will be usigned");

        uint16_t l_size_ban_list = 0, l_size_white_list = 0;
        const char **l_ban_list = dap_config_get_array_str(g_config, "global_db", "ban_list_sync_groups", &l_size_ban_list);
        for (int i = 0; i < l_size_ban_list; i++)
            s_dbi->blacklist = dap_list_append(s_dbi->blacklist, dap_strdup(l_ban_list[i]));
        const char **l_white_list = dap_config_get_array_str(g_config, "global_db", "white_list_sync_groups", &l_size_white_list);
        for (int i = 0; i < l_size_white_list; i++)
            s_dbi->whitelist = dap_list_append(s_dbi->whitelist, dap_strdup(l_white_list[i]));
        // One week for objects lifetime by default
        s_dbi->store_time_limit = dap_config_get_item_uint64(g_config, "global_db", "ttl");
        // Time between sync attempts, in seconds
        s_dbi->sync_idle_time = dap_config_get_item_uint32_default(g_config, "global_db", "sync_idle_time", 30);
    }

    // Native storage initialization
    if ( (l_rc = s_storage_init(s_dbi->storage_path)) )
        return log_it(L_CRITICAL, "Hadn't initialized native storage on path \"%s\", code: %d",
                       s_dbi->storage_path, l_rc), l_rc;

    // Clusters initialization
    if ( (l_rc = dap_global_db_cluster_init()) )
        return log_it(L_CRITICAL, "Can't initialize GlobalDB clusters"), l_rc;
    // Check version and update if need it
    if ( (l_rc = s_check_db_version()) )
        return log_it(L_ERROR, "GlobalDB version changed, please export or remove old version!"), l_rc;
    

lb_return:
    if (l_rc == 0 )
        log_it(L_NOTICE, "GlobalDB initialized");
    else
        log_it(L_CRITICAL, "GlobalDB wasn't initialized, code %d", l_rc);

    return l_rc;
}

int dap_global_db_clean_init() {
    int l_rc = 0;
    if ( (l_rc = s_pinned_objs_group_init()))
        return log_it(L_ERROR, "GlobalDB pinned objs init failed"), l_rc;

    if ( (l_rc = s_gdb_clean_init()))
        return log_it(L_ERROR, "GlobalDB clean init failed"), l_rc;

    return l_rc;
}

int dap_global_db_clean_deinit() {
    s_check_pinned_db_objs_deinit();
    s_gdb_clean_deinit();
    return 0;
}

/**
 * @brief kill context thread and clean context
 */
void dap_global_db_instance_deinit()
{
    dap_return_if_fail(s_dbi)
    dap_list_free_full(s_dbi->blacklist, NULL);
    dap_list_free_full(s_dbi->whitelist, NULL);
    DAP_DEL_Z(s_dbi->storage_path);
    DAP_DEL_Z(s_dbi);
}

inline dap_global_db_instance_t *dap_global_db_instance_get_default()
{
    return s_dbi;
}

/**
 * @brief dap_global_db_deinit, after fix ticket 9030 need add dap_global_db_instance_deinit()
 */
void dap_global_db_deinit() {
    dap_global_db_clean_deinit();
    dap_global_db_cluster_deinit();
    s_storage_deinit();
    dap_global_db_instance_deinit();
}

bool dap_global_db_group_match_mask(const char *a_group, const char *a_mask)
{
    dap_return_val_if_fail(a_group && a_mask && *a_group && *a_mask, false);
    const char *l_group_tail = a_group + strlen(a_group);           // Pointer to trailng zero
    const char *l_mask_tail = a_mask + strlen(a_mask);
    const char *l_group_it = a_group, *l_mask_it = a_mask;
    const char *l_wildcard = strchr(a_mask, '*');
    while (l_mask_it < (l_wildcard ? l_wildcard : l_mask_tail) &&
                l_group_it < l_group_tail)
        if (*l_group_it++ != *l_mask_it++)
            return false;
    if (l_mask_it == l_wildcard && ++l_mask_it < l_mask_tail)
        return strstr(l_group_it, l_mask_it);
    return true;
}

static void s_store_obj_update_timestamp(dap_global_db_store_obj_t *a_obj, dap_global_db_instance_t *a_dbi, dap_nanotime_t a_new_timestamp)
{
    a_obj->timestamp = a_new_timestamp;
    DAP_DEL_Z(a_obj->sign);
    a_obj->crc = 0;
    a_obj->sign = dap_global_db_store_obj_sign(a_obj, a_dbi ? a_dbi->signing_key :  dap_global_db_instance_get_default()->signing_key, &a_obj->crc);
}

static int s_store_obj_apply(dap_global_db_instance_t *a_dbi, dap_global_db_store_obj_t *a_obj)
{
    log_it(L_DEBUG, "s_store_obj_apply: group=%s key=%s value_len=%zu flags=0x%x",
           a_obj->group, a_obj->key, a_obj->value_len, a_obj->flags);
    dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(a_dbi, a_obj->group);
    if (!l_cluster) {
        log_it(L_WARNING, "An entry in the group %s was rejected because the group name doesn't match any cluster", a_obj->group);
        return -11;
    }
    dap_global_db_hash_t a_obj_drv_hash = dap_global_db_hash_get(a_obj);
    // Check if hash exists in storage
    if (dap_global_db_exists_hash(a_obj->group, a_obj_drv_hash)) {
        debug_if(g_dap_global_db_debug_more, L_NOTICE, "Rejected duplicate object with group %s and key %s",
                                            a_obj->group, a_obj->key);
        return -12;
    }
    // Check time
    dap_nanotime_t l_ttl = dap_nanotime_from_sec(l_cluster->ttl),
                   l_now = dap_nanotime_now();
    if ( a_obj->timestamp > l_now ) {
        if (g_dap_global_db_debug_more) {
            char l_ts_str[DAP_TIME_STR_SIZE];
            dap_time_to_str_rfc822(l_ts_str, sizeof(l_ts_str), dap_nanotime_to_sec(a_obj->timestamp));
            log_it(L_NOTICE, "Rejected record \"%s : %s\" from future ts %s",
                             a_obj->group, a_obj->key, l_ts_str);
        }
        return -13;
    }
    if ( l_ttl && a_obj->timestamp + l_ttl < l_now ) {
        if (g_dap_global_db_debug_more) {
            char l_ts_str[DAP_TIME_STR_SIZE];
            dap_time_to_str_rfc822(l_ts_str, sizeof(l_ts_str), dap_nanotime_to_sec(a_obj->timestamp));
            log_it(L_NOTICE, "Rejected too old record \"%s : %s\" ts %s",
                            a_obj->group, a_obj->key, l_ts_str);
        }
        return -13;
    }

    dap_global_db_role_t l_signer_role = DAP_GDB_MEMBER_ROLE_INVALID;
    dap_stream_node_addr_t l_signer_addr = {0};
    if (a_obj->sign) {
        l_signer_addr = dap_stream_node_addr_from_sign(a_obj->sign);
        l_signer_role = dap_cluster_member_find_role(l_cluster->role_cluster, &l_signer_addr);
    }
    if (l_signer_role == DAP_GDB_MEMBER_ROLE_INVALID)
        l_signer_role = l_cluster->default_role;
    if (l_signer_role < DAP_GDB_MEMBER_ROLE_USER) {
        char *l_signer_addr_str = dap_stream_node_addr_to_str_static(l_signer_addr);
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Global DB record with group %s and key %s is rejected "
                                                        "with signer role %s with no write access to cluster. Signer addr: %s",
                                                            a_obj->group, a_obj->key,
                                                            dap_global_db_cluster_role_str(l_signer_role), l_signer_addr_str);
        return -14;
    }

    dap_global_db_role_t l_required_role = DAP_GDB_MEMBER_ROLE_USER;
    dap_global_db_optype_t l_obj_type = dap_global_db_store_obj_get_type(a_obj);
    dap_global_db_store_obj_t *l_read_obj = NULL;
    bool l_existed_obj_pinned = false;
    int l_ret = 0;
    // Search for existing record by text key using storage API
    l_read_obj = s_storage_read_by_key(a_obj->group, a_obj->key, true);
    if (l_read_obj) { // Need to rewrite existed value
        l_required_role = DAP_GDB_MEMBER_ROLE_ROOT;
        if (l_read_obj->flags & DAP_GLOBAL_DB_RECORD_PINNED) {
            l_existed_obj_pinned = true;
        }
    }
    UNUSED(l_existed_obj_pinned);
    if (l_read_obj && l_cluster->owner_root_access &&
            a_obj->sign && (!l_read_obj->sign ||
            dap_sign_compare_pkeys(a_obj->sign, l_read_obj->sign)))
        l_signer_role = DAP_GDB_MEMBER_ROLE_ROOT;
    if (l_signer_role < l_required_role) {
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Global DB record with group %s and key %s is rejected "
                                                        "with signer role %s and required role %s",
                                                            a_obj->group, a_obj->key,
                                                            dap_global_db_cluster_role_str(l_signer_role),
                                                            dap_global_db_cluster_role_str(l_required_role));
        l_ret = -16;
        goto free_n_exit;
    }
    switch (dap_global_db_store_obj_hash_compare(l_read_obj, a_obj)) {
    case 1:         // Received object is older
        if (a_obj->key && (a_obj->flags & DAP_GLOBAL_DB_RECORD_NEW)) {
            dap_nanotime_t l_time_diff = l_read_obj->timestamp - a_obj->timestamp;
            s_store_obj_update_timestamp(a_obj, a_dbi, l_read_obj->timestamp + 1);
            debug_if(g_dap_global_db_debug_more, L_WARNING, "DB record with group %s and key %s need time correction for %"DAP_UINT64_FORMAT_U" seconds to be properly applied",
                                                            a_obj->group, a_obj->key, dap_nanotime_to_sec(l_time_diff));
            if (!a_obj->sign) {
                log_it(L_ERROR, "Can't sign object with group %s and key %s", a_obj->group, a_obj->key);
                return -20;
            }
        } else {
            debug_if(g_dap_global_db_debug_more, L_DEBUG, "DB record with group %s and key %s is not applied. It's older than existed record with same key",
                                                            a_obj->group, a_obj->key);
            l_ret = -18;
        }
        break;
    case 0:         // Objects the same, omg! Use the basic object
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Duplicate record with group %s and key %s not dropped by hash filter",
                                                                    a_obj->group, a_obj->key);
        l_ret = -17;
        break;
    case -1:       // Existed obj is older
        debug_if(g_dap_global_db_debug_more, L_INFO, "Applied new global DB record with type '%c' and group %s and key %s",
                                                                                        l_obj_type, a_obj->group, a_obj->key);
        break;
    default:
        log_it(L_ERROR, "Unexpected comparision result");
        l_ret = -19;
        break;
    }
    log_it(L_DEBUG, "s_store_obj_apply: group=%s key=%s hash_cmp=%d l_ret=%d signer_role=%d",
           a_obj->group, a_obj->key, l_ret ? l_ret : -999, l_ret,
           (int)l_signer_role);
    if (!l_ret) {
        // Apply new object via storage API
        l_ret = s_storage_write(a_obj);
        log_it(L_DEBUG, "s_store_obj_apply: s_storage_write returned %d for group=%s key=%s",
               l_ret, a_obj->group, a_obj->key);

        // if global_db obj is pinned
        if (a_obj->flags & DAP_GLOBAL_DB_RECORD_PINNED) {
            s_add_pinned_obj_in_pinned_group(a_obj);
        // if upin obj
        } else if (l_existed_obj_pinned && !(a_obj->flags & DAP_GLOBAL_DB_RECORD_PINNED)) {
            s_del_pinned_obj_from_pinned_group_by_source_group(a_obj->group, a_obj->key);
        }

        if (l_obj_type != DAP_GLOBAL_DB_OPTYPE_DEL || l_read_obj) {
            // Do not notify for delete if deleted record not exists
            if (a_obj->flags & DAP_GLOBAL_DB_RECORD_NEW)
                // Notify sync cluster first
                dap_global_db_cluster_broadcast(l_cluster, a_obj);
            if (l_cluster->notifiers)
                // Notify others
                dap_global_db_cluster_notify(l_cluster, a_obj);
        }
    }
free_n_exit:
    if (l_read_obj)
        dap_global_db_store_obj_free_one(l_read_obj);
    return l_ret;
}

/* *** Get functions group *** */

byte_t *dap_global_db_get_sync(const char *a_group,
                                 const char *a_key, size_t *a_data_size,
                                 bool *a_is_pinned, dap_nanotime_t *a_ts)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, NULL);
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "get call executes for group \"%s\" and key \"%s\"", a_group, a_key);
    dap_global_db_store_obj_t *l_store_obj = s_storage_read_by_key(a_group, a_key, false);
    if (!l_store_obj)
        return NULL;
    if (a_data_size)
        *a_data_size = l_store_obj->value_len;
    if (a_is_pinned)
        *a_is_pinned = s_check_is_obj_pinned(a_group, a_key);
    if (a_ts)
        *a_ts = l_store_obj->timestamp;
    byte_t *l_res = l_store_obj->value;
    l_store_obj->value = NULL;
    dap_global_db_store_obj_free_one(l_store_obj);
    return l_res;
}

/**
 * @brief dap_global_db_get
 * @details Get record value from GlobalDB group by key
 * @param a_group
 * @param a_key
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get(const char * a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->key = dap_strdup(a_key);
    if (!l_msg->key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_MULTY(l_msg->group, l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_result = a_callback;
    l_msg->callback_arg = a_arg;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get request for %s:%s", a_group, a_key);
    return l_ret;
}

/**
 * @brief s_msg_opcode_get
 * @param a_msg
 * @return
 */
static void s_msg_opcode_get(struct queue_io_msg * a_msg)
{
    size_t l_value_len = 0;
    bool l_pinned = false;
    dap_nanotime_t l_ts = 0;
    byte_t *l_value = dap_global_db_get_sync(a_msg->group, a_msg->key, &l_value_len, &l_pinned, &l_ts);
    if (l_value && l_value_len) {
        if(a_msg->callback_result)
            a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_SUCCESS, a_msg->group, a_msg->key,
                               l_value, l_value_len, l_ts,
                               l_pinned, a_msg->callback_arg);
        DAP_DELETE(l_value);
    } else if(a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_NO_RESULTS, a_msg->group, a_msg->key,
                               NULL, 0, 0,0, a_msg->callback_arg);
}

/* *** Get raw functions group *** */

dap_global_db_store_obj_t *dap_global_db_get_raw_sync(const char *a_group, const char *a_key)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, NULL);
    return s_storage_read_by_key(a_group, a_key, true);
}

/**
 * @brief dap_global_db_get_raw
 * @param a_group
 * @param a_key
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_raw(const char *a_group, const char *a_key, dap_global_db_callback_result_raw_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_RAW;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->key = dap_strdup(a_key);
    if (!l_msg->key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_MULTY(l_msg->group, l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_result_raw = a_callback;
    l_msg->callback_arg = a_arg;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get_raw request for %s:%s", a_group, a_key);
    return l_ret;
}

/**
 * @brief s_msg_opcode_get_raw
 * @param a_msg
 * @return
 */
static void s_msg_opcode_get_raw(struct queue_io_msg * a_msg)
{
    dap_global_db_store_obj_t *l_store_obj = dap_global_db_get_raw_sync(a_msg->group, a_msg->key);

    if(a_msg->callback_result_raw)
        a_msg->callback_result_raw(a_msg->dbi, l_store_obj ? DAP_GLOBAL_DB_RC_SUCCESS:
                                                                      DAP_GLOBAL_DB_RC_NO_RESULTS,
                                                        l_store_obj, a_msg->callback_arg );
    dap_global_db_store_obj_free_one(l_store_obj);
}

/* *** Get_del_ts functions group *** */

dap_nanotime_t dap_global_db_get_del_ts_sync(const char *a_group, const char *a_key)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, 0);
    dap_global_db_store_obj_t *l_store_obj_del = dap_global_db_get_raw_sync(a_group, a_key);
    dap_nanotime_t l_timestamp = 0;
    if (l_store_obj_del) {
        if (l_store_obj_del->flags & DAP_GLOBAL_DB_RECORD_DEL)
            l_timestamp = l_store_obj_del->timestamp;
        dap_global_db_store_obj_free_one(l_store_obj_del);
    }
    return l_timestamp;
}

/**
 * @brief dap_global_db_get_del_ts
 * @param a_group
 * @param a_key
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_del_ts(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_DEL_TS;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->key = dap_strdup(a_key);
    if (!l_msg->key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_MULTY(l_msg->group, l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_result = a_callback;
    l_msg->callback_arg = a_arg;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get_del_ts request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get_del_ts request for \"%s\" group \"%s\" key", a_group, a_key);
    return l_ret;
}

/**
 * @brief s_msg_opcode_get_del_ts
 * @param a_msg
 * @return
 */
static void s_msg_opcode_get_del_ts(struct queue_io_msg * a_msg)
{
    dap_nanotime_t l_timestamp = dap_global_db_get_del_ts_sync(a_msg->group, a_msg->key);
    if(l_timestamp) {
        if(a_msg->callback_result)
            a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_SUCCESS, a_msg->group, a_msg->key,
                               NULL, 0, l_timestamp,
                               false, a_msg->callback_arg );
    } else if(a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_NO_RESULTS, a_msg->group, a_msg->key,
                               NULL, 0, 0,0, a_msg->callback_arg );
}

/* *** Get_last functions group *** */

byte_t *dap_global_db_get_last_sync(const char *a_group, char **a_key, size_t *a_data_size,
                                      bool *a_is_pinned, dap_nanotime_t *a_ts)
{
    dap_return_val_if_fail(s_dbi && a_group, NULL);
    dap_global_db_store_obj_t *l_store_obj = s_storage_read_last(a_group, false);
    if (!l_store_obj)
        return NULL;

    if (a_key) {
        *a_key = dap_strdup(l_store_obj->key);
        if (!*a_key) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            dap_global_db_store_obj_free_one(l_store_obj);
            return NULL;
        }
    }
    if (a_data_size)
        *a_data_size = l_store_obj->value_len;
    if (a_is_pinned)
        *a_is_pinned = s_check_is_obj_pinned(a_group, (const char*)a_key);
    if (a_ts)
        *a_ts = l_store_obj->timestamp;
    byte_t *l_res = l_store_obj->value;
    l_store_obj->value = NULL;
    dap_global_db_store_obj_free_one(l_store_obj);
    return l_res;
}

/**
 * @brief dap_global_db_get_last
 * @details Get the last value in GlobalDB group
 * @param a_group
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_last(const char * a_group, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_LAST;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get_last request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get_last request for \"%s\" group", a_group);
    return l_ret;
}

/**
 * @brief s_msg_opcode_get_last
 * @param a_msg
 * @return
 */
static void s_msg_opcode_get_last(struct queue_io_msg * a_msg)
{
    size_t l_value_len = 0;
    bool l_pinned = false;
    dap_nanotime_t l_ts = 0;
    char *l_key = NULL;
    byte_t *l_value = dap_global_db_get_last_sync(a_msg->group, &l_key, &l_value_len, &l_pinned, &l_ts);
    if (l_value && l_value_len) {
        if(a_msg->callback_result)
            a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_SUCCESS, a_msg->group, l_key,
                               l_value, l_value_len, l_ts,
                               l_pinned, a_msg->callback_arg);
        DAP_DELETE(l_value);
    } else if(a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_NO_RESULTS, a_msg->group, l_key,
                               NULL, 0, 0,0, a_msg->callback_arg );
}

/* *** Get_last_raw functions group *** */

dap_global_db_store_obj_t *dap_global_db_get_last_raw_sync(const char *a_group)
{
    dap_return_val_if_fail(s_dbi && a_group, NULL);
    dap_global_db_store_obj_t *l_ret = s_storage_read_last(a_group, true);
    return l_ret;
}

/**
 * @brief dap_global_db_get_last_raw
 * @param a_group
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_last_raw(const char * a_group, dap_global_db_callback_result_raw_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_LAST_RAW;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_arg = a_arg;
    l_msg->callback_result_raw = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get_last request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get_last request for \"%s\" group", a_group);
    return l_ret;
}

/**
 * @brief s_msg_opcode_get_last_raw
 * @param a_msg
 * @return
 */
static void s_msg_opcode_get_last_raw(struct queue_io_msg * a_msg)
{
    dap_global_db_store_obj_t *l_store_obj = dap_global_db_get_last_raw_sync(a_msg->group);
    if(a_msg->callback_result)
        a_msg->callback_result_raw(a_msg->dbi, l_store_obj ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS, l_store_obj, a_msg->callback_arg);
    dap_global_db_store_obj_free(l_store_obj, 1);
}

/* *** Get_all functions group *** */

dap_global_db_obj_t *dap_global_db_get_all_sync(const char *a_group, size_t *a_objs_count)
{
    dap_return_val_if_fail(s_dbi && a_group, NULL);
    size_t l_values_count = 0;
    dap_global_db_store_obj_t *l_store_objs = s_storage_read_all(a_group, &l_values_count, false);
    debug_if(g_dap_global_db_debug_more, L_DEBUG,
             "Get all request from group %s recieved %zu values", a_group, l_values_count);
    dap_global_db_obj_t *l_objs = l_store_objs ? s_objs_from_store_objs(l_store_objs, l_values_count) : NULL;
    if (a_objs_count)
        *a_objs_count = l_values_count;
    return l_objs;
}

/**
 * @brief dap_global_db_get_all Get all records from the group
 * @param a_group
 * @param a_results_page_size
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_all(const char *a_group, size_t a_results_page_size, dap_global_db_callback_results_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_ALL;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_arg = a_arg;
    l_msg->callback_results = a_callback;
    l_msg->values_page_size = a_results_page_size;
    l_msg->last_hash = c_dap_global_db_hash_blank;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);

    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get_all request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get_all request for \"%s\" group", a_group);

    return l_ret;
}

/**
 * @brief s_msg_opcode_get_all
 * @param a_msg
 * @return
 */
static bool s_msg_opcode_get_all(struct queue_io_msg * a_msg)
{  
    dap_return_val_if_pass(!a_msg, false);

    size_t l_values_count = a_msg->values_page_size;
    dap_global_db_obj_t *l_objs = NULL;
    dap_global_db_store_obj_t *l_store_objs = NULL;
    if (!a_msg->values_page_size) {
        l_objs = dap_global_db_get_all_sync(a_msg->group, &l_values_count);
        if (a_msg->callback_results)
            a_msg->callback_results(a_msg->dbi,
                                l_objs ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS,
                                a_msg->group, l_values_count, l_values_count,
                                l_objs, a_msg->callback_arg);
        dap_global_db_objs_delete(l_objs, l_values_count);
         // All values are sent
        return false;
    }
    if (!a_msg->total_records)
        a_msg->total_records = dap_global_db_group_count(a_msg->group, false);
    if (a_msg->total_records)
        l_store_objs = s_storage_read_cond(a_msg->group, a_msg->last_hash, 0, &l_values_count, false);
    int l_rc = DAP_GLOBAL_DB_RC_NO_RESULTS;
    if (l_store_objs && l_values_count) {
        a_msg->last_hash = dap_global_db_hash_get(l_store_objs + l_values_count - 1);
        if (dap_global_db_hash_is_blank(&a_msg->last_hash)) {
            l_rc = DAP_GLOBAL_DB_RC_SUCCESS;
            l_values_count--;
        } else
            l_rc = DAP_GLOBAL_DB_RC_PROGRESS;
        a_msg->processed_records += l_values_count;
    }
    l_objs = l_store_objs ? s_objs_from_store_objs(l_store_objs, l_values_count) : NULL;
    // Call callback if present
    bool l_ret = false;
    if (a_msg->callback_results)
        l_ret = a_msg->callback_results(a_msg->dbi, l_rc,
                        a_msg->group, a_msg->total_records, l_values_count,
                        l_objs, a_msg->callback_arg);
    dap_global_db_objs_delete(l_objs, l_values_count);
    return l_rc == DAP_GLOBAL_DB_RC_PROGRESS && l_ret;
}

/* *** Get_all_raw functions group *** */

dap_global_db_store_obj_t *dap_global_db_get_all_raw_sync(const char* a_group, size_t *a_objs_count)
{
    dap_return_val_if_fail(a_group, NULL);
    
    size_t l_values_count = 0;
    dap_global_db_store_obj_t *l_store_objs = s_storage_read_all(a_group, &l_values_count, true);
    if (a_objs_count)
        *a_objs_count = l_values_count;
    return l_store_objs;
}

/**
 * @brief dap_global_db_get_all_raw
 * @param a_group
 * @param a_first_id
 * @param a_results_page_size
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_all_raw(const char *a_group, size_t a_results_page_size, dap_global_db_callback_results_raw_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_ALL_RAW;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->values_page_size = a_results_page_size;
    l_msg->callback_arg = a_arg;
    l_msg->callback_results_raw = a_callback;
    l_msg->last_hash = c_dap_global_db_hash_blank;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get_all_raw request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get_all request for \"%s\" group", a_group);
    return l_ret;
}

/**
 * @brief Get all records in raw format inside GlobalDB context
 * @param a_msg
 * @return
 */
static bool s_msg_opcode_get_all_raw(struct queue_io_msg *a_msg)
{
    dap_return_val_if_pass(!a_msg, false);

    size_t l_values_count = a_msg->values_page_size;
    dap_global_db_store_obj_t *l_store_objs = NULL;
    if (!a_msg->values_page_size) {
        l_store_objs = dap_global_db_get_all_raw_sync(a_msg->group, &l_values_count);
        if (a_msg->callback_results)
            a_msg->callback_results_raw(s_dbi,
                                l_store_objs ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS,
                                a_msg->group, a_msg->total_records, l_values_count,
                                l_store_objs, a_msg->callback_arg);
        dap_global_db_store_obj_free(l_store_objs, l_values_count);
        // All values are sent
       return false;
    }
    if (!a_msg->total_records)
        a_msg->total_records = dap_global_db_group_count(a_msg->group, true);
    if (a_msg->total_records)
        l_store_objs = s_storage_read_cond(a_msg->group, a_msg->last_hash, 0, &l_values_count, true);
    int l_rc = DAP_GLOBAL_DB_RC_NO_RESULTS;
    if (l_store_objs && l_values_count) {
        a_msg->last_hash = dap_global_db_hash_get(l_store_objs + l_values_count - 1);
        if (dap_global_db_hash_is_blank(&a_msg->last_hash)) {
            l_rc = DAP_GLOBAL_DB_RC_SUCCESS;
            l_values_count--;
        } else
            l_rc = DAP_GLOBAL_DB_RC_PROGRESS;
        a_msg->processed_records += l_values_count;
    }
    // Call callback if present
    bool l_ret = false;
    if (a_msg->callback_results)
        l_ret = a_msg->callback_results_raw(a_msg->dbi, l_rc,
                        a_msg->group, a_msg->total_records, l_values_count,
                        l_store_objs, a_msg->callback_arg);
    dap_global_db_store_obj_free(l_store_objs, l_values_count);
    return l_rc == DAP_GLOBAL_DB_RC_PROGRESS && l_ret;
}

static int s_set_sync_with_ts(dap_global_db_instance_t *a_dbi, const char *a_group, const char *a_key, const void *a_value,
                              const size_t a_value_length, bool a_pin_value, dap_nanotime_t a_timestamp)
{
    dap_global_db_store_obj_t l_store_data = {
        .timestamp  = a_timestamp,
        .flags      = DAP_GLOBAL_DB_RECORD_NEW | (a_pin_value ? DAP_GLOBAL_DB_RECORD_PINNED : 0),
        .group      = (char *)a_group,
        .key        = (char *)a_key,
        .value      = (byte_t *)a_value,
        .value_len  = a_value_length
    };
    l_store_data.sign = dap_global_db_store_obj_sign(&l_store_data, a_dbi->signing_key, &l_store_data.crc);
    if (!l_store_data.sign) {
        log_it(L_ERROR, "Can't sign new global DB object group %s key %s", a_group, a_key);
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    int l_res = s_store_obj_apply(a_dbi, &l_store_data);
    if (a_pin_value)
        s_add_pinned_obj_in_pinned_group(&l_store_data);
    DAP_DELETE(l_store_data.sign);
    return l_res;
}

/**
 * @brief dap_global_db_set_unsafe
 * @param a_dbi
 * @param a_group
 * @param a_key
 * @param a_value
 * @param a_value_length
 * @param a_pin_value
 * @return
 */
int dap_global_db_set_sync(const char *a_group, const char *a_key, const void *a_value, const size_t a_value_length, bool a_pin_value)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    return s_set_sync_with_ts(s_dbi, a_group, a_key, a_value,
                                a_value_length, a_pin_value, dap_nanotime_now());
}

/**
 * @brief Set GlobalDB record, identified with group and key
 * @param a_group Group name
 * @param a_key Key string
 * @param a_value Value data's pointer
 * @param a_value_length Value data's length
 * @param a_pin_value Pin value or not
 * @param a_callback  Callback executed after request processing
 * @param a_arg Argument passed to the callback
 * @return 0 if success, error code if not
 */
int dap_global_db_set(const char * a_group, const char *a_key, const void * a_value, const size_t a_value_length, bool a_pin_value, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_SET;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->key = dap_strdup(a_key);
    if (!l_msg->key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_MULTY(l_msg->group, l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    if (a_value && a_value_length) {
        l_msg->value = DAP_DUP_SIZE_RET_VAL_IF_FAIL((char*)a_value, a_value_length, DAP_GLOBAL_DB_RC_CRITICAL, l_msg->key, l_msg->group, l_msg);
        l_msg->value_length = a_value_length;
    }
    l_msg->value_is_pinned = a_pin_value;
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec set request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent sent request for \"%s\" group \"%s\" key", a_group, a_key);

    return l_ret;
}

/**
 * @brief s_msg_opcode_set
 * @param a_msg
 * @return
 */
static void s_msg_opcode_set(struct queue_io_msg * a_msg)
{
    dap_nanotime_t l_ts_now = dap_nanotime_now();
    int l_res = s_set_sync_with_ts(a_msg->dbi, a_msg->group, a_msg->key, a_msg->value,
                                     a_msg->value_length, a_msg->value_is_pinned, l_ts_now);

    if (l_res != DAP_GLOBAL_DB_RC_SUCCESS)
        log_it(L_ERROR, "Save error for %s:%s code %d", a_msg->group, a_msg->key, l_res);
    if (a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, l_res, a_msg->group, a_msg->key,
                               a_msg->value, a_msg->value_length, l_ts_now,
                               a_msg->value_is_pinned, a_msg->callback_arg);
}

/* *** Set_raw functions group *** */

int s_db_set_raw_sync(dap_global_db_instance_t *a_dbi, dap_global_db_store_obj_t *a_store_objs, size_t a_store_objs_count)
{
    int l_ret = DAP_GLOBAL_DB_RC_ERROR;
    for (size_t i = 0; i < a_store_objs_count; i++) {
        l_ret = s_store_obj_apply(a_dbi, a_store_objs + i);
        if (l_ret)
            debug_if(g_dap_global_db_debug_more, L_ERROR, "Can't save raw gdb data to %s/%s, code %d", (a_store_objs + i)->group, (a_store_objs + i)->key, l_ret);
    }
    if (a_store_objs->flags & DAP_GLOBAL_DB_RECORD_PINNED)
        s_add_pinned_obj_in_pinned_group(a_store_objs);
    return l_ret;
}

int dap_global_db_set_raw_sync(dap_global_db_store_obj_t *a_store_objs, size_t a_store_objs_count)
{
    dap_return_val_if_fail(s_dbi && a_store_objs && a_store_objs_count, DAP_GLOBAL_DB_RC_ERROR);
    return s_db_set_raw_sync(s_dbi, a_store_objs, a_store_objs_count);
}

/**
 * @brief dap_global_db_set_raw
 * @param a_store_objs
 * @param a_store_objs_count
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_set_raw(dap_global_db_store_obj_t *a_store_objs, size_t a_store_objs_count, dap_global_db_callback_results_raw_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_store_objs && a_store_objs_count, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_SET_RAW;
    l_msg->callback_arg = a_arg;
    l_msg->callback_results_raw = a_callback;

    l_msg->values_raw = dap_global_db_store_obj_copy(a_store_objs, a_store_objs_count);
    if (!l_msg->values_raw) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->values_raw_total = a_store_objs_count;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec set_raw request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent set_raw request for %zu objects", a_store_objs_count);
    return l_ret;

}

/**
 * @brief s_msg_opcode_set_raw
 * @param a_msg
 * @return
 */
static void s_msg_opcode_set_raw(struct queue_io_msg * a_msg)
{
    int l_ret = -1;
    if (a_msg->values_raw_total > 0)
        l_ret = s_db_set_raw_sync(a_msg->dbi, a_msg->values_raw, a_msg->values_raw_total);
    if (a_msg->callback_results_raw)
        a_msg->callback_results_raw(a_msg->dbi,
                                    l_ret == 0 ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_ERROR,
                                    a_msg->group, a_msg->values_raw_total, a_msg->values_raw_total,
                                    a_msg->values_raw, a_msg->callback_arg);
}

/* *** Set_multiple_zc functions group *** */

/**
 * @brief dap_global_db_set_multiple_zc Set multiple values, without duplication (zero copy, values are freed after set callback execution )
 * @param a_group
 * @param a_values
 * @param a_values_count
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_set_multiple_zc(const char *a_group, dap_global_db_obj_t *a_values, size_t a_values_count, dap_global_db_callback_results_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_values && a_values_count, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_SET_MULTIPLE;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->values = a_values;
    l_msg->value_is_pinned = false;
    l_msg->values_count = a_values_count;
    l_msg->callback_arg = a_arg;
    l_msg->callback_results = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec set_multiple request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent set_multiple request for \"%s\" group with %zu values", a_group, a_values_count);
    return l_ret;
}

/**
 * @brief s_msg_opcode_set_multiple
 * @param a_msg
 * @return
 */
static void s_msg_opcode_set_multiple_zc(struct queue_io_msg * a_msg)
{
    int l_ret = 0;
    size_t i=0;
    // DAP_TPS_TEST removed: test-only file marker and logging
    if(a_msg->values_count>0) {
        dap_global_db_store_obj_t l_store_obj = {};
        for(;  i < a_msg->values_count && l_ret == 0  ; i++ ) {
            l_ret = s_set_sync_with_ts(a_msg->dbi, a_msg->group, a_msg->values[i].key, a_msg->values[i].value, a_msg->values[i].value_len, a_msg->value_is_pinned, a_msg->values[i].timestamp);
        }
    }
    if(a_msg->callback_results) {
        a_msg->callback_results(a_msg->dbi,
                                l_ret == 0 ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_ERROR,
                                a_msg->group, i, a_msg->values_count,
                                a_msg->values, a_msg->callback_arg);
    }
    dap_global_db_objs_delete( a_msg->values, a_msg->values_count);
    // DAP_TPS_TEST removed: test-only file marker and logging
}

/* *** Pin/unpin functions group *** */

int s_db_object_pin_sync(dap_global_db_instance_t *a_dbi, const char *a_group, const char *a_key, bool a_pin)
{
    int l_res = DAP_GLOBAL_DB_RC_NO_RESULTS;
    dap_global_db_store_obj_t *l_store_obj = dap_global_db_get_raw_sync(a_group, a_key);
    if (l_store_obj) {
        l_res = dap_global_db_set_sync(l_store_obj->group, l_store_obj->key, l_store_obj->value, l_store_obj->value_len, a_pin);
        if (l_res) {
            log_it(L_ERROR,"Can't save pinned gdb data, code %d ", l_res);
            l_res = DAP_GLOBAL_DB_RC_ERROR;
        } else {
            if (a_pin)
                s_add_pinned_obj_in_pinned_group(l_store_obj);
            else if (!a_pin)
                s_del_pinned_obj_from_pinned_group_by_source_group(a_group, a_key);
            
        }
    }
    dap_global_db_store_obj_free_one(l_store_obj);
    return l_res;
}

int dap_global_db_pin_sync(const char *a_group, const char *a_key)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    return s_db_object_pin_sync(s_dbi, a_group, a_key, true);
}

int dap_global_db_unpin_sync(const char *a_group, const char *a_key)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    return s_db_object_pin_sync(s_dbi, a_group, a_key, false);
}

int s_db_object_pin(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg, bool a_pin)
{
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_PIN;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->key = dap_strdup(a_key);
    if (!l_msg->key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_MULTY(l_msg->group, l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;
    l_msg->value_is_pinned = a_pin;
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "%s \"%s\" group \"%s\" key from pinned groups",
                                                    a_pin ? "Add" : "Remove", a_group, a_key);

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec %s request, code %d", a_pin ? "pin" : "unpin", l_ret);
        s_queue_io_msg_delete(l_msg);
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent %s request for \"%s\" group \"%s\" key",
                                                       a_pin ? "pin" : "unpin", a_group, a_key);
    return l_ret;
}

/**
 * @brief s_msg_opcode_pin
 * @param a_msg
 * @return
 */
static void s_msg_opcode_pin(struct queue_io_msg * a_msg)
{
    int l_res = s_db_object_pin_sync(a_msg->dbi, a_msg->group, a_msg->key, a_msg->value_is_pinned);
    if (a_msg->callback_result)
        a_msg->callback_result(s_dbi, l_res, a_msg->group, a_msg->key,
                               NULL, 0, 0, a_msg->value_is_pinned, a_msg->callback_arg);
}

/**
 * @brief dap_global_db_pin
 * @param a_group
 * @param a_key
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_pin(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    return s_db_object_pin(a_group, a_key, a_callback, a_arg, true);
}
/**
 * @brief dap_global_db_unpin
 * @param a_group
 * @param a_key
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_unpin(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    return s_db_object_pin(a_group, a_key, a_callback, a_arg, false);
}

/* *** Del functions group *** */

/**
 * @brief dap_global_db_del_unsafe
 * @param a_group
 * @param a_key
 * @return
 */
static int s_del_sync_with_dbi_ex(dap_global_db_instance_t *a_dbi, const char *a_group, const char *a_key, const char * a_value, size_t a_value_len)
{
    dap_global_db_store_obj_t l_store_obj = {
        .key        = (char*)a_key,
        .group      = (char*)a_group,
        .flags      = DAP_GLOBAL_DB_RECORD_NEW | (a_key ? DAP_GLOBAL_DB_RECORD_DEL : DAP_GLOBAL_DB_RECORD_ERASE),
        .timestamp  = dap_nanotime_now()
    };

    if (a_value) {
        l_store_obj.value = (byte_t*)a_value;
        l_store_obj.value_len = a_value_len;
    }

    if (a_key)
        l_store_obj.sign = dap_global_db_store_obj_sign(&l_store_obj, a_dbi->signing_key, &l_store_obj.crc);

    int l_res = -1;
    if (a_key) {
        l_res = s_store_obj_apply(a_dbi, &l_store_obj);
        if (l_store_obj.flags & DAP_GLOBAL_DB_RECORD_PINNED)
            s_del_pinned_obj_from_pinned_group_by_source_group(a_group, a_key);
        DAP_DELETE(l_store_obj.sign);
    } else {
        // Drop the whole table
        l_store_obj.flags |= DAP_GLOBAL_DB_RECORD_ERASE;
        l_res = s_storage_write(&l_store_obj);
        if (l_res && l_res != DAP_GLOBAL_DB_RC_NOT_FOUND)
            log_it(L_ERROR, "Can't delete group %s", l_store_obj.group);
    }    
    return l_res;
}

static int s_del_sync_with_dbi(dap_global_db_instance_t *a_dbi, const char *a_group, const char *a_key) {
    return s_del_sync_with_dbi_ex(a_dbi, a_group, a_key, NULL, 0);
}

inline int dap_global_db_del_sync(const char *a_group, const char *a_key)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    return s_del_sync_with_dbi(s_dbi, a_group, a_key);
}

inline int dap_global_db_del_sync_ex(const char *a_group, const char *a_key, const char * a_value, size_t a_value_len)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    return s_del_sync_with_dbi_ex(s_dbi, a_group, a_key, a_value, a_value_len);
}


/**
 * @brief dap_global_db_delete
 * @param a_group
 * @param a_key
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_del_ex(const char * a_group, const char *a_key, const void * a_value, 
                         const size_t a_value_len, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_DELETE;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;
    if (a_value_len) {
        l_msg->value = DAP_DUP_SIZE((void*)a_value, a_value_len);
        l_msg->value_length = a_value_len;
    }

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec del request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent del request for \"%s\" group \"%s\" key", a_group, a_key);

    return l_ret;
}

int dap_global_db_del(const char * a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg) {
    return dap_global_db_del_ex(a_group, a_key, NULL, 0, a_callback, a_arg);
}

/**
 * @brief erase table, call dap_global_db_del_sync with NULL key
 * @param a_group - table name
 * @return result of dap_global_db_del_sync
 */
DAP_INLINE int dap_global_db_erase_table_sync(const char *a_group)
{
    return dap_global_db_del_sync(a_group, NULL);
}

/**
 * @brief erase table, call dap_global_db_del with NULL key
 * @param a_group - table name
 * @param a_callback - callback result
 * @param a_arg - callback args
 * @return result of dap_global_db_del
 */
DAP_INLINE int dap_global_db_erase_table(const char *a_group, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    return dap_global_db_del(a_group, NULL, a_callback, a_arg);
}

/**
 * @brief s_msg_opcode_delete
 * @param a_msg
 * @return
 */
static void s_msg_opcode_delete(struct queue_io_msg * a_msg)
{
    int l_res = 0;
    if (a_msg->value && a_msg->value_length) {
        l_res = dap_global_db_del_sync_ex(a_msg->group, a_msg->key, a_msg->value, a_msg->value_length);
    } else {
        l_res = dap_global_db_del_sync(a_msg->group, a_msg->key);
    }

    if(a_msg->callback_result) {
        a_msg->callback_result(a_msg->dbi, l_res==0 ? DAP_GLOBAL_DB_RC_SUCCESS:
                                        DAP_GLOBAL_DB_RC_ERROR,
                                a_msg->group, a_msg->key,
                               NULL, 0, 0 , false, a_msg->callback_arg );
    }
}

/* *** Flush functions group *** */

/**
 * @brief dap_global_db_flush_sync
 * @return
 */
int dap_global_db_flush_sync()
{
    return s_storage_flush();
}

/**
 * @brief dap_global_db_flush
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_flush(dap_global_db_callback_result_t a_callback, void * a_arg)
{
    dap_return_val_if_fail(s_dbi, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_FLUSH;
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec flush request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }
    return l_ret;
}

/**
 * @brief s_msg_opcode_flush
 * @param a_msg
 * @return
 */
static void s_msg_opcode_flush(struct queue_io_msg * a_msg)
{
    int l_res = dap_global_db_flush_sync();
    if (a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, l_res ? DAP_GLOBAL_DB_RC_ERROR : DAP_GLOBAL_DB_RC_SUCCESS,
                                NULL, NULL, NULL, 0, 0, false, a_msg->callback_arg);
}

/* *** Other functions *** */

/**
 * @brief Copies memory of an objs array.
 * @param objs a pointer to the first source object of the array
 * @param a_count a number of objects in the array
 * @return (none)
 */
dap_global_db_obj_t *dap_global_db_objs_copy(const dap_global_db_obj_t *a_objs_src, size_t a_count)
{   /* Sanity checks */
    dap_return_val_if_fail(a_objs_src && a_count, NULL);

    /* Run over array's elements */
    const dap_global_db_obj_t *l_obj = a_objs_src;
    dap_global_db_obj_t *l_objs_dest = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(dap_global_db_obj_t, a_count, NULL);
    for (dap_global_db_obj_t *l_cur = l_objs_dest; a_count--; l_cur++, l_obj++) {
        *l_cur = *l_obj;
        if (l_obj->key) {
            l_cur->key = dap_strdup(l_obj->key);
            if (!l_cur->key) {
                log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                DAP_DELETE(l_objs_dest);
                return NULL;
            }
        } else
            log_it(L_WARNING, "Inconsistent global DB object copy requested");

        if (l_obj->value) {
            if (l_obj->value_len) {
                l_cur->value = DAP_DUP_SIZE(l_obj->value, l_obj->value_len);
                if (!l_cur->value) {
                    log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                    DAP_DEL_MULTY(l_cur->key, l_objs_dest);
                    return NULL;
                }
            } else
                log_it(L_WARNING, "Inconsistent global DB object copy requested");
        }
    }
    return l_objs_dest;
}

/**
 * @brief Deallocates memory of an objs array.
 * @param objs a pointer to the first object of the array
 * @param a_count a number of objects in the array
 * @return (none)
 */
void dap_global_db_objs_delete(dap_global_db_obj_t *a_objs, size_t a_count)
{
    if (a_objs && a_count) {
        for (dap_global_db_obj_t *l_obj = a_objs; a_count--; ++l_obj)
            DAP_DEL_MULTY(l_obj->key, l_obj->value);
    }
    DAP_DELETE(a_objs);
}

/**
 * @brief s_msg_opcode_to_str
 * @param a_opcode
 * @return
 */
static const char *s_msg_opcode_to_str(enum queue_io_msg_opcode a_opcode)
{
    switch(a_opcode) {
        case MSG_OPCODE_GET:            return "GET";
        case MSG_OPCODE_GET_RAW:        return "GET_RAW";
        case MSG_OPCODE_GET_LAST:       return "GET_LAST";
        case MSG_OPCODE_GET_DEL_TS:     return "GET_DEL_TS";
        case MSG_OPCODE_GET_LAST_RAW:   return "GET_LAST_RAW";
        case MSG_OPCODE_GET_ALL:        return "GET_ALL";
        case MSG_OPCODE_GET_ALL_RAW:    return "GET_ALL_RAW";
        case MSG_OPCODE_SET:            return "SET";
        case MSG_OPCODE_SET_MULTIPLE:   return "SET_MULTIPLE";
        case MSG_OPCODE_SET_RAW:        return "SET_RAW";
        case MSG_OPCODE_PIN:            return "PIN";
        case MSG_OPCODE_DELETE:         return "DELETE";
        case MSG_OPCODE_FLUSH:          return "FLUSH";
        default:                        return "UNKNOWN";
    }
}

/**
 * @brief s_queue_io_callback
 * @details Queue I/O process callback
 * @param a_es
 * @param a_arg
 */
static bool s_queue_io_callback(void * a_arg)
{
    struct queue_io_msg *l_msg = (struct queue_io_msg *) a_arg;
    assert(l_msg);

    debug_if(g_dap_global_db_debug_more, L_NOTICE, "Received GlobalDB I/O message with opcode %s", s_msg_opcode_to_str(l_msg->opcode) );

    switch (l_msg->opcode) {
    case MSG_OPCODE_GET:            s_msg_opcode_get(l_msg); break;
    case MSG_OPCODE_GET_RAW:        s_msg_opcode_get_raw(l_msg); break;
    case MSG_OPCODE_GET_LAST:       s_msg_opcode_get_last(l_msg); break;
    case MSG_OPCODE_GET_LAST_RAW:   s_msg_opcode_get_last_raw(l_msg); break;
    case MSG_OPCODE_GET_DEL_TS:     s_msg_opcode_get_del_ts(l_msg); break;
    case MSG_OPCODE_GET_ALL:        if (s_msg_opcode_get_all(l_msg)) return true; break;
    case MSG_OPCODE_GET_ALL_RAW:    if (s_msg_opcode_get_all_raw(l_msg)) return true; break;
    case MSG_OPCODE_SET:            s_msg_opcode_set(l_msg); break;
    case MSG_OPCODE_SET_MULTIPLE:   s_msg_opcode_set_multiple_zc(l_msg); break;
    case MSG_OPCODE_SET_RAW:        s_msg_opcode_set_raw(l_msg); break;
    case MSG_OPCODE_PIN:            s_msg_opcode_pin(l_msg); break;
    case MSG_OPCODE_DELETE:         s_msg_opcode_delete(l_msg); break;
    case MSG_OPCODE_FLUSH:          s_msg_opcode_flush(l_msg); break;
    default:
        log_it(L_WARNING, "Message with undefined opcode %d received in queue_io",
               l_msg->opcode);
    }
    s_queue_io_msg_delete(l_msg);
    return false;
}

/**
 * @brief s_queue_io_msg_delete
 * @param a_msg
 */
static void s_queue_io_msg_delete( struct queue_io_msg * a_msg)
{
    switch(a_msg->opcode) {    
    case MSG_OPCODE_SET:
        DAP_DEL_Z(a_msg->value);
    case MSG_OPCODE_GET:
    case MSG_OPCODE_GET_RAW:
    case MSG_OPCODE_GET_DEL_TS:
    case MSG_OPCODE_PIN:
    case MSG_OPCODE_DELETE:
        DAP_DEL_Z(a_msg->key);
    case MSG_OPCODE_GET_LAST:
    case MSG_OPCODE_GET_LAST_RAW:
    case MSG_OPCODE_GET_ALL:
    case MSG_OPCODE_GET_ALL_RAW:
    case MSG_OPCODE_SET_MULTIPLE:
        DAP_DEL_Z(a_msg->group);
        break;
    case MSG_OPCODE_SET_RAW:
        dap_global_db_store_obj_free(a_msg->values_raw, a_msg->values_raw_total);
    default:;
    }
    DAP_DEL_Z(a_msg);
}

/**
 * @brief s_check_db_version
 * @return
 */
static int s_check_db_version()
{
    pthread_mutex_lock(&s_check_db_mutex);
    int l_ret = dap_global_db_get(DAP_GLOBAL_DB_LOCAL_GENERAL, "gdb_version",s_check_db_version_callback_get, NULL);
    if (l_ret == 0) {
        while (s_check_db_ret == INVALID_RETCODE)
            pthread_cond_wait(&s_check_db_cond, &s_check_db_mutex);
        l_ret = s_check_db_ret;
    } else{
        log_it(L_CRITICAL, "Can't process get gdb_version request, code %d", l_ret);
    }
    pthread_mutex_unlock(&s_check_db_mutex);
    return l_ret;
}

static void s_clean_old_obj_gdb_callback(void UNUSED_ARG *a_arg) {
    debug_if(g_dap_global_db_debug_more, L_INFO, "Start clean old objs in global_db callback");
    dap_list_t *l_group_list = dap_global_db_get_groups_by_mask("*");
    size_t l_count = 0;
    for (dap_list_t *l_list = l_group_list; l_list; l_list = dap_list_next(l_list), ++l_count) {
        size_t l_count_obj = dap_global_db_group_count((char*)l_list->data, true);
        if (!l_count_obj) {
            debug_if(g_dap_global_db_debug_more, L_INFO, "Empty group %s, delete it", (char*)l_list->data);
            dap_global_db_del_sync((char*)l_list->data, NULL);
            continue;
        }
        dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(s_dbi, (char*)l_list->data);
        if (!l_cluster) {
            continue;
        }
        dap_nanotime_t l_time_now = dap_nanotime_now();
        dap_nanotime_t l_ttl = dap_nanotime_from_sec(l_cluster->ttl);
        size_t l_ret_count = 0;
        dap_global_db_store_obj_t *l_ret = s_storage_read_below_timestamp((char*)l_list->data, l_time_now - l_ttl, &l_ret_count);
        log_it(L_DEBUG, "Start clean gdb group %s, %zu records will check", (char*)l_list->data, l_ret_count);
        while (l_ret_count > 0 && l_ret && l_ret->group) {
            for(size_t i = 0; i < l_ret_count; i++) {
                if (!(l_ret[i].flags & DAP_GLOBAL_DB_RECORD_PINNED)) {
                    if (l_ttl != 0) {
                        debug_if(g_dap_global_db_debug_more, L_INFO, "Try to delete from global_db the obj %s group, %s key", l_ret[i].group, l_ret[i].key);
                        if (l_cluster->del_callback)
                            l_cluster->del_callback(l_ret+i, l_cluster->del_arg);
                        else s_storage_erase(l_ret[i].group, dap_global_db_hash_get(l_ret + i));
                    } else if ( l_ret[i].flags & DAP_GLOBAL_DB_RECORD_DEL && dap_global_db_group_match_mask(l_ret->group, "local.*")) {       
                        debug_if(g_dap_global_db_debug_more, L_INFO, "Delete from empty local global_db obj %s group, %s key", l_ret[i].group, l_ret[i].key);
                        s_storage_erase(l_ret[i].group, dap_global_db_hash_get(l_ret + i));
                    }
                } 
            }
            dap_global_db_store_obj_free(l_ret, l_ret_count);
            // filter for local groups
            if (l_ttl == 0)
                break;
            l_ret_count = 0;
            l_ret = s_storage_read_below_timestamp((char*)l_list->data, l_time_now - l_ttl, &l_ret_count);
        }
    }
    dap_list_free(l_group_list);
}

static int s_gdb_clean_init()
{
    debug_if(g_dap_global_db_debug_more, L_INFO, "Init global_db clean old objects");
    s_gdb_auto_clean_period = dap_config_get_item_int32_default(g_config, "global_db", "gdb_auto_clean_period", s_gdb_auto_clean_period);
    dap_proc_thread_timer_add(NULL, (dap_thread_timer_callback_t)s_clean_old_obj_gdb_callback, NULL, s_gdb_auto_clean_period * 1000);
    return 0;
}

static void s_gdb_clean_deinit() {
}

static bool s_check_is_obj_pinned(const char * a_group, const char * a_key) {
    bool l_ret = false;
    if (dap_global_db_group_match_mask(a_group, "*pinned")) { 
        l_ret = true;
    } else {
        char * l_pinned_mask = dap_get_local_pinned_groups_mask(a_group);
        l_ret = s_storage_exists_key(l_pinned_mask, a_key);
        DAP_DELETE(l_pinned_mask);
    }
    return l_ret;
}

/// @brief 
/// @param a_pinned_obj 
/// @return 0 restore obj
///         -1 obj is the hole, delete them
static int s_is_require_restore_del_pin_obj(dap_global_db_store_obj_t * a_pinned_obj) {
    if (dap_global_db_store_obj_get_type(a_pinned_obj) == DAP_GLOBAL_DB_OPTYPE_DEL)
        return -1;
    return 0;
}

static bool s_check_pinned_db_objs_callback(void UNUSED_ARG *a_arg)
{
    debug_if(g_dap_global_db_debug_more, L_INFO, "Start check pinned objs callback");
    dap_nanotime_t l_time_now = dap_nanotime_now();
    dap_list_t *l_group_list = dap_global_db_get_groups_by_mask("*.pinned");
    size_t l_count = 0;
    for (dap_list_t *l_list = l_group_list; l_list; l_list = dap_list_next(l_list), ++l_count) {
        size_t l_ret_count = 0;
        char * l_group_name = dap_get_group_from_pinned_groups_mask((char*)l_list->data);
        if (!l_group_name) {
            continue;
        }
        dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(s_dbi, l_group_name);
        if (!l_cluster) {
            debug_if(g_dap_global_db_debug_more, L_ERROR, "Invalid group or cluster: %s", l_group_name ? l_group_name : "NULL");
            DAP_DELETE(l_group_name);
            continue;
        }
        dap_nanotime_t l_ttl = dap_nanotime_from_sec(l_cluster->ttl);
        if (l_ttl == 0) {
            debug_if(g_dap_global_db_debug_more, L_INFO, "Pinned object with 0 ttl %s", l_group_name);
            DAP_DELETE(l_group_name);
            continue;
        }
        dap_global_db_store_obj_t *l_ret = s_storage_read_below_timestamp((char*)l_list->data, l_time_now - l_ttl + s_minimal_ttl + 100, &l_ret_count);
        while (l_ret_count > 0 && l_ret && l_ret->group) {
            for(size_t i = 0; i < l_ret_count; i++) {
                if (l_ret[i].timestamp + l_ttl <= l_time_now + s_minimal_ttl) {
                    dap_global_db_store_obj_t * l_gdb_rec =  dap_global_db_get_raw_sync(l_group_name, l_ret[i].key);
                    if (!l_gdb_rec) {
                        debug_if(g_dap_global_db_debug_more, L_INFO, "Can't find source gdb obj %s group, %s key restore them", l_group_name, l_ret[i].key);
                        dap_global_db_set_sync(l_group_name, l_ret[i].key, l_ret[i].value, l_ret[i].value_len, true);
                        continue;
                    }
                    dap_global_db_set_sync(l_ret[i].group, l_ret[i].key, l_ret[i].value, l_ret[i].value_len, true);
                    switch (s_is_require_restore_del_pin_obj(l_gdb_rec)) {
                        case 0:
                            debug_if(g_dap_global_db_debug_more, L_INFO, "Repin pinned gdb obj %s group, %s key", l_gdb_rec->group, l_gdb_rec->key);
                            dap_global_db_set_sync(l_gdb_rec->group, l_gdb_rec->key, l_ret[i].value, l_ret[i].value_len, true);
                            break;
                        case -1:
                            debug_if(g_dap_global_db_debug_more, L_INFO, "Remove pinned gdb obj %s group, %s key after manually delete", l_gdb_rec->group, l_gdb_rec->key);
                            s_del_pinned_obj_from_pinned_group_by_source_group(l_ret[i].group, l_ret[i].key);
                            if (l_gdb_rec->flags & DAP_GLOBAL_DB_RECORD_PINNED)
                                dap_global_db_unpin_sync(l_gdb_rec->group, l_gdb_rec->key);
                            break;
                        default: 
                            debug_if(g_dap_global_db_debug_more, L_INFO, "Unrecognized pinned gdb obj %s group, %s key", l_gdb_rec->group, l_gdb_rec->key);
                            break;
                    }
                    dap_global_db_store_obj_free(l_gdb_rec, 1);
                }
            }
            dap_global_db_store_obj_free(l_ret, l_ret_count);
            l_ret = s_storage_read_below_timestamp((char*)l_list->data, l_time_now - l_ttl + s_minimal_ttl + 100, &l_ret_count);
        }
    }
    dap_list_free_full(l_group_list, NULL);
    return false;
}

static bool s_start_check_pinned_db_objs_callback(void UNUSED_ARG *a_arg) {
    int l_ret = dap_proc_thread_callback_add(NULL, s_check_pinned_db_objs_callback, NULL);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec pinned objs check request, code %d", l_ret);
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Sent request for pinned objs check");
    return true;
}

DAP_STATIC_INLINE char *dap_get_local_pinned_groups_mask(const char *a_group)
{
    return dap_strdup_printf("local.%s.pinned", a_group);
}

DAP_STATIC_INLINE char *dap_get_group_from_pinned_groups_mask(const char *a_group) {
    if (!a_group)
        return NULL;
    char ** l_group_arr = dap_strsplit(a_group, ".", 10);
    if (!l_group_arr[1] || !l_group_arr[2]) {
        dap_strfreev(l_group_arr);
        return NULL;
    }
    size_t l_size = dap_str_countv(l_group_arr);
    char * result = dap_strdup_printf("%s.%s", l_group_arr[1], l_group_arr[2]);
    for (size_t i = 3; i < l_size - 1;i++) {
        char * tmp = dap_strdup_printf("%s.%s", result, l_group_arr[i]);
        DAP_DELETE(result);
        result = tmp;
    }
    dap_strfreev(l_group_arr);
    return result;
}

static void s_set_pinned_timer(const char *a_group)
{
    dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(s_dbi, a_group);
    if (!l_cluster)
        return;
    if ((l_cluster->ttl != 0 && s_minimal_ttl > dap_nanotime_from_sec(l_cluster->ttl)) || !s_check_pinned_db_objs_timer) {
        s_check_pinned_db_objs_deinit();
        if (l_cluster->ttl != 0)
            s_minimal_ttl = dap_nanotime_from_sec(l_cluster->ttl);
        s_check_pinned_db_objs_timer = dap_timerfd_start(dap_nanotime_to_millitime(s_minimal_ttl/2), 
                                                        (dap_timerfd_callback_t)s_start_check_pinned_db_objs_callback, NULL);
        debug_if(g_dap_global_db_debug_more, L_INFO, "New pinned callback timer %"DAP_UINT64_FORMAT_U" sec", (uint64_t)dap_nanotime_to_sec(s_minimal_ttl/2));
    }
}

static int s_add_pinned_obj_in_pinned_group(dap_global_db_store_obj_t * a_objs){
    if (!dap_global_db_group_match_mask(a_objs->group, "*pinned")) {
        char * l_pinned_mask = dap_get_local_pinned_groups_mask(a_objs->group);
        dap_global_db_store_obj_t * l_ret_check = dap_global_db_get_raw_sync(l_pinned_mask, a_objs->key);
        if (!l_ret_check) {
            if (!dap_global_db_set_sync(l_pinned_mask, a_objs->key, a_objs->value, a_objs->value_len, false)) {
                debug_if(g_dap_global_db_debug_more, L_INFO, "Pinned objs was added in pinned group %s, %s key", l_pinned_mask, a_objs->key);
                s_store_obj_update_timestamp(a_objs, NULL, dap_nanotime_now());
                s_storage_write(a_objs);
            } else
                debug_if(g_dap_global_db_debug_more, L_ERROR, "Adding error in pinned group %s", a_objs->group);
        }
        s_set_pinned_timer(a_objs->group);
        dap_global_db_store_obj_free_one(l_ret_check);
        DAP_DELETE(l_pinned_mask);
    }
    return 0;
}

static void s_del_pinned_obj_from_pinned_group_by_source_group(const char * a_group, const char * a_key) {
    debug_if(g_dap_global_db_debug_more, L_INFO, "Delete pinned group by source group %s, %s key", a_group, a_key);
    char * l_pinned_group = dap_get_local_pinned_groups_mask(a_group);
    dap_global_db_store_obj_t * l_pin_del_obj = dap_global_db_get_raw_sync(l_pinned_group, a_key);
    if (l_pin_del_obj)
        s_storage_erase(l_pin_del_obj->group, dap_global_db_hash_get(l_pin_del_obj));
}

static void s_get_all_pinned_objs_in_group(dap_global_db_store_obj_t * a_objs, size_t a_objs_count) {
    for(size_t i = 0; i < a_objs_count; i++) {
        if (a_objs[i].flags & DAP_GLOBAL_DB_RECORD_PINNED)
            s_add_pinned_obj_in_pinned_group(a_objs + i);
    }
}

static void s_check_pinned_db_objs_timer_callback(void *a_arg) {
    s_check_pinned_db_objs_callback(a_arg);
}

static int s_pinned_objs_group_init() {
    debug_if(g_dap_global_db_debug_more, L_INFO, "Check pinned db objs init");
    dap_nanotime_t l_time_now = dap_nanotime_now();
    dap_list_t *l_group_list = dap_global_db_get_groups_by_mask("*");
    size_t l_count = 0;
    for (dap_list_t *l_list = l_group_list; l_list; l_list = dap_list_next(l_list), ++l_count) {
        size_t l_ret_count = 0;
        dap_global_db_store_obj_t  * l_ret = dap_global_db_get_all_raw_sync((char*)l_list->data, &l_ret_count);
        if (!l_ret) {
            dap_global_db_store_obj_free(l_ret, l_ret_count);
            continue;
        }
        s_get_all_pinned_objs_in_group(l_ret, l_ret_count);
        dap_global_db_store_obj_free(l_ret, l_ret_count);
    }
    dap_list_free_full(l_group_list, NULL);
    dap_proc_thread_timer_add_pri(NULL, s_check_pinned_db_objs_timer_callback, NULL, 300000, true, DAP_QUEUE_MSG_PRIORITY_NORMAL);  // 5 min wait before repin
    return 0;
}

static void s_check_pinned_db_objs_deinit() {
    if (s_check_pinned_db_objs_timer)
        dap_timerfd_delete(s_check_pinned_db_objs_timer->worker, s_check_pinned_db_objs_timer->esocket_uuid);
}


/**
 * @brief s_check_db_version_callback_get
 * @details Notify callback on reading GlobalDB version
 * @param a_errno
 * @param a_group
 * @param a_key
 * @param a_value
 * @param a_value_len
 * @param a_arg
 */
static void s_check_db_version_callback_get (dap_global_db_instance_t *a_dbi, int a_errno, const char * a_group, const char * a_key,
                                             const void * a_value, const size_t a_value_len,
                                             dap_nanotime_t value_ts, bool a_is_pinned, void * a_arg)
{
    int res = 0;
    if (a_errno != 0) { // No DB at all
        log_it(L_NOTICE, "No GlobalDB version at all, creating the new GlobalDB from scratch");
        a_dbi->version = DAP_GLOBAL_DB_VERSION;
        if ( (res = dap_global_db_set(DAP_GLOBAL_DB_LOCAL_GENERAL, "gdb_version",
                                      &a_dbi->version,
                                      sizeof(a_dbi->version), false,
                                      s_check_db_version_callback_set, NULL) ) != 0) {
            log_it(L_NOTICE, "Can't set GlobalDB version, code %d", res);
            goto lb_exit;
        }
        return; // In this case the condition broadcast should happens in s_check_db_version_callback_set()

    }

    if (a_value_len == sizeof(a_dbi->version))
        a_dbi->version = *(uint32_t *)a_value;

    if( a_dbi->version < DAP_GLOBAL_DB_VERSION) {
        log_it(L_NOTICE, "Current GlobalDB version is %u, but %u is required. The current database will be recreated",
               a_dbi->version, DAP_GLOBAL_DB_VERSION);
        s_storage_deinit();
        // Database path
        const char *l_storage_path = a_dbi->storage_path;
        // Delete database
        if(dap_file_test(l_storage_path) || dap_dir_test(l_storage_path)) {
            // Backup filename: backup_global_db_ver.X_DATE_TIME.zip
            char l_ts_now_str[255];
            dap_time_t t = dap_time_now();
            time_t tt = (time_t)t;
            strftime(l_ts_now_str, 200, "%y.%m.%d-%H_%M_%S", localtime(&tt));
#ifdef DAP_BUILD_WITH_ZIP
            char *l_output_file_name = dap_strdup_printf("backup_%s_ver.%d_%s.zip", dap_path_get_basename(l_storage_path), l_gdb_version, now);
            char *l_output_file_path = dap_build_filename(l_storage_path, "../", l_output_file_name, NULL);
            // Create backup as ZIP file
            if(dap_zip_directory(l_storage_path, l_output_file_path)) {
#else
            char *l_output_file_name = dap_strdup_printf("backup_%s_ver.%d_%s.tar",
                                                         dap_path_get_basename(a_dbi->storage_path),
                                                         a_dbi->version, l_ts_now_str);
            char *l_output_file_path = dap_build_filename(l_storage_path, "../", l_output_file_name, NULL);
            // Create backup as TAR file
            if(dap_tar_directory(l_storage_path, l_output_file_path)) {
#endif
                char *l_rm_path = dap_strdup_printf("%s/*", l_storage_path);
                // Delete database file or directory
                dap_rm_rf(l_rm_path);
                DAP_DELETE(l_rm_path);
            }
            else {
                log_it(L_ERROR, "Can't backup GlobalDB version %d", a_dbi->version);
                res = -2;
                goto lb_exit;
            }
            DAP_DELETE(l_output_file_name);
            DAP_DELETE(l_output_file_path);
        }
        // Reinitialize database
        res = s_storage_init(s_dbi->storage_path);
        // Save current db version
        if(!res) {
            a_dbi->version = DAP_GLOBAL_DB_VERSION;
            if ( (res = dap_global_db_set(DAP_GLOBAL_DB_LOCAL_GENERAL, "gdb_version",
                                          &a_dbi->version,
                                          sizeof(uint32_t), false,
                                          s_check_db_version_callback_set, NULL) ) != 0) {
                log_it(L_NOTICE, "Can't set GlobalDB version, code %d", res);
                goto lb_exit;
            }
            return; // In this case the condition broadcast should happens in s_check_db_version_callback_set()
        }
    } else if(a_dbi->version > DAP_GLOBAL_DB_VERSION) {
        log_it(L_ERROR, "GlobalDB version %d is newer than supported version %d",
                            a_dbi->version, DAP_GLOBAL_DB_VERSION);
        res = -1;
    }
    else {
        log_it(L_NOTICE, "GlobalDB version %d", a_dbi->version);
    }
lb_exit:
    pthread_mutex_lock(&s_check_db_mutex); //    To be sure thats we're on pthread_cond_wait() line
    s_check_db_ret = res;
    pthread_cond_signal(&s_check_db_cond);
    pthread_mutex_unlock(&s_check_db_mutex); //  in calling thread
}

/**
 * @brief s_check_db_version_callback_set
 * @details GlobalDB version update callback
 * @param a_errno
 * @param a_group
 * @param a_key
 * @param a_value
 * @param a_value_len
 * @param a_arg
 */
static void s_check_db_version_callback_set (dap_global_db_instance_t *a_dbi,int a_errno, const char * a_group, const char * a_key,
                                             const void * a_value, const size_t a_value_len,
                                             dap_nanotime_t value_ts, bool a_is_pinned, void * a_arg)
{
    int l_res = 0;
    if(a_errno != 0) {
        log_it(L_ERROR, "Can't process request for DB version, error code %d", a_errno);
        l_res = a_errno;
    } else
        log_it(L_NOTICE, "GlobalDB version updated to %d", a_dbi->version);

    pthread_mutex_lock(&s_check_db_mutex); //  in calling thread
    s_check_db_ret = l_res;
    pthread_cond_signal(&s_check_db_cond);
    pthread_mutex_unlock(&s_check_db_mutex); //  in calling thread
}

/**
 * @brief s_objs_from_store_objs
 * @details convert dap_global_db_store_obj_t to dap_global_db_obj_t
 * @param a_store_objs src dap_global_db_store_obj_t pointer
 * @param a_values_count count records inarray
 * @return pointer if not error, else NULL
 */

dap_global_db_obj_t *s_objs_from_store_objs(dap_global_db_store_obj_t *a_store_objs, size_t a_values_count)
{
    dap_return_val_if_pass(!a_store_objs, NULL);
    
    dap_global_db_obj_t *l_objs = NULL;

    l_objs = DAP_NEW_Z_SIZE(dap_global_db_obj_t, sizeof(dap_global_db_obj_t) *a_values_count);
    if (!l_objs) {
        log_it(L_CRITICAL, "Insufficient memory");
        return NULL;
    }
    for (size_t i = 0; i < a_values_count; i++) {
        if (!dap_global_db_isalnum_group_key(a_store_objs + i, true)) {
            log_it(L_ERROR, "Delete broken object");
            dap_global_db_del(a_store_objs[i].group, a_store_objs[i].key, NULL, NULL);
            continue;
        }
        l_objs[i].is_pinned = a_store_objs[i].flags & DAP_GLOBAL_DB_RECORD_PINNED;
        l_objs[i].key = a_store_objs[i].key;
        l_objs[i].value = a_store_objs[i].value;
        l_objs[i].value_len = a_store_objs[i].value_len;
        l_objs[i].timestamp = a_store_objs[i].timestamp;
        DAP_DELETE(a_store_objs[i].group);
        DAP_DEL_Z(a_store_objs[i].sign);
    }
    DAP_DELETE(a_store_objs);
    return l_objs;
}

bool dap_global_db_isalnum_group_key(const dap_global_db_store_obj_t *a_obj, bool a_not_null_key)
{
    dap_return_val_if_fail(a_obj && a_obj->group, false);

    bool ret = true;
    if (a_obj->key) {
        for (char *c = (char*)a_obj->key; *c; ++c) {
            if (!dap_ascii_isprint(*c)) {
                ret = false;
                break;
            }
        }
    } else if (a_not_null_key)
        ret = false;

    if (ret) {
        for (char *c = (char*)a_obj->group; *c; ++c) {
            if (!dap_ascii_isprint(*c)) {
                ret = false;
                break;
            }
        }
    }

    if (!ret) {
        char l_ts[128] = { '\0' };
        dap_nanotime_to_str_rfc822(l_ts, sizeof(l_ts), a_obj->timestamp);
        log_it(L_MSG, "[!] Corrupted object %s (len %zu) : %s (len %zu), ts %s",
               a_obj->group, dap_strlen(a_obj->group), a_obj->key, dap_strlen(a_obj->key), l_ts);
    }
    return ret;
}

/**
 * @brief dap_global_db_group_clear
 * @details Erase all records in the group with flag DAP_GLOBAL_DB_RECORD_DEL
 * @param a_group group name
 * @param a_pinned clean pinned records
 * @return count of deleted records
 */
size_t dap_global_db_group_clear(const char *a_group, bool a_pinned)
{
    dap_return_val_if_fail(a_group, 0);
    size_t
        l_obj_count = 0,
        l_ret = 0;
    dap_global_db_store_obj_t *l_objs = dap_global_db_get_all_raw_sync(a_group, &l_obj_count);
    log_it(L_DEBUG, "Start clean gdb group %s, %zu records will check", a_group, l_obj_count);
    for(size_t i = 0; i < l_obj_count; ++i) {
        if (l_objs[i].flags & DAP_GLOBAL_DB_RECORD_DEL && (a_pinned || !(l_objs[i].flags & DAP_GLOBAL_DB_RECORD_PINNED))) {       
            debug_if(g_dap_global_db_debug_more, L_INFO, "Delete from empty local global_db obj %s group, %s key", l_objs[i].group, l_objs[i].key);
            s_storage_erase(l_objs[i].group, dap_global_db_hash_get(l_objs + i));
            l_ret++;
        }
    }
    dap_global_db_store_obj_free(l_objs, l_obj_count);
    return l_ret;
}
