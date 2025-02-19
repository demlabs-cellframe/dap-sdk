#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_events.h"
#include "dap_proc_thread.h"
#include "dap_hash.h"

#include "dap_global_db.h"
#include "dap_global_db_driver.h"
#include "dap_test.h"
#include "dap_global_db_pkt.h"

#define LOG_TAG "dap_globaldb_test"

#define DB_FILE "./base.tmp"

static const char *s_db_types[] = {
#ifdef DAP_CHAIN_GDB_ENGINE_SQLITE
    "sqlite",
#endif
#ifdef DAP_CHAIN_GDB_ENGINE_CUTTDB
    "cdb",
#endif
#ifdef DAP_CHAIN_GDB_ENGINE_MDBX
    "mdbx",
#endif
#ifdef DAP_CHAIN_GDB_ENGINE_PGSQL
    "pgsql",
#endif
    "none"
};

// benchmarks
static uint64_t    s_write = 0;
static uint64_t    s_rewrite = 0;
static uint64_t    s_read = 0;
static uint64_t    s_read_all_with_holes = 0;
static uint64_t    s_read_all_without_holes = 0;
static uint64_t    s_read_cond_store = 0;
static uint64_t    s_count_with_holes = 0;
static uint64_t    s_count_without_holes = 0;
static uint64_t    s_tx_start_end_erase = 0;
static uint64_t    s_tx_start_end_add = 0;
static uint64_t    s_flush = 0;
static uint64_t    s_is_obj = 0;
static uint64_t    s_is_obj_wrong = 0;
static uint64_t    s_is_obj_not_existed = 0;
static uint64_t    s_is_hash = 0;
static uint64_t    s_is_hash_wrong = 0;
static uint64_t    s_is_hash_not_existed = 0;
static uint64_t    s_last_with_holes = 0;
static uint64_t    s_last_without_holes = 0;
static uint64_t    s_last_with_holes_wrong = 0;
static uint64_t    s_last_without_holes_wrong = 0;
static uint64_t    s_last_with_holes_not_existed = 0;
static uint64_t    s_last_without_holes_not_existed = 0;
static uint64_t    s_read_hashes = 0;
static uint64_t    s_read_hashes_wrong = 0;
static uint64_t    s_read_hashes_not_existed = 0;
static uint64_t    s_get_by_hash = 0;
static uint64_t    s_get_groups_by_mask = 0;


typedef struct __dap_test_record__ {
    dap_chain_hash_fast_t   csum;                                           /* CRC32 , cover <len> and <data> fields */
    unsigned    len;                                                        /* Length of the <data> field */
    char        data[];                                                     /* Place holder for data area */
} dap_db_test_record_t;

#define DAP_DB$SZ_DATA                  8192
#define DAP_DB$SZ_KEY                   64
#define DAP_DB$SZ_HOLES                 3
#define DAP_DB$T_GROUP_PREF                  "group.zero."
#define DAP_DB$T_GROUP_WRONG_PREF            "group.wrong."
#define DAP_DB$T_GROUP_NOT_EXISTED_PREF      "group.not.existed."
static char s_group[64] = {};
static char s_group_worng[64] = {};
static char s_group_not_existed[64] = {};


static int s_test_create_db(const char *db_type)
{
    int l_rc = 0;
    char l_cmd[MAX_PATH];
    dap_test_msg("Initializatiion test db %s driver in %s file", db_type, DB_FILE);

    if (!dap_strcmp(db_type, "pgsql"))
        l_rc = dap_global_db_driver_init(db_type, "dbname=postgres");
    else
        l_rc = dap_global_db_driver_init(db_type, DB_FILE);
    dap_assert(l_rc == 0, "Initialization db driver");
    return l_rc;
}

static int s_test_write(size_t a_count, bool a_with_value)
{
    dap_store_obj_t l_store_obj = {0};
    int l_value_len = 0, *l_pvalue, i, ret;
    char l_key[64] = {0}, l_value[sizeof(dap_db_test_record_t) + DAP_DB$SZ_DATA + 1] = {0};
    dap_enc_key_t *l_enc_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
    dap_db_test_record_t *prec;
    struct timespec now;

    dap_test_msg("Start writing %zu records ...", a_count);

                                                                            /* Fill static part of the <store_object> descriptor  */
                                    /* Do INSERT */

                                    /* "Table" name */
    l_store_obj.key = l_key;                                                /* Point <.key> to the buffer with the key of record */
    l_store_obj.value = a_with_value ? (uint8_t *) l_value : NULL;                                 /* Point <.value> to static buffer area */
    prec = (dap_db_test_record_t *) l_value;
    size_t l_rewrite_count = rand() % (a_count / 2) + 2; 
    for (size_t i = 0; i < a_count; ++i)
    {
        log_it(L_DEBUG, "Write %zu record in GDB", i);

        l_store_obj.group = s_group; 
        snprintf(l_key, sizeof(l_key), "KEY$%08zx", i); // add bad to check rewrite          /* Generate a key of record */

        clock_gettime(CLOCK_REALTIME, &now);                                /* Get and save record's timestamp */
        l_store_obj.timestamp = ((uint64_t)now.tv_sec << 32) | ((uint32_t) (now.tv_nsec));
        if (a_with_value) {
            prec->len = rand() % DAP_DB$SZ_DATA + 1;                                /* Variable payload length */
            l_pvalue   = (int *) prec->data;
            for (int  i = prec->len / sizeof(int); i--; l_pvalue++)             /* Fill record's payload with random data */
                *l_pvalue = rand() + 1;
            sprintf(prec->data, "DATA$%08zx%s", i, i < l_rewrite_count ? "rw" : "");                         /* Just for fun ... */
            l_value_len = prec->len + sizeof(dap_db_test_record_t);
            l_store_obj.value_len = l_value_len;
            assert(l_store_obj.value_len < sizeof(l_value));
            dap_hash_fast (prec->data, prec->len, &prec->csum);                 /* Compute a hash of the payload part of the record */
        }

        if (i >= l_rewrite_count) {
            l_store_obj.flags = i % DAP_DB$SZ_HOLES ? 0 : DAP_GLOBAL_DB_RECORD_DEL;
        }
        l_store_obj.sign = dap_store_obj_sign(&l_store_obj, l_enc_key, &l_store_obj.crc);
        log_it(L_DEBUG, "Store object: [%s, %s, %zu octets]", l_store_obj.group, l_store_obj.key, l_store_obj.value_len);

        uint64_t l_time = get_cur_time_nsec();
        ret = dap_global_db_driver_add(&l_store_obj, 1);
        s_write += get_cur_time_nsec() - l_time;
        dap_assert_PIF(!ret, "Write record to DB");

        // rewrite block
        if ( i < l_rewrite_count) {
            DAP_DEL_Z(l_store_obj.sign);
            clock_gettime(CLOCK_REALTIME, &now);
            l_store_obj.timestamp = ((uint64_t)now.tv_sec << 32) | ((uint32_t) (now.tv_nsec));
            sprintf(prec->data, "DATA$%08zx", i);
            dap_hash_fast (prec->data, prec->len, &prec->csum);
            l_store_obj.flags = i % DAP_DB$SZ_HOLES ? 0 : DAP_GLOBAL_DB_RECORD_DEL;
            l_store_obj.sign = dap_store_obj_sign(&l_store_obj, l_enc_key, &l_store_obj.crc);
            
            l_time = get_cur_time_nsec();
            ret = dap_global_db_driver_add(&l_store_obj, 1);
            s_rewrite += get_cur_time_nsec() - l_time;
            dap_assert_PIF(!ret, "Rewrite with key conflict record to DB");
        }

        l_store_obj.group = s_group_worng;
        l_store_obj.crc = i + 1;
        snprintf(l_key, sizeof(l_key), "KEY$%09zx", i);

        ret = dap_global_db_driver_add(&l_store_obj, 1);
        dap_assert_PIF(!ret, "Write record to wrong group DB");
        DAP_DEL_Z(l_store_obj.sign);
    }
    dap_enc_key_delete(l_enc_key);
    dap_pass_msg("apply check");
    return  0;
}

static int s_test_read(size_t a_count, bool a_bench)
{
    dap_test_msg("Start reading %zu records ...", a_count);
    int l_time = 0;
    size_t l_read_count = 0;
    dap_store_obj_t *l_store_obj = dap_global_db_driver_read(s_group, NULL, &l_read_count, true);
    dap_assert_PIF(l_read_count == a_count, "All records count not equal writed");
    dap_store_obj_free(l_store_obj, l_read_count);
    for (size_t i = 0; i < a_count; ++i ) {
        dap_chain_hash_fast_t csum = { 0 };;
        dap_db_test_record_t *prec = NULL;
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key), "KEY$%08zx", i);           /* Generate a key of record */

        uint64_t l_time = get_cur_time_nsec();
        dap_store_obj_t *l_store_obj = dap_global_db_driver_read(s_group, l_key, NULL, true);
        s_read += a_bench ? get_cur_time_nsec() - l_time : 0;

        dap_assert_PIF(l_store_obj, "Record-Not-Found");
        if (l_store_obj->sign)  // to test rewriting with hash conflict some records wiwthout sign
            dap_assert_PIF(dap_global_db_pkt_check_sign_crc(l_store_obj), "Record sign not verified");
        dap_assert_PIF(!strcmp(s_group, l_store_obj->group), "Check group name");
        dap_assert_PIF(!strcmp(l_key, l_store_obj->key), "Check key name");

        if (l_store_obj->value) {
            prec = (dap_db_test_record_t *) l_store_obj->value;
            log_it(L_DEBUG, "Retrieved object: [%s, %s, %zu octets]", l_store_obj->group, l_store_obj->key,
                        l_store_obj->value_len);
            log_it(L_DEBUG, "Record: ['%.*s', %d octets]", prec->len, prec->data, prec->len);
            dap_hash_fast(prec->data, prec->len,
                        &csum);                       /* Compute a hash of the payload part of the record */
            dap_assert_PIF(memcmp(&csum, &prec->csum, sizeof(dap_chain_hash_fast_t)) == 0,
                        "Record check sum"); /* Integriry checking ... */
        }
        dap_store_obj_free_one(l_store_obj);
    }
    dap_pass_msg("read check");

    return  0;
}

static int s_test_read_all(size_t a_count)
{
    dap_test_msg("Start reading all %zu records ...", a_count);
    // with holes
    size_t l_count = 0;
    s_read_all_with_holes = get_cur_time_nsec();
    dap_store_obj_t *l_store_obj_all = dap_global_db_driver_read(s_group, NULL, &l_count, true);
    s_read_all_with_holes = get_cur_time_nsec() - s_read_all_with_holes;
    dap_assert_PIF(l_count == a_count, "Count of all read records with holes not equal count of write records");
    for (size_t i = 0; i < l_count; ++i ) {
        dap_store_obj_t *l_store_obj = l_store_obj_all + i;
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key), "KEY$%08zx", i);           /* Generate a key of record */

        dap_assert_PIF(l_store_obj, "Record-Not-Found");
        if (l_store_obj->sign)  // to test rewriting with hash conflict some records wiwthout sign
            dap_assert_PIF(dap_global_db_pkt_check_sign_crc(l_store_obj), "Record sign not verified");
        dap_assert_PIF(!strcmp(s_group, l_store_obj->group), "Check group name");
        dap_assert_PIF(!strcmp(l_key, l_store_obj->key), "Check key name");

        if (l_store_obj->value) {
            dap_chain_hash_fast_t csum = { 0 };
            dap_db_test_record_t *prec = (dap_db_test_record_t *) l_store_obj->value;
            log_it(L_DEBUG, "Retrieved object: [%s, %s, %zu octets]", l_store_obj->group, l_store_obj->key,
                        l_store_obj->value_len);
            log_it(L_DEBUG, "Record: ['%.*s', %d octets]", prec->len, prec->data, prec->len);
            dap_hash_fast(prec->data, prec->len,
                        &csum);                       /* Compute a hash of the payload part of the record */
            dap_assert_PIF(memcmp(&csum, &prec->csum, sizeof(dap_chain_hash_fast_t)) == 0,
                        "Record check sum"); /* Integriry checking ... */
        }
    }
    dap_store_obj_free(l_store_obj_all, l_count);
    dap_pass_msg("read_all check");

    // without holes
    l_count = 0;
    s_read_all_without_holes = get_cur_time_nsec();
    l_store_obj_all = dap_global_db_driver_read(s_group, NULL, &l_count, false);
    s_read_all_without_holes = get_cur_time_nsec() - s_read_all_without_holes;
    dap_assert_PIF(l_count == a_count - a_count / DAP_DB$SZ_HOLES, "Count of all read records without holes not equal count of write records");
    for (size_t i = 0, j = 0; i < a_count; ++i ) {
        if (a_count % DAP_DB$SZ_HOLES) {
            dap_chain_hash_fast_t csum = { 0 };
            dap_db_test_record_t *prec = NULL;
            dap_store_obj_t *l_store_obj = l_store_obj_all + j;
            char l_key[64] = { 0 };
            snprintf(l_key, sizeof(l_key), "KEY$%08zx", i);           /* Generate a key of record */

            dap_assert_PIF(l_store_obj, "Record-Not-Found");
            if (l_store_obj->sign)  // to test rewriting with hash conflict some records wiwthout sign
                dap_assert_PIF(dap_global_db_pkt_check_sign_crc(l_store_obj), "Record sign not verified");
            dap_assert_PIF(!strcmp(s_group, l_store_obj->group), "Check group name");
            dap_assert_PIF(!strcmp(l_key, l_store_obj->key), "Check key name");

            prec = (dap_db_test_record_t *) l_store_obj->value;
            log_it(L_DEBUG, "Retrieved object: [%s, %s, %zu octets]", l_store_obj->group, l_store_obj->key,
                        l_store_obj->value_len);
            log_it(L_DEBUG, "Record: ['%.*s', %d octets]", prec->len, prec->data, prec->len);
            dap_hash_fast(prec->data, prec->len,
                        &csum);                       /* Compute a hash of the payload part of the record */
            dap_assert_PIF(memcmp(&csum, &prec->csum, sizeof(dap_chain_hash_fast_t)) == 0,
                        "Record check sum"); /* Integriry checking ... */
            ++j;
        }
    }
    dap_store_obj_free(l_store_obj_all, l_count);

    return  0;
}


static void s_test_read_cond_store(size_t a_count, bool a_bench)
{
    dap_global_db_driver_hash_t l_driver_key = {0};
    size_t l_count = 0;
    for (size_t i = 0; i < a_count; ++i) {
        uint64_t l_time = get_cur_time_nsec();
        dap_store_obj_t *l_objs = dap_global_db_driver_cond_read(s_group, l_driver_key, &l_count, true);
        s_read_cond_store += a_bench ? get_cur_time_nsec() - l_time : 0;
        
        dap_assert_PIF(l_objs, "Records-Not-Found");
        dap_global_db_driver_hash_t l_blank_check = dap_global_db_driver_hash_get(l_objs + l_count - 1);
        dap_assert_PIF(l_count <= DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT + dap_global_db_driver_hash_is_blank(&l_blank_check), "Wrong finded records count");
        for (size_t j = i, k = 0; j < a_count && k < l_count; ++j, ++k) {
            char l_key[64] = { 0 };
            snprintf(l_key, sizeof(l_key), "KEY$%08zx", j);           /* Generate a key of record */
            dap_store_obj_t *l_store_obj = dap_global_db_driver_read(s_group, l_key, NULL, true);
            dap_assert_PIF(l_store_obj, "Record-Not-Found");
            dap_assert_PIF(!strcmp(s_group, (l_objs + k)->group), "Wrong group");
            dap_assert_PIF(!dap_store_obj_driver_obj_compare(l_store_obj, l_objs + k), "Records not equal");
            if (i == j)
                l_driver_key = dap_global_db_driver_hash_get(l_store_obj);
            dap_store_obj_free_one(l_store_obj);
        }
        dap_store_obj_free(l_objs, l_count);
    }

    l_count = DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT / 4;
    l_driver_key = (dap_global_db_driver_hash_t){0};
    size_t l_total_count = 0;
    for (dap_store_obj_t *l_objs = dap_global_db_driver_cond_read(s_group, l_driver_key, &l_count, true);
            l_objs;
            l_objs = dap_global_db_driver_cond_read(s_group, l_driver_key, &l_count, true)) {
        l_driver_key = dap_global_db_driver_hash_get(l_objs + l_count - 1);
        dap_store_obj_free(l_objs, l_count);
        l_total_count += l_count;
        if (dap_global_db_driver_hash_is_blank(&l_driver_key)) {
            break;
        }
    }
    dap_assert_PIF(l_total_count - dap_global_db_driver_hash_is_blank(&l_driver_key) == a_count, "Total cond read count with holes not equal total records count");

    l_count = DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT / 4;
    l_driver_key = (dap_global_db_driver_hash_t){0};
    l_total_count = 0;
    for (dap_store_obj_t *l_objs = dap_global_db_driver_cond_read(s_group, l_driver_key, &l_count, false);
            l_objs;
            l_objs = dap_global_db_driver_cond_read(s_group, l_driver_key, &l_count, false)) {
        l_driver_key = dap_global_db_driver_hash_get(l_objs + l_count - 1);
        dap_store_obj_free(l_objs, l_count);
        l_total_count += l_count;
        if (dap_global_db_driver_hash_is_blank(&l_driver_key)) {
            break;
        }
    }
    dap_assert_PIF(l_total_count - dap_global_db_driver_hash_is_blank(&l_driver_key) == a_count / DAP_DB$SZ_HOLES * (DAP_DB$SZ_HOLES - 1), "Total cond read count without holes not equal total records count");
    dap_pass_msg("read_cond_store check");
}

static void s_test_count(size_t a_count, bool a_bench)
{
    dap_global_db_driver_hash_t l_driver_key = {0};
    for (size_t i = 0; i < a_count; ++i) {
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key), "KEY$%08zx", i);  
        dap_store_obj_t *l_store_obj = dap_global_db_driver_read(s_group, l_key, NULL, true);
        dap_assert_PIF(l_store_obj, "Records-Not-Found");
        
        uint64_t l_time = get_cur_time_nsec();
        size_t l_count = dap_global_db_driver_count(s_group, l_driver_key, true);
        s_count_with_holes += a_bench ? get_cur_time_nsec() - l_time : 0;
        dap_assert_PIF(a_count - i == l_count, "Count with holes");
        
        
        l_driver_key = dap_global_db_driver_hash_get(l_store_obj);
        dap_store_obj_free_one(l_store_obj);
    }
    l_driver_key = (dap_global_db_driver_hash_t){0};
    for (size_t i = 0, k = 0; i < a_count; ++i, ++k) {
        char l_key[64] = { 0 };
        if(!(i % DAP_DB$SZ_HOLES)) {
            ++i;
            if (i >= a_count)
                break;
        }
        snprintf(l_key, sizeof(l_key), "KEY$%08zx", i);  
        dap_store_obj_t *l_store_obj = dap_global_db_driver_read(s_group, l_key, NULL, false);
        dap_assert_PIF(l_store_obj, "Records-Not-Found");
        
        uint64_t l_time = get_cur_time_nsec();
        size_t l_count = dap_global_db_driver_count(s_group, l_driver_key, false);
        s_count_without_holes += a_bench ? get_cur_time_nsec() - l_time : 0;
        dap_assert_PIF(a_count / DAP_DB$SZ_HOLES * (DAP_DB$SZ_HOLES - 1) - k == l_count, "Count without holes");

        l_driver_key = dap_global_db_driver_hash_get(l_store_obj);
        dap_store_obj_free_one(l_store_obj);
    }
    
    dap_assert_PIF(a_count == dap_global_db_driver_count(s_group_worng, (dap_global_db_driver_hash_t){0}, true), "Count in wrong group with holes");
    dap_assert_PIF(a_count / DAP_DB$SZ_HOLES * (DAP_DB$SZ_HOLES - 1) == dap_global_db_driver_count(s_group_worng, (dap_global_db_driver_hash_t){0}, false), "Count in wrong group without holes");
    dap_assert_PIF(!dap_global_db_driver_count(s_group_not_existed, (dap_global_db_driver_hash_t){0}, true), "Count in not existed group with holes");
    dap_assert_PIF(!dap_global_db_driver_count(s_group_not_existed, (dap_global_db_driver_hash_t){0}, false), "Count in not existed group without holes");
    dap_pass_msg("count check");
}

static void s_test_is_obj(size_t a_count, bool a_bench)
{
    for (size_t i = 0; i < a_count; ++i) {
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key), "KEY$%08zx", i);           /* Generate a key of record */
        uint64_t l_time = get_cur_time_nsec();
        dap_assert_PIF(dap_global_db_driver_is(s_group, l_key), "Key not finded");
        s_is_obj += a_bench ? get_cur_time_nsec() - l_time : 0;

        l_time = get_cur_time_nsec();
        dap_assert_PIF(!dap_global_db_driver_is(s_group_worng, l_key), "Key finded in wrong group");
        s_is_obj_wrong += a_bench ? get_cur_time_nsec() - l_time : 0;
        
        l_time = get_cur_time_nsec();
        dap_assert_PIF(!dap_global_db_driver_is(s_group_not_existed, l_key), "Key finded in not existed group");
        s_is_obj_not_existed += a_bench ? get_cur_time_nsec() - l_time : 0;
    }
    for (size_t i = a_count; i < a_count * 2; ++i) {
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key), "KEY$%08zx", i);           /* Generate a key of record */
        dap_assert_PIF(!dap_global_db_driver_is(s_group, l_key), "Finded not existed key")
        dap_assert_PIF(!dap_global_db_driver_is(s_group_worng, l_key), "Finded not existed key in wrong group")
        dap_assert_PIF(!dap_global_db_driver_is(s_group_not_existed, l_key), "Finded not existed key in not existed group")
    }
    dap_pass_msg("is_obj check");
}

static void s_test_is_hash(size_t a_count, bool a_bench)
{
    for (size_t i = 0; i < a_count; ++i) {
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key), "KEY$%08zx", i);           /* Generate a key of record */
        dap_store_obj_t *l_store_obj = dap_global_db_driver_read(s_group, l_key, NULL, true);
        dap_assert_PIF(l_store_obj, "Record-Not-Found");
        dap_global_db_driver_hash_t l_driver_key = dap_global_db_driver_hash_get(l_store_obj);
        
        uint64_t l_time = get_cur_time_nsec();
        dap_assert_PIF(dap_global_db_driver_is_hash(s_group, l_driver_key), "Hash not finded")
        s_is_hash += a_bench ? get_cur_time_nsec() - l_time : 0;

        l_time = get_cur_time_nsec();
        dap_assert_PIF(!dap_global_db_driver_is_hash(s_group_worng, l_driver_key), "Hash finded in wrong group")
        s_is_hash_wrong += a_bench ? get_cur_time_nsec() - l_time : 0;

        l_time = get_cur_time_nsec();
        dap_assert_PIF(!dap_global_db_driver_is_hash(s_group_not_existed, l_driver_key), "Hash finded in not existed group")
        s_is_hash_not_existed += a_bench ? get_cur_time_nsec() - l_time : 0;
        
        l_driver_key.becrc = 0;
        dap_assert_PIF(!dap_global_db_driver_is_hash(s_group, l_driver_key), "Finded not existed hash")
        dap_assert_PIF(!dap_global_db_driver_is_hash(s_group_worng, l_driver_key), "Finded not existed hash in wrong group")
        dap_assert_PIF(!dap_global_db_driver_is_hash(s_group_not_existed, l_driver_key), "Finded not existed hash in not existed group")
        
        dap_store_obj_free_one(l_store_obj);
    }
    dap_pass_msg("is_hash check");
}

static void s_test_last(size_t a_count, bool a_bench)
{
    char l_key[64] = { 0 };
    // with holes
    snprintf(l_key, sizeof(l_key), "KEY$%08zx", a_count - 1);
    for (size_t i = 0; i < a_count; ++i) {
        uint64_t l_time = get_cur_time_nsec();
        dap_store_obj_t *l_store_obj = dap_global_db_driver_read_last(s_group, true);
        s_last_with_holes += a_bench ? get_cur_time_nsec() - l_time : 0;

        dap_assert_PIF(l_store_obj && !strcmp(l_key, l_store_obj->key), "Last with holes");
        dap_store_obj_free_one(l_store_obj);

        l_time = get_cur_time_nsec();
        l_store_obj = dap_global_db_driver_read_last(s_group_worng, true);
        s_last_with_holes_wrong += a_bench ? get_cur_time_nsec() - l_time : 0;

        dap_assert_PIF(l_store_obj && strcmp(l_key, l_store_obj->key), "Last with holes in wrong group");
        dap_store_obj_free_one(l_store_obj);

        l_time = get_cur_time_nsec();
        l_store_obj = dap_global_db_driver_read_last(s_group_not_existed, true);
        s_last_with_holes_not_existed += a_bench ? get_cur_time_nsec() - l_time : 0;

        dap_assert_PIF(!l_store_obj, "Last with holes in not existed group");
    }

    // without holes
    snprintf(l_key, sizeof(l_key), "KEY$%08zx", a_count - 1 - a_count % DAP_DB$SZ_HOLES);
    
    for (size_t i = 0; i < a_count; ++i) {
        uint64_t l_time = get_cur_time_nsec();
        dap_store_obj_t *l_store_obj = dap_global_db_driver_read_last(s_group, false);
        s_last_without_holes += a_bench ? get_cur_time_nsec() - l_time : 0;

        dap_assert_PIF(l_store_obj && !strcmp(l_key, l_store_obj->key), "Last without holes");
        dap_store_obj_free_one(l_store_obj);

        l_time = get_cur_time_nsec();
        l_store_obj = dap_global_db_driver_read_last(s_group_worng, false);
        s_last_without_holes_wrong += a_bench ? get_cur_time_nsec() - l_time : 0;
        
        dap_assert_PIF(l_store_obj && strcmp(l_key, l_store_obj->key), "Last without holes in wrong group");
        dap_store_obj_free_one(l_store_obj);

        l_time = get_cur_time_nsec();
        l_store_obj = dap_global_db_driver_read_last(s_group_not_existed, false);
        s_last_without_holes_not_existed += a_bench ? get_cur_time_nsec() - l_time : 0;

        dap_assert_PIF(!l_store_obj, "Last without holes in not existed group");
    }
    dap_pass_msg("read_last check");
}


static void s_test_read_hashes(size_t a_count, bool a_bench)
{
    dap_global_db_driver_hash_t l_driver_key = {0};
    for (size_t i = 0; i < a_count; ++i) {
        uint64_t l_time = get_cur_time_nsec();
        dap_global_db_hash_pkt_t *l_hashes = dap_global_db_driver_hashes_read(s_group, l_driver_key);
        s_read_hashes += a_bench ? get_cur_time_nsec() - l_time : 0;

        l_time = get_cur_time_nsec();
        dap_global_db_hash_pkt_t *l_hashes_wrong = dap_global_db_driver_hashes_read(s_group_worng, l_driver_key);
        s_read_hashes_wrong += a_bench ? get_cur_time_nsec() - l_time : 0;

        l_time = get_cur_time_nsec();
        dap_global_db_hash_pkt_t *l_hashes_not_existed = dap_global_db_driver_hashes_read(s_group_not_existed, l_driver_key);
        s_read_hashes_not_existed += a_bench ? get_cur_time_nsec() - l_time : 0;

        dap_assert_PIF(l_hashes && l_hashes_wrong, "Hashes-Not-Found");
        dap_assert_PIF(!l_hashes_not_existed, "Finded hashes in not existed group");
        size_t l_bias = l_hashes->group_name_len;
        for (size_t j = i, k = 0; j < a_count && k < DAP_GLOBAL_DB_COND_READ_KEYS_DEFAULT; ++j, ++k) {
            char l_key[64] = { 0 };
            snprintf(l_key, sizeof(l_key), "KEY$%08zx", j);           /* Generate a key of record */
            dap_store_obj_t *l_store_obj = dap_global_db_driver_read(s_group, l_key, NULL, true);
            dap_assert_PIF(l_store_obj, "Record-Not-Found");
            dap_global_db_driver_hash_t l_driver_key_current = dap_global_db_driver_hash_get(l_store_obj);
            dap_assert_PIF(!memcmp(l_hashes->group_n_hashses + l_bias, &l_driver_key_current, sizeof(dap_global_db_driver_hash_t)), "Hash not finded")
            dap_assert_PIF(memcmp(l_hashes_wrong->group_n_hashses + l_bias, &l_driver_key_current, sizeof(dap_global_db_driver_hash_t)), "Hash finded in wrong group")
            if (i == j)
                l_driver_key = l_driver_key_current;
            dap_store_obj_free_one(l_store_obj);
            l_bias += sizeof(dap_global_db_driver_hash_t);
        }
        DAP_DEL_MULTY(l_hashes, l_hashes_wrong);
    }
    dap_pass_msg("read_hashes check");
}

static void s_test_get_by_hash(size_t a_count, bool a_bench)
{
    dap_global_db_driver_hash_t l_driver_key = {0};
    for (size_t i = 0; i < a_count; ++i) {
        dap_global_db_hash_pkt_t *l_hashes = dap_global_db_driver_hashes_read(s_group, l_driver_key);
        
        uint64_t l_time = get_cur_time_nsec();
        dap_global_db_pkt_pack_t *l_objs = dap_global_db_driver_get_by_hash(s_group, (dap_global_db_driver_hash_t *)(l_hashes->group_n_hashses + l_hashes->group_name_len), l_hashes->hashes_count);
        s_get_by_hash += a_bench ? get_cur_time_nsec() - l_time : 0;

        dap_assert_PIF(l_objs, "Records-Not-Found");
        dap_assert_PIF(l_objs->obj_count == l_hashes->hashes_count - dap_global_db_driver_hash_is_blank((dap_global_db_driver_hash_t *)(l_hashes->group_n_hashses + l_hashes->group_name_len) + l_hashes->hashes_count - 1), "Wrong finded records count");
        size_t l_total_data = 0;
        for (size_t j = 0; j < l_hashes->hashes_count - dap_global_db_driver_hash_is_blank((dap_global_db_driver_hash_t *)(l_hashes->group_n_hashses + l_hashes->group_name_len) + l_hashes->hashes_count - 1); ++j) {
            char l_key[64] = { 0 };
            snprintf(l_key, sizeof(l_key), "KEY$%08zx", i + j);           /* Generate a key of record */
            dap_store_obj_t *l_store_obj = dap_global_db_driver_read(s_group, l_key, NULL, true);
            dap_assert_PIF(l_store_obj, "Record-Not-Found");
            dap_global_db_pkt_t *l_cur_pkt = (dap_global_db_pkt_t *)(l_objs->data + l_total_data);
            dap_store_obj_t l_store_obj_cur = {
                .crc = l_cur_pkt->crc,
                .timestamp = l_cur_pkt->timestamp,
                .flags = l_cur_pkt->flags,
                .value_len = l_cur_pkt->value_len,
                .group = (char *)l_cur_pkt->data,
                .key = (char *)(l_cur_pkt->data + l_cur_pkt->group_len),
                .value = l_cur_pkt->data + l_cur_pkt->group_len + l_cur_pkt->key_len,
                .sign = (dap_sign_t *)(l_cur_pkt->data + l_cur_pkt->group_len + l_cur_pkt->key_len + l_cur_pkt->value_len)
            };
            dap_assert_PIF(!strcmp(s_group, l_store_obj_cur.group), "Wrong group");
            dap_assert_PIF(!dap_store_obj_driver_obj_compare(l_store_obj, &l_store_obj_cur), "Records not equal");
            if (!j)
                l_driver_key = dap_global_db_driver_hash_get(l_store_obj);
            l_total_data += sizeof(dap_global_db_pkt_t) + l_cur_pkt->data_len;
            dap_store_obj_free_one(l_store_obj);
        }
        dap_assert_PIF(l_total_data == l_objs->data_size, "Wrong total data size");
        DAP_DEL_MULTY(l_hashes, l_objs);
    }
    l_driver_key = (dap_global_db_driver_hash_t){0};
    size_t l_total_count = 0;
    
    dap_pass_msg("get_by_hash check");
}

static void s_test_get_groups_by_mask(size_t a_count, bool a_bench)
{
    dap_list_t *l_groups = NULL;

    l_groups = dap_global_db_driver_get_groups_by_mask("group.z*");
    dap_assert_PIF(dap_list_length(l_groups) == 1 && !strcmp(s_group, l_groups->data), "Wrong finded group by mask");
    dap_list_free_full(l_groups, NULL);
    DAP_DELETE(l_mask_str);

    l_mask_str = dap_strdup_printf("*%s", s_group_worng + strlen(DAP_DB$T_GROUP_WRONG_PREF));
    l_groups = dap_global_db_driver_get_groups_by_mask(l_mask_str);
    dap_assert_PIF(dap_list_length(l_groups) == 1 && !strcmp(s_group_worng, l_groups->data), "Wrong finded group by mask");
    dap_list_free_full(l_groups, NULL);
    DAP_DELETE(l_mask_str);

    l_mask_str = dap_strdup_printf("*%s", s_group_not_existed + strlen(DAP_DB$T_GROUP_NOT_EXISTED_PREF));
    l_groups = dap_global_db_driver_get_groups_by_mask(l_mask_str);
    dap_assert_PIF(!dap_list_length(l_groups), "Finded not existed groups");
    dap_list_free_full(l_groups, NULL);
    DAP_DELETE(l_mask_str);

    for (size_t i = 0; i < a_count; ++i) {
        uint64_t l_time = get_cur_time_nsec();
        l_groups = dap_global_db_driver_get_groups_by_mask("group.*");
        s_get_groups_by_mask += a_bench ? get_cur_time_nsec() - l_time : 0;

        dap_assert_PIF(dap_list_length(l_groups) == 2, "Wrong finded groups by mask");
        dap_list_free_full(l_groups, NULL);
    }
    dap_pass_msg("get_groups_by_mask check");
}

static void s_test_flush()
{
    dap_global_db_driver_flush();
}

static void s_test_tx_start_end(size_t a_count, bool a_missing_allow)
{   
    size_t l_count = 0;
    dap_store_obj_t *l_objs = dap_global_db_driver_cond_read(s_group, (dap_global_db_driver_hash_t){0}, &l_count, true);
    dap_global_db_driver_hash_t l_hash_last = dap_global_db_driver_hash_get(l_objs + l_count - 1);
    // erase some records
    for (size_t i = 0; i < l_count; ++i) {
        l_objs[i].flags |= DAP_GLOBAL_DB_RECORD_ERASE;
    }

    s_tx_start_end_erase = get_cur_time_nsec();
    int ret = dap_global_db_driver_apply(l_objs, l_count);
    s_tx_start_end_erase = get_cur_time_nsec() - s_tx_start_end_erase;

    if (!a_missing_allow) {
        dap_assert_PIF(!ret || ret == DAP_GLOBAL_DB_RC_NOT_FOUND, "Erased records from DB");
        dap_assert_PIF(a_count - l_count + dap_global_db_driver_hash_is_blank(&l_hash_last) == dap_global_db_driver_count(s_group, (dap_global_db_driver_hash_t){0}, true), "Wrong records count after erasing");
    }
    // restore erased records
    for (size_t i = 0; i < l_count; ++i) {
        l_objs[i].flags &= ~DAP_GLOBAL_DB_RECORD_ERASE;
    }

    s_tx_start_end_add = get_cur_time_nsec();
    ret = dap_global_db_driver_apply(l_objs, l_count);
    s_tx_start_end_add = get_cur_time_nsec() - s_tx_start_end_add;

    dap_assert_PIF(!ret, "Restore records to DB");
    if (!a_missing_allow) {
        dap_assert_PIF(a_count == dap_global_db_driver_count(s_group, (dap_global_db_driver_hash_t){0}, true), "Wrong records count after restoring");
    }
    dap_store_obj_free(l_objs, l_count);
    dap_pass_msg("tx_start tx_end check");
}

static void s_test_close_db(void)
{
    dap_global_db_driver_deinit();
    dap_test_msg("Close global_db");
    log_it(L_NOTICE, "Close global_db");
}


static void s_test_all(size_t a_count, bool a_with_value)
{
    s_test_write(a_count, a_with_value);
    s_test_read(a_count, true);
    s_test_read_all(a_count);
    s_test_read_cond_store(a_count, true);
    s_test_count(a_count, true);
    s_test_tx_start_end(a_count, false);  // if after this tests fail try comment

    s_flush = get_cur_time_nsec();
    s_test_flush();
    s_flush = get_cur_time_nsec() - s_flush;

    s_test_is_obj(a_count, true);

    s_test_is_hash(a_count, true);

    s_test_last(a_count, true);

    s_test_read_hashes(a_count, true);
    s_test_get_by_hash(a_count, true);

    s_test_get_groups_by_mask(a_count, true);
}

static void *s_test_thread_rewrite_records(void *a_arg)
{
    size_t a_count = *(size_t *)a_arg;
    uint64_t l_time = 0;
    size_t l_count = 0;
    dap_store_obj_t *l_objs = dap_global_db_driver_cond_read(s_group, (dap_global_db_driver_hash_t){0}, &l_count, true);
    if (l_count) {
        // erase some records
        for (size_t i = 0; i < l_count; ++i) {
            l_objs[i].flags |= DAP_GLOBAL_DB_RECORD_ERASE;
        }

        int ret = dap_global_db_driver_apply(l_objs, l_count);

        // restore erased records
        for (size_t i = 0; i < l_count; ++i) {
            l_objs[i].flags &= ~DAP_GLOBAL_DB_RECORD_ERASE;
        }

        ret = dap_global_db_driver_apply(l_objs, l_count);

        dap_assert_PIF(!ret, "Restore records to DB");
        dap_store_obj_free(l_objs, l_count);
        dap_pass_msg("tx_start tx_end check");
    }
    pthread_exit(NULL);
    return NULL;
}

static void *s_test_thread(void *a_arg)
{
    size_t l_count = *(size_t *)a_arg;
    s_test_read(l_count, false);
    s_test_read_cond_store(l_count, false);
    s_test_count(l_count, false);
    s_test_flush();
    s_test_is_obj(l_count, false);
    s_test_is_hash(l_count, false);
    s_test_last(l_count, false);
    s_test_read_hashes(l_count, false);
    s_test_get_by_hash(l_count, false);
    s_test_get_groups_by_mask(l_count, false);
    pthread_exit(NULL);
    return NULL;
}

static void s_test_multithread(size_t a_count)
{
    uint32_t l_thread_count = 3;
#ifdef DAP_CHAIN_GDB_ENGINE_SQLITE
    dap_global_db_driver_sqlite_set_attempts_count(l_thread_count);
#endif
    dap_test_msg("Test with %u threads", l_thread_count);
    pthread_t *l_threads = DAP_NEW_Z_COUNT(pthread_t, l_thread_count);

    size_t l_objs_count = 0;
    dap_store_obj_t *l_objs = dap_global_db_driver_cond_read(s_group, (dap_global_db_driver_hash_t){0}, &l_objs_count, true);

    for (uint32_t i = 0; i < l_thread_count; ++i) {
        pthread_create(l_threads + i, NULL, s_test_thread_rewrite_records, &a_count);
    }
    for (uint32_t i = 0; i < l_thread_count; ++i) {
        pthread_join(l_threads[i], NULL);
    }

    size_t l_new_objs_count = 0;
    dap_store_obj_t *l_new_objs = dap_global_db_driver_cond_read(s_group, (dap_global_db_driver_hash_t){0}, &l_new_objs_count, true);

    dap_assert_PIF(l_objs_count == l_new_objs_count, "The amount of data in the GDB table before the multithreaded test and after it.");
    for (size_t i = 0; i < l_objs_count; i++) {
        dap_assert_PIF(!dap_strcmp(l_objs[i].key, l_new_objs[i].key), "In the array, the key of the same objects are not equal");
        dap_assert_PIF(l_objs[i].value_len == l_new_objs[i].value_len, "In the array, the lengths of the same objects are not equal");
        dap_assert_PIF(!memcmp(l_objs[i].value, l_new_objs[i].value, l_objs[i].value_len), "In the array, the value of the same objects are not equal");
    }
    dap_store_obj_free(l_new_objs, l_new_objs_count);
    dap_store_obj_free(l_objs, l_objs_count);

    for (uint32_t i = 0; i < l_thread_count; ++i) {
        pthread_create(l_threads + i, NULL, s_test_thread, &a_count);
    }
    for (uint32_t i = 0; i < l_thread_count; ++i) {
        pthread_join(l_threads[i], NULL);
    }
    DAP_DEL_Z(l_threads);
    dap_pass_msg("multithread check");
}

void s_test_table_erase() {
    dap_test_msg("Start erase tables");

    dap_store_obj_t l_erase_table_obj = {
        .flags = DAP_GLOBAL_DB_RECORD_NEW | DAP_GLOBAL_DB_RECORD_ERASE,
        .timestamp = dap_nanotime_now()
    };
    l_erase_table_obj.group = s_group;
    dap_global_db_driver_apply(&l_erase_table_obj, 1);
    dap_assert_PIF(!dap_global_db_driver_cond_read(s_group, (dap_global_db_driver_hash_t){0}, NULL, true), "Erase zero table");
    l_erase_table_obj.group = s_group_worng;
    dap_global_db_driver_apply(&l_erase_table_obj, 1);
    dap_assert_PIF(!dap_global_db_driver_cond_read(s_group_worng, (dap_global_db_driver_hash_t){0}, NULL, true), "Erase wrong table");
    l_erase_table_obj.group = s_group_not_existed;
    dap_global_db_driver_apply(&l_erase_table_obj, 1);
    dap_assert_PIF(!dap_global_db_driver_cond_read(s_group_not_existed, (dap_global_db_driver_hash_t){0}, NULL, true), "Erase not existed table");
    dap_assert(true, "Table erased");
}

static void s_test_full(size_t a_db_count, size_t a_count, bool a_with_value)
{
    for (size_t i = 0; i < a_db_count; ++i) {
        s_write = 0;
        s_rewrite = 0;
        s_read = 0;
        s_read_all_with_holes = 0;
        s_read_all_without_holes = 0;
        s_read_cond_store = 0;
        s_count_with_holes = 0;
        s_count_without_holes = 0;
        s_tx_start_end_erase = 0;
        s_tx_start_end_add = 0;
        s_flush = 0;
        s_is_obj = 0;
        s_is_obj_wrong = 0;
        s_is_obj_not_existed = 0;
        s_is_hash = 0;
        s_is_hash_wrong = 0;
        s_is_hash_not_existed = 0;
        s_last_with_holes = 0;
        s_last_without_holes = 0;
        s_last_with_holes_wrong = 0;
        s_last_without_holes_wrong = 0;
        s_last_with_holes_not_existed = 0;
        s_last_without_holes_not_existed = 0;
        s_read_hashes = 0;
        s_read_hashes_wrong = 0;
        s_read_hashes_not_existed = 0;
        s_get_by_hash = 0;
        s_get_groups_by_mask = 0;

        dap_print_module_name(s_db_types[i]);
        s_test_create_db(s_db_types[i]);
        uint64_t l_t1 = get_cur_time_nsec();
        s_test_all(a_count, true);
        uint64_t l_t2 = get_cur_time_nsec();
        char l_msg[120] = {0};
        sprintf(l_msg, "All tests to %zu records", a_count);
        dap_print_module_name("Multithread");
        s_test_multithread(a_count);
        dap_print_module_name("Benchmark");
        benchmark_mgs_time("Tests to write", s_write / 1000000);
        benchmark_mgs_time("Tests to rewrite", s_rewrite / 1000000);
        benchmark_mgs_time("Tests to read", s_read / 1000000);
        benchmark_mgs_time("Tests to read_all with holes", s_read_all_with_holes / 1000000);
        benchmark_mgs_time("Tests to read_all without holes", s_read_all_without_holes / 1000000);
        benchmark_mgs_time("Tests to read_cond_store", s_read_cond_store / 1000000);
        benchmark_mgs_time("Tests to count with holes", s_count_with_holes / 1000000);
        benchmark_mgs_time("Tests to count without holes", s_count_without_holes / 1000000);
        benchmark_mgs_time("Tests to tx_start_end erase record", s_tx_start_end_erase / 1000000);
        benchmark_mgs_time("Tests to tx_start_end add record", s_tx_start_end_add / 1000000);
        benchmark_mgs_time("Tests to flush", s_flush / 1000000);
        benchmark_mgs_time("Tests to is_obj", s_is_obj / 1000000);
        benchmark_mgs_time("Tests to is_obj in wrong group", s_is_obj_wrong / 1000000);
        benchmark_mgs_time("Tests to is_obj in not existed group", s_is_obj_not_existed / 1000000);
        benchmark_mgs_time("Tests to is_hash", s_is_hash / 1000000);
        benchmark_mgs_time("Tests to is_hash in wrong group", s_is_hash_wrong / 1000000);
        benchmark_mgs_time("Tests to is_hash in not existed group", s_is_hash_not_existed / 1000000);
        benchmark_mgs_time("Tests to last with holes", s_last_with_holes / 1000000);
        benchmark_mgs_time("Tests to last without holes", s_last_without_holes / 1000000);
        benchmark_mgs_time("Tests to last with holes in wrong group", s_last_with_holes_wrong / 1000000);
        benchmark_mgs_time("Tests to last without holes in wrong group", s_last_without_holes_wrong / 1000000);
        benchmark_mgs_time("Tests to last with holes in not existed group", s_last_with_holes_not_existed / 1000000);
        benchmark_mgs_time("Tests to last without holes in not existed group", s_last_without_holes_not_existed / 1000000);
        benchmark_mgs_time("Tests to read_hashes", s_read_hashes / 1000000);
        benchmark_mgs_time("Tests to read_hashes in wrong group", s_read_hashes_wrong / 1000000);
        benchmark_mgs_time("Tests to read_hashes in not existed group", s_read_hashes_not_existed / 1000000);
        benchmark_mgs_time("Tests to get_by_hash", s_get_by_hash / 1000000);
        benchmark_mgs_time("Tests to get_groups_by_mask", s_get_groups_by_mask / 1000000);
        benchmark_mgs_time(l_msg, (l_t2 - l_t1) / 1000000);
        s_test_table_erase();
        s_test_close_db();
    }
}

int main(int argc, char **argv)
{
    dap_log_level_set(L_DEBUG);
    size_t l_db_count = sizeof(s_db_types) / sizeof(char *) - 1;
    dap_assert_PIF(l_db_count, "Use minimum 1 DB driver");
    size_t l_count = DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT + 2;
    dap_assert_PIF(!(l_count % DAP_DB$SZ_HOLES), "If l_count \% DAP_DB$SZ_HOLES tests will fail");
    dap_print_module_name("Tests with value");
    s_test_full(l_db_count, l_count, true);
    dap_print_module_name("Tests without value");
    s_test_full(l_db_count, l_count, false);
}

