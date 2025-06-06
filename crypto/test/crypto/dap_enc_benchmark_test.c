#include "dap_enc_benchmark_test.h"
#include "dap_sign.h"
#include "dap_pkey.h"
#include "dap_test.h"
#include "dap_enc_test.h"
#include "rand/dap_rand.h"
#include "uthash.h"
#define LOG_TAG "dap_crypto_benchmark_tests"

typedef struct pkey_hash_table {
    uint8_t *pkey_hash;
    dap_pkey_t *pkey;
    UT_hash_handle hh;
} pkey_hash_table_t;

static pkey_hash_table_t *s_pkey_hash_table = NULL;

static dap_pkey_t *s_get_pkey_by_hash_callback(const uint8_t *a_hash)
{
    pkey_hash_table_t *l_finded = NULL;

    pkey_hash_table_t *l_temp, *l_current = NULL;
    HASH_ITER(hh, s_pkey_hash_table, l_current, l_temp){
        if (!memcmp(l_current->pkey_hash, a_hash, DAP_CHAIN_HASH_FAST_SIZE))
            l_finded = l_current;
    }
    return l_finded->pkey;
}

#define KEYS_TOTAL_COUNT 10

/*--------------------------TRANSFER TEST BLOCK--------------------------*/
static void s_transfer_test(dap_enc_key_type_t a_key_type, int a_times, int *a_gen_time, int *a_alice_shared, int *a_bob_shared)
{
    dap_enc_key_t **l_alice_keys = DAP_NEW_Z_COUNT_RET_IF_FAIL(dap_enc_key_t*, a_times);
    dap_enc_key_t **l_bob_keys = DAP_NEW_Z_COUNT_RET_IF_FAIL(dap_enc_key_t*, a_times, l_alice_keys);

    int l_t1 = get_cur_time_msec();

    for (int i = 0; i < a_times; ++i) {
        l_alice_keys[i] = dap_enc_key_new_generate(a_key_type, NULL, 0, NULL, 0, 0);
        dap_assert_PIF(l_alice_keys[i], "Key generate");
    }

    int l_t2 = get_cur_time_msec();
    *a_gen_time = l_t2 - l_t1;

    l_t1 = get_cur_time_msec();
    for(int i = 0; i < a_times; ++i) {
        l_bob_keys[i] = dap_enc_key_new(a_key_type);
        l_bob_keys[i]->pub_key_data_size = l_bob_keys[i]->gen_bob_shared_key(l_bob_keys[i], l_alice_keys[i]->pub_key_data, l_alice_keys[i]->pub_key_data_size, (void**)&l_bob_keys[i]->pub_key_data);
        dap_assert_PIF(l_bob_keys[i]->pub_key_data_size, "Bob shared key gen");
    }
    l_t2 = get_cur_time_msec();
    *a_bob_shared = l_t2 - l_t1;

    l_t1 = get_cur_time_msec();
    for(int i = 0; i < a_times; ++i) {
        l_alice_keys[i]->gen_alice_shared_key(l_alice_keys[i], l_alice_keys[i]->priv_key_data, l_bob_keys[i]->pub_key_data_size, l_bob_keys[i]->pub_key_data);
    }
    l_t2 = get_cur_time_msec();
    *a_alice_shared = l_t2 - l_t1;

    for(int i = 0; i < a_times; ++i) {
        int l_cmp = memcmp(l_alice_keys[i]->shared_key, l_bob_keys[i]->shared_key, l_alice_keys[i]->shared_key_size);
        dap_assert_PIF(!l_cmp, "Session keys equals");
        dap_enc_key_delete(l_alice_keys[i]);
        dap_enc_key_delete(l_bob_keys[i]);
    }
    DAP_DEL_MULTY(l_alice_keys, l_bob_keys);
}

static void s_transfer_test_benchmark(const char *a_name, dap_enc_key_type_t a_key_type, int a_times) {
    dap_print_module_name(a_name);
    int l_gen_time = 0;
    int l_alice_shared = 0;
    int l_bob_shared = 0;

    s_transfer_test(a_key_type, a_times, &l_gen_time, &l_alice_shared, &l_bob_shared);

    char l_msg[120] = {0};
    sprintf(l_msg, "Key gen %d times", a_times);
    benchmark_mgs_time(l_msg, l_gen_time);
    sprintf(l_msg, "Bob shared key gen %d times", a_times);
    benchmark_mgs_time(l_msg, l_bob_shared);
    sprintf(l_msg, "Alice shared key gen %d times", a_times);
    benchmark_mgs_time(l_msg, l_alice_shared);
}
/*-----------------------------------------------------------------------*/

/*------------------SIGNING AND VERIFICATION TEST BLOCK------------------*/
static void s_sign_verify_test(dap_enc_key_type_t a_key_type, int a_times, int *a_sig_time, int *a_verify_time)
{
    size_t seed_size = sizeof(uint8_t);
    uint8_t seed[sizeof(uint8_t)];
    randombytes(seed, seed_size);
    uint8_t *l_signs[a_times], *l_source[a_times];
    dap_enc_key_t *l_keys[a_times];
    size_t l_source_size[a_times];
    dap_enc_key_t *l_key_temp = a_key_type == DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED ? dap_enc_key_new(a_key_type)
                                                                                 : dap_enc_key_new_generate(a_key_type, NULL, 0, seed, seed_size, 0);
    size_t max_signature_size = dap_sign_create_output_unserialized_calc_size(l_key_temp);
    dap_enc_key_delete(l_key_temp);

    int l_t1 = 0;
    *a_sig_time = 0;
    for (int i = 0; i < a_times; ++i) {
        randombytes(seed, seed_size);
        // used only in multisign
        dap_enc_key_type_t l_key[KEYS_TOTAL_COUNT];
        for (int j = 0; j < KEYS_TOTAL_COUNT; j++) {
            int l_step = random_uint32_t(c_keys_count);
            l_key[j] = c_key_type_arr[l_step];
        }
        // ----------
        
        l_signs[i] = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, max_signature_size);
        l_source_size[i] = 1 + random_uint32_t(20);
        l_source[i] = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, l_source_size[i]);
        randombytes(l_source[i], l_source_size[i]);

        l_t1 = get_cur_time_msec();
        int l_signed = 0;
        l_keys[i] = dap_enc_key_new_generate(a_key_type, l_key, KEYS_TOTAL_COUNT, seed, seed_size, 0);
        if (l_keys[i]->type == DAP_ENC_KEY_TYPE_SIG_ECDSA)
            l_signed = l_keys[i]->sign_get(l_keys[i], l_source[i], l_source_size[i], l_signs[i], max_signature_size);
        else {
            dap_chain_hash_fast_t l_hash;
            dap_hash_fast(l_source[i], l_source_size[i], &l_hash);
            l_signed = l_keys[i]->sign_get(l_keys[i], &l_hash, sizeof(l_hash), l_signs[i], max_signature_size);
        }
        *a_sig_time += get_cur_time_msec() - l_t1;
        dap_assert_PIF(!l_signed, "Signing message");
    }

    l_t1 = get_cur_time_msec();
    for(int i = 0; i < a_times; ++i) {
        int l_verified = 0;
        if (l_keys[i]->type == DAP_ENC_KEY_TYPE_SIG_ECDSA)
            l_verified = l_keys[i]->sign_verify(l_keys[i], l_source[i], l_source_size[i], l_signs[i], max_signature_size);
        else {
            dap_chain_hash_fast_t l_hash;
            dap_hash_fast(l_source[i], l_source_size[i], &l_hash);
            l_verified = l_keys[i]->sign_verify(l_keys[i], &l_hash, sizeof(l_hash), l_signs[i], max_signature_size);
        }
        dap_assert_PIF(!l_verified, "Verifying signature");
    }
    *a_verify_time = get_cur_time_msec() - l_t1;
//memory free
    for(int i = 0; i < a_times; ++i) {
        dap_enc_key_signature_delete(l_keys[i]->type, l_signs[i]);
        dap_enc_key_delete(l_keys[i]);
        DAP_DELETE(l_source[i]);
    }
}

static void s_sign_verify_ser_test(dap_enc_key_type_t a_key_type, int a_times, int *a_sig_time, int *a_verify_time)
{
    size_t seed_size = sizeof(uint8_t);
    uint8_t seed[seed_size];
    dap_sign_t *l_signs[a_times];
    uint8_t *l_source[a_times];
    size_t l_source_size[a_times];

    int l_t1 = 0;
    *a_sig_time = 0;
    for (int i = 0; i < a_times; ++i) {
        randombytes(seed, seed_size);

        // used only in multisign
        dap_enc_key_type_t l_key[KEYS_TOTAL_COUNT];
        for (int j = 0; j < KEYS_TOTAL_COUNT; j++) {
            int l_step = random_uint32_t(c_keys_count);
            l_key[j] = c_key_type_arr[l_step];
        }
        // ----------
        l_source_size[i] = 1 + random_uint32_t(20);
        l_source[i] = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, l_source_size[i]);
        randombytes(l_source[i], l_source_size[i]);
        
        l_t1 = get_cur_time_msec();
        dap_enc_key_t *key = dap_enc_key_new_generate(a_key_type, l_key, KEYS_TOTAL_COUNT, seed, seed_size, 0);
        uint32_t l_hash_type = i % 2 ? DAP_SIGN_ADD_PKEY_HASHING_FLAG(DAP_SIGN_HASH_TYPE_DEFAULT) : DAP_SIGN_HASH_TYPE_DEFAULT;
        
        dap_chain_hash_fast_t l_hash = {};
        if (key->type == DAP_ENC_KEY_TYPE_SIG_ECDSA)
            l_signs[i] = dap_sign_create_with_hash_type(key, l_source[i], l_source_size[i], l_hash_type);
        else {
            dap_hash_fast(l_source[i], l_source_size[i], &l_hash);
            l_signs[i] = dap_sign_create_with_hash_type(key, &l_hash, sizeof(l_hash), l_hash_type);
        }
        if (i % 2) {
            pkey_hash_table_t *l_item = DAP_NEW_Z(pkey_hash_table_t);
            l_item->pkey_hash = DAP_NEW_Z(dap_chain_hash_fast_t);
            dap_enc_key_get_pkey_hash(key, (dap_chain_hash_fast_t *)l_item->pkey_hash);
            
            dap_assert_PIF(dap_sign_get_pkey_hash(l_signs[i], &l_hash), "Get pkeyhash by sign");
            dap_assert_PIF(!memcmp(l_item->pkey_hash, &l_hash, DAP_CHAIN_HASH_FAST_SIZE), "pkey hash in enc_key and sign equal")

            l_item->pkey = dap_pkey_from_enc_key (key);
            HASH_ADD(hh, s_pkey_hash_table, pkey_hash, DAP_CHAIN_HASH_FAST_SIZE, l_item);
        }
        *a_sig_time += get_cur_time_msec() - l_t1;
        
        dap_assert_PIF(l_signs[i], "Signing message and serialize");
        dap_enc_key_delete(key);
    }

    l_t1 = get_cur_time_msec();
    for(int i = 0; i < a_times; ++i) {
        int l_verified = 0;
        if (dap_sign_type_to_key_type(l_signs[i]->header.type) == DAP_ENC_KEY_TYPE_SIG_ECDSA) {
            if (dap_sign_is_use_pkey_hash(l_signs[i])) {
                dap_hash_fast_t l_sign_pkey_hash = {};
                dap_sign_get_pkey_hash(l_signs[i], &l_sign_pkey_hash);
                l_verified = dap_sign_verify_by_pkey(l_signs[i], l_source[i], l_source_size[i], s_get_pkey_by_hash_callback( (const uint8_t *)&l_sign_pkey_hash));
            } else {
                l_verified = dap_sign_verify(l_signs[i], l_source[i], l_source_size[i]);
            }
        } else {
            dap_chain_hash_fast_t l_hash;
            dap_hash_fast(l_source[i], l_source_size[i], &l_hash);
            if (dap_sign_is_use_pkey_hash(l_signs[i])) {
                dap_hash_fast_t l_sign_pkey_hash = {};
                dap_sign_get_pkey_hash(l_signs[i], &l_sign_pkey_hash);
                l_verified = dap_sign_verify_by_pkey(l_signs[i], &l_hash, sizeof(l_hash), s_get_pkey_by_hash_callback( (const uint8_t *)&l_sign_pkey_hash));
            } else {
                l_verified = dap_sign_verify(l_signs[i], &l_hash, sizeof(l_hash));
            }
        }
        dap_assert_PIF(!l_verified, "Deserialize and verifying signature");
    }
    *a_verify_time = get_cur_time_msec() - l_t1;

    for(int i = 0; i < a_times; ++i) {
        DAP_DEL_MULTY(l_signs[i], l_source[i]);
    }
    pkey_hash_table_t *l_temp, *l_current = NULL;
    HASH_ITER(hh, s_pkey_hash_table, l_current, l_temp){
        HASH_DEL(s_pkey_hash_table, l_current);
        DAP_DEL_MULTY(l_current->pkey, l_current->pkey_hash, l_current);
    }
}

static void s_sign_verify_test_becnhmark(const char *a_name, dap_enc_key_type_t a_key_type, int a_times) {
    dap_print_module_name(a_name);
    int l_sig_time = 0;
    int l_verify_time = 0;

    s_sign_verify_test(a_key_type, a_times, &l_sig_time, &l_verify_time);

    char l_msg[120] = {0};
    sprintf(l_msg, "Signing message %d times", a_times);
    benchmark_mgs_time(l_msg, l_sig_time);
    sprintf(l_msg, "Verifying message %d times", a_times);
    benchmark_mgs_time(l_msg, l_verify_time);

    s_sign_verify_ser_test(a_key_type, a_times, &l_sig_time, &l_verify_time);
    sprintf(l_msg, "Signing message with serialization %d times", a_times);
    benchmark_mgs_time(l_msg, l_sig_time);
    sprintf(l_msg, "Verifying message with serialization %d times", a_times);
    benchmark_mgs_time(l_msg, l_verify_time);
}
/*-----------------------------------------------------------------------*/

static void s_transfer_tests_run(int a_times)
{
    dap_init_test_case();
    // NEWHOPE removed - obsolete algorithm
    s_transfer_test_benchmark("KYBER512", DAP_ENC_KEY_TYPE_KEM_KYBER512, a_times);
    s_transfer_test_benchmark("MSRLN", DAP_ENC_KEY_TYPE_MSRLN, a_times);
    dap_cleanup_test_case();
}

static void s_sign_verify_tests_run(int a_times)
{
    dap_sign_set_pkey_by_hash_callback(s_get_pkey_by_hash_callback);
    dap_init_test_case();
    for (size_t i = 0; i < c_keys_count; ++i) {
        s_sign_verify_test_becnhmark(s_key_type_to_str(c_key_type_arr[i]), c_key_type_arr[i], a_times);
    }
    s_sign_verify_test_becnhmark("MULTISIGN", DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED, a_times);
    dap_cleanup_test_case();
}

void dap_enc_benchmark_tests_run(int a_times)
{
    s_transfer_tests_run(a_times);
    s_sign_verify_tests_run(a_times);
}

