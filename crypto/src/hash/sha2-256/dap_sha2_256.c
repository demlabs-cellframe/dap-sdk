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

#include "dap_sha2_256.h"
#include "hash.h"
#include "hash_impl.h"

/**
 * @brief Compute SHA2-256 hash
 * @param[out] a_output Output buffer (must be 32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_inlen Input length
 * @return Returns 0 on success, negative error code on failure
 */
int dap_sha2_256(uint8_t a_output[32], const uint8_t *a_input, size_t a_inlen) {
    if (!a_output || !a_input) {
        return -1;  // NULL parameter error
    }
    
    secp256k1_sha256 l_hasher;
    secp256k1_sha256_initialize(&l_hasher);
    secp256k1_sha256_write(&l_hasher, a_input, a_inlen);
    secp256k1_sha256_finalize(&l_hasher, a_output);
    
    return 0;  // Success
} 