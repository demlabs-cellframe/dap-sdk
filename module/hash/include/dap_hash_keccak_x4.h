/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
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

/**
 * @file dap_hash_keccak_x4.h
 * @brief 4-way parallel Keccak-p[1600] permutation API
 * @details Processes 4 independent Keccak states simultaneously using SIMD.
 *          AVX2: true 4-way (256-bit = 4×64-bit lanes).
 *          NEON: 2-way internally, x4 = 2×x2 interleaved.
 *          Reference: 4× sequential (for correctness testing).
 *
 *          State layout: interleaved — lanes[i*4+j] = instance j, lane i.
 *          This allows AVX2 to load all 4 instances of lane i in one vmovdqu.
 *
 *          Primary use case: parallel SHAKE for PQ crypto (expand_mat, sampling).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "dap_common.h"
#include "dap_hash_keccak.h"
#include "dap_cpu_arch.h"
#include "dap_cpu_detect.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Constants
// ============================================================================

#define DAP_KECCAK_X4_INSTANCES   4
#define DAP_KECCAK_X4_LANES       (DAP_KECCAK_STATE_SIZE * DAP_KECCAK_X4_INSTANCES) // 100

// ============================================================================
// Types
// ============================================================================

/**
 * @brief 4-way interleaved Keccak state.
 *
 * Layout: lanes[lane_index * 4 + instance_index]
 *   lane_index:     0..24 (25 Keccak lanes)
 *   instance_index: 0..3  (4 parallel states)
 *
 * Aligned to 32 bytes for AVX2 loads/stores.
 */
typedef struct dap_keccak_x4_state {
    DAP_ALIGNED(32) uint64_t lanes[DAP_KECCAK_X4_LANES];
} dap_keccak_x4_state_t;

// ============================================================================
// State operations
// ============================================================================

/**
 * @brief Zero-initialize all 4 states
 */
static inline void dap_keccak_x4_init(dap_keccak_x4_state_t *a_state)
{
    memset(a_state->lanes, 0, sizeof(a_state->lanes));
}

/**
 * @brief XOR data into a single instance of the x4 state (for absorb)
 * @param a_state  x4 state
 * @param a_inst   Instance index (0..3)
 * @param a_data   Data to XOR
 * @param a_len    Length in bytes (must be <= 200)
 */
static inline void dap_keccak_x4_xor_bytes(dap_keccak_x4_state_t *a_state,
                                            unsigned a_inst,
                                            const uint8_t *a_data, size_t a_len)
{
    const size_t l_full_lanes = a_len / 8;
    const size_t l_tail       = a_len & 7;
    const uint64_t *l_words = (const uint64_t *)a_data;

    for (size_t i = 0; i < l_full_lanes; i++)
        a_state->lanes[i * 4 + a_inst] ^= l_words[i];

    if (l_tail) {
        uint64_t l_tmp = 0;
        memcpy(&l_tmp, a_data + l_full_lanes * 8, l_tail);
        a_state->lanes[l_full_lanes * 4 + a_inst] ^= l_tmp;
    }
}

/**
 * @brief Extract bytes from a single instance of the x4 state (for squeeze)
 */
static inline void dap_keccak_x4_extract_bytes(const dap_keccak_x4_state_t *a_state,
                                                unsigned a_inst,
                                                uint8_t *a_out, size_t a_len)
{
    const size_t l_full_lanes = a_len / 8;
    const size_t l_tail       = a_len & 7;
    uint64_t *l_out_words = (uint64_t *)a_out;

    for (size_t i = 0; i < l_full_lanes; i++)
        l_out_words[i] = a_state->lanes[i * 4 + a_inst];

    if (l_tail) {
        uint64_t l_tmp = a_state->lanes[l_full_lanes * 4 + a_inst];
        memcpy(a_out + l_full_lanes * 8, &l_tmp, l_tail);
    }
}

// ============================================================================
// AVX2 SIMD: 4×4 uint64 transpose for interleave/deinterleave
// ============================================================================

#if DAP_CPU_DETECT_X86
void dap_keccak_x4_xor_bytes_all_avx2(dap_keccak_x4_state_t *a_state,
                                       const uint8_t *a_in0, const uint8_t *a_in1,
                                       const uint8_t *a_in2, const uint8_t *a_in3,
                                       size_t a_len);
void dap_keccak_x4_extract_bytes_all_avx2(const dap_keccak_x4_state_t *a_state,
                                           uint8_t *a_out0, uint8_t *a_out1,
                                           uint8_t *a_out2, uint8_t *a_out3,
                                           size_t a_len);
#endif

#if DAP_CPU_DETECT_ARM && defined(__aarch64__)
#include <arm_neon.h>
static inline void dap_keccak_x4_xor_bytes_all_neon(dap_keccak_x4_state_t *a_state,
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
static inline void dap_keccak_x4_extract_bytes_all_neon(const dap_keccak_x4_state_t *a_state,
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
#endif

/**
 * @brief XOR data into all 4 instances at once (same-length inputs)
 */
static inline void dap_keccak_x4_xor_bytes_all(dap_keccak_x4_state_t *a_state,
                                                const uint8_t *a_in0,
                                                const uint8_t *a_in1,
                                                const uint8_t *a_in2,
                                                const uint8_t *a_in3,
                                                size_t a_len)
{
#if DAP_CPU_DETECT_X86
    static int s_avx2 = -1;
    if (__builtin_expect(s_avx2 < 0, 0))
        s_avx2 = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2);
    if (__builtin_expect(s_avx2, 1)) {
        dap_keccak_x4_xor_bytes_all_avx2(a_state, a_in0, a_in1, a_in2, a_in3, a_len);
        return;
    }
#elif DAP_CPU_DETECT_ARM && defined(__aarch64__)
    dap_keccak_x4_xor_bytes_all_neon(a_state, a_in0, a_in1, a_in2, a_in3, a_len);
    return;
#endif
    const size_t l_full_lanes = a_len / 8;
    const size_t l_tail       = a_len & 7;
    const uint64_t *l_w0 = (const uint64_t *)a_in0;
    const uint64_t *l_w1 = (const uint64_t *)a_in1;
    const uint64_t *l_w2 = (const uint64_t *)a_in2;
    const uint64_t *l_w3 = (const uint64_t *)a_in3;

    for (size_t i = 0; i < l_full_lanes; i++) {
        size_t l_base = i * 4;
        a_state->lanes[l_base + 0] ^= l_w0[i];
        a_state->lanes[l_base + 1] ^= l_w1[i];
        a_state->lanes[l_base + 2] ^= l_w2[i];
        a_state->lanes[l_base + 3] ^= l_w3[i];
    }

    if (l_tail) {
        uint64_t t0 = 0, t1 = 0, t2 = 0, t3 = 0;
        size_t l_off = l_full_lanes * 8;
        memcpy(&t0, a_in0 + l_off, l_tail);
        memcpy(&t1, a_in1 + l_off, l_tail);
        memcpy(&t2, a_in2 + l_off, l_tail);
        memcpy(&t3, a_in3 + l_off, l_tail);
        size_t l_base = l_full_lanes * 4;
        a_state->lanes[l_base + 0] ^= t0;
        a_state->lanes[l_base + 1] ^= t1;
        a_state->lanes[l_base + 2] ^= t2;
        a_state->lanes[l_base + 3] ^= t3;
    }
}

/**
 * @brief Extract bytes from all 4 instances at once (same-length outputs)
 */
static inline void dap_keccak_x4_extract_bytes_all(const dap_keccak_x4_state_t *a_state,
                                                    uint8_t *a_out0,
                                                    uint8_t *a_out1,
                                                    uint8_t *a_out2,
                                                    uint8_t *a_out3,
                                                    size_t a_len)
{
#if DAP_CPU_DETECT_X86
    static int s_avx2 = -1;
    if (__builtin_expect(s_avx2 < 0, 0))
        s_avx2 = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2);
    if (__builtin_expect(s_avx2, 1)) {
        dap_keccak_x4_extract_bytes_all_avx2(a_state, a_out0, a_out1, a_out2, a_out3, a_len);
        return;
    }
#elif DAP_CPU_DETECT_ARM && defined(__aarch64__)
    dap_keccak_x4_extract_bytes_all_neon(a_state, a_out0, a_out1, a_out2, a_out3, a_len);
    return;
#endif
    const size_t l_full_lanes = a_len / 8;
    const size_t l_tail       = a_len & 7;
    uint64_t *l_w0 = (uint64_t *)a_out0;
    uint64_t *l_w1 = (uint64_t *)a_out1;
    uint64_t *l_w2 = (uint64_t *)a_out2;
    uint64_t *l_w3 = (uint64_t *)a_out3;

    for (size_t i = 0; i < l_full_lanes; i++) {
        size_t l_base = i * 4;
        l_w0[i] = a_state->lanes[l_base + 0];
        l_w1[i] = a_state->lanes[l_base + 1];
        l_w2[i] = a_state->lanes[l_base + 2];
        l_w3[i] = a_state->lanes[l_base + 3];
    }

    if (l_tail) {
        size_t l_base = l_full_lanes * 4;
        size_t l_off  = l_full_lanes * 8;
        uint64_t t0 = a_state->lanes[l_base + 0];
        uint64_t t1 = a_state->lanes[l_base + 1];
        uint64_t t2 = a_state->lanes[l_base + 2];
        uint64_t t3 = a_state->lanes[l_base + 3];
        memcpy(a_out0 + l_off, &t0, l_tail);
        memcpy(a_out1 + l_off, &t1, l_tail);
        memcpy(a_out2 + l_off, &t2, l_tail);
        memcpy(a_out3 + l_off, &t3, l_tail);
    }
}

// ============================================================================
// Deinterleave / interleave helpers (shared by _ref, _opt)
// ============================================================================

static inline void dap_keccak_x4_deinterleave(dap_hash_keccak_state_t a_out[4],
                                                const dap_keccak_x4_state_t *a_x4)
{
    for (unsigned i = 0; i < DAP_KECCAK_STATE_SIZE; i++) {
        a_out[0].lanes[i] = a_x4->lanes[i * 4 + 0];
        a_out[1].lanes[i] = a_x4->lanes[i * 4 + 1];
        a_out[2].lanes[i] = a_x4->lanes[i * 4 + 2];
        a_out[3].lanes[i] = a_x4->lanes[i * 4 + 3];
    }
}

static inline void dap_keccak_x4_interleave(dap_keccak_x4_state_t *a_x4,
                                              const dap_hash_keccak_state_t a_in[4])
{
    for (unsigned i = 0; i < DAP_KECCAK_STATE_SIZE; i++) {
        a_x4->lanes[i * 4 + 0] = a_in[0].lanes[i];
        a_x4->lanes[i * 4 + 1] = a_in[1].lanes[i];
        a_x4->lanes[i * 4 + 2] = a_in[2].lanes[i];
        a_x4->lanes[i * 4 + 3] = a_in[3].lanes[i];
    }
}

// ============================================================================
// Core x4 permutation — implementations
// ============================================================================

/** Pure-C reference (4× permute_ref). Only for correctness testing. */
static inline void dap_keccak_x4_permute_ref(dap_keccak_x4_state_t *a_state)
{
    dap_hash_keccak_state_t l_s[4];
    dap_keccak_x4_deinterleave(l_s, a_state);

    dap_hash_keccak_permute_ref(&l_s[0]);
    dap_hash_keccak_permute_ref(&l_s[1]);
    dap_hash_keccak_permute_ref(&l_s[2]);
    dap_hash_keccak_permute_ref(&l_s[3]);

    dap_keccak_x4_interleave(a_state, l_s);
}

/**
 * Optimized scalar fallback: deinterleave → 4× single-state dispatch
 * (best available 1x SIMD) → interleave.
 * Default when no native x4 SIMD backend exists for the current arch.
 */
static inline void dap_keccak_x4_permute_opt(dap_keccak_x4_state_t *a_state)
{
    dap_hash_keccak_state_t l_s[4];
    dap_keccak_x4_deinterleave(l_s, a_state);

    dap_hash_keccak_permute(&l_s[0]);
    dap_hash_keccak_permute(&l_s[1]);
    dap_hash_keccak_permute(&l_s[2]);
    dap_hash_keccak_permute(&l_s[3]);

    dap_keccak_x4_interleave(a_state, l_s);
}

/** Native SIMD x4 — generated from dap_keccak_x4.c.tpl */
#if DAP_CPU_DETECT_X86
void dap_keccak_x4_permute_avx512(dap_keccak_x4_state_t *a_state);
void dap_keccak_x4_permute_avx512vl_asm(dap_keccak_x4_state_t *a_state);
void dap_keccak_x4_permute_avx2(dap_keccak_x4_state_t *a_state);
#endif

#if DAP_CPU_DETECT_ARM
void dap_keccak_x4_permute_neon(dap_keccak_x4_state_t *a_state);
#endif

// ============================================================================
// Dispatch — selects best available x4 permutation at runtime
//
// Priority:
//   x86: AVX2 native x4 → _opt (SSE2/AVX2 single-state fallback)
//   ARM: NEON native x4  → _opt (NEON single-state fallback)
//   Other:                → _opt (reference single-state)
// ============================================================================

typedef void (*dap_keccak_x4_permute_fn_t)(dap_keccak_x4_state_t *);

static inline dap_keccak_x4_permute_fn_t dap_keccak_x4_resolve_permute(void)
{
    dap_cpu_arch_t l_arch = dap_cpu_arch_get();
    (void)l_arch;
#if DAP_CPU_DETECT_X86
    if (l_arch >= DAP_CPU_ARCH_AVX512)
        return dap_keccak_x4_permute_avx512vl_asm;
    if (l_arch >= DAP_CPU_ARCH_AVX2)
        return dap_keccak_x4_permute_avx2;
#endif
#if DAP_CPU_DETECT_ARM
    if (l_arch >= DAP_CPU_ARCH_NEON)
        return dap_keccak_x4_permute_neon;
#endif
    return dap_keccak_x4_permute_opt;
}

static dap_keccak_x4_permute_fn_t s_keccak_x4_permute_fn = NULL;

static inline void dap_keccak_x4_permute(dap_keccak_x4_state_t *a_state)
{
    if (__builtin_expect(!s_keccak_x4_permute_fn, 0))
        s_keccak_x4_permute_fn = dap_keccak_x4_resolve_permute();
    s_keccak_x4_permute_fn(a_state);
}

#ifdef __cplusplus
}
#endif
