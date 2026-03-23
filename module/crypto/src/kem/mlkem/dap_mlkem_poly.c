/**
 * @file dap_mlkem_poly.c
 * @brief Polynomial operations for ML-KEM (FIPS 203).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <string.h>
#include "dap_mlkem_poly.h"
#include "dap_mlkem_ntt.h"
#include "dap_mlkem_reduce.h"
#include "dap_mlkem_cbd.h"
#include "dap_mlkem_symmetric.h"
#include "dap_mlkem_poly_simd.h"
#include "dap_cpu_arch.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define MLKEM_POLY_AVX2 1
#endif

#ifdef MLKEM_POLY_AVX2
__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_poly_compress_d4_onepass_avx2(uint8_t *a_r, const int16_t *a_coeffs)
{
    const __m256i v = _mm256_set1_epi16(20159);
    const __m256i shift1 = _mm256_set1_epi16(1 << 9);
    const __m256i mask = _mm256_set1_epi16(15);
    const __m256i shift2 = _mm256_set1_epi16((16 << 8) + 1);
    const __m256i permdidx = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);

    for (unsigned i = 0; i < MLKEM_N / 64; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 64 * i));
        __m256i f1 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 64 * i + 16));
        __m256i f2 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 64 * i + 32));
        __m256i f3 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 64 * i + 48));
        f0 = _mm256_mulhi_epi16(f0, v);
        f1 = _mm256_mulhi_epi16(f1, v);
        f2 = _mm256_mulhi_epi16(f2, v);
        f3 = _mm256_mulhi_epi16(f3, v);
        f0 = _mm256_mulhrs_epi16(f0, shift1);
        f1 = _mm256_mulhrs_epi16(f1, shift1);
        f2 = _mm256_mulhrs_epi16(f2, shift1);
        f3 = _mm256_mulhrs_epi16(f3, shift1);
        f0 = _mm256_and_si256(f0, mask);
        f1 = _mm256_and_si256(f1, mask);
        f2 = _mm256_and_si256(f2, mask);
        f3 = _mm256_and_si256(f3, mask);
        f0 = _mm256_packus_epi16(f0, f1);
        f2 = _mm256_packus_epi16(f2, f3);
        f0 = _mm256_maddubs_epi16(f0, shift2);
        f2 = _mm256_maddubs_epi16(f2, shift2);
        f0 = _mm256_packus_epi16(f0, f2);
        f0 = _mm256_permutevar8x32_epi32(f0, permdidx);
        _mm256_storeu_si256((__m256i *)(a_r + 32 * i), f0);
    }
}

__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_poly_compress_d5_onepass_avx2(uint8_t *a_r, const int16_t *a_coeffs)
{
    const __m256i v = _mm256_set1_epi16(20159);
    const __m256i shift1 = _mm256_set1_epi16(1 << 10);
    const __m256i mask = _mm256_set1_epi16(31);
    const __m256i shift2 = _mm256_set1_epi16((32 << 8) + 1);
    const __m256i shift3 = _mm256_set1_epi32((1024 << 16) + 1);
    const __m256i sllvdidx = _mm256_set1_epi64x(12);
    const __m256i shufbidx = _mm256_set_epi8(
        8, -1, -1, -1, -1, -1, 4, 3, 2, 1, 0, -1, 12, 11, 10, 9,
        -1, 12, 11, 10, 9, 8, -1, -1, -1, -1, -1, 4, 3, 2, 1, 0);

    for (unsigned i = 0; i < MLKEM_N / 32; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 32 * i));
        __m256i f1 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 32 * i + 16));
        f0 = _mm256_mulhi_epi16(f0, v);
        f1 = _mm256_mulhi_epi16(f1, v);
        f0 = _mm256_mulhrs_epi16(f0, shift1);
        f1 = _mm256_mulhrs_epi16(f1, shift1);
        f0 = _mm256_and_si256(f0, mask);
        f1 = _mm256_and_si256(f1, mask);
        f0 = _mm256_packus_epi16(f0, f1);
        f0 = _mm256_maddubs_epi16(f0, shift2);
        f0 = _mm256_madd_epi16(f0, shift3);
        f0 = _mm256_sllv_epi32(f0, sllvdidx);
        f0 = _mm256_srlv_epi64(f0, sllvdidx);
        f0 = _mm256_shuffle_epi8(f0, shufbidx);
        __m128i t0 = _mm256_castsi256_si128(f0);
        __m128i t1 = _mm256_extracti128_si256(f0, 1);
        t0 = _mm_blendv_epi8(t0, t1, _mm256_castsi256_si128(shufbidx));
        _mm_storeu_si128((__m128i *)(a_r + 20 * i), t0);
        memcpy(a_r + 20 * i + 16, &t1, 4);
    }
}

__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_poly_decompress_d4_avx2(int16_t *a_r, const uint8_t *a_a)
{
    const __m256i q = _mm256_set1_epi16(MLKEM_Q);
    const __m256i shufbidx = _mm256_set_epi8(
        7,7,7,7, 6,6,6,6, 5,5,5,5, 4,4,4,4,
        3,3,3,3, 2,2,2,2, 1,1,1,1, 0,0,0,0);
    const __m256i mask = _mm256_set1_epi32(0x00F0000F);
    const __m256i shift = _mm256_set1_epi32((128 << 16) + 2048);

    for (unsigned i = 0; i < MLKEM_N / 16; i++) {
        __m128i t = _mm_loadl_epi64((const __m128i *)(a_a + 8 * i));
        __m256i f = _mm256_broadcastsi128_si256(t);
        f = _mm256_shuffle_epi8(f, shufbidx);
        f = _mm256_and_si256(f, mask);
        f = _mm256_mullo_epi16(f, shift);
        f = _mm256_mulhrs_epi16(f, q);
        _mm256_storeu_si256((__m256i *)(a_r + 16 * i), f);
    }
}

__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_poly_decompress_d5_avx2(int16_t *a_r, const uint8_t *a_a)
{
    const __m256i q = _mm256_set1_epi16(MLKEM_Q);
    const __m256i shufbidx = _mm256_set_epi8(
        9,9,9,8, 8,8,8,7, 7,6,6,6, 6,5,5,5,
        4,4,4,3, 3,3,3,2, 2,1,1,1, 1,0,0,0);
    const __m256i mask = _mm256_set_epi16(
        248, 1984, 62, 496, 3968, 124, 992, 31,
        248, 1984, 62, 496, 3968, 124, 992, 31);
    const __m256i shift = _mm256_set_epi16(
        128, 16, 512, 64, 8, 256, 32, 1024,
        128, 16, 512, 64, 8, 256, 32, 1024);

    for (unsigned i = 0; i < MLKEM_N / 16; i++) {
        __m128i t = _mm_loadl_epi64((const __m128i *)(a_a + 10 * i));
        int16_t ti;
        memcpy(&ti, a_a + 10 * i + 8, 2);
        t = _mm_insert_epi16(t, ti, 4);
        __m256i f = _mm256_broadcastsi128_si256(t);
        f = _mm256_shuffle_epi8(f, shufbidx);
        f = _mm256_and_si256(f, mask);
        f = _mm256_mullo_epi16(f, shift);
        f = _mm256_mulhrs_epi16(f, q);
        _mm256_storeu_si256((__m256i *)(a_r + 16 * i), f);
    }
}
#endif /* MLKEM_POLY_AVX2 */

void MLKEM_NAMESPACE(_poly_compress)(uint8_t *a_r, dap_mlkem_poly *a_a)
{
    MLKEM_NAMESPACE(_poly_csubq)(a_a);
#if MLKEM_POLYCOMPRESSEDBYTES == 128
#ifdef MLKEM_POLY_AVX2
    if (dap_mlkem_poly_simd_available()) {
        s_poly_compress_d4_onepass_avx2(a_r, a_a->coeffs);
        return;
    }
#endif
    for (unsigned k = 0; k < MLKEM_N; k++)
        a_a->coeffs[k] = (int16_t)((((uint16_t)a_a->coeffs[k] << 4) + MLKEM_Q / 2) / MLKEM_Q) & 15;
    for (unsigned i = 0; i < MLKEM_N / 2; i++)
        a_r[i] = (uint8_t)(a_a->coeffs[2 * i] | (a_a->coeffs[2 * i + 1] << 4));
#elif MLKEM_POLYCOMPRESSEDBYTES == 160
#ifdef MLKEM_POLY_AVX2
    if (dap_mlkem_poly_simd_available()) {
        s_poly_compress_d5_onepass_avx2(a_r, a_a->coeffs);
        return;
    }
#endif
    for (unsigned k = 0; k < MLKEM_N; k++)
        a_a->coeffs[k] = (int16_t)((((uint32_t)a_a->coeffs[k] << 5) + MLKEM_Q / 2) / MLKEM_Q) & 31;
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        int16_t *c = a_a->coeffs + 8 * i;
        a_r[0] = (uint8_t)((c[0]) | (c[1] << 5));
        a_r[1] = (uint8_t)((c[1] >> 3) | (c[2] << 2) | (c[3] << 7));
        a_r[2] = (uint8_t)((c[3] >> 1) | (c[4] << 4));
        a_r[3] = (uint8_t)((c[4] >> 4) | (c[5] << 1) | (c[6] << 6));
        a_r[4] = (uint8_t)((c[6] >> 2) | (c[7] << 3));
        a_r += 5;
    }
#endif
}

void MLKEM_NAMESPACE(_poly_decompress)(dap_mlkem_poly *a_r, const uint8_t *a_a)
{
#if MLKEM_POLYCOMPRESSEDBYTES == 128
#ifdef MLKEM_POLY_AVX2
    if (dap_mlkem_poly_simd_available()) {
        s_poly_decompress_d4_avx2(a_r->coeffs, a_a);
        return;
    }
#endif
    for (unsigned i = 0; i < MLKEM_N / 2; i++) {
        a_r->coeffs[2 * i]     = (int16_t)(((uint16_t)(a_a[0] & 15) * MLKEM_Q + 8) >> 4);
        a_r->coeffs[2 * i + 1] = (int16_t)(((uint16_t)(a_a[0] >> 4) * MLKEM_Q + 8) >> 4);
        a_a++;
    }
#elif MLKEM_POLYCOMPRESSEDBYTES == 160
#ifdef MLKEM_POLY_AVX2
    if (dap_mlkem_poly_simd_available()) {
        s_poly_decompress_d5_avx2(a_r->coeffs, a_a);
        return;
    }
#endif
    uint8_t t[8];
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        t[0] = a_a[0];
        t[1] = (a_a[0] >> 5) | (a_a[1] << 3);
        t[2] = a_a[1] >> 2;
        t[3] = (a_a[1] >> 7) | (a_a[2] << 1);
        t[4] = (a_a[2] >> 4) | (a_a[3] << 4);
        t[5] = a_a[3] >> 1;
        t[6] = (a_a[3] >> 6) | (a_a[4] << 2);
        t[7] = a_a[4] >> 3;
        a_a += 5;
        for (unsigned j = 0; j < 8; j++)
            a_r->coeffs[8 * i + j] = (int16_t)(((uint32_t)(t[j] & 31) * MLKEM_Q + 16) >> 5);
    }
#endif
}

#ifdef MLKEM_POLY_AVX2
__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_poly_tobytes_avx2(uint8_t *a_r, const int16_t *a_coeffs)
{
    const __m256i v_mix = _mm256_set1_epi32(0x10000001);
    const __m256i v_shuf = _mm256_setr_epi8(
        0,1,2, 4,5,6, 8,9,10, 12,13,14, -1,-1,-1,-1,
        0,1,2, 4,5,6, 8,9,10, 12,13,14, -1,-1,-1,-1);

    for (int i = 0; i < 16; i++) {
        __m256i f = _mm256_loadu_si256((const __m256i *)&a_coeffs[16 * i]);
        __m256i t = _mm256_madd_epi16(f, v_mix);
        t = _mm256_shuffle_epi8(t, v_shuf);
        uint8_t *out = a_r + 24 * i;
        __m128i lo = _mm256_castsi256_si128(t);
        __m128i hi = _mm256_extracti128_si256(t, 1);
        _mm_storel_epi64((__m128i *)out, lo);
        *(uint32_t *)(out + 8) = (uint32_t)_mm_extract_epi32(lo, 2);
        _mm_storel_epi64((__m128i *)(out + 12), hi);
        *(uint32_t *)(out + 20) = (uint32_t)_mm_extract_epi32(hi, 2);
    }
}
#endif

void MLKEM_NAMESPACE(_poly_tobytes)(uint8_t *a_r, dap_mlkem_poly *a_a)
{
    MLKEM_NAMESPACE(_poly_csubq)(a_a);
#ifdef MLKEM_POLY_AVX2
    s_poly_tobytes_avx2(a_r, a_a->coeffs);
#else
    for (unsigned i = 0; i < MLKEM_N / 2; i++) {
        uint16_t t0 = (uint16_t)a_a->coeffs[2 * i];
        uint16_t t1 = (uint16_t)a_a->coeffs[2 * i + 1];
        a_r[3 * i]     = (uint8_t)(t0);
        a_r[3 * i + 1] = (uint8_t)((t0 >> 8) | (t1 << 4));
        a_r[3 * i + 2] = (uint8_t)(t1 >> 4);
    }
#endif
}

#ifdef MLKEM_POLY_AVX2
__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_poly_frombytes_avx2(int16_t *a_r, const uint8_t *a_a)
{
    const __m256i v_shuf = _mm256_setr_epi8(
        0,1, 1,2, 3,4, 4,5, 6,7, 7,8, 9,10, 10,11,
        0,1, 1,2, 3,4, 4,5, 6,7, 7,8, 9,10, 10,11);
    const __m256i v_mask = _mm256_set1_epi16(0x0FFF);

    for (int i = 0; i < 16; i++) {
        const uint8_t *p = a_a + 24 * i;
        __m128i lo_raw = _mm_loadu_si128((const __m128i *)p);
        __m128i hi_raw;
        if (__builtin_expect(i < 15, 1))
            hi_raw = _mm_loadu_si128((const __m128i *)(p + 12));
        else {
            uint8_t tmp[16] __attribute__((aligned(16))) = {0};
            memcpy(tmp, p + 12, 12);
            hi_raw = _mm_load_si128((const __m128i *)tmp);
        }
        __m256i raw = _mm256_inserti128_si256(_mm256_castsi128_si256(lo_raw), hi_raw, 1);
        __m256i arranged = _mm256_shuffle_epi8(raw, v_shuf);
        __m256i masked  = _mm256_and_si256(arranged, v_mask);
        __m256i shifted = _mm256_srli_epi16(arranged, 4);
        __m256i result  = _mm256_blend_epi16(masked, shifted, 0xAA);
        _mm256_storeu_si256((__m256i *)&a_r[16 * i], result);
    }
}
#endif

void MLKEM_NAMESPACE(_poly_frombytes)(dap_mlkem_poly *a_r, const uint8_t *a_a)
{
#ifdef MLKEM_POLY_AVX2
    s_poly_frombytes_avx2(a_r->coeffs, a_a);
#else
    for (unsigned i = 0; i < MLKEM_N / 2; i++) {
        a_r->coeffs[2 * i]     = (int16_t)(((a_a[3 * i]) | ((uint16_t)a_a[3 * i + 1] << 8)) & 0xFFF);
        a_r->coeffs[2 * i + 1] = (int16_t)(((a_a[3 * i + 1] >> 4) | ((uint16_t)a_a[3 * i + 2] << 4)) & 0xFFF);
    }
#endif
}

#ifdef MLKEM_POLY_AVX2
__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_poly_frommsg_avx2(int16_t *a_r, const uint8_t *a_msg)
{
    const __m256i v_half_q = _mm256_set1_epi16((MLKEM_Q + 1) / 2);
    const __m128i v_bits = _mm_setr_epi16(1, 2, 4, 8, 16, 32, 64, 128);

    for (int i = 0; i < 16; i++) {
        __m128i b0 = _mm_set1_epi16(a_msg[2 * i]);
        __m128i b1 = _mm_set1_epi16(a_msg[2 * i + 1]);
        b0 = _mm_and_si128(b0, v_bits);
        b1 = _mm_and_si128(b1, v_bits);
        __m256i nz = _mm256_inserti128_si256(_mm256_castsi128_si256(b0), b1, 1);
        __m256i cmp = _mm256_cmpeq_epi16(nz, _mm256_setzero_si256());
        __m256i result = _mm256_andnot_si256(cmp, v_half_q);
        _mm256_storeu_si256((__m256i *)&a_r[16 * i], result);
    }
}

__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_poly_tomsg_avx2(uint8_t *a_msg, const int16_t *a_coeffs)
{
    const __m256i v_lo = _mm256_set1_epi16(832);
    const __m256i v_hi = _mm256_set1_epi16(2497);

    for (int i = 0; i < 16; i++) {
        __m256i f = _mm256_loadu_si256((const __m256i *)&a_coeffs[16 * i]);
        __m256i c_lo = _mm256_cmpgt_epi16(f, v_lo);
        __m256i c_hi = _mm256_cmpgt_epi16(v_hi, f);
        __m256i valid = _mm256_and_si256(c_lo, c_hi);
        __m256i packed = _mm256_packs_epi16(valid, _mm256_setzero_si256());
        packed = _mm256_permute4x64_epi64(packed, 0xD8);
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(packed);
        a_msg[2 * i]     = (uint8_t)(mask & 0xFF);
        a_msg[2 * i + 1] = (uint8_t)((mask >> 8) & 0xFF);
    }
}
#endif

void MLKEM_NAMESPACE(_poly_frommsg)(dap_mlkem_poly *a_r, const uint8_t a_msg[MLKEM_INDCPA_MSGBYTES])
{
#ifdef MLKEM_POLY_AVX2
    s_poly_frommsg_avx2(a_r->coeffs, a_msg);
#else
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        for (unsigned j = 0; j < 8; j++) {
            int16_t mask = -(int16_t)((a_msg[i] >> j) & 1);
            a_r->coeffs[8 * i + j] = mask & ((MLKEM_Q + 1) / 2);
        }
    }
#endif
}

void MLKEM_NAMESPACE(_poly_tomsg)(uint8_t a_msg[MLKEM_INDCPA_MSGBYTES], dap_mlkem_poly *a_a)
{
    MLKEM_NAMESPACE(_poly_csubq)(a_a);
#ifdef MLKEM_POLY_AVX2
    s_poly_tomsg_avx2(a_msg, a_a->coeffs);
#else
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        a_msg[i] = 0;
        for (unsigned j = 0; j < 8; j++) {
            uint16_t t = (uint16_t)((((uint16_t)a_a->coeffs[8 * i + j] << 1) + MLKEM_Q / 2) / MLKEM_Q) & 1;
            a_msg[i] |= (uint8_t)(t << j);
        }
    }
#endif
}

void MLKEM_NAMESPACE(_poly_getnoise_eta1)(dap_mlkem_poly *a_r,
                                           const uint8_t a_seed[MLKEM_SYMBYTES], uint8_t a_nonce)
{
    uint8_t l_buf[MLKEM_ETA1 * MLKEM_N / 4];
    dap_mlkem_prf(l_buf, sizeof(l_buf), a_seed, a_nonce);
    MLKEM_NAMESPACE(_cbd_eta1)(a_r, l_buf);
}

void MLKEM_NAMESPACE(_poly_getnoise_eta1_x4)(dap_mlkem_poly *a_r0, dap_mlkem_poly *a_r1,
                                               dap_mlkem_poly *a_r2, dap_mlkem_poly *a_r3,
                                               const uint8_t a_seed[MLKEM_SYMBYTES],
                                               uint8_t a_n0, uint8_t a_n1,
                                               uint8_t a_n2, uint8_t a_n3)
{
    enum { BUFLEN = MLKEM_ETA1 * MLKEM_N / 4 };
    uint8_t l_buf[4][BUFLEN];
    dap_mlkem_prf_x4(l_buf[0], l_buf[1], l_buf[2], l_buf[3],
                      BUFLEN, a_seed, a_n0, a_n1, a_n2, a_n3);
    MLKEM_NAMESPACE(_cbd_eta1)(a_r0, l_buf[0]);
    MLKEM_NAMESPACE(_cbd_eta1)(a_r1, l_buf[1]);
    MLKEM_NAMESPACE(_cbd_eta1)(a_r2, l_buf[2]);
    MLKEM_NAMESPACE(_cbd_eta1)(a_r3, l_buf[3]);
}

void MLKEM_NAMESPACE(_poly_getnoise_eta2)(dap_mlkem_poly *a_r,
                                           const uint8_t a_seed[MLKEM_SYMBYTES], uint8_t a_nonce)
{
    uint8_t l_buf[MLKEM_ETA2 * MLKEM_N / 4];
    dap_mlkem_prf(l_buf, sizeof(l_buf), a_seed, a_nonce);
    MLKEM_NAMESPACE(_cbd_eta2)(a_r, l_buf);
}

void MLKEM_NAMESPACE(_poly_getnoise_eta2_x4)(dap_mlkem_poly *a_r0, dap_mlkem_poly *a_r1,
                                               dap_mlkem_poly *a_r2, dap_mlkem_poly *a_r3,
                                               const uint8_t a_seed[MLKEM_SYMBYTES],
                                               uint8_t a_n0, uint8_t a_n1,
                                               uint8_t a_n2, uint8_t a_n3)
{
    enum { BUFLEN = MLKEM_ETA2 * MLKEM_N / 4 };
    uint8_t l_buf[4][BUFLEN];
    dap_mlkem_prf_x4(l_buf[0], l_buf[1], l_buf[2], l_buf[3],
                      BUFLEN, a_seed, a_n0, a_n1, a_n2, a_n3);
    MLKEM_NAMESPACE(_cbd_eta2)(a_r0, l_buf[0]);
    MLKEM_NAMESPACE(_cbd_eta2)(a_r1, l_buf[1]);
    MLKEM_NAMESPACE(_cbd_eta2)(a_r2, l_buf[2]);
    MLKEM_NAMESPACE(_cbd_eta2)(a_r3, l_buf[3]);
}

void MLKEM_NAMESPACE(_poly_ntt)(dap_mlkem_poly *a_r)
{
    MLKEM_NAMESPACE(_ntt)(a_r->coeffs);
}

void MLKEM_NAMESPACE(_poly_invntt_tomont)(dap_mlkem_poly *a_r)
{
    MLKEM_NAMESPACE(_invntt)(a_r->coeffs);
}

static const int16_t s_basemul_zetas_nttpack[128] = {
     2226, -2226,   430,  -430,   555,  -555,   843,  -843,
     2078, -2078,   871,  -871,  1550, -1550,   105,  -105,
      422,  -422,   587,  -587,   177,  -177,  3094, -3094,
     3038, -3038,  2869, -2869,  1574, -1574,  1653, -1653,
     3083, -3083,   778,  -778,  1159, -1159,  3182, -3182,
     2552, -2552,  1483, -1483,  2727, -2727,  1119, -1119,
     1739, -1739,   644,  -644,  2457, -2457,   349,  -349,
      418,  -418,   329,  -329,  3173, -3173,  3254, -3254,
      817,  -817,  1097, -1097,   603,  -603,   610,  -610,
     1322, -1322,  2044, -2044,  1864, -1864,   384,  -384,
     2114, -2114,  3193, -3193,  1218, -1218,  1994, -1994,
     2455, -2455,   220,  -220,  2142, -2142,  1670, -1670,
     2144, -2144,  1799, -1799,  2051, -2051,   794,  -794,
     1819, -1819,  2475, -2475,  2459, -2459,   478,  -478,
     3221, -3221,  3021, -3021,   996,  -996,   991,  -991,
      958,  -958,  1869, -1869,  1522, -1522,  1628, -1628,
};

void MLKEM_NAMESPACE(_poly_basemul_montgomery)(dap_mlkem_poly *a_r,
                                                const dap_mlkem_poly *a_a,
                                                const dap_mlkem_poly *a_b)
{
    if (MLKEM_SIMD_DISPATCH(basemul_montgomery, a_r->coeffs, a_a->coeffs, a_b->coeffs, NULL))
        return;
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        const int16_t *l_ae = a_a->coeffs + 32 * l_p;
        const int16_t *l_ao = a_a->coeffs + 32 * l_p + 16;
        const int16_t *l_be = a_b->coeffs + 32 * l_p;
        const int16_t *l_bo = a_b->coeffs + 32 * l_p + 16;
        int16_t *l_re = a_r->coeffs + 32 * l_p;
        int16_t *l_ro = a_r->coeffs + 32 * l_p + 16;
        const int16_t *l_z = s_basemul_zetas_nttpack + 16 * l_p;

        for (unsigned l_j = 0; l_j < 16; l_j++) {
            l_re[l_j] = dap_mlkem_fqmul(l_ae[l_j], l_be[l_j])
                       + dap_mlkem_fqmul(dap_mlkem_fqmul(l_ao[l_j], l_bo[l_j]), l_z[l_j]);
            l_ro[l_j] = dap_mlkem_fqmul(l_ae[l_j], l_bo[l_j])
                       + dap_mlkem_fqmul(l_ao[l_j], l_be[l_j]);
        }
    }
}

void MLKEM_NAMESPACE(_poly_tomont)(dap_mlkem_poly *a_r)
{
    if (MLKEM_SIMD_DISPATCH(tomont, a_r->coeffs))
        return;
    const int16_t f = (int16_t)((1ULL << 32) % MLKEM_Q);
    for (unsigned i = 0; i < MLKEM_N; i++)
        a_r->coeffs[i] = dap_mlkem_montgomery_reduce((int32_t)a_r->coeffs[i] * f);
}

void MLKEM_NAMESPACE(_poly_reduce)(dap_mlkem_poly *a_r)
{
    if (MLKEM_SIMD_DISPATCH(reduce, a_r->coeffs))
        return;
    for (unsigned i = 0; i < MLKEM_N; i++)
        a_r->coeffs[i] = dap_mlkem_barrett_reduce(a_r->coeffs[i]);
}

void MLKEM_NAMESPACE(_poly_csubq)(dap_mlkem_poly *a_r)
{
    if (MLKEM_SIMD_DISPATCH(csubq, a_r->coeffs))
        return;
    for (unsigned i = 0; i < MLKEM_N; i++)
        a_r->coeffs[i] = dap_mlkem_csubq(a_r->coeffs[i]);
}

MLKEM_HOTFN void MLKEM_NAMESPACE(_poly_add)(dap_mlkem_poly *a_r, const dap_mlkem_poly *a_a,
                                              const dap_mlkem_poly *a_b)
{
    if (MLKEM_SIMD_DISPATCH(add, a_r->coeffs, a_a->coeffs, a_b->coeffs))
        return;
    for (unsigned i = 0; i < MLKEM_N; i++)
        a_r->coeffs[i] = a_a->coeffs[i] + a_b->coeffs[i];
}

MLKEM_HOTFN void MLKEM_NAMESPACE(_poly_sub)(dap_mlkem_poly *a_r, const dap_mlkem_poly *a_a,
                                              const dap_mlkem_poly *a_b)
{
    if (MLKEM_SIMD_DISPATCH(sub, a_r->coeffs, a_a->coeffs, a_b->coeffs))
        return;
    for (unsigned i = 0; i < MLKEM_N; i++)
        a_r->coeffs[i] = a_a->coeffs[i] - a_b->coeffs[i];
}

#ifdef MLKEM_POLY_AVX2
__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_poly_mulcache_compute_avx2(int16_t * restrict a_cache,
                                          const int16_t * restrict a_b)
{
    const __m256i l_qinv = _mm256_set1_epi16((int16_t)MLKEM_QINV);
    const __m256i l_q    = _mm256_set1_epi16(MLKEM_Q);
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        __m256i l_be = _mm256_loadu_si256((const __m256i *)(a_b + 32 * l_p));
        __m256i l_bo = _mm256_loadu_si256((const __m256i *)(a_b + 32 * l_p + 16));
        __m256i l_z  = _mm256_load_si256((const __m256i *)(s_basemul_zetas_nttpack + 16 * l_p));
        __m256i lo = _mm256_mullo_epi16(l_bo, l_z);
        __m256i hi = _mm256_mulhi_epi16(l_bo, l_z);
        __m256i u  = _mm256_mullo_epi16(lo, l_qinv);
        __m256i uq = _mm256_mulhi_epi16(u, l_q);
        __m256i l_boz = _mm256_sub_epi16(hi, uq);
        _mm256_storeu_si256((__m256i *)(a_cache + 32 * l_p), l_be);
        _mm256_storeu_si256((__m256i *)(a_cache + 32 * l_p + 16), l_boz);
    }
}
#endif

void MLKEM_NAMESPACE(_poly_mulcache_compute)(dap_mlkem_poly_mulcache *a_cache,
                                              const dap_mlkem_poly *a_b)
{
#ifdef MLKEM_POLY_AVX2
    if (dap_mlkem_poly_simd_available()) {
        s_poly_mulcache_compute_avx2(a_cache->coeffs, a_b->coeffs);
        return;
    }
#endif
    const int16_t *l_z = s_basemul_zetas_nttpack;
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        const int16_t *l_be = a_b->coeffs + 32 * l_p;
        const int16_t *l_bo = a_b->coeffs + 32 * l_p + 16;
        int16_t *l_ce = a_cache->coeffs + 32 * l_p;
        int16_t *l_co = a_cache->coeffs + 32 * l_p + 16;
        for (unsigned l_j = 0; l_j < 16; l_j++) {
            l_ce[l_j] = l_be[l_j];
            l_co[l_j] = dap_mlkem_fqmul(l_bo[l_j], l_z[16 * l_p + l_j]);
        }
    }
}
