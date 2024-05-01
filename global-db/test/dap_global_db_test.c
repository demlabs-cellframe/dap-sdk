#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_events.h"
#include "dap_proc_thread.h"
//#include "dap_proc_queue.h"
#include "dap_hash.h"

#include "dap_global_db.h"
#include "dap_global_db_driver.h"
#include "dap_test.h"
#include "dap_global_db_pkt.h"

#define LOG_TAG "dap_globaldb_test"

#define DB_FILE "./base.tmp"

typedef enum s_test_mode_work{
    SYNC,
    ASYNC
}s_test_mode_work_t;

static int s_test_create_db(const char *db_type)
{
    int rc;
    char l_cmd[MAX_PATH];
    dap_test_msg("Initializatiion test db %s driver in %s file", db_type, DB_FILE);

    if( dap_dir_test(DB_FILE) ) {
        rmdir(DB_FILE);
        dap_snprintf(l_cmd, sizeof(l_cmd), "rm -rf %s", DB_FILE);
        if ( (rc = system(l_cmd)) )
             log_it(L_ERROR, "system(%s)->%d", l_cmd, rc);
    }
    else
        unlink(DB_FILE);
    rc = dap_db_driver_init(db_type, DB_FILE, -1);
    dap_assert(rc == 0, "Initialization db driver");
    return rc;
}

typedef struct __dap_test_record__ {
    dap_chain_hash_fast_t   csum;                                           /* CRC32 , cover <len> and <data> fields */
    unsigned    len;                                                        /* Length of the <data> field */
    char        data[];                                                     /* Place holder for data area */
} dap_db_test_record_t;

#define DAP_DB$SZ_DATA  8192
#define DAP_DB$SZ_KEY   64
#define DAP_DB$T_GROUP  "Group.Zero"


static  void    s_test_cb_end   (void *__unused_arg__, const void *arg)
{
int     *l_is_completed = (int *) arg;

    log_it(L_NOTICE, "Callback is called with arg: %p", arg);
    atomic_fetch_add(l_is_completed, 1);
}

static int s_test_write(int a_count, s_test_mode_work_t a_mode)
{
    dap_store_obj_t l_store_obj = {0};
    int l_value_len = 0, *l_pvalue, i, ret, l_key_nr;
    atomic_int l_is_completed = 0;
    char l_key[64] = {0}, l_value[sizeof(dap_db_test_record_t) + DAP_DB$SZ_DATA] = {0};
    dap_enc_key_t *l_enc_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
    dap_db_test_record_t *prec;
    struct timespec now;

    dap_test_msg("Start writing %d records ...", a_count);

                                                                            /* Fill static part of the <store_object> descriptor  */
                                    /* Do INSERT */


    l_store_obj.group = DAP_DB$T_GROUP;                                     /* "Table" name */
    l_store_obj.key = l_key;                                                /* Point <.key> to the buffer with the key of record */
    l_store_obj.value = (uint8_t *) l_value;                                 /* Point <.value> to static buffer area */
    prec = (dap_db_test_record_t *) l_value;

    for (l_key_nr = 0; l_key_nr < a_count; l_key_nr++ )
    {
        dap_test_msg("Write %d record in GDB", l_key_nr);

        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08x", l_key_nr);           /* Generate a key of record */

        clock_gettime(CLOCK_REALTIME, &now);                                /* Get and save record's timestamp */
        l_store_obj.timestamp = (now.tv_sec << 32) | ((uint32_t) (now.tv_nsec));


        prec->len = rand() % DAP_DB$SZ_DATA;                                /* Variable payload length */
        l_pvalue   = (int *) prec->data;

        for (int  i = prec->len / sizeof(int); i--; l_pvalue++)             /* Fill record's payload with random data */
            *l_pvalue = rand();

        sprintf(prec->data, "DATA$%08x", l_key_nr);                         /* Just for fun ... */
        l_value_len = prec->len + sizeof(dap_db_test_record_t);

        l_store_obj.value_len = l_value_len;
        l_store_obj.flags = l_key_nr % 3 ? 0 : DAP_GLOBAL_DB_RECORD_DEL; 
        assert(l_store_obj.value_len < sizeof(l_value));


        dap_hash_fast (prec->data, prec->len, &prec->csum);                 /* Compute a hash of the payload part of the record */

        l_store_obj.sign = dap_store_obj_sign(&l_store_obj, l_enc_key, &l_store_obj.crc);

        dap_test_msg("Store object: [%s, %s, %zu octets]", l_store_obj.group, l_store_obj.key, l_store_obj.value_len);
        ret = dap_global_db_driver_add(&l_store_obj, 1);
        dap_assert_PIF(!ret, "Write record to DB is ok");
        DAP_DEL_Z(l_store_obj.sign);
    }
    dap_enc_key_delete(l_enc_key);
    dap_pass_msg("Write test");
    return  0;
}

static int s_test_read(int a_count)
{
    dap_store_obj_t *l_store_obj;
    int l_key_nr;
    char l_key[64], l_buf[512];
    dap_chain_hash_fast_t csum;
    dap_db_test_record_t *prec;

    dap_test_msg("Start reading %d records ...", a_count);

    for (l_key_nr = 0; l_key_nr < a_count; l_key_nr++ ) {
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08x", l_key_nr);           /* Generate a key of record */

        dap_assert_PIF((l_store_obj = dap_global_db_driver_read(DAP_DB$T_GROUP, l_key, NULL, true)) != NULL, "Record-Not-Found");
        dap_assert_PIF(dap_global_db_pkt_check_sign_crc(l_store_obj), "Record sign not verified");

        prec = (dap_db_test_record_t *) l_store_obj->value;
        dap_test_msg("Retrieved object: [%s, %s, %zu octets]", l_store_obj->group, l_store_obj->key,
                     l_store_obj->value_len);
        dap_test_msg("Record: ['%.*s', %d octets]", prec->len, prec->data, prec->len);
        dap_hash_fast(prec->data, prec->len,
                      &csum);                       /* Compute a hash of the payload part of the record */

#if 0
        dap_bin2hex (l_buf, prec->csum, sizeof(csum) );
        log_it(L_DEBUG, "%.*s", 2*DAP_HASH_FAST_SIZE, l_buf);
        dap_bin2hex (l_buf, csum, sizeof(csum) );
        log_it(L_DEBUG, "%.*s", 2*DAP_HASH_FAST_SIZE, l_buf);
#endif
        dap_assert_PIF(memcmp(&csum, &prec->csum, sizeof(dap_chain_hash_fast_t)) == 0,
                       "Record check sum"); /* Integriry checking ... */
    }
    dap_pass_msg("Reading check");

    return  0;
}

static void s_test_count(size_t a_count)
{
    dap_assert_PIF(a_count == dap_global_db_driver_count(DAP_DB$T_GROUP, (dap_global_db_driver_hash_t){0}, true), "Count with holes");
    dap_assert_PIF(a_count / 3 * 2 == dap_global_db_driver_count(DAP_DB$T_GROUP, (dap_global_db_driver_hash_t){0}, false), "Count without holes");
    dap_pass_msg("Count check");
}

static void s_test_close_db(void)
{
    dap_db_driver_deinit();
    dap_test_msg("Close global_db");
    log_it(L_NOTICE, "Close global_db");
}

void s_test_all(size_t a_count)
{
    s_test_write(a_count, 0);
    s_test_read(a_count);
    s_test_count(a_count);
}

int    main (int argc, char **argv)
{
    /* MDBX DB */
    dap_print_module_name("MDBX R/W SYNC");
    s_test_create_db("sqlite");
    size_t l_count = 1350;
    s_test_all(l_count);
    s_test_close_db();
}

