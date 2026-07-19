/*
 * Authors:
 * Dmitriy A. Gearasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2024
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_hash.h"
#include "dap_hash.h"
#include "dap_hash_shake256.h"
#include "dap_common.h"
#include "dap_crypto_common.h"
#include "dap_rand.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#define LOG_TAG "chipmunk_poly"

// Определение MIN для использования в функциях работы с массивами
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// Флаг для расширенного логирования
static bool s_debug_more = false;

/**
 * @brief Transform polynomial to NTT form
 */
int chipmunk_poly_ntt(chipmunk_poly_t *a_poly) {
    if (!a_poly) {
        log_it(L_ERROR, "NULL input parameter in chipmunk_poly_ntt");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    chipmunk_ntt(a_poly->coeffs);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Inverse transform from NTT form
 */
int chipmunk_poly_invntt(chipmunk_poly_t *a_poly) {
    if (!a_poly) {
        log_it(L_ERROR, "NULL input parameter in chipmunk_poly_invntt");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    chipmunk_invntt(a_poly->coeffs);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Fill polynomial with uniformly distributed coefficients
 */
int chipmunk_poly_uniform(chipmunk_poly_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce) {
    if (!a_poly || !a_seed) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_poly_uniform");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    int l_result = dap_chipmunk_hash_sample_poly(a_poly->coeffs, a_seed, a_nonce);
    if (l_result != 0) {
        log_it(L_WARNING, "Error in polynomial sampling");
        return CHIPMUNK_ERROR_HASH_FAILED;
    }
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Check polynomial norm
 *
 * @param[in] a_poly Polynomial to check
 * @param[in] a_bound Maximum absolute value that coefficients can have
 * @return Returns 0 if all coefficients are within the bound, 1 otherwise
 */
int chipmunk_poly_chknorm(const chipmunk_poly_t *a_poly, int32_t a_bound) {
    return chipmunk_poly_chknorm_q(a_poly, a_bound, (uint64_t)CHIPMUNK_Q);
}

int chipmunk_poly_chknorm_q(const chipmunk_poly_t *a_poly, int32_t a_bound, uint64_t q) {
    if (!a_poly) {
        log_it(L_ERROR, "NULL input parameter in chipmunk_poly_chknorm_q");
        return 1;
    }

    int32_t l_q_half = (int32_t)(q / 2u);
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        int32_t l_t = a_poly->coeffs[l_i];
        if (l_t >= l_q_half)
            l_t -= (int32_t)q;
        int32_t l_abs_val = (l_t < 0) ? -l_t : l_t;
        if (l_abs_val > a_bound)
            return 1;
    }
    return 0;
}

/**
 * @brief Generate challenge polynomial from hash
 * 
 * NOTE: This function generates a sparse polynomial for HOTS challenge
 */
int chipmunk_poly_challenge(chipmunk_poly_t *c, const uint8_t *hash, size_t hash_len) {
    if (!c || !hash) {
        log_it(L_ERROR, "NULL parameters in chipmunk_poly_challenge");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    if (hash_len < 16) {
        log_it(L_ERROR, "Hash too short in chipmunk_poly_challenge: %zu bytes", hash_len);
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }

    /*
     * CR-D14 remediation: the previous implementation derived its entropy
     * pool by XOR'ing the input hash with the byte index
     * (extended_hash[i] = hash[i % hash_len] ^ (i + 1)) — a reversible
     * transformation that provides no new entropy, only permutes the hash
     * and introduces a data-dependent exit when MAX_ATTEMPTS was hit
     * (producing fewer-than-ALPHA_H coefficients without signalling an
     * error to the caller). Both issues are fixed here:
     *
     *   state = SHAKE256("CHIPMUNK/poly_challenge/v1" || hash)
     *   pull 2 bytes → pos in [0, 2^16)
     *   reject if pos >= (2^16 - 2^16 % N)
     *   pos %= N
     *   pull 1 byte, sign = (byte & 1) ? +1 : -1
     *   if coeffs[pos] == 0 assign sign, weight++
     *
     * The loop runs until exactly ALPHA_H non-zero coefficients are
     * placed. On inputs that cannot produce enough distinct positions
     * (N too small for ALPHA_H) we return CHIPMUNK_ERROR_INTERNAL
     * instead of silently emitting a truncated challenge.
     */

    static const uint8_t k_domain[] = "CHIPMUNK/poly_challenge/v1";

    memset(c, 0, sizeof(*c));

    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));

    /* Guard against integer overflow in l_in_len (heap overflow). */
    if (hash_len > SIZE_MAX - sizeof(k_domain) - 1) {
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    const size_t l_in_len = sizeof(k_domain) + hash_len;
    uint8_t *l_in = DAP_NEW_Z_SIZE(uint8_t, l_in_len);
    if (!l_in) {
        return CHIPMUNK_ERROR_MEMORY;
    }
    memcpy(l_in, k_domain, sizeof(k_domain));
    memcpy(l_in + sizeof(k_domain), hash, hash_len);
    dap_hash_shake256_absorb(l_state, l_in, l_in_len);
    DAP_DELETE(l_in);

    uint8_t l_squeeze[DAP_SHAKE256_RATE];
    size_t l_sq_pos = DAP_SHAKE256_RATE;

    const uint32_t l_range16 = 1u << 16;
    const uint32_t l_mul_n = (l_range16 / (uint32_t)CHIPMUNK_N) * (uint32_t)CHIPMUNK_N;

    int l_weight_set = 0;
    const size_t k_max_blocks = 1u << 20;
    size_t l_blocks_squeezed = 0;

    while (l_weight_set < CHIPMUNK_ALPHA_H) {
        if (l_sq_pos + 3 > DAP_SHAKE256_RATE) {
            if (l_blocks_squeezed++ >= k_max_blocks) {
                log_it(L_ERROR, "chipmunk_poly_challenge: SHAKE squeeze budget exhausted");
                return CHIPMUNK_ERROR_INTERNAL;
            }
            dap_hash_shake256_squeezeblocks(l_squeeze, 1, l_state);
            l_sq_pos = 0;
        }

        uint32_t l_pos16 = (uint32_t)l_squeeze[l_sq_pos]
                         | ((uint32_t)l_squeeze[l_sq_pos + 1] << 8);
        uint8_t  l_sign_byte = l_squeeze[l_sq_pos + 2];
        l_sq_pos += 3;

        if (l_pos16 >= l_mul_n) {
            continue; // reject-sample to avoid modulo bias
        }
        uint32_t l_pos = l_pos16 % (uint32_t)CHIPMUNK_N;

        if (c->coeffs[l_pos] == 0) {
            c->coeffs[l_pos] = (l_sign_byte & 1u) ? 1 : -1;
            l_weight_set++;
        }
    }

    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Create polynomial from hash of message (следуя оригинальному Rust коду)
 * 
 * КРИТИЧЕСКИ ВАЖНО: оригинальный Rust код:
 * fn from_hash_message(msg: &[u8]) -> Self {
 *     let mut hasher = Sha256::new();
 *     hasher.update(msg);
 *     let seed = hasher.finalize().into();
 *     let mut rng = rand_chacha::ChaCha20Rng::from_seed(seed);
 *     Self::rand_ternary(&mut rng, ALPHA_H)
 * }
 * 
 * @param a_poly Output polynomial
 * @param a_message Message to hash
 * @param a_message_len Message length
 * @return 0 on success, negative on error
 */
int chipmunk_poly_from_hash(chipmunk_poly_t *a_poly, const uint8_t *a_message, size_t a_message_len) {
    debug_if(s_debug_more, L_INFO, "chipmunk_poly_from_hash: message=%p, len=%zu", a_message, a_message_len);

    if (!a_poly) {
        log_it(L_ERROR, "NULL poly parameter in chipmunk_poly_from_hash");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    // Allow empty message (len=0) but require non-NULL pointer for non-empty message
    if (a_message_len > 0 && !a_message) {
        log_it(L_ERROR, "NULL message with non-zero length in chipmunk_poly_from_hash");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    /*
     * CR-D5 remediation: replace the 32-bit LCG (coeff a=1664525, c=1013904223)
     * seeded from only 4 bytes of SHA3-256 with a SHAKE256 extendable-output
     * function fed the full message and a domain separator. The ALPHA_H-of-N
     * ternary polynomial is now sampled via unbiased rejection sampling:
     *
     *   state = SHAKE256(DOMAIN_TAG || message)
     *   repeat:
     *     pull 3 bytes → pos in [0, 2^24)
     *     reject if pos >= (2^24 - 2^24 % N)   (avoids modulo bias)
     *     pos %= N
     *     if coeffs[pos] == 0:
     *         pull 1 byte, sign = (byte & 1) ? +1 : -1
     *         coeffs[pos] = sign
     *         weight++
     *
     * No 32-bit cycle, no seed truncation, no unbalanced +1/-1 distribution.
     */

    static const uint8_t k_domain[] = "CHIPMUNK/poly_from_hash/v1";

    memset(a_poly, 0, sizeof(*a_poly));

    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));

    // Absorb domain tag and message as one contiguous SHAKE256 input.
    // A single absorb call is sufficient because the rate (136 bytes) is
    // already large enough to swallow the domain tag and the message in
    // one shot.
    /* Guard against integer overflow in l_in_len (heap overflow). */
    if (a_message_len > SIZE_MAX - sizeof(k_domain) - 1) {
        log_it(L_ERROR, "chipmunk_poly_from_hash: message too long (%zu)", a_message_len);
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    const size_t l_in_len = sizeof(k_domain) + a_message_len;
    uint8_t *l_in = DAP_NEW_Z_SIZE(uint8_t, l_in_len);
    if (!l_in) {
        log_it(L_ERROR, "chipmunk_poly_from_hash: allocation failed (len=%zu)", l_in_len);
        return CHIPMUNK_ERROR_MEMORY;
    }
    memcpy(l_in, k_domain, sizeof(k_domain));
    if (a_message_len > 0) {
        memcpy(l_in + sizeof(k_domain), a_message, a_message_len);
    }
    dap_hash_shake256_absorb(l_state, l_in, l_in_len);
    DAP_DELETE(l_in);

    uint8_t l_squeeze[DAP_SHAKE256_RATE];
    size_t l_sq_pos = DAP_SHAKE256_RATE;

    // Reject-sample positions in [0, N). 3 bytes → [0, 2^24).
    const uint32_t l_range24 = 1u << 24;
    const uint32_t l_mul_n = (l_range24 / (uint32_t)CHIPMUNK_N) * (uint32_t)CHIPMUNK_N;

    int l_weight_set = 0;
    // CHIPMUNK_ALPHA_H is small (≈37). Even adversarial inputs need only a
    // few hundred bytes of SHAKE output in practice; we cap at 1<<20 rate
    // blocks to fail fast on impossible parameters rather than hang.
    const size_t k_max_blocks = 1u << 20;
    size_t l_blocks_squeezed = 0;

    while (l_weight_set < CHIPMUNK_ALPHA_H) {
        // Refill the SHAKE buffer when we've exhausted it or when we'd
        // need to straddle the end (we consume 4 bytes per trial: 3 for
        // position, 1 for sign).
        if (l_sq_pos + 4 > DAP_SHAKE256_RATE) {
            if (l_blocks_squeezed++ >= k_max_blocks) {
                log_it(L_ERROR, "chipmunk_poly_from_hash: SHAKE squeeze budget exhausted");
                return CHIPMUNK_ERROR_INTERNAL;
            }
            dap_hash_shake256_squeezeblocks(l_squeeze, 1, l_state);
            l_sq_pos = 0;
        }

        uint32_t l_pos24 = (uint32_t)l_squeeze[l_sq_pos]
                         | ((uint32_t)l_squeeze[l_sq_pos + 1] << 8)
                         | ((uint32_t)l_squeeze[l_sq_pos + 2] << 16);
        uint8_t  l_sign_byte = l_squeeze[l_sq_pos + 3];
        l_sq_pos += 4;

        if (l_pos24 >= l_mul_n) {
            continue; // reject to avoid modulo bias
        }
        uint32_t l_pos = l_pos24 % (uint32_t)CHIPMUNK_N;

        if (a_poly->coeffs[l_pos] == 0) {
            a_poly->coeffs[l_pos] = (l_sign_byte & 1u) ? 1 : -1;
            l_weight_set++;
        }
    }

    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Lift coefficient to positive representation [0, q)
 * Based on original Rust implementation: (a % modulus + modulus) % modulus
 */
static int32_t chipmunk_poly_lift(int32_t a, int32_t modulus) {
    return (a % modulus + modulus) % modulus;
}

/* =========================================================================
 * Phase 9.14a: Per-q polynomial operations
 *
 * These _q variants accept an explicit modulus.
 * ======================================================================= */

int chipmunk_poly_ntt_q(chipmunk_poly_t *a_poly, const chipmunk_ntt_ctx_t *a_ctx) {
    if (!a_poly || !a_ctx) return CHIPMUNK_ERROR_NULL_PARAM;
    chipmunk_ntt_q(a_poly->coeffs, a_ctx);
    return CHIPMUNK_ERROR_SUCCESS;
}

int chipmunk_poly_invntt_q(chipmunk_poly_t *a_poly, const chipmunk_ntt_ctx_t *a_ctx) {
    if (!a_poly || !a_ctx) return CHIPMUNK_ERROR_NULL_PARAM;
    chipmunk_invntt_q(a_poly->coeffs, a_ctx);
    return CHIPMUNK_ERROR_SUCCESS;
}

int chipmunk_poly_add_q(chipmunk_poly_t *r, const chipmunk_poly_t *a,
                          const chipmunk_poly_t *b, uint64_t q) {
    if (!r || !a || !b) return CHIPMUNK_ERROR_NULL_PARAM;
    int32_t l_q = (int32_t)q;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t l_temp = (int64_t)a->coeffs[i] + (int64_t)b->coeffs[i];
        r->coeffs[i] = (int32_t)(l_temp % (int64_t)q);
        if (r->coeffs[i] < 0) r->coeffs[i] += l_q;
        /* Centered normalization [-q/2, q/2] (matches non-_q semantics). */
        if (r->coeffs[i] > l_q / 2) r->coeffs[i] -= l_q;
    }
    return CHIPMUNK_ERROR_SUCCESS;
}

int chipmunk_poly_sub_q(chipmunk_poly_t *r, const chipmunk_poly_t *a,
                          const chipmunk_poly_t *b, uint64_t q) {
    if (!r || !a || !b) return CHIPMUNK_ERROR_NULL_PARAM;
    int32_t l_q = (int32_t)q;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t l_temp = (int64_t)a->coeffs[i] - (int64_t)b->coeffs[i];
        r->coeffs[i] = (int32_t)(l_temp % (int64_t)q);
        if (r->coeffs[i] < 0) r->coeffs[i] += l_q;
        if (r->coeffs[i] > l_q / 2) r->coeffs[i] -= l_q;
    }
    return CHIPMUNK_ERROR_SUCCESS;
}

void chipmunk_poly_mul_ntt_q(chipmunk_poly_t *r, const chipmunk_poly_t *a,
                               const chipmunk_poly_t *b, uint64_t q) {
    if (!r || !a || !b) return;
    int32_t l_q = (int32_t)q;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t l_temp = ((int64_t)a->coeffs[i] * (int64_t)b->coeffs[i]) % (int64_t)q;
        r->coeffs[i] = (int32_t)l_temp;
        if (r->coeffs[i] < 0) r->coeffs[i] += l_q;
    }
}

void chipmunk_poly_add_ntt_q(chipmunk_poly_t *r, const chipmunk_poly_t *a,
                               const chipmunk_poly_t *b, uint64_t q) {
    if (!r || !a || !b) return;
    int64_t l_q = (int64_t)q;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t l_sum = (int64_t)a->coeffs[i] + (int64_t)b->coeffs[i];
        int64_t l_r = l_sum % l_q;
        int64_t l_neg = -(int64_t)(l_r < 0);
        l_r += l_q & l_neg;
        r->coeffs[i] = (int32_t)l_r;
    }
}

void chipmunk_poly_sub_ntt_q(chipmunk_poly_t *r, const chipmunk_poly_t *a,
                               const chipmunk_poly_t *b, uint64_t q) {
    if (!r || !a || !b) return;
    int64_t l_q = (int64_t)q;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t l_diff = (int64_t)a->coeffs[i] - (int64_t)b->coeffs[i];
        int64_t l_r = l_diff % l_q;
        int64_t l_neg = -(int64_t)(l_r < 0);
        l_r += l_q & l_neg;
        r->coeffs[i] = (int32_t)l_r;
    }
}

bool chipmunk_poly_equal_q(const chipmunk_poly_t *a, const chipmunk_poly_t *b,
                            uint64_t q) {
    if (!a || !b) return false;
    int32_t l_q = (int32_t)q;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int32_t l_l = chipmunk_poly_lift(a->coeffs[i], l_q);
        int32_t l_r = chipmunk_poly_lift(b->coeffs[i], l_q);
        if (l_l != l_r) return false;
    }
    return true;
}

/**
 * @brief Generate uniform polynomial with coefficients in range [-bound, bound]
 * Based on original Rust HOTSPoly::rand_mod_p function
 */
int chipmunk_poly_uniform_mod_p(chipmunk_poly_t *a_poly, const uint8_t a_seed[36], int32_t a_bound) {
    if (!a_poly || !a_seed) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    if (a_bound <= 0) {
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }

    /*
     * CR-D5 remediation: replace the 8×32-bit LCG (per-lane state fed from
     * 36 seed bytes, stepped with a=1664525, c=1013904223) by SHAKE256 over
     * a domain-separated seed. Coefficients in [-bound, bound] are drawn
     * via unbiased rejection sampling on 2*bound+1 values:
     *
     *   state = SHAKE256("CHIPMUNK/uniform_mod_p/v1" || seed)
     *   for each i in [0, N):
     *     pull 3 bytes → r in [0, 2^24)
     *     reject if r >= (2^24 - 2^24 % range)
     *     r %= range; coeff = (int32)r - bound
     *
     * No deterministic short period, no bias (the range is small for the
     * HOTS y-polynomial, so the rejection rate is negligible).
     */

    static const uint8_t k_domain[] = "CHIPMUNK/uniform_mod_p/v1";

    memset(a_poly, 0, sizeof(*a_poly));

    uint8_t l_abs_in[sizeof(k_domain) + 36];
    memcpy(l_abs_in, k_domain, sizeof(k_domain));
    memcpy(l_abs_in + sizeof(k_domain), a_seed, 36);

    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));
    dap_hash_shake256_absorb(l_state, l_abs_in, sizeof(l_abs_in));

    uint8_t l_squeeze[DAP_SHAKE256_RATE];
    size_t l_sq_pos = DAP_SHAKE256_RATE;

    const uint32_t l_range24 = 1u << 24;
    const uint32_t l_range = (uint32_t)(2 * a_bound + 1);
    if (l_range == 0 || l_range > l_range24) {
        log_it(L_ERROR, "chipmunk_poly_uniform_mod_p: bound %d out of supported range", a_bound);
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    const uint32_t l_mul = (l_range24 / l_range) * l_range;

    // Bound on SHAKE squeeze blocks for defence-in-depth; realistic worst
    // case for the Chipmunk bounds (≤ a few thousand) is well below 2^20.
    const size_t k_max_blocks = 1u << 20;
    size_t l_blocks_squeezed = 0;

    for (int i = 0; i < CHIPMUNK_N; i++) {
        uint32_t l_val;
        for (;;) {
            if (l_sq_pos + 3 > DAP_SHAKE256_RATE) {
                if (l_blocks_squeezed++ >= k_max_blocks) {
                    log_it(L_ERROR, "chipmunk_poly_uniform_mod_p: SHAKE squeeze budget exhausted");
                    return CHIPMUNK_ERROR_INTERNAL;
                }
                dap_hash_shake256_squeezeblocks(l_squeeze, 1, l_state);
                l_sq_pos = 0;
            }
            l_val = (uint32_t)l_squeeze[l_sq_pos]
                  | ((uint32_t)l_squeeze[l_sq_pos + 1] << 8)
                  | ((uint32_t)l_squeeze[l_sq_pos + 2] << 16);
            l_sq_pos += 3;
            if (l_val < l_mul) {
                break;
            }
        }
        a_poly->coeffs[i] = (int32_t)(l_val % l_range) - a_bound;
    }

    return CHIPMUNK_ERROR_SUCCESS;
}