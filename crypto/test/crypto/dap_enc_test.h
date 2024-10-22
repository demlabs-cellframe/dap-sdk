#pragma once
#include "dap_enc_key.h"

extern const dap_enc_key_type_t c_key_type_arr[];
extern const size_t c_keys_count;

DAP_STATIC_INLINE const char *s_key_type_to_str(dap_enc_key_type_t a_signe_key_type)
{
    switch (a_signe_key_type) {
        case DAP_ENC_KEY_TYPE_SIG_TESLA: return "TESLA";
        case DAP_ENC_KEY_TYPE_SIG_BLISS: return "BLISS";
        case DAP_ENC_KEY_TYPE_SIG_DILITHIUM: return "DILITHIUM";
        case DAP_ENC_KEY_TYPE_SIG_PICNIC: return "PICNIC";
        case DAP_ENC_KEY_TYPE_SIG_FALCON: return "FALCON";
        case DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS: return "SPHINCSPLUS";
#ifdef DAP_ECDSA
        case DAP_ENC_KEY_TYPE_SIG_ECDSA: return "ECDSA";
#endif
#ifdef DAP_SHIPOVNIK
        case DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK: return "SHIPOVNIK";
#endif
        default: return "UNDEFINED";//DAP_ENC_KEY_TYPE_NULL;
    }

}

void test_encypt_decrypt(int count_steps, const dap_enc_key_type_t key_type, const int cipher_key_size);
void test_encypt_decrypt_fast(int count_steps, const dap_enc_key_type_t key_type, const int cipher_key_size);

void dap_enc_tests_run(void);
void dap_enc_benchmark_tests_run(int a_times);
void dap_init_test_case();
void dap_cleanup_test_case();