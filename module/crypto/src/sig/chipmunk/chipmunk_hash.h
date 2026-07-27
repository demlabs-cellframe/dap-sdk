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

#ifndef _CHIPMUNK_HASH_H_
#define _CHIPMUNK_HASH_H_

#include <stdint.h>
#include <stddef.h>

/* ---------------------------------------------------------------------- *
 *  CR-D11 — canonical hash surface for the Chipmunk module                *
 *                                                                         *
 *  The active code path uses ONE family only: NIST SHA-3 / Keccak.        *
 *      - SHA3-256 / SHA3-384  → fixed-output digests for tr / ring_hash   *
 *      - SHAKE128              → XOF for sample_poly / sample_matrix and  *
 *                                 (via dap_hash_shake128) all bulk        *
 *                                 polynomial expansion                    *
 *      - SHAKE256              → used directly through dap_hash_shake256  *
 *                                 from chipmunk_poly.c for uniform        *
 *                                 rejection sampling                      *
 *                                                                         *
 *  No SHA2-based primitive is exposed here: the Round-3 audit found       *
 *  three SHA2-flavoured helpers (`dap_chipmunk_hash_to_seed`,             *
 *  `dap_chipmunk_hash_challenge`) plus the orphan SHA3-256                *
 *  `dap_chipmunk_hash_to_point` and an unused SHA3-512 wrapper that       *
 *  together formed a "mixed-primitive" surface (CR-D11).  All of them     *
 *  were dead code at the call-site level but acted as a foot-gun for any  *
 *  future caller who would unwittingly inherit a SHA2 hash chain inside   *
 *  an otherwise SHA3-pure protocol — breaking domain separation and the   *
 *  random-oracle model the scheme is analysed under.                      *
 *                                                                         *
 *  Policy: any new chipmunk hash primitive MUST be either SHA3-* or       *
 *  SHAKE*; SHA2 calls are forbidden in this module.  Re-introducing a     *
 *  SHA2 helper requires both an explicit security justification and an    *
 *  update to the CR-D11 regression test in                                *
 *  tests/unit/crypto/chipmunk_ring/test_chipmunk_round3_regression.c.     *
 * ---------------------------------------------------------------------- */

/**
 * @brief Initialize hash functions for Chipmunk
 * @return Returns 0 on success, negative error code on failure
 */
int dap_chipmunk_hash_init(void);

/**
 * @brief Compute SHA3-256 hash
 * @param[out] a_output Output buffer (32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_input_len Input data length
 * @return 0 on success
 */
int dap_chipmunk_hash_sha3_256(uint8_t *a_output, const uint8_t *a_input, size_t a_input_len);

/**
 * @brief Compute SHA3-384 hash
 * @param[out] a_output Output buffer (48 bytes)
 * @param[in] a_input Input data
 * @param[in] a_input_len Input data length
 * @return 0 on success
 */
int dap_chipmunk_hash_sha3_384(uint8_t *a_output, const uint8_t *a_input, size_t a_input_len);

/**
 * @brief SHAKE-128 wrapper function for extendable output
 * @param[out] a_output Output buffer
 * @param[in] a_outlen Desired output length
 * @param[in] a_input Input data
 * @param[in] a_inlen Length of input data
 * @return Returns 0 on success, negative error code on failure
 */
int dap_chipmunk_hash_shake128(uint8_t *a_output, size_t a_outlen, const uint8_t *a_input, size_t a_inlen);

/**
 * @brief Generate random polynomial based on seed and nonce
 * 
 * @param a_poly Output polynomial coefficients array
 * @param a_seed Input seed (must be 32 bytes)
 * @param a_nonce Nonce value
 * @return Returns 0 on success, -1 for NULL pointers, -2 for overflow, -3 for memory allocation failure
 */
int dap_chipmunk_hash_sample_poly(int32_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce);

/**
 * @brief Generate random polynomial for matrix A based on seed and nonce (parameterized)
 *
 * @param a_poly Output polynomial coefficients array
 * @param a_seed Input seed (must be 32 bytes)
 * @param a_nonce Nonce value
 * @param q Prime modulus
 * @return Returns 0 on success, -1 for NULL pointers, -2 for overflow, -3 for memory allocation failure
 */
int dap_chipmunk_hash_sample_matrix_q(int32_t *a_poly, const uint8_t a_seed[32],
                                       uint16_t a_nonce, uint64_t q);

/**
 * @brief Generate random polynomial for matrix A based on seed and nonce
 *
 * @param a_poly Output polynomial coefficients array
 * @param a_seed Input seed (must be 32 bytes)
 * @param a_nonce Nonce value
 * @return Returns 0 on success, -1 for NULL pointers, -2 for overflow, -3 for memory allocation failure
 */
int dap_chipmunk_hash_sample_matrix(int32_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce);

/**
 * @brief TupleHash-style domain-separated SHA3-256 KDF.
 *
 * Computes:
 *   PRK = SHA3-256( LE32(len(D)) || D ||
 *                   LE32(len(S)) || S ||
 *                   LE32(len(I)) || I )
 *   (optionally iterated a_iterations times by re-hashing PRK)
 *   OKM = HKDF-Expand-style SHA3-256 counter mode of length a_output_size.
 *
 * The domain string MUST end with the "/v2" suffix and contain no
 * embedded NUL.  Lengths must fit in uint32_t.
 *
 * Used by Chipmunk hypertree PoP message derivation.
 *
 * @return 0 on success; -EINVAL on invalid arguments; -ENOMEM on alloc
 *         failure; -1 on hash failure.
 */
int dap_chipmunk_domain_hash(const char *a_domain,
                             const void *a_salt, size_t a_salt_size,
                             const void *a_input, size_t a_input_size,
                             void *a_output, size_t a_output_size,
                             uint32_t a_iterations);

#endif // _CHIPMUNK_HASH_H_ 