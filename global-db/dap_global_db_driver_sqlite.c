/*
* Authors:
* Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
* Roman Khlopkov <roman.khlopkov@demlabs.net>
* Pavel Uhanov <pavel.uhanov@demlabs.net>
* Cellframe       https://cellframe.net
* DeM Labs Inc.   https://demlabs.net
* Copyright  (c) 2017-2024
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

#include <sqlite3.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <stdatomic.h>

#ifdef DAP_OS_UNIX
#include <unistd.h>
#endif
#include "dap_global_db_driver_sqlite.h"
#include "dap_common.h"
#include "dap_time.h"
#include "dap_file_utils.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_global_db_pkt.h"
#include "dap_global_db.h"
#include "dap_proc_thread.h"

#define LOG_TAG "db_sqlite"
#define DAP_GLOBAL_DB_TYPE_CURRENT DAP_GLOBAL_DB_TYPE_SQLITE

typedef struct conn_pool_item {
    sqlite3 *conn;                                                  /* SQLITE connection context itself */
    int idx;                                                    /* Just index, no more */
    atomic_flag busy_conn;                                      /* Connection busy flag */
    atomic_flag busy_trans;                                     /* Outstanding transaction busy flag */
    atomic_ullong  usage;                                       /* Usage counter */
} conn_list_item_t;

extern int g_dap_global_db_debug_more;                         /* Enable extensible debug output */

static char s_filename_db [MAX_PATH];

static uint32_t s_attempts_count = 10;
static const int s_sleep_period = 500 * 1000;  /* Wait 0.5 sec */;
static bool s_db_inited = false;
static _Thread_local conn_list_item_t *s_conn = NULL;  // local connection


static void s_connection_destructor(UNUSED_ARG void *a_conn) {
    sqlite3_close(s_conn->conn);
    log_it(L_DEBUG, "Close  connection: @%p/%p, usage: %llu", s_conn, s_conn->conn, s_conn->usage);
    DAP_DEL_Z(s_conn);
}


/**
 * @brief Opens a SQLite database and adds byte_to_bin function.
 * @param a_filename_utf8 a SQLite database file name
 * @param a_flags database access flags (SQLITE_OPEN_READONLY, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)
 * @param a_error_message[out] an error message that's received from the SQLite database
 * @return Returns a pointer to an instance of SQLite database structure.
 */
sqlite3* s_db_sqlite_open(const char *a_filename_utf8, int a_flags, char **a_error_message)
{
    sqlite3 *l_db = NULL;

    int l_rc = sqlite3_open_v2(a_filename_utf8, &l_db, a_flags, NULL); // SQLITE_OPEN_FULLMUTEX by default set with sqlite3_config SERIALIZED
    // if unable to open the database file
    if(l_rc == SQLITE_CANTOPEN) {
        log_it(L_DEBUG,"No database on path %s, creating one from scratch", a_filename_utf8);
        if(l_db)
            sqlite3_close(l_db);
        // try to create database
        l_rc = sqlite3_open_v2(a_filename_utf8, &l_db, a_flags | SQLITE_OPEN_CREATE, NULL);
    }

    if(l_rc != SQLITE_OK) {
        log_it(L_CRITICAL,"Can't open database on path %s (code %d: \"%s\" )", a_filename_utf8, l_rc, sqlite3_errstr(l_rc));
        if(a_error_message)
            *a_error_message = sqlite3_mprintf("Can't open database: %s\n", sqlite3_errmsg(l_db));
        sqlite3_close(l_db);
        return NULL;
    }
    return l_db;
}

/**
 * @brief Free connections busy flags.
 * @param a_conn a connection item
 * @param a_trans if false clear connection flag, if true - outstanding transaction
 */
static inline void s_db_sqlite_free_connection(conn_list_item_t *a_conn, bool a_trans)
{
    if (g_dap_global_db_debug_more)
        log_it(L_DEBUG, "Free  l_conn: @%p/%p, usage: %llu", a_conn, a_conn->conn, a_conn->usage);
    if (a_trans)
        atomic_flag_clear(&a_conn->busy_trans);  
    else
        atomic_flag_clear(&a_conn->busy_conn);
}

/**
 * @brief Free connection, dynamic num sql items and finalize sqlite3_stmts.
 * @param a_conn connection item to free
 * @param a_count num of pairs sql item + sqlite3_stmts
 */
static void s_db_sqlite_clean(conn_list_item_t *a_conn, size_t a_count, ... ) {
    va_list l_args_list;
    va_start(l_args_list, a_count);
    for (size_t i = 0; i < a_count; ++i)
        sqlite3_free(va_arg(l_args_list, void*));
    for (size_t i = 0; i < a_count; ++i)
        sqlite3_finalize(va_arg(l_args_list, void*));
    va_end(l_args_list);
    s_db_sqlite_free_connection(a_conn, false);
}

/**
 * @brief One step to sqlite3_stmt with 7 try is sql bust
 * @param a_stmt sqlite3_stmt to step
 * @param a_error_msg module name
 * @return result code
 */
static int s_db_sqlite_step(sqlite3_stmt *a_stmt, const char *a_error_msg)
{
    dap_return_val_if_pass(!a_stmt, SQLITE_ERROR);
    int l_ret = 0;
    for ( char i = s_attempts_count; i--; ) {
        l_ret = sqlite3_step(a_stmt);
        if (l_ret != SQLITE_BUSY && l_ret != SQLITE_LOCKED)
            break;
        dap_usleep(s_sleep_period);
    }
    debug_if(l_ret != SQLITE_ROW && l_ret != SQLITE_DONE, L_DEBUG, "SQLite step in %s error %d(%s)", a_error_msg ? a_error_msg : "", l_ret, sqlite3_errstr(l_ret));
    return l_ret;
}

/**
 * @brief One step to sqlite3_stmt with 7 try is sql bust
 * @param a_db a pointer to an instance of SQLite connection
 * @param a_str_query SQL query string
 * @param a_stmt pointer to generate sqlite3_stmt
 * @param a_error_msg module name
 * @return result code
 */
static int s_db_sqlite_prepare(sqlite3 *a_db, const char *a_str_query, sqlite3_stmt **a_stmt, const char *a_error_msg)
{
    dap_return_val_if_pass(!a_stmt || !a_str_query || !a_stmt, SQLITE_ERROR);
    int l_ret = 0;
    for (char i = s_attempts_count; i--; ) {
        l_ret = sqlite3_prepare_v2(a_db, a_str_query, -1, a_stmt, NULL);
        if (l_ret != SQLITE_BUSY && l_ret != SQLITE_LOCKED)
            break;
        dap_usleep(s_sleep_period);
    }
    debug_if(l_ret != SQLITE_OK, L_DEBUG, "SQLite prepare %s error %d(%s)", a_error_msg ? a_error_msg : "", sqlite3_errcode(a_db), sqlite3_errmsg(a_db));
    return l_ret;
}

/**
 * @brief One step to sqlite3_stmt with 7 try is sql bust
 * @param a_stmt sqlite3_stmt to step
 * @param a_pos blob element position in query
 * @param a_data blob data
 * @param a_data_size blob data size
 * @param a_destructor SQL destructor type
 * @param a_error_msg module name
 * @return result code
 */
static int s_db_sqlite_bind_blob64(sqlite3_stmt *a_stmt, int a_pos, const void *a_data, sqlite3_uint64 a_data_size, sqlite3_destructor_type a_destructor, const char *a_error_msg)
{
    dap_return_val_if_pass(!a_stmt || !a_data || !a_data_size || a_pos < 0, SQLITE_ERROR);
    int l_ret = 0;
    for ( char i = s_attempts_count; i--; ) {
        l_ret = sqlite3_bind_blob64(a_stmt, a_pos, a_data, a_data_size, a_destructor);
        if (l_ret != SQLITE_BUSY && l_ret != SQLITE_LOCKED)
            break;
        dap_usleep(s_sleep_period);
    }
    debug_if(l_ret != SQLITE_OK, L_DEBUG, "SQLite bind blob64 %s error %d(%s)", a_error_msg ? a_error_msg : "", l_ret, sqlite3_errstr(l_ret));
    return l_ret;
}

/**
 * @brief Executes SQL statements.
 * @param a_db a pointer to an instance of SQLite connection
 * @param a_query the SQL statement
 * @param a_hash pointer to data hash
 * @param a_value pointer to data
 * @param a_value_len data len to write
 * @param a_sign record sign
 * @return result code.
 */
static int s_db_sqlite_exec(sqlite3 *a_db, const char *a_query, dap_global_db_driver_hash_t *a_hash, byte_t *a_value, size_t a_value_len, dap_sign_t *a_sign)
{
    dap_return_val_if_pass(!a_db || !a_query, SQLITE_ERROR);
    int l_ret = 0;
    sqlite3_stmt *l_stmt = NULL;
    if (
        (l_ret = s_db_sqlite_prepare(a_db, a_query, &l_stmt, a_query)) != SQLITE_OK ||
        (a_hash && (l_ret = s_db_sqlite_bind_blob64(l_stmt, 1, a_hash, sizeof(*a_hash), SQLITE_STATIC, a_query)) != SQLITE_OK) ||
        (a_value && (l_ret = s_db_sqlite_bind_blob64(l_stmt, 2, a_value, a_value_len, SQLITE_STATIC, a_query)) != SQLITE_OK) ||
        (a_sign && (l_ret = s_db_sqlite_bind_blob64(l_stmt, 3, a_sign, dap_sign_get_size(a_sign), SQLITE_STATIC, a_query)) != SQLITE_OK)
        ) {
        sqlite3_finalize(l_stmt);
        return l_ret;
    }
    l_ret = s_db_sqlite_step(l_stmt, a_query);
    sqlite3_finalize(l_stmt);
    if (l_ret != SQLITE_DONE && l_ret != SQLITE_ROW) {
        return l_ret;
    }
    return SQLITE_OK;
}

/**
 * @brief Prepare connection item
 * @param a_trans outstanding transaction flag
 * @return pointer to connection item, otherwise NULL.
 */
static conn_list_item_t *s_db_sqlite_get_connection(bool a_trans)
{
// sanity check
    dap_return_val_if_pass_err(!s_db_inited, NULL, "SQLite driver not inited");
// func work
    static int l_conn_idx = 0;
    if (!s_conn) {
        s_conn = DAP_NEW_Z_RET_VAL_IF_FAIL(conn_list_item_t, NULL);
        pthread_key_t s_destructor_key;
        pthread_key_create(&s_destructor_key, s_connection_destructor);
        pthread_setspecific(s_destructor_key, (const void *)s_conn);
        char *l_error_message = NULL;
        if ( !(s_conn->conn = s_db_sqlite_open(s_filename_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, &l_error_message)) ) {
            log_it(L_ERROR, "Can't init sqlite err: \"%s\"", l_error_message ? l_error_message: "UNKNOWN");
            sqlite3_free(l_error_message);
            DAP_DEL_Z(s_conn);
            return NULL;
        }
        s_conn->idx = l_conn_idx++;
        if((s_db_sqlite_exec(s_conn->conn, "PRAGMA synchronous = NORMAL", NULL, NULL, 0, NULL)))
            log_it(L_ERROR, "can't set new synchronous mode\n");
        if(s_db_sqlite_exec(s_conn->conn, "PRAGMA journal_mode = WAL", NULL, NULL, 0, NULL))
            log_it(L_ERROR, "can't set new journal mode\n");
        if(s_db_sqlite_exec(s_conn->conn, "PRAGMA page_size = 4096", NULL, NULL, 0, NULL))
            log_it(L_ERROR, "can't set page_size\n");
        log_it(L_DEBUG, "SQL connection #%d is created @%p", s_conn->idx, s_conn);
    }
    // busy check
    if (a_trans) {
        if (atomic_flag_test_and_set (&s_conn->busy_trans)) {
            log_it(L_ERROR, "Busy check error in connection %p idx %d", s_conn, s_conn->idx);
            return  NULL;
        }
    } else {
        if (atomic_flag_test_and_set (&s_conn->busy_conn)) {
            log_it(L_ERROR, "Busy check error in connection %p idx %d", s_conn, s_conn->idx);
            return  NULL;
        }
    }
    atomic_fetch_add(&s_conn->usage, 1);
    if (g_dap_global_db_debug_more )
        log_it(L_DEBUG, "Start use connection %p, usage %llu, idx %d", s_conn, s_conn->usage, s_conn->idx);
    return s_conn;
}

/**
 * @brief Deinitializes a SQLite database.
 * @return result code.
 */
int s_db_sqlite_deinit(void)
{
    if (!s_db_inited) {
        log_it(L_WARNING, "SQLite driver already deinited");
        return -1;
    }
    s_connection_destructor(NULL);
    s_db_inited = false;
    return sqlite3_shutdown();
}

/**
 * @brief Creates a table and unique index in the s_db database.
 * @param a_table_name a table name string
 * @param a_conn connection item to use query
 * @return result code
 */
static int s_db_sqlite_create_group_table(const char *a_table_name, conn_list_item_t *a_conn)
{
// sanity check
    dap_return_val_if_pass(!a_table_name || !a_conn, -EINVAL);
    char *l_query = dap_strdup_printf("CREATE TABLE IF NOT EXISTS '%s'"
        "(driver_key BLOB UNIQUE NOT NULL PRIMARY KEY ON CONFLICT REPLACE, key TEXT UNIQUE NOT NULL, flags INTEGER, value BLOB, sign BLOB)",
        a_table_name);
    int l_ret = s_db_sqlite_exec(a_conn->conn, l_query, NULL, NULL, 0, NULL);
    return DAP_DELETE(l_query), l_ret;
}

/**
 * @brief Applies an object to a database.
 * @param a_store_obj a pointer to the object structure
 * @return result code.
 */
int s_db_sqlite_apply_store_obj(dap_store_obj_t *a_store_obj)
{
// sanity check
    dap_return_val_if_pass(!a_store_obj || !a_store_obj->group || (!a_store_obj->crc && a_store_obj->key), -EINVAL);
    uint8_t l_type_erase = a_store_obj->flags & DAP_GLOBAL_DB_RECORD_ERASE;
    dap_return_val_if_pass(!a_store_obj->key && !l_type_erase, -EINVAL);
// func work
    // execute request
    conn_list_item_t *l_conn = s_db_sqlite_get_connection(false);
    if (!l_conn)
        return -2;

    int l_ret = 0;
    char *l_query = NULL, *l_table_name = dap_str_replace_char(a_store_obj->group, '.', '_');
    if (!l_type_erase) {
        if (!a_store_obj->key) {
            log_it(L_ERROR, "Global DB store object unsigned");
            l_ret = -3;
            goto clean_and_ret;
        } else { //add one record
            l_query = sqlite3_mprintf("INSERT INTO '%s' VALUES(?, '%s', '%d', ?, ?) "
            "ON CONFLICT(key) DO UPDATE SET driver_key = excluded.driver_key, flags = excluded.flags, value = excluded.value, sign = excluded.sign;",
                                                  l_table_name, a_store_obj->key, (int)(a_store_obj->flags & ~DAP_GLOBAL_DB_RECORD_NEW));
        }
        dap_global_db_driver_hash_t l_driver_key = dap_global_db_driver_hash_get(a_store_obj);
        l_ret = s_db_sqlite_exec(l_conn->conn, l_query, &l_driver_key, a_store_obj->value, a_store_obj->value_len, a_store_obj->sign);
        if (l_ret == SQLITE_ERROR) {
            // create table and repeat request
            if (s_db_sqlite_create_group_table(l_table_name, l_conn) == SQLITE_OK)
                l_ret = s_db_sqlite_exec(l_conn->conn, l_query, &l_driver_key, a_store_obj->value, a_store_obj->value_len, a_store_obj->sign);
        }
    } else {
        if (a_store_obj->key) //delete one record
            l_query = sqlite3_mprintf("DELETE FROM '%s' WHERE key = '%s'", l_table_name, a_store_obj->key);
        else // remove all group
            l_query = sqlite3_mprintf("DROP TABLE IF EXISTS '%s'", l_table_name);
        l_ret = s_db_sqlite_exec(l_conn->conn, l_query, NULL, NULL, 0, NULL);
    }
clean_and_ret:
    s_db_sqlite_free_connection(l_conn, false);
    if (l_query)
        sqlite3_free(l_query);
    DAP_DELETE(l_table_name);
    return l_ret;
}

/**
 * @brief Fills a object from a row
 * @param a_group a group name string
 * @param a_obj a pointer to the object
 * @param a_stmt a ponter to the sqlite3_stmt
 * @return result code
 */
static int s_db_sqlite_fill_one_item(const char *a_group, dap_store_obj_t *a_obj, sqlite3_stmt *a_stmt)
{
// sanity check
    dap_return_val_if_pass(!a_group || !a_obj || !a_stmt, SQLITE_ERROR);
// preparing
    int l_ret = s_db_sqlite_step(a_stmt, "fill one item");
    if(l_ret != SQLITE_ROW)
        goto clean_and_ret;
    a_obj->group = dap_strdup(a_group);
    size_t l_count_col = sqlite3_column_count(a_stmt);
    for (size_t i = 0; i < l_count_col; ++i) {
        switch ( sqlite3_column_type(a_stmt, i) ) {
        case SQLITE_BLOB:
            if ( i == 0 ) {
                dap_global_db_driver_hash_t *l_driver_key = (dap_global_db_driver_hash_t *)sqlite3_column_blob(a_stmt, i);
                a_obj->timestamp = be64toh(l_driver_key->bets);
                a_obj->crc = be64toh(l_driver_key->becrc);
            } else if ( i == 3 ) {
                a_obj->value_len = sqlite3_column_bytes(a_stmt, i);
                a_obj->value = DAP_DUP_SIZE((byte_t*)sqlite3_column_blob(a_stmt, i), a_obj->value_len);
            } else if ( i == 4 ) {
                 a_obj->sign = DAP_DUP_SIZE((dap_sign_t*)sqlite3_column_blob(a_stmt, i), sqlite3_column_bytes(a_stmt, i));
            } continue;
        case SQLITE_TEXT:
            if ( i == 1 ) {
                 a_obj->key = dap_strdup((const char*)sqlite3_column_text(a_stmt, i));
            } continue;
        case SQLITE_INTEGER:
            if ( i == 2 ) {
                a_obj->flags = sqlite3_column_int64(a_stmt, i);
            } continue;
        default: continue;
        }
    }
clean_and_ret:
    return l_ret;
}

/**
 * @brief Reads a last object from the s_db database.
 * @param a_group a group name string
 * @param a_with_holes if true - read any records, if false - only actual records
 * @return If successful, a pointer to the object, otherwise NULL.
 */
static dap_store_obj_t* s_db_sqlite_read_last_store_obj(const char *a_group, bool a_with_holes)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_sqlite_get_connection(false)), NULL);
// preparing
    dap_store_obj_t *l_ret = NULL;
    sqlite3_stmt *l_stmt = NULL;
    char *l_table_name = dap_str_replace_char(a_group, '.', '_');
    char *l_str_query = sqlite3_mprintf("SELECT * FROM '%s'"
                                        " WHERE flags & '%d' %s 0"
                                        " ORDER BY driver_key DESC LIMIT 1",
                                        l_table_name, DAP_GLOBAL_DB_RECORD_DEL,
                                        a_with_holes ? ">=" : "=");
    DAP_DEL_Z(l_table_name);
    if (!l_str_query) {
        log_it(L_ERROR, "Error in SQL request forming");
        goto clean_and_ret;
    }
    
    if(s_db_sqlite_prepare(l_conn->conn, l_str_query, &l_stmt, "last read")!= SQLITE_OK) {
        goto clean_and_ret;
    }
// memory alloc
    l_ret = DAP_NEW_Z(dap_store_obj_t);
    if (!l_ret) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        goto clean_and_ret;
    }
// fill item
    int l_ret_code = s_db_sqlite_fill_one_item(a_group, l_ret, l_stmt);
    if(l_ret_code != SQLITE_ROW && l_ret_code != SQLITE_DONE) {
        log_it(L_ERROR, "SQLite last read error %d(%s)", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
        goto clean_and_ret;
    }
    if( l_ret_code != SQLITE_ROW) {
        log_it(L_INFO, "There are no records satisfying the last read request");
    }
clean_and_ret:
    s_db_sqlite_clean(l_conn, 1, l_str_query, l_stmt);    
    return l_ret;
}

/**
 * @brief Forming objects pack from hash list.
 * @param a_group a group name string
 * @param a_hashes pointer to hashes
 * @param a_count hashes num
 * @return If successful, a pointer to objects pack, otherwise NULL.
 */
static dap_global_db_pkt_pack_t *s_db_sqlite_get_by_hash(const char *a_group, dap_global_db_driver_hash_t *a_hashes, size_t a_count)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !a_hashes || !a_count || !(l_conn = s_db_sqlite_get_connection(false)), NULL);
// preparing
    const char *l_error_msg = "get by hash";
    dap_global_db_pkt_pack_t *l_ret = NULL;
    sqlite3_stmt *l_stmt_count = NULL, *l_stmt = NULL, *l_stmt_size = NULL;
    char *l_table_name = dap_str_replace_char(a_group, '.', '_'), *l_blob_str = DAP_NEW_Z_SIZE(char, a_count * 2);
    if (!l_blob_str || !l_table_name) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_MULTY(l_table_name, l_blob_str);
        return NULL;
    }
    for (size_t k = 0; k < a_count; memcpy(l_blob_str + 2 * (k++), "?,", 2));
    l_blob_str[2 * a_count - 1] = '\0';

    char *l_str_query_count = sqlite3_mprintf("SELECT COUNT(*) FROM '%s' "
                                        " WHERE driver_key IN (%s)",
                                        l_table_name, l_blob_str);
    char *l_str_query_size = sqlite3_mprintf("SELECT SUM(LENGTH(key)) + SUM(LENGTH(value)) + SUM(LENGTH(sign)) FROM '%s' "
                                        " WHERE driver_key IN (%s)",
                                        l_table_name, l_blob_str);
    char *l_str_query = sqlite3_mprintf("SELECT * FROM '%s'"
                                        " WHERE driver_key IN (%s) ORDER BY driver_key",
                                        l_table_name, l_blob_str);
    DAP_DEL_MULTY(l_table_name, l_blob_str);
    if (!l_str_query_count || !l_str_query) {
        log_it(L_ERROR, "Error in SQL request forming");
        goto clean_and_ret;
    }
    if( s_db_sqlite_prepare(l_conn->conn, l_str_query_count, &l_stmt_count, l_error_msg)!= SQLITE_OK ||
        s_db_sqlite_prepare(l_conn->conn, l_str_query_size, &l_stmt_size, l_error_msg)  != SQLITE_OK ||
        s_db_sqlite_prepare(l_conn->conn, l_str_query, &l_stmt, l_error_msg)            != SQLITE_OK )
    {
        goto clean_and_ret;
    }
    for (size_t i = 1; i <= a_count; ++i) {
        if( s_db_sqlite_bind_blob64(l_stmt_count, i,a_hashes + i - 1, sizeof(*a_hashes), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
            s_db_sqlite_bind_blob64(l_stmt_size, i, a_hashes + i - 1, sizeof(*a_hashes), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
            s_db_sqlite_bind_blob64(l_stmt, i,      a_hashes + i - 1, sizeof(*a_hashes), SQLITE_STATIC, l_error_msg) != SQLITE_OK )
        {
            goto clean_and_ret;
        }
    }
    if ( s_db_sqlite_step(l_stmt_count, l_error_msg) != SQLITE_ROW || s_db_sqlite_step(l_stmt_size, l_error_msg) != SQLITE_ROW ) {
        goto clean_and_ret;
    }
// memory alloc
    int64_t i, j, l_count = sqlite3_column_int64(l_stmt_count, 0), l_size = sqlite3_column_int64(l_stmt_size, 0);
    if ( l_count <= 0 || l_size <= 0 ) {
        log_it(L_INFO, "There are no records satisfying the get by hash request");
        goto clean_and_ret;
    }
    size_t l_group_name_len = strlen(a_group) + 1;
    size_t l_data_size = l_count * (sizeof(dap_global_db_pkt_t) + l_group_name_len + 1) + l_size;
    l_ret = DAP_NEW_Z_SIZE(dap_global_db_pkt_pack_t, sizeof(dap_global_db_pkt_pack_t) + l_data_size);
    if ( !l_ret ) {
        log_it(L_CRITICAL, "Memory allocation error!");
        goto clean_and_ret;
    }
    byte_t *l_data_pos = l_ret->data, *l_data_end = l_data_pos + l_data_size;
    for (i = 0; i < l_count && s_db_sqlite_step(l_stmt, l_error_msg) == SQLITE_ROW; ++i) {
        dap_global_db_pkt_t *l_cur_pkt = (dap_global_db_pkt_t*)(l_data_pos);
        l_data_pos = l_cur_pkt->data;
        if ( l_data_pos + l_group_name_len > l_data_end )
            break;
        l_data_pos = dap_mempcpy(l_data_pos, a_group, l_cur_pkt->group_len = l_group_name_len);
        int l_count_col = sqlite3_column_count(l_stmt);
        for (j = 0; j < l_count_col; ++j) {
            switch ( sqlite3_column_type(l_stmt, j) ) {
            case SQLITE_BLOB:
                if ( j == 0 ) {
                    dap_global_db_driver_hash_t *l_driver_key = (dap_global_db_driver_hash_t*)sqlite3_column_blob(l_stmt, j);
                    l_cur_pkt->timestamp = be64toh(l_driver_key->bets);
                    l_cur_pkt->crc = be64toh(l_driver_key->becrc);
                } else if ( j == 3 ) {
                    l_cur_pkt->value_len = sqlite3_column_bytes(l_stmt, j);
                    if ( l_data_pos + l_cur_pkt->value_len > l_data_end )
                        break;
                    l_data_pos = dap_mempcpy(l_data_pos, sqlite3_column_blob(l_stmt, j), l_cur_pkt->value_len);
                } else if ( j == 4 ) {
                    size_t l_sign_size = sqlite3_column_bytes(l_stmt, j);
                    if (l_sign_size) {
                        dap_sign_t *l_sign = (dap_sign_t*)sqlite3_column_blob(l_stmt, j);
                        if ( dap_sign_get_size(l_sign) != l_sign_size || l_data_pos + l_sign_size > l_data_end ) {
                            log_it(L_ERROR, "Wrong sign size in GDB group %s", a_group);
                            break;
                        }
                        l_data_pos = dap_mempcpy(l_data_pos, sqlite3_column_blob(l_stmt, j), l_sign_size);
                    }
                } continue;
            case SQLITE_TEXT:
                if ( j == 1 ) {
                    l_cur_pkt->key_len = sqlite3_column_bytes(l_stmt, j);
                    if ( l_data_pos + l_cur_pkt->key_len > l_data_end )
                        break;
                    l_data_pos = dap_mempcpy(l_data_pos, sqlite3_column_text(l_stmt, j), l_cur_pkt->key_len++) + 1;
                } continue;
            case SQLITE_INTEGER:
                if ( j == 2 ) {
                    if (sqlite3_column_bytes(l_stmt, j))
                        l_cur_pkt->flags = sqlite3_column_int64(l_stmt, j) & DAP_GLOBAL_DB_RECORD_DEL;
                } continue;
            default:
                continue;
            }
            // Some break occured, escape from loops
            j = -1;
            break;
        }
        if ( j == -1 ) {
            l_data_pos = (byte_t*)l_cur_pkt;
            break;
        }
        l_cur_pkt->data_len = (uint32_t)(l_data_pos - l_cur_pkt->data);
        l_ret->data_size = (uint64_t)(l_data_pos - l_ret->data);
    }
    l_ret->obj_count = i;
    if (i < l_count) {
        log_it(L_ERROR, "Invalid pack size, only %ld / %ld pkts (%zu / %zu bytes) fit the storage",
                        i, l_count, l_ret->data_size, l_data_size);
        size_t l_new_size = (size_t)(l_data_pos - (byte_t*)l_ret);
        dap_global_db_pkt_pack_t *l_new_pack = DAP_REALLOC(l_ret, l_new_size);
        if (l_new_pack)
            l_ret = l_new_pack;
        else
            DAP_DEL_Z(l_ret);
    }
clean_and_ret:
    s_db_sqlite_clean(l_conn, 3, l_str_query, l_str_query_count, l_str_query_size, l_stmt, l_stmt_count, l_stmt_size);
    return l_ret;
}

/**
 * @brief Forming hashes pack started from concretic hash.
 * @param a_group a group name string
 * @param a_hash_from startin hash (not include to result)
 * @return If successful, a pointer to hashes pack, otherwise NULL.
 */
static dap_global_db_hash_pkt_t *s_db_sqlite_read_hashes(const char *a_group, dap_global_db_driver_hash_t a_hash_from)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_sqlite_get_connection(false)), NULL);
// preparing
    const char *l_error_msg = "hashes read";
    dap_global_db_hash_pkt_t *l_ret = NULL;
    sqlite3_stmt *l_stmt_count = NULL, *l_stmt = NULL;
    char *l_table_name = dap_str_replace_char(a_group, '.', '_');
    char *l_str_query_count = sqlite3_mprintf("SELECT COUNT(*) FROM '%s' "
                                        " WHERE driver_key > ?",
                                        l_table_name);
    char *l_str_query = sqlite3_mprintf("SELECT driver_key FROM '%s'"
                                        " WHERE driver_key > ?"
                                        " ORDER BY driver_key LIMIT '%d'",
                                        l_table_name, (int)DAP_GLOBAL_DB_COND_READ_KEYS_DEFAULT);
    DAP_DEL_Z(l_table_name);
    if (!l_str_query_count || !l_str_query) {
        log_it(L_ERROR, "Error in SQL request forming");
        goto clean_and_ret;
    }
    
    if(s_db_sqlite_prepare(l_conn->conn, l_str_query_count, &l_stmt_count, l_error_msg)!= SQLITE_OK ||
        s_db_sqlite_bind_blob64(l_stmt_count, 1, &a_hash_from, sizeof(a_hash_from), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
        s_db_sqlite_prepare(l_conn->conn, l_str_query, &l_stmt, l_error_msg)!= SQLITE_OK ||
        s_db_sqlite_bind_blob64(l_stmt, 1, &a_hash_from, sizeof(a_hash_from), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
        s_db_sqlite_step(l_stmt_count, l_error_msg) != SQLITE_ROW)
    {
        goto clean_and_ret;
    }
// memory alloc
    uint64_t l_count = sqlite3_column_int64(l_stmt_count, 0);
    uint64_t l_blank_add = l_count;
    l_count = dap_min(l_count, DAP_GLOBAL_DB_COND_READ_KEYS_DEFAULT);
    if (!l_count) {
        log_it(L_INFO, "There are no records satisfying the hashes read request");
        goto clean_and_ret;
    }
    l_blank_add = l_count == l_blank_add;
    l_count += l_blank_add;
    size_t l_group_name_len = strlen(a_group) + 1;
    l_ret = DAP_NEW_Z_SIZE(dap_global_db_hash_pkt_t, sizeof(dap_global_db_hash_pkt_t) + l_count * sizeof(dap_global_db_driver_hash_t) + l_group_name_len);
    if (!l_ret) {
        log_it(L_CRITICAL, "Memory allocation error!");
        goto clean_and_ret;
    }
// data forming
    size_t l_count_out;
    l_ret->group_name_len = l_group_name_len;
    byte_t *l_pos = dap_mempcpy(l_ret->group_n_hashses, a_group, l_group_name_len);

    for ( l_count_out = 0; l_count_out < l_count 
            && s_db_sqlite_step(l_stmt, l_error_msg) == SQLITE_ROW
            && sqlite3_column_type(l_stmt, 0) == SQLITE_BLOB;
            ++l_count_out )
    {
        if ( sqlite3_column_bytes(l_stmt, 0) != sizeof(dap_global_db_driver_hash_t) ) {
            log_it(L_ERROR, "Invalid hash size, skip record");
            continue;
        }
        l_pos = dap_mempcpy(l_pos, sqlite3_column_blob(l_stmt, 0), sizeof(dap_global_db_driver_hash_t));
    }
    l_ret->hashes_count = l_count_out + l_blank_add;
clean_and_ret:
    s_db_sqlite_clean(l_conn, 2, l_str_query, l_str_query_count, l_stmt, l_stmt_count);
    return l_ret;
}

/**
 * @brief Reads some objects from a database by conditions started from concretic hash
 * @param a_group a group name string
 * @param a_hash_from startin hash (not include to result)
 * @param a_count_out[in] a number of objects to be read, if equals 0 reads with no limits
 * @param a_count_out[out] a number of objects that were read
 * @param a_with_holes if true - read any records, if false - only actual records
 * @return If successful, a pointer to an objects, otherwise NULL.
 */
static dap_store_obj_t* s_db_sqlite_read_cond_store_obj(const char *a_group, dap_global_db_driver_hash_t a_hash_from, size_t *a_count_out, bool a_with_holes)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_sqlite_get_connection(false)), NULL);
// preparing
    const char *l_error_msg = "conditional read";
    dap_store_obj_t *l_ret = NULL;
    sqlite3_stmt *l_stmt_count = NULL, *l_stmt = NULL;
    char *l_table_name = dap_str_replace_char(a_group, '.', '_');
    char *l_str_query_count = sqlite3_mprintf("SELECT COUNT(*) FROM '%s' "
                                        " WHERE driver_key > ? AND (flags & '%d' %s 0)",
                                        l_table_name, DAP_GLOBAL_DB_RECORD_DEL,
                                        a_with_holes ? ">=" : "=");
    char *l_str_query = sqlite3_mprintf("SELECT * FROM '%s'"
                                        " WHERE driver_key > ? AND (flags & '%d' %s 0)"
                                        " ORDER BY driver_key LIMIT '%d'",
                                        l_table_name, DAP_GLOBAL_DB_RECORD_DEL,
                                        a_with_holes ? ">=" : "=",
                                        a_count_out && *a_count_out ? (int)*a_count_out : (int)DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT);
    DAP_DELETE(l_table_name);
    if (!l_str_query_count || !l_str_query) {
        log_it(L_ERROR, "Error in SQL request forming");
        goto clean_and_ret;
    }
    
    if( s_db_sqlite_prepare(l_conn->conn, l_str_query_count, &l_stmt_count, l_error_msg)                        != SQLITE_OK ||
        s_db_sqlite_bind_blob64(l_stmt_count, 1, &a_hash_from, sizeof(a_hash_from), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
        s_db_sqlite_prepare(l_conn->conn, l_str_query, &l_stmt, l_error_msg)                                    != SQLITE_OK ||
        s_db_sqlite_bind_blob64(l_stmt, 1, &a_hash_from, sizeof(a_hash_from), SQLITE_STATIC, l_error_msg)       != SQLITE_OK ||
        s_db_sqlite_step(l_stmt_count, l_error_msg)                                                             != SQLITE_ROW )
    {
        goto clean_and_ret;
    }
// memory alloc
    uint64_t l_count = sqlite3_column_int64(l_stmt_count, 0);
    l_count = a_count_out && *a_count_out ? dap_min(l_count, *a_count_out) : dap_min(l_count, DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT);
    if (!l_count) {
        log_it(L_INFO, "There are no records satisfying the conditional read request");
        goto clean_and_ret;
    }
    if (!( l_ret = DAP_NEW_Z_COUNT(dap_store_obj_t, l_count + 1) )) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        goto clean_and_ret;
    }
// data forming
    size_t l_count_out = 0;
    int rc;
    for ( rc = 0; SQLITE_ROW == ( rc = s_db_sqlite_fill_one_item(a_group, l_ret + l_count_out, l_stmt) ) && l_count_out < l_count; ++l_count_out ) {};
    if ( rc != SQLITE_DONE )
        log_it(L_ERROR, "SQLite conditional read error %d(%s)", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
    if (a_count_out)
        *a_count_out = l_count_out;
clean_and_ret:
    s_db_sqlite_clean(l_conn, 2, l_str_query, l_str_query_count, l_stmt, l_stmt_count);
    return l_ret;
}

/**
 * @brief Reads some objects from a SQLite database by a_group, a_key.
 * @param a_group a group name string
 * @param a_key an object key string, if equals NULL reads the whole group
 * @param a_count_out[out] a number of objects that were read
 * @param a_with_holes if true - read any records, if false - only actual records
 * @return If successful, a pointer to an objects, otherwise NULL.
 */
static dap_store_obj_t* s_db_sqlite_read_store_obj(const char *a_group, const char *a_key, size_t *a_count_out, bool a_with_holes)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_sqlite_get_connection(false)), NULL);
// func work
    const char *l_error_msg = "read";
    dap_store_obj_t *l_ret = NULL;
    sqlite3_stmt *l_stmt_count = NULL, *l_stmt = NULL;
    char *l_table_name = dap_str_replace_char(a_group, '.', '_');
    char *l_str_query_count = NULL;
    char *l_str_query = NULL;
    if (a_key) {
        l_str_query_count = sqlite3_mprintf("SELECT COUNT(*) FROM '%s' WHERE key='%s' AND (flags & '%d' %s 0)", l_table_name, a_key, DAP_GLOBAL_DB_RECORD_DEL, a_with_holes ? ">=" : "=");
        l_str_query = sqlite3_mprintf("SELECT * FROM '%s' WHERE key='%s' AND (flags & '%d' %s 0)", l_table_name, a_key, DAP_GLOBAL_DB_RECORD_DEL, a_with_holes ? ">=" : "=");
    } else { // no limit
        l_str_query_count = sqlite3_mprintf("SELECT COUNT(*) FROM '%s' WHERE flags & '%d' %s 0 ORDER BY driver_key", l_table_name, DAP_GLOBAL_DB_RECORD_DEL, a_with_holes ? ">=" : "=");
        l_str_query = sqlite3_mprintf("SELECT * FROM '%s' WHERE flags & '%d' %s 0 ORDER BY driver_key LIMIT '%d'",
                                        l_table_name, DAP_GLOBAL_DB_RECORD_DEL,
                                        a_with_holes ? ">=" : "=",
                                        a_count_out && *a_count_out ? (int)*a_count_out : -1);
    }
    DAP_DEL_Z(l_table_name);
    if (!l_str_query_count || !l_str_query) {
        log_it(L_ERROR, "Error in SQL request forming");
        goto clean_and_ret;
    }
    
    if(s_db_sqlite_prepare(l_conn->conn, l_str_query_count, &l_stmt_count, l_error_msg)!= SQLITE_OK ||
        s_db_sqlite_prepare(l_conn->conn, l_str_query, &l_stmt, l_error_msg)!= SQLITE_OK ||
        s_db_sqlite_step(l_stmt_count, l_error_msg) != SQLITE_ROW)
    {
        goto clean_and_ret;
    }
// memory alloc
    uint64_t l_count = sqlite3_column_int64(l_stmt_count, 0);
    l_count = a_count_out && *a_count_out ? dap_min(*a_count_out, l_count) : l_count;
    if (!l_count) {
        log_it(L_INFO, "There are no records satisfying the read request");
        goto clean_and_ret;
    }
    if (!( l_ret = DAP_NEW_Z_COUNT(dap_store_obj_t, l_count) )) {
        log_it(L_CRITICAL, "Memory allocation error");
        goto clean_and_ret;
    }
    
// data forming
    size_t l_count_out = 0;
    int rc;
    for ( rc = 0; SQLITE_ROW == ( rc = s_db_sqlite_fill_one_item(a_group, l_ret + l_count_out, l_stmt) ) && l_count_out < l_count; ++l_count_out ) {};
    if ( rc != SQLITE_DONE )
        log_it(L_ERROR, "SQLite read error %d(%s)", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
    if (a_count_out)
        *a_count_out = l_count_out;
clean_and_ret:
    s_db_sqlite_clean(l_conn, 2, l_str_query, l_str_query_count, l_stmt, l_stmt_count);
    return l_ret;
}

static dap_store_obj_t* s_db_sqlite_read_store_obj_below_timestamp(const char *a_group, dap_nanotime_t a_timestamp, size_t *a_count_out) {
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_fail(a_group && (l_conn = s_db_sqlite_get_connection(false)), NULL);

    const char *l_error_msg = "read below timestamp";
    char *l_str_query = NULL;
    size_t l_row = 0;
    dap_store_obj_t * l_ret = NULL;

    sqlite3_stmt *l_stmt_count = NULL, *l_stmt = NULL;
    char *l_table_name = dap_str_replace_char(a_group, '.', '_');
    char *l_query_count_str = sqlite3_mprintf("SELECT COUNT(*) FROM '%s'"
                                        " WHERE driver_key < ?", l_table_name);
    char *l_query_str = sqlite3_mprintf("SELECT * FROM '%s'"
                                        " WHERE driver_key < ?"
                                        " ORDER BY driver_key DESC LIMIT '%d'", l_table_name, (int)DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT);
    DAP_DELETE(l_table_name);
    if (!l_query_count_str || !l_query_str) {
        log_it(L_ERROR, "Error in SQL request forming");
        goto clean_and_ret;
    }

    dap_global_db_driver_hash_t l_hash_from = { .bets = htobe64(a_timestamp), .becrc = (uint64_t)-1 };
    if( s_db_sqlite_prepare(l_conn->conn, l_query_count_str, &l_stmt_count, l_error_msg)                        != SQLITE_OK ||
        s_db_sqlite_bind_blob64(l_stmt_count, 1, &l_hash_from, sizeof(l_hash_from), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
        s_db_sqlite_prepare(l_conn->conn, l_query_str, &l_stmt, l_error_msg)                                    != SQLITE_OK ||
        s_db_sqlite_bind_blob64(l_stmt, 1, &l_hash_from, sizeof(l_hash_from), SQLITE_STATIC, l_error_msg)       != SQLITE_OK ||
        s_db_sqlite_step(l_stmt_count, l_error_msg)                                                             != SQLITE_ROW )
    {
        goto clean_and_ret;
    }
// memory alloc
    uint64_t l_count = sqlite3_column_int64(l_stmt_count, 0);
    l_count = a_count_out && *a_count_out ? dap_min(l_count, *a_count_out) : dap_min(l_count, DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT);
    if (!l_count) {
        log_it(L_INFO, "There are no records satisfying the conditional read request");
        goto clean_and_ret;
    }
    if (!( l_ret = DAP_NEW_Z_COUNT(dap_store_obj_t, l_count) )) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        goto clean_and_ret;
    }
// data forming
    size_t l_count_out = 0;
    int rc;
    for ( rc = 0; SQLITE_ROW == ( rc = s_db_sqlite_fill_one_item(a_group, l_ret + l_count_out, l_stmt) ) && l_count_out < l_count; ++l_count_out ) {};
    if ( rc != SQLITE_DONE )
        log_it(L_ERROR, "SQLite conditional read error %d(%s)", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
    if (a_count_out)
        *a_count_out = l_count_out;
clean_and_ret:
    s_db_sqlite_clean(l_conn, 2, l_query_str, l_query_count_str, l_stmt, l_stmt_count);
    return l_ret;
}



/**
 * @brief Gets a list of group names by a_group_mask.
 * @param a_group_mask a group name mask
 * @return If successful, a pointer to a list of group names, otherwise NULL.
 */
static dap_list_t *s_db_sqlite_get_groups_by_mask(const char *a_group_mask)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group_mask || !(l_conn = s_db_sqlite_get_connection(false)), NULL);
// preparing
    const char *l_error_msg = "get groups";
    dap_list_t* l_ret = NULL;
    sqlite3_stmt *l_stmt = NULL;
    char *l_mask = NULL;
    char *l_str_query = sqlite3_mprintf("SELECT name FROM sqlite_master WHERE type ='table' AND name NOT LIKE 'sqlite_%c'", '%');
    if (!l_str_query) {
        log_it(L_ERROR, "Error in SQL request forming");
        goto clean_and_ret;
    }
    
    if(s_db_sqlite_prepare(l_conn->conn, l_str_query, &l_stmt, l_error_msg)!= SQLITE_OK) {
        goto clean_and_ret;
    }
    l_mask = dap_str_replace_char(a_group_mask, '.', '_');
    int rc = 0;
    while ( SQLITE_ROW == ( rc = s_db_sqlite_step(l_stmt, l_error_msg) ) && sqlite3_column_type(l_stmt, 0) == SQLITE_TEXT ) {
        const char *l_table_name = (const char*)sqlite3_column_text(l_stmt, 0);
        if (dap_global_db_group_match_mask(l_table_name, l_mask))
            l_ret = dap_list_prepend(l_ret, dap_str_replace_char(l_table_name, '_', '.'));
    }
    if ( rc != SQLITE_DONE )
        log_it(L_ERROR, "SQLite read error %d(%s)", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
clean_and_ret:
    s_db_sqlite_clean(l_conn, 1, l_str_query, l_stmt);
    DAP_DEL_Z(l_mask);
    return l_ret;
}

/**
 * @brief Reads a number of objects from a s_db database by a hash
 * @param a_group a group name string
 * @param a_hash_from startin hash (not include to result)
 * @param a_with_holes if true - read any records, if false - only actual records
 * @return Returns a number of objects.
 */
static size_t s_db_sqlite_read_count_store(const char *a_group, dap_global_db_driver_hash_t a_hash_from, bool a_with_holes)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_sqlite_get_connection(false)), 0);
// preparing
    const char *l_error_msg = "count read";
    size_t l_ret = 0;
    sqlite3_stmt *l_stmt_count = NULL;
    char *l_table_name = dap_str_replace_char(a_group, '.', '_');
    char *l_str_query_count = sqlite3_mprintf("SELECT COUNT(*) FROM '%s' "
                                        " WHERE driver_key > ? AND (flags & '%d' %s 0)",
                                        l_table_name, DAP_GLOBAL_DB_RECORD_DEL,
                                        a_with_holes ? ">=" : "=");
    DAP_DEL_Z(l_table_name);
    if (!l_str_query_count) {
        log_it(L_ERROR, "Error in SQL request forming");
        goto clean_and_ret;
    }
    
    if(s_db_sqlite_prepare(l_conn->conn, l_str_query_count, &l_stmt_count, l_error_msg)!= SQLITE_OK ||
        s_db_sqlite_bind_blob64(l_stmt_count, 1, &a_hash_from, sizeof(a_hash_from), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
        s_db_sqlite_step(l_stmt_count, l_error_msg) != SQLITE_ROW)
    {
        goto clean_and_ret;
    }
    l_ret = sqlite3_column_int64(l_stmt_count, 0);
clean_and_ret:
    s_db_sqlite_clean(l_conn, 1, l_str_query_count, l_stmt_count);
    return l_ret;
}

/**
 * @brief Checks if an object is in a database by hash.
 * @param a_group a group name string
 * @param a_hash a object hash
 * @return Returns true if it is, false it's not.
 */
static bool s_db_sqlite_is_hash(const char *a_group, dap_global_db_driver_hash_t a_hash)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_sqlite_get_connection(false)), false);
// preparing
    const char *l_error_msg = "is hash read";
    bool l_ret = false;
    sqlite3_stmt *l_stmt_count = NULL;
    char *l_table_name = dap_str_replace_char(a_group, '.', '_');
    char *l_str_query_count = sqlite3_mprintf("SELECT COUNT(*) FROM '%s' "
                                        " WHERE driver_key = ?",
                                        l_table_name);
    DAP_DEL_Z(l_table_name);
    if (!l_str_query_count) {
        log_it(L_ERROR, "Error in SQL request forming");
        goto clean_and_ret;
    }
    
    if(s_db_sqlite_prepare(l_conn->conn, l_str_query_count, &l_stmt_count, l_error_msg)!= SQLITE_OK ||
        s_db_sqlite_bind_blob64(l_stmt_count, 1, &a_hash, sizeof(a_hash), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
        s_db_sqlite_step(l_stmt_count, l_error_msg) != SQLITE_ROW)
    {
        goto clean_and_ret;
    }
    l_ret = (bool)sqlite3_column_int64(l_stmt_count, 0);
clean_and_ret:
    s_db_sqlite_clean(l_conn, 1, l_str_query_count, l_stmt_count);
    return l_ret;
}

/**
 * @brief Checks if an object is in a database by a_group and a_key.
 * @param a_group a group name string
 * @param a_key a object key string
 * @return Returns true if it is, false it's not.
 */
static bool s_db_sqlite_is_obj(const char *a_group, const char *a_key)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_sqlite_get_connection(false)), false);
// preparing
    const char *l_error_msg = "is obj read";
    bool l_ret = false;
    sqlite3_stmt *l_stmt_count = NULL;
    char *l_table_name = dap_str_replace_char(a_group, '.', '_');
    char *l_str_query_count = sqlite3_mprintf("SELECT COUNT(*) FROM '%s' "
                                        " WHERE key = '%s'",
                                        l_table_name, a_key);
    DAP_DEL_Z(l_table_name);
    if (!l_str_query_count) {
        log_it(L_ERROR, "Error in SQL request forming");
        goto clean_and_ret;
    }
    
    if(s_db_sqlite_prepare(l_conn->conn, l_str_query_count, &l_stmt_count, l_error_msg)!= SQLITE_OK ||
        s_db_sqlite_step(l_stmt_count, l_error_msg) != SQLITE_ROW)
    {
        goto clean_and_ret;
    }
    l_ret = (bool)sqlite3_column_int64(l_stmt_count, 0);
clean_and_ret:
    s_db_sqlite_clean(l_conn, 1, l_str_query_count, l_stmt_count);
    return l_ret;
}

/**
 * @brief Flushes a SQLite database cahce to disk
 * @note The function closes and opens the database connection
 * @return result code.
 */
static int s_db_sqlite_flush()
{
// sanity check
    conn_list_item_t *l_conn = s_db_sqlite_get_connection(false);
    dap_return_val_if_pass(!l_conn, -1);
// preparing
    char *l_error_message = NULL;
    log_it(L_DEBUG, "Start flush sqlite data base.");
    sqlite3_close(l_conn->conn);
    if ( !(l_conn->conn = s_db_sqlite_open(s_filename_db, SQLITE_OPEN_READWRITE, &l_error_message)) ) {
        log_it(L_ERROR, "Can't init sqlite err: \"%s\"", l_error_message ? l_error_message: "UNKNOWN");
        sqlite3_free(l_error_message);
        return -2;
    }

#ifndef _WIN32
    sync();
#endif
    s_db_sqlite_free_connection(l_conn, false);
    s_db_sqlite_free_connection(l_conn, true);
    return 0;
}

/**
 * @brief Starts a outstanding transaction in database.
 * @return result code.
 */
static int s_db_sqlite_transaction_start()
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!(l_conn = s_db_sqlite_get_connection(true)), 0);
// func work
    if ( g_dap_global_db_debug_more )
        log_it(L_DEBUG, "Start TX: @%p", l_conn->conn);
    
    int l_ret = 0;
    s_db_sqlite_exec(l_conn->conn, "BEGIN", NULL, NULL, 0, NULL);
    if ( l_ret != SQLITE_OK ) {
        s_db_sqlite_free_connection(l_conn, true);
    }
    return  l_ret;
}

/**
 * @brief Ends a outstanding transaction in database.
 * @return result code.
 */
static int s_db_sqlite_transaction_end(bool a_commit)
{
// sanity check
    dap_return_val_if_pass_err(!s_conn || !s_conn->conn, -1, "Outstanding connection not exist");
// func work
    if ( g_dap_global_db_debug_more )
        log_it(L_DEBUG, "End TX l_conn: @%p", s_conn->conn);
    int l_ret = 0;
    if (a_commit)
        l_ret = s_db_sqlite_exec(s_conn->conn, "COMMIT", NULL, NULL, 0, NULL);
    else
        l_ret = s_db_sqlite_exec(s_conn->conn, "ROLLBACK", NULL, NULL, 0, NULL);
    if ( l_ret == SQLITE_OK ) {
        s_db_sqlite_free_connection(s_conn, true);
    }
    return  l_ret;
}

void dap_global_db_driver_sqlite_set_attempts_count(uint32_t a_attempts, bool a_force)
{
    s_attempts_count = a_force ? a_attempts : dap_max(s_attempts_count, a_attempts);
}

/**
 * @brief Initializes a SQLite database.
 * @note no thread safe
 * @param a_filename_db a path to the database file
 * @param a_drv_callback a pointer to a structure of callback functions
 * @return If successful returns 0, else a code < 0.
 */
int dap_global_db_driver_sqlite_init(const char *a_filename_db, dap_global_db_driver_callbacks_t *a_drv_callback)
{
// sanity check
    dap_return_val_if_pass(!a_filename_db, -1);
    dap_return_val_if_pass_err(s_db_inited, -2, "SQLite driver already init")
// func work
    int l_ret = -1;
    char l_errbuf[255] = "", *l_error_message = NULL;
    if ( sqlite3_threadsafe() && !sqlite3_config(SQLITE_CONFIG_MULTITHREAD) )
        l_ret = sqlite3_initialize();

    if ( l_ret != SQLITE_OK ) {
        log_it(L_ERROR, "Can't init sqlite err=%d (%s)", l_ret, sqlite3_errstr(l_ret));
        return -3;
    }

    // Check paths and create them if nessesary
    char * l_filename_dir = dap_path_get_dirname(a_filename_db);
    strncpy(s_filename_db, a_filename_db, sizeof(s_filename_db) - 1);

    if ( !dap_dir_test(l_filename_dir) ){
        log_it(L_NOTICE, "No directory %s, trying to create...",l_filename_dir);

        int l_mkdir_ret = dap_mkdir_with_parents(l_filename_dir);

        if(!dap_dir_test(l_filename_dir)){
            log_it(L_ERROR, "Can't create directory, error code %d, error %d: \"%s\"",
                            l_mkdir_ret, errno, dap_strerror(errno));
            DAP_DELETE(l_filename_dir);
            return -errno;
        }else
            log_it(L_NOTICE, "Directory created");
    }
    DAP_DEL_Z(l_filename_dir);

    a_drv_callback->apply_store_obj              = s_db_sqlite_apply_store_obj;
    a_drv_callback->read_store_obj               = s_db_sqlite_read_store_obj;
    a_drv_callback->read_cond_store_obj          = s_db_sqlite_read_cond_store_obj;
    a_drv_callback->read_store_obj_by_timestamp  = s_db_sqlite_read_store_obj_below_timestamp;
    a_drv_callback->read_last_store_obj          = s_db_sqlite_read_last_store_obj;
    a_drv_callback->transaction_start            = s_db_sqlite_transaction_start;
    a_drv_callback->transaction_end              = s_db_sqlite_transaction_end;
    a_drv_callback->get_groups_by_mask           = s_db_sqlite_get_groups_by_mask;
    a_drv_callback->read_count_store             = s_db_sqlite_read_count_store;
    a_drv_callback->is_obj                       = s_db_sqlite_is_obj;
    a_drv_callback->deinit                       = s_db_sqlite_deinit;
    a_drv_callback->flush                        = s_db_sqlite_flush;
    a_drv_callback->get_by_hash                  = s_db_sqlite_get_by_hash;
    a_drv_callback->read_hashes                  = s_db_sqlite_read_hashes;
    a_drv_callback->is_hash                      = s_db_sqlite_is_hash;
    s_db_inited = true;

    conn_list_item_t *l_conn = s_db_sqlite_get_connection(false);
    if (!l_conn) {
        log_it(L_ERROR, "Can't create base connection\n");
        s_db_inited = false;
        return -3;
    }

    dap_global_db_driver_sqlite_set_attempts_count(dap_proc_thread_get_count(), false);
    s_db_sqlite_free_connection(l_conn, false);
    return l_ret;
}
