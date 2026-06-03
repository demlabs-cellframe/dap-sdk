/**
 * @file dap_keccak_x4_transpose_neon.c
 * @brief AArch64 NEON XOR/extract for x4 Keccak state.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#if defined(__aarch64__)

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <arm_neon.h>
#include "dap_hash_keccak_x4.h"

void dap_keccak_x4_xor_bytes_all_neon(dap_keccak_x4_state_t *a_state,
    const uint8_t *a_in0, const uint8_t *a_in1,
    const uint8_t *a_in2, const uint8_t *a_in3, size_t a_len)
{
    const size_t l_full = a_len / 8;
    const uint64_t *w0 = (const uint64_t *)a_in0, *w1 = (const uint64_t *)a_in1;
    const uint64_t *w2 = (const uint64_t *)a_in2, *w3 = (const uint64_t *)a_in3;
    for (size_t i = 0; i < l_full; i++) {
        uint64x2_t s01 = vld1q_u64(&a_state->lanes[i * 4]);
        uint64x2_t s23 = vld1q_u64(&a_state->lanes[i * 4 + 2]);
        uint64x2_t d01 = vcombine_u64(vld1_u64(&w0[i]), vld1_u64(&w1[i]));
        uint64x2_t d23 = vcombine_u64(vld1_u64(&w2[i]), vld1_u64(&w3[i]));
        vst1q_u64(&a_state->lanes[i * 4],     veorq_u64(s01, d01));
        vst1q_u64(&a_state->lanes[i * 4 + 2], veorq_u64(s23, d23));
    }
    size_t l_tail = a_len & 7;
    if (l_tail) {
        uint64_t t0 = 0, t1 = 0, t2 = 0, t3 = 0;
        size_t off = l_full * 8;
        memcpy(&t0, a_in0 + off, l_tail); memcpy(&t1, a_in1 + off, l_tail);
        memcpy(&t2, a_in2 + off, l_tail); memcpy(&t3, a_in3 + off, l_tail);
        size_t base = l_full * 4;
        a_state->lanes[base] ^= t0; a_state->lanes[base+1] ^= t1;
        a_state->lanes[base+2] ^= t2; a_state->lanes[base+3] ^= t3;
    }
}

void dap_keccak_x4_extract_bytes_all_neon(const dap_keccak_x4_state_t *a_state,
    uint8_t *a_out0, uint8_t *a_out1,
    uint8_t *a_out2, uint8_t *a_out3, size_t a_len)
{
    const size_t l_full = a_len / 8;
    uint64_t *w0 = (uint64_t *)a_out0, *w1 = (uint64_t *)a_out1;
    uint64_t *w2 = (uint64_t *)a_out2, *w3 = (uint64_t *)a_out3;
    for (size_t i = 0; i < l_full; i++) {
        uint64x2_t s01 = vld1q_u64(&a_state->lanes[i * 4]);
        uint64x2_t s23 = vld1q_u64(&a_state->lanes[i * 4 + 2]);
        vst1_u64(&w0[i], vget_low_u64(s01));
        vst1_u64(&w1[i], vget_high_u64(s01));
        vst1_u64(&w2[i], vget_low_u64(s23));
        vst1_u64(&w3[i], vget_high_u64(s23));
    }
    size_t l_tail = a_len & 7;
    if (l_tail) {
        size_t base = l_full * 4, off = l_full * 8;
        uint64_t t0 = a_state->lanes[base], t1 = a_state->lanes[base+1];
        uint64_t t2 = a_state->lanes[base+2], t3 = a_state->lanes[base+3];
        memcpy(a_out0 + off, &t0, l_tail); memcpy(a_out1 + off, &t1, l_tail);
        memcpy(a_out2 + off, &t2, l_tail); memcpy(a_out3 + off, &t3, l_tail);
    }
}

#endif /* __aarch64__ */
