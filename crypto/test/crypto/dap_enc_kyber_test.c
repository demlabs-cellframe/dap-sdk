#include "dap_enc_kyber_test.h"
#include "dap_enc_key.h"
#include "rand/dap_rand.h"
#define LOG_TAG "dap_crypto_tests"

void key_kem_kyber512_transfer_simulation_test(int a_times, int *a_gen_time, int *a_alice_shared, int *a_bob_shared)
{
    size_t seed_size = sizeof(uint8_t);
    uint8_t seed[seed_size];
    dap_enc_key_t **l_alice_keys = NULL;
    dap_enc_key_t **l_bob_keys = NULL;

    DAP_NEW_Z_COUNT_RET(l_alice_keys, dap_enc_key_t*, a_times, NULL);
    DAP_NEW_Z_COUNT_RET(l_bob_keys, dap_enc_key_t*, a_times, NULL);

    int l_t1 = get_cur_time_msec();

    for (int i = 0; i < a_times; ++i) {
        randombytes(seed, seed_size);

        l_alice_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_KYBER512, NULL, 0, seed, seed_size, 0);

        dap_assert_PIF(l_alice_keys[i], "Key generate");
    }

    int l_t2 = get_cur_time_msec();
    *a_gen_time = l_t2 - l_t1;

    l_t1 = get_cur_time_msec();
    for(int i = 0; i < a_times; ++i) {
        l_bob_keys[i] = dap_enc_key_new(DAP_ENC_KEY_TYPE_KEM_KYBER512);
        size_t l_res = l_bob_keys[i]->gen_bob_shared_key(l_bob_keys[i], l_alice_keys[i]->pub_key_data, l_alice_keys[i]->pub_key_data_size, (void**)&l_bob_keys[i]->pub_key_data);
        dap_assert_PIF(l_res, "Bob shared key gen");
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

int dap_enc_kyber_test_run(int a_times) {
    dap_print_module_name("KYBER512");
    int l_gen_time = 0;
    int l_alice_shared = 0;
    int l_bob_shared = 0;

    key_kem_kyber512_transfer_simulation_test(a_times, &l_gen_time, &l_alice_shared, &l_bob_shared);

    char l_msg[120] = {0};
    sprintf(l_msg, "Key gen %d times", a_times);
    benchmark_mgs_time(l_msg, l_gen_time);
    sprintf(l_msg, "Bob shared key gen %d times", a_times);
    benchmark_mgs_time(l_msg, l_bob_shared);
    sprintf(l_msg, "Alice shared key gen %d times", a_times);
    benchmark_mgs_time(l_msg, l_alice_shared);
    return 0;
}