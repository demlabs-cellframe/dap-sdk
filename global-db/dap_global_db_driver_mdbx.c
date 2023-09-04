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

#include "mdbx.h"                                                           /* LibMDBX API */
#define LOG_TAG "dap_global_db_mdbx"
#define DAP_GLOBAL_DB_TYPE_CURRENT DAP_GLOBAL_DB_TYPE_MDBX


/** Struct for a MDBX DB context */
typedef struct __db_ctx__ {
        size_t  namelen;                                                    /* Group name length */
        char name[DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX + 1];                   /* Group's name */

        pthread_mutex_t dbi_mutex;                                          /* Coordinate access the MDBX's <dbi> */
        MDBX_dbi    dbi;                                                    /* MDBX's internal context id */
        MDBX_txn    *txn;                                                   /* Current MDBX's transaction */

        UT_hash_handle hh;
} dap_db_ctx_t;

// mdbx element iterator
typedef struct dap_db_mbdbx_iter {
    MDBX_val    key;
    dap_db_ctx_t *ctx;
} dap_db_mdbx_iter_t;

static pthread_mutex_t s_db_ctx_mutex = PTHREAD_MUTEX_INITIALIZER;          /* A mutex  for working with a DB context */



static dap_db_ctx_t *s_db_ctxs = NULL;                                      /* A hash table of <group/subDB/table> == <MDBX DB context> */
static pthread_rwlock_t s_db_ctxs_rwlock = PTHREAD_RWLOCK_INITIALIZER;      /* A read-write lock for working with a <s_db_ctxs>. */

static char s_db_path[MAX_PATH];                                            /* A root directory for the MDBX files */

/* Forward declarations of action routines */
static int              s_db_mdbx_deinit();
static int              s_db_mdbx_flush(void);
static int              s_db_mdbx_apply_store_obj (dap_store_obj_t *a_store_obj);
static dap_store_obj_t  *s_db_mdbx_read_last_store_obj(const char* a_group);
static bool s_db_mdbx_is_obj(const char *a_group, const char *a_key);
static dap_store_obj_t  *s_db_mdbx_read_store_obj(const char *a_group, const char *a_key, size_t *a_count_out);
static dap_store_obj_t  *s_db_mdbx_read_cond_store_obj(dap_db_iter_t *a_iter, size_t *a_count_out);
static size_t           s_db_mdbx_read_count_store(dap_db_iter_t *a_iter);
static dap_list_t       *s_db_mdbx_get_groups_by_mask(const char *a_group_mask);
static dap_db_iter_t    *s_db_mdbx_iter_create(const char *a_group);
static void             s_db_mdbx_iter_delete(dap_db_iter_t* a_iter);


static MDBX_env *s_mdbx_env;                                                /* MDBX's context area */
static char s_subdir [] = "";                                               /* Name of subdir for the MDBX's database files */

static char s_db_master_tbl [] = "MDBX$MASTER";                             /* A name of master table in the MDBX
                                                                              to keep and maintains application level information */
static MDBX_dbi s_db_master_dbi;                                            /* A handle of the MDBX' DBI of the master subDB */


/*
 * Suffix structure is supposed to be added at end of MDBX record, so :
 * <value> + <suffix>
 */
struct DAP_ALIGN_PACKED __record_suffix__ {
        uint64_t        mbz;                                                /* Must Be Zero ! */
        uint64_t        id;                                                 /* An uniqe-like Id of the record - internaly created and maintained */
        uint64_t        flags;                                              /* Flag of the record : see RECORD_FLAGS enums */
        dap_time_t      ts;                                                 /* Timestamp of the record */
};

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
            l_rc = dap_bin2hex (l_buf, l_data_iov.iov_base, min(l_data_iov.iov_len, 72) );

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
uint64_t l_seq;
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
    dap_assert ( !pthread_mutex_init(&l_db_ctx->dbi_mutex, NULL));

    /*
    ** Start transaction, create table, commit.
    */
    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, 0, &l_db_ctx->txn)) )
        return  log_it(L_CRITICAL, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), NULL;

    if  ( MDBX_SUCCESS != (l_rc = mdbx_dbi_open(l_db_ctx->txn, a_group, a_flags, &l_db_ctx->dbi)) )
        return  log_it(L_CRITICAL, "mdbx_dbi_open: (%d) %s", l_rc, mdbx_strerror(l_rc)), NULL;

    /* MDBX sequence is started from zero, zero is not so good for our case,
     * so we just increment a current (may be is not zero) sequence for <dbi>
     */
    mdbx_dbi_sequence (l_db_ctx->txn, l_db_ctx->dbi, &l_seq, 1);

    /*
     * Save new subDB name into the master table
     */
    l_data_iov.iov_base =  l_key_iov.iov_base = l_db_ctx->name;
    l_data_iov.iov_len = l_key_iov.iov_len = l_db_ctx->namelen + 1;    /* Count '\0' */

    if ( MDBX_SUCCESS != (l_rc = mdbx_put(l_db_ctx->txn, s_db_master_dbi, &l_key_iov, &l_data_iov, MDBX_NOOVERWRITE ))
         && (l_rc != MDBX_KEYEXIST) )
    {
        log_it (L_ERROR, "mdbx_put: (%d) %s", l_rc, mdbx_strerror(l_rc));

        if ( MDBX_SUCCESS != (l_rc = mdbx_txn_abort(l_db_ctx->txn)) )
            return  log_it(L_CRITICAL, "mdbx_txn_abort: (%d) %s", l_rc, mdbx_strerror(l_rc)), NULL;
    }

    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_commit(l_db_ctx->txn)) )
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

    HASH_ITER(hh, s_db_ctxs, l_db_ctx, l_tmp)                               /* run over the hash table of the DB contexts */
    {
        
        dap_assert( !pthread_mutex_lock(&l_db_ctx->dbi_mutex) );
        if (l_db_ctx->txn)                                                  /* Commit, close table */
            mdbx_txn_commit(l_db_ctx->txn);

        if (l_db_ctx->dbi)
            mdbx_dbi_close(s_mdbx_env, l_db_ctx->dbi);

        dap_assert( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );

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
dap_slist_t l_slist = {0};
char        *l_cp;
size_t     l_upper_limit_of_db_size = 16;

    /*
     * [resources]
     * mdbx_upper_limit_of_db_size=32
     */
    l_upper_limit_of_db_size = dap_config_get_item_uint32_default ( g_config,  "resources", "mdbx_upper_limit_of_db_size", l_upper_limit_of_db_size);
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
            dap_slist_add2tail(&l_slist, l_cp, l_data_iov.iov_len);
            }
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "--- End-Of-List  ---");
        }

    dap_assert ( MDBX_SUCCESS == mdbx_txn_commit (l_txn) );


    /* Run over the list and create/open group/tables and DB context ... */
    while ( !dap_slist_get4head (&l_slist, &l_data_iov.iov_base, &l_data_iov.iov_len) )
    {
        s_cre_db_ctx_for_group(l_data_iov.iov_base, MDBX_CREATE);
        DAP_DELETE(l_data_iov.iov_base);
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
    a_drv_dpt->iter_delete         = s_db_mdbx_iter_delete;

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
static dap_db_iter_t *s_db_mdbx_iter_create(const char *a_group)
{
    dap_return_val_if_pass(!a_group, NULL);                                      /* Sanity check */

    int l_rc = 0;
    dap_db_ctx_t *l_db_ctx = s_get_db_ctx_for_group(a_group);
    MDBX_cursor *l_cursor = NULL;
    MDBX_val l_key = {0};

    if (!l_db_ctx)                    /* Get DB Context for group/table */
        return NULL;
    
    // create mdbx iter
    dap_db_mdbx_iter_t *l_mdbx_iter = DAP_NEW_Z(dap_db_mdbx_iter_t);
    if (!l_mdbx_iter) {
        log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        return NULL;
    }
    
    // create return object
    dap_db_iter_t *l_ret = DAP_NEW_Z(dap_db_iter_t);
    if (!l_ret) {
        log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        DAP_DELETE(l_mdbx_iter);
        return NULL;
    }

    l_ret->db_group = dap_strdup(a_group);
    if (!l_ret->db_group) {
        log_it(L_CRITICAL, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        DAP_DELETE(l_mdbx_iter);
        DAP_DELETE(l_ret);
        return NULL;
    }

    dap_assert ( !pthread_mutex_lock(&l_db_ctx->dbi_mutex));

    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &l_db_ctx->txn)) ) {
        dap_assert( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );
        log_it (L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc));
        DAP_DELETE(l_mdbx_iter);
        DAP_DELETE(l_ret);
        return NULL;
    }

    /* Initialize MDBX cursor context area */
    if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_open(l_db_ctx->txn, l_db_ctx->dbi, &l_cursor)) ) {
        log_it (L_ERROR, "mdbx_cursor_open: (%d) %s", l_rc, mdbx_strerror(l_rc));
    }

// mdbx_cursor_first
    if (MDBX_SUCCESS != (l_rc = mdbx_cursor_get(l_cursor, &l_key, NULL, MDBX_FIRST))) {
        dap_assert( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );
        log_it (L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc));
        DAP_DELETE(l_mdbx_iter);
        DAP_DELETE(l_ret);
        return NULL;
    }

    mdbx_cursor_close(l_cursor);
    mdbx_txn_commit(l_db_ctx->txn);
    dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );

    // get generated values
    l_mdbx_iter->key = l_key;
    l_mdbx_iter->ctx = l_db_ctx;

    l_ret->db_type = DAP_GLOBAL_DB_TYPE_CURRENT;
    l_ret->db_iter = l_mdbx_iter;

    return l_ret;
}

/**
 * @brief Delete iterator and memory free
 * @param a_iter deleting iterator
 * @return -
 */
static void s_db_mdbx_iter_delete(dap_db_iter_t *a_iter)
{   
    if (!a_iter)
        return;
    if (a_iter->db_type != DAP_GLOBAL_DB_TYPE_CURRENT) {
        log_it(L_ERROR, "Trying delete iterator from another data base");
        return;
    }
    DAP_DEL_Z(a_iter->db_iter);
    DAP_DEL_Z(a_iter->db_group);
    DAP_DEL_Z(a_iter);
}

/*
 *  DESCRIPTION: Action routine to read record with a give <id > from the table
 *
 *  INPUTS:
 *      a_group:    A group/table name to be looked in
 *      a_id:       An id of record to be looked for
 *      a_obj:      An address to the <store object> with the record
 *
 *  OUTPUTS:
 *      NONE
 *
 *  RETURNS:
 *      error code
 */
int s_fill_store_obj (const char        *a_group,
                      MDBX_val          *a_key,
                      MDBX_val          *a_data,
                      dap_store_obj_t   *a_obj
                      )
{
size_t  l_len;
struct  __record_suffix__   *l_suff;

    if (!a_group || !a_key || !a_data || !a_obj)
        return -1;

    /* Fill the <store obj> by data from the retrieved record */
    l_len = dap_strlen(a_group);
    if (!l_len)
        return log_it(L_ERROR, "Zero length of global DB group name"), -2;
    a_obj->group_len = l_len;
    if ( (a_obj->group = DAP_CALLOC(1, l_len + 1)) )
        memcpy(a_obj->group, a_group, a_obj->group_len);
    else
        return log_it(L_ERROR, "Cannot allocate a memory for store object group, errno=%d", errno), -3;

    a_obj->key_len = a_key->iov_len;
    if (!a_obj->key_len)
        return log_it(L_ERROR, "Zero length of global DB record key"), -4;
    if ( (a_obj->key = DAP_CALLOC(1, a_obj->key_len + 1)) )
        memcpy((char *) a_obj->key, a_key->iov_base, a_obj->key_len);
    else {
        DAP_DELETE(a_obj->group);
        return log_it(L_ERROR, "Cannot allocate a memory for store object key, errno=%d", errno), -5;
    }

    if (a_data->iov_len < sizeof(struct __record_suffix__))
        return log_it(L_ERROR, "Too small length of global DB record internal value, must be at least %zu",
                                        sizeof(struct __record_suffix__)), -6;
    a_obj->value_len = a_data->iov_len - sizeof(struct __record_suffix__);
    if (a_obj->value_len) {
        if ( (a_obj->value = DAP_CALLOC(1, a_obj->value_len)) )
            memcpy(a_obj->value, a_data->iov_base, a_obj->value_len);
        else {
            DAP_DELETE(a_obj->group);
            DAP_DELETE(a_obj->key);
            return log_it (L_ERROR, "Cannot allocate a memory for store object value, errno=%d", errno), -7;
        }
    }

    l_suff = (struct __record_suffix__ *) (a_data->iov_base + a_obj->value_len);
    a_obj->id = l_suff->id;
    a_obj->timestamp = l_suff->ts;
    a_obj->flags = l_suff->flags;

    return 0;
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
MDBX_val    l_key={0}, l_data={0}, l_last_data={0}, l_last_key={0};
MDBX_cursor *l_cursor = NULL;
struct  __record_suffix__   *l_suff;
uint64_t    l_id;
dap_store_obj_t *l_obj;

    if (!a_group)                                                           /* Sanity check */
        return NULL;

    if ( !(l_db_ctx = s_get_db_ctx_for_group(a_group)) )                    /* Get DB Context for group/table */
        return NULL;

    dap_assert ( !pthread_mutex_lock(&l_db_ctx->dbi_mutex) );

    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &l_db_ctx->txn)) )
    {
        dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );
        return  log_it (L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), NULL;
    }

    do {
        l_cursor = NULL;
        l_id  = 0;
        l_last_key = l_last_data = (MDBX_val) {0, 0};

        if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_open(l_db_ctx->txn, l_db_ctx->dbi, &l_cursor)) ) {
          log_it (L_ERROR, "mdbx_cursor_open: (%d) %s", l_rc, mdbx_strerror(l_rc));
          break;
        }

        /* Iterate cursor to retrieve records from DB - select a <key> and <data> pair
        ** with maximal <id>
        */
        while ( MDBX_SUCCESS == (l_rc = mdbx_cursor_get(l_cursor, &l_key, &l_data, MDBX_NEXT)) )
        {
            l_suff = (struct __record_suffix__ *) (l_data.iov_base + l_data.iov_len - sizeof(struct __record_suffix__));
            if ( l_id < l_suff->id )
            {
                l_id = l_suff->id;
                l_last_key = l_key;                                         /* <l_last_key> point to real key area in the MDBX DB */
                l_last_data = l_data;                                       /* <l_last_data> point to real data area in the MDBX DB */
            }
        }

    } while (0);

    if (l_cursor)                                                           /* Release uncesessary MDBX cursor area,
                                                                              but keep transaction !!! */
        mdbx_cursor_close(l_cursor);

    if ( !(l_last_key.iov_len || l_data.iov_len) )                          /* Not found anything  - return NULL */
    {
        mdbx_txn_commit(l_db_ctx->txn);
        dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );
        return  NULL;
    }

    /* Found ! Allocate memory for <store object>, <key> and <value> */
    if ( (l_obj = DAP_CALLOC(1, sizeof( dap_store_obj_t ))) ) {
        if (s_fill_store_obj(a_group, &l_key, &l_data, l_obj)) {
            l_rc = MDBX_PROBLEM;
            DAP_DEL_Z(l_obj);
        }
    } else
        l_rc = MDBX_PROBLEM, log_it (L_ERROR, "Cannot allocate a memory for store object, errno=%d", errno);

    mdbx_txn_commit(l_db_ctx->txn);
    dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );

    return l_rc == MDBX_SUCCESS ? l_obj : NULL;
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
MDBX_val    l_key, l_data;

    if (!a_group || !a_key)                                                           /* Sanity check */
        return 0;

    if ( !(l_db_ctx = s_get_db_ctx_for_group(a_group)) )                    /* Get DB Context for group/table */
        return 0;

    dap_assert ( !pthread_mutex_lock(&l_db_ctx->dbi_mutex) );

    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &l_db_ctx->txn)) )
    {
        dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );
        return  log_it (L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), 0;
    }

    l_key.iov_base = (void *) a_key;                                        /* Fill IOV for MDBX key */
    l_key.iov_len =  strlen(a_key);

    l_rc = mdbx_get(l_db_ctx->txn, l_db_ctx->dbi, &l_key, &l_data);

    if ( MDBX_SUCCESS != (l_rc2 = mdbx_txn_commit(l_db_ctx->txn)) )
        log_it (L_ERROR, "mdbx_txn_commit: (%d) %s", l_rc2, mdbx_strerror(l_rc2));

    dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );

    return ( l_rc == MDBX_SUCCESS );    /*0 - RNF, 1 - SUCCESS */
}

/**
 * @brief Reads some objects from a database by conditions
 * @param a_iter iterator to looked for item
 * @param a_count_out[in] a number of objects to be read, if equals 0 reads with no limits
 * @param a_count_out[out] a number of objects that were read
 * @return If successful, a pointer to an objects, otherwise NULL.
 */
static dap_store_obj_t  *s_db_mdbx_read_cond_store_obj(dap_db_iter_t *a_iter, size_t *a_count_out)
{
    dap_return_val_if_pass(!a_iter || !a_iter->db_iter || !a_iter->db_group, NULL);                                       /* Sanity check */

    int l_rc = 0;
    dap_db_mdbx_iter_t* l_mdbx_iter = (dap_db_mdbx_iter_t*)a_iter->db_iter;
    MDBX_val    l_data = {0};
    MDBX_cursor* l_cursor = NULL;
    dap_store_obj_t *l_obj = NULL, *l_obj_arr = NULL;
    size_t  l_cnt = 0, l_count_out = 0;

    /* Limit a number of objects to be returned */
    l_count_out = (a_count_out && *a_count_out) ? *a_count_out : DAP_GLOBAL_DB_MAX_OBJS;
    l_count_out = MIN(l_count_out, DAP_GLOBAL_DB_MAX_OBJS);
    /* Iterate cursor to retrieve records from DB */

    dap_assert ( !pthread_mutex_lock(&l_mdbx_iter->ctx->dbi_mutex));

    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &l_mdbx_iter->ctx->txn)) ) {
        dap_assert( !pthread_mutex_unlock(&l_mdbx_iter->ctx->dbi_mutex) );
        log_it (L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc));
        return NULL;
    }

    /* Initialize MDBX cursor context area */
    if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_open(l_mdbx_iter->ctx->txn, l_mdbx_iter->ctx->dbi, &l_cursor)) ) {
        log_it (L_ERROR, "mdbx_cursor_open: (%d) %s", l_rc, mdbx_strerror(l_rc));
        return NULL;
    }

    for (int i = l_count_out; i && (MDBX_SUCCESS == (l_rc = mdbx_cursor_get(l_cursor, &l_mdbx_iter->key, &l_data, MDBX_NEXT))); i--) {
        /*
        * Expand a memory for new <store object> structure
        */
        ++l_cnt;
        if ( !(l_obj_arr = DAP_REALLOC(l_obj_arr, l_cnt * sizeof(dap_store_obj_t))) ) {
            log_it(L_ERROR, "Cannot expand area to keep %zu <store objects>", l_cnt);
            l_rc = MDBX_PROBLEM;
            break;
        }

        l_obj = l_obj_arr + (l_cnt - 1);                                /* Point <l_obj> to last array's element */
        memset(l_obj, 0, sizeof(dap_store_obj_t));

        if (s_fill_store_obj(a_iter->db_group, &l_mdbx_iter->key, &l_data, l_obj)) {
            l_rc = MDBX_PROBLEM;
            break;
        }
    }

    if ( (MDBX_SUCCESS != l_rc) && (l_rc != MDBX_NOTFOUND) ) {
        log_it (L_ERROR, "mdbx_cursor_get: (%d) %s", l_rc, mdbx_strerror(l_rc));
    }

    mdbx_cursor_close(l_cursor);
    mdbx_txn_commit(l_mdbx_iter->ctx->txn);
    dap_assert ( !pthread_mutex_unlock(&l_mdbx_iter->ctx->dbi_mutex) );

    if(a_count_out)
        *a_count_out = l_cnt;
    return l_obj_arr;
}

/**
 * @brief Reads a number of records for specified record's iterator.
 * @param a_iter started iterator
 * @return count of has been found record.
 */
size_t  s_db_mdbx_read_count_store(dap_db_iter_t *a_iter)
{
    dap_return_val_if_pass(!a_iter || !a_iter->db_iter || !a_iter->db_group, 0);                                       /* Sanity check */

    int l_rc = 0;
    dap_db_mdbx_iter_t* l_mdbx_iter = (dap_db_mdbx_iter_t*)a_iter->db_iter;
    MDBX_cursor* l_cursor = NULL;
    size_t  l_ret_count = 0;

    dap_assert ( !pthread_mutex_lock(&l_mdbx_iter->ctx->dbi_mutex));

    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &l_mdbx_iter->ctx->txn)) ) {
        dap_assert( !pthread_mutex_unlock(&l_mdbx_iter->ctx->dbi_mutex) );
        log_it (L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc));
        return NULL;
    }

    /* Initialize MDBX cursor context area */
    if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_open(l_mdbx_iter->ctx->txn, l_mdbx_iter->ctx->dbi, &l_cursor)) ) {
        log_it (L_ERROR, "mdbx_cursor_open: (%d) %s", l_rc, mdbx_strerror(l_rc));
        return NULL;
    }

    while ((MDBX_SUCCESS == (l_rc = mdbx_cursor_get(l_cursor, &l_mdbx_iter->key, NULL, MDBX_NEXT)))) {
        ++l_ret_count;
    }

    if ( (MDBX_SUCCESS != l_rc) && (l_rc != MDBX_NOTFOUND) ) {
        log_it (L_ERROR, "mdbx_cursor_get: (%d) %s", l_rc, mdbx_strerror(l_rc));
    }

    mdbx_cursor_close(l_cursor);
    mdbx_txn_commit(l_mdbx_iter->ctx->txn);
    dap_assert ( !pthread_mutex_unlock(&l_mdbx_iter->ctx->dbi_mutex) );

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
dap_list_t *l_ret_list;
dap_db_ctx_t *l_db_ctx, *l_db_ctx2;

    if(!a_group_mask)
        return NULL;

    l_ret_list = NULL;

    dap_assert ( !pthread_rwlock_rdlock(&s_db_ctxs_rwlock) );

    HASH_ITER(hh, s_db_ctxs, l_db_ctx, l_db_ctx2) {
        if (!dap_fnmatch(a_group_mask, l_db_ctx->name, 0) )                 /* Name match a pattern/mask ? */
            l_ret_list = dap_list_prepend(l_ret_list, dap_strdup(l_db_ctx->name)); /* Add group name to output list */
    }

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
size_t l_summary_len;
dap_db_ctx_t *l_db_ctx;
MDBX_val    l_key, l_data;
char    *l_val;
struct  __record_suffix__   *l_suff;

    if ( !a_store_obj || !a_store_obj->group)                               /* Sanity checks ... */
        return -EINVAL;



    if ( !(l_db_ctx = s_get_db_ctx_for_group(a_store_obj->group)) ) {       /* Get a DB context for the group */
                                                                            /* Group is not found ? Try to create table for new group */
        if ( !(l_db_ctx = s_cre_db_ctx_for_group(a_store_obj->group, MDBX_CREATE)) )
            return  log_it(L_WARNING, "Cannot create DB context for the group '%s'", a_store_obj->group), -EIO;

        log_it(L_NOTICE, "DB context for the group '%s' has been created", a_store_obj->group);

        if ( a_store_obj->type == DAP_DB$K_OPTYPE_DEL )                     /* Nothing to do anymore */
            return 1;
    }


    /* At this point we have got the DB Context for the table/group
     * so we are can performs a main work
     */

    dap_assert ( !pthread_mutex_lock(&l_db_ctx->dbi_mutex) );


    if (a_store_obj->type == DAP_DB$K_OPTYPE_ADD ) {
        if( !a_store_obj->key )
        {
            dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );
            return -ENOENT;
        }

        l_key.iov_base = (void *) a_store_obj->key;                         /* Fill IOV for MDBX key */
        l_key.iov_len =  a_store_obj->key_len ? a_store_obj->key_len : strnlen(a_store_obj->key, DAP_GLOBAL_DB_KEY_MAX);

        /*
         * Now we are ready  to form a record in next format:
         * <value> + <suffix>
         */
        l_summary_len = a_store_obj->value_len + sizeof(struct  __record_suffix__); /* Compute a length of the area to keep value+suffix */

        if ( !(l_val = DAP_NEW_Z_SIZE(char, l_summary_len)) )
        {
            dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );
            return  log_it(L_ERROR, "Cannot allocate memory for new records, %zu octets, errno=%d", l_summary_len, errno), -errno;
        }

        l_data.iov_base = l_val;                                            /* Fill IOV for MDBX data */
        l_data.iov_len = l_summary_len;

        /*
         * Fill suffix's fields
        */
        l_suff = (struct __record_suffix__ *) (l_val + a_store_obj->value_len);
        l_suff->flags = a_store_obj->flags;
        l_suff->ts = a_store_obj->timestamp;

        memcpy(l_val, a_store_obj->value, a_store_obj->value_len);          /* Put <value> into the record */

        /* So, finaly: BEGIN transaction, do INSERT, COMMIT or ABORT ... */
        if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, 0, &l_db_ctx->txn)) )
        {
            dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );
            return  DAP_FREE(l_val), log_it (L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), -EIO;
        }


                                                                            /* Generate <sequence number> for new record */
        uint64_t l_id = 0;
        if ( MDBX_SUCCESS != mdbx_dbi_sequence	(l_db_ctx->txn, l_db_ctx->dbi, &l_id, 1) )
        {
            log_it (L_CRITICAL, "mdbx_dbi_sequence: (%d) %s", l_rc, mdbx_strerror(l_rc));

            if ( MDBX_SUCCESS != (l_rc = mdbx_txn_abort(l_db_ctx->txn)) )
                log_it (L_ERROR, "mdbx_txn_abort: (%d) %s", l_rc, mdbx_strerror(l_rc));

            mdbx_txn_abort(l_db_ctx->txn);
            dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );

            return  DAP_FREE(l_val), -EIO;
        }
        l_suff->id = l_id;


        if ( MDBX_SUCCESS != (l_rc = mdbx_put(l_db_ctx->txn, l_db_ctx->dbi, &l_key, &l_data, 0)) )
        {
            log_it (L_ERROR, "mdbx_put: (%d) %s", l_rc, mdbx_strerror(l_rc));

            if ( MDBX_SUCCESS != (l_rc2 = mdbx_txn_abort(l_db_ctx->txn)) )
                log_it (L_ERROR, "mdbx_txn_abort: (%d) %s", l_rc2, mdbx_strerror(l_rc2));
        }
        else if ( MDBX_SUCCESS != (l_rc = mdbx_txn_commit(l_db_ctx->txn)) )
            log_it (L_ERROR, "mdbx_txn_commit: (%d) %s", l_rc, mdbx_strerror(l_rc));

        dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );

        return DAP_FREE(l_val), (( l_rc == MDBX_SUCCESS ) ? 0 : -EIO);
    } /* DAP_DB$K_OPTYPE_ADD */



    if (a_store_obj->type == DAP_DB$K_OPTYPE_DEL)  {
        if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, 0, &l_db_ctx->txn)) )
        {
            dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );

            return  log_it (L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), -ENOENT;
        }

        l_rc2 = 0;

        if ( a_store_obj->key ) {                                           /* Delete record */
                l_key.iov_base = (void *) a_store_obj->key;
                l_key.iov_len =  a_store_obj->key_len ? a_store_obj->key_len : strnlen(a_store_obj->key, DAP_GLOBAL_DB_KEY_MAX);

                if ( MDBX_SUCCESS != (l_rc = mdbx_del(l_db_ctx->txn, l_db_ctx->dbi, &l_key, NULL))
                     && ( l_rc != MDBX_NOTFOUND) )
                    l_rc2 = -EIO, log_it (L_ERROR, "mdbx_del: (%d) %s", l_rc, mdbx_strerror(l_rc));
            }
        else {                                                              /* Truncate only  table */
                if ( MDBX_SUCCESS != (l_rc = mdbx_drop(l_db_ctx->txn, l_db_ctx->dbi, 0))
                     && ( l_rc != MDBX_NOTFOUND) )
                    l_rc2 = -EIO, log_it (L_ERROR, "mdbx_drop: (%d) %s", l_rc, mdbx_strerror(l_rc));
            }


        l_rc = (l_rc == MDBX_NOTFOUND) ? 1 : l_rc;               /* Not found ?! It's Okay !!! */



        if ( l_rc != MDBX_SUCCESS ) {                                       /* Check result of mdbx_drop/del */
            if ( MDBX_SUCCESS != (l_rc = mdbx_txn_abort(l_db_ctx->txn)) )
                l_rc2 = -EIO, log_it (L_ERROR, "mdbx_txn_abort: (%d) %s", l_rc, mdbx_strerror(l_rc));
        }
        else if ( MDBX_SUCCESS != (l_rc = mdbx_txn_commit(l_db_ctx->txn)) )
            l_rc2 = -EIO, log_it (L_ERROR, "mdbx_txn_commit: (%d) %s", l_rc, mdbx_strerror(l_rc));

        dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );

        return ( l_rc2 == MDBX_SUCCESS ) ? 0 : -EIO;
    } /* DAP_DB$K_OPTYPE_DEL */


    dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );

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
MDBX_cursor *l_cursor;
MDBX_stat   l_stat;

    if (!a_group)                                                           /* Sanity check */
        return NULL;

    if ( !(l_db_ctx = s_get_db_ctx_for_group(a_group)) )                    /* Get DB Context for group/table */
        return NULL;


    dap_assert ( !pthread_mutex_lock(&l_db_ctx->dbi_mutex) );

    if ( MDBX_SUCCESS != (l_rc = mdbx_txn_begin(s_mdbx_env, NULL, MDBX_TXN_RDONLY, &l_db_ctx->txn)) )
    {
        dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );
        return  log_it (L_ERROR, "mdbx_txn_begin: (%d) %s", l_rc, mdbx_strerror(l_rc)), NULL;
    }


    if ( a_count_out )
        *a_count_out = 0;

    /*
     *  Perfroms a find/get a record with the given key
     */
    if ( a_key )
    {
        l_key.iov_base = (void *) a_key;                                    /* Fill IOV for MDBX key */
        l_key.iov_len =  strlen(a_key);

        if ( MDBX_SUCCESS == (l_rc = mdbx_get(l_db_ctx->txn, l_db_ctx->dbi, &l_key, &l_data)) )
        {
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


        mdbx_txn_commit(l_db_ctx->txn);
        dap_assert ( !pthread_mutex_unlock(&l_db_ctx->dbi_mutex) );

        return ( l_rc == MDBX_SUCCESS ) ? l_obj : NULL;
    }


    /*
    ** If a_key is NULL - retrieve a requested number of records from the table
    */
    do  {
        l_count_out = (a_count_out && *a_count_out)? *a_count_out : DAP_GLOBAL_DB_MAX_OBJS;/* Limit a number of objects to be returned */
        l_cursor = NULL;

        /*
         * Retrieve statistic for group/table, we need to compute a number of records can be retreived
         */
        l_rc2 = 0;
        if ( MDBX_SUCCESS != (l_rc = mdbx_dbi_stat	(l_db_ctx->txn, l_db_ctx->dbi, &l_stat, sizeof(MDBX_stat))) ) {
            log_it (L_ERROR, "mdbx_dbi_stat: (%d) %s", l_rc2, mdbx_strerror(l_rc2));
            break;
        }
        else if ( !l_stat.ms_entries )                                      /* Nothing to retrieve , table contains no record */
            break;

        if ( !(  l_count_out = min(l_stat.ms_entries, l_count_out)) ) {
            debug_if(g_dap_global_db_debug_more, L_WARNING, "No object (-s) to be retrieved from the group '%s'", a_group);
            break;
        }

        /*
         * Allocate memory for array[l_count_out] of returned objects
        */
        if ( !(l_obj_arr = (dap_store_obj_t *) DAP_NEW_Z_SIZE(char, l_count_out * sizeof(dap_store_obj_t))) ) {
            log_it(L_ERROR, "Cannot allocate %zu octets for %"DAP_UINT64_FORMAT_U" store objects", l_count_out * sizeof(dap_store_obj_t), l_count_out);
            break;
        }

                                                                            /* Initialize MDBX cursor context area */
        if ( MDBX_SUCCESS != (l_rc = mdbx_cursor_open(l_db_ctx->txn, l_db_ctx->dbi, &l_cursor)) ) {
            log_it (L_ERROR, "mdbx_cursor_open: (%d) %s", l_rc, mdbx_strerror(l_rc));
            break;
        }


                                                                            /* Iterate cursor to retrieve records from DB */
        l_obj = l_obj_arr;
        for (int i = l_count_out;
             i && (MDBX_SUCCESS == (l_rc = mdbx_cursor_get(l_cursor, &l_key, &l_data, MDBX_NEXT))); i--,  l_obj++)
        {
            if (s_fill_store_obj(a_group, &l_key, &l_data, l_obj)) {
                l_rc = MDBX_PROBLEM;
                break;
            } else if ( a_count_out )
                (*a_count_out)++;
        }

        if ( (MDBX_SUCCESS != l_rc) && (l_rc != MDBX_NOTFOUND) ) {
            log_it (L_ERROR, "mdbx_cursor_get: (%d) %s", l_rc, mdbx_strerror(l_rc));
            break;
        }

    } while (0);


    if (l_cursor)
        mdbx_cursor_close(l_cursor);

    mdbx_txn_commit(l_db_ctx->txn);
    pthread_mutex_unlock(&l_db_ctx->dbi_mutex);

    return l_obj_arr;
}
