/**
 * @file dap_ntru_prime_sig.h
 * @brief NTRU Prime Signature — Fiat-Shamir with Aborts on R = Z[x]/(x^p - x - 1).
 *
 * Ring: R = Z[x]/(x^761 - x - 1), q = 131071 (Mersenne prime 2^17-1)
 * Secret: f (ternary), g (weight-w). Public: h = g·f^{-1} mod q, t = g.
 * Sign: y ← [-γ₁,γ₁], w = h·y mod q, c = H(msg||w), z = y + c·f.
 * Reject if ||z||∞ ≥ γ₁ - β. Signature = (c_seed, z).
 * Verify: w' = h·z - c·g mod q, check H(msg||w') == c_seed.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NTRU_PRIME_SIG_P     761
#define NTRU_PRIME_SIG_Q     131071      /* 2^17 - 1, Mersenne prime */
#define NTRU_PRIME_SIG_W     286
#define NTRU_PRIME_SIG_TAU   39          /* challenge weight (non-zero coefficients) */
#define NTRU_PRIME_SIG_GAMMA1 65535      /* (q-1)/2, mask range */
#define NTRU_PRIME_SIG_BETA  78          /* 2·τ, rejection bound */

/* coefficient encoding: 17 bits per coefficient, packed */
#define NTRU_PRIME_SIG_POLY_BYTES    ((NTRU_PRIME_SIG_P * 17 + 7) / 8)  /* 1617 bytes */
#define NTRU_PRIME_SIG_SEED_BYTES    32  /* SHA3-256 hash */
#define NTRU_PRIME_SIG_SMALL_BYTES   ((NTRU_PRIME_SIG_P + 3) / 4)  /* 2-bit encoding */

/* key and signature sizes */
#define NTRU_PRIME_SIG_PUBLICKEYBYTES  (2 * NTRU_PRIME_SIG_POLY_BYTES)  /* h + g */
#define NTRU_PRIME_SIG_SECRETKEYBYTES  (NTRU_PRIME_SIG_SMALL_BYTES + NTRU_PRIME_SIG_PUBLICKEYBYTES)  /* f + pk */
#define NTRU_PRIME_SIG_BYTES           (NTRU_PRIME_SIG_SEED_BYTES + NTRU_PRIME_SIG_POLY_BYTES)  /* c_seed + z */

/**
 * @brief Generate signature key pair.
 * @param[out] a_pk  Public key buffer (NTRU_PRIME_SIG_PUBLICKEYBYTES)
 * @param[out] a_sk  Secret key buffer (NTRU_PRIME_SIG_SECRETKEYBYTES)
 * @return 0 on success, -1 on failure.
 */
int ntru_prime_sig_keypair(uint8_t *a_pk, uint8_t *a_sk);

/**
 * @brief Sign a message.
 * @param[out] a_sig      Signature buffer (NTRU_PRIME_SIG_BYTES)
 * @param[out] a_sig_len  Actual signature length
 * @param[in]  a_msg      Message to sign
 * @param[in]  a_msg_len  Message length
 * @param[in]  a_sk       Secret key
 * @return 0 on success, -1 on failure.
 */
int ntru_prime_sig_sign(uint8_t *a_sig, size_t *a_sig_len,
                       const uint8_t *a_msg, size_t a_msg_len,
                       const uint8_t *a_sk);

/**
 * @brief Verify a signature.
 * @param[in] a_sig      Signature
 * @param[in] a_sig_len  Signature length
 * @param[in] a_msg      Message
 * @param[in] a_msg_len  Message length
 * @param[in] a_pk       Public key
 * @return 0 if valid, non-zero if invalid.
 */
int ntru_prime_sig_verify(const uint8_t *a_sig, size_t a_sig_len,
                         const uint8_t *a_msg, size_t a_msg_len,
                         const uint8_t *a_pk);

#ifdef __cplusplus
}
#endif
