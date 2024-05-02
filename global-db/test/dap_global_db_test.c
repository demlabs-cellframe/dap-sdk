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

#define DAP_DB$SZ_DATA                  8192
#define DAP_DB$SZ_KEY                   64
#define DAP_DB$T_GROUP                  "group.zero"
#define DAP_DB$T_GROUP_WRONG            "group.wrong"
#define DAP_DB$T_GROUP_NOT_EXISTED      "group.not.existed"


static  void    s_test_cb_end   (void *__unused_arg__, const void *arg)
{
int     *l_is_completed = (int *) arg;

    log_it(L_NOTICE, "Callback is called with arg: %p", arg);
    atomic_fetch_add(l_is_completed, 1);
}

static int s_test_write(size_t a_count, s_test_mode_work_t a_mode)
{
    dap_store_obj_t l_store_obj = {0};
    int l_value_len = 0, *l_pvalue, i, ret;
    atomic_int l_is_completed = 0;
    char l_key[64] = {0}, l_value[sizeof(dap_db_test_record_t) + DAP_DB$SZ_DATA] = {0};
    dap_enc_key_t *l_enc_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
    dap_db_test_record_t *prec;
    struct timespec now;

    dap_test_msg("Start writing %zu records ...", a_count);

                                                                            /* Fill static part of the <store_object> descriptor  */
                                    /* Do INSERT */

                                    /* "Table" name */
    l_store_obj.key = l_key;                                                /* Point <.key> to the buffer with the key of record */
    l_store_obj.value = (uint8_t *) l_value;                                 /* Point <.value> to static buffer area */
    prec = (dap_db_test_record_t *) l_value;

    for (size_t i = 0; i < a_count; ++i)
    {
        dap_test_msg("Write %zu record in GDB", i);

        l_store_obj.group = DAP_DB$T_GROUP; 
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08lx", i);           /* Generate a key of record */

        clock_gettime(CLOCK_REALTIME, &now);                                /* Get and save record's timestamp */
        l_store_obj.timestamp = (now.tv_sec << 32) | ((uint32_t) (now.tv_nsec));


        prec->len = rand() % DAP_DB$SZ_DATA + 1;                                /* Variable payload length */
        l_pvalue   = (int *) prec->data;

        for (int  i = prec->len / sizeof(int); i--; l_pvalue++)             /* Fill record's payload with random data */
            *l_pvalue = rand() + 1;

        sprintf(prec->data, "DATA$%08lx", i);                         /* Just for fun ... */
        l_value_len = prec->len + sizeof(dap_db_test_record_t);

        l_store_obj.value_len = l_value_len;
        l_store_obj.flags = i % 3 ? 0 : DAP_GLOBAL_DB_RECORD_DEL; 
        assert(l_store_obj.value_len < sizeof(l_value));


        dap_hash_fast (prec->data, prec->len, &prec->csum);                 /* Compute a hash of the payload part of the record */

        l_store_obj.sign = dap_store_obj_sign(&l_store_obj, l_enc_key, &l_store_obj.crc);

        dap_test_msg("Store object: [%s, %s, %zu octets]", l_store_obj.group, l_store_obj.key, l_store_obj.value_len);

        ret = dap_global_db_driver_add(&l_store_obj, 1);
        dap_assert_PIF(!ret, "Write record to DB is ok");

        l_store_obj.group = DAP_DB$T_GROUP_WRONG;
        l_store_obj.crc = i + 1;
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%09lx", i); 
        ret = dap_global_db_driver_add(&l_store_obj, 1);
        dap_assert_PIF(!ret, "Write record to wrong group DB is ok");
        DAP_DEL_Z(l_store_obj.sign);
    }
    dap_enc_key_delete(l_enc_key);
    dap_pass_msg("Write test");
    return  0;
}

static int s_test_read(size_t a_count)
{
    dap_test_msg("Start reading %zu records ...", a_count);

    for (size_t i = 0; i < a_count; ++i ) {
        dap_chain_hash_fast_t csum = { 0 };;
        dap_db_test_record_t *prec = NULL;
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08lx", i);           /* Generate a key of record */
        dap_store_obj_t *l_store_obj = dap_global_db_driver_read(DAP_DB$T_GROUP, l_key, NULL, true);
        dap_assert_PIF(l_store_obj, "Record-Not-Found");
        dap_assert_PIF(dap_global_db_pkt_check_sign_crc(l_store_obj), "Record sign not verified");

        prec = (dap_db_test_record_t *) l_store_obj->value;
        dap_test_msg("Retrieved object: [%s, %s, %zu octets]", l_store_obj->group, l_store_obj->key,
                     l_store_obj->value_len);
        dap_test_msg("Record: ['%.*s', %d octets]", prec->len, prec->data, prec->len);
        dap_hash_fast(prec->data, prec->len,
                      &csum);                       /* Compute a hash of the payload part of the record */
        dap_assert_PIF(memcmp(&csum, &prec->csum, sizeof(dap_chain_hash_fast_t)) == 0,
                       "Record check sum"); /* Integriry checking ... */
        dap_store_obj_free_one(l_store_obj);
    }
    dap_pass_msg("Reading check");

    return  0;
}

static void s_test_count(size_t a_count)
{
    dap_assert_PIF(a_count == dap_global_db_driver_count(DAP_DB$T_GROUP, (dap_global_db_driver_hash_t){0}, true), "Count with holes");
    dap_assert_PIF(a_count / 3 * 2 == dap_global_db_driver_count(DAP_DB$T_GROUP, (dap_global_db_driver_hash_t){0}, false), "Count without holes");
    dap_assert_PIF(a_count == dap_global_db_driver_count(DAP_DB$T_GROUP_WRONG, (dap_global_db_driver_hash_t){0}, true), "Count in wrong group with holes");
    dap_assert_PIF(a_count / 3 * 2 == dap_global_db_driver_count(DAP_DB$T_GROUP_WRONG, (dap_global_db_driver_hash_t){0}, false), "Count in wrong group without holes");
    dap_assert_PIF(!dap_global_db_driver_count(DAP_DB$T_GROUP_NOT_EXISTED, (dap_global_db_driver_hash_t){0}, true), "Count in not existed group with holes");
    dap_assert_PIF(!dap_global_db_driver_count(DAP_DB$T_GROUP_NOT_EXISTED, (dap_global_db_driver_hash_t){0}, false), "Count in not existed group without holes");
    dap_pass_msg("Count check");
}

static void s_test_is_obj(size_t a_count)
{
    for (size_t i = 0; i < a_count; ++i) {
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08lx", i);           /* Generate a key of record */
        dap_assert_PIF(dap_global_db_driver_is(DAP_DB$T_GROUP, l_key), "Key not finded")
        dap_assert_PIF(!dap_global_db_driver_is(DAP_DB$T_GROUP_WRONG, l_key), "Key finded in wrong group")
        dap_assert_PIF(!dap_global_db_driver_is(DAP_DB$T_GROUP_NOT_EXISTED, l_key), "Key finded in not existed group")
    }
    for (size_t i = a_count; i < a_count * 2; ++i) {
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08lx", i);           /* Generate a key of record */
        dap_assert_PIF(!dap_global_db_driver_is(DAP_DB$T_GROUP, l_key), "Finded not existed key")
        dap_assert_PIF(!dap_global_db_driver_is(DAP_DB$T_GROUP_WRONG, l_key), "Finded not existed key in wrong group")
        dap_assert_PIF(!dap_global_db_driver_is(DAP_DB$T_GROUP_NOT_EXISTED, l_key), "Finded not existed key in not existed group")
    }
    dap_pass_msg("Is obj check");
}

static void s_test_is_hash(size_t a_count)
{
    for (size_t i = 0; i < a_count; ++i) {
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08lx", i);           /* Generate a key of record */
        dap_store_obj_t *l_store_obj = dap_global_db_driver_read(DAP_DB$T_GROUP, l_key, NULL, true);
        dap_assert_PIF(l_store_obj, "Record-Not-Found");
        dap_global_db_driver_hash_t l_driver_key = dap_global_db_driver_hash_get(l_store_obj);
        dap_assert_PIF(dap_global_db_driver_is_hash(DAP_DB$T_GROUP, l_driver_key), "Hash not finded")
        dap_assert_PIF(!dap_global_db_driver_is_hash(DAP_DB$T_GROUP_WRONG, l_driver_key), "Hash finded in wrong group")
        dap_assert_PIF(!dap_global_db_driver_is_hash(DAP_DB$T_GROUP_NOT_EXISTED, l_driver_key), "Hash finded in not existed group")
        l_driver_key.becrc = 0;
        dap_assert_PIF(!dap_global_db_driver_is_hash(DAP_DB$T_GROUP, l_driver_key), "Finded not existed hash")
        dap_assert_PIF(!dap_global_db_driver_is_hash(DAP_DB$T_GROUP_WRONG, l_driver_key), "Finded not existed hash in wrong group")
        dap_assert_PIF(!dap_global_db_driver_is_hash(DAP_DB$T_GROUP_NOT_EXISTED, l_driver_key), "Finded not existed hash in not existed group")
        dap_store_obj_free_one(l_store_obj);
    }
    dap_pass_msg("Is hash check");
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
    s_test_is_obj(a_count);
    s_test_is_hash(a_count);
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

