/*
 * Random number generation: OS-secure + deterministic pseudo-random.
 *
 * OS-secure: /dev/urandom on Unix, CryptGenRandom on Windows.
 * Pseudo-random: SHISHUA PRNG with uint256 seed (for consensus).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "dap_math_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

int dap_random_bytes(void *a_buf, unsigned int a_nbytes);
uint32_t dap_random_uint32(uint32_t a_max);
uint8_t dap_random_byte(void);
uint16_t dap_random_uint16(void);

void dap_pseudo_random_seed(uint256_t a_seed);
uint256_t dap_pseudo_random_get(uint256_t a_rand_max, uint256_t *a_raw_result);

#define dap_random_mem(buf, len) dap_random_bytes((buf), (len))
#define randombytes(buf, len)    dap_random_bytes((buf), (len))

#ifdef __cplusplus
}
#endif
