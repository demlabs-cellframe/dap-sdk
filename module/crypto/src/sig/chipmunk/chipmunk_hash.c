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
#include "dap_hash.h"
#include "dap_crypto_common.h"
#include "chipmunk.h"
// SHA2-256 provided by native dap_hash module
#include "dap_hash_sha3.h"
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"
#include <string.h>

#define LOG_TAG "chipmunk_hash"

/**
 * @brief Compute SHA2-256 hash using DAP wrapper
 * @param[out] a_output Output buffer (32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_inlen Input length
 * @return Returns 0 on success, negative error code on failure
 */
static int dap_chipmunk_hash_sha2_256(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen) {
    if (!a_output || !a_input) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    dap_hash_sha2_256(a_output, a_input, a_inlen);
    return CHIPMUNK_ERROR_SUCCESS;
}

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
 * @brief SHA3-512 wrapper function implementation
 */
int dap_chipmunk_hash_sha3_512(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen) {
    if (!a_output || !a_input) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Perform SHA3-512 hash
    dap_hash_sha3_512(a_output, a_input, a_inlen);
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
 * @brief Generate seed for polynomials from message
 */
int dap_chipmunk_hash_to_seed(uint8_t a_output[32], const uint8_t *a_message, size_t a_msglen) 
{
    if (!a_output || !a_message) {
        log_it(L_ERROR, "NULL input parameters in dap_chipmunk_hash_to_seed");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    // ИСПРАВЛЕНО: Используем SHA2-256 вместо SHA3-256
    return dap_chipmunk_hash_sha2_256(a_output, a_message, a_msglen);
}

/**
 * @brief Generate hash for challenge function
 */
int dap_chipmunk_hash_challenge(uint8_t a_output[32], const uint8_t *a_input, size_t a_inlen) {
    if (!a_output || !a_input) {
        log_it(L_ERROR, "NULL input parameters in dap_chipmunk_hash_challenge");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    // ИСПРАВЛЕНО: Используем SHA2-256 вместо SHA3-256
    return dap_chipmunk_hash_sha2_256(a_output, a_input, a_inlen);
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
 * @brief Generate point from hash
 */
int dap_chipmunk_hash_to_point(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen) 
{
    if (!a_output || !a_input) {
        log_it(L_ERROR, "NULL input parameters in dap_chipmunk_hash_to_point");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    return dap_chipmunk_hash_sha3_256(a_output, a_input, a_inlen);
}

/**
 * @brief Generate random polynomial for matrix A based on seed and nonce
 * 
 * @param[out] a_poly Output polynomial coefficients
 * @param[in] a_seed 32-byte seed
 * @param[in] a_nonce Nonce value
 * @return Returns 0 on success, negative values on error
 */
int dap_chipmunk_hash_sample_matrix(int32_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce)
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

    const uint32_t l_mul = (0x800000u / (uint32_t)CHIPMUNK_Q) * (uint32_t)CHIPMUNK_Q;
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
        a_poly[i] = (int32_t)(l_val % (uint32_t)CHIPMUNK_Q);
    }

    return CHIPMUNK_ERROR_SUCCESS;
}