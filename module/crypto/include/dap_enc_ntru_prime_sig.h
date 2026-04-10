/**
 * @file dap_enc_ntru_prime_sig.h
 * @brief DAP encryption wrapper for NTRU Prime Signature scheme.
 *
 * Ring: R = Z[x]/(x^761 - x - 1), q = 131071 (Mersenne prime 2^17-1)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "dap_enc_key.h"

#define DAP_NTRU_PRIME_SIG_P             761
#define DAP_NTRU_PRIME_SIG_Q             131071

#define DAP_NTRU_PRIME_SIG_POLY_BYTES    ((DAP_NTRU_PRIME_SIG_P * 17 + 7) / 8)   /* 1617 */
#define DAP_NTRU_PRIME_SIG_SEED_BYTES    32
#define DAP_NTRU_PRIME_SIG_SMALL_BYTES   ((DAP_NTRU_PRIME_SIG_P + 3) / 4)        /* 191  */

#define DAP_NTRU_PRIME_SIG_PUBLICKEYBYTES  (2 * DAP_NTRU_PRIME_SIG_POLY_BYTES)
#define DAP_NTRU_PRIME_SIG_SECRETKEYBYTES  (DAP_NTRU_PRIME_SIG_SMALL_BYTES + DAP_NTRU_PRIME_SIG_PUBLICKEYBYTES)
#define DAP_NTRU_PRIME_SIG_BYTES           (DAP_NTRU_PRIME_SIG_SEED_BYTES + DAP_NTRU_PRIME_SIG_POLY_BYTES)

#ifdef __cplusplus
extern "C" {
#endif

void dap_enc_ntru_prime_sig_key_new(dap_enc_key_t *a_key);
void dap_enc_ntru_prime_sig_key_new_generate(dap_enc_key_t *a_key,
        const void *kex_buf, size_t kex_size,
        const void *seed, size_t seed_size, size_t key_size);
void dap_enc_ntru_prime_sig_key_delete(dap_enc_key_t *a_key);

int dap_enc_ntru_prime_sig_get_sign(dap_enc_key_t *a_key,
        const void *a_msg, const size_t a_msg_len,
        void *a_sig, const size_t a_sig_len);
int dap_enc_ntru_prime_sig_verify_sign(dap_enc_key_t *a_key,
        const void *a_msg, const size_t a_msg_len,
        void *a_sig, const size_t a_sig_len);

uint8_t *dap_enc_ntru_prime_sig_write_signature(const void *a_sig, size_t *a_buflen_out);
void    *dap_enc_ntru_prime_sig_read_signature(const uint8_t *a_buf, size_t a_buflen);
uint8_t *dap_enc_ntru_prime_sig_write_public_key(const void *a_key, size_t *a_buflen_out);
void    *dap_enc_ntru_prime_sig_read_public_key(const uint8_t *a_buf, size_t a_buflen);
uint8_t *dap_enc_ntru_prime_sig_write_private_key(const void *a_key, size_t *a_buflen_out);
void    *dap_enc_ntru_prime_sig_read_private_key(const uint8_t *a_buf, size_t a_buflen);

void dap_enc_ntru_prime_sig_signature_delete(void *a_sig);
void dap_enc_ntru_prime_sig_public_key_delete(void *a_pubkey);
void dap_enc_ntru_prime_sig_private_key_delete(void *a_privkey);
void dap_enc_ntru_prime_sig_private_and_public_keys_delete(dap_enc_key_t *a_key);

DAP_STATIC_INLINE uint64_t dap_enc_ntru_prime_sig_ser_key_size(UNUSED_ARG const void *a_in) {
    return DAP_NTRU_PRIME_SIG_SECRETKEYBYTES;
}
DAP_STATIC_INLINE uint64_t dap_enc_ntru_prime_sig_ser_pkey_size(UNUSED_ARG const void *a_in) {
    return DAP_NTRU_PRIME_SIG_PUBLICKEYBYTES;
}
DAP_STATIC_INLINE uint64_t dap_enc_ntru_prime_sig_deser_key_size(UNUSED_ARG const void *a_in) {
    return DAP_NTRU_PRIME_SIG_SECRETKEYBYTES;
}
DAP_STATIC_INLINE uint64_t dap_enc_ntru_prime_sig_deser_pkey_size(UNUSED_ARG const void *a_in) {
    return DAP_NTRU_PRIME_SIG_PUBLICKEYBYTES;
}
DAP_STATIC_INLINE uint64_t dap_enc_ntru_prime_sig_signature_size(UNUSED_ARG const void *a_in) {
    return DAP_NTRU_PRIME_SIG_BYTES;
}

#ifdef __cplusplus
}
#endif
