/*
 * OS-based cryptographically secure random number generation.
 *
 * Uses /dev/urandom on Unix, CryptGenRandom on Windows.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int dap_random_bytes(void *a_buf, unsigned int a_nbytes);
uint32_t dap_random_uint32(uint32_t a_max);
uint8_t dap_random_byte(void);
uint16_t dap_random_uint16(void);

#ifdef __cplusplus
}
#endif
