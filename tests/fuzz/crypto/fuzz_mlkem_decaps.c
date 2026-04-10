/**
 * @file fuzz_mlkem_decaps.c
 * @brief libFuzzer target for ML-KEM decapsulation.
 *
 * Generates a valid Alice keypair once, then feeds arbitrary
 * ciphertext blobs into the decapsulation (gen_alice_shared_key) path.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_enc_mlkem.h"
#include "dap_sign.h"

static dap_enc_key_t *s_alice = NULL;

static void s_init(void)
{
    dap_log_level_set(L_CRITICAL);
    s_alice = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_KEM_ML_KEM,
        NULL, 0, NULL, 0, DAP_SIGN_PARAMS_SECURITY_3);
}

int LLVMFuzzerTestOneInput(const uint8_t *a_data, size_t a_size)
{
    if (!s_alice)
        s_init();
    if (a_size == 0)
        return 0;

    dap_enc_mlkem_gen_alice_shared_key(
        s_alice, NULL, a_size, (uint8_t *)a_data);

    return 0;
}
