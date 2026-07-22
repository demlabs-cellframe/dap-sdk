/*
 * Authors:
 * Dmitriy A. Gearasimov <kahovski@gmail.com>
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

#include "chipmunk_hash.h"
#include "chipmunk_poly.h"
#include "dap_common.h"
#include "dap_hash.h"
#include "dap_crypto_common.h"
#include "chipmunk.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"
#include <errno.h>
#include <string.h>

#define LOG_TAG "chipmunk_hash"

/* CR-D11 (Round-3): the canonical hash surface of the Chipmunk module is
 * SHA-3 / Keccak only.  See chipmunk_hash.h for the policy statement.
 *
 * The previous Round-3 audit caught five dead-code helpers that diluted
 * this surface and acted as a foot-gun:
 *
 *   - dap_chipmunk_hash_sha2_256  (static SHA2-256 wrapper)
 *   - dap_chipmunk_hash_to_seed   (SHA2-256, never called)
 *   - dap_chipmunk_hash_challenge (SHA2-256, never called)
 *   - dap_chipmunk_hash_to_point  (SHA3-256, never called)
 *   - dap_chipmunk_hash_sha3_512  (SHA3-512, never called)
 *
 * They were removed wholesale in this patch.  Re-introducing any SHA2
 * primitive here is forbidden — see the policy comment in the header.
 */

/**
 * @brief Initialize hash functions for Chipmunk
 * @return Returns 0 on success, negative error code on failure
 */
int dap_chipmunk_hash_init(void) {
    // Currently there's no specific initialization needed
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief SHA3-256 wrapper function implementation
 */
int dap_chipmunk_hash_sha3_256(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen) {
    if (!a_output || !a_input) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Perform SHA3-256 hash
    dap_hash_sha3_256_raw(a_output, a_input, a_inlen);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief SHA3-384 wrapper function implementation
 */
int dap_chipmunk_hash_sha3_384(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen) {
    if (!a_output || !a_input) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Perform SHA3-384 hash
    dap_hash_sha3_384(a_output, a_input, a_inlen);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief SHAKE-128 XOF wrapper over the native DAP Keccak implementation.
 *
 * CR-D10 remediation: the previous body of this function was NOT SHAKE128
 * at all. It built the XOF output from chained SHA2-256 calls
 * (SHA256(input || counter_byte)) which
 *   - is not indifferentiable from a random oracle (block-wise independent),
 *   - silently truncated output at 4 KiB,
 *   - wraps a uint8_t counter → can only emit 256×32 = 8 KiB distinct blocks,
 *   - gives only the 256-bit preimage-resistance of SHA2, nowhere near
 *     the SHAKE128 XOF contract expected by the Chipmunk paper.
 *
 * This wrapper now dispatches to dap_hash_shake128 (real Keccak-based XOF
 * with rate 168 bytes) which is the primitive assumed by the reference
 * Chipmunk code and by the poly/matrix sampling routines below.
 */
int dap_chipmunk_hash_shake128(uint8_t *a_output, size_t a_outlen, const uint8_t *a_input, size_t a_inlen)
{
    if (!a_output || !a_input || !a_outlen) {
        log_it(L_ERROR, "NULL input parameters in dap_chipmunk_hash_shake128");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    dap_hash_shake128(a_output, a_outlen, a_input, a_inlen);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Generate random polynomial based on seed and nonce
 * 
 * @return Returns 0 on success, negative values on error:
 *         CHIPMUNK_ERROR_NULL_PARAM: NULL pointers
 *         CHIPMUNK_ERROR_OVERFLOW: Size overflow
 *         CHIPMUNK_ERROR_MEMORY: Memory allocation failure
 */
int dap_chipmunk_hash_sample_poly(int32_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce)
{
    if (!a_poly || !a_seed) {
        log_it(L_ERROR, "NULL input parameters in dap_chipmunk_hash_sample_poly");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    /*
     * CR-D11 remediation: sample CHIPMUNK_N coefficients of the HOTS
     * y-polynomial uniformly in [-gamma1, gamma1] using a streaming
     * SHAKE128 XOF seeded with domain-separated (seed || nonce_le16).
     *
     * The previous code read exactly 3 * CHIPMUNK_N bytes from the fake
     * SHAKE128 (SHA2+counter) wrapper and reduced them with `% range`,
     * which (a) sampled from a 256-bit/chunk hash rather than a true XOF
     * and (b) introduced ~0.1% modulo bias because 2^23 is not a multiple
     * of `range = 2*gamma1 + 1`. We replace this with true Keccak-based
     * SHAKE128 streaming + unbiased rejection sampling.
     */

    static const uint8_t k_domain[] = "CHIPMUNK/sample_poly/v1";
    uint8_t l_in[sizeof(k_domain) + 32 + 2];
    memcpy(l_in, k_domain, sizeof(k_domain));
    memcpy(l_in + sizeof(k_domain), a_seed, 32);
    l_in[sizeof(k_domain) + 32 + 0] = (uint8_t)(a_nonce & 0xff);
    l_in[sizeof(k_domain) + 32 + 1] = (uint8_t)((a_nonce >> 8) & 0xff);

    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));
    dap_hash_shake128_absorb(l_state, l_in, sizeof(l_in));

    uint8_t l_sq[DAP_SHAKE128_RATE];
    size_t  l_sq_pos = DAP_SHAKE128_RATE;

    const int32_t  l_gamma1 = 1 << 17;            // 131072
    const uint32_t l_range  = (uint32_t)(2 * l_gamma1 + 1); // 262145
    const uint32_t l_mul    = (0x800000u / l_range) * l_range; // 2^23 split on range boundary

    const size_t k_max_blocks = 1u << 20;
    size_t l_blocks = 0;

    for (int i = 0; i < CHIPMUNK_N; i++) {
        uint32_t l_val;
        for (;;) {
            if (l_sq_pos + 3 > DAP_SHAKE128_RATE) {
                if (l_blocks++ >= k_max_blocks) {
                    log_it(L_ERROR, "dap_chipmunk_hash_sample_poly: SHAKE128 squeeze budget exhausted");
                    memset(a_poly, 0, CHIPMUNK_N * sizeof(int32_t));
                    return CHIPMUNK_ERROR_INTERNAL;
                }
                dap_hash_shake128_squeezeblocks(l_sq, 1, l_state);
                l_sq_pos = 0;
            }
            l_val = (uint32_t)l_sq[l_sq_pos]
                  | ((uint32_t)l_sq[l_sq_pos + 1] << 8)
                  | ((uint32_t)l_sq[l_sq_pos + 2] << 16);
            l_val &= 0x7FFFFFu; // 23-bit word
            l_sq_pos += 3;
            if (l_val < l_mul) {
                break;
            }
        }
        a_poly[i] = (int32_t)(l_val % l_range) - l_gamma1;
    }

    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Generate random polynomial for matrix A based on seed and nonce
 * 
 * @param[out] a_poly Output polynomial coefficients
 * @param[in] a_seed 32-byte seed
 * @param[in] a_nonce Nonce value
 * @return Returns 0 on success, negative values on error
 */
int dap_chipmunk_hash_sample_matrix_q(int32_t *a_poly, const uint8_t a_seed[32],
                                       uint16_t a_nonce, uint64_t q)
{
    if (!a_poly || !a_seed) {
        log_it(L_ERROR, "NULL input parameters in dap_chipmunk_hash_sample_matrix");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    /*
     * CR-D11 remediation: sample matrix A coefficients uniformly in
     * [0, q-1] using a streaming SHAKE128 XOF with rejection sampling.
     *
     * The original routine read exactly 3*N bytes from the fake SHAKE128
     * and reduced them with `% CHIPMUNK_Q` — biased because 2^23 is not a
     * multiple of q = 8380417. We now use real Keccak-SHAKE128, pull
     * 23-bit words and accept only values in [0, floor(2^23/q)*q) before
     * applying the modulo. Bias is eliminated by construction.
     */

    static const uint8_t k_domain[] = "CHIPMUNK/sample_matrix/v1";
    uint8_t l_in[sizeof(k_domain) + 32 + 2];
    memcpy(l_in, k_domain, sizeof(k_domain));
    memcpy(l_in + sizeof(k_domain), a_seed, 32);
    l_in[sizeof(k_domain) + 32 + 0] = (uint8_t)(a_nonce & 0xff);
    l_in[sizeof(k_domain) + 32 + 1] = (uint8_t)((a_nonce >> 8) & 0xff);

    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));
    dap_hash_shake128_absorb(l_state, l_in, sizeof(l_in));

    uint8_t l_sq[DAP_SHAKE128_RATE];
    size_t  l_sq_pos = DAP_SHAKE128_RATE;

    const uint32_t l_q32 = (uint32_t)q;
    const uint32_t l_mul = (0x800000u / l_q32) * l_q32;
    const size_t   k_max_blocks = 1u << 20;
    size_t         l_blocks = 0;

    for (int i = 0; i < CHIPMUNK_N; i++) {
        uint32_t l_val;
        for (;;) {
            if (l_sq_pos + 3 > DAP_SHAKE128_RATE) {
                if (l_blocks++ >= k_max_blocks) {
                    log_it(L_ERROR, "dap_chipmunk_hash_sample_matrix: SHAKE128 squeeze budget exhausted");
                    memset(a_poly, 0, CHIPMUNK_N * sizeof(int32_t));
                    return CHIPMUNK_ERROR_INTERNAL;
                }
                dap_hash_shake128_squeezeblocks(l_sq, 1, l_state);
                l_sq_pos = 0;
            }
            l_val = (uint32_t)l_sq[l_sq_pos]
                  | ((uint32_t)l_sq[l_sq_pos + 1] << 8)
                  | ((uint32_t)l_sq[l_sq_pos + 2] << 16);
            l_val &= 0x7FFFFFu;
            l_sq_pos += 3;
            if (l_val < l_mul) {
                break;
            }
        }
        a_poly[i] = (int32_t)(l_val % l_q32);
    }

    return CHIPMUNK_ERROR_SUCCESS;
}

int dap_chipmunk_hash_sample_matrix(int32_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce)
{
    return dap_chipmunk_hash_sample_matrix_q(a_poly, a_seed, a_nonce, (uint64_t)CHIPMUNK_Q);
}

static inline void s_le32_store(uint8_t *a_dst, uint32_t a_v)
{
    a_dst[0] = (uint8_t)(a_v);
    a_dst[1] = (uint8_t)(a_v >> 8);
    a_dst[2] = (uint8_t)(a_v >> 16);
    a_dst[3] = (uint8_t)(a_v >> 24);
}

int dap_chipmunk_domain_hash(const char *a_domain,
                             const void *a_salt, size_t a_salt_size,
                             const void *a_input, size_t a_input_size,
                             void *a_output, size_t a_output_size,
                             uint32_t a_iterations)
{
    if (!a_domain || !a_input || !a_output || a_input_size == 0 || a_output_size == 0) {
        return -1;
    }

    enum { S_DOMAIN_MAX = 1024 };
    size_t l_domain_len = strnlen(a_domain, S_DOMAIN_MAX);
    if (l_domain_len == S_DOMAIN_MAX || l_domain_len == 0) {
        return -EINVAL;
    }
    static const char l_required_suffix[] = "/v2";
    const size_t l_required_suffix_len = sizeof(l_required_suffix) - 1u;
    if (l_domain_len < l_required_suffix_len ||
        memcmp(a_domain + l_domain_len - l_required_suffix_len,
               l_required_suffix, l_required_suffix_len) != 0) {
        log_it(L_ERROR, "Chipmunk domain hash rejected non-v2 domain: %s", a_domain);
        return -EINVAL;
    }

    if (a_salt_size > UINT32_MAX || a_input_size > UINT32_MAX ||
        l_domain_len > UINT32_MAX) {
        return -EINVAL;
    }

    const size_t l_prefix_bytes = 4u * 3u;
    if (a_salt_size > SIZE_MAX - a_input_size ||
        l_domain_len > SIZE_MAX - (a_salt_size + a_input_size) ||
        l_prefix_bytes > SIZE_MAX - (l_domain_len + a_salt_size + a_input_size)) {
        return -EINVAL;
    }
    size_t l_prk_input_size = l_prefix_bytes + l_domain_len + a_salt_size + a_input_size;
    uint8_t *l_prk_input = DAP_NEW_SIZE(uint8_t, l_prk_input_size);
    if (!l_prk_input) {
        return -ENOMEM;
    }

    size_t l_off = 0;
    s_le32_store(l_prk_input + l_off, (uint32_t)l_domain_len);
    l_off += 4;
    memcpy(l_prk_input + l_off, a_domain, l_domain_len);
    l_off += l_domain_len;

    s_le32_store(l_prk_input + l_off, (uint32_t)a_salt_size);
    l_off += 4;
    if (a_salt && a_salt_size > 0) {
        memcpy(l_prk_input + l_off, a_salt, a_salt_size);
        l_off += a_salt_size;
    }

    s_le32_store(l_prk_input + l_off, (uint32_t)a_input_size);
    l_off += 4;
    memcpy(l_prk_input + l_off, a_input, a_input_size);

    uint8_t l_prk[32];
    if (!dap_hash(DAP_HASH_TYPE_SHA3_256, l_prk_input, l_prk_input_size,
                  l_prk, sizeof(l_prk))) {
        DAP_DELETE(l_prk_input);
        return -1;
    }
    DAP_DELETE(l_prk_input);

    uint32_t l_iters = a_iterations > 0 ? a_iterations : 1u;
    for (uint32_t i = 1; i < l_iters; i++) {
        dap_hash(DAP_HASH_TYPE_SHA3_256, l_prk, sizeof(l_prk), l_prk, sizeof(l_prk));
    }

    uint8_t *l_out = (uint8_t *)a_output;
    size_t l_remaining = a_output_size;
    uint8_t l_counter = 1u;
    uint8_t l_prev_block[32] = { 0 };

    while (l_remaining > 0) {
        uint8_t l_expand_input[32 + 32 + 1];
        size_t l_expand_len = 0;
        memcpy(l_expand_input + l_expand_len, l_prk, 32); l_expand_len += 32;
        if (l_counter > 1) {
            memcpy(l_expand_input + l_expand_len, l_prev_block, 32);
            l_expand_len += 32;
        }
        l_expand_input[l_expand_len++] = l_counter;

        uint8_t l_block[32];
        if (!dap_hash(DAP_HASH_TYPE_SHA3_256, l_expand_input, l_expand_len,
                      l_block, sizeof(l_block))) {
            memset(l_prk, 0, sizeof(l_prk));
            return -1;
        }
        size_t l_to_copy = l_remaining < 32 ? l_remaining : 32u;
        memcpy(l_out, l_block, l_to_copy);
        memcpy(l_prev_block, l_block, 32);
        l_out += l_to_copy;
        l_remaining -= l_to_copy;
        if (++l_counter == 0u) {
            memset(l_prk, 0, sizeof(l_prk));
            return -1;
        }
    }

    memset(l_prk, 0, sizeof(l_prk));
    return 0;
}