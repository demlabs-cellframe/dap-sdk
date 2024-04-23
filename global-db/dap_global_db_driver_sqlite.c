/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Sources         https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2019
 * All rights reserved.

 This file is part of CellFrame SDK the open source project

    CellFrame SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CellFrame SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any CellFrame SDK based project.  If not, see <http://www.gnu.org/licenses/>.
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
#include "dap_hash.h"
#include "dap_file_utils.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"

#define LOG_TAG "db_sqlite"
#define DAP_GLOBAL_DB_TYPE_CURRENT DAP_GLOBAL_DB_TYPE_SQLITE

typedef struct conn_pool_item {
    void    *flink;                                                 /* Forward link to next element in the simple list */
    sqlite3 *conn;                                                  /* SQLITE connection context itself */
    int     idx;                                                    /* Just index, no more */
    atomic_flag busy;                                                   /* "Context is busy" flag */
    atomic_ullong  usage;                                                  /* Usage counter */
} conn_pool_item_t;                                      /* Preallocate a storage for the SQLITE connections  */

static conn_pool_item_t *s_trans = NULL;                               /* SQL context of outstanding  transaction */
static pthread_mutex_t s_trans_mtx = PTHREAD_MUTEX_INITIALIZER;

extern  int g_dap_global_db_debug_more;                                     /* Enable extensible debug output */

static char s_filename_db [MAX_PATH];

static pthread_mutex_t s_conn_free_mtx = PTHREAD_MUTEX_INITIALIZER;         /* Lock to coordinate access to the free connections pool */
static pthread_cond_t s_conn_free_cnd = PTHREAD_COND_INITIALIZER;           /* To signaling to waites of the free connection */
static bool s_conn_free_present = true;

static pthread_mutex_t s_db_mtx = PTHREAD_MUTEX_INITIALIZER;

static uint32_t s_conn_count = 2;  // connection count
conn_pool_item_t *s_conn_pool = NULL;  // connection pool

static dap_store_obj_t* s_db_sqlite_read_cond_store_obj(const char *a_group, dap_global_db_driver_hash_t a_hash_from, size_t *a_count_out, bool a_with_holes);

// Value of one field in the table
typedef struct _sqlite_value_
{
    int32_t len,
            type;
    /*
     #define SQLITE_INTEGER  1
     #define SQLITE_FLOAT    2
     #define SQLITE_TEXT     3
     #define SQLITE_BLOB     4
     #define SQLITE_NULL     5
     */

    union {
        int         val_int;
        long long   val_int64;
        double      val_float;
        const char *val_str;
        const unsigned char *val_blob;
    } val;
} SQLITE_VALUE;

// Content of one row in the table
typedef struct _sqlite_row_value_
{
    uint32_t count; // number of columns in a row
    uint32_t reserv;
    SQLITE_VALUE *val; // array of field values
} SQLITE_ROW_VALUE;

/*
 * SQLite record structure
 */
struct DAP_ALIGN_PACKED driver_record {
    uint64_t        value_len;                                              /* Length of value part */
    uint8_t         flags;                                                  /* Flag of the record : see RECORD_FLAGS enums */
    uint32_t        crc;                                                    /* Object integrity */
    uint64_t        sign_len;                                               /* Size control */
    byte_t          value_n_sign[];                                         /* Serialized form */
};

/**
 * @brief Closes a SQLite database.
 *
 * @param l_db a pointer to an instance of SQLite database structure
 * @return (none)
 */
static inline void s_dap_db_driver_sqlite_close(sqlite3 *l_db)
{
    if(l_db)
        sqlite3_close(l_db);
}

static conn_pool_item_t *s_sqlite_test_free_connection(void)
{
    int l_rc;
    conn_pool_item_t *l_conn = s_conn_pool;
    /* Run over connection list, try to get a free connection */
    for (uint32_t j = s_conn_count; j--; l_conn++) {
        if ( !(l_rc = atomic_flag_test_and_set (&l_conn->busy)) ) {     /* Test-and-set ... */
                                                                        /* l_rc == 0 - so connection was free, */
                                                                        /* we got free connection, so get out */
            atomic_fetch_add(&l_conn->usage, 1);
            if (g_dap_global_db_debug_more )
                log_it(L_DEBUG, "Alloc l_conn: @%p/%p, usage: %llu", l_conn, l_conn->conn, l_conn->usage);
            return  l_conn;
        }
    }
    return NULL;
}

#define DAP_SQLITE_CONN_TIMEOUT 5   // sec

static conn_pool_item_t *s_sqlite_get_connection(void)
{
int     l_rc;
struct timespec tmo = {0};

    if ( (l_rc = pthread_mutex_lock(&s_db_mtx)) == EDEADLK )                /* Get the mutex */
        return s_trans;                                                     /* DEADLOCK is detected ? Return pointer to current transaction */
    else if ( l_rc )
        return  log_it(L_ERROR, "Cannot get free SQLITE connection, errno=%d", l_rc), NULL;

    pthread_mutex_unlock(&s_db_mtx);

    pthread_mutex_lock(&s_conn_free_mtx);
    conn_pool_item_t *l_conn = s_sqlite_test_free_connection();
    if (l_conn) {
        pthread_mutex_unlock(&s_conn_free_mtx);
        return l_conn;
    }

    log_it(L_INFO, "No free SQLITE connection, wait %d seconds ...", DAP_SQLITE_CONN_TIMEOUT);

    /* No free connection at the moment, so, prepare to wait a condition ... */

    clock_gettime(CLOCK_REALTIME, &tmo);
    tmo.tv_sec += DAP_SQLITE_CONN_TIMEOUT;
    s_conn_free_present = false;
    l_rc = 0;
    while (!s_conn_free_present && !l_rc)
        l_rc = pthread_cond_timedwait(&s_conn_free_cnd, &s_conn_free_mtx, &tmo);
    if (!l_rc)
        l_conn = s_sqlite_test_free_connection();
    pthread_mutex_unlock(&s_conn_free_mtx);

    if (l_rc)
        log_it(L_DEBUG, "pthread_cond_timedwait(), error=%d", l_rc);
    if (!l_conn)
        log_it(L_ERROR, "No free SQLITE connection");
    return l_conn;
}

static inline int s_sqlite_free_connection(conn_pool_item_t *a_conn)
{
int     l_rc;

    if (g_dap_global_db_debug_more)
        log_it(L_DEBUG, "Free  l_conn: @%p/%p, usage: %llu", a_conn, a_conn->conn, a_conn->usage);

    atomic_flag_clear(&a_conn->busy);                                       /* Clear busy flag */

    l_rc = pthread_mutex_lock (&s_conn_free_mtx);                           /* Send a signal to waiters to wake-up */
    s_conn_free_present = true;
    l_rc = pthread_cond_signal(&s_conn_free_cnd);
    l_rc = pthread_mutex_unlock(&s_conn_free_mtx);
#ifndef DAP_DEBUG
    UNUSED(l_rc);
#endif
    return  0;
}

/**
 * @brief Deinitializes a SQLite database.
 *
 * @return Returns 0 if successful.
 */
int s_db_sqlite_deinit(void)
{
        pthread_mutex_lock(&s_db_mtx);
        for (int i = 0; i < s_conn_pool; i++) {
            if (s_conn_pool[i].conn) {
                s_dap_db_driver_sqlite_close(s_conn_pool[i].conn);
                atomic_flag_clear (&s_conn_pool[i].busy);
            }
        }
        pthread_mutex_unlock(&s_db_mtx);
        //s_db = NULL;
        return sqlite3_shutdown();
}

/**
 * @brief Opens a SQLite database and adds byte_to_bin function.
 *
 * @param a_filename_utf8 a SQLite database file name
 * @param a_flags database access flags (SQLITE_OPEN_READONLY, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)
 * @param a_error_message[out] an error message that's received from the SQLite database
 * @return Returns a pointer to an instance of SQLite database structure.
 */
sqlite3* dap_db_driver_sqlite_open(const char *a_filename_utf8, int a_flags, char **a_error_message)
{
    sqlite3 *l_db = NULL;

    int l_rc = sqlite3_open_v2(a_filename_utf8, &l_db, a_flags, NULL); // SQLITE_OPEN_FULLMUTEX by default set with sqlite3_config SERIALIZED
    // if unable to open the database file
    if(l_rc == SQLITE_CANTOPEN) {
        log_it(L_WARNING,"No database on path %s, creating one from scratch", a_filename_utf8);
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
 * @brief Executes SQL statements.
 *
 * @param a_db a pointer to an instance of SQLite database structure
 * @param l_query the SQL statement
 * @param l_error_message[out] an error message that's received from the SQLite database
 * @return Returns 0 if successful.
 */
static int s_db_driver_sqlite_exec(sqlite3 *a_db, const char *a_query, dap_global_db_driver_hash_t *a_hash, byte_t *a_value, size_t a_value_len)
{
    int l_rc = 0;
    sqlite3_stmt *l_stmt = NULL;
    
    if (
        (l_rc = sqlite3_prepare_v2(a_db, a_query, -1, &l_stmt, NULL)) != SQLITE_OK ||
        (a_hash && (l_rc = sqlite3_bind_blob64(l_stmt, 1, a_hash, sizeof(*a_hash), SQLITE_STATIC) != SQLITE_OK)) ||
        (a_value && (l_rc = sqlite3_bind_blob64(l_stmt, 2, a_value, a_value_len, SQLITE_STATIC)) != SQLITE_OK)
        ) {
        log_it(L_ERROR, "SQL error %d(%s)", sqlite3_errcode(a_db), sqlite3_errmsg(a_db));
        return l_rc;
    }
    for ( int i = 7; i--; ) {
        l_rc = sqlite3_step(l_stmt);
        if (l_rc != SQLITE_BUSY)
            break;
        if (g_dap_global_db_debug_more)
            log_it(L_WARNING, "SQL error: %d(%s), sqlite step retry for %s",
                                sqlite3_errcode(a_db), sqlite3_errmsg(a_db), a_query);
        dap_usleep(500 * 1000);                                             /* Wait 0.5 sec */
    }
    sqlite3_finalize(l_stmt);
    if (l_rc != SQLITE_DONE && l_rc != SQLITE_ROW) {
        log_it(L_ERROR, "SQL error %d(%s)", sqlite3_errcode(a_db), sqlite3_errmsg(a_db));
        return l_rc;
    }
    return SQLITE_OK;
}

/**
 * @brief Creates a table and unique index in the s_db database.
 *
 * @param a_table_name a table name string
 * @return Returns 0 if successful, otherwise -1.
 */
static int s_db_driver_sqlite_create_group_table(const char *a_table_name)
{
int l_rc;
conn_pool_item_t *l_conn;
char l_query[512];

    if( !a_table_name )
        return  -EINVAL;

    if ( !(l_conn = s_sqlite_get_connection()) )
        return log_it(L_ERROR, "Error create group table '%s'", a_table_name), -ENOENT;

    snprintf(l_query, sizeof(l_query) - 1,
                    "CREATE TABLE IF NOT EXISTS '%s'(driver_key BLOB UNIQUE NOT NULL PRIMARY KEY, key TEXT UNIQUE NOT NULL, value BLOB)",
                    a_table_name);

    if ( (l_rc = s_db_driver_sqlite_exec(l_conn->conn, l_query, NULL, NULL, 0)) != SQLITE_OK ) {
        s_sqlite_free_connection(l_conn);
        return -1;
    }

    s_sqlite_free_connection(l_conn);
    return 0;
}

/**
 * @brief Releases memory allocated for a row.
 *
 * @param row a database row
 * @return (none)
 */
static void s_dap_db_driver_sqlite_row_free(SQLITE_ROW_VALUE *row)
{
    if(row) {
        // delete the whole string
        sqlite3_free(row->val);
        // delete structure
        sqlite3_free(row);
    }
}


/**
 * @brief Fetches a result values from a query to a_row_out
 *
 * @param l_res a pointer to a prepared statement structure
 * @param a_row_out a pointer to a pointer to a row structure
 * @return Returns SQLITE_ROW(100) or SQLITE_DONE(101) or SQLITE_BUSY(5)
 */
static int s_db_driver_sqlite_fetch_array(sqlite3_stmt *l_res, SQLITE_ROW_VALUE **a_row_out)
{
    SQLITE_ROW_VALUE *l_row = NULL;
    // go to next the string
    int l_rc = sqlite3_step(l_res);
    if(l_rc == SQLITE_ROW) {  // SQLITE_ROW(100) or SQLITE_DONE(101) or SQLITE_BUSY(5)
        // allocate memory for a row with data
        l_row = (SQLITE_ROW_VALUE*) sqlite3_malloc(sizeof(SQLITE_ROW_VALUE));
        int l_count = sqlite3_column_count(l_res); // get the number of columns
        // allocate memory for all columns
        l_row->val = (SQLITE_VALUE*) sqlite3_malloc(l_count * (int)sizeof(SQLITE_VALUE));
        if(l_row->val) {
            l_row->count = l_count; // number of columns
            for(uint32_t l_iCol = 0; l_iCol < l_row->count; l_iCol++) {
                SQLITE_VALUE *cur_val = l_row->val + l_iCol;
                cur_val->len = sqlite3_column_bytes(l_res, l_iCol); // how many bytes will be needed
                cur_val->type = sqlite3_column_type(l_res, l_iCol); // field type
                if(cur_val->type == SQLITE_INTEGER)
                    cur_val->val.val_int64 = sqlite3_column_int64(l_res, l_iCol);
                else if(cur_val->type == SQLITE_FLOAT)
                    cur_val->val.val_float = sqlite3_column_double(l_res, l_iCol);
                else if(cur_val->type == SQLITE_BLOB)
                    cur_val->val.val_blob = (const unsigned char*) sqlite3_column_blob(l_res, l_iCol);
                else if(cur_val->type == SQLITE_TEXT)
                    cur_val->val.val_str = (const char*) sqlite3_column_text(l_res, l_iCol);
                else
                    cur_val->val.val_str = NULL;
            }
        }
        else
            l_row->count = 0; // number of columns
    }
    if(a_row_out)
        *a_row_out = l_row;
    else
        s_dap_db_driver_sqlite_row_free(l_row);
    return l_rc;
}

/**
 * @brief Executes a VACUUM statement in a database.
 *
 * @param a_db a a pointer to an instance of SQLite database structure
 * @return Returns 0 if successful.
 */
int s_dap_db_driver_sqlite_vacuum(sqlite3 *a_db)
{
    if(!a_db)
        return -1;

    return  s_db_driver_sqlite_exec(a_db, "VACUUM", NULL, NULL, 0);
}

/**
 * @brief Starts a transaction in s_db database.
 *
 * @return Returns 0 if successful, otherwise -1.
 */
static int s_db_sqlite_start_transaction(void)
{
int l_rc;

    /* Try to lock */
    if ( EDEADLK == (l_rc = pthread_mutex_lock(&s_trans_mtx)) ) {
        /* DEADLOCK ?! - so transaction is already active ... */
        log_it(L_DEBUG, "Active TX l_conn: @%p/%p", s_trans, s_trans->conn);
        return  0;
    }

    if ( ! (s_trans = s_sqlite_get_connection()) )
        return  -666;

    if ( g_dap_global_db_debug_more )
        log_it(L_DEBUG, "Start TX l_conn: @%p/%p", s_trans, s_trans->conn);

    pthread_mutex_lock(&s_db_mtx);
    l_rc = s_db_driver_sqlite_exec(s_trans->conn, "BEGIN", NULL, NULL, 0);
    pthread_mutex_unlock(&s_db_mtx);

    if ( l_rc != SQLITE_OK ) {
        pthread_mutex_unlock(&s_trans_mtx);
        s_sqlite_free_connection(s_trans);
        s_trans = NULL;
        }

    return  ( l_rc == SQLITE_OK ) ? 0 : -l_rc;
}

/**
 * @brief Ends a transaction in s_db database.
 *
 * @return Returns 0 if successful, otherwise -1.
 */
static int s_db_sqlite_end_transaction(void)
{
int l_rc;
conn_pool_item_t *l_conn;

    if ( !s_trans)
        return  log_it(L_ERROR, "No active TX!"), -666;

    l_conn = s_trans;
    s_trans = NULL;                                                         /* Zeroing current TX's context until
                                                                              it's protected by the mutex ! */

    if ( g_dap_global_db_debug_more )
        log_it(L_DEBUG, "End TX l_conn: @%p/%p", l_conn, l_conn->conn);

    pthread_mutex_unlock(&s_trans_mtx);                                     /* Free TX context to other ... */

    pthread_mutex_lock(&s_db_mtx);
    l_rc = s_db_driver_sqlite_exec(l_conn->conn, "COMMIT", NULL, NULL, 0);
    pthread_mutex_unlock(&s_db_mtx);

    s_sqlite_free_connection(l_conn);

    return  ( l_rc == SQLITE_OK ) ? 0 : -l_rc;
}


/**
 * @brief Replaces '_' char with '.' char in a_table_name.
 *
 * @param a_table_name a table name string
 * @return Returns a group name string with the replaced character
 */
static inline char *s_sqlite_make_group_name(const char *a_table_name)
{
    char *l_table_name = dap_strdup(a_table_name), *l_str;

    for ( l_str = l_table_name; (l_str = strchr(l_str, '_')); l_str++)
        *l_str = '.';

    return l_table_name;
}

/**
 * @brief Replaces '.' char with '_' char in a_group_name.
 *
 * @param a_group_name a group name string
 * @return Returns a table name string with the replaced character
 */
static inline char *s_sqlite_make_table_name(const char *a_group_name)
{
    char *l_group_name = dap_strdup(a_group_name), *l_str;

    for ( l_str = l_group_name; (l_str = strchr(l_str, '.')); l_str++)
        *l_str = '_';

    return l_group_name;
}

/**
 * @brief Applies an object to a database.
 * @param a_store_obj a pointer to the object structure
 * @return Returns 0 if successful.
 */
int s_db_sqlite_apply_store_obj(dap_store_obj_t *a_store_obj)
{
// sanity check
    dap_return_val_if_pass(!a_store_obj || !a_store_obj->group, -1);
// func work
    // execute request
    conn_pool_item_t *l_conn = s_sqlite_get_connection();
    if (!l_conn)
        return -2;

    int l_ret = 0;
    char *l_query = NULL;
    size_t l_record_len = 0;
    struct driver_record *l_record;
    char *l_table_name = s_sqlite_make_table_name(a_store_obj->group);
    uint8_t l_type_erase = a_store_obj->flags & DAP_GLOBAL_DB_RECORD_ERASE;
    if (!l_type_erase) {
        if (!a_store_obj->key) {
            log_it(L_ERROR, "Global DB store object unsigned");
            l_ret = -3;
            goto ret_n_free;
        } else { //add one record
            l_query = sqlite3_mprintf("INSERT OR REPLACE INTO '%s' VALUES(?, '%s', ?)",
                                                  l_table_name, a_store_obj->key);
            /* Compute a length of the area to keep record */
            l_record_len = sizeof(struct driver_record) + a_store_obj->value_len + dap_sign_get_size(a_store_obj->sign);
            l_record = DAP_NEW_Z_SIZE(struct driver_record, l_record_len);
            if (!l_record) {
                log_it(L_ERROR, "Cannot allocate memory for new records, %zu octets, errno=%d", l_record_len, errno);
                l_ret = -4;
                goto ret_n_free;
            }
            l_record->value_len = a_store_obj->value_len;
            // Don't save NEW attribute
            l_record->flags = a_store_obj->flags & ~DAP_GLOBAL_DB_RECORD_NEW;
            if (!a_store_obj->crc) {
                log_it(L_ERROR, "Global DB store object corrupted");
                l_ret = -5;
            }
            l_record->crc = a_store_obj->crc;
            if (!a_store_obj->sign) {
                log_it(L_ERROR, "Global DB store object unsigned");
                l_ret = -6;
                goto ret_n_free;
            }
            l_record->sign_len = dap_sign_get_size(a_store_obj->sign);
            if (!l_record->sign_len) {
                log_it(L_ERROR, "Global DB store object sign corrupted");
                l_ret = -7;
                goto ret_n_free;
            }
            if (a_store_obj->value_len) /* Put <value> into the record */
                memcpy(l_record->value_n_sign, a_store_obj->value, a_store_obj->value_len);
            /* Put the authorization sign */
            memcpy(l_record->value_n_sign + a_store_obj->value_len, a_store_obj->sign, l_record->sign_len);
        }
        dap_global_db_driver_hash_t l_driver_key = dap_global_db_driver_hash_get(a_store_obj);
        l_ret = s_db_driver_sqlite_exec(l_conn->conn, l_query, &l_driver_key, (byte_t *)l_record, l_record_len);
        if (l_ret == SQLITE_ERROR) {
            // create table
            s_db_driver_sqlite_create_group_table(l_table_name);
            // repeat request
            l_ret = s_db_driver_sqlite_exec(l_conn->conn, l_query, &l_driver_key, (byte_t *)l_record, l_record_len);
        }
        //s_db_sqlite_read_cond_store_obj(a_store_obj->group, (dap_global_db_driver_hash_t){0}, NULL, false);
    } else {
        if (a_store_obj->key) //delete one record
            l_query = sqlite3_mprintf("DELETE FROM '%s' WHERE key = '%s'", l_table_name, a_store_obj->key);
        else // remove all group
            l_query = sqlite3_mprintf("DROP TABLE IF EXISTS '%s'", l_table_name);
        l_ret = s_db_driver_sqlite_exec(l_conn->conn, l_query, NULL, NULL, 0);
    }
ret_n_free:
    DAP_DEL_Z(l_record);
    s_sqlite_free_connection(l_conn);
    if (l_query)
        sqlite3_free(l_query);
    DAP_DELETE(l_table_name);
    return l_ret;
}

/**
 * @brief Fills a object from a row
 *
 * @param a_group a group name string
 * @param a_obj a pointer to the object
 * @param a_row a ponter to the row structure
 */
static void s_fill_one_item(const char *a_group, dap_store_obj_t *a_obj, SQLITE_ROW_VALUE *a_row)
{
    if(!a_obj){
        log_it(L_ERROR, "Object is not initialized, can't call fill_one_item");
        return;
    }
    a_obj->group = dap_strdup(a_group);

    for(uint32_t l_iCol = 0; l_iCol < a_row->count; l_iCol++) {
        SQLITE_VALUE *l_cur_val = a_row->val + l_iCol;
        switch (l_iCol) {
        case 0:
            if(l_cur_val->type == SQLITE_BLOB)
                a_obj->timestamp = be64toh(((dap_global_db_driver_hash_t *)l_cur_val->val.val_blob)->bets);
            break; // ts
        case 1:
            if(l_cur_val->type == SQLITE_TEXT)
                a_obj->key = dap_strdup(l_cur_val->val.val_str);
            break; // key
        case 2:
            if(l_cur_val->type == SQLITE_BLOB) {
                struct driver_record *l_record = l_cur_val->val.val_blob;
                if ((uint64_t)l_cur_val->len < sizeof(*l_record) || // Do not intersct bounds of readed array, check it twice
                        (uint64_t)l_cur_val->len < sizeof(*l_record) + l_record->sign_len + l_record->value_len ||
                        l_record->sign_len == 0) {
                    log_it(L_ERROR, "Corrupted global DB record internal value");
                    break;
                }
                a_obj->value_len = l_record->value_len;
                a_obj->flags = l_record->flags;
                a_obj->crc = l_record->crc;
                if (a_obj->value_len &&
                        !(a_obj->value = DAP_DUP_SIZE(l_record->value_n_sign, a_obj->value_len))) {
                    DAP_DELETE(a_obj->group);
                    DAP_DELETE(a_obj->key);
                    log_it(L_CRITICAL, "Cannot allocate a memory for store object value");
                    break;
                }
                dap_sign_t *l_sign = (dap_sign_t *)(l_record->value_n_sign + l_record->value_len);
                if (dap_sign_get_size(l_sign) != l_record->sign_len ||
                        !(a_obj->sign = DAP_DUP_SIZE(l_sign, l_record->sign_len))) {
                    DAP_DELETE(a_obj->group);
                    DAP_DELETE(a_obj->key);
                    DAP_DEL_Z(a_obj->value);
                    if (dap_sign_get_size(l_sign) != l_record->sign_len)
                        log_it(L_ERROR, "Corrupted global DB record internal value");
                    else
                        log_it(L_CRITICAL, "Cannot allocate a memory for store object value");
                    break;
                }
                a_obj->value_len = (size_t) l_cur_val->len;
                a_obj->value = DAP_NEW_SIZE(uint8_t, a_obj->value_len);
                memcpy((byte_t *)a_obj->value, l_cur_val->val.val_blob, a_obj->value_len);
            }
            break; // value
        }
    }

}

/**
 * @brief Reads a last object from the s_db database.
 *
 * @param a_group a group name string
 * @return Returns a pointer to the object.
 */
dap_store_obj_t* s_db_sqlite_read_last_store_obj(const char *a_group)
{
dap_store_obj_t *l_obj = NULL;
sqlite3_stmt *l_res = NULL;
conn_pool_item_t *l_conn;

    if(!a_group)
        return NULL;

    if ( !(l_conn = s_sqlite_get_connection()) )
        return NULL;

    char * l_table_name = s_sqlite_make_table_name(a_group);
    char *l_str_query = sqlite3_mprintf("SELECT key,timestamp,value FROM '%s' ORDER BY timestamp DESC LIMIT 1", l_table_name);
    int l_ret = sqlite3_prepare_v2(l_conn->conn, l_str_query, -1, &l_res, NULL);
    sqlite3_free(l_str_query);
    DAP_DEL_Z(l_table_name);
    if (l_ret != SQLITE_OK) {
        log_it(L_ERROR, "SQLite read last obj error %d(%s)\n", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
        s_sqlite_free_connection(l_conn);
        return NULL;
    }

    SQLITE_ROW_VALUE *l_row = NULL;
    l_ret = s_db_driver_sqlite_fetch_array(l_res, &l_row);
    if(l_ret != SQLITE_ROW && l_ret != SQLITE_DONE)
    {
        //log_it(L_ERROR, "read l_ret=%d, %s\n", sqlite3_errcode(s_db), sqlite3_errmsg(s_db));
    }
    if(l_ret == SQLITE_ROW && l_row) {
        l_obj = DAP_NEW_Z(dap_store_obj_t);
        s_fill_one_item(a_group, l_obj, l_row);
    }
    s_dap_db_driver_sqlite_row_free(l_row);
    sqlite3_finalize(l_res);

    s_sqlite_free_connection(l_conn);

    return l_obj;
}

/**
 * @brief Reads some objects from a database by conditions
 *
 * @param a_group a group name string
 * @param a_iter iterator to looked for item
 * @param a_count_out[in] a number of objects to be read, if equals 0 reads with no limits
 * @param a_count_out[out] a number of objects that were read
 * @return If successful, a pointer to an objects, otherwise NULL.
 */
static dap_store_obj_t* s_db_sqlite_read_cond_store_obj(const char *a_group, dap_global_db_driver_hash_t a_hash_from, size_t *a_count_out, bool a_with_holes)
{
// sanity check
    conn_pool_item_t *l_conn = s_sqlite_get_connection();
    dap_return_val_if_pass(!a_group || !l_conn, NULL);
// preparing
    dap_store_obj_t *l_obj_ret = NULL;
    sqlite3_stmt *l_stmt_count = NULL, *l_stmt = NULL;
    char * l_table_name = s_sqlite_make_table_name(a_group);
    char *l_str_query_count = sqlite3_mprintf("SELECT COUNT(*) FROM '%s'"
                                        " WHERE driver_key > ?", l_table_name);
    char *l_str_query = sqlite3_mprintf("SELECT driver_key, key, value FROM '%s'"
                                        " WHERE driver_key > ? ORDER BY driver_key LIMIT %d",
                                                                        l_table_name, a_count_out ? (int)*a_count_out : -1);
    DAP_DEL_Z(l_table_name);
    if (!l_str_query_count || !l_str_query) {
        log_it(L_ERROR, "Error in SQL request forming");
        goto clean_and_ret;;
    }
    
    if(sqlite3_prepare_v2(l_conn->conn, l_str_query_count, -1, &l_stmt_count, NULL)!= SQLITE_OK ||
        sqlite3_bind_blob64(l_stmt_count, 1, &a_hash_from, sizeof(a_hash_from), SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_prepare_v2(l_conn->conn, l_str_query, -1, &l_stmt, NULL)!= SQLITE_OK ||
        sqlite3_bind_blob64(l_stmt, 1, &a_hash_from, sizeof(a_hash_from), SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_step(l_stmt_count) != SQLITE_ROW)
    {
        log_it(L_ERROR, "SQLite conditional read error %d(%s)", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
        goto clean_and_ret;;
    }
// memory alloc
    int64_t l_count = sqlite3_column_int64(l_stmt_count, 0);
    if (!l_count) {
        log_it(L_INFO, "There are no records satisfying the request");
        goto clean_and_ret;
    }
    DAP_NEW_Z_COUNT_RET_VAL(l_obj_ret, dap_store_obj_t, l_count, NULL, l_str_query_count, l_str_query);
// data forming
    SQLITE_ROW_VALUE *l_row = NULL;
    int l_count_out = 0;
    do {
        int l_ret_code = s_db_driver_sqlite_fetch_array(l_stmt, &l_row);
        if(l_ret_code != SQLITE_ROW && l_ret_code != SQLITE_DONE)
        {
           log_it(L_ERROR, "SQLite conditional read error %d(%s)", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
        }
        if(l_ret_code == SQLITE_ROW && l_row) {
            s_fill_one_item(a_group, l_obj_ret + l_count_out, l_row);
            l_count_out++;
        }
        s_dap_db_driver_sqlite_row_free(l_row);
    } while(l_row && --l_count);
    if (a_count_out)
        *a_count_out = (size_t)l_count_out;
clean_and_ret:
    sqlite3_finalize(l_stmt_count);
    sqlite3_finalize(l_stmt);
    s_sqlite_free_connection(l_conn);
    sqlite3_free(l_str_query_count);
    sqlite3_free(l_str_query);
    return l_obj_ret;
}

/**
 * @brief Reads some objects from a SQLite database by a_group, a_key.
 * @param a_group a group name string
 * @param a_key an object key string, if equals NULL reads the whole group
 * @param a_count_out[out] a number of objects that were read
 * @return If successful, a pointer to an objects, otherwise NULL.
 */
dap_store_obj_t* s_db_sqlite_read_store_obj(const char *a_group, const char *a_key, size_t *a_count_out)
{
// sanity check
    conn_pool_item_t *l_conn = s_sqlite_get_connection();
    dap_return_val_if_pass(!a_group || !l_conn, NULL)
// func work
    dap_store_obj_t *l_obj = NULL;
    sqlite3_stmt *l_res = NULL;
    char *l_table_name = s_sqlite_make_table_name(a_group);
    char *l_str_query = NULL;
    if (a_key)
        l_str_query = sqlite3_mprintf("SELECT driver_key, key, value FROM '%s' WHERE key='%s'", l_table_name, a_key);
    else // no limit
        l_str_query = sqlite3_mprintf("SELECT driver_key, key, value FROM '%s' ORDER BY driver_key", l_table_name);
    int l_ret = sqlite3_prepare_v2(l_conn->conn, l_str_query, -1, &l_res, NULL);
    sqlite3_free(l_str_query);
    DAP_DEL_Z(l_table_name);
    if (l_ret != SQLITE_OK) {
        log_it(L_ERROR, "SQLite read error %d(%s)", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
        s_sqlite_free_connection(l_conn);
        return NULL;
    }

    SQLITE_ROW_VALUE *l_row = NULL;
    size_t l_count_out = 0;
    uint64_t l_count_sized = 0;
    do {
        l_ret = s_db_driver_sqlite_fetch_array(l_res, &l_row);
        if (l_ret != SQLITE_ROW && l_ret != SQLITE_DONE) {
            log_it(L_ERROR, "SQLite read error array %d(%s)\n", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
            break;
        }
        if(l_ret == SQLITE_ROW && l_row) {
            // realloc memory
            if(l_count_out >= l_count_sized) {
                l_count_sized += 10;
                l_obj = DAP_REALLOC(l_obj, sizeof(dap_store_obj_t) * l_count_sized);
                if (!l_obj) {
                    log_it(L_CRITICAL, "%s", g_error_memory_alloc);
                    sqlite3_finalize(l_res);
                    s_sqlite_free_connection(l_conn);
                    s_dap_db_driver_sqlite_row_free(l_row);
                    return NULL;
                }
                memset(l_obj + l_count_out, 0, sizeof(dap_store_obj_t) * (l_count_sized - l_count_out));
            }
            // fill currrent item
            s_fill_one_item(a_group, l_obj + l_count_out, l_row);
            l_count_out++;
        }
        s_dap_db_driver_sqlite_row_free(l_row);
    } while(l_row);

    sqlite3_finalize(l_res);
    s_sqlite_free_connection(l_conn);

    if (a_count_out)
        *a_count_out = l_count_out;

    return l_obj;
}

/**
 * @brief Gets a list of group names from a s_db database by a_group_mask.
 *
 * @param a_group_mask a group name mask
 * @return Returns a pointer to a list of group names.
 */
dap_list_t* s_db_sqlite_get_groups_by_mask(const char *a_group_mask)
{
    conn_pool_item_t *l_conn = s_sqlite_get_connection();

    if(!a_group_mask || !l_conn)
        return NULL;

    sqlite3_stmt *l_res = NULL;
    const char *l_str_query = "SELECT name FROM sqlite_master WHERE type ='table' AND name NOT LIKE 'sqlite_%'";
    int l_ret = sqlite3_prepare_v2(l_conn->conn, l_str_query, -1, &l_res, NULL);
    if (l_ret != SQLITE_OK) {
        log_it(L_ERROR, "SQLite get groups error %d(%s)\n", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
        s_sqlite_free_connection(l_conn);
        return NULL;
    }
    char * l_mask = s_sqlite_make_table_name(a_group_mask);
    SQLITE_ROW_VALUE *l_row = NULL;
    dap_list_t *l_ret_list = NULL;
    while (s_db_driver_sqlite_fetch_array(l_res, &l_row) == SQLITE_ROW && l_row) {
        char *l_table_name = (char *)l_row->val->val.val_str;
        if(!dap_fnmatch(l_mask, l_table_name, 0))
            l_ret_list = dap_list_prepend(l_ret_list, s_sqlite_make_group_name(l_table_name));
        s_dap_db_driver_sqlite_row_free(l_row);
    }
    sqlite3_finalize(l_res);

    s_sqlite_free_connection(l_conn);

    return l_ret_list;
}

/**
 * @brief Reads a number of objects from a s_db database by a iterator
 *
 * @param a_group a group name string
 * @param a_id id starting from which the quantity is calculated
 * @return Returns a number of objects.
 */
size_t s_db_sqlite_read_count_store(const char *a_group, dap_nanotime_t a_timestamp)
{
    conn_pool_item_t *l_conn = s_sqlite_get_connection();

    dap_return_val_if_fail(l_conn && a_group, 0);

    sqlite3_stmt *l_res = NULL;
    char * l_table_name = s_sqlite_make_table_name(a_group);
    char *l_str_query = sqlite3_mprintf("SELECT COUNT(*) FROM '%s' WHERE timestamp > '%lld'", l_table_name, a_timestamp);
    int l_ret = sqlite3_prepare_v2(l_conn->conn, l_str_query, -1, &l_res, NULL);
    sqlite3_free(l_str_query);
    DAP_DEL_Z(l_table_name);
    if(l_ret != SQLITE_OK) {
        log_it(L_ERROR, "SQLite read count error %d(%s)\n", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
        s_sqlite_free_connection(l_conn);
        return 0;
    }
    size_t l_ret_val = 0;
    SQLITE_ROW_VALUE *l_row = NULL;
    if (s_db_driver_sqlite_fetch_array(l_res, &l_row) == SQLITE_ROW && l_row) {
        l_ret_val = (size_t)l_row->val->val.val_int64;
        s_dap_db_driver_sqlite_row_free(l_row);
    }
    sqlite3_finalize(l_res);

    s_sqlite_free_connection(l_conn);

    return l_ret_val;
}

/**
 * @brief Checks if an object is in a s_db database by a_group and a_key.
 *
 * @param a_group a group name string
 * @param a_key a object key string
 * @return Returns true if it is, false it's not.
 */
bool s_db_sqlite_is_obj(const char *a_group, const char *a_key)
{
    conn_pool_item_t *l_conn = s_sqlite_get_connection();

    if(!a_group || !l_conn)
        return false;

    sqlite3_stmt *l_res = NULL;
    char * l_table_name = s_sqlite_make_table_name(a_group);
    char *l_str_query = sqlite3_mprintf("SELECT EXISTS(SELECT * FROM '%s' WHERE key='%s')", l_table_name, a_key);
    int l_ret = sqlite3_prepare_v2(l_conn->conn, l_str_query, -1, &l_res, NULL);
    sqlite3_free(l_str_query);
    DAP_DEL_Z(l_table_name);
    if (l_ret != SQLITE_OK) {
        log_it(L_ERROR, "SQLite is obj error %d(%s)", sqlite3_errcode(l_conn->conn), sqlite3_errmsg(l_conn->conn));
        s_sqlite_free_connection(l_conn);
        return false;
    }
    bool l_ret_val = false;
    SQLITE_ROW_VALUE *l_row = NULL;
    if (s_db_driver_sqlite_fetch_array(l_res, &l_row) == SQLITE_ROW && l_row) {
        l_ret_val = (size_t)l_row->val->val.val_int64;
        s_dap_db_driver_sqlite_row_free(l_row);
    }
    sqlite3_finalize(l_res);

    s_sqlite_free_connection(l_conn);

    return l_ret_val;
}


/**
 * @brief Flushes a SQLite database cahce to disk.
 * @note The function closes and opens the database
 *
 * @return Returns 0 if successful.
 */
static int s_db_sqlite_flush()
{
    conn_pool_item_t *l_conn = s_sqlite_get_connection();

    char *l_error_message = NULL;

    log_it(L_DEBUG, "Start flush sqlite data base.");

    if(!(l_conn = s_sqlite_get_connection()) )
        return -666;

    s_dap_db_driver_sqlite_close(l_conn->conn);

    if ( !(l_conn->conn = dap_db_driver_sqlite_open(s_filename_db, SQLITE_OPEN_READWRITE, &l_error_message)) ) {
        log_it(L_ERROR, "Can't init sqlite err: \"%s\"", l_error_message? l_error_message: "UNKNOWN");
        sqlite3_free(l_error_message);
        return -3;
    }

#ifndef _WIN32
    sync();
#endif

    if(s_db_driver_sqlite_exec(l_conn->conn, "PRAGMA synchronous = NORMAL", NULL, NULL, 0)) // 0 | OFF | 1 | NORMAL | 2 | FULL
        log_it(L_WARNING, "Can't set new synchronous mode\n");
    if(s_db_driver_sqlite_exec(l_conn->conn, "PRAGMA journal_mode = OFF", NULL, NULL, 0)) // DELETE | TRUNCATE | PERSIST | MEMORY | WAL | OFF
        log_it(L_WARNING, "Can't set new journal mode\n");
    if(s_db_driver_sqlite_exec(l_conn->conn, "PRAGMA page_size = 1024", NULL, NULL, 0)) // DELETE | TRUNCATE | PERSIST | MEMORY | WAL | OFF
        log_it(L_WARNING, "Can't set page_size\n");

    return 0;
}




/**
 * @brief Initializes a SQLite database.
 * @note no thread safe
 * @param a_filename_db a path to the database file
 * @param a_drv_callback a pointer to a structure of callback functions
 * @return If successful returns 0, else a code < 0.
 */
int dap_db_driver_sqlite_init(const char *a_filename_db, dap_db_driver_callbacks_t *a_drv_callback)
{
// sanity check
    dap_return_val_if_pass(!a_filename_db, -1);
    dap_return_val_if_pass_err(s_conn_pool, -2, "SQLite driver already init")
// func work
    int l_ret = -1, l_errno = errno;
    sqlite3 *l_conn = NULL;
    char l_errbuf[255] = {0}, *l_error_message = NULL;


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
        l_errno = errno;

        if(!dap_dir_test(l_filename_dir)){
            strerror_r(l_errno,l_errbuf,sizeof(l_errbuf));
            log_it(L_ERROR, "Can't create directory, error code %d, error string \"%s\"", l_mkdir_ret, l_errbuf);
            DAP_DELETE(l_filename_dir);
            return -l_errno;
        }else
            log_it(L_NOTICE, "Directory created");
    }
    DAP_DEL_Z(l_filename_dir);

    s_conn_count += dap_events_thread_get_count();
    DAP_NEW_Z_COUNT_RET_VAL(s_conn_pool, conn_pool_item_t, s_conn_count, NULL, NULL)
    /* Create a pool of connection */
    for (int i = 0; i < (int)s_conn_count; ++i)
    {
        if ( !(l_conn = dap_db_driver_sqlite_open(a_filename_db, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE, &l_error_message)) )
        {
            log_it(L_ERROR, "Can't init SQL connection context #%d err: \"%s\"", i, l_error_message);
            sqlite3_free(l_error_message);
            l_ret = -4;
            for(int ii = i - 1; ii >= 0; --ii) {
                s_dap_db_driver_sqlite_close(s_conn_pool[ii].conn);
            }
            goto end;
        }

        s_conn_pool[i].conn = l_conn;
        s_conn_pool[i].idx = i;

        atomic_store(&s_conn_pool[i].usage, 0);

        log_it(L_DEBUG, "SQL connection context #%d is created @%p", i, l_conn);

        if(s_db_driver_sqlite_exec(l_conn, "PRAGMA synchronous = NORMAL", NULL, NULL, 0))
            log_it(L_ERROR, "can't set new synchronous mode\n");
        if(s_db_driver_sqlite_exec(l_conn, "PRAGMA journal_mode = OFF", NULL, NULL, 0))
            log_it(L_ERROR, "can't set new journal mode\n");
        if(s_db_driver_sqlite_exec(l_conn, "PRAGMA page_size = 4096", NULL, NULL, 0))
            log_it(L_ERROR, "can't set page_size\n");
    }

    // *PRAGMA page_size = bytes; // page size DB; it is reasonable to make it equal to the size of the disk cluster 4096
    // *PRAGMA cache_size = -kibibytes; // by default it is equal to 2000 pages of database
    //
    a_drv_callback->apply_store_obj         = s_db_sqlite_apply_store_obj;
    a_drv_callback->read_store_obj          = s_db_sqlite_read_store_obj;
    a_drv_callback->read_cond_store_obj     = s_db_sqlite_read_cond_store_obj;
    a_drv_callback->read_last_store_obj     = s_db_sqlite_read_last_store_obj;
    a_drv_callback->transaction_start       = s_db_sqlite_start_transaction;
    a_drv_callback->transaction_end         = s_db_sqlite_end_transaction;
    a_drv_callback->get_groups_by_mask      = s_db_sqlite_get_groups_by_mask;
    a_drv_callback->read_count_store        = s_db_sqlite_read_count_store;
    a_drv_callback->is_obj                  = s_db_sqlite_is_obj;
    a_drv_callback->deinit                  = s_db_sqlite_deinit;
    a_drv_callback->flush                   = s_db_sqlite_flush;

end:
    return l_ret;
}
