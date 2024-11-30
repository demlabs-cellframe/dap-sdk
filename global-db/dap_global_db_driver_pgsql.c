/*
* Authors:
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

#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "dap_global_db_driver_pgsql.h"
#include "/usr/include/postgresql/libpq-fe.h"
#include "dap_common.h"
#include "dap_hash.h"
#include "dap_file_utils.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"

#include "dap_enc_base64.h"

#include <pwd.h>

#define LOG_TAG "db_pgsql"
#define DAP_GLOBAL_DB_TYPE_CURRENT DAP_GLOBAL_DB_TYPE_PGSQL

static char s_db_name[DAP_PGSQL_DBHASHNAME_LEN + 1];

typedef struct conn_pool_item {
    PGconn *conn;                                               /* SQLITE connection context itself */
    int idx;                                                    /* Just index, no more */
    atomic_flag busy_conn;                                      /* Connection busy flag */
    atomic_flag busy_trans;                                     /* Outstanding transaction busy flag */
    atomic_ullong  usage;                                       /* Usage counter */
} conn_list_item_t;

extern int g_dap_global_db_debug_more;                         /* Enable extensible debug output */

// static uint32_t s_attempts_count = 10;
// static const int s_sleep_period = 500 * 1000;  /* Wait 0.5 sec */;
static bool s_db_inited = false;
static _Thread_local conn_list_item_t *s_conn = NULL;  // local connection


static void s_connection_destructor(UNUSED_ARG void *a_conn) {
    PQfinish(s_conn->conn);
    log_it(L_DEBUG, "Close  connection: @%p/%p, usage: %llu", s_conn, s_conn->conn, s_conn->usage);
    DAP_DEL_Z(s_conn);
}


// /**
//  * @brief Opens a SQLite database and adds byte_to_bin function.
//  * @param a_filename_utf8 a SQLite database file name
//  * @param a_flags database access flags (SQLITE_OPEN_READONLY, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)
//  * @param a_error_message[out] an error message that's received from the SQLite database
//  * @return Returns a pointer to an instance of SQLite database structure.
//  */
// sqlite3* s_db_sqlite_open(const char *a_filename_utf8, int a_flags, char **a_error_message)
// {
//     sqlite3 *l_db = NULL;

//     int l_rc = sqlite3_open_v2(a_filename_utf8, &l_db, a_flags, NULL); // SQLITE_OPEN_FULLMUTEX by default set with sqlite3_config SERIALIZED
//     // if unable to open the database file
//     if(l_rc == SQLITE_CANTOPEN) {
//         log_it(L_DEBUG,"No database on path %s, creating one from scratch", a_filename_utf8);
//         if(l_db)
//             sqlite3_close(l_db);
//         // try to create database
//         l_rc = sqlite3_open_v2(a_filename_utf8, &l_db, a_flags | SQLITE_OPEN_CREATE, NULL);
//     }

//     if(l_rc != SQLITE_OK) {
//         log_it(L_CRITICAL,"Can't open database on path %s (code %d: \"%s\" )", a_filename_utf8, l_rc, sqlite3_errstr(l_rc));
//         if(a_error_message)
//             *a_error_message = sqlite3_mprintf("Can't open database: %s\n", sqlite3_errmsg(l_db));
//         sqlite3_close(l_db);
//         return NULL;
//     }
//     return l_db;
// }

/**
 * @brief Free connections busy flags.
 * @param a_conn a connection item
 * @param a_trans if false clear connection flag, if true - outstanding transaction
 */
static inline void s_db_pgsql_free_connection(conn_list_item_t *a_conn, bool a_trans)
{
    if (g_dap_global_db_debug_more)
        log_it(L_DEBUG, "Free  l_conn: @%p/%p, usage: %llu", a_conn, a_conn->conn, a_conn->usage);
    if (a_trans)
        atomic_flag_clear(&a_conn->busy_trans);  
    else
        atomic_flag_clear(&a_conn->busy_conn);
}

// /**
//  * @brief Free connection, dynamic num sql items and finalize sqlite3_stmts.
//  * @param a_conn connection item to free
//  * @param a_count num of pairs sql item + sqlite3_stmts
//  */
// static void s_db_sqlite_clean(conn_list_item_t *a_conn, size_t a_count, ... ) {
//     va_list l_args_list;
//     va_start(l_args_list, a_count);
//     for (size_t i = 0; i < a_count; ++i)
//         sqlite3_free(va_arg(l_args_list, void*));
//     for (size_t i = 0; i < a_count; ++i)
//         sqlite3_finalize(va_arg(l_args_list, void*));
//     va_end(l_args_list);
//     s_db_pgsql_free_connection(a_conn, false);
// }

// /**
//  * @brief One step to sqlite3_stmt with 7 try is sql bust
//  * @param a_stmt sqlite3_stmt to step
//  * @param a_error_msg module name
//  * @return result code
//  */
// static int s_db_sqlite_step(sqlite3_stmt *a_stmt, const char *a_error_msg)
// {
//     dap_return_val_if_pass(!a_stmt, SQLITE_ERROR);
//     int l_ret = 0;
//     for ( char i = s_attempts_count; i--; ) {
//         l_ret = sqlite3_step(a_stmt);
//         if (l_ret != SQLITE_BUSY && l_ret != SQLITE_LOCKED)
//             break;
//         dap_usleep(s_sleep_period);
//     }
//     debug_if(l_ret != SQLITE_ROW && l_ret != SQLITE_DONE, L_DEBUG, "SQLite step in %s error %d(%s)", a_error_msg ? a_error_msg : "", l_ret, sqlite3_errstr(l_ret));
//     return l_ret;
// }

// /**
//  * @brief One step to sqlite3_stmt with 7 try is sql bust
//  * @param a_db a pointer to an instance of SQLite connection
//  * @param a_str_query SQL query string
//  * @param a_stmt pointer to generate sqlite3_stmt
//  * @param a_error_msg module name
//  * @return result code
//  */
// static int s_db_sqlite_prepare(sqlite3 *a_db, const char *a_str_query, sqlite3_stmt **a_stmt, const char *a_error_msg)
// {
//     dap_return_val_if_pass(!a_stmt || !a_str_query || !a_stmt, SQLITE_ERROR);
//     int l_ret = 0;
//     for (char i = s_attempts_count; i--; ) {
//         l_ret = sqlite3_prepare_v2(a_db, a_str_query, -1, a_stmt, NULL);
//         if (l_ret != SQLITE_BUSY && l_ret != SQLITE_LOCKED)
//             break;
//         dap_usleep(s_sleep_period);
//     }
//     debug_if(l_ret != SQLITE_OK, L_DEBUG, "SQLite prepare %s error %d(%s)", a_error_msg ? a_error_msg : "", sqlite3_errcode(a_db), sqlite3_errmsg(a_db));
//     return l_ret;
// }

// /**
//  * @brief One step to sqlite3_stmt with 7 try is sql bust
//  * @param a_stmt sqlite3_stmt to step
//  * @param a_pos blob element position in query
//  * @param a_data blob data
//  * @param a_data_size blob data size
//  * @param a_destructor SQL destructor type
//  * @param a_error_msg module name
//  * @return result code
//  */
// static int s_db_sqlite_bind_blob64(sqlite3_stmt *a_stmt, int a_pos, const void *a_data, sqlite3_uint64 a_data_size, sqlite3_destructor_type a_destructor, const char *a_error_msg)
// {
//     dap_return_val_if_pass(!a_stmt || !a_data || !a_data_size || a_pos < 0, SQLITE_ERROR);
//     int l_ret = 0;
//     for ( char i = s_attempts_count; i--; ) {
//         l_ret = sqlite3_bind_blob64(a_stmt, a_pos, a_data, a_data_size, a_destructor);
//         if (l_ret != SQLITE_BUSY && l_ret != SQLITE_LOCKED)
//             break;
//         dap_usleep(s_sleep_period);
//     }
//     debug_if(l_ret != SQLITE_OK, L_DEBUG, "SQLite bind blob64 %s error %d(%s)", a_error_msg ? a_error_msg : "", l_ret, sqlite3_errstr(l_ret));
//     return l_ret;
// }

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
static PGresult *s_db_pgsql_exec(PGconn *a_db, const char *a_query, dap_global_db_driver_hash_t *a_hash, byte_t *a_value, size_t a_value_len, dap_sign_t *a_sign, ExecStatusType a_valid_result)
{
    dap_return_val_if_pass(!a_db || !a_query, NULL);
    
    const char *l_param_vals[3] = {(const char *)a_hash, (const char *)a_value, (const char *)a_sign};
    int l_param_lens[3] = {sizeof(dap_global_db_driver_hash_t), a_value_len, dap_sign_get_size(a_sign)};
    int l_param_formats[3] = {1, 1, 1};
    uint8_t l_param_count = 3 - !a_hash - !a_value - !a_sign;
    PGresult *l_ret = PQexecParams(a_db, a_query, l_param_count, NULL, l_param_vals, l_param_lens, l_param_formats, 1);
    if ( PQresultStatus(l_ret) != a_valid_result ) {
        const char *l_err = PQresultErrorField(l_ret, PG_DIAG_SQLSTATE);
        if (!l_err || strcmp(l_err, PGSQL_INVALID_TABLE))
            log_it(L_ERROR, "Query failed with message: \"%s\"", PQresultErrorMessage(l_ret));
        PQclear(l_ret);
        return NULL;
    }
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
DAP_STATIC_INLINE int s_db_pgsql_exec_command(PGconn *a_db, const char *a_query, dap_global_db_driver_hash_t *a_hash, byte_t *a_value, size_t a_value_len, dap_sign_t *a_sign)
{
    PGresult *l_res = s_db_pgsql_exec(a_db, a_query, a_hash, a_value, a_value_len, a_sign, PGRES_COMMAND_OK);
    if (l_res) {
        PQclear(l_res);
        return 0;
    }
    return -1;
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
DAP_STATIC_INLINE PGresult *s_db_pgsql_exec_tuples(PGconn *a_db, const char *a_query, dap_global_db_driver_hash_t *a_hash, byte_t *a_value, size_t a_value_len, dap_sign_t *a_sign)
{
    return s_db_pgsql_exec(a_db, a_query, a_hash, a_value, a_value_len, a_sign, PGRES_TUPLES_OK);
}

/**
 * @brief Prepare connection item
 * @param a_trans outstanding transaction flag
 * @return pointer to connection item, otherwise NULL.
 */
static conn_list_item_t *s_db_pgsql_get_connection(bool a_trans)
{
// sanity check
    dap_return_val_if_pass_err(!s_db_inited, NULL, "SQLite driver not inited");
// func work
    static int l_conn_idx = 0;
    if (!s_conn) {
        s_conn = DAP_NEW_Z_RET_VAL_IF_FAIL(conn_list_item_t, NULL, NULL);
        pthread_key_t s_destructor_key;
        pthread_key_create(&s_destructor_key, s_connection_destructor);
        pthread_setspecific(s_destructor_key, (const void *)s_conn);
        char *l_conn_str = dap_strdup_printf("dbname=%s", s_db_name);
        s_conn->conn = PQconnectdb(l_conn_str);
        DAP_DELETE(l_conn_str);
        if (PQstatus(s_conn->conn) != CONNECTION_OK) {
            log_it(L_ERROR, "Can't connect PostgreSQL database: \"%s\"", PQerrorMessage(s_conn->conn));
            DAP_DEL_Z(s_conn);
            return NULL;
        }
        s_conn->idx = l_conn_idx++;
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

// /**
//  * @brief Deinitializes a SQLite database.
//  * @return result code.
//  */
// int s_db_sqlite_deinit(void)
// {
//     if (!s_db_inited) {
//         log_it(L_WARNING, "SQLite driver already deinited");
//         return -1;
//     }
//     s_connection_destructor(NULL);
//     s_db_inited = false;
//     return sqlite3_shutdown();
// }

/**
 * @brief Creates a table and unique index in the s_db database.
 * @param a_table_name a table name string
 * @param a_conn connection item to use query
 * @return result code
 */
static int s_db_pgsql_create_group_table(const char *a_table_name, conn_list_item_t *a_conn)
{
// sanity check
    dap_return_val_if_pass(!a_table_name || !a_conn, -EINVAL);
    char l_query[512];

    snprintf(l_query, sizeof(l_query) - 1,
                    "CREATE TABLE IF NOT EXISTS \"%s\"(driver_key BYTEA UNIQUE NOT NULL PRIMARY KEY, key TEXT UNIQUE NOT NULL, flags INTEGER, value BYTEA, sign BYTEA)",
                    a_table_name);
    return s_db_pgsql_exec_command(a_conn->conn, l_query, NULL, NULL, 0, NULL);
}

/**
 * @brief Applies an object to a database.
 * @param a_store_obj a pointer to the object structure
 * @return result code.
 */
static int s_db_pgsql_apply_store_obj(dap_store_obj_t *a_store_obj)
{
// sanity check
    dap_return_val_if_pass(!a_store_obj || !a_store_obj->group || (!a_store_obj->crc && a_store_obj->key), -EINVAL);
    uint8_t l_type_erase = a_store_obj->flags & DAP_GLOBAL_DB_RECORD_ERASE;
    dap_return_val_if_pass(!a_store_obj->key && !l_type_erase, -EINVAL);
// func work
    // execute request
    conn_list_item_t *l_conn = s_db_pgsql_get_connection(false);
    if (!l_conn)
        return -2;
    int l_ret = 0;
    char *l_query = NULL;
    if (!l_type_erase) {
        if (!a_store_obj->key) {
            log_it(L_ERROR, "Global DB store object unsigned");
            l_ret = -3;
            goto clean_and_ret;
        } else { //add one record
            l_query = dap_strdup_printf("INSERT INTO \"%s\" VALUES($1, '%s', '%d', $2, $3) "
            "ON CONFLICT(key) DO UPDATE SET driver_key = EXCLUDED.driver_key, flags = EXCLUDED.flags, value = EXCLUDED.value, sign = EXCLUDED.sign;",
                                                  a_store_obj->group, a_store_obj->key, (int)(a_store_obj->flags & ~DAP_GLOBAL_DB_RECORD_NEW));
        }
        dap_global_db_driver_hash_t l_driver_key = dap_global_db_driver_hash_get(a_store_obj);
        l_ret = s_db_pgsql_exec_command(l_conn->conn, l_query, &l_driver_key, a_store_obj->value, a_store_obj->value_len, a_store_obj->sign);
        if (l_ret) {
            // create table and repeat request
            if (!s_db_pgsql_create_group_table(a_store_obj->group, l_conn))
                l_ret = s_db_pgsql_exec_command(l_conn->conn, l_query, &l_driver_key, a_store_obj->value, a_store_obj->value_len, a_store_obj->sign);
        }
    } else {
        if (a_store_obj->key) //delete one record
            l_query = dap_strdup_printf("DELETE FROM \"%s\" WHERE key = '%s'", a_store_obj->group, a_store_obj->key);
        else // remove all group
            l_query = dap_strdup_printf("DROP TABLE IF EXISTS \"%s\"", a_store_obj->group);
        l_ret = s_db_pgsql_exec_command(l_conn->conn, l_query, NULL, NULL, 0, NULL);
    }
clean_and_ret:
    s_db_pgsql_free_connection(l_conn, false);
    DAP_DELETE(l_query);
    return l_ret;
}

/**
 * @brief Fills a object from a row
 * @param a_group a group name string
 * @param a_obj a pointer to the object
 * @param a_stmt a ponter to the sqlite3_stmt
 * @return result code
 */
static int s_db_pgsql_fill_one_item(const char *a_group, dap_store_obj_t *a_obj, PGresult *a_query_res, int a_row)
{
// sanity check
    dap_return_val_if_pass(!a_group || !a_obj || !a_query_res, -1);
    a_obj->group = dap_strdup(a_group);
// preparing
    int l_count_col = PQnfields(a_query_res);
    int l_count_col_out = 0;
    const char *l_fields_name[] = {"driver_key", "key", "flags", "value", "sign"};
    for (size_t i = 0; i < sizeof(l_fields_name) / sizeof (const char *); ++i) {
        dap_global_db_driver_hash_t *l_driver_key = NULL;
        int l_col_num = PQfnumber(a_query_res, l_fields_name[i]);
        int size = 0;
        size_t l_decode_len = 0;
        switch (l_col_num) {
            case 0:
                l_driver_key = (dap_global_db_driver_hash_t *)PQgetvalue(a_query_res, a_row, l_col_num);
                a_obj->timestamp = be64toh(l_driver_key->bets);
                a_obj->crc = be64toh(l_driver_key->becrc);
                ++l_count_col_out;
                break;
            case 1:
                a_obj->key = dap_strdup((const char*)PQgetvalue(a_query_res, a_row, l_col_num));
                ++l_count_col_out;
                break;
            case 2:
                a_obj->flags = *((int *)PQgetvalue(a_query_res, a_row, l_col_num));
                ++l_count_col_out;
                break;
            case 3:
                a_obj->value_len = PQgetlength(a_query_res, a_row, l_col_num);
                a_obj->value = DAP_DUP_SIZE((byte_t*)PQgetvalue(a_query_res, a_row, l_col_num), a_obj->value_len);
                ++l_count_col_out;
                break;
            case 4:
                a_obj->sign = DAP_DUP_SIZE((dap_sign_t*)PQgetvalue(a_query_res, a_row, l_col_num), PQgetlength(a_query_res, a_row, l_col_num));
                ++l_count_col_out;
                break;
            default: continue;
        }
    }
    if (l_count_col_out != l_count_col) {
        log_it(L_ERROR, "Error in PGSQL fill item - filled collumn == %d, expected %d", l_count_col_out, l_count_col);
        return -2;
    }
    return 0;
}

// /**
//  * @brief Reads a last object from the s_db database.
//  * @param a_group a group name string
//  * @param a_with_holes if true - read any records, if false - only actual records
//  * @return If successful, a pointer to the object, otherwise NULL.
//  */
// static dap_store_obj_t* s_db_sqlite_read_last_store_obj(const char *a_group, bool a_with_holes)
// {
// // sanity check
//     conn_list_item_t *l_conn = NULL;
//     dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// // preparing
//     dap_store_obj_t *l_ret = NULL;
//     sqlite3_stmt *l_stmt = NULL;
//     char *l_table_name = dap_str_replace_char(a_group, '.', '_');
//     char *l_query_str = sqlite3_mprintf("SELECT * FROM '%s'"
//                                         " WHERE flags & '%d' %s 0"
//                                         " ORDER BY driver_key DESC LIMIT 1",
//                                         l_table_name, DAP_GLOBAL_DB_RECORD_DEL,
//                                         a_with_holes ? ">=" : "=");
//     DAP_DEL_Z(l_table_name);
//     if (!l_query_str) {
//         log_it(L_ERROR, "Error in SQL request forming");
//         goto clean_and_ret;
//     }
    
//     if(s_db_sqlite_prepare(l_conn->conn, l_query_str, &l_stmt, "last read")!= SQLITE_OK) {
//         goto clean_and_ret;
//     }
// // memory alloc
//     l_ret = DAP_NEW_Z(dap_store_obj_t);
//     if (!l_ret) {
//         log_it(L_CRITICAL, "%s", c_error_memory_alloc);
//         goto clean_and_ret;
//     }
// // fill item
//     int l_ret_code = s_db_pgsql_fill_one_item(a_group, l_ret, l_stmt);
//     if(l_ret_code != SQLITE_ROW && l_ret_code != SQLITE_DONE) {
//         log_it(L_ERROR, "SQLite last read error %d(%s)", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
//         goto clean_and_ret;
//     }
//     if( l_ret_code != SQLITE_ROW) {
//         log_it(L_INFO, "There are no records satisfying the last read request");
//     }
// clean_and_ret:
//     s_db_sqlite_clean(l_conn, 1, l_query_str, l_stmt);    
//     return l_ret;
// }

// /**
//  * @brief Forming objects pack from hash list.
//  * @param a_group a group name string
//  * @param a_hashes pointer to hashes
//  * @param a_count hashes num
//  * @return If successful, a pointer to objects pack, otherwise NULL.
//  */
// static dap_global_db_pkt_pack_t *s_db_sqlite_get_by_hash(const char *a_group, dap_global_db_driver_hash_t *a_hashes, size_t a_count)
// {
// // sanity check
//     conn_list_item_t *l_conn = NULL;
//     dap_return_val_if_pass(!a_group || !a_hashes || !a_count || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// // preparing
//     const char *l_error_msg = "get by hash";
//     dap_global_db_pkt_pack_t *l_ret = NULL;
//     sqlite3_stmt *l_stmt_count = NULL, *l_stmt = NULL, *l_stmt_size = NULL;
//     char *l_blob_str = DAP_NEW_Z_SIZE(char, a_count * 2);
//     char *l_table_name = dap_str_replace_char(a_group, '.', '_');
//     if (!l_blob_str || !l_table_name) {
//         log_it(L_CRITICAL, "%s", c_error_memory_alloc);
//         DAP_DEL_MULTY(l_table_name, l_blob_str);
//         return NULL;
//     }
//     for (size_t i = 0; i < a_count * 2; i += 2) {
//         l_blob_str[i] = '?';
//     }
//     for (size_t i = 1; i + 1 < a_count * 2; i += 2) {
//         l_blob_str[i] = ',';
//     }
//     char *l_query_count_str = sqlite3_mprintf("SELECT COUNT(*) FROM '%s' "
//                                         " WHERE driver_key IN (%s)",
//                                         l_table_name, l_blob_str);
//     char *l_query_str_size = sqlite3_mprintf("SELECT SUM(LENGTH(key)) + SUM(LENGTH(value)) + SUM(LENGTH(sign)) FROM '%s' "
//                                         " WHERE driver_key IN (%s)",
//                                         l_table_name, l_blob_str);
//     char *l_query_str = sqlite3_mprintf("SELECT * FROM '%s'"
//                                         " WHERE driver_key IN (%s) ORDER BY driver_key",
//                                         l_table_name, l_blob_str);
//     if (!l_query_count_str || !l_query_str) {
//         log_it(L_ERROR, "Error in SQL request forming");
//         goto clean_and_ret;
//     }
//     if(s_db_sqlite_prepare(l_conn->conn, l_query_count_str, &l_stmt_count, l_error_msg)!= SQLITE_OK ||
//         s_db_sqlite_prepare(l_conn->conn, l_query_str_size, &l_stmt_size, l_error_msg)!= SQLITE_OK ||
//         s_db_sqlite_prepare(l_conn->conn, l_query_str, &l_stmt, l_error_msg)!= SQLITE_OK)
//     {
//         goto clean_and_ret;
//     }
//     for (size_t i = 1; i <= a_count; ++i) {
//         if( s_db_sqlite_bind_blob64(l_stmt_count, i, a_hashes + i - 1, sizeof(*a_hashes), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
//             s_db_sqlite_bind_blob64(l_stmt_size, i, a_hashes + i - 1, sizeof(*a_hashes), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
//             s_db_sqlite_bind_blob64(l_stmt, i, a_hashes + i - 1, sizeof(*a_hashes), SQLITE_STATIC, l_error_msg) != SQLITE_OK)
//         {
//             goto clean_and_ret;
//         }
//     }
//     if (s_db_sqlite_step(l_stmt_count, l_error_msg) != SQLITE_ROW || s_db_sqlite_step(l_stmt_size, l_error_msg) != SQLITE_ROW) {
//         goto clean_and_ret;
//     }
// // memory alloc
//     uint64_t l_count = sqlite3_column_int64(l_stmt_count, 0);
//     uint64_t l_size = sqlite3_column_int64(l_stmt_size, 0);
//     if (!l_count || !l_size) {
//         log_it(L_INFO, "There are no records satisfying the get by hash request");
//         goto clean_and_ret;
//     }
//     size_t l_group_name_len = strlen(a_group) + 1;
//     size_t l_data_size = l_count * (sizeof(dap_global_db_pkt_t) + l_group_name_len + 1) + l_size;
//     DAP_NEW_Z_SIZE_RET_VAL(l_ret, dap_global_db_pkt_pack_t, sizeof(dap_global_db_pkt_pack_t) + l_data_size, NULL, l_query_count_str, l_query_str);
// // data forming
//     for (size_t i = 0; i < l_count && l_ret->data_size < l_data_size && s_db_sqlite_step(l_stmt, l_error_msg) == SQLITE_ROW; ++i) {
//         dap_global_db_pkt_t *l_cur_pkt = (dap_global_db_pkt_t *)(l_ret->data + l_ret->data_size);
//         size_t l_count_col = sqlite3_column_count(l_stmt);
//         l_cur_pkt->group_len = l_group_name_len;
//         memcpy(l_cur_pkt->data, a_group, l_cur_pkt->group_len);
//         l_cur_pkt->data_len += l_cur_pkt->group_len;
//         for (size_t j = 0; j < l_count_col; ++j) {
//             if (j == 0 && sqlite3_column_type(l_stmt, j) == SQLITE_BLOB) {
//                 if (sqlite3_column_bytes(l_stmt, j)) {
//                     dap_global_db_driver_hash_t *l_driver_key = (dap_global_db_driver_hash_t *)sqlite3_column_blob(l_stmt, j);
//                     l_cur_pkt->timestamp = be64toh(l_driver_key->bets);
//                     l_cur_pkt->crc = be64toh(l_driver_key->becrc);
//                 }
//                 continue;
//             }
//             if (j == 1 && sqlite3_column_type(l_stmt, j) == SQLITE_TEXT) {
//                 l_cur_pkt->key_len = sqlite3_column_bytes(l_stmt, j);
//                 memcpy(l_cur_pkt->data + l_cur_pkt->data_len, sqlite3_column_text(l_stmt, j), l_cur_pkt->key_len);
//                 l_cur_pkt->key_len++;
//                 l_cur_pkt->data_len += l_cur_pkt->key_len;
//                 continue;
//             }
//             if (j == 2 && sqlite3_column_type(l_stmt, j) == SQLITE_INTEGER) {
//                 if (sqlite3_column_bytes(l_stmt, j))
//                     l_cur_pkt->flags = sqlite3_column_int64(l_stmt, j) & DAP_GLOBAL_DB_RECORD_DEL;
//                 continue;
//             }
//             if (j == 3 && sqlite3_column_type(l_stmt, j) == SQLITE_BLOB) {
//                 l_cur_pkt->value_len = sqlite3_column_bytes(l_stmt, j);
//                 memcpy(l_cur_pkt->data + l_cur_pkt->data_len, sqlite3_column_blob(l_stmt, j), l_cur_pkt->value_len);
//                 l_cur_pkt->data_len += l_cur_pkt->value_len;
//                 continue;
//             }
//             if (j == 4 && sqlite3_column_type(l_stmt, j) == SQLITE_BLOB) {
//                 size_t l_sign_size = sqlite3_column_bytes(l_stmt, j);
//                 if (l_sign_size) {
//                     dap_sign_t *l_sign = (dap_sign_t *)sqlite3_column_blob(l_stmt, j);
//                     if (l_sign_size != dap_sign_get_size(l_sign)) {
//                         log_it(L_ERROR, "Wrong sign size from global_db");
//                         goto clean_and_ret;
//                     }
//                     memcpy(l_cur_pkt->data + l_cur_pkt->data_len, sqlite3_column_blob(l_stmt, j), l_sign_size);
//                     l_cur_pkt->data_len += l_sign_size;
//                 }
//                 continue;
//             }
//         }
//         l_ret->data_size += sizeof(dap_global_db_pkt_t) + l_cur_pkt->data_len;
//         l_ret->obj_count++;
//     }
//     if (l_ret->data_size != l_data_size) {
//         log_it(L_ERROR, "Wrong pkt pack size %"DAP_UINT64_FORMAT_U", expected %zu", l_ret->data_size, l_data_size); 
//     }
// clean_and_ret:
//     s_db_sqlite_clean(l_conn, 3, l_query_str, l_query_count_str, l_query_str_size, l_stmt, l_stmt_count, l_stmt_size);
//     DAP_DEL_MULTY(l_table_name, l_blob_str);
//     return l_ret;
// }

// /**
//  * @brief Forming hashes pack started from concretic hash.
//  * @param a_group a group name string
//  * @param a_hash_from startin hash (not include to result)
//  * @return If successful, a pointer to hashes pack, otherwise NULL.
//  */
// static dap_global_db_hash_pkt_t *s_db_sqlite_read_hashes(const char *a_group, dap_global_db_driver_hash_t a_hash_from)
// {
// // sanity check
//     conn_list_item_t *l_conn = NULL;
//     dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// // preparing
//     const char *l_error_msg = "hashes read";
//     dap_global_db_hash_pkt_t *l_ret = NULL;
//     sqlite3_stmt *l_stmt_count = NULL, *l_stmt = NULL;
//     char *l_table_name = dap_str_replace_char(a_group, '.', '_');
//     char *l_query_count_str = sqlite3_mprintf("SELECT COUNT(*) FROM '%s' "
//                                         " WHERE driver_key > ?",
//                                         l_table_name);
//     char *l_query_str = sqlite3_mprintf("SELECT driver_key FROM '%s'"
//                                         " WHERE driver_key > ?"
//                                         " ORDER BY driver_key LIMIT '%d'",
//                                         l_table_name, (int)DAP_GLOBAL_DB_COND_READ_KEYS_DEFAULT);
//     DAP_DEL_Z(l_table_name);
//     if (!l_query_count_str || !l_query_str) {
//         log_it(L_ERROR, "Error in SQL request forming");
//         goto clean_and_ret;
//     }
    
//     if(s_db_sqlite_prepare(l_conn->conn, l_query_count_str, &l_stmt_count, l_error_msg)!= SQLITE_OK ||
//         s_db_sqlite_bind_blob64(l_stmt_count, 1, &a_hash_from, sizeof(a_hash_from), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
//         s_db_sqlite_prepare(l_conn->conn, l_query_str, &l_stmt, l_error_msg)!= SQLITE_OK ||
//         s_db_sqlite_bind_blob64(l_stmt, 1, &a_hash_from, sizeof(a_hash_from), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
//         s_db_sqlite_step(l_stmt_count, l_error_msg) != SQLITE_ROW)
//     {
//         goto clean_and_ret;
//     }
// // memory alloc
//     uint64_t l_count = sqlite3_column_int64(l_stmt_count, 0);
//     uint64_t l_blank_add = l_count;
//     l_count = dap_min(l_count, DAP_GLOBAL_DB_COND_READ_KEYS_DEFAULT);
//     if (!l_count) {
//         log_it(L_INFO, "There are no records satisfying the hashes read request");
//         goto clean_and_ret;
//     }
//     l_blank_add = l_count == l_blank_add;
//     l_count += l_blank_add;
//     size_t l_group_name_len = strlen(a_group) + 1;
//     DAP_NEW_Z_SIZE_RET_VAL(l_ret, dap_global_db_hash_pkt_t, sizeof(dap_global_db_hash_pkt_t) + l_count * sizeof(dap_global_db_driver_hash_t) + l_group_name_len, NULL, l_query_count_str, l_query_str);
// // data forming
//     size_t l_count_out = 0;
//     l_ret->group_name_len = l_group_name_len;
//     byte_t *l_curr_point = l_ret->group_n_hashses + l_ret->group_name_len;
//     memcpy(l_ret->group_n_hashses, a_group, l_group_name_len);
//     int l_count_col = sqlite3_column_count(l_stmt);
//     for(;l_count_out < l_count && s_db_sqlite_step(l_stmt, l_error_msg) == SQLITE_ROW && sqlite3_column_type(l_stmt, 0) == SQLITE_BLOB; ++l_count_out) {
//         byte_t *l_current_hash = (byte_t*) sqlite3_column_blob(l_stmt, 0);
//         memcpy(l_curr_point, l_current_hash, sizeof(dap_global_db_driver_hash_t));
//         l_curr_point += sizeof(dap_global_db_driver_hash_t);
//     }
//     l_ret->hashes_count = l_count_out + l_blank_add;
// clean_and_ret:
//     s_db_sqlite_clean(l_conn, 2, l_query_str, l_query_count_str, l_stmt, l_stmt_count);
//     return l_ret;
// }

/**
 * @brief Reads some objects from a database by conditions started from concretic hash
 * @param a_group a group name string
 * @param a_hash_from startin hash (not include to result)
 * @param a_count_out[in] a number of objects to be read, if equals 0 reads with no limits
 * @param a_count_out[out] a number of objects that were read
 * @param a_with_holes if true - read any records, if false - only actual records
 * @return If successful, a pointer to an objects, otherwise NULL.
 */
static dap_store_obj_t* s_db_pgsql_read_cond_store_obj(const char *a_group, dap_global_db_driver_hash_t a_hash_from, size_t *a_count_out, bool a_with_holes)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// func work
    const char *l_error_msg = "read";
    dap_store_obj_t *l_ret = NULL;
    char *l_query_str = dap_strdup_printf("SELECT * FROM \"%s\""
                                    " WHERE driver_key > $1 AND (flags & '%d' %s 0)"
                                    " ORDER BY driver_key LIMIT '%d'",
                                    a_group, DAP_GLOBAL_DB_RECORD_DEL,
                                    a_with_holes ? ">=" : "=",
                                    a_count_out && *a_count_out ? (int)*a_count_out : (int)DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT);
    if (!l_query_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }
    PGresult *l_query_res = s_db_pgsql_exec_tuples(l_conn->conn, l_query_str, &a_hash_from, NULL, 0, NULL);
    DAP_DELETE(l_query_str);
    
// memory alloc
    uint64_t l_count = PQntuples(l_query_res);
    l_count = a_count_out && *a_count_out ? dap_min(*a_count_out, l_count) : l_count;
    if (!l_count) {
        log_it(L_INFO, "There are no records satisfying the read request");
        goto clean_and_ret;
    }
    if (!( l_ret = DAP_NEW_Z_COUNT(dap_store_obj_t, l_count) )) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        goto clean_and_ret;
    }
    
// data forming
    size_t l_count_out = 0;
    for ( ; l_count_out < l_count && !s_db_pgsql_fill_one_item(a_group, l_ret + l_count_out, l_query_res, l_count_out); ++l_count_out ) {};
    if (a_count_out)
        *a_count_out = l_count_out;
clean_and_ret:
    PQclear(l_query_res);
    s_db_pgsql_free_connection(l_conn, false);
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
static dap_store_obj_t* s_db_pgsql_read_store_obj(const char *a_group, const char *a_key, size_t *a_count_out, bool a_with_holes)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// func work
    const char *l_error_msg = "read";
    dap_store_obj_t *l_ret = NULL;
    char *l_query_str = NULL;
    if (a_key) {
        l_query_str = dap_strdup_printf("SELECT * FROM \"%s\" WHERE key='%s' AND (flags & '%d' %s 0)", a_group, a_key, DAP_GLOBAL_DB_RECORD_DEL, a_with_holes ? ">=" : "=");
    } else { // no limit
        l_query_str = dap_strdup_printf("SELECT * FROM \"%s\" WHERE flags & '%d' %s 0 ORDER BY driver_key LIMIT '%d'",
                                        a_group, DAP_GLOBAL_DB_RECORD_DEL,
                                        a_with_holes ? ">=" : "=",
                                        a_count_out && *a_count_out ? (int)*a_count_out : -1);
    }
    if (!l_query_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }
    PGresult *l_query_res = s_db_pgsql_exec_tuples(l_conn->conn, l_query_str, NULL, NULL, 0, NULL);
    DAP_DELETE(l_query_str);
    
// memory alloc
    uint64_t l_count = PQntuples(l_query_res);
    l_count = a_count_out && *a_count_out ? dap_min(*a_count_out, l_count) : l_count;
    if (!l_count) {
        log_it(L_INFO, "There are no records satisfying the read request");
        goto clean_and_ret;
    }
    if (!( l_ret = DAP_NEW_Z_COUNT(dap_store_obj_t, l_count) )) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        goto clean_and_ret;
    }
    
// data forming
    size_t l_count_out = 0;
    for ( ; l_count_out < l_count && !s_db_pgsql_fill_one_item(a_group, l_ret + l_count_out, l_query_res, l_count_out); ++l_count_out ) {};
    if (a_count_out)
        *a_count_out = l_count_out;
clean_and_ret:
    s_db_pgsql_free_connection(l_conn, false);
    PQclear(l_query_res);
    return l_ret;
}

// /**
//  * @brief Gets a list of group names by a_group_mask.
//  * @param a_group_mask a group name mask
//  * @return If successful, a pointer to a list of group names, otherwise NULL.
//  */
// static dap_list_t *s_db_sqlite_get_groups_by_mask(const char *a_group_mask)
// {
// // sanity check
//     conn_list_item_t *l_conn = NULL;
//     dap_return_val_if_pass(!a_group_mask || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// // preparing
//     const char *l_error_msg = "get groups";
//     dap_list_t* l_ret = NULL;
//     sqlite3_stmt *l_stmt = NULL;
//     char *l_mask = NULL;
//     char *l_query_str = sqlite3_mprintf("SELECT name FROM sqlite_master WHERE type ='table' AND name NOT LIKE 'sqlite_%c'", '%');
//     if (!l_query_str) {
//         log_it(L_ERROR, "Error in SQL request forming");
//         goto clean_and_ret;
//     }
    
//     if(s_db_sqlite_prepare(l_conn->conn, l_query_str, &l_stmt, l_error_msg)!= SQLITE_OK) {
//         goto clean_and_ret;
//     }
//     l_mask = dap_str_replace_char(a_group_mask, '.', '_');
//     int l_ret_code = 0;
//     for (l_ret_code = s_db_sqlite_step(l_stmt, l_error_msg); l_ret_code == SQLITE_ROW && sqlite3_column_type(l_stmt, 0) == SQLITE_TEXT; l_ret_code = s_db_sqlite_step(l_stmt, l_error_msg)) {
//         const char *l_table_name = (const char *)sqlite3_column_text(l_stmt, 0);
//         if (dap_global_db_group_match_mask(l_table_name, l_mask))
//             l_ret = dap_list_prepend(l_ret, dap_str_replace_char(l_table_name, '_', '.'));
//     }
//     if(l_ret_code != SQLITE_DONE) {
//         log_it(L_ERROR, "SQLite read error %d(%s)", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
//     }
// clean_and_ret:
//     s_db_sqlite_clean(l_conn, 1, l_query_str, l_stmt);
//     DAP_DEL_Z(l_mask);
//     return l_ret;
// }

/**
 * @brief Reads a number of objects from a s_db database by a hash
 * @param a_group a group name string
 * @param a_hash_from startin hash (not include to result)
 * @param a_with_holes if true - read any records, if false - only actual records
 * @return Returns a number of objects.
 */
static size_t s_db_pgsql_read_count_store(const char *a_group, dap_global_db_driver_hash_t a_hash_from, bool a_with_holes)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), 0);
// preparing
    const char *l_error_msg = "count read";
    char *l_query_count_str = dap_strdup_printf("SELECT COUNT(*) FROM \"%s\" "
                                        " WHERE driver_key > $1 AND (flags & '%d' %s 0)",
                                        a_group, DAP_GLOBAL_DB_RECORD_DEL,
                                        a_with_holes ? ">=" : "=");
    if (!l_query_count_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }
    PGresult *l_query_res = s_db_pgsql_exec_tuples(l_conn->conn, l_query_count_str, &a_hash_from, NULL, 0, NULL);
    DAP_DELETE(l_query_count_str);
    uint64_t *l_ret = (uint64_t *)PQgetvalue(l_query_res, 0, 0);
clean_and_ret:
    s_db_pgsql_free_connection(l_conn, false);
    PQclear(l_query_res);
    return l_ret ? be64toh(*l_ret) : 0;
}

// /**
//  * @brief Checks if an object is in a database by hash.
//  * @param a_group a group name string
//  * @param a_hash a object hash
//  * @return Returns true if it is, false it's not.
//  */
// static bool s_db_sqlite_is_hash(const char *a_group, dap_global_db_driver_hash_t a_hash)
// {
// // sanity check
//     conn_list_item_t *l_conn = NULL;
//     dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), false);
// // preparing
//     const char *l_error_msg = "is hash read";
//     bool l_ret = false;
//     sqlite3_stmt *l_stmt_count = NULL;
//     char *l_table_name = dap_str_replace_char(a_group, '.', '_');
//     char *l_query_count_str = sqlite3_mprintf("SELECT COUNT(*) FROM '%s' "
//                                         " WHERE driver_key = ?",
//                                         l_table_name);
//     DAP_DEL_Z(l_table_name);
//     if (!l_query_count_str) {
//         log_it(L_ERROR, "Error in SQL request forming");
//         goto clean_and_ret;
//     }
    
//     if(s_db_sqlite_prepare(l_conn->conn, l_query_count_str, &l_stmt_count, l_error_msg)!= SQLITE_OK ||
//         s_db_sqlite_bind_blob64(l_stmt_count, 1, &a_hash, sizeof(a_hash), SQLITE_STATIC, l_error_msg) != SQLITE_OK ||
//         s_db_sqlite_step(l_stmt_count, l_error_msg) != SQLITE_ROW)
//     {
//         goto clean_and_ret;
//     }
//     l_ret = (bool)sqlite3_column_int64(l_stmt_count, 0);
// clean_and_ret:
//     s_db_sqlite_clean(l_conn, 1, l_query_count_str, l_stmt_count);
//     return l_ret;
// }

/**
 * @brief Checks if an object is in a database by a_group and a_key.
 * @param a_group a group name string
 * @param a_key a object key string
 * @return Returns true if it is, false it's not.
 */
static bool s_db_pgsql_is_obj(const char *a_group, const char *a_key)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !a_key || !(l_conn = s_db_pgsql_get_connection(false)), 0);
// preparing
    const char *l_error_msg = "is obj read";
    char *l_key_escape = dap_strdup(a_key);
    if (!PQescapeStringConn(l_conn->conn, l_key_escape, a_key, dap_strlen(a_key), NULL)) {
        log_it(L_ERROR, "Error in PGSQL string escaping");
        goto clean_and_ret;
    }
    char *l_query_str = dap_strdup_printf("SELECT EXISTS(SELECT * FROM \"%s\" WHERE key='%s')", a_group, l_key_escape);
    DAP_DELETE(l_key_escape);
    if (!l_query_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }
    PGresult *l_query_res = s_db_pgsql_exec_tuples(l_conn->conn, l_query_str, NULL, NULL, 0, NULL);
    DAP_DELETE(l_query_str);
    char *l_ret = PQgetvalue(l_query_res, 0, 0);
clean_and_ret:
    s_db_pgsql_free_connection(l_conn, false);
    PQclear(l_query_res);
    return l_ret ? *l_ret : false;
}

// /**
//  * @brief Flushes a SQLite database cahce to disk
//  * @note The function closes and opens the database connection
//  * @return result code.
//  */
// static int s_db_sqlite_flush()
// {
// // sanity check
//     conn_list_item_t *l_conn = s_db_pgsql_get_connection(false);
//     dap_return_val_if_pass(!l_conn, -1);
// // preparing
//     char *l_error_message = NULL;
//     log_it(L_DEBUG, "Start flush sqlite data base.");
//     sqlite3_close(l_conn->conn);
//     if ( !(l_conn->conn = s_db_sqlite_open(s_filename_db, SQLITE_OPEN_READWRITE, &l_error_message)) ) {
//         log_it(L_ERROR, "Can't init sqlite err: \"%s\"", l_error_message ? l_error_message: "UNKNOWN");
//         sqlite3_free(l_error_message);
//         return -2;
//     }

// #ifndef _WIN32
//     sync();
// #endif
//     s_db_pgsql_free_connection(l_conn, false);
//     s_db_pgsql_free_connection(l_conn, true);
//     return 0;
// }

/**
 * @brief Starts a outstanding transaction in database.
 * @return result code.
 */
static int s_db_pgsql_transaction_start()
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!(l_conn = s_db_pgsql_get_connection(true)), 0);
// func work
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "Start TX: @%p", l_conn->conn);
    
    int l_ret = s_db_pgsql_exec_command(l_conn->conn, "BEGIN", NULL, NULL, 0, NULL);
    if ( l_ret ) {
        s_db_pgsql_free_connection(l_conn, true);
    }
    return  l_ret;
}

/**
 * @brief Ends a outstanding transaction in database.
 * @return result code.
 */
static int s_db_pgsql_transaction_end(bool a_commit)
{
// sanity check
    dap_return_val_if_pass_err(!s_conn || !s_conn->conn, -1, "Outstanding connection not exist");
// func work
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "End TX l_conn: @%p", s_conn->conn);
    int l_ret = 0;
    if (a_commit)
        l_ret = s_db_pgsql_exec_command(s_conn->conn, "COMMIT", NULL, NULL, 0, NULL);
    else
        l_ret = s_db_pgsql_exec_command(s_conn->conn, "ROLLBACK", NULL, NULL, 0, NULL);
    if ( !l_ret ) {
        s_db_pgsql_free_connection(s_conn, true);
    }
    return  l_ret;
}

// void dap_global_db_driver_sqlite_set_attempts_count(uint32_t a_attempts, bool a_force)
// {
//     s_attempts_count = a_force ? a_attempts : dap_max(s_attempts_count, a_attempts);
// }

// /**
//  * @brief Initializes a SQLite database.
//  * @note no thread safe
//  * @param a_filename_db a path to the database file
//  * @param a_drv_callback a pointer to a structure of callback functions
//  * @return If successful returns 0, else a code < 0.
//  */
int dap_global_db_driver_pgsql_init(const char *a_db_path, dap_global_db_driver_callbacks_t *a_drv_callback)
{
// sanity check
    dap_return_val_if_pass(!a_db_path, -1);
    dap_return_val_if_pass_err(s_db_inited, -2, "SQLite driver already init")
// func work


    dap_hash_fast_t l_dir_hash;
    dap_hash_fast(a_db_path, strlen(a_db_path), &l_dir_hash);
    dap_htoa64(s_db_name, l_dir_hash.raw, DAP_PGSQL_DBHASHNAME_LEN);
    s_db_name[DAP_PGSQL_DBHASHNAME_LEN] = '\0';
    if (!dap_dir_test(a_db_path) || !readdir(opendir(a_db_path))) {
        // Create PostgreSQL database
        const char *l_base_conn_str = "dbname=postgres";
        PGconn *l_base_conn = PQconnectdb(l_base_conn_str);
        if (PQstatus(l_base_conn) != CONNECTION_OK) {
            log_it(L_ERROR, "Can't init PostgreSQL database: \"%s\"", PQerrorMessage(l_base_conn));
            PQfinish(l_base_conn);
            return -2;
        }
        char *l_query_str = dap_strdup_printf("DROP DATABASE IF EXISTS \"%s\"", s_db_name);
        PGresult *l_res = PQexec(l_base_conn, l_query_str);
        DAP_DELETE(l_query_str);
        if (PQresultStatus(l_res) != PGRES_COMMAND_OK) {
            log_it(L_ERROR, "Drop database failed: \"%s\"", PQresultErrorMessage(l_res));
            PQclear(l_res);
            PQfinish(l_base_conn);
            return -3;
        }
        PQclear(l_res);
        l_query_str = dap_strdup_printf("DROP TABLESPACE IF EXISTS \"%s\"", s_db_name);
        l_res = PQexec(l_base_conn, l_query_str);
        DAP_DELETE(l_query_str);
        if (PQresultStatus(l_res) != PGRES_COMMAND_OK) {
            log_it(L_ERROR, "Drop tablespace failed with message: \"%s\"", PQresultErrorMessage(l_res));
            PQclear(l_res);
            PQfinish(l_base_conn);
            return -4;
        }
        PQclear(l_res);
        // Check paths and create them if nessesary
        if (!dap_dir_test(a_db_path)) {
            log_it(L_NOTICE, "No directory %s, trying to create...", a_db_path);
            dap_mkdir_with_parents(a_db_path);
            if (!dap_dir_test(a_db_path)) {
                char l_errbuf[255];
                l_errbuf[0] = '\0';
                strerror_r(errno, l_errbuf, sizeof(l_errbuf));
                log_it(L_ERROR, "Can't create directory, error code %d, error string \"%s\"", errno, l_errbuf);
                return -1;
            }
            log_it(L_NOTICE,"Directory created");
            chown(a_db_path, getpwnam("postgres")->pw_uid, -1);
        }
        char l_absolute_path[MAX_PATH] = {};
        if (realpath(a_db_path, l_absolute_path) == NULL) {
            log_it(L_ERROR, "Can't get absolute db dir path");
            PQfinish(l_base_conn);
            return -2;
        }
        l_query_str = dap_strdup_printf("CREATE TABLESPACE \"%s\" LOCATION '%s'", s_db_name, l_absolute_path);
        l_res = PQexec(l_base_conn, l_query_str);
        DAP_DELETE(l_query_str);
        if (PQresultStatus(l_res) != PGRES_COMMAND_OK) {
            log_it(L_ERROR, "Create tablespace failed with message: \"%s\"", PQresultErrorMessage(l_res));
            PQclear(l_res);
            PQfinish(l_base_conn);
            return -5;
        }
        chmod(a_db_path, S_IRWXU | S_IRWXG | S_IRWXO);
        PQclear(l_res);
        l_query_str = dap_strdup_printf("CREATE DATABASE \"%s\" WITH TABLESPACE \"%s\"", s_db_name, s_db_name);
        l_res = PQexec(l_base_conn, l_query_str);
        DAP_DELETE(l_query_str);
        if (PQresultStatus(l_res) != PGRES_COMMAND_OK) {
            log_it(L_ERROR, "Create database failed with message: \"%s\"", PQresultErrorMessage(l_res));
            PQclear(l_res);
            PQfinish(l_base_conn);
            return -6;
        }
        PQclear(l_res);
        PQfinish(l_base_conn);
    }

    a_drv_callback->apply_store_obj         = s_db_pgsql_apply_store_obj;
    a_drv_callback->read_store_obj          = s_db_pgsql_read_store_obj;
    a_drv_callback->read_cond_store_obj     = s_db_pgsql_read_cond_store_obj;
    // a_drv_callback->read_last_store_obj     = s_db_sqlite_read_last_store_obj;
    a_drv_callback->transaction_start       = s_db_pgsql_transaction_start;
    a_drv_callback->transaction_end         = s_db_pgsql_transaction_end;
    // a_drv_callback->get_groups_by_mask      = s_db_sqlite_get_groups_by_mask;
    a_drv_callback->read_count_store        = s_db_pgsql_read_count_store;
    a_drv_callback->is_obj                  = s_db_pgsql_is_obj;
    // a_drv_callback->deinit                  = s_db_sqlite_deinit;
    // a_drv_callback->flush                   = s_db_sqlite_flush;
    // a_drv_callback->get_by_hash             = s_db_sqlite_get_by_hash;
    // a_drv_callback->read_hashes             = s_db_sqlite_read_hashes;
    // a_drv_callback->is_hash                 = s_db_sqlite_is_hash;
    s_db_inited = true;

    return 0;
}


struct dap_pgsql_conn_pool_item {
    PGconn *conn;
    int busy;
};

static PGconn *s_trans_conn = NULL;
static struct dap_pgsql_conn_pool_item s_conn_pool[DAP_PGSQL_POOL_COUNT];
static pthread_rwlock_t s_db_rwlock = PTHREAD_RWLOCK_INITIALIZER;

static PGconn *s_pgsql_get_connection(void)
{
    if (pthread_rwlock_wrlock(&s_db_rwlock) == EDEADLK) {
        return s_trans_conn;
    }
    PGconn *l_ret = NULL;
    for (int i = 0; i < DAP_PGSQL_POOL_COUNT; i++) {
        if (!s_conn_pool[i].busy) {
            l_ret = s_conn_pool[i].conn;
            s_conn_pool[i].busy = 1;
            break;
        }
    }
    pthread_rwlock_unlock(&s_db_rwlock);
    return l_ret;
}

static void s_pgsql_free_connection(PGconn *a_conn)
{
    if (pthread_rwlock_wrlock(&s_db_rwlock) == EDEADLK) {
        return;
    }
    for (int i = 0; i < DAP_PGSQL_POOL_COUNT; i++) {
        if (s_conn_pool[i].conn == a_conn) {
            s_conn_pool[i].busy = 0;
			break;
        }
    }
    pthread_rwlock_unlock(&s_db_rwlock);
}

/**
 * @brief Deinitializes a PostgreSQL database.
 * 
 * @return Returns 0 if successful.
 */
int dap_db_driver_pgsql_deinit(void)
{
    pthread_rwlock_wrlock(&s_db_rwlock);
    for (int j = 0; j < DAP_PGSQL_POOL_COUNT; j++)
        PQfinish(s_conn_pool[j].conn);
    pthread_rwlock_unlock(&s_db_rwlock);
    pthread_rwlock_destroy(&s_db_rwlock);
    return 0;
}

/**
 * @brief Starts a transaction in a PostgreSQL database.
 * 
 * @return Returns 0 if successful, otherwise -1.
 */
int dap_db_driver_pgsql_start_transaction(void)
{
    s_trans_conn = s_pgsql_get_connection();
    if (!s_trans_conn)
        return -1;
    pthread_rwlock_wrlock(&s_db_rwlock);
    PGresult *l_res = PQexec(s_trans_conn, "BEGIN");
    if (PQresultStatus(l_res) != PGRES_COMMAND_OK) {
        log_it(L_ERROR, "Begin transaction failed with message: \"%s\"", PQresultErrorMessage(l_res));
        pthread_rwlock_unlock(&s_db_rwlock);
        s_pgsql_free_connection(s_trans_conn);
        s_trans_conn = NULL;
    }
    return 0;
}

/**
 * @brief Ends a transaction in a PostgreSQL database.
 * 
 * @return Returns 0 if successful, otherwise -1.
 */
int dap_db_driver_pgsql_end_transaction(void)
{
    if (!s_trans_conn)
        return -1;
    PGresult *l_res = PQexec(s_trans_conn, "COMMIT");
    if (PQresultStatus(l_res) != PGRES_COMMAND_OK) {
        log_it(L_ERROR, "End transaction failed with message: \"%s\"", PQresultErrorMessage(l_res));
    }
    pthread_rwlock_unlock(&s_db_rwlock);
    s_pgsql_free_connection(s_trans_conn);
    s_trans_conn = NULL;
    return 0;
}

/**
 * @brief Creates a table in a PostgreSQL database.
 * 
 * @param a_table_name a table name string
 * @param a_conn a pointer to the connection object
 * @return Returns 0 if successful, otherwise -1.
 */
static int s_pgsql_create_group_table(const char *a_table_name, PGconn *a_conn)
{
    if (!a_table_name)
        return -1;
    int l_ret = 0;
    char *l_query_str = dap_strdup_printf("CREATE TABLE \"%s\""
                                          "(obj_id BIGSERIAL PRIMARY KEY, obj_ts BIGINT, "
                                          "obj_key TEXT UNIQUE, obj_val BYTEA)",
                                          a_table_name);
    PGresult *l_res = PQexec(a_conn, l_query_str);
    DAP_DELETE(l_query_str);
    if (PQresultStatus(l_res) != PGRES_COMMAND_OK) {
        log_it(L_ERROR, "Create table failed with message: \"%s\"", PQresultErrorMessage(l_res));
        l_ret = -3;
    }
    PQclear(l_res);
    return l_ret;
}

/**
 * @brief Applies an object to a PostgreSQL database.
 * 
 * @param a_store_obj a pointer to the object structure
 * @return Returns 0 if successful, else a error code less than zero.
 */
int dap_db_driver_pgsql_apply_store_obj(dap_store_obj_t *a_store_obj)
{
    if (!a_store_obj || !a_store_obj->group)
        return -1;
    char *l_query_str = NULL;
    int l_ret = 0;
    PGresult *l_res = NULL;
    PGconn *l_conn = s_pgsql_get_connection();
    if (!l_conn) {
        log_it(L_ERROR, "Can't pick PostgreSQL connection from pool");
        return -2;
    }
    if (true /* a_store_obj->type == 'a' */) {
        const char *l_param_vals[2];
        time_t l_ts_to_store = htobe64(a_store_obj->timestamp);
        l_param_vals[0] = (const char *)&l_ts_to_store;
        l_param_vals[1] = (const char *)a_store_obj->value;
        int l_param_lens[2] = {sizeof(time_t), a_store_obj->value_len};
        int l_param_formats[2] = {1, 1};
        l_query_str = dap_strdup_printf("INSERT INTO \"%s\" (obj_ts, obj_key, obj_val) VALUES ($1, '%s', $2) "
                                        "ON CONFLICT (obj_key) DO UPDATE SET "
                                        "obj_id = EXCLUDED.obj_id, obj_ts = EXCLUDED.obj_ts, obj_val = EXCLUDED.obj_val;",
                                        a_store_obj->group,  a_store_obj->key);

        // execute add request
        l_res = PQexecParams(l_conn, l_query_str, 2, NULL, l_param_vals, l_param_lens, l_param_formats, 0);
        if (PQresultStatus(l_res) != PGRES_COMMAND_OK) {
            if (s_trans_conn) { //we shouldn't fail within a transaacion
                dap_db_driver_pgsql_end_transaction();
                dap_db_driver_pgsql_start_transaction();
                l_conn = s_pgsql_get_connection();
            }
            if (s_pgsql_create_group_table(a_store_obj->group, l_conn) == 0) {
                PQclear(l_res);
                l_res = PQexecParams(l_conn, l_query_str, 2, NULL, l_param_vals, l_param_lens, l_param_formats, 0);
            }
            if (PQresultStatus(l_res) != PGRES_COMMAND_OK) {
                log_it(L_ERROR, "Add object failed with message: \"%s\"", PQresultErrorMessage(l_res));
                l_ret = -3;
            }
        }
    } else if (true /*a_store_obj->type == 'd'*/) {
        // delete one record
        if (a_store_obj->key)
            l_query_str = dap_strdup_printf("DELETE FROM \"%s\" WHERE obj_key = '%s'",
                                            a_store_obj->group, a_store_obj->key);
        // remove all group
        else
            l_query_str = dap_strdup_printf("DROP TABLE \"%s\"", a_store_obj->group);
        // execute delete request
        l_res = PQexec(l_conn, l_query_str);
        if (PQresultStatus(l_res) != PGRES_COMMAND_OK) {
            const char *l_err = PQresultErrorField(l_res, PG_DIAG_SQLSTATE);
            if (!l_err || strcmp(l_err, PGSQL_INVALID_TABLE)) {
                log_it(L_ERROR, "Delete object failed with message: \"%s\"", PQresultErrorMessage(l_res));
                l_ret = -4;
            }
        }
    }
    else {
        // log_it(L_ERROR, "Unknown store_obj type '0x%x'", a_store_obj->type);
        s_pgsql_free_connection(l_conn);
        return -5;
    }
    DAP_DELETE(l_query_str);
    PQclear(l_res);
    s_pgsql_free_connection(l_conn);
    return l_ret;
}

/**
 * @brief Fills a object from a row
 * @param a_group a group name string
 * @param a_obj a pointer to the object
 * @param a_res a pointer to the result structure
 * @param a_row a row number
 * @return (none)
 */
static void s_pgsql_fill_object(const char *a_group, dap_store_obj_t *a_obj, PGresult *a_res, int a_row)
{
    a_obj->group = dap_strdup(a_group);
    int q = PQnfields(a_res);
    while (q-- > 0) {
        switch (q) {
        // case PQfnumber(a_res, "obj_val"):
        //     a_obj->value_len = PQgetlength(a_res, a_row, q);
        //     a_obj->value = DAP_DUP_SIZE(PQgetvalue(a_res, a_row, q), a_obj->value_len);
        //     break;
        // case PQfnumber(a_res, "obj_key"):
        //     a_obj->key = dap_strdup(PQgetvalue(a_res, a_row, q));
        //     break;
        // case PQfnumber(a_res, "obj_ts"):
        //     a_obj->timestamp = be64toh(*(time_t*)PQgetvalue(a_res, a_row, q));
        //     break;
        // case PQfnumber(a_res, "obj_id"):
        //     a_obj->id = be64toh(*(uint64_t*)PQgetvalue(a_res, a_row, q));
        //     break;
        }
    }
}

/**
 * @brief Reads some objects from a PostgreSQL database by a_group and a_key.
 * @param a_group a group name string
 * @param a_key an object key string, if equals NULL reads the whole group
 * @param a_count_out[in] a number of objects to be read, if equals 0 reads with no limits
 * @param a_count_out[out] a number of objects that were read
 * @return If successful, a pointer to an objects, otherwise a null pointer.
 */
dap_store_obj_t *dap_db_driver_pgsql_read_store_obj(const char *a_group, const char *a_key, size_t *a_count_out)
{
    if (!a_group)
        return NULL;
    PGconn *l_conn = s_pgsql_get_connection();
    if (!l_conn) {
        log_it(L_ERROR, "Can't pick PostgreSQL connection from pool");
        return NULL;
    }
    char *l_query_str;
    if (a_key) {
       l_query_str = dap_strdup_printf("SELECT * FROM \"%s\" WHERE obj_key = '%s'", a_group, a_key);
    } else {
        if (a_count_out && *a_count_out)
            l_query_str = dap_strdup_printf("SELECT * FROM \"%s\" ORDER BY obj_id ASC LIMIT %d", a_group, *a_count_out);
        else
            l_query_str = dap_strdup_printf("SELECT * FROM \"%s\" ORDER BY obj_id ASC", a_group);
    }

    PGresult *l_res = PQexecParams(l_conn, l_query_str, 0, NULL, NULL, NULL, NULL, 1);
    DAP_DELETE(l_query_str);
    if (PQresultStatus(l_res) != PGRES_TUPLES_OK) {
        const char *l_err = PQresultErrorField(l_res, PG_DIAG_SQLSTATE);
        if (!l_err || strcmp(l_err, PGSQL_INVALID_TABLE))
            log_it(L_ERROR, "Read objects failed with message: \"%s\"", PQresultErrorMessage(l_res));
        PQclear(l_res);
        s_pgsql_free_connection(l_conn);
        return NULL;
    }

    // parse reply
    size_t l_count = PQntuples(l_res);
    if (l_count) {
        dap_store_obj_t *l_obj = DAP_NEW_Z_COUNT(dap_store_obj_t, l_count);
        if (!l_obj) {
            log_it(L_ERROR, "Memory allocation error");
            l_count = 0;
        } else {
            for (size_t i = 0; i < l_count; ++i) {
                s_pgsql_fill_object(a_group, (dap_store_obj_t*)(l_obj + i), l_res, i);
            }
        }
    }
    PQclear(l_res);
    s_pgsql_free_connection(l_conn);
    if (a_count_out)
        *a_count_out = l_count;
    return NULL;
}

/**
 * @brief Reads a last object from a PostgreSQL database.
 * @param a_group a group name string
 * @return Returns a pointer to the object if successful, otherwise a null pointer.
 */
dap_store_obj_t *dap_db_driver_pgsql_read_last_store_obj(const char *a_group)
{
    if (!a_group)
        return NULL;
    PGconn *l_conn = s_pgsql_get_connection();
    if (!l_conn) {
        log_it(L_ERROR, "Can't pick PostgreSQL connection from pool");
        return NULL;
    }
    char *l_query_str = dap_strdup_printf("SELECT * FROM \"%s\" ORDER BY obj_id DESC LIMIT 1", a_group);
    PGresult *l_res = PQexecParams(l_conn, l_query_str, 0, NULL, NULL, NULL, NULL, 1);
    DAP_DELETE(l_query_str);
    if (PQresultStatus(l_res) != PGRES_TUPLES_OK) {
        const char *l_err = PQresultErrorField(l_res, PG_DIAG_SQLSTATE);
        if (!l_err || strcmp(l_err, PGSQL_INVALID_TABLE))
            log_it(L_ERROR, "Read last object failed with message: \"%s\"", PQresultErrorMessage(l_res));
        PQclear(l_res);
        s_pgsql_free_connection(l_conn);
        return NULL;
    }
    dap_store_obj_t *l_obj = NULL;
    if (PQntuples(l_res)) {
        l_obj = DAP_NEW_Z(dap_store_obj_t);
        s_pgsql_fill_object(a_group, l_obj, l_res, 0);
    }
    PQclear(l_res);
    s_pgsql_free_connection(l_conn);
    return l_obj;
}

/**
 * @brief Reads some objects from a PostgreSQL database by conditions.
 * @param a_group a group name string
 * @param a_id id
 * @param a_count_out[in] a number of objects to be read, if equals 0 reads with no limits
 * @param a_count_out[out] a number of objects that were read 
 * @return If successful, a pointer to an objects, otherwise a null pointer. 
 */
dap_store_obj_t *dap_db_driver_pgsql_read_cond_store_obj(const char *a_group, uint64_t a_id, size_t *a_count_out)
{
    if (!a_group)
        return NULL;
    PGconn *l_conn = s_pgsql_get_connection();
    if (!l_conn) {
        log_it(L_ERROR, "Can't pick PostgreSQL connection from pool");
        return NULL;
    }
    char *l_query_str;
    if (a_count_out && *a_count_out)
        l_query_str = dap_strdup_printf("SELECT * FROM \"%s\" WHERE obj_id >= '%"DAP_UINT64_FORMAT_U"' "
                                        "ORDER BY obj_id ASC LIMIT %zu", a_group, a_id, *a_count_out);
    else
        l_query_str = dap_strdup_printf("SELECT * FROM \"%s\" WHERE obj_id >= '%"DAP_UINT64_FORMAT_U"' "
                                        "ORDER BY obj_id ASC", a_group, a_id);
    PGresult *l_res = PQexecParams(l_conn, l_query_str, 0, NULL, NULL, NULL, NULL, 1);
    DAP_DELETE(l_query_str);
    if (PQresultStatus(l_res) != PGRES_TUPLES_OK) {
        const char *l_err = PQresultErrorField(l_res, PG_DIAG_SQLSTATE);
        if (!l_err || strcmp(l_err, PGSQL_INVALID_TABLE))
            log_it(L_ERROR, "Conditional read objects failed with message: \"%s\"", PQresultErrorMessage(l_res));
        PQclear(l_res);
        s_pgsql_free_connection(l_conn);
        return NULL;
    }

    // parse reply
    size_t l_count = PQntuples(l_res);
    if (l_count) {
        dap_store_obj_t *l_obj = DAP_NEW_Z_COUNT(dap_store_obj_t, l_count);
        if (!l_obj) {
            log_it(L_ERROR, "Memory allocation error");
            l_count = 0;
        } else {
            for (size_t i = 0; i < l_count; ++i) {
                s_pgsql_fill_object(a_group, (dap_store_obj_t*)(l_obj + i), l_res, i);
            }
        }
    }
    PQclear(l_res);
    s_pgsql_free_connection(l_conn);
    if (a_count_out)
        *a_count_out = l_count;
    return NULL;
}

/**
 * @brief Gets a list of group names from a PostgreSQL database by a_group_mask.
 * @param a_group_mask a group name mask
 * @return Returns a pointer to a list of group names if successful, otherwise a null pointer.
 */
dap_list_t *dap_db_driver_pgsql_get_groups_by_mask(const char *a_group_mask)
{
    if (!a_group_mask)
        return NULL;
    PGconn *l_conn = s_pgsql_get_connection();
    if (!l_conn) {
        log_it(L_ERROR, "Can't pick PostgreSQL connection from pool");
        return NULL;
    }
    const char *l_query_str = "SELECT tablename FROM pg_catalog.pg_tables WHERE "
                              "schemaname != 'information_schema' AND schemaname != 'pg_catalog'";
    PGresult *l_res = PQexec(l_conn, l_query_str);
    if (PQresultStatus(l_res) != PGRES_TUPLES_OK) {
        log_it(L_ERROR, "Read tables failed with message: \"%s\"", PQresultErrorMessage(l_res));
        PQclear(l_res);
        s_pgsql_free_connection(l_conn);
        return NULL;
    }

    dap_list_t *l_ret_list = NULL;
    for (int i = 0; i < PQntuples(l_res); i++) {
        char *l_table_name = (char *)PQgetvalue(l_res, i, 0);
        if(!dap_fnmatch(a_group_mask, l_table_name, 0))
            l_ret_list = dap_list_prepend(l_ret_list, dap_strdup(l_table_name));
    }
    PQclear(l_res);
    s_pgsql_free_connection(l_conn);
    return l_ret_list;
}

/**
 * @brief Reads a number of objects from a PostgreSQL database by a_group and a_id.
 * @param a_group a group name string
 * @param a_id id starting from which the quantity is calculated
 * @return Returns a number of objects.
 */
size_t dap_db_driver_pgsql_read_count_store(const char *a_group, uint64_t a_id)
{
    if (!a_group)
        return 0;
    PGconn *l_conn = s_pgsql_get_connection();
    if (!l_conn) {
        log_it(L_ERROR, "Can't pick PostgreSQL connection from pool");
        return 0;
    }
    char *l_query_str = dap_strdup_printf("SELECT count(*) FROM \"%s\" WHERE obj_id >= '%"DAP_UINT64_FORMAT_U"'",
                                          a_group, a_id);
    PGresult *l_res = PQexecParams(l_conn, l_query_str, 0, NULL, NULL, NULL, NULL, 1);
    DAP_DELETE(l_query_str);
    if (PQresultStatus(l_res) != PGRES_TUPLES_OK) {
        const char *l_err = PQresultErrorField(l_res, PG_DIAG_SQLSTATE);
        if (!l_err || strcmp(l_err, PGSQL_INVALID_TABLE))
            log_it(L_ERROR, "Count objects failed with message: \"%s\"", PQresultErrorMessage(l_res));
        PQclear(l_res);
        s_pgsql_free_connection(l_conn);
        return 0;
    }
    size_t l_ret = be64toh(*(uint64_t *)PQgetvalue(l_res, 0, 0));
    PQclear(l_res);
    s_pgsql_free_connection(l_conn);
    return l_ret;
}

/**
 * @brief Checks if an object is in a PostgreSQL database by a_group and a_key.
 * @param a_group a group name string
 * @param a_key a object key string
 * @return Returns true if it is, false it's not.
 */
bool dap_db_driver_pgsql_is_obj(const char *a_group, const char *a_key)
{
    if (!a_group)
        return NULL;
    PGconn *l_conn = s_pgsql_get_connection();
    if (!l_conn) {
        log_it(L_ERROR, "Can't pick PostgreSQL connection from pool");
        return NULL;
    }
    char *l_query_str = dap_strdup_printf("SELECT EXISTS(SELECT * FROM \"%s\" WHERE obj_key = '%s')", a_group, a_key);
    PGresult *l_res = PQexecParams(l_conn, l_query_str, 0, NULL, NULL, NULL, NULL, 1);
    DAP_DELETE(l_query_str);
    if (PQresultStatus(l_res) != PGRES_TUPLES_OK) {
        const char *l_err = PQresultErrorField(l_res, PG_DIAG_SQLSTATE);
        if (!l_err || strcmp(l_err, PGSQL_INVALID_TABLE))
            log_it(L_ERROR, "Existance check of object failed with message: \"%s\"", PQresultErrorMessage(l_res));
        PQclear(l_res);
        s_pgsql_free_connection(l_conn);
        return 0;
    }
    int l_ret = *PQgetvalue(l_res, 0, 0);
    PQclear(l_res);
    s_pgsql_free_connection(l_conn);
    return l_ret;
}

/**
 * @brief Flushes a PostgreSQ database cahce to disk.
 * @return Returns 0 if successful, else a error code less than zero.
 */
int dap_db_driver_pgsql_flush()
{
    PGconn *l_conn = s_pgsql_get_connection();
    if (!l_conn) {
        log_it(L_ERROR, "Can't pick PostgreSQL connection from pool");
        return -4;
    }
    int l_ret = 0;
    PGresult *l_res = PQexec(l_conn, "CHECKPOINT");
    if (PQresultStatus(l_res) != PGRES_COMMAND_OK) {
        log_it(L_ERROR, "Flushing database on disk failed with message: \"%s\"", PQresultErrorMessage(l_res));
        l_ret = -5;
    }
    PQclear(l_res);
    if (!l_ret) {
        PGresult *l_res = PQexec(l_conn, "VACUUM");
        if (PQresultStatus(l_res) != PGRES_COMMAND_OK) {
            log_it(L_ERROR, "Vaccuming database failed with message: \"%s\"", PQresultErrorMessage(l_res));
            l_ret = -6;
        }
        PQclear(l_res);
    }
    s_pgsql_free_connection(l_conn);
    return l_ret;
}
