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
#ifdef DAP_OS_DARWIN
#include <libpq-fe.h>
#else
#include <postgresql/libpq-fe.h>
#endif

#include "dap_global_db_driver_pgsql.h"
#include "dap_common.h"
#include "dap_global_db_pkt.h"
#include "dap_global_db.h"
#include "dap_strfuncs.h"

#define LOG_TAG "db_pgsql"
#define DAP_GLOBAL_DB_TYPE_CURRENT DAP_GLOBAL_DB_TYPE_PGSQL

static char s_db_conn_info[MAX_PATH + 1];

typedef struct conn_pool_item {
    PGconn *conn;                                               /* PGSQL connection context itself */
    int idx;                                                    /* Just index, no more */
    atomic_flag busy_conn;                                      /* Connection busy flag */
    atomic_flag busy_trans;                                     /* Outstanding transaction busy flag */
    atomic_ullong  usage;                                       /* Usage counter */
} conn_list_item_t;

extern int g_dap_global_db_debug_more;                         /* Enable extensible debug output */

static bool s_db_inited = false;
static _Thread_local conn_list_item_t *s_conn = NULL;  // local connection

static const char *s_db_fields_name[] = {"driver_key", "key", "flags", "value", "sign"};

DAP_STATIC_INLINE void s_request_err_msg(const char *a_request_str)
{
    return debug_if(g_dap_global_db_debug_more, L_INFO, "There are no records satisfying the %s request", a_request_str);
}

static void s_connection_destructor(UNUSED_ARG void *a_conn) {
    if (!s_conn)
        return;
    PQfinish(s_conn->conn);
    log_it(L_DEBUG, "Close  connection: @%p/%p, usage: %llu", s_conn, s_conn->conn, s_conn->usage);
    DAP_DEL_Z(s_conn);
}

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

static PGresult *s_db_pgsql_exec(PGconn *a_conn, const char *a_query, int a_param_count, const char *const *a_param_vals, const int *a_param_lens, const int *a_param_formats, int a_result_format, ExecStatusType a_req_result, const char *a_error_msg)
{   
    PGresult *l_query_res = PQexecParams(a_conn, a_query, a_param_count, NULL, a_param_vals, a_param_lens, a_param_formats, a_result_format);
    if ( PQresultStatus(l_query_res) != a_req_result ) {
        const char *l_err = PQresultErrorField(l_query_res, PG_DIAG_SQLSTATE);
        if (!l_err || dap_strcmp(l_err, PGSQL_INVALID_TABLE))
            log_it(L_ERROR, "Query from \"%s\" was failed with message: \"%s\"", a_error_msg, PQresultErrorMessage(l_query_res));
        PQclear(l_query_res);
        return  NULL;
    }
    return l_query_res;
}

/**
 * @brief Executes PGSQL statements.
 * @param a_conn a pointer to an instance of PGSQL connection
 * @param a_query the PGSQL statement
 * @param a_hash pointer to data hash
 * @param a_value pointer to data
 * @param a_value_len data len to write
 * @param a_sign record sign
 * @param a_error_msg additional error log info
 * @return pass 0, error - other.
 */
static int s_db_pgsql_exec_command(PGconn *a_conn, const char *a_query, dap_global_db_driver_hash_t *a_hash, byte_t *a_value, size_t a_value_len, dap_sign_t *a_sign, const char *a_error_msg)
{
    dap_return_val_if_pass(!a_conn || !a_query, -1);
    
    const char *l_param_vals[3] = {(const char *)a_hash, (const char *)a_value, (const char *)a_sign};
    int l_param_lens[3] = {sizeof(dap_global_db_driver_hash_t), a_value_len, dap_sign_get_size(a_sign)};
    int l_param_formats[3] = {1, 1, 1};
    uint8_t l_param_count = a_hash || a_value || a_sign ? 3 : 0;

    PGresult *l_query_res = s_db_pgsql_exec(a_conn, a_query, l_param_count, l_param_vals, l_param_lens, l_param_formats, 1, PGRES_COMMAND_OK, a_error_msg);

    if ( !l_query_res ) {
        return -2;
    }
    PQclear(l_query_res);
    return 0;
}

/**
 * @brief Executes PGSQL statements.
 * @param a_conn a pointer to an instance of PGSQL connection
 * @param a_query the PGSQL statement
 * @param a_hash pointer to data hash
 * @param a_value pointer to data
 * @param a_value_len data len to write
 * @param a_sign record sign
 * @param a_error_msg additional error log info
 * @return pass pointer to PGresult, error NULL.
 */
static PGresult *s_db_pgsql_exec_tuples(PGconn *a_conn, const char *a_query, dap_global_db_driver_hash_t *a_hash, const char *a_error_msg)
{
    dap_return_val_if_pass(!a_conn || !a_query, NULL);
    return s_db_pgsql_exec(a_conn, a_query, 
        a_hash ? 1 : 0, 
        a_hash ? (const char*[]){ (const char*)a_hash } : NULL,
        a_hash ? (const int[]){ sizeof(dap_global_db_driver_hash_t)} : NULL,
        a_hash ? (const int[]){ 1 } : NULL, 1, PGRES_TUPLES_OK, a_error_msg);
}

/**
 * @brief Prepare connection item
 * @param a_trans outstanding transaction flag
 * @return pointer to connection item, otherwise NULL.
 */
static conn_list_item_t *s_db_pgsql_get_connection(bool a_trans)
{
// sanity check
    dap_return_val_if_pass_err(!s_db_inited, NULL, "PGSQL driver not inited");
// func work
    static int l_conn_idx = 0;
    if (!s_conn) {
        s_conn = DAP_NEW_Z_RET_VAL_IF_FAIL(conn_list_item_t, NULL, NULL);
        pthread_key_t s_destructor_key;
        pthread_key_create(&s_destructor_key, s_connection_destructor);
        pthread_setspecific(s_destructor_key, (const void *)s_conn);
        s_conn->conn = PQconnectdb(s_db_conn_info);
        if (PQstatus(s_conn->conn) != CONNECTION_OK) {
            log_it(L_ERROR, "Can't connect PostgreSQL database: \"%s\"", PQerrorMessage(s_conn->conn));
            DAP_DEL_Z(s_conn);
            return NULL;
        }
        s_conn->idx = l_conn_idx++;
        log_it(L_DEBUG, "PGSQL connection #%d is created @%p", s_conn->idx, s_conn);
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
 * @brief Deinitializes a PGSQL database.
 * @return error -1, pass 0.
 */
int s_db_pqsql_deinit(void)
{
    if (!s_db_inited) {
        log_it(L_WARNING, "PGSQL driver already deinited");
        return -1;
    }
    s_connection_destructor(NULL);
    s_db_inited = false;
    return 0;
}

/**
 * @brief Creates a table and unique index in the s_db database.
 * @param a_table_name a table name string
 * @param a_conn connection item to use query
 * @return error -1, pass 0.
 */
static int s_db_pgsql_create_group_table(const char *a_table_name, conn_list_item_t *a_conn)
{
// sanity check
    dap_return_val_if_pass(!a_table_name || !a_conn, -EINVAL);
    char *l_query = dap_strdup_printf("CREATE TABLE IF NOT EXISTS \"%s\"(driver_key BYTEA UNIQUE NOT NULL PRIMARY KEY, key TEXT UNIQUE NOT NULL, flags INT8, value BYTEA, sign BYTEA)", a_table_name);
    int l_ret = s_db_pgsql_exec_command(a_conn->conn, l_query, NULL, NULL, 0, NULL, __FUNCTION__);
    DAP_DELETE(l_query);
    return l_ret;
}

/**
 * @brief Applies an object to a database.
 * @param a_store_obj a pointer to the object structure
 * @return error -1, pass 0.
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
            l_query = dap_strdup_printf("INSERT INTO \"%s\" (driver_key, key, flags, value, sign) VALUES($1, '%s', '%d', $2, $3)"
            "ON CONFLICT(key) DO UPDATE SET driver_key = EXCLUDED.driver_key, flags = EXCLUDED.flags, value = EXCLUDED.value, sign = EXCLUDED.sign;",
                                                  a_store_obj->group, a_store_obj->key, (int)(a_store_obj->flags & ~DAP_GLOBAL_DB_RECORD_NEW));
        }
        dap_global_db_driver_hash_t l_driver_key = dap_global_db_driver_hash_get(a_store_obj);
        l_ret = s_db_pgsql_exec_command(l_conn->conn, l_query, &l_driver_key, a_store_obj->value, a_store_obj->value_len, a_store_obj->sign, "insert");
        if (l_ret) {
            // create table and repeat request
            if (!s_db_pgsql_create_group_table(a_store_obj->group, l_conn))
                l_ret = s_db_pgsql_exec_command(l_conn->conn, l_query, &l_driver_key, a_store_obj->value, a_store_obj->value_len, a_store_obj->sign, "insert");
        }
    } else {
        const char *l_err_msg;
        if (a_store_obj->key) {  // delete one record
            l_query = dap_strdup_printf("DELETE FROM \"%s\" WHERE key = '%s';", a_store_obj->group, a_store_obj->key);
            l_err_msg = "delete";
        } else {  // remove all group
            l_query = dap_strdup_printf("DROP TABLE IF EXISTS \"%s\";", a_store_obj->group);
            l_err_msg = "drop table";
        }
        l_ret = s_db_pgsql_exec_command(l_conn->conn, l_query, NULL, NULL, 0, NULL, l_err_msg);
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
 * @param a_query_res a ponter to the PGresult
 * @param a_row row num
 * @return error -1, pass 0.
 */
static int s_db_pgsql_fill_one_item(const char *a_group, dap_store_obj_t *a_obj, PGresult *a_query_res, int a_row)
{
// sanity check
    dap_return_val_if_pass(!a_group || !a_obj || !a_query_res, -1);
    a_obj->group = dap_strdup(a_group);
// preparing
    int l_count_col = PQnfields(a_query_res);
    int l_count_col_out = 0;
    for (size_t i = 0; i < sizeof(s_db_fields_name) / sizeof (const char *); ++i) {
        dap_global_db_driver_hash_t *l_driver_key = NULL;
        int l_col_num = PQfnumber(a_query_res, s_db_fields_name[i]);
        switch (i) {
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
                a_obj->flags = be64toh(*((int64_t *)PQgetvalue(a_query_res, a_row, l_col_num)));
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

/**
 * @brief Reads a last object from the s_db database.
 * @param a_group a group name string
 * @param a_with_holes if true - read any records, if false - only actual records
 * @return pass - a pointer to the object, error - NULL.
 */
static dap_store_obj_t* s_db_pgsql_read_last_store_obj(const char *a_group, bool a_with_holes)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// func work
    dap_store_obj_t *l_ret = NULL;
    char *l_query_str = dap_strdup_printf("SELECT * FROM \"%s\""
                                        " WHERE flags & '%d' %s 0"
                                        " ORDER BY driver_key DESC LIMIT 1;",
                                        a_group, DAP_GLOBAL_DB_RECORD_DEL,
                                        a_with_holes ? ">=" : "=");
    if (!l_query_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }
    PGresult *l_query_res = s_db_pgsql_exec_tuples(l_conn->conn, l_query_str, NULL, __FUNCTION__);
    DAP_DELETE(l_query_str);
    
// memory alloc
    uint64_t l_count = PQntuples(l_query_res);
    if (!l_count) {
        s_request_err_msg(__FUNCTION__);
        goto clean_and_ret;
    }
    if (!( l_ret = DAP_NEW_Z_COUNT(dap_store_obj_t, l_count) )) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        goto clean_and_ret;
    }
// data forming
    s_db_pgsql_fill_one_item(a_group, l_ret, l_query_res, 0);
clean_and_ret:
    PQclear(l_query_res);
    s_db_pgsql_free_connection(l_conn, false);
    return l_ret;
}

/**
 * @brief Forming objects pack from hash list.
 * @param a_group a group name string
 * @param a_hashes pointer to hashes
 * @param a_count hashes num
 * @return pass - a pointer to the object pack, error - NULL.
 */
static dap_global_db_pkt_pack_t *s_db_pgsql_get_by_hash(const char *a_group, dap_global_db_driver_hash_t *a_hashes, size_t a_count)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !a_hashes || !a_count || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// preparing
    dap_global_db_pkt_pack_t *l_ret = NULL;

    const char **l_param_vals = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(const char *, a_count, NULL);
    int *l_param_lens = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(int, a_count, NULL, l_param_vals);
    int *l_param_formats = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(int, a_count, NULL, l_param_vals, l_param_lens);

    dap_string_t *l_blob_str = dap_string_new_len(NULL, a_count * 4);
    if (!l_blob_str) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_MULTY(l_param_vals, l_param_lens, l_param_formats);
        return NULL;
    }
    for (size_t i = 0; i < a_count; ++i) {
        dap_string_append_printf(l_blob_str, "$%zu,", i + 1);
        l_param_vals[i] = (const char *)(a_hashes + i);
        l_param_lens[i] = sizeof(dap_global_db_driver_hash_t);
        l_param_formats[i] = 1;
    }
    l_blob_str->str[l_blob_str->len - 1] = '\0';
    --l_blob_str->len;
    char *l_query_size_str = dap_strdup_printf("SELECT SUM(LENGTH(key)) + COALESCE(SUM(LENGTH(value)), 0) + COALESCE(SUM(LENGTH(sign)), 0) FROM \"%s\" "
                                        " WHERE driver_key IN (%s);",
                                        a_group, l_blob_str->str);
    char *l_query_str = dap_strdup_printf("SELECT * FROM \"%s\""
                                        " WHERE driver_key IN (%s) ORDER BY driver_key;",
                                        a_group, l_blob_str->str);
    dap_string_free(l_blob_str, true);
    if (!l_query_size_str || !l_query_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }

// memory alloc
    // size count
    PGresult
        *l_query_res = NULL, 
        *l_query_size_res = s_db_pgsql_exec(l_conn->conn, (const char *)l_query_size_str, a_count, l_param_vals, l_param_lens, l_param_formats, 1, PGRES_TUPLES_OK, "get_by_hash_size_count");
    if ( !l_query_size_res ) {
        goto clean_and_ret;
    }
    int64_t *l_size_p = (int64_t *)PQgetvalue(l_query_size_res, 0, 0);
    int64_t l_size = l_size_p ? be64toh(*l_size_p) : 0;
    PQclear(l_query_size_res);

    // get data
    l_query_res = s_db_pgsql_exec(l_conn->conn, (const char *)l_query_str, a_count, l_param_vals, l_param_lens, l_param_formats, 1, PGRES_TUPLES_OK, __FUNCTION__);;
    if ( !l_query_res ) {
        goto clean_and_ret;
    }
    size_t l_count = PQntuples(l_query_res);
    if ( !l_count || l_size <= 0 ) {
        s_request_err_msg(__FUNCTION__);
        goto clean_and_ret;
    }
    // memory alloc
    size_t l_group_name_len = strlen(a_group) + 1;
    size_t l_data_size = l_count * (sizeof(dap_global_db_pkt_t) + l_group_name_len + 1) + l_size;
    l_ret = DAP_NEW_Z_SIZE(dap_global_db_pkt_pack_t, sizeof(dap_global_db_pkt_pack_t) + l_data_size);
    if ( !l_ret ) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        goto clean_and_ret;
    }
    // fill data
    byte_t
        *l_data_pos = l_ret->data,
        *l_data_end = l_data_pos + l_data_size;
    size_t i = 0;
    for (i = 0; i < l_count; ++i) {
        dap_global_db_pkt_t *l_cur_pkt = (dap_global_db_pkt_t*)(l_data_pos);
        l_data_pos = l_cur_pkt->data;
        if ( l_data_pos + l_group_name_len > l_data_end )
            break;
        l_data_pos = dap_mempcpy(l_data_pos, a_group, l_cur_pkt->group_len = l_group_name_len);
        int l_count_col = PQnfields(l_query_res);
        int l_count_col_out = 0;
        for (size_t j = 0; j < sizeof(s_db_fields_name) / sizeof (const char *); ++j) {
            int l_col_num = PQfnumber(l_query_res, s_db_fields_name[j]);
            dap_global_db_driver_hash_t *l_driver_key = NULL;
            size_t l_sign_size = 0;
            switch (j) {
                case 0:
                    l_driver_key = (dap_global_db_driver_hash_t *)PQgetvalue(l_query_res, i, l_col_num);
                    l_cur_pkt->timestamp = be64toh(l_driver_key->bets);
                    l_cur_pkt->crc = be64toh(l_driver_key->becrc);
                    ++l_count_col_out;
                    break;
                case 1:
                    l_cur_pkt->key_len = PQgetlength(l_query_res, i, l_col_num);
                    if ( l_data_pos + l_cur_pkt->key_len + 1 > l_data_end )
                        break;
                    l_data_pos = dap_mempcpy(l_data_pos, PQgetvalue(l_query_res, i, l_col_num), l_cur_pkt->key_len++) + 1;
                    ++l_count_col_out;
                    break;
                case 2:
                    if (PQgetlength(l_query_res, i, l_col_num))
                        l_cur_pkt->flags = be64toh(*((int64_t *)PQgetvalue(l_query_res, i, l_col_num))) & DAP_GLOBAL_DB_RECORD_DEL;
                    ++l_count_col_out;
                    break;
                case 3:
                    l_cur_pkt->value_len = PQgetlength(l_query_res, i, l_col_num);
                    if ( l_data_pos + l_cur_pkt->value_len > l_data_end )
                        break;
                    l_data_pos = dap_mempcpy(l_data_pos, PQgetvalue(l_query_res, i, l_col_num), l_cur_pkt->value_len);
                    ++l_count_col_out;
                    break;
                case 4:
                    l_sign_size = PQgetlength(l_query_res, i, l_col_num);
                    if (l_sign_size) {
                        dap_sign_t *l_sign = (dap_sign_t*)PQgetvalue(l_query_res, i, l_col_num);
                        if ( dap_sign_get_size(l_sign) != l_sign_size || l_data_pos + l_sign_size > l_data_end ) {
                            log_it(L_ERROR, "Wrong sign size in GDB group %s", a_group);
                            break;
                        }
                        l_data_pos = dap_mempcpy(l_data_pos, l_sign, l_sign_size);
                    }
                    ++l_count_col_out;
                    break;
                default:
                    continue;
            }
        }
        if (l_count_col_out != l_count_col) {
            log_it(L_ERROR, "Error in PGSQL fill pkt pack item - filled collumn == %d, expected %d", l_count_col_out, l_count_col);
            break;
        }
        l_cur_pkt->data_len = (uint32_t)(l_data_pos - l_cur_pkt->data);
        l_ret->data_size = (uint64_t)(l_data_pos - l_ret->data);

    }
    l_ret->obj_count = i;
    if (i < l_count) {
        log_it(L_ERROR, "Invalid pack size, only %zu / %zu pkts (%zu / %zu bytes) fit the storage",
                        i, l_count, l_ret->data_size, l_data_size);
        size_t l_new_size = (size_t)(l_data_pos - (byte_t*)l_ret);
        dap_global_db_pkt_pack_t *l_new_pack = DAP_REALLOC(l_ret, l_new_size);
        if (l_new_pack)
            l_ret = l_new_pack;
        else
            DAP_DEL_Z(l_ret);
    }
clean_and_ret:
    PQclear(l_query_res);
    DAP_DEL_MULTY(l_query_size_str, l_query_str, l_param_vals, l_param_lens, l_param_formats);
    s_db_pgsql_free_connection(l_conn, false);
    return l_ret;
}

/**
 * @brief Forming hashes pack started from concretic hash.
 * @param a_group a group name string
 * @param a_hash_from startin hash (not include to result)
 * @return pass - a pointer to the hashes, error - NULL.
 */
static dap_global_db_hash_pkt_t *s_db_pgsql_read_hashes(const char *a_group, dap_global_db_driver_hash_t a_hash_from)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// func work
    dap_global_db_hash_pkt_t *l_ret = NULL;
    char *l_query_str = dap_strdup_printf("SELECT driver_key FROM \"%s\""
                                        " WHERE driver_key > $1"
                                        " ORDER BY driver_key LIMIT '%d';",
                                        a_group, (int)DAP_GLOBAL_DB_COND_READ_KEYS_DEFAULT);
    if (!l_query_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }
    PGresult *l_query_res = s_db_pgsql_exec_tuples(l_conn->conn, l_query_str, &a_hash_from, __FUNCTION__);
    DAP_DELETE(l_query_str);
    
// memory alloc
    uint64_t l_count = PQntuples(l_query_res);
    if (!l_count) {
        s_request_err_msg(__FUNCTION__);
        goto clean_and_ret;
    }
    if (!( l_ret = DAP_NEW_Z_COUNT(dap_store_obj_t, l_count) )) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        goto clean_and_ret;
    }
    size_t l_group_name_len = strlen(a_group) + 1;
    l_ret = DAP_NEW_Z_SIZE(dap_global_db_hash_pkt_t, sizeof(dap_global_db_hash_pkt_t) + (l_count + 1) * sizeof(dap_global_db_driver_hash_t) + l_group_name_len);
    if (!l_ret) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        goto clean_and_ret;
    }
// data forming 
    l_ret->group_name_len = l_group_name_len;
    byte_t *l_curr_point = l_ret->group_n_hashses + l_ret->group_name_len;
    memcpy(l_ret->group_n_hashses, a_group, l_group_name_len);
    int l_col_num = PQfnumber(l_query_res, "driver_key");
    for(size_t i = 0; i < l_count; ++i) {
        dap_global_db_driver_hash_t *l_current_hash = (dap_global_db_driver_hash_t *)PQgetvalue(l_query_res, i, l_col_num);;
        memcpy(l_curr_point, l_current_hash, sizeof(dap_global_db_driver_hash_t));
        l_curr_point += sizeof(dap_global_db_driver_hash_t);
    }
    l_ret->hashes_count = l_count + 1;
clean_and_ret:
    PQclear(l_query_res);
    s_db_pgsql_free_connection(l_conn, false);
    return l_ret;
}

/**
 * @brief Reads some objects from a database by conditions started from concretic hash
 * @param a_group a group name string
 * @param a_hash_from startin hash (not include to result)
 * @param a_count_out[in] a number of objects to be read, if equals 0 reads with no limits
 * @param a_count_out[out] a number of objects that were read
 * @param a_with_holes if true - read any records, if false - only actual records
 * @return pass - a pointer to the object, error - NULL.
 */
static dap_store_obj_t* s_db_pgsql_read_cond_store_obj(const char *a_group, dap_global_db_driver_hash_t a_hash_from, size_t *a_count_out, bool a_with_holes)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// func work
    dap_store_obj_t *l_ret = NULL;
    char *l_query_str = dap_strdup_printf("SELECT * FROM \"%s\""
                                    " WHERE driver_key > $1 AND (flags & '%d' %s 0)"
                                    " ORDER BY driver_key LIMIT '%d';",
                                    a_group, DAP_GLOBAL_DB_RECORD_DEL,
                                    a_with_holes ? ">=" : "=",
                                    a_count_out && *a_count_out ? (int)*a_count_out : (int)DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT);
    if (!l_query_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }
    PGresult *l_query_res = s_db_pgsql_exec_tuples(l_conn->conn, l_query_str, &a_hash_from, __FUNCTION__);
    DAP_DELETE(l_query_str);
    
// memory alloc
    uint64_t l_count = PQntuples(l_query_res);
    l_count = a_count_out && *a_count_out ? dap_min(*a_count_out, l_count) : l_count;
    if (!l_count) {
        s_request_err_msg(__FUNCTION__);
        goto clean_and_ret;
    }
    if (!( l_ret = DAP_NEW_Z_COUNT(dap_store_obj_t, l_count + 1) )) {
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

static dap_store_obj_t *s_db_pgsql_read_store_obj_below_timestamp(const char *a_group, dap_nanotime_t a_timestamp, size_t *a_count_out)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// func work
    dap_store_obj_t *l_ret = NULL;
    char *l_query_str = dap_strdup_printf("SELECT * FROM \"%s\""
                                    " WHERE driver_key < $1"
                                    " ORDER BY driver_key;", a_group);
    if (!l_query_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }
    dap_global_db_driver_hash_t l_hash_from = { .bets = htobe64(a_timestamp), .becrc = (uint64_t)-1 };
    PGresult *l_query_res = s_db_pgsql_exec_tuples(l_conn->conn, l_query_str, &l_hash_from, __FUNCTION__);
    DAP_DELETE(l_query_str);
    
// memory alloc
    uint64_t l_count = PQntuples(l_query_res);
    if (!l_count) {
        s_request_err_msg(__FUNCTION__);
        goto clean_and_ret;
    }
    if (!( l_ret = DAP_NEW_Z_COUNT(dap_store_obj_t, l_count + 1) )) {
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
 * @brief Reads some objects from a PGSQL database by a_group, a_key.
 * @param a_group a group name string
 * @param a_key an object key string, if equals NULL reads the whole group
 * @param a_count_out[out] a number of objects that were read
 * @param a_with_holes if true - read any records, if false - only actual records
 * @return pass - a pointer to the object, error - NULL.
 */
static dap_store_obj_t* s_db_pgsql_read_store_obj(const char *a_group, const char *a_key, size_t *a_count_out, bool a_with_holes)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// func work
    dap_store_obj_t *l_ret = NULL;
    char *l_query_str = NULL;
    if (a_key) {
        l_query_str = dap_strdup_printf("SELECT * FROM \"%s\" WHERE key='%s' AND (flags & '%d' %s 0);", a_group, a_key, DAP_GLOBAL_DB_RECORD_DEL, a_with_holes ? ">=" : "=");
    } else { // no limit
        if (a_count_out && *a_count_out)
            l_query_str = dap_strdup_printf("SELECT * FROM \"%s\" WHERE flags & '%d' %s 0 ORDER BY driver_key LIMIT '%d';",
                                        a_group, DAP_GLOBAL_DB_RECORD_DEL,
                                        a_with_holes ? ">=" : "=",
                                        a_count_out && *a_count_out ? (int)*a_count_out : -1);
        else
            l_query_str = dap_strdup_printf("SELECT * FROM \"%s\" WHERE flags & '%d' %s 0 ORDER BY driver_key;",
                                        a_group, DAP_GLOBAL_DB_RECORD_DEL,
                                        a_with_holes ? ">=" : "=");
    }
    if (!l_query_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }
    PGresult *l_query_res = s_db_pgsql_exec_tuples(l_conn->conn, l_query_str, NULL, __FUNCTION__);
    DAP_DELETE(l_query_str);
    
// memory alloc
    uint64_t l_count = PQntuples(l_query_res);
    l_count = a_count_out && *a_count_out ? dap_min(*a_count_out, l_count) : l_count;
    if (!l_count) {
        s_request_err_msg(__FUNCTION__);
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
 * @brief Gets a list of group names by a_group_mask.
 * @param a_group_mask a group name mask
 * @return pass - a pointer to a list of group names, error - NULL.
 */
static dap_list_t *s_db_pgsql_get_groups_by_mask(const char *a_group_mask)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group_mask || !(l_conn = s_db_pgsql_get_connection(false)), NULL);
// preparing
    dap_list_t* l_ret = NULL;
    
    PGresult *l_res = s_db_pgsql_exec_tuples(
        l_conn->conn,
        "SELECT tablename FROM pg_catalog.pg_tables WHERE schemaname != 'information_schema' AND schemaname != 'pg_catalog'",
        NULL, __FUNCTION__);
    size_t l_count = PQntuples(l_res);
    for (size_t i = 0; i < l_count; ++i) {
        char *l_table_name = (char *)PQgetvalue(l_res, i, 0);
        if(dap_global_db_group_match_mask(l_table_name, a_group_mask))
            l_ret = dap_list_prepend(l_ret, dap_strdup(l_table_name));
    }
    PQclear(l_res);
    s_db_pgsql_free_connection(l_conn, false);
    return l_ret;
}

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
    char *l_query_count_str = dap_strdup_printf("SELECT COUNT(*) FROM \"%s\" "
                                        " WHERE driver_key > $1 AND (flags & '%d' %s 0);",
                                        a_group, DAP_GLOBAL_DB_RECORD_DEL,
                                        a_with_holes ? ">=" : "=");
    if (!l_query_count_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }
    PGresult *l_query_res = s_db_pgsql_exec_tuples(l_conn->conn, l_query_count_str, &a_hash_from, __FUNCTION__);
    DAP_DELETE(l_query_count_str);
    uint64_t *l_ret = (uint64_t *)PQgetvalue(l_query_res, 0, 0);
clean_and_ret:
    s_db_pgsql_free_connection(l_conn, false);
    PQclear(l_query_res);
    return l_ret ? be64toh(*l_ret) : 0;
}

/**
 * @brief Checks if an object is in a database by hash.
 * @param a_group a group name string
 * @param a_hash a object hash
 * @return Returns true if it is, false it's not.
 */
static bool s_db_pgsql_is_hash(const char *a_group, dap_global_db_driver_hash_t a_hash)
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), 0);
// preparing
    char *l_query_str = dap_strdup_printf("SELECT EXISTS(SELECT * FROM \"%s\" WHERE driver_key=$1);", a_group);
    if (!l_query_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }
    PGresult *l_query_res = s_db_pgsql_exec_tuples(l_conn->conn, l_query_str, &a_hash, __FUNCTION__);
    DAP_DELETE(l_query_str);
    char *l_ret = PQgetvalue(l_query_res, 0, 0);
clean_and_ret:
    s_db_pgsql_free_connection(l_conn, false);
    PQclear(l_query_res);
    return l_ret ? *l_ret : false;
}

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
    char *l_query_str = dap_strdup_printf("SELECT EXISTS(SELECT * FROM \"%s\" WHERE key='%s');", a_group, a_key);
    if (!l_query_str) {
        log_it(L_ERROR, "Error in PGSQL request forming");
        goto clean_and_ret;
    }
    PGresult *l_query_res = s_db_pgsql_exec_tuples(l_conn->conn, l_query_str, NULL, __FUNCTION__);
    DAP_DELETE(l_query_str);
    char *l_ret = PQgetvalue(l_query_res, 0, 0);
clean_and_ret:
    s_db_pgsql_free_connection(l_conn, false);
    PQclear(l_query_res);
    return l_ret ? *l_ret : false;
}

static size_t s_db_pgsql_read_size_store(const char *a_group, const char *a_key, bool a_with_holes)
{
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!a_group || !(l_conn = s_db_pgsql_get_connection(false)), 0);
    size_t l_ret_size = 0;
    PGresult *l_query_res = NULL;
    if (a_key) {
        char *l_query_str = dap_strdup_printf(
            "SELECT COALESCE(SUM(OCTET_LENGTH(key)),0) + COALESCE(SUM(LENGTH(value)),0) + COALESCE(SUM(LENGTH(sign)),0) FROM \"%s\""
            " WHERE key=$1 AND (flags & %d %s 0);",
            a_group, DAP_GLOBAL_DB_RECORD_DEL, a_with_holes ? ">=" : "=");
        if (!l_query_str)
            goto clean_and_ret;
        const char *l_param_vals[1] = { a_key };
        int l_param_lens[1] = { (int)dap_strlen(a_key) };
        int l_param_formats[1] = { 0 }; // text
        l_query_res = s_db_pgsql_exec(l_conn->conn, l_query_str, 1,
                                      l_param_vals, l_param_lens, l_param_formats,
                                      1, PGRES_TUPLES_OK, __FUNCTION__);
        DAP_DELETE(l_query_str);
    } else {
        char *l_group_reg = dap_strdup_printf("\"%s\"", a_group);
        if (!l_group_reg)
            goto clean_and_ret;
        const char *l_param_vals[1] = { l_group_reg };
        int l_param_lens[1] = { (int)dap_strlen(l_group_reg) };
        int l_param_formats[1] = { 0 };
        const char *l_query_str = "SELECT COALESCE(pg_total_relation_size(to_regclass($1)),0)";
        l_query_res = s_db_pgsql_exec(l_conn->conn, l_query_str, 1,
                                      l_param_vals, l_param_lens, l_param_formats,
                                      1, PGRES_TUPLES_OK, __FUNCTION__);
        DAP_DELETE(l_group_reg);
    }
    if (!l_query_res)
        goto clean_and_ret;
    if (PQntuples(l_query_res)) {
        if (PQgetlength(l_query_res, 0, 0) == 8) {
            int64_t *l_size_p = (int64_t *)PQgetvalue(l_query_res, 0, 0);
            if (l_size_p)
                l_ret_size = (size_t)be64toh(*l_size_p);
        }
    }
clean_and_ret:
    if (l_query_res)
        PQclear(l_query_res);
    s_db_pgsql_free_connection(l_conn, false);
    return l_ret_size;
}

/**
 * @brief Flushes a PGSQL database cahce to disk
 * @note The function closes and opens the database connection
 * @return pass - 0, error - other.
 */
static int s_db_pgsql_flush()
{
// sanity check
    conn_list_item_t *l_conn = s_db_pgsql_get_connection(false);
    dap_return_val_if_pass(!l_conn, -1);
// preparing
    log_it(L_DEBUG, "Start flush PGSQL data base.");
    int l_ret = 0;
    if ( !(l_ret = s_db_pgsql_exec_command(l_conn->conn, "CHECKPOINT", NULL, NULL, 0, NULL, "checkpint")) ) {
        l_ret = s_db_pgsql_exec_command(l_conn->conn, "VACUUM", NULL, NULL, 0, NULL, "vacuum");
    }

#ifndef _WIN32
    sync();
#endif
    s_db_pgsql_free_connection(l_conn, false);
    s_db_pgsql_free_connection(l_conn, true);
    return l_ret;
}

/**
 * @brief Starts a outstanding transaction in database.
 * @return pass - 0, error - other.
 */
static int s_db_pgsql_transaction_start()
{
// sanity check
    conn_list_item_t *l_conn = NULL;
    dap_return_val_if_pass(!(l_conn = s_db_pgsql_get_connection(true)), 0);
// func work
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "Start TX: @%p", l_conn->conn);
    
    int l_ret = s_db_pgsql_exec_command(l_conn->conn, "BEGIN", NULL, NULL, 0, NULL, "begin");
    if ( l_ret ) {
        s_db_pgsql_free_connection(l_conn, true);
    }
    return  l_ret;
}

/**
 * @brief Ends a outstanding transaction in database.
 * @return pass - 0, error - other.
 */
static int s_db_pgsql_transaction_end(bool a_commit)
{
// sanity check
    dap_return_val_if_pass_err(!s_conn || !s_conn->conn, -1, "Outstanding connection not exist");
// func work
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "End TX l_conn: @%p", s_conn->conn);
    int l_ret = 0;
    if (a_commit)
        l_ret = s_db_pgsql_exec_command(s_conn->conn, "COMMIT", NULL, NULL, 0, NULL, "commit");
    else
        l_ret = s_db_pgsql_exec_command(s_conn->conn, "ROLLBACK", NULL, NULL, 0, NULL, "rollback");
    s_db_pgsql_free_connection(s_conn, true);
    return  l_ret;
}

/**
 * @brief Initializes a PGSQL database.
 * @note no thread safe
 * @param a_filename_db a path to the database file
 * @param a_drv_callback a pointer to a structure of callback functions
 * @return pass - 0, error - other.
 */
int dap_global_db_driver_pgsql_init(const char *a_db_conn_info, dap_global_db_driver_callbacks_t *a_drv_callback)
{
// sanity check
    dap_return_val_if_pass(!a_db_conn_info, -1);
    dap_return_val_if_pass_err(s_db_inited, -2, "PGSQL driver already init")
// func work
    log_it(L_INFO, "PGSQL will use second conn info: %s", a_db_conn_info);
    dap_strncpy(s_db_conn_info, a_db_conn_info, MAX_PATH);
    PGconn *l_base_conn = PQconnectdb(s_db_conn_info);
    if (PQstatus(l_base_conn) != CONNECTION_OK) {
        log_it(L_ERROR, "Can't create base connection to PGSQL db: \"%s\"", PQerrorMessage(l_base_conn));
        return -3;
    }
    PQfinish(l_base_conn);

    a_drv_callback->apply_store_obj         = s_db_pgsql_apply_store_obj;
    a_drv_callback->read_store_obj          = s_db_pgsql_read_store_obj;
    a_drv_callback->read_cond_store_obj     = s_db_pgsql_read_cond_store_obj;
    a_drv_callback->read_last_store_obj     = s_db_pgsql_read_last_store_obj;
    a_drv_callback->read_store_obj_by_timestamp = s_db_pgsql_read_store_obj_below_timestamp;
    // a_drv_callback->transaction_start       = s_db_pgsql_transaction_start;
    // a_drv_callback->transaction_end         = s_db_pgsql_transaction_end;
    a_drv_callback->get_groups_by_mask      = s_db_pgsql_get_groups_by_mask;
    a_drv_callback->read_count_store        = s_db_pgsql_read_count_store;
    a_drv_callback->is_obj                  = s_db_pgsql_is_obj;
    a_drv_callback->deinit                  = s_db_pqsql_deinit;
    a_drv_callback->flush                   = s_db_pgsql_flush;
    a_drv_callback->get_by_hash             = s_db_pgsql_get_by_hash;
    a_drv_callback->read_hashes             = s_db_pgsql_read_hashes;
    a_drv_callback->is_hash                 = s_db_pgsql_is_hash;
    a_drv_callback->read_size_store         = s_db_pgsql_read_size_store;
    a_drv_callback->physical_size           = NULL;
    s_db_inited = true;

    return 0;
}