/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2021
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include "dap_math_ops.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// UUID string length: "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" + null terminator
#define DAP_UUID_STR_SIZE 37

uint128_t dap_uuid_generate_uint128(); // Produce uint128 global unique id
uint64_t dap_uuid_generate_uint64(); // Produce uint64 global unique id
// Produces unique nonce
void dap_uuid_generate_nonce(void *a_nonce, size_t a_nonce_size);

/**
 * @brief Convert UUID (16-byte binary) to hex string representation
 * @param a_uuid Pointer to 16-byte UUID
 * @param a_buf Output buffer for string (must be at least DAP_UUID_STR_SIZE bytes)
 * @param a_buf_size Size of output buffer
 * @return 0 on success, negative error code on failure
 * @note Output format: "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
 */
int dap_uuid_to_str(const void *a_uuid, char *a_buf, size_t a_buf_size);

/**
 * @brief Check if UUID is blank (all zeros)
 * @param a_uuid Pointer to 16-byte UUID
 * @param a_uuid_size Size of UUID (typically 16 bytes)
 * @return true if UUID is all zeros, false otherwise
 */
bool dap_uuid_is_blank(const void *a_uuid, size_t a_uuid_size);

#ifdef __cplusplus
}
#endif

