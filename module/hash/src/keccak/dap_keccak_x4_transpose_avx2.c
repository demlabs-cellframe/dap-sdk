/**
 * @file dap_keccak_x4_transpose_avx2.c
 * @brief AVX2 4×4 uint64 transpose for x4 Keccak state extract/xor operations.
 *
 * The x4 Keccak state uses interleaved layout: lanes[lane_idx * 4 + inst].
 * Extract = deinterleave (state → 4 output buffers).
 * XOR = interleave + XOR (4 input buffers → state).
 *
 * The 4×4 transpose uses vpunpcklqdq/vpunpckhqdq + vperm2i128.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <immintrin.h>
#include "dap_hash_keccak_x4.h"

void dap_keccak_x4_xor_bytes_all_avx2(dap_keccak_x4_state_t *a_state,
                                       const uint8_t *a_in0,
                                       const uint8_t *a_in1,
                                       const uint8_t *a_in2,
                                       const uint8_t *a_in3,
                                       size_t a_len)
{
    const size_t l_full = a_len / 8;
    const size_t l_tail = a_len & 7;
    const uint64_t *w0 = (const uint64_t *)a_in0;
    const uint64_t *w1 = (const uint64_t *)a_in1;
    const uint64_t *w2 = (const uint64_t *)a_in2;
    const uint64_t *w3 = (const uint64_t *)a_in3;

    size_t i = 0;
    for (; i + 4 <= l_full; i += 4) {
        size_t base = i * 4;
        __m256i d0 = _mm256_loadu_si256((const __m256i *)&w0[i]);
        __m256i d1 = _mm256_loadu_si256((const __m256i *)&w1[i]);
        __m256i d2 = _mm256_loadu_si256((const __m256i *)&w2[i]);
        __m256i d3 = _mm256_loadu_si256((const __m256i *)&w3[i]);
        __m256i t0 = _mm256_unpacklo_epi64(d0, d1);
        __m256i t1 = _mm256_unpackhi_epi64(d0, d1);
        __m256i t2 = _mm256_unpacklo_epi64(d2, d3);
        __m256i t3 = _mm256_unpackhi_epi64(d2, d3);
        __m256i r0 = _mm256_permute2x128_si256(t0, t2, 0x20);
        __m256i r1 = _mm256_permute2x128_si256(t1, t3, 0x20);
        __m256i r2 = _mm256_permute2x128_si256(t0, t2, 0x31);
        __m256i r3 = _mm256_permute2x128_si256(t1, t3, 0x31);
        __m256i s0 = _mm256_loadu_si256((const __m256i *)&a_state->lanes[base]);
        __m256i s1 = _mm256_loadu_si256((const __m256i *)&a_state->lanes[base + 4]);
        __m256i s2 = _mm256_loadu_si256((const __m256i *)&a_state->lanes[base + 8]);
        __m256i s3 = _mm256_loadu_si256((const __m256i *)&a_state->lanes[base + 12]);
        _mm256_storeu_si256((__m256i *)&a_state->lanes[base],      _mm256_xor_si256(s0, r0));
        _mm256_storeu_si256((__m256i *)&a_state->lanes[base + 4],  _mm256_xor_si256(s1, r1));
        _mm256_storeu_si256((__m256i *)&a_state->lanes[base + 8],  _mm256_xor_si256(s2, r2));
        _mm256_storeu_si256((__m256i *)&a_state->lanes[base + 12], _mm256_xor_si256(s3, r3));
    }

    for (; i < l_full; i++) {
        size_t base = i * 4;
        a_state->lanes[base + 0] ^= w0[i];
        a_state->lanes[base + 1] ^= w1[i];
        a_state->lanes[base + 2] ^= w2[i];
        a_state->lanes[base + 3] ^= w3[i];
    }

    if (l_tail) {
        uint64_t tt0 = 0, tt1 = 0, tt2 = 0, tt3 = 0;
        size_t off = l_full * 8;
        memcpy(&tt0, a_in0 + off, l_tail);
        memcpy(&tt1, a_in1 + off, l_tail);
        memcpy(&tt2, a_in2 + off, l_tail);
        memcpy(&tt3, a_in3 + off, l_tail);
        size_t base = l_full * 4;
        a_state->lanes[base + 0] ^= tt0;
        a_state->lanes[base + 1] ^= tt1;
        a_state->lanes[base + 2] ^= tt2;
        a_state->lanes[base + 3] ^= tt3;
    }
}

void dap_keccak_x4_extract_bytes_all_avx2(const dap_keccak_x4_state_t *a_state,
                                            uint8_t *a_out0,
                                            uint8_t *a_out1,
                                            uint8_t *a_out2,
                                            uint8_t *a_out3,
                                            size_t a_len)
{
    const size_t l_full = a_len / 8;
    const size_t l_tail = a_len & 7;
    uint64_t *w0 = (uint64_t *)a_out0;
    uint64_t *w1 = (uint64_t *)a_out1;
    uint64_t *w2 = (uint64_t *)a_out2;
    uint64_t *w3 = (uint64_t *)a_out3;

    size_t i = 0;
    for (; i + 4 <= l_full; i += 4) {
        size_t base = i * 4;
        __m256i v0 = _mm256_loadu_si256((const __m256i *)&a_state->lanes[base]);
        __m256i v1 = _mm256_loadu_si256((const __m256i *)&a_state->lanes[base + 4]);
        __m256i v2 = _mm256_loadu_si256((const __m256i *)&a_state->lanes[base + 8]);
        __m256i v3 = _mm256_loadu_si256((const __m256i *)&a_state->lanes[base + 12]);
        __m256i t0 = _mm256_unpacklo_epi64(v0, v1);
        __m256i t1 = _mm256_unpackhi_epi64(v0, v1);
        __m256i t2 = _mm256_unpacklo_epi64(v2, v3);
        __m256i t3 = _mm256_unpackhi_epi64(v2, v3);
        _mm256_storeu_si256((__m256i *)&w0[i], _mm256_permute2x128_si256(t0, t2, 0x20));
        _mm256_storeu_si256((__m256i *)&w1[i], _mm256_permute2x128_si256(t1, t3, 0x20));
        _mm256_storeu_si256((__m256i *)&w2[i], _mm256_permute2x128_si256(t0, t2, 0x31));
        _mm256_storeu_si256((__m256i *)&w3[i], _mm256_permute2x128_si256(t1, t3, 0x31));
    }

    for (; i < l_full; i++) {
        size_t base = i * 4;
        w0[i] = a_state->lanes[base + 0];
        w1[i] = a_state->lanes[base + 1];
        w2[i] = a_state->lanes[base + 2];
        w3[i] = a_state->lanes[base + 3];
    }

    if (l_tail) {
        size_t base = l_full * 4;
        size_t off  = l_full * 8;
        uint64_t tt0 = a_state->lanes[base + 0];
        uint64_t tt1 = a_state->lanes[base + 1];
        uint64_t tt2 = a_state->lanes[base + 2];
        uint64_t tt3 = a_state->lanes[base + 3];
        memcpy(a_out0 + off, &tt0, l_tail);
        memcpy(a_out1 + off, &tt1, l_tail);
        memcpy(a_out2 + off, &tt2, l_tail);
        memcpy(a_out3 + off, &tt3, l_tail);
    }
}
