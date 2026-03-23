/**
 * @file dap_mlkem_polyvec.c
 * @brief Polynomial vector operations for ML-KEM (FIPS 203).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <string.h>
#include "dap_mlkem_polyvec.h"
#include "dap_mlkem_ntt.h"
#include "dap_mlkem_poly_simd.h"
#include "dap_cpu_arch.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define MLKEM_POLYVEC_AVX2 1
#endif

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#include "dap_mlkem_poly_simd.h"

#if MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 352)
__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_polyvec_compress_d11_avx2(uint8_t *a_r, const int16_t *a_coeffs)
{
    const __m256i v = _mm256_set1_epi16(20159);
    const __m256i v8 = _mm256_slli_epi16(v, 3);
    const __m256i off = _mm256_set1_epi16(36);
    const __m256i shift1 = _mm256_set1_epi16(1 << 13);
    const __m256i mask = _mm256_set1_epi16(2047);
    const __m256i shift2 = _mm256_set1_epi64x((2048LL << 48) + (1LL << 32) + (2048 << 16) + 1);
    const __m256i sllvdidx = _mm256_set1_epi64x(10);
    const __m256i srlvqidx = _mm256_set_epi64x(30, 10, 30, 10);
    const __m256i shufbidx = _mm256_set_epi8(
        4,3,2,1,0, 0,-1,-1,-1,-1, 10,9,8,7,6,5,
        -1,-1,-1,-1,-1, 10,9,8,7,6, 5,4,3,2,1,0);

    for (unsigned i = 0; i < MLKEM_N / 16; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 16 * i));
        __m256i f1 = _mm256_mullo_epi16(f0, v8);
        __m256i f2 = _mm256_add_epi16(f0, off);
        f0 = _mm256_slli_epi16(f0, 3);
        f0 = _mm256_mulhi_epi16(f0, v);
        f2 = _mm256_sub_epi16(f1, f2);
        f1 = _mm256_andnot_si256(f1, f2);
        f1 = _mm256_srli_epi16(f1, 15);
        f0 = _mm256_sub_epi16(f0, f1);
        f0 = _mm256_mulhrs_epi16(f0, shift1);
        f0 = _mm256_and_si256(f0, mask);
        f0 = _mm256_madd_epi16(f0, shift2);
        f0 = _mm256_sllv_epi32(f0, sllvdidx);
        f1 = _mm256_bsrli_epi128(f0, 8);
        f0 = _mm256_srlv_epi64(f0, srlvqidx);
        f1 = _mm256_slli_epi64(f1, 34);
        f0 = _mm256_add_epi64(f0, f1);
        f0 = _mm256_shuffle_epi8(f0, shufbidx);
        __m128i t0 = _mm256_castsi256_si128(f0);
        __m128i t1 = _mm256_extracti128_si256(f0, 1);
        t0 = _mm_blendv_epi8(t0, t1, _mm256_castsi256_si128(shufbidx));
        _mm_storeu_si128((__m128i *)(a_r + 22 * i), t0);
        if (i < MLKEM_N / 16 - 1)
            _mm_storel_epi64((__m128i *)(a_r + 22 * i + 16), t1);
        else
            memcpy(a_r + 22 * i + 16, &t1, 6);
    }
}
#elif MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 320)
__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_polyvec_compress_d10_avx2(uint8_t *a_r, const int16_t *a_coeffs)
{
    const __m256i v = _mm256_set1_epi16(20159);
    const __m256i v8 = _mm256_slli_epi16(v, 3);
    const __m256i off = _mm256_set1_epi16(15);
    const __m256i shift1 = _mm256_set1_epi16(1 << 12);
    const __m256i mask = _mm256_set1_epi16(1023);
    const __m256i shift2 = _mm256_set1_epi64x((1024LL << 48) + (1LL << 32) + (1024 << 16) + 1);
    const __m256i sllvdidx = _mm256_set1_epi64x(12);
    const __m256i shufbidx = _mm256_set_epi8(
        8, 4,3,2,1,0, -1,-1,-1,-1,-1,-1, 12,11,10,9,
        -1,-1,-1,-1,-1,-1, 12,11,10,9,8, 4,3,2,1,0);

    for (unsigned i = 0; i < MLKEM_N / 16; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 16 * i));
        __m256i f1 = _mm256_mullo_epi16(f0, v8);
        __m256i f2 = _mm256_add_epi16(f0, off);
        f0 = _mm256_slli_epi16(f0, 3);
        f0 = _mm256_mulhi_epi16(f0, v);
        f2 = _mm256_sub_epi16(f1, f2);
        f1 = _mm256_andnot_si256(f1, f2);
        f1 = _mm256_srli_epi16(f1, 15);
        f0 = _mm256_sub_epi16(f0, f1);
        f0 = _mm256_mulhrs_epi16(f0, shift1);
        f0 = _mm256_and_si256(f0, mask);
        f0 = _mm256_madd_epi16(f0, shift2);
        f0 = _mm256_sllv_epi32(f0, sllvdidx);
        f0 = _mm256_srli_epi64(f0, 12);
        f0 = _mm256_shuffle_epi8(f0, shufbidx);
        __m128i t0 = _mm256_castsi256_si128(f0);
        __m128i t1 = _mm256_extracti128_si256(f0, 1);
        t0 = _mm_blend_epi16(t0, t1, 0xE0);
        _mm_storeu_si128((__m128i *)(a_r + 20 * i), t0);
        memcpy(a_r + 20 * i + 16, &t1, 4);
    }
}
#endif

#if MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 352)
__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_polyvec_decompress_d11_avx2(int16_t *a_r, const uint8_t *a_a)
{
    const __m256i q = _mm256_set1_epi16(MLKEM_Q);
    const __m256i shufbidx = _mm256_set_epi8(
        13,12,12,11, 10,9,9,8, 8,7,6,5, 5,4,4,3,
        10,9,9,8, 7,6,6,5, 5,4,3,2, 2,1,1,0);
    const __m256i srlvdidx = _mm256_set_epi32(0, 0, 1, 0, 0, 0, 1, 0);
    const __m256i srlvqidx = _mm256_set_epi64x(2, 0, 2, 0);
    const __m256i shift = _mm256_set_epi16(
        4,32,1,8, 32,1,4,32, 4,32,1,8, 32,1,4,32);
    const __m256i mask = _mm256_set1_epi16(32752);

    for (unsigned i = 0; i < MLKEM_N / 16; i++) {
        __m256i f;
        if (i < MLKEM_N / 16 - 1)
            f = _mm256_loadu_si256((const __m256i *)(a_a + 22 * i));
        else
            memcpy(&f, a_a + 22 * i, 22);
        f = _mm256_permute4x64_epi64(f, 0x94);
        f = _mm256_shuffle_epi8(f, shufbidx);
        f = _mm256_srlv_epi32(f, srlvdidx);
        f = _mm256_srlv_epi64(f, srlvqidx);
        f = _mm256_mullo_epi16(f, shift);
        f = _mm256_srli_epi16(f, 1);
        f = _mm256_and_si256(f, mask);
        f = _mm256_mulhrs_epi16(f, q);
        _mm256_storeu_si256((__m256i *)(a_r + 16 * i), f);
    }
}
#elif MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 320)
__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_polyvec_decompress_d10_avx2(int16_t *a_r, const uint8_t *a_a)
{
    const __m256i q = _mm256_set1_epi32((MLKEM_Q << 16) + 4 * MLKEM_Q);
    const __m256i shufbidx = _mm256_set_epi8(
        11,10,10,9, 9,8,8,7, 6,5,5,4, 4,3,3,2,
        9,8,8,7, 7,6,6,5, 4,3,3,2, 2,1,1,0);
    const __m256i sllvdidx = _mm256_set1_epi64x(4);
    const __m256i mask = _mm256_set1_epi32((32736 << 16) + 8184);

    for (unsigned i = 0; i < MLKEM_N / 16; i++) {
        __m256i f;
        if (i < MLKEM_N / 16 - 1)
            f = _mm256_loadu_si256((const __m256i *)(a_a + 20 * i));
        else
            memcpy(&f, a_a + 20 * i, 20);
        f = _mm256_permute4x64_epi64(f, 0x94);
        f = _mm256_shuffle_epi8(f, shufbidx);
        f = _mm256_sllv_epi32(f, sllvdidx);
        f = _mm256_srli_epi16(f, 1);
        f = _mm256_and_si256(f, mask);
        f = _mm256_mulhrs_epi16(f, q);
        _mm256_storeu_si256((__m256i *)(a_r + 16 * i), f);
    }
}
#endif
#define MLKEM_POLYVEC_AVX2 1
#endif /* x86_64 */

void MLKEM_NAMESPACE(_polyvec_compress)(uint8_t *a_r, dap_mlkem_polyvec *a_a)
{
    MLKEM_NAMESPACE(_polyvec_csubq)(a_a);
#ifdef MLKEM_POLYVEC_AVX2
    if (dap_mlkem_poly_simd_available()) {
        for (unsigned i = 0; i < MLKEM_K; i++) {
#if MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 352)
            s_polyvec_compress_d11_avx2(a_r, a_a->vec[i].coeffs);
            a_r += 352;
#elif MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 320)
            s_polyvec_compress_d10_avx2(a_r, a_a->vec[i].coeffs);
            a_r += 320;
#endif
        }
        return;
    }
#endif
#if MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 352)
    for (unsigned i = 0; i < MLKEM_K; i++) {
        for (unsigned k = 0; k < MLKEM_N; k++)
            a_a->vec[i].coeffs[k] = (int16_t)((((uint32_t)a_a->vec[i].coeffs[k] << 11)
                + MLKEM_Q / 2) / MLKEM_Q) & 0x7ff;
        for (unsigned j = 0; j < MLKEM_N / 8; j++) {
            int16_t *t = a_a->vec[i].coeffs + 8 * j;
            a_r[ 0] = (uint8_t)(t[0]);
            a_r[ 1] = (uint8_t)((t[0] >> 8) | (t[1] << 3));
            a_r[ 2] = (uint8_t)((t[1] >> 5) | (t[2] << 6));
            a_r[ 3] = (uint8_t)(t[2] >> 2);
            a_r[ 4] = (uint8_t)((t[2] >> 10) | (t[3] << 1));
            a_r[ 5] = (uint8_t)((t[3] >> 7) | (t[4] << 4));
            a_r[ 6] = (uint8_t)((t[4] >> 4) | (t[5] << 7));
            a_r[ 7] = (uint8_t)(t[5] >> 1);
            a_r[ 8] = (uint8_t)((t[5] >> 9) | (t[6] << 2));
            a_r[ 9] = (uint8_t)((t[6] >> 6) | (t[7] << 5));
            a_r[10] = (uint8_t)(t[7] >> 3);
            a_r += 11;
        }
    }
#elif MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 320)
    for (unsigned i = 0; i < MLKEM_K; i++) {
        for (unsigned k = 0; k < MLKEM_N; k++)
            a_a->vec[i].coeffs[k] = (int16_t)((((uint32_t)a_a->vec[i].coeffs[k] << 10)
                + MLKEM_Q / 2) / MLKEM_Q) & 0x3ff;
        for (unsigned j = 0; j < MLKEM_N / 4; j++) {
            int16_t *t = a_a->vec[i].coeffs + 4 * j;
            a_r[0] = (uint8_t)(t[0]);
            a_r[1] = (uint8_t)((t[0] >> 8) | (t[1] << 2));
            a_r[2] = (uint8_t)((t[1] >> 6) | (t[2] << 4));
            a_r[3] = (uint8_t)((t[2] >> 4) | (t[3] << 6));
            a_r[4] = (uint8_t)(t[3] >> 2);
            a_r += 5;
        }
    }
#endif
}

MLKEM_HOTFN void MLKEM_NAMESPACE(_polyvec_decompress)(dap_mlkem_polyvec *a_r, const uint8_t *a_a)
{
#ifdef MLKEM_POLYVEC_AVX2
    if (dap_mlkem_poly_simd_available()) {
        for (unsigned i = 0; i < MLKEM_K; i++) {
#if MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 352)
            s_polyvec_decompress_d11_avx2(a_r->vec[i].coeffs, a_a);
            a_a += 352;
#elif MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 320)
            s_polyvec_decompress_d10_avx2(a_r->vec[i].coeffs, a_a);
            a_a += 320;
#endif
        }
        return;
    }
#endif
#if MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 352)
    uint16_t t[8];
    for (unsigned i = 0; i < MLKEM_K; i++) {
        for (unsigned j = 0; j < MLKEM_N / 8; j++) {
            t[0] = (a_a[0])       | ((uint16_t)a_a[1] << 8);
            t[1] = (a_a[1] >> 3)  | ((uint16_t)a_a[2] << 5);
            t[2] = (a_a[2] >> 6)  | ((uint16_t)a_a[3] << 2) | ((uint16_t)a_a[4] << 10);
            t[3] = (a_a[4] >> 1)  | ((uint16_t)a_a[5] << 7);
            t[4] = (a_a[5] >> 4)  | ((uint16_t)a_a[6] << 4);
            t[5] = (a_a[6] >> 7)  | ((uint16_t)a_a[7] << 1) | ((uint16_t)a_a[8] << 9);
            t[6] = (a_a[8] >> 2)  | ((uint16_t)a_a[9] << 6);
            t[7] = (a_a[9] >> 5)  | ((uint16_t)a_a[10] << 3);
            a_a += 11;
            for (unsigned k = 0; k < 8; k++)
                a_r->vec[i].coeffs[8 * j + k] = (int16_t)(((uint32_t)(t[k] & 0x7FF) * MLKEM_Q + 1024) >> 11);
        }
    }
#elif MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 320)
    uint16_t t[4];
    for (unsigned i = 0; i < MLKEM_K; i++) {
        for (unsigned j = 0; j < MLKEM_N / 4; j++) {
            t[0] = (a_a[0])       | ((uint16_t)a_a[1] << 8);
            t[1] = (a_a[1] >> 2)  | ((uint16_t)a_a[2] << 6);
            t[2] = (a_a[2] >> 4)  | ((uint16_t)a_a[3] << 4);
            t[3] = (a_a[3] >> 6)  | ((uint16_t)a_a[4] << 2);
            a_a += 5;
            for (unsigned k = 0; k < 4; k++)
                a_r->vec[i].coeffs[4 * j + k] = (int16_t)(((uint32_t)(t[k] & 0x3FF) * MLKEM_Q + 512) >> 10);
        }
    }
#endif
}

void MLKEM_NAMESPACE(_polyvec_tobytes)(uint8_t *a_r, dap_mlkem_polyvec *a_a)
{
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_poly_tobytes)(a_r + i * MLKEM_POLYBYTES, &a_a->vec[i]);
}

void MLKEM_NAMESPACE(_polyvec_frombytes)(dap_mlkem_polyvec *a_r, const uint8_t *a_a)
{
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_poly_frombytes)(&a_r->vec[i], a_a + i * MLKEM_POLYBYTES);
}

void MLKEM_NAMESPACE(_polyvec_ntt)(dap_mlkem_polyvec *a_r)
{
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_poly_ntt)(&a_r->vec[i]);
}

void MLKEM_NAMESPACE(_polyvec_invntt_tomont)(dap_mlkem_polyvec *a_r)
{
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_poly_invntt_tomont)(&a_r->vec[i]);
}

#ifdef MLKEM_POLYVEC_AVX2
__attribute__((target("avx2,avx512f,avx512vl,avx512bw")))
static void s_polyvec_basemul_acc_cached_avx2(
    int16_t * restrict a_r,
    const int16_t * const *a_polys_a,
    const int16_t * const *a_polys_b,
    const int16_t * const *a_caches,
    unsigned a_count)
{
    const __m256i l_qinv16 = _mm256_set1_epi16((int16_t)MLKEM_QINV);
    const __m256i l_q32    = _mm256_set1_epi32(MLKEM_Q);

    for (unsigned l_p = 0; l_p < 8; l_p++) {
        __m256i l_diag_lo  = _mm256_setzero_si256();
        __m256i l_diag_hi  = _mm256_setzero_si256();
        __m256i l_cross_lo = _mm256_setzero_si256();
        __m256i l_cross_hi = _mm256_setzero_si256();

        for (unsigned k = 0; k < a_count; k++) {
            __m256i l_ae = _mm256_loadu_si256((const __m256i *)(a_polys_a[k] + 32 * l_p));
            __m256i l_ao = _mm256_loadu_si256((const __m256i *)(a_polys_a[k] + 32 * l_p + 16));
            __m256i l_ce = _mm256_loadu_si256((const __m256i *)(a_caches[k] + 32 * l_p));
            __m256i l_co = _mm256_loadu_si256((const __m256i *)(a_caches[k] + 32 * l_p + 16));
            __m256i l_bo = _mm256_loadu_si256((const __m256i *)(a_polys_b[k] + 32 * l_p + 16));

            __m256i l_a_lo  = _mm256_unpacklo_epi16(l_ae, l_ao);
            __m256i l_a_hi  = _mm256_unpackhi_epi16(l_ae, l_ao);
            __m256i l_cd_lo = _mm256_unpacklo_epi16(l_ce, l_co);
            __m256i l_cd_hi = _mm256_unpackhi_epi16(l_ce, l_co);
            __m256i l_bc_lo = _mm256_unpacklo_epi16(l_bo, l_ce);
            __m256i l_bc_hi = _mm256_unpackhi_epi16(l_bo, l_ce);

            l_diag_lo  = _mm256_add_epi32(l_diag_lo,  _mm256_madd_epi16(l_a_lo, l_cd_lo));
            l_diag_hi  = _mm256_add_epi32(l_diag_hi,  _mm256_madd_epi16(l_a_hi, l_cd_hi));
            l_cross_lo = _mm256_add_epi32(l_cross_lo, _mm256_madd_epi16(l_a_lo, l_bc_lo));
            l_cross_hi = _mm256_add_epi32(l_cross_hi, _mm256_madd_epi16(l_a_hi, l_bc_hi));
        }

        __m256i ud_lo  = _mm256_mullo_epi16(l_diag_lo, l_qinv16);
        __m256i tqd_lo = _mm256_madd_epi16(ud_lo, l_q32);
        l_diag_lo = _mm256_srai_epi32(_mm256_sub_epi32(l_diag_lo, tqd_lo), 16);

        __m256i ud_hi  = _mm256_mullo_epi16(l_diag_hi, l_qinv16);
        __m256i tqd_hi = _mm256_madd_epi16(ud_hi, l_q32);
        l_diag_hi = _mm256_srai_epi32(_mm256_sub_epi32(l_diag_hi, tqd_hi), 16);

        __m256i uc_lo  = _mm256_mullo_epi16(l_cross_lo, l_qinv16);
        __m256i tqc_lo = _mm256_madd_epi16(uc_lo, l_q32);
        l_cross_lo = _mm256_srai_epi32(_mm256_sub_epi32(l_cross_lo, tqc_lo), 16);

        __m256i uc_hi  = _mm256_mullo_epi16(l_cross_hi, l_qinv16);
        __m256i tqc_hi = _mm256_madd_epi16(uc_hi, l_q32);
        l_cross_hi = _mm256_srai_epi32(_mm256_sub_epi32(l_cross_hi, tqc_hi), 16);

        __m256i l_re = _mm256_packs_epi32(l_diag_lo, l_diag_hi);
        __m256i l_ro = _mm256_packs_epi32(l_cross_lo, l_cross_hi);
        _mm256_storeu_si256((__m256i *)(a_r + 32 * l_p), l_re);
        _mm256_storeu_si256((__m256i *)(a_r + 32 * l_p + 16), l_ro);
    }
}
#endif

MLKEM_HOTFN void MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(
    dap_mlkem_poly *a_r,
    const dap_mlkem_polyvec *a_a,
    const dap_mlkem_polyvec *a_b,
    const dap_mlkem_polyvec_mulcache *a_b_cache)
{
#ifdef MLKEM_POLYVEC_AVX2
    if (dap_mlkem_poly_simd_available()) {
        const int16_t *l_pa[MLKEM_K], *l_pb[MLKEM_K], *l_pc[MLKEM_K];
        for (unsigned i = 0; i < MLKEM_K; i++) {
            l_pa[i] = a_a->vec[i].coeffs;
            l_pb[i] = a_b->vec[i].coeffs;
            l_pc[i] = a_b_cache->vec[i].coeffs;
        }
        s_polyvec_basemul_acc_cached_avx2(a_r->coeffs, l_pa, l_pb, l_pc, MLKEM_K);
        return;
    }
#endif
    dap_mlkem_poly l_t;
    MLKEM_NAMESPACE(_poly_basemul_montgomery)(a_r, &a_a->vec[0], &a_b->vec[0]);
    for (unsigned i = 1; i < MLKEM_K; i++) {
        MLKEM_NAMESPACE(_poly_basemul_montgomery)(&l_t, &a_a->vec[i], &a_b->vec[i]);
        MLKEM_NAMESPACE(_poly_add)(a_r, a_r, &l_t);
    }
    MLKEM_NAMESPACE(_poly_reduce)(a_r);
}

MLKEM_HOTFN void MLKEM_NAMESPACE(_polyvec_pointwise_acc_montgomery)(dap_mlkem_poly *a_r,
                                                         const dap_mlkem_polyvec *a_a,
                                                         const dap_mlkem_polyvec *a_b)
{
    dap_mlkem_polyvec_mulcache l_cache;
    MLKEM_NAMESPACE(_polyvec_mulcache_compute)(&l_cache, a_b);
    MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(a_r, a_a, a_b, &l_cache);
}

void MLKEM_NAMESPACE(_polyvec_reduce)(dap_mlkem_polyvec *a_r)
{
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_poly_reduce)(&a_r->vec[i]);
}

void MLKEM_NAMESPACE(_polyvec_csubq)(dap_mlkem_polyvec *a_r)
{
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_poly_csubq)(&a_r->vec[i]);
}

void MLKEM_NAMESPACE(_polyvec_add)(dap_mlkem_polyvec *a_r,
                                    const dap_mlkem_polyvec *a_a,
                                    const dap_mlkem_polyvec *a_b)
{
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_poly_add)(&a_r->vec[i], &a_a->vec[i], &a_b->vec[i]);
}

void MLKEM_NAMESPACE(_polyvec_mulcache_compute)(dap_mlkem_polyvec_mulcache *a_cache,
                                                 const dap_mlkem_polyvec *a_b)
{
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_poly_mulcache_compute)(&a_cache->vec[i], &a_b->vec[i]);
}

void MLKEM_NAMESPACE(_polyvec_nttpack)(dap_mlkem_polyvec *a_r)
{
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_nttpack)(a_r->vec[i].coeffs);
}

void MLKEM_NAMESPACE(_polyvec_nttunpack)(dap_mlkem_polyvec *a_r)
{
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_nttunpack)(a_r->vec[i].coeffs);
}
