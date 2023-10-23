#include "rand/dap_rand.h"
#include "dap_enc_falcon_test.h"
#include "dap_enc_falcon.h"
#include "dap_sign.h"

#define LOG_TAG "dap_crypto_tests"

static void test_signing_verifying(int a_times, int *a_sig_time, int *a_verify_time)
{

    size_t seed_size = sizeof(uint8_t);
    uint8_t seed[seed_size];
    uint8_t **l_signs = NULL;
    uint8_t **l_source = NULL;
    dap_enc_key_t **l_keys = NULL;
    size_t l_source_size[a_times];
    size_t max_signature_size = dap_enc_falcon_calc_signature_unserialized_size();

    DAP_NEW_Z_COUNT_RET(l_signs, uint8_t*, a_times, NULL);
    DAP_NEW_Z_COUNT_RET(l_source, uint8_t*, a_times, NULL);
    DAP_NEW_Z_COUNT_RET(l_keys, dap_enc_key_t*, a_times, NULL);

    int l_t1 = get_cur_time_msec();

    for (int i = 0; i < a_times; ++i) {
        randombytes(seed, seed_size);

        l_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_FALCON, NULL, 0, seed, seed_size, 0);
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
        dap_enc_key_delete(l_keys[i]);
        falcon_signature_delete((falcon_signature_t*)l_signs[i]);
        free(l_signs[i]);
    }
    l_t2 = get_cur_time_msec();
    *a_verify_time = l_t2 - l_t1;
    DAP_DEL_MULTY(l_signs, l_source, l_keys);
}

static void test_signing_verifying_serial(int a_times, int *a_sig_time, int *a_verify_time)
{
    size_t seed_size = sizeof(uint8_t);
    uint8_t seed[seed_size];
    dap_sign_t **l_signs = NULL;
    uint8_t **l_source = NULL;
    size_t l_source_size[a_times];

    DAP_NEW_Z_COUNT_RET(l_signs, dap_sign_t*, a_times, NULL);
    DAP_NEW_Z_COUNT_RET(l_source, uint8_t*, a_times, NULL);

    int l_t1 = get_cur_time_msec();

    for (int i = 0; i < a_times; ++i) {
        randombytes(seed, seed_size);

        dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_FALCON, NULL, 0, seed, seed_size, 0);

        l_source_size[i] = 1 + random_uint32_t(20);
        DAP_NEW_Z_SIZE_RET(l_source[i], uint8_t, l_source_size[i], NULL);
        randombytes(l_source[i], l_source_size[i]);

        l_signs[i] = dap_sign_create(key, l_source[i], l_source_size[i], 0);
        dap_assert_PIF(l_signs[i], "Signing message and serialize");
        dap_enc_key_delete(key);
    }

    int l_t2 = get_cur_time_msec();
    *a_sig_time = l_t2 - l_t1;

    l_t1 = get_cur_time_msec();
    for(int i = 0; i < a_times; ++i) {
        int verify = dap_sign_verify(l_signs[i], l_source[i], l_source_size[i]);
        dap_assert_PIF(!verify, "Deserialize and verifying signature");
        free(l_signs[i]);
    }
    l_t2 = get_cur_time_msec();
    *a_verify_time = l_t2 - l_t1;

    DAP_DEL_MULTY(l_signs, l_source);
}

static void init_test_case()
{
    srand((uint32_t) time(NULL));
    dap_enc_key_init();
}

static void cleanup_test_case()
{
    dap_enc_key_deinit();
}

void dap_enc_falcon_tests_run(int a_times)
{
    dap_print_module_name("FALCOM");
    init_test_case();

    int l_sig_time = 0;
    int l_verify_time = 0;

    test_signing_verifying(a_times, &l_sig_time, &l_verify_time);

    char l_msg[120] = {0};
    sprintf(l_msg, "Signing message %d times", a_times);
    benchmark_mgs_time(l_msg, l_sig_time);
    sprintf(l_msg, "Verifying message %d times", a_times);
    benchmark_mgs_time(l_msg, l_verify_time);

    test_signing_verifying_serial(a_times, &l_sig_time, &l_verify_time);
    sprintf(l_msg, "Signing message with serialization %d times", a_times);
    benchmark_mgs_time(l_msg, l_sig_time);
    sprintf(l_msg, "Verifying message with serialization %d times", a_times);
    benchmark_mgs_time(l_msg, l_verify_time);

    cleanup_test_case();
}

