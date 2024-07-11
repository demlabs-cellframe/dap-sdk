#include "dap_enc_benchmark_test.h"
#include "dap_sign.h"
#include "dap_test.h"
#include "dap_enc_test.h"
#include "rand/dap_rand.h"
#define LOG_TAG "dap_crypto_benchmark_tests"

//#define SIGNATURE_TYPE_COUNT 7
#define SIGN_COUNT 5
#define KEYS_TOTAL_COUNT 10

dap_enc_key_type_t s_key_type_arr[] = {
        DAP_ENC_KEY_TYPE_SIG_TESLA,
        DAP_ENC_KEY_TYPE_SIG_BLISS,
        DAP_ENC_KEY_TYPE_SIG_DILITHIUM,
        // DAP_ENC_KEY_TYPE_SIG_PICNIC,
        DAP_ENC_KEY_TYPE_SIG_FALCON,
        DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS,
#ifdef DAP_ECDSA
        DAP_ENC_KEY_TYPE_SIG_ECDSA,
#endif
        DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK };

/*--------------------------TRANSFER TEST BLOCK--------------------------*/
static int s_transfer_test(dap_enc_key_type_t a_key_type, int a_times, int *a_gen_time, int *a_alice_shared, int *a_bob_shared)
{
    int l_ret = 0;
    dap_enc_key_t **l_alice_keys = NULL;
    dap_enc_key_t **l_bob_keys = NULL;

    DAP_NEW_Z_COUNT_RET_VAL(l_alice_keys, dap_enc_key_t*, a_times, 1, NULL);
    DAP_NEW_Z_COUNT_RET_VAL(l_bob_keys, dap_enc_key_t*, a_times, 1, l_alice_keys);

    int l_t1 = get_cur_time_msec();

    for (int i = 0; i < a_times; ++i) {
        l_alice_keys[i] = dap_enc_key_new_generate(a_key_type, NULL, 0, NULL, 0, 0);
        dap_assert_PIF(l_alice_keys[i], "Key generate");
        l_ret |= !l_alice_keys[i];
    }

    int l_t2 = get_cur_time_msec();
    *a_gen_time = l_t2 - l_t1;

    l_t1 = get_cur_time_msec();
    for(int i = 0; i < a_times; ++i) {
        l_bob_keys[i] = dap_enc_key_new(a_key_type);
        l_bob_keys[i]->pub_key_data_size = l_bob_keys[i]->gen_bob_shared_key(l_bob_keys[i], l_alice_keys[i]->pub_key_data, l_alice_keys[i]->pub_key_data_size, (void**)&l_bob_keys[i]->pub_key_data);
        dap_assert_PIF(l_bob_keys[i]->pub_key_data_size, "Bob shared key gen");
        l_ret |= !l_bob_keys[i]->pub_key_data_size;
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
        l_ret |= l_cmp;
        dap_enc_key_delete(l_alice_keys[i]);
        dap_enc_key_delete(l_bob_keys[i]);
    }
    DAP_DEL_MULTY(l_alice_keys, l_bob_keys);
    return l_ret;
}

static int s_transfer_test_benchmark(const char *a_name, dap_enc_key_type_t a_key_type, int a_times) {
    dap_print_module_name(a_name);
    int l_ret = 0;
    int l_gen_time = 0;
    int l_alice_shared = 0;
    int l_bob_shared = 0;

    l_ret |= s_transfer_test(a_key_type, a_times, &l_gen_time, &l_alice_shared, &l_bob_shared);

    char l_msg[120] = {0};
    sprintf(l_msg, "Key gen %d times", a_times);
    benchmark_mgs_time(l_msg, l_gen_time);
    sprintf(l_msg, "Bob shared key gen %d times", a_times);
    benchmark_mgs_time(l_msg, l_bob_shared);
    sprintf(l_msg, "Alice shared key gen %d times", a_times);
    benchmark_mgs_time(l_msg, l_alice_shared);
    return l_ret;
}
/*-----------------------------------------------------------------------*/

/*------------------SIGNING AND VERIFICATION TEST BLOCK------------------*/
static int s_sign_verify_test(dap_enc_key_type_t a_key_type, int a_times, int *a_sig_time, int *a_verify_time)
{
    int l_ret = 0;
    size_t seed_size = sizeof(uint8_t);
    uint8_t seed[sizeof(uint8_t)];
    randombytes(seed, seed_size);
    uint8_t **l_signs = NULL;
    uint8_t **l_source = NULL;
    dap_enc_key_t **l_keys = NULL;
    size_t l_source_size[a_times];
    dap_enc_key_t *l_key_temp = dap_enc_key_new_generate(a_key_type, NULL, 0, seed, seed_size, 0);
    size_t max_signature_size = dap_sign_create_output_unserialized_calc_size(l_key_temp, 0);
    dap_enc_key_delete(l_key_temp);

    DAP_NEW_Z_COUNT_RET_VAL(l_signs, uint8_t*, a_times, 1, NULL);
    DAP_NEW_Z_COUNT_RET_VAL(l_source, uint8_t*, a_times, 1, l_signs);
    DAP_NEW_Z_COUNT_RET_VAL(l_keys, dap_enc_key_t*, a_times, 1, l_source, l_signs);

    int l_t1 = 0;
    *a_sig_time = 0;
    int l_sig_type_count = sizeof(s_key_type_arr) / sizeof(*s_key_type_arr);
    for (int i = 0; i < a_times; ++i) {
        randombytes(seed, seed_size);
        // used only in multisign
        dap_enc_key_type_t l_key[KEYS_TOTAL_COUNT];
        for (int j = 0; j < KEYS_TOTAL_COUNT; j++) {
            int l_step = random_uint32_t(l_sig_type_count);
            l_key[j] = s_key_type_arr[l_step];
        }
        // ----------
        
        DAP_NEW_Z_SIZE_RET_VAL(l_signs[i], uint8_t, max_signature_size, 1, NULL);
        l_source_size[i] = 1 + random_uint32_t(20);
        DAP_NEW_Z_SIZE_RET_VAL(l_source[i], uint8_t, l_source_size[i], 1, NULL);
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
        l_ret |= l_signed;
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
        l_ret |= l_verified;
    }
    *a_verify_time = get_cur_time_msec() - l_t1;
//memory free
    for(int i = 0; i < a_times; ++i) {
        dap_enc_key_signature_delete(l_keys[i]->type, l_signs[i]);
        dap_enc_key_delete(l_keys[i]);
        DAP_DELETE(l_source[i]);
    }
    DAP_DEL_MULTY(l_signs, l_source, l_keys);
    return l_ret;
}

static int s_sign_verify_ser_test(dap_enc_key_type_t a_key_type, int a_times, int *a_sig_time, int *a_verify_time)
{
    int l_ret = 0;
    size_t seed_size = sizeof(uint8_t);
    uint8_t seed[seed_size];
    dap_sign_t **l_signs = NULL;
    uint8_t **l_source = NULL;
    size_t l_source_size[a_times];

    DAP_NEW_Z_COUNT_RET_VAL(l_signs, dap_sign_t*, a_times, 1, NULL);
    DAP_NEW_Z_COUNT_RET_VAL(l_source, uint8_t*, a_times, 1, l_signs);

    int l_t1 = 0;
    *a_sig_time = 0;
    int l_sig_type_count = sizeof(s_key_type_arr) / sizeof(*s_key_type_arr);
    for (int i = 0; i < a_times; ++i) {
        randombytes(seed, seed_size);

        // used only in multisign
        dap_enc_key_type_t l_key[KEYS_TOTAL_COUNT];
        for (int j = 0; j < KEYS_TOTAL_COUNT; j++) {
            int l_step = random_uint32_t(l_sig_type_count);
            l_key[j] = s_key_type_arr[l_step];
        }
        // ----------
        l_source_size[i] = 1 + random_uint32_t(20);
        DAP_NEW_Z_SIZE_RET_VAL(l_source[i], uint8_t, l_source_size[i], 1, NULL);
        randombytes(l_source[i], l_source_size[i]);
        
        l_t1 = get_cur_time_msec();
        dap_enc_key_t *key = dap_enc_key_new_generate(a_key_type, l_key, KEYS_TOTAL_COUNT, seed, seed_size, 0);
        if (key->type == DAP_ENC_KEY_TYPE_SIG_ECDSA)
            l_signs[i] = dap_sign_create(key, l_source[i], l_source_size[i], 0);
        else {
            dap_chain_hash_fast_t l_hash;
            dap_hash_fast(l_source[i], l_source_size[i], &l_hash);
            l_signs[i] = dap_sign_create(key, &l_hash, sizeof(l_hash), 0);
        }
        *a_sig_time += get_cur_time_msec() - l_t1;
        
        dap_assert_PIF(l_signs[i], "Signing message and serialize");
        l_ret |= !l_signs[i];
        dap_enc_key_delete(key);
    }

    l_t1 = get_cur_time_msec();
    for(int i = 0; i < a_times; ++i) {
        int l_verified = 0;
       if (dap_sign_type_to_key_type(l_signs[i]->header.type) == DAP_ENC_KEY_TYPE_SIG_ECDSA)
            l_verified = dap_sign_verify(l_signs[i], l_source[i], l_source_size[i]);
        else {
            dap_chain_hash_fast_t l_hash;
            dap_hash_fast(l_source[i], l_source_size[i], &l_hash);
            l_verified = dap_sign_verify(l_signs[i], &l_hash, sizeof(l_hash));
        }
        dap_assert_PIF(!l_verified, "Deserialize and verifying signature");
        l_ret |= l_verified;
    }
    *a_verify_time = get_cur_time_msec() - l_t1;

    for(int i = 0; i < a_times; ++i) {
        DAP_DEL_MULTY(l_signs[i], l_source[i]);
    }
    DAP_DEL_MULTY(l_signs, l_source);
    return l_ret;
}

static int s_sign_verify_test_becnhmark(const char *a_name, dap_enc_key_type_t a_key_type, int a_times) {
    dap_print_module_name(a_name);
    int l_ret = 0;
    int l_sig_time = 0;
    int l_verify_time = 0;

    l_ret |= s_sign_verify_test(a_key_type, a_times, &l_sig_time, &l_verify_time);

    char l_msg[120] = {0};
    sprintf(l_msg, "Signing message %d times", a_times);
    benchmark_mgs_time(l_msg, l_sig_time);
    sprintf(l_msg, "Verifying message %d times", a_times);
    benchmark_mgs_time(l_msg, l_verify_time);

    l_ret |= s_sign_verify_ser_test(a_key_type, a_times, &l_sig_time, &l_verify_time);
    sprintf(l_msg, "Signing message with serialization %d times", a_times);
    benchmark_mgs_time(l_msg, l_sig_time);
    sprintf(l_msg, "Verifying message with serialization %d times", a_times);
    benchmark_mgs_time(l_msg, l_verify_time);
    return l_ret;
}
/*-----------------------------------------------------------------------*/

static int s_transfer_tests_run(int a_times)
{
    int l_ret = 0;
    dap_init_test_case();
    l_ret |= s_transfer_test_benchmark("NEWHOPE", DAP_ENC_KEY_TYPE_RLWE_NEWHOPE_CPA_KEM, a_times);
    l_ret |= s_transfer_test_benchmark("KYBER512", DAP_ENC_KEY_TYPE_KEM_KYBER512, a_times);
    l_ret |= s_transfer_test_benchmark("MSRLN", DAP_ENC_KEY_TYPE_MSRLN, a_times);
    dap_cleanup_test_case();
    return l_ret;
}

static int s_sign_verify_tests_run(int a_times)
{
    int l_ret = 0;
    dap_init_test_case();
    // s_sign_verify_test_becnhmark("PICNIC", DAP_ENC_KEY_TYPE_SIG_PICNIC, a_times);
    // l_ret |= s_sign_verify_test_becnhmark("TESLA", DAP_ENC_KEY_TYPE_SIG_TESLA, a_times);
    // l_ret |= s_sign_verify_test_becnhmark("BLISS", DAP_ENC_KEY_TYPE_SIG_BLISS, a_times);
    // l_ret |= s_sign_verify_test_becnhmark("DILITHIUM", DAP_ENC_KEY_TYPE_SIG_DILITHIUM, a_times);
    // l_ret |= s_sign_verify_test_becnhmark("FALCON", DAP_ENC_KEY_TYPE_SIG_FALCON, a_times);
    l_ret |= s_sign_verify_test_becnhmark("SPHINCSPLUS", DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS, a_times);
// #ifdef DAP_ECDSA
//     l_ret |= s_sign_verify_test_becnhmark("ECDSA", DAP_ENC_KEY_TYPE_SIG_ECDSA, a_times);
// #endif
//     l_ret |= s_sign_verify_test_becnhmark("SHIPOVNIK", DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK, a_times);
//     l_ret |= s_sign_verify_test_becnhmark("MULTISIGN", DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED, a_times);
    dap_cleanup_test_case();
    return l_ret;
}

int dap_enc_benchmark_tests_run(int a_times)
{
    int l_ret = 0;
    l_ret |= s_transfer_tests_run(a_times);
    l_ret |= s_sign_verify_tests_run(a_times);
    return l_ret;
}

