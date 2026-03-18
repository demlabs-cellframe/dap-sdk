/*
 * DAP encryption wrapper for ChaCha20-Poly1305 AEAD.
 * Wire format: nonce (12 bytes) || ciphertext || tag (16 bytes).
 */

#include <string.h>
#include "dap_enc_chacha20_poly1305.h"
#include "dap_chacha20_poly1305.h"
#include "dap_common.h"
#include "dap_rand.h"

#define LOG_TAG "dap_enc_chacha20_poly1305"

void dap_enc_chacha20_poly1305_key_new(dap_enc_key_t *a_key)
{
    a_key->type = DAP_ENC_KEY_TYPE_CHACHA20_POLY1305;
    a_key->enc = dap_enc_chacha20_poly1305_encrypt;
    a_key->dec = dap_enc_chacha20_poly1305_decrypt;
    a_key->enc_na = NULL;
    a_key->dec_na = NULL;
}

void dap_enc_chacha20_poly1305_key_generate(dap_enc_key_t *a_key,
        UNUSED_ARG const void *kex_buf, UNUSED_ARG size_t kex_size,
        const void *seed, size_t seed_size, UNUSED_ARG size_t key_size)
{
    dap_return_if_pass(!a_key);
    DAP_DEL_Z(a_key->priv_key_data);

    a_key->priv_key_data_size = DAP_CHACHA20_KEY_SIZE;
    a_key->priv_key_data = DAP_NEW_Z_SIZE(uint8_t, DAP_CHACHA20_KEY_SIZE);
    if (!a_key->priv_key_data) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return;
    }

    if (seed && seed_size >= DAP_CHACHA20_KEY_SIZE) {
        memcpy(a_key->priv_key_data, seed, DAP_CHACHA20_KEY_SIZE);
    } else {
        dap_random_bytes(a_key->priv_key_data, DAP_CHACHA20_KEY_SIZE);
    }
}

void dap_enc_chacha20_poly1305_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    if (a_key->priv_key_data) {
        memset(a_key->priv_key_data, 0, a_key->priv_key_data_size);
        DAP_DEL_Z(a_key->priv_key_data);
    }
    a_key->priv_key_data_size = 0;
}

size_t dap_enc_chacha20_poly1305_calc_encode_size(size_t a_size)
{
    return DAP_CHACHA20_NONCE_SIZE + a_size + DAP_CHACHA20_POLY1305_TAG_SIZE;
}

size_t dap_enc_chacha20_poly1305_calc_decode_size(size_t a_size)
{
    if (a_size <= DAP_CHACHA20_NONCE_SIZE + DAP_CHACHA20_POLY1305_TAG_SIZE)
        return 0;
    return a_size - DAP_CHACHA20_NONCE_SIZE - DAP_CHACHA20_POLY1305_TAG_SIZE;
}

size_t dap_enc_chacha20_poly1305_encrypt(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void **a_out)
{
    dap_return_val_if_pass(!a_key || !a_key->priv_key_data || !a_in || !a_out, 0);

    size_t l_out_size = dap_enc_chacha20_poly1305_calc_encode_size(a_in_size);
    *a_out = DAP_NEW_Z_SIZE(uint8_t, l_out_size);
    if (!*a_out)
        return 0;

    uint8_t *l_buf = (uint8_t *)*a_out;
    uint8_t *l_nonce = l_buf;
    uint8_t *l_ct = l_buf + DAP_CHACHA20_NONCE_SIZE;
    uint8_t *l_tag = l_ct + a_in_size;

    dap_random_bytes(l_nonce, DAP_CHACHA20_NONCE_SIZE);

    if (dap_chacha20_poly1305_seal(l_ct, l_tag,
            (const uint8_t *)a_in, a_in_size,
            NULL, 0,
            (const uint8_t *)a_key->priv_key_data, l_nonce) != 0) {
        DAP_DEL_Z(*a_out);
        return 0;
    }
    return l_out_size;
}

size_t dap_enc_chacha20_poly1305_decrypt(dap_enc_key_t *a_key, const void *a_in,
        size_t a_in_size, void **a_out)
{
    dap_return_val_if_pass(!a_key || !a_key->priv_key_data || !a_in || !a_out, 0);

    size_t l_pt_size = dap_enc_chacha20_poly1305_calc_decode_size(a_in_size);
    if (!l_pt_size)
        return 0;

    const uint8_t *l_buf = (const uint8_t *)a_in;
    const uint8_t *l_nonce = l_buf;
    const uint8_t *l_ct = l_buf + DAP_CHACHA20_NONCE_SIZE;
    const uint8_t *l_tag = l_ct + l_pt_size;

    *a_out = DAP_NEW_Z_SIZE(uint8_t, l_pt_size);
    if (!*a_out)
        return 0;

    if (dap_chacha20_poly1305_open((uint8_t *)*a_out,
            l_ct, l_pt_size, l_tag,
            NULL, 0,
            (const uint8_t *)a_key->priv_key_data, l_nonce) != 0) {
        DAP_DEL_Z(*a_out);
        return 0;
    }
    return l_pt_size;
}
