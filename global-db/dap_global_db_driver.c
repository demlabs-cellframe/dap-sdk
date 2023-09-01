/*
 * Authors:
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Kelvin Project https://github.com/kelvinblockchain
 * Copyright  (c) 2019
 * All rights reserved.

 This file is part of DAP (Deus Applications Prototypes) the open source project

 DAP (Deus Applicaions Prototypes) is free software: you can redistribute it and/or modify
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
#include "dap_proc_queue.h"
#include "dap_events.h"
#include "dap_list.h"
#include "dap_common.h"
#include "dap_global_db.h"

#ifdef DAP_CHAIN_GDB_ENGINE_SQLITE
#include "dap_global_db_driver_sqlite.h"
#endif

#ifdef DAP_CHAIN_GDB_ENGINE_CUTTDB
#include "dap_global_db_driver_cdb.h"
#endif

#ifdef DAP_CHAIN_GDB_ENGINE_MDBX
#include "dap_global_db_driver_mdbx.h"
#endif

#ifdef DAP_CHAIN_GDB_ENGINE_PGSQL
#include "dap_global_db_driver_pgsql.h"
#endif

#include "dap_global_db_driver.h"

#define LOG_TAG "db_driver"

// A selected database driver.
static char s_used_driver [32];                                             /* Name of the driver */


static dap_db_driver_callbacks_t s_drv_callback;                            /* A set of interface routines for the selected
                                                                            DB Driver at startup time */

/**
 * @brief Initializes a database driver.
 * @note You should Call this function before using the driver.
 * @param driver_name a string determining a type of database driver:
 * "сdb", "sqlite" ("sqlite3") or "pgsql"
 * @param a_filename_db a path to a database file
 * @return Returns 0, if successful; otherwise <0.
 */
int dap_db_driver_init(const char *a_driver_name, const char *a_filename_db, int a_mode_async)
{
int l_ret = -1;

    if (s_used_driver[0] )
        dap_db_driver_deinit();

    // Fill callbacks with zeros
    memset(&s_drv_callback, 0, sizeof(dap_db_driver_callbacks_t));

    // Setup driver name
    strncpy( s_used_driver, a_driver_name, sizeof(s_used_driver) - 1);

    dap_mkdir_with_parents(a_filename_db);

    // Compose path
    char l_db_path_ext[strlen(a_driver_name) + strlen(a_filename_db) + 6];
    snprintf(l_db_path_ext, sizeof(l_db_path_ext), "%s/gdb-%s", a_filename_db, a_driver_name);

   // Check for engine
    if(!dap_strcmp(s_used_driver, "ldb"))
        l_ret = -1;
#ifdef DAP_CHAIN_GDB_ENGINE_SQLITE
    else if(!dap_strcmp(s_used_driver, "sqlite") || !dap_strcmp(s_used_driver, "sqlite3") )
        l_ret = dap_db_driver_sqlite_init(l_db_path_ext, &s_drv_callback);
#endif
#ifdef DAP_CHAIN_GDB_ENGINE_CUTTDB
    else if(!dap_strcmp(s_used_driver, "cdb"))
        l_ret = dap_db_driver_cdb_init(l_db_path_ext, &s_drv_callback);
#endif
#ifdef DAP_CHAIN_GDB_ENGINE_MDBX
    else if(!dap_strcmp(s_used_driver, "mdbx"))
        l_ret = dap_db_driver_mdbx_init(l_db_path_ext, &s_drv_callback);
#endif

#ifdef DAP_CHAIN_GDB_ENGINE_PGSQL
    else if(!dap_strcmp(s_used_driver, "pgsql"))
        l_ret = dap_db_driver_pgsql_init(l_db_path_ext, &s_drv_callback);
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
void dap_db_driver_deinit(void)
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
int dap_db_driver_flush(void)
{
    return s_drv_callback.flush();
}

/**
 * @brief Copies objects from a_store_obj.
 * @param a_store_obj a pointer to the source objects
 * @param a_store_count a number of objects
 * @return A pointer to the copied objects.
 */
dap_store_obj_t* dap_store_obj_copy(dap_store_obj_t *a_store_obj, size_t a_store_count)
{
dap_store_obj_t *l_store_obj, *l_store_obj_dst, *l_store_obj_src;

    if(!a_store_obj || !a_store_count)
        return NULL;

    if ( !(l_store_obj = DAP_NEW_Z_SIZE(dap_store_obj_t, sizeof(dap_store_obj_t) * a_store_count)) )
         return NULL;

    l_store_obj_dst = l_store_obj;
    l_store_obj_src = a_store_obj;

    for( int i =  a_store_count; i--; l_store_obj_dst++, l_store_obj_src++) {
        *l_store_obj_dst = *l_store_obj_src;
        l_store_obj_dst->group = dap_strdup(l_store_obj_src->group);
        l_store_obj_dst->key = dap_strdup(l_store_obj_src->key);
        if (l_store_obj_src->value) {
            if (!l_store_obj->value_len)
                log_it(L_WARNING, "Inconsistent global DB object copy requested");
            else
                l_store_obj_dst->value = DAP_DUP_SIZE(l_store_obj_src->value, l_store_obj_src->value_len);
        }
    }

    return l_store_obj;
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

    dap_store_obj_t *l_store_obj_cur = a_store_obj;

    for ( ; a_store_count--; l_store_obj_cur++ ) {
        DAP_DEL_Z(l_store_obj_cur->group);
        DAP_DEL_Z(l_store_obj_cur->key);
        DAP_DEL_Z(l_store_obj_cur->value);
    }
    DAP_DEL_Z(a_store_obj);
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

    if (a_store_count > 1 && s_drv_callback.transaction_start)
        s_drv_callback.transaction_start();

    if(s_drv_callback.apply_store_obj)
        for(int i = a_store_count; !l_ret && i; l_store_obj_cur++, i--) {
            if ( 1 == (l_ret = s_drv_callback.apply_store_obj(l_store_obj_cur)) )
                log_it(L_INFO, "[%p] Item is missing (may be already deleted) %s/%s", a_store_obj, l_store_obj_cur->group, l_store_obj_cur->key);
            else if (l_ret < 0)
                log_it(L_ERROR, "[%p] Can't write item %s/%s (code %d)", a_store_obj, l_store_obj_cur->group, l_store_obj_cur->key, l_ret);
        }

    if(a_store_count > 1 && s_drv_callback.transaction_end)
        s_drv_callback.transaction_end();

    debug_if(g_dap_global_db_debug_more, L_DEBUG, "[%p] Finished DB Request (code %d)", a_store_obj, l_ret);
    return l_ret;
}

/**
 * @brief Adds objects to a database.
 * @param a_store_obj objects to be added
 * @param a_store_count a number of added objects
 * @return Returns 0 if sucseesful.
 */
int dap_global_db_driver_add(pdap_store_obj_t a_store_obj, size_t a_store_count)
{
dap_store_obj_t *l_store_obj_cur = a_store_obj;

    for(int i = a_store_count; i--; l_store_obj_cur++)
        l_store_obj_cur->type = DAP_DB$K_OPTYPE_ADD;

    return dap_global_db_driver_apply(a_store_obj, a_store_count);
}

/**
 * @brief Deletes objects from a database.
 * @param a_store_obj objects to be deleted
 * @param a_store_count a number of deleted objects
 * @return Returns 0 if sucseesful.
 */
int dap_global_db_driver_delete(pdap_store_obj_t a_store_obj, size_t a_store_count)
{
dap_store_obj_t *l_store_obj_cur = a_store_obj;

    for(int i = a_store_count; i--; l_store_obj_cur++)
        l_store_obj_cur->type = DAP_DB$K_OPTYPE_DEL;

    return dap_global_db_driver_apply(a_store_obj, a_store_count);
}

/**
 * @brief Gets a number of stored objects in a database by a_group and id.
 * @param a_group the group name string
 * @param a_id id
 * @return Returns a number of objects.
 */
size_t dap_global_db_driver_count(const char *a_group, uint64_t id)
{
    size_t l_count_out = 0;
    // read the number of items
    if(s_drv_callback.read_count_store)
        l_count_out = s_drv_callback.read_count_store(a_group, id);
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
    return l_list;
}


/**
 * @brief Reads last object in the database.
 * @param a_group the group name
 * @return If successful, a pointer to the object, otherwise NULL.
 */
dap_store_obj_t* dap_global_db_driver_read_last(const char *a_group)
{
    dap_store_obj_t *l_ret = NULL;
    // read records using the selected database engine
    if(s_drv_callback.read_last_store_obj)
        l_ret = s_drv_callback.read_last_store_obj(a_group);
    return l_ret;
}

/**
 * @brief Reads several objects from a database by a_group and id.
 * @param a_group the group name string
 * @param a_id id
 * @param a_count_out[in] a number of objects to be read, if 0 - no limits
 * @param a_count_out[out] a count of objects that were read
 * @return If successful, a pointer to an objects, otherwise NULL.
 */
dap_store_obj_t* dap_global_db_driver_cond_read(const char *a_group, uint64_t id, size_t *a_count_out)
{
    dap_store_obj_t *l_ret = NULL;
    // read records using the selected database engine
    if(s_drv_callback.read_cond_store_obj)
        l_ret = s_drv_callback.read_cond_store_obj(a_group, id, a_count_out);
    return l_ret;
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
dap_store_obj_t* dap_global_db_driver_read(const char *a_group, const char *a_key, size_t *a_count_out)
{
    dap_store_obj_t *l_ret = NULL;
    // read records using the selected database engine
    if(s_drv_callback.read_store_obj)
        l_ret = s_drv_callback.read_store_obj(a_group, a_key, a_count_out);
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
    if(s_drv_callback.is_obj && a_group && a_key)
        return s_drv_callback.is_obj(a_group, a_key);
    else
        return false;
}
