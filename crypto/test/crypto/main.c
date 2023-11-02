#include "dap_enc_base64_test.h"
#include "dap_enc_base58_test.h"
#include "dap_enc_test.h"
#include "dap_enc_msrln_test.h"
#include "dap_enc_ringct20_test.h"
#include "dap_enc_kyber_test.h"
#ifndef DAP_CRYPTO_MULTISIGN_TEST_OFF
#include "dap_enc_sign_multi_test.h"
#endif
#include "rand/dap_rand.h"

#include "dap_common.h"
void dap_enc_newhope_tests_run(const int times);


int main(void) {
    // switch off debug info from library
    dap_log_level_set(L_CRITICAL);
    const int l_times = 10;
#ifndef DAP_CRYPTO_NEWHOPE_TEST_OFF
    // dap_enc_newhope_tests_run(l_times);
#endif

    test_encypt_decrypt(l_times, DAP_ENC_KEY_TYPE_SALSA2012, 32);
    test_encypt_decrypt_fast(l_times, DAP_ENC_KEY_TYPE_SALSA2012, 32);
    test_encypt_decrypt(l_times, DAP_ENC_KEY_TYPE_SEED_OFB, 32);
    test_encypt_decrypt_fast(l_times, DAP_ENC_KEY_TYPE_SEED_OFB, 32);
    test_encypt_decrypt(l_times, DAP_ENC_KEY_TYPE_GOST_OFB, 32);
    test_encypt_decrypt_fast(l_times, DAP_ENC_KEY_TYPE_GOST_OFB, 32);
    test_encypt_decrypt(l_times, DAP_ENC_KEY_TYPE_KUZN_OFB, 32);
    test_encypt_decrypt_fast(l_times, DAP_ENC_KEY_TYPE_KUZN_OFB, 32);
    test_encypt_decrypt(l_times, DAP_ENC_KEY_TYPE_BF_CBC, 0);
    test_encypt_decrypt_fast(l_times, DAP_ENC_KEY_TYPE_BF_CBC, 0);
    test_encypt_decrypt(l_times, DAP_ENC_KEY_TYPE_BF_OFB, 0);
    test_encypt_decrypt_fast(l_times, DAP_ENC_KEY_TYPE_BF_OFB, 0);
    test_encypt_decrypt(l_times, DAP_ENC_KEY_TYPE_IAES, 32);
    test_encypt_decrypt_fast(l_times, DAP_ENC_KEY_TYPE_IAES, 32);
    test_encypt_decrypt(l_times, DAP_ENC_KEY_TYPE_OAES, 32);
    test_encypt_decrypt_fast(l_times, DAP_ENC_KEY_TYPE_OAES, 32);

    dap_enc_tests_run();
    dap_enc_msrln_tests_run();
    dap_enc_base64_tests_run(l_times);
    dap_enc_base58_tests_run(l_times);
    dap_enc_kyber_test_run(l_times);
    dap_enc_benchmark_tests_run(l_times);


#ifndef DAP_CRYPTO_MULTISIGN_TEST_OFF
    dap_enc_multi_sign_tests_run(l_times);
#endif

    dap_enc_ringct20_tests_run(l_times);
}
