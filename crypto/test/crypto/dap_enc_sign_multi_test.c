#include "dap_enc_sign_multi_test.h"
#include "dap_test.h"
#include "rand/dap_rand.h"
#include "dap_sign.h"

#define LOG_TAG "dap_crypto_multy_sign_tests"

#define SIGNATURE_TYPE_COUNT 5
#define SIGN_COUNT 5
#define KEYS_TOTAL_COUNT 10

static void test_signing_verifying(
    dap_enc_key_type_t a_sign_type, 
    int a_times,
    int *a_sig_time, 
    int *a_verify_time, 
    int *a_ser_time, 
    int *a_deser_time)
{
/// prepare
    size_t seed_size = sizeof(uint8_t);
    uint8_t seed[seed_size];
    dap_multi_sign_t **l_signs = NULL;
    uint8_t **l_signs_ser = NULL;
    uint8_t **l_source = NULL;
    size_t l_source_size[a_times];
    size_t l_ser_size[a_times];

    DAP_NEW_Z_COUNT_RET(l_signs, dap_multi_sign_t*, a_times, NULL);
    DAP_NEW_Z_COUNT_RET(l_signs_ser, uint8_t*, a_times, l_signs);
    DAP_NEW_Z_COUNT_RET(l_source, uint8_t*, a_times, l_signs_ser, l_signs);
//check sign time
    int l_t1 = get_cur_time_msec();

    for (int i = 0; i < a_times; ++i) {
        randombytes(seed, seed_size);

        dap_enc_key_type_t key_type_arr[SIGNATURE_TYPE_COUNT] = {\
                DAP_ENC_KEY_TYPE_SIG_TESLA,\
                DAP_ENC_KEY_TYPE_SIG_BLISS,\
                DAP_ENC_KEY_TYPE_SIG_DILITHIUM,\
                /* DAP_ENC_KEY_TYPE_SIG_PICNIC,\ */
                DAP_ENC_KEY_TYPE_SIG_FALCON,\
                DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS};
        int step;
        dap_enc_key_t* key[KEYS_TOTAL_COUNT];
        for (int i = 0; i < KEYS_TOTAL_COUNT; i++) {
            step = random_uint32_t( SIGNATURE_TYPE_COUNT);
            if (a_sign_type != DAP_ENC_KEY_TYPE_NULL)
                key[i] = dap_enc_key_new_generate(a_sign_type, NULL, 0, seed, seed_size, 0);
            else
                key[i] = dap_enc_key_new_generate(key_type_arr[step], NULL, 0, seed, seed_size, 0);
        }

        l_source_size[i] = 1 + random_uint32_t(20);
        DAP_NEW_Z_SIZE_RET(l_source[i], uint8_t, l_source_size[i], NULL);
        randombytes(l_source[i], l_source_size[i]);

        dap_multi_sign_params_t *l_params = dap_multi_sign_params_make(SIG_TYPE_MULTI_CHAINED, KEYS_TOTAL_COUNT, 5,\
                                                                    key[0], key[1], key[2], key[3], key[4], key[5],\
                                                                    key[6], key[7], key[8], key[9],\
                                                                    random_uint32_t(SIGN_COUNT),
                                                                    random_uint32_t(SIGN_COUNT),
                                                                    random_uint32_t(SIGN_COUNT),
                                                                    random_uint32_t(SIGN_COUNT),
                                                                    random_uint32_t(SIGN_COUNT));
        dap_assert_PIF(l_params, "Creating multi-sign parameters");
        
        l_signs[i] = dap_multi_sign_create(l_params, l_source[i], l_source_size[i]);
        dap_assert_PIF(l_signs[i], "Signing message");

        for (int i = 0; i < KEYS_TOTAL_COUNT; i++) {
            dap_enc_key_delete(key[i]);
        }
        dap_multi_sign_params_delete(l_params);
    }
    int l_t2 = get_cur_time_msec();
    *a_sig_time = l_t2 - l_t1;
// check serialize time
    l_t1 = get_cur_time_msec();
    for (int i = 0; i < a_times; ++i) {
        l_signs_ser[i] = dap_multi_sign_serialize(l_signs[i], &l_ser_size[i]);
        dap_assert_PIF(l_signs_ser[i], "Serializing signature");
    }
    l_t2 = get_cur_time_msec();
    *a_ser_time = l_t2 - l_t1;
// free memory
    for (int i = 0; i < a_times; ++i) {
        dap_multi_sign_delete(l_signs[i]);
    }

// check deserialize time
    l_t1 = get_cur_time_msec();
    for (int i = 0; i < a_times; ++i) {
        l_signs[i] = dap_multi_sign_deserialize(SIG_TYPE_MULTI_CHAINED, l_signs_ser[i], l_ser_size[i]);
        dap_assert_PIF(l_signs[i], "Deserializing signature");
    }
    l_t2 = get_cur_time_msec();
    *a_deser_time = l_t2 - l_t1;
// free memory
    for (int i = 0; i < a_times; ++i) {
        DAP_DELETE(l_signs_ser[i]);
    }

// check verify time
    l_t1 = get_cur_time_msec();
    for (int i = 0; i < a_times; ++i) {
        int verify = dap_multi_sign_verify(l_signs[i], l_source[i], l_source_size[i]);
        dap_assert_PIF(!verify, "Verifying signature");
    }
    l_t2 = get_cur_time_msec();
    *a_verify_time = l_t2 - l_t1;
// free memory
    for (int i = 0; i < a_times; ++i) {
        dap_multi_sign_delete(l_signs[i]);
        DAP_DELETE(l_source[i]);
    }
    DAP_DEL_MULTY(l_signs, l_signs_ser, l_source);
}

static void init_test_case()
{
    srand((uint32_t) time(NULL));
}

static void test_becnhmark_get_verify_ser_deser_sign(const char *a_name, dap_enc_key_type_t a_key_type, int a_times)
{
    dap_print_module_name(a_name);
    int l_sig_time = 0;
    int l_verify_time = 0;
    int l_ser_time = 0;
    int l_deser_time = 0;

    char l_msg[120] = {0};

    test_signing_verifying(a_key_type, a_times, &l_sig_time, &l_verify_time, &l_ser_time, &l_deser_time);
    sprintf(l_msg, "Signing message %d times, %d keys, %d signs", a_times, KEYS_TOTAL_COUNT, SIGN_COUNT);
    benchmark_mgs_time(l_msg, l_sig_time);
    sprintf(l_msg, "Verifying message %d times, %d keys, %d signs", a_times, KEYS_TOTAL_COUNT, SIGN_COUNT);
    benchmark_mgs_time(l_msg, l_verify_time);
    // sprintf(l_msg, "Serialize multising %d times, %d keys, %d signs", a_times, KEYS_TOTAL_COUNT, SIGN_COUNT);
    // benchmark_mgs_time(l_msg, l_ser_time);
    // sprintf(l_msg, "Deserialize multising %d times, %d keys, %d signs", a_times, KEYS_TOTAL_COUNT, SIGN_COUNT);
    // benchmark_mgs_time(l_msg, l_deser_time);
}

void dap_enc_multi_sign_tests_run(int a_times)
{
    
    init_test_case();

    test_becnhmark_get_verify_ser_deser_sign("MULTISIG_BLISS", DAP_ENC_KEY_TYPE_SIG_BLISS, a_times / SIGN_COUNT);
    test_becnhmark_get_verify_ser_deser_sign("MULTISIG_DILITHIUM", DAP_ENC_KEY_TYPE_SIG_DILITHIUM, a_times / SIGN_COUNT);
    // test_becnhmark_get_verify_ser_deser_sign("MULTISIG_PICNIC", DAP_ENC_KEY_TYPE_SIG_PICNIC, a_times / SIGN_COUNT);
    test_becnhmark_get_verify_ser_deser_sign("MULTISIG_FALCON", DAP_ENC_KEY_TYPE_SIG_FALCON, a_times / SIGN_COUNT);
    test_becnhmark_get_verify_ser_deser_sign("MULTISIG_SPHINCSPLUS", DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS, a_times / SIGN_COUNT);
    test_becnhmark_get_verify_ser_deser_sign("MULTISIG_RANDOM", DAP_ENC_KEY_TYPE_NULL, a_times / SIGN_COUNT);

}
