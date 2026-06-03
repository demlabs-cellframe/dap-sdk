/**
 * @file dap_enc_ntru_prime_sig.c
 * @brief DAP encryption wrapper for NTRU Prime Signature.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "dap_enc_ntru_prime_sig.h"
#include "dap_ntru_prime_sig.h"
#include "dap_common.h"

#define LOG_TAG "dap_enc_ntru_prime_sig"

_Static_assert(DAP_NTRU_PRIME_SIG_PUBLICKEYBYTES == NTRU_PRIME_SIG_PUBLICKEYBYTES, "public key size mismatch");
_Static_assert(DAP_NTRU_PRIME_SIG_SECRETKEYBYTES == NTRU_PRIME_SIG_SECRETKEYBYTES, "secret key size mismatch");
_Static_assert(DAP_NTRU_PRIME_SIG_BYTES == NTRU_PRIME_SIG_BYTES, "signature size mismatch");

void dap_enc_ntru_prime_sig_key_new(dap_enc_key_t *a_key)
{
    a_key->type = DAP_ENC_KEY_TYPE_SIG_NTRU_PRIME;
    a_key->enc = NULL;
    a_key->dec = NULL;
    a_key->enc_na = NULL;
    a_key->dec_na = NULL;
    a_key->sign_get = dap_enc_ntru_prime_sig_get_sign;
    a_key->sign_verify = dap_enc_ntru_prime_sig_verify_sign;
}

void dap_enc_ntru_prime_sig_key_new_generate(dap_enc_key_t *a_key,
        UNUSED_ARG const void *kex_buf, UNUSED_ARG size_t kex_size,
        UNUSED_ARG const void *seed, UNUSED_ARG size_t seed_size,
        UNUSED_ARG size_t key_size)
{
    dap_return_if_pass(!a_key);

    a_key->priv_key_data_size = NTRU_PRIME_SIG_SECRETKEYBYTES;
    a_key->pub_key_data_size  = NTRU_PRIME_SIG_PUBLICKEYBYTES;

    a_key->pub_key_data  = DAP_NEW_Z_SIZE(uint8_t, NTRU_PRIME_SIG_PUBLICKEYBYTES);
    a_key->priv_key_data = DAP_NEW_Z_SIZE(uint8_t, NTRU_PRIME_SIG_SECRETKEYBYTES);

    if (!a_key->pub_key_data || !a_key->priv_key_data) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_Z(a_key->pub_key_data);
        DAP_DEL_Z(a_key->priv_key_data);
        return;
    }

    if (ntru_prime_sig_keypair(a_key->pub_key_data, a_key->priv_key_data) != 0) {
        log_it(L_ERROR, "NTRU Prime signature key generation failed");
        DAP_DEL_Z(a_key->pub_key_data);
        DAP_DEL_Z(a_key->priv_key_data);
    }
}

void dap_enc_ntru_prime_sig_key_delete(dap_enc_key_t *a_key)
{
    dap_enc_ntru_prime_sig_private_and_public_keys_delete(a_key);
}

int dap_enc_ntru_prime_sig_get_sign(dap_enc_key_t *a_key,
        const void *a_msg, const size_t a_msg_len,
        void *a_sig, const size_t a_sig_len)
{
    dap_return_val_if_pass(!a_key || !a_key->priv_key_data || !a_msg || !a_sig, -1);
    dap_return_val_if_pass(a_sig_len < NTRU_PRIME_SIG_BYTES, -2);

    size_t l_sig_len = 0;
    int l_ret = ntru_prime_sig_sign(a_sig, &l_sig_len,
            a_msg, a_msg_len, a_key->priv_key_data);
    return l_ret;
}

int dap_enc_ntru_prime_sig_verify_sign(dap_enc_key_t *a_key,
        const void *a_msg, const size_t a_msg_len,
        void *a_sig, const size_t a_sig_len)
{
    dap_return_val_if_pass(!a_key || !a_key->pub_key_data || !a_msg || !a_sig, -1);

    return ntru_prime_sig_verify(a_sig, a_sig_len,
            a_msg, a_msg_len, a_key->pub_key_data);
}

uint8_t *dap_enc_ntru_prime_sig_write_signature(const void *a_sig, size_t *a_buflen_out)
{
    dap_return_val_if_pass(!a_sig || !a_buflen_out, NULL);
    *a_buflen_out = NTRU_PRIME_SIG_BYTES;
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, NTRU_PRIME_SIG_BYTES);
    if (l_buf)
        memcpy(l_buf, a_sig, NTRU_PRIME_SIG_BYTES);
    return l_buf;
}

void *dap_enc_ntru_prime_sig_read_signature(const uint8_t *a_buf, size_t a_buflen)
{
    dap_return_val_if_pass(!a_buf || a_buflen < NTRU_PRIME_SIG_BYTES, NULL);
    uint8_t *l_sig = DAP_NEW_Z_SIZE(uint8_t, NTRU_PRIME_SIG_BYTES);
    if (l_sig)
        memcpy(l_sig, a_buf, NTRU_PRIME_SIG_BYTES);
    return l_sig;
}

uint8_t *dap_enc_ntru_prime_sig_write_public_key(const void *a_key, size_t *a_buflen_out)
{
    dap_return_val_if_pass(!a_key || !a_buflen_out, NULL);
    *a_buflen_out = NTRU_PRIME_SIG_PUBLICKEYBYTES;
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, NTRU_PRIME_SIG_PUBLICKEYBYTES);
    if (l_buf)
        memcpy(l_buf, a_key, NTRU_PRIME_SIG_PUBLICKEYBYTES);
    return l_buf;
}

void *dap_enc_ntru_prime_sig_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
    dap_return_val_if_pass(!a_buf || a_buflen < NTRU_PRIME_SIG_PUBLICKEYBYTES, NULL);
    uint8_t *l_key = DAP_NEW_Z_SIZE(uint8_t, NTRU_PRIME_SIG_PUBLICKEYBYTES);
    if (l_key)
        memcpy(l_key, a_buf, NTRU_PRIME_SIG_PUBLICKEYBYTES);
    return l_key;
}

uint8_t *dap_enc_ntru_prime_sig_write_private_key(const void *a_key, size_t *a_buflen_out)
{
    dap_return_val_if_pass(!a_key || !a_buflen_out, NULL);
    *a_buflen_out = NTRU_PRIME_SIG_SECRETKEYBYTES;
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, NTRU_PRIME_SIG_SECRETKEYBYTES);
    if (l_buf)
        memcpy(l_buf, a_key, NTRU_PRIME_SIG_SECRETKEYBYTES);
    return l_buf;
}

void *dap_enc_ntru_prime_sig_read_private_key(const uint8_t *a_buf, size_t a_buflen)
{
    dap_return_val_if_pass(!a_buf || a_buflen < NTRU_PRIME_SIG_SECRETKEYBYTES, NULL);
    uint8_t *l_key = DAP_NEW_Z_SIZE(uint8_t, NTRU_PRIME_SIG_SECRETKEYBYTES);
    if (l_key)
        memcpy(l_key, a_buf, NTRU_PRIME_SIG_SECRETKEYBYTES);
    return l_key;
}

void dap_enc_ntru_prime_sig_signature_delete(void *a_sig)
{
    if (a_sig)
        memset(a_sig, 0, NTRU_PRIME_SIG_BYTES);
}

void dap_enc_ntru_prime_sig_public_key_delete(void *a_pubkey)
{
    if (a_pubkey) {
        memset(a_pubkey, 0, NTRU_PRIME_SIG_PUBLICKEYBYTES);
        DAP_DELETE(a_pubkey);
    }
}

void dap_enc_ntru_prime_sig_private_key_delete(void *a_privkey)
{
    if (a_privkey) {
        memset(a_privkey, 0, NTRU_PRIME_SIG_SECRETKEYBYTES);
        DAP_DELETE(a_privkey);
    }
}

void dap_enc_ntru_prime_sig_private_and_public_keys_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    dap_enc_ntru_prime_sig_private_key_delete(a_key->priv_key_data);
    a_key->priv_key_data = NULL;
    a_key->priv_key_data_size = 0;
    dap_enc_ntru_prime_sig_public_key_delete(a_key->pub_key_data);
    a_key->pub_key_data = NULL;
    a_key->pub_key_data_size = 0;
}
