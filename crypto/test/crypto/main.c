#include "dap_enc_base64_test.h"
#include "dap_enc_base58_test.h"
#include "dap_enc_test.h"
#include "dap_enc_benchmark_test.h"
#include "dap_enc_multithread_test.h"
#include "dap_enc_ringct20_test.h"
#include "rand/dap_rand.h"
#include "dap_common.h"

int main(void) {
    // switch off debug info from library
    dap_log_level_set(L_WARNING);
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    
    int l_ret = 0;
    const int l_times = 5;

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
    dap_enc_base64_tests_run(l_times);
    dap_enc_base58_tests_run(l_times);
    dap_enc_ringct20_tests_run(l_times);
    dap_enc_benchmark_tests_run(l_times);
    dap_enc_multithread_tests_run(l_times);
    return 0;
}
