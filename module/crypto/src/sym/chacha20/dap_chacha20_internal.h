/**
 * @file dap_chacha20_internal.h
 * @brief Internal ChaCha20 helpers and SIMD backend declarations.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "dap_arch_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

void dap_chacha20_encrypt_sse2(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
void dap_chacha20_encrypt_avx2(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
void dap_chacha20_encrypt_avx2_512vl(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
void dap_chacha20_encrypt_asm(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
void dap_chacha20_encrypt_avx512(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
void dap_chacha20_encrypt_neon(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);

#ifdef __cplusplus
}
#endif
