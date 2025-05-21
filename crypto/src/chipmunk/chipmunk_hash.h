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

/**
 * @brief Initialize hash functions for Chipmunk
 * @return Returns 0 on success, negative error code on failure
 */
int dap_chipmunk_hash_init(void);

/**
 * @brief Compute SHA3-256 hash
 * @param[out] a_output Output buffer (32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_inlen Input length
 * @return Returns 0 on success, negative error code on failure
 */
int dap_chipmunk_hash_sha3_256(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen);

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
 * @brief Generate seed for polynomials from message
 * @param[out] a_output Output buffer for seed (must be 32 bytes)
 * @param[in] a_message Input message
 * @param[in] a_msglen Length of input message
 * @return Returns 0 on success, negative error code on failure
 */
int dap_chipmunk_hash_to_seed(uint8_t a_output[32], const uint8_t *a_message, size_t a_msglen);

/**
 * @brief Generate point from hash
 * @param[out] a_output Output buffer (must be 32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_inlen Length of input data
 * @return Returns 0 on success, negative error code on failure
 */
int dap_chipmunk_hash_to_point(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen);

/**
 * @brief Generate hash for challenge function
 * @param[out] a_output Output buffer for hash (must be 32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_inlen Length of input data
 * @return Returns 0 on success, negative error code on failure
 */
int dap_chipmunk_hash_challenge(uint8_t a_output[32], const uint8_t *a_input, size_t a_inlen);

/**
 * @brief Generate random polynomial based on seed and nonce
 * 
 * @param a_poly Output polynomial coefficients array
 * @param a_seed Input seed (must be 32 bytes)
 * @param a_nonce Nonce value
 * @return Returns 0 on success, -1 for NULL pointers, -2 for overflow, -3 for memory allocation failure
 */
int dap_chipmunk_hash_sample_poly(int32_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce);

#endif // _CHIPMUNK_HASH_H_ 