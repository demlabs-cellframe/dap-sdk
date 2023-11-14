#include "dap_enc_benchmark_test.h"
#include "dap_sign.h"
#include "dap_test.h"
#include "dap_enc_test.h"
#include "rand/dap_rand.h"
#define LOG_TAG "dap_crypto_benchmark_tests"

/*--------------------------TRANSFER TEST BLOCK--------------------------*/
static void s_transfer_test(dap_enc_key_type_t a_key_type, int a_times, int *a_gen_time, int *a_alice_shared, int *a_bob_shared)
{
    dap_enc_key_t **l_alice_keys = NULL;
    dap_enc_key_t **l_bob_keys = NULL;

    DAP_NEW_Z_COUNT_RET(l_alice_keys, dap_enc_key_t*, a_times, NULL);
    DAP_NEW_Z_COUNT_RET(l_bob_keys, dap_enc_key_t*, a_times, l_alice_keys);

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
        dap_assert_PIF(!memcmp(l_alice_keys[i]->shared_key, l_bob_keys[i]->shared_key, l_alice_keys[i]->shared_key_size), "Session keys equals");
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
    uint8_t seed[seed_size];
    uint8_t **l_signs = NULL;
    uint8_t **l_source = NULL;
    dap_enc_key_t **l_keys = NULL;
    size_t l_source_size[a_times];
    dap_enc_key_t *l_key = dap_enc_key_new_generate(a_key_type, NULL, 0, seed, seed_size, 0);
    size_t max_signature_size = dap_sign_create_output_unserialized_calc_size(l_key, 0);
    dap_enc_key_delete(l_key);

    DAP_NEW_Z_COUNT_RET(l_signs, uint8_t*, a_times, NULL);
    DAP_NEW_Z_COUNT_RET(l_source, uint8_t*, a_times, NULL);
    DAP_NEW_Z_COUNT_RET(l_keys, dap_enc_key_t*, a_times, NULL);

    int l_t1 = get_cur_time_msec();

    for (int i = 0; i < a_times; ++i) {
        randombytes(seed, seed_size);

        l_keys[i] = dap_enc_key_new_generate(a_key_type, NULL, 0, seed, seed_size, 0);
        DAP_NEW_Z_SIZE_RET(l_signs[i], uint8_t, max_signature_size, NULL);

        l_source_size[i] = 1 + random_uint32_t(20);
        DAP_NEW_Z_SIZE_RET(l_source[i], uint8_t, l_source_size[i], NULL);
        randombytes(l_source[i], l_source_size[i]);

        int l_signed = l_keys[i]->sign_get(l_keys[i], l_source[i], l_source_size[i], l_signs[i], max_signature_size);
        dap_assert_PIF(!l_signed, "Signing message");
    }

    int l_t2 = get_cur_time_msec();
    *a_sig_time = l_t2 - l_t1;

    l_t1 = get_cur_time_msec();
    for(int i = 0; i < a_times; ++i) {
        int l_verified = l_keys[i]->sign_verify(l_keys[i], l_source[i], l_source_size[i], l_signs[i], max_signature_size);
        dap_assert_PIF(!l_verified, "Verifying signature");
    }
    l_t2 = get_cur_time_msec();
    *a_verify_time = l_t2 - l_t1;
//memory free
    for(int i = 0; i < a_times; ++i) {
        dap_enc_key_signature_delete(l_keys[i]->type, l_signs[i]);
        dap_enc_key_delete(l_keys[i]);
        DAP_DELETE(l_source[i]);
    }
    DAP_DEL_MULTY(l_signs, l_source, l_keys);
}

static void s_sign_verify_ser_test(dap_enc_key_type_t a_key_type, int a_times, int *a_sig_time, int *a_verify_time)
{
    size_t seed_size = sizeof(uint8_t);
    uint8_t seed[seed_size];
    dap_sign_t **l_signs = NULL;
    uint8_t **l_source = NULL;
    size_t l_source_size[a_times];
    dap_pass_msg("verify test start");

    DAP_NEW_Z_COUNT_RET(l_signs, dap_sign_t*, a_times, NULL);
    DAP_NEW_Z_COUNT_RET(l_source, uint8_t*, a_times, NULL);

    int l_t1 = get_cur_time_msec();

    for (int i = 0; i < a_times; ++i) {
        randombytes(seed, seed_size);

        dap_enc_key_t *key = dap_enc_key_new_generate(a_key_type, NULL, 0, seed, seed_size, 0);

        l_source_size[i] = 1 + random_uint32_t(20);
        DAP_NEW_Z_SIZE_RET(l_source[i], uint8_t, l_source_size[i], NULL);
        randombytes(l_source[i], l_source_size[i]);

        l_signs[i] = dap_sign_create(key, l_source[i], l_source_size[i], 0);
        dap_assert_PIF(l_signs[i], "Signing message and serialize");
        dap_enc_key_delete(key);
        dap_pass_msg("sign created");
    }

    int l_t2 = get_cur_time_msec();
    *a_sig_time = l_t2 - l_t1;

    l_t1 = get_cur_time_msec();
    dap_pass_msg("_sign verify start");
    for(int i = 0; i < a_times; ++i) {
        printf("___sign verify start");
        int verify = dap_sign_verify(l_signs[i], l_source[i], l_source_size[i]);
        printf("___verify result %d", verify);
        dap_assert_PIF(!verify, "Deserialize and verifying signature");
        dap_pass_msg("___sign verified");
    }
    dap_pass_msg("_sign verified");
    l_t2 = get_cur_time_msec();
    *a_verify_time = l_t2 - l_t1;

    for(int i = 0; i < a_times; ++i) {
        DAP_DEL_MULTY(l_signs[i], l_source[i]);
        dap_pass_msg("sign deleted");
    }
    DAP_DEL_MULTY(l_signs, l_source);
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
    s_transfer_test_benchmark("NEWHOPE", DAP_ENC_KEY_TYPE_RLWE_NEWHOPE_CPA_KEM, a_times);
    s_transfer_test_benchmark("KYBER512", DAP_ENC_KEY_TYPE_KEM_KYBER512, a_times);
    s_transfer_test_benchmark("MSRLN", DAP_ENC_KEY_TYPE_MSRLN, a_times);
    dap_cleanup_test_case();
}

static void s_sign_verify_tests_run(int a_times)
{
    dap_init_test_case();
    // s_sign_verify_test_becnhmark("PICNIC", DAP_ENC_KEY_TYPE_SIG_PICNIC, a_times);
    // s_sign_verify_test_becnhmark("TESLA", DAP_ENC_KEY_TYPE_SIG_TESLA, a_times);
    s_sign_verify_test_becnhmark("BLISS", DAP_ENC_KEY_TYPE_SIG_TESLA, a_times);
    s_sign_verify_test_becnhmark("DILITHIUM", DAP_ENC_KEY_TYPE_SIG_DILITHIUM, a_times);
    s_sign_verify_test_becnhmark("FALCON", DAP_ENC_KEY_TYPE_SIG_FALCON, a_times);
    s_sign_verify_test_becnhmark("SPHINCSPLUS", DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS, a_times);
    dap_cleanup_test_case();
}

void dap_enc_benchmark_tests_run(int a_times)
{
    s_transfer_tests_run(a_times);
    s_sign_verify_tests_run(a_times);
}

