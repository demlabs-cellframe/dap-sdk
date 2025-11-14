/*
 * Authors:
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Kelvin Project https://github.com/kelvinblockchain
 * Copyright  (c) 2019
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

 DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 DAP is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  MODIFICATION HISTORY:
 *
 *      24-FEB-2022 RRL Added Async I/O functionality for DB request processing
 *
 *      15-MAR-2022 RRL Some cosmetic changes to reduce a diagnostic output.
 */

#include <stddef.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "dap_worker.h"
#include "dap_file_utils.h"
#include "dap_strfuncs.h"
#include "dap_hash.h"
#include "dap_events.h"
#include "dap_list.h"
#include "dap_common.h"
#include "dap_global_db.h"
#include "dap_config.h"

#ifdef DAP_CHAIN_GDB_ENGINE_SQLITE
#include "dap_global_db_driver_sqlite.h"
#endif

#ifdef DAP_CHAIN_GDB_ENGINE_MDBX
#include "dap_global_db_driver_mdbx.h"
#endif

#ifdef DAP_CHAIN_GDB_ENGINE_PGSQL
#include "dap_global_db_driver_pgsql.h"
#endif

#include "dap_global_db_driver.h"

#define LOG_TAG "db_driver"

const dap_global_db_driver_hash_t c_dap_global_db_driver_hash_blank = { 0 };

// A selected database driver.
static char s_used_driver [32];                                             /* Name of the driver */

static dap_global_db_driver_callbacks_t s_drv_callback;                            /* A set of interface routines for the selected
                                                                            DB Driver at startup time */

/**
 * @brief Initializes a database driver.
 * @note You should Call this function before using the driver.
 * @param driver_name a string determining a type of database driver:
 * "сdb", "sqlite" ("sqlite3") or "pgsql"
 * @param a_filename_db a path to a database file
 * @return Returns 0, if successful; otherwise <0.
 */
int dap_global_db_driver_init(const char *a_driver_name, const char *a_filename_db)
{
    int l_ret = -1;
    if (s_used_driver[0] )
        dap_global_db_driver_deinit();

    // Fill callbacks with zeros
    s_drv_callback = (dap_global_db_driver_callbacks_t){ };

    // Setup driver name
    dap_strncpy( s_used_driver, a_driver_name, sizeof(s_used_driver));

    char l_db_path_ext[strlen(a_driver_name) + strlen(a_filename_db) + 6];
    if (dap_strcmp(s_used_driver, "pgsql")) { // Compose path
        dap_mkdir_with_parents(a_filename_db);
        snprintf(l_db_path_ext, sizeof(l_db_path_ext), "%s/gdb-%s", a_filename_db, a_driver_name);
    }

   // Check for engine
    if(!dap_strcmp(s_used_driver, "ldb"))
        log_it(L_ERROR, "Unsupported global_db driver \"%s\"", a_driver_name);
#ifdef DAP_CHAIN_GDB_ENGINE_SQLITE
    else if(!dap_strcmp(s_used_driver, "sqlite") || !dap_strcmp(s_used_driver, "sqlite3") )
        l_ret = dap_global_db_driver_sqlite_init(l_db_path_ext, &s_drv_callback);
#endif
#ifdef DAP_CHAIN_GDB_ENGINE_MDBX
    else if(!dap_strcmp(s_used_driver, "mdbx"))
        l_ret = dap_global_db_driver_mdbx_init(l_db_path_ext, &s_drv_callback);
#endif

#ifdef DAP_CHAIN_GDB_ENGINE_PGSQL
    else if(!dap_strcmp(s_used_driver, "pgsql"))
    #ifdef DAP_SDK_TESTS
    {
        char *l_pg_conninfo = getenv("PG_CONNINFO");
        if (!l_pg_conninfo) {
            log_it(L_WARNING, "PG_CONNINFO not defined, using to tests second conn info:\"%s\"", a_filename_db);
            l_ret = dap_global_db_driver_pgsql_init(a_filename_db, &s_drv_callback);
        } else {
            l_ret = dap_global_db_driver_pgsql_init(l_pg_conninfo, &s_drv_callback);
        }
    }
    #else
    {
        uint16_t l_arr_len = 0;
        const char **l_conn_info_arr = dap_config_get_array_str(g_config, "global_db", "pg_conninfo", &l_arr_len);
        dap_string_t *l_conn_info = NULL;
        if (l_arr_len) {
            l_conn_info = dap_string_new_len(NULL, l_arr_len * 16);
            while (l_arr_len--)
                dap_string_append_printf(l_conn_info, "%s ", l_conn_info_arr[l_arr_len]);
        } else {
            l_conn_info = dap_string_new("dbname=postgres");
        }
        l_ret = dap_global_db_driver_pgsql_init(l_conn_info->str, &s_drv_callback);
        dap_string_free(l_conn_info, true);
    }
    #endif
#endif
    else
        log_it(L_ERROR, "Unknown global_db driver \"%s\"", a_driver_name);

    return l_ret;
}

/**
 * @brief Deinitializes a database driver.
 * @note You should call this function after using the driver.
 * @return (none)
 */
void dap_global_db_driver_deinit(void)
{
    log_it(L_NOTICE, "DeInit for %s ...", s_used_driver);

    // deinit driver
    if(s_drv_callback.deinit)
        s_drv_callback.deinit();

    s_used_driver [ 0 ] = '\0';
}

/**
 * @brief Flushes a database cahce to disk.
 * @return Returns 0, if successful; otherwise <0.
 */
int dap_global_db_driver_flush(void)
{
    if (s_drv_callback.flush)
        return s_drv_callback.flush();
    else
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have flush callback", s_used_driver);
    return 0;
}

static inline void s_store_obj_copy_one(dap_store_obj_t *a_store_obj_dst, const dap_store_obj_t *a_store_obj_src)
{
    *a_store_obj_dst = *a_store_obj_src;
    a_store_obj_dst->group = dap_strdup(a_store_obj_src->group);
    if (a_store_obj_src->group && !a_store_obj_dst->group)
        return log_it(L_CRITICAL, "%s", c_error_memory_alloc);

    a_store_obj_dst->key = dap_strdup(a_store_obj_src->key);
    if (a_store_obj_src->key && !a_store_obj_dst->key) {
        return DAP_DELETE(a_store_obj_dst->group), log_it(L_CRITICAL, "%s", c_error_memory_alloc);
    }
    if (a_store_obj_src->sign)
        a_store_obj_dst->sign = DAP_DUP_SIZE_RET_IF_FAIL(a_store_obj_src->sign, dap_sign_get_size(a_store_obj_src->sign),
                                                         a_store_obj_dst->group, a_store_obj_dst->key);
    if (a_store_obj_src->value) {
        if (!a_store_obj_src->value_len)
            log_it(L_WARNING, "Inconsistent global DB object copy requested");
        else
            a_store_obj_dst->value = DAP_DUP_SIZE_RET_IF_FAIL(a_store_obj_src->value, a_store_obj_src->value_len,
                                                              a_store_obj_dst->group, a_store_obj_dst->key, a_store_obj_dst->sign);
    }
}

/**
 * @brief Copies objects from a_store_obj.
 * @param a_store_obj a pointer to the source objects
 * @param a_store_count a number of objects
 * @return A pointer to the copied objects.
 */
dap_store_obj_t *dap_store_obj_copy(dap_store_obj_t *a_store_obj, size_t a_store_count)
{
dap_store_obj_t *l_store_obj, *l_store_obj_dst, *l_store_obj_src;

    if (!a_store_obj || !a_store_count)
        return NULL;

    if ( !(l_store_obj = DAP_NEW_Z_SIZE(dap_store_obj_t, sizeof(dap_store_obj_t) * a_store_count)) ) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }

    l_store_obj_dst = l_store_obj;
    l_store_obj_src = a_store_obj;
    for (int i = a_store_count; i--; l_store_obj_dst++, l_store_obj_src++)
        s_store_obj_copy_one(l_store_obj_dst, l_store_obj_src);
    return l_store_obj;
}

dap_store_obj_t *dap_store_obj_copy_ext(dap_store_obj_t *a_store_obj, void *a_ext, size_t a_ext_size)
{
    dap_store_obj_t *l_ret = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_store_obj_t, sizeof(dap_store_obj_t) + a_ext_size, NULL);
    s_store_obj_copy_one(l_ret, a_store_obj);
    if (a_ext_size)
        memcpy(l_ret->ext, a_ext, a_ext_size);
    return l_ret;
}

dap_store_obj_t* dap_global_db_store_objs_copy(dap_store_obj_t *a_store_objs_dest, const dap_store_obj_t *a_store_objs_src, size_t a_store_count)
{
    dap_return_val_if_pass(!a_store_objs_dest || !a_store_objs_src || !a_store_count, NULL);

    /* Run over array's elements */
    const dap_store_obj_t *l_obj = a_store_objs_src;
    for (dap_store_obj_t *l_cur = a_store_objs_dest; a_store_count--; l_cur++, l_obj++)
         s_store_obj_copy_one(l_cur, l_obj);
    return a_store_objs_dest;
}

/**
 * @brief Deallocates memory of objects.
 * @param a_store_obj a pointer to objects
 * @param a_store_count a number of objects
 * @return (none)
 */
void dap_store_obj_free(dap_store_obj_t *a_store_obj, size_t a_store_count)
{
    if(!a_store_obj || !a_store_count)
        return;

    for ( dap_store_obj_t *l_cur = a_store_obj; a_store_count--; ++l_cur ) {
        DAP_DEL_MULTY(l_cur->group, l_cur->key, l_cur->value, l_cur->sign);
    }
    DAP_DELETE(a_store_obj);
}

/**
 * @brief Applies objects to database.
 * @param a_store an pointer to the objects
 * @param a_store_count a number of objectss
 * @return Returns 0, if successful.
 */
int dap_global_db_driver_apply(dap_store_obj_t *a_store_obj, size_t a_store_count)
{
int l_ret;
dap_store_obj_t *l_store_obj_cur;

    if(!a_store_obj || !a_store_count)
        return -1;

    debug_if(g_dap_global_db_debug_more, L_DEBUG, "[%p] Process DB Request ...", a_store_obj);

    l_store_obj_cur = a_store_obj;                                          /* We have to  use a power of the address's incremental arithmetic */
    l_ret = 0;                                                              /* Preset return code to OK */

    if (a_store_count > 1)
        dap_global_db_driver_txn_start();

    if (s_drv_callback.apply_store_obj) {
        for(int i = a_store_count; !l_ret && i; l_store_obj_cur++, i--) {
            dap_global_db_driver_hash_t l_hash_cur = dap_global_db_driver_hash_get(l_store_obj_cur);
            if (dap_global_db_driver_hash_is_blank(&l_hash_cur)) {
                log_it(L_ERROR, "Item %zu / %zu is blank!", a_store_count - i + 1, a_store_count);
                continue;
            }
            if (!dap_global_db_isalnum_group_key(l_store_obj_cur, !(l_store_obj_cur->flags & DAP_GLOBAL_DB_RECORD_ERASE))) {
                log_it(L_MSG, "Item %zu / %zu is broken!", a_store_count - i, a_store_count);
                l_ret = -9;
                break;
            }
            if ( DAP_GLOBAL_DB_RC_NOT_FOUND == (l_ret = s_drv_callback.apply_store_obj(l_store_obj_cur)) ) {
                const char *l_item = l_store_obj_cur->key;
                if (!l_item)
                    l_item = l_store_obj_cur->crc ? dap_global_db_driver_hash_print(dap_global_db_driver_hash_get(l_store_obj_cur))
                                                  : "";
                const char *l_group = (*l_item) ? "Item" : "Group";
                const char *l_break = (*l_item) ? "/" : "";           
                log_it(L_INFO, "%s %s%s%s is missing (may be already deleted)", l_group, l_store_obj_cur->group, l_break, l_item);
            } else if (l_ret)
                log_it(L_ERROR, "Can't write item %s/%s (code %d)", l_store_obj_cur->group, l_store_obj_cur->key, l_ret);
        }
    } else {
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have apply_store_obj callback", s_used_driver);
    }

    if (a_store_count > 1)
        dap_global_db_driver_txn_end(true);

    debug_if(g_dap_global_db_debug_more, L_DEBUG, "[%p] Finished DB Request (code %d)", a_store_obj, l_ret);
    return l_ret;
}

/**
 * @brief Adds objects to a database.
 * @param a_store_obj objects to be added
 * @param a_store_count a number of added objects
 * @return Returns 0 if sucseesful.
 */
int dap_global_db_driver_add(dap_store_obj_t *a_store_obj, size_t a_store_count)
{
dap_store_obj_t *l_store_obj_cur = a_store_obj;

    for(int i = a_store_count; i--; l_store_obj_cur++)
        l_store_obj_cur->flags &= ~DAP_GLOBAL_DB_RECORD_ERASE;

    return dap_global_db_driver_apply(a_store_obj, a_store_count);
}

/**
 * @brief Deletes objects from a database.
 * @param a_store_obj objects to be deleted
 * @param a_store_count a number of deleted objects
 * @return Returns 0 if sucseesful.
 */
int dap_global_db_driver_delete(dap_store_obj_t * a_store_obj, size_t a_store_count)
{
if (!a_store_obj)
    return -1;


dap_store_obj_t *l_store_obj_cur = a_store_obj;

    for(int i = a_store_count; i--; l_store_obj_cur++)
        l_store_obj_cur->flags |= DAP_GLOBAL_DB_RECORD_ERASE;

    return dap_global_db_driver_apply(a_store_obj, a_store_count);
}

/**
 * @brief Gets a number of stored objects in a database by a_group and id.
 * @param a_group the group name string
 * @param a_iter data base iterator
 * @return Returns a number of objects.
 */
size_t dap_global_db_driver_count(const char *a_group, dap_global_db_driver_hash_t a_hash_from, bool a_with_holes)
{
    size_t l_count_out = 0;
    // read the number of items
    if (s_drv_callback.read_count_store)
        l_count_out = s_drv_callback.read_count_store(a_group, a_hash_from, a_with_holes);
    else
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have read_count_store callback", s_used_driver);
    return l_count_out;
}

/**
 * @brief Gets a list of group names matching the pattern.
 * Check whether the groups match the pattern a_group_mask, which is a shell wildcard pattern
 * patterns: [] {} [!] * ? https://en.wikipedia.org/wiki/Glob_(programming).
 * @param a_group_mask the group mask string
 * @return If successful, returns the list of group names, otherwise NULL.
 */
dap_list_t *dap_global_db_driver_get_groups_by_mask(const char *a_group_mask)
{
    dap_list_t *l_list = NULL;
    if(s_drv_callback.get_groups_by_mask)
        l_list = s_drv_callback.get_groups_by_mask(a_group_mask);
    else
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have get_groups_by_mask callback", s_used_driver);
    return l_list;
}


/**
 * @brief Reads last object in the database.
 * @param a_group the group name
 * @return If successful, a pointer to the object, otherwise NULL.
 */
dap_store_obj_t *dap_global_db_driver_read_last(const char *a_group, bool a_with_holes)
{
    dap_store_obj_t *l_ret = NULL;
    // read records using the selected database engine
    if(s_drv_callback.read_last_store_obj)
        l_ret = s_drv_callback.read_last_store_obj(a_group, a_with_holes);
    else
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have read_last_store_obj callback", s_used_driver);
    return l_ret;
}

dap_global_db_hash_pkt_t *dap_global_db_driver_hashes_read(const char *a_group, dap_global_db_driver_hash_t a_hash_from)
{
    dap_return_val_if_fail(a_group, NULL);
    // read records using the selected database engine
    if (s_drv_callback.read_hashes)
        return s_drv_callback.read_hashes(a_group, a_hash_from);
    else
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have read_hashes callback", s_used_driver);
    return NULL;
}

/**
 * @brief Read elements starting grom iterator
 * @param a_group the group name
 * @param a_iter data base iterator
 * @param a_count_out elements count
 * @return If successful, a pointer to the object, otherwise NULL.
 */
dap_store_obj_t *dap_global_db_driver_cond_read(const char *a_group, dap_global_db_driver_hash_t a_hash_from, size_t *a_count_out, bool a_with_holes)
{
    dap_return_val_if_fail(a_group, NULL);
    // read records using the selected database engine
    if (s_drv_callback.read_cond_store_obj)
        return s_drv_callback.read_cond_store_obj(a_group, a_hash_from, a_count_out, a_with_holes);
    else
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have read_cond_store callback", s_used_driver);
    return NULL;
}

/**
 * @brief Reads several objects from a database by a_group and a_key.
 * If a_key is NULL, reads whole group.
 * @param a_group a group name string
 * @param a_key  an object key string. If equal NULL, it means reading the whole group
 * @param a_count_out[in] a number of objects to be read, if 0 - no limits
 * @param a_count_out[out] a number of objects that were read
 * @return If successful, a pointer to an objects, otherwise NULL.
 */
dap_store_obj_t *dap_global_db_driver_read(const char *a_group, const char *a_key, size_t *a_count_out, bool a_with_holes)
{
    dap_store_obj_t *l_ret = NULL;
    // read records using the selected database engine
    if (s_drv_callback.read_store_obj)
        l_ret = s_drv_callback.read_store_obj(a_group, a_key, a_count_out, a_with_holes);
    else
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have read_store_obj callback", s_used_driver);
    return l_ret;
}
/**
 * @brief Reads all objects with timestamp below given 
 * @param a_group
 * @param a_timestamp
 */
dap_store_obj_t *dap_global_db_driver_read_obj_below_timestamp(const char *a_group, dap_nanotime_t a_timestamp, size_t *a_count)
{
    dap_store_obj_t *l_ret = NULL;
    // read records using the selected database engine
    if (s_drv_callback.read_store_obj_by_timestamp)
        l_ret = s_drv_callback.read_store_obj_by_timestamp(a_group, a_timestamp, a_count);
    else
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have read_obj_below_timestamp callback", s_used_driver);
    return l_ret;
}


/**
 * @brief Checks if an object is in a database by a_group and a_key.
 * @param a_group a group name string
 * @param a_key a object key string
 * @return Returns true if it is, false otherwise.
 */
bool dap_global_db_driver_is(const char *a_group, const char *a_key)
{
    // read records using the selected database engine
    if (s_drv_callback.is_obj && a_group && a_key)
        return s_drv_callback.is_obj(a_group, a_key);
    debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have is_obj callback", s_used_driver);
    return false;
}

bool dap_global_db_driver_is_hash(const char *a_group, dap_global_db_driver_hash_t a_hash)
{
    if (s_drv_callback.is_hash && a_group)
        return s_drv_callback.is_hash(a_group, a_hash);
    debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have is_hash callback", s_used_driver);
    return false;
}

dap_global_db_pkt_pack_t *dap_global_db_driver_get_by_hash(const char *a_group, dap_global_db_driver_hash_t *a_hashes, size_t a_count)
{
    if (s_drv_callback.get_by_hash && a_group)
        return s_drv_callback.get_by_hash(a_group, a_hashes, a_count);
    debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have get_by_hash callback", s_used_driver);
    return NULL;
}

int dap_global_db_driver_txn_start()
{
    if (s_drv_callback.transaction_start)
        return s_drv_callback.transaction_start();
    debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have transaction_start callback", s_used_driver);
    return -1;
}

int dap_global_db_driver_txn_end(bool a_commit)
{
    if (s_drv_callback.transaction_end)
        return  s_drv_callback.transaction_end(a_commit);
    debug_if(g_dap_global_db_debug_more, L_WARNING, "Driver %s not have transaction_end callback", s_used_driver);
    return -1;
}
