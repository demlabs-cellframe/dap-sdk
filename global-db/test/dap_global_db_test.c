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

// benchmarks
static int    s_write = 0;
static int    s_read = 0;
static int    s_read_cond_store = 0;
static int    s_count = 0;
static int    s_tx_start_end = 0;
static int    s_flush = 0;
static int    s_is_obj = 0;
static int    s_is_hash = 0;
static int    s_last = 0;
static int    s_read_hashes = 0;
static int    s_get_by_hash = 0;
static int    s_get_groups_by_mask = 0;


typedef struct __dap_test_record__ {
    dap_chain_hash_fast_t   csum;                                           /* CRC32 , cover <len> and <data> fields */
    unsigned    len;                                                        /* Length of the <data> field */
    char        data[];                                                     /* Place holder for data area */
} dap_db_test_record_t;

#define DAP_DB$SZ_DATA                  8192
#define DAP_DB$SZ_KEY                   64
#define DAP_DB$SZ_HOLES                 3
#define DAP_DB$T_GROUP                  "group.zero"
#define DAP_DB$T_GROUP_WRONG            "group.wrong"
#define DAP_DB$T_GROUP_NOT_EXISTED      "group.not.existed"


static int s_test_create_db(const char *db_type)
{
    int rc;
    char l_cmd[MAX_PATH];
    dap_test_msg("Initializatiion test db %s driver in %s file", db_type, DB_FILE);

    if( dap_dir_test(DB_FILE) ) {
        rmdir(DB_FILE);
        snprintf(l_cmd, sizeof(l_cmd), "rm -rf %s", DB_FILE);
        if ( (rc = system(l_cmd)) )
             log_it(L_ERROR, "system(%s)->%d", l_cmd, rc);
    }
    else
        unlink(DB_FILE);
    rc = dap_global_db_driver_init(db_type, DB_FILE);
    dap_assert(rc == 0, "Initialization db driver");
    return rc;
}

static int s_test_write(size_t a_count)
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
    l_store_obj.value = (uint8_t *) l_value;                                 /* Point <.value> to static buffer area */
    prec = (dap_db_test_record_t *) l_value;
    int l_time = 0;
    size_t l_rewrite_count = rand() % (a_count / 2) + 2; 
    for (size_t i = 0; i < a_count; ++i)
    {
        log_it(L_DEBUG, "Write %zu record in GDB", i);

        l_store_obj.group = DAP_DB$T_GROUP; 
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08zx", i); // add bad to check rewrite          /* Generate a key of record */

        clock_gettime(CLOCK_REALTIME, &now);                                /* Get and save record's timestamp */
        l_store_obj.timestamp = ((uint64_t)now.tv_sec << 32) | ((uint32_t) (now.tv_nsec));

        prec->len = rand() % DAP_DB$SZ_DATA + 1;                                /* Variable payload length */
        l_pvalue   = (int *) prec->data;
        for (int  i = prec->len / sizeof(int); i--; l_pvalue++)             /* Fill record's payload with random data */
            *l_pvalue = rand() + 1;
        sprintf(prec->data, "DATA$%08zx%s", i, i < l_rewrite_count ? "rw" : "");                         /* Just for fun ... */
        l_value_len = prec->len + sizeof(dap_db_test_record_t);
        l_store_obj.value_len = l_value_len;
        assert(l_store_obj.value_len < sizeof(l_value));

        dap_hash_fast (prec->data, prec->len, &prec->csum);                 /* Compute a hash of the payload part of the record */

        if (i >= l_rewrite_count) {
            l_store_obj.flags = i % DAP_DB$SZ_HOLES ? 0 : DAP_GLOBAL_DB_RECORD_DEL;
        }
        l_store_obj.sign = dap_store_obj_sign(&l_store_obj, l_enc_key, &l_store_obj.crc);
        log_it(L_DEBUG, "Store object: [%s, %s, %zu octets]", l_store_obj.group, l_store_obj.key, l_store_obj.value_len);

        l_time = get_cur_time_msec();
        ret = dap_global_db_driver_add(&l_store_obj, 1);
        s_write += get_cur_time_msec() - l_time;
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
            
            l_time = get_cur_time_msec();
            ret = dap_global_db_driver_add(&l_store_obj, 1);
            s_write += get_cur_time_msec() - l_time;
            dap_assert_PIF(!ret, "Rewrite with key conflict record to DB");
        }

        l_store_obj.group = DAP_DB$T_GROUP_WRONG;
        l_store_obj.crc = i + 1;
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%09zx", i);

        l_time = get_cur_time_msec();
        ret = dap_global_db_driver_add(&l_store_obj, 1);
        s_write += get_cur_time_msec() - l_time;
        dap_assert_PIF(!ret, "Write record to wrong group DB");
        DAP_DEL_Z(l_store_obj.sign);
    }
    dap_enc_key_delete(l_enc_key);
    dap_pass_msg("apply check");
    return  0;
}

static int s_test_read(size_t a_count)
{
    dap_test_msg("Start reading %zu records ...", a_count);
    int l_time = 0;
    for (size_t i = 0; i < a_count; ++i ) {
        dap_chain_hash_fast_t csum = { 0 };;
        dap_db_test_record_t *prec = NULL;
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08zx", i);           /* Generate a key of record */

        l_time = get_cur_time_msec();
        dap_store_obj_t *l_store_obj = dap_global_db_driver_read(DAP_DB$T_GROUP, l_key, NULL, true);
        s_read += get_cur_time_msec() - l_time;

        dap_assert_PIF(l_store_obj, "Record-Not-Found");
        if (l_store_obj->sign)  // to test rewriting with hash conflict some records wiwthout sign
            dap_assert_PIF(dap_global_db_pkt_check_sign_crc(l_store_obj), "Record sign not verified");
        dap_assert_PIF(!strcmp(DAP_DB$T_GROUP, l_store_obj->group), "Check group name");
        dap_assert_PIF(!strcmp(l_key, l_store_obj->key), "Check key name");

        prec = (dap_db_test_record_t *) l_store_obj->value;
        log_it(L_DEBUG, "Retrieved object: [%s, %s, %zu octets]", l_store_obj->group, l_store_obj->key,
                     l_store_obj->value_len);
        log_it(L_DEBUG, "Record: ['%.*s', %d octets]", prec->len, prec->data, prec->len);
        dap_hash_fast(prec->data, prec->len,
                      &csum);                       /* Compute a hash of the payload part of the record */
        dap_assert_PIF(memcmp(&csum, &prec->csum, sizeof(dap_chain_hash_fast_t)) == 0,
                       "Record check sum"); /* Integriry checking ... */
        dap_store_obj_free_one(l_store_obj);
    }
    dap_pass_msg("read check");

    return  0;
}


static void s_test_read_cond_store(size_t a_count)
{
    dap_global_db_driver_hash_t l_driver_key = {0};
    size_t l_count = 0;
    int l_time = 0;
    for (size_t i = 0; i < a_count; ++i) {
        l_time = get_cur_time_msec();
        dap_store_obj_t *l_objs = dap_global_db_driver_cond_read(DAP_DB$T_GROUP, l_driver_key, &l_count, true);
        s_read_cond_store += get_cur_time_msec() - l_time;
        dap_assert_PIF(l_objs, "Records-Not-Found");
        dap_global_db_driver_hash_t l_blank_check = dap_global_db_driver_hash_get(l_objs + l_count - 1);
        dap_assert_PIF(l_count <= DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT + dap_global_db_driver_hash_is_blank(&l_blank_check), "Wrong finded records count");
        for (size_t j = i, k = 0; j < a_count && k < l_count; ++j, ++k) {
            char l_key[64] = { 0 };
            snprintf(l_key, sizeof(l_key) - 1, "KEY$%08zx", j);           /* Generate a key of record */
            dap_store_obj_t *l_store_obj = dap_global_db_driver_read(DAP_DB$T_GROUP, l_key, NULL, true);
            dap_assert_PIF(l_store_obj, "Record-Not-Found");
            dap_assert_PIF(!strcmp(DAP_DB$T_GROUP, (l_objs + k)->group), "Wrong group");
            dap_assert_PIF(!dap_store_obj_driver_obj_compare(l_store_obj, l_objs + k), "Records not equal");
            if (i == j)
                l_driver_key = dap_global_db_driver_hash_get(l_store_obj);
            dap_store_obj_free_one(l_store_obj);
        }
        dap_store_obj_free(l_objs, l_count);
    }

    l_time = get_cur_time_msec();
    l_count = 99;
    l_driver_key = (dap_global_db_driver_hash_t){0};
    size_t l_total_count = 0;
    for (dap_store_obj_t *l_objs = dap_global_db_driver_cond_read(DAP_DB$T_GROUP, l_driver_key, &l_count, true);
            l_objs;
            l_objs = dap_global_db_driver_cond_read(DAP_DB$T_GROUP, l_driver_key, &l_count, true)) {
        l_driver_key = dap_global_db_driver_hash_get(l_objs + l_count - 1);
        dap_store_obj_free(l_objs, l_count);
        l_total_count += l_count;
        if (dap_global_db_driver_hash_is_blank(&l_driver_key)) {
            break;
        }
    }
    s_read_cond_store += get_cur_time_msec() - l_time;
    dap_assert_PIF(l_total_count - dap_global_db_driver_hash_is_blank(&l_driver_key) == a_count, "Total cond read count with holes not equal total records count");

    l_time = get_cur_time_msec();
    l_count = 99;
    l_driver_key = (dap_global_db_driver_hash_t){0};
    l_total_count = 0;
    for (dap_store_obj_t *l_objs = dap_global_db_driver_cond_read(DAP_DB$T_GROUP, l_driver_key, &l_count, false);
            l_objs;
            l_objs = dap_global_db_driver_cond_read(DAP_DB$T_GROUP, l_driver_key, &l_count, false)) {
        l_driver_key = dap_global_db_driver_hash_get(l_objs + l_count - 1);
        dap_store_obj_free(l_objs, l_count);
        l_total_count += l_count;
        if (dap_global_db_driver_hash_is_blank(&l_driver_key)) {
            break;
        }
    }
    s_read_cond_store += get_cur_time_msec() - l_time;
    dap_assert_PIF(l_total_count - dap_global_db_driver_hash_is_blank(&l_driver_key) == a_count / DAP_DB$SZ_HOLES * (DAP_DB$SZ_HOLES - 1), "Total cond read count without holes not equal total records count");
    dap_pass_msg("read_cond_store check");
}

static void s_test_count(size_t a_count)
{
    dap_global_db_driver_hash_t l_driver_key = {0};
    int l_time = 0;
    for (size_t i = 0; i < a_count; ++i) {
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08zx", i);  
        dap_store_obj_t *l_store_obj = dap_global_db_driver_read(DAP_DB$T_GROUP, l_key, NULL, true);
        dap_assert_PIF(l_store_obj, "Records-Not-Found");
        
        l_time = get_cur_time_msec();
        dap_assert_PIF(a_count - i == dap_global_db_driver_count(DAP_DB$T_GROUP, l_driver_key, true), "Count with holes");
        s_count += get_cur_time_msec() - l_time;
        
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
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08zx", i);  
        dap_store_obj_t *l_store_obj = dap_global_db_driver_read(DAP_DB$T_GROUP, l_key, NULL, false);
        dap_assert_PIF(l_store_obj, "Records-Not-Found");
        
        l_time = get_cur_time_msec();
        dap_assert_PIF(a_count / DAP_DB$SZ_HOLES * (DAP_DB$SZ_HOLES - 1) - k == dap_global_db_driver_count(DAP_DB$T_GROUP, l_driver_key, false), "Count without holes");
        s_count += get_cur_time_msec() - l_time;

        l_driver_key = dap_global_db_driver_hash_get(l_store_obj);
        dap_store_obj_free_one(l_store_obj);
    }
    
    dap_assert_PIF(a_count == dap_global_db_driver_count(DAP_DB$T_GROUP_WRONG, (dap_global_db_driver_hash_t){0}, true), "Count in wrong group with holes");
    dap_assert_PIF(a_count / DAP_DB$SZ_HOLES * (DAP_DB$SZ_HOLES - 1) == dap_global_db_driver_count(DAP_DB$T_GROUP_WRONG, (dap_global_db_driver_hash_t){0}, false), "Count in wrong group without holes");
    dap_assert_PIF(!dap_global_db_driver_count(DAP_DB$T_GROUP_NOT_EXISTED, (dap_global_db_driver_hash_t){0}, true), "Count in not existed group with holes");
    dap_assert_PIF(!dap_global_db_driver_count(DAP_DB$T_GROUP_NOT_EXISTED, (dap_global_db_driver_hash_t){0}, false), "Count in not existed group without holes");
    dap_pass_msg("count check");
}

static void s_test_is_obj(size_t a_count)
{
    for (size_t i = 0; i < a_count; ++i) {
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08zx", i);           /* Generate a key of record */
        dap_assert_PIF(dap_global_db_driver_is(DAP_DB$T_GROUP, l_key), "Key not finded")
        dap_assert_PIF(!dap_global_db_driver_is(DAP_DB$T_GROUP_WRONG, l_key), "Key finded in wrong group")
        dap_assert_PIF(!dap_global_db_driver_is(DAP_DB$T_GROUP_NOT_EXISTED, l_key), "Key finded in not existed group")
    }
    for (size_t i = a_count; i < a_count * 2; ++i) {
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08zx", i);           /* Generate a key of record */
        dap_assert_PIF(!dap_global_db_driver_is(DAP_DB$T_GROUP, l_key), "Finded not existed key")
        dap_assert_PIF(!dap_global_db_driver_is(DAP_DB$T_GROUP_WRONG, l_key), "Finded not existed key in wrong group")
        dap_assert_PIF(!dap_global_db_driver_is(DAP_DB$T_GROUP_NOT_EXISTED, l_key), "Finded not existed key in not existed group")
    }
    dap_pass_msg("is_obj check");
}

static void s_test_is_hash(size_t a_count)
{
    for (size_t i = 0; i < a_count; ++i) {
        char l_key[64] = { 0 };
        snprintf(l_key, sizeof(l_key) - 1, "KEY$%08zx", i);           /* Generate a key of record */
        dap_store_obj_t *l_store_obj = dap_global_db_driver_read(DAP_DB$T_GROUP, l_key, NULL, true);
        dap_assert_PIF(l_store_obj, "Record-Not-Found");
        dap_global_db_driver_hash_t l_driver_key = dap_global_db_driver_hash_get(l_store_obj);
        
        int l_time = get_cur_time_msec();
        dap_assert_PIF(dap_global_db_driver_is_hash(DAP_DB$T_GROUP, l_driver_key), "Hash not finded")
        dap_assert_PIF(!dap_global_db_driver_is_hash(DAP_DB$T_GROUP_WRONG, l_driver_key), "Hash finded in wrong group")
        dap_assert_PIF(!dap_global_db_driver_is_hash(DAP_DB$T_GROUP_NOT_EXISTED, l_driver_key), "Hash finded in not existed group")
        l_driver_key.becrc = 0;
        dap_assert_PIF(!dap_global_db_driver_is_hash(DAP_DB$T_GROUP, l_driver_key), "Finded not existed hash")
        dap_assert_PIF(!dap_global_db_driver_is_hash(DAP_DB$T_GROUP_WRONG, l_driver_key), "Finded not existed hash in wrong group")
        dap_assert_PIF(!dap_global_db_driver_is_hash(DAP_DB$T_GROUP_NOT_EXISTED, l_driver_key), "Finded not existed hash in not existed group")
        s_is_hash = get_cur_time_msec() - l_time;
        
        dap_store_obj_free_one(l_store_obj);
    }
    dap_pass_msg("is_hash check");
}

static void s_test_last(size_t a_count)
{
    char l_key[64] = { 0 };
    // with holes
    snprintf(l_key, sizeof(l_key) - 1, "KEY$%08zx", a_count - 1);
    dap_store_obj_t *l_store_obj = dap_global_db_driver_read_last(DAP_DB$T_GROUP, true);
    dap_assert_PIF(l_store_obj && !strcmp(l_key, l_store_obj->key), "Last with holes");
    dap_store_obj_free_one(l_store_obj);

    l_store_obj = dap_global_db_driver_read_last(DAP_DB$T_GROUP_WRONG, true);
    dap_assert_PIF(l_store_obj && strcmp(l_key, l_store_obj->key), "Last with holes in wrong group");
    dap_store_obj_free_one(l_store_obj);

    l_store_obj = dap_global_db_driver_read_last(DAP_DB$T_GROUP_NOT_EXISTED, true);
    dap_assert_PIF(!l_store_obj, "Last with holes in not existed group");
    // without holes
    snprintf(l_key, sizeof(l_key) - 1, "KEY$%08zx", a_count - 1 - a_count % DAP_DB$SZ_HOLES);
    l_store_obj = dap_global_db_driver_read_last(DAP_DB$T_GROUP, false);
    dap_assert_PIF(l_store_obj && !strcmp(l_key, l_store_obj->key), "Last without holes");
    dap_store_obj_free_one(l_store_obj);

    l_store_obj = dap_global_db_driver_read_last(DAP_DB$T_GROUP_WRONG, false);
    dap_assert_PIF(l_store_obj && strcmp(l_key, l_store_obj->key), "Last without holes in wrong group");
    dap_store_obj_free_one(l_store_obj);

    l_store_obj = dap_global_db_driver_read_last(DAP_DB$T_GROUP_NOT_EXISTED, false);
    dap_assert_PIF(!l_store_obj, "Last without holes in not existed group");
    dap_pass_msg("read_last check");
}


static void s_test_read_hashes(size_t a_count)
{
    dap_global_db_driver_hash_t l_driver_key = {0};
    int l_time = 0;
    for (size_t i = 0; i < a_count; ++i) {
        l_time = get_cur_time_msec();
        dap_global_db_hash_pkt_t *l_hashes = dap_global_db_driver_hashes_read(DAP_DB$T_GROUP, l_driver_key);
        dap_global_db_hash_pkt_t *l_hashes_wrong = dap_global_db_driver_hashes_read(DAP_DB$T_GROUP_WRONG, l_driver_key);
        dap_global_db_hash_pkt_t *l_hashes_not_existed = dap_global_db_driver_hashes_read(DAP_DB$T_GROUP_NOT_EXISTED, l_driver_key);
        s_read_hashes += get_cur_time_msec() - l_time;

        dap_assert_PIF(l_hashes && l_hashes_wrong, "Hashes-Not-Found");
        dap_assert_PIF(!l_hashes_not_existed, "Finded hashes in not existed group");
        size_t l_bias = l_hashes->group_name_len;
        for (size_t j = i, k = 0; j < a_count && k < DAP_GLOBAL_DB_COND_READ_KEYS_DEFAULT; ++j, ++k) {
            char l_key[64] = { 0 };
            snprintf(l_key, sizeof(l_key) - 1, "KEY$%08zx", j);           /* Generate a key of record */
            dap_store_obj_t *l_store_obj = dap_global_db_driver_read(DAP_DB$T_GROUP, l_key, NULL, true);
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

static void s_test_get_by_hash(size_t a_count)
{
    dap_global_db_driver_hash_t l_driver_key = {0};
    size_t l_count = 99;
    int l_time = 0;
    for (size_t i = 0; i < a_count; ++i) {
        dap_global_db_hash_pkt_t *l_hashes = dap_global_db_driver_hashes_read(DAP_DB$T_GROUP, l_driver_key);
        
        l_time = get_cur_time_msec();
        dap_global_db_pkt_pack_t *l_objs = dap_global_db_driver_get_by_hash(DAP_DB$T_GROUP, (dap_global_db_driver_hash_t *)(l_hashes->group_n_hashses + l_hashes->group_name_len), l_hashes->hashes_count);
        s_get_by_hash += get_cur_time_msec() - l_time;

        dap_assert_PIF(l_objs, "Records-Not-Found");
        dap_assert_PIF(l_objs->obj_count == l_hashes->hashes_count - dap_global_db_driver_hash_is_blank((dap_global_db_driver_hash_t *)(l_hashes->group_n_hashses + l_hashes->group_name_len) + l_hashes->hashes_count - 1), "Wrong finded records count");
        size_t l_total_data = 0;
        for (size_t j = 0; j < l_hashes->hashes_count - dap_global_db_driver_hash_is_blank((dap_global_db_driver_hash_t *)(l_hashes->group_n_hashses + l_hashes->group_name_len) + l_hashes->hashes_count - 1); ++j) {
            char l_key[64] = { 0 };
            snprintf(l_key, sizeof(l_key) - 1, "KEY$%08zx", i + j);           /* Generate a key of record */
            dap_store_obj_t *l_store_obj = dap_global_db_driver_read(DAP_DB$T_GROUP, l_key, NULL, true);
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
            dap_assert_PIF(!strcmp(DAP_DB$T_GROUP, l_store_obj_cur.group), "Wrong group");
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
    
    for (dap_global_db_hash_pkt_t *l_hashes = dap_global_db_driver_hashes_read(DAP_DB$T_GROUP, l_driver_key);
            l_hashes;
            l_hashes = dap_global_db_driver_hashes_read(DAP_DB$T_GROUP, l_driver_key)) {
        l_driver_key = ((dap_global_db_driver_hash_t *)(l_hashes->group_n_hashses + l_hashes->group_name_len))[l_hashes->hashes_count - 1];
        
        l_time = get_cur_time_msec();
        dap_global_db_pkt_pack_t *l_objs = dap_global_db_driver_get_by_hash(DAP_DB$T_GROUP, (dap_global_db_driver_hash_t *)(l_hashes->group_n_hashses + l_hashes->group_name_len), l_hashes->hashes_count);
        s_get_by_hash += get_cur_time_msec() - l_time;
        
        l_total_count += l_hashes->hashes_count;
        DAP_DEL_MULTY(l_hashes, l_objs);
        if (dap_global_db_driver_hash_is_blank(&l_driver_key)) {
            break;
        }
    }
    dap_assert_PIF(l_total_count - dap_global_db_driver_hash_is_blank(&l_driver_key) == a_count, "Total get by hash count not equal total records count");
    dap_pass_msg("get_by_hash check");
}

static void s_test_get_groups_by_mask()
{
    dap_list_t *l_groups = NULL;
    l_groups = dap_global_db_driver_get_groups_by_mask("group.z*");
    dap_assert_PIF(dap_list_length(l_groups) == 1 && !strcmp(DAP_DB$T_GROUP, l_groups->data), "Wrong finded group by mask");
    dap_list_free_full(l_groups, NULL);

    l_groups = dap_global_db_driver_get_groups_by_mask("group.w*");
    dap_assert_PIF(dap_list_length(l_groups) == 1 && !strcmp(DAP_DB$T_GROUP_WRONG, l_groups->data), "Wrong finded group by mask");
    dap_list_free_full(l_groups, NULL);

    l_groups = dap_global_db_driver_get_groups_by_mask("group.n*");
    dap_assert_PIF(!dap_list_length(l_groups), "Finded not existed groups");
    dap_list_free_full(l_groups, NULL);

    l_groups = dap_global_db_driver_get_groups_by_mask("group.*");
    dap_assert_PIF(dap_list_length(l_groups) == 2, "Wrong finded groups by mask");
    dap_list_free_full(l_groups, NULL);
    dap_pass_msg("get_groups_by_mask check");
}

static void s_test_flush()
{
    dap_global_db_driver_flush();
}

static void s_test_tx_start_end(size_t a_count, bool a_missing_allow)
{   
    int l_time = 0;
    size_t l_count = 0;
    dap_store_obj_t *l_objs = dap_global_db_driver_cond_read(DAP_DB$T_GROUP, (dap_global_db_driver_hash_t){0}, &l_count, true);
    dap_global_db_driver_hash_t l_hash_last = dap_global_db_driver_hash_get(l_objs + l_count - 1);
    // erase some records
    for (size_t i = 0; i < l_count; ++i) {
        l_objs[i].flags |= DAP_GLOBAL_DB_RECORD_ERASE;
    }

    l_time = get_cur_time_msec();
    int ret = dap_global_db_driver_apply(l_objs, l_count);
    s_tx_start_end += get_cur_time_msec() - l_time;

    if (!a_missing_allow) {
        dap_assert_PIF(!ret || ret == DAP_GLOBAL_DB_RC_NOT_FOUND, "Erased records from DB");
        dap_assert_PIF(a_count - l_count + dap_global_db_driver_hash_is_blank(&l_hash_last) == dap_global_db_driver_count(DAP_DB$T_GROUP, (dap_global_db_driver_hash_t){0}, true), "Wrong records count after erasing");
    }
    // restore erased records
    for (size_t i = 0; i < l_count; ++i) {
        l_objs[i].flags &= ~DAP_GLOBAL_DB_RECORD_ERASE;
    }

    l_time = get_cur_time_msec();
    ret = dap_global_db_driver_apply(l_objs, l_count);
    s_tx_start_end += get_cur_time_msec() - l_time;

    dap_assert_PIF(!ret, "Restore records to DB");
    if (!a_missing_allow) {
        dap_assert_PIF(a_count == dap_global_db_driver_count(DAP_DB$T_GROUP, (dap_global_db_driver_hash_t){0}, true), "Wrong records count after restoring");
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


static void s_test_all(size_t a_count)
{
    s_test_write(a_count);
    s_test_read(a_count);
    s_test_read_cond_store(a_count);
    s_test_count(a_count);
    s_test_tx_start_end(a_count, false);  // if after this tests fail try comment

    s_flush = get_cur_time_msec();
    s_test_flush();
    s_flush = get_cur_time_msec() - s_flush;

    s_is_obj = get_cur_time_msec();
    s_test_is_obj(a_count);
    s_is_obj = get_cur_time_msec() - s_is_obj;

    s_test_is_hash(a_count);

    s_last = get_cur_time_msec();
    s_test_last(a_count);
    s_last = get_cur_time_msec() - s_last;

    s_test_read_hashes(a_count);
    s_test_get_by_hash(a_count);

    s_get_groups_by_mask = get_cur_time_msec();
    s_test_get_groups_by_mask();
    s_get_groups_by_mask = get_cur_time_msec() - s_get_groups_by_mask;
}


static void *s_test_thread_rewrite_records(void *a_arg)
{
    size_t l_count = *(size_t *)a_arg;
    s_test_tx_start_end(l_count, true);
    pthread_exit(NULL);
    return NULL;
}

static void *s_test_thread(void *a_arg)
{
    size_t l_count = *(size_t *)a_arg;
    s_test_read(l_count);
    s_test_read_cond_store(l_count);
    s_test_count(l_count);
    s_test_flush();
    s_test_is_obj(l_count);
    s_test_is_hash(l_count);
    s_test_last(l_count);
    s_test_read_hashes(l_count);
    s_test_get_by_hash(l_count);
    s_test_get_groups_by_mask();
    pthread_exit(NULL);
    return NULL;
}

static void s_test_multithread(size_t a_count)
{
    uint32_t l_thread_count = 2;
    log_it(L_INFO, "Test with %u threads", l_thread_count);
    pthread_t *l_threads = DAP_NEW_Z_COUNT(pthread_t, l_thread_count);

    for (uint32_t i = 0; i < l_thread_count; ++i) {
        pthread_create(l_threads + i, NULL, s_test_thread_rewrite_records, &a_count);
    }
    for (uint32_t i = 0; i < l_thread_count; ++i) {
        pthread_join(l_threads[i], NULL);
    }
    for (uint32_t i = 0; i < l_thread_count; ++i) {
        pthread_create(l_threads + i, NULL, s_test_thread, &a_count);
    }
    for (uint32_t i = 0; i < l_thread_count; ++i) {
        pthread_join(l_threads[i], NULL);
    }
    DAP_DEL_Z(l_threads);
    dap_pass_msg("multithread check");
}

int main(int argc, char **argv)
{
    dap_log_level_set(L_ERROR);
#ifdef DAP_CHAIN_GDB_ENGINE_SQLITE
    dap_print_module_name("SQLite");
    s_test_create_db("sqlite");
#endif
#ifdef DAP_CHAIN_GDB_ENGINE_CUTTDB
    dap_print_module_name("CDB");
    s_test_create_db("cdb");
#endif
#ifdef DAP_CHAIN_GDB_ENGINE_MDBX
    dap_print_module_name("MDBX");
    s_test_create_db("mdbx");
#endif

#ifdef DAP_CHAIN_GDB_ENGINE_PGSQL
    dap_print_module_name("PostgresQL");
    s_test_create_db("pgsql");
#endif

    size_t l_count = DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT + 2;
    int l_t1 = get_cur_time_msec();
    s_test_all(l_count);
    int l_t2 = get_cur_time_msec();
    char l_msg[120] = {0};
    sprintf(l_msg, "Tests to %zu records", l_count);
// dap_print_module_name("Multithread");  // TODO need update test, fail on pipelines
//     s_test_multithread(l_count);
dap_print_module_name("Benchmark");
    benchmark_mgs_time(l_msg, l_t2 - l_t1);
    benchmark_mgs_time("Tests to write", s_write);
    benchmark_mgs_time("Tests to read", s_read);
    benchmark_mgs_time("Tests to read_cond_store", s_read_cond_store);
    benchmark_mgs_time("Tests to count", s_count);
    benchmark_mgs_time("Tests to tx_start_end", s_tx_start_end);
    benchmark_mgs_time("Tests to flush", s_flush);
    benchmark_mgs_time("Tests to is_obj", s_is_obj);
    benchmark_mgs_time("Tests to is_hash", s_is_hash);
    benchmark_mgs_time("Tests to last", s_last);
    benchmark_mgs_time("Tests to read_hashes", s_read_hashes);
    benchmark_mgs_time("Tests to get_by_hash", s_get_by_hash);
    benchmark_mgs_time("Tests to get_groups_by_mask", s_get_groups_by_mask);
    s_test_close_db();
}

