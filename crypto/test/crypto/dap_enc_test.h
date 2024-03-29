#pragma once
#include "dap_enc_key.h"
void test_encypt_decrypt(int count_steps, const dap_enc_key_type_t key_type, const int cipher_key_size);
void test_encypt_decrypt_fast(int count_steps, const dap_enc_key_type_t key_type, const int cipher_key_size);

void dap_enc_tests_run(void);
int dap_enc_benchmark_tests_run(int a_times);
void dap_init_test_case();
void dap_cleanup_test_case();