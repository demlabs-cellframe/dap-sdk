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
#if MLKEM_POLYCOMPRESSEDBYTES == 128
__attribute__((target("avx2")))
static void s_poly_compress_d4_avx2(uint8_t *a_r, const int16_t *a_coeffs)
{
    const __m256i v_mul = _mm256_set1_epi16(0x1001);
    for (int i = 0; i < 8; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)&a_coeffs[32 * i]);
        __m256i f1 = _mm256_loadu_si256((const __m256i *)&a_coeffs[32 * i + 16]);
        __m256i packed = _mm256_packus_epi16(f0, f1);
        packed = _mm256_permute4x64_epi64(packed, 0xD8);
        __m256i merged = _mm256_maddubs_epi16(packed, v_mul);
        merged = _mm256_packus_epi16(merged, _mm256_setzero_si256());
        merged = _mm256_permute4x64_epi64(merged, 0xD8);
        _mm_storeu_si128((__m128i *)(a_r + 16 * i), _mm256_castsi256_si128(merged));
    }
}
#elif MLKEM_POLYCOMPRESSEDBYTES == 160
__attribute__((target("avx2")))
static void s_poly_compress_d5_avx2(uint8_t *a_r, const int16_t *a_coeffs)
{
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        int16_t *c = (int16_t *)a_coeffs + 8 * i;
        a_r[0] = (uint8_t)((c[0]) | (c[1] << 5));
        a_r[1] = (uint8_t)((c[1] >> 3) | (c[2] << 2) | (c[3] << 7));
        a_r[2] = (uint8_t)((c[3] >> 1) | (c[4] << 4));
        a_r[3] = (uint8_t)((c[4] >> 4) | (c[5] << 1) | (c[6] << 6));
        a_r[4] = (uint8_t)((c[6] >> 2) | (c[7] << 3));
        a_r += 5;
    }
}
#endif
#endif /* MLKEM_POLY_AVX2 */

void MLKEM_NAMESPACE(_poly_compress)(uint8_t *a_r, dap_mlkem_poly *a_a)
{
    MLKEM_NAMESPACE(_poly_csubq)(a_a);
#if MLKEM_POLYCOMPRESSEDBYTES == 128
    if (!MLKEM_SIMD_DISPATCH(compress_coeffs, a_a->coeffs, 157, 15)) {
        for (unsigned k = 0; k < MLKEM_N; k++)
            a_a->coeffs[k] = (int16_t)((((uint16_t)a_a->coeffs[k] << 4) + MLKEM_Q / 2) / MLKEM_Q) & 15;
    }
#ifdef MLKEM_POLY_AVX2
    s_poly_compress_d4_avx2(a_r, a_a->coeffs);
#else
    for (unsigned i = 0; i < MLKEM_N / 2; i++)
        a_r[i] = (uint8_t)(a_a->coeffs[2 * i] | (a_a->coeffs[2 * i + 1] << 4));
#endif
#elif MLKEM_POLYCOMPRESSEDBYTES == 160
    if (!MLKEM_SIMD_DISPATCH(compress_coeffs, a_a->coeffs, 315, 31)) {
        for (unsigned k = 0; k < MLKEM_N; k++)
            a_a->coeffs[k] = (int16_t)((((uint32_t)a_a->coeffs[k] << 5) + MLKEM_Q / 2) / MLKEM_Q) & 31;
    }
#ifdef MLKEM_POLY_AVX2
    s_poly_compress_d5_avx2(a_r, a_a->coeffs);
#else
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
#endif
}

#ifdef MLKEM_POLY_AVX2
#if MLKEM_POLYCOMPRESSEDBYTES == 128
__attribute__((target("avx2")))
static void s_poly_decompress_d4_avx2(int16_t *a_r, const uint8_t *a_a)
{
    const __m256i v_q  = _mm256_set1_epi16(MLKEM_Q);
    const __m256i v_8  = _mm256_set1_epi16(8);
    const __m256i v_nib = _mm256_set1_epi8(0x0F);

    for (int i = 0; i < 8; i++) {
        __m128i raw = _mm_loadu_si128((const __m128i *)(a_a + 16 * i));
        __m128i lo_nib = _mm_and_si128(raw, _mm_set1_epi8(0x0F));
        __m128i hi_nib = _mm_and_si128(_mm_srli_epi16(raw, 4), _mm_set1_epi8(0x0F));
        __m128i interleaved_lo = _mm_unpacklo_epi8(lo_nib, hi_nib);
        __m128i interleaved_hi = _mm_unpackhi_epi8(lo_nib, hi_nib);
        __m256i w0 = _mm256_cvtepu8_epi16(interleaved_lo);
        __m256i w1 = _mm256_cvtepu8_epi16(interleaved_hi);
        w0 = _mm256_srli_epi16(_mm256_add_epi16(_mm256_mullo_epi16(w0, v_q), v_8), 4);
        w1 = _mm256_srli_epi16(_mm256_add_epi16(_mm256_mullo_epi16(w1, v_q), v_8), 4);
        _mm256_storeu_si256((__m256i *)&a_r[32 * i],      w0);
        _mm256_storeu_si256((__m256i *)&a_r[32 * i + 16], w1);
    }
}
#elif MLKEM_POLYCOMPRESSEDBYTES == 160
__attribute__((target("avx2")))
static void s_poly_decompress_d5_avx2(int16_t *a_r, const uint8_t *a_a)
{
    const __m256i v_q  = _mm256_set1_epi16(MLKEM_Q);
    const __m256i v_16 = _mm256_set1_epi16(16);
    const __m256i v_mask5 = _mm256_set1_epi16(31);

    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        const uint8_t *p = a_a + 5 * i;
        int16_t t[8];
        t[0] = p[0] & 31;
        t[1] = ((p[0] >> 5) | (p[1] << 3)) & 31;
        t[2] = (p[1] >> 2) & 31;
        t[3] = ((p[1] >> 7) | (p[2] << 1)) & 31;
        t[4] = ((p[2] >> 4) | (p[3] << 4)) & 31;
        t[5] = (p[3] >> 1) & 31;
        t[6] = ((p[3] >> 6) | (p[4] << 2)) & 31;
        t[7] = (p[4] >> 3) & 31;
        for (unsigned j = 0; j < 8; j++)
            a_r[8 * i + j] = (int16_t)(((uint32_t)t[j] * MLKEM_Q + 16) >> 5);
    }
}
#endif
#endif /* MLKEM_POLY_AVX2 */

void MLKEM_NAMESPACE(_poly_decompress)(dap_mlkem_poly *a_r, const uint8_t *a_a)
{
#if MLKEM_POLYCOMPRESSEDBYTES == 128
#ifdef MLKEM_POLY_AVX2
    s_poly_decompress_d4_avx2(a_r->coeffs, a_a);
#else
    for (unsigned i = 0; i < MLKEM_N / 2; i++) {
        a_r->coeffs[2 * i]     = (int16_t)(((uint16_t)(a_a[0] & 15) * MLKEM_Q + 8) >> 4);
        a_r->coeffs[2 * i + 1] = (int16_t)(((uint16_t)(a_a[0] >> 4) * MLKEM_Q + 8) >> 4);
        a_a++;
    }
#endif
#elif MLKEM_POLYCOMPRESSEDBYTES == 160
#ifdef MLKEM_POLY_AVX2
    s_poly_decompress_d5_avx2(a_r->coeffs, a_a);
#else
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
#endif
}

#ifdef MLKEM_POLY_AVX2
__attribute__((target("avx2")))
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
__attribute__((target("avx2")))
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
__attribute__((target("avx2")))
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

__attribute__((target("avx2")))
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

void MLKEM_NAMESPACE(_poly_basemul_montgomery)(dap_mlkem_poly *a_r,
                                                const dap_mlkem_poly *a_a,
                                                const dap_mlkem_poly *a_b)
{
    const int16_t *l_z = MLKEM_NAMESPACE(_get_zetas)();
#if defined(__x86_64__) || defined(_M_X64)
    {
        extern void dap_mlkem_basemul_asm(int16_t *, const int16_t *, const int16_t *, const int16_t *);
        if (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX512) {
            dap_mlkem_basemul_asm(a_r->coeffs, a_a->coeffs, a_b->coeffs, l_z + 64);
            return;
        }
    }
#endif
    if (MLKEM_SIMD_DISPATCH(basemul_montgomery, a_r->coeffs, a_a->coeffs, a_b->coeffs, l_z + 64))
        return;
    for (unsigned i = 0; i < MLKEM_N / 4; i++) {
        int16_t zeta = l_z[64 + i];

        a_r->coeffs[4 * i]     = dap_mlkem_fqmul(a_a->coeffs[4 * i + 1], a_b->coeffs[4 * i + 1]);
        a_r->coeffs[4 * i]     = dap_mlkem_fqmul(a_r->coeffs[4 * i], zeta);
        a_r->coeffs[4 * i]    += dap_mlkem_fqmul(a_a->coeffs[4 * i], a_b->coeffs[4 * i]);
        a_r->coeffs[4 * i + 1] = dap_mlkem_fqmul(a_a->coeffs[4 * i], a_b->coeffs[4 * i + 1]);
        a_r->coeffs[4 * i + 1]+= dap_mlkem_fqmul(a_a->coeffs[4 * i + 1], a_b->coeffs[4 * i]);

        a_r->coeffs[4 * i + 2] = dap_mlkem_fqmul(a_a->coeffs[4 * i + 3], a_b->coeffs[4 * i + 3]);
        a_r->coeffs[4 * i + 2] = dap_mlkem_fqmul(a_r->coeffs[4 * i + 2], (int16_t)-zeta);
        a_r->coeffs[4 * i + 2]+= dap_mlkem_fqmul(a_a->coeffs[4 * i + 2], a_b->coeffs[4 * i + 2]);
        a_r->coeffs[4 * i + 3] = dap_mlkem_fqmul(a_a->coeffs[4 * i + 2], a_b->coeffs[4 * i + 3]);
        a_r->coeffs[4 * i + 3]+= dap_mlkem_fqmul(a_a->coeffs[4 * i + 3], a_b->coeffs[4 * i + 2]);
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
