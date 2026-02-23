/*
 * Authors:
 * Dmitriy A. Gearasimov <kahovski@gmail.com>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://github.com/demlabsinc
 * Copyright  (c) 2017-2018
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
#include <stdio.h>
#include <stdlib.h>
#include "dap_common.h"
#include "dap_hash.h"
#include "sha2-256/dap_sha2_256.h"

#define LOG_TAG "dap_hash"

/**
 * @brief Compute hash of data using specified algorithm
 * @param a_type Hash algorithm
 * @param a_data_in Input data
 * @param a_data_in_size Size of input data
 * @param a_hash_out Output buffer (size depends on algorithm)
 * @param a_hash_out_size Size of output buffer
 * @return true on success, false on error
 */
bool dap_hash(dap_hash_type_t a_type, const void *a_data_in, size_t a_data_in_size,
              void *a_hash_out, size_t a_hash_out_size)
{
    if (!a_hash_out)
        return false;

    switch (a_type) {
        case DAP_HASH_TYPE_SHA3_256:
            if (a_hash_out_size < DAP_HASH_SHA3_256_SIZE)
                return false;
            return dap_hash_sha3_256(a_data_in, a_data_in_size, (dap_hash_sha3_256_t *)a_hash_out);

        case DAP_HASH_TYPE_SHA2_256:
            if (a_hash_out_size < 32)
                return false;
            dap_hash_sha2_256((uint8_t *)a_hash_out, (const uint8_t *)a_data_in, a_data_in_size);
            return true;

        case DAP_HASH_TYPE_KECCAK_256:
            // TODO: Implement Keccak-256
            log_it(L_ERROR, "Keccak-256 not yet implemented");
            return false;

        case DAP_HASH_TYPE_SLOW_0:
            // TODO: Implement slow hash
            log_it(L_ERROR, "Slow hash not yet implemented");
            return false;

        default:
            log_it(L_ERROR, "Unknown hash type: %d", a_type);
            return false;
    }
}

// Note: dap_hash_sha2_256() is now provided by dap_hash_sha2.h/c
