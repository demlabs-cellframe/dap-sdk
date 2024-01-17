/*
 * AUTHORS:
 * Ruslan R. (The BadAss SysMan) Laishev  <ruslan.laishev@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2022
 * All rights reserved.

 This file is part of DAP SDK the open source project

 DAP SDK is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 DAP is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.


    DESCRIPTION: A database driver module provide an interface to MDBX API.
        https://gitflic.ru/project/erthink/libmdbx
        TG group: @libmdbx


    MODIFICATION HISTORY:

          4-MAY-2022    RRL Developing for actual version of the LibMDBX

         12-MAY-2022    RRL Finished developing of preliminary version

         19-MAY-2022    RRL Added routines' decsriptions

           1-JUN-2022   RRL Introduced dap_assert()

          18-JUL-2022   RRL Fixed unitialized l_obj_arr in the s_db_mdbx_read_store_obj()
 */

#include <stddef.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <uthash.h>
#include <stdatomic.h>

#define _GNU_SOURCE

#include "dap_global_db.h"
#include "dap_config.h"
#include "dap_global_db_driver_mdbx.h"
#include "dap_hash.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_common.h"
#include "dap_sign.h"

#include "mdbx.h"                                                           /* LibMDBX API */
#define LOG_TAG "dap_global_db_mdbx"
#define DAP_GLOBAL_DB_TYPE_CURRENT DAP_GLOBAL_DB_TYPE_MDBX

/** Struct for a MDBX DB context */
typedef struct __db_ctx__ {
        size_t  namelen;                                                    /* Group name length */
        char name[DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX + 1];                   /* Group's name */
        MDBX_dbi    dbi;                                                    /* MDBX's internal context id */
        UT_hash_handle hh;
} dap_db_ctx_t;

/*
 * MDBX record structure
 */
struct DAP_ALIGN_PACKED driver_record {                                     /* Timestamp and CRC is the driver key of the record, packed in big-endian format */
    uint64_t        key_len;                                                /* Legth of global DB text key part */
    uint64_t        value_len;                                              /* Length of value part */
    uint64_t        sign_len;                                               /* Size control */
    uint8_t         flags;                                                  /* Flag of the record : see RECORD_FLAGS enums */
    byte_t          key_n_value_n_sign[];                                   /* Serialized form */
};

static pthread_mutex_t s_db_ctx_mutex = PTHREAD_MUTEX_INITIALIZER;          /* A mutex  for working with a DB context */
static dap_db_ctx_t *s_db_ctxs = NULL;                                      /* A hash table of <group/subDB/table> == <MDBX DB context> */
static pthread_rwlock_t s_db_ctxs_rwlock = PTHREAD_RWLOCK_INITIALIZER;      /* A read-write lock for working with a <s_db_ctxs>. */

static char s_db_path[MAX_PATH];                                            /* A root directory for the MDBX files */

/* Forward declarations of action routines */
static int              s_db_mdbx_deinit();
static int              s_db_mdbx_flush(void);
static int              s_db_mdbx_apply_store_obj (dap_store_obj_t *a_store_obj);
static dap_store_obj_t  *s_db_mdbx_read_last_store_obj(const char* a_group);
static bool             s_db_mdbx_is_obj(const char *a_group, const char *a_key);
static dap_store_obj_t  *s_db_mdbx_read_store_obj(const char *a_group, const char *a_key, size_t *a_count_out);
static dap_store_obj_t  *s_db_mdbx_read_cond_store_obj(dap_global_db_iter_t *a_iter, size_t *a_count_out, dap_nanotime_t a_timestamp);
static size_t           s_db_mdbx_read_count_store(const char *a_group, dap_nanotime_t a_timestamp);
static dap_list_t       *s_db_mdbx_get_groups_by_mask(const char *a_group_mask);
static int              s_db_mdbx_iter_create(dap_global_db_iter_t *a_iter);


static MDBX_env *s_mdbx_env;                                                /* MDBX's context area */
static char s_subdir [] = "";                                               /* Name of subdir for the MDBX's database files */

static char s_db_master_tbl [] = "MDBX$MASTER";                             /* A name of master table in the MDBX
                                                                              to keep and maintains application level information */
static MDBX_dbi s_db_master_dbi;                                            /* A handle of the MDBX' DBI of the master subDB */

/*
 *   DESCRIPTION: A kind of replacement of the C RTL assert()
 *
 *   INPUTS:
 *
 *   OUTPUTS:
 *
 *   RETURNS:
 *      NONE
 */
static inline void s_dap_assert_fail(int a_condition, const char *a_expr, const char *a_file, int a_line)
{
char    buf[255];
int     buflen;

    if ( a_condition )
        return;

    buflen = snprintf(buf, sizeof(buf), "\n[%s:%d] <%s> expresion return false\n", a_file, a_line, a_expr);
    write(STDOUT_FILENO, buf, buflen);
    abort();
}

#define dap_assert(expr)  s_dap_assert_fail( (bool) (expr), #expr, __FILE__, __LINE__)


#ifdef  DAP_SYS_DEBUG   /* cmake ../ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-DDAP_SYS_DEBUG=1" */
/*
 *  DESCRIPTION: Dump all records from the table . Is supposed to be used at debug time.
 *
 *  INPUTS:
 *      a_db_ctx:   DB context
 *
 *  OUTPUTS:
 *      NONE:
 *
 *  RETURNS:
 *      NONE
 */
static void s_db_dump (dap_db_ctx_t *a_db_ctx)
{
int l_rc;
MDBX_val    l_key_iov, l_data_iov;
MDBX_cursor *l_cursor;
char    l_buf[1024] = {0};

    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &a_db_ctx->txn)) )
        log_it(L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc));
    else if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_open(a_db_ctx->txn, a_db_ctx->dbi, &l_cursor)) )
        log_it(L_ERROR, "mdbx_cursor_open: (%d) %s", l_rc, mdbx_strerror(l_rc));
    else {
        while ( !(l_rc = mdbx_cursor_get (l_cursor, &l_key_iov, &l_data_iov, MDBX_NEXT )) )
            {
            l_rc = dap_bin2hex (l_buf, l_data_iov.iov_base, dap_min(l_data_iov.iov_len, 72) );

            debug_if(g_dap_global_db_debug_more, L_DEBUG, "[0:%zu]: '%.*s' = [0:%zu]: '%.*s'",
                    l_key_iov.iov_len, (int) l_key_iov.iov_len, (char *) l_key_iov.iov_base,
                    l_data_iov.iov_len, l_rc, l_buf);
            }
    }

    if (l_cursor)
        mdbx_cursor_close(l_cursor);

    if (a_db_ctx->txn)
        mdbx_txn_abort(a_db_ctx->txn);
}
#endif     /* DAP_SYS_DEBUG */

/*
 *   DESCRIPTION: Open or create (if a_flag=MDBX_CREATE) a DB context for a given group.
 *      Initialize an MDBX's internal context for the subDB (== a_group);
 *      Add new group/table name into the special MDBX subDB named MDBX$MASTER.
 *
 *   INPUTS:
 *      a_group:    A group name (in terms of MDBX it's subDB), ASCIZ
 *      a_flag:     A flag
 *
 *   IMPLICITE OUTPUTS:
 *
 *      s_db_ctxs:  Add new DB context into the hash table
 *
 *   OUTPUTS:
 *      NONE
 *
 *   RETURNS:
 *      A has been created DB Context
 *      NULL in case of error
 *
 */
static dap_db_ctx_t *s_cre_db_ctx_for_group(const char *a_group, int a_flags)
{
int l_rc;
dap_db_ctx_t *l_db_ctx, *l_db_ctx2;
size_t l_name_len;
MDBX_val    l_key_iov, l_data_iov;

    debug_if(g_dap_global_db_debug_more, L_DEBUG, "Init group/table '%s', flags: %#x ...", a_group, a_flags);


    dap_assert( !pthread_rwlock_rdlock(&s_db_ctxs_rwlock) );                /* Get RD lock for lookup only */
    HASH_FIND_STR(s_db_ctxs, a_group, l_db_ctx);                            /* Is there exist context for the group ? */
    dap_assert( !pthread_rwlock_unlock(&s_db_ctxs_rwlock) );

    if ( l_db_ctx )                                                         /* Found! Good job - return DB context */
        return  log_it(L_INFO, "Found DB context: %p for group: '%s'", l_db_ctx, a_group), l_db_ctx;


    /* So , at this point we are going to create (if not exist)  'table' for new group */

    if ( (l_name_len = strlen(a_group)) >(int) DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX )                /* Check length of the group name */
        return  log_it(L_ERROR, "Group name '%s' is too long (%zu>%lu)", a_group, l_name_len, DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX), NULL;

    if ( !(l_db_ctx = DAP_NEW_Z(dap_db_ctx_t)) )                            /* Allocate zeroed memory for new DB context */
        return  log_it(L_ERROR, "Cannot allocate DB context for '%s', errno=%d", a_group, errno), NULL;

    memcpy(l_db_ctx->name,  a_group, l_db_ctx->namelen = l_name_len);             /* Store group name in the DB context */
    /*
    ** Start transaction, create table, commit.
    */
    MDBX_txn *l_txn;
    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, 0, &l_txn)) )
        return  log_it(L_CRITICAL, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), NULL;

    if  ( MDBX_SUCCESS != (l_rc = mdbx_dbi_open(l_txn, a_group, a_flags, &l_db_ctx->dbi)) )
        return  log_it(L_CRITICAL, "mdbx_dbi_open: (%d) %s", l_rc, mdbx_strerror(l_rc)), NULL;

    /*
     * Save new subDB name into the master table
     */
    l_data_iov.iov_base =  l_key_iov.iov_base = l_db_ctx->name;
    l_data_iov.iov_len = l_key_iov.iov_len = l_db_ctx->namelen + 1;    /* Count '\0' */

    if (MDBX_SUCCESS != (l_rc = mdbx_put(l_txn, s_db_master_dbi, &l_key_iov, &l_data_iov, MDBX_NOOVERWRITE))
         && (l_rc != MDBX_KEYEXIST)) {
        log_it (L_ERROR, "mdbx_put: (%d) %s", l_rc, mdbx_strerror(l_rc));
        if ( MDBX_SUCCESS != (l_rc = mdbx_txn_abort(l_txn)) )
            return  log_it(L_CRITICAL, "mdbx_txn_abort: (%d) %s", l_rc, mdbx_strerror(l_rc)), NULL;
    }

    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_commit(l_txn)) )
        return  log_it(L_CRITICAL, "mdbx_txn_commit: (%d) %s", l_rc, mdbx_strerror(l_rc)), NULL;

    /*
    ** Add new DB Context for the group into the hash for quick access
    */
    dap_assert( !pthread_rwlock_wrlock(&s_db_ctxs_rwlock) );                /* Get WR lock for the hash-table */

    l_db_ctx2 = NULL;
    HASH_FIND_STR(s_db_ctxs, a_group, l_db_ctx2);                           /* Check for existence of group again!!! */

    if ( !l_db_ctx2)                                                        /* Still not exist - fine, add new record */
        HASH_ADD_STR(s_db_ctxs, name, l_db_ctx);

    dap_assert( !pthread_rwlock_unlock(&s_db_ctxs_rwlock) );

    if ( l_db_ctx2 )                                                        /* Release unnecessary new context */
        DAP_DEL_Z(l_db_ctx);

    return l_db_ctx2 ? l_db_ctx2 : l_db_ctx;
}



/*
 *  DESCRIPTION: Action routine - cleanup this module's internal contexts, DB contexts hash table,
 *      close MDBX context. After call this routine any DB operation of this module is impossible.
 *      You must/can perfroms initialization.
 *
 *  INPUTS:
 *      NONE
 *
 *  OUTPUTS:
 *      NONE
 *
 *  RETURNS:
 *      0   - SUCCESS
 */

static  int s_db_mdbx_deinit(void)
{
    dap_db_ctx_t *l_db_ctx = NULL, *l_tmp;

    dap_assert ( !pthread_rwlock_wrlock(&s_db_ctxs_rwlock) );               /* Prelock for WR */
    HASH_ITER(hh, s_db_ctxs, l_db_ctx, l_tmp) {                             /* run over the hash table of the DB contexts */
        if (l_db_ctx->dbi)
            mdbx_dbi_close(s_mdbx_env, l_db_ctx->dbi);
        HASH_DEL(s_db_ctxs, l_db_ctx);                                      /* Delete DB context from the hash-table */
        DAP_DELETE(l_db_ctx);                                               /* Release memory of DB context area */
    }
    if (s_mdbx_env)
        mdbx_env_close(s_mdbx_env);                                         /* Finaly close MDBX DB */

    dap_assert ( !pthread_rwlock_unlock(&s_db_ctxs_rwlock) );

    return 0;
}

/*
 *  DESCRIPTION: Performs an initial module internal context creation and setup,
 *      Fill dispatch procedure table (a_drv_callback) by entries of this module;
 *      Create MDBX data files on the specified path, open MDBX context area;
 *      Load from the MDBX$MASTER table names of groups - create DB context
 *
 *      This is a first routine before any other calls of action routines in this module !!!
 *
 *
 *  INPUTS:
 *      a_mdbx_path:    A root directory for the MDBX database files
 *      a_drv_callback
 *
 *  IMPLICITE OUTPUTS:
 *      s_mdbx_env
 *
 *  RETURNS:
 *      0       - SUCCESS
 *      0>      - <errno>
 */

int     dap_db_driver_mdbx_init(const char *a_mdbx_path, dap_db_driver_callbacks_t *a_drv_dpt)
{
int l_rc;
MDBX_txn    *l_txn;
MDBX_cursor *l_cursor;
MDBX_val    l_key_iov, l_data_iov;
dap_list_t  *l_slist = NULL;
char        *l_cp;
size_t     l_upper_limit_of_db_size = 16;

    /*
     * [resources]
     * mdbx_upper_limit_of_db_size=32
     */
    l_upper_limit_of_db_size = dap_config_get_item_uint32_default ( g_config,  "global_db", "mdbx_upper_limit_of_db_size", l_upper_limit_of_db_size);
    l_upper_limit_of_db_size  *= 1024*1024*1024ULL;
    log_it(L_INFO, "Set MDBX Upper Limit of DB Size to %zu octets", l_upper_limit_of_db_size);

    snprintf(s_db_path, sizeof(s_db_path), "%s/%s", a_mdbx_path, s_subdir );/* Make a path to MDBX root */
    dap_mkdir_with_parents(s_db_path);                                      /* Create directory for the MDBX storage */

    log_it(L_NOTICE, "Directory '%s' will be used as an location for MDBX database files", s_db_path);
    s_mdbx_env = NULL;
    if ( MDBX_SUCCESS != (l_rc = mdbx_env_create(&s_mdbx_env)) )
        return  log_it(L_CRITICAL, "mdbx_env_create: (%d) %s", l_rc, mdbx_strerror(l_rc)), -ENOENT;

#if 0
    if ( g_dap_global_db_debug_more )
        mdbx_setup_debug	(	MDBX_LOG_VERBOSE, 0, 0);
#endif


    log_it(L_NOTICE, "Set maximum number of local groups: %lu", DAP_GLOBAL_DB_GROUPS_COUNT_MAX);
    dap_assert ( !(l_rc =  mdbx_env_set_maxdbs (s_mdbx_env, DAP_GLOBAL_DB_GROUPS_COUNT_MAX)) );/* Set maximum number of the file-tables (MDBX subDB)
                                                                              according to number of supported groups */

                                                                            /* We set "unlim" for all MDBX characteristics at the moment */

    if ( MDBX_SUCCESS != (l_rc = mdbx_env_set_geometry(s_mdbx_env, -1, -1, l_upper_limit_of_db_size, -1, -1, -1)) )
        return  log_it (L_CRITICAL, "mdbx_env_set_geometry (%s): (%d) %s", s_db_path, l_rc, mdbx_strerror(l_rc)),  -EINVAL;

    if ( MDBX_SUCCESS != (l_rc = mdbx_env_open(s_mdbx_env, s_db_path, MDBX_CREATE |  MDBX_COALESCE | MDBX_LIFORECLAIM, 0664)) )
        return  log_it (L_CRITICAL, "mdbx_env_open (%s): (%d) %s", s_db_path, l_rc, mdbx_strerror(l_rc)),  -EINVAL;

    /*
     * Since MDBX don't maintain a list of subDB with public API, we will use a "MASTER Table",
     * be advised that this MASTER teble is not maintained accurately!!!
     *
     * So, Create (If)/Open a master DB (table) to keep  list of subDBs (group/table/subDB name)
    */
    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, 0, &l_txn)) )
        return  log_it(L_CRITICAL, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), -EIO;

    if ( MDBX_SUCCESS != (l_rc = mdbx_dbi_open(l_txn, s_db_master_tbl, MDBX_CREATE, &s_db_master_dbi)) )
        return  log_it(L_CRITICAL, "mdbx_dbi_open: (%d) %s", l_rc, mdbx_strerror(l_rc)), -EIO;

    dap_assert ( MDBX_SUCCESS == (l_rc = mdbx_txn_commit (l_txn)) );

    /*
     * Run over records in the  MASTER table to get subDB names
     */
    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &l_txn)) )
        log_it(L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc));
    else if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_open(l_txn, s_db_master_dbi, &l_cursor)) )
        log_it(L_ERROR, "mdbx_cursor_open: (%d) %s", l_rc, mdbx_strerror(l_rc));
    else{
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "--- List of stored groups ---");

        for ( int i = 0;  !(l_rc = mdbx_cursor_get (l_cursor, &l_key_iov, &l_data_iov, MDBX_NEXT )); i++ )
            {
            debug_if(g_dap_global_db_debug_more, L_DEBUG, "MDBX SubDB #%03d [0:%zu]: '%.*s' = [0:%zu]: '%.*s'", i,
                    l_key_iov.iov_len, (int) l_key_iov.iov_len, (char *) l_key_iov.iov_base,
                    l_data_iov.iov_len, (int) l_data_iov.iov_len, (char *) l_data_iov.iov_base);

            /* Form a simple list of the group/table name to be used after */
            l_cp = dap_strdup(l_data_iov.iov_base);                         /* We expect an ASCIZ string as the table name */
            l_data_iov.iov_len = strlen(l_cp);
            l_slist = dap_list_append(l_slist, l_cp);
            }
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "--- End-Of-List  ---");
        }

    dap_assert ( MDBX_SUCCESS == mdbx_txn_commit (l_txn) );


    /* Run over the list and create/open group/tables and DB context ... */
    dap_list_t *l_el, *l_tmp;
    DL_FOREACH_SAFE(l_slist, l_el, l_tmp) {
        l_data_iov.iov_base = l_el->data;
        s_cre_db_ctx_for_group(l_data_iov.iov_base, MDBX_CREATE);
        DL_DELETE(l_slist, l_el);
        DAP_DELETE(l_el);
    }

    /*
    ** Fill the Driver Interface Table
    */
    a_drv_dpt->apply_store_obj     = s_db_mdbx_apply_store_obj;
    a_drv_dpt->read_last_store_obj = s_db_mdbx_read_last_store_obj;

    a_drv_dpt->read_store_obj      = s_db_mdbx_read_store_obj;
    a_drv_dpt->read_cond_store_obj = s_db_mdbx_read_cond_store_obj;
    a_drv_dpt->read_count_store    = s_db_mdbx_read_count_store;
    a_drv_dpt->get_groups_by_mask  = s_db_mdbx_get_groups_by_mask;
    a_drv_dpt->is_obj              = s_db_mdbx_is_obj;
    a_drv_dpt->deinit              = s_db_mdbx_deinit;
    a_drv_dpt->flush               = s_db_mdbx_flush;
    a_drv_dpt->iter_create         = s_db_mdbx_iter_create;

    /*
     * MDBX support transactions but on the current circuimstance we will not get
     * advantages of using DB Driver level BEGIN/END transactions
     */
    a_drv_dpt->transaction_start   = NULL;
    a_drv_dpt->transaction_end     = NULL;

    return MDBX_SUCCESS;
}

/*
 *  DESCRIPTION: Get a DB context for the specified group/table name
 *      from the DB context hash table. This context is just pointer to the DB Context
 *      structure, so don't modify it.
 *
 *  INPUTS:
 *      a_group:    Group/table name to be looked for DB context
 *
 *  OUTPUTS:
 *      NONE
 *
 *  RETURNS
 *      address of DB Context
 *      NULL    - no DB context has been craeted for the group
 *
 */
static  dap_db_ctx_t  *s_get_db_ctx_for_group(const char *a_group)
{
dap_db_ctx_t *l_db_ctx = NULL;

    dap_assert ( !pthread_rwlock_rdlock(&s_db_ctxs_rwlock) );
    HASH_FIND_STR(s_db_ctxs, a_group, l_db_ctx);
    dap_assert ( !pthread_rwlock_unlock(&s_db_ctxs_rwlock) );

    if ( !l_db_ctx )
        debug_if(g_dap_global_db_debug_more, L_WARNING, "No DB context for the group '%s'", a_group);

    return l_db_ctx;
}

/*
 *  DESCRIPTION: Action routine - perform flushing action. Actualy MDBX internaly maintain processes of the flushing
 *      and other things related to  data integrity.
 *
 *  INPUTS:
 *      NONE
 *
 *  OUTPUTS:
 *      NONE
 *
 *  RETURNS:
 *      0 - SUCCESS
 */
static  int s_db_mdbx_flush(void)
{
    return  log_it(L_DEBUG, "Flushing resident part of the MDBX to disk"), 0;
}

/**
 * @brief Create iterator with position on first element
 *
 * @param a_group a group name string
 * @return If successful, a pointer to an objects, otherwise NULL.
 */
static int s_db_mdbx_iter_create(dap_global_db_iter_t *a_iter)
{
    dap_return_val_if_pass(!a_iter || !a_iter->db_group, -1);                              /* Sanity check */
    MDBX_val *l_mdbx_iter = DAP_NEW_Z(MDBX_val);
    if (!l_mdbx_iter) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -2;
    }
    a_iter->db_type = DAP_GLOBAL_DB_TYPE_CURRENT;
    a_iter->db_iter = (void *)l_mdbx_iter;
    return 0;
}

/*
 *  DESCRIPTION: Action routine to read record from the table
 *
 *  INPUTS:
 *      a_group:    A group/table name to be looked in
 *      a_obj:      An address to the <store object> with the record
 *
 *  OUTPUTS:
 *      NONE
 *
 *  RETURNS:
 *      error code
 */
int s_fill_store_obj(const char *a_group, MDBX_val *a_key, MDBX_val *a_data, dap_store_obj_t *a_obj)
{
    dap_return_val_if_fail(a_group && a_key && a_data && a_obj, -1)
    /* Fill the <store obj> by data from the retrieved record */
    if (!dap_strlen(a_group))
        return log_it(L_ERROR, "Zero length of global DB group name"), -2;
    else
        a_obj->group = dap_strdup(a_group);
    if (!a_obj->group)
        return log_it(L_CRITICAL, "Cannot allocate a memory for store object group"), -3;

    if (a_key->iov_len != sizeof(dap_global_db_driver_hash_t)) {
        DAP_DELETE(a_obj->group);
        return log_it(L_ERROR, "Invalid length of global DB record key, expected %zu, got %zu",
                                                    sizeof(dap_global_db_driver_hash_t), a_key->iov_len), -4;
    }
    dap_global_db_driver_hash_t *l_driver_key = a_key->iov_base;
    a_obj->timestamp = be64toh(l_driver_key->bets);
    a_obj->crc = be64toh(l_driver_key->becrc);

    struct driver_record *l_record = a_data->iov_base;
    if (a_data->iov_len < sizeof(*l_record) || // Do not intersect bounds of read array, check it twice
            a_data->iov_len < sizeof(*l_record) + l_record->sign_len + l_record->value_len + l_record->key_len) {
        DAP_DELETE(a_obj->group);
        return log_it(L_ERROR, "Corrupted global DB record internal value"), -6;
    }
    if (!l_record->key_len) {
        DAP_DELETE(a_obj->group);
        return log_it(L_ERROR, "Ivalid driver record with zero text key length"), -9;
    }
    if ( (a_obj->key = DAP_NEW_SIZE(char, l_record->key_len)) )
        memcpy((char *)a_obj->key, l_record->key_n_value_n_sign, l_record->key_len);
    else {
        DAP_DELETE(a_obj->group);
        return log_it(L_CRITICAL, "Cannot allocate a memory for store object key"), -5;
    }
    a_obj->value_len = l_record->value_len;
    a_obj->flags = l_record->flags;
        if (a_obj->value_len &&
            !(a_obj->value = DAP_DUP_SIZE(l_record->key_n_value_n_sign + l_record->key_len, a_obj->value_len))) {
        DAP_DELETE(a_obj->group);
        DAP_DELETE(a_obj->key);
        return log_it(L_CRITICAL, "Cannot allocate a memory for store object value"), -7;
    }
    if (l_record->sign_len >= sizeof(dap_sign_t)) {
        dap_sign_t *l_sign = (dap_sign_t *)(l_record->key_n_value_n_sign + l_record->key_len + l_record->value_len);
        if (dap_sign_get_size(l_sign) != l_record->sign_len ||
                !(a_obj->sign = (dap_sign_t *)DAP_DUP_SIZE(l_sign, l_record->sign_len))) {
            DAP_DELETE(a_obj->group);
            DAP_DELETE(a_obj->key);
            DAP_DEL_Z(a_obj->value);
            if (dap_sign_get_size(l_sign) != l_record->sign_len)
                return log_it(L_ERROR, "Corrupted global DB record internal value"), -6;
            else
                return log_it(L_CRITICAL, "Cannot allocate a memory for store object value"), -8;
        }
    }
    return 0;
}

int s_get_obj_by_text_key(MDBX_txn *a_txn, MDBX_dbi a_dbi, MDBX_val *a_key, MDBX_val *a_data, const char *a_text_key)
{
    int l_rc = 0;
    MDBX_cursor *l_cursor = NULL;
    if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_open(a_txn, a_dbi, &l_cursor)) ) {
        log_it(L_ERROR, "mdbx_cursor_open: (%d) %s", l_rc, mdbx_strerror(l_rc));
        return l_rc;
    }
    if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_get(l_cursor, a_key, a_data, MDBX_FIRST)) ) {
        log_it(L_ERROR, "mdbx_cursor_get: (%d) %s", l_rc, mdbx_strerror(l_rc));
        return l_rc;
    }
    size_t l_key_len = strlen(a_text_key);
    do {
        struct driver_record *l_record = a_data->iov_base;
        if (l_key_len == l_record->key_len &&
                !memcmp(l_record->key_n_value_n_sign, a_text_key, l_key_len)) {
            mdbx_cursor_close(l_cursor);
            return MDBX_SUCCESS;
        }
    } while ( MDBX_SUCCESS == (l_rc = mdbx_cursor_get(l_cursor, a_key, a_data, MDBX_NEXT)) );
    mdbx_cursor_close(l_cursor);
    return MDBX_NOTFOUND;
}

/*
 *  DESCRIPTION: Action routine - lookup in the group/table a last stored record (with the bigest Id).
 *      We mainatain internaly <id> of record (it's just sequence),
 *      so actualy we need to performs a full scan of the table to reach a record with the bigest <id>.
 *      In case of success create and return <store_object> for the has been found records.
 *
 *  INPUTS:
 *      a_group:    A group/table name to be scanned
 *
 *  OUTPUTS:
 *      NONE
 *
 *  RETURNS:
 *      An address to the <store object> with the record
 *      NULL - table is empty
 */
dap_store_obj_t *s_db_mdbx_read_last_store_obj(const char* a_group)
{
int l_rc;
dap_db_ctx_t *l_db_ctx;
MDBX_val    l_key={0}, l_data={0};
MDBX_cursor *l_cursor = NULL;
dap_store_obj_t *l_obj;

     /* Sanity check for group/table */
    dap_return_val_if_fail(a_group, NULL);

    if ( !(l_db_ctx = s_get_db_ctx_for_group(a_group)) )
        return NULL;

    MDBX_txn *l_txn;
    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &l_txn)) )
        return log_it (L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), NULL;

    if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_open(l_txn, l_db_ctx->dbi, &l_cursor)) ) {
        mdbx_txn_commit(l_txn);
        log_it(L_ERROR, "mdbx_cursor_open: (%d) %s", l_rc, mdbx_strerror(l_rc));
        return NULL;
    }
    if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_get(l_cursor, &l_key, &l_data, MDBX_LAST)) ) {
        mdbx_txn_commit(l_txn);
        log_it(L_ERROR, "mdbx_cursor_get: (%d) %s", l_rc, mdbx_strerror(l_rc));
        return NULL;
    }

    if (l_cursor)                                                           // Release uncesessary MDBX cursor area,
        mdbx_cursor_close(l_cursor);                                        //but keep transaction !!!

    if (!l_key.iov_len || !l_data.iov_len) {                                /* Not found anything  - return NULL */
        mdbx_txn_commit(l_txn);
        return NULL;
    }
    /* Found ! Allocate memory for <store object>, <key> and <value> */
    if ( (l_obj = DAP_CALLOC(1, sizeof( dap_store_obj_t ))) ) {
        if (s_fill_store_obj(a_group, &l_key, &l_data, l_obj)) {
            l_rc = MDBX_PROBLEM;
            DAP_DEL_Z(l_obj);
        }
    } else {
        l_rc = MDBX_PROBLEM, log_it (L_ERROR, "Cannot allocate a memory for store object, errno=%d", errno);
    }
    mdbx_txn_commit(l_txn);

    return l_obj;
}

/*
 *  DESCRIPTION: An action routine to check a presence specified key in the group/table
 *
 *  INPUTS:
 *      a_group:    A group/table to looking in
 *      a_key:      A key of record to looked for
 *  OUTPUTS:
 *      NONE
 *  RETURNS
 *      1   -   SUCCESS, record is exist
 *      0   - Record-No-Found
 */
bool     s_db_mdbx_is_obj(const char *a_group, const char *a_key)
{
int l_rc, l_rc2;
dap_db_ctx_t *l_db_ctx;
MDBX_val l_key, l_data;

    dap_return_val_if_fail(a_group && a_key, NULL)                          /* Sanity check */

    if ( !(l_db_ctx = s_get_db_ctx_for_group(a_group)) )                    /* Get DB Context for group/table */
        return 0;

    MDBX_txn *l_txn;
    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &l_txn)) )
       return log_it(L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), 0;

    l_rc = s_get_obj_by_text_key(l_txn, l_db_ctx->dbi, &l_key, &l_data, a_key);

    if ( MDBX_SUCCESS != (l_rc2 = mdbx_txn_commit(l_txn)) )
        log_it (L_ERROR, "mdbx_txn_commit: (%d) %s", l_rc2, mdbx_strerror(l_rc2));
    return ( l_rc == MDBX_SUCCESS );    /*0 - RNF, 1 - SUCCESS */
}

/**
 * @brief Reads some objects from a database by conditions
 * @param a_iter iterator to looked for item
 * @param a_count_out[in] a number of objects to be read, if equals 0 reads with no limits
 * @param a_count_out[out] a number of objects that were read
 * @return If successful, a pointer to an objects, otherwise NULL.
 */
static dap_store_obj_t *s_db_mdbx_read_cond_store_obj(dap_global_db_iter_t *a_iter, size_t *a_count_out, dap_nanotime_t a_timestamp)
{
    dap_return_val_if_pass(!a_iter || !a_iter->db_iter || !a_iter->db_group, NULL);  /* Sanity check, if !a_count_out return all items*/

    if (a_iter->db_type != DAP_GLOBAL_DB_TYPE_CURRENT) {
        log_it(L_ERROR, "Trying use iterator from another database for MDBX");
        return NULL;
    }

    int l_rc = 0;
    MDBX_val    l_data = {0};
    MDBX_cursor* l_cursor = NULL;
    dap_store_obj_t *l_obj = NULL, *l_obj_arr = NULL;
    size_t  l_count_out = 0, l_count_current = 0;
    dap_db_ctx_t *l_db_ctx = s_get_db_ctx_for_group(a_iter->db_group);
    dap_return_val_if_pass(!l_db_ctx, NULL);                                       /* Sanity check */

    /* Initialize MDBX cursor context area */
    MDBX_txn *l_txn;
    if (MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &l_txn)) ||
                MDBX_SUCCESS != (l_rc = mdbx_cursor_open(l_txn, l_db_ctx->dbi, &l_cursor))) {
        log_it (L_ERROR, "mdbx_txn: (%d) %s", l_rc, mdbx_strerror(l_rc));
        goto safe_ret;
    }
    MDBX_val *l_key = (MDBX_val *)a_iter->db_iter;
    if (l_key->iov_base)
        l_rc = mdbx_cursor_get(l_cursor, l_key, NULL, MDBX_SET_RANGE);

    if (a_count_out && *a_count_out)
        l_count_out = *a_count_out;
    else
        l_count_current = s_db_mdbx_read_count_store(a_iter->db_group, a_timestamp);
    if (!(l_obj_arr = DAP_NEW_Z_SIZE(dap_store_obj_t, sizeof(dap_store_obj_t) * l_count_out))) {
        log_it(L_CRITICAL, "Memory allocation error");
        goto safe_ret;
    }
    /* Iterate cursor to retrieve records from DB
    for (int i = l_count_out; i && (MDBX_SUCCESS == (l_rc = mdbx_cursor_get(l_cursor, l_key, &l_data, MDBX_NEXT))); i--) {
        struct driver_record *l_record = l_data.iov_base;
        if (l_record->timestamp > a_timestamp) {        // Add if newer
            l_obj = l_obj_arr + l_count_current;        // Point <l_obj> to last array's element
            if (s_fill_store_obj(a_iter->db_group, l_key, &l_data, l_obj)) {
                l_rc = MDBX_PROBLEM;
                break;
            }
            l_count_current++;
        }
    } */
    // cut unused memory
    if (!l_count_current) {
        DAP_DEL_Z(l_obj_arr);
    } else if (l_count_current < l_count_out && !(l_obj_arr = DAP_REALLOC(l_obj_arr, sizeof(dap_store_obj_t) * l_count_current))) {
        log_it(L_ERROR, "Cannot cut area to keep %zu <store objects>", l_count_current);
        l_rc = MDBX_PROBLEM;
    }
    if ( (MDBX_SUCCESS != l_rc) && (l_rc != MDBX_NOTFOUND) )
        log_it (L_ERROR, "mdbx_cursor_get: (%d) %s", l_rc, mdbx_strerror(l_rc));

safe_ret:
    if (l_cursor)
        mdbx_cursor_close(l_cursor);
    mdbx_txn_commit(l_txn);
    if (a_count_out)
        *a_count_out = l_count_current;
    return l_obj_arr;
}

/**
 * @brief Reads a number of records for specified record's iterator.
 * @param a_iter started iterator
 * @return count of has been found record.
 */
size_t s_db_mdbx_read_count_store(const char *a_group, dap_nanotime_t a_timestamp)
{
    dap_return_val_if_fail(a_group, 0);                                       /* Sanity check */


    int l_rc = 0;
    MDBX_val l_key = {}, l_data = {};
    MDBX_cursor* l_cursor = NULL;
    dap_store_obj_t *l_obj = NULL;
    size_t  l_ret_count = 0;

    dap_db_ctx_t *l_db_ctx = s_get_db_ctx_for_group(a_group);
    dap_return_val_if_pass(!l_db_ctx, 0); 

    if (!(l_obj = DAP_NEW_Z(dap_store_obj_t))) {
        log_it(L_CRITICAL, "Memory allocation error");
        return 0;
    }

    MDBX_txn *l_txn;
    if ( 
        MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &l_txn)) ||
        MDBX_SUCCESS != (l_rc = mdbx_cursor_open(l_txn, l_db_ctx->dbi, &l_cursor))
        ) {
        mdbx_txn_commit(l_txn);
        log_it (L_ERROR, "mdbx_txn: (%d) %s", l_rc, mdbx_strerror(l_rc));
        DAP_DEL_Z(l_obj);
        return 0;
    }

   /* while ((MDBX_SUCCESS == (l_rc = mdbx_cursor_get(l_cursor, &l_key, &l_data, MDBX_NEXT))))
        if (((struct driver_record *)l_data.iov_base)->timestamp > a_timestamp)
            l_ret_count++; */

    if ( (MDBX_SUCCESS != l_rc) && (l_rc != MDBX_NOTFOUND) ) {
        log_it (L_ERROR, "mdbx_cursor_get: (%d) %s", l_rc, mdbx_strerror(l_rc));
    }
    mdbx_cursor_close(l_cursor);
    mdbx_txn_commit(l_txn);
    dap_store_obj_free(l_obj, 1);
    return l_ret_count;
}

/*
 *  DESCRIPTION: Action routine - returns a list of group/table names in DB contexts hash table is matched
 *      to specified pattern.
 *
 *  INPUTS:
 *      a_group_mask:   A pattern string
 *
 *  OUTPUTS:
 *      NONE
 *
 *  RETURNS:
 *      list of has been found groups
 */

static dap_list_t  *s_db_mdbx_get_groups_by_mask(const char *a_group_mask)
{
dap_list_t *l_ret_list = NULL;
dap_db_ctx_t *l_db_ctx, *l_db_ctx2;

    dap_return_val_if_fail(a_group_mask, NULL);

    dap_assert ( !pthread_rwlock_rdlock(&s_db_ctxs_rwlock) );

    HASH_ITER(hh, s_db_ctxs, l_db_ctx, l_db_ctx2)
        if (dap_global_db_group_match_mask(l_db_ctx->name, a_group_mask) )  /* Name match a pattern/mask ? */
            l_ret_list = dap_list_append(l_ret_list, l_db_ctx->name);       /* Add group name to output list */

    dap_assert ( !pthread_rwlock_unlock(&s_db_ctxs_rwlock) );

    return l_ret_list;
}


/*
 *  DESCRIPTION:  Action routine - insert/delete a record with data from the <store_object>  to/from database.
 *      Be advised that we performs a transaction processing to ensure DB consistency
 *
 *  INPUTS:
 *      a_store_obj:    An object with data to be stored
 *
 *  OUTPUTS:
 *      NONE:
 *
 *  RETURNS:
 *      0   - SUCCESS
 *      0>  - <errno>
 */
static  int s_db_mdbx_apply_store_obj (dap_store_obj_t *a_store_obj)
{
int     l_rc = 0, l_rc2;
dap_db_ctx_t *l_db_ctx;
MDBX_val    l_key, l_data;
MDBX_txn *l_txn;

    if ( !a_store_obj || !a_store_obj->group)                                       /* Sanity checks ... */
        return -EINVAL;
    if ( !(l_db_ctx = s_get_db_ctx_for_group(a_store_obj->group)) ) {               /* Get a DB context for the group */
                                                                                    /* Group is not found ? Try to create table for new group */
        if ( !(l_db_ctx = s_cre_db_ctx_for_group(a_store_obj->group, MDBX_CREATE)) )
            return  log_it(L_WARNING, "Cannot create DB context for the group '%s'", a_store_obj->group), -EIO;
        log_it(L_NOTICE, "DB context for the group '%s' has been created", a_store_obj->group);
        if ( a_store_obj->type == DAP_GLOBAL_DB_OPTYPE_DEL )                        /* Nothing to do anymore */
            return 1;
    }
    /* At this point we have got the DB Context for the table/group so we are can performs a main work */
    if (a_store_obj->type == DAP_GLOBAL_DB_OPTYPE_ADD) {
        if( !a_store_obj->key || !*a_store_obj->key)
            return -ENOENT;
        /* Fill IOV for MDBX key */
        if (!a_store_obj->crc) {
            log_it(L_ERROR, "Global DB store object corrupted");
            return MDBX_EINVAL;
        }
        l_rc = s_get_obj_by_text_key(l_txn, l_db_ctx->dbi, &l_key, &l_data, a_store_obj->key);
        if (l_rc == MDBX_SUCCESS) {
            // Drop object with same text key
            if ( MDBX_SUCCESS != (l_rc = mdbx_del(l_txn, l_db_ctx->dbi, &l_key, NULL)) && l_rc != MDBX_NOTFOUND)
                return log_it(L_ERROR, "mdbx_del: (%d) %s", l_rc, mdbx_strerror(l_rc)), l_rc;
        }
        dap_global_db_driver_hash_t l_driver_key = dap_global_db_driver_hash_get(a_store_obj);
        l_key.iov_base = &l_driver_key;
        l_key.iov_len = sizeof(l_driver_key);
        /* Compute a length of the area to keep record */
        size_t l_key_len = strnlen(a_store_obj->key, DAP_GLOBAL_DB_KEY_SIZE_MAX - 1) + 1;
        size_t l_record_len = sizeof(struct driver_record) + a_store_obj->value_len + l_key_len + dap_sign_get_size(a_store_obj->sign);
        struct driver_record *l_record = DAP_NEW_Z_SIZE(struct driver_record, l_record_len);
        if (!l_record) {
            log_it(L_ERROR, "Cannot allocate memory for new records, %zu octets, errno=%d", l_record_len, errno);
            return MDBX_PANIC;
        }
        dap_strncpy((char *)l_record->key_n_value_n_sign, a_store_obj->key, DAP_GLOBAL_DB_KEY_SIZE_MAX);
        l_record->key_len = l_key_len;
        l_record->value_len = a_store_obj->value_len;
        // Don't save NEW attribute
        l_record->flags = a_store_obj->flags & ~DAP_GLOBAL_DB_RECORD_NEW;
        if (a_store_obj->value_len)                                                 /* Put <value> into the record */
            memcpy(l_record->key_n_value_n_sign + l_key_len, a_store_obj->value, a_store_obj->value_len);
        if (a_store_obj->sign) {
            /* Put the authorization sign */
            l_record->sign_len = dap_sign_get_size(a_store_obj->sign);
            memcpy(l_record->key_n_value_n_sign + l_key_len + a_store_obj->value_len, a_store_obj->sign, l_record->sign_len);
            if (!l_record->sign_len) {
                DAP_DELETE(l_record);
                log_it(L_ERROR, "Global DB store object sign corrupted");
                return MDBX_EINVAL;
            }
        }
        l_data.iov_base = l_record;                                                 /* Fill IOV for MDBX data */
        l_data.iov_len = l_record_len;

        /* So, finaly: BEGIN transaction, do INSERT, COMMIT or ABORT ... */
        if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, 0, &l_txn)) )
            return DAP_FREE(l_record), log_it(L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), -EIO;
        if ( MDBX_SUCCESS != (l_rc = mdbx_put(l_txn, l_db_ctx->dbi, &l_key, &l_data, 0)) ) {
            log_it (L_ERROR, "mdbx_put: (%d) %s", l_rc, mdbx_strerror(l_rc));
            if ( MDBX_SUCCESS != (l_rc2 = mdbx_txn_abort(l_txn)) )
                log_it (L_ERROR, "mdbx_txn_abort: (%d) %s", l_rc2, mdbx_strerror(l_rc2));
        }
        else if ( MDBX_SUCCESS != (l_rc = mdbx_txn_commit(l_txn)) )
            log_it (L_ERROR, "mdbx_txn_commit: (%d) %s", l_rc, mdbx_strerror(l_rc));
        return DAP_FREE(l_record), (( l_rc == MDBX_SUCCESS ) ? 0 : -EIO);
    } /* DAP_GLOBAL_DB_OPTYPE_ADD */

    if (a_store_obj->type == DAP_GLOBAL_DB_OPTYPE_DEL) {
        if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, 0, &l_txn)) )
            return  log_it (L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), -ENOENT;
        if ( a_store_obj->key ) {
            /* Delete record */
            if (a_store_obj->crc && a_store_obj->timestamp) {
                dap_global_db_driver_hash_t l_driver_key = dap_global_db_driver_hash_get(a_store_obj);
                l_key.iov_base = &l_driver_key;
                l_key.iov_len = sizeof(l_driver_key);
                if ( MDBX_SUCCESS != (l_rc = mdbx_del(l_txn, l_db_ctx->dbi, &l_key, NULL)) && l_rc != MDBX_NOTFOUND)
                    log_it(L_ERROR, "mdbx_del: (%d) %s", l_rc, mdbx_strerror(l_rc));
            } else {
                l_rc = s_get_obj_by_text_key(l_txn, l_db_ctx->dbi, &l_key, &l_data, a_store_obj->key);
                if (l_rc == MDBX_SUCCESS) {
                    if ( MDBX_SUCCESS != (l_rc = mdbx_del(l_txn, l_db_ctx->dbi, &l_key, NULL)) && l_rc != MDBX_NOTFOUND)
                        log_it(L_ERROR, "mdbx_del: (%d) %s", l_rc, mdbx_strerror(l_rc));
                }
            }
            l_rc = (l_rc == MDBX_NOTFOUND) ? 1 : l_rc;                      /* Not found? It's OK */
        } else if (MDBX_SUCCESS != (l_rc = mdbx_drop(l_txn, l_db_ctx->dbi, 0))) /* Drop the whole table */
            log_it (L_ERROR, "mdbx_drop: (%d) %s", l_rc, mdbx_strerror(l_rc));
        l_rc2 = 0;
        if ( l_rc != MDBX_SUCCESS ) {                                       /* Check result of mdbx_drop/del */
            if ( MDBX_SUCCESS != (l_rc2 = mdbx_txn_abort(l_txn)) )
                log_it (L_ERROR, "mdbx_txn_abort: (%d) %s", l_rc2, mdbx_strerror(l_rc2));
        } else if ( MDBX_SUCCESS != (l_rc2 = mdbx_txn_commit(l_txn)) )
            log_it (L_ERROR, "mdbx_txn_commit: (%d) %s", l_rc2, mdbx_strerror(l_rc2));
        return ( l_rc2 == MDBX_SUCCESS ) ? l_rc : -EIO;
    } /* DAP_GLOBAL_DB_OPTYPE_DEL */

    log_it (L_ERROR, "Unhandle/unknown DB opcode (%d/%#x)", a_store_obj->type, a_store_obj->type);
    return  -EIO;
}

/*
 *  DESCRIPTION: Action routine - retrieve from specified group/table a record with the given key,
 *      theoreticaly we can return a set of records - but actualy we don't allow dupplicates in the DB,
 *      so count of records is always == 1.
 *
 *  INPUTS:
 *      a_group:    A group/table name to lokkind in
 *      a_key:      A key's record to looked for
 *
 *  OUTPUTS:
 *      a_count_out
 *
 *  RETURNS
 *      Array of store objects
 */

static dap_store_obj_t *s_db_mdbx_read_store_obj(const char *a_group, const char *a_key, size_t *a_count_out)
{
int l_rc, l_rc2;
uint64_t l_count_out;
dap_db_ctx_t *l_db_ctx;
dap_store_obj_t *l_obj, *l_obj_arr = NULL;
MDBX_val    l_key, l_data;
MDBX_stat   l_stat;
MDBX_txn *l_txn;

    dap_return_val_if_fail(a_group, NULL); /* Sanity check */
    if (!(l_db_ctx = s_get_db_ctx_for_group(a_group)))
        return NULL;
    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &l_txn)) )
        return log_it(L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), NULL;
    if ( a_count_out )
        *a_count_out = 0;
    /*
     *  Perfroms a find/get a record with the given key
     */
    if ( a_key ) {
        if (MDBX_SUCCESS == (l_rc = s_get_obj_by_text_key(l_txn, l_db_ctx->dbi, &l_key, &l_data, a_key))) {
            /* Found ! Make new <store_obj> */
            if ( !(l_obj = DAP_CALLOC(1, sizeof(dap_store_obj_t))) ) {
                log_it (L_ERROR, "Cannot allocate a memory for store object key, errno=%d", errno);
                l_rc = MDBX_PROBLEM;
            } else if ( !s_fill_store_obj(a_group, &l_key, &l_data, l_obj) ) {
                if ( a_count_out )
                    *a_count_out = 1;
            } else {
                l_rc = MDBX_PROBLEM;
                DAP_DELETE(l_obj);
            }
        } else if ( l_rc != MDBX_NOTFOUND )
            log_it (L_ERROR, "mdbx_get: (%d) %s", l_rc, mdbx_strerror(l_rc));
        mdbx_txn_commit(l_txn);
        return ( l_rc == MDBX_SUCCESS ) ? l_obj : NULL;
    }

    /*
    ** If a_key is NULL - retrieve a requested number of records from the table
    */
    MDBX_cursor *l_cursor = NULL;                                       /* Initialize MDBX cursor context area */
    do  {        
        /*
         * Retrieve statistic for group/table, we need to compute a number of records can be retreived
         */
        l_rc2 = 0;
        if (MDBX_SUCCESS != (l_rc = mdbx_dbi_stat(l_txn, l_db_ctx->dbi, &l_stat, sizeof(MDBX_stat)))) {
            log_it (L_ERROR, "mdbx_dbi_stat: (%d) %s", l_rc2, mdbx_strerror(l_rc2));
            break;
        } else if (!l_stat.ms_entries) {                                    /* Nothing to retrieve , table contains no record */
            debug_if(g_dap_global_db_debug_more, L_WARNING, "No object (-s) to be retrieved from the group '%s'", a_group);
            break;
        }
        l_count_out = a_count_out && *a_count_out && *a_count_out <= l_stat.ms_entries ? *a_count_out : l_stat.ms_entries;

        /*
         * Allocate memory for array[l_count_out] of returned objects
        */
        if ( !(l_obj_arr = (dap_store_obj_t *)DAP_NEW_Z_SIZE(char, l_count_out * sizeof(dap_store_obj_t))) ) {
            log_it(L_ERROR, "Cannot allocate %zu octets for %"DAP_UINT64_FORMAT_U" store objects", l_count_out * sizeof(dap_store_obj_t), l_count_out);
            break;
        }
        /* Iterate cursor to retrieve records from DB */
        if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_open(l_txn, l_db_ctx->dbi, &l_cursor)) ) {
            log_it (L_ERROR, "mdbx_cursor_open: (%d) %s", l_rc, mdbx_strerror(l_rc));
            break;
        }
        if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_get(l_cursor, &l_key, &l_data, MDBX_FIRST)) ) {
            log_it (L_ERROR, "mdbx_cursor_get FIRST: (%d) %s", l_rc, mdbx_strerror(l_rc));
            break;
        }
        l_obj = l_obj_arr;
        do {
            if (s_fill_store_obj(a_group, &l_key, &l_data, l_obj)) {
                l_rc = MDBX_PROBLEM;
                break;
            } else if (a_count_out)
                (*a_count_out)++;
            l_count_out--;
            l_obj++;
        } while (MDBX_SUCCESS == (l_rc = mdbx_cursor_get(l_cursor, &l_key, &l_data, MDBX_NEXT)) && l_count_out);

        if ( (MDBX_SUCCESS != l_rc) && (l_rc != MDBX_NOTFOUND) ) {
            log_it (L_ERROR, "mdbx_get ALL: (%d) %s", l_rc, mdbx_strerror(l_rc));
            break;
        }

    } while (0);
    if (l_cursor)
        mdbx_cursor_close(l_cursor);
    mdbx_txn_commit(l_txn);

    return l_obj_arr;
}
