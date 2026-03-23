/**
 * @file dap_mlkem_indcpa.c
 * @brief IND-CPA public-key encryption for ML-KEM (FIPS 203).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <string.h>
#include "dap_mlkem_indcpa.h"
#include "dap_mlkem_poly.h"
#include "dap_mlkem_polyvec.h"
#include "dap_mlkem_ntt.h"
#include "dap_mlkem_symmetric.h"
#include "dap_rand.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define MLKEM_REJ_AVX2 1
#endif

/* ---- pack / unpack ---- */

static void s_pack_pk(uint8_t a_r[MLKEM_INDCPA_PUBLICKEYBYTES],
                       dap_mlkem_polyvec *a_pk,
                       const uint8_t a_seed[MLKEM_SYMBYTES])
{
    MLKEM_NAMESPACE(_polyvec_tobytes)(a_r, a_pk);
    memcpy(a_r + MLKEM_POLYVECBYTES, a_seed, MLKEM_SYMBYTES);
}

static void s_unpack_pk(dap_mlkem_polyvec *a_pk,
                         uint8_t a_seed[MLKEM_SYMBYTES],
                         const uint8_t a_packed[MLKEM_INDCPA_PUBLICKEYBYTES])
{
    MLKEM_NAMESPACE(_polyvec_frombytes)(a_pk, a_packed);
    memcpy(a_seed, a_packed + MLKEM_POLYVECBYTES, MLKEM_SYMBYTES);
}

static void s_pack_sk(uint8_t a_r[MLKEM_INDCPA_SECRETKEYBYTES], dap_mlkem_polyvec *a_sk)
{
    MLKEM_NAMESPACE(_polyvec_tobytes)(a_r, a_sk);
}

static void s_unpack_sk(dap_mlkem_polyvec *a_sk,
                         const uint8_t a_packed[MLKEM_INDCPA_SECRETKEYBYTES])
{
    MLKEM_NAMESPACE(_polyvec_frombytes)(a_sk, a_packed);
}

static void s_pack_ciphertext(uint8_t a_r[MLKEM_INDCPA_BYTES],
                               dap_mlkem_polyvec *a_b, dap_mlkem_poly *a_v)
{
    MLKEM_NAMESPACE(_polyvec_compress)(a_r, a_b);
    MLKEM_NAMESPACE(_poly_compress)(a_r + MLKEM_POLYVECCOMPRESSEDBYTES, a_v);
}

static void s_unpack_ciphertext(dap_mlkem_polyvec *a_b, dap_mlkem_poly *a_v,
                                 const uint8_t a_c[MLKEM_INDCPA_BYTES])
{
    MLKEM_NAMESPACE(_polyvec_decompress)(a_b, a_c);
    MLKEM_NAMESPACE(_poly_decompress)(a_v, a_c + MLKEM_POLYVECCOMPRESSEDBYTES);
}

/* ---- rejection sampling ---- */

static unsigned s_rej_uniform_scalar(int16_t *a_r, unsigned a_len,
                                      const uint8_t *a_buf, unsigned a_buflen)
{
    unsigned l_ctr = 0, l_pos = 0;
    while (l_ctr < a_len && l_pos + 3 <= a_buflen) {
        uint16_t val0 = ((a_buf[l_pos] >> 0) | ((uint16_t)a_buf[l_pos + 1] << 8)) & 0xFFF;
        uint16_t val1 = ((a_buf[l_pos + 1] >> 4) | ((uint16_t)a_buf[l_pos + 2] << 4)) & 0xFFF;
        l_pos += 3;
        if (val0 < MLKEM_Q)
            a_r[l_ctr++] = (int16_t)val0;
        if (l_ctr < a_len && val1 < MLKEM_Q)
            a_r[l_ctr++] = (int16_t)val1;
    }
    return l_ctr;
}

#ifdef MLKEM_REJ_AVX2
/*
 * AVX2 rejection sampling: processes 48 bytes → 32 candidates per iteration.
 * Uses LUT-based compress-store to pack valid (< Q) candidates.
 * Based on pqcrystals technique with byte-shuffle index table.
 */
static const uint8_t s_rej_idx[256][8] = {
    {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
    { 2,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 2,0xff,0xff,0xff,0xff,0xff,0xff},
    { 4,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 4,0xff,0xff,0xff,0xff,0xff,0xff},
    { 2, 4,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 4,0xff,0xff,0xff,0xff,0xff},
    { 6,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 6,0xff,0xff,0xff,0xff,0xff,0xff},
    { 2, 6,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 6,0xff,0xff,0xff,0xff,0xff},
    { 4, 6,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 4, 6,0xff,0xff,0xff,0xff,0xff},
    { 2, 4, 6,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 4, 6,0xff,0xff,0xff,0xff},
    { 8,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 8,0xff,0xff,0xff,0xff,0xff,0xff},
    { 2, 8,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 8,0xff,0xff,0xff,0xff,0xff},
    { 4, 8,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 4, 8,0xff,0xff,0xff,0xff,0xff},
    { 2, 4, 8,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 4, 8,0xff,0xff,0xff,0xff},
    { 6, 8,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 6, 8,0xff,0xff,0xff,0xff,0xff},
    { 2, 6, 8,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 6, 8,0xff,0xff,0xff,0xff},
    { 4, 6, 8,0xff,0xff,0xff,0xff,0xff},
    { 0, 4, 6, 8,0xff,0xff,0xff,0xff},
    { 2, 4, 6, 8,0xff,0xff,0xff,0xff},
    { 0, 2, 4, 6, 8,0xff,0xff,0xff},
    {10,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0,10,0xff,0xff,0xff,0xff,0xff,0xff},
    { 2,10,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 2,10,0xff,0xff,0xff,0xff,0xff},
    { 4,10,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 4,10,0xff,0xff,0xff,0xff,0xff},
    { 2, 4,10,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 4,10,0xff,0xff,0xff,0xff},
    { 6,10,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 6,10,0xff,0xff,0xff,0xff,0xff},
    { 2, 6,10,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 6,10,0xff,0xff,0xff,0xff},
    { 4, 6,10,0xff,0xff,0xff,0xff,0xff},
    { 0, 4, 6,10,0xff,0xff,0xff,0xff},
    { 2, 4, 6,10,0xff,0xff,0xff,0xff},
    { 0, 2, 4, 6,10,0xff,0xff,0xff},
    { 8,10,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 8,10,0xff,0xff,0xff,0xff,0xff},
    { 2, 8,10,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 8,10,0xff,0xff,0xff,0xff},
    { 4, 8,10,0xff,0xff,0xff,0xff,0xff},
    { 0, 4, 8,10,0xff,0xff,0xff,0xff},
    { 2, 4, 8,10,0xff,0xff,0xff,0xff},
    { 0, 2, 4, 8,10,0xff,0xff,0xff},
    { 6, 8,10,0xff,0xff,0xff,0xff,0xff},
    { 0, 6, 8,10,0xff,0xff,0xff,0xff},
    { 2, 6, 8,10,0xff,0xff,0xff,0xff},
    { 0, 2, 6, 8,10,0xff,0xff,0xff},
    { 4, 6, 8,10,0xff,0xff,0xff,0xff},
    { 0, 4, 6, 8,10,0xff,0xff,0xff},
    { 2, 4, 6, 8,10,0xff,0xff,0xff},
    { 0, 2, 4, 6, 8,10,0xff,0xff},
    {12,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0,12,0xff,0xff,0xff,0xff,0xff,0xff},
    { 2,12,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 2,12,0xff,0xff,0xff,0xff,0xff},
    { 4,12,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 4,12,0xff,0xff,0xff,0xff,0xff},
    { 2, 4,12,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 4,12,0xff,0xff,0xff,0xff},
    { 6,12,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 6,12,0xff,0xff,0xff,0xff,0xff},
    { 2, 6,12,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 6,12,0xff,0xff,0xff,0xff},
    { 4, 6,12,0xff,0xff,0xff,0xff,0xff},
    { 0, 4, 6,12,0xff,0xff,0xff,0xff},
    { 2, 4, 6,12,0xff,0xff,0xff,0xff},
    { 0, 2, 4, 6,12,0xff,0xff,0xff},
    { 8,12,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 8,12,0xff,0xff,0xff,0xff,0xff},
    { 2, 8,12,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 8,12,0xff,0xff,0xff,0xff},
    { 4, 8,12,0xff,0xff,0xff,0xff,0xff},
    { 0, 4, 8,12,0xff,0xff,0xff,0xff},
    { 2, 4, 8,12,0xff,0xff,0xff,0xff},
    { 0, 2, 4, 8,12,0xff,0xff,0xff},
    { 6, 8,12,0xff,0xff,0xff,0xff,0xff},
    { 0, 6, 8,12,0xff,0xff,0xff,0xff},
    { 2, 6, 8,12,0xff,0xff,0xff,0xff},
    { 0, 2, 6, 8,12,0xff,0xff,0xff},
    { 4, 6, 8,12,0xff,0xff,0xff,0xff},
    { 0, 4, 6, 8,12,0xff,0xff,0xff},
    { 2, 4, 6, 8,12,0xff,0xff,0xff},
    { 0, 2, 4, 6, 8,12,0xff,0xff},
    {10,12,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0,10,12,0xff,0xff,0xff,0xff,0xff},
    { 2,10,12,0xff,0xff,0xff,0xff,0xff},
    { 0, 2,10,12,0xff,0xff,0xff,0xff},
    { 4,10,12,0xff,0xff,0xff,0xff,0xff},
    { 0, 4,10,12,0xff,0xff,0xff,0xff},
    { 2, 4,10,12,0xff,0xff,0xff,0xff},
    { 0, 2, 4,10,12,0xff,0xff,0xff},
    { 6,10,12,0xff,0xff,0xff,0xff,0xff},
    { 0, 6,10,12,0xff,0xff,0xff,0xff},
    { 2, 6,10,12,0xff,0xff,0xff,0xff},
    { 0, 2, 6,10,12,0xff,0xff,0xff},
    { 4, 6,10,12,0xff,0xff,0xff,0xff},
    { 0, 4, 6,10,12,0xff,0xff,0xff},
    { 2, 4, 6,10,12,0xff,0xff,0xff},
    { 0, 2, 4, 6,10,12,0xff,0xff},
    { 8,10,12,0xff,0xff,0xff,0xff,0xff},
    { 0, 8,10,12,0xff,0xff,0xff,0xff},
    { 2, 8,10,12,0xff,0xff,0xff,0xff},
    { 0, 2, 8,10,12,0xff,0xff,0xff},
    { 4, 8,10,12,0xff,0xff,0xff,0xff},
    { 0, 4, 8,10,12,0xff,0xff,0xff},
    { 2, 4, 8,10,12,0xff,0xff,0xff},
    { 0, 2, 4, 8,10,12,0xff,0xff},
    { 6, 8,10,12,0xff,0xff,0xff,0xff},
    { 0, 6, 8,10,12,0xff,0xff,0xff},
    { 2, 6, 8,10,12,0xff,0xff,0xff},
    { 0, 2, 6, 8,10,12,0xff,0xff},
    { 4, 6, 8,10,12,0xff,0xff,0xff},
    { 0, 4, 6, 8,10,12,0xff,0xff},
    { 2, 4, 6, 8,10,12,0xff,0xff},
    { 0, 2, 4, 6, 8,10,12,0xff},
    {14,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0,14,0xff,0xff,0xff,0xff,0xff,0xff},
    { 2,14,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 2,14,0xff,0xff,0xff,0xff,0xff},
    { 4,14,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 4,14,0xff,0xff,0xff,0xff,0xff},
    { 2, 4,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 4,14,0xff,0xff,0xff,0xff},
    { 6,14,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 6,14,0xff,0xff,0xff,0xff,0xff},
    { 2, 6,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 6,14,0xff,0xff,0xff,0xff},
    { 4, 6,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 4, 6,14,0xff,0xff,0xff,0xff},
    { 2, 4, 6,14,0xff,0xff,0xff,0xff},
    { 0, 2, 4, 6,14,0xff,0xff,0xff},
    { 8,14,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0, 8,14,0xff,0xff,0xff,0xff,0xff},
    { 2, 8,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 2, 8,14,0xff,0xff,0xff,0xff},
    { 4, 8,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 4, 8,14,0xff,0xff,0xff,0xff},
    { 2, 4, 8,14,0xff,0xff,0xff,0xff},
    { 0, 2, 4, 8,14,0xff,0xff,0xff},
    { 6, 8,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 6, 8,14,0xff,0xff,0xff,0xff},
    { 2, 6, 8,14,0xff,0xff,0xff,0xff},
    { 0, 2, 6, 8,14,0xff,0xff,0xff},
    { 4, 6, 8,14,0xff,0xff,0xff,0xff},
    { 0, 4, 6, 8,14,0xff,0xff,0xff},
    { 2, 4, 6, 8,14,0xff,0xff,0xff},
    { 0, 2, 4, 6, 8,14,0xff,0xff},
    {10,14,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0,10,14,0xff,0xff,0xff,0xff,0xff},
    { 2,10,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 2,10,14,0xff,0xff,0xff,0xff},
    { 4,10,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 4,10,14,0xff,0xff,0xff,0xff},
    { 2, 4,10,14,0xff,0xff,0xff,0xff},
    { 0, 2, 4,10,14,0xff,0xff,0xff},
    { 6,10,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 6,10,14,0xff,0xff,0xff,0xff},
    { 2, 6,10,14,0xff,0xff,0xff,0xff},
    { 0, 2, 6,10,14,0xff,0xff,0xff},
    { 4, 6,10,14,0xff,0xff,0xff,0xff},
    { 0, 4, 6,10,14,0xff,0xff,0xff},
    { 2, 4, 6,10,14,0xff,0xff,0xff},
    { 0, 2, 4, 6,10,14,0xff,0xff},
    { 8,10,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 8,10,14,0xff,0xff,0xff,0xff},
    { 2, 8,10,14,0xff,0xff,0xff,0xff},
    { 0, 2, 8,10,14,0xff,0xff,0xff},
    { 4, 8,10,14,0xff,0xff,0xff,0xff},
    { 0, 4, 8,10,14,0xff,0xff,0xff},
    { 2, 4, 8,10,14,0xff,0xff,0xff},
    { 0, 2, 4, 8,10,14,0xff,0xff},
    { 6, 8,10,14,0xff,0xff,0xff,0xff},
    { 0, 6, 8,10,14,0xff,0xff,0xff},
    { 2, 6, 8,10,14,0xff,0xff,0xff},
    { 0, 2, 6, 8,10,14,0xff,0xff},
    { 4, 6, 8,10,14,0xff,0xff,0xff},
    { 0, 4, 6, 8,10,14,0xff,0xff},
    { 2, 4, 6, 8,10,14,0xff,0xff},
    { 0, 2, 4, 6, 8,10,14,0xff},
    {12,14,0xff,0xff,0xff,0xff,0xff,0xff},
    { 0,12,14,0xff,0xff,0xff,0xff,0xff},
    { 2,12,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 2,12,14,0xff,0xff,0xff,0xff},
    { 4,12,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 4,12,14,0xff,0xff,0xff,0xff},
    { 2, 4,12,14,0xff,0xff,0xff,0xff},
    { 0, 2, 4,12,14,0xff,0xff,0xff},
    { 6,12,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 6,12,14,0xff,0xff,0xff,0xff},
    { 2, 6,12,14,0xff,0xff,0xff,0xff},
    { 0, 2, 6,12,14,0xff,0xff,0xff},
    { 4, 6,12,14,0xff,0xff,0xff,0xff},
    { 0, 4, 6,12,14,0xff,0xff,0xff},
    { 2, 4, 6,12,14,0xff,0xff,0xff},
    { 0, 2, 4, 6,12,14,0xff,0xff},
    { 8,12,14,0xff,0xff,0xff,0xff,0xff},
    { 0, 8,12,14,0xff,0xff,0xff,0xff},
    { 2, 8,12,14,0xff,0xff,0xff,0xff},
    { 0, 2, 8,12,14,0xff,0xff,0xff},
    { 4, 8,12,14,0xff,0xff,0xff,0xff},
    { 0, 4, 8,12,14,0xff,0xff,0xff},
    { 2, 4, 8,12,14,0xff,0xff,0xff},
    { 0, 2, 4, 8,12,14,0xff,0xff},
    { 6, 8,12,14,0xff,0xff,0xff,0xff},
    { 0, 6, 8,12,14,0xff,0xff,0xff},
    { 2, 6, 8,12,14,0xff,0xff,0xff},
    { 0, 2, 6, 8,12,14,0xff,0xff},
    { 4, 6, 8,12,14,0xff,0xff,0xff},
    { 0, 4, 6, 8,12,14,0xff,0xff},
    { 2, 4, 6, 8,12,14,0xff,0xff},
    { 0, 2, 4, 6, 8,12,14,0xff},
    {10,12,14,0xff,0xff,0xff,0xff,0xff},
    { 0,10,12,14,0xff,0xff,0xff,0xff},
    { 2,10,12,14,0xff,0xff,0xff,0xff},
    { 0, 2,10,12,14,0xff,0xff,0xff},
    { 4,10,12,14,0xff,0xff,0xff,0xff},
    { 0, 4,10,12,14,0xff,0xff,0xff},
    { 2, 4,10,12,14,0xff,0xff,0xff},
    { 0, 2, 4,10,12,14,0xff,0xff},
    { 6,10,12,14,0xff,0xff,0xff,0xff},
    { 0, 6,10,12,14,0xff,0xff,0xff},
    { 2, 6,10,12,14,0xff,0xff,0xff},
    { 0, 2, 6,10,12,14,0xff,0xff},
    { 4, 6,10,12,14,0xff,0xff,0xff},
    { 0, 4, 6,10,12,14,0xff,0xff},
    { 2, 4, 6,10,12,14,0xff,0xff},
    { 0, 2, 4, 6,10,12,14,0xff},
    { 8,10,12,14,0xff,0xff,0xff,0xff},
    { 0, 8,10,12,14,0xff,0xff,0xff},
    { 2, 8,10,12,14,0xff,0xff,0xff},
    { 0, 2, 8,10,12,14,0xff,0xff},
    { 4, 8,10,12,14,0xff,0xff,0xff},
    { 0, 4, 8,10,12,14,0xff,0xff},
    { 2, 4, 8,10,12,14,0xff,0xff},
    { 0, 2, 4, 8,10,12,14,0xff},
    { 6, 8,10,12,14,0xff,0xff,0xff},
    { 0, 6, 8,10,12,14,0xff,0xff},
    { 2, 6, 8,10,12,14,0xff,0xff},
    { 0, 2, 6, 8,10,12,14,0xff},
    { 4, 6, 8,10,12,14,0xff,0xff},
    { 0, 4, 6, 8,10,12,14,0xff},
    { 2, 4, 6, 8,10,12,14,0xff},
    { 0, 2, 4, 6, 8,10,12,14},
};

__attribute__((target("avx2,popcnt")))
static unsigned s_rej_uniform_avx2(int16_t * restrict a_r, unsigned a_len,
                                    const uint8_t *a_buf, unsigned a_buflen)
{
    unsigned l_ctr = 0, l_pos = 0;
    const __m256i l_bound = _mm256_set1_epi16((int16_t)MLKEM_Q);
    const __m256i l_ones  = _mm256_set1_epi8(1);
    const __m256i l_mask  = _mm256_set1_epi16(0xFFF);
    const __m256i l_idx8  = _mm256_set_epi8(
        15,14,14,13,12,11,11,10, 9, 8, 8, 7, 6, 5, 5, 4,
        11,10,10, 9, 8, 7, 7, 6, 5, 4, 4, 3, 2, 1, 1, 0);

    while (l_ctr + 32 <= a_len && l_pos + 48 <= a_buflen) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)&a_buf[l_pos]);
        __m256i f1 = _mm256_loadu_si256((const __m256i *)&a_buf[l_pos + 24]);
        f0 = _mm256_permute4x64_epi64(f0, 0x94);
        f1 = _mm256_permute4x64_epi64(f1, 0x94);
        f0 = _mm256_shuffle_epi8(f0, l_idx8);
        f1 = _mm256_shuffle_epi8(f1, l_idx8);
        __m256i g0 = _mm256_srli_epi16(f0, 4);
        __m256i g1 = _mm256_srli_epi16(f1, 4);
        f0 = _mm256_blend_epi16(f0, g0, 0xAA);
        f1 = _mm256_blend_epi16(f1, g1, 0xAA);
        f0 = _mm256_and_si256(f0, l_mask);
        f1 = _mm256_and_si256(f1, l_mask);
        l_pos += 48;

        g0 = _mm256_cmpgt_epi16(l_bound, f0);
        g1 = _mm256_cmpgt_epi16(l_bound, f1);
        g0 = _mm256_packs_epi16(g0, g1);
        uint32_t good = (uint32_t)_mm256_movemask_epi8(g0);

        __m256i p0 = _mm256_castsi128_si256(
            _mm_loadl_epi64((const __m128i *)&s_rej_idx[(good >>  0) & 0xFF]));
        __m256i p1 = _mm256_castsi128_si256(
            _mm_loadl_epi64((const __m128i *)&s_rej_idx[(good >>  8) & 0xFF]));
        p0 = _mm256_inserti128_si256(p0,
            _mm_loadl_epi64((const __m128i *)&s_rej_idx[(good >> 16) & 0xFF]), 1);
        p1 = _mm256_inserti128_si256(p1,
            _mm_loadl_epi64((const __m128i *)&s_rej_idx[(good >> 24) & 0xFF]), 1);

        __m256i q0 = _mm256_add_epi8(p0, l_ones);
        __m256i q1 = _mm256_add_epi8(p1, l_ones);
        p0 = _mm256_unpacklo_epi8(p0, q0);
        p1 = _mm256_unpacklo_epi8(p1, q1);

        f0 = _mm256_shuffle_epi8(f0, p0);
        f1 = _mm256_shuffle_epi8(f1, p1);

        _mm_storeu_si128((__m128i *)&a_r[l_ctr], _mm256_castsi256_si128(f0));
        l_ctr += _mm_popcnt_u32((good >>  0) & 0xFF);
        _mm_storeu_si128((__m128i *)&a_r[l_ctr], _mm256_extracti128_si256(f0, 1));
        l_ctr += _mm_popcnt_u32((good >> 16) & 0xFF);
        _mm_storeu_si128((__m128i *)&a_r[l_ctr], _mm256_castsi256_si128(f1));
        l_ctr += _mm_popcnt_u32((good >>  8) & 0xFF);
        _mm_storeu_si128((__m128i *)&a_r[l_ctr], _mm256_extracti128_si256(f1, 1));
        l_ctr += _mm_popcnt_u32((good >> 24) & 0xFF);
    }

    while (l_ctr < a_len && l_pos + 3 <= a_buflen) {
        uint16_t val0 = ((a_buf[l_pos] >> 0) | ((uint16_t)a_buf[l_pos + 1] << 8)) & 0xFFF;
        uint16_t val1 = ((a_buf[l_pos + 1] >> 4) | ((uint16_t)a_buf[l_pos + 2] << 4)) & 0xFFF;
        l_pos += 3;
        if (val0 < MLKEM_Q)
            a_r[l_ctr++] = (int16_t)val0;
        if (l_ctr < a_len && val1 < MLKEM_Q)
            a_r[l_ctr++] = (int16_t)val1;
    }
    return l_ctr;
}
#endif /* MLKEM_REJ_AVX2 */

static inline unsigned s_rej_uniform(int16_t *a_r, unsigned a_len,
                                      const uint8_t *a_buf, unsigned a_buflen)
{
#ifdef MLKEM_REJ_AVX2
    return s_rej_uniform_avx2(a_r, a_len, a_buf, a_buflen);
#else
    return s_rej_uniform_scalar(a_r, a_len, a_buf, a_buflen);
#endif
}

/* ---- matrix generation ---- */

#define GEN_MATRIX_NBLOCKS ((12 * MLKEM_N / 8 * (1 << 12) / MLKEM_Q \
                             + MLKEM_XOF_BLOCKBYTES) / MLKEM_XOF_BLOCKBYTES)

static void s_gen_matrix(dap_mlkem_polyvec *a_mat, const uint8_t a_seed[MLKEM_SYMBYTES],
                          int a_transposed)
{
    unsigned l_total = MLKEM_K * MLKEM_K;
    uint8_t l_buf[4][GEN_MATRIX_NBLOCKS * MLKEM_XOF_BLOCKBYTES + 2];

    for (unsigned l_idx = 0; l_idx < l_total; l_idx += 4) {
        uint8_t l_x[4], l_y[4];
        unsigned l_count = (l_total - l_idx >= 4) ? 4 : l_total - l_idx;
        for (unsigned k = 0; k < l_count; k++) {
            unsigned ii = (l_idx + k) / MLKEM_K;
            unsigned jj = (l_idx + k) % MLKEM_K;
            l_x[k] = a_transposed ? (uint8_t)ii : (uint8_t)jj;
            l_y[k] = a_transposed ? (uint8_t)jj : (uint8_t)ii;
        }
        for (unsigned k = l_count; k < 4; k++) {
            l_x[k] = l_x[0];
            l_y[k] = l_y[0];
        }

        dap_keccak_x4_state_t l_x4;
        dap_mlkem_xof_absorb_squeeze_x4(&l_x4,
                                          l_buf[0], l_buf[1], l_buf[2], l_buf[3],
                                          GEN_MATRIX_NBLOCKS, a_seed,
                                          l_x[0], l_y[0], l_x[1], l_y[1],
                                          l_x[2], l_y[2], l_x[3], l_y[3]);

        unsigned l_ctr[4];
        int l_need_more = 0;
        for (unsigned k = 0; k < l_count; k++) {
            unsigned ii = (l_idx + k) / MLKEM_K;
            unsigned jj = (l_idx + k) % MLKEM_K;
            l_ctr[k] = s_rej_uniform(a_mat[ii].vec[jj].coeffs, MLKEM_N,
                                      l_buf[k], GEN_MATRIX_NBLOCKS * MLKEM_XOF_BLOCKBYTES);
            if (l_ctr[k] < MLKEM_N)
                l_need_more = 1;
        }
        for (unsigned k = l_count; k < 4; k++)
            l_ctr[k] = MLKEM_N;

        while (l_need_more) {
            uint8_t l_extra[4][MLKEM_XOF_BLOCKBYTES];
            dap_mlkem_xof_squeezeblocks_x4(l_extra[0], l_extra[1], l_extra[2], l_extra[3],
                                             1, &l_x4);
            l_need_more = 0;
            for (unsigned k = 0; k < l_count; k++) {
                if (l_ctr[k] < MLKEM_N) {
                    unsigned ii = (l_idx + k) / MLKEM_K;
                    unsigned jj = (l_idx + k) % MLKEM_K;
                    l_ctr[k] += s_rej_uniform(a_mat[ii].vec[jj].coeffs + l_ctr[k],
                                               MLKEM_N - l_ctr[k],
                                               l_extra[k], MLKEM_XOF_BLOCKBYTES);
                    if (l_ctr[k] < MLKEM_N)
                        l_need_more = 1;
                }
            }
        }
    }
}

/* ---- IND-CPA ---- */

void MLKEM_NAMESPACE(_indcpa_keypair)(uint8_t a_pk[MLKEM_INDCPA_PUBLICKEYBYTES],
                                       uint8_t a_sk[MLKEM_INDCPA_SECRETKEYBYTES])
{
    uint8_t l_buf[2 * MLKEM_SYMBYTES];
    const uint8_t *l_publicseed = l_buf;
    const uint8_t *l_noiseseed  = l_buf + MLKEM_SYMBYTES;
    uint8_t l_nonce = 0;
    dap_mlkem_polyvec l_a[MLKEM_K], l_e, l_pkpv, l_skpv;

    dap_random_bytes(l_buf, MLKEM_SYMBYTES);
    dap_mlkem_hash_g(l_buf, l_buf, MLKEM_SYMBYTES);

    s_gen_matrix(l_a, l_publicseed, 0);
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_polyvec_nttpack)(&l_a[i]);

    /* Batch noise sampling: skpv[0..K-1] then e[0..K-1], all eta1 — always x4 */
    dap_mlkem_poly *l_npolys[2 * MLKEM_K];
    for (unsigned j = 0; j < MLKEM_K; j++)
        l_npolys[j] = &l_skpv.vec[j];
    for (unsigned j = 0; j < MLKEM_K; j++)
        l_npolys[MLKEM_K + j] = &l_e.vec[j];

    {
        dap_mlkem_poly l_dummy;
        unsigned i = 0;
        for (; i + 4 <= 2 * MLKEM_K; i += 4, l_nonce += 4)
            MLKEM_NAMESPACE(_poly_getnoise_eta1_x4)(l_npolys[i], l_npolys[i + 1],
                                                      l_npolys[i + 2], l_npolys[i + 3],
                                                      l_noiseseed, l_nonce, (uint8_t)(l_nonce + 1),
                                                      (uint8_t)(l_nonce + 2), (uint8_t)(l_nonce + 3));
        if (i < 2 * MLKEM_K) {
            dap_mlkem_poly *l_ptrs[4];
            uint8_t l_nonces[4];
            unsigned k = 0;
            for (; i + k < 2 * MLKEM_K; k++) {
                l_ptrs[k] = l_npolys[i + k];
                l_nonces[k] = (uint8_t)(l_nonce + k);
            }
            for (; k < 4; k++) {
                l_ptrs[k] = &l_dummy;
                l_nonces[k] = l_nonces[0];
            }
            MLKEM_NAMESPACE(_poly_getnoise_eta1_x4)(l_ptrs[0], l_ptrs[1], l_ptrs[2], l_ptrs[3],
                                                      l_noiseseed, l_nonces[0], l_nonces[1],
                                                      l_nonces[2], l_nonces[3]);
            l_nonce += (uint8_t)(2 * MLKEM_K - i);
        }
    }

    MLKEM_NAMESPACE(_polyvec_ntt)(&l_skpv);
    MLKEM_NAMESPACE(_polyvec_ntt)(&l_e);

    dap_mlkem_polyvec_mulcache l_skpv_cache;
    MLKEM_NAMESPACE(_polyvec_mulcache_compute)(&l_skpv_cache, &l_skpv);
    for (unsigned i = 0; i < MLKEM_K; i++) {
        MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(&l_pkpv.vec[i], &l_a[i], &l_skpv, &l_skpv_cache);
        MLKEM_NAMESPACE(_poly_tomont)(&l_pkpv.vec[i]);
    }
    MLKEM_NAMESPACE(_polyvec_add)(&l_pkpv, &l_pkpv, &l_e);
    MLKEM_NAMESPACE(_polyvec_reduce)(&l_pkpv);

    MLKEM_NAMESPACE(_polyvec_nttunpack)(&l_skpv);
    MLKEM_NAMESPACE(_polyvec_nttunpack)(&l_pkpv);
    s_pack_sk(a_sk, &l_skpv);
    s_pack_pk(a_pk, &l_pkpv, l_publicseed);
}

void MLKEM_NAMESPACE(_indcpa_enc)(uint8_t a_c[MLKEM_INDCPA_BYTES],
                                   const uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                   const uint8_t a_pk[MLKEM_INDCPA_PUBLICKEYBYTES],
                                   const uint8_t a_coins[MLKEM_SYMBYTES])
{
    uint8_t l_seed[MLKEM_SYMBYTES];
    uint8_t l_nonce = 0;
    dap_mlkem_polyvec l_sp, l_pkpv, l_ep, l_at[MLKEM_K], l_bp;
    dap_mlkem_poly l_v, l_k, l_epp;

    s_unpack_pk(&l_pkpv, l_seed, a_pk);
    MLKEM_NAMESPACE(_polyvec_nttpack)(&l_pkpv);
    MLKEM_NAMESPACE(_poly_frommsg)(&l_k, a_m);
    s_gen_matrix(l_at, l_seed, 1);
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_polyvec_nttpack)(&l_at[i]);

    /* sp: K polys with eta1 — always x4 (pad with dummy if K < 4) */
    {
        dap_mlkem_poly l_dummy;
        unsigned i = 0;
        for (; i + 4 <= MLKEM_K; i += 4, l_nonce += 4)
            MLKEM_NAMESPACE(_poly_getnoise_eta1_x4)(&l_sp.vec[i], &l_sp.vec[i + 1],
                                                      &l_sp.vec[i + 2], &l_sp.vec[i + 3],
                                                      a_coins, l_nonce, (uint8_t)(l_nonce + 1),
                                                      (uint8_t)(l_nonce + 2), (uint8_t)(l_nonce + 3));
        if (i < MLKEM_K) {
            dap_mlkem_poly *l_ptrs[4];
            uint8_t l_nonces[4];
            unsigned k = 0;
            for (; i + k < MLKEM_K; k++) {
                l_ptrs[k] = &l_sp.vec[i + k];
                l_nonces[k] = (uint8_t)(l_nonce + k);
            }
            for (; k < 4; k++) {
                l_ptrs[k] = &l_dummy;
                l_nonces[k] = l_nonces[0];
            }
            MLKEM_NAMESPACE(_poly_getnoise_eta1_x4)(l_ptrs[0], l_ptrs[1], l_ptrs[2], l_ptrs[3],
                                                      a_coins, l_nonces[0], l_nonces[1],
                                                      l_nonces[2], l_nonces[3]);
            l_nonce += (uint8_t)(MLKEM_K - i);
            i = MLKEM_K;
        }
    }
    /* ep: K polys with eta2, epp: 1 poly with eta2 — always x4 */
    {
        dap_mlkem_poly l_dummy;
        dap_mlkem_poly *l_eta2[MLKEM_K + 1];
        for (unsigned j = 0; j < MLKEM_K; j++)
            l_eta2[j] = &l_ep.vec[j];
        l_eta2[MLKEM_K] = &l_epp;
        unsigned i = 0;
        for (; i + 4 <= MLKEM_K + 1; i += 4, l_nonce += 4)
            MLKEM_NAMESPACE(_poly_getnoise_eta2_x4)(l_eta2[i], l_eta2[i + 1],
                                                      l_eta2[i + 2], l_eta2[i + 3],
                                                      a_coins, l_nonce, (uint8_t)(l_nonce + 1),
                                                      (uint8_t)(l_nonce + 2), (uint8_t)(l_nonce + 3));
        if (i < MLKEM_K + 1) {
            dap_mlkem_poly *l_ptrs[4];
            uint8_t l_nonces[4];
            unsigned k = 0;
            for (; i + k < MLKEM_K + 1; k++) {
                l_ptrs[k] = l_eta2[i + k];
                l_nonces[k] = (uint8_t)(l_nonce + k);
            }
            for (; k < 4; k++) {
                l_ptrs[k] = &l_dummy;
                l_nonces[k] = l_nonces[0];
            }
            MLKEM_NAMESPACE(_poly_getnoise_eta2_x4)(l_ptrs[0], l_ptrs[1], l_ptrs[2], l_ptrs[3],
                                                      a_coins, l_nonces[0], l_nonces[1],
                                                      l_nonces[2], l_nonces[3]);
            l_nonce += (uint8_t)(MLKEM_K + 1 - i);
        }
    }

    MLKEM_NAMESPACE(_polyvec_ntt)(&l_sp);

    dap_mlkem_polyvec_mulcache l_sp_cache;
    MLKEM_NAMESPACE(_polyvec_mulcache_compute)(&l_sp_cache, &l_sp);
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(&l_bp.vec[i], &l_at[i], &l_sp, &l_sp_cache);
    MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(&l_v, &l_pkpv, &l_sp, &l_sp_cache);

    MLKEM_NAMESPACE(_polyvec_invntt_tomont)(&l_bp);
    MLKEM_NAMESPACE(_poly_invntt_tomont)(&l_v);

    MLKEM_NAMESPACE(_polyvec_add)(&l_bp, &l_bp, &l_ep);
    MLKEM_NAMESPACE(_poly_add)(&l_v, &l_v, &l_epp);
    MLKEM_NAMESPACE(_poly_add)(&l_v, &l_v, &l_k);
    MLKEM_NAMESPACE(_polyvec_reduce)(&l_bp);
    MLKEM_NAMESPACE(_poly_reduce)(&l_v);

    s_pack_ciphertext(a_c, &l_bp, &l_v);
}

void MLKEM_NAMESPACE(_indcpa_dec)(uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                   const uint8_t a_c[MLKEM_INDCPA_BYTES],
                                   const uint8_t a_sk[MLKEM_INDCPA_SECRETKEYBYTES])
{
    dap_mlkem_polyvec l_bp, l_skpv;
    dap_mlkem_poly l_v, l_mp;

    s_unpack_ciphertext(&l_bp, &l_v, a_c);
    s_unpack_sk(&l_skpv, a_sk);
    MLKEM_NAMESPACE(_polyvec_nttpack)(&l_skpv);

    MLKEM_NAMESPACE(_polyvec_ntt)(&l_bp);
    dap_mlkem_polyvec_mulcache l_bp_cache;
    MLKEM_NAMESPACE(_polyvec_mulcache_compute)(&l_bp_cache, &l_bp);
    MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(&l_mp, &l_skpv, &l_bp, &l_bp_cache);
    MLKEM_NAMESPACE(_poly_invntt_tomont)(&l_mp);

    MLKEM_NAMESPACE(_poly_sub)(&l_mp, &l_v, &l_mp);
    MLKEM_NAMESPACE(_poly_reduce)(&l_mp);

    MLKEM_NAMESPACE(_poly_tomsg)(a_m, &l_mp);
}
