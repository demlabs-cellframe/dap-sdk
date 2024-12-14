#include "dap_enc_multithread_test.h"
#include "dap_sign.h"
#include "dap_test.h"
#include "dap_enc_test.h"
#include "rand/dap_rand.h"
#include "dap_enc_sphincsplus.h"

#define LOG_TAG "dap_crypto_multithread_tests"

static dap_enc_key_t *s_enc_key_new_generate(dap_enc_key_type_t a_key_type, const void *a_kex_buf, size_t a_kex_size, const void *a_seed, size_t a_seed_size, size_t a_key_size) {
    switch (a_key_type)
    {
    case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS:
        dap_enc_sig_sphincsplus_set_default_config(dap_random_uint16() % dap_enc_sig_sphincsplus_get_configs_count() + 1);
        return dap_enc_key_new_generate(a_key_type, a_kex_buf, a_kex_size, a_seed, a_seed_size, a_key_size);
    default:
        return dap_enc_key_new_generate(a_key_type, a_kex_buf, a_kex_size, a_seed, a_seed_size, a_key_size);
    }
}

static int s_test_thread(dap_enc_key_type_t a_key_type, int a_times)
{
    int l_ret = 0;
    size_t seed_size = sizeof(uint8_t);
    uint8_t seed[seed_size];
    dap_sign_t *l_signs[a_times];
    uint8_t *l_source[a_times];
    size_t l_source_size[a_times];

    for (int i = 0; i < a_times; ++i) {
        randombytes(seed, seed_size);

         // ----------
        l_source_size[i] = 1 + random_uint32_t(20);
        l_source[i] = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_source_size[i], 1, NULL);
        randombytes(l_source[i], l_source_size[i]);
        
        dap_enc_key_t *key = s_enc_key_new_generate(a_key_type, NULL, 0, seed, seed_size, 0);
        if (key->type == DAP_ENC_KEY_TYPE_SIG_ECDSA)
            l_signs[i] = dap_sign_create(key, l_source[i], l_source_size[i], 0);
        else {
            dap_chain_hash_fast_t l_hash;
            dap_hash_fast(l_source[i], l_source_size[i], &l_hash);
            l_signs[i] = dap_sign_create(key, &l_hash, sizeof(l_hash), 0);
        }
        
        dap_assert_PIF(l_signs[i], "Signing message and serialize");
        l_ret |= !l_signs[i];
        dap_enc_key_delete(key);
    }

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
    DAP_DEL_ARRAY(l_signs, a_times);
    DAP_DEL_ARRAY(l_source, a_times);
    return l_ret;
}

static void *s_test_thread_dilithium(void *a_arg) {
    int a_times = *(int *)a_arg;
    s_test_thread(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, a_times);
    pthread_exit(NULL);
}

static void *s_test_thread_falcon(void *a_arg) {
    int a_times = *(int *)a_arg;
    s_test_thread(DAP_ENC_KEY_TYPE_SIG_FALCON, a_times);
    pthread_exit(NULL);
}

static void *s_test_thread_ecdsa(void *a_arg) {
    int a_times = *(int *)a_arg;
    s_test_thread(DAP_ENC_KEY_TYPE_SIG_ECDSA, a_times);
    pthread_exit(NULL);
}

static void *s_test_thread_sphincs(void *a_arg) {
    int a_times = *(int *)a_arg;
    s_test_thread(DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS, a_times);
    pthread_exit(NULL);
}

static void *s_test_thread_shipovnik(void *a_arg) {
    int a_times = *(int *)a_arg;
    s_test_thread(DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK, a_times);
    pthread_exit(NULL);
}

static void s_test_multithread(const char *a_name, void *(*a_func)(void *), int a_times)
{
    uint32_t l_thread_count = 3;
    log_it(L_INFO, "Test %s with %u threads", a_name, l_thread_count);
    pthread_t *l_threads = DAP_NEW_Z_COUNT(pthread_t, l_thread_count);

    for (uint32_t i = 0; i < l_thread_count; ++i) {
        pthread_create(l_threads + i, NULL, a_func, &a_times);
    }
    for (uint32_t i = 0; i < l_thread_count; ++i) {
        pthread_join(l_threads[i], NULL);
    }
    DAP_DEL_Z(l_threads);
}

int dap_enc_multithread_tests_run(int a_times)
{
    dap_print_module_name("Multithread sign create and verify");

    s_test_multithread("Dilithium", s_test_thread_dilithium, a_times);
    dap_pass_msg("Dilithium multithread tests");

    s_test_multithread("Falcon", s_test_thread_falcon, a_times);
    dap_pass_msg("Falcon multithread tests");

    s_test_multithread("Sphincs plus", s_test_thread_sphincs, a_times);
    dap_pass_msg("Sphincs plus multithread tests");

#ifdef DAP_ECDSA
    s_test_multithread("ECDSA", s_test_thread_ecdsa, a_times);
    dap_pass_msg("ECDSA multithread tests");
#endif
#ifdef DAP_SHIPOVNIK
    s_test_multithread("Shipovnik", s_test_thread_shipovnik, a_times);
    dap_pass_msg("Shipovnik plus multithread tests");
#endif
    dap_cleanup_test_case();
    return 0;
}

