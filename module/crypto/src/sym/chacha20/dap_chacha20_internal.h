/**
 * @file dap_chacha20_internal.h
 * @brief Internal declarations for ChaCha20 SIMD backends.
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

#if DAP_PLATFORM_X86
void dap_chacha20_encrypt_sse2(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
void dap_chacha20_encrypt_avx2(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
void dap_chacha20_encrypt_avx2_512vl(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
void dap_chacha20_encrypt_avx512(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
#elif DAP_PLATFORM_ARM
void dap_chacha20_encrypt_neon(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[32], const uint8_t a_nonce[12], uint32_t a_counter);
#endif

#ifdef __cplusplus
}
#endif
