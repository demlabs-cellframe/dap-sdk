#include "rand/dap_rand.h"
#include "dap_enc_dilithium_test.h"
#include "dap_enc_dilithium.h"
#include "dap_sign.h"

#define LOG_TAG "dap_crypto_tests"

static void test_signing_verifying(void)
{
    static size_t source_size = 0;
    size_t seed_size = sizeof(uint8_t);
    uint8_t seed[seed_size];

    randombytes(seed, seed_size);

    dap_enc_key_t* key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, seed, seed_size, 0);

    size_t max_signature_size = dap_enc_dilithium_calc_signature_unserialized_size();
    uint8_t* sig = calloc(max_signature_size, 1);

    int step = 1 + random_uint32_t( 20);
    source_size += (size_t) step;

    uint8_t source[source_size];
    randombytes(source, source_size);

    int l_signed = key->sign_get(key, source, source_size, sig, max_signature_size);
    dap_assert_PIF(!l_signed, "Signing message");

    int l_verified = key->sign_verify(key, source, source_size, sig, max_signature_size);
    dap_assert_PIF(!l_verified, "Verifying signature");

    dilithium_signature_delete((dilithium_signature_t*)sig);
    free(sig);
    dap_enc_key_delete(key);
}

static void test_verifying_serial(dap_sign_t **l_signs, int a_times)
{

}

static void test_signing_serial(int a_times, int *a_sig_time, int *a_verify_time)
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

        dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, seed, seed_size, 0);

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

void dap_enc_dilithium_tests_run(int a_times)
{
    dap_print_module_name("dap_enc_dilithium");
    init_test_case();

    int l_sig_time = 0;
    int l_verify_time = 0;

    test_signing_serial(a_times, &l_sig_time, &l_verify_time);

    char l_msg[50] = {0};
    sprintf(l_msg, "Signing and verifying message %d time", a_times);
    benchmark_mgs_time(l_msg, l_sig_time);
    sprintf(l_msg, "Signing and verifying message with serialization %d time", a_times);
    benchmark_mgs_time(l_msg, l_verify_time);

    cleanup_test_case();
}

