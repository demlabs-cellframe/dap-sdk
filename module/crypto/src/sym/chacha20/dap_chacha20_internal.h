/**
 * @file dap_chacha20_internal.h
 * @brief Internal declarations for ChaCha20 SIMD backends.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
void dap_chacha20_encrypt_sse2(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
void dap_chacha20_encrypt_avx2(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
#elif defined(__aarch64__) || defined(__arm__)
void dap_chacha20_encrypt_neon(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
#endif

#ifdef __cplusplus
}
#endif
