/**
 * @file fuzz_aes256_decrypt.c
 * @brief libFuzzer target for AES-256-CBC decryption.
 *
 * Feeds arbitrary ciphertext into the AES-256-CBC decrypt path.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_enc_aes.h"

static dap_enc_key_t *s_key = NULL;

static void s_init(void)
{
    dap_log_level_set(L_CRITICAL);
    s_key = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_IAES, NULL, 0, NULL, 0, 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *a_data, size_t a_size)
{
    if (!s_key)
        s_init();

    void *l_out = NULL;
    size_t l_out_len = dap_enc_aes256_cbc_decrypt(
        s_key, a_data, a_size, &l_out);

    if (l_out)
        DAP_DELETE(l_out);

    (void)l_out_len;
    return 0;
}
